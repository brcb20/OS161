#define TABLEINLINE
#define CONTAINERINLINE

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <table.h>

struct table *
table_create(void)
{
	struct table *tb;

	tb = kmalloc(sizeof(*tb));
	if (tb != NULL && table_init(tb)) {
		kfree(tb);
		tb = NULL;	
	}
	return tb;
}

void 
table_destroy(struct table *tb)
{
	table_cleanup(tb);
	kfree(tb);
}

int 
table_init(struct table *tb)
{
	tb->containers = containerarray_create();
	if (tb->containers == NULL) {
		return ENOMEM;
	}
	tb->container_lock = lock_create("Table: container lock");
	tb->num = tb->max = 0;	
	spinlock_init(&tb->table_lock);
	return 0;
}

void
table_cleanup(struct table *tb)
{
	TABLEASSERT(tb->num == 0);

	struct container *container;

	for (unsigned i = 0, len = containerarray_num(tb->containers); i < len; i++) {
		container = containerarray_get(tb->containers, i);
		TABLEASSERT(container != NULL);
		TABLEASSERT(container->section == NULL);
		rwlock_destroy(container->section_lock);
		kfree(container);
	}

	containerarray_setsize(tb->containers, 0);
	containerarray_destroy(tb->containers);
	spinlock_cleanup(&tb->table_lock);
	lock_destroy(tb->container_lock);
#ifdef TABLE_CHECKED
	tb->containers = NULL;
	tb->container_lock = NULL;
	tb->max = 0;
#endif
}

static
int
table_preallocate(struct table *tb, unsigned long num)
{
	struct container *container;
	unsigned i, container_num, tmp, index,
			 max_containers = (num - num % SECTION_SIZE)/SECTION_SIZE + 1;

 	if (num > tb->max) {
		lock_acquire(tb->container_lock);
		container_num = containerarray_num(tb->containers);

		for (i = container_num; i < max_containers; i++) {
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
			tmp = containerarray_add(tb->containers, container, &index);
			if (tmp) {
				rwlock_destroy(container->section_lock);
				kfree(container);
				goto fail;
			}
			TABLEASSERT(i == index);
		}
		lock_release(tb->container_lock);
	}
	return 0;

fail:
	for (i = i-1; i >= container_num; i--) {
		container = containerarray_get(tb->containers, i);
		TABLEASSERT(container != NULL);
		rwlock_destroy(container->section_lock);
		kfree(container);
		containerarray_remove(tb->containers, i);
	}
	lock_release(tb->container_lock);
	return ENOMEM;
}

int
table_setsize(struct table *tb, unsigned long num)
{
	int result;

	result = table_preallocate(tb, num);
	if (result) {
		return result;
	}
	tb->max = num;

	return 0;
}
