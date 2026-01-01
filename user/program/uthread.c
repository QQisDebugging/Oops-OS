#include "types.h"
#include "riscv.h"
#include "user/user.h"

#define NTHREAD 4

struct thread_slot
{
  int pid;
  void *stack_base;
  void *stack_alloc;
  int used;
} threads[NTHREAD];

struct thread_start_ctx
{
  void (*fn)(void *);
  void *arg;
};

static int
find_free_slot(void)
{
  for (int i = 0; i < NTHREAD; i++)
  {
    if (threads[i].used == 0)
      return i;
  }
  return -1;
}

static void
add_thread(int slot, int pid, void *stack_base, void *stack_alloc)
{
  threads[slot].pid = pid;
  threads[slot].stack_base = stack_base;
  threads[slot].stack_alloc = stack_alloc;
  threads[slot].used = 1;
}

static int
remove_thread(int pid, uint64 stackaddr)
{
  for (int i = 0; i < NTHREAD; i++)
  {
    if (threads[i].used && threads[i].pid == pid)
    {
      if (stackaddr != 0 && (uint64)threads[i].stack_base != stackaddr)
        return -1;
      free(threads[i].stack_alloc);
      threads[i].pid = 0;
      threads[i].stack_base = 0;
      threads[i].stack_alloc = 0;
      threads[i].used = 0;
      return 0;
    }
  }
  return -1;
}

static void
thread_entry(void *arg)
{
  struct thread_start_ctx *ctx = (struct thread_start_ctx *)arg;
  void (*fn)(void *) = ctx->fn;
  void *fnarg = ctx->arg;
  free(ctx);
  fn(fnarg);
  thread_exit(0);
}

int thread_create(void(*start_routine)(void*),void*arg)
{
  static int first = 1;
  if (first)
  {
    first = 0;
    for (int i = 0; i < NTHREAD; i++)
    {
      threads[i].pid = 0;
      threads[i].stack_base = 0;
      threads[i].stack_alloc = 0;
      threads[i].used = 0;
    }
  }

  int slot = find_free_slot();
  if (slot < 0)
    return -1;

  struct thread_start_ctx *ctx = (struct thread_start_ctx *)malloc(sizeof(*ctx));
  if (ctx == 0)
    return -1;
  ctx->fn = start_routine;
  ctx->arg = arg;

  void *stack_alloc = malloc(2 * PGSIZE + PGSIZE);
  if (stack_alloc == 0)
  {
    free(ctx);
    return -1;
  }
  uint64 base = ((uint64)stack_alloc + PGSIZE - 1) & ~(PGSIZE - 1);
  void *stack_base = (void *)(base + PGSIZE);
  // Touch guard + stack pages to ensure they are mapped before clone.
  ((volatile char *)stack_base)[0] = 0;
  ((volatile char *)stack_base - PGSIZE)[0] = 0;

  int pid = clone((uint64)thread_entry, (uint64)ctx, (uint64)stack_base);
  if (pid < 0)
  {
    free(ctx);
    free(stack_alloc);
    return -1;
  }
  add_thread(slot, pid, stack_base, stack_alloc);
  return pid;
}

int thread_join(void)
{
  uint64 stackaddr = 0;
  int pid = join((uint64)&stackaddr);
  if (pid > 0)
  {
    if (remove_thread(pid, stackaddr) < 0)
      return -1;
    return pid;
  }
  return pid;
}

void printTCB(void)
{
  for (int i = 0; i < NTHREAD; i++)
    printf("TCB %d: %d\n",i,threads[i].used);
}
