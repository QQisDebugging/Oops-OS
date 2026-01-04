#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

static const char *
base_name(const char *path)
{
  int len = strlen(path);
  while (len > 0 && path[len - 1] == '/')
    len--;
  int i = len;
  while (i > 0 && path[i - 1] != '/')
    i--;
  return path + i;
}

static int
build_dest(char *out, int outsz, const char *dir, const char *name)
{
  int dlen = strlen(dir);
  int nlen = strlen(name);
  int need = dlen + 1 + nlen + 1;
  if (dlen > 0 && dir[dlen - 1] == '/')
    need--;
  if (need > outsz)
    return -1;
  memmove(out, dir, dlen);
  if (dlen > 0 && out[dlen - 1] != '/')
    out[dlen++] = '/';
  memmove(out + dlen, name, nlen);
  out[dlen + nlen] = 0;
  return 0;
}

int main(int argc, char *argv[])
{
  int fdin, fdout, n;
  char buf[512];
  struct stat st;
  char dstpath[512];

  if (argc != 3)
  {
    fprintf(2, "Usage: cp source dest\n");
    exit(1);
  }

  if (stat(argv[1], &st) < 0)
  {
    fprintf(2, "cp: cannot stat %s\n", argv[1]);
    exit(1);
  }
  if (st.type == T_DIR)
  {
    fprintf(2, "cp: %s is a directory\n", argv[1]);
    exit(1);
  }

  if ((fdin = open(argv[1], O_RDONLY)) < 0)
  {
    fprintf(2, "cp: cannot open %s\n", argv[1]);
    exit(1);
  }

  char *dst = argv[2];
  if (stat(argv[2], &st) >= 0 && st.type == T_DIR)
  {
    if (build_dest(dstpath, sizeof(dstpath), argv[2], base_name(argv[1])) < 0)
    {
      fprintf(2, "cp: path too long\n");
      close(fdin);
      exit(1);
    }
    dst = dstpath;
  }

  if ((fdout = open(dst, O_CREATE | O_WRONLY | O_TRUNC)) < 0)
  {
    fprintf(2, "cp: cannot create %s\n", dst);
    close(fdin);
    exit(1);
  }

  while ((n = read(fdin, buf, sizeof(buf))) > 0)
  {
    if (write(fdout, buf, n) != n)
    {
      fprintf(2, "cp: write failed\n");
      close(fdin);
      close(fdout);
      exit(1);
    }
  }

  close(fdin);
  close(fdout);
  exit(0);
}
