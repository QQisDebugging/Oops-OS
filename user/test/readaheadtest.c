#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"
#include "fs.h"

#define TESTFILE "rahfile"
#define NBLKS 24

static void
fill_block(char *buf, int val)
{
  for (int i = 0; i < BSIZE / sizeof(int); i++) {
    ((int *)buf)[i] = val;
  }
}

static void
write_blocks(void)
{
  char buf[BSIZE];
  int fd = open(TESTFILE, O_CREATE | O_TRUNC | O_RDWR);
  if (fd < 0) {
    printf("readaheadtest: open %s failed\n", TESTFILE);
    exit(1);
  }

  for (int i = 0; i < NBLKS; i++) {
    fill_block(buf, i);
    if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("readaheadtest: write failed at block %d\n", i);
      close(fd);
      exit(1);
    }
  }
  close(fd);
}

static int
read_blocks(int quiet)
{
  char buf[BSIZE];
  int fd = open(TESTFILE, O_RDONLY);
  if (fd < 0) {
    printf("readaheadtest: open %s failed\n", TESTFILE);
    exit(1);
  }

  int start = uptime();
  for (int i = 0; i < NBLKS; i++) {
    if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("readaheadtest: read failed at block %d\n", i);
      close(fd);
      exit(1);
    }
    if (((int *)buf)[0] != i) {
      printf("readaheadtest: bad data at block %d\n", i);
      close(fd);
      exit(1);
    }
    if (!quiet && (i + 1) % (NBLKS / 3 + 1) == 0)
      printf("readaheadtest: read %d/%d\n", i + 1, NBLKS);
  }
  int end = uptime();
  close(fd);
  return end - start;
}

int
main(int argc, char *argv[])
{
  int quiet = 0;
  if (argc > 1 && strcmp(argv[1], "quiet") == 0)
    quiet = 1;

  unlink(TESTFILE);
  write_blocks();

  int t1 = read_blocks(quiet);
  int t2 = read_blocks(quiet);

  if (!quiet)
    printf("readaheadtest: pass1 %d ticks, pass2 %d ticks\n", t1, t2);

  if (t1 >= 3 && t2 > t1 + t1 / 2) {
    printf("readaheadtest: second pass too slow\n");
    unlink(TESTFILE);
    exit(1);
  }

  unlink(TESTFILE);
  printf("readaheadtest: ok\n");
  exit(0);
}
