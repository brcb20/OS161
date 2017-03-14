#ifndef _FILE_SYSCALL_H 
#define _FILE_SYSCALL_H 

/* 
 * Prototypes for file related syscalls
 */

/* Open */
int sys_open(const_userptr_t path_ptr, int flags, int32_t *ret);
/* Close */
int sys_close(int fd);
/* read */
int sys_read(int fd, userptr_t buffer, size_t buflen, int32_t *ret);
/* write */
int sys_write(int fd, userptr_t buffer, size_t buflen, int32_t *ret);

#endif
