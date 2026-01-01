// truncatetest.c - truncate/ftruncate 系统调用测试程序
// 测试文件截断和扩展功能

#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

#define TESTFILE "truncfile"

void test_ftruncate_shrink(void);
void test_ftruncate_extend(void);
void test_truncate_by_path(void);
void test_truncate_with_lseek(void);
void test_invalid_truncate(void);

int
main(void)
{
  printf("truncatetest starting\n");

  test_ftruncate_shrink();
  test_ftruncate_extend();
  test_truncate_by_path();
  test_truncate_with_lseek();
  test_invalid_truncate();

  printf("truncatetest: all tests passed!\n");
  exit(0);
}

// 测试1: ftruncate 缩小文件
void test_ftruncate_shrink(void)
{
  int fd;
  struct stat st;
  char buf[32];

  printf("test_ftruncate_shrink...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  // 写入20字节
  if (write(fd, "12345678901234567890", 20) != 20) {
    printf("FAIL: write failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 截断到10字节
  if (ftruncate(fd, 10) < 0) {
    printf("FAIL: ftruncate failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 验证大小
  if (fstat(fd, &st) < 0 || st.size != 10) {
    printf("FAIL: size %d, expected 10\n", st.size);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 验证内容
  lseek(fd, 0, SEEK_SET);
  memset(buf, 0, sizeof(buf));
  if (read(fd, buf, 10) != 10) {
    printf("FAIL: read failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  if (strcmp(buf, "1234567890") != 0) {
    printf("FAIL: content mismatch, got '%s'\n", buf);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_ftruncate_shrink passed\n");
}

// 测试2: ftruncate 扩展文件
void test_ftruncate_extend(void)
{
  int fd;
  struct stat st;

  printf("test_ftruncate_extend...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  // 写入5字节
  if (write(fd, "Hello", 5) != 5) {
    printf("FAIL: write failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 扩展到20字节
  if (ftruncate(fd, 20) < 0) {
    printf("FAIL: ftruncate extend failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 验证大小
  if (fstat(fd, &st) < 0 || st.size != 20) {
    printf("FAIL: size %d, expected 20\n", st.size);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_ftruncate_extend passed\n");
}

// 测试3: truncate 按路径截断
void test_truncate_by_path(void)
{
  int fd;
  struct stat st;

  printf("test_truncate_by_path...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  // 写入30字节
  if (write(fd, "AAAAABBBBBCCCCCDDDDDEEEEEFFFFFF", 30) != 30) {
    printf("FAIL: write failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }
  close(fd);

  // 使用 truncate 按路径截断
  if (truncate(TESTFILE, 15) < 0) {
    printf("FAIL: truncate by path failed\n");
    unlink(TESTFILE);
    exit(1);
  }

  // 重新打开验证
  fd = open(TESTFILE, O_RDONLY);
  if (fd < 0) {
    printf("FAIL: reopen failed\n");
    unlink(TESTFILE);
    exit(1);
  }

  if (fstat(fd, &st) < 0 || st.size != 15) {
    printf("FAIL: size %d, expected 15\n", st.size);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_truncate_by_path passed\n");
}

// 测试4: truncate 与 lseek 配合使用
void test_truncate_with_lseek(void)
{
  int fd;
  char buf[32];

  printf("test_truncate_with_lseek...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  // 写入数据
  write(fd, "ABCDEFGHIJ", 10);

  // 移动到位置8
  lseek(fd, 8, SEEK_SET);

  // 截断到5字节（当前偏移量应该被调整）
  if (ftruncate(fd, 5) < 0) {
    printf("FAIL: ftruncate failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 读取验证（从调整后的位置读取应该读不到数据）
  memset(buf, 0, sizeof(buf));
  int n = read(fd, buf, 10);
  // 因为偏移量被调整到文件末尾(5)，应该读不到任何数据
  if (n != 0) {
    printf("FAIL: expected 0 bytes, got %d\n", n);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 回到开头读取
  lseek(fd, 0, SEEK_SET);
  memset(buf, 0, sizeof(buf));
  if (read(fd, buf, 10) != 5) {
    printf("FAIL: expected 5 bytes\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  if (strcmp(buf, "ABCDE") != 0) {
    printf("FAIL: content mismatch, got '%s'\n", buf);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_truncate_with_lseek passed\n");
}

// 测试5: 无效的 truncate 操作
void test_invalid_truncate(void)
{
  int fd;
  int ret;

  printf("test_invalid_truncate...\n");

  // 测试负数长度
  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }
  write(fd, "test", 4);

  ret = ftruncate(fd, -1);
  if (ret != -1) {
    printf("FAIL: negative length should return -1\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);

  // 测试不存在的文件
  ret = truncate("nonexistent_file", 10);
  if (ret != -1) {
    printf("FAIL: nonexistent file should return -1\n");
    unlink(TESTFILE);
    exit(1);
  }

  // 测试只读文件描述符
  fd = open(TESTFILE, O_RDONLY);
  if (fd >= 0) {
    ret = ftruncate(fd, 2);
    if (ret != -1) {
      printf("FAIL: read-only fd should return -1\n");
      close(fd);
      unlink(TESTFILE);
      exit(1);
    }
    close(fd);
  }

  unlink(TESTFILE);
  printf("test_invalid_truncate passed\n");
}
