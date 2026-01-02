#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "param.h"

#define HOG_TICKS 200
#define ROUNDS 40
#define PERIOD 10
#define RUNTIME 2
#define DEADLINE 6
#define TASK_WORK 4000
#define HOG_WORK 40000

static void
busywork(int iters)
{
  volatile int x = 0;
  for (int i = 0; i < iters; i++)
    x += i;
  if (x == 0x7fffffff)
    printf("llftest: unreachable\n");
}

static void
hog(int ticks)
{
  int start = uptime();
  while (uptime() - start < ticks)
    busywork(HOG_WORK);
  exit(0);
}

static int
run_once(const char *label, int use_rt)
{
  int hogs = NCPU * 2;
  if (hogs < 2)
    hogs = 2;
  if (hogs > NPROC - 4)
    hogs = NPROC - 4;

  printf("llftest: mode=%s period=%d runtime=%d deadline=%d rounds=%d hogs=%d\n",
         label, PERIOD, RUNTIME, DEADLINE, ROUNDS, hogs);

  int started = 0;
  for (int i = 0; i < hogs; i++)
  {
    int pid = fork();
    if (pid == 0)
      hog(HOG_TICKS);
    if (pid > 0)
      started++;
    if (pid < 0)
      break;
  }

  if (use_rt)
  {
    if (rt_set(getpid(), PERIOD, RUNTIME, DEADLINE) < 0)
    {
      printf("llftest: rt_set failed\n");
      exit(1);
    }
  }
  else
  {
    if (rt_clear(getpid()) < 0)
    {
      printf("llftest: rt_clear failed\n");
      exit(1);
    }
  }

  int misses = 0;
  for (int i = 0; i < ROUNDS; i++)
  {
    int start = uptime();
    while (uptime() - start < RUNTIME)
      busywork(TASK_WORK);
    int finish = uptime();
    if (finish - start > DEADLINE)
      misses++;

    int elapsed = uptime() - start;
    int rest = PERIOD - elapsed;
    if (rest > 0)
      sleep(rest);
  }

  if (use_rt)
    rt_clear(getpid());

  for (int i = 0; i < started; i++)
    wait(0);

  printf("llftest: mode=%s misses=%d\n", label, misses);
  return misses;
}

int
main(void)
{
  int pid = getpid();
  if (rt_set(pid, 0, 1, 1) >= 0)
  {
    printf("llftest: rt_set accepted bad period\n");
    exit(1);
  }
  if (rt_set(pid, 10, 0, 5) >= 0)
  {
    printf("llftest: rt_set accepted bad runtime\n");
    exit(1);
  }
  if (rt_set(pid, 10, 5, 0) >= 0)
  {
    printf("llftest: rt_set accepted bad deadline\n");
    exit(1);
  }
  if (rt_set(pid, 10, 9, 5) >= 0)
  {
    printf("llftest: rt_set accepted runtime > deadline\n");
    exit(1);
  }
  if (rt_set(pid, 10, 6, 11) >= 0)
  {
    printf("llftest: rt_set accepted deadline > period\n");
    exit(1);
  }
  rt_clear(pid);

  int misses_normal = run_once("normal", 0);
  int misses_rt = run_once("rt", 1);
  printf("llftest: compare normal=%d rt=%d delta=%d\n",
         misses_normal, misses_rt, misses_normal - misses_rt);

  if (misses_rt > misses_normal)
  {
    printf("llftest: rt worse than normal\n");
    exit(1);
  }

  printf("llftest: ok\n");
  exit(0);
}
