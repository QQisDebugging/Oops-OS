#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
  int i;

  if (argc < 2)
  {
    fprintf(2, "Usage: touch files...\n");
    exit(1);
  }

  for (i = 1; i < argc; i++)
  {
    int fd = open(argv[i], O_CREATE | O_WRONLY);
    if (fd < 0)
    {
      fprintf(2, "touch: %s failed to create\n", argv[i]);
      continue;
    }
    close(fd);
  }

  exit(0);
}
