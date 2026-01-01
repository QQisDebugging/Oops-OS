#include "types.h"
#include "stat.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("monitortest: %s\n", msg);
  exit(1);
}

static void
test_invalid_args(void)
{
  if (mon_free(-1) != -1)
    fail("mon_free negative should fail");
  if (mon_enter(-1) != -1)
    fail("mon_enter negative should fail");
  if (cond_create(-1) != -1)
    fail("cond_create invalid monitor should fail");
  if (cond_wait(-1, 0) != -1)
    fail("cond_wait invalid monitor should fail");
  if (cond_signal(-1, 0) != -1)
    fail("cond_signal invalid monitor should fail");
  if (cond_broadcast(-1, 0) != -1)
    fail("cond_broadcast invalid monitor should fail");
}

static void
test_create_free(void)
{
  int m = mon_create();
  if (m < 0)
    fail("mon_create failed");

  int c = cond_create(m);
  if (c < 0)
    fail("cond_create failed");

  if (mon_free(m) == 0)
    fail("mon_free should fail with active condvar");
  if (cond_free(m, c) != 0)
    fail("cond_free failed");
  if (mon_free(m) != 0)
    fail("mon_free failed");
}

static void
test_mutex(void)
{
  int m = mon_create();
  if (m < 0)
    fail("mon_create failed");

  sh_var_write(0);
  int nchild = 4;
  int iters = 200;
  for (int i = 0; i < nchild; i++) {
    int pid = fork();
    if (pid == 0) {
      for (int j = 0; j < iters; j++) {
        if (mon_enter(m) != 0)
          exit(1);
        int v = sh_var_read();
        sh_var_write(v + 1);
        if (mon_exit(m) != 0)
          exit(1);
      }
      exit(0);
    }
  }

  for (int i = 0; i < nchild; i++)
    wait(0);

  if (sh_var_read() != nchild * iters)
    fail("mutex counter mismatch");
  if (mon_free(m) != 0)
    fail("mon_free failed");
}

static void
test_cond_signal(void)
{
  int m = mon_create();
  if (m < 0)
    fail("mon_create failed");
  int c = cond_create(m);
  if (c < 0)
    fail("cond_create failed");

  int fds[2];
  if (pipe(fds) != 0)
    fail("pipe failed");

  sh_var_write(0);
  int pid = fork();
  if (pid == 0) {
    close(fds[0]);
    if (mon_enter(m) != 0)
      exit(1);
    if (write(fds[1], "r", 1) != 1)
      exit(1);
    while (sh_var_read() == 0) {
      if (cond_wait(m, c) != 0)
        exit(1);
    }
    int v = sh_var_read();
    sh_var_write(v + 1);
    if (mon_exit(m) != 0)
      exit(1);
    close(fds[1]);
    exit(0);
  }

  close(fds[1]);
  char ch;
  if (read(fds[0], &ch, 1) != 1)
    fail("ready read failed");

  if (mon_enter(m) != 0)
    fail("mon_enter failed");
  sh_var_write(1);
  if (cond_signal(m, c) != 0)
    fail("cond_signal failed");
  if (mon_exit(m) != 0)
    fail("mon_exit failed");

  wait(0);
  close(fds[0]);

  if (sh_var_read() != 2)
    fail("cond_signal did not wake waiter");

  if (cond_free(m, c) != 0)
    fail("cond_free failed");
  if (mon_free(m) != 0)
    fail("mon_free failed");
}

static void
test_cond_broadcast(void)
{
  int m = mon_create();
  if (m < 0)
    fail("mon_create failed");
  int c = cond_create(m);
  if (c < 0)
    fail("cond_create failed");

  int fds[2];
  if (pipe(fds) != 0)
    fail("pipe failed");

  sh_var_write(0);
  int nchild = 3;
  for (int i = 0; i < nchild; i++) {
    int pid = fork();
    if (pid == 0) {
      close(fds[0]);
      if (mon_enter(m) != 0)
        exit(1);
      if (write(fds[1], "r", 1) != 1)
        exit(1);
      while (sh_var_read() == 0) {
        if (cond_wait(m, c) != 0)
          exit(1);
      }
      int v = sh_var_read();
      sh_var_write(v + 1);
      if (mon_exit(m) != 0)
        exit(1);
      close(fds[1]);
      exit(0);
    }
  }

  close(fds[1]);
  char ch;
  for (int i = 0; i < nchild; i++) {
    if (read(fds[0], &ch, 1) != 1)
      fail("ready read failed");
  }

  if (mon_enter(m) != 0)
    fail("mon_enter failed");
  sh_var_write(1);
  if (cond_broadcast(m, c) != 0)
    fail("cond_broadcast failed");
  if (mon_exit(m) != 0)
    fail("mon_exit failed");

  for (int i = 0; i < nchild; i++)
    wait(0);
  close(fds[0]);

  if (sh_var_read() != 1 + nchild)
    fail("cond_broadcast did not wake all waiters");

  if (cond_free(m, c) != 0)
    fail("cond_free failed");
  if (mon_free(m) != 0)
    fail("mon_free failed");
}

int
main(int argc, char *argv[])
{
  test_invalid_args();
  test_create_free();
  test_mutex();
  test_cond_signal();
  test_cond_broadcast();
  printf("monitortest: ok\n");
  exit(0);
}
