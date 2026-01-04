#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
// 实现安全的参数读取。
int fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if (addr >= p->sz || addr + sizeof(uint64) > p->sz)
    return -1;
  if (copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
// 实现安全的字符串参数读取。
int fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if (addr >= KERNBASE)
    return -1;
  if (addr >= p->sz)
  {
    uint64 shm = (uint64)p->shm;
    if (!(addr >= shm && addr < KERNBASE))
      return -1;
  }
  int err = copyinstr(p->pagetable, buf, addr, max);
  if (err < 0)
    return err;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n)
  {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
int argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  if (*ip == 0)
    return 0;
  struct proc *p = myproc();

  // handle lazy allocation or demand-loaded VMA
  if (walkaddr(p->pagetable, *ip) == 0)
  {
    int mmret = mmap_handler(*ip, 13);
    if (mmret == 0)
      return 0;
    if (mmret < 0)
      return -1;
    if (PGROUNDUP(p->trapframe->sp) - 1 < *ip && *ip < p->sz)
    {
      char *pa = kalloc();
      if (pa == 0)
        return -1;
      memset(pa, 0, PGSIZE);

      if (mappages(p->pagetable, PGROUNDDOWN(*ip), PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_X | PTE_U) != 0)
      {
        kfree(pa);
        return -1;
      }
    }
    else
    {
      return -1;
    }
  }
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int argstr(int n, char *buf, int max)
{
  uint64 addr;
  if (argaddr(n, &addr) < 0)
    return -1;
  
  return fetchstr(addr, buf, max);
}

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_fsinfo(void);
extern uint64 sys_getpid(void);
extern uint64 sys_gettid(void);
extern uint64 sys_gettgid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_cps(void);
extern uint64 sys_trace(void);
extern uint64 sys_sysinfo(void);
extern uint64 sys_setPriority(void);
extern uint64 sys_execve(void);
extern uint64 sys_getparentpid(void);
extern uint64 sys_print_pgtable(void);
extern uint64 sys_mmap(void);
extern uint64 sys_munmap(void);
extern uint64 sys_sh_var_read(void);
extern uint64 sys_sh_var_write(void);
extern uint64 sys_sem_create(void);
extern uint64 sys_sem_free(void);
extern uint64 sys_sem_p(void);
extern uint64 sys_sem_v(void);
extern uint64 sys_sem_p_multi(void);
extern uint64 sys_semset_create(void);
extern uint64 sys_semset_free(void);
extern uint64 sys_semset_p(void);
extern uint64 sys_semset_v(void);
extern uint64 sys_semset_p_multi(void);
extern uint64 sys_dmsgsend(void);
extern uint64 sys_dmsgrcv(void);
extern uint64 sys_symlink(void);
extern uint64 sys_mkf(void);
extern uint64 sys_shmgetat(void); // 共享内存
extern uint64 sys_shmrefcount(void); // 共享内存
extern uint64 sys_mqget(void);
extern uint64 sys_msgsnd(void);
extern uint64 sys_msgrcv(void);
extern uint64 sys_getcwd(void);
extern uint64 sys_dup_new(void);
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);
extern uint64 sys_connect(void);
extern uint64 sys_chmod(void);
extern uint64 sys_geti(void);
extern uint64 sys_recoveri(void);
extern uint64 sys_clone(void);
extern uint64 sys_join(void);
extern uint64 sys_thread_exit(void);
extern uint64 sys_mon_create(void);
extern uint64 sys_mon_free(void);
extern uint64 sys_mon_enter(void);
extern uint64 sys_mon_exit(void);
extern uint64 sys_cond_create(void);
extern uint64 sys_cond_free(void);
extern uint64 sys_cond_wait(void);
extern uint64 sys_cond_signal(void);
extern uint64 sys_cond_broadcast(void);
extern uint64 sys_fallocate(void);
extern uint64 sys_fclone(void);
extern uint64 sys_fclonerange(void);
extern uint64 sys_lseek(void);
extern uint64 sys_truncate(void);
extern uint64 sys_ftruncate(void);
extern uint64 sys_rename(void);
extern uint64 sys_dedup(void);
extern uint64 sys_rt_set(void);
extern uint64 sys_rt_clear(void);
extern uint64 sys_flock(void);
extern uint64 sys_fsync(void);
extern uint64 sys_fdatasync(void);
extern uint64 sys_setxattr(void);
extern uint64 sys_getxattr(void);
extern uint64 sys_listxattr(void);
extern uint64 sys_removexattr(void);
extern uint64 sys_pread(void);
extern uint64 sys_pwrite(void);
extern uint64 sys_dup2(void);
extern uint64 sys_readv(void);
extern uint64 sys_writev(void);
extern uint64 sys_access(void);
static uint64 (*syscalls[])(void) = {
    [SYS_fork] sys_fork,
    [SYS_exit] sys_exit,
    [SYS_wait] sys_wait,
    [SYS_pipe] sys_pipe,
    [SYS_read] sys_read,
    [SYS_kill] sys_kill,
    [SYS_exec] sys_exec,
    [SYS_fstat] sys_fstat,
    [SYS_fsinfo] sys_fsinfo,
    [SYS_chdir] sys_chdir,
    [SYS_dup] sys_dup,
    [SYS_getpid] sys_getpid,
    [SYS_gettid] sys_gettid,
    [SYS_gettgid] sys_gettgid,
    [SYS_sbrk] sys_sbrk,
    [SYS_sleep] sys_sleep,
    [SYS_uptime] sys_uptime,
    [SYS_open] sys_open,
    [SYS_write] sys_write,
    [SYS_mknod] sys_mknod,
    [SYS_unlink] sys_unlink,
    [SYS_link] sys_link,
    [SYS_mkdir] sys_mkdir,
    [SYS_close] sys_close,
    [SYS_cps] sys_cps,
    [SYS_trace] sys_trace,
    [SYS_sysinfo] sys_sysinfo,
    [SYS_setPriority] sys_setPriority,
    [SYS_execve] sys_execve,
    [SYS_getparentpid] sys_getparentpid,
    [SYS_print_pgtable] sys_print_pgtable,
    [SYS_mmap] sys_mmap,
    [SYS_munmap] sys_munmap,
    [SYS_sh_var_read] sys_sh_var_read,
    [SYS_sh_var_write] sys_sh_var_write,
    [SYS_sem_create] sys_sem_create,
    [SYS_sem_free] sys_sem_free,
    [SYS_sem_p] sys_sem_p,
    [SYS_sem_v] sys_sem_v,
    [SYS_sem_p_multi] sys_sem_p_multi,
    [SYS_semset_create] sys_semset_create,
    [SYS_semset_free] sys_semset_free,
    [SYS_semset_p] sys_semset_p,
    [SYS_semset_v] sys_semset_v,
    [SYS_semset_p_multi] sys_semset_p_multi,
    [SYS_dmsgsend] sys_dmsgsend,
    [SYS_dmsgrcv] sys_dmsgrcv,
    [SYS_symlink] sys_symlink,
    [SYS_mkf] sys_mkf,
    [SYS_shmgetat] sys_shmgetat,
    [SYS_shmrefcount] sys_shmrefcount,
    [SYS_getcwd] sys_getcwd,
    [SYS_dup_new] sys_dup_new,
    [SYS_sigalarm] sys_sigalarm,
    [SYS_sigreturn] sys_sigreturn,
    [SYS_connect] sys_connect,
    [SYS_mqget] sys_mqget,
    [SYS_msgsnd] sys_msgsnd,
    [SYS_msgrcv] sys_msgrcv,
    [SYS_chmod] sys_chmod,
    [SYS_geti] sys_geti,
    [SYS_recoveri] sys_recoveri,
    [SYS_clone] sys_clone,
    [SYS_join] sys_join,
    [SYS_thread_exit] sys_thread_exit,
    [SYS_mon_create] sys_mon_create,
    [SYS_mon_free] sys_mon_free,
    [SYS_mon_enter] sys_mon_enter,
    [SYS_mon_exit] sys_mon_exit,
    [SYS_cond_create] sys_cond_create,
    [SYS_cond_free] sys_cond_free,
    [SYS_cond_wait] sys_cond_wait,
    [SYS_cond_signal] sys_cond_signal,
    [SYS_cond_broadcast] sys_cond_broadcast,
    [SYS_fallocate] sys_fallocate,
    [SYS_fclone] sys_fclone,
    [SYS_fclonerange] sys_fclonerange,
    [SYS_lseek] sys_lseek,
    [SYS_truncate] sys_truncate,
    [SYS_ftruncate] sys_ftruncate,
    [SYS_rename] sys_rename,
    [SYS_dedup] sys_dedup,
    [SYS_rt_set] sys_rt_set,
    [SYS_rt_clear] sys_rt_clear,
    [SYS_flock] sys_flock,
    [SYS_fsync] sys_fsync,
    [SYS_fdatasync] sys_fdatasync,
    [SYS_setxattr] sys_setxattr,
    [SYS_getxattr] sys_getxattr,
    [SYS_listxattr] sys_listxattr,
    [SYS_removexattr] sys_removexattr,
    [SYS_pread] sys_pread,
    [SYS_pwrite] sys_pwrite,
    [SYS_dup2] sys_dup2,
    [SYS_readv] sys_readv,
    [SYS_writev] sys_writev,
    [SYS_access] sys_access,
};
static char *syscall_names[] = {
    [SYS_fork] "fork",
    [SYS_exit] "exit",
    [SYS_wait] "wait",
    [SYS_pipe] "pipe",
    [SYS_read] "read",
    [SYS_kill] "kill",
    [SYS_exec] "exec",
    [SYS_fstat] "fstat",
    [SYS_fsinfo] "sys_fsinfo",
    [SYS_chdir] "chdir",
    [SYS_dup] "dup",
    [SYS_getpid] "getpid",
    [SYS_gettid] "gettid",
    [SYS_gettgid] "gettgid",
    [SYS_sbrk] "sbrk",
    [SYS_sleep] "sleep",
    [SYS_uptime] "uptime",
    [SYS_open] "open",
    [SYS_write] "write",
    [SYS_mknod] "mknod",
    [SYS_unlink] "unlink",
    [SYS_link] "link",
    [SYS_mkdir] "mkdir",
    [SYS_close] "close",
    [SYS_cps] "sys_cps",
    [SYS_trace] "trace",
    [SYS_sysinfo] "sys_sysinfo",
    [SYS_setPriority] "setPriority",
    [SYS_execve] "sys_execve",
    [SYS_getparentpid] "sys_getparentpid",
    [SYS_print_pgtable] "sys_print_pgtable",
    [SYS_mmap] "sys_mmap",
    [SYS_munmap] "sys_munmap",
    [SYS_sh_var_read] "sys_sh_var_read",
    [SYS_sh_var_write] "sys_sh_var_write",
    [SYS_sem_create] "sys_sem_create",
    [SYS_sem_free] "sys_sem_free",
    [SYS_sem_p] "sys_sem_p",
    [SYS_sem_v] "sys_sem_v",
    [SYS_sem_p_multi] "sys_sem_p_multi",
    [SYS_semset_create] "sys_semset_create",
    [SYS_semset_free] "sys_semset_free",
    [SYS_semset_p] "sys_semset_p",
    [SYS_semset_v] "sys_semset_v",
    [SYS_semset_p_multi] "sys_semset_p_multi",
    [SYS_dmsgsend] "sys_dmsgsend",
    [SYS_dmsgrcv] "sys_dmsgrcv",
    [SYS_symlink] "sys_symlink",
    [SYS_mkf] "sys_mkf",
    [SYS_shmgetat] "sys_shmgetat",
    [SYS_shmrefcount] "sys_shmrefcount",
    [SYS_getcwd] "sys_getcwd",
    [SYS_dup_new] "sys_dup_new",
    [SYS_sigalarm] "sys_sigalarm",
    [SYS_sigreturn] "sys_sigreturn",
    [SYS_connect] "sys_connect",
    [SYS_mqget] "sys_mqget",
    [SYS_msgsnd] "sys_msgsnd",
    [SYS_msgrcv] "sys_msgrcv",
    [SYS_chmod] "sys_chmod",
    [SYS_geti] "sys_geti",
    [SYS_recoveri] "sys_recoveri",
    [SYS_clone] "sys_clone",
    [SYS_join] "sys_join",
    [SYS_thread_exit] "sys_thread_exit",
    [SYS_mon_create] "sys_mon_create",
    [SYS_mon_free] "sys_mon_free",
    [SYS_mon_enter] "sys_mon_enter",
    [SYS_mon_exit] "sys_mon_exit",
    [SYS_cond_create] "sys_cond_create",
    [SYS_cond_free] "sys_cond_free",
    [SYS_cond_wait] "sys_cond_wait",
    [SYS_cond_signal] "sys_cond_signal",
    [SYS_cond_broadcast] "sys_cond_broadcast",
    [SYS_fallocate] "sys_fallocate",
    [SYS_fclone] "sys_fclone",
    [SYS_fclonerange] "sys_fclonerange",
    [SYS_lseek] "sys_lseek",
    [SYS_truncate] "sys_truncate",
    [SYS_ftruncate] "sys_ftruncate",
    [SYS_rename] "sys_rename",
    [SYS_dedup] "sys_dedup",
    [SYS_rt_set] "sys_rt_set",
    [SYS_rt_clear] "sys_rt_clear",
    [SYS_flock] "sys_flock",
    [SYS_fsync] "sys_fsync",
    [SYS_fdatasync] "sys_fdatasync",
    [SYS_setxattr] "sys_setxattr",
    [SYS_getxattr] "sys_getxattr",
    [SYS_listxattr] "sys_listxattr",
    [SYS_removexattr] "sys_removexattr",
    [SYS_pread] "sys_pread",
    [SYS_pwrite] "sys_pwrite",
    [SYS_dup2] "sys_dup2",
    [SYS_readv] "sys_readv",
    [SYS_writev] "sys_writev",
    [SYS_access] "sys_access",
};
void syscall(void)
{
  int num;
  struct proc *p = myproc();
  char *syscall_name;

  num = p->trapframe->a7;
  if (num > 0 && num < NELEM(syscalls) && syscalls[num])
  {
    p->trapframe->a0 = syscalls[num]();
    if (num < 64 && (p->trace_mask & (1ULL << num)) != 0)
    {
      syscall_name = syscall_names[num];
      printf("%d: syscall %s -> %d", p->pid, syscall_name, p->trapframe->a0);
    }
  }
  else
  {
    printf("%d %s: unknown sys call %d\n",
           p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}

