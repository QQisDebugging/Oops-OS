struct file
{
  enum
  {
    FD_NONE,
    FD_PIPE,
    FD_INODE,
    FD_DEVICE,
    FD_SOCK
  } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  struct sock *sock;
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
  char flock_held;   // 该文件描述符持有的 flock 类型：0=无, LOCK_SH, LOCK_EX
};

#define major(dev) ((dev) >> 16 & 0xFFFF)
#define minor(dev) ((dev) & 0xFFFF)
#define mkdev(m, n) ((uint)((m) << 16 | (n)))

// in-memory copy of an inode
struct inode
{
  uint dev;              // Device number
  uint inum;             // Inode number
  int ref;               // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;             // inode has been read from disk?

  char type; // copy of disk inode
  char mode;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT + 2];

  // 文件锁（flock）
  int flock_type;        // 当前锁类型：0=无锁, LOCK_SH=共享锁, LOCK_EX=排他锁
  int flock_count;       // 共享锁持有者数量
};

// map major device number to device functions.
struct devsw
{
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
