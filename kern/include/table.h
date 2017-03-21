#ifndef _TABLE_H_
#define _TABLE_H_

#include <cdefs.h>
#include <kern/errno.h>
#include <spinlock.h>
#include <synch.h>
#include <lib.h>
#include <section.h>

/* For testing */
#define TABLES_CHECKED

#ifdef TABLES_CHECKED
#define TABLEASSERT KASSERT
#else
#define TABLEASSERT(x) ((void)(x))
#endif

#ifndef TABLEINLINE
#define TABLEINLINE INLINE
#endif

#ifndef CONTAINERINLINE
#define CONTAINERINLINE INLINE
#endif

/*
 * Base table type and operations.
 * Only use if you have a lot to store, each section allocates
 * 1024 bytes a pop. 
 *
 * create - allocate a table
 * destroy - destroy an allocated table
 * init - initialize a table in space externally allocated and set max capacity
 * cleanup - clean up a table in space externally allocated 
 * num - return number of elements in table
 * get - return element no. INDEX
 * set - set element no. INDEX (cannot be NULL - use remove for that); May fail and return error; 
 * setfirst - set first free element in table; May fail and return error;
 * setsize - change size to NUM elements; May fail and return error;
 * add - append VAL to end of table; return its index in INDEX_RET if
 *       INDEX_RET isn't null; may fail and return error.
 * remove - deletes entry INDEX (warning: simply nulls INDEX and 
 *          decreases element counter
 * 
 * Note there is no need for preallocating because the table allocates 
 * blocks of SECTION_SIZE and automagically removes blocks not in use. 
 *
 * Synchronization: get, set, setfirst,  remove are thread safe
 * NOTE: Add is not thread safe
 */

/* container for section */
struct container {
	struct rwlock *section_lock;
	struct section *section;
};

DECLARRAY(container, CONTAINERINLINE);
DEFARRAY(container, CONTAINERINLINE);

/* Table structure */
struct table {
	struct containerarray  *containers; // Section container 
	struct lock        *container_lock;
	unsigned long                  max; // Table size (max num of elements)
	unsigned long volatile	       num; // Number of elements in table
	struct spinlock         table_lock;
};


struct table *table_create(void);
void table_destroy(struct table *);
int table_init(struct table *);
void table_cleanup(struct table *);
TABLEINLINE unsigned long table_num(const struct table *);
TABLEINLINE void *table_get(const struct table *, unsigned long index);
TABLEINLINE int table_set(struct table *, unsigned long index, void *val);
TABLEINLINE int table_setfirst(struct table *, void *val, unsigned long start, unsigned long *index_ret);
void table_setsize(struct table *, unsigned long num);
TABLEINLINE int table_add(struct table *, void *val, unsigned long *index_ret);
TABLEINLINE void table_remove(struct table *, unsigned long index);

/*
 * Inlining for base operations
 */

TABLEINLINE unsigned long
table_num(const struct table *tb)
{
	return tb->num;
}

TABLEINLINE void *
table_get(const struct table *tb, unsigned long index)
{
	TABLEASSERT(tb != NULL);
	TABLEASSERT(index < tb->max);
	struct container *container;
	void *result;
	unsigned rem = index % SECTION_SIZE;
	unsigned sect_index = (index - rem)/SECTION_SIZE;

	if ((containerarray_num(tb->containers) <= sect_index) ||
	    (container = containerarray_get(tb->containers, sect_index)) == NULL) {
		return NULL;
	}
	rwlock_acquire_read(container->section_lock);
	if (container->section == NULL) {
		rwlock_release_read(container->section_lock);
		return NULL;
	}
	result = section_get(container->section, rem);
	rwlock_release_read(container->section_lock);

	return result;
}

TABLEINLINE int 
table_set(struct table *tb, unsigned long index, void *val)
{
	TABLEASSERT(tb != NULL);
	TABLEASSERT(index < tb->max);
	TABLEASSERT(val != NULL);

	bool newadd;
	int tmp;
	unsigned i, container_num, container_index,
			 rem = index % SECTION_SIZE,
			 sect_index = (index - rem)/SECTION_SIZE;
	struct container *container = NULL;

	/* Suppress warning */
	container_index = 0;

	if (!lock_do_i_hold(tb->container_lock))
		lock_acquire(tb->container_lock);
	container_num = containerarray_num(tb->containers);

	for (i = container_num; i <= sect_index; i++) {
		container = kmalloc(sizeof(*container));
		/*
		 * If we fail to alloc each container up to the 
		 * container we need then we must fail gracefully
		 * and make sure that the size is set back to the
		 * last initialized container
		 */
		if (container == NULL) {
			goto fail;
		}
		container->section_lock = rwlock_create("Section lock");
		if (container->section_lock  == NULL) {
			kfree(container);
			goto fail;
		}
		container->section = NULL;
		tmp = containerarray_add(tb->containers, container, &container_index);
		if (tmp) {
			rwlock_destroy(container->section_lock);
			kfree(container);
			goto fail;
		}
		TABLEASSERT(i == container_index);
	}
	lock_release(tb->container_lock);

	if (container == NULL)
		container = containerarray_get(tb->containers, sect_index);

	/* Lock the section being modified */
	rwlock_acquire_write(container->section_lock);
	if (container->section == NULL && (container->section = section_create()) == NULL) {
		rwlock_release_write(container->section_lock);
		return ENOMEM;
	}
	newadd = (section_get(container->section, rem) == NULL) ? true : false; 
	section_set(container->section, rem, val);
	rwlock_release_write(container->section_lock);

	if (newadd) {
		spinlock_acquire(&tb->table_lock);
		++tb->num;
		spinlock_release(&tb->table_lock);
	}

	return 0;

fail:
	lock_release(tb->container_lock);
	return ENOMEM;

}

TABLEINLINE 
int
table_setfirst(struct table *tb, void *val, unsigned long start, unsigned long *index_ret)
{
	TABLEASSERT(tb != NULL);
	TABLEASSERT(start < tb->max);
	TABLEASSERT(index_ret != NULL);

	struct container *container;
	int index, result;
	unsigned i, container_num,
			 start_section_index = start % SECTION_SIZE,
			 start_section = (start - start_section_index)/SECTION_SIZE,
			 max_containers = (tb->max - tb->max % SECTION_SIZE)/SECTION_SIZE + 1;

start:
	/* Table is full */
	if (tb->num == tb->max)
		return 2;

	lock_acquire(tb->container_lock);
	container_num = containerarray_num(tb->containers);
	if (start_section >= container_num) {
		result = table_set(tb, start, val);
		if (!result)
			*index_ret = start;
		return result;
	}
	lock_release(tb->container_lock);

	for (i = start_section; i < container_num; i++) {
		container = containerarray_get(tb->containers, i);
		TABLEASSERT(container != NULL);
		/* Lock the section being modified */
		rwlock_acquire_write(container->section_lock);
		if (container->section == NULL && (container->section = section_create()) == NULL) {
			rwlock_release_write(container->section_lock);
			return ENOMEM;
		}
		if ((index = section_setfirst(container->section, 
									  val, 
							   		  (i == start_section)?start_section_index:0,
									  ((i+1)*SECTION_SIZE <= tb->max)?SECTION_SIZE:tb->max-i*SECTION_SIZE)) >= 0) {
			rwlock_release_write(container->section_lock);
			goto success;
		}
		rwlock_release_write(container->section_lock);

		lock_acquire(tb->container_lock);
		container_num = containerarray_num(tb->containers);
		lock_release(tb->container_lock);
	}

	/* No space in the table for start pos to end  */ 
	if (container_num == max_containers)
		return 2;

	start_section_index = 0; 
	start_section = i;
	start = start_section * SECTION_SIZE;
	goto start;

success:
	spinlock_acquire(&tb->table_lock);
	++tb->num;
	spinlock_release(&tb->table_lock);
	
	*index_ret = i * SECTION_SIZE + (unsigned)index;
	return 0;
	
}
		

TABLEINLINE int
table_add(struct table *tb, void *val, unsigned long *index_ret)
{
	TABLEASSERT(val != NULL);
	int ret;
	unsigned long index;

	index = tb->max;
	table_setsize(tb, index + 1);
	ret = table_set(tb, index, val);
	if (!ret && (index_ret != NULL))
		*index_ret = index;
	return ret;
}

TABLEINLINE void 
table_remove(struct table *tb, unsigned long index)
{
	TABLEASSERT(index < tb->max);
	struct container *container;
	unsigned rem = index % SECTION_SIZE,
			 container_num = containerarray_num(tb->containers),
			 sect_index = (index - rem)/SECTION_SIZE;

	if ((container_num <= sect_index)  ||
		(container = containerarray_get(tb->containers, sect_index))->section == NULL)
		return;

	/* Protect removal of section */
	rwlock_acquire_write(container->section_lock);
	section_remove(container->section, rem);
	if (container->section == NULL ||
		(section_num(container->section) != 0)) {
		rwlock_release_write(container->section_lock);
		goto end;
	}
	section_destroy(container->section);
	container->section = NULL;
	rwlock_release_write(container->section_lock);

end:
	spinlock_acquire(&tb->table_lock);
	--tb->num;
	spinlock_release(&tb->table_lock);
}

/*
 * Bits for declaring and defining typed tables
 */

#define DECLTABLE_BYTYPE(TABLE, T, INLINE) 			\
	struct TABLE {									\
		struct table my_tb;							\
	};												\
													\
	INLINE struct TABLE *TABLE##_create(void);									\
	INLINE void TABLE##_destroy(struct TABLE *tb);								\
	INLINE int TABLE##_init(struct TABLE *tb);									\
	INLINE void TABLE##_cleanup(struct TABLE *tb);								\
	INLINE unsigned long TABLE##_num(const struct TABLE *tb);					\
	INLINE T *TABLE##_get(const struct TABLE *tb, unsigned long index); 		\
	INLINE int TABLE##_set(struct TABLE *tb, unsigned long index, T *val); 		\
	INLINE int TABLE##_setfirst(struct TABLE *tb, void *val, unsigned long start, unsigned long *index_ret); \
	INLINE void TABLE##_setsize(struct TABLE *tb, unsigned long num);			\
	INLINE int TABLE##_add(struct TABLE *tb, T *val, unsigned long *index_ret); \
	INLINE void TABLE##_remove(struct TABLE *tb, unsigned long index)

#define DEFTABLE_BYTYPE(TABLE, T, INLINE)               \
	INLINE struct TABLE *                               \
	TABLE##_create(void)                        		\
	{                                                   \
		struct TABLE *tb = kmalloc(sizeof(*tb));        \
		if (tb != NULL && table_init(&tb->my_tb)) {     \
			kfree(tb);                                  \
			tb = NULL;                                  \
		}                                               \
		return tb;                                      \
	}                                                   \
                                                        \
	INLINE void                                         \
	TABLE##_destroy(struct TABLE *tb) 					\
	{              										\
		table_cleanup(&tb->my_tb);                      \
		kfree(tb);                                      \
	}                                                   \
 														\
	INLINE int  										\
	TABLE##_init(struct TABLE *tb) 						\
	{ 													\
		return table_init(&tb->my_tb); 					\
	} 													\
														\
	INLINE void 										\
	TABLE##_cleanup(struct TABLE *tb) 					\
	{ 													\
  		table_cleanup(&tb->my_tb); 						\
	} 													\
														\
	INLINE unsigned long								\
	TABLE##_num(const struct TABLE *tb) 		    	\
	{ 													\
		return table_num(&tb->my_tb); 					\
	} 													\
														\
	INLINE T * 											\
	TABLE##_get(const struct TABLE *tb, unsigned long index)   \
	{ 														   \
		return (T *)table_get(&tb->my_tb, index); 			   \
	} 														   \
															   \
	INLINE int 												   \
	TABLE##_set(struct TABLE *tb, unsigned long index, T *val) \
	{ 														   \
		return table_set(&tb->my_tb, index, (void *)val); 	   \
	} 														   \
															   \
	INLINE int 										           \
	TABLE##_setfirst(struct TABLE *tb, void *val, unsigned long start, unsigned long *index_ret) 	\
	{ 														   \
		return table_setfirst(&tb->my_tb, val, start, index_ret); 	\
	} 														   \
															   \
	INLINE void 											   \
	TABLE##_setsize(struct TABLE *tb, unsigned long num)       \
	{ 														   \
		table_setsize(&tb->my_tb, num); 					   \
	} 														   \
 															   \
	INLINE int 												   \
	TABLE##_add(struct TABLE *tb, T *val, unsigned long *index_ret)  	\
	{ 																	\
		return table_add(&tb->my_tb, (void *)val, index_ret); 			\
	} 																	\
																		\
	INLINE void  														\
	TABLE##_remove(struct TABLE *tb, unsigned long index) 				\
	{ 																	\
		table_remove(&tb->my_tb, index); 								\
	}

#define DECLTABLE(T, INLINE) DECLTABLE_BYTYPE(T##table, struct T, INLINE)
#define DEFTABLE(T, INLINE) DEFTABLE_BYTYPE(T##table, struct T, INLINE)

#endif /* _TABLE_H_ */
