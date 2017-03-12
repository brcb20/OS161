#ifndef _SECTION_H_
#define _SECTION_H_

#include <cdefs.h>
#include <spinlock.h>
#include <array.h>

/* Don't make smaller than 256 */
#define SECTION_SIZE 256
/* For testing */
#define SECTION_CHECKED

#ifdef SECTION_CHECKED 
#define SECTIONASSERT KASSERT
#else
#define SECTIONASSERT(x) ((void)(x))
#endif

/*
 * Base section type useful for building tables
 * A section allocates 1024 bytes worth of void pointers
 *
 * create - allocate a section
 * destroy - destroy an allocated section (must be empty)
 * init - initialize a section in space externally available
 * cleanup - clean up a section in space externally available 
 * num - return number of elements in section
 * get - return element no INDEX
 * set - set element no. INDEX (cannot be set to null - use remove for that)
 * setfirst - set the first empty element including START upto excluding END
 * add - set the first empty element in section to VAL; returns index or -1 if failed
 * remove - deletes entry INDEX (nulls value at INDEX and decreases counter)
 */

/*
 * Section structure
 */
struct section {
	void 				   **start;  /* start of section */
	unsigned                   max;  /* Size of section */ 
	unsigned volatile 		   num;  /* num of elements in section */ 
	struct spinlock 	  num_lock;  /* Protect num from concurrent access during add or remove */
};

struct section *section_create(void);
void section_destroy(struct section *);
int section_init(struct section *);
void section_cleanup(struct section *);
unsigned section_num(const struct section *);
void *section_get(const struct section *, unsigned index);
void section_set(struct section *, unsigned index, void *val);
int section_setfirst(struct section *, void *val, unsigned start, unsigned end);
int section_add(struct section *, void *val);
void section_remove(struct section *, unsigned index);

#endif /* _SECTION_H_ */
