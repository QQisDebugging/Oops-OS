// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents. Caching disk blocks in memory
// reduces the number of disk reads and provides a synchronization point
// for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use the buffer, so do not keep it
//   longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// Hash buckets for buffer lookup.
#define NBUCKET 13
#define HASH(id) ((id) % NBUCKET)

struct hashbuf {
  struct buf head;
  struct spinlock lock;
};

struct {
  struct spinlock evict_lock;
  struct buf buf[NBUF];
  struct hashbuf buckets[NBUCKET];
  uint clock_hand;

  struct spinlock ra_lock;
  uint ra_dev;
  uint ra_blockno;
} bcache;

static int
buf_cached(uint dev, uint blockno)
{
  struct buf *b;
  int bid = HASH(blockno);

  acquire(&bcache.buckets[bid].lock);
  for (b = bcache.buckets[bid].head.next; b != &bcache.buckets[bid].head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      release(&bcache.buckets[bid].lock);
      return 1;
    }
  }
  release(&bcache.buckets[bid].lock);
  return 0;
}

static struct buf*
bget_common(uint dev, uint blockno, int must_succeed)
{
  struct buf *b;
  int bid = HASH(blockno);

  for (;;) {
    acquire(&bcache.buckets[bid].lock);
    for (b = bcache.buckets[bid].head.next; b != &bcache.buckets[bid].head; b = b->next) {
      if (b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        b->refbit = 1;
        release(&bcache.buckets[bid].lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.buckets[bid].lock);

    acquire(&bcache.evict_lock);

    // Re-check after waiting for eviction lock to avoid duplicates.
    acquire(&bcache.buckets[bid].lock);
    for (b = bcache.buckets[bid].head.next; b != &bcache.buckets[bid].head; b = b->next) {
      if (b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        b->refbit = 1;
        release(&bcache.buckets[bid].lock);
        release(&bcache.evict_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.buckets[bid].lock);

    struct buf *victim = 0;
    for (int scanned = 0; scanned < NBUF * 2; scanned++) {
      b = &bcache.buf[bcache.clock_hand];
      bcache.clock_hand = (bcache.clock_hand + 1) % NBUF;

      int vb = HASH(b->blockno);
      acquire(&bcache.buckets[vb].lock);
      if (b->refcnt == 0) {
        if (b->refbit) {
          b->refbit = 0;
        } else {
          b->next->prev = b->prev;
          b->prev->next = b->next;
          b->refcnt = 1;
          victim = b;
          release(&bcache.buckets[vb].lock);
          break;
        }
      }
      release(&bcache.buckets[vb].lock);
    }

    if (victim == 0) {
      release(&bcache.evict_lock);
      if (must_succeed)
        panic("bget: no buffers");
      return 0;
    }

    acquire(&bcache.buckets[bid].lock);
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refbit = 1;
    victim->next = bcache.buckets[bid].head.next;
    victim->prev = &bcache.buckets[bid].head;
    bcache.buckets[bid].head.next->prev = victim;
    bcache.buckets[bid].head.next = victim;
    release(&bcache.buckets[bid].lock);

    release(&bcache.evict_lock);
    acquiresleep(&victim->lock);
    return victim;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  return bget_common(dev, blockno, 1);
}

// Best-effort buffer lookup for read-ahead.
static struct buf*
bget_ra(uint dev, uint blockno)
{
  return bget_common(dev, blockno, 0);
}

static int
should_readahead(uint dev, uint blockno)
{
  int seq = 0;

  acquire(&bcache.ra_lock);
  if (bcache.ra_dev == dev && bcache.ra_blockno + 1 == blockno)
    seq = 1;
  bcache.ra_dev = dev;
  bcache.ra_blockno = blockno;
  release(&bcache.ra_lock);

  return seq;
}

static void
readahead(uint dev, uint blockno)
{
  if (blockno >= FSSIZE)
    return;
  if (buf_cached(dev, blockno))
    return;

  struct buf *b = bget_ra(dev, blockno);
  if (b == 0)
    return;
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  b->refbit = 0;
  brelse(b);
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.evict_lock, "bcache_evict");
  initlock(&bcache.ra_lock, "bcache_ra");
  bcache.clock_hand = 0;
  bcache.ra_dev = (uint)-1;
  bcache.ra_blockno = 0;

  char lockname[9] = "bcache_";
  for (int i = 0; i < NBUCKET; ++i) {
    lockname[7] = '0' + i;
    lockname[8] = '\0';
    initlock(&bcache.buckets[i].lock, lockname);
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->valid = 0;
    b->disk = 0;
    b->dev = 0;
    b->blockno = 0;
    b->refcnt = 0;
    b->refbit = 0;
    b->timestamp = 0;
    initsleeplock(&b->lock, "buffer");
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  if (should_readahead(dev, blockno))
    readahead(dev, blockno + 1);
  return b;
}

// Write b's contents to disk. Must be locked.
void
bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  int bid = HASH(b->blockno);

  releasesleep(&b->lock);

  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;
  release(&bcache.buckets[bid].lock);
}

void
bpin(struct buf *b)
{
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt++;
  release(&bcache.buckets[bid].lock);
}

void
bunpin(struct buf *b)
{
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;
  release(&bcache.buckets[bid].lock);
}
