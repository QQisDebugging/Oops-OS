//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

// 文件锁全局锁，用于保护 inode 的 flock 字段
struct spinlock flock_lock;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
  initlock(&flock_lock, "flock");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
  else if(ff.type == FD_SOCK){
    sockclose(ff.sock);
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((f->ip->mode&1)==0){//不可读
      iunlock(f->ip);
      printf("have no permission to read\n");
      return -1;  // 表示读操作失败
    }
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else if(f->type == FD_SOCK){
    r = sockread(f->sock, addr, n);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;
      begin_op();
      ilock(f->ip);
      if((f->ip->mode&2)==0){ //判断是否可写
        iunlock(f->ip);
        end_op();
        printf("have no permission to write\n");
        return -1;    //写操作失败
      }
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    ret = (i == n ? n : -1);
  } else if(f->type == FD_SOCK){
    ret = sockwrite(f->sock, addr, n);
  }
  else {
    panic("filewrite");
  }

  return ret;
}

// flock - 对文件加锁/解锁
// operation: LOCK_SH（共享锁）, LOCK_EX（排他锁）, LOCK_UN（解锁）
//            可与 LOCK_NB（非阻塞）组合使用
// 返回：成功返回 0，失败返回 -1
int
fileflock(struct file *f, int operation)
{
  struct inode *ip;
  int nonblock = operation & LOCK_NB;
  int op = operation & ~LOCK_NB;

  // 只支持对普通文件加锁
  if(f->type != FD_INODE)
    return -1;
  
  ip = f->ip;

  acquire(&flock_lock);

  if(op == LOCK_UN) {
    // 解锁操作
    if(ip->flock_type == LOCK_SH) {
      ip->flock_count--;
      if(ip->flock_count == 0) {
        ip->flock_type = 0;
        wakeup(&ip->flock_type);
      }
    } else if(ip->flock_type == LOCK_EX) {
      ip->flock_type = 0;
      ip->flock_count = 0;
      wakeup(&ip->flock_type);
    }
    release(&flock_lock);
    return 0;
  }

  if(op == LOCK_SH) {
    // 申请共享锁
    while(ip->flock_type == LOCK_EX) {
      if(nonblock) {
        release(&flock_lock);
        return -1;  // EWOULDBLOCK
      }
      sleep(&ip->flock_type, &flock_lock);
    }
    ip->flock_type = LOCK_SH;
    ip->flock_count++;
    release(&flock_lock);
    return 0;
  }

  if(op == LOCK_EX) {
    // 申请排他锁
    while(ip->flock_type != 0) {
      if(nonblock) {
        release(&flock_lock);
        return -1;  // EWOULDBLOCK
      }
      sleep(&ip->flock_type, &flock_lock);
    }
    ip->flock_type = LOCK_EX;
    ip->flock_count = 1;
    release(&flock_lock);
    return 0;
  }

  release(&flock_lock);
  return -1;  // 无效操作
}

