#include "types.h"
#include "stat.h"
#include "user/user.h"

static int verbose = 1;

static void
fail(const char *msg)
{
  if (verbose)
    printf("semsettest: %s\n", msg);
  exit(1);
}

static void
test_invalid_args(void)
{
  int init[2] = {0, 0};
  if (semset_create(0, init) != -1)
    fail("semset_create zero should fail");
  if (semset_create(99, 0) != -1)
    fail("semset_create oversize should fail");

  int set = semset_create(2, init);
  if (set < 0)
    fail("semset_create failed");

  if (semset_p(-1, 0) != -1)
    fail("semset_p invalid set should fail");
  if (semset_v(set, 2) != -1)
    fail("semset_v invalid idx should fail");

  int dup[2] = {0, 0};
  if (semset_p_multi(set, 2, dup) != -1)
    fail("semset_p_multi duplicate should fail");
  int bad[2] = {0, 3};
  if (semset_p_multi(set, 2, bad) != -1)
    fail("semset_p_multi out of range should fail");

  if (semset_free(set) != 0)
    fail("semset_free failed");
  if (semset_free(set) == 0)
    fail("semset_free twice should fail");
}

static void
test_free_waiter(void)
{
  int init[1] = {0};
  int set = semset_create(1, init);
  if (set < 0)
    fail("semset_create failed");

  int pid = fork();
  if (pid == 0)
  {
    semset_p(set, 0);
    exit(0);
  }

  sleep(1);
  if (semset_free(set) == 0)
    fail("semset_free should fail with waiters");

  semset_v(set, 0);
  wait(0);
  if (semset_free(set) != 0)
    fail("semset_free after wake failed");
}

static void
test_deadlock_without_and(void)
{
  int init[4] = {1, 1, 0, 0};
  int set = semset_create(4, init);
  if (set < 0)
    fail("semset_create failed");

  sh_var_write(0);

  int pid1 = fork();
  if (pid1 == 0)
  {
    semset_p(set, 0);
    semset_v(set, 2);
    semset_p(set, 3);
    if (semset_p(set, 1) < 0)
    {
      int v = sh_var_read();
      sh_var_write(v + 1);
      semset_v(set, 3);
      semset_v(set, 0);
      exit(0);
    }
    exit(2);
  }

  int pid2 = fork();
  if (pid2 == 0)
  {
    semset_p(set, 1);
    semset_v(set, 2);
    semset_p(set, 3);
    sleep(5);
    if (semset_p(set, 0) < 0)
    {
      int v = sh_var_read();
      sh_var_write(v + 1);
      semset_v(set, 3);
      semset_v(set, 1);
      exit(0);
    }
    exit(2);
  }

  semset_p(set, 2);
  semset_p(set, 2);
  semset_v(set, 3);
  semset_v(set, 3);
  sleep(10);

  if (sh_var_read() == 0)
  {
    kill(pid1);
    kill(pid2);
  }

  int x1 = 0, x2 = 0;
  wait(&x1);
  wait(&x2);
  if (sh_var_read() == 0)
    fail("deadlock not triggered");
  if (verbose)
    printf("semsettest: deadlock detected\n");

  semset_v(set, 0);
  semset_v(set, 1);
  if (semset_free(set) != 0)
    fail("semset_free failed");
}

static void
test_and_semset(void)
{
  int init[2] = {1, 1};
  int set = semset_create(2, init);
  if (set < 0)
    fail("semset_create failed");

  int idxs[2] = {0, 1};
  int pid1 = fork();
  if (pid1 == 0)
  {
    if (semset_p_multi(set, 2, idxs) != 0)
      exit(1);
    semset_v(set, 0);
    semset_v(set, 1);
    exit(0);
  }

  int pid2 = fork();
  if (pid2 == 0)
  {
    if (semset_p_multi(set, 2, idxs) != 0)
      exit(1);
    semset_v(set, 0);
    semset_v(set, 1);
    exit(0);
  }

  int x1 = 0, x2 = 0;
  wait(&x1);
  wait(&x2);
  if (x1 != 0 || x2 != 0)
    fail("semset_p_multi failed");
  if (verbose)
    printf("semsettest: AND completed\n");

  if (semset_free(set) != 0)
    fail("semset_free failed");
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "-q") == 0)
    verbose = 0;
  test_invalid_args();
  test_free_waiter();
  test_deadlock_without_and();
  test_and_semset();
  if (verbose)
    printf("semsettest: ok\n");
  exit(0);
}
