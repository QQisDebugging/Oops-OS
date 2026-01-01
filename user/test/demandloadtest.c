#include "param.h"
#include "types.h"
#include "user/user.h"

int
main(void)
{
  int pid = fork();
  if (pid < 0)
  {
    printf("demandloadtest: fork failed\n");
    exit(1);
  }
  if (pid == 0)
  {
    char *argv[] = {"bigbss", 0};
    exec("bigbss", argv);
    printf("demandloadtest: exec bigbss failed\n");
    exit(1);
  }

  int status;
  if (wait(&status) < 0)
  {
    printf("demandloadtest: wait failed\n");
    exit(1);
  }
  if (status != 0)
  {
    printf("demandloadtest: child failed\n");
    exit(1);
  }

  printf("demandloadtest: ok\n");
  exit(0);
}
