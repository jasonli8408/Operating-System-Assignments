#ifndef __PAGETABLE_H__
#define __PAGETABLE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>


// User-level virtual addresses on a 64-bit Linux system are 48 bits in our
// traces, and the page size is 4096 (12 bits). The remaining 36 bits are
// the virtual page number, which is used as the lookup key (or index) into
// your page table. 

#define PAGE_LEN 4096
#define REF (0x1)
#define DIRTY (0x2) 
#define VALID (0x4) 
#define ONSWAP (0x8) 

#define TOP_SHIFT 36
#define MID_SHIFT 24
#define BOT_SHIFT 12
#define LOWER_BITS (PAGE_SIZE - 1)

#define TOP_INDEX(x) (((x) >> TOP_SHIFT) & LOWER_BITS)
#define MID_INDEX(x) (((x) >> MID_SHIFT) & LOWER_BITS)
#define BOT_INDEX(x) (((x) >> BOT_SHIFT) & LOWER_BITS)


// Page table entry 
// This structure will need to record the physical page frame number
// for a virtual page, as well as the swap offset if it is evicted. 
// You will also need to keep track of the Valid, Dirty and Referenced
// status bits (or flags). 
// You do not need to keep track of Read/Write/Execute permissions.
typedef struct pt_entry_s {
	size_t frame_num;
	off_t offset;
	struct pt_entry_s ** lower;
} pt_entry_t;

#endif /* __PAGETABLE_H__ */