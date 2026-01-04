// fat.c - FAT16 文件系统实现
// 支持读取 FAT16 格式的磁盘镜像

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "buf.h"
#include "vfs.h"
#include "fat.h"

// FAT inode 缓存
#define FAT_NINODE 50
static struct {
  struct spinlock lock;
  struct fat_inode inodes[FAT_NINODE];
} fat_icache;

static struct spinlock fat_lock;

// 初始化 FAT 模块
void
fat_init(void)
{
  initlock(&fat_lock, "fat");
  initlock(&fat_icache.lock, "fat_icache");
  
  for (int i = 0; i < FAT_NINODE; i++) {
    fat_icache.inodes[i].ref = 0;
    fat_icache.inodes[i].valid = 0;
  }
}

// 读取扇区
static int
fat_read_sector(int dev, uint sector, void *buf)
{
  // FAT 使用 512 字节扇区，xv6 块大小是 1024
  // 需要将两个 FAT 扇区映射到一个 xv6 块
  uint blockno = sector / 2;
  uint offset = (sector % 2) * FAT16_SECTOR_SIZE;
  
  struct buf *bp = bread(dev, blockno);
  memmove(buf, bp->data + offset, FAT16_SECTOR_SIZE);
  brelse(bp);
  
  return 0;
}

// 写入扇区
static int
fat_write_sector(int dev, uint sector, void *buf)
{
  uint blockno = sector / 2;
  uint offset = (sector % 2) * FAT16_SECTOR_SIZE;
  
  struct buf *bp = bread(dev, blockno);
  memmove(bp->data + offset, buf, FAT16_SECTOR_SIZE);
  bwrite(bp);
  brelse(bp);
  
  return 0;
}

// 读取 FAT 表项
static ushort
fat_get_entry(struct fat_sb *sb, uint cluster)
{
  if (sb->fat_cache)
    return sb->fat_cache[cluster];
  
  // 从磁盘读取（备用方案）
  uint fat_offset = cluster * 2;
  uint fat_sector = sb->fat_start + (fat_offset / FAT16_SECTOR_SIZE);
  uint offset = fat_offset % FAT16_SECTOR_SIZE;
  
  uchar buf[FAT16_SECTOR_SIZE];
  fat_read_sector(sb->dev, fat_sector, buf);
  
  return *(ushort *)(buf + offset);
}

// 设置 FAT 表项
static void
fat_set_entry(struct fat_sb *sb, uint cluster, ushort value)
{
  if (sb->fat_cache) {
    sb->fat_cache[cluster] = value;
    sb->fat_dirty = 1;
    return;
  }
  
  uint fat_offset = cluster * 2;
  uint fat_sector = sb->fat_start + (fat_offset / FAT16_SECTOR_SIZE);
  uint offset = fat_offset % FAT16_SECTOR_SIZE;
  
  uchar buf[FAT16_SECTOR_SIZE];
  fat_read_sector(sb->dev, fat_sector, buf);
  *(ushort *)(buf + offset) = value;
  fat_write_sector(sb->dev, fat_sector, buf);
}

// 分配新簇
static uint
fat_alloc_cluster(struct fat_sb *sb)
{
  for (uint i = FAT16_CLUSTER_MIN; i < sb->total_clusters + 2; i++) {
    if (fat_get_entry(sb, i) == FAT16_CLUSTER_FREE) {
      fat_set_entry(sb, i, FAT16_CLUSTER_EOF);
      return i;
    }
  }
  return 0;  // 没有空闲簇
}

// 释放簇链
static void
fat_free_chain(struct fat_sb *sb, uint cluster)
{
  while (cluster >= FAT16_CLUSTER_MIN && cluster < FAT16_CLUSTER_RESV) {
    uint next = fat_get_entry(sb, cluster);
    fat_set_entry(sb, cluster, FAT16_CLUSTER_FREE);
    cluster = next;
  }
}

// 将簇号转换为扇区号
static uint
fat_cluster_to_sector(struct fat_sb *sb, uint cluster)
{
  return sb->data_start + (cluster - 2) * sb->sectors_per_cluster;
}

// 挂载 FAT16 文件系统
int
fat_mount(struct vfs_mount *mnt, int dev)
{
  uchar buf[FAT16_SECTOR_SIZE];
  
  // 读取引导扇区
  fat_read_sector(dev, 0, buf);
  struct fat_bpb *bpb = (struct fat_bpb *)buf;
  
  // 验证是否为 FAT16
  if (bpb->bytes_per_sector != FAT16_SECTOR_SIZE) {
    printf("fat_mount: invalid sector size %d\n", bpb->bytes_per_sector);
    return -1;
  }
  
  // 分配超级块
  struct fat_sb *sb = (struct fat_sb *)kalloc();
  if (sb == 0)
    return -1;
  
  memset(sb, 0, PGSIZE);
  
  // 填充超级块信息
  sb->dev = dev;
  sb->bytes_per_sector = bpb->bytes_per_sector;
  sb->sectors_per_cluster = bpb->sectors_per_cluster;
  sb->bytes_per_cluster = sb->bytes_per_sector * sb->sectors_per_cluster;
  sb->reserved_sectors = bpb->reserved_sectors;
  sb->num_fats = bpb->num_fats;
  sb->root_entries = bpb->root_entries;
  sb->sectors_per_fat = bpb->sectors_per_fat;
  
  if (bpb->total_sectors16 != 0)
    sb->total_sectors = bpb->total_sectors16;
  else
    sb->total_sectors = bpb->total_sectors32;
  
  // 计算各区域起始位置
  sb->fat_start = sb->reserved_sectors;
  sb->root_start = sb->fat_start + sb->num_fats * sb->sectors_per_fat;
  sb->root_sectors = (sb->root_entries * 32 + sb->bytes_per_sector - 1) / sb->bytes_per_sector;
  sb->data_start = sb->root_start + sb->root_sectors;
  sb->total_clusters = (sb->total_sectors - sb->data_start) / sb->sectors_per_cluster;
  
  // 缓存 FAT 表（暂不缓存，按需读取）
  (void)(sb->sectors_per_fat * FAT16_SECTOR_SIZE);  // fat_bytes
  
  sb->fat_cache = 0;
  sb->fat_dirty = 0;
  
  mnt->fs_data = sb;
  mnt->dev = dev;
  
  printf("fat_mount: mounted FAT16 on dev %d\n", dev);
  printf("  sectors_per_cluster: %d\n", sb->sectors_per_cluster);
  printf("  total_clusters: %d\n", sb->total_clusters);
  printf("  data_start: %d\n", sb->data_start);
  
  return 0;
}

// 卸载 FAT16 文件系统
int
fat_unmount(struct vfs_mount *mnt)
{
  struct fat_sb *sb = (struct fat_sb *)mnt->fs_data;
  
  if (sb) {
    // 写回 FAT 表
    if (sb->fat_cache && sb->fat_dirty) {
      // TODO: 写回 FAT 表到磁盘
    }
    kfree(sb);
  }
  
  mnt->fs_data = 0;
  return 0;
}

// 将 8.3 格式文件名转换为普通字符串
static void
fat_name_to_str(struct fat_dirent *de, char *str)
{
  int i, j = 0;
  
  // 复制文件名（去除尾部空格）
  for (i = 0; i < 8 && de->name[i] != ' '; i++)
    str[j++] = de->name[i];
  
  // 如果有扩展名
  if (de->ext[0] != ' ') {
    str[j++] = '.';
    for (i = 0; i < 3 && de->ext[i] != ' '; i++)
      str[j++] = de->ext[i];
  }
  
  str[j] = '\0';
}

// 将普通字符串转换为 8.3 格式
static void
fat_str_to_name(const char *str, char *name, char *ext)
{
  int i, j;
  
  // 初始化为空格
  for (i = 0; i < 8; i++) name[i] = ' ';
  for (i = 0; i < 3; i++) ext[i] = ' ';
  
  // 查找点的位置
  const char *dot = 0;
  for (i = 0; str[i]; i++) {
    if (str[i] == '.')
      dot = &str[i];
  }
  
  // 复制文件名
  j = 0;
  for (i = 0; str[i] && str[i] != '.' && j < 8; i++) {
    char c = str[i];
    if (c >= 'a' && c <= 'z')
      c = c - 'a' + 'A';  // 转大写
    name[j++] = c;
  }
  
  // 复制扩展名
  if (dot) {
    j = 0;
    for (i = 1; dot[i] && j < 3; i++) {
      char c = dot[i];
      if (c >= 'a' && c <= 'z')
        c = c - 'a' + 'A';
      ext[j++] = c;
    }
  }
}

// 分配 FAT inode
static struct fat_inode*
fat_ialloc(void)
{
  acquire(&fat_icache.lock);
  
  for (int i = 0; i < FAT_NINODE; i++) {
    if (fat_icache.inodes[i].ref == 0) {
      fat_icache.inodes[i].ref = 1;
      fat_icache.inodes[i].valid = 0;
      release(&fat_icache.lock);
      return &fat_icache.inodes[i];
    }
  }
  
  release(&fat_icache.lock);
  panic("fat_ialloc: no inodes");
  return 0;
}

// 获取 FAT inode（通过 inum = cluster）
struct vfs_inode*
fat_iget(struct vfs_mount *mnt, uint inum)
{
  struct fat_inode *fip = fat_ialloc();
  if (fip == 0)
    return 0;
  
  fip->cluster = inum;
  fip->sb = (struct fat_sb *)mnt->fs_data;
  fip->valid = 0;
  
  // 包装为 vfs_inode
  struct vfs_inode *vip = (struct vfs_inode *)kalloc();
  if (vip == 0) {
    fip->ref = 0;
    return 0;
  }
  
  memset(vip, 0, sizeof(struct vfs_inode));
  vip->inum = inum;
  vip->dev = mnt->dev;
  vip->mnt = mnt;
  vip->fs_data = fip;
  vip->ref = 1;
  
  return vip;
}

// 释放 FAT inode
void
fat_iput(struct vfs_inode *ip)
{
  if (ip == 0)
    return;
  
  struct fat_inode *fip = (struct fat_inode *)ip->fs_data;
  if (fip) {
    acquire(&fat_icache.lock);
    fip->ref--;
    release(&fat_icache.lock);
  }
  
  kfree(ip);
}

void
fat_ilock(struct vfs_inode *ip)
{
  // FAT 使用简单的锁定（实际应使用 sleeplock）
  struct fat_inode *fip = (struct fat_inode *)ip->fs_data;
  if (fip && !fip->valid) {
    // 读取 inode 信息（对于根目录特殊处理）
    if (fip->cluster == 0) {
      // 根目录
      fip->size = fip->sb->root_entries * 32;
      fip->attr = FAT_ATTR_DIRECTORY;
    }
    fip->valid = 1;
  }
  
  // 同步 vfs_inode
  ip->type = (fip->attr & FAT_ATTR_DIRECTORY) ? VFS_DIR : VFS_FILE;
  ip->size = fip->size;
  ip->valid = 1;
}

void
fat_iunlock(struct vfs_inode *ip)
{
  // 简单实现，无需操作
}

// 读取文件数据
int
fat_readi(struct vfs_inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  struct fat_inode *fip = (struct fat_inode *)ip->fs_data;
  struct fat_sb *sb = fip->sb;
  
  if (off > fip->size)
    return 0;
  if (off + n > fip->size)
    n = fip->size - off;
  if (n == 0)
    return 0;
  
  uint total = 0;
  uint cluster = fip->cluster;
  
  // 根目录特殊处理（连续扇区）
  if (cluster == 0) {
    while (total < n) {
      uint sector = sb->root_start + (off + total) / FAT16_SECTOR_SIZE;
      uint sector_off = (off + total) % FAT16_SECTOR_SIZE;
      uint m = FAT16_SECTOR_SIZE - sector_off;
      if (m > n - total)
        m = n - total;
      
      uchar buf[FAT16_SECTOR_SIZE];
      fat_read_sector(sb->dev, sector, buf);
      
      if (user_dst) {
        if (copyout(myproc()->pagetable, dst + total, (char *)buf + sector_off, m) < 0)
          return -1;
      } else {
        memmove((char *)dst + total, buf + sector_off, m);
      }
      total += m;
    }
    return total;
  }
  
  // 跳过前面的簇
  uint skip_clusters = off / sb->bytes_per_cluster;
  for (uint i = 0; i < skip_clusters && cluster >= FAT16_CLUSTER_MIN && cluster < FAT16_CLUSTER_RESV; i++) {
    cluster = fat_get_entry(sb, cluster);
  }
  
  uint cluster_off = off % sb->bytes_per_cluster;
  
  // 读取数据
  while (total < n && cluster >= FAT16_CLUSTER_MIN && cluster < FAT16_CLUSTER_RESV) {
    uint sector = fat_cluster_to_sector(sb, cluster);
    uint sector_in_cluster = cluster_off / FAT16_SECTOR_SIZE;
    uint sector_off = cluster_off % FAT16_SECTOR_SIZE;
    
    while (sector_in_cluster < sb->sectors_per_cluster && total < n) {
      uchar buf[FAT16_SECTOR_SIZE];
      fat_read_sector(sb->dev, sector + sector_in_cluster, buf);
      
      uint m = FAT16_SECTOR_SIZE - sector_off;
      if (m > n - total)
        m = n - total;
      
      if (user_dst) {
        if (copyout(myproc()->pagetable, dst + total, (char *)buf + sector_off, m) < 0)
          return -1;
      } else {
        memmove((char *)dst + total, buf + sector_off, m);
      }
      
      total += m;
      sector_off = 0;
      sector_in_cluster++;
    }
    
    cluster_off = 0;
    cluster = fat_get_entry(sb, cluster);
  }
  
  return total;
}

// 写入文件数据
int
fat_writei(struct vfs_inode *ip, int user_src, uint64 src, uint off, uint n)
{
  struct fat_inode *fip = (struct fat_inode *)ip->fs_data;
  struct fat_sb *sb = fip->sb;
  
  if (fip->cluster == 0)  // 不支持写入根目录
    return -1;
  
  uint total = 0;
  uint cluster = fip->cluster;
  
  // 如果文件为空，分配第一个簇
  if (cluster == 0 && n > 0) {
    cluster = fat_alloc_cluster(sb);
    if (cluster == 0)
      return -1;
    fip->cluster = cluster;
  }
  
  // 跳到正确的簇
  uint skip_clusters = off / sb->bytes_per_cluster;
  for (uint i = 0; i < skip_clusters && cluster >= FAT16_CLUSTER_MIN; i++) {
    uint next = fat_get_entry(sb, cluster);
    if (next >= FAT16_CLUSTER_EOF) {
      // 需要分配新簇
      next = fat_alloc_cluster(sb);
      if (next == 0)
        return total > 0 ? total : -1;
      fat_set_entry(sb, cluster, next);
    }
    cluster = next;
  }
  
  uint cluster_off = off % sb->bytes_per_cluster;
  
  // 写入数据
  while (total < n) {
    if (cluster < FAT16_CLUSTER_MIN) {
      cluster = fat_alloc_cluster(sb);
      if (cluster == 0)
        break;
      // 链接到前一个簇（简化处理）
    }
    
    uint sector = fat_cluster_to_sector(sb, cluster);
    uint sector_in_cluster = cluster_off / FAT16_SECTOR_SIZE;
    uint sector_off = cluster_off % FAT16_SECTOR_SIZE;
    
    while (sector_in_cluster < sb->sectors_per_cluster && total < n) {
      uchar buf[FAT16_SECTOR_SIZE];
      fat_read_sector(sb->dev, sector + sector_in_cluster, buf);
      
      uint m = FAT16_SECTOR_SIZE - sector_off;
      if (m > n - total)
        m = n - total;
      
      if (user_src) {
        if (copyin(myproc()->pagetable, (char *)buf + sector_off, src + total, m) < 0)
          return -1;
      } else {
        memmove(buf + sector_off, (char *)src + total, m);
      }
      
      fat_write_sector(sb->dev, sector + sector_in_cluster, buf);
      
      total += m;
      sector_off = 0;
      sector_in_cluster++;
    }
    
    cluster_off = 0;
    uint next = fat_get_entry(sb, cluster);
    if (next >= FAT16_CLUSTER_EOF && total < n) {
      next = fat_alloc_cluster(sb);
      if (next == 0)
        break;
      fat_set_entry(sb, cluster, next);
    }
    cluster = next;
  }
  
  // 更新文件大小
  if (off + total > fip->size)
    fip->size = off + total;
  ip->size = fip->size;
  
  return total;
}

// 在目录中查找文件
struct vfs_inode*
fat_dirlookup(struct vfs_inode *dp, char *name, uint *poff)
{
  struct fat_inode *fip = (struct fat_inode *)dp->fs_data;
  
  if (!(fip->attr & FAT_ATTR_DIRECTORY) && fip->cluster != 0)
    return 0;  // 不是目录
  
  // 转换为 8.3 格式
  char fname[8], fext[3];
  fat_str_to_name(name, fname, fext);
  
  struct fat_dirent de;
  uint off = 0;
  
  while (fat_readi(dp, 0, (uint64)&de, off, sizeof(de)) == sizeof(de)) {
    if (de.name[0] == 0)  // 目录结束
      break;
    if (de.name[0] == 0xE5)  // 已删除
      goto next;
    if (de.attr == FAT_ATTR_LONG_NAME)  // 长文件名
      goto next;
    if (de.attr & FAT_ATTR_VOLUME_ID)  // 卷标
      goto next;
    
    // 比较文件名
    int match = 1;
    for (int i = 0; i < 8; i++) {
      if (de.name[i] != fname[i]) {
        match = 0;
        break;
      }
    }
    if (match) {
      for (int i = 0; i < 3; i++) {
        if (de.ext[i] != fext[i]) {
          match = 0;
          break;
        }
      }
    }
    
    if (match) {
      if (poff)
        *poff = off;
      
      // 创建 inode
      struct vfs_inode *ip = fat_iget(dp->mnt, de.cluster_low);
      if (ip) {
        struct fat_inode *fip2 = (struct fat_inode *)ip->fs_data;
        fip2->size = de.file_size;
        fip2->attr = de.attr;
        fip2->parent_cluster = fip->cluster;
        fip2->dir_offset = off;
        fat_name_to_str(&de, fip2->name);
        fip2->valid = 1;
        
        ip->type = (de.attr & FAT_ATTR_DIRECTORY) ? VFS_DIR : VFS_FILE;
        ip->size = de.file_size;
      }
      return ip;
    }
    
next:
    off += sizeof(de);
  }
  
  return 0;  // 未找到
}

// 在目录中添加链接
int
fat_dirlink(struct vfs_inode *dp, char *name, uint inum)
{
  struct fat_inode *fip = (struct fat_inode *)dp->fs_data;
  
  if (!(fip->attr & FAT_ATTR_DIRECTORY) && fip->cluster != 0)
    return -1;
  
  // 检查是否已存在
  if (fat_dirlookup(dp, name, 0) != 0)
    return -1;
  
  // 查找空闲目录项
  struct fat_dirent de;
  uint off = 0;
  
  while (fat_readi(dp, 0, (uint64)&de, off, sizeof(de)) == sizeof(de)) {
    if (de.name[0] == 0 || de.name[0] == 0xE5)
      break;
    off += sizeof(de);
  }
  
  // 填充目录项
  memset(&de, 0, sizeof(de));
  fat_str_to_name(name, de.name, de.ext);
  de.cluster_low = inum;
  de.file_size = 0;
  de.attr = FAT_ATTR_ARCHIVE;
  
  // 写入目录项
  if (fat_writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;
  
  return 0;
}

// 创建文件
struct vfs_inode*
fat_create(struct vfs_inode *dp, char *name, short type, short major, short minor)
{
  struct fat_inode *fip = (struct fat_inode *)dp->fs_data;
  struct fat_sb *sb = fip->sb;
  
  // 检查是否已存在
  struct vfs_inode *ip = fat_dirlookup(dp, name, 0);
  if (ip != 0)
    return ip;  // 已存在
  
  // 分配新簇（如果是目录或大文件）
  uint cluster = 0;
  if (type == VFS_DIR) {
    cluster = fat_alloc_cluster(sb);
    if (cluster == 0)
      return 0;
  }
  
  // 创建目录项
  struct fat_dirent de;
  memset(&de, 0, sizeof(de));
  fat_str_to_name(name, de.name, de.ext);
  de.cluster_low = cluster;
  de.file_size = 0;
  de.attr = (type == VFS_DIR) ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE;
  
  // 查找空闲目录项并写入
  uint off = 0;
  struct fat_dirent tmp;
  while (fat_readi(dp, 0, (uint64)&tmp, off, sizeof(tmp)) == sizeof(tmp)) {
    if (tmp.name[0] == 0 || tmp.name[0] == 0xE5)
      break;
    off += sizeof(tmp);
  }
  
  if (fat_writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
    if (cluster)
      fat_free_chain(sb, cluster);
    return 0;
  }
  
  // 返回新创建的 inode
  ip = fat_iget(dp->mnt, cluster);
  if (ip) {
    struct fat_inode *fip2 = (struct fat_inode *)ip->fs_data;
    fip2->size = 0;
    fip2->attr = de.attr;
    fip2->parent_cluster = fip->cluster;
    fip2->dir_offset = off;
    fat_name_to_str(&de, fip2->name);
    fip2->valid = 1;
    
    ip->type = type;
    ip->size = 0;
  }
  
  return ip;
}

// 删除文件
int
fat_unlink(struct vfs_inode *dp, char *name)
{
  uint off;
  struct vfs_inode *ip = fat_dirlookup(dp, name, &off);
  if (ip == 0)
    return -1;
  
  struct fat_inode *fip = (struct fat_inode *)ip->fs_data;
  struct fat_sb *sb = fip->sb;
  
  // 释放簇链
  if (fip->cluster >= FAT16_CLUSTER_MIN)
    fat_free_chain(sb, fip->cluster);
  
  // 标记目录项为已删除
  struct fat_dirent de;
  if (fat_readi(dp, 0, (uint64)&de, off, sizeof(de)) == sizeof(de)) {
    de.name[0] = 0xE5;  // 删除标记
    fat_writei(dp, 0, (uint64)&de, off, sizeof(de));
  }
  
  fat_iput(ip);
  return 0;
}

// 路径解析
struct vfs_inode*
fat_namei(struct vfs_mount *mnt, char *path)
{
  struct vfs_inode *ip;
  char name[14];
  
  // 从根目录开始
  ip = fat_iget(mnt, 0);  // 0 表示根目录
  if (ip == 0)
    return 0;
  
  fat_ilock(ip);
  
  while (*path == '/')
    path++;
  
  if (*path == '\0')
    return ip;  // 根目录
  
  while (*path) {
    // 提取路径组件
    char *s = name;
    while (*path && *path != '/' && s < name + 13)
      *s++ = *path++;
    *s = '\0';
    
    while (*path == '/')
      path++;
    
    // 查找
    struct vfs_inode *next = fat_dirlookup(ip, name, 0);
    fat_iput(ip);
    
    if (next == 0)
      return 0;
    
    ip = next;
    fat_ilock(ip);
  }
  
  return ip;
}
