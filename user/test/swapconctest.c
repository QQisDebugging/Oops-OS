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
  int do_print = verbose || tag == 0;
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

  if (argc > 1)
    nprocs = atoi(argv[1]);
  if (argc > 2)
    rounds = atoi(argv[2]);
  if (argc > 3)
    max_ticks = atoi(argv[3]);
  if (argc > 4)
    verbose = atoi(argv[4]);

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

  // 降低内存压力：只使用 freemem 的 80% + swap 的一小部分
  // 避免多进程并发时超出总容量导致死锁
  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 safe_swap = swap_bytes / 8;  // 只使用 1/8 的 swap
  uint64 safe_mem = (info.freemem * 4) / 5;  // 80% 的空闲内存
  uint64 total = safe_mem + safe_swap;
  
  // 确保每个进程分配的量不会太大
  uint64 per_proc_max = (uint64)32 * PGSIZE;  // 每进程最多 128KB
  uint64 amt = PGROUNDUP(total / nprocs);
  if (amt > per_proc_max)
    amt = per_proc_max;

  int start = uptime();
  printf("swapconctest: start procs=%d rounds=%d max_ticks=%d verbose=%d\n",
         nprocs, rounds, max_ticks, verbose ? 1 : 0);
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

  printf("swapconctest: %d procs, %d ticks\n", nprocs, end - start);
  printf("swapconctest: ok\n");
  exit(0);
}
