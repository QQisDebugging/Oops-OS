// vfs.c - 虚拟文件系统核心实现
// 提供统一的文件系统抽象层，支持多种文件系统挂载

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "vfs.h"
#include "fat.h"

// 挂载点表
static struct vfs_mount mounts[MAX_MOUNTS];
static struct spinlock mount_lock;

// 已注册的文件系统操作
static struct fs_operations *fs_types[8];
static int fs_count = 0;

// 注册文件系统类型
static void
vfs_register_fs(struct fs_operations *ops, int type)
{
  if (type < 8) {
    fs_types[type] = ops;
    fs_count++;
  }
}

// xv6 VFS 适配器
static struct vfs_inode*
xv6_vfs_iget(struct vfs_mount *mnt, uint inum)
{
  struct inode *ip = iget(mnt->dev, inum);
  if (ip == 0)
    return 0;
  
  // 将 xv6 inode 包装为 vfs_inode
  // 注意：这里简化处理，直接返回转换后的指针
  // 实际实现需要维护 vfs_inode 池
  struct vfs_inode *vip = (struct vfs_inode *)ip;
  vip->mnt = mnt;
  return vip;
}

static void
xv6_vfs_iput(struct vfs_inode *ip)
{
  iput((struct inode *)ip->fs_data);
}

static void
xv6_vfs_ilock(struct vfs_inode *ip)
{
  ilock((struct inode *)ip->fs_data);
}

static void
xv6_vfs_iunlock(struct vfs_inode *ip)
{
  iunlock((struct inode *)ip->fs_data);
}

static int
xv6_vfs_readi(struct vfs_inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  return readi((struct inode *)ip->fs_data, user_dst, dst, off, n);
}

static int
xv6_vfs_writei(struct vfs_inode *ip, int user_src, uint64 src, uint off, uint n)
{
  return writei((struct inode *)ip->fs_data, user_src, src, off, n);
}

static struct vfs_inode*
xv6_vfs_dirlookup(struct vfs_inode *dp, char *name, uint *poff)
{
  struct inode *ip = dirlookup((struct inode *)dp->fs_data, name, poff);
  if (ip == 0)
    return 0;
  struct vfs_inode *vip = (struct vfs_inode *)ip;
  vip->mnt = dp->mnt;
  return vip;
}

static int
xv6_vfs_mount(struct vfs_mount *mnt, int dev)
{
  // xv6 文件系统已在启动时初始化，这里只需设置设备号
  mnt->dev = dev;
  return 0;
}

static int
xv6_vfs_unmount(struct vfs_mount *mnt)
{
  // xv6 根文件系统不能卸载
  return -1;
}

static struct fs_operations xv6_ops = {
  .name = "xv6fs",
  .mount = xv6_vfs_mount,
  .unmount = xv6_vfs_unmount,
  .iget = xv6_vfs_iget,
  .iput = xv6_vfs_iput,
  .ilock = xv6_vfs_ilock,
  .iunlock = xv6_vfs_iunlock,
  .readi = xv6_vfs_readi,
  .writei = xv6_vfs_writei,
  .dirlookup = xv6_vfs_dirlookup,
};

// FAT16 文件系统操作
static struct fs_operations fat_ops = {
  .name = "fat16",
  .mount = fat_mount,
  .unmount = fat_unmount,
  .iget = fat_iget,
  .iput = fat_iput,
  .ilock = fat_ilock,
  .iunlock = fat_iunlock,
  .readi = fat_readi,
  .writei = fat_writei,
  .dirlookup = fat_dirlookup,
  .dirlink = fat_dirlink,
  .create = fat_create,
  .unlink = fat_unlink,
  .namei = fat_namei,
};

// 初始化 VFS
void
vfs_init(void)
{
  initlock(&mount_lock, "vfs_mount");
  
  // 清空挂载表
  for (int i = 0; i < MAX_MOUNTS; i++) {
    mounts[i].used = 0;
  }
  
  // 注册文件系统类型
  vfs_register_fs(&xv6_ops, FS_TYPE_XV6);
  vfs_register_fs(&fat_ops, FS_TYPE_FAT);
  
  // 将根文件系统设为 xv6
  mounts[0].used = 1;
  mounts[0].dev = ROOTDEV;
  mounts[0].fs_type = FS_TYPE_XV6;
  mounts[0].ops = &xv6_ops;
  strncpy(mounts[0].mountpoint, "/", VFS_MAXPATH);
}

// 挂载文件系统
// source: 设备路径或设备号
// target: 挂载点路径
// fs_type: 文件系统类型
int
vfs_mount(const char *source, const char *target, int fs_type)
{
  if (fs_type < 0 || fs_type >= 8 || fs_types[fs_type] == 0)
    return -1;
  
  acquire(&mount_lock);
  
  // 查找空闲挂载点
  int slot = -1;
  for (int i = 0; i < MAX_MOUNTS; i++) {
    if (!mounts[i].used) {
      slot = i;
      break;
    }
  }
  
  if (slot < 0) {
    release(&mount_lock);
    return -1;  // 挂载点已满
  }
  
  // 解析设备（简化：假设 source 是设备号字符串）
  int dev = 1;  // 默认设备 1（用于 FAT 镜像）
  
  // 设置挂载点（标记为正在使用以防止并发）
  mounts[slot].used = 1;
  mounts[slot].dev = dev;
  mounts[slot].fs_type = fs_type;
  mounts[slot].ops = fs_types[fs_type];
  strncpy(mounts[slot].mountpoint, target, VFS_MAXPATH);
  
  // 在调用文件系统挂载函数前释放锁（挂载可能会睡眠）
  release(&mount_lock);
  
  // 调用文件系统的挂载函数
  if (mounts[slot].ops->mount(&mounts[slot], dev) < 0) {
    acquire(&mount_lock);
    mounts[slot].used = 0;
    release(&mount_lock);
    return -1;
  }
  
  return 0;
}

// 卸载文件系统
int
vfs_umount(const char *target)
{
  acquire(&mount_lock);
  
  for (int i = 1; i < MAX_MOUNTS; i++) {  // 跳过根文件系统
    if (mounts[i].used && strncmp(mounts[i].mountpoint, target, VFS_MAXPATH) == 0) {
      // 调用文件系统的卸载函数
      if (mounts[i].ops->unmount && mounts[i].ops->unmount(&mounts[i]) < 0) {
        release(&mount_lock);
        return -1;
      }
      mounts[i].used = 0;
      release(&mount_lock);
      return 0;
    }
  }
  
  release(&mount_lock);
  return -1;  // 未找到挂载点
}

// 根据路径查找对应的挂载点
struct vfs_mount*
vfs_get_mount(const char *path)
{
  struct vfs_mount *best = &mounts[0];  // 默认根文件系统
  int best_len = 1;
  
  acquire(&mount_lock);
  
  for (int i = 0; i < MAX_MOUNTS; i++) {
    if (!mounts[i].used)
      continue;
    
    int len = strlen(mounts[i].mountpoint);
    if (len > best_len && strncmp(mounts[i].mountpoint, path, len) == 0) {
      // 确保是完整的路径前缀
      if (path[len] == '/' || path[len] == '\0') {
        best = &mounts[i];
        best_len = len;
      }
    }
  }
  
  release(&mount_lock);
  return best;
}

// 通过路径获取 inode
struct vfs_inode*
vfs_namei(char *path)
{
  struct vfs_mount *mnt = vfs_get_mount(path);
  if (mnt == 0 || mnt->ops == 0)
    return 0;
  
  // 如果有专用的 namei 实现
  if (mnt->ops->namei)
    return mnt->ops->namei(mnt, path);
  
  // 否则使用默认实现
  return mnt->ops->iget(mnt, ROOTINO);
}

// 获取父目录 inode
struct vfs_inode*
vfs_nameiparent(char *path, char *name)
{
  struct vfs_mount *mnt = vfs_get_mount(path);
  if (mnt == 0 || mnt->ops == 0)
    return 0;
  
  if (mnt->ops->nameiparent)
    return mnt->ops->nameiparent(mnt, path, name);
  
  return 0;
}

// 列出所有挂载点（用于调试）
void
vfs_list_mounts(void)
{
  acquire(&mount_lock);
  
  printf("VFS Mount Table:\n");
  for (int i = 0; i < MAX_MOUNTS; i++) {
    if (mounts[i].used) {
      printf("  [%d] %s (dev=%d, type=%d)\n", 
             i, mounts[i].mountpoint, mounts[i].dev, mounts[i].fs_type);
    }
  }
  
  release(&mount_lock);
}
