#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "proc.h"
#include "fcntl.h"

extern struct superblock sb;

#define SWAP_BLOCKS_PER_PAGE (PGSIZE / BSIZE)

static struct spinlock swapmap_lock;
static uint swapstart;
static uint8 swapmap[SWAP_PAGES];
static uint swap_hint;
static int swapready;

static int
swap_alloc(void)
{
  int slot = -1;

  acquire(&swapmap_lock);
  for (int i = 0; i < SWAP_PAGES; i++) {
    int idx = (swap_hint + i) % SWAP_PAGES;
    if (swapmap[idx] == 0) {
      swapmap[idx] = 1;
      slot = idx;
      swap_hint = (idx + 1) % SWAP_PAGES;
      break;
    }
  }
  release(&swapmap_lock);
  return slot;
}

void
swapfree(uint64 slot)
{
  if (slot >= SWAP_PAGES)
    return;

  acquire(&swapmap_lock);
  swapmap[slot] = 0;
  release(&swapmap_lock);
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

  initlock(&swapmap_lock, "swapmap");
  swap_hint = 0;
  swapready = 1;
}

static struct vm_area *
vma_lookup(struct proc *p, uint64 va)
{
  for (int i = 0; i < NVMA; i++) {
    if (p->vma[i].used &&
        p->vma[i].addr <= va &&
        va < p->vma[i].addr + p->vma[i].len) {
      return &p->vma[i];
    }
  }
  return 0;
}

static int
vma_writeback(struct vm_area *vma, uint64 va, uint64 pa)
{
  if (vma->vfile == 0 || vma->vfile->type != FD_INODE)
    return -1;
  if ((vma->prot & PROT_WRITE) == 0)
    return 0;

  uint64 page_off = PGROUNDDOWN(va - vma->addr);
  if (page_off >= vma->len)
    return 0;

  uint64 offset = vma->offset + page_off;
  uint64 n = PGSIZE;
  uint64 remain = vma->len - page_off;
  if (remain < n)
    n = remain;

  begin_op();
  ilock(vma->vfile->ip);
  int r = writei(vma->vfile->ip, 0, pa, offset, n);
  iunlock(vma->vfile->ip);
  end_op();

  return (r == n) ? 0 : -1;
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
  uint64 sz = PGROUNDUP(p->sz);
  if (sz == 0)
    return -1;

  uint64 start = p->swap_hand;

  start = PGROUNDDOWN(start);
  if (start >= sz)
    start = 0;

  uint64 npages = sz / PGSIZE;
  uint64 va = start;
  uint64 scanned = 0;
  uint64 max_scans = npages * 2;

  // Second-chance (clock) scan: clear A on first encounter, evict when A stays clear.
  while (scanned < max_scans) {
    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte && (*pte & PTE_V) && (*pte & PTE_U)) {
      if (*pte & PTE_A) {
        *pte &= ~PTE_A;
        sfence_vma();
      } else {
        uint64 pa = PTE2PA(*pte);
        if (krefcnt((void *)pa) == 1) {
          struct vm_area *vma = vma_lookup(p, va);
          if (vma && vma->flags == MAP_SHARED) {
            if (vma_writeback(vma, va, pa) < 0) {
              goto advance;
            }

            *pte = 0;
            sfence_vma();
            kfree((void *)pa);

            uint64 next = va + PGSIZE;
            if (next >= sz)
              next = 0;
            p->swap_hand = next;
            return 0;
          }

          int slot = swap_alloc();
          if (slot < 0)
            return -1;

          swap_write(slot, (char *)pa);

          uint flags = PTE_FLAGS(*pte);
          flags = (flags & ~PTE_V) | PTE_S;
          *pte = SWAP2PTE(slot) | flags;
          sfence_vma();

          kfree((void *)pa);

          uint64 next = va + PGSIZE;
          if (next >= sz)
            next = 0;
          p->swap_hand = next;
          return 0;
        }
      }
    }

advance:
    va += PGSIZE;
    if (va >= sz)
      va = 0;
    scanned++;
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
