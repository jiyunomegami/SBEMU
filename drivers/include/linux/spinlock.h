#ifndef SBEMU_LINUX_SPINLOCK_H
#define SBEMU_LINUX_SPINLOCK_H

#include "linux/kernel.h"

typedef unsigned long spinlock_t;

#define spin_lock_init(x) // do nothing
#define spin_lock(x) // do nothing
#define spin_unlock(x) // do nothing
#define spin_lock_irq(x) // do nothing
#define spin_unlock_irq(x) // do nothing
#define spin_lock_irqsave(x,f) // do nothing
#define spin_unlock_irqrestore(x,f) // do nothing

#endif
