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
    printf("swapconctest: %s\n", msg);
  exit(1);
}

static void
worker(uint64 amt, int rounds, int tag, int max_ticks, int verbose)
{
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("sbrk grow failed");

  uint64 pages = amt / PGSIZE;
  uint64 step = pages / 4;
  if (step < 512)
    step = 512;
  int start = uptime();
  int do_print = !quiet && (verbose || tag == 0);
  for (int r = 0; r < rounds; r++) {
    if (do_print && (r & 1) == 0) {
      printf("swapconctest: pid %d round %d/%d\n", getpid(), r + 1, rounds);
      printf("swapconctest: write phase\n");
    }
    for (uint64 i = 0; i < pages; i++) {
      base[i * PGSIZE] = (char)((i + r + tag) & 0xff);
      if (do_print && i != 0 && (i % step) == 0)
        printf("swapconctest: progress %d/%d\n", (int)i, (int)pages);
      if ((i & 255) == 0 && uptime() - start > max_ticks)
        fail("timeout");
    }
  }

  uint64 expect_off = (uint64)(rounds - 1 + tag);
  if (do_print)
    printf("swapconctest: verify phase\n");
  for (uint64 i = 0; i < pages; i++) {
    char v = base[i * PGSIZE];
    if (v != (char)((i + expect_off) & 0xff))
      fail("pattern mismatch");
    if (do_print && i != 0 && (i % step) == 0)
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
  int verbose = 0;
  int nidx = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-q") == 0) {
      quiet = 1;
      continue;
    }
    if (strcmp(argv[i], "-f") == 0) {
      full = 1;
      continue;
    }
    if (nidx == 0)
      nprocs = atoi(argv[i]);
    else if (nidx == 1)
      rounds = atoi(argv[i]);
    else if (nidx == 2)
      max_ticks = atoi(argv[i]);
    else if (nidx == 3)
      verbose = atoi(argv[i]);
    nidx++;
  }

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
  uint64 total;
  if (full) {
    uint64 extra = swap_bytes / 16;
    uint64 max_extra = 8 * 1024 * 1024;
    if (extra > max_extra)
      extra = max_extra;
    total = info.freemem + extra;
  } else {
    total = 16 * 1024 * 1024;
    if (total > info.freemem / 2)
      total = info.freemem / 2;
  }
  uint64 min_total = (uint64)nprocs * 8 * PGSIZE;
  if (total < min_total)
    total = min_total;

  uint64 amt = PGROUNDUP(total / nprocs);

  int start = uptime();
  if (!quiet) {
    printf("swapconctest: mode=%s total=%lu\n", full ? "full" : "light", total);
    printf("swapconctest: start procs=%d rounds=%d max_ticks=%d verbose=%d\n",
           nprocs, rounds, max_ticks, verbose ? 1 : 0);
  }
  for (int i = 0; i < nprocs; i++) {
    int pid = fork();
    if (pid < 0)
      fail("fork failed");
    if (pid == 0)
      worker(amt, rounds, i, max_ticks, verbose);
  }

  for (int i = 0; i < nprocs; i++) {
    if (wait(0) < 0)
      fail("wait failed");
  }
  int end = uptime();

  if (!quiet) {
    printf("swapconctest: %d procs, %d ticks\n", nprocs, end - start);
    printf("swapconctest: ok\n");
  }
  exit(0);
}
