#include "param.h"
struct stat;
struct fsinfo;
struct rtcdate;
struct sysinfo;
// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int *);
int pipe(int *);
int write(int, const void *, int);
int read(int, void *, int);
int close(int);
int kill(int);
int exec(char *, char **);
int open(const char *, int);
int mknod(const char *, short, short);
int unlink(const char *);
int fstat(int fd, struct stat *);
int link(const char *, const char *);
int mkdir(const char *);
int chdir(const char *);
int dup(int);
int getpid(void);
int gettid(void);
int gettgid(void);
char *sbrk(int);
int sleep(int);
int uptime(void);
int cps(void);
int trace(int);
int sysinfo(struct sysinfo *);
int fsinfo(struct fsinfo *);
int setPriority(int pid, int priority);
int rt_set(int pid, int period, int runtime, int deadline);
int rt_clear(int pid);
int banker_init(int nres, int *total);
int banker_set_max(int nres, int *max);
int banker_request(int nres, int *req);
int banker_release(int nres, int *rel);
int midsched(int on);
int execve(const char *path, char *argv[], char *envp[]);
int getparentpid(void);
int print_pgtable(void);
void *mmap(void *addr, int length, int prot, int flags, int fd, int offset);
int munmap(void *addr, int length);
int sh_var_read(void);
void sh_var_write(int n);
int symlink(const char*,const char*);
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
int connect(uint32, uint16, uint16);
//恢复被删除的文件
int geti(const char*,uint64);
int recoveri(uint,uint64);
int fallocate(int fd, int offset, int len, int flags);
int fclone(const char *, const char *);
int fclonerange(int srcfd, int srcoff, int dstfd, int dstoff, int len);
int lseek(int fd, int offset, int whence);
int truncate(const char *path, int length);
int ftruncate(int fd, int length);
int rename(const char *oldpath, const char *newpath);
int dedup(const char *srcpath, const char *dstpath);
int flock(int fd, int operation);
int fsync(int fd);
int fdatasync(int fd);
int setxattr(const char *path, const char *name, const void *value, int size);
int getxattr(const char *path, const char *name, void *value, int size);
int listxattr(const char *path, char *list, int size);
int removexattr(const char *path, const char *name);
int pread(int fd, void *buf, int count, int offset);
int pwrite(int fd, const void *buf, int count, int offset);
int dup2(int oldfd, int newfd);
int access(const char *path, int mode);
int mount(const char *source, const char *target, const char *fstype);
int umount(const char *target);

// iovec 结构体用于分散/聚集 I/O
struct iovec {
  void  *iov_base;  // 缓冲区起始地址
  int    iov_len;   // 缓冲区长度
};
int readv(int fd, struct iovec *iov, int iovcnt);
int writev(int fd, struct iovec *iov, int iovcnt);

// access 模式标志
#define F_OK 0  // 测试文件是否存在
#define R_OK 4  // 测试读权限
#define W_OK 2  // 测试写权限
#define X_OK 1  // 测试执行权限

// fallocate flags
#define FALLOC_KEEP_SIZE     0x001
#define FALLOC_FL_PUNCH_HOLE 0x002

// flock 操作标志
#define LOCK_SH   1   // 共享锁（读锁）
#define LOCK_EX   2   // 排他锁（写锁）
#define LOCK_UN   8   // 解锁
#define LOCK_NB   4   // 非阻塞模式（可与 LOCK_SH/LOCK_EX 组合）

// lseek whence values
#define SEEK_SET 0  // 从文件开头计算偏移
#define SEEK_CUR 1  // 从当前位置计算偏移
#define SEEK_END 2  // 从文件末尾计算偏移

// 信号量
int sem_create(int);
int sem_free(int);
int sem_p(int);
int sem_v(int);
int sem_p_multi(int, int *);
int semset_create(int, int *);
int semset_free(int);
int semset_p(int, int);
int semset_v(int, int);
int semset_p_multi(int, int, int *);
int dmsgsend(int, void *, int);
int dmsgrcv(void *, int);
int mon_create(void);
int mon_free(int);
int mon_enter(int);
int mon_exit(int);
int cond_create(int);
int cond_free(int, int);
int cond_wait(int, int);
int cond_signal(int, int);
int cond_broadcast(int, int);
// 共享内存
uint64 shmgetat(int, int);
int shmrefcount(int);
// 消息队列
int mqget(uint);               // 申请使用某个消息队列
int msgsnd(uint, void *, int); // 发送消息
int msgrcv(uint, void *, int); // 接收消息
// 文件权限
int chmod(const char*,char);
// 内核线程
int clone(uint64,uint64,uint64);
int join(uint64);
int thread_exit(int) __attribute__((noreturn));

// ulib.c
int stat(const char *, struct stat *);
char *strcpy(char *, const char *);
void *memmove(void *, const void *, int);
char *strchr(const char *, char c);
int strcmp(const char *, const char *);
void fprintf(int, const char *, ...);
void printf(const char *, ...);
char *gets(char *, int);
uint strlen(const char *);
void *memset(void *, int, uint);
void *malloc(uint);
void free(void *);
int atoi(const char *);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
int statistics(void *buf, int sz);
// uthread.c
int thread_join(void);
int thread_create(void(*start_routine)(void*),void*arg);
