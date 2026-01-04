#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "riscv.h"
#include "user/user.h"

static int quiet = 0;
static int full = 0;

static void
fail(const char *msg)
{
  if (!quiet)
    printf("swaptest: %s\n", msg);
  exit(1);
}

int
main(int argc, char *argv[])
{
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-q") == 0)
      quiet = 1;
    else if (strcmp(argv[i], "-f") == 0)
      full = 1;
  }

  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 target;
  if (full) {
    target = info.freemem + swap_bytes / 2;
  } else {
    target = 16 * 1024 * 1024;
    if (target > info.freemem / 2)
      target = info.freemem / 2;
    if (target < 8 * PGSIZE)
      target = 8 * PGSIZE;
  }
  if (target < 8 * PGSIZE)
    target = 8 * PGSIZE;
  if (!quiet)
    printf("swaptest: mode=%s target=%lu\n", full ? "full" : "light", target);

  uint64 amt = PGROUNDUP(target);
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("sbrk grow failed");

  uint64 pages = amt / PGSIZE;
  uint64 step = pages / 50;
  if (step == 0)
    step = 1;
  if (!quiet)
    printf("swaptest: write phase%s\n", full ? " (full)" : "");
  int start = uptime();
  int last = start;
  for (uint64 i = 0; i < pages; i++) {
    base[i * PGSIZE] = (char)((i ^ 0x5a) & 0xff);
    if (!quiet && ((i + 1) % step == 0 || uptime() - last > 50)) {
      last = uptime();
      printf("swaptest: write %lu/%lu\n", i + 1, pages);
    }
  }

  if (!quiet)
    printf("swaptest: read phase\n");
  last = uptime();
  for (uint64 i = pages; i > 0; i--) {
    uint64 idx = i - 1;
    char v = base[idx * PGSIZE];
    if (v != (char)((idx ^ 0x5a) & 0xff))
      fail("pattern mismatch");
    if (!quiet && ((pages - i + 1) % step == 0 || uptime() - last > 50)) {
      last = uptime();
      printf("swaptest: read %lu/%lu\n", pages - i + 1, pages);
    }
  }

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("sbrk shrink failed");

  if (!quiet)
    printf("swaptest: ok\n");
  exit(0);
}
