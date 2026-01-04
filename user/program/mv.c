#include "types.h"
#include "stat.h"
#include "user/user.h"

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
  struct stat st;
  char dstpath[512];

  if (argc != 3)
  {
    fprintf(2, "Usage: mv old new\n");
    exit(1);
  }

  char *dst = argv[2];
  if (stat(argv[2], &st) >= 0 && st.type == T_DIR)
  {
    if (build_dest(dstpath, sizeof(dstpath), argv[2], base_name(argv[1])) < 0)
    {
      fprintf(2, "mv: path too long\n");
      exit(1);
    }
    dst = dstpath;
  }

  if (rename(argv[1], dst) == 0)
    exit(0);

  if (link(argv[1], dst) == 0 && unlink(argv[1]) == 0)
    exit(0);

  fprintf(2, "mv: %s to %s failed\n", argv[1], dst);
  exit(1);
}
