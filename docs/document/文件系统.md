# OopsOS 文件系统

OopsOS 基于 XV6 实现，文件系统采用近似 ext4 的方式设计。本文档分为两部分：xv6 的基本功能与改进创新功能。

## 目录

**一、基本功能**
- 1.1 层次结构
- 1.2 磁盘划分
- 1.3 逻辑结构与物理结构
- 1.4 文件描述符、file 结构体、索引节点 inode 和盘块关系
- 1.5 Block 块管理
- 1.6 Inode 保存数据的结构
- 1.7 Buffer Cache 层
- 1.8 超级块（Superblock）
- 1.9 日志（Log）

**二、改进与创新**

| 分类 | 功能 |
|------|------|
| **索引分配** | 2.1 二级间接块的混合索引分配方式 |
| **缓存优化** | 2.2 细粒度化 buffer cache 互斥锁 |
| **链接功能** | 2.3 符号链接 |
| **权限控制** | 2.4 文件访问控制权限 (chmod/access) |
| **文件恢复** | 2.5 基于索引信息的文件恢复策略 |
| **空间管理** | 2.6 文件预分配（fallocate）<br>2.7 文件截断（truncate/ftruncate）<br>2.8 稀疏文件打洞（punch hole） |
| **文件操作** | 2.9 文件克隆与写时复制（fclone）<br>2.10 文件偏移量随机访问（lseek）<br>2.11 文件重命名（rename） |
| **数据优化** | 2.12 块级在线去重（dedup）<br>2.13 文件范围克隆（fclonerange） |
| **并发控制** | 2.14 文件锁（flock） |
| **数据持久化** | 2.15 文件同步（fsync/fdatasync） |
| **元数据扩展** | 2.16 文件扩展属性（xattr） |
| **系统信息** | 2.17 文件系统空间统计（fsinfo/statfs） |
| **高级 I/O** | 2.18 位置读写（pread/pwrite）<br>2.19 文件描述符复制（dup2）<br>2.20 分散/聚集 I/O（readv/writev） |
| **VFS 支持** | 2.21 虚拟文件系统（VFS）<br>2.22 挂载/卸载（mount/umount） |

---

## 一、基本功能

### 1.1 层次结构

xv6 文件系统分为七层：

1. **Disk 层**：读取和写入 virtio 硬盘上的块。
2. **Buffer cache 层**：缓存磁盘块并同步访问，确保每次只有一个进程可以修改数据。
3. **Logging 层**：将多个块的更新包装为事务，崩溃时保证数据一致性。
4. **Inode 层**：每个文件通过索引结点（i-number）表示，保存文件数据的块。
5. **Directory 层**：目录实现为索引结点，包含目录项（文件名和 inode 编号）。
6. **Pathname 层**：解析分层路径名，通过递归查找。
7. **File descriptor 层**：抽象 Unix 资源（如文件、管道等），简化应用程序使用。

### 1.2 磁盘划分

在 xv6 文件系统中，为了存储索引节点和数据块的位置，文件系统将磁盘划分为多个部分：

1. **块 0**：保留用于引导扇区，文件系统不使用此块。
2. **块 1**：称为超级块，包含整个文件系统的元数据。
3. **日志区域**：从块 2 开始的区域用于存储日志。
4. **索引节点区**：日志区域之后是索引节点区，每个磁盘块中存储多个索引节点（inode）。
5. **位图块**：位图块紧随索引节点区，用于跟踪数据块的使用情况。
6. **数据块**：剩余的磁盘块是数据块，用于存储文件或目录的实际内容。

### 1.3 逻辑结构与物理结构

- **逻辑结构**：包括目录、文件和路径名。文件由索引节点（inode）表示，每个文件或目录都有一个唯一的 inode 编号。
- **物理结构**：包括磁盘上分配的块。磁盘分为多个区域：引导块、超级块、日志区域、索引节点区域、位图块、数据块等。

### 1.4 文件描述符、file 结构体、索引节点 inode 和盘块关系

- **文件描述符（File Descriptor）**：每个进程通过文件描述符与打开的文件进行交互。
- **`file` 结构体**：每个文件描述符都对应一个 `file` 结构体，包含指向 `inode` 的指针、文件当前的偏移量、文件状态等。
- **索引节点（inode）**：每个文件都有一个对应的索引节点，存储文件的元数据和数据块的指针。
- **磁盘块（Block）**：磁盘上的数据块用于存储文件的实际内容。

### 1.5 Block 块管理

xv6 使用位图（Bitmap）来管理块的使用情况：

- **位图（Bitmap）**：用于表示磁盘块的分配情况。每个比特位对应一个磁盘块，值为 1 表示该块已分配，值为 0 表示该块为空闲状态。
- **块分配**：文件系统通过块分配算法从位图中选择空闲块，并将其分配给文件。
- **块回收**：当文件删除时，系统会回收不再使用的块，并将其标记为可用。

### 1.6 Inode 保存数据的结构

inode 采用两层结构来存储文件数据：

1. **直接指针（Direct Pointers）**：每个 inode 包含 12 个直接指针，直接指向文件数据块。
2. **间接指针（Indirect Pointers）**：
   - **一级间接指针**：指向一个数据块，该数据块存储更多的数据块指针。
   - **二级间接指针**：指向一个数据块，该数据块包含一级间接指针的地址。

### 1.7 Buffer Cache 层

Buffer cache 有两个任务：

1. 同步对磁盘块的访问，确保磁盘块在内存中只有一个副本。
2. 缓存常用块，以便不需要从慢速磁盘重新读取。

主要接口：
- `bread`：获取一个 buf，其中包含一个可以在内存中读取或修改的块的副本。
- `bwrite`：将修改后的缓冲区写入磁盘上的相应块。
- `brelse`：释放缓冲区。

### 1.8 超级块（Superblock）

超级块是文件系统的核心元数据，描述了文件系统的布局、容量、状态等关键信息：

```c
struct superblock {
  uint magic;      // 文件系统魔数，必须为 FSMAGIC
  uint size;       // 文件系统镜像的总大小（以块为单位）
  uint nblocks;    // 数据块的数量
  uint ninodes;    // inode 的数量
  uint nlog;       // 日志块的数量
  uint logstart;   // 日志块的起始位置
  uint inodestart; // inode 块的起始位置
  uint bmapstart;  // 位图块的起始位置
};
```

### 1.9 日志（Log）

日志用于保证文件系统的事务性和一致性：

- **事务**：多个文件系统操作被包装为一个事务。
- **崩溃恢复**：如果系统崩溃，日志可以用于恢复文件系统到一致状态。
- **写前日志（WAL）**：先将修改写入日志，确认后再写入实际数据块。

---

## 二、改进与创新

### 2.1 二级间接块的混合索引分配方式

扩展原有的一级间接索引，支持二级间接索引，使文件系统能够支持更大的文件。

### 2.2 细粒度化 buffer cache 互斥锁

将原有的全局 buffer cache 锁改为细粒度锁，提高并发性能。

### 2.3 符号链接

实现符号链接（软链接）功能：

```c
int symlink(const char *target, const char *linkpath);
```

### 2.4 文件访问控制权限

实现文件权限控制：

```c
int chmod(const char *path, int mode);
int access(const char *path, int mode);
```

### 2.5 基于索引信息的文件恢复策略

通过保存的索引信息实现已删除文件的恢复。

### 2.6 文件预分配（fallocate）

预分配文件空间，减少碎片：

```c
int fallocate(int fd, int offset, int len, int flags);
```

### 2.7 文件截断（truncate/ftruncate）

调整文件大小：

```c
int truncate(const char *path, int length);
int ftruncate(int fd, int length);
```

### 2.8 稀疏文件打洞（punch hole）

在文件中创建空洞，释放磁盘空间。

### 2.9 文件克隆与写时复制（fclone）

实现文件的高效复制：

```c
int fclone(const char *src, const char *dst);
```

### 2.10 文件偏移量随机访问（lseek）

支持文件随机访问：

```c
int lseek(int fd, int offset, int whence);
```

其中 `whence` 可以是：
- `SEEK_SET`：从文件开头计算偏移
- `SEEK_CUR`：从当前位置计算偏移
- `SEEK_END`：从文件末尾计算偏移

### 2.11 文件重命名（rename）

原子性地重命名文件：

```c
int rename(const char *oldpath, const char *newpath);
```

### 2.12 块级在线去重（dedup）

检测并合并重复的数据块：

```c
int dedup(const char *srcpath, const char *dstpath);
```

### 2.13 文件范围克隆（fclonerange）

克隆文件的指定范围：

```c
int fclonerange(int srcfd, int srcoff, int dstfd, int dstoff, int len);
```

### 2.14 文件锁（flock）

实现文件级别的锁定机制：

```c
int flock(int fd, int operation);
```

操作类型：
- `LOCK_SH`：共享锁（读锁）
- `LOCK_EX`：排他锁（写锁）
- `LOCK_UN`：解锁
- `LOCK_NB`：非阻塞模式

### 2.15 文件同步（fsync/fdatasync）

将文件数据同步到磁盘：

```c
int fsync(int fd);      // 同步数据和元数据
int fdatasync(int fd);  // 仅同步数据
```

### 2.16 文件扩展属性（xattr）

支持文件的扩展属性：

```c
int setxattr(const char *path, const char *name, const void *value, int size);
int getxattr(const char *path, const char *name, void *value, int size);
int listxattr(const char *path, char *list, int size);
int removexattr(const char *path, const char *name);
```

### 2.17 文件系统空间统计（fsinfo）

获取文件系统空间使用信息：

```c
int fsinfo(struct fsinfo *info);
```

### 2.18 位置读写（pread/pwrite）

在指定位置读写，不改变文件偏移量：

```c
int pread(int fd, void *buf, int count, int offset);
int pwrite(int fd, const void *buf, int count, int offset);
```

### 2.19 文件描述符复制（dup2）

将文件描述符复制到指定位置：

```c
int dup2(int oldfd, int newfd);
```

### 2.20 分散/聚集 I/O（readv/writev）

一次系统调用读写多个缓冲区：

```c
struct iovec {
  void  *iov_base;  // 缓冲区起始地址
  int    iov_len;   // 缓冲区长度
};

int readv(int fd, struct iovec *iov, int iovcnt);
int writev(int fd, struct iovec *iov, int iovcnt);
```

### 2.21 虚拟文件系统（VFS）

VFS 是一个抽象层，允许不同的文件系统通过统一的接口被访问：

- 支持 xv6 原生文件系统
- 支持 FAT16 文件系统（扩展）

### 2.22 挂载/卸载（mount/umount）

支持文件系统的挂载和卸载：

```c
int mount(const char *source, const char *target, const char *fstype);
int umount(const char *target);
```

---

## 三、文件系统调用汇总

| 分类 | 系统调用 | 说明 |
|------|----------|------|
| **基础 I/O** | read, write, open, close | 基本文件读写操作 |
| **文件描述符** | dup, dup2 | 复制文件描述符 |
| **目录操作** | mkdir, chdir, link, unlink | 目录管理 |
| **文件信息** | fstat, fsinfo | 获取文件/文件系统状态 |
| **位置操作** | lseek, pread, pwrite | 文件偏移和定位读写 |
| **文件大小** | truncate, ftruncate, fallocate | 调整文件大小 |
| **链接** | symlink | 符号链接 |
| **权限** | chmod, access | 权限控制 |
| **锁定** | flock | 文件锁 |
| **同步** | fsync, fdatasync | 数据同步 |
| **克隆** | fclone, fclonerange | 文件复制 |
| **去重** | dedup | 数据去重 |
| **扩展属性** | setxattr, getxattr, listxattr, removexattr | 元数据扩展 |
| **批量 I/O** | readv, writev | 分散/聚集 I/O |
| **VFS** | mount, umount | 文件系统挂载 |
| **重命名** | rename | 文件重命名 |

---

## 四、内核文件结构

| 文件 | 说明 |
|------|------|
| kernel/filesystem/fs.c | 文件系统核心实现 |
| kernel/filesystem/bio.c | Buffer cache 实现 |
| kernel/filesystem/log.c | 日志系统实现 |
| kernel/filesystem/file.c | 文件操作实现 |
| kernel/filesystem/vfs.c | 虚拟文件系统 |
| kernel/filesystem/fat.c | FAT16 文件系统驱动 |
| kernel/filesystem/xattr.c | 扩展属性实现 |
| kernel/sysfile.c | 文件系统系统调用 |
| kernel/include/fs.h | 文件系统数据结构 |
| kernel/include/file.h | 文件结构定义 |
| kernel/include/buf.h | 缓冲区结构定义 |
| kernel/include/vfs.h | VFS 接口定义 |
| kernel/include/fat.h | FAT16 数据结构 |

---

## 五、已废弃功能

以下功能因冗余已被移除：

| 功能 | 原系统调用号 | 废弃原因 |
|------|-------------|----------|
| sys_mkf | SYS_mkf=38 | 与 `open(O_CREATE)` 功能重复 |
| sys_dup_new | SYS_dup_new=42 | 与 `dup2` 功能完全重复 |
