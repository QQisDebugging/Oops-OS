#define NPROC 64  // maximum number of processes
#define NCPU 8    // maximum number of CPUs
#define NOFILE 16 // open files per process
#define NFILE 100 // open files per system
#define NINODE 50 // maximum number of active i-nodes
#define NDEV 10   // maximum major device number
#define ROOTDEV 1 // device number of file system root disk
#define MAXARG 32 // max exec arguments
// 指定了文件系统（FS）操作中每次可以写入的最大磁盘块数
// 这意味着任何一个文件系统操作（如读写文件、修改文件系统结构等）
// 最多会涉及 10 个磁盘块。
#define MAXOPBLOCKS 10
#define LOGSIZE (MAXOPBLOCKS * 3) // 指定日志区域的大小为每次可以写入的最大磁盘块数的3倍
#define NBUF (MAXOPBLOCKS * 3)    // size of disk block cache
#define FSSIZE 100000             // size of file system in blocks
#define SWAP_PAGES 512            // swap size in pages (4 blocks per page)
#define SWAPBLOCKS (SWAP_PAGES * 4)
#define MAXPATH 128               // maximum file path name
#define MQMAX 8                   // 消息队列数量
#define DMSG_MAX 128              // direct message max size
#define DMSG_QUEUE_MAX 16         // direct message queue length
#define MONITOR_MAX_NUM 16        // monitor count
#define MONITOR_COND_MAX 16       // condition variables per monitor
#define MLFQ_LEVELS 4             // number of MLFQ levels
#define MLFQ_BOOST_TICKS 200      // periodic priority boost interval
#define MLFQ_SLICE_L0 1           // time slice for level 0
#define MLFQ_SLICE_L1 2           // time slice for level 1
#define MLFQ_SLICE_L2 4           // time slice for level 2
#define MLFQ_SLICE_L3 8           // time slice for level 3
#define SCHED_NORMAL 0            // normal scheduler class
#define SCHED_RT_LLF 1            // real-time LLF scheduler class
