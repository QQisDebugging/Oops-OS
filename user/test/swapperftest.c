#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "riscv.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("swapperftest: %s\n", msg);
  exit(1);
}

int
main(void)
{
  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 target = info.freemem + (swap_bytes * 3) / 4;
  if (target < 32 * PGSIZE)
    target = 32 * PGSIZE;

  uint64 amt = PGROUNDUP(target);
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("sbrk grow failed");

  uint64 pages = amt / PGSIZE;
  for (uint64 i = 0; i < pages; i++) {
    base[i * PGSIZE] = (char)((i ^ 0x5a) & 0xff);
  }

  int t0 = uptime();
  for (int round = 0; round < 3; round++) {
    for (uint64 i = 0; i < pages; i++) {
      base[i * PGSIZE] ^= (char)(round + 1);
    }
  }
  int t1 = uptime();

  for (uint64 i = 0; i < pages; i++) {
    char v = base[i * PGSIZE];
    char expect = (char)((i ^ 0x5a) & 0xff);
    if (v != expect)
      fail("pattern mismatch");
  }

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("sbrk shrink failed");

  printf("swapperftest: %d ticks for %d pages\n", t1 - t0, (int)pages);
  printf("swapperftest: ok\n");
  exit(0);
}
