// renametest.c - rename 系统调用测试程序

#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

static void
fail(const char *msg)
{
  printf("FAIL: %s\n", msg);
  exit(1);
}

static void
write_all(int fd, const char *s)
{
  int n = strlen(s);
  if(write(fd, s, n) != n)
    fail("write failed");
}

static void
read_exact(int fd, char *buf, int n)
{
  int m = read(fd, buf, n);
  if(m != n)
    fail("read size mismatch");
}

static void
test_basic_rename(void)
{
  int fd;
  char buf[16];

  printf("test_basic_rename...\n");

  unlink("a");
  unlink("b");

  fd = open("a", O_CREATE | O_RDWR);
  if(fd < 0)
    fail("open a");
  write_all(fd, "hello");
  close(fd);

  if(rename("a", "b") < 0)
    fail("rename a->b");

  fd = open("a", O_RDONLY);
  if(fd >= 0)
    fail("a should not exist");

  fd = open("b", O_RDONLY);
  if(fd < 0)
    fail("open b");
  memset(buf, 0, sizeof(buf));
  read_exact(fd, buf, 5);
  close(fd);

  if(strcmp(buf, "hello") != 0)
    fail("content mismatch after rename");

  unlink("b");
  printf("test_basic_rename passed\n");
}

static void
test_overwrite_rename(void)
{
  int fd;
  char buf[16];

  printf("test_overwrite_rename...\n");

  unlink("b");
  unlink("c");

  fd = open("b", O_CREATE | O_RDWR);
  if(fd < 0)
    fail("open b");
  write_all(fd, "BBBB");
  close(fd);

  fd = open("c", O_CREATE | O_RDWR);
  if(fd < 0)
    fail("open c");
  write_all(fd, "CCCC");
  close(fd);

  if(rename("b", "c") < 0)
    fail("rename b->c overwrite");

  fd = open("b", O_RDONLY);
  if(fd >= 0)
    fail("b should not exist after overwrite");

  fd = open("c", O_RDONLY);
  if(fd < 0)
    fail("open c after overwrite");
  memset(buf, 0, sizeof(buf));
  read_exact(fd, buf, 4);
  close(fd);

  if(strcmp(buf, "BBBB") != 0)
    fail("overwrite content mismatch");

  unlink("c");
  printf("test_overwrite_rename passed\n");
}

static void
test_cross_dir_move(void)
{
  int fd;
  char buf[16];

  printf("test_cross_dir_move...\n");

  unlink("y");
  unlink("d/z");
  unlink("d/x");
  unlink("d");

  if(mkdir("d") < 0)
    fail("mkdir d");

  fd = open("d/x", O_CREATE | O_RDWR);
  if(fd < 0)
    fail("open d/x");
  write_all(fd, "MOVE");
  close(fd);

  if(rename("d/x", "y") < 0)
    fail("rename d/x -> y");

  fd = open("d/x", O_RDONLY);
  if(fd >= 0)
    fail("d/x should not exist");

  fd = open("y", O_RDONLY);
  if(fd < 0)
    fail("open y");
  memset(buf, 0, sizeof(buf));
  read_exact(fd, buf, 4);
  close(fd);
  if(strcmp(buf, "MOVE") != 0)
    fail("move content mismatch");

  if(rename("y", "d/z") < 0)
    fail("rename y -> d/z");

  fd = open("y", O_RDONLY);
  if(fd >= 0)
    fail("y should not exist");

  fd = open("d/z", O_RDONLY);
  if(fd < 0)
    fail("open d/z");
  memset(buf, 0, sizeof(buf));
  read_exact(fd, buf, 4);
  close(fd);
  if(strcmp(buf, "MOVE") != 0)
    fail("move-back content mismatch");

  unlink("d/z");
  unlink("d");
  printf("test_cross_dir_move passed\n");
}

static void
test_failures(void)
{
  int r;

  printf("test_failures...\n");

  unlink("no_such");
  unlink("tgt");

  r = rename("no_such", "tgt");
  if(r != -1)
    fail("rename from non-existent should fail");

  unlink("f");
  unlink("dir");
  if(mkdir("dir") < 0)
    fail("mkdir dir");

  int fd = open("f", O_CREATE | O_RDWR);
  if(fd < 0)
    fail("open f");
  write_all(fd, "X");
  close(fd);

  // 目标是目录应失败（当前实现不支持覆盖目录）。
  r = rename("f", "dir");
  if(r != -1)
    fail("rename to directory should fail");

  unlink("f");
  unlink("dir");
  printf("test_failures passed\n");
}

int
main(void)
{
  printf("renametest starting\n");
  test_basic_rename();
  test_overwrite_rename();
  test_cross_dir_move();
  test_failures();
  printf("renametest: all tests passed!\n");
  exit(0);
}
