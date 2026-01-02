//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "buf.h"

static struct inode *create(char *path, char type, short major, short minor);
static struct inode *create_exclusive(char *path, char type, short major, short minor);

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if (argint(n, &fd) < 0)
    return -1;
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd] == 0)
    {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

// lseek - 改变文件读写偏移量
// 参数: fd - 文件描述符
//       offset - 偏移量
//       whence - 偏移基准 (SEEK_SET=0, SEEK_CUR=1, SEEK_END=2)
// 返回: 成功返回新的偏移量，失败返回-1
uint64
sys_lseek(void)
{
  struct file *f;
  int offset;
  int whence;
  int newoff;

  // 获取参数
  if (argfd(0, 0, &f) < 0 || argint(1, &offset) < 0 || argint(2, &whence) < 0)
    return -1;

  // 只支持普通文件 (FD_INODE 类型)
  if (f->type != FD_INODE)
    return -1;

  ilock(f->ip);

  // 根据 whence 计算新的偏移量
  switch (whence) {
    case 0:  // SEEK_SET: 从文件开头计算
      newoff = offset;
      break;
    case 1:  // SEEK_CUR: 从当前位置计算
      newoff = f->off + offset;
      break;
    case 2:  // SEEK_END: 从文件末尾计算
      newoff = f->ip->size + offset;
      break;
    default:
      iunlock(f->ip);
      return -1;
  }

  // 检查新偏移量是否有效（不能为负数）
  if (newoff < 0) {
    iunlock(f->ip);
    return -1;
  }

  // 设置新的偏移量
  f->off = newoff;

  iunlock(f->ip);
  return newoff;
}
// truncate - 按路径截断文件到指定长度
// 参数: path - 文件路径
//       length - 目标长度
// 返回: 成功返回0，失败返回-1
uint64
sys_truncate(void)
{
  char path[MAXPATH];
  int length;
  struct inode *ip;

  if (argstr(0, path, MAXPATH) < 0 || argint(1, &length) < 0)
    return -1;

  if (length < 0)
    return -1;

  if ((uint)length > MAXFILE * BSIZE)
    return -1;

  begin_op();
  if ((ip = namei(path)) == 0) {
    end_op();
    return -1;
  }

  ilock(ip);

  // 只支持普通文件
  if (ip->type != T_FILE) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  // 截断或扩展文件
  itrunc_to(ip, (uint)length);

  iunlockput(ip);
  end_op();
  return 0;
}

// ftruncate - 按文件描述符截断文件到指定长度
// 参数: fd - 文件描述符
//       length - 目标长度
// 返回: 成功返回0，失败返回-1
uint64
sys_ftruncate(void)
{
  struct file *f;
  int length;

  if (argfd(0, 0, &f) < 0 || argint(1, &length) < 0)
    return -1;

  if (length < 0)
    return -1;

  if ((uint)length > MAXFILE * BSIZE)
    return -1;

  // 只支持普通文件
  if (f->type != FD_INODE)
    return -1;

  // 检查是否可写
  if (f->writable == 0)
    return -1;

  begin_op();
  ilock(f->ip);

  // 只支持普通文件类型
  if (f->ip->type != T_FILE) {
    iunlock(f->ip);
    end_op();
    return -1;
  }

  // 截断或扩展文件（会释放多余数据块）
  itrunc_to(f->ip, (uint)length);

  // 如果当前偏移量超过新大小，调整偏移量
  if (f->off > (uint)length) {
    f->off = (uint)length;
  }

  iunlock(f->ip);
  end_op();
  return 0;
}

uint64
sys_fallocate(void)
{
  struct file *f;
  int size;
  int flags;

  if (argfd(0, 0, &f) < 0 || argint(1, &size) < 0 || argint(2, &flags) < 0)
    return -1;
  if (size < 0)
    return -1;
  if (flags & ~FALLOC_KEEP_SIZE)
    return -1;
  if (f->type != FD_INODE || f->ip->type != T_FILE)
    return -1;
  if (f->writable == 0)
    return -1;

  uint target = (uint)size;

  ilock(f->ip);
  uint cur_size = f->ip->size;
  iunlock(f->ip);
  if (target <= cur_size)
    return 0;

  uint startb = (cur_size + BSIZE - 1) / BSIZE;
  uint endb = (target + BSIZE - 1) / BSIZE;
  uint maxblocks = (MAXOPBLOCKS - 1 - 1 - 2) / 2;
  if (maxblocks < 1)
    maxblocks = 1;

  uint b = startb;
  while (b < endb)
  {
    uint chunk = endb - b;
    if (chunk > maxblocks)
      chunk = maxblocks;

    begin_op();
    ilock(f->ip);
    uint cur_start = (f->ip->size + BSIZE - 1) / BSIZE;
    if (cur_start > b)
      b = cur_start;
    if (b >= endb)
    {
      iunlock(f->ip);
      end_op();
      break;
    }
    if (chunk > endb - b)
      chunk = endb - b;
    uint newsize = target;
    uint block_end_size = (b + chunk) * BSIZE;
    if (block_end_size < newsize)
      newsize = block_end_size;
    if (falloc(f->ip, b, b + chunk, newsize, flags & FALLOC_KEEP_SIZE) < 0)
    {
      iunlock(f->ip);
      end_op();
      return -1;
    }
    iunlock(f->ip);
    end_op();
    b += chunk;
  }

  return 0;
}

uint64
sys_fclone(void)
{
  char src[MAXPATH], dst[MAXPATH];
  struct inode *ip_src, *ip_dst;

  if (argstr(0, src, MAXPATH) < 0 || argstr(1, dst, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip_src = namei(src)) == 0)
  {
    end_op();
    return -1;
  }
  ilock(ip_src);
  if (ip_src->type != T_FILE)
  {
    iunlockput(ip_src);
    end_op();
    return -1;
  }

  if ((ip_dst = create_exclusive(dst, T_FILE, 0, 0)) == 0)
  {
    iunlockput(ip_src);
    end_op();
    return -1;
  }

  if (iclone(ip_src, ip_dst) < 0)
  {
    iunlockput(ip_dst);
    iunlockput(ip_src);
    end_op();
    return -1;
  }

  iunlockput(ip_dst);
  iunlockput(ip_src);
  end_op();
  return 0;
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if (argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = namei(old)) == 0)
  {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0)
  {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de))
  {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

// Update directory's ".." entry to point to newparent.
// Caller must hold dir->lock.
static int
set_dotdot(struct inode *dir, uint newparent)
{
  struct dirent de;

  // ".." is the second dirent after "." in xv6-style directories.
  if(readi(dir, 0, (uint64)&de, sizeof(de), sizeof(de)) != sizeof(de))
    return -1;
  if(namecmp(de.name, "..") != 0)
    return -1;
  de.inum = newparent;
  if(writei(dir, 0, (uint64)&de, sizeof(de), sizeof(de)) != sizeof(de))
    return -1;
  return 0;
}

// Return 1 if start is within (or equal to) the directory subtree rooted at inum.
// start is treated as a directory inode (not necessarily locked).
static int
dir_is_descendant(struct inode *start, uint inum)
{
  struct inode *cur = idup(start);

  for(;;){
    struct inode *parent;
    uint curinum;

    ilock(cur);
    if(cur->inum == inum){
      iunlock(cur);
      iput(cur);
      return 1;
    }
    curinum = cur->inum;
    parent = dirlookup(cur, "..", 0);
    iunlock(cur);
    iput(cur);

    if(parent == 0)
      return 0;
    // Reached root (".." points to itself).
    if(parent->inum == curinum){
      iput(parent);
      return 0;
    }
    cur = parent;
  }
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0)
  {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip))
  {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR)
  {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

// rename - 重命名（或移动）路径。
// 说明：支持非目录项 rename；目录仅支持重命名/移动到一个不存在的目标名（不支持覆盖已有目录项）。
// 语义：
// - new 已存在且为非目录项：覆盖 new（原 inode nlink--）。
// - new 已存在且指向同一 inode：等价于 unlink(old)。
// - old 与 new 同目录且 new 不存在：就地修改目录项名称（不需要新槽位）。
uint64
sys_rename(void)
{
  char old[MAXPATH], new[MAXPATH];
  char oldname[DIRSIZ], newname[DIRSIZ];
  struct inode *olddp = 0, *newdp = 0;
  struct inode *ip = 0, *ip2 = 0;
  struct inode *ip_check = 0;
  struct dirent de;
  uint off_old = 0, off_new = 0;
  int ret = -1;
  int olddp_locked = 0;
  int newdp_locked = 0;
  int ip_locked = 0;
  int ip2_locked = 0;
  int same_parent = 0;
  int isdir = 0;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;
  if(strncmp(old, new, MAXPATH) == 0)
    return 0;

  begin_op();

  if((olddp = nameiparent(old, oldname)) == 0)
    goto out;
  if((newdp = nameiparent(new, newname)) == 0)
    goto out;

  // Cannot rename "." or "..".
  if(namecmp(oldname, ".") == 0 || namecmp(oldname, "..") == 0 ||
     namecmp(newname, ".") == 0 || namecmp(newname, "..") == 0)
    goto out;
  if(olddp->dev != newdp->dev)
    goto out;

  same_parent = (olddp == newdp);

  // If moving a directory across parents, forbid moving into its own subtree.
  // Do this check before taking multiple locks to avoid deadlocks.
  if(!same_parent){
    ip_check = namei(old);
    if(ip_check == 0)
      goto out;
    ilock(ip_check);
    isdir = (ip_check->type == T_DIR);
    iunlock(ip_check);
    if(isdir){
      if(dir_is_descendant(newdp, ip_check->inum)){
        iput(ip_check);
        ip_check = 0;
        goto out;
      }
    }
    iput(ip_check);
    ip_check = 0;
  }

  // Lock parent inodes in a stable order to avoid deadlock.
  if(same_parent){
    ilock(olddp);
    olddp_locked = 1;
  } else if(olddp->inum < newdp->inum){
    ilock(olddp);
    olddp_locked = 1;
    ilock(newdp);
    newdp_locked = 1;
  } else {
    ilock(newdp);
    newdp_locked = 1;
    ilock(olddp);
    olddp_locked = 1;
  }

  if((ip = dirlookup(olddp, oldname, &off_old)) == 0)
    goto out;
  ilock(ip);
  ip_locked = 1;
  isdir = (ip->type == T_DIR);

  ip2 = dirlookup(newdp, newname, &off_new);
  if(ip2){
    ilock(ip2);
    ip2_locked = 1;

    if(isdir){
      // Directory can only replace an empty directory.
      if(ip2->type != T_DIR)
        goto out;
      if(!isdirempty(ip2))
        goto out;
    } else {
      // File renames do not support overwriting a directory.
      if(ip2->type == T_DIR)
        goto out;
    }

    // If new already refers to the same inode, just unlink old.
    if(ip2->inum == ip->inum){
      memset(&de, 0, sizeof(de));
      if(writei(olddp, 0, (uint64)&de, off_old, sizeof(de)) != sizeof(de))
        panic("rename: clear old");
      ip->nlink--;
      iupdate(ip);
      ret = 0;
      goto out;
    }

    // Overwrite new's directory entry to point to ip.
    if(readi(newdp, 0, (uint64)&de, off_new, sizeof(de)) != sizeof(de))
      panic("rename: read new");
    de.inum = ip->inum;
    if(writei(newdp, 0, (uint64)&de, off_new, sizeof(de)) != sizeof(de))
      panic("rename: write new");

    // Drop the overwritten target inode link.
    if(ip2->nlink < 1)
      panic("rename: target nlink < 1");
    ip2->nlink--;
    iupdate(ip2);

    if(isdir){
      // Replacing an existing directory removes one directory from olddp.
      olddp->nlink--;
      iupdate(olddp);

      if(!same_parent){
        // When moving across parents, update "..".
        if(set_dotdot(ip, newdp->inum) < 0)
          goto out;
        iupdate(ip);
      }
    }
  } else {
    // new doesn't exist.
    if(same_parent){
      // Rename in-place within the same directory.
      if(readi(olddp, 0, (uint64)&de, off_old, sizeof(de)) != sizeof(de))
        panic("rename: read old");
      memmove(de.name, newname, DIRSIZ);
      if(writei(olddp, 0, (uint64)&de, off_old, sizeof(de)) != sizeof(de))
        panic("rename: write old");
      ret = 0;
      goto out;
    }
    // Different parent: need a new directory entry.
    if(dirlink(newdp, newname, ip->inum) < 0)
      goto out;

    if(isdir){
      // Moving a directory changes parent link counts and "..".
      newdp->nlink++;
      iupdate(newdp);
      olddp->nlink--;
      iupdate(olddp);

      if(set_dotdot(ip, newdp->inum) < 0)
        goto out;
      iupdate(ip);
    }
  }

  // Clear old directory entry.
  memset(&de, 0, sizeof(de));
  if(writei(olddp, 0, (uint64)&de, off_old, sizeof(de)) != sizeof(de))
    panic("rename: clear old");
  ret = 0;

out:
  if(ip2){
    if(ip2_locked)
      iunlock(ip2);
    iput(ip2);
  }
  if(ip_check)
    iput(ip_check);
  if(ip){
    if(ip_locked)
      iunlock(ip);
    iput(ip);
  }

  if(same_parent){
    if(olddp){
      if(olddp_locked)
        iunlock(olddp);
      iput(olddp);
    }
    if(newdp && newdp != olddp)
      iput(newdp);
  } else {
    if(olddp){
      if(olddp_locked)
        iunlock(olddp);
      iput(olddp);
    }
    if(newdp){
      if(newdp_locked)
        iunlock(newdp);
      iput(newdp);
    }
  }

  end_op();
  return ret;
}


static struct inode *create(char *path, char type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0)
  {
    
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }
  if ((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");
  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  ip->type=type;
  iupdate(ip);

  if (type == T_DIR)
  {              // Create . and .. entries.
    
    dp->nlink++; // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if (dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);
  
  return ip;
}

static struct inode *create_exclusive(char *path, char type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0)
  {
    iput(ip);
    iunlockput(dp);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0)
    panic("create_exclusive: ialloc");
  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  ip->type = type;
  iupdate(ip);

  if (dirlink(dp, name, ip->inum) < 0)
    panic("create_exclusive: dirlink");

  iunlockput(dp);
  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if ((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if (omode & O_CREATE)
  {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0)
    {
      end_op();
      return -1;
    }
  }
  else
  {
    if ((ip = namei(path)) == 0)
    {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY)
    {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV))
  {
    iunlockput(ip);
    end_op();
    return -1;
  }
  // 处理符号链接
  if(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)) {
    // 若符号链接指向的仍然是符号链接，则递归的跟随它
    // 直到找到真正指向的文件
    // 但深度不能超过MAX_SYMLINK_DEPTH
    for(int i = 0; i <MAX_SYMLINK_DEPTH; ++i) {
      // 读出符号链接指向的路径
      if(readi(ip, 0, (uint64)path, 0, MAXPATH) != MAXPATH) {
        iunlockput(ip);
        end_op();
        return -1;
      }
      iunlockput(ip);
      ip = namei(path);
      if(ip == 0) {
        end_op();
        return -1;
      }
      ilock(ip);
      if(ip->type != T_SYMLINK)
        break;
    }
    // 超过最大允许深度后仍然为符号链接，则返回错误
    if(ip->type == T_SYMLINK) {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }
  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0)
  {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE)
  {
    f->type = FD_DEVICE;
    f->major = ip->major;
  }
  else
  {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE)
  {
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0)
  {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if ((argstr(0, path, MAXPATH)) < 0 ||
      argint(1, &major) < 0 ||
      argint(2, &minor) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0)
  {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0)
  {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if (argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0)
  {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++)
  {
    if (i >= NELEM(argv))
    {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0)
    {
      goto bad;
    }
    if (uarg == 0)
    {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if (argaddr(0, &fdarray) < 0)
    return -1;
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0)
  {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0)
  {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

uint64
sys_mmap(void)
{
  uint64 addr;
  int length;
  int prot;
  int flags;
  int vfd;
  struct file *vfile;
  int offset;
  uint64 err = 0xffffffffffffffff;

  // 获取系统调用参数
  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &prot) < 0 ||
      argint(3, &flags) < 0 || argfd(4, &vfd, &vfile) < 0 || argint(5, &offset) < 0)
    return err;

  // 实验提示中假定addr和offset为0，简化程序可能发生的情况
  if (addr != 0 || offset != 0 || length < 0)
    return err;

  // 文件不可写则不允许拥有PROT_WRITE权限时映射为MAP_SHARED
  if (vfile->writable == 0 && (prot & PROT_WRITE) != 0 && flags == MAP_SHARED)
    return err;

  struct proc *p = myproc();
  // 没有足够的虚拟地址空间
  if (p->sz + length > MAXVA)
    return err;

  // 遍历查找未使用的VMA结构体
  for (int i = 0; i < NVMA; ++i)
  {
    if (p->vma[i].used == 0)
    {
      p->vma[i].used = 1;
      p->vma[i].addr = p->sz;
      p->vma[i].len = (uint64)length;
      p->vma[i].flags = flags;
      p->vma[i].prot = prot;
      p->vma[i].vfile = vfile;
      p->vma[i].vfd = vfd;
      p->vma[i].offset = (uint64)offset;
      p->vma[i].filesz = (uint64)length;
      if (vfile->type == FD_INODE)
      {
        ilock(vfile->ip);
        uint64 fsz = vfile->ip->size;
        iunlock(vfile->ip);
        if (p->vma[i].offset >= fsz)
          p->vma[i].filesz = 0;
        else if (p->vma[i].offset + p->vma[i].filesz > fsz)
          p->vma[i].filesz = fsz - p->vma[i].offset;
      }

      // 增加文件的引用计数
      filedup(vfile);

      p->sz += length;
      return p->vma[i].addr;
    }
  }

  return err;
}

uint64
sys_munmap(void)
{
  uint64 addr;
  int length;
  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0)
    return -1;

  int i;
  struct proc *p = myproc();
  for (i = 0; i < NVMA; ++i)
  {
    if (p->vma[i].used && p->vma[i].len >= length)
    {
      // 根据提示，munmap的地址范围只能是
      // 1. 起始位置
      if (p->vma[i].addr == addr)
      {
        p->vma[i].addr += length;
        p->vma[i].len -= length;
        break;
      }
      // 2. 结束位置
      if (addr + length == p->vma[i].addr + p->vma[i].len)
      {
        p->vma[i].len -= length;
        break;
      }
    }
  }
  if (i == NVMA)
    return -1;

  // 将MAP_SHARED页面写回文件系统
  if (p->vma[i].flags == MAP_SHARED && (p->vma[i].prot & PROT_WRITE) != 0)
  {
    filewrite(p->vma[i].vfile, addr, length);
  }

  // 判断此页面是否存在映射
  uvmunmap(p->pagetable, addr, length / PGSIZE, 1);

  // 当前VMA中全部映射都被取消
  if (p->vma[i].len == 0)
  {
    fileclose(p->vma[i].vfile);
    p->vma[i].used = 0;
  }

  return 0;
}
uint64
sys_symlink(void) {
  char target[MAXPATH], path[MAXPATH];
  struct inode* ip_path;

  if(argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0) {
    return -1;
  }
  begin_op();
  // 分配一个inode结点，create返回锁定的inode
  ip_path = create(path, T_SYMLINK, 0, 0);
  
  if(ip_path == 0) {
    end_op();
    return -1;
  }
  // 向inode数据块中写入target路径
  if(writei(ip_path, 0, (uint64)target, 0, MAXPATH) < MAXPATH) {
    iunlockput(ip_path);
    end_op();
    return -1;
  }

  iunlockput(ip_path);
  end_op();
  return 0;
}

uint64 sys_mkf(void) {
    char path[MAXPATH];  // 用于存储文件路径
    struct inode *ip;    // 用于返回创建的 inode
    // 获取系统调用参数：路径
    if (argstr(0, path, MAXPATH) < 0) {          
        return -1;  // 如果参数获取失败，返回错误
    }
    // 调用 create 函数创建文件
    begin_op();
    ip = create(path, T_FILE, 0, 0);
    
    // 如果文件创建失败，则返回错误
    if (ip == 0)
    {
      end_op();
      return -1;
    }
    // 如果创建成功，返回 inode 的编号
    end_op();
    return ip->inum;
}
int sys_connect(void)
{
  struct file *f;
  int fd;
  uint32 raddr;
  uint32 rport;
  uint32 lport;
  if (argint(0, (int*)&raddr) < 0 ||
      argint(1, (int*)&lport) < 0 ||
      argint(2, (int*)&rport) < 0) {
    return -1;
  }
  if(sockalloc(&f, raddr, lport, rport) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0){
    fileclose(f);
    return -1;
  }
  return fd;
}
int sys_chmod(void)
{
  char pathname[MAXPATH];
  int mode;
  struct inode*ip;
  
  if(argstr(0,pathname,MAXPATH)<0||argint(1,&mode)<0)
    return -1;
  begin_op();
  if((ip=namei(pathname))==0)
  {
    end_op();
    return -1;
  }
  
  ilock(ip);
  ip->mode=(char)mode;
  iupdate(ip);
  iunlock(ip);
  end_op();
  return 0;
}
int sys_geti()  //保存文件索引信息
{
  char pathname[MAXPATH];
  uint64 addrsout;
  uint addrsin[14];
  struct inode*ip;
  if(argstr(0,pathname,MAXPATH)<0||argaddr(1,&addrsout)<0) return -1;
  begin_op();
  if((ip=namei(pathname))==0)	// 得不到对应索引节点
  {
    end_op();
    return -1;
  }
  ilock(ip);	// 同步inode和dinode
  for(int i=0;i<13;i++)
    addrsin[i]=ip->addrs[i];	// 复制索引
  addrsin[13]=ip->size;
  iunlock(ip);
  end_op();
   // 将内核中的 addrsin 写入到用户空间的 addrsout
    if (copyout(myproc()->pagetable, addrsout, (char *)addrsin, sizeof(addrsin)) < 0) {
        return -1; // 如果写入失败，返回错误
    }
  return 0;
}

int sys_recoveri() //根据文件索引信息恢复文件
{
    uint blockno;  // 用户传入的块号（可能是直接块、间接块或二级间接块）
    uint64 bufout; // 用户缓冲区地址
    char bufin[BSIZE]; // 缓冲区大小
    struct buf *b;
    // 获取用户传入的参数
    if (argint(0, (int *)&blockno) < 0 || argaddr(1, &bufout) < 0) {
        return -1;
    }
    b = bread(1, blockno); // 直接读取块
    // 将块内容复制到用户缓冲区
    memmove(bufin, b->data, BSIZE);
    if (copyout(myproc()->pagetable, bufout, bufin, BSIZE) < 0) {
        brelse(b);
        return -1;
    }
    brelse(b);
    return 0; // 成功
}

/*
uint64 sys_getcwd(void) {
  uint64 addr;
  
  // 获取用户传入的地址参数，如果失败则返回-1
  if (argaddr(0, &addr) < 0)
    return -1;

  struct dirent *de = myproc()->cwd; // 获取当前进程的当前工作目录
  char path[FAT32_MAX_PATH];         // 用于存储路径的缓冲区
  char *s = path + FAT32_MAX_PATH - 1;  // 指向路径的末尾
  int len;
  
  // 初始化路径缓冲区
  *s = '\0';
  
  // 处理根目录（没有父目录的情况）
  if (de->parent == NULL) {
    s = "/";
  } else {
    // 遍历父目录，构建路径
    while (de->parent) {
      len = strlen(de->name);
      
      // 检查是否有足够的空间来存储目录名和斜杠
      s -= len;
      if (s <= path) {
        // 如果路径超出了缓冲区，返回-1，表示路径无法构造
        return -1;
      }

      // 将当前目录名复制到缓冲区
      strncpy(s, de->name, len);
      s -= 1; // 为目录名添加斜杠
      *s = '/';

      de = de->parent; // 移动到父目录
    }
  }

  // 检查是否提供了有效的地址，如果地址为0则分配内存
  if (addr == 0) {
    addr = (uint64)kalloc();
    if (addr == 0) {
      return -1; // 内存分配失败
    }

    mappages(myproc()->pagetable, addr, PGSIZE, addr, PTE_R | PTE_W);
  }

  // 将路径字符串从内核空间复制到用户空间
  if (copyout2(addr, s, strlen(s) + 1) < 0) {
    return -1; // 如果复制失败，返回-1
  }

  return addr; // 返回路径字符串的用户空间地址
}

*/
uint64
sys_getcwd(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;

  char *mp = "/";

  if (copyout(myproc()->pagetable, addr, mp, strlen(mp) + 1) < 0)
    return -1;
  // printf("[sys_getcwd] cwd: %s, cwd_len: %d, addr: %p\n", mp, strlen(mp) + 1, addr);
  return addr;
}

uint64 sys_dup_new(void) // 复制文件描述符到指定的新文件描述符的系统调用
{
    struct file *f; // 文件指针
    int newfd; // 新的文件描述符

    // 获取文件指针
    if(argfd(0, 0, &f) < 0) {
        return -1;
    }

    // 获取新文件描述符
    if(argint(1, &newfd) < 0 || newfd < 0 || newfd >= NOFILE) {
        return -1;
    }

    // 如果新文件描述符已占用，则先关闭它
    if (myproc()->ofile[newfd] != ((void*)0)) {
        fileclose(myproc()->ofile[newfd]);  // 关闭已占用的文件描述符
    }

    // 将新文件描述符指向文件指针
    if ((newfd = fdalloc(f)) < 0)
      return -1;
    filedup(f);  // 增加文件指针的引用计数

    return newfd; // 返回新的文件描述符
}
