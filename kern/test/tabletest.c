#include <cdefs.h>
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <table.h>
#include <test.h>

#define BIGTESTSIZE 10000
#define NLOOPS 500
#define NTHREADS 40

struct test {
	void *ptr;
};

#define NTH(i) ((struct test *)(0xb007 + 3*(i)))

#ifndef TESTINLINE 
#define TESTINLINE INLINE
#endif


DECLTABLE(test, TESTINLINE);
DEFTABLE(test, TESTINLINE);

static struct testtable *tb = NULL;
static struct semaphore *startsem = NULL;
static struct semaphore *endsem = NULL;

int
tabletest(int nargs, char **args)
{
	struct table *tb;
	void *p;
	unsigned long i, x;
	int result;


	(void)nargs;
	(void)args;

	x = 0;

	kprintf("Beginning table test...\n");
	tb = table_create();
	KASSERT(tb != NULL);

	p = (void *)0xc0ffee;
	for (i=0; i<BIGTESTSIZE; i++) {
		result = table_add(tb, p, &x);
		KASSERT(result == 0);
		KASSERT(x == i);
	}
	KASSERT(table_num(tb) == BIGTESTSIZE);

	for (i=0; i<BIGTESTSIZE; i++) {
		KASSERT(table_get(tb, i) == p);
	}

	for (i=0; i<BIGTESTSIZE; i++) {
		table_remove(tb, i);
		KASSERT(table_get(tb, i) == NULL);
		KASSERT(table_num(tb) == BIGTESTSIZE - (i+1));
	}
	KASSERT(table_num(tb) == 0);

	for (i=0; i<BIGTESTSIZE; i++) {
		result = table_set(tb, i, (void *)NTH(i));
		KASSERT(result == 0);
		KASSERT(table_num(tb) == i+1);
	}

	for (i=0; i<BIGTESTSIZE; i++) {
		KASSERT(table_get(tb, i) == (void *)NTH(i));
		table_remove(tb, i);
		KASSERT(table_get(tb, i) == NULL);
		KASSERT(table_num(tb) == BIGTESTSIZE - 1);
		result = table_setfirst(tb, (void *)NTH(i), 0, &x);
		KASSERT(result == 0);
		KASSERT(x == i);
		KASSERT(table_num(tb) == BIGTESTSIZE);
		KASSERT(table_get(tb, i) == (void *)NTH(i));
	}

	/* Check that table is full */
	KASSERT(table_setfirst(tb, NTH(i), 0, &x) == 2);

	if (BIGTESTSIZE > 250) {
		table_remove(tb, 234);
		table_remove(tb, 35);
		KASSERT(table_setfirst(tb, p, 235, &x) == 2);
		table_setfirst(tb, p, 230, &x);
		KASSERT(table_get(tb, 234) == p);
		table_setfirst(tb, p, 28, &x);
		KASSERT(table_get(tb, 35) == p);
	}


	for (i=0; i<BIGTESTSIZE; i++) {
		table_remove(tb, i);
		KASSERT(table_num(tb) == BIGTESTSIZE - (i+1));
	}

	table_destroy(tb);
		
	kprintf("Done.\n");

	return 0;
}

static 
void
aandr(void *unused1, unsigned long index) {
	unsigned long i;
	int result;

	(void)unused1;

	P(startsem);
	random_yielder(4);

	for (i = 0; i < NLOOPS; i++) {
		random_yielder(4);
		result = testtable_set(tb, index, NTH(index));
		KASSERT(result == 0);
		random_yielder(4);
		KASSERT(testtable_get(tb, index) == NTH(index));
		random_yielder(4);
		testtable_remove(tb, index);
		random_yielder(4);
		KASSERT(testtable_get(tb, index) == NULL);
	}

	V(endsem);
}
	
int
tabletest2(int nargs, char **args)
{
	unsigned long i;
	int result;


	(void)nargs;
	(void)args;

	kprintf("Beginning threaded table test...\n");
	tb = testtable_create();
	KASSERT(tb != NULL);
	testtable_setsize(tb, 500);

	startsem = sem_create("startsem", 0);
	if (startsem == NULL) {
		panic("startsem: sem_create failed\n");
	}
	endsem = sem_create("endsem", 0);
	if (endsem == NULL) {
		panic("endsem: sem_create failed\n");
	}

	for (i=0; i<2; i++) {
	result = thread_fork("tabletest", NULL, aandr, NULL, 257 + i);
		if (result) {
			panic("sem1: thread_fork failed: %s\n",
				  strerror(result));
		}
	}

	for (i=0; i<2; i++) {
		V(startsem);
	}

	for (i=0; i<2; i++) {
		P(endsem);
	}

	KASSERT(testtable_num(tb) == 0);
	testtable_destroy(tb);
	tb = NULL;

	sem_destroy(startsem);
	sem_destroy(endsem);
	startsem = endsem = NULL;

	kprintf("Test done...\n");
	return 0;
}

static 
void
superaandr(void *unused1, unsigned long sect_index) {
	int result;

	(void)unused1;

	P(startsem);

	for (unsigned long i = SECTION_SIZE*sect_index, len = i + SECTION_SIZE; i < len; i++) {
		result = testtable_set(tb, i, NTH(sect_index));
		KASSERT(result == 0);
	}
	for (unsigned long i = SECTION_SIZE*sect_index, len = i + SECTION_SIZE; i < len; i++)
		KASSERT(testtable_get(tb, i) == NTH(sect_index));

	for (unsigned long i = SECTION_SIZE*sect_index, len = i + SECTION_SIZE; i < len; i++) {
		testtable_remove(tb, i);
		KASSERT(testtable_get(tb, i) == NULL);
	}

	V(endsem);
}

int
tabletest3(int nargs, char **args)
{
	unsigned long i;
	int result;


	(void)nargs;
	(void)args;

	kprintf("Beginning max concurrency table test...\n");
	tb = testtable_create();
	KASSERT(tb != NULL);
	testtable_setsize(tb, SECTION_SIZE * NTHREADS);

	startsem = sem_create("startsem", 0);
	if (startsem == NULL) {
		panic("startsem: sem_create failed\n");
	}
	endsem = sem_create("endsem", 0);
	if (endsem == NULL) {
		panic("endsem: sem_create failed\n");
	}

	for (i=0; i<NTHREADS; i++) {
	result = thread_fork("tabletest3", NULL, superaandr, NULL, i);
		if (result) {
			panic("sem1: thread_fork failed: %s\n",
				  strerror(result));
		}
	}

	for (i=0; i<NTHREADS; i++) {
		V(startsem);
	}

	for (i=0; i<NTHREADS; i++) {
		P(endsem);
	}

	KASSERT(testtable_num(tb) == 0);
	testtable_destroy(tb);
	tb = NULL;

	sem_destroy(startsem);
	sem_destroy(endsem);
	startsem = endsem = NULL;

	kprintf("Test done...\n");
	return 0;
}
