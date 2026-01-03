#include "types.h"
#include "riscv.h"
#include "defs.h" //存放函数声明
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

int sh_var_for_sem_demo = 0; // 信号量；共享变量

uint64 sys_setPriority(void)
{
  int pid, priority;

  // 从用户栈中读取 pid 和 priority 参数
  if (argint(0, &pid) < 0 || argint(1, &priority) < 0)
  {
    return -1; // 参数读取失败，返回错误
  }

  // 调用 setPriority 函数设置进程优先级
  return setPriority(pid, priority);
}

uint64 sys_rt_set(void)
{
  int pid, period, runtime, deadline;
  if (argint(0, &pid) < 0 || argint(1, &period) < 0 ||
      argint(2, &runtime) < 0 || argint(3, &deadline) < 0)
  {
    return -1;
  }
  return rt_set(pid, period, runtime, deadline);
}

uint64 sys_rt_clear(void)
{
  int pid;
  if (argint(0, &pid) < 0)
  {
    return -1;
  }
  return rt_clear(pid);
}

uint64 sys_banker_init(void)
{
  int n;
  uint64 uarr;
  int total[BANKER_MAX_RES];

  if (argint(0, &n) < 0 || argaddr(1, &uarr) < 0)
    return -1;
  if (n <= 0 || n > BANKER_MAX_RES)
    return -1;
  if (copyin(myproc()->pagetable, (char *)total, uarr, n * sizeof(int)) < 0)
    return -1;
  return banker_init(n, total);
}

uint64 sys_banker_set_max(void)
{
  int n;
  uint64 uarr;
  int max[BANKER_MAX_RES];

  if (argint(0, &n) < 0 || argaddr(1, &uarr) < 0)
    return -1;
  if (n <= 0 || n > BANKER_MAX_RES)
    return -1;
  if (copyin(myproc()->pagetable, (char *)max, uarr, n * sizeof(int)) < 0)
    return -1;
  return banker_set_max(myproc(), n, max);
}

uint64 sys_banker_request(void)
{
  int n;
  uint64 uarr;
  int req[BANKER_MAX_RES];

  if (argint(0, &n) < 0 || argaddr(1, &uarr) < 0)
    return -1;
  if (n <= 0 || n > BANKER_MAX_RES)
    return -1;
  if (copyin(myproc()->pagetable, (char *)req, uarr, n * sizeof(int)) < 0)
    return -1;
  return banker_request(myproc(), n, req);
}

uint64 sys_banker_release(void)
{
  int n;
  uint64 uarr;
  int rel[BANKER_MAX_RES];

  if (argint(0, &n) < 0 || argaddr(1, &uarr) < 0)
    return -1;
  if (n <= 0 || n > BANKER_MAX_RES)
    return -1;
  if (copyin(myproc()->pagetable, (char *)rel, uarr, n * sizeof(int)) < 0)
    return -1;
  return banker_release(myproc(), n, rel);
}

uint64
sys_exit(void)
{
  int n;
  if (argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_gettid(void)
{
  return myproc()->pid;
}

uint64
sys_gettgid(void)
{
  struct proc *p = myproc();
  if (p->tgid != 0)
    return p->tgid;
  return p->pid;
}

uint64
sys_thread_exit(void)
{
  int status = 0;
  argint(0, &status);
  exit(status);
  return 0;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if (argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;

  struct proc *p = myproc();
  addr = p->sz;
  uint64 sz = p->sz;

  if (n > 0)
  {
    // 懒分配
    p->sz += n;
  }
  else if (sz + n > 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
    p->sz = sz;
  }
  else
  {
    return -1;
  }
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (myproc()->killed)
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_cps(void)
{
  return cps();
}

uint64 sys_trace(void)
{ // 为当前进程的trace_mask赋值
  uint64 n;
  if (argaddr(0, &n) < 0)
  { // n赋值为p->trapframe->a0，a0来自于进程用户空间，用与传参
    return -1;
  }
  myproc()->trace_mask = n; // trace_mask保存了a0的信息，用于调试
  return 0;
}

uint64 sys_sysinfo(void)
{
  struct sysinfo info;
  freebytes(&info.freemem);
  procnum(&info.nproc);

  // 获取虚拟地址
  uint64 dstaddr;
  argaddr(0, &dstaddr);

  // 从内核空间拷贝数据到用户空间
  if (copyout(myproc()->pagetable, dstaddr, (char *)&info, sizeof info) < 0)
    return -1;

  return 0;
}

uint64
sys_execve(void)
{
  char path[260], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;
  // 获取路径和参数地址
  if (argstr(0, path, 260) < 0 || argaddr(1, &uargv) < 0)
  {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  // 设置 argv[0] 为程序的路径
  argv[0] = path;
  // 从用户空间获取其他参数
  for (i = 1;; i++)
  {
    if (i >= NELEM(argv))
      goto bad;
    // 获取每个参数的地址
    if (fetchaddr(uargv + sizeof(uint64) * (i - 1), (uint64 *)&uarg) < 0)
      goto bad;
    // 如果参数为空，结束循环
    if (uarg == 0)
    {
      argv[i] = 0;
      break;
    }
    // 为参数分配内存
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    // 获取字符串内容
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }
  // 打印参数（调试用）
  // for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
  // {
  //   printf("argv[%d]: %s\n", i, argv[i]);
  // }
  // 调用 exec 执行程序
  // printf("%s", path);
  int ret = exec(path, argv);
  // 清理已分配的内存
  for (i = 1; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return ret;
bad:
  // 清理已分配的内存
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64 sys_getparentpid(void)
{
  struct proc *p = myproc();
  return p->parent->pid;
}

uint64 sys_print_pgtable(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  vmprint(p->pagetable);
  release(&p->lock);
  return 0;
}

// 信号量
int sys_sh_var_read()
{
  return sh_var_for_sem_demo;
}
// 信号量
int sys_sh_var_write()
{
  int n;
  if (argint(0, &n) < 0)
  {
    return -1;
  }
  sh_var_for_sem_demo = n;
  return sh_var_for_sem_demo;
}

// 信号量：创建信号量
int sys_sem_create()
{
  int n_sem, id;
  if (argint(0, &n_sem) < 0 || n_sem < 0) // 参数必须合法且非负
  {
    return -1;
  }

  for (id = 0; id < SEM_MAX_NUM; id++)
  {
    acquire(&sems[id].lock);
    if (sems[id].allocated == 0)
    {
      sems[id].allocated = 1;
      sems[id].resource_count = n_sem; // 分配资源
      sems[id].owner = 0;
      sem_used_count++;
      release(&sems[id].lock);
      return id; // 返回信号量索引
    }
    release(&sems[id].lock);
  }

  return -1; // 没有可用的信号量
}
// 信号量：释放信号量
int sys_sem_free()
{
  int id;
  if (argint(0, &id) < 0 || id < 0 || id >= SEM_MAX_NUM) // 检查参数范围
  {
    return -1;
  }

  acquire(&sems[id].lock);
  if (sems[id].allocated == 0)
  {
    release(&sems[id].lock);
    return -1;
  }
  if (sems[id].resource_count < 0 || sems[id].waiters > 0)
  {
    release(&sems[id].lock);
    return -1;
  }
  sems[id].allocated = 0;
  sems[id].resource_count = 0; // 清除资源计数
  sems[id].owner = 0;
  sem_used_count--;
  release(&sems[id].lock);

  return 0;
}
// 信号量：P操作，获取资源
int sys_sem_p()
{
  int id;
  if (argint(0, &id) < 0 || id < 0 || id >= SEM_MAX_NUM) // 参数合法性检查
  {
    return -1;
  }

  // printf("sem_p: 尝试获取信号量 id = %d\n", id);

  acquire(&sems[id].lock); // 获取信号量锁
  if (sems[id].allocated == 0)
  {
    release(&sems[id].lock);
    return -1;
  }

  int pid = myproc()->pid;
  sems[id].resource_count--;
  if (sems[id].resource_count < 0)
  {
    if (deadlock_detect(&sems[id]))
    {
      sems[id].resource_count++;
      release(&sems[id].lock);
      return -1;
    }
    sems[id].waiters++;
    sleep(&sems[id], &sems[id].lock); // 使用信号量锁进行休眠
    sems[id].waiters--;
  }
  if (sems[id].resource_count == 0)
  {
    sems[id].owner = pid;
  }
  else if (sems[id].resource_count > 0)
  {
    sems[id].owner = 0;
  }
  release(&sems[id].lock); // 释放信号量锁
  return 0;
}

// 信号量：P 操作（AND），一次获取多个信号量
int sys_sem_p_multi()
{
  int n;
  uint64 uids;
  int ids[SEM_MAX_NUM];

  if (argint(0, &n) < 0 || n <= 0 || n > SEM_MAX_NUM)
  {
    return -1;
  }
  if (argaddr(1, &uids) < 0)
  {
    return -1;
  }
  if (copyin(myproc()->pagetable, (char *)ids, uids, n * sizeof(int)) < 0)
  {
    return -1;
  }

  for (int i = 1; i < n; i++)
  {
    int key = ids[i];
    int j = i - 1;
    while (j >= 0 && ids[j] > key)
    {
      ids[j + 1] = ids[j];
      j--;
    }
    ids[j + 1] = key;
  }

  for (int i = 0; i < n; i++)
  {
    if (ids[i] < 0 || ids[i] >= SEM_MAX_NUM)
    {
      return -1;
    }
    if (i > 0 && ids[i] == ids[i - 1])
    {
      return -1;
    }
  }

  for (;;)
  {
    int wait_id = -1;
    int invalid = 0;

    for (int i = 0; i < n; i++)
    {
      acquire(&sems[ids[i]].lock);
    }
    for (int i = 0; i < n; i++)
    {
      struct sem *s = &sems[ids[i]];
      if (s->allocated == 0)
      {
        invalid = 1;
        break;
      }
      if (s->resource_count <= 0 && wait_id < 0)
      {
        wait_id = ids[i];
      }
    }

    if (invalid)
    {
      for (int i = 0; i < n; i++)
      {
        release(&sems[ids[i]].lock);
      }
      return -1;
    }
    if (wait_id < 0)
    {
      for (int i = 0; i < n; i++)
      {
        sems[ids[i]].resource_count--;
        if (sems[ids[i]].resource_count == 0)
          sems[ids[i]].owner = myproc()->pid;
        else if (sems[ids[i]].resource_count > 0)
          sems[ids[i]].owner = 0;
      }
      for (int i = 0; i < n; i++)
      {
        release(&sems[ids[i]].lock);
      }
      return 0;
    }

    if (deadlock_detect(&sems[wait_id]))
    {
      for (int i = 0; i < n; i++)
      {
        release(&sems[ids[i]].lock);
      }
      return -1;
    }

    for (int i = 0; i < n; i++)
    {
      sems[ids[i]].waiters++;
    }
    for (int i = 0; i < n; i++)
    {
      if (ids[i] != wait_id)
      {
        release(&sems[ids[i]].lock);
      }
    }
    sleep(&sems[wait_id], &sems[wait_id].lock);
    release(&sems[wait_id].lock);

    for (int i = 0; i < n; i++)
    {
      acquire(&sems[ids[i]].lock);
    }
    for (int i = 0; i < n; i++)
    {
      sems[ids[i]].waiters--;
    }
    for (int i = 0; i < n; i++)
    {
      release(&sems[ids[i]].lock);
    }
  }
}

// 信号量：V操作，释放资源
int sys_sem_v()
{
  int id;
  if (argint(0, &id) < 0 || id < 0 || id >= SEM_MAX_NUM) // 参数合法性检查
  {
    return -1;
  }

  // printf("sem_v: 尝试释放信号量 id = %d\n", id);

  acquire(&sems[id].lock); // 获取信号量锁
  if (sems[id].allocated == 0)
  {
    release(&sems[id].lock);
    return -1;
  }

  sems[id].resource_count++;
  sems[id].owner = 0;
  if (sems[id].resource_count <= 0 || sems[id].waiters > 0)
  {
    wakeupOneProc(&sems[id]); // 唤醒一个等待的进程
  }

  release(&sems[id].lock); // 释放信号量锁
  return 0;
}

int sys_semset_create()
{
  int n;
  uint64 uvals;
  int vals[SEMSET_MAX_SIZE];

  if (argint(0, &n) < 0 || n <= 0 || n > SEMSET_MAX_SIZE)
  {
    return -1;
  }
  if (argaddr(1, &uvals) < 0)
  {
    return -1;
  }

  if (uvals != 0)
  {
    if (copyin(myproc()->pagetable, (char *)vals, uvals, n * sizeof(int)) < 0)
    {
      return -1;
    }
    for (int i = 0; i < n; i++)
    {
      if (vals[i] < 0)
      {
        return -1;
      }
    }
  }
  else
  {
    for (int i = 0; i < n; i++)
    {
      vals[i] = 0;
    }
  }

  for (int id = 0; id < SEMSET_MAX_NUM; id++)
  {
    acquire(&semsets[id].lock);
    if (semsets[id].allocated == 0)
    {
      semsets[id].allocated = 1;
      semsets[id].count = n;
      semset_used_count++;
      for (int i = 0; i < n; i++)
      {
        semsets[id].sems[i].allocated = 1;
        semsets[id].sems[i].resource_count = vals[i];
        semsets[id].sems[i].waiters = 0;
        semsets[id].sems[i].owner = 0;
      }
      for (int i = n; i < SEMSET_MAX_SIZE; i++)
      {
        semsets[id].sems[i].allocated = 0;
        semsets[id].sems[i].resource_count = 0;
        semsets[id].sems[i].waiters = 0;
        semsets[id].sems[i].owner = 0;
      }
      release(&semsets[id].lock);
      return id;
    }
    release(&semsets[id].lock);
  }

  return -1;
}

int sys_semset_free()
{
  int id;
  if (argint(0, &id) < 0 || id < 0 || id >= SEMSET_MAX_NUM)
  {
    return -1;
  }

  struct semset *set = &semsets[id];
  acquire(&set->lock);
  if (set->allocated == 0)
  {
    release(&set->lock);
    return -1;
  }
  int n = set->count;
  release(&set->lock);

  for (int i = 0; i < n; i++)
  {
    acquire(&set->sems[i].lock);
    if (set->sems[i].allocated == 0 || set->sems[i].resource_count < 0 || set->sems[i].waiters > 0)
    {
      release(&set->sems[i].lock);
      return -1;
    }
    release(&set->sems[i].lock);
  }

  for (int i = 0; i < n; i++)
  {
    acquire(&set->sems[i].lock);
    set->sems[i].allocated = 0;
    set->sems[i].resource_count = 0;
    set->sems[i].waiters = 0;
    set->sems[i].owner = 0;
    release(&set->sems[i].lock);
  }

  acquire(&set->lock);
  set->allocated = 0;
  set->count = 0;
  semset_used_count--;
  release(&set->lock);
  return 0;
}

int sys_semset_p()
{
  int id, idx;
  if (argint(0, &id) < 0 || argint(1, &idx) < 0 || id < 0 || id >= SEMSET_MAX_NUM)
  {
    return -1;
  }

  struct semset *set = &semsets[id];
  acquire(&set->lock);
  if (set->allocated == 0 || idx < 0 || idx >= set->count)
  {
    release(&set->lock);
    return -1;
  }
  release(&set->lock);

  struct sem *s = &set->sems[idx];
  acquire(&s->lock);
  if (s->allocated == 0)
  {
    release(&s->lock);
    return -1;
  }

  s->resource_count--;
  if (s->resource_count < 0)
  {
    if (deadlock_detect(s))
    {
      s->resource_count++;
      release(&s->lock);
      return -1;
    }
    s->waiters++;
    sleep(s, &s->lock);
    s->waiters--;
  }
  if (s->resource_count == 0)
  {
    s->owner = myproc()->pid;
  }
  else if (s->resource_count > 0)
  {
    s->owner = 0;
  }
  release(&s->lock);
  return 0;
}

int sys_semset_v()
{
  int id, idx;
  if (argint(0, &id) < 0 || argint(1, &idx) < 0 || id < 0 || id >= SEMSET_MAX_NUM)
  {
    return -1;
  }

  struct semset *set = &semsets[id];
  acquire(&set->lock);
  if (set->allocated == 0 || idx < 0 || idx >= set->count)
  {
    release(&set->lock);
    return -1;
  }
  release(&set->lock);

  struct sem *s = &set->sems[idx];
  acquire(&s->lock);
  if (s->allocated == 0)
  {
    release(&s->lock);
    return -1;
  }

  s->resource_count++;
  s->owner = 0;
  if (s->resource_count <= 0 || s->waiters > 0)
  {
    wakeupOneProc(s);
  }
  release(&s->lock);
  return 0;
}

int sys_semset_p_multi()
{
  int id, n;
  uint64 uidxs;
  int idxs[SEMSET_MAX_SIZE];

  if (argint(0, &id) < 0 || argint(1, &n) < 0 || id < 0 || id >= SEMSET_MAX_NUM)
  {
    return -1;
  }
  if (n <= 0 || n > SEMSET_MAX_SIZE)
  {
    return -1;
  }
  if (argaddr(2, &uidxs) < 0)
  {
    return -1;
  }

  struct semset *set = &semsets[id];
  acquire(&set->lock);
  if (set->allocated == 0 || n > set->count)
  {
    release(&set->lock);
    return -1;
  }
  release(&set->lock);

  if (copyin(myproc()->pagetable, (char *)idxs, uidxs, n * sizeof(int)) < 0)
  {
    return -1;
  }

  for (int i = 1; i < n; i++)
  {
    int key = idxs[i];
    int j = i - 1;
    while (j >= 0 && idxs[j] > key)
    {
      idxs[j + 1] = idxs[j];
      j--;
    }
    idxs[j + 1] = key;
  }

  for (int i = 0; i < n; i++)
  {
    if (idxs[i] < 0 || idxs[i] >= set->count)
    {
      return -1;
    }
    if (i > 0 && idxs[i] == idxs[i - 1])
    {
      return -1;
    }
  }

  for (;;)
  {
    int wait_idx = -1;
    int invalid = 0;

    for (int i = 0; i < n; i++)
    {
      acquire(&set->sems[idxs[i]].lock);
    }
    for (int i = 0; i < n; i++)
    {
      struct sem *s = &set->sems[idxs[i]];
      if (s->allocated == 0)
      {
        invalid = 1;
        break;
      }
      if (s->resource_count <= 0 && wait_idx < 0)
      {
        wait_idx = idxs[i];
      }
    }

    if (invalid)
    {
      for (int i = 0; i < n; i++)
      {
        release(&set->sems[idxs[i]].lock);
      }
      return -1;
    }
    if (wait_idx < 0)
    {
      for (int i = 0; i < n; i++)
      {
        set->sems[idxs[i]].resource_count--;
        if (set->sems[idxs[i]].resource_count == 0)
          set->sems[idxs[i]].owner = myproc()->pid;
        else if (set->sems[idxs[i]].resource_count > 0)
          set->sems[idxs[i]].owner = 0;
      }
      for (int i = 0; i < n; i++)
      {
        release(&set->sems[idxs[i]].lock);
      }
      return 0;
    }

    if (deadlock_detect(&set->sems[wait_idx]))
    {
      for (int i = 0; i < n; i++)
      {
        release(&set->sems[idxs[i]].lock);
      }
      return -1;
    }

    for (int i = 0; i < n; i++)
    {
      set->sems[idxs[i]].waiters++;
    }
    for (int i = 0; i < n; i++)
    {
      if (idxs[i] != wait_idx)
      {
        release(&set->sems[idxs[i]].lock);
      }
    }
    sleep(&set->sems[wait_idx], &set->sems[wait_idx].lock);
    release(&set->sems[wait_idx].lock);

    for (int i = 0; i < n; i++)
    {
      acquire(&set->sems[idxs[i]].lock);
    }
    for (int i = 0; i < n; i++)
    {
      set->sems[idxs[i]].waiters--;
    }
    for (int i = 0; i < n; i++)
    {
      release(&set->sems[idxs[i]].lock);
    }
  }
}

int sys_mon_create()
{
  for (int id = 0; id < MONITOR_MAX_NUM; id++)
  {
    struct monitor *m = &monitors[id];
    acquire(&m->lock);
    if (m->allocated == 0)
    {
      m->allocated = 1;
      m->locked = 0;
      m->owner = 0;
      m->waiters = 0;
      m->pi_waiter_max = 0;
      for (int i = 0; i < MONITOR_COND_MAX; i++)
      {
        m->conds[i].allocated = 0;
        m->conds[i].waiters = 0;
      }
      release(&m->lock);
      return id;
    }
    release(&m->lock);
  }
  return -1;
}

int sys_mon_free()
{
  int id;
  if (argint(0, &id) < 0 || id < 0 || id >= MONITOR_MAX_NUM)
  {
    return -1;
  }

  struct monitor *m = &monitors[id];
  acquire(&m->lock);
  if (m->allocated == 0 || m->locked || m->waiters > 0)
  {
    release(&m->lock);
    return -1;
  }
  for (int i = 0; i < MONITOR_COND_MAX; i++)
  {
    if (m->conds[i].allocated || m->conds[i].waiters > 0)
    {
      release(&m->lock);
      return -1;
    }
  }
  m->allocated = 0;
  m->pi_waiter_max = 0;
  release(&m->lock);
  return 0;
}

int sys_mon_enter()
{
  int id;
  if (argint(0, &id) < 0 || id < 0 || id >= MONITOR_MAX_NUM)
  {
    return -1;
  }

  struct monitor *m = &monitors[id];
  int pid = myproc()->pid;

  acquire(&m->lock);
  if (m->allocated == 0 || m->owner == pid)
  {
    release(&m->lock);
    return -1;
  }
  while (m->locked)
  {
    if (myproc()->killed)
    {
      release(&m->lock);
      return -1;
    }
    if (deadlock_detect(m))
    {
      release(&m->lock);
      return -1;
    }
    int donor = myproc()->priority;
    if (myproc()->pi_boost > donor)
      donor = myproc()->pi_boost;
    if (donor > m->pi_waiter_max)
      m->pi_waiter_max = donor;
    if (m->owner > 0)
      pi_donate(m->owner, donor);
    m->waiters++;
    sleep(m, &m->lock);
    m->waiters--;
  }
  m->locked = 1;
  m->owner = pid;
  m->pi_waiter_max = 0;
  release(&m->lock);
  return 0;
}

int sys_mon_exit()
{
  int id;
  if (argint(0, &id) < 0 || id < 0 || id >= MONITOR_MAX_NUM)
  {
    return -1;
  }

  struct monitor *m = &monitors[id];
  int pid = myproc()->pid;

  acquire(&m->lock);
  if (m->allocated == 0 || m->owner != pid)
  {
    release(&m->lock);
    return -1;
  }
  m->locked = 0;
  m->owner = 0;
  m->pi_waiter_max = 0;
  wakeupOneProc(m);
  release(&m->lock);
  pi_recalc(pid);
  return 0;
}

int sys_cond_create()
{
  int mid;
  if (argint(0, &mid) < 0 || mid < 0 || mid >= MONITOR_MAX_NUM)
  {
    return -1;
  }

  struct monitor *m = &monitors[mid];
  acquire(&m->lock);
  if (m->allocated == 0)
  {
    release(&m->lock);
    return -1;
  }
  for (int i = 0; i < MONITOR_COND_MAX; i++)
  {
    if (m->conds[i].allocated == 0)
    {
      m->conds[i].allocated = 1;
      m->conds[i].waiters = 0;
      release(&m->lock);
      return i;
    }
  }
  release(&m->lock);
  return -1;
}

int sys_cond_free()
{
  int mid, cid;
  if (argint(0, &mid) < 0 || argint(1, &cid) < 0)
  {
    return -1;
  }
  if (mid < 0 || mid >= MONITOR_MAX_NUM || cid < 0 || cid >= MONITOR_COND_MAX)
  {
    return -1;
  }

  struct monitor *m = &monitors[mid];
  acquire(&m->lock);
  if (m->allocated == 0 || m->conds[cid].allocated == 0 || m->conds[cid].waiters > 0)
  {
    release(&m->lock);
    return -1;
  }
  m->conds[cid].allocated = 0;
  release(&m->lock);
  return 0;
}

int sys_cond_wait()
{
  int mid, cid;
  if (argint(0, &mid) < 0 || argint(1, &cid) < 0)
  {
    return -1;
  }
  if (mid < 0 || mid >= MONITOR_MAX_NUM || cid < 0 || cid >= MONITOR_COND_MAX)
  {
    return -1;
  }

  struct monitor *m = &monitors[mid];
  int pid = myproc()->pid;

  acquire(&m->lock);
  if (m->allocated == 0 || m->conds[cid].allocated == 0 || m->owner != pid)
  {
    release(&m->lock);
    return -1;
  }

  m->conds[cid].waiters++;
  m->locked = 0;
  m->owner = 0;
  wakeupOneProc(m);
  sleep(&m->conds[cid], &m->lock);
  m->conds[cid].waiters--;

  if (myproc()->killed)
  {
    release(&m->lock);
    return -1;
  }

  while (m->locked)
  {
    m->waiters++;
    sleep(m, &m->lock);
    m->waiters--;
    if (myproc()->killed)
    {
      release(&m->lock);
      return -1;
    }
  }
  m->locked = 1;
  m->owner = pid;
  release(&m->lock);
  return 0;
}

int sys_cond_signal()
{
  int mid, cid;
  if (argint(0, &mid) < 0 || argint(1, &cid) < 0)
  {
    return -1;
  }
  if (mid < 0 || mid >= MONITOR_MAX_NUM || cid < 0 || cid >= MONITOR_COND_MAX)
  {
    return -1;
  }

  struct monitor *m = &monitors[mid];
  int pid = myproc()->pid;

  acquire(&m->lock);
  if (m->allocated == 0 || m->conds[cid].allocated == 0 || m->owner != pid)
  {
    release(&m->lock);
    return -1;
  }
  if (m->conds[cid].waiters > 0)
  {
    wakeupOneProc(&m->conds[cid]);
  }
  release(&m->lock);
  return 0;
}

int sys_cond_broadcast()
{
  int mid, cid;
  if (argint(0, &mid) < 0 || argint(1, &cid) < 0)
  {
    return -1;
  }
  if (mid < 0 || mid >= MONITOR_MAX_NUM || cid < 0 || cid >= MONITOR_COND_MAX)
  {
    return -1;
  }

  struct monitor *m = &monitors[mid];
  int pid = myproc()->pid;

  acquire(&m->lock);
  if (m->allocated == 0 || m->conds[cid].allocated == 0 || m->owner != pid)
  {
    release(&m->lock);
    return -1;
  }
  if (m->conds[cid].waiters > 0)
  {
    wakeup(&m->conds[cid]);
  }
  release(&m->lock);
  return 0;
}
static struct proc *
dmsg_lock_target(int pid)
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED)
    {
      acquire(&p->dmsg_lock);
      release(&p->lock);
      if (p->dmsg_closed)
      {
        release(&p->dmsg_lock);
        return 0;
      }
      return p;
    }
    release(&p->lock);
  }
  return 0;
}

int sys_dmsgsend()
{
  int pid, len;
  uint64 uaddr;
  char kbuf[DMSG_MAX];

  if (argint(0, &pid) < 0 || argaddr(1, &uaddr) < 0 || argint(2, &len) < 0)
  {
    return -1;
  }
  if (pid <= 0 || len < 0 || len > DMSG_MAX)
  {
    return -1;
  }
  if (len > 0 && copyin(myproc()->pagetable, kbuf, uaddr, len) < 0)
  {
    return -1;
  }

  for (;;)
  {
    struct proc *p = dmsg_lock_target(pid);
    if (p == 0)
    {
      return -1;
    }
    while (p->dmsg_count >= DMSG_QUEUE_MAX)
    {
      sleep(&p->dmsg_count, &p->dmsg_lock);
      if (p->dmsg_closed)
      {
        release(&p->dmsg_lock);
        return -1;
      }
    }

    struct dmsg *m = (struct dmsg *)kalloc();
    if (m == 0)
    {
      release(&p->dmsg_lock);
      return -1;
    }
    m->next = 0;
    m->len = len;
    m->sender = myproc()->pid;
    if (len > 0)
    {
      memmove(m->data, kbuf, len);
    }
    if (p->dmsg_tail != 0)
    {
      p->dmsg_tail->next = m;
    }
    else
    {
      p->dmsg_head = m;
    }
    p->dmsg_tail = m;
    p->dmsg_count++;
    p->dmsg_bytes += len;
    wakeupOneProc(&p->dmsg_count);
    release(&p->dmsg_lock);
    return 0;
  }
}

int sys_dmsgrcv()
{
  uint64 uaddr;
  int len;
  struct proc *p = myproc();
  char kbuf[DMSG_MAX];

  if (argaddr(0, &uaddr) < 0 || argint(1, &len) < 0)
  {
    return -1;
  }
  if (len < 0 || len > DMSG_MAX)
  {
    return -1;
  }

  acquire(&p->dmsg_lock);
  while (p->dmsg_count == 0)
  {
    if (p->killed || p->dmsg_closed)
    {
      release(&p->dmsg_lock);
      return -1;
    }
    sleep(&p->dmsg_count, &p->dmsg_lock);
  }

  struct dmsg *m = p->dmsg_head;
  if (len < m->len)
  {
    release(&p->dmsg_lock);
    return -1;
  }
  int mlen = m->len;
  int sender = m->sender;
  if (mlen > 0)
  {
    memmove(kbuf, m->data, mlen);
  }

  p->dmsg_head = m->next;
  if (p->dmsg_head == 0)
  {
    p->dmsg_tail = 0;
  }
  p->dmsg_count--;
  p->dmsg_bytes -= mlen;
  wakeupOneProc(&p->dmsg_count);
  release(&p->dmsg_lock);
  kfree((void *)m);

  if (mlen > 0 &&
      copyout(p->pagetable, uaddr, kbuf, mlen) < 0)
  {
    return -1;
  }
  return sender;
}

uint64 sys_shmgetat(void)
{
  int key, num;
  if (argint(0, &key) < 0 || argint(1, &num) < 0)
    return -1;
  return (uint64)shmgetat(key, num);
}

int sys_shmrefcount(void)
{
  int key;
  if (argint(0, &key) < 0)
  {
    return -1;
  }
  return shmrefcount(key);
}
// sysproc.c
uint64 sys_sigalarm(void) {
  int n;
  uint64 fn;
  if(argint(0, &n) < 0)
    return -1;
  if(argaddr(1, &fn) < 0)
    return -1;
  
  return sigalarm(n, (void(*)())(fn));
}

uint64 sys_sigreturn(void) {
	return sigreturn();
}
int sys_clone(void)
{
  uint64 fcn;
  uint64 arg;
  uint64 stack;
  argaddr(0,&fcn);
  argaddr(1,&arg);
  argaddr(2,&stack);
  return clone(fcn,arg,stack);
}
int sys_join(void)
{
  uint64 stackaddr;
  argaddr(0,&stackaddr);
  return join(stackaddr);
}
