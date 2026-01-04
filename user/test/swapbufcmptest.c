#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "riscv.h"
#include "user/user.h"

struct case_result {
  int ticks;
  uint64 hits;
  uint64 misses;
};

static void
fail(const char *msg)
{
  printf("swapbufcmptest: %s\n", msg);
  exit(1);
}

static void
run_case(char *base, int pages, int step, int max_ticks, int tag,
         int reverse, const char *label, struct case_result *out)
{
  struct sysinfo before;
  if (sysinfo(&before) < 0)
    fail("sysinfo failed");

  int start = uptime();
  for (int i = 0; i < pages; i++) {
    base[(uint64)i * PGSIZE] = (char)((i ^ tag) & 0xff);
    if (i != 0 && (i % step) == 0)
      printf("swapbufcmptest: %s write %d/%d\n", label, i, pages);
    if (uptime() - start > max_ticks)
      fail("timeout");
  }

  if (reverse) {
    for (int i = pages - 1; i >= 0; i--) {
      char v = base[(uint64)i * PGSIZE];
      if (v != (char)((i ^ tag) & 0xff))
        fail("pattern mismatch");
      if (i != pages - 1 && (i % step) == 0)
        printf("swapbufcmptest: %s read %d/%d\n", label, pages - i, pages);
      if (uptime() - start > max_ticks)
        fail("timeout");
    }
  } else {
    for (int i = 0; i < pages; i++) {
      char v = base[(uint64)i * PGSIZE];
      if (v != (char)((i ^ tag) & 0xff))
        fail("pattern mismatch");
      if (i != 0 && (i % step) == 0)
        printf("swapbufcmptest: %s read %d/%d\n", label, i, pages);
      if (uptime() - start > max_ticks)
        fail("timeout");
    }
  }

  int end = uptime();
  struct sysinfo after;
  if (sysinfo(&after) < 0)
    fail("sysinfo failed");

  out->ticks = end - start;
  out->hits = after.swapbuf_hits - before.swapbuf_hits;
  out->misses = after.swapbuf_misses - before.swapbuf_misses;
  printf("swapbufcmptest: %s ticks=%d hits=%d misses=%d\n",
         label, out->ticks, (int)out->hits, (int)out->misses);
  if (out->hits == 0 && out->misses == 0)
    fail("no swap activity");
}

int
main(int argc, char *argv[])
{
  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  int pages = 0;
  int max_ticks = 6000;
  if (argc > 1)
    pages = atoi(argv[1]);
  if (argc > 2)
    max_ticks = atoi(argv[2]);
  if (max_ticks < 200)
    max_ticks = 200;

  int free_pages = info.freemem / PGSIZE;
  int extra = 64;
  if (pages <= 0)
    pages = free_pages + extra;
  if (pages < 32)
    pages = 32;

  uint64 amt = (uint64)pages * PGSIZE;
  char *base = sbrk(amt);
  if (base == (char *)-1)
    fail("sbrk grow failed");

  int step = pages / 4;
  if (step < 256)
    step = 256;
  printf("swapbufcmptest: start pages=%d max_ticks=%d\n", pages, max_ticks);

  struct case_result base_res;
  struct case_result rev_res;
  run_case(base, pages, step, max_ticks, 0x5a, 0, "baseline", &base_res);
  run_case(base, pages, step, max_ticks, 0x33, 1, "reverse", &rev_res);

  printf("swapbufcmptest: summary baseline ticks=%d hits=%d misses=%d\n",
         base_res.ticks, (int)base_res.hits, (int)base_res.misses);
  printf("swapbufcmptest: summary reverse  ticks=%d hits=%d misses=%d\n",
         rev_res.ticks, (int)rev_res.hits, (int)rev_res.misses);

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("sbrk shrink failed");
  printf("swapbufcmptest: ok\n");
  exit(0);
}
