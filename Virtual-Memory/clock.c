#include <stdlib.h>
#include "pagetable_generic.h"

/* Page to evict is chosen using the CLOCK algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int clock_hand = 0;

int clock_evict(void)
{
	int ret = -1;
	while (1) {
		if (get_referenced(coremap[clock_hand].pte)) {	
			set_referenced(coremap[clock_hand].pte, 0);
		}
		else { 
			// found index to be evited
			ret = clock_hand; 
			break;

		}
		// increment in a circular way
		clock_hand++; 
		clock_hand = clock_hand % memsize;
	}

	clock_hand++; 
	clock_hand = clock_hand % memsize;

	return ret;
}

/* This function is called on each access to a page to update any information
 * needed by the CLOCK algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void clock_ref(int frame)
{
	(void) frame;
}

/* Initialize any data structures needed for this replacement algorithm. */
void clock_init(void)
{
	clock_hand = 0;
}

/* Cleanup any data structures created in clock_init(). */
void clock_cleanup(void)
{
}
