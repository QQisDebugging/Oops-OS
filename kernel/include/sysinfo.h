struct sysinfo {
  uint64 freemem;   // amount of free memory (bytes)
  uint64 nproc;     // number of process
  uint64 nsuspended; // number of suspended processes
  uint64 swapbuf_hits;
  uint64 swapbuf_misses;
  uint64 swapbuf_cached;
};
