#include "linux/kernel.h"
#include "linux/wait.h"
#include "linux/list.h"
#include "linux/pci.h"

unsigned int nr_node_ids = 1;
struct task_struct __g_current;
// kernel/sched/wait.c
int autoremove_wake_function(struct wait_queue_entry *wq_entry, unsigned mode, int sync, void *key)
{
        int ret = default_wake_function(wq_entry, mode, sync, key);

        if (ret)
                list_del_init_careful(&wq_entry->entry);

        return ret;
}
EXPORT_SYMBOL(autoremove_wake_function);

void init_wait_entry(struct wait_queue_entry *wq_entry, int flags)
{
        wq_entry->flags = flags;
        wq_entry->private = current;
        wq_entry->func = autoremove_wake_function;
        INIT_LIST_HEAD(&wq_entry->entry);
}
EXPORT_SYMBOL(init_wait_entry);

void add_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry)
{
        unsigned long flags;

        wq_entry->flags &= ~WQ_FLAG_EXCLUSIVE;
        spin_lock_irqsave(&wq_head->lock, flags);
        __add_wait_queue(wq_head, wq_entry);
        spin_unlock_irqrestore(&wq_head->lock, flags);
}
EXPORT_SYMBOL(add_wait_queue);

void remove_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry)
{
        unsigned long flags;

        spin_lock_irqsave(&wq_head->lock, flags);
        __remove_wait_queue(wq_head, wq_entry);
        spin_unlock_irqrestore(&wq_head->lock, flags);
}
EXPORT_SYMBOL(remove_wait_queue);

int default_wake_function(struct wait_queue_entry *wq_entry, unsigned mode, int sync, void *key)
{
  return 0;
}

#if 0
/**
 * kobject_put() - Decrement refcount for object.
 * @kobj: object.
 *
 * Decrement the refcount, and if 0, call kobject_cleanup().
 */
void kobject_put(struct kobject *kobj)
{
        if (kobj) {
                if (!kobj->state_initialized)
                        WARN(1, KERN_WARNING
                                "kobject: '%s' (%p): is not initialized, yet kobject_put() is being called.\n",
                             kobject_name(kobj), kobj);
                kref_put(&kobj->kref, kobject_release);
        }
}
EXPORT_SYMBOL(kobject_put);
#endif

/**
 * get_device - increment reference count for device.
 * @dev: device.
 *
 * This simply forwards the call to kobject_get(), though
 * we do take care to provide for the case that we get a NULL
 * pointer passed in.
 */
struct device *get_device(struct device *dev)
{
  if (dev)
    dev->refcnt++;
  return dev;
  //return dev ? kobj_to_dev(kobject_get(&dev->kobj)) : NULL;
}
EXPORT_SYMBOL_GPL(get_device);

/**
 * put_device - decrement reference count.
 * @dev: device in question.
 */
void put_device(struct device *dev)
{
  if (dev)
    dev->refcnt--;
#if 0
        /* might_sleep(); */
        if (dev)
                kobject_put(&dev->kobj);
#endif
}
EXPORT_SYMBOL_GPL(put_device);
