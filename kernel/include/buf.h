#include "fs.h"  // for BSIZE

struct buf {
  int valid;   // has data been read from disk?
  int disk;    // device in use by disk driver?
  uint dev;    // device number
  uint blockno; // disk block number
  struct sleeplock lock; // protects buffer contents
  uint refcnt; // number of users
  uchar refbit; // clock reference bit
  struct buf *prev; // hash bucket list
  struct buf *next;
  uchar data[BSIZE]; // disk block data
  uint timestamp; // last access time (ticks)
};
