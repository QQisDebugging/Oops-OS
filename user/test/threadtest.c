#include "types.h"
#include "stat.h"
#include "riscv.h"
#include "user/user.h"

#define NTHREAD 4

struct thread_arg
{
  int idx;
};

static struct thread_arg args[NTHREAD];
static int results[NTHREAD];
static int child_tid = -1;
static int child_tgid = -1;

static void
fail(const char *msg)
{
  printf("threadtest: %s\n", msg);
  exit(1);
}

static void
worker(void *arg)
{
  struct thread_arg *a = (struct thread_arg *)arg;
  results[a->idx] = a->idx + 10;
  if (a->idx == 0)
  {
    child_tid = gettid();
    child_tgid = gettgid();
  }
  thread_exit(0);
}

static void
noop(void *arg)
{
  (void)arg;
  thread_exit(0);
}

static void
test_basic(void)
{
  for (int i = 0; i < NTHREAD; i++)
  {
    args[i].idx = i;
    results[i] = 0;
  }

  for (int i = 0; i < NTHREAD; i++)
  {
    if (thread_create(worker, &args[i]) < 0)
      fail("thread_create failed");
  }

  for (int i = 0; i < NTHREAD; i++)
  {
    if (thread_join() < 0)
      fail("thread_join failed");
  }

  for (int i = 0; i < NTHREAD; i++)
  {
    if (results[i] != i + 10)
      fail("shared memory mismatch");
  }

  int parent_pid = getpid();
  if (child_tgid != parent_pid)
    fail("tgid mismatch");
  if (child_tid == parent_pid)
    fail("tid should differ from leader pid");
}

static void
test_invalid_stack(void)
{
  void *bad = malloc(PGSIZE);
  if (bad == 0)
    fail("malloc failed");
  uint64 badstack = (uint64)bad;
  if ((badstack % PGSIZE) == 0)
    badstack += 8;

  int pid = clone((uint64)noop, 0, badstack);
  if (pid >= 0)
  {
    uint64 stackaddr = 0;
    join((uint64)&stackaddr);
    fail("clone accepted unaligned stack");
  }
  free(bad);
}

int
main(int argc, char *argv[])
{
  test_basic();
  test_invalid_stack();
  printf("threadtest: ok\n");
  exit(0);
}
