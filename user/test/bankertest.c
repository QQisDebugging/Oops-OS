#include "types.h"
#include "stat.h"
#include "user/user.h"

static int verbose = 1;

static void
fail(const char *msg)
{
  if (verbose)
    printf("bankertest: %s\n", msg);
  exit(1);
}

static void
send_cmd(int fd, char c)
{
  if (write(fd, &c, 1) != 1)
    fail("pipe write failed");
}

static char
recv_cmd(int fd)
{
  char c;
  if (read(fd, &c, 1) != 1)
    fail("pipe read failed");
  return c;
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "-q") == 0)
    verbose = 0;

  int bad_total[1] = {-1};
  if (banker_init(1, bad_total) == 0)
    fail("negative total accepted");

  int total[2] = {3, 3};
  if (banker_init(2, total) != 0)
    fail("banker_init failed");
  if (verbose)
    printf("bankertest: total=[3,3]\n");

  int max0[2] = {2, 2};
  int req0[2] = {1, 0};
  if (banker_set_max(2, max0) != 0)
    fail("set max p0 failed");
  if (banker_request(2, req0) != 0)
    fail("p0 initial request failed");
  if (verbose)
    printf("bankertest: p0 alloc=[1,0] max=[2,2]\n");

  int p1_cmd[2], p1_resp[2];
  int p2_cmd[2], p2_resp[2];
  if (pipe(p1_cmd) < 0 || pipe(p1_resp) < 0 ||
      pipe(p2_cmd) < 0 || pipe(p2_resp) < 0)
    fail("pipe failed");

  int pid1 = fork();
  if (pid1 < 0)
    fail("fork failed");
  if (pid1 == 0)
  {
    int bad = 0;
    close(p1_cmd[1]);
    close(p1_resp[0]);
    close(p2_cmd[0]);
    close(p2_cmd[1]);
    close(p2_resp[0]);
    close(p2_resp[1]);

    int max1[2] = {2, 2};
    int req1[2] = {1, 1};
    if (banker_set_max(2, max1) != 0)
      bad = 1;
    if (banker_request(2, req1) != 0)
      bad = 1;

    send_cmd(p1_resp[1], bad ? 'F' : 'R');
    char cmd = recv_cmd(p1_cmd[0]);
    if (cmd != 'R')
      bad = 1;
    if (banker_release(2, req1) != 0)
      bad = 1;
    send_cmd(p1_resp[1], bad ? 'F' : 'D');
    exit(bad ? 1 : 0);
  }

  int pid2 = fork();
  if (pid2 < 0)
    fail("fork failed");
  if (pid2 == 0)
  {
    int bad = 0;
    close(p2_cmd[1]);
    close(p2_resp[0]);
    close(p1_cmd[0]);
    close(p1_cmd[1]);
    close(p1_resp[0]);
    close(p1_resp[1]);

    int max2[2] = {2, 2};
    int req2a[2] = {0, 1};
    int req2b[2] = {1, 1};
    if (banker_set_max(2, max2) != 0)
      bad = 1;
    if (banker_request(2, req2a) != 0)
      bad = 1;

    send_cmd(p2_resp[1], bad ? 'F' : 'R');

    char cmd = recv_cmd(p2_cmd[0]);
    if (cmd == 'U')
    {
    if (banker_request(2, req2b) == 0)
      bad = 1;
    send_cmd(p2_resp[1], bad ? 'F' : 'P');
    }
    else
    {
      bad = 1;
      send_cmd(p2_resp[1], 'F');
    }

    cmd = recv_cmd(p2_cmd[0]);
    if (cmd == 'S')
    {
      if (banker_request(2, req2b) != 0)
        bad = 1;
      if (banker_release(2, req2b) != 0)
        bad = 1;
      send_cmd(p2_resp[1], bad ? 'F' : 'D');
    }
    else
    {
      bad = 1;
      send_cmd(p2_resp[1], 'F');
    }

    if (banker_release(2, req2a) != 0)
      bad = 1;
    exit(bad ? 1 : 0);
  }

  close(p1_cmd[0]);
  close(p1_resp[1]);
  close(p2_cmd[0]);
  close(p2_resp[1]);

  if (recv_cmd(p1_resp[0]) != 'R')
    fail("p1 setup failed");
  if (recv_cmd(p2_resp[0]) != 'R')
    fail("p2 setup failed");
  if (verbose)
    printf("bankertest: p1 alloc=[1,1] max=[2,2]\n");
  if (verbose)
    printf("bankertest: p2 alloc=[0,1] max=[2,2]\n");

  send_cmd(p2_cmd[1], 'U');
  char resp = recv_cmd(p2_resp[0]);
  if (resp != 'P')
    fail("unsafe request granted");
  if (verbose)
    printf("bankertest: unsafe request rejected\n");

  send_cmd(p1_cmd[1], 'R');
  resp = recv_cmd(p1_resp[0]);
  if (resp != 'D')
    fail("p1 release failed");

  send_cmd(p2_cmd[1], 'S');
  resp = recv_cmd(p2_resp[0]);
  if (resp != 'D')
    fail("safe request rejected");
  if (verbose)
    printf("bankertest: safe request granted\n");

  if (banker_release(2, req0) != 0)
    fail("p0 release failed");

  int x = 0;
  wait(&x);
  if (x != 0)
    fail("p1 failed");
  wait(&x);
  if (x != 0)
    fail("p2 failed");

  if (verbose)
    printf("bankertest: ok\n");
  exit(0);
}
