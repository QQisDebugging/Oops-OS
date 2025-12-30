#include "types.h"
#include "stat.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("semandtest: %s\n", msg);
  exit(1);
}

static void
test_deadlock_without_and(void)
{
  int a = sem_create(1);
  int b = sem_create(1);
  int ready = sem_create(0);
  int go = sem_create(0);
  if (a < 0 || b < 0 || ready < 0 || go < 0)
    fail("sem_create failed");

  int pid1 = fork();
  if (pid1 == 0)
  {
    sem_p(a);
    sem_v(ready);
    sem_p(go);
    sem_p(b);
    exit(2);
  }

  int pid2 = fork();
  if (pid2 == 0)
  {
    sem_p(b);
    sem_v(ready);
    sem_p(go);
    sem_p(a);
    exit(2);
  }

  sem_p(ready);
  sem_p(ready);
  sem_v(go);
  sem_v(go);
  sleep(10);

  kill(pid1);
  kill(pid2);

  int x1 = 0, x2 = 0;
  wait(&x1);
  wait(&x2);
  if (x1 == 2 || x2 == 2)
    fail("deadlock not triggered");
  printf("semandtest: deadlock detected\n");

  sem_v(a);
  sem_v(b);
  if (sem_free(a) != 0 || sem_free(b) != 0 ||
      sem_free(ready) != 0 || sem_free(go) != 0)
    fail("sem_free failed");
}

static void
test_and_sem(void)
{
  int a = sem_create(1);
  int b = sem_create(1);
  if (a < 0 || b < 0)
    fail("sem_create failed");

  int ids[2] = {a, b};
  int pid1 = fork();
  if (pid1 == 0)
  {
    if (sem_p_multi(2, ids) != 0)
      exit(1);
    sem_v(a);
    sem_v(b);
    exit(0);
  }

  int pid2 = fork();
  if (pid2 == 0)
  {
    if (sem_p_multi(2, ids) != 0)
      exit(1);
    sem_v(a);
    sem_v(b);
    exit(0);
  }

  int x1 = 0, x2 = 0;
  wait(&x1);
  wait(&x2);
  if (x1 != 0 || x2 != 0)
    fail("sem_p_multi failed");
  printf("semandtest: AND completed\n");
  if (sem_free(a) != 0 || sem_free(b) != 0)
    fail("sem_free failed");
}

int
main(int argc, char *argv[])
{
  test_deadlock_without_and();
  test_and_sem();
  printf("semandtest: ok\n");
  exit(0);
}
