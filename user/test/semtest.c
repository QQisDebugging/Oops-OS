#include "types.h"
#include "stat.h"
#include "user/user.h"

static int verbose = 1;

static void
fail(const char *msg)
{
  if (verbose)
    printf("semtest: %s\n", msg);
  exit(1);
}

static void
test_invalid_args(void)
{
  if (sem_create(-1) != -1)
    fail("sem_create negative should fail");
  if (sem_p(-1) != -1)
    fail("sem_p negative should fail");
  if (sem_v(-1) != -1)
    fail("sem_v negative should fail");
  if (sem_free(-1) != -1)
    fail("sem_free negative should fail");
  if (sem_p(9999) != -1)
    fail("sem_p invalid id should fail");
  if (sem_v(9999) != -1)
    fail("sem_v invalid id should fail");
  if (sem_free(9999) != -1)
    fail("sem_free invalid id should fail");
}

static void
test_use_after_free(void)
{
  int id = sem_create(1);
  if (id < 0)
    fail("sem_create failed");
  if (sem_free(id) != 0)
    fail("sem_free failed");
  if (sem_free(id) == 0)
    fail("sem_free twice should fail");
  if (sem_p(id) != -1)
    fail("sem_p after free should fail");
  if (sem_v(id) != -1)
    fail("sem_v after free should fail");
}

static void
test_blocking_sem(void)
{
  int id = sem_create(0);
  if (id < 0)
    fail("sem_create failed");

  sh_var_write(0);
  int pid = fork();
  if (pid == 0) {
    sleep(10);
    sh_var_write(1);
    sem_v(id);
    exit(0);
  }

  if (sem_p(id) != 0)
    fail("sem_p failed");
  if (sh_var_read() != 1)
    fail("blocking failed");

  wait(0);
  if (sem_free(id) != 0)
    fail("sem_free failed");
}

static void
test_free_waiter(void)
{
  int id = sem_create(0);
  if (id < 0)
    fail("sem_create failed");

  int pid = fork();
  if (pid == 0) {
    sem_p(id);
    exit(0);
  }

  sleep(1);
  if (sem_free(id) == 0)
    fail("sem_free should fail with waiters");

  sem_v(id);
  wait(0);
  if (sem_free(id) != 0)
    fail("sem_free after wake failed");
}

static void
test_multiple_waiters(void)
{
  int id = sem_create(0);
  if (id < 0)
    fail("sem_create failed");

  sh_var_write(0);
  for (int i = 0; i < 2; i++) {
    int pid = fork();
    if (pid == 0) {
      sem_p(id);
      int v = sh_var_read();
      sh_var_write(v + 1);
      exit(0);
    }
  }

  sem_v(id);
  wait(0);
  if (sh_var_read() != 1)
    fail("first waiter did not run");

  sem_v(id);
  wait(0);
  if (sh_var_read() != 2)
    fail("second waiter did not run");

  if (sem_free(id) != 0)
    fail("sem_free failed");
}

static void
test_many_increments(void)
{
  int id = sem_create(1);
  if (id < 0)
    fail("sem_create failed");

  sh_var_write(0);
  int nchild = 4;
  int iters = 200;
  for (int i = 0; i < nchild; i++) {
    int pid = fork();
    if (pid == 0) {
      for (int j = 0; j < iters; j++) {
        if (sem_p(id) != 0)
          exit(1);
        int v = sh_var_read();
        sh_var_write(v + 1);
        sem_v(id);
      }
      exit(0);
    }
  }

  for (int i = 0; i < nchild; i++)
    wait(0);

  if (sh_var_read() != nchild * iters)
    fail("counter mismatch under contention");

  if (sem_free(id) != 0)
    fail("sem_free failed");
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "-q") == 0)
    verbose = 0;

  test_invalid_args();
  test_use_after_free();
  test_blocking_sem();
  test_free_waiter();
  test_multiple_waiters();
  test_many_increments();
  if (verbose)
    printf("semtest: ok\n");
  exit(0);
}
