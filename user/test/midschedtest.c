// midschedtest.c - 中级调度（进程级挂起/恢复）测试

#include "types.h"
#include "riscv.h"
#include "param.h"
#include "sysinfo.h"
#include "user/user.h"

static int quiet = 0;

static void
fail(const char *msg)
{
  if (!quiet)
    printf("midschedtest: %s\n", msg);
  exit(1);
}

static void
touch_pages(char *p, uint64 len)
{
  for (uint64 i = 0; i < len; i += PGSIZE)
    p[i] = (char)i;
}

static int
wait_nsuspended(int want_ge, uint64 target, int timeout_ticks)
{
  struct sysinfo info;
  int start = uptime();

  while (uptime() - start < timeout_ticks)
  {
    if (sysinfo(&info) < 0)
      return 0;
    if (want_ge)
    {
      if (info.nsuspended >= target)
        return 1;
    }
    else
    {
      if (info.nsuspended <= target)
        return 1;
    }
    sleep(5);
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "-q") == 0)
    quiet = 1;

  int old = midsched(1);
  if (old < 0)
    fail("midsched syscall missing");

  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 free0 = info.freemem;
  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 victim_bytes = free0 / 2;
  if (victim_bytes < 4 * PGSIZE)
    victim_bytes = 4 * PGSIZE;
  uint64 pressure_bytes = free0 + swap_bytes / 8;
  if (!quiet)
  {
    printf("midschedtest: freemem=%lu swap=%lu victim=%lu\n",
           free0, swap_bytes, victim_bytes);
  }

  int pid = fork();
  if (pid < 0)
    fail("fork failed");
  if (pid == 0)
  {
    char *p = sbrk(victim_bytes);
    if (p == (char *)-1)
      fail("child sbrk failed");
    touch_pages(p, victim_bytes);
    for (;;)
      sleep(1000000);
  }

  sleep(20);

  if (sysinfo(&info) < 0)
    fail("sysinfo failed (parent)");
  uint64 free1 = info.freemem;
  uint64 min_free = 16 * PGSIZE;
  if (free1 > min_free)
    pressure_bytes = free1 - min_free;
  else
    pressure_bytes = free1 / 2;
  if (!quiet)
  {
    printf("midschedtest: free_after_child=%lu pressure=%lu\n",
           free1, pressure_bytes);
  }

  uint64 chunk = 8 * PGSIZE;
  uint64 got = 0;
  char *q = 0;
  int start = uptime();
  int last_report = start;
  while (got < pressure_bytes)
  {
    if (sbrk(chunk) == (char *)-1)
      break;
    got += chunk;
    q = sbrk(0);
    touch_pages(q - chunk, chunk);
    if (sysinfo(&info) == 0 && info.nsuspended >= 1)
      break;
    if (uptime() - start > 200)
      break;
    if ((got / chunk) % 64 == 0 || uptime() - last_report > 50)
    {
      last_report = uptime();
      if (!quiet && sysinfo(&info) == 0)
      {
        printf("midschedtest: progress got=%lu freemem=%lu nsuspended=%lu\n",
               got, info.freemem, info.nsuspended);
      }
    }
  }
  if (got < chunk)
    fail("parent sbrk failed");
  if (!quiet)
    printf("midschedtest: pressure_alloc=%lu\n", got);
  if (!quiet && sysinfo(&info) == 0)
  {
    printf("midschedtest: after_pressure freemem=%lu nsuspended=%lu swapcached=%lu\n",
           info.freemem, info.nsuspended, info.swapbuf_cached);
  }

  if (!wait_nsuspended(1, 1, 200))
  {
    int force_start = uptime();
    for (int i = 0; i < 128; i++)
    {
      if (sbrk(chunk) == (char *)-1)
        break;
      got += chunk;
      q = sbrk(0);
      touch_pages(q - chunk, chunk);
      if (wait_nsuspended(1, 1, 20))
        break;
      if (uptime() - force_start > 100)
        break;
    }
  }
  if (!wait_nsuspended(1, 1, 200))
    fail("suspend not triggered");
  if (!quiet)
    printf("midschedtest: suspended ok\n");

  if (sbrk(-got) == (char *)-1)
    fail("sbrk shrink failed");

  if (!wait_nsuspended(0, 0, 800))
    fail("resume timeout");
  if (!quiet)
    printf("midschedtest: resumed ok\n");

  kill(pid);
  wait(0);

  midsched(old);
  if (!quiet)
    printf("midschedtest: ok\n");
  exit(0);
}
