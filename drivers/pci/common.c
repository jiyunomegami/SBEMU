// SPDX-License-Identifier: GPL-2.0-only
/*
 *	Low-Level PCI Support for PC
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 */

//#include <linux/sched.h>
#include "linux/pci.h"
//#include <linux/pci-acpi.h>
//#include <linux/ioport.h>
//#include <linux/init.h>
//#include <linux/dmi.h>
#include "linux/slab.h"

//#include <asm/acpi.h>
//#include <asm/segment.h>
//#include <asm/io.h>
//#include <asm/smp.h>
#include "linux/pci_x86.h"
//#include <asm/setup.h>
//#include <asm/irqdomain.h>

//unsigned int pci_probe = PCI_PROBE_CONF1 | PCI_PROBE_CONF2 | PCI_PROBE_MMCONF
//  //| (IS_ENABLED(CONFIG_X86_64) || IS_ENABLED(CONFIG_X86_PAE) ? 0 : PCI_PROBE_BIOS)
//  ;

static int pci_bf_sort;
int pci_routeirq;
int noioapicquirk;
#ifdef CONFIG_X86_REROUTE_FOR_BROKEN_BOOT_IRQS
int noioapicreroute = 0;
#else
int noioapicreroute = 1;
#endif
int pcibios_last_bus = -1;
unsigned long pirq_table_addr;
//const struct pci_raw_ops *__read_mostly raw_pci_ops;
//const struct pci_raw_ops *__read_mostly raw_pci_ext_ops;

int raw_pci_read(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 *val)
{
	if (domain == 0 && reg < 256 && raw_pci_ops)
		return raw_pci_ops->read(domain, bus, devfn, reg, len, val);
	if (raw_pci_ext_ops)
		return raw_pci_ext_ops->read(domain, bus, devfn, reg, len, val);
	return -EINVAL;
}

int raw_pci_write(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val)
{
	if (domain == 0 && reg < 256 && raw_pci_ops)
		return raw_pci_ops->write(domain, bus, devfn, reg, len, val);
	if (raw_pci_ext_ops)
		return raw_pci_ext_ops->write(domain, bus, devfn, reg, len, val);
	return -EINVAL;
}

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	return raw_pci_read(pci_domain_nr(bus), bus->number,
				 devfn, where, size, value);
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	return raw_pci_write(pci_domain_nr(bus), bus->number,
				  devfn, where, size, value);
}

static struct pci_ops common_pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

/*
 * This interrupt-safe spinlock protects all accesses to PCI configuration
 * space, except for the mmconfig (ECAM) based operations.
 */
DEFINE_RAW_SPINLOCK(pci_config_lock);

static void pcibios_fixup_device_resources(struct pci_dev *dev)
{
	struct resource *rom_r = &dev->resource[PCI_ROM_RESOURCE];
	struct resource *bar_r;
	int bar;

        //printk("%s:%d\n",__FILE__,__LINE__);
	if (pci_probe & PCI_NOASSIGN_BARS) {
		/*
		* If the BIOS did not assign the BAR, zero out the
		* resource so the kernel doesn't attempt to assign
		* it later on in pci_assign_unassigned_resources
		*/
		for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
			bar_r = &dev->resource[bar];
			if (bar_r->start == 0 && bar_r->end != 0) {
				bar_r->flags = 0;
				bar_r->end = 0;
			}
		}
	}

	if (pci_probe & PCI_NOASSIGN_ROMS) {
		if (rom_r->parent)
			return;
		if (rom_r->start) {
			/* we deal with BIOS assigned ROM later */
			return;
		}
		rom_r->start = rom_r->end = rom_r->flags = 0;
	}
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void pcibios_fixup_bus(struct pci_bus *b)
{
  //printk("%s:%d\n",__FILE__,__LINE__);
	struct pci_dev *dev;

	pci_read_bridge_bases(b);
	list_for_each_entry(dev, &b->devices, bus_list)
		pcibios_fixup_device_resources(dev);
}
