#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       1000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define QUANTUM      2    // maximum number of ticks per process
#define MLFLEVELS    4     // number of levels in multilevel feedback array
#define MAXAGE       5     // process max age
#define TIMEUNIT     10    // process time unit
#define NOSEM        16    // open semaphores per processs
#define NSEM         100   // open semaphores per system
#define MAXSTACKPGS  10    // maximum number of pages between the stack and the process
