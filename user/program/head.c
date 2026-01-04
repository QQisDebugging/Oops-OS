#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

static int
parse_count(int *idx, int argc, char *argv[])
{
  int n = 10;
  if (*idx < argc && argv[*idx][0] == '-')
  {
    if (argv[*idx][1] == 'n')
    {
      if (*idx + 1 >= argc)
        return -1;
      n = atoi(argv[*idx + 1]);
      *idx += 2;
    }
    else
    {
      n = atoi(argv[*idx] + 1);
      *idx += 1;
    }
  }
  if (n < 0)
    n = 0;
  return n;
}

static int
head_file(char *path, int n)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0)
  {
    fprintf(2, "head: cannot open %s\n", path);
    return -1;
  }

  int lines = 0;
  char c;
  while (lines < n)
  {
    int r = read(fd, &c, 1);
    if (r < 1)
      break;
    write(1, &c, 1);
    if (c == '\n')
      lines++;
  }
  close(fd);
  return 0;
}

int main(int argc, char *argv[])
{
  int idx = 1;
  int n = parse_count(&idx, argc, argv);
  if (n < 0 || idx >= argc)
  {
    fprintf(2, "Usage: head [-n N] file...\n");
    exit(1);
  }

  for (int i = idx; i < argc; i++)
  {
    if (argc - idx > 1)
      printf("==> %s <==\n", argv[i]);
    head_file(argv[i], n);
    if (argc - idx > 1 && i + 1 < argc)
      printf("\n");
  }
  exit(0);
}
