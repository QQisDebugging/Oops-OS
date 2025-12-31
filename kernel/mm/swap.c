#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#include "proc.h"

extern struct superblock sb;

#define SWAP_BLOCKS_PER_PAGE (PGSIZE / BSIZE)

static struct spinlock swaplock;
static uint swapstart;
static uint8 swapmap[SWAP_PAGES];
static uint64 hand_va;
static int swapready;

static int
swap_alloc(void)
{
  int slot = -1;

  acquire(&swaplock);
  for (int i = 0; i < SWAP_PAGES; i++) {
    if (swapmap[i] == 0) {
      swapmap[i] = 1;
      slot = i;
      break;
    }
  }
  release(&swaplock);
  return slot;
}

void
swapfree(uint64 slot)
{
  if (slot >= SWAP_PAGES)
    return;

  acquire(&swaplock);
  swapmap[slot] = 0;
  release(&swaplock);
}

static void
swap_write(uint64 slot, char *src)
{
  for (int i = 0; i < SWAP_BLOCKS_PER_PAGE; i++) {
    uint blockno = swapstart + slot * SWAP_BLOCKS_PER_PAGE + i;
    struct buf *bp = bread(ROOTDEV, blockno);
    memmove(bp->data, src + i * BSIZE, BSIZE);
    bwrite(bp);
    brelse(bp);
  }
}

static void
swap_read(uint64 slot, char *dst)
{
  for (int i = 0; i < SWAP_BLOCKS_PER_PAGE; i++) {
    uint blockno = swapstart + slot * SWAP_BLOCKS_PER_PAGE + i;
    struct buf *bp = bread(ROOTDEV, blockno);
    memmove(dst + i * BSIZE, bp->data, BSIZE);
    brelse(bp);
  }
}

void
swapinit(void)
{
  int nbitmap = sb.size / (BSIZE * 8) + 1;
  swapstart = sb.bmapstart + nbitmap;

  if (swapstart + SWAPBLOCKS > sb.size)
    panic("swapinit: swap blocks overflow");

  initlock(&swaplock, "swap");
  hand_va = 0;
  swapready = 1;
}

static int
vma_contains(struct proc *p, uint64 va)
{
  for (int i = 0; i < NVMA; i++) {
    if (p->vma[i].used &&
        p->vma[i].addr <= va &&
        va < p->vma[i].addr + p->vma[i].len) {
      return 1;
    }
  }
  return 0;
}

int
swapout(void)
{
  struct proc *cur = myproc();

  if (!swapready)
    return -1;
  if (cur == 0)
    return -1;

  struct proc *p = cur;
  uint64 start;
  acquire(&swaplock);
  start = hand_va;
  release(&swaplock);

  for (int pass = 0; pass < 2; pass++) {
    uint64 begin = (pass == 0) ? PGROUNDDOWN(start) : 0;
    uint64 end = (pass == 0) ? p->sz : PGROUNDDOWN(start);

    for (uint64 va = begin; va < end; va += PGSIZE) {
      pte_t *pte = walk(p->pagetable, va, 0);
      if (pte == 0)
        continue;
      if ((*pte & PTE_V) == 0)
        continue;
      if ((*pte & PTE_U) == 0)
        continue;
      if (vma_contains(p, va))
        continue;

      uint64 pa = PTE2PA(*pte);
      if (krefcnt((void *)pa) != 1)
        continue;

      int slot = swap_alloc();
      if (slot < 0)
        return -1;

      swap_write(slot, (char *)pa);

      uint flags = PTE_FLAGS(*pte);
      flags = (flags & ~PTE_V) | PTE_S;
      *pte = SWAP2PTE(slot) | flags;
      sfence_vma();

      kfree((void *)pa);

      acquire(&swaplock);
      hand_va = va + PGSIZE;
      release(&swaplock);
      return 0;
    }
  }

  return -1;
}

int
swapin(pagetable_t pagetable, uint64 va)
{
  if (!swapready)
    return -1;

  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 1;
  if ((*pte & PTE_S) == 0)
    return 1;

  uint64 slot = PTE2SWAP(*pte);
  char *mem = kalloc();
  if (mem == 0)
    return -1;

  swap_read(slot, mem);
  swapfree(slot);

  uint flags = PTE_FLAGS(*pte);
  flags = (flags & ~PTE_S) | PTE_V;
  *pte = PA2PTE(mem) | flags;
  sfence_vma();

  return 0;
}

int
swapcopy(pagetable_t pagetable, uint64 va, pte_t src_pte)
{
  if (!swapready)
    return -1;

  uint64 oldslot = PTE2SWAP(src_pte);
  int newslot = swap_alloc();
  if (newslot < 0)
    return -1;

  char buf[BSIZE];
  for (int i = 0; i < SWAP_BLOCKS_PER_PAGE; i++) {
    uint oldblock = swapstart + oldslot * SWAP_BLOCKS_PER_PAGE + i;
    uint newblock = swapstart + newslot * SWAP_BLOCKS_PER_PAGE + i;
    struct buf *bp = bread(ROOTDEV, oldblock);
    memmove(buf, bp->data, BSIZE);
    brelse(bp);
    bp = bread(ROOTDEV, newblock);
    memmove(bp->data, buf, BSIZE);
    bwrite(bp);
    brelse(bp);
  }

  pte_t *pte = walk(pagetable, va, 1);
  if (pte == 0)
  {
    swapfree(newslot);
    return -1;
  }

  uint flags = PTE_FLAGS(src_pte);
  flags = (flags & ~PTE_V) | PTE_S;
  *pte = SWAP2PTE(newslot) | flags;
  return 0;
}
