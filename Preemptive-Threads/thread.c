#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

/* This is the wait queue structure */
struct wait_queue {
    Tid thread_id;
    struct wait_queue * next;
};

/* This is the thread control block */
struct thread {
    Tid thread_id;
    ucontext_t uc;
    unsigned long * sp;

    // 1 when is runnable, 0 when not runnable
    int runnable;

    struct wait_queue * location;

    struct wait_queue * wq;

    int exit_code;

    int locked;
};

struct thread active_threads[THREAD_MAX_THREADS];
int exit_codes[THREAD_MAX_THREADS];
struct thread * curr_thread;
static const struct thread empty_thread = {0};
int from_exit;
Tid locked_count = 0;

void
thread_init(void)
{
    /* Add necessary initialization for your threads library here. */
	/* Initialize the thread control block for the first thread */
    // set curr to be thread 0
    // register_interrupt_handler(1);
    interrupts_off();
    curr_thread = &(active_threads[0]);
    curr_thread->thread_id = 0;
    curr_thread->sp = NULL;
    curr_thread->runnable = 1;
    curr_thread->location = NULL;
    int res = getcontext(&(curr_thread->uc));

    for (int i = 0; i < THREAD_MAX_THREADS; i++){
        exit_codes[i] = 0;
    }

    assert(res == 0);
    interrupts_on();
}

Tid
thread_id()
{
    return curr_thread->thread_id;
}

// helper to free stack when it's not NULL
void 
guarded_free(void * sp)
{
    if (sp != NULL) {
        free(sp);
    }
}

// clean unrunnable threads - helper in stub
void
clean_unrunnable()
{
    for (int i = 0; i < THREAD_MAX_THREADS; i++){
        if ((active_threads[i]).runnable == 0){
            guarded_free((active_threads[i]).sp);
            active_threads[i] = empty_thread;
        }
    }
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
    clean_unrunnable();
    interrupts_on();
	thread_main(arg); // call thread_main() function with arg
	thread_exit(0);
}

Tid
thread_create(void (*fn) (void *), void *parg)
{   
    // assume initialized
    interrupts_off();
    clean_unrunnable();

    Tid new_id = -1;

    // find non-occupied thread id
    for (int i = 0; i < THREAD_MAX_THREADS; i++){
        //ADDED THIS TO HANDLE CASE WHERE OCCUPIED = 0 but runnable = 1
        if (active_threads[i].runnable == 0){
            new_id = i;
            break;
        }
    }

    // all threads occupied
    if (new_id == -1){
        interrupts_on();
        return THREAD_NOMORE;
    }

    int res = getcontext(&(active_threads[new_id].uc));
    assert(res == 0);


    active_threads[new_id].sp = malloc(THREAD_MIN_STACK);
    if (!(active_threads[new_id].sp)){
        interrupts_on();
        return THREAD_NOMEMORY;
    }
    active_threads[new_id].location = NULL;
    active_threads[new_id].thread_id = new_id;
    active_threads[new_id].runnable = 1;
    active_threads[new_id].uc.uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
    active_threads[new_id].uc.uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;
    active_threads[new_id].uc.uc_mcontext.gregs[REG_RIP] = (unsigned long) thread_stub;
    //active_threads[new_id].uc.uc_mcontext.gregs[REG_RBP] = (unsigned long) 0;
    active_threads[new_id].uc.uc_mcontext.gregs[REG_RSP] = (unsigned long) active_threads[new_id].sp + THREAD_MIN_STACK - sizeof(unsigned long);
    interrupts_on();
    return new_id;
}

Tid
thread_yield(Tid want_tid)
{
    int old = interrupts_off();
    volatile int finished_cw = 0;

    // tid out of bounds or not created
    if (want_tid < -2 || want_tid >= THREAD_MAX_THREADS || (want_tid >= 0 && active_threads[want_tid].runnable == 0)){
        interrupts_set(old);
        return THREAD_INVALID;
    }

    // yield to self, before checking other threads
    if (want_tid == THREAD_SELF){
        interrupts_set(old);
        return curr_thread->thread_id;
    }

    // main thread
    if (want_tid == 0 && curr_thread->thread_id == 0){
        interrupts_set(old);
        return curr_thread->thread_id;
    }

    Tid any;
    // check for created threads other than self


    // WHY WILL THIS HAPPEN THO?? --> CAUSES SEG FAULT IN ./test_wait_kill
    if (! curr_thread){
        return -1;
    }


    int no_more = 1;
    for (int i = 0; i < THREAD_MAX_THREADS; i++){
        if (i != curr_thread->thread_id && (active_threads[i]).runnable == 1 && active_threads[i].location == NULL){
            no_more = 0;
            // save an available TID for THREAD_ANY
            any = (active_threads[i]).thread_id;
            break;
        }
    }

    if (no_more){
        interrupts_set(old);
        return THREAD_NONE;
    }

    // case where we want to get any thread, and "any" has already been computed
    if (want_tid == THREAD_ANY){
        if (curr_thread->thread_id != 1023 && active_threads[curr_thread->thread_id + 1].runnable && active_threads[curr_thread->thread_id + 1].location == NULL){
            want_tid = curr_thread->thread_id + 1;
        } else {
            want_tid = any;
        }
    }



    //struct thread * old = curr_thread;
    int res = getcontext(&(curr_thread->uc));
    assert(res == 0);

    if (finished_cw == 0){


        finished_cw = 1;
        curr_thread = &(active_threads[want_tid]);

        int ret = setcontext(&(curr_thread->uc));
        assert(!ret);
    }
    if (from_exit){
        clean_unrunnable();
        from_exit = 0;
    } 
    interrupts_set(old);
    return want_tid;
}


void
clean_up_locked(){
    for (int i = 0; i < THREAD_MAX_THREADS; i++){
        active_threads[i].locked = 0;
    }
}


Tid
thread_yield_lock(Tid want_tid)
{
    int old = interrupts_off();
    volatile int finished_cw = 0;

    // tid out of bounds or not created
    if (want_tid < -2 || want_tid >= THREAD_MAX_THREADS || (want_tid >= 0 && active_threads[want_tid].runnable == 0)){
        interrupts_set(old);
        return THREAD_INVALID;
    }

    // yield to self, before checking other threads
    if (want_tid == THREAD_SELF){
        interrupts_set(old);
        return curr_thread->thread_id;
    }

    // main thread
    if (want_tid == 0 && curr_thread->thread_id == 0){
        interrupts_set(old);
        return curr_thread->thread_id;
    }

    Tid any;
    // check for created threads other than self


    // WHY WILL THIS HAPPEN THO?? --> CAUSES SEG FAULT IN ./test_wait_kill
    if (! curr_thread){
        return -1;
    }


    int no_more = 1;
    for (int i = locked_count; i < THREAD_MAX_THREADS; i++){
        if (i != curr_thread->thread_id && (active_threads[i]).runnable == 1 && active_threads[i].location == NULL && active_threads[i].locked == 0){
            no_more = 0;
            // save an available TID for THREAD_ANY
            any = (active_threads[i]).thread_id;
            break;
        }
    }

    

    if (no_more == 1){
        for (int i = 0; i < THREAD_MAX_THREADS; i++){
        if (i != curr_thread->thread_id && (active_threads[i]).runnable == 1 && active_threads[i].location == NULL && active_threads[i].locked == 0){
            no_more = 0;
            // save an available TID for THREAD_ANY
            any = (active_threads[i]).thread_id;
            break;
            }
        }
    }

    if (no_more){
        //printf("CLEAN UP\n");
        clean_up_locked();
        locked_count = 0;
        any = 1;
    } else{
        //printf("thread %d will yield to %d\n", curr_thread->thread_id, any);
    }

    // case where we want to get any thread, and "any" has already been computed
    if (want_tid == THREAD_ANY){
        want_tid = any;
    }

    //struct thread * old = curr_thread;
    int res = getcontext(&(curr_thread->uc));
    assert(res == 0);

    if (finished_cw == 0){


        finished_cw = 1;
        curr_thread = &(active_threads[want_tid]);

        int ret = setcontext(&(curr_thread->uc));
        assert(!ret);
    }
    if (from_exit){
        clean_unrunnable();
        from_exit = 0;
    } 
    interrupts_set(old);
    return want_tid;
}

void
thread_exit(int exit_code)
{
    int res = interrupts_off();
    // // printf("thread %d being exited with exit code %d\n", curr_thread->thread_id, exit_code);
    int last_thread = 1;
    for (int i = 0; i < THREAD_MAX_THREADS; i++){
        if ((active_threads[i]).runnable == 1 && i != curr_thread->thread_id){
            last_thread = 0;
            break;
        }
    }

    exit_codes[curr_thread->thread_id] = exit_code;

    if (curr_thread->wq){
        //curr_thread->exit_code = curr_thread->wq->next->thread_id;
        thread_wakeup(curr_thread->wq, 1);
    } else{
        // // printf("exit: thread %d has no wq\n", curr_thread->thread_id);
    }

    // clean memory
    if (curr_thread->wq){
        struct wait_queue * curr = curr_thread->wq;
        struct wait_queue * tmp;
        while (curr){
            tmp = curr;
            curr = curr->next;
            free(tmp);
        }
    } 

    from_exit = 1;

    // waking waited threads
    if (last_thread){
        // handle case of NULL sp on thread 0
        //free(curr_thread->sp);
        curr_thread->runnable = 0;
        active_threads[curr_thread->thread_id] = empty_thread;
        curr_thread = NULL;
        interrupts_set(res);
        exit(exit_code);
    } else {
        // mark it as destroyed, and yield to any other thread
        curr_thread->runnable = 0;
        //interrupts_set(res);
        thread_yield(THREAD_ANY);
        interrupts_set(res);
    }
}

Tid
thread_kill(Tid tid)
{
    int res = interrupts_off();
    // // printf("thread %d being killed\n", tid);

    if (tid < 0 || tid >= THREAD_MAX_THREADS){
        interrupts_set(res);
        return THREAD_INVALID;
    }

    if (tid == curr_thread->thread_id || active_threads[tid].runnable == 0){
        interrupts_set(res);
        return THREAD_INVALID;
    }

    exit_codes[curr_thread->thread_id] = THREAD_KILLED;

    if (curr_thread->wq){
        //curr_thread->exit_code = curr_thread->wq->next->thread_id;
        thread_wakeup(curr_thread->wq, 1);
    } else{
        // // printf("kill: thread %d has no wq\n", curr_thread->thread_id);
    }

    if (active_threads[tid].wq){
        struct wait_queue * curr = active_threads[tid].wq;
        struct wait_queue * tmp;
        while (curr){
            tmp = curr;
            curr = curr->next;
            free(tmp);
        }
    }

    guarded_free((active_threads[tid]).sp);
    active_threads[tid].runnable = 0;
    active_threads[tid] = empty_thread;

    interrupts_set(res);
    return tid;
}

/**************************************************************************
 * Important: The rest of the code should be implemented in Assignment 3. *
 **************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
    int res = interrupts_off();
    struct wait_queue *wq;

    wq = malloc(sizeof(struct wait_queue));
    wq->thread_id = -1;
    wq->next = NULL;
    interrupts_set(res);
    return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
    interrupts_off();
    if (wq == NULL){
        interrupts_on();
        return;
    }
    while (wq) {
        struct wait_queue * tmp = wq;
        wq = wq->next;
        free(tmp);
    }
    interrupts_on();
}

Tid
thread_sleep(struct wait_queue *queue)
{
    int res = interrupts_off();
    if (queue == NULL){
        interrupts_set(res);
        return THREAD_INVALID;
    }

    Tid any;
    // check for created threads other than self
    int no_more = 1;
    for (int i = curr_thread->thread_id+1; i < THREAD_MAX_THREADS; i++){
        if (i != curr_thread->thread_id && (active_threads[i]).runnable == 1 && active_threads[i].location == NULL){
            no_more = 0;
            // save an available TID for THREAD_ANY
            any = (active_threads[i]).thread_id;
            break;
        }
    }

    if (no_more){
        for (int i = 0; i < THREAD_MAX_THREADS; i++){
        if (i != curr_thread->thread_id && (active_threads[i]).runnable == 1 && active_threads[i].location == NULL){
            no_more = 0;
            // save an available TID for THREAD_ANY
            any = (active_threads[i]).thread_id;
            break;
            }
        }
    }

    // if (curr_thread->thread_id < THREAD_MAX_THREADS - 1 && (active_threads[curr_thread->thread_id + 1]).runnable == 1 && active_threads[curr_thread->thread_id + 1].location == NULL){
    //     no_more = 0;
    //     any = (active_threads[curr_thread->thread_id + 1]).thread_id;
    // }

    if (no_more){
        interrupts_set(res);
        return THREAD_NONE;
    }

    struct wait_queue * new = malloc(sizeof(struct wait_queue));
    new->thread_id = curr_thread->thread_id;
    new->next = NULL;

    while (queue->next){
        queue = queue->next;
        assert(queue != queue->next);
    }
    queue->next = new;
    
    // now this thread sleeping
    curr_thread->location = new;

    //getcontext(&(curr_thread->uc));
    ///// // printf("thread %d slept @ %p, yielding to thread %d with location: %p = NULL\n", curr_thread->thread_id, curr_thread->location, any, active_threads[any].location);
    //interrupts_set(res);
    int ret = thread_yield(any);
    interrupts_set(res);
    return ret;
}


Tid
thread_sleep_lock(struct wait_queue *queue)
{
    int res = interrupts_off();
    if (queue == NULL){
        interrupts_set(res);
        return THREAD_INVALID;
    }

    Tid any;
    // check for created threads other than self
    int no_more = 1;
    for (int i = curr_thread->thread_id+1; i < THREAD_MAX_THREADS; i++){
        if (i != curr_thread->thread_id && (active_threads[i]).runnable == 1 && active_threads[i].location == NULL){
            no_more = 0;
            // save an available TID for THREAD_ANY
            any = (active_threads[i]).thread_id;
            break;
        }
    }

    if (no_more){
        for (int i = 0; i < THREAD_MAX_THREADS; i++){
        if (i != curr_thread->thread_id && (active_threads[i]).runnable == 1 && active_threads[i].location == NULL){
            no_more = 0;
            // save an available TID for THREAD_ANY
            any = (active_threads[i]).thread_id;
            break;
            }
        }
    }

    // if (curr_thread->thread_id < THREAD_MAX_THREADS - 1 && (active_threads[curr_thread->thread_id + 1]).runnable == 1 && active_threads[curr_thread->thread_id + 1].location == NULL){
    //     no_more = 0;
    //     any = (active_threads[curr_thread->thread_id + 1]).thread_id;
    // }

    if (no_more){
        interrupts_set(res);
        return THREAD_NONE;
    }

    struct wait_queue * new = malloc(sizeof(struct wait_queue));
    new->thread_id = curr_thread->thread_id;
    new->next = NULL;

    while (queue->next){
        queue = queue->next;
        assert(queue != queue->next);
    }
    queue->next = new;
    
    // now this thread sleeping
    curr_thread->location = new;

    //getcontext(&(curr_thread->uc));
    ///// // printf("thread %d slept @ %p, yielding to thread %d with location: %p = NULL\n", curr_thread->thread_id, curr_thread->location, any, active_threads[any].location);
    //interrupts_set(res);
    int ret = thread_yield_lock(any);
    interrupts_set(res);
    return ret;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
    int res = interrupts_off();
    // // printf("thread %d starting to wake its wq\n", curr_thread->thread_id);
    if (queue == NULL){
        interrupts_on();
        return 0;
    }


    if (!queue->next){
        return 0;
    }

    int count = 0;
    struct wait_queue *tmp = NULL;
    while (queue->next){
        // // printf("thread %d waking %d\n", curr_thread->thread_id, queue->next->thread_id);
        tmp = queue -> next;
        active_threads[tmp->thread_id].location = NULL;
        // printf("waking thread %d\n", tmp->thread_id);
        queue->next = queue->next->next;
        free(tmp);

        if (!all){
            interrupts_set(res);
            return 1;
        }
        count ++;
    }

    interrupts_set(res);
    return count;

}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
    int res = interrupts_off();
    if (tid < 0 || tid >= THREAD_MAX_THREADS || tid == curr_thread->thread_id || active_threads[tid].runnable == 0){
        if (exit_code){
            *exit_code = THREAD_INVALID;
        }
        interrupts_set(res);
        return THREAD_INVALID;
    }

    struct wait_queue * queue;
    int first_caller;
    if (!active_threads[tid].wq){
        // FIRST CALLER
        first_caller = 1;
        queue = wait_queue_create();
        active_threads[tid].wq = queue;

    } else{
        // NOT FIRST CALLER
        first_caller = 0;
        queue = active_threads[tid].wq;
    }      
    // // printf("wait: current = %d sleeping, waiting till %d exit\n", curr_thread->thread_id, tid);
    thread_sleep(queue);


    // set exit code when waken up
    // // printf("curr_thread = %d finished sleeping, is first caller = %d\n", curr_thread->thread_id, first_caller);

    if (exit_code){
        *exit_code = exit_codes[tid];

        // // printf("exit code for thread %d is %d, e_code = %d\n",  curr_thread->thread_id, *exit_code, curr_thread->exit_code);
    }   else{
        // // printf("exit code for thread %d is NULL\n",  curr_thread->thread_id);
    }

    if (first_caller){
        interrupts_set(res);
        return tid;
    } else {
        interrupts_set(res);
        return THREAD_INVALID;
    }
}

struct lock {
    int mutex;
    struct wait_queue * wq;
    Tid thread_id;
};

struct lock *
lock_create()
{
    interrupts_off();
    struct lock *lock = malloc(sizeof(struct lock));
    lock->mutex = 0;
    lock->thread_id = -1;
    lock->wq = wait_queue_create();
    interrupts_on();
    return lock;
}

void
lock_destroy(struct lock *lock)
{
    interrupts_off();
    if (lock->mutex == 0){
        wait_queue_destroy(lock->wq);
        free(lock);
    } else{
        // printf("cannot destroy, thread locked\n");
    }
    interrupts_on();
}

void
lock_acquire(struct lock *lock)
{
    int res = interrupts_off();
    assert(lock != NULL);

	while(lock->mutex == 1){
        //printf("sleeping\n");
		thread_sleep(lock->wq);
	}
	lock->mutex = 1;
    lock->thread_id = curr_thread->thread_id;
    // // if (curr_thread->thread_id != curr_locked_thread && lock->mutex == 0){
    // //     interrupts_set(res);
    // //     return;
    // // }

    // if (lock->mutex == 0){
    //     assert(curr_thread->thread_id > -1);
    //     assert(lock->wq->next == NULL);
    //     printf("curr_thread->thread_id = %d, curr_locked_thread %d\n", curr_thread->thread_id, locked_count);

    //     lock->mutex = 1;
    //     lock->thread_id = curr_thread->thread_id;
    //     active_threads[lock->thread_id].locked = 1;
    //     locked_count += 1;

    //     // if (locked_count == 128){
    //     //     clean_up_locked();
    //     // }

    //     printf("locked thread %d\n", lock->thread_id);
    // }  else {
    //     printf("thread %d sleeping, waiting for thread %d to release lock\n", curr_thread->thread_id, lock->thread_id);
    //     //thread_sleep(lock->wq);

    //     struct wait_queue * queue = lock->wq;
    //     struct wait_queue * new = malloc(sizeof(struct wait_queue));
    //     new->thread_id = curr_thread->thread_id;
    //     new->next = NULL;

    //     while (queue->next){
    //         queue = queue->next;
    //         assert(queue != queue->next);
    //     }
    //     queue->next = new;
        
    //     // now this thread sleeping
    //     curr_thread->location = new;

    //     thread_yield_lock(THREAD_ANY);

    //     printf("thread %d finished sleeping on thread %d\n", curr_thread->thread_id, lock->thread_id);

    // }
    
    interrupts_set(res);

}


void
lock_release(struct lock *lock)
{
    interrupts_off();
    assert(lock != NULL);
    if (lock->mutex == 0){
        interrupts_on();
        return;
    }

    // printf("releasing slept thread for thread %d, curr_thread = %d\n", lock->thread_id, curr_thread->thread_id);
    thread_wakeup(lock->wq, 1);

    assert(lock->wq);
    lock->thread_id = -1;
    lock->mutex = 0;
    interrupts_on();
}

struct cv {
    int waiting;
    struct wait_queue * wq;
};

struct cv *
cv_create()
{
    interrupts_off();
    struct cv *cv;
    cv = malloc(sizeof(struct cv));
    assert(cv);
    cv->wq = wait_queue_create();
    cv->waiting = 0;

    interrupts_on();
    return cv;
}

void
cv_destroy(struct cv *cv)
{
    assert(cv != NULL);

    interrupts_off();
    if (cv->waiting == 0){
        wait_queue_destroy(cv->wq);
        free(cv);
    } else{
        // printf("cannot destroy, threads waiting on cv\n");
    }
    interrupts_on();
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    int res = interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);
    if (curr_thread->thread_id != lock->thread_id){
        return;
    }

    lock_release(lock);

    thread_sleep(cv->wq);

    interrupts_set(res);

    lock_acquire(lock);

    interrupts_on();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);

    thread_wakeup(cv->wq, 0);

    interrupts_on();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    interrupts_off();
    assert(cv != NULL);
    assert(lock != NULL);

    thread_wakeup(cv->wq, 1);

    interrupts_on();
}
