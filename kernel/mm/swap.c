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
extern uint ticks;
extern struct spinlock tickslock;

#define SWAP_BLOCKS_PER_PAGE (PGSIZE / BSIZE)
#define SWAP_THRASH_WINDOW 20
#define SWAP_THRASH_EVENTS 128
#define SWAP_THRASH_SLEEP 1
#define SWAP_PBUF_SIZE 64

static struct spinlock swapmap_lock;
static uint swapstart;
static uint8 swapmap[SWAP_PAGES];
static uint swap_hint;
static int swapready;
static uint swap_window_start;
static uint swap_events;
static struct spinlock swap_pbuf_lock;
static uint swap_pbuf_hand;
static uint swap_pbuf_cached;
static uint64 swap_pbuf_hits;
static uint64 swap_pbuf_misses;

struct swap_pbuf_entry {
  uint valid;
  uint slot;
  uint64 pa;
};

static struct swap_pbuf_entry swap_pbuf[SWAP_PBUF_SIZE];

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

static void
swap_pbuf_put(uint slot, uint64 pa)
{
  acquire(&swap_pbuf_lock);
  uint idx = swap_pbuf_hand;
  if (swap_pbuf[idx].valid) {
    kfree((void *)swap_pbuf[idx].pa);
    if (swap_pbuf_cached > 0)
      swap_pbuf_cached--;
  }
  swap_pbuf[idx].valid = 1;
  swap_pbuf[idx].slot = slot;
  swap_pbuf[idx].pa = pa;
  swap_pbuf_cached++;
  swap_pbuf_hand = (idx + 1) % SWAP_PBUF_SIZE;
  release(&swap_pbuf_lock);
}

static void *
swap_pbuf_get(uint slot)
{
  void *pa = 0;

  acquire(&swap_pbuf_lock);
  for (uint i = 0; i < SWAP_PBUF_SIZE; i++) {
    if (swap_pbuf[i].valid && swap_pbuf[i].slot == slot) {
      pa = (void *)swap_pbuf[i].pa;
      swap_pbuf[i].valid = 0;
      if (swap_pbuf_cached > 0)
        swap_pbuf_cached--;
      swap_pbuf_hits++;
      break;
    }
  }
  if (pa == 0)
    swap_pbuf_misses++;
  release(&swap_pbuf_lock);
  return pa;
}

void
swap_pbuf_stats(uint64 *hits, uint64 *misses, uint64 *cached)
{
  acquire(&swap_pbuf_lock);
  *hits = swap_pbuf_hits;
  *misses = swap_pbuf_misses;
  *cached = swap_pbuf_cached;
  release(&swap_pbuf_lock);
}

static void
swap_throttle(void)
{
  acquire(&tickslock);
  uint now = ticks;
  if (now - swap_window_start >= SWAP_THRASH_WINDOW) {
    swap_window_start = now;
    swap_events = 0;
  }
  swap_events++;
  if (swap_events >= SWAP_THRASH_EVENTS) {
    uint until = now + SWAP_THRASH_SLEEP;
    swap_window_start = now;
    swap_events = 0;
    release(&tickslock);
    for (;;) {
      acquire(&tickslock);
      if (ticks >= until) {
        release(&tickslock);
        break;
      }
      release(&tickslock);
    }
    return;
  }
  release(&tickslock);
}

void
swapinit(void)
{
  int nbitmap = sb.size / (BSIZE * 8) + 1;
  swapstart = sb.bmapstart + nbitmap;

  if (swapstart + SWAPBLOCKS > sb.size)
    panic("swapinit: swap blocks overflow");

  initlock(&swapmap_lock, "swapmap");
  initlock(&swap_pbuf_lock, "swappbuf");
  swap_hint = 0;
  swapready = 1;
  swap_window_start = 0;
  swap_events = 0;
  swap_pbuf_hand = 0;
  swap_pbuf_cached = 0;
  swap_pbuf_hits = 0;
  swap_pbuf_misses = 0;
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
  if (mycpu()->noff > 0)
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
            swap_throttle();
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

          swap_pbuf_put(slot, pa);

          uint64 next = va + PGSIZE;
          if (next >= sz)
            next = 0;
          p->swap_hand = next;
          swap_throttle();
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
swapout_proc(struct proc *p, int max_pages)
{
  if (!swapready)
    return -1;
  if (p == 0 || p->pagetable == 0)
    return -1;
  if (mycpu()->noff > 0)
    return -1;
  if (max_pages <= 0)
    max_pages = 0x7fffffff;

  uint64 sz = PGROUNDUP(p->sz);
  if (sz == 0)
    return 0;

  uint64 npages = sz / PGSIZE;
  uint64 start = PGROUNDDOWN(p->swap_hand);
  if (start >= sz)
    start = 0;

  uint64 va = start;
  uint64 scanned = 0;
  uint64 max_scans = npages * 2;
  int swapped = 0;

  while (scanned < max_scans && swapped < max_pages) {
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
            if (vma_writeback(vma, va, pa) < 0)
              goto advance;

            *pte = 0;
            sfence_vma();
            kfree((void *)pa);
            swapped++;
            swap_throttle();
            goto advance;
          }

          int slot = swap_alloc();
          if (slot < 0)
            break;

          swap_write(slot, (char *)pa);

          uint flags = PTE_FLAGS(*pte);
          flags = (flags & ~PTE_V) | PTE_S;
          *pte = SWAP2PTE(slot) | flags;
          sfence_vma();

          swap_pbuf_put(slot, pa);
          swapped++;
          swap_throttle();
        }
      }
    }

advance:
    va += PGSIZE;
    if (va >= sz)
      va = 0;
    scanned++;
  }

  p->swap_hand = va;
  return swapped;
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
  char *mem = swap_pbuf_get(slot);
  if (mem == 0)
  {
    mem = kalloc();
    if (mem == 0)
      return -1;
    swap_read(slot, mem);
  }
  swapfree(slot);

  uint flags = PTE_FLAGS(*pte);
  flags = (flags & ~PTE_S) | PTE_V;
  *pte = PA2PTE(mem) | flags;
  sfence_vma();
  swap_throttle();

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
