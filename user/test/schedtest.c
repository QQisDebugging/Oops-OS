#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "param.h"

#define HOG_TICKS 200
#define ROUNDS 60
#define HANDLER_WORK 4000
#define HOG_WORK 40000

static void
busywork(int iters)
{
  volatile int x = 0;
  for (int i = 0; i < iters; i++)
    x += i;
  if (x == 0x7fffffff)
    printf("schedtest: unreachable\n");
}

static void
hog(int ticks)
{
  int start = uptime();
  while (uptime() - start < ticks)
    busywork(HOG_WORK);
  exit(0);
}

static void
worker(int rfd, int wfd)
{
  char ch;
  for (int i = 0; i < ROUNDS; i++)
  {
    if (read(rfd, &ch, 1) != 1)
      exit(1);
    busywork(HANDLER_WORK);
    if (write(wfd, &ch, 1) != 1)
      exit(1);
  }
  exit(0);
}

static void
insertion_sort(int *a, int n)
{
  for (int i = 1; i < n; i++)
  {
    int key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key)
    {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = key;
  }
}

int
main(void)
{
  printf("schedtest: scheduler=");
#if defined(SCHED_MLFQ)
  printf("MLFQ\n");
#elif defined(SCHED_DYNPRIO)
  printf("DYNPRIO\n");
#else
  printf("UNKNOWN\n");
#endif

  int hogs = NCPU * 2;
  if (hogs < 2)
    hogs = 2;
  if (hogs > NPROC - 4)
    hogs = NPROC - 4;

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

  int p2c[2];
  int c2p[2];
  if (pipe(p2c) < 0 || pipe(c2p) < 0)
  {
    printf("schedtest: pipe failed\n");
    exit(1);
  }

  int worker_pid = fork();
  if (worker_pid == 0)
  {
    close(p2c[1]);
    close(c2p[0]);
    worker(p2c[0], c2p[1]);
  }

  close(p2c[0]);
  close(c2p[1]);

  int lat[ROUNDS];
  char ch = 'x';
  int start = uptime();
  for (int i = 0; i < ROUNDS; i++)
  {
    int t0 = uptime();
    if (write(p2c[1], &ch, 1) != 1 || read(c2p[0], &ch, 1) != 1)
    {
      printf("schedtest: ping-pong failed\n");
      exit(1);
    }
    lat[i] = uptime() - t0;
  }

  int st = 0;
  wait(&st);

  int sum = 0;
  int max = 0;
  int sorted[ROUNDS];
  for (int i = 0; i < ROUNDS; i++)
  {
    sum += lat[i];
    if (lat[i] > max)
      max = lat[i];
    sorted[i] = lat[i];
  }
  insertion_sort(sorted, ROUNDS);
  int p95 = sorted[(ROUNDS * 95) / 100];

  printf("schedtest: hogs=%d rounds=%d\n", started, ROUNDS);
  printf("schedtest: latency avg %d ticks, p95 %d, max %d\n",
         sum / ROUNDS, p95, max);
  printf("schedtest: total %d ticks\n", uptime() - start);

  for (int i = 0; i < started; i++)
    wait(&st);

  exit(0);
}
