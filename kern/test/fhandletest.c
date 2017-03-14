#include <types.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <fhandle.h>
#include <test.h>

int 
fhtest(int argc, char **args)
{
	struct fd **fds;
	long unsigned i, j,
		 		  max = 60000;
	int result;
	char *path;

	(void)argc;
	(void)args;

	kprintf("Beginning open file table tests...\n");
	fds = kmalloc(sizeof(*fds)*max);
	KASSERT(fds != NULL);
	path = kmalloc(sizeof(char) * 5);
	KASSERT(path != NULL);

	for (i = 0; i < max; i++) {
		strcpy(path, "con:");	
		 result = fh_add(O_RDONLY, path, fds + i);
		 if (result) { goto next; }
		 KASSERT(*(fds+i) != NULL);
		 KASSERT((*(fds+i))->index == i);
		 KASSERT(fds[i]->fh->refcount == 1);
		 fh_inc(fds[i]);
		 KASSERT(fds[i]->fh->refcount == 2);
	}

next:
	for (j = 0; j < i; j++) {
		fh_dec(*(fds+j));
		KASSERT(fds[j]->fh->refcount == 1);
		fh_dec(*(fds+j));
	}
	
	kfree(path);
	kfree(fds);
	return 0;
}
