// User-level IPC library routines

#include <inc/lib.h>

// Receive a value via IPC and return it.
// If 'pg' is nonnull, then any page sent by the sender will be mapped at
//	that address.
// If 'from_env_store' is nonnull, then store the IPC sender's envid in
//	*from_env_store.
// If 'perm_store' is nonnull, then store the IPC sender's page permission
//	in *perm_store (this is nonzero iff a page was successfully
//	transferred to 'pg').
// If the system call fails, then store 0 in *fromenv and *perm (if
//	they're nonnull) and return the error.
// Otherwise, return the value sent by the sender
//
int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	int32_t error;

	// if 'pg' is null, pass sys_ipc_recv a value which means "no page"
	if (pg == NULL)
		pg = (void *) -1;

	error = sys_ipc_recv(pg);

	if (error) {
		// syscall failed
		if (from_env_store)
			*from_env_store = 0;
		if (perm_store)
			*perm_store = 0;
		return error;
	}

	// syscall succeeded
	if (from_env_store)
		*from_env_store = thisenv->env_ipc_from;
	if (perm_store)
		*perm_store = thisenv->env_ipc_perm;

	return thisenv->env_ipc_value;
}

// Send 'val' (and 'pg' with 'perm', if 'pg' is nonnull) to 'toenv'.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
	int error;

	// if 'pg' is null, pass a value which means "no page"
	if (pg == NULL) {
		pg = (void *) -1;
		perm = 0;
	}

	while (1) {
		error = sys_ipc_try_send(to_env, val, pg, perm);
		if (error == 0)
			break;
		else if (error == -E_IPC_NOT_RECV)
			sys_yield();
		else
			panic("sys_ipc_try_send returned %d ('%e')", error, error);
	}
}

// Find the first environment of the given type.  We'll use this to
// find special environments.
// Returns 0 if no such environment exists.
envid_t
ipc_find_env(enum EnvType type)
{
	int i;
	for (i = 0; i < NENV; i++)
		if (envs[i].env_type == type)
			return envs[i].env_id;
	return 0;
}
