#ifndef _FSINFO_H_
#define _FSINFO_H_

#include "types.h"

struct fsinfo {
  uint bsize;
  uint total_blocks;
  uint free_blocks;
  uint total_inodes;
  uint free_inodes;
};

#endif
