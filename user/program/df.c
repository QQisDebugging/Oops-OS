#include "types.h"
#include "stat.h"
#include "fsinfo.h"
#include "user/user.h"

static void
print_df(struct fsinfo *info)
{
  uint used_blocks = info->total_blocks - info->free_blocks;
  uint total_k = info->total_blocks * (info->bsize / 1024);
  uint used_k = used_blocks * (info->bsize / 1024);
  uint free_k = info->free_blocks * (info->bsize / 1024);
  uint used_pct = info->total_blocks ? (used_blocks * 100) / info->total_blocks : 0;

  printf("Filesystem 1K-blocks Used Available Use%%\n");
  printf("root      %d %d %d %d%%\n", total_k, used_k, free_k, used_pct);
  printf("Inodes: total %d used %d free %d\n",
         info->total_inodes,
         info->total_inodes - info->free_inodes,
         info->free_inodes);
}

int
main(int argc, char *argv[])
{
  struct fsinfo info;

  (void)argc;
  (void)argv;

  if (fsinfo(&info) < 0) {
    printf("df: fsinfo failed\n");
    exit(1);
  }
  print_df(&info);
  exit(0);
}
