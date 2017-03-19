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
/* lseek */
int sys_lseek(int fd, uint32_t u_off, uint32_t l_off, userptr_t whence_ptr, int32_t *ret1, int32_t *ret2);
/* Dup2 */
int sys_dup2(int oldfd, int newfd);
/* chdir */
int sys_chdir(const_userptr_t pathname);
/* getcwd */
int sys___getcwd(userptr_t buf, size_t buflen, int32_t *ret);

#endif
