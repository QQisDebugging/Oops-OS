#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "riscv.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("swapthrashtest: %s\n", msg);
  exit(1);
}

static void
worker(uint64 amt, int rounds, int max_ticks, int tag)
{
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("sbrk grow failed");

  uint64 pages = amt / PGSIZE;
  int start = uptime();
  for (int r = 0; r < rounds; r++) {
    if ((r & 1) == 0)
      printf("swapthrashtest: pid %d round %d/%d\n", getpid(), r + 1, rounds);
    for (uint64 i = 0; i < pages; i++) {
      base[i * PGSIZE] = (char)((i + r + tag) & 0xff);
    }
    for (uint64 i = 0; i < pages; i++) {
      char v = base[i * PGSIZE];
      if (v != (char)((i + r + tag) & 0xff))
        fail("pattern mismatch");
    }
    if (uptime() - start > max_ticks)
      fail("timeout");
  }

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("sbrk shrink failed");
  exit(0);
}

int
main(int argc, char *argv[])
{
  int nprocs = 3;
  int rounds = 2;
  int max_ticks = 5000;

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
  if (max_ticks < 100)
    max_ticks = 100;

  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 extra = swap_bytes / 8;
  uint64 max_extra = 16 * 1024 * 1024;
  if (extra > max_extra)
    extra = max_extra;
  uint64 total = info.freemem + extra;
  uint64 min_total = (uint64)nprocs * 8 * PGSIZE;
  if (total < min_total)
    total = min_total;

  uint64 amt = PGROUNDUP(total / nprocs);

  printf("swapthrashtest: start procs=%d rounds=%d pages=%d max_ticks=%d\n",
         nprocs, rounds, (int)(amt / PGSIZE), max_ticks);
  int start = uptime();
  for (int i = 0; i < nprocs; i++) {
    int pid = fork();
    if (pid < 0)
      fail("fork failed");
    if (pid == 0)
      worker(amt, rounds, max_ticks, i);
  }

  for (int i = 0; i < nprocs; i++) {
    if (wait(0) < 0)
      fail("wait failed");
  }
  int end = uptime();

  printf("swapthrashtest: %d procs, %d ticks\n", nprocs, end - start);
  printf("swapthrashtest: ok\n");
  exit(0);
}
