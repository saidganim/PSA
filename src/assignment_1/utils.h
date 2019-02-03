#ifndef UTILS_MOD
#define UTILS_MOD


static const int MEM_SIZE = 512 * 1024; // Memory size is 2MB
static const int CACHE_SIZE = 8 * 1024; // Cache size is 32KB
static const int CACHE_SET_SIZE = 8;
static const int CACHE_SETS_NUMBER = CACHE_SIZE / CACHE_SET_SIZE;
static const int MAX_COUNTER = (2048 + 1);

#define DRAM_IDENTIFIER        0xffffffff

#define CACHEL_MODIFIED        0x1
#define CACHEL_OWNED           0x2
#define CACHEL_EXCLUSIVE       0x3
#define CACHEL_SHARED          0x4
#define CACHEL_REQUESTED       0x5  // this is a new state which is needed to show that current cacheline is requested
                                    // to eliminate a problem of two consequative requests and two exclusive states  
#define CACHEL_INVALID         0x0

#define CACHETAG_MASK          (~0b111111111111)   // 0b11..10000000000000000
#define CACHEINDEX_MASK        0b111111100000
#define CACHETAG_SHIFT         12
#define CACHEINDEX_SHIFT       5

#endif