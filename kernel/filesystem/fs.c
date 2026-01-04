// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"


#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 
struct {
  struct spinlock lock;
  uint ref[FSSIZE];
} brefs;

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

static void bref_init(int dev);

// Init fs
void
fsinit(int dev) {
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
  // Rebuild block refcounts after log recovery.
  bref_init(dev);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

static void
bref_inc(uint b)
{
  if(b >= sb.size)
    panic("bref_inc");
  acquire(&brefs.lock);
  brefs.ref[b]++;
  release(&brefs.lock);
}

static int
bref_dec(uint b)
{
  int ref;

  if(b >= sb.size)
    panic("bref_dec");
  acquire(&brefs.lock);
  if(brefs.ref[b] < 1)
    panic("bref_dec: underflow");
  brefs.ref[b]--;
  ref = brefs.ref[b];
  release(&brefs.lock);
  return ref;
}

static uint
bref_get(uint b)
{
  uint ref;

  if(b >= sb.size)
    panic("bref_get");
  acquire(&brefs.lock);
  ref = brefs.ref[b];
  release(&brefs.lock);
  return ref;
}

static void
bref_init(int dev)
{
  int i, j, inum;
  struct buf *bp;
  struct dinode *dip;
  uint *a;

  initlock(&brefs.lock, "brefs");
  for(i = 0; i < sb.size && i < FSSIZE; i++)
    brefs.ref[i] = 0;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){
      brelse(bp);
      continue;
    }
    for(i = 0; i < NDIRECT; i++){
      if(dip->addrs[i])
        brefs.ref[dip->addrs[i]]++;
    }
    if(dip->addrs[NDIRECT]){
      brefs.ref[dip->addrs[NDIRECT]]++;
      struct buf *bp1 = bread(dev, dip->addrs[NDIRECT]);
      a = (uint*)bp1->data;
      for(j = 0; j < NINDIRECT; j++){
        if(a[j])
          brefs.ref[a[j]]++;
      }
      brelse(bp1);
    }
    if(dip->addrs[NDIRECT + 1]){
      brefs.ref[dip->addrs[NDIRECT + 1]]++;
      struct buf *bp2 = bread(dev, dip->addrs[NDIRECT + 1]);
      a = (uint*)bp2->data;
      for(i = 0; i < NADDR_PER_BLOCK; i++){
        if(a[i] == 0)
          continue;
        brefs.ref[a[i]]++;
        struct buf *bp3 = bread(dev, a[i]);
        uint *a1 = (uint*)bp3->data;
        for(j = 0; j < NADDR_PER_BLOCK; j++){
          if(a1[j])
            brefs.ref[a1[j]]++;
        }
        brelse(bp3);
      }
      brelse(bp2);
    }
    brelse(bp);
  }
}

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        bref_inc(b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  if(bref_dec(b) > 0)
    return;
  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit()
{
  int i = 0;
  
  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, char type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      dip->mode=3;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  dip->mode=ip->mode;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    ip->mode=dip->mode;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquire(&icache.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);

    release(&icache.lock);

    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&icache.lock);
  }

  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint
bmap(struct inode *ip, uint bn, int for_write)
{
  uint addr, *a;
  struct buf *bp;
  uint *p = 0;
  struct buf *pbuf = 0;

  if(bn < NDIRECT){
    p = &ip->addrs[bn];
    if((addr = *p) == 0)
      *p = addr = balloc(ip->dev);
  } else {
    bn -= NDIRECT;
    if(bn < NINDIRECT){
      // Load indirect block, allocating if necessary.
      if((addr = ip->addrs[NDIRECT]) == 0)
        ip->addrs[NDIRECT] = addr = balloc(ip->dev);
      bp = bread(ip->dev, addr);
      a = (uint*)bp->data;
      p = &a[bn];
      if((addr = *p) == 0){
        *p = addr = balloc(ip->dev);
        log_write(bp);
      }
      pbuf = bp;
    } else {
      bn -= NINDIRECT;
      // Double indirect blocks.
      if(bn < NDINDIRECT) {
        int level2_idx = bn / NADDR_PER_BLOCK;
        int level1_idx = bn % NADDR_PER_BLOCK;

        if((addr = ip->addrs[NDIRECT + 1]) == 0)
          ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
        bp = bread(ip->dev, addr);
        a = (uint*)bp->data;
        if((addr = a[level2_idx]) == 0) {
          a[level2_idx] = addr = balloc(ip->dev);
          log_write(bp);
        }
        brelse(bp);

        bp = bread(ip->dev, addr);
        a = (uint*)bp->data;
        p = &a[level1_idx];
        if((addr = *p) == 0) {
          *p = addr = balloc(ip->dev);
          log_write(bp);
        }
        pbuf = bp;
      } else {
        panic("bmap: out of range");
      }
    }
  }

  if(for_write && addr && bref_get(addr) > 1){
    uint newb = balloc(ip->dev);
    struct buf *src = bread(ip->dev, addr);
    struct buf *dst = bread(ip->dev, newb);
    memmove(dst->data, src->data, BSIZE);
    log_write(dst);
    brelse(src);
    brelse(dst);
    if(p)
      *p = newb;
    if(pbuf)
      log_write(pbuf);
    bfree(ip->dev, addr);
    addr = newb;
  }

  if(pbuf)
    brelse(pbuf);

  return addr;
}

// Like bmap(), but does not allocate blocks.
// Returns 0 if the block is not mapped.
static uint
bmap_nalloc(struct inode *ip, uint bn)
{
  uint addr;
  struct buf *bp;
  uint *a;

  if(bn < NDIRECT)
    return ip->addrs[bn];

  bn -= NDIRECT;
  if(bn < NINDIRECT){
    if((addr = ip->addrs[NDIRECT]) == 0)
      return 0;
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    addr = a[bn];
    brelse(bp);
    return addr;
  }

  bn -= NINDIRECT;
  if(bn < NDINDIRECT){
    int level2_idx = bn / NADDR_PER_BLOCK;
    int level1_idx = bn % NADDR_PER_BLOCK;
    uint l1;

    if((addr = ip->addrs[NDIRECT + 1]) == 0)
      return 0;
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    l1 = a[level2_idx];
    brelse(bp);
    if(l1 == 0)
      return 0;

    bp = bread(ip->dev, l1);
    a = (uint*)bp->data;
    addr = a[level1_idx];
    brelse(bp);
    return addr;
  }

  return 0;
}

// Set an existing block mapping to addr. Never allocates metadata blocks.
// Returns: 1 if inode needs iupdate(), 0 if already logged, -1 on failure.
static int
bmap_set_existing(struct inode *ip, uint bn, uint addr)
{
  struct buf *bp;
  uint *a;
  uint b;

  if(bn < NDIRECT){
    ip->addrs[bn] = addr;
    return 1;
  }

  bn -= NDIRECT;
  if(bn < NINDIRECT){
    if((b = ip->addrs[NDIRECT]) == 0)
      return -1;
    bp = bread(ip->dev, b);
    a = (uint*)bp->data;
    a[bn] = addr;
    log_write(bp);
    brelse(bp);
    return 0;
  }

  bn -= NINDIRECT;
  if(bn < NDINDIRECT){
    int level2_idx = bn / NADDR_PER_BLOCK;
    int level1_idx = bn % NADDR_PER_BLOCK;
    uint l1;

    if((b = ip->addrs[NDIRECT + 1]) == 0)
      return -1;
    bp = bread(ip->dev, b);
    a = (uint*)bp->data;
    l1 = a[level2_idx];
    brelse(bp);
    if(l1 == 0)
      return -1;

    bp = bread(ip->dev, l1);
    a = (uint*)bp->data;
    a[level1_idx] = addr;
    log_write(bp);
    brelse(bp);
    return 0;
  }

  return -1;
}

// Ensure metadata blocks exist for bn without allocating data blocks.
// Returns: 1 if inode needs iupdate(), 0 if not, -1 on failure.
static int
bmap_ensure(struct inode *ip, uint bn)
{
  struct buf *bp;
  uint *a;
  int need_update = 0;

  if(bn < NDIRECT)
    return 0;

  bn -= NDIRECT;
  if(bn < NINDIRECT){
    if(ip->addrs[NDIRECT] == 0){
      ip->addrs[NDIRECT] = balloc(ip->dev);
      need_update = 1;
    }
    return need_update;
  }

  bn -= NINDIRECT;
  if(bn < NDINDIRECT){
    int level2_idx = bn / NADDR_PER_BLOCK;

    if(ip->addrs[NDIRECT + 1] == 0){
      ip->addrs[NDIRECT + 1] = balloc(ip->dev);
      need_update = 1;
    }
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint*)bp->data;
    if(a[level2_idx] == 0){
      a[level2_idx] = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return need_update;
  }

  return -1;
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
  struct buf* bp1;
  uint* a1;
  if(ip->addrs[NDIRECT + 1]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint*)bp->data;
    for(i = 0; i < NADDR_PER_BLOCK; i++) {
      // 每个一级间接块的操作都类似于上面的
      // if(ip->addrs[NDIRECT])中的内容
      if(a[i]) {
        bp1 = bread(ip->dev, a[i]);
        a1 = (uint*)bp1->data;
        for(j = 0; j < NADDR_PER_BLOCK; j++) {
          if(a1[j])
            bfree(ip->dev, a1[j]);
        }
        brelse(bp1);
        bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }
  ip->size = 0;
  iupdate(ip);
}

static int
addr_block_empty(uint *a, int n)
{
  for(int i = 0; i < n; i++){
    if(a[i])
      return 0;
  }
  return 1;
}

static void
free_indirect_block(int dev, uint ind_bno)
{
  if(ind_bno == 0)
    return;
  struct buf *bp = bread(dev, ind_bno);
  uint *a = (uint*)bp->data;
  for(int j = 0; j < NADDR_PER_BLOCK; j++){
    if(a[j])
      bfree(dev, a[j]);
  }
  brelse(bp);
  bfree(dev, ind_bno);
}

// Truncate inode to a specific size, freeing blocks beyond newsize.
// Caller must hold ip->lock.
void
itrunc_to(struct inode *ip, uint newsize)
{
  if(newsize >= ip->size){
    ip->size = newsize;
    iupdate(ip);
    return;
  }

  uint new_nblocks = (newsize + BSIZE - 1) / BSIZE;

  // Direct blocks.
  for(uint i = new_nblocks; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // Single indirect blocks.
  if(new_nblocks <= NDIRECT){
    if(ip->addrs[NDIRECT]){
      free_indirect_block(ip->dev, ip->addrs[NDIRECT]);
      ip->addrs[NDIRECT] = 0;
    }
  } else if(ip->addrs[NDIRECT]){
    uint keep = new_nblocks - NDIRECT;
    struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
    uint *a = (uint*)bp->data;
    int changed = 0;
    for(uint j = keep; j < NINDIRECT; j++){
      if(a[j]){
        bfree(ip->dev, a[j]);
        a[j] = 0;
        changed = 1;
      }
    }
    int empty = addr_block_empty(a, NINDIRECT);
    if(changed && !empty)
      log_write(bp);
    brelse(bp);
    if(empty){
      bfree(ip->dev, ip->addrs[NDIRECT]);
      ip->addrs[NDIRECT] = 0;
    }
  }

  // Double indirect blocks.
  if(new_nblocks <= NDIRECT + NINDIRECT){
    if(ip->addrs[NDIRECT + 1]){
      struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
      uint *a = (uint*)bp->data;
      for(int i = 0; i < NADDR_PER_BLOCK; i++){
        if(a[i])
          free_indirect_block(ip->dev, a[i]);
      }
      brelse(bp);
      bfree(ip->dev, ip->addrs[NDIRECT + 1]);
      ip->addrs[NDIRECT + 1] = 0;
    }
  } else if(ip->addrs[NDIRECT + 1]){
    uint dkeep = new_nblocks - (NDIRECT + NINDIRECT);
    uint keep2 = dkeep / NADDR_PER_BLOCK;
    uint keep1 = dkeep % NADDR_PER_BLOCK;
    struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    uint *a = (uint*)bp->data;
    int changed = 0;

    // Free all level-1 blocks after keep2.
    for(uint i = keep2 + 1; i < NADDR_PER_BLOCK; i++){
      if(a[i]){
        free_indirect_block(ip->dev, a[i]);
        a[i] = 0;
        changed = 1;
      }
    }

    // Handle keep2 block.
    if(keep2 < NADDR_PER_BLOCK && a[keep2]){
      if(keep1 == 0){
        free_indirect_block(ip->dev, a[keep2]);
        a[keep2] = 0;
        changed = 1;
      } else {
        struct buf *bp1 = bread(ip->dev, a[keep2]);
        uint *a1 = (uint*)bp1->data;
        int changed1 = 0;
        for(uint j = keep1; j < NADDR_PER_BLOCK; j++){
          if(a1[j]){
            bfree(ip->dev, a1[j]);
            a1[j] = 0;
            changed1 = 1;
          }
        }
        int empty1 = addr_block_empty(a1, NADDR_PER_BLOCK);
        if(changed1 && !empty1)
          log_write(bp1);
        brelse(bp1);
        if(empty1){
          bfree(ip->dev, a[keep2]);
          a[keep2] = 0;
          changed = 1;
        }
      }
    }

    int empty = addr_block_empty(a, NADDR_PER_BLOCK);
    if(changed && !empty)
      log_write(bp);
    brelse(bp);
    if(empty){
      bfree(ip->dev, ip->addrs[NDIRECT + 1]);
      ip->addrs[NDIRECT + 1] = 0;
    }
  }

  ip->size = newsize;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->mode=ip->mode;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;
  uint blkaddr;
  static char zeros[BSIZE];

  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    blkaddr = bmap_nalloc(ip, off/BSIZE);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(blkaddr == 0){
      // Sparse hole: return zeros without reading disk.
      if(either_copyout(user_dst, dst, zeros + (off % BSIZE), m) == -1)
        break;
    } else {
      bp = bread(ip->dev, blkaddr);
      if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
        brelse(bp);
        break;
      }
      brelse(bp);
    }
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE, 1));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(n > 0){
    if(off > ip->size)
      ip->size = off;
    // write the i-node back to disk even if the size didn't change
    // because the loop above might have called bmap() and added a new
    // block to ip->addrs[].
    iupdate(ip);
  }

  return n;
}

// Pre-allocate blocks for [startb, endb) and optionally update size.
// Caller must hold ip->lock.
int
falloc(struct inode *ip, uint startb, uint endb, uint newsize, int keep_size)
{
  uint b;
  int need_update = keep_size;

  if(newsize > MAXFILE*BSIZE)
    return -1;
  for(b = startb; b < endb; b++)
    bmap(ip, b, 0);
  if(!keep_size && newsize > ip->size){
    ip->size = newsize;
    need_update = 1;
  }
  if(need_update)
    iupdate(ip);
  return 0;
}

// Punch a hole in the file, releasing blocks in [startb, endb).
// File size remains unchanged; reading the hole returns zeros.
// Caller must hold ip->lock.
int
ipunch(struct inode *ip, uint startb, uint endb)
{
  uint bn;
  struct buf *bp;
  uint *a;
  int need_update = 0;

  if(ip->type != T_FILE)
    return -1;

  for(bn = startb; bn < endb; bn++){
    if(bn < NDIRECT){
      // Direct block.
      if(ip->addrs[bn]){
        bfree(ip->dev, ip->addrs[bn]);
        ip->addrs[bn] = 0;
        need_update = 1;
      }
    } else if(bn < NDIRECT + NINDIRECT){
      // Single indirect block.
      uint idx = bn - NDIRECT;
      if(ip->addrs[NDIRECT] == 0)
        continue;
      bp = bread(ip->dev, ip->addrs[NDIRECT]);
      a = (uint*)bp->data;
      if(a[idx]){
        bfree(ip->dev, a[idx]);
        a[idx] = 0;
        log_write(bp);
      }
      // Check if entire indirect block is empty.
      int empty = addr_block_empty(a, NINDIRECT);
      brelse(bp);
      if(empty){
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
        need_update = 1;
      }
    } else if(bn < NDIRECT + NINDIRECT + NDINDIRECT){
      // Double indirect block.
      uint dbn = bn - NDIRECT - NINDIRECT;
      uint level2_idx = dbn / NADDR_PER_BLOCK;
      uint level1_idx = dbn % NADDR_PER_BLOCK;

      if(ip->addrs[NDIRECT + 1] == 0)
        continue;
      bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
      a = (uint*)bp->data;
      if(a[level2_idx] == 0){
        brelse(bp);
        continue;
      }
      uint l1_bno = a[level2_idx];
      brelse(bp);

      struct buf *bp1 = bread(ip->dev, l1_bno);
      uint *a1 = (uint*)bp1->data;
      if(a1[level1_idx]){
        bfree(ip->dev, a1[level1_idx]);
        a1[level1_idx] = 0;
        log_write(bp1);
      }
      int empty1 = addr_block_empty(a1, NADDR_PER_BLOCK);
      brelse(bp1);

      if(empty1){
        // Free level-1 indirect block.
        bfree(ip->dev, l1_bno);
        bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
        a = (uint*)bp->data;
        a[level2_idx] = 0;
        log_write(bp);
        int empty2 = addr_block_empty(a, NADDR_PER_BLOCK);
        brelse(bp);
        if(empty2){
          bfree(ip->dev, ip->addrs[NDIRECT + 1]);
          ip->addrs[NDIRECT + 1] = 0;
          need_update = 1;
        }
      }
    }
  }

  if(need_update)
    iupdate(ip);
  return 0;
}

// Clone file data blocks from src to dst, sharing data blocks.
// Caller must hold both inode locks.
int
iclone(struct inode *src, struct inode *dst)
{
  int i, j;

  if(src->type != T_FILE || dst->type != T_FILE)
    return -1;

  dst->mode = src->mode;
  dst->size = src->size;

  for(i = 0; i < NDIRECT; i++){
    dst->addrs[i] = src->addrs[i];
    if(src->addrs[i])
      bref_inc(src->addrs[i]);
  }

  if(src->addrs[NDIRECT]){
    uint new_ind = balloc(dst->dev);
    dst->addrs[NDIRECT] = new_ind;
    struct buf *bp_src = bread(src->dev, src->addrs[NDIRECT]);
    struct buf *bp_dst = bread(dst->dev, new_ind);
    memmove(bp_dst->data, bp_src->data, BSIZE);
    uint *a = (uint*)bp_dst->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a[i])
        bref_inc(a[i]);
    }
    log_write(bp_dst);
    brelse(bp_src);
    brelse(bp_dst);
  }

  if(src->addrs[NDIRECT + 1]){
    uint new_dind = balloc(dst->dev);
    dst->addrs[NDIRECT + 1] = new_dind;
    struct buf *bp_src = bread(src->dev, src->addrs[NDIRECT + 1]);
    struct buf *bp_dst = bread(dst->dev, new_dind);
    memset(bp_dst->data, 0, BSIZE);
    uint *a_src = (uint*)bp_src->data;
    uint *a_dst = (uint*)bp_dst->data;
    for(i = 0; i < NADDR_PER_BLOCK; i++){
      if(a_src[i] == 0)
        continue;
      uint new_l1 = balloc(dst->dev);
      a_dst[i] = new_l1;
      struct buf *bp_src_l1 = bread(src->dev, a_src[i]);
      struct buf *bp_dst_l1 = bread(dst->dev, new_l1);
      memmove(bp_dst_l1->data, bp_src_l1->data, BSIZE);
      uint *a_l1 = (uint*)bp_dst_l1->data;
      for(j = 0; j < NADDR_PER_BLOCK; j++){
        if(a_l1[j])
          bref_inc(a_l1[j]);
      }
      log_write(bp_dst_l1);
      brelse(bp_src_l1);
      brelse(bp_dst_l1);
    }
    log_write(bp_dst);
    brelse(bp_src);
    brelse(bp_dst);
  }

  iupdate(dst);
  return 0;
}

// Clone a block-aligned range from src to dst, sharing data blocks.
// Caller must hold both inode locks.
int
iclone_range(struct inode *src, struct inode *dst, uint src_off, uint dst_off, uint len)
{
  uint bn;
  uint nblocks;
  uint srcb;
  uint dstb;
  int need_update = 0;

  if(src->type != T_FILE || dst->type != T_FILE)
    return -1;
  if(src->dev != dst->dev)
    return -1;

  nblocks = len / BSIZE;
  srcb = src_off / BSIZE;
  dstb = dst_off / BSIZE;

  for(bn = 0; bn < nblocks; bn++){
    uint s = bmap_nalloc(src, srcb + bn);
    uint d = bmap_nalloc(dst, dstb + bn);

    if(s == 0){
      if(d != 0){
        int r = bmap_set_existing(dst, dstb + bn, 0);
        if(r < 0)
          return -1;
        if(r == 1)
          need_update = 1;
        bfree(dst->dev, d);
      }
      continue;
    }

    if(d == s)
      continue;

    bref_inc(s);
    int r = bmap_ensure(dst, dstb + bn);
    if(r < 0){
      bref_dec(s);
      return -1;
    }
    if(r == 1)
      need_update = 1;

    r = bmap_set_existing(dst, dstb + bn, s);
    if(r < 0){
      bref_dec(s);
      return -1;
    }
    if(r == 1)
      need_update = 1;

    if(d != 0)
      bfree(dst->dev, d);
  }

  if(dst_off + len > dst->size){
    dst->size = dst_off + len;
    need_update = 1;
  }
  if(need_update)
    iupdate(dst);
  return 0;
}

// Deduplicate identical data blocks from dst against src.
// For each block where both files have an allocated block with identical
// content, make dst reference src's block and free dst's original block.
// Caller must hold both inode locks.
// Returns the number of blocks newly shared, or -1 on error.
int
idedup(struct inode *src, struct inode *dst)
{
  uint bn;
  uint nblocks;
  int shared = 0;
  int need_iupdate = 0;

  if(src->type != T_FILE || dst->type != T_FILE)
    return -1;
  if(src->dev != dst->dev)
    return -1;
  if(src->inum == dst->inum)
    return 0;

  nblocks = min((src->size + BSIZE - 1) / BSIZE,
                (dst->size + BSIZE - 1) / BSIZE);

  for(bn = 0; bn < nblocks; bn++){
    uint s = bmap_nalloc(src, bn);
    uint d = bmap_nalloc(dst, bn);
    struct buf *bps;
    struct buf *bpd;

    if(s == 0 || d == 0 || s == d)
      continue;

    bps = bread(src->dev, s);
    bpd = bread(dst->dev, d);
    int eq = (memcmp(bps->data, bpd->data, BSIZE) == 0);
    brelse(bps);
    brelse(bpd);
    if(!eq)
      continue;

    // Update dst mapping first while d is still reachable.
    // After switching the pointer, drop the old block reference.
    bref_inc(s);
    int r = bmap_set_existing(dst, bn, s);
    if(r < 0){
      // Undo bref_inc().
      bref_dec(s);
      return -1;
    }
    if(r == 1)
      need_iupdate = 1;
    bfree(dst->dev, d);
    shared++;
  }

  if(need_iupdate)
    iupdate(dst);
  return shared;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
