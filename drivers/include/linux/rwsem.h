#ifndef SBEMU_LINUX_RWSEM_H
#define SBEMU_LINUX_RWSEM_H

#include "atomic-long.h"

typedef unsigned long rwlock_t;

struct rw_semaphore;

/* All arch specific implementations share the same struct */
struct rw_semaphore {
	atomic_long_t count;
	struct list_head wait_list;
  /*raw_*/spinlock_t wait_lock;
#if 0
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	struct optimistic_spin_queue osq; /* spinner MCS lock */
	/*
	 * Write owner. Used as a speculative check to see
	 * if the owner is running on the cpu.
	 */
	struct task_struct *owner;
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
#endif
};


#endif /* _LINUX_RWSEM_H */
