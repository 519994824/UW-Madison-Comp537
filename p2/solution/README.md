name: Qinxinghao Chen
cs login: qinxinghao
wisc ID: qchen463
email: qchen463@wisc.edu
the status of my implementation: finished.


Please list the names of all the files you changed in solution, with a brief description of what the change was.
update UPROGS in Makefile: 	_getparentname\
update syscall.h: #define SYS_getparentname 22
update syscall.c: [SYS_getparentname]   sys_getparentname | extern int sys_getparentname(void)
update usys.S: SYSCALL(getparentname)
update user.h: add a function declaration: int getparentname(char*, char*, int, int)
<!-- update proc.c: add a function: int getparentname -->
update sysproc.c: add a function: int sys_getparentname
<!-- update defs.h: add a function declaration: int getparentname(char*, char*, int, int); -->
add getparentname.c