#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "file.h"
#include "defs.h"
#include "fcntl.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();
extern struct proc *initproc;

void trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void)
{
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  // set alarm fields
  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();
  uint64 cause = r_scause();
  if (cause == 8)
  {
    // system call

    if (p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  }
  else if ((which_dev = devintr()) != 0)
  {
    // ok
  }
  else if (cause == 12 || cause == 13 || cause == 15)
  {
    uint64 fault_va = r_stval(); // 获取出错的虚拟地址
    if (fault_va >= MAXVA)
    {
      p->killed = 1;
      if (p == initproc)
        printf("init killed: bad va=%p sepc=%p\n", fault_va, p->trapframe->epc);
    }
    else
    {
    int swapret = swapin(p->pagetable, PGROUNDDOWN(fault_va));
    if (swapret == 0)
    {
    }
    else if (swapret < 0)
    {
      p->killed = 1;
      if (p == initproc)
        printf("init killed: swapin failed va=%p sepc=%p\n", fault_va, p->trapframe->epc);
    }
    else if (cowpage(p->pagetable, fault_va) == 0)
    { // 如果是 COW 页异常
      if (fault_va >= p->sz || cowalloc(p->pagetable, PGROUNDDOWN(fault_va)) == 0)
      {
        p->killed = 1;
        if (p == initproc)
          printf("init killed: cowalloc failed va=%p sepc=%p\n", fault_va, p->trapframe->epc);
      }
    }
    else
    {           //  缺页异常（懒分配引起的）
      char *pa; // 分配的物理地址
      int mmret = mmap_handler(r_stval(), cause);
      if (mmret == 0)
      {
      } //  缺页异常（内存映射文件/按需装入引起的）
      else if (mmret < 0)
      {
        p->killed = 1;
        if (p == initproc)
          printf("init killed: mmap_handler failed va=%p sepc=%p\n", fault_va, p->trapframe->epc);
      }
      else if (PGROUNDUP(p->trapframe->sp) - 1 < fault_va && fault_va < p->sz && (pa = kalloc()) != 0)
      {
        memset(pa, 0, PGSIZE);
        if (mappages(p->pagetable, PGROUNDDOWN(fault_va), PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_X | PTE_U) != 0)
        {
          kfree(pa);
          p->killed = 1;
          if (p == initproc)
            printf("init killed: lazy map failed va=%p sepc=%p\n", fault_va, p->trapframe->epc);
        }
      }
      else
      {
        printf("usertrap(): out of memory!\n"); // 已经没有可分配的空闲页
        p->killed = 1;
        if (p == initproc)
          printf("init killed: out of memory va=%p sepc=%p\n", fault_va, p->trapframe->epc);
      }
    }
    }
  }
  else
  {
    printf("usertrap(): unexpected scause %p pid=%d\n", cause, p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if (p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2)
  {
    if (p->alarm_interval != 0)
    {
      if (--p->alarm_ticks <= 0)
      {
        if (!p->alarm_goingoff)
        {
          p->alarm_ticks = p->alarm_interval;
          // jump to execute alarm_handler
          *p->alarm_trapframe = *p->trapframe; // backup trapframe
          p->trapframe->epc = (uint64)p->alarm_handler;
          p->alarm_goingoff = 1;
        }
      }
    }
#if defined(SCHED_MLFQ)
    if (mlfq_tick())
      yield();
#else
    yield();
#endif
  }

  usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
  // set alarm fields
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0)
  {
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
  {
#if defined(SCHED_MLFQ)
    if (mlfq_tick())
      yield();
#else
    yield();
#endif
  }

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr()
{
#if defined(SCHED_MLFQ)
  uint now;
#endif
  acquire(&tickslock);
  ticks++;
#if defined(SCHED_MLFQ)
  now = ticks;
#endif
  wakeup(&ticks);
  release(&tickslock);
#if defined(SCHED_MLFQ)
  if (now % MLFQ_BOOST_TICKS == 0)
    mlfq_boost(now);
#endif

}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr()
{
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) &&
      (scause & 0xff) == 9)
  {
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();
    if (irq == UART0_IRQ)
    {
      uartintr();
    }
    else if (irq == VIRTIO0_IRQ)
    {
      virtio_disk_intr();
    }
    else if (irq == E1000_IRQ)
    {
      e1000_intr();
    }
    else if (irq)
    {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq)
      plic_complete(irq);

    return 1;
  }
  else if (scause == 0x8000000000000001L)
  {
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if (cpuid() == 0)
    {
      clockintr();
    }

    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  }
  else
  {
    return 0;
  }
}
/**
 * @brief mmap_handler 处理 mmap 惰性分配导致的页异常
 * @param va 页面故障虚拟地址
 * @param cause 页面故障原因
 * @return 0 success, 1 not handled, -1 failure
 */
int mmap_handler(uint64 va, int cause)
{
  int i;
  // set alarm fields
  struct proc *p = myproc();
  if (va >= MAXVA)
    return -1;
  // 根据地址查找属于哪一个 VMA
  for (i = 0; i < NVMA; ++i)
  {
    if (p->vma[i].used && p->vma[i].addr <= va && va <= p->vma[i].addr + p->vma[i].len - 1)
    {
      break;
    }
  }
  if (i == NVMA)
    return 1;

  int pte_flags = PTE_U;
  if (p->vma[i].prot & PROT_READ)
    pte_flags |= PTE_R;
  if (p->vma[i].prot & PROT_WRITE)
    pte_flags |= PTE_W;
  if (p->vma[i].prot & PROT_EXEC)
    pte_flags |= PTE_X;

  struct file *vf = p->vma[i].vfile;
  if (cause == 12 && (p->vma[i].prot & PROT_EXEC) == 0)
    return -1;
  if (cause == 13 && (p->vma[i].prot & PROT_READ) == 0)
    return -1;
  if (cause == 15 && (p->vma[i].prot & PROT_WRITE) == 0)
    return -1;
  if (vf == 0)
  {
    void *pa = kalloc();
    if (pa == 0)
      return -1;
    memset(pa, 0, PGSIZE);
    if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa, pte_flags) != 0)
    {
      kfree(pa);
      return -1;
    }
    return 0;
  }
  if (vf->readable == 0)
    return -1;
  if (cause == 15 && p->vma[i].flags == MAP_SHARED && vf->writable == 0)
    return -1;

  void *pa = kalloc();
  if (pa == 0)
    return -1;
  memset(pa, 0, PGSIZE);

  uint64 page_off = PGROUNDDOWN(va - p->vma[i].addr);
  uint64 offset = p->vma[i].offset + page_off;
  if (page_off < p->vma[i].filesz)
  {
    uint64 n = p->vma[i].filesz - page_off;
    if (n > PGSIZE)
      n = PGSIZE;
    ilock(vf->ip);
    (void)readi(vf->ip, 0, (uint64)pa, offset, n);
    iunlock(vf->ip);
  }

  if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa, pte_flags) != 0)
  {
    kfree(pa);
    return -1;
  }

  return 0;
}
int sigalarm(int ticks, void (*handler)())
{
  // set alarm fields
  struct proc *p = myproc();
  p->alarm_interval = ticks;
  p->alarm_handler = handler;
  p->alarm_ticks = ticks;
  return 0;
}

int sigreturn()
{
  // 将 trapframe 恢复到时钟中断之前的状态，恢复原本正在执行的程序流
  // set alarm fields
  struct proc *p = myproc();
  *p->trapframe = *p->alarm_trapframe;
  p->alarm_goingoff = 0;
  return 0;
}
