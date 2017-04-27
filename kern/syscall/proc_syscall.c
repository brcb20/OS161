#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
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
#include <vfs.h>
#include <syscall.h>
#include <proc_syscall.h>

static char k_buffer[ARG_MAX];
static struct lock *kb_lock;

void 
sys_bootstrap()
{
	kb_lock = lock_create("Kernel buffer lock");
	if (kb_lock == NULL)
		panic("Kernel buffer lock failed\n");
}

/*
 * fork syscall
 */
int
sys_fork(struct trapframe *c_tf, int32_t *ret)
{
	KASSERT(curproc != NULL);
	KASSERT(curproc != kproc);

	struct trapframe *h_tf;
	struct proc *newproc,
				*proc = curproc;
	struct fd *fd;
	int result;
	unsigned num, index, i;

	/* silence warning */
	index = 0;

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
		goto fail;

	/* PPID */
	newproc->ppid = proc->pid;

	/* VM fields */
	result = as_copy(proc->p_addrspace, &newproc->p_addrspace); 
	if (result)
		goto fail;

	lock_acquire(proc->p_mainlock);
	num = fdarray_num(proc->fds);
	for (i = 0; i < num; i++) {
		fd = fdarray_get(proc->fds, i);
		result = fdarray_add(newproc->fds, fd, &index);
		if (result) { 
			lock_release(proc->p_mainlock);
			goto fail;
		}
		KASSERT(i == index);
		if (fd != NULL)
			fh_inc(fd);
	}
	lock_release(proc->p_mainlock);

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&proc->p_lock);
	if (proc->p_cwd != NULL) {
		VOP_INCREF(proc->p_cwd);
		newproc->p_cwd = proc->p_cwd;
	}
	spinlock_release(&proc->p_lock);
	/* End of child process setup */

	/* Add child process to array */
	result = cparray_add(proc->cps, newproc, &index);
	if (result) 
		goto fail;

	/* Fork the thread */
	result = thread_fork("Forked child thread", newproc, enter_forked_process, (void *)h_tf, 0);
	if (result) {
		cparray_remove(proc->cps, cparray_num(proc->cps) - 1);
		goto fail;
	}

	*ret = newproc->pid;
	return result;

fail:
	KASSERT(newproc->p_numthreads == 0);
	proc_exit(newproc);
	proc_destroy(newproc);
	kfree(h_tf);
	return result;
}

/*
 * Helper function for the execv syscall
 *
 * Copies args from the old address space to the stack of the
 * new address space. 
 */
static
int
copyargv(struct addrspace *old_as, struct addrspace *new_as, userptr_t old_args, int *argc, vaddr_t *sp_ptr) 
{
	KASSERT(old_as != NULL);
	KASSERT(new_as != NULL);
	KASSERT(old_args != NULL);
	KASSERT(sp_ptr != NULL);
	KASSERT(*sp_ptr != 0);
	KASSERT(proc_getas() == old_as || proc_getas() == new_as);
	KASSERT(kb_lock != NULL);

	typedef struct arg {
		size_t len;
		struct arg *next;
	} argv_t;

	int result;
	unsigned i, count;
	argv_t *head, *tail;
	size_t actual, space;
	char *arg_ptr,
		 **old_argv = (char **)old_args; /* Warning: userspace pointer */
	userptr_t tmp_ptr,
			  stackptr = (userptr_t)*sp_ptr;

	i = count = 0;
	space = 0;

	if (proc_getas() == new_as) {
		proc_setas(old_as);
		as_activate();
	}

	/* Setup linked list of new argv */
	head = kmalloc(sizeof(argv_t));
	if (head == NULL) {
		return ENOMEM;
	}
	tail = head;
	tail->next = NULL;

	lock_acquire(kb_lock);
	/* Loop through args */
	while (true) {
		/* Get pointer to arg */
		result = copyin((userptr_t)(old_argv +i), &arg_ptr, sizeof(arg_ptr));
		if (result) { goto fail; }
		if (arg_ptr == NULL) { goto success; } 
		/* Get argument */
		result = copyinstr((userptr_t)arg_ptr, k_buffer + space, ARG_MAX - space, &actual);
		if (result) { 
			if (result == ENAMETOOLONG)
				result = E2BIG;
			goto fail; 
		}

		/* Add argument pointer to linked listt */
		tail->len = space;
		tail->next = kmalloc(sizeof(*tail));
		if (tail->next == NULL) { goto fail; }
		tail = tail->next;
		tail->len = 0;
		tail->next = NULL;

		++count;
		/* Check if ARG_MAX reached */
		space += ((actual - actual%4) + 4); // assumes pointers are 4 bytes
		if (space + (count*4) > ARG_MAX) {
			result = E2BIG;
			goto fail;
		}

		++i;
	}

success:
	if (proc_getas() == old_as) {
		proc_setas(new_as);
		as_activate();
	}
	/* Copyout entire buffer */
	stackptr -= space;
	result = copyout(k_buffer, stackptr, space);
	KASSERT(result == 0);
	lock_release(kb_lock);

	tail = head;
	for (size_t i = 0; i < count; i++) {
		tmp_ptr = stackptr + tail->len;
		result = copyout(&tmp_ptr, stackptr - (count+1-i)*4, 4);
		KASSERT(result == 0);
		tail = head->next;
		kfree(head);
		head = tail;
	}
	KASSERT(head->next == NULL);
	kfree(head);

	*argc = (int)count;
	*sp_ptr = (vaddr_t)(stackptr - (count+1)*4);
	return 0;

fail:
	lock_release(kb_lock);
	while (head != NULL) {
		tail = head->next;	
		kfree(head);
		head = tail;
	}
	return result;
}

/*
 * Execv syscall
 */
int
sys_execv(const_userptr_t progname, userptr_t args)
{
	struct addrspace *old_as, *new_as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result, argc;
	char *tmp_b;
	size_t tmp_size = 256; /* Must always be a power of 2 */

	if (args == NULL)
		return EFAULT;

	tmp_b = kmalloc(tmp_size);
	if (tmp_b == NULL)
		return ENOMEM;

	while ((result = copyinstr(progname, tmp_b, tmp_size, NULL)) != 0) {
		if (result == ENAMETOOLONG) {
			kfree(tmp_b);
			if (tmp_size == PATH_MAX)
				return result;
			tmp_size *=2;
			tmp_b = kmalloc(tmp_size);
			if (tmp_b == NULL)
				return ENOMEM;
		}
		else {
			kfree(tmp_b);
			return result;
		}
	}

	/* Open the file. */
	result = vfs_open(tmp_b, O_RDONLY, 0, &v);
	if (result) {
		kfree(tmp_b);
		return result;
	}

	kfree(tmp_b);

	/* Create new address space */
	new_as = as_create();
	if (new_as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Replace old as with new one */
	old_as = proc_setas(new_as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		vfs_close(v);
		goto fail;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(new_as, &stackptr);
	if (result)
		goto fail;

	/* Copy argv to new as */
	result = copyargv(old_as, new_as, args, &argc, &stackptr);
	if (result)
		goto fail;
	
	as_destroy(old_as);

	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, (userptr_t)stackptr/*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* Should never get here */

fail:
	if (proc_getas() == new_as) {
		proc_setas(old_as);
		as_activate();
	}
	as_destroy(new_as);
	return result;
}

/*
 * _exit syscall
 */
void
sys__exit(int exitcode) {
	KASSERT(curproc != NULL);

	struct proc *proc = curproc;

	proc->exit_val = _MKWAIT_EXIT(exitcode);
	thread_exit();
}

/*
 * waitpid 
 */
int
sys_waitpid(pid_t pid, userptr_t status, int options, int32_t *ret)
{
	KASSERT(curproc != NULL);

	struct proc *childproc,
				*proc = curproc;
	unsigned num, i;
	int result, exit_val;

	i = exit_val = 0;
	
	/* Check options */
	if (options != 0)
		return EINVAL;

	/* Check pid is within std range */
	if (pid < PID_MIN || pid > PID_MAX)
		return ESRCH;

	/* Check status pointer is valid */
	if (status != NULL) {
		result = copyout(&exit_val, status, sizeof(int));
		if (result)
			return result;
	}

	/* Check pid is child proc */
	num = cparray_num(proc->cps);
	while (i < num && pid != (childproc = cparray_get(proc->cps, i))->pid)
		++i;

	if (i == num)
		return ECHILD;

	/* Wait for child proc to exit */
	P(childproc->exit_sem);

	exit_val = childproc->exit_val;

	if (status != NULL)
		copyout(&exit_val, status, sizeof(int)); /* shoudn't fail */

	*ret = childproc->pid;

	proc_destroy(childproc);
	cparray_remove(proc->cps, i); 

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
