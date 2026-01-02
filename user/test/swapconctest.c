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
worker(uint64 amt, int rounds, int tag)
{
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("sbrk grow failed");

  uint64 pages = amt / PGSIZE;
  for (int r = 0; r < rounds; r++) {
    for (uint64 i = 0; i < pages; i++) {
      base[i * PGSIZE] = (char)((i + r + tag) & 0xff);
    }
  }

  uint64 expect_off = (uint64)(rounds - 1 + tag);
  for (uint64 i = 0; i < pages; i++) {
    char v = base[i * PGSIZE];
    if (v != (char)((i + expect_off) & 0xff))
      fail("pattern mismatch");
  }

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("sbrk shrink failed");
  exit(0);
}

int
main(int argc, char *argv[])
{
  int nprocs = 4;
  int rounds = 2;

  if (argc > 1)
    nprocs = atoi(argv[1]);
  if (argc > 2)
    rounds = atoi(argv[2]);

  if (nprocs < 1)
    nprocs = 1;
  if (nprocs > 8)
    nprocs = 8;
  if (rounds < 1)
    rounds = 1;

  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 total = info.freemem + (swap_bytes * 3) / 4;
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
      worker(amt, rounds, i);
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
