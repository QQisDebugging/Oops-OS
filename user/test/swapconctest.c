#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "riscv.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("swapconctest: %s\n", msg);
  exit(1);
}

static void
worker(uint64 amt, int rounds, int tag, int max_ticks)
{
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("sbrk grow failed");

  uint64 pages = amt / PGSIZE;
  uint64 step = pages / 4;
  if (step < 512)
    step = 512;
  int start = uptime();
  for (int r = 0; r < rounds; r++) {
    if (tag == 0 && (r & 1) == 0) {
      printf("swapconctest: pid %d round %d/%d\n", getpid(), r + 1, rounds);
      printf("swapconctest: write phase\n");
    }
    for (uint64 i = 0; i < pages; i++) {
      base[i * PGSIZE] = (char)((i + r + tag) & 0xff);
      if (tag == 0 && i != 0 && (i % step) == 0)
        printf("swapconctest: progress %d/%d\n", (int)i, (int)pages);
      if ((i & 255) == 0 && uptime() - start > max_ticks)
        fail("timeout");
    }
  }

  uint64 expect_off = (uint64)(rounds - 1 + tag);
  if (tag == 0)
    printf("swapconctest: verify phase\n");
  for (uint64 i = 0; i < pages; i++) {
    char v = base[i * PGSIZE];
    if (v != (char)((i + expect_off) & 0xff))
      fail("pattern mismatch");
    if (tag == 0 && i != 0 && (i % step) == 0)
      printf("swapconctest: verify %d/%d\n", (int)i, (int)pages);
    if ((i & 255) == 0 && uptime() - start > max_ticks)
      fail("timeout");
  }

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("sbrk shrink failed");
  exit(0);
}

int
main(int argc, char *argv[])
{
  int nprocs = 2;
  int rounds = 1;
  int max_ticks = 3000;

  if (argc > 1)
    nprocs = atoi(argv[1]);
  if (argc > 2)
    rounds = atoi(argv[2]);
  if (argc > 3)
    max_ticks = atoi(argv[3]);

  if (nprocs < 1)
    nprocs = 1;
  if (nprocs > 8)
    nprocs = 8;
  if (rounds < 1)
    rounds = 1;
  if (max_ticks < 200)
    max_ticks = 200;

  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 extra = swap_bytes / 16;
  uint64 max_extra = 8 * 1024 * 1024;
  if (extra > max_extra)
    extra = max_extra;
  uint64 total = info.freemem + extra;
  uint64 min_total = (uint64)nprocs * 8 * PGSIZE;
  if (total < min_total)
    total = min_total;

  uint64 amt = PGROUNDUP(total / nprocs);

  int start = uptime();
  for (int i = 0; i < nprocs; i++) {
    int pid = fork();
    if (pid < 0)
      fail("fork failed");
    if (pid == 0)
      worker(amt, rounds, i, max_ticks);
  }

  for (int i = 0; i < nprocs; i++) {
    if (wait(0) < 0)
      fail("wait failed");
  }
  int end = uptime();

  printf("swapconctest: %d procs, %d ticks\n", nprocs, end - start);
  printf("swapconctest: ok\n");
  exit(0);
}
