#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"
#include "fs.h"

#define SRCFILE "fclonerange_src"
#define DSTFILE "fclonerange_dst"

static void
fillbuf(char *buf, char v)
{
  for(int i = 0; i < BSIZE; i++)
    buf[i] = v;
}

static int
checkbuf(char *buf, char v)
{
  for(int i = 0; i < BSIZE; i++){
    if(buf[i] != v)
      return -1;
  }
  return 0;
}

int
main(void)
{
  char buf[BSIZE];
  int fd_src, fd_dst;

  unlink(SRCFILE);
  unlink(DSTFILE);

  fd_src = open(SRCFILE, O_CREATE | O_RDWR);
  if(fd_src < 0){
    printf("fclonerangetest: open src failed\n");
    exit(1);
  }

  fillbuf(buf, 'A');
  if(write(fd_src, buf, BSIZE) != BSIZE){
    printf("fclonerangetest: write src block0 failed\n");
    close(fd_src);
    exit(1);
  }

  fillbuf(buf, 'X');
  if(write(fd_src, buf, BSIZE) != BSIZE){
    printf("fclonerangetest: write src block1 failed\n");
    close(fd_src);
    exit(1);
  }

  fillbuf(buf, 'B');
  if(write(fd_src, buf, BSIZE) != BSIZE){
    printf("fclonerangetest: write src block2 failed\n");
    close(fd_src);
    exit(1);
  }

  if(fallocate(fd_src, BSIZE, BSIZE, FALLOC_FL_PUNCH_HOLE) < 0){
    printf("fclonerangetest: punch hole failed\n");
    close(fd_src);
    exit(1);
  }
  close(fd_src);

  fd_dst = open(DSTFILE, O_CREATE | O_RDWR);
  if(fd_dst < 0){
    printf("fclonerangetest: open dst failed\n");
    exit(1);
  }
  fillbuf(buf, 'X');
  if(write(fd_dst, buf, BSIZE) != BSIZE){
    printf("fclonerangetest: prewrite dst block0 failed\n");
    close(fd_dst);
    exit(1);
  }
  if(write(fd_dst, buf, BSIZE) != BSIZE){
    printf("fclonerangetest: prewrite dst block1 failed\n");
    close(fd_dst);
    exit(1);
  }
  close(fd_dst);

  fd_src = open(SRCFILE, O_RDONLY);
  fd_dst = open(DSTFILE, O_RDWR);
  if(fd_src < 0 || fd_dst < 0){
    printf("fclonerangetest: reopen failed\n");
    exit(1);
  }
  if(fclonerange(fd_src, 0, fd_dst, 0, 3 * BSIZE) < 0){
    printf("fclonerangetest: fclonerange failed\n");
    close(fd_src);
    close(fd_dst);
    exit(1);
  }
  close(fd_src);
  close(fd_dst);

  fd_dst = open(DSTFILE, O_RDONLY);
  if(fd_dst < 0){
    printf("fclonerangetest: open dst verify failed\n");
    exit(1);
  }

  if(read(fd_dst, buf, BSIZE) != BSIZE || checkbuf(buf, 'A') < 0){
    printf("fclonerangetest: block0 mismatch\n");
    close(fd_dst);
    exit(1);
  }
  if(read(fd_dst, buf, BSIZE) != BSIZE || checkbuf(buf, 0) < 0){
    printf("fclonerangetest: hole not zero\n");
    close(fd_dst);
    exit(1);
  }
  if(read(fd_dst, buf, BSIZE) != BSIZE || checkbuf(buf, 'B') < 0){
    printf("fclonerangetest: block2 mismatch\n");
    close(fd_dst);
    exit(1);
  }
  close(fd_dst);

  fd_dst = open(DSTFILE, O_RDWR);
  if(fd_dst < 0){
    printf("fclonerangetest: open dst write failed\n");
    exit(1);
  }
  fillbuf(buf, 'Z');
  if(lseek(fd_dst, 0, SEEK_SET) != 0 || write(fd_dst, buf, BSIZE) != BSIZE){
    printf("fclonerangetest: write dst block0 failed\n");
    close(fd_dst);
    exit(1);
  }
  close(fd_dst);

  fd_src = open(SRCFILE, O_RDONLY);
  if(fd_src < 0){
    printf("fclonerangetest: open src verify failed\n");
    exit(1);
  }
  if(read(fd_src, buf, BSIZE) != BSIZE || checkbuf(buf, 'A') < 0){
    printf("fclonerangetest: COW failed\n");
    close(fd_src);
    exit(1);
  }
  close(fd_src);

  unlink(SRCFILE);
  unlink(DSTFILE);
  printf("fclonerangetest ok\n");
  exit(0);
}
