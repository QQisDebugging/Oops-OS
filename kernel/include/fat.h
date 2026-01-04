// fat.h - FAT16 文件系统定义
// FAT (File Allocation Table) 是一种简单的文件系统，广泛用于 U 盘和 SD 卡

#ifndef _FAT_H_
#define _FAT_H_

#include "types.h"

// FAT16 常量
#define FAT16_SECTOR_SIZE    512
#define FAT16_CLUSTER_FREE   0x0000
#define FAT16_CLUSTER_RESV   0xFFF0
#define FAT16_CLUSTER_BAD    0xFFF7
#define FAT16_CLUSTER_EOF    0xFFFF
#define FAT16_CLUSTER_MIN    2        // 有效簇号从 2 开始

// FAT 目录项属性
#define FAT_ATTR_READ_ONLY   0x01
#define FAT_ATTR_HIDDEN      0x02
#define FAT_ATTR_SYSTEM      0x04
#define FAT_ATTR_VOLUME_ID   0x08
#define FAT_ATTR_DIRECTORY   0x10
#define FAT_ATTR_ARCHIVE     0x20
#define FAT_ATTR_LONG_NAME   0x0F

// FAT16 引导扇区（BPB - BIOS Parameter Block）
struct fat_bpb {
  uchar  jmp[3];           // 跳转指令
  char   oem[8];           // OEM 名称
  ushort bytes_per_sector; // 每扇区字节数（通常 512）
  uchar  sectors_per_cluster; // 每簇扇区数
  ushort reserved_sectors; // 保留扇区数
  uchar  num_fats;         // FAT 表数量（通常 2）
  ushort root_entries;     // 根目录条目数
  ushort total_sectors16;  // 总扇区数（16位，小于 65536）
  uchar  media_type;       // 媒体类型
  ushort sectors_per_fat;  // 每个 FAT 的扇区数
  ushort sectors_per_track;// 每磁道扇区数
  ushort num_heads;        // 磁头数
  uint   hidden_sectors;   // 隐藏扇区数
  uint   total_sectors32;  // 总扇区数（32位，大于 65535）
  // FAT16 扩展 BPB
  uchar  drive_number;     // 驱动器号
  uchar  reserved1;
  uchar  boot_signature;   // 扩展引导签名（0x29）
  uint   volume_id;        // 卷序列号
  char   volume_label[11]; // 卷标
  char   fs_type[8];       // 文件系统类型（"FAT16   "）
} __attribute__((packed));

// FAT16 目录项（32 字节）
struct fat_dirent {
  char   name[8];          // 文件名（空格填充）
  char   ext[3];           // 扩展名（空格填充）
  uchar  attr;             // 属性
  uchar  reserved;         // 保留
  uchar  create_time_ms;   // 创建时间（10ms 单位）
  ushort create_time;      // 创建时间
  ushort create_date;      // 创建日期
  ushort access_date;      // 最后访问日期
  ushort cluster_high;     // 起始簇号高 16 位（FAT32 用，FAT16 为 0）
  ushort modify_time;      // 修改时间
  ushort modify_date;      // 修改日期
  ushort cluster_low;      // 起始簇号低 16 位
  uint   file_size;        // 文件大小
} __attribute__((packed));

// FAT16 超级块信息（内存中）
struct fat_sb {
  int dev;                 // 设备号
  uint bytes_per_sector;
  uint sectors_per_cluster;
  uint bytes_per_cluster;
  uint reserved_sectors;
  uint num_fats;
  uint root_entries;
  uint sectors_per_fat;
  uint total_sectors;
  
  uint fat_start;          // FAT 表起始扇区
  uint root_start;         // 根目录起始扇区
  uint root_sectors;       // 根目录占用扇区数
  uint data_start;         // 数据区起始扇区
  uint total_clusters;     // 总簇数
  
  ushort *fat_cache;       // FAT 表缓存
  int fat_dirty;           // FAT 是否被修改
};

// FAT inode（内存中）
struct fat_inode {
  uint cluster;            // 起始簇号
  uint size;               // 文件大小
  uchar attr;              // 属性
  uint parent_cluster;     // 父目录簇号
  uint dir_offset;         // 在父目录中的偏移
  char name[12];           // 8.3 格式文件名
  struct fat_sb *sb;       // 所属超级块
  int ref;                 // 引用计数
  int valid;               // 是否有效
};

// FAT16 操作函数
int fat_mount(struct vfs_mount *mnt, int dev);
int fat_unmount(struct vfs_mount *mnt);
struct vfs_inode* fat_iget(struct vfs_mount *mnt, uint inum);
void fat_iput(struct vfs_inode *ip);
void fat_ilock(struct vfs_inode *ip);
void fat_iunlock(struct vfs_inode *ip);
int fat_readi(struct vfs_inode *ip, int user_dst, uint64 dst, uint off, uint n);
int fat_writei(struct vfs_inode *ip, int user_src, uint64 src, uint off, uint n);
struct vfs_inode* fat_dirlookup(struct vfs_inode *dp, char *name, uint *poff);
int fat_dirlink(struct vfs_inode *dp, char *name, uint inum);
struct vfs_inode* fat_create(struct vfs_inode *dp, char *name, short type, short major, short minor);
int fat_unlink(struct vfs_inode *dp, char *name);
struct vfs_inode* fat_namei(struct vfs_mount *mnt, char *path);

#endif // _FAT_H_
