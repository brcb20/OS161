#ifndef _PROC_SYSCALL_H
#define _PROC_SYSCALL_H

#include <cdefs.h>

/*
 * Prototypes for process related syscalls
 */

void sys_bootstrap(void);
/* fork */
int sys_fork(struct trapframe *c_tf, int32_t *ret);
/* Execv */
int sys_execv(const_userptr_t progname, userptr_t args);
/* waitpid */
int sys_waitpid(pid_t pid, userptr_t status, int options, int32_t *ret);
/* _exit */
__DEAD void sys__exit(int exitcode);
/* getpid */
void sys_getpid(int32_t *ret);

#endif
