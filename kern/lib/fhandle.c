#ifndef FHANDLEINLINE
#define FHANDLEINLINE INLINE
#endif

#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <vfs.h>
#include <synch.h>
#include <table.h>
#include <fhandle.h>

#define OFT_SIZE OPEN_FILE_MAX/(sizeof(struct fhandle) + sizeof(struct fhandle *))


/*
 * The Open File Table; this holds all file handles
 */
DECLTABLE(fhandle, FHANDLEINLINE);
DEFTABLE(fhandle, FHANDLEINLINE);
static struct fhandletable *fht;

void
oft_bootstrap(void)
{
	fht = fhandletable_create();
	if (fht == NULL)
		panic("Unable to create open file table\n");
	fhandletable_setsize(fht, OFT_SIZE);
}

int
fh_add(int openflags, char *path, struct fd **ret)
{
	KASSERT(fht != NULL);
	KASSERT(path != NULL);
	KASSERT(ret != NULL);

	int result;
	unsigned long index;
	struct fd *fd;
	struct fhandle *fh;
	struct vnode *vn;

	/* Suppress warning */
	index = 0;

	fd = kmalloc(sizeof(*fd));
	if (fd == NULL)
		return ENOMEM;

	fh = kmalloc(sizeof(*fh));
	if (fh == NULL) {
		kfree(fd);
		return ENOMEM;
	}

	fh->fh_lock = lock_create("fh lock");
	if (fh->fh_lock == NULL) {
		kfree(fd);
		kfree(fh);
		return ENOMEM;
	}

	result = vfs_open(path, openflags, 0, &vn);
	if (result) {
		kfree(fd);
		kfree(fh);
		return result;
	}

	result = fhandletable_setfirst(fht, fh, 0, &index);
	if (result) {
		kfree(fd);
		kfree(fh);
		vfs_close(vn);
		return ENFILE;
	}
	fd->index = index;

	/* Set up file handle */
	fh->open_v = vn;
	fh->mode = openflags & O_ACCMODE;
	spinlock_init(&fh->ref_lock);
	fh->refcount = 1;
	fh->offset = 0;

	fd->fh = fh;
	*ret = fd;

	return result;
}

void
fh_inc(struct fd *fd) 
{
	KASSERT(fd->fh->refcount != 0);

	spinlock_acquire(&fd->fh->ref_lock);
	++fd->fh->refcount;
	spinlock_release(&fd->fh->ref_lock);
}

void 
fh_dec(struct fd *fd)
{
	KASSERT(fd->fh->refcount > 0);

	struct fhandle *fh;

	fh = fd->fh;

	spinlock_acquire(&fh->ref_lock);
	--fh->refcount;
	if (fh->refcount == 0) {
		spinlock_release(&fh->ref_lock);
		fhandletable_remove(fht, fd->index);
		lock_destroy(fh->fh_lock);
		vfs_close(fh->open_v);
		spinlock_cleanup(&fh->ref_lock);
		kfree(fh);
		kfree(fd);
	}
	else {
		spinlock_release(&fh->ref_lock);
	}
}
