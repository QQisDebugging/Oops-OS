// fsynctest.c - 文件同步功能测试
// 测试 fsync 和 fdatasync 系统调用

#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

#define TESTFILE "fsynctest_file"

void test_fsync_basic(void)
{
  int fd;
  char buf[512];
  
  printf("=== 测试1: fsync 基本功能 ===\n");
  
  fd = open(TESTFILE, O_RDWR | O_CREATE);
  if(fd < 0) {
    printf("FAIL: 无法创建测试文件\n");
    exit(1);
  }
  
  // 写入数据
  memset(buf, 'A', sizeof(buf));
  if(write(fd, buf, sizeof(buf)) != sizeof(buf)) {
    printf("FAIL: 写入失败\n");
    close(fd);
    exit(1);
  }
  printf("  写入 512 字节数据\n");
  
  // 调用 fsync 确保数据持久化
  if(fsync(fd) < 0) {
    printf("FAIL: fsync 调用失败\n");
    close(fd);
    exit(1);
  }
  printf("  fsync 调用成功\n");
  
  close(fd);
  
  // 重新打开验证数据
  fd = open(TESTFILE, O_RDONLY);
  if(fd < 0) {
    printf("FAIL: 无法重新打开文件\n");
    exit(1);
  }
  
  memset(buf, 0, sizeof(buf));
  if(read(fd, buf, sizeof(buf)) != sizeof(buf)) {
    printf("FAIL: 读取失败\n");
    close(fd);
    exit(1);
  }
  
  // 验证数据正确性
  for(int i = 0; i < sizeof(buf); i++) {
    if(buf[i] != 'A') {
      printf("FAIL: 数据不一致，位置 %d\n", i);
      close(fd);
      exit(1);
    }
  }
  printf("  数据验证正确\n");
  
  close(fd);
  printf("PASS: fsync 基本功能测试通过\n\n");
}

void test_fdatasync_basic(void)
{
  int fd;
  char buf[512];
  
  printf("=== 测试2: fdatasync 基本功能 ===\n");
  
  fd = open(TESTFILE, O_RDWR | O_TRUNC);
  if(fd < 0) {
    printf("FAIL: 无法打开测试文件\n");
    exit(1);
  }
  
  // 写入数据
  memset(buf, 'B', sizeof(buf));
  if(write(fd, buf, sizeof(buf)) != sizeof(buf)) {
    printf("FAIL: 写入失败\n");
    close(fd);
    exit(1);
  }
  printf("  写入 512 字节数据\n");
  
  // 调用 fdatasync 仅同步数据
  if(fdatasync(fd) < 0) {
    printf("FAIL: fdatasync 调用失败\n");
    close(fd);
    exit(1);
  }
  printf("  fdatasync 调用成功\n");
  
  close(fd);
  
  // 重新打开验证
  fd = open(TESTFILE, O_RDONLY);
  if(fd < 0) {
    printf("FAIL: 无法重新打开文件\n");
    exit(1);
  }
  
  memset(buf, 0, sizeof(buf));
  if(read(fd, buf, sizeof(buf)) != sizeof(buf)) {
    printf("FAIL: 读取失败\n");
    close(fd);
    exit(1);
  }
  
  for(int i = 0; i < sizeof(buf); i++) {
    if(buf[i] != 'B') {
      printf("FAIL: 数据不一致\n");
      close(fd);
      exit(1);
    }
  }
  printf("  数据验证正确\n");
  
  close(fd);
  printf("PASS: fdatasync 基本功能测试通过\n\n");
}

void test_multiple_writes_fsync(void)
{
  int fd;
  char buf[256];
  
  printf("=== 测试3: 多次写入后 fsync ===\n");
  
  fd = open(TESTFILE, O_RDWR | O_TRUNC);
  if(fd < 0) {
    printf("FAIL: 无法打开测试文件\n");
    exit(1);
  }
  
  // 多次写入
  for(int i = 0; i < 4; i++) {
    memset(buf, 'C' + i, sizeof(buf));
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("FAIL: 第 %d 次写入失败\n", i);
      close(fd);
      exit(1);
    }
  }
  printf("  完成 4 次写入共 1024 字节\n");
  
  // 一次 fsync 同步所有数据
  if(fsync(fd) < 0) {
    printf("FAIL: fsync 调用失败\n");
    close(fd);
    exit(1);
  }
  printf("  fsync 调用成功\n");
  
  close(fd);
  
  // 验证数据
  fd = open(TESTFILE, O_RDONLY);
  if(fd < 0) {
    printf("FAIL: 无法重新打开文件\n");
    exit(1);
  }
  
  for(int i = 0; i < 4; i++) {
    memset(buf, 0, sizeof(buf));
    if(read(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("FAIL: 第 %d 次读取失败\n", i);
      close(fd);
      exit(1);
    }
    for(int j = 0; j < sizeof(buf); j++) {
      if(buf[j] != 'C' + i) {
        printf("FAIL: 第 %d 块数据不一致\n", i);
        close(fd);
        exit(1);
      }
    }
  }
  printf("  所有数据验证正确\n");
  
  close(fd);
  printf("PASS: 多次写入后 fsync 测试通过\n\n");
}

void test_fsync_invalid_fd(void)
{
  printf("=== 测试4: 无效文件描述符 ===\n");
  
  // 对无效 fd 调用 fsync 应失败
  if(fsync(100) == 0) {
    printf("FAIL: 对无效 fd 的 fsync 应失败\n");
    exit(1);
  }
  printf("  对无效 fd 调用 fsync 返回错误（符合预期）\n");
  
  if(fdatasync(100) == 0) {
    printf("FAIL: 对无效 fd 的 fdatasync 应失败\n");
    exit(1);
  }
  printf("  对无效 fd 调用 fdatasync 返回错误（符合预期）\n");
  
  printf("PASS: 无效文件描述符测试通过\n\n");
}

int main(int argc, char *argv[])
{
  printf("\n========== 文件同步（fsync/fdatasync）功能测试 ==========\n\n");
  
  test_fsync_basic();
  test_fdatasync_basic();
  test_multiple_writes_fsync();
  test_fsync_invalid_fd();
  
  // 清理测试文件
  unlink(TESTFILE);
  
  printf("========== 所有测试通过! ==========\n");
  exit(0);
}
