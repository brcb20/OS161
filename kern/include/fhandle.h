#ifndef _FHANDLE_H_
#define _FHANDLE_H_

#include <spinlock.h>

struct vnode;
struct lock;

/*
 * File handler structure
 */
struct fhandle {
	struct spinlock 	ref_lock; 	/* refcount lock */
	unsigned volatile 	refcount; 	/* Number of connections to this handler */
	off_t volatile  	  offset;	/* offset in file */
	int                     mode;	/* file handle mode */
	struct vnode 		 *open_v;   /* vnode being handled */
	struct lock 	    *fh_lock;   /* Lock for this structure */
};

/* File descriptor structure */
struct fd {
	unsigned long 		index;
	struct fhandle        *fh;
};

/* Bootstrap Open file table */
void oft_bootstrap(void);
/* Add a file handle to the Open file table */
int fh_add(int openflags, char *path, struct fd **);
/* Increment reference count */
void fh_inc(struct fd *);
/* Decrement reference count of file handle */
void fh_dec(struct fd *);

#endif /* _FHANDLE_H_ */
