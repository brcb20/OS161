#include <types.h>
#include <lib.h>
#include <limits.h>
#include <thread.h>
#include <proc.h>
#include <synch.h>
#include <test.h>

#define NTHREADS 50

static struct semaphore *startsem = NULL;
static struct semaphore *blocksem = NULL;
static struct semaphore *endsem = NULL;

int 
proctest(int nargs, char **args)
{
	struct proc **proc;
	unsigned long i;
	unsigned j,
			 rem = (PID_MAX + 1 - PID_MIN)%PROC_MAX,
			 loops = (PID_MAX + 1 - PID_MIN - rem)/PROC_MAX;

	(void)nargs;
	(void)args;

	proc = kmalloc(sizeof(*proc)*PROC_MAX);

	kprintf("Beginning process table testing...\n");
	for (j = 0; j < loops; j++) { 
		kprintf("Start of loop %u...\n", j);
		for (i = j*PROC_MAX; i < (j+1)*PROC_MAX; i++) {
			proc[i%PROC_MAX] = proc_create_runprogram("process");
			KASSERT(proc != NULL);
			KASSERT((unsigned)proc[i%PROC_MAX]->pid == i+PID_MIN);
		}
		for (i = 0; i < PROC_MAX; i++) {
			proc_destroy(proc[i]);
			proc[i] = NULL;
		}
	}
	kprintf("Testing circular pid implementation\n");
	for (i = 0; i < rem; i++) {
		proc[i] = proc_create_runprogram("process");
		KASSERT(proc != NULL);
		KASSERT((unsigned)proc[i]->pid == j*PROC_MAX + i + PID_MIN);
	}
	for (i = rem; i < PROC_MAX; i++) {
		proc[i] = proc_create_runprogram("process");
		KASSERT(proc != NULL);
		KASSERT((unsigned)proc[i]->pid == PID_MIN + i - rem);
	}

	/* process table should be full */
	KASSERT(proc_create_runprogram("process") == NULL);

	for (i = 0; i < PROC_MAX; i++) {
		proc_destroy(proc[i]);
		proc[i] = NULL;
	}

	kfree(proc);
	kprintf("Done\n");
	return 0;
}

static
void
proc_create_recursive(void *procarray, unsigned long index)
{
	struct proc **testproc = (struct proc **)procarray;
	unsigned i = 0,
			 num = (unsigned)index;

	kprintf_n("Thread %u starting\n", num);
	if (index == NTHREADS-1) { V(blocksem); }
	P(startsem);

	while ((testproc[i] = proc_create_runprogram("process")) != NULL) {
		++i;
		random_yielder(4);
	}

	kprintf("Thread %u ending\n", num);
	V(endsem);
}

int
proctest2(int nargs, char **args)
{
	unsigned i, j, k, l;
	int result;
	struct proc ***testprocs;

	(void)nargs;
	(void)args;

	kprintf_n("Beginning concurrency proc table test...\n");
	testprocs = kmalloc(sizeof(struct proc **) * NTHREADS);
	KASSERT(testprocs != NULL);

	startsem = sem_create("startsem", 0);
	if (startsem == NULL) {
		panic("startsem: sem_create failed\n");
	}
	blocksem = sem_create("blocksem", 0);
	if (blocksem == NULL) {
		panic("blocksem: sem_create failed\n");
	}
	endsem = sem_create("endsem", 0);
	if (endsem == NULL) {
		panic("endsem: sem_create failed\n");
	}

	for (i=0; i<NTHREADS; i++) {
	testprocs[i] = kmalloc(sizeof(struct proc *) * PROC_MAX);
	KASSERT(testprocs[i] != NULL);
	result = thread_fork("proctest2", NULL, proc_create_recursive, (void *)testprocs[i], i);
		if (result) {
			panic("thread_fork failed: %s\n",
				  strerror(result));
		}
	}

	P(blocksem);

	for (i=0; i<NTHREADS; i++) {
		V(startsem);
	}

	for (i=0; i<NTHREADS; i++) {
		P(endsem);
	}

	/* Check no pid collision */
	kprintf("Checking for pid collision\n");
	for (i=0; i<NTHREADS; i++) {
		for (j=0; i<NTHREADS; i++) {
			k = 0;
			if (i != j) {
				while (testprocs[i][k] != NULL) {
					l = 0;
					while (testprocs[j][l] != NULL) {
						KASSERT(testprocs[i][k]->pid != testprocs[j][l]->pid);
						++l;
					}
					++k;
				}
				kprintf("Thead %u has %u unique procs\n", i, k);
			}
		}
	}

	for (i=0; i<NTHREADS; i++) {
		for (k=0; k < PROC_MAX; k++) {
			if (testprocs[i][k] != NULL) {
				proc_destroy(testprocs[i][k]);
			}
			testprocs[i][k] = NULL;
		}
	}

	for (i=0; i<NTHREADS; i++) {
		kfree(testprocs[i]);
	}
	kfree(testprocs);

	sem_destroy(startsem);
	sem_destroy(blocksem);
	sem_destroy(endsem);
	startsem = blocksem = endsem = NULL;

	kprintf("Test done...\n");
	return 0;
}
