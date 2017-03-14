#include <types.h>
#include <current.h> 
#include <proc.h>
#include <synch.h>
#include <proc_syscall.h>

/*
 * _exit syscall
 */
void
sys__exit(int exitcode) {
	/* Need to kill the thread ... */
	curproc->exit_val = exitcode;
	V(curproc->exit_sem);
	thread_exit();
}
