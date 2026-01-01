#include "types.h"
#include "riscv.h"
#include "user/user.h"

#define BIGBSS (200 * 1024 * 1024)

char big[BIGBSS];

int
main(void)
{
  big[0] = 1;
  big[PGSIZE] = 2;
  big[BIGBSS - PGSIZE] = 3;

  if (big[0] != 1 || big[PGSIZE] != 2 || big[BIGBSS - PGSIZE] != 3)
  {
    printf("bigbss: failed\n");
    exit(1);
  }

  printf("bigbss: ok\n");
  exit(0);
}
