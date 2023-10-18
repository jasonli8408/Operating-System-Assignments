#ifndef __SWAP_H__
#define __SWAP_H__

#include <sys/types.h>


// Swap functions for use in other files

void swap_init(size_t size);
void swap_destroy(void);

int swap_pagein(unsigned int frame, off_t offset);
off_t swap_pageout(unsigned int frame, off_t offset);


#endif /* __SWAP_H__ */
