// lseektest.c - lseek 系统调用测试程序
// 测试文件偏移量的设置功能

#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

#define TESTFILE "lseekfile"

void test_basic_seek(void);
void test_seek_set(void);
void test_seek_cur(void);
void test_seek_end(void);
void test_random_access(void);
void test_seek_beyond_eof(void);
void test_invalid_seek(void);

int
main(void)
{
  printf("lseektest starting\n");

  test_basic_seek();
  test_seek_set();
  test_seek_cur();
  test_seek_end();
  test_random_access();
  test_seek_beyond_eof();
  test_invalid_seek();

  printf("lseektest: all tests passed!\n");
  exit(0);
}

// 测试1: 基本的 lseek 功能
void test_basic_seek(void)
{
  int fd;
  char buf[32];
  int pos;

  printf("test_basic_seek...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open %s failed\n", TESTFILE);
    exit(1);
  }

  // 写入测试数据
  if (write(fd, "Hello, World!", 13) != 13) {
    printf("FAIL: write failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 使用 SEEK_SET 回到开头
  pos = lseek(fd, 0, SEEK_SET);
  if (pos != 0) {
    printf("FAIL: lseek to 0 returned %d, expected 0\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 读取数据验证
  memset(buf, 0, sizeof(buf));
  if (read(fd, buf, 5) != 5) {
    printf("FAIL: read failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  if (strcmp(buf, "Hello") != 0) {
    printf("FAIL: read data mismatch, got '%s'\n", buf);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_basic_seek passed\n");
}

// 测试2: SEEK_SET - 从文件开头定位
void test_seek_set(void)
{
  int fd;
  char buf[32];
  int pos;

  printf("test_seek_set...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  write(fd, "ABCDEFGHIJ", 10);

  // 定位到第5个字节
  pos = lseek(fd, 5, SEEK_SET);
  if (pos != 5) {
    printf("FAIL: SEEK_SET to 5 returned %d\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 读取剩余数据
  memset(buf, 0, sizeof(buf));
  read(fd, buf, 5);
  if (strcmp(buf, "FGHIJ") != 0) {
    printf("FAIL: expected 'FGHIJ', got '%s'\n", buf);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_seek_set passed\n");
}

// 测试3: SEEK_CUR - 从当前位置定位
void test_seek_cur(void)
{
  int fd;
  char buf[32];
  int pos;

  printf("test_seek_cur...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  write(fd, "0123456789", 10);
  lseek(fd, 0, SEEK_SET);

  // 先读取3个字节，当前位置变为3
  read(fd, buf, 3);

  // 从当前位置向前移动2个字节
  pos = lseek(fd, 2, SEEK_CUR);
  if (pos != 5) {
    printf("FAIL: SEEK_CUR +2 from 3 returned %d, expected 5\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 读取验证
  memset(buf, 0, sizeof(buf));
  read(fd, buf, 1);
  if (buf[0] != '5') {
    printf("FAIL: expected '5', got '%c'\n", buf[0]);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 测试负向移动
  pos = lseek(fd, -3, SEEK_CUR);
  if (pos != 3) {
    printf("FAIL: SEEK_CUR -3 from 6 returned %d, expected 3\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  memset(buf, 0, sizeof(buf));
  read(fd, buf, 1);
  if (buf[0] != '3') {
    printf("FAIL: expected '3', got '%c'\n", buf[0]);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_seek_cur passed\n");
}

// 测试4: SEEK_END - 从文件末尾定位
void test_seek_end(void)
{
  int fd;
  char buf[32];
  int pos;

  printf("test_seek_end...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  write(fd, "TESTDATA", 8);

  // 定位到文件末尾
  pos = lseek(fd, 0, SEEK_END);
  if (pos != 8) {
    printf("FAIL: SEEK_END 0 returned %d, expected 8\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 从末尾向前移动5个字节
  pos = lseek(fd, -5, SEEK_END);
  if (pos != 3) {
    printf("FAIL: SEEK_END -5 returned %d, expected 3\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  memset(buf, 0, sizeof(buf));
  read(fd, buf, 5);
  if (strcmp(buf, "TDATA") != 0) {
    printf("FAIL: expected 'TDATA', got '%s'\n", buf);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_seek_end passed\n");
}

// 测试5: 随机访问读写
void test_random_access(void)
{
  int fd;
  char buf[32];

  printf("test_random_access...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  // 写入初始数据
  write(fd, "AAAAAAAAAA", 10);

  // 随机位置写入
  lseek(fd, 3, SEEK_SET);
  write(fd, "BBB", 3);

  lseek(fd, 7, SEEK_SET);
  write(fd, "CC", 2);

  // 验证结果
  lseek(fd, 0, SEEK_SET);
  memset(buf, 0, sizeof(buf));
  read(fd, buf, 10);

  if (strcmp(buf, "AAABBBACCA") != 0) {
    printf("FAIL: expected 'AAABBBACCA', got '%s'\n", buf);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_random_access passed\n");
}

// 测试6: seek 超过文件末尾
void test_seek_beyond_eof(void)
{
  int fd;
  int pos;

  printf("test_seek_beyond_eof...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  write(fd, "TEST", 4);

  // seek 到超过文件末尾的位置（这是允许的）
  pos = lseek(fd, 100, SEEK_SET);
  if (pos != 100) {
    printf("FAIL: seek beyond EOF returned %d, expected 100\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_seek_beyond_eof passed\n");
}

// 测试7: 无效的 seek 操作
void test_invalid_seek(void)
{
  int fd;
  int pos;

  printf("test_invalid_seek...\n");

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if (fd < 0) {
    printf("FAIL: open failed\n");
    exit(1);
  }

  write(fd, "DATA", 4);

  // 尝试 seek 到负数位置（应该失败）
  pos = lseek(fd, -10, SEEK_SET);
  if (pos != -1) {
    printf("FAIL: negative seek should return -1, got %d\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  // 无效的 whence 值（应该失败）
  pos = lseek(fd, 0, 99);
  if (pos != -1) {
    printf("FAIL: invalid whence should return -1, got %d\n", pos);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }

  close(fd);
  unlink(TESTFILE);
  printf("test_invalid_seek passed\n");
}
