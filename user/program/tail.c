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

static void
free_lines(char **lines, int count)
{
  for (int i = 0; i < count; i++)
  {
    if (lines[i])
      free(lines[i]);
  }
}

static int
tail_file(char *path, int n)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0)
  {
    fprintf(2, "tail: cannot open %s\n", path);
    return -1;
  }

  if (n == 0)
  {
    close(fd);
    return 0;
  }

  char **lines = malloc(sizeof(char *) * n);
  if (lines == 0)
  {
    close(fd);
    fprintf(2, "tail: out of memory\n");
    return -1;
  }
  for (int i = 0; i < n; i++)
    lines[i] = 0;

  int count = 0;
  int pos = 0;
  char line[512];
  int len = 0;
  char c;
  while (read(fd, &c, 1) == 1)
  {
    if (len < (int)sizeof(line) - 1)
      line[len++] = c;
    if (c == '\n' || len == (int)sizeof(line) - 1)
    {
      line[len] = 0;
      if (lines[pos])
        free(lines[pos]);
      lines[pos] = malloc(len + 1);
      if (lines[pos] == 0)
      {
        free_lines(lines, n);
        free(lines);
        close(fd);
        fprintf(2, "tail: out of memory\n");
        return -1;
      }
      memmove(lines[pos], line, len + 1);
      pos = (pos + 1) % n;
      if (count < n)
        count++;
      len = 0;
    }
  }

  if (len > 0)
  {
    line[len] = 0;
    if (lines[pos])
      free(lines[pos]);
    lines[pos] = malloc(len + 1);
    if (lines[pos])
      memmove(lines[pos], line, len + 1);
    pos = (pos + 1) % n;
    if (count < n)
      count++;
  }

  int start = pos - count;
  while (start < 0)
    start += n;
  for (int i = 0; i < count; i++)
  {
    int idx = (start + i) % n;
    if (lines[idx])
      write(1, lines[idx], strlen(lines[idx]));
  }

  free_lines(lines, n);
  free(lines);
  close(fd);
  return 0;
}

int main(int argc, char *argv[])
{
  int idx = 1;
  int n = parse_count(&idx, argc, argv);
  if (n < 0 || idx >= argc)
  {
    fprintf(2, "Usage: tail [-n N] file...\n");
    exit(1);
  }

  for (int i = idx; i < argc; i++)
  {
    if (argc - idx > 1)
      printf("==> %s <==\n", argv[i]);
    tail_file(argv[i], n);
    if (argc - idx > 1 && i + 1 < argc)
      printf("\n");
  }
  exit(0);
}
