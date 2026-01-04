#include "types.h"
#include "riscv.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "defs.h"

#define SPOOL_BUF 4096

struct {
  struct spinlock lock;
  char buf[SPOOL_BUF];
  uint r;
  uint w;
} spool;

static int
spoolread(int user_dst, uint64 dst, int n)
{
  uint target = n;
  acquire(&spool.lock);
  while (n > 0) {
    while (spool.r == spool.w) {
      if (myproc()->killed) {
        release(&spool.lock);
        return -1;
      }
      if (n < target) {
        release(&spool.lock);
        return target - n;
      }
      sleep(&spool.r, &spool.lock);
    }
    char c = spool.buf[spool.r++ % SPOOL_BUF];
    if (either_copyout(user_dst, dst, &c, 1) == -1)
      break;
    dst++;
    n--;
  }
  wakeup(&spool.w);
  release(&spool.lock);
  return target - n;
}

static int
spoolwrite(int user_src, uint64 src, int n)
{
  int i;
  acquire(&spool.lock);
  for (i = 0; i < n; i++) {
    while (((spool.w + 1) % SPOOL_BUF) == spool.r) {
      if (myproc()->killed) {
        release(&spool.lock);
        return -1;
      }
      sleep(&spool.w, &spool.lock);
    }
    char c;
    if (either_copyin(&c, user_src, src + i, 1) == -1)
      break;
    spool.buf[spool.w++ % SPOOL_BUF] = c;
    wakeup(&spool.r);
  }
  release(&spool.lock);
  return i;
}

void
spoolinit(void)
{
  initlock(&spool.lock, "spool");
  devsw[SPOOL].read = spoolread;
  devsw[SPOOL].write = spoolwrite;
}
