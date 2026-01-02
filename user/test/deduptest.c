#include "param.h"
#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user/user.h"
#include "fs.h"

static char blkbuf[BSIZE];

static void
fillbuf(char *buf, int v)
{
  for(int i = 0; i < BSIZE; i++)
    buf[i] = (char)v;
}

static void
xassert(int cond, const char *msg)
{
  if(cond)
    return;
  fprintf(2, "deduptest: %s\n", msg);
  exit(1);
}

static void
write_block(int fd, int bn, int v)
{
  fillbuf(blkbuf, v);
  xassert(lseek(fd, bn * BSIZE, SEEK_SET) == bn * BSIZE, "lseek failed");
  xassert(write(fd, blkbuf, BSIZE) == BSIZE, "write block failed");
}

static void
read_block(int fd, int bn, char *out)
{
  xassert(lseek(fd, bn * BSIZE, SEEK_SET) == bn * BSIZE, "lseek failed");
  xassert(read(fd, out, BSIZE) == BSIZE, "read block failed");
}

static void
read_indirect_block(uint blockno, uint *out)
{
  xassert(recoveri(blockno, (uint64)out) == 0, "recoveri failed");
}

int
main(int argc, char *argv[])
{
  const char *src = "dedup_src";
  const char *dst = "dedup_dst";
  int nblocks = NDIRECT + 4; // force single-indirect blocks

  unlink(src);
  unlink(dst);

  int fd1 = open(src, O_CREATE | O_RDWR);
  xassert(fd1 >= 0, "open src failed");
  int fd2 = open(dst, O_CREATE | O_RDWR);
  xassert(fd2 >= 0, "open dst failed");

  // Make dst partially identical to src.
  // blocks with (i % 3) == 0 are different; others are identical.
  for(int i = 0; i < nblocks; i++){
    write_block(fd1, i, i);
    if(i % 3 == 0)
      write_block(fd2, i, i + 1);
    else
      write_block(fd2, i, i);
  }

  close(fd1);
  close(fd2);

  uint srci[14], dsti[14];
  xassert(geti(src, (uint64)srci) == 0, "geti src failed");
  xassert(geti(dst, (uint64)dsti) == 0, "geti dst failed");

  int shared = dedup(src, dst);
  xassert(shared >= 0, "dedup syscall failed");

  // Re-read inode mapping after dedup.
  uint dsti2[14];
  xassert(geti(dst, (uint64)dsti2) == 0, "geti dst after failed");

  // Direct blocks should be shared for identical blocks.
  for(int i = 0; i < NDIRECT && i < nblocks; i++){
    if(i % 3 == 0)
      xassert(dsti2[i] != srci[i], "direct block unexpectedly shared");
    else
      xassert(dsti2[i] == srci[i], "direct block not shared");
  }

  // Single-indirect blocks.
  if(nblocks > NDIRECT){
    xassert(srci[NDIRECT] != 0 && dsti2[NDIRECT] != 0, "missing indirect block");

    uint *src_ind = (uint*)malloc(BSIZE);
    uint *dst_ind = (uint*)malloc(BSIZE);
    xassert(src_ind != 0 && dst_ind != 0, "malloc failed");
    memset(src_ind, 0, BSIZE);
    memset(dst_ind, 0, BSIZE);

    read_indirect_block(srci[NDIRECT], src_ind);
    read_indirect_block(dsti2[NDIRECT], dst_ind);

    for(int bi = NDIRECT; bi < nblocks; bi++){
      int idx = bi - NDIRECT;
      xassert(src_ind[idx] != 0 && dst_ind[idx] != 0, "indirect data block missing");
      if(bi % 3 == 0)
        xassert(dst_ind[idx] != src_ind[idx], "indirect block unexpectedly shared");
      else
        xassert(dst_ind[idx] == src_ind[idx], "indirect block not shared");
    }

    free(src_ind);
    free(dst_ind);
  }

  // COW sanity: modify a shared block in dst; src should remain unchanged.
  int modbn = 1;
  xassert(modbn < nblocks && (modbn % 3) != 0, "bad modbn selection");

  fd2 = open(dst, O_RDWR);
  xassert(fd2 >= 0, "reopen dst failed");
  write_block(fd2, modbn, 99);
  close(fd2);

  fd1 = open(src, O_RDONLY);
  xassert(fd1 >= 0, "reopen src failed");
  read_block(fd1, modbn, blkbuf);
  close(fd1);

  for(int i = 0; i < BSIZE; i++){
    if(blkbuf[i] != (char)modbn){
      fprintf(2, "deduptest: COW failed, src modified\n");
      exit(1);
    }
  }

  unlink(src);
  unlink(dst);

  printf("deduptest: OK (shared=%d)\n", shared);
  exit(0);
}
