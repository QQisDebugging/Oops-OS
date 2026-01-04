#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "fcntl.h"
#include "riscv.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("mmapswaptest: %s\n", msg);
  exit(1);
}

int
main(void)
{
  int pages = 12;
  int len = pages * PGSIZE;
  int fd = open("mmapswap", O_CREATE | O_RDWR);
  if (fd < 0)
    fail("open failed");

  char *zero = sbrk(PGSIZE);
  if (zero == (char *)-1)
    fail("sbrk zero failed");
  memset(zero, 0, PGSIZE);
  for (int i = 0; i < pages; i++) {
    if (write(fd, zero, PGSIZE) != PGSIZE)
      fail("file write failed");
  }

  char *addr = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == (char *)-1)
    fail("mmap failed");
  close(fd);

  for (int i = 0; i < pages; i++) {
    addr[i * PGSIZE] = (char)((i ^ 0x5a) & 0xff);
    addr[i * PGSIZE + 13] = (char)((i ^ 0x33) & 0xff);
  }

  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 target = info.freemem + swap_bytes / 4;
  if (target < 16 * PGSIZE)
    target = 16 * PGSIZE;

  uint64 amt = PGROUNDUP(target);
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("pressure sbrk failed");

  uint64 npages = amt / PGSIZE;
  for (uint64 i = 0; i < npages; i++) {
    base[i * PGSIZE] = (char)i;
  }

  for (int i = 0; i < pages; i++) {
    char v0 = addr[i * PGSIZE];
    char v1 = addr[i * PGSIZE + 13];
    if (v0 != (char)((i ^ 0x5a) & 0xff) ||
        v1 != (char)((i ^ 0x33) & 0xff))
      fail("pattern mismatch");
  }

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("pressure shrink failed");

  if (munmap(addr, len) < 0)
    fail("munmap failed");

  printf("mmapswaptest: ok\n");
  exit(0);
}
