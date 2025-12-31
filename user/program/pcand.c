#include "types.h"
#include "stat.h"
#include "user/user.h"

#define BUF_MAX 1
#define ITERS 10

static void
fail(const char *msg)
{
  printf("pcand: %s\n", msg);
  exit(1);
}

static void
producer_bad(int mutex, int empty, int ready)
{
  if (sem_p(mutex) != 0)
    exit(1);
  sem_v(ready);
  sem_p(empty);
  sem_v(mutex);
  exit(2);
}

static void
consumer_bad(int mutex, int full, int ready)
{
  if (sem_p(ready) != 0)
    exit(1);
  sem_p(mutex);
  sem_p(full);
  sem_v(mutex);
  exit(2);
}

static void
demo_deadlock(void)
{
  int mutex = sem_create(1);
  int empty = sem_create(0);
  int full = sem_create(1);
  int ready = sem_create(0);
  if (mutex < 0 || empty < 0 || full < 0 || ready < 0)
    fail("sem_create failed");

  int pid1 = fork();
  if (pid1 == 0)
    producer_bad(mutex, empty, ready);

  int pid2 = fork();
  if (pid2 == 0)
    consumer_bad(mutex, full, ready);

  sleep(10);
  kill(pid1);
  kill(pid2);

  int x1 = 0, x2 = 0;
  wait(&x1);
  wait(&x2);
  if (x1 == 2 || x2 == 2)
    fail("deadlock not triggered");
  printf("pcand: deadlock detected\n");

  sem_v(mutex);
  sem_v(mutex);
  sem_v(empty);

  sem_free(mutex);
  sem_free(empty);
  sem_free(full);
  sem_free(ready);
}

static void
producer_and(int mutex, int empty, int full)
{
  int ids[2] = {mutex, empty};
  for (int i = 0; i < ITERS; i++)
  {
    if (sem_p_multi(2, ids) != 0)
      exit(1);
    int v = sh_var_read();
    if (v >= BUF_MAX)
      exit(2);
    sh_var_write(v + 1);
    sem_v(mutex);
    sem_v(full);
  }
  exit(0);
}

static void
consumer_and(int mutex, int empty, int full)
{
  int ids[2] = {mutex, full};
  for (int i = 0; i < ITERS; i++)
  {
    if (sem_p_multi(2, ids) != 0)
      exit(1);
    int v = sh_var_read();
    if (v <= 0)
      exit(2);
    sh_var_write(v - 1);
    sem_v(mutex);
    sem_v(empty);
  }
  exit(0);
}

static void
demo_and(void)
{
  int mutex = sem_create(1);
  int empty = sem_create(BUF_MAX);
  int full = sem_create(0);
  if (mutex < 0 || empty < 0 || full < 0)
    fail("sem_create failed");

  sh_var_write(0);

  int pid1 = fork();
  if (pid1 == 0)
    producer_and(mutex, empty, full);

  int pid2 = fork();
  if (pid2 == 0)
    consumer_and(mutex, empty, full);

  int x1 = 0, x2 = 0;
  wait(&x1);
  wait(&x2);
  if (x1 != 0 || x2 != 0)
    fail("and failed");
  if (sh_var_read() != 0)
    fail("count mismatch");
  printf("pcand: AND completed\n");

  if (sem_free(mutex) != 0 || sem_free(empty) != 0 || sem_free(full) != 0)
    fail("sem_free failed");
}

int
main(int argc, char *argv[])
{
  demo_deadlock();
  demo_and();
  printf("pcand: ok\n");
  exit(0);
}
