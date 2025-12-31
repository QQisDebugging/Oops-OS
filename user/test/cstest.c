#include "types.h"
#include "stat.h"
#include "user/user.h"

#define NCLIENT 3
#define NREQ 5

struct request
{
  int client;
  int seq;
  int value;
};

struct response
{
  int client;
  int seq;
  int value;
};

static void
fail(const char *msg)
{
  printf("cstest: %s\n", msg);
  exit(1);
}

static void
server(int total, int wfd)
{
  int pid = getpid();
  if (write(wfd, &pid, sizeof(pid)) != sizeof(pid))
    fail("server pid write failed");
  close(wfd);

  for (int i = 0; i < total; i++) {
    struct request req;
    int sender = dmsgrcv(&req, sizeof(req));
    if (sender < 0)
      fail("server recv failed");

    struct response resp;
    resp.client = req.client;
    resp.seq = req.seq;
    resp.value = req.value + 1;
    if (dmsgsend(sender, &resp, sizeof(resp)) != 0)
      fail("server send failed");
  }
  exit(0);
}

static void
client(int server_pid, int cid)
{
  for (int i = 0; i < NREQ; i++) {
    struct request req;
    req.client = cid;
    req.seq = i;
    req.value = cid * 100 + i;
    if (dmsgsend(server_pid, &req, sizeof(req)) != 0)
      fail("client send failed");

    struct response resp;
    int sender = dmsgrcv(&resp, sizeof(resp));
    if (sender != server_pid)
      fail("client wrong sender");
    if (resp.client != cid || resp.seq != i || resp.value != req.value + 1)
      fail("client response mismatch");
  }
  exit(0);
}

int
main(int argc, char *argv[])
{
  int fds[2];
  if (pipe(fds) != 0)
    fail("pipe failed");

  int server_pid = -1;
  int pid = fork();
  if (pid == 0) {
    close(fds[0]);
    server(NCLIENT * NREQ, fds[1]);
  }
  close(fds[1]);
  if (read(fds[0], &server_pid, sizeof(server_pid)) != sizeof(server_pid))
    fail("read server pid failed");
  close(fds[0]);
  if (server_pid <= 0)
    fail("bad server pid");

  for (int i = 0; i < NCLIENT; i++) {
    int cid = i;
    pid = fork();
    if (pid == 0)
      client(server_pid, cid);
  }

  for (int i = 0; i < NCLIENT + 1; i++)
    wait(0);

  printf("cstest: ok\n");
  exit(0);
}
