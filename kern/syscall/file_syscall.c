#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <limits.h>
#include <copyinout.h>
#include <uio.h>
#include <vnode.h>
#include <current.h>
#include <fhandle.h>
#include <proc.h>
#include <file_syscall.h>

/*
 * Open syscall
 */
int
sys_open(const_userptr_t path_ptr, int flags, int32_t *ret)
{
	KASSERT(curproc != NULL);

	struct proc *proc = curproc;
	struct fd *fd;
	int result;
	unsigned num, i, val;
	char *path;

	i = val = 0;

	num = fdarray_num(proc->fds);

	KASSERT(num <= OPEN_MAX);
	
	while (i < num && fdarray_get(proc->fds, i) != NULL)
		i++;

	if (i == num && num == OPEN_MAX) 
		return EMFILE;

	path = kmalloc(sizeof(char) * PATH_MAX);
	result = copyinstr(path_ptr, path, PATH_MAX, NULL);  
	if (result)
		goto end;

	result = fh_add(flags, path, &fd);
	if (result)
		goto end;

	if (i < num) {
		fdarray_set(proc->fds, i, fd);
		*ret = i;
	}
	else {
		result = fdarray_add(proc->fds, fd, &val);
		*ret = val;
	}

end:
	kfree(path);
	return result;
}

/*
 * Close syscall
 */
int
sys_close(int fd)
{
	KASSERT(curproc != NULL);

	struct proc *proc = curproc;
	struct fd *fd_ptr;

	/* Check if valid file descriptor */
	if (fd < 0 || fdarray_num(proc->fds) <= (unsigned)fd) 
		return EBADF;

	fd_ptr = fdarray_get(proc->fds, fd);
	if (fd_ptr == NULL)
		return EBADF;

	fh_dec(fd_ptr);
	fdarray_set(proc->fds, fd, NULL);

	return 0;
}

/*
 * Read syscall
 */
int
sys_read(int fd, userptr_t buffer, size_t buflen, int32_t *ret)
{
	KASSERT(curproc != NULL);

	struct proc *proc = curproc;
	struct fhandle *fh;
	struct fd *fd_ptr;
	struct iovec iov;
	struct uio u;
	int result;

	/* Check if valid file descriptor */
	if (fd < 0 || fdarray_num(proc->fds) <= (unsigned)fd) 
		return EBADF;

	fd_ptr = fdarray_get(proc->fds, fd);

	/* Check read permission */
	if (fd_ptr == NULL || (fh = fd_ptr->fh)->mode == O_WRONLY)
		return EBADF;

	/* Build uio for data transfer */
	iov.iov_ubase = buffer;
	iov.iov_len = buflen;
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen;
	u.uio_offset = fh->offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = proc_getas();

	result = VOP_READ(fh->open_v, &u);
	if (result)
		return result;
	
	/* Update offset */
	spinlock_acquire(&fh->fh_lock);
	fh->offset = u.uio_offset;
	spinlock_release(&fh->fh_lock);
	
	*ret = buflen - u.uio_resid;
	return result;
}

/*
 * Write syscall
 */
int
sys_write(int fd, userptr_t buffer, size_t buflen, int32_t *ret)
{
	KASSERT(curproc != NULL);

	struct proc *proc = curproc;
	struct fhandle *fh;
	struct fd *fd_ptr;
	struct iovec iov;
	struct uio u;
	int result;

	/* Check if valid file descriptor */
	if (fd < 0 || fdarray_num(proc->fds) <= (unsigned)fd) 
		return EBADF;

	fd_ptr = fdarray_get(proc->fds, fd);

	/* Check read permission */
	if (fd_ptr == NULL || (fh = fd_ptr->fh)->mode == O_RDONLY)
		return EBADF;

	/* Build uio for data transfer */
	iov.iov_ubase = buffer;
	iov.iov_len = buflen;
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen;
	u.uio_offset = fh->offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE;
	u.uio_space = proc_getas();

	result = VOP_WRITE(fh->open_v, &u);
	if (result)
		return result;
	
	/* Update offset */
	spinlock_acquire(&fh->fh_lock);
	fh->offset = u.uio_offset;
	spinlock_release(&fh->fh_lock);
	
	*ret = buflen - u.uio_resid;
	return result;
}

/*
 * lseek syscall
 */
int 
sys_lseek(int fd, uint32_t u_off, uint32_t l_off, userptr_t whence_ptr, int32_t *ret1, int32_t *ret2)
{
	KASSERT(curproc != NULL);

	struct proc *proc = curproc;
	struct fd *fd_ptr;
	struct fhandle *fh_ptr;
	struct stat statbuf;
	int result;
	int32_t whence; 
	off_t pos;

	/* Check if valid file descriptor */
	if (fd < 0 || fdarray_num(proc->fds) <= (unsigned)fd) 
		return EBADF;

	fd_ptr = fdarray_get(proc->fds, fd);
	if (fd_ptr == NULL)
		return EBADF;

	fh_ptr = fd_ptr->fh;

	if (!VOP_ISSEEKABLE(fh_ptr->open_v))
		return ESPIPE;

	result = copyin(whence_ptr, &whence, sizeof(int32_t));
	if (result)
		return result;
	
	/* set up pos */
	pos = 0;
	pos |= u_off;
	pos <<= 32;
	pos |= l_off;
	
	if (whence == SEEK_SET && pos >= 0) {
		spinlock_acquire(&fh_ptr->fh_lock);
		fh_ptr->offset = pos;
		spinlock_release(&fh_ptr->fh_lock);
	}
	else if (whence == SEEK_CUR && (fh_ptr->offset + pos) >= 0) {
		spinlock_acquire(&fh_ptr->fh_lock);
		fh_ptr->offset += pos;
		spinlock_release(&fh_ptr->fh_lock);
	}
	else if (whence == SEEK_END) {
		result = VOP_STAT(fh_ptr->open_v, &statbuf);
		if (result)
			return result;
		if ((statbuf.st_size + pos) < 0)
			return EINVAL;
		spinlock_acquire(&fh_ptr->fh_lock);
		fh_ptr->offset = statbuf.st_size + pos;
		spinlock_release(&fh_ptr->fh_lock);
	}
	else {
		return EINVAL;
	}
	*ret1 = (int32_t)((int64_t)fh_ptr->offset >> 32);
	*ret2 = (int32_t)(((int64_t)fh_ptr->offset << 32) >> 32);

	return 0;
}

/*
 * dup2 syscall
 */
int 
sys_dup2(int oldfd, int newfd)
{
	KASSERT(curproc != NULL);

	struct proc *proc = curproc;
	struct fd *oldfd_ptr, *newfd_ptr;
	unsigned num, newnum;
	int result;

	num = fdarray_num(proc->fds);

	/* Check if valid file descriptor */
	if (oldfd < 0                   || 
		newfd < 0                   || 
		num <= (unsigned)oldfd      || 
		(unsigned)newfd >= OPEN_MAX) 
		return EBADF;

	if (oldfd == newfd)
		return 0;

	oldfd_ptr = fdarray_get(proc->fds, oldfd);
	if (oldfd_ptr == NULL)
		return EBADF;

	if ((unsigned)newfd >= num) {
		result = fdarray_setsize(proc->fds, newfd + 1);
		if (result)
			return result;
		goto skip;
	}

	newfd_ptr = fdarray_get(proc->fds, newfd);
	if (newfd_ptr != NULL)
		fh_dec(newfd_ptr);

skip:
	/* Null any new spaces in fdarray */
	newnum = fdarray_num(proc->fds);
	for (unsigned i = num; i < newnum-1; i++)
		fdarray_set(proc->fds, newfd, NULL);
	/* Insert copy of oldfd */
	fdarray_set(proc->fds, newfd, oldfd_ptr);
	/* Increment refcount of file handle */
	fh_inc(oldfd_ptr);

	return 0;
}
