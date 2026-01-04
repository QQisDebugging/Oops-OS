// vfs.h - 虚拟文件系统抽象层
// 提供统一的文件系统接口，支持多种文件系统类型

#ifndef _VFS_H_
#define _VFS_H_

#include "types.h"

// 文件系统类型
#define FS_TYPE_XV6  0   // xv6 原生文件系统
#define FS_TYPE_FAT  1   // FAT16 文件系统

// 最大挂载点数量
#define MAX_MOUNTS 8

// 最大路径长度
#define VFS_MAXPATH 128

// 文件类型常量
#define VFS_FILE    1
#define VFS_DIR     2
#define VFS_SYMLINK 3

// 前向声明
struct vfs_inode;
struct vfs_superblock;
struct vfs_mount;

// 文件系统操作接口
struct fs_operations {
  // 文件系统名称
  const char *name;
  
  // 挂载/卸载
  int (*mount)(struct vfs_mount *mnt, int dev);
  int (*unmount)(struct vfs_mount *mnt);
  
  // inode 操作
  struct vfs_inode* (*iget)(struct vfs_mount *mnt, uint inum);
  void (*iput)(struct vfs_inode *ip);
  void (*ilock)(struct vfs_inode *ip);
  void (*iunlock)(struct vfs_inode *ip);
  void (*iupdate)(struct vfs_inode *ip);
  
  // 读写操作
  int (*readi)(struct vfs_inode *ip, int user_dst, uint64 dst, uint off, uint n);
  int (*writei)(struct vfs_inode *ip, int user_src, uint64 src, uint off, uint n);
  
  // 目录操作
  struct vfs_inode* (*dirlookup)(struct vfs_inode *dp, char *name, uint *poff);
  int (*dirlink)(struct vfs_inode *dp, char *name, uint inum);
  
  // 文件操作
  struct vfs_inode* (*create)(struct vfs_inode *dp, char *name, short type, short major, short minor);
  int (*unlink)(struct vfs_inode *dp, char *name);
  int (*truncate)(struct vfs_inode *ip, uint size);
  
  // 路径解析
  struct vfs_inode* (*namei)(struct vfs_mount *mnt, char *path);
  struct vfs_inode* (*nameiparent)(struct vfs_mount *mnt, char *path, char *name);
};

// VFS inode - 通用 inode 结构
struct vfs_inode {
  uint dev;              // 设备号
  uint inum;             // inode 编号
  int ref;               // 引用计数
  int valid;             // 是否有效
  
  // 文件属性
  short type;            // 文件类型
  short mode;            // 权限模式
  short major;
  short minor;
  short nlink;           // 硬链接数
  uint size;             // 文件大小
  
  // 所属挂载点
  struct vfs_mount *mnt;
  
  // 底层文件系统特定数据
  void *fs_data;         // 指向具体文件系统的 inode 结构
};

// VFS 挂载点
struct vfs_mount {
  int used;                        // 是否使用中
  int dev;                         // 设备号
  int fs_type;                     // 文件系统类型
  char mountpoint[VFS_MAXPATH];    // 挂载点路径
  struct fs_operations *ops;       // 文件系统操作
  void *fs_data;                   // 文件系统私有数据（如超级块）
  struct vfs_inode *root;          // 根目录 inode
};

// VFS 全局函数
void vfs_init(void);
int vfs_mount(const char *source, const char *target, int fs_type);
int vfs_umount(const char *target);
struct vfs_mount* vfs_get_mount(const char *path);
struct vfs_inode* vfs_namei(char *path);
struct vfs_inode* vfs_nameiparent(char *path, char *name);

// 文件系统注册
void vfs_register_xv6(void);
void vfs_register_fat(void);

#endif // _VFS_H_
