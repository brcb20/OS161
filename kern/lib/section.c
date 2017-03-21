#define SECTIONINLINE

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <section.h>

struct section *
section_create(void)
{
	struct section *section;

	section = kmalloc(sizeof(*section));
	if ((section != NULL) && section_init(section)) {
		kfree(section);
		section = NULL;
	}
	return section;
}

void 
section_destroy(struct section *section)
{
	section_cleanup(section);
	kfree(section);
}

int 
section_init(struct section *section)
{
	section->start = kmalloc(sizeof(void *) * SECTION_SIZE);
	if (section->start == NULL) {
		return ENOMEM;
	}
	for (int i = 0; i < SECTION_SIZE; i++)
		section->start[i] = NULL;
	section->num = 0;
	section->max = SECTION_SIZE;
	spinlock_init(&section->num_lock);
	return 0;
}

void 
section_cleanup(struct section *section)
{
	SECTIONASSERT(section->num == 0);
	kfree(section->start);
	spinlock_cleanup(&section->num_lock);
#ifdef SECTION_CHECKED
	section->start = NULL;
	section->max = 0;
#endif
}

unsigned 
section_num(const struct section *section)
{
	return section->num;
}

void *
section_get(const struct section *section, unsigned index)
{
	SECTIONASSERT(index < section->max);
	return section->start[index];
}

void
section_set(struct section *section, unsigned index, void *val)
{
	SECTIONASSERT(index < section->max);
	SECTIONASSERT(val != NULL);

	void *old_val = section_get(section, index);
	section->start[index] = val;
	if (old_val == NULL) {
		spinlock_acquire(&section->num_lock);
		++section->num;
		spinlock_release(&section->num_lock);
	}
	SECTIONASSERT(section->num <= section->max);
}

int
section_setfirst(struct section *section, void *val, unsigned start, unsigned end)
{
	SECTIONASSERT(start < section->max);
	SECTIONASSERT(end <= section->max);

	unsigned index = start;

	if (section->num == section->max)
		return -1;

	while ((index < end) && section_get(section, index) != NULL)
		++index;

	if (index == end)
		return -1;

	section_set(section, index, val);
	return (int)index;
}

int
section_add(struct section *section, void *val)
{
	SECTIONASSERT(section->num <= section->max);
	unsigned index = 0;
	if (section->num == section->max) {
		return -1;
	}
	while(section->start[index] != NULL && index < section->max)
		++index;

	section_set(section, index, val);
	return index;
}

void 
section_remove(struct section *section, unsigned index)
{
	SECTIONASSERT(index < section->max);

	void *old_val = section_get(section, index);
	section->start[index] = NULL;
	if (old_val != NULL) {
		spinlock_acquire(&section->num_lock);
		--section->num;
		spinlock_release(&section->num_lock);
	}
	SECTIONASSERT(section->num <= section->max);
}
