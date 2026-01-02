#include "types.h"
#include "user/user.h"

#define WORK_LOW 10000000ULL
#define WORK_MID 40000000ULL

static volatile uint64 sink = 0;

static void
busy(uint64 loops)
{
  for (uint64 i = 0; i < loops; i++)
    sink += i;
}

static void
safe_close(int fd)
{
  if (fd >= 0)
    close(fd);
}

int
main(void)
{
  int mon = mon_create();
  if (mon < 0)
  {
    printf("pitest: mon_create failed\n");
    exit(1);
  }

  int low_ready[2];
  int high_ready[2];
  int order[2];

  if (pipe(low_ready) < 0 || pipe(high_ready) < 0 || pipe(order) < 0)
  {
    printf("pitest: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid == 0)
  {
    safe_close(low_ready[0]);
    safe_close(high_ready[0]);
    safe_close(high_ready[1]);
    safe_close(order[0]);

    setPriority(getpid(), 1);
    if (mon_enter(mon) < 0)
    {
      printf("pitest: low mon_enter failed\n");
      exit(1);
    }
    write(low_ready[1], "L", 1);
    busy(WORK_LOW);
    mon_exit(mon);
    exit(0);
  }

  safe_close(low_ready[1]);

  char ch;
  if (read(low_ready[0], &ch, 1) != 1)
  {
    printf("pitest: low sync failed\n");
    exit(1);
  }
  safe_close(low_ready[0]);

  pid = fork();
  if (pid == 0)
  {
    safe_close(high_ready[0]);
    safe_close(order[0]);

    setPriority(getpid(), 20);
    write(high_ready[1], "H", 1);

    if (mon_enter(mon) < 0)
    {
      printf("pitest: high mon_enter failed\n");
      exit(1);
    }
    write(order[1], "H", 1);
    mon_exit(mon);
    exit(0);
  }

  if (read(high_ready[0], &ch, 1) != 1)
  {
    printf("pitest: high sync failed\n");
    exit(1);
  }
  safe_close(high_ready[1]);
  safe_close(high_ready[0]);

  pid = fork();
  if (pid == 0)
  {
    safe_close(order[0]);

    setPriority(getpid(), 18);
    busy(WORK_MID);
    write(order[1], "M", 1);
    exit(0);
  }

  safe_close(order[1]);

  if (read(order[0], &ch, 1) != 1)
  {
    printf("pitest: order read failed\n");
    exit(1);
  }
  if (ch == 'H')
    printf("pitest: ok (high before mid)\n");
  else
    printf("pitest: inversion (mid before high)\n");

  int status = 0;
  for (int i = 0; i < 3; i++)
    wait(&status);

  mon_free(mon);
  exit(0);
}
