#include <stdlib.h>
#include "pagetable_generic.h"


/* Page to evict is chosen using the RAND algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int rand_evict(void)
{
	//NOTE: We keep the default seed (don't call srandom) for repeatable results
	return random() % memsize;
}

/* This function is called on each access to a page to update any information
 * needed by the RAND algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void rand_ref(int frame)
{
	(void)frame;
}

/* Initialize any data structures needed for this replacement algorithm. */
void rand_init(void)
{
}

/* Cleanup any data structures created in rand_init(). */
void rand_cleanup(void)
{
}
