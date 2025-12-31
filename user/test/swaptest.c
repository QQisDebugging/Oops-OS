#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "riscv.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("swaptest: %s\n", msg);
  exit(1);
}

int
main(void)
{
  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 target = info.freemem + swap_bytes / 2;
  if (target < 8 * PGSIZE)
    target = 8 * PGSIZE;

  uint64 amt = PGROUNDUP(target);
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("sbrk grow failed");

  uint64 pages = amt / PGSIZE;
  for (uint64 i = 0; i < pages; i++) {
    base[i * PGSIZE] = (char)((i ^ 0x5a) & 0xff);
  }

  for (uint64 i = pages; i > 0; i--) {
    uint64 idx = i - 1;
    char v = base[idx * PGSIZE];
    if (v != (char)((idx ^ 0x5a) & 0xff))
      fail("pattern mismatch");
  }

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("sbrk shrink failed");

  printf("swaptest: ok\n");
  exit(0);
}
