#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"
#include "fs.h"

#define TESTFILE "fallocfile"
#define TESTSIZE (300 * BSIZE)

int
main(void)
{
  int fd;
  struct stat st;
  int total = 0;
  char buf[256];

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if(fd < 0){
    printf("fallocatetest: open %s failed\n", TESTFILE);
    exit(1);
  }

  // fallocate(fd, offset, len, flags) - pre-allocate TESTSIZE bytes from offset 0
  if(fallocate(fd, 0, TESTSIZE, 0) < 0){
    printf("fallocatetest: fallocate failed\n");
    close(fd);
    exit(1);
  }
  if(fstat(fd, &st) < 0 || st.size != TESTSIZE){
    printf("fallocatetest: size %d expected %d\n", st.size, TESTSIZE);
    close(fd);
    exit(1);
  }
  close(fd);

  fd = open(TESTFILE, O_RDONLY);
  if(fd < 0){
    printf("fallocatetest: reopen %s failed\n", TESTFILE);
    exit(1);
  }
  for(;;){
    int n = read(fd, buf, sizeof(buf));
    if(n < 0){
      printf("fallocatetest: read failed\n");
      close(fd);
      unlink(TESTFILE);
      exit(1);
    }
    if(n == 0)
      break;
    for(int i = 0; i < n; i++){
      if(buf[i] != 0){
        printf("fallocatetest: non-zero data\n");
        close(fd);
        unlink(TESTFILE);
        exit(1);
      }
    }
    total += n;
  }
  close(fd);
  unlink(TESTFILE);

  if(total != TESTSIZE){
    printf("fallocatetest: total %d expected %d\n", total, TESTSIZE);
    exit(1);
  }

  fd = open(TESTFILE, O_CREATE | O_RDWR);
  if(fd < 0){
    printf("fallocatetest: open %s failed\n", TESTFILE);
    exit(1);
  }
  if(write(fd, buf, 1) != 1){
    printf("fallocatetest: write failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }
  // Test FALLOC_KEEP_SIZE: pre-allocate but keep size at 1
  if(fallocate(fd, 0, TESTSIZE, FALLOC_KEEP_SIZE) < 0){
    printf("fallocatetest: fallocate keep size failed\n");
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }
  if(fstat(fd, &st) < 0 || st.size != 1){
    printf("fallocatetest: keep size %d expected 1\n", st.size);
    close(fd);
    unlink(TESTFILE);
    exit(1);
  }
  close(fd);
  unlink(TESTFILE);

  printf("fallocatetest ok\n");
  exit(0);
}
