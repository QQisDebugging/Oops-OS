#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "riscv.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("swapbuftest: %s\n", msg);
  exit(1);
}

int
main(int argc, char *argv[])
{
  struct sysinfo before;
  if (sysinfo(&before) < 0)
    fail("sysinfo failed");

  int pages = 0;
  int max_ticks = 4000;
  if (argc > 1)
    pages = atoi(argv[1]);
  if (argc > 2)
    max_ticks = atoi(argv[2]);
  if (max_ticks < 100)
    max_ticks = 100;

  int free_pages = before.freemem / PGSIZE;
  int extra = 64;
  if (pages <= 0)
    pages = free_pages + extra;
  if (pages < 16)
    pages = 16;

  uint64 amt = (uint64)pages * PGSIZE;
  char *base = sbrk(amt);
  if (base == (char *)-1)
    fail("sbrk grow failed");

  int start = uptime();
  int step = pages / 4;
  if (step < 256)
    step = 256;
  printf("swapbuftest: start pages=%d max_ticks=%d\n", pages, max_ticks);

  for (int i = 0; i < pages; i++) {
    base[(uint64)i * PGSIZE] = (char)(i & 0xff);
    if (i != 0 && (i % step) == 0)
      printf("swapbuftest: write %d/%d\n", i, pages);
    if (uptime() - start > max_ticks)
      fail("timeout");
  }
  for (int i = pages - 1; i >= 0; i--) {
    char v = base[(uint64)i * PGSIZE];
    if (v != (char)(i & 0xff))
      fail("pattern mismatch");
    if (i != pages - 1 && (i % step) == 0)
      printf("swapbuftest: read %d/%d\n", pages - i, pages);
    if (uptime() - start > max_ticks)
      fail("timeout");
  }

  struct sysinfo after;
  if (sysinfo(&after) < 0)
    fail("sysinfo failed");
  printf("swapbuftest: stats hits=%d misses=%d cached=%d\n",
         (int)(after.swapbuf_hits - before.swapbuf_hits),
         (int)(after.swapbuf_misses - before.swapbuf_misses),
         (int)after.swapbuf_cached);
  if (after.swapbuf_hits == before.swapbuf_hits &&
      after.swapbuf_misses == before.swapbuf_misses)
    fail("no swap activity");

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("sbrk shrink failed");
  printf("swapbuftest: ok\n");
  exit(0);
}
