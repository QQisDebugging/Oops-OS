#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR 0x002
#define O_CREATE 0x200
#define O_TRUNC 0x400
#define FALLOC_KEEP_SIZE 0x001
#define FALLOC_FL_PUNCH_HOLE 0x002  // 释放指定范围的块（稀疏文件打洞）
#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define O_NOFOLLOW 0x004

// lseek whence values
#define SEEK_SET 0  // 从文件开头计算偏移
#define SEEK_CUR 1  // 从当前位置计算偏移
#define SEEK_END 2  // 从文件末尾计算偏移

// flock 操作标志
#define LOCK_SH   1   // 共享锁（读锁）
#define LOCK_EX   2   // 排他锁（写锁）
#define LOCK_UN   8   // 解锁
#define LOCK_NB   4   // 非阻塞模式（可与 LOCK_SH/LOCK_EX 组合）
