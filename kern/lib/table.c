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

void
table_setsize(struct table *tb, unsigned long num)
{
	if (num > tb->max)
		tb->max = num;
}
