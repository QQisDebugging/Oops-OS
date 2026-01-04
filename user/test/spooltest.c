#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user/user.h"

#define SPOOL 2

static const char kMsg1[] = "spooltest: job1 hello\n";
static const char kMsg2[] = "spooltest: job2 world\n";
static const int kExpectLen = sizeof(kMsg1) - 1 + sizeof(kMsg2) - 1;

static int quiet;

static void
fail(const char *msg)
{
  printf("spooltest: %s\n", msg);
  exit(1);
}

static void
spooler(void)
{
  int in = open("spool", O_RDONLY);
  if (in < 0)
    fail("open spool failed");
  unlink("spool.out");
  int out = open("spool.out", O_CREATE | O_WRONLY);
  if (out < 0)
    fail("open spool.out failed");

  char buf[128];
  int total = 0;
  while (total < kExpectLen) {
    int n = read(in, buf, sizeof(buf));
    if (n < 0)
      fail("read failed");
    if (n == 0)
      fail("unexpected eof");
    int remain = kExpectLen - total;
    if (n > remain)
      n = remain;
    if (write(out, buf, n) != n)
      fail("write spool.out failed");
    total += n;
  }
  close(in);
  close(out);
  exit(0);
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "quiet") == 0)
    quiet = 1;

  if (!quiet)
    printf("spooltest: start\n");

  int fd = open("spool", O_RDWR);
  if (fd < 0) {
    if (mknod("spool", SPOOL, 0) < 0)
      fail("mknod spool failed");
    fd = open("spool", O_RDWR);
  }
  if (fd < 0)
    fail("open spool failed");
  close(fd);

  int pid = fork();
  if (pid < 0)
    fail("fork failed");
  if (pid == 0)
    spooler();
  if (!quiet)
    printf("spooltest: spooler pid %d\n", pid);

  int out = open("spool", O_WRONLY);
  if (out < 0)
    fail("open spool writer failed");
  if (write(out, kMsg1, sizeof(kMsg1) - 1) != sizeof(kMsg1) - 1)
    fail("write msg1 failed");
  if (write(out, kMsg2, sizeof(kMsg2) - 1) != sizeof(kMsg2) - 1)
    fail("write msg2 failed");
  close(out);
  if (!quiet)
    printf("spooltest: writer done\n");

  if (wait(0) < 0)
    fail("wait failed");
  if (!quiet)
    printf("spooltest: spooler finished\n");

  int in = open("spool.out", O_RDONLY);
  if (in < 0)
    fail("open spool.out failed");
  char expect[sizeof(kMsg1) + sizeof(kMsg2) - 2];
  memmove(expect, kMsg1, sizeof(kMsg1) - 1);
  memmove(expect + sizeof(kMsg1) - 1, kMsg2, sizeof(kMsg2) - 1);
  int expect_len = sizeof(expect);

  char got[256];
  int off = 0;
  int n;
  while ((n = read(in, got + off, sizeof(got) - off)) > 0) {
    off += n;
    if (off >= sizeof(got))
      break;
  }
  close(in);

  if (off != expect_len)
    fail("output length mismatch");
  for (int i = 0; i < expect_len; i++) {
    if (got[i] != expect[i])
      fail("output mismatch");
  }

  if (!quiet)
    printf("spooltest: ok\n");
  exit(0);
}
