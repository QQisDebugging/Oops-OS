#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: dedup <src> <dst>\n");
    exit(1);
  }

  int n = dedup(argv[1], argv[2]);
  if(n < 0){
    fprintf(2, "dedup: failed\n");
    exit(1);
  }

  printf("dedup: shared %d blocks\n", n);
  exit(0);
}
