#include "pagetable_generic.h"


/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
struct frame * head;
struct frame * tail;


int lru_evict(void)
{
	int ret = (tail - coremap);

	// set new head;
	struct frame * tmp = tail;
	tail = tail->prev;

	// clear connection for old head
	tmp->next = NULL;
	tmp->prev = NULL;

	// if new head is not NULL, then its prev should be NULL
	if (tail){
		tail->next = NULL;
	} else{
		head = NULL;
	} 
	return ret;

}

/* This function is called on each access to a page to update any information
 * needed by the LRU algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(int frame)
{
	int idx = frame;

	struct frame * new = &(coremap[idx]);

	// delete from ll if exists
	if (new->next && new->prev){
		// in middle
		struct frame * before = new->prev;
		struct frame * after = new->next;
		before->next = before->next->next;
		after->prev = after->prev->prev;

	} else if(new->next){
		// is head
		struct frame * after = new->next;
		after->prev = NULL;
		head = after;

	} else if(new->prev){
		// is tail
		struct frame * before = new->prev;
		before->next = NULL;
		tail = before;
	} else if (new == head){
		// new is the only element in ll
		//assert(new == tail);
		head = NULL;
		tail = NULL;
	} 



	// add to ll
	if ((!head) && (!tail)){
		head = new;
		tail = new;
	} else if (head == tail){
		new->next = tail;
		new->prev = NULL;
		tail->next = NULL;
		tail->prev = new;
		head = new;
	} else{
		new->next = head;
		new->prev = NULL;
		head->prev = new;
		// head->next unchanged
		head = new;
	}

}

/* Initialize any data structures needed for this replacement algorithm. */
void lru_init(void)
{
	head = NULL;
	tail = NULL;
}

/* Cleanup any data structures created in lru_init(). */
void lru_cleanup(void)
{
	head = NULL;
	tail = NULL;
}


