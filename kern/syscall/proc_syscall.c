#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <limits.h>
#include <current.h> 
#include <proc.h>
#include <fhandle.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <copyinout.h>
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
	memcpy(h_tf, c_tf, sizeof(*h_tf));

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
	proc->exit_val = _MKWAIT_EXIT(exitcode);
	V(proc->exit_sem);
	thread_exit();
}

/*
 * waitpid 
 */
int
sys_waitpid(pid_t pid, userptr_t status, int options, int32_t *ret)
{
	KASSERT(curproc != NULL);

	struct proc *childproc;
	unsigned num, i;
	int result, exit_val;
	size_t stoplen;
	
	/* Check options */
	if (options != 0)
		return EINVAL;

	/* Check pid is within std range */
	if (pid < PID_MIN || pid > PID_MAX)
		return ESRCH;

	/* Check status pointer is valid */
	if (status != NULL) {
		result = copycheck(status, sizeof(int), &stoplen);
		if (result)
			return result;

		if (stoplen != sizeof(int))
			return EFAULT;
	}

	/* Check pid is child proc */
	num = cparray_num(curproc->cps);
	i = 0;
	while (i < num && pid != (childproc = cparray_get(curproc->cps, i))->pid)
		++i;

	if (i == num)
		return ECHILD;

	if (childproc->exited)
		goto exited;
				
	/* Wait for child proc to exit */
	P(childproc->exit_sem);

exited:
	if (WIFEXITED(childproc->exit_val))
		exit_val = WEXITSTATUS(childproc->exit_val);
	else if (WIFSIGNALED(childproc->exit_val))
		exit_val = WTERMSIG(childproc->exit_val);
	else /* STOPPED */
		exit_val = WSTOPSIG(childproc->exit_val);

	copyout(&exit_val, status, sizeof(int)); /* shoudn't fail */

	*ret = childproc->pid;

	proc_destroy(childproc);
	cparray_remove(curproc->cps, i); 

	return 0;
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
