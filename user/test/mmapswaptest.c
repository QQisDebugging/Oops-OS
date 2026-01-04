#include "param.h"
#include "types.h"
#include "sysinfo.h"
#include "fcntl.h"
#include "riscv.h"
#include "user/user.h"

static int quiet = 0;
static int full = 0;

static void
fail(const char *msg)
{
  if (!quiet)
    printf("mmapswaptest: %s\n", msg);
  exit(1);
}

int
main(int argc, char *argv[])
{
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-q") == 0)
      quiet = 1;
    else if (strcmp(argv[i], "-f") == 0)
      full = 1;
  }
  int pages = 12;
  int len = pages * PGSIZE;
  int fd = open("mmapswap", O_CREATE | O_RDWR);
  if (fd < 0)
    fail("open failed");

  char *zero = sbrk(PGSIZE);
  if (zero == (char *)-1)
    fail("sbrk zero failed");
  memset(zero, 0, PGSIZE);
  for (int i = 0; i < pages; i++) {
    if (write(fd, zero, PGSIZE) != PGSIZE)
      fail("file write failed");
  }

  char *addr = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == (char *)-1)
    fail("mmap failed");
  close(fd);

  for (int i = 0; i < pages; i++) {
    addr[i * PGSIZE] = (char)((i ^ 0x5a) & 0xff);
    addr[i * PGSIZE + 13] = (char)((i ^ 0x33) & 0xff);
  }

  struct sysinfo info;
  if (sysinfo(&info) < 0)
    fail("sysinfo failed");

  uint64 swap_bytes = (uint64)SWAP_PAGES * PGSIZE;
  uint64 target;
  if (full) {
    target = info.freemem + swap_bytes / 4;
  } else {
    target = 8 * 1024 * 1024;
    if (target > info.freemem / 2)
      target = info.freemem / 2;
    if (target < 16 * PGSIZE)
      target = 16 * PGSIZE;
  }
  if (!quiet)
    printf("mmapswaptest: mode=%s target=%lu\n", full ? "full" : "light", target);

  uint64 amt = PGROUNDUP(target);
  char *base = sbrk(0);
  if (sbrk(amt) != base)
    fail("pressure sbrk failed");

  uint64 npages = amt / PGSIZE;
  uint64 step = npages / 20;
  if (step == 0)
    step = 1;
  if (!quiet)
    printf("mmapswaptest: pressure phase\n");
  for (uint64 i = 0; i < npages; i++) {
    base[i * PGSIZE] = (char)i;
    if (!quiet && (i + 1) % step == 0)
      printf("mmapswaptest: pressure %lu/%lu\n", i + 1, npages);
  }

  for (int i = 0; i < pages; i++) {
    char v0 = addr[i * PGSIZE];
    char v1 = addr[i * PGSIZE + 13];
    if (v0 != (char)((i ^ 0x5a) & 0xff) ||
        v1 != (char)((i ^ 0x33) & 0xff))
      fail("pattern mismatch");
  }

  if (sbrk(-((int)amt)) == (char *)-1)
    fail("pressure shrink failed");

  if (munmap(addr, len) < 0)
    fail("munmap failed");

  if (!quiet)
    printf("mmapswaptest: ok\n");
  exit(0);
}
