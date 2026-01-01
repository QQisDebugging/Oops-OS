#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "file.h"
#include "fcntl.h"

int exec(char *path, char **argv)
{
  // printf("%s\n%s", path, argv[1]);
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG + 1], stackbase;
  uint64 sz1;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  struct vm_area newvma[NVMA];
  struct file *vf = 0;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  if ((ip = namei(path)) == 0)
  {
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if (elf.magic != ELF_MAGIC)
    goto bad;

  if ((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  memset(newvma, 0, sizeof(newvma));
  if ((vf = filealloc()) == 0)
    goto bad;
  vf->type = FD_INODE;
  vf->readable = 1;
  vf->writable = 0;
  vf->ip = idup(ip);
  vf->off = 0;

  // Load program into memory.
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph))
  {
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      goto bad;
    if (ph.memsz == 0)
      continue;
    if (ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if (ph.vaddr % PGSIZE != 0)
      goto bad;
    if (ph.memsz > 0x7fffffff)
      goto bad;

    uint64 segend = ph.vaddr + ph.memsz;
    if (segend > sz)
      sz = segend;

    int vma_idx = -1;
    for (int j = 0; j < NVMA; j++)
    {
      if (newvma[j].used == 0)
      {
        vma_idx = j;
        break;
      }
    }
    if (vma_idx < 0)
      goto bad;

    newvma[vma_idx].used = 1;
    newvma[vma_idx].addr = ph.vaddr;
    newvma[vma_idx].len = PGROUNDUP(ph.memsz);
    newvma[vma_idx].prot = 0;
    if (ph.flags & ELF_PROG_FLAG_READ)
      newvma[vma_idx].prot |= PROT_READ;
    if (ph.flags & ELF_PROG_FLAG_WRITE)
      newvma[vma_idx].prot |= PROT_WRITE;
    if (ph.flags & ELF_PROG_FLAG_EXEC)
      newvma[vma_idx].prot |= PROT_EXEC;
    newvma[vma_idx].flags = MAP_PRIVATE;
    newvma[vma_idx].vfile = vf;
    newvma[vma_idx].vfd = -1;
    newvma[vma_idx].offset = ph.off;
    newvma[vma_idx].filesz = ph.filesz;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate two pages at the next page boundary.
  // Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if ((sz1 = uvmalloc(pagetable, sz, sz + 2 * PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz - 2 * PGSIZE);
  sp = sz;
  stackbase = sp - PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for (argc = 0; argv[argc]; argc++)
  {
    if (argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if (sp < stackbase)
      goto bad;
    if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase)
    goto bad;
  if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  safestrcpy(p->name, last, sizeof(p->name));

  // Clear existing VMAs before committing the new image.
  for (i = 0; i < NVMA; i++)
  {
    if (p->vma[i].used)
    {
      if (p->vma[i].flags == MAP_SHARED && (p->vma[i].prot & PROT_WRITE) != 0)
      {
        filewrite(p->vma[i].vfile, p->vma[i].addr, p->vma[i].len);
      }
      fileclose(p->vma[i].vfile);
      uvmunmap(p->pagetable, p->vma[i].addr, p->vma[i].len / PGSIZE, 1);
      p->vma[i].used = 0;
    }
  }
  memset(p->vma, 0, sizeof(p->vma));

  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;                         // initial program counter = main
  p->trapframe->sp = sp;                                 // initial stack pointer
  memmove(p->vma, newvma, sizeof(newvma));
  if (vf)
  {
    int first_vma = 1;
    for (i = 0; i < NVMA; i++)
    {
      if (p->vma[i].used)
      {
        if (first_vma)
        {
          p->vma[i].vfile = vf;
          first_vma = 0;
        }
        else
        {
          p->vma[i].vfile = filedup(vf);
        }
      }
    }
    if (first_vma)
    {
      fileclose(vf);
      vf = 0;
    }
  }
  shmrelease(oldpagetable, p->shm, p->shmkeymask); // 回收共享内存
  proc_freepagetable(oldpagetable, oldsz);
  p->shm = KERNBASE; // 重置虚拟内存信息
  p->shmkeymask = 0;
  releasemq(p->mqmask);
  p->mqmask = 0;
  return argc; // this ends up in a0, the first argument to main(argc, argv)

bad:
  if (vf)
    fileclose(vf);
  if (pagetable)
    proc_freepagetable(pagetable, sz);
  if (ip)
  {
    iunlockput(ip);
    end_op();
  }
  return -1;
}
