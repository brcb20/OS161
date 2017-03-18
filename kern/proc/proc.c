/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */
#ifndef PROCINLINE
#define PROCINLINE INLINE
#endif

#define FDINLINE
#define CPINLINE

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <table.h>
#include <limits.h>
#include <fhandle.h>
#include <synch.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * The process table; this holds all user level processes
 */
DECLTABLE(proc, PROCINLINE);
DEFTABLE(proc, PROCINLINE);
static struct proctable *ptb;

static unsigned volatile proc_num;
static unsigned long volatile pid_ref;
static struct spinlock proc_spinlock;

/* 
 * Add process to the proc table
 */
int
proc_setpid(struct proc *proc)
{
	unsigned long tmp_pid;
	int result;

	KASSERT(ptb != NULL);
	KASSERT(proc != NULL);

	/* Suppress warning */
	tmp_pid = 0;

	spinlock_acquire(&proc_spinlock);
	if (proc_num >= PROC_MAX) {
		spinlock_release(&proc_spinlock);
		return EMPROC;
	}
	++proc_num;
	if (pid_ref == PID_MAX + 1)
		pid_ref = PID_MIN;
	spinlock_release(&proc_spinlock);

rewind:
	result = proctable_setfirst(ptb, proc, pid_ref, &tmp_pid);

	if (result == 0) {
		proc->pid = (pid_t)tmp_pid;
		spinlock_acquire(&proc_spinlock);
		if (tmp_pid >= pid_ref)
			pid_ref = tmp_pid + 1;
		spinlock_release(&proc_spinlock);
	}
	else {
		spinlock_acquire(&proc_spinlock);
		if (pid_ref != PID_MIN) {
			pid_ref = PID_MIN;
			spinlock_release(&proc_spinlock);
			goto rewind;
		}
		--proc_num;
		spinlock_release(&proc_spinlock);
	}

	return result;
}

/*
 * Create a proc structure.
 */
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}
	/* Handle exit */
	proc->exit_sem = sem_create("exit sem", 0);
	if (proc->exit_sem == NULL) {
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	/* Child process array */
	proc->cps = cparray_create();
	if (proc->cps == NULL) {
		sem_destroy(proc->exit_sem);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	/* file descriptor array */
	proc->fds = fdarray_create();
	if (proc->fds == NULL) {
		cparray_destroy(proc->cps);
		sem_destroy(proc->exit_sem);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	/* Exit values */
	proc->exited = false;
	proc->exit_val = 0;

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* PID will be set separately */

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	return proc;
}

/*
 * Exit a process
 *
 * Cleans up process structure but leaves bare bones for
 * parent process to get exit value
 */
void
proc_exit(struct proc *proc)
{
	/*
	 * This should only be called by the last process thread
	 * when it exits
	 */
	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	int index;
	struct fd *fd;


	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	/*
	 * Cleanup file descriptors
	 */
	while ((index = fdarray_num(proc->fds)) != 0) {
		fd = fdarray_get(proc->fds, index - 1);
		if (fd != NULL)
			fh_dec(fd);
		fdarray_remove(proc->fds, index - 1);
	}
	fdarray_destroy(proc->fds);
	
	/*
	 * Cleanup child process array
	 */
	while ((index = cparray_num(proc->cps)) != 0) {
		cparray_remove(proc->cps, index - 1);
	}
	cparray_destroy(proc->cps);

	/* Let the parent process collect exit status */
	proc->exited = true;
	V(proc->exit_sem);

	KASSERT(proc->p_numthreads == 0);
}




/*
 * Destroy a proc structure.
 *
 * Note: Called by the parent to clean up
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);
	/* Ensure proc_exit has been called */
	KASSERT(proc->p_addrspace == NULL);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	if (proc->pid >= PID_MIN  &&
		proc->pid <= PID_MAX  && 
		proctable_get(ptb, (unsigned long)proc->pid) == proc) {
		proctable_remove(ptb, (unsigned long)proc->pid);
		spinlock_acquire(&proc_spinlock);
		--proc_num;
		spinlock_release(&proc_spinlock);
	}

	sem_destroy(proc->exit_sem);
	spinlock_cleanup(&proc->p_lock);
	kfree(proc->p_name);
	kfree(proc);
}

/* Create process table */
void
proctable_bootstrap(void)
{
	ptb = proctable_create();
	if (ptb == NULL) {
		panic("proctable_create for process table failed\n");
	}
	/* Extra initialization for proc table */
	proctable_setsize(ptb, PID_MAX + 1);
	pid_ref = PID_MIN;
	proc_num = 0;
	spinlock_init(&proc_spinlock);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	/* Kernel process */
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 * It will be issued a pid and it's parent pid will be set, if 
 * kproc is parent then ppid is set to 0.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	int result;
	unsigned index;
	struct proc *newproc;
	struct fd *fd;
	char *path;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* TODO may move outside to become part of
	 * fork  so you can return the correct error 
	 * values to errno 
	 */
	/* pid */
	if (proc_setpid(newproc)) {
		proc_destroy(newproc);
		return NULL;
	}
	/* ppid */
	if (curproc == kproc)
		newproc->ppid = 0;
	else
		newproc->ppid = curproc->pid;

	/*
	 * TODO might be able to use fork instead of this
	 */
	/* Stds */
	if (newproc->ppid == 0) {
		path = kmalloc(sizeof(char)*5);
		if (path == NULL) {
			proc_destroy(newproc);
			return NULL;
		}
		strcpy(path, "con:");
		result = fh_add(O_RDONLY, path, &fd);
		if (result) {
			proc_destroy(newproc);
			kfree(path);
			return NULL;
		}
		fdarray_add(newproc->fds, fd, &index);
		for (int i = 0; i < 2; i++) {
			strcpy(path, "con:");
			result = fh_add(O_WRONLY, path, &fd);
			if (result) {
				while (fdarray_num(newproc->fds) != 0) {
					fh_dec(fdarray_get(newproc->fds, 0));
					fdarray_remove(newproc->fds, 0);
				}
				proc_destroy(newproc);
				kfree(path);
				return NULL;
			}
			fdarray_add(newproc->fds, fd, &index);
		}	
		kfree(path);
	}
	
	/* VM fields */
	newproc->p_addrspace = NULL;

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

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);

	/* Need to remove process if no more threads but leave bare bones */
	if (proc->p_numthreads == 0) 
		proc_exit(proc);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
