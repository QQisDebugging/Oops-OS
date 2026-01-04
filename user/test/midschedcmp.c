// midschedcmp.c - mid-term scheduling on/off comparison (manual)
#include "types.h"
#include "riscv.h"
#include "param.h"
#include "sysinfo.h"
#include "user/user.h"

static void
touch_pages(volatile char *p, uint64 len)
{
  for (uint64 i = 0; i < len; i += PGSIZE)
    p[i]++;
}

static int
start_hog(uint64 target_bytes)
{
  int pid = fork();
  if (pid < 0)
    return -1;
  if (pid == 0)
  {
    uint64 chunk = 4 * PGSIZE;
    uint64 got = 0;
    while (got < target_bytes)
    {
      if (sbrk(chunk) == (char *)-1)
        break;
      got += chunk;
    }
    if (got < chunk)
      exit(1);
    char *base = sbrk(0) - got;
    touch_pages(base, got);
    for (;;)
    {
      volatile int spin = 0;
      for (int k = 0; k < 100000; k++)
        spin += k;
      if (spin == -1)
        printf("");
    }
  }
  return pid;
}

static void
stop_hogs(int *pids, int count)
{
  for (int i = 0; i < count; i++)
  {
    if (pids[i] > 0)
      kill(pids[i]);
  }
  for (int i = 0; i < count; i++)
  {
    if (pids[i] > 0)
      wait(0);
  }
}

static int
ping_rounds(int rounds, int timeout_ticks)
{
  sh_var_write(0);

  int pid = fork();
  if (pid < 0)
    return -1;

  if (pid == 0)
  {
    for (int i = 0; i < rounds; i++)
    {
      int want = 2 * i + 1;
      int start = uptime();
      while (sh_var_read() != want)
      {
        if (uptime() - start > timeout_ticks)
          exit(1);
      }
      sh_var_write(want + 1);
    }
    exit(0);
  }

  int start = uptime();
  for (int i = 0; i < rounds; i++)
  {
    int want = 2 * i + 1;
    sh_var_write(want);
    int wait_start = uptime();
    while (sh_var_read() != want + 1)
    {
      if (uptime() - wait_start > timeout_ticks)
      {
        kill(pid);
        wait(0);
        return -1;
      }
    }
  }
  int elapsed = uptime() - start;
  wait(0);
  return elapsed;
}

static int
run_phase(int enable, int hogs, int rounds, int *nsuspended_out)
{
  int old = midsched(enable);
  if (old < 0)
    return -1;

  struct sysinfo info;
  if (sysinfo(&info) < 0)
    return -1;

  uint64 free0 = info.freemem;
  uint64 hog_bytes = free0 / (hogs + 4);
  if (hog_bytes < 4 * PGSIZE)
    hog_bytes = 4 * PGSIZE;

  int pids[16];
  if (hogs > (int)(sizeof(pids) / sizeof(pids[0])))
    hogs = sizeof(pids) / sizeof(pids[0]);

  int started = 0;
  int elapsed = -1;
  for (int i = 0; i < hogs; i++)
  {
    pids[i] = start_hog(hog_bytes);
    if (pids[i] < 0)
      goto cleanup;
    started++;
  }

  sleep(20);

  if (sysinfo(&info) < 0)
    goto cleanup;
  uint64 free1 = info.freemem;
  uint64 min_free = 16 * PGSIZE;
  uint64 pressure_bytes = free1 * 3 / 4;
  if (free1 > min_free && pressure_bytes + min_free > free1)
    pressure_bytes = free1 - min_free;
  if (free1 <= min_free)
    pressure_bytes = 0;

  printf("midschedcmp: phase %s pressure...\n", enable ? "on" : "off");
  uint64 chunk = 8 * PGSIZE;
  uint64 got = 0;
  char *base = 0;
  while (got < pressure_bytes)
  {
    if (sbrk(chunk) == (char *)-1)
      break;
    got += chunk;
    base = sbrk(0) - chunk;
    touch_pages(base, chunk);
    if (sysinfo(&info) == 0 && info.nsuspended >= 1)
      break;
  }
  printf("midschedcmp: phase %s pressure_alloc=%lu\n",
         enable ? "on" : "off", got);
  if (sysinfo(&info) == 0)
  {
    printf("midschedcmp: phase %s freemem=%lu nsuspended=%lu\n",
           enable ? "on" : "off", info.freemem, info.nsuspended);
  }

  printf("midschedcmp: phase %s ping...\n", enable ? "on" : "off");
  elapsed = ping_rounds(rounds, 200);
  if (sysinfo(&info) < 0)
    elapsed = -1;
  if (nsuspended_out)
    *nsuspended_out = (int)info.nsuspended;

  if (got > 0)
    sbrk(-got);
cleanup:
  stop_hogs(pids, started);
  midsched(old);
  return elapsed;
}

int
main(int argc, char *argv[])
{
  int hogs = 2;
  int rounds = 200;

  if (argc > 1)
    hogs = atoi(argv[1]);
  if (argc > 2)
    rounds = atoi(argv[2]);
  if (hogs <= 0 || hogs > 12)
    hogs = 2;
  if (rounds <= 0)
    rounds = 200;

  int nsusp_off = 0;
  int nsusp_on = 0;
  int t_off = run_phase(0, hogs, rounds, &nsusp_off);
  int t_on = run_phase(1, hogs, rounds, &nsusp_on);

  if (t_off < 0 || t_on < 0)
  {
    printf("midschedcmp: failed\n");
    exit(1);
  }

  printf("midschedcmp: hogs=%d rounds=%d\n", hogs, rounds);
  printf("midschedcmp: midsched=off elapsed=%d ticks nsuspended=%d\n", t_off, nsusp_off);
  printf("midschedcmp: midsched=on  elapsed=%d ticks nsuspended=%d\n", t_on, nsusp_on);
  printf("midschedcmp: delta=%d ticks\n", t_off - t_on);
  exit(0);
}
