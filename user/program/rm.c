#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fs.h"

static int
rm_path(char *path)
{
  int fd;
  struct stat st;
  struct dirent de;
  char buf[512], *p;

  if (stat(path, &st) < 0)
  {
    fprintf(2, "rm: cannot stat %s\n", path);
    return -1;
  }

  if (st.type == T_FILE || st.type == T_DEVICE)
  {
    if (unlink(path) < 0)
    {
      fprintf(2, "rm: %s failed to delete\n", path);
      return -1;
    }
    return 0;
  }

  if (st.type != T_DIR)
  {
    fprintf(2, "rm: %s unsupported type\n", path);
    return -1;
  }

  if ((fd = open(path, 0)) < 0)
  {
    fprintf(2, "rm: cannot open %s\n", path);
    return -1;
  }

  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
  {
    fprintf(2, "rm: path too long\n");
    close(fd);
    return -1;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';
  while (read(fd, &de, sizeof(de)) == sizeof(de))
  {
    if (de.inum == 0)
      continue;
    char name[DIRSIZ + 1];
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;
    memmove(p, name, DIRSIZ + 1);
    if (rm_path(buf) < 0)
    {
      close(fd);
      return -1;
    }
  }
  close(fd);

  if (unlink(path) < 0)
  {
    fprintf(2, "rm: %s failed to delete\n", path);
    return -1;
  }
  return 0;
}

int main(int argc, char *argv[])
{
  int i;
  int recursive = 0;

  if (argc < 2)
  {
    fprintf(2, "Usage: rm files...\n");
    exit(1);
  }

  i = 1;
  if (i < argc && argv[i][0] == '-' && argv[i][1] == 'r' && argv[i][2] == 0)
  {
    recursive = 1;
    i++;
  }
  if (i < argc && argv[i][0] == '-' && argv[i][1] == 'R' && argv[i][2] == 0)
  {
    recursive = 1;
    i++;
  }

  for (; i < argc; i++)
  {
    if (recursive)
    {
      if (rm_path(argv[i]) < 0)
        break;
      continue;
    }
    if (unlink(argv[i]) < 0)
    {
      fprintf(2, "rm: %s failed to delete\n", argv[i]);
      break;
    }
  }

  exit(0);
}
