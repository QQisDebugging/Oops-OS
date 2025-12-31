// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 定义哈希桶结构，
// 删除原来的全局缓冲区链表，改为使用素数个散列桶
#define NBUCKET 13
#define HASH(id) (id % NBUCKET)

struct hashbuf {
  struct buf head;       // 头节点
  struct spinlock lock;  // 锁
};

struct {
  struct spinlock evict_lock;
  struct buf buf[NBUF];
  struct hashbuf buckets[NBUCKET];  // 散列桶
} bcache;

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;

void binit(void)
{
  struct buf *b;
  initlock(&bcache.evict_lock, "bcache_evict");
  char lockname[9]="bcache_";  // 为每个散列桶的锁命名
  for(int i = 0; i < NBUCKET; ++i) {
    // 初始化散列桶的自旋锁
    lockname[7]='0'+i;lockname[8]='\0';
    initlock(&bcache.buckets[i].lock, lockname);
    // 初始化散列桶的头节点
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }
  // Create linked list of buffers
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    // 利用头插法初始化缓冲区列表,全部放到散列桶0上
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bid = HASH(blockno);

  for(;;) {
    acquire(&bcache.buckets[bid].lock);

    // Is the block already cached?
    for(b = bcache.buckets[bid].head.next; b != &bcache.buckets[bid].head; b = b->next) {
      if(b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        acquire(&tickslock);
        b->timestamp = ticks;
        release(&tickslock);
        release(&bcache.buckets[bid].lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.buckets[bid].lock);

    acquire(&bcache.evict_lock);

    // Re-check after waiting for eviction lock to avoid duplicates.
    acquire(&bcache.buckets[bid].lock);
    for(b = bcache.buckets[bid].head.next; b != &bcache.buckets[bid].head; b = b->next) {
      if(b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        acquire(&tickslock);
        b->timestamp = ticks;
        release(&tickslock);
        release(&bcache.buckets[bid].lock);
        release(&bcache.evict_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.buckets[bid].lock);

    // Not cached. Recycle an unused buffer without cross-bucket locking.
    struct buf *victim = 0;
    for(int i = 0, idx = bid; i < NBUCKET; ++i, idx = (idx + 1) % NBUCKET) {
      struct buf *cand = 0;
      acquire(&bcache.buckets[idx].lock);
      for(b = bcache.buckets[idx].head.next; b != &bcache.buckets[idx].head; b = b->next) {
        if(b->refcnt == 0 && (cand == 0 || b->timestamp < cand->timestamp))
          cand = b;
      }
      if(cand) {
        cand->next->prev = cand->prev;
        cand->prev->next = cand->next;
        cand->refcnt = 1;
        victim = cand;
        release(&bcache.buckets[idx].lock);
        break;
      }
      release(&bcache.buckets[idx].lock);
    }

    if(victim == 0) {
      release(&bcache.evict_lock);
      panic("bget: no buffers");
    }

    acquire(&bcache.buckets[bid].lock);
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    acquire(&tickslock);
    victim->timestamp = ticks;
    release(&tickslock);
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

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf* b) {
  if(!holdingsleep(&b->lock))
    panic("brelse");

  int bid = HASH(b->blockno);

  releasesleep(&b->lock);

  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;

  // 更新时间戳
  // 由于LRU改为使用时间戳判定，不再需要头插法
  acquire(&tickslock);
  b->timestamp = ticks;
  release(&tickslock);

  release(&bcache.buckets[bid].lock);
}


void
bpin(struct buf* b) {
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt++;
  release(&bcache.buckets[bid].lock);
}

void
bunpin(struct buf* b) {
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lock);
  b->refcnt--;
  release(&bcache.buckets[bid].lock);
}

