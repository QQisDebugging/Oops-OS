#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user/user.h"
#include "fs.h"

#define NFILES 32
#define NBLKS  8

static void
fill_block(char *buf, int val)
{
  memset(buf, val, BSIZE);
}

static void
make_name(char *buf, int i)
{
  buf[0] = 'b';
  buf[1] = 'h';
  buf[2] = 't';
  buf[3] = '0' + (i / 10);
  buf[4] = '0' + (i % 10);
  buf[5] = 0;
}

static void
create_files(int quiet)
{
  char name[8];
  char buf[BSIZE];

  for (int i = 0; i < NFILES; i++) {
    make_name(name, i);
    int fd = open(name, O_CREATE | O_TRUNC | O_RDWR);
    if (fd < 0) {
      printf("ballochinttest: open %s failed\n", name);
      exit(1);
    }
    for (int b = 0; b < NBLKS; b++) {
      fill_block(buf, (i + b) & 0xff);
      if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
        printf("ballochinttest: write failed\n");
        close(fd);
        exit(1);
      }
    }
    close(fd);
    if (!quiet && (i + 1) % 20 == 0)
      printf("ballochinttest: wrote %d/%d\n", i + 1, NFILES);
  }
}

static void
verify_files(int quiet)
{
  char name[8];
  char buf[BSIZE];

  for (int i = 0; i < NFILES; i++) {
    make_name(name, i);
    int fd = open(name, O_RDONLY);
    if (fd < 0) {
      printf("ballochinttest: open %s failed\n", name);
      exit(1);
    }
    if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("ballochinttest: read failed\n");
      close(fd);
      exit(1);
    }
    if ((buf[0] & 0xff) != (i & 0xff)) {
      printf("ballochinttest: bad data\n");
      close(fd);
      exit(1);
    }
    if (lseek(fd, (NBLKS - 1) * sizeof(buf), 0) < 0) {
      printf("ballochinttest: lseek failed\n");
      close(fd);
      exit(1);
    }
    if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("ballochinttest: read last failed\n");
      close(fd);
      exit(1);
    }
    if ((buf[0] & 0xff) != ((i + NBLKS - 1) & 0xff)) {
      printf("ballochinttest: bad last data\n");
      close(fd);
      exit(1);
    }
    close(fd);
    if (!quiet && (i + 1) % 20 == 0)
      printf("ballochinttest: read %d/%d\n", i + 1, NFILES);
  }
}

static void
unlink_files(void)
{
  char name[8];

  for (int i = 0; i < NFILES; i++) {
    make_name(name, i);
    unlink(name);
  }
}

int
main(int argc, char *argv[])
{
  int quiet = 0;
  if (argc > 1 && strcmp(argv[1], "quiet") == 0)
    quiet = 1;

  int start = uptime();
  for (int r = 0; r < 2; r++) {
    if (!quiet)
      printf("ballochinttest: round %d/2\n", r + 1);
    create_files(quiet);
    verify_files(quiet);
    unlink_files();
  }
  int end = uptime();
  if (!quiet)
    printf("ballochinttest: ticks %d\n", end - start);
  printf("ballochinttest: ok\n");
  exit(0);
}
