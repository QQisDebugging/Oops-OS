#include "param.h"
#include "types.h"
#include "stat.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("dmsgtest: %s\n", msg);
  exit(1);
}

static void
test_invalid_args(void)
{
  char buf[4] = {0};
  if (dmsgsend(-1, buf, 1) != -1)
    fail("send negative pid should fail");
  if (dmsgsend(9999, buf, 1) != -1)
    fail("send invalid pid should fail");
  if (dmsgsend(getpid(), buf, DMSG_MAX + 1) != -1)
    fail("send oversize should fail");
  if (dmsgrcv(buf, DMSG_MAX + 1) != -1)
    fail("recv oversize should fail");
}

static void
test_basic_send_recv(void)
{
  int pid = fork();
  if (pid == 0)
  {
    char buf[16] = {0};
    int sender = dmsgrcv(buf, sizeof(buf));
    if (sender <= 0)
      fail("recv sender invalid");
    if (strcmp(buf, "hello") != 0)
      fail("recv payload mismatch");
    exit(0);
  }

  char msg[] = "hello";
  if (dmsgsend(pid, msg, sizeof(msg)) != 0)
    fail("send failed");
  int st = 0;
  wait(&st);
  if (st != 0)
    fail("child failed");
}

static void
test_small_buffer(void)
{
  char msg[DMSG_MAX];
  memset(msg, 'a', sizeof(msg));
  if (dmsgsend(getpid(), msg, sizeof(msg)) != 0)
    fail("send self failed");

  char small[DMSG_MAX - 1];
  if (dmsgrcv(small, sizeof(small)) != -1)
    fail("recv should fail on small buffer");

  char big[DMSG_MAX];
  if (dmsgrcv(big, sizeof(big)) < 0)
    fail("recv should succeed after small buffer fail");
}

static void
test_blocking_send(void)
{
  int pid = fork();
  if (pid == 0)
  {
    char buf[DMSG_MAX];
    sleep(20);
    for (int i = 0; i < DMSG_QUEUE_MAX + 1; i++)
    {
      dmsgrcv(buf, sizeof(buf));
    }
    exit(0);
  }

  char msg[] = "x";
  for (int i = 0; i < DMSG_QUEUE_MAX; i++)
  {
    if (dmsgsend(pid, msg, sizeof(msg)) != 0)
      fail("send fill failed");
  }
  int t0 = uptime();
  if (dmsgsend(pid, msg, sizeof(msg)) != 0)
    fail("send blocking failed");
  int t1 = uptime();
  if (t1 - t0 < 5)
    fail("send did not block");

  int st = 0;
  wait(&st);
  if (st != 0)
    fail("child failed in blocking test");
}

static void
test_multiple_senders(void)
{
  int parent = getpid();
  int pid1 = fork();
  if (pid1 == 0)
  {
    char msg[] = "p1";
    if (dmsgsend(parent, msg, sizeof(msg)) != 0)
      exit(1);
    exit(0);
  }

  int pid2 = fork();
  if (pid2 == 0)
  {
    char msg[] = "p2";
    if (dmsgsend(parent, msg, sizeof(msg)) != 0)
      exit(1);
    exit(0);
  }

  char buf[DMSG_MAX];
  int s1 = dmsgrcv(buf, sizeof(buf));
  int s2 = dmsgrcv(buf, sizeof(buf));
  if (!((s1 == pid1 && s2 == pid2) || (s1 == pid2 && s2 == pid1)))
    fail("sender pid mismatch");

  int st = 0;
  wait(&st);
  wait(&st);
  if (st != 0)
    fail("sender failed");
}

static void
test_exit_while_sending(void)
{
  int pid = fork();
  if (pid == 0)
  {
    sleep(20);
    exit(0);
  }

  char msg[] = "x";
  for (int i = 0; i < DMSG_QUEUE_MAX; i++)
  {
    if (dmsgsend(pid, msg, sizeof(msg)) != 0)
      fail("fill before exit failed");
  }

  int t0 = uptime();
  int ret = dmsgsend(pid, msg, sizeof(msg));
  int t1 = uptime();
  if (ret != -1)
    fail("send should fail after exit");
  if (t1 - t0 < 5)
    fail("send did not block on exit");

  int st = 0;
  wait(&st);
  if (st != 0)
    fail("receiver exit failed");
}

static void
test_fifo_order(void)
{
  int pid = fork();
  if (pid == 0)
  {
    char buf[16];
    if (dmsgrcv(buf, sizeof(buf)) <= 0 || strcmp(buf, "m0") != 0)
      exit(1);
    if (dmsgrcv(buf, sizeof(buf)) <= 0 || strcmp(buf, "m1") != 0)
      exit(1);
    if (dmsgrcv(buf, sizeof(buf)) <= 0 || strcmp(buf, "m2") != 0)
      exit(1);
    exit(0);
  }

  char m0[] = "m0";
  char m1[] = "m1";
  char m2[] = "m2";
  if (dmsgsend(pid, m0, sizeof(m0)) != 0)
    fail("fifo send 0 failed");
  if (dmsgsend(pid, m1, sizeof(m1)) != 0)
    fail("fifo send 1 failed");
  if (dmsgsend(pid, m2, sizeof(m2)) != 0)
    fail("fifo send 2 failed");

  int st = 0;
  wait(&st);
  if (st != 0)
    fail("fifo order failed");
}

int
main(int argc, char *argv[])
{
  test_invalid_args();
  test_basic_send_recv();
  test_small_buffer();
  test_blocking_send();
  test_multiple_senders();
  test_exit_while_sending();
  test_fifo_order();
  printf("dmsgtest: ok\n");
  exit(0);
}
