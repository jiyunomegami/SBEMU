// SPDX-License-Identifier: GPL-2.0
/*
 * From setup-res.c, by:
 *	Dave Rusling (david.rusling@reo.mts.dec.com)
 *	David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *	Ivan Kokshaysky (ink@jurassic.park.msu.ru)
 */
#include "linux/module.h"
#include "linux/kernel.h"
#include "linux/pci.h"
//#include <linux/errno.h>
//#include <linux/ioport.h>
//#include <linux/of.h>
//#include <linux/proc_fs.h>
#include "linux/slab.h"
#include "linux/rwsem.h"

#include "pci.h"
struct device *bus_find_device(const struct bus_type *bus,
                               struct device *start, const void *data,
                               int (*match)(struct device *dev, const void *data));

int allocate_resource(struct resource *root, struct resource *new,
		      resource_size_t size, resource_size_t min,
		      resource_size_t max, resource_size_t align,
		      resource_size_t (*alignf)(void *,
						const struct resource *,
						resource_size_t,
						resource_size_t),
		      void *alignf_data);

void pci_add_resource_offset(struct list_head *resources, struct resource *res,
			     resource_size_t offset)
{
	struct resource_entry *entry;

	entry = resource_list_create_entry(res, 0);
	if (!entry) {
		pr_err("PCI: can't add host bridge window %pR\n", res);
		return;
	}

	entry->offset = offset;
	resource_list_add_tail(entry, resources);
}
EXPORT_SYMBOL(pci_add_resource_offset);

void pci_add_resource(struct list_head *resources, struct resource *res)
{
	pci_add_resource_offset(resources, res, 0);
}
EXPORT_SYMBOL(pci_add_resource);

void pci_free_resource_list(struct list_head *resources)
{
	resource_list_free(resources);
}
EXPORT_SYMBOL(pci_free_resource_list);

void pci_bus_add_resource(struct pci_bus *bus, struct resource *res,
			  unsigned int flags)
{
	struct pci_bus_resource *bus_res;

	bus_res = kzalloc(sizeof(struct pci_bus_resource), GFP_KERNEL);
	if (!bus_res) {
		dev_err(&bus->dev, "can't add %pR resource\n", res);
		return;
	}

	bus_res->res = res;
	bus_res->flags = flags;
	list_add_tail(&bus_res->list, &bus->resources);
}

struct resource *pci_bus_resource_n(const struct pci_bus *bus, int n)
{
	struct pci_bus_resource *bus_res;

	if (n < PCI_BRIDGE_RESOURCE_NUM)
		return bus->resource[n];

	n -= PCI_BRIDGE_RESOURCE_NUM;
	list_for_each_entry(bus_res, &bus->resources, list) {
		if (n-- == 0)
			return bus_res->res;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(pci_bus_resource_n);

void pci_bus_remove_resource(struct pci_bus *bus, struct resource *res)
{
	struct pci_bus_resource *bus_res, *tmp;
	int i;

	for (i = 0; i < PCI_BRIDGE_RESOURCE_NUM; i++) {
		if (bus->resource[i] == res) {
			bus->resource[i] = NULL;
			return;
		}
	}

	list_for_each_entry_safe(bus_res, tmp, &bus->resources, list) {
		if (bus_res->res == res) {
			list_del(&bus_res->list);
			kfree(bus_res);
			return;
		}
	}
}

void pci_bus_remove_resources(struct pci_bus *bus)
{
	int i;
	struct pci_bus_resource *bus_res, *tmp;

	for (i = 0; i < PCI_BRIDGE_RESOURCE_NUM; i++)
		bus->resource[i] = NULL;

	list_for_each_entry_safe(bus_res, tmp, &bus->resources, list) {
		list_del(&bus_res->list);
		kfree(bus_res);
	}
}

#if 0
int devm_request_pci_bus_resources(struct device *dev,
				   struct list_head *resources)
{
	struct resource_entry *win;
	struct resource *parent, *res;
	int err;

	resource_list_for_each_entry(win, resources) {
		res = win->res;
		switch (resource_type(res)) {
		case IORESOURCE_IO:
			parent = &ioport_resource;
			break;
		case IORESOURCE_MEM:
			parent = &iomem_resource;
			break;
		default:
			continue;
		}

		err = devm_request_resource(dev, parent, res);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(devm_request_pci_bus_resources);
#endif

static struct pci_bus_region pci_32_bit = {0, 0xffffffffULL};
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
static struct pci_bus_region pci_64_bit = {0,
				(pci_bus_addr_t) 0xffffffffffffffffULL};
static struct pci_bus_region pci_high = {(pci_bus_addr_t) 0x100000000ULL,
				(pci_bus_addr_t) 0xffffffffffffffffULL};
#endif

/*
 * @res contains CPU addresses.  Clip it so the corresponding bus addresses
 * on @bus are entirely within @region.  This is used to control the bus
 * addresses of resources we allocate, e.g., we may need a resource that
 * can be mapped by a 32-bit BAR.
 */
static void pci_clip_resource_to_region(struct pci_bus *bus,
					struct resource *res,
					struct pci_bus_region *region)
{
	struct pci_bus_region r;

	pcibios_resource_to_bus(bus, &r, res);
	if (r.start < region->start)
		r.start = region->start;
	if (r.end > region->end)
		r.end = region->end;

	if (r.end < r.start)
		res->end = res->start - 1;
	else
		pcibios_bus_to_resource(bus, res, &r);
}

static int pci_bus_alloc_from_region(struct pci_bus *bus, struct resource *res,
		resource_size_t size, resource_size_t align,
		resource_size_t min, unsigned long type_mask,
		resource_size_t (*alignf)(void *,
					  const struct resource *,
					  resource_size_t,
					  resource_size_t),
		void *alignf_data,
		struct pci_bus_region *region)
{
	struct resource *r, avail;
	resource_size_t max;
	int ret;

	type_mask |= IORESOURCE_TYPE_BITS;

	pci_bus_for_each_resource(bus, r) {
		resource_size_t min_used = min;

		if (!r)
			continue;

		/* type_mask must match */
		if ((res->flags ^ r->flags) & type_mask)
			continue;

		/* We cannot allocate a non-prefetching resource
		   from a pre-fetching area */
		if ((r->flags & IORESOURCE_PREFETCH) &&
		    !(res->flags & IORESOURCE_PREFETCH))
			continue;

		avail = *r;
		pci_clip_resource_to_region(bus, &avail, region);

		/*
		 * "min" is typically PCIBIOS_MIN_IO or PCIBIOS_MIN_MEM to
		 * protect badly documented motherboard resources, but if
		 * this is an already-configured bridge window, its start
		 * overrides "min".
		 */
		if (avail.start)
			min_used = avail.start;

		max = avail.end;

		/* Don't bother if available space isn't large enough */
		if (size > max - min_used + 1)
			continue;

		/* Ok, try it out.. */
		ret = allocate_resource(r, res, size, min_used, max,
					align, alignf, alignf_data);
		if (ret == 0)
			return 0;
	}
	return -ENOMEM;
}

/**
 * pci_bus_alloc_resource - allocate a resource from a parent bus
 * @bus: PCI bus
 * @res: resource to allocate
 * @size: size of resource to allocate
 * @align: alignment of resource to allocate
 * @min: minimum /proc/iomem address to allocate
 * @type_mask: IORESOURCE_* type flags
 * @alignf: resource alignment function
 * @alignf_data: data argument for resource alignment function
 *
 * Given the PCI bus a device resides on, the size, minimum address,
 * alignment and type, try to find an acceptable resource allocation
 * for a specific device resource.
 */
int pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
		resource_size_t size, resource_size_t align,
		resource_size_t min, unsigned long type_mask,
		resource_size_t (*alignf)(void *,
					  const struct resource *,
					  resource_size_t,
					  resource_size_t),
		void *alignf_data)
{
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	int rc;

	if (res->flags & IORESOURCE_MEM_64) {
		rc = pci_bus_alloc_from_region(bus, res, size, align, min,
					       type_mask, alignf, alignf_data,
					       &pci_high);
		if (rc == 0)
			return 0;

		return pci_bus_alloc_from_region(bus, res, size, align, min,
						 type_mask, alignf, alignf_data,
						 &pci_64_bit);
	}
#endif

	return pci_bus_alloc_from_region(bus, res, size, align, min,
					 type_mask, alignf, alignf_data,
					 &pci_32_bit);
}
EXPORT_SYMBOL(pci_bus_alloc_resource);

/*
 * The @idx resource of @dev should be a PCI-PCI bridge window.  If this
 * resource fits inside a window of an upstream bridge, do nothing.  If it
 * overlaps an upstream window but extends outside it, clip the resource so
 * it fits completely inside.
 */
bool pci_bus_clip_resource(struct pci_dev *dev, int idx)
{
	struct pci_bus *bus = dev->bus;
	struct resource *res = &dev->resource[idx];
	struct resource orig_res = *res;
	struct resource *r;

	pci_bus_for_each_resource(bus, r) {
		resource_size_t start, end;

		if (!r)
			continue;

		if (resource_type(res) != resource_type(r))
			continue;

		start = max(r->start, res->start);
		end = min(r->end, res->end);

		if (start > end)
			continue;	/* no overlap */

		if (res->start == start && res->end == end)
			return false;	/* no change */

		res->start = start;
		res->end = end;
		res->flags &= ~IORESOURCE_UNSET;
		orig_res.flags &= ~IORESOURCE_UNSET;
		pci_info(dev, "%pR clipped to %pR\n", &orig_res, res);

		return true;
	}

	return false;
}

void __weak pcibios_resource_survey_bus(struct pci_bus *bus) { }

void __weak pcibios_bus_add_device(struct pci_dev *pdev) { }

/**
 * pci_bus_add_device - start driver for a single device
 * @dev: device to add
 *
 * This adds add sysfs entries and start device drivers
 */
void pci_bus_add_device(struct pci_dev *dev)
{
	struct device_node *dn = dev->dev.of_node;
	int retval;

	/*
	 * Can not put in pci_device_add yet because resources
	 * are not assigned yet for some devices.
	 */
	pcibios_bus_add_device(dev);
	pci_fixup_device(pci_fixup_final, dev);
	if (pci_is_bridge(dev))
		of_pci_make_dev_node(dev);
	//pci_create_sysfs_dev_files(dev);
	pci_proc_attach_device(dev);
	pci_bridge_d3_update(dev);

#if 0 // XXX
	dev->match_driver = !dn || of_device_is_available(dn);
	retval = device_attach(&dev->dev);
	if (retval < 0 && retval != -EPROBE_DEFER)
		pci_warn(dev, "device attach failed (%d)\n", retval);
#endif

	pci_dev_assign_added(dev, true);
}
EXPORT_SYMBOL_GPL(pci_bus_add_device);

/**
 * pci_bus_add_devices - start driver for PCI devices
 * @bus: bus to check for new devices
 *
 * Start driver for PCI devices and add some sysfs entries.
 */
void pci_bus_add_devices(const struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct pci_bus *child;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/* Skip already-added devices */
		if (pci_dev_is_added(dev))
			continue;
		pci_bus_add_device(dev);
	}

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/* Skip if device attach failed */
		if (!pci_dev_is_added(dev))
			continue;
		child = dev->subordinate;
		if (child)
			pci_bus_add_devices(child);
	}
}
EXPORT_SYMBOL(pci_bus_add_devices);

/** pci_walk_bus - walk devices on/under bus, calling callback.
 *  @top      bus whose devices should be walked
 *  @cb       callback to be called for each device found
 *  @userdata arbitrary pointer to be passed to callback.
 *
 *  Walk the given bus, including any bridged devices
 *  on buses under this bus.  Call the provided callback
 *  on each device found.
 *
 *  We check the return of @cb each time. If it returns anything
 *  other than 0, we break out.
 *
 */
void pci_walk_bus(struct pci_bus *top, int (*cb)(struct pci_dev *, void *),
		  void *userdata)
{
	struct pci_dev *dev;
	struct pci_bus *bus;
	struct list_head *next;
	int retval;

	bus = top;
	down_read(&pci_bus_sem);
	next = top->devices.next;
	for (;;) {
		if (next == &bus->devices) {
			/* end of this bus, go up or finish */
			if (bus == top)
				break;
			next = bus->self->bus_list.next;
			bus = bus->self->bus;
			continue;
		}
		dev = list_entry(next, struct pci_dev, bus_list);
		if (dev->subordinate) {
			/* this is a pci-pci bridge, do its devices next */
			next = dev->subordinate->devices.next;
			bus = dev->subordinate;
		} else
			next = dev->bus_list.next;

		retval = cb(dev, userdata);
		if (retval)
			break;
	}
	up_read(&pci_bus_sem);
}
EXPORT_SYMBOL_GPL(pci_walk_bus);

struct pci_bus *pci_bus_get(struct pci_bus *bus)
{
	if (bus)
		get_device(&bus->dev);
	return bus;
}

void pci_bus_put(struct pci_bus *bus)
{
	if (bus)
		put_device(&bus->dev);
}


// from drivers/base/bus.c
/* /sys/devices/system */
static struct kset *system_kset;

/* /sys/bus */
static struct kset *bus_kset;

static struct device *next_device(struct klist_iter *i)
{
        struct klist_node *n = klist_next(i);
        struct device *dev = NULL;
        struct device_private *dev_prv;

        if (n) {
                dev_prv = to_device_private_bus(n);
                dev = dev_prv->device;
        }
        return dev;
}

struct kset_uevent_ops {
  //int (* const filter)(const struct kobject *kobj);
  //const char *(* const name)(const struct kobject *kobj);
  //int (* const uevent)(const struct kobject *kobj, struct kobj_uevent_env *env);
};

/**
 * kset_create() - Create a struct kset dynamically.
 *
 * @name: the name for the kset
 * @uevent_ops: a struct kset_uevent_ops for the kset
 * @parent_kobj: the parent kobject of this kset, if any.
 *
 * This function creates a kset structure dynamically.  This structure can
 * then be registered with the system and show up in sysfs with a call to
 * kset_register().  When you are finished with this structure, if
 * kset_register() has been called, call kset_unregister() and the
 * structure will be dynamically freed when it is no longer being used.
 *
 * If the kset was not able to be created, NULL will be returned.
 */
static struct kset *kset_create(const char *name,
                                const struct kset_uevent_ops *uevent_ops,
                                struct kobject *parent_kobj)
{
        struct kset *kset;
        int retval;

        kset = kzalloc(sizeof(*kset), GFP_KERNEL);
        if (!kset)
                return NULL;
#if 0
        retval = kobject_set_name(&kset->kobj, "%s", name);
        if (retval) {
                kfree(kset);
                return NULL;
        }
        kset->uevent_ops = uevent_ops;
        kset->kobj.parent = parent_kobj;

        /*
         * The kobject of this kset will have a type of kset_ktype and belong to
         * no kset itself.  That way we can properly free it when it is
         * finished being used.
         */
        kset->kobj.ktype = &kset_ktype;
        kset->kobj.kset = NULL;
#endif

        return kset;
}

/**
 * kset_create_and_add() - Create a struct kset dynamically and add it to sysfs.
 *
 * @name: the name for the kset
 * @uevent_ops: a struct kset_uevent_ops for the kset
 * @parent_kobj: the parent kobject of this kset, if any.
 *
 * This function creates a kset structure dynamically and registers it
 * with sysfs.  When you are finished with this structure, call
 * kset_unregister() and the structure will be dynamically freed when it
 * is no longer being used.
 *
 * If the kset was not able to be created, NULL will be returned.
 */
struct kset *kset_create_and_add(const char *name,
                                 const struct kset_uevent_ops *uevent_ops,
                                 struct kobject *parent_kobj)
{
        struct kset *kset;
        int error;

        kset = kset_create(name, uevent_ops, parent_kobj);
        if (!kset)
                return NULL;
#if 0
        error = kset_register(kset);
        if (error) {
                kfree(kset);
                return NULL;
        }
#endif
        return kset;
}
EXPORT_SYMBOL_GPL(kset_create_and_add);

#if 0
static int bus_uevent_filter(const struct kobject *kobj)
{
        const struct kobj_type *ktype = get_ktype(kobj);

        if (ktype == &bus_ktype)
                return 1;
        return 0;
}
#endif

static const struct kset_uevent_ops bus_uevent_ops = {
  //.filter = bus_uevent_filter,
};

/**
 * bus_to_subsys - Turn a struct bus_type into a struct subsys_private
 *
 * @bus: pointer to the struct bus_type to look up
 *
 * The driver core internals needs to work on the subsys_private structure, not
 * the external struct bus_type pointer.  This function walks the list of
 * registered busses in the system and finds the matching one and returns the
 * internal struct subsys_private that relates to that bus.
 *
 * Note, the reference count of the return value is INCREMENTED if it is not
 * NULL.  A call to subsys_put() must be done when finished with the pointer in
 * order for it to be properly freed.
 */
static struct subsys_private *bus_to_subsys(const struct bus_type *bus)
{
        struct subsys_private *sp = NULL;
        struct kobject *kobj;

        printk("bus_to_subsys bus: %p...\n", bus);
        if (!bus_kset) {
          //bus_kset = kset_create_and_add("bus", &bus_uevent_ops, NULL);
        }

        if (!bus || !bus_kset)
                return NULL;

        spin_lock(&bus_kset->list_lock);

        printf("%s:%d\n", __FILE__, __LINE__);
        if (list_empty(&bus_kset->list))
                goto done;

        printf("%s:%d\n", __FILE__, __LINE__);
        list_for_each_entry(kobj, &bus_kset->list, entry) {
                struct kset *kset = container_of(kobj, struct kset, kobj);

        printf("%s:%d\n", __FILE__, __LINE__);
                sp = container_of_const(kset, struct subsys_private, subsys);
                if (sp->bus == bus)
                        goto done;
        }
        sp = NULL;
done:
        sp = subsys_get(sp);
        spin_unlock(&bus_kset->list_lock);
        return sp;
}

/**
 * bus_find_device - device iterator for locating a particular device.
 * @bus: bus type
 * @start: Device to begin with
 * @data: Data to pass to match function
 * @match: Callback function to check device
 *
 * This is similar to the bus_for_each_dev() function above, but it
 * returns a reference to a device that is 'found' for later use, as
 * determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero, this function will
 * return to the caller and not iterate over any more devices.
 */
struct device *bus_find_device(const struct bus_type *bus,
                               struct device *start, const void *data,
                               int (*match)(struct device *dev, const void *data))
{
        struct subsys_private *sp = bus_to_subsys(bus);
        struct klist_iter i;
        struct device *dev;

        printk("bus_find_device sp: %p...\n", sp);
        if (!sp)
                return NULL;

        klist_iter_init_node(&sp->klist_devices, &i,
                             (start ? &start->p->knode_bus : NULL));
        while ((dev = next_device(&i))) {
          printk("dev: %p\n", dev);
                if (match(dev, data) && get_device(dev))
                        break;
        }
        klist_iter_exit(&i);
        subsys_put(sp);
        return dev;
}
EXPORT_SYMBOL_GPL(bus_find_device);

/*
 * Yes, this forcibly breaks the klist abstraction temporarily.  It
 * just wants to sort the klist, not change reference counts and
 * take/drop locks rapidly in the process.  It does all this while
 * holding the lock for the list, so objects can't otherwise be
 * added/removed while we're swizzling.
 */
static void device_insertion_sort_klist(struct device *a, struct list_head *list,
                                        int (*compare)(const struct device *a,
                                                        const struct device *b))
{
        struct klist_node *n;
        struct device_private *dev_prv;
        struct device *b;

        list_for_each_entry(n, list, n_node) {
                dev_prv = to_device_private_bus(n);
                b = dev_prv->device;
                if (compare(a, b) <= 0) {
                        list_move_tail(&a->p->knode_bus.n_node,
                                       &b->p->knode_bus.n_node);
                        return;
                }
        }
        list_move_tail(&a->p->knode_bus.n_node, list);
}

void bus_sort_breadthfirst(struct bus_type *bus,
                           int (*compare)(const struct device *a,
                                          const struct device *b))
{
        struct subsys_private *sp = bus_to_subsys(bus);
        LIST_HEAD(sorted_devices);
        struct klist_node *n, *tmp;
        struct device_private *dev_prv;
        struct device *dev;
        struct klist *device_klist;

        if (!sp)
                return;
        device_klist = &sp->klist_devices;

        spin_lock(&device_klist->k_lock);
        list_for_each_entry_safe(n, tmp, &device_klist->k_list, n_node) {
                dev_prv = to_device_private_bus(n);
                dev = dev_prv->device;
                device_insertion_sort_klist(dev, &sorted_devices, compare);
        }
        list_splice(&sorted_devices, &device_klist->k_list);
        spin_unlock(&device_klist->k_lock);
        subsys_put(sp);
}
EXPORT_SYMBOL_GPL(bus_sort_breadthfirst);
