// Shell.

#include "types.h"
#include "user/user.h"
#include "fcntl.h"
#include "fs.h"

// Parsed command representation
#define EXEC 1
#define REDIR 2
#define PIPE 3
#define LIST 4
#define BACK 5

#define MAXARGS 10
#define MAXBUF 128
#define HIST_MAX 16
#define JOB_MAX 16
#define MATCH_MAX 64

struct cmd
{
  int type;
};

struct execcmd
{
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd
{
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd
{
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd
{
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd
{
  int type;
  struct cmd *cmd;
};

struct job
{
  int used;
  int jid;
  int pid;
  int done;
  int status;
  char cmd[MAXBUF];
};

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
extern char whitespace[];
static struct job jobs[JOB_MAX];
static int next_jid = 1;

// Execute cmd.  Never returns.
void runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type)
  {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    if (strchr(ecmd->argv[0], '/') == 0)
    {
      char path[MAXBUF];
      int len = strlen(ecmd->argv[0]);
      if (len + 1 < MAXBUF)
      {
        path[0] = '/';
        memmove(path + 1, ecmd->argv[0], len);
        path[len + 1] = 0;
        exec(path, ecmd->argv);
      }
    }
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0)
    {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0)
    {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0)
    {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

static char history[HIST_MAX][MAXBUF];
static int hist_head = 0;
static int hist_size = 0;

static void
hist_add(const char *line)
{
  if (line == 0 || line[0] == 0)
    return;
  int len = strlen(line);
  if (len >= MAXBUF)
    len = MAXBUF - 1;
  memmove(history[hist_head], line, len);
  history[hist_head][len] = 0;
  hist_head = (hist_head + 1) % HIST_MAX;
  if (hist_size < HIST_MAX)
    hist_size++;
}

static const char *
hist_get(int pos_newest)
{
  int idx = hist_head - 1 - pos_newest;
  while (idx < 0)
    idx += HIST_MAX;
  idx %= HIST_MAX;
  return history[idx];
}

static void
hist_print(void)
{
  for (int i = hist_size - 1; i >= 0; i--)
  {
    int no = hist_size - i;
    printf("%d %s\n", no, hist_get(i));
  }
}

static void
jobs_add(int pid, const char *cmd)
{
  if (pid <= 0 || cmd == 0 || cmd[0] == 0)
    return;
  int slot = -1;
  for (int i = 0; i < JOB_MAX; i++)
  {
    if (!jobs[i].used)
    {
      slot = i;
      break;
    }
  }
  if (slot < 0)
  {
    fprintf(2, "jobs: list full\n");
    return;
  }
  jobs[slot].used = 1;
  jobs[slot].jid = next_jid++;
  if (next_jid <= 0)
    next_jid = 1;
  jobs[slot].pid = pid;
  jobs[slot].done = 0;
  jobs[slot].status = 0;
  int len = strlen(cmd);
  if (len >= MAXBUF)
    len = MAXBUF - 1;
  memmove(jobs[slot].cmd, cmd, len);
  jobs[slot].cmd[len] = 0;
  printf("[%d] %d %s\n", jobs[slot].jid, jobs[slot].pid, jobs[slot].cmd);
}

static void
jobs_reap(int notify)
{
  for (int i = 0; i < JOB_MAX; i++)
  {
    if (!jobs[i].used || jobs[i].done)
      continue;
    int st = 0;
    int r = waitpid(jobs[i].pid, &st, WNOHANG);
    if (r == 0)
      continue;
    jobs[i].done = 1;
    jobs[i].status = st;
    if (notify)
    {
      printf("[%d] done %s\n", jobs[i].jid, jobs[i].cmd);
      jobs[i].used = 0;
    }
  }
}

static void
jobs_print(void)
{
  jobs_reap(0);
  for (int i = 0; i < JOB_MAX; i++)
  {
    if (!jobs[i].used)
      continue;
    if (jobs[i].done)
    {
      printf("[%d] done %s\n", jobs[i].jid, jobs[i].cmd);
      jobs[i].used = 0;
    }
    else
    {
      printf("[%d] running %s\n", jobs[i].jid, jobs[i].cmd);
    }
  }
}

static void
redraw_line(const char *prompt, const char *buf, int len, int prev_len, int pos)
{
  (void)prev_len;
  printf("\r%s", prompt);
  if (len > 0)
    write(1, buf, len);
  printf("\033[K");
  printf("\r%s", prompt);
  if (pos > 0)
    write(1, buf, pos);
}

static int
read_esc_final(char *final, int *saw3)
{
  char c;
  *final = 0;
  *saw3 = 0;
  for (int i = 0; i < 8; i++)
  {
    if (read(0, &c, 1) < 1)
      return 0;
    if (c >= '0' && c <= '9')
    {
      if (c == '3')
        *saw3 = 1;
      continue;
    }
    if (c == ';')
      continue;
    if ((c >= 'A' && c <= 'Z') || c == '~')
    {
      *final = c;
      return 1;
    }
  }
  return 0;
}

static int
match_add(char matches[][MAXBUF], int count, const char *name)
{
  for (int i = 0; i < count; i++)
  {
    if (strcmp(matches[i], name) == 0)
      return count;
  }
  if (count >= MATCH_MAX)
    return count;
  int len = strlen(name);
  if (len >= MAXBUF)
    len = MAXBUF - 1;
  memmove(matches[count], name, len);
  matches[count][len] = 0;
  return count + 1;
}

static int
prefix_match(const char *s, const char *prefix, int len)
{
  for (int i = 0; i < len; i++)
  {
    if (s[i] == 0 || s[i] != prefix[i])
      return 0;
  }
  return 1;
}

static void
tab_complete(const char *prompt, char *buf, int nbuf, int *len, int *pos, int *prev_len)
{
  static const char *builtins[] = {
      "cd", "clear", "exit", "help", "history", "jobs", "pwd", 0};
  int start = *pos;
  while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t')
    start--;
  int prefix_len = *pos - start;
  char prefix[MAXBUF];
  if (prefix_len >= MAXBUF)
    prefix_len = MAXBUF - 1;
  memmove(prefix, &buf[start], prefix_len);
  prefix[prefix_len] = 0;

  char matches[MATCH_MAX][MAXBUF];
  int count = 0;
  for (int i = 0; builtins[i]; i++)
  {
    if (prefix_len == 0 || prefix_match(builtins[i], prefix, prefix_len))
      count = match_add(matches, count, builtins[i]);
  }

  int fd = open(".", O_RDONLY);
  if (fd >= 0)
  {
    struct dirent de;
    while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
      if (de.inum == 0)
        continue;
      char name[DIRSIZ + 1];
      memmove(name, de.name, DIRSIZ);
      name[DIRSIZ] = 0;
      if (prefix_len == 0 || prefix_match(name, prefix, prefix_len))
        count = match_add(matches, count, name);
    }
    close(fd);
  }

  if (count == 0)
  {
    write(1, "\a", 1);
    return;
  }

  int common = strlen(matches[0]);
  for (int i = 1; i < count; i++)
  {
    int j = 0;
    while (j < common && matches[0][j] && matches[i][j] &&
           matches[0][j] == matches[i][j])
      j++;
    common = j;
  }
  if (common < prefix_len)
    common = prefix_len;

  int add = common - prefix_len;
  if (add > 0)
  {
    if (*len + add >= nbuf)
      add = nbuf - 1 - *len;
    if (add > 0)
    {
      memmove(&buf[*pos + add], &buf[*pos], *len - *pos);
      memmove(&buf[*pos], matches[0] + prefix_len, add);
      *len += add;
      *pos += add;
      buf[*len] = 0;
      redraw_line(prompt, buf, *len, *prev_len, *pos);
      *prev_len = *len;
    }
  }
  else if (count > 1)
  {
    printf("\n");
    for (int i = 0; i < count; i++)
    {
      printf("%s  ", matches[i]);
    }
    printf("\n");
    redraw_line(prompt, buf, *len, *prev_len, *pos);
    *prev_len = *len;
  }
}

static int
getcmd(char *buf, int nbuf)
{
  const char *prompt = "$ ";
  char saved[MAXBUF];
  int saved_len = 0;
  int hist_pos = -1;
  int len = 0;
  int pos = 0;
  int prev_len = 0;

  consctl(1);
  printf("%s", prompt);
  memset(buf, 0, nbuf);
  for (;;)
  {
    char c;
    int cc = read(0, &c, 1);
    if (cc < 1)
    {
      consctl(0);
      return -1;
    }

    if (c == '\r' || c == '\n')
    {
      if (len + 1 < nbuf)
        buf[len++] = '\n';
      buf[len] = 0;
      write(1, "\n", 1);
      consctl(0);
      break;
    }
    if (c == 0x04) // Ctrl+D
    {
      if (len == 0)
      {
        consctl(0);
        return -1;
      }
      continue;
    }
    if (c == 0x03) // Ctrl+C
    {
      write(1, "^C\n", 3);
      len = 0;
      pos = 0;
      buf[0] = 0;
      hist_pos = -1;
      saved_len = 0;
      prev_len = 0;
      printf("%s", prompt);
      continue;
    }
    if (c == 0x15) // Ctrl+U
    {
      if (len > 0 || pos > 0)
      {
        len = 0;
        pos = 0;
        buf[0] = 0;
        hist_pos = -1;
        saved_len = 0;
        redraw_line(prompt, buf, len, prev_len, pos);
        prev_len = len;
      }
      continue;
    }
    if (c == 0x7f || c == '\b')
    {
      if (pos > 0)
      {
        if (pos == len)
        {
          len--;
          pos--;
          buf[len] = 0;
          write(1, "\b \b", 3);
          prev_len = len;
        }
        else
        {
          memmove(&buf[pos - 1], &buf[pos], len - pos);
          len--;
          pos--;
          buf[len] = 0;
          redraw_line(prompt, buf, len, prev_len, pos);
          prev_len = len;
        }
      }
      continue;
    }
    if (c == 0x01) // Ctrl+A
    {
      if (pos != 0)
      {
        pos = 0;
        redraw_line(prompt, buf, len, prev_len, pos);
        prev_len = len;
      }
      continue;
    }
    if (c == 0x05) // Ctrl+E
    {
      if (pos != len)
      {
        pos = len;
        redraw_line(prompt, buf, len, prev_len, pos);
        prev_len = len;
      }
      continue;
    }
    if (c == 0x17) // Ctrl+W
    {
      while (pos > 0 && (buf[pos - 1] == ' ' || buf[pos - 1] == '\t'))
        pos--;
      int start = pos;
      while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t')
        start--;
      if (start < pos)
      {
        memmove(&buf[start], &buf[pos], len - pos);
        len -= (pos - start);
        pos = start;
        buf[len] = 0;
        redraw_line(prompt, buf, len, prev_len, pos);
        prev_len = len;
      }
      continue;
    }
    if (c == 0x0c) // Ctrl+L
    {
      printf("\033[2J\033[H");
      redraw_line(prompt, buf, len, prev_len, pos);
      prev_len = len;
      continue;
    }
    if (c == 0x1b) // ESC
    {
      char start;
      if (read(0, &start, 1) < 1)
        continue;
      if (start == '[' || start == 'O')
      {
        char final;
        int saw3;
        if (!read_esc_final(&final, &saw3))
          continue;
        if (final == 'C')
        {
          if (pos < len)
          {
            pos++;
            write(1, "\033[C", 3);
          }
        }
        else if (final == 'D')
        {
          if (pos > 0)
          {
            pos--;
            write(1, "\033[D", 3);
          }
        }
        else if (final == 'H')
        {
          pos = 0;
          redraw_line(prompt, buf, len, prev_len, pos);
          prev_len = len;
        }
        else if (final == 'F')
        {
          pos = len;
          redraw_line(prompt, buf, len, prev_len, pos);
          prev_len = len;
        }
        else if (final == '~' && saw3)
        {
          if (pos < len)
          {
            memmove(&buf[pos], &buf[pos + 1], len - pos - 1);
            len--;
            buf[len] = 0;
            redraw_line(prompt, buf, len, prev_len, pos);
            prev_len = len;
          }
        }
        else if (final == 'A' || final == 'B')
        {
          if (hist_size == 0)
            continue;
          if (hist_pos == -1)
          {
            saved_len = len;
            memmove(saved, buf, len);
            saved[len] = 0;
          }
          int old = hist_pos;
          if (final == 'A')
          {
            if (hist_pos + 1 < hist_size)
              hist_pos++;
          }
          else
          {
            if (hist_pos >= 0)
              hist_pos--;
          }
          if (hist_pos == old)
            continue;
          if (hist_pos >= 0)
          {
            const char *h = hist_get(hist_pos);
            len = strlen(h);
            if (len >= nbuf)
              len = nbuf - 1;
            memmove(buf, h, len);
            buf[len] = 0;
          }
          else
          {
            len = saved_len;
            memmove(buf, saved, len);
            buf[len] = 0;
          }
          pos = len;
          redraw_line(prompt, buf, len, prev_len, pos);
          prev_len = len;
        }
      }
      continue;
    }

    if (c == '\t') // Tab
    {
      if (hist_pos != -1)
      {
        hist_pos = -1;
        saved_len = 0;
      }
      tab_complete(prompt, buf, nbuf, &len, &pos, &prev_len);
      continue;
    }

    if (c < ' ')
      continue;
    if (hist_pos != -1)
    {
      hist_pos = -1;
      saved_len = 0;
    }
    if (len + 1 < nbuf)
    {
      if (pos == len)
      {
        buf[len++] = c;
        pos = len;
        buf[len] = 0;
        write(1, &c, 1);
        prev_len = len;
      }
      else
      {
        memmove(&buf[pos + 1], &buf[pos], len - pos);
        buf[pos] = c;
        len++;
        pos++;
        buf[len] = 0;
        redraw_line(prompt, buf, len, prev_len, pos);
        prev_len = len;
      }
    }
  }

  if (len > 1)
  {
    int end = len;
    if (buf[end - 1] == '\n' || buf[end - 1] == '\r')
      end--;
    while (end > 0 && (buf[end - 1] == ' ' || buf[end - 1] == '\t'))
      end--;
    int start = 0;
    while (start < end && (buf[start] == ' ' || buf[start] == '\t'))
      start++;
    if (end > start)
    {
      char line[MAXBUF];
      int hlen = end - start;
      if (hlen >= MAXBUF)
        hlen = MAXBUF - 1;
      memmove(line, buf + start, hlen);
      line[hlen] = 0;
      hist_add(line);
    }
  }
  return 0;
}

static char *
trim(char *s)
{
  while (*s && strchr(whitespace, *s))
    s++;
  char *e = s + strlen(s);
  while (e > s && strchr(whitespace, e[-1]))
    *--e = 0;
  return s;
}

static void
strip_ampersand(char *s)
{
  int len = strlen(s);
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
    len--;
  if (len > 0 && s[len - 1] == '&')
  {
    len--;
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
      len--;
  }
  s[len] = 0;
}

static int
is_background(const char *s)
{
  int len = strlen(s);
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
    len--;
  return len > 0 && s[len - 1] == '&';
}

static int
handle_builtin(char *line)
{
  if (line[0] == 0)
    return 1;
  if (strcmp(line, "exit") == 0)
    exit(0);
  if (strcmp(line, "clear") == 0)
  {
    printf("\033[2J\033[H");
    return 1;
  }
  if (strcmp(line, "help") == 0)
  {
    printf("builtins: cd clear exit help history jobs pwd\n");
    printf("keys: up/down history, left/right move, del, tab, ctrl+a/e/u/w/l\n");
    return 1;
  }
  if (strcmp(line, "history") == 0)
  {
    hist_print();
    return 1;
  }
  if (strcmp(line, "jobs") == 0)
  {
    jobs_print();
    return 1;
  }
  if (strcmp(line, "pwd") == 0)
  {
    char cwd[MAXBUF];
    if (getcwd(cwd) < 0)
      fprintf(2, "pwd: getcwd failed\n");
    else
      printf("%s\n", cwd);
    return 1;
  }
  if (line[0] == 'c' && line[1] == 'd' &&
      (line[2] == 0 || line[2] == ' ' || line[2] == '\t'))
  {
    char *path = line + 2;
    while (*path == ' ' || *path == '\t')
      path++;
    if (*path == 0)
      path = "/";
    if (chdir(path) < 0)
      fprintf(2, "cannot cd %s\n", path);
    return 1;
  }
  return 0;
}

static int
expand_history(char *line, char *out, int outlen)
{
  if (line[0] != '!')
    return 0;
  if (hist_size == 0)
  {
    fprintf(2, "history: empty\n");
    return -1;
  }
  int idx = -1;
  if (strcmp(line, "!!") == 0)
  {
    idx = 0;
  }
  else
  {
    int n = atoi(line + 1);
    if (n <= 0 || n > hist_size)
    {
      fprintf(2, "history: %s: out of range\n", line);
      return -1;
    }
    idx = hist_size - n;
  }
  const char *h = hist_get(idx);
  int len = strlen(h);
  if (len >= outlen)
    len = outlen - 1;
  memmove(out, h, len);
  out[len] = 0;
  printf("%s\n", out);
  return 1;
}

int main(void)
{
  static char buf[MAXBUF];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0)
  {
    if (fd >= 3)
    {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (1)
  {
    consfg(0);
    jobs_reap(1);
    if (getcmd(buf, sizeof(buf)) < 0)
      break;
    char line[MAXBUF];
    strcpy(line, buf);
    char expanded[MAXBUF];
    char *t = trim(line);
    int ex = expand_history(t, expanded, sizeof(expanded));
    if (ex < 0)
      continue;
    if (ex > 0)
    {
      strcpy(line, expanded);
      t = trim(line);
    }
    if (handle_builtin(t))
      continue;
    char cmdline[MAXBUF];
    strcpy(cmdline, t);
    int background = is_background(t);
    strip_ampersand(cmdline);
    if (background)
      strip_ampersand(line);

    int pid = fork1();
    if (pid == 0)
      runcmd(parsecmd(line));
    if (background)
      jobs_add(pid, cmdline);
    else
    {
      consfg(pid);
      wait(0);
      consfg(0);
    }
  }
  exit(0);
}

void panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

// PAGEBREAK!
//  Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
// PAGEBREAK!
//  Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s)
  {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>')
    {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es)
  {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&"))
  {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";"))
  {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|"))
  {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>"))
  {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok)
    {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;"))
  {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type)
  {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
