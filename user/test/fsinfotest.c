#include "types.h"
#include "stat.h"
#include "fsinfo.h"
#include "user/user.h"
#include "fcntl.h"
#include "fs.h"

static void
die(const char *msg)
{
  printf("fsinfotest: %s\n", msg);
  exit(1);
}

int
main(int argc, char *argv[])
{
  struct fsinfo before;
  struct fsinfo after;
  struct fsinfo final;
  char buf[BSIZE];

  (void)argc;
  (void)argv;

  if (fsinfo(&before) < 0)
    die("fsinfo failed");
  if (before.bsize != BSIZE || before.total_blocks == 0 || before.total_inodes == 0)
    die("invalid fsinfo values");
  if (before.free_blocks > before.total_blocks || before.free_inodes > before.total_inodes)
    die("free counts exceed total");

  int fd = open("fsinfo_tmp", O_CREATE | O_RDWR);
  if (fd < 0)
    die("open fsinfo_tmp failed");
  memset(buf, 'a', sizeof(buf));
  if (write(fd, buf, sizeof(buf)) != sizeof(buf))
    die("write fsinfo_tmp failed");
  close(fd);

  if (fsinfo(&after) < 0)
    die("fsinfo after failed");
  if (after.free_blocks > before.free_blocks)
    die("free blocks increased after allocation");
  if (after.free_inodes > before.free_inodes)
    die("free inodes increased after allocation");

  if (unlink("fsinfo_tmp") < 0)
    die("unlink fsinfo_tmp failed");

  if (fsinfo(&final) < 0)
    die("fsinfo final failed");
  if (final.free_blocks < after.free_blocks)
    die("free blocks decreased after unlink");
  if (final.free_inodes < after.free_inodes)
    die("free inodes decreased after unlink");

  printf("fsinfotest: OK\n");
  exit(0);
}
