/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Andrew Peterson, Karen Reid, Alexey Khrabrov
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2021 Karen Reid
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pagetable_generic.h"
#include "pagetable.h"
#include "swap.h"

// helpers for part 2
bool is_valid(struct pt_entry_s *pte){
	return ((pte->frame_num & VALID) >> 2) & 1;
}

bool is_dirty(struct pt_entry_s *pte){
	return ((pte->frame_num & DIRTY) >> 1) & 1;
}

bool get_referenced(struct pt_entry_s *pte){
	return (pte->frame_num & REF);
}

void set_referenced(struct pt_entry_s *pte, bool val){
	if (val){
		pte->frame_num = pte->frame_num | REF;
	} else{
		pte->frame_num = pte->frame_num & ~REF;
	}
}


// Counters for various events.
// Your code must increment these when the related events occur.
size_t hit_count = 0;
size_t miss_count = 0;
size_t ref_count = 0;
size_t evict_clean_count = 0;
size_t evict_dirty_count = 0;

pt_entry_t ** toplevel;

/*
 * Allocates a frame to be used for the virtual page represented by p.
 * If all frames are in use, calls the replacement algorithm's evict_func to
 * select a victim frame. Writes victim to swap if needed, and updates
 * page table entry for victim to indicate that virtual page is no longer in
 * (simulated) physical memory.
 *
 * Counters for evictions should be updated appropriately in this function.
 */
static int allocate_frame(pt_entry_t *pte)
{
	//printf("allocate frame\n");
	int frame = -1;
	for (size_t i = 0; i < memsize; ++i) {
		if (!coremap[i].in_use) {
			frame = i;
			break;
		}
	}

	if (frame == -1) { // Didn't find a free page.
		// Call replacement algorithm's evict function to select victim
		frame = evict_func();
		assert(frame != -1);

		// All frames were in use, so victim frame must hold some page
		// Write victim page to swap, if needed, and update page table

		// IMPLEMENTATION NEEDED
		pt_entry_t * victim = coremap[frame].pte;
		
		// increment count
		if (victim->frame_num & DIRTY){
			evict_dirty_count ++;
		} else{
			evict_clean_count ++;
		}

		off_t actual_offset = swap_pageout(frame, victim->offset);

		// reset bits if swap_pageout successful
		if (actual_offset != INVALID_SWAP){
			victim->frame_num = (victim->frame_num & ~VALID & ~DIRTY) | ONSWAP;
			victim->offset = actual_offset;
		}

	}

	// Record information for virtual page that will now be stored in frame
	coremap[frame].in_use = true;
	coremap[frame].pte = pte;

	return frame;
}

/*
 * Initializes your page table.
 * This function is called once at the start of the simulation.
 * For the simulation, there is a single "process" whose reference trace is
 * being simulated, so there is just one overall page table.
 *
 * In a real OS, each process would have its own page table, which would
 * need to be allocated and initialized as part of process creation.
 * 
 * The format of the page table, and thus what you need to do to get ready
 * to start translating virtual addresses, is up to you. 
 */
void init_pagetable(void)
{
	//printf("startin");
	toplevel = malloc(sizeof(pt_entry_t *) * PAGE_LEN);
	for (int i = 0; i < PAGE_LEN; i++){
		toplevel[i] = malloc(sizeof(pt_entry_t));
		toplevel[i]->frame_num = 0;
		toplevel[i]->offset = INVALID_SWAP;
	}
}

pt_entry_t ** init_lower(void)
{
	pt_entry_t **page_table = malloc(sizeof(pt_entry_t *) * PAGE_LEN);
	assert(page_table);

	for (int i = 0; i < PAGE_LEN; i++){
		page_table[i] = malloc(sizeof(pt_entry_t));
		page_table[i]->frame_num = 0;
		page_table[i]->offset = INVALID_SWAP;
	}

	// let first entry to be valid upon initialization
	//page_table[0].frame_num = page_table[0].frame_num | VALID;
	return page_table;

}

/*
 * Initializes the content of a (simulated) physical memory frame when it
 * is first allocated for some virtual address. Just like in a real OS, we
 * fill the frame with zeros to prevent leaking information across pages.
 */
static void init_frame(int frame)
{
	//printf("init_frame\n");
	// Calculate pointer to start of frame in (simulated) physical memory
	unsigned char *mem_ptr = &physmem[frame * SIMPAGESIZE];
	memset(mem_ptr, 0, SIMPAGESIZE); // zero-fill the frame
}

/*
 * Locate the physical frame number for the given vaddr using the page table.
 *
 * If the page table entry is invalid and not on swap, then this is the first 
 * reference to the page and a (simulated) physical frame should be allocated 
 * and initialized to all zeros (using init_frame).
 *
 * If the page table entry is invalid and on swap, then a (simulated) physical 
 * frame should be allocated and filled by reading the page data from swap.
 *
 * When you have a valid page table entry, return the start of the page frame
 * that holds the requested virtual page.
 *
 * Counters for hit, miss and reference events should be incremented in
 * this function.
 */
unsigned char *find_physpage(vaddr_t vaddr, char type)
{
	// IMPLEMENTATION NEEDED

	// Use your page table to find the page table entry (pte) for the 
	// requested vaddr. 

	unsigned idx1 = TOP_INDEX(vaddr);
	unsigned idx2 = MID_INDEX(vaddr);
	unsigned idx3 = BOT_INDEX(vaddr);

	// init second level if needed
	if (!(toplevel[idx1]->frame_num & VALID)){
		//printf("track_down 2nd level from %d at frame %ld\n", idx1, toplevel[idx1]->frame_num);
		toplevel[idx1]->lower = init_lower();
		toplevel[idx1]->frame_num = toplevel[idx1]->frame_num | VALID;
	} else{
		//printf("frame 1 valid, frame_num = %ld\n", toplevel[idx1]->frame_num);
	}
	//printf("frame 1 points to %p\n", toplevel[idx1]);

	// init third level if needed
	pt_entry_t ** second_level = toplevel[idx1]->lower;

	if (!(second_level[idx2]->frame_num & VALID)){
		//printf("track_down 3rd level from %d at frame %ld\n", idx2, second_level[idx2]->frame_num );
		second_level[idx2]->lower = init_lower();
		second_level[idx2]->frame_num = second_level[idx2]->frame_num | VALID;
	} 

	//printf("frame 2 points to %p\n", second_level[idx2]);

	// 3rd level page address
	//printf("vaddr = %ld, idx1 = %d, idx2 = %d, idx3 = %d\n", vaddr, idx1, idx2, idx3);
	pt_entry_t * pt = (second_level[idx2]->lower)[idx3];
	//printf("page address = %p\n", pt);


	// Check if pte is valid or not, on swap or not, and handle appropriately.
	// You can use the allocate_frame() and init_frame() functions here,
	// as needed.
	
	if (is_valid(pt)){
		hit_count ++;
	} else{
		miss_count ++;

		// create new frame
		unsigned long new_frame = allocate_frame(pt);
		
		// swap page back or init frame 
		if (pt->frame_num & ONSWAP){
			swap_pagein(new_frame, pt->offset);
			pt->frame_num = new_frame << PAGE_SHIFT;
			pt->frame_num = pt->frame_num & ~DIRTY;
		} else{
			init_frame(new_frame);
			pt->frame_num = new_frame << PAGE_SHIFT;
			pt->frame_num = pt->frame_num | DIRTY;
		}
		//printf("alloc frame = %ld, shifted frame = %ld\n", new_frame, new_frame << PAGE_SHIFT);
		//pt->frame_num = new_frame << PAGE_SHIFT;

	}
	// Make sure that pte is marked valid and referenced. Also mark it
	// dirty if the access type indicates that the page will be written to.
	// (Note that a page should be marked DIRTY when it is first accessed, 
	// even if the type of first access is a read (Load or Instruction type).

	pt->frame_num = pt->frame_num | VALID;
	pt->frame_num = pt->frame_num | REF;
	ref_count ++;

	if (type == 'M' || type == 'S') {
		pt->frame_num = pt->frame_num | DIRTY;
	}


	// Call replacement algorithm's ref_func for this page.
	assert(pt->frame_num != (unsigned long) -1);
	ref_func(pt->frame_num);
	//printf("finished find_physpage\n");
	// Return pointer into (simulated) physical memory at start of frame
	return &physmem[(pt->frame_num >> PAGE_SHIFT) * SIMPAGESIZE];

}


void print_pagetable(void)
{
}


void free_pagetable(void)
{

}
