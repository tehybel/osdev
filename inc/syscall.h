#ifndef JOS_INC_SYSCALL_H
#define JOS_INC_SYSCALL_H

/* system call numbers */
enum {
	SYS_cputs = 0,
	SYS_cgetc,
	SYS_getenvid,
	SYS_env_destroy,
	SYS_page_alloc,
	SYS_page_map,
	SYS_page_unmap,
	SYS_exofork,
	SYS_env_set_status,
	SYS_env_set_trapframe,
	SYS_env_set_pgfault_upcall,
	SYS_yield,
	SYS_ipc_try_send,
	SYS_ipc_recv,
	SYS_time_msec,
	SYS_transmit,
	SYS_receive,
	SYS_v86,
	SYS_map_lfb,
	SYS_get_io_events,
	SYS_get_ide_io_base,
	SYS_get_mode_info,
	NSYSCALLS
};

#endif /* !JOS_INC_SYSCALL_H */
