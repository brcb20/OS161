/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/test161.h>
#include <spinlock.h>

/*
 * Use these stubs to test your reader-writer locks.
 */

#define NLOOPS 250
#define NTHREADS 32 

static volatile unsigned long testval1;
static volatile unsigned long testval2;

static struct rwlock *rwlock = NULL;
static struct semaphore *exitsem = NULL;

static struct spinlock status_lock;
static bool test_status = TEST161_FAIL;

static
bool
failif(bool condition) {
	if (condition) {
		spinlock_acquire(&status_lock);
		test_status = TEST161_FAIL;
		spinlock_release(&status_lock);
	}
	return condition;
}

static
void
writethread(void *junk1, unsigned long num)
{
	(void)junk1;

	unsigned i;

	kprintf_n("Write thread %2lu starting...\n", num);
	random_yielder(4);
	for (i=0; i < NLOOPS; i++) {
		random_yielder(4);
		rwlock_acquire_write(rwlock);
		spinlock_acquire(&status_lock);
		testval1 = 0;
		testval2 = num;
		spinlock_release(&status_lock);
		random_yielder(4);
		failif(testval1 != 0);
		random_yielder(4);
		failif(testval2 != num);
		rwlock_release_write(rwlock);
	}
	kprintf_n("Write thread %2lu ending...\n", num);
	V(exitsem);
}

static
void
readthread(void *junk1, unsigned long num)
{

	(void)junk1;
	unsigned i;

	kprintf_n("Read thread %2lu starting...\n", num);
	random_yielder(4);

	for (i=0; i < NLOOPS; i++) {
		random_yielder(4);
		rwlock_acquire_read(rwlock);
		spinlock_acquire(&status_lock);
		testval1++;
		spinlock_release(&status_lock);
		random_yielder(4);
		failif(testval1 == 0);
		random_yielder(4);
		rwlock_release_read(rwlock);
	}
	kprintf_n("Read thread %2lu ending...\n", num);
	V(exitsem);
}

// Next time you write a test and want to control the order of execution
// use wrapper such as read_wrapper or write_wrapper
int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	int i, result;

	kprintf_n("Starting rwt1...\n");
	exitsem = sem_create("exitsem", 0);
	if (exitsem == NULL) {
		panic("rwt1: sem_create failed\n");
	}
	rwlock = rwlock_create("rwlock");
	if (rwlock == NULL) {
		panic("rwt1: rwlock_create failed");
	}
	spinlock_init(&status_lock);
	test_status = TEST161_SUCCESS;
	testval1 = 0;
	
	for (i=0; i < NTHREADS; i++) {
		result = thread_fork("rwt1", NULL, writethread, NULL, i);
		if (result) {
			panic("rwt1: thread_fork failed\n");
		}
		result = thread_fork("rwt1", NULL, readthread, NULL, i);
		if (result) {
			panic("rwt1: thread_fork failed\n");
		}
	}

	for (i=0; i < NTHREADS; i++) {
		P(exitsem);
		P(exitsem);
	}

	sem_destroy(exitsem);
	rwlock_destroy(rwlock);
	exitsem = NULL;
	rwlock = NULL;

	kprintf_n("\n");
	success(test_status, SECRET, "rwt1");

	return 0;
}

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt2 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt2");

	return 0;
}

int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt3 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt3");

	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt4 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt4");

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt5 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt5");

	return 0;
}
