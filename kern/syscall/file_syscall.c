#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <uio.h>
#include <vnode.h>
#include <current.h>
#include <fhandle.h>
#include <proc.h>
#include <file_syscall.h>

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

	spinlock_acquire(&proc->p_lock);
	fd_ptr = fdarray_get(proc->fds, fd);
	spinlock_release(&proc->p_lock);

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

	spinlock_acquire(&proc->p_lock);
	fd_ptr = fdarray_get(proc->fds, fd);
	spinlock_release(&proc->p_lock);

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
