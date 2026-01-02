#include "types.h"
#include "stat.h"
#include "user/user.h"

static int verbose = 1;

static void
fail(const char *msg)
{
  if (verbose)
    printf("deadlocktest: %s\n", msg);
  exit(1);
}

static void
wait_two(int pid1, int pid2, int total, int *st1, int *st2)
{
  *st1 = -1;
  *st2 = -1;
  for (int i = 0; i < total; i++)
  {
    int st = 0;
    int pid = wait(&st);
    if (pid == pid1)
      *st1 = st;
    else if (pid == pid2)
      *st2 = st;
  }
  if (*st1 == -1 || *st2 == -1)
    fail("wait failed");
}

static void
sem_deadlock(void)
{
  int a = sem_create(1);
  int b = sem_create(1);
  int ready = sem_create(0);
  int go = sem_create(0);
  if (a < 0 || b < 0 || ready < 0 || go < 0)
    fail("sem_create failed");

  sh_var_write(0);

  int pid1 = fork();
  if (pid1 < 0)
    fail("fork failed");
  if (pid1 == 0)
  {
    if (sem_p(a) < 0)
      exit(2);
    sem_v(ready);
    sem_p(go);
    if (sem_p(b) < 0)
    {
      int v = sh_var_read();
      sh_var_write(v + 1);
      sem_v(a);
      exit(0);
    }
    sem_v(b);
    sem_v(a);
    exit(2);
  }

  int pid2 = fork();
  if (pid2 < 0)
    fail("fork failed");
  if (pid2 == 0)
  {
    if (sem_p(b) < 0)
      exit(2);
    sem_v(ready);
    sem_p(go);
    sleep(5);
    if (sem_p(a) < 0)
    {
      int v = sh_var_read();
      sh_var_write(v + 1);
      sem_v(b);
      exit(0);
    }
    sem_v(a);
    sem_v(b);
    exit(2);
  }

  sem_p(ready);
  sem_p(ready);
  sem_v(go);
  sem_v(go);
  sleep(10);

  int breaker = fork();
  if (breaker < 0)
    fail("fork failed");
  if (breaker == 0)
  {
    sleep(30);
    sem_v(a);
    sem_v(b);
    exit(0);
  }

  int s1 = 0, s2 = 0;
  wait_two(pid1, pid2, 3, &s1, &s2);
  if (sh_var_read() == 0)
    fail("sem deadlock not detected");
  if (verbose)
    printf("deadlocktest: sem deadlock detected\n");

  if (sem_p(a) != 0 || sem_p(b) != 0)
    fail("sem recover failed");
  sem_v(b);
  sem_v(a);
  if (verbose)
    printf("deadlocktest: sem recovered\n");

  if (sem_free(a) != 0 || sem_free(b) != 0 ||
      sem_free(ready) != 0 || sem_free(go) != 0)
    fail("sem_free failed");
  if (verbose)
    printf("deadlocktest: sem ok\n");
}

static void
semset_deadlock(void)
{
  int init[2] = {1, 1};
  int set = semset_create(2, init);
  int ready = sem_create(0);
  int go = sem_create(0);
  if (set < 0 || ready < 0 || go < 0)
    fail("semset_create failed");

  sh_var_write(0);

  int pid1 = fork();
  if (pid1 < 0)
    fail("fork failed");
  if (pid1 == 0)
  {
    if (semset_p(set, 0) < 0)
      exit(2);
    sem_v(ready);
    sem_p(go);
    if (semset_p(set, 1) < 0)
    {
      int v = sh_var_read();
      sh_var_write(v + 1);
      semset_v(set, 0);
      exit(0);
    }
    semset_v(set, 1);
    semset_v(set, 0);
    exit(2);
  }

  int pid2 = fork();
  if (pid2 < 0)
    fail("fork failed");
  if (pid2 == 0)
  {
    if (semset_p(set, 1) < 0)
      exit(2);
    sem_v(ready);
    sem_p(go);
    sleep(5);
    if (semset_p(set, 0) < 0)
    {
      int v = sh_var_read();
      sh_var_write(v + 1);
      semset_v(set, 1);
      exit(0);
    }
    semset_v(set, 0);
    semset_v(set, 1);
    exit(2);
  }

  sem_p(ready);
  sem_p(ready);
  sem_v(go);
  sem_v(go);
  sleep(10);

  int breaker = fork();
  if (breaker < 0)
    fail("fork failed");
  if (breaker == 0)
  {
    sleep(30);
    semset_v(set, 0);
    semset_v(set, 1);
    exit(0);
  }

  int s1 = 0, s2 = 0;
  wait_two(pid1, pid2, 3, &s1, &s2);
  if (sh_var_read() == 0)
    fail("semset deadlock not detected");
  if (verbose)
    printf("deadlocktest: semset deadlock detected\n");

  if (semset_p(set, 0) != 0 || semset_p(set, 1) != 0)
    fail("semset recover failed");
  semset_v(set, 1);
  semset_v(set, 0);
  if (verbose)
    printf("deadlocktest: semset recovered\n");

  if (semset_free(set) != 0 ||
      sem_free(ready) != 0 || sem_free(go) != 0)
    fail("semset_free failed");
  if (verbose)
    printf("deadlocktest: semset ok\n");
}

static void
monitor_deadlock(void)
{
  int m1 = mon_create();
  int m2 = mon_create();
  int ready = sem_create(0);
  int go = sem_create(0);
  if (m1 < 0 || m2 < 0 || ready < 0 || go < 0)
    fail("mon_create failed");

  sh_var_write(0);

  int pid1 = fork();
  if (pid1 < 0)
    fail("fork failed");
  if (pid1 == 0)
  {
    if (mon_enter(m1) != 0)
      exit(2);
    sem_v(ready);
    sem_p(go);
    if (mon_enter(m2) < 0)
    {
      sh_var_write(1);
      mon_exit(m1);
      exit(0);
    }
    mon_exit(m2);
    mon_exit(m1);
    exit(2);
  }

  int pid2 = fork();
  if (pid2 < 0)
    fail("fork failed");
  if (pid2 == 0)
  {
    if (mon_enter(m2) != 0)
      exit(2);
    sem_v(ready);
    sem_p(go);
    sleep(5);
    if (mon_enter(m1) < 0)
    {
      sh_var_write(1);
      mon_exit(m2);
      exit(0);
    }
    mon_exit(m1);
    mon_exit(m2);
    exit(2);
  }

  sem_p(ready);
  sem_p(ready);
  sem_v(go);
  sem_v(go);

  int watchdog = fork();
  if (watchdog < 0)
    fail("fork failed");
  if (watchdog == 0)
  {
    sleep(30);
    if (sh_var_read() == 0)
    {
      sh_var_write(2);
      kill(pid1);
      kill(pid2);
    }
    exit(0);
  }

  int s1 = 0, s2 = 0;
  wait_two(pid1, pid2, 3, &s1, &s2);
  int flag = sh_var_read();
  if (flag == 0)
    fail("monitor deadlock not detected");
  if (flag == 2)
    fail("monitor deadlock not detected");
  if (verbose)
    printf("deadlocktest: monitor deadlock detected\n");

  if (mon_enter(m1) != 0 || mon_enter(m2) != 0)
    fail("monitor recover failed");
  mon_exit(m2);
  mon_exit(m1);
  if (verbose)
    printf("deadlocktest: monitor recovered\n");

  if (mon_free(m1) != 0 || mon_free(m2) != 0)
    fail("mon_free failed");
  if (sem_free(ready) != 0 || sem_free(go) != 0)
    fail("sem_free failed");
  sh_var_write(0);
  if (verbose)
    printf("deadlocktest: monitor ok\n");
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "-q") == 0)
    verbose = 0;
  sem_deadlock();
  semset_deadlock();
  monitor_deadlock();
  if (verbose)
    printf("deadlocktest: ok\n");
  exit(0);
}
