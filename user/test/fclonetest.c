#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"
#include "fs.h"

#define SRCFILE "fclone_src"
#define DSTFILE "fclone_dst"
#define NBLK 4

static void
fill_pattern(char *buf, int blk)
{
  for(int i = 0; i < BSIZE; i++)
    buf[i] = 'a' + (blk + i) % 26;
}

static int
check_pattern(char *buf, int blk)
{
  for(int i = 0; i < BSIZE; i++){
    if(buf[i] != (char)('a' + (blk + i) % 26))
      return -1;
  }
  return 0;
}

int
main(void)
{
  int fd;
  char buf[BSIZE];

  fd = open(SRCFILE, O_CREATE | O_RDWR);
  if(fd < 0){
    printf("fclonetest: open %s failed\n", SRCFILE);
    exit(1);
  }
  for(int b = 0; b < NBLK; b++){
    fill_pattern(buf, b);
    if(write(fd, buf, BSIZE) != BSIZE){
      printf("fclonetest: write src failed\n");
      close(fd);
      exit(1);
    }
  }
  close(fd);

  fd = open(DSTFILE, O_CREATE | O_RDWR);
  if(fd < 0){
    printf("fclonetest: open %s failed\n", DSTFILE);
    exit(1);
  }
  close(fd);
  if(fclone(SRCFILE, DSTFILE) == 0){
    printf("fclonetest: clone should fail when dst exists\n");
    unlink(DSTFILE);
    exit(1);
  }
  unlink(DSTFILE);

  if(fclone(SRCFILE, DSTFILE) < 0){
    printf("fclonetest: clone failed\n");
    unlink(SRCFILE);
    exit(1);
  }

  fd = open(DSTFILE, O_RDONLY);
  if(fd < 0){
    printf("fclonetest: open %s failed\n", DSTFILE);
    exit(1);
  }
  for(int b = 0; b < NBLK; b++){
    if(read(fd, buf, BSIZE) != BSIZE || check_pattern(buf, b) < 0){
      printf("fclonetest: read clone mismatch\n");
      close(fd);
      exit(1);
    }
  }
  close(fd);

  fd = open(DSTFILE, O_RDWR);
  if(fd < 0){
    printf("fclonetest: reopen %s failed\n", DSTFILE);
    exit(1);
  }
  memset(buf, 'Z', BSIZE);
  if(write(fd, buf, BSIZE) != BSIZE){
    printf("fclonetest: write clone failed\n");
    close(fd);
    exit(1);
  }
  close(fd);

  fd = open(SRCFILE, O_RDONLY);
  if(fd < 0){
    printf("fclonetest: reopen %s failed\n", SRCFILE);
    exit(1);
  }
  if(read(fd, buf, BSIZE) != BSIZE || check_pattern(buf, 0) < 0){
    printf("fclonetest: source changed after clone write\n");
    close(fd);
    exit(1);
  }
  close(fd);

  fd = open(DSTFILE, O_RDONLY);
  if(fd < 0){
    printf("fclonetest: reopen %s failed\n", DSTFILE);
    exit(1);
  }
  if(read(fd, buf, BSIZE) != BSIZE){
    printf("fclonetest: read clone failed\n");
    close(fd);
    exit(1);
  }
  for(int i = 0; i < BSIZE; i++){
    if(buf[i] != 'Z'){
      printf("fclonetest: clone data not updated\n");
      close(fd);
      exit(1);
    }
  }
  close(fd);

  unlink(SRCFILE);
  unlink(DSTFILE);

  printf("fclonetest ok\n");
  exit(0);
}
