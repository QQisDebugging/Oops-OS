// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

#define MAX_SLEEPLOCKS (NINODE + NBUF)

static struct sleeplock *sleeplocks[MAX_SLEEPLOCKS];
static int sleeplock_count = 0;

static void
register_sleeplock(struct sleeplock *lk)
{
  if (sleeplock_count < MAX_SLEEPLOCKS) {
    sleeplocks[sleeplock_count++] = lk;
  } else {
    panic("sleeplock table overflow");
  }
}

int
sleeplock_max_waiter_for_pid(int pid)
{
  int max = 0;

  for (int i = 0; i < sleeplock_count; i++) {
    struct sleeplock *lk = sleeplocks[i];
    acquire(&lk->lk);
    if (lk->locked && lk->pid == pid && lk->pi_waiter_max > max)
      max = lk->pi_waiter_max;
    release(&lk->lk);
  }
  return max;
}

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
  lk->pi_waiter_max = 0;
  register_sleeplock(lk);
}

void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  while (lk->locked) {
    int donor = myproc()->priority;
    if (myproc()->pi_boost > donor)
      donor = myproc()->pi_boost;
    if (donor > lk->pi_waiter_max)
      lk->pi_waiter_max = donor;
    if (lk->pid > 0)
      pi_donate(lk->pid, donor);
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  lk->pi_waiter_max = 0;
  release(&lk->lk);
}

void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  int owner_pid = lk->pid;
  lk->locked = 0;
  lk->pid = 0;
  lk->pi_waiter_max = 0;
  wakeup(lk);
  release(&lk->lk);
  if (owner_pid > 0)
    pi_recalc(owner_pid);
}

int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  release(&lk->lk);
  return r;
}


