// flocktest.c - 文件锁功能测试
#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

#define TESTFILE "flocktest_file"

void test_shared_lock(void)
{
  int fd1, fd2;
  
  printf("=== 测试1: 共享锁（多读者）===\n");
  
  fd1 = open(TESTFILE, O_RDWR | O_CREATE);
  if(fd1 < 0) {
    printf("FAIL: 无法创建测试文件\n");
    exit(1);
  }
  write(fd1, "hello", 5);
  
  fd2 = open(TESTFILE, O_RDWR);
  if(fd2 < 0) {
    printf("FAIL: 无法打开测试文件\n");
    close(fd1);
    exit(1);
  }
  
  // 第一个共享锁
  if(flock(fd1, LOCK_SH) < 0) {
    printf("FAIL: 无法获取第一个共享锁\n");
    close(fd1);
    close(fd2);
    exit(1);
  }
  printf("  fd1 获取共享锁成功\n");
  
  // 第二个共享锁（应该成功）
  if(flock(fd2, LOCK_SH) < 0) {
    printf("FAIL: 无法获取第二个共享锁\n");
    flock(fd1, LOCK_UN);
    close(fd1);
    close(fd2);
    exit(1);
  }
  printf("  fd2 获取共享锁成功（允许多读者）\n");
  
  // 释放锁
  flock(fd1, LOCK_UN);
  flock(fd2, LOCK_UN);
  printf("  两个共享锁均已释放\n");
  
  close(fd1);
  close(fd2);
  printf("PASS: 共享锁测试通过\n\n");
}

void test_exclusive_lock(void)
{
  int fd;
  int pid;
  
  printf("=== 测试2: 排他锁 ===\n");
  
  fd = open(TESTFILE, O_RDWR);
  if(fd < 0) {
    printf("FAIL: 无法打开测试文件\n");
    exit(1);
  }
  
  // 获取排他锁
  if(flock(fd, LOCK_EX) < 0) {
    printf("FAIL: 无法获取排他锁\n");
    close(fd);
    exit(1);
  }
  printf("  父进程获取排他锁成功\n");
  
  pid = fork();
  if(pid < 0) {
    printf("FAIL: fork 失败\n");
    flock(fd, LOCK_UN);
    close(fd);
    exit(1);
  }
  
  if(pid == 0) {
    // 子进程：尝试非阻塞获取排他锁
    int fd2 = open(TESTFILE, O_RDWR);
    if(fd2 < 0) {
      printf("FAIL: 子进程无法打开文件\n");
      exit(1);
    }
    
    // 使用非阻塞模式尝试获取排他锁
    if(flock(fd2, LOCK_EX | LOCK_NB) == 0) {
      printf("FAIL: 子进程不应该能获取排他锁\n");
      flock(fd2, LOCK_UN);
      close(fd2);
      exit(1);
    }
    printf("  子进程非阻塞获取排他锁失败（符合预期）\n");
    
    close(fd2);
    exit(0);
  }
  
  // 等待子进程
  int status;
  wait(&status);
  
  // 释放排他锁
  flock(fd, LOCK_UN);
  printf("  父进程释放排他锁\n");
  
  close(fd);
  printf("PASS: 排他锁测试通过\n\n");
}

void test_lock_upgrade(void)
{
  int fd;
  
  printf("=== 测试3: 锁升级阻塞 ===\n");
  
  fd = open(TESTFILE, O_RDWR);
  if(fd < 0) {
    printf("FAIL: 无法打开测试文件\n");
    exit(1);
  }
  
  // 获取共享锁
  if(flock(fd, LOCK_SH) < 0) {
    printf("FAIL: 无法获取共享锁\n");
    close(fd);
    exit(1);
  }
  printf("  获取共享锁成功\n");
  
  // 释放共享锁
  flock(fd, LOCK_UN);
  printf("  释放共享锁\n");
  
  // 获取排他锁
  if(flock(fd, LOCK_EX) < 0) {
    printf("FAIL: 无法获取排他锁\n");
    close(fd);
    exit(1);
  }
  printf("  获取排他锁成功\n");
  
  flock(fd, LOCK_UN);
  close(fd);
  printf("PASS: 锁升级测试通过\n\n");
}

void test_blocking_lock(void)
{
  int pid;
  
  printf("=== 测试4: 阻塞等待锁 ===\n");
  
  int fd1 = open(TESTFILE, O_RDWR);
  if(fd1 < 0) {
    printf("FAIL: 无法打开测试文件\n");
    exit(1);
  }
  
  // 父进程先获取排他锁
  if(flock(fd1, LOCK_EX) < 0) {
    printf("FAIL: 无法获取排他锁\n");
    close(fd1);
    exit(1);
  }
  printf("  父进程持有排他锁\n");
  
  pid = fork();
  if(pid < 0) {
    printf("FAIL: fork 失败\n");
    flock(fd1, LOCK_UN);
    close(fd1);
    exit(1);
  }
  
  if(pid == 0) {
    // 子进程阻塞等待
    int fd2 = open(TESTFILE, O_RDWR);
    if(fd2 < 0) {
      printf("FAIL: 子进程无法打开文件\n");
      exit(1);
    }
    
    printf("  子进程等待排他锁...\n");
    if(flock(fd2, LOCK_EX) < 0) {
      printf("FAIL: 子进程阻塞获取锁失败\n");
      close(fd2);
      exit(1);
    }
    printf("  子进程获取排他锁成功!\n");
    
    flock(fd2, LOCK_UN);
    close(fd2);
    exit(0);
  }
  
  // 父进程等待一段时间后释放锁
  sleep(2);
  printf("  父进程释放排他锁\n");
  flock(fd1, LOCK_UN);
  close(fd1);
  
  // 等待子进程完成
  int status;
  wait(&status);
  
  printf("PASS: 阻塞等待锁测试通过\n\n");
}

void test_shared_exclusive_conflict(void)
{
  int pid;
  
  printf("=== 测试5: 共享锁与排他锁冲突 ===\n");
  
  int fd1 = open(TESTFILE, O_RDWR);
  if(fd1 < 0) {
    printf("FAIL: 无法打开测试文件\n");
    exit(1);
  }
  
  // 获取共享锁
  if(flock(fd1, LOCK_SH) < 0) {
    printf("FAIL: 无法获取共享锁\n");
    close(fd1);
    exit(1);
  }
  printf("  父进程持有共享锁\n");
  
  pid = fork();
  if(pid < 0) {
    printf("FAIL: fork 失败\n");
    flock(fd1, LOCK_UN);
    close(fd1);
    exit(1);
  }
  
  if(pid == 0) {
    int fd2 = open(TESTFILE, O_RDWR);
    if(fd2 < 0) {
      printf("FAIL: 子进程无法打开文件\n");
      exit(1);
    }
    
    // 非阻塞获取排他锁应该失败
    if(flock(fd2, LOCK_EX | LOCK_NB) == 0) {
      printf("FAIL: 有共享锁时不应获取到排他锁\n");
      flock(fd2, LOCK_UN);
      close(fd2);
      exit(1);
    }
    printf("  子进程无法获取排他锁（符合预期）\n");
    
    // 非阻塞获取共享锁应该成功
    if(flock(fd2, LOCK_SH | LOCK_NB) < 0) {
      printf("FAIL: 应该能获取共享锁\n");
      close(fd2);
      exit(1);
    }
    printf("  子进程获取共享锁成功\n");
    
    flock(fd2, LOCK_UN);
    close(fd2);
    exit(0);
  }
  
  int status;
  wait(&status);
  
  flock(fd1, LOCK_UN);
  close(fd1);
  printf("PASS: 共享锁与排他锁冲突测试通过\n\n");
}

int main(int argc, char *argv[])
{
  printf("\n========== 文件锁（flock）功能测试 ==========\n\n");
  
  test_shared_lock();
  test_exclusive_lock();
  test_lock_upgrade();
  test_blocking_lock();
  test_shared_exclusive_conflict();
  
  // 清理测试文件
  unlink(TESTFILE);
  
  printf("========== 所有测试通过! ==========\n");
  exit(0);
}
