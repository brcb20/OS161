#ifndef _FILE_SYSCALL_H 
#define _FILE_SYSCALL_H 

/* 
 * Prototypes for file related syscalls
 */

/* read */
int sys_read(int fd, userptr_t buffer, size_t buflen, int32_t *ret);
/* write */
int sys_write(int fd, userptr_t buffer, size_t buflen, int32_t *ret);

#endif
