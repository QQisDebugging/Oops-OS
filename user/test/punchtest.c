// punchtest.c - 测试 FALLOC_FL_PUNCH_HOLE 稀疏文件打洞功能
// 验证释放文件中间区域的块后，读取该区域返回全零

#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"
#include "fs.h"

#define TESTFILE "punchfile"

static void
fail(const char *msg)
{
  printf("punchtest FAIL: %s\n", msg);
  unlink(TESTFILE);
  exit(1);
}

static void
fill_block(int fd, int bn, char val)
{
  char buf[BSIZE];
  memset(buf, val, BSIZE);
  if(lseek(fd, bn * BSIZE, SEEK_SET) < 0)
    fail("lseek failed");
  if(write(fd, buf, BSIZE) != BSIZE)
    fail("write failed");
}

static void
check_block(int fd, int bn, char expected)
{
  char buf[BSIZE];
  if(lseek(fd, bn * BSIZE, SEEK_SET) < 0)
    fail("lseek failed");
  if(read(fd, buf, BSIZE) != BSIZE)
    fail("read failed");
  for(int i = 0; i < BSIZE; i++){
    if(buf[i] != expected){
      printf("punchtest: block %d byte %d: got %d expected %d\n",
             bn, i, buf[i], expected);
      fail("data mismatch");
    }
  }
}

int
main(void)
{
  int fd;
  struct stat st;

  printf("punchtest starting\n");

  // Create a file with 5 blocks (0-4), each filled with block number.
  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if(fd < 0)
    fail("open failed");

  for(int i = 0; i < 5; i++)
    fill_block(fd, i, 'A' + i);

  // Verify file size.
  if(fstat(fd, &st) < 0 || st.size != 5 * BSIZE)
    fail("initial size mismatch");

  printf("  created file with 5 blocks\n");

  // Punch a hole in blocks 1-3 (middle blocks).
  // fallocate(fd, offset, len, flags)
  if(fallocate(fd, 1 * BSIZE, 3 * BSIZE, FALLOC_FL_PUNCH_HOLE) < 0)
    fail("fallocate punch hole failed");

  printf("  punched hole in blocks 1-3\n");

  // File size should remain unchanged.
  if(fstat(fd, &st) < 0 || st.size != 5 * BSIZE){
    printf("punchtest: size after punch %d expected %d\n", st.size, 5 * BSIZE);
    fail("size changed after punch");
  }

  // Block 0 should be unchanged.
  check_block(fd, 0, 'A');
  printf("  block 0 unchanged: OK\n");

  // Blocks 1-3 should now read as zeros (sparse hole).
  for(int i = 1; i <= 3; i++){
    char buf[BSIZE];
    if(lseek(fd, i * BSIZE, SEEK_SET) < 0)
      fail("lseek to hole failed");
    if(read(fd, buf, BSIZE) != BSIZE)
      fail("read hole failed");
    for(int j = 0; j < BSIZE; j++){
      if(buf[j] != 0){
        printf("punchtest: hole block %d byte %d not zero\n", i, j);
        fail("hole not zero");
      }
    }
  }
  printf("  blocks 1-3 read as zeros: OK\n");

  // Block 4 should be unchanged.
  check_block(fd, 4, 'E');
  printf("  block 4 unchanged: OK\n");

  // Test: write to a punched block should allocate new block.
  fill_block(fd, 2, 'X');
  check_block(fd, 2, 'X');
  printf("  write to punched block: OK\n");

  // Other punched blocks should still be zeros.
  for(int i = 1; i <= 3; i++){
    if(i == 2) continue;
    char buf[BSIZE];
    if(lseek(fd, i * BSIZE, SEEK_SET) < 0)
      fail("lseek failed");
    if(read(fd, buf, BSIZE) != BSIZE)
      fail("read failed");
    for(int j = 0; j < BSIZE; j++){
      if(buf[j] != 0)
        fail("other punched block modified");
    }
  }
  printf("  other punched blocks still zero: OK\n");

  close(fd);
  unlink(TESTFILE);

  printf("punchtest: OK\n");
  exit(0);
}
