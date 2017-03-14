#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
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
	KASSERT(curthread != NULL);

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
	KASSERT(curthread != NULL);

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
	KASSERT(curthread != NULL);

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
	KASSERT(curthread != NULL);

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
