#include "thread.h"
#include "interrupt.h"
#include "test_thread.h"

int
main(int argc, char **argv)
{
	thread_init();
	/* Test preemptive threads */
	test_preemptive();
	return 0;
}
