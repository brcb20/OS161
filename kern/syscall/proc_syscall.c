#include <types.h>
#include <kern/errno.h>
#include <current.h> 
#include <proc.h>
#include <fhandle.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <addrspace.h>
#include <vnode.h>
#include <syscall.h>
#include <proc_syscall.h>

/*
 * fork syscall
 */
int
sys_fork(struct trapframe *c_tf, int32_t *ret)
{
	KASSERT(curproc != NULL);
	KASSERT(curproc != kproc);

	struct trapframe *h_tf;
	struct proc *newproc;
	struct fd *fd;
	int result;
	unsigned num, index, i;

	h_tf = kmalloc(sizeof(*h_tf));
	if (h_tf == NULL)
		return ENOMEM;

	/* Push trapframe to heap from current stack */
	for (i = 0; i < sizeof(*h_tf); i++)
		*((char *)h_tf + i) = *((char *)c_tf + i);

	/*
	 * New process setup 
	 */

	/* Create a new proc */
	newproc = proc_create("Forked process");
	if (newproc == NULL) {
		kfree(h_tf);
		return ENOMEM;
	}

	/* PID */
	result = proc_setpid(newproc);
	if (result)
		goto fail2;

	/* PPID */
	newproc->ppid = curproc->pid;

	/* VM fields */
	result = as_copy(curproc->p_addrspace, &newproc->p_addrspace); 
	if (result)
		goto fail2;

	/* File descriptor table
	 * TODO You might need to protect this from other threads
	 * if you choose to implement multi threaded processes
	 */
	num = fdarray_num(curproc->fds);
	for (i = 0; i < num; i++) {
		fd = fdarray_get(curproc->fds, i);
		result = fdarray_add(newproc->fds, fd, &index);
		if (result) { goto fail1; }
		fh_inc(fd);
	}

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);
	/* End of child process setup */

	/* Add child process to array */
	result = cparray_add(curproc->cps, newproc, &index);
	if (result) 
		goto fail1;

	/* Fork the thread */
	result = thread_fork("Forked child thread", newproc, enter_forked_process, (void *)h_tf, 0);
	if (result) {
		cparray_remove(curproc->cps, cparray_num(curproc->cps) - 1);
		goto fail1;
	}

	*ret = newproc->pid;
	return result;

fail1:
	proc_exit(newproc);

fail2:
	proc_destroy(newproc);
	kfree(h_tf);
	return result;
}

/*
 * _exit syscall
 */
void
sys__exit(int exitcode) {
	KASSERT(curproc != NULL);

	struct proc *proc = curproc;

	proc->exited = true;
	proc->exit_val = exitcode;
	V(proc->exit_sem);
	thread_exit();
}

/* 
 * getpid
 */
void
sys_getpid(int32_t *ret)
{
	KASSERT(curproc != NULL);

	*ret = curproc->pid;
}
