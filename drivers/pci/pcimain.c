#include "linux/pci.h"

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

struct pci_ops pci_root_ops = {
        .read = pci_read,
        .write = pci_write,
};

int x86_pci_root_bus_node(int bus);
void x86_pci_root_bus_resources(int bus, struct list_head *resources);

void pcibios_scan_root(int busnum)
{
        struct pci_bus *bus;
        struct pci_sysdata *sd;
        LIST_HEAD(resources);

        sd = kzalloc(sizeof(*sd), GFP_KERNEL);
        if (!sd) {
                printk(KERN_ERR "PCI: OOM, skipping PCI bus %02x\n", busnum);
                return;
        }
        sd->node = x86_pci_root_bus_node(busnum);
        x86_pci_root_bus_resources(busnum, &resources);
        //printk(KERN_DEBUG "PCI: Probing PCI hardware (bus %02x)\n", busnum);
        bus = pci_scan_root_bus(NULL, busnum, &pci_root_ops, sd, &resources);
        if (!bus) {
                pci_free_resource_list(&resources);
                kfree(sd);
                return;
        }
        pci_bus_add_devices(bus);
}

void pci_walk_bus(struct pci_bus *top, int (*cb)(struct pci_dev *, void *),
		  void *userdata);

struct pcitree {
  int level;
  struct pci_dev *prev;
  struct pci_bus *parent;
};

static int
pciwalk_cb (struct pci_dev *dev, void *up)
{
  if (!dev) return 0;
  struct pcitree *tree = (struct pcitree *)up;
  if (tree->prev == dev->bus->self) {
    tree->level++;
  } else if (tree->parent == dev->bus->parent) {
  } else {
    if (tree->level)
      tree->level--;
  }
  tree->prev = dev;
  tree->parent = dev->bus->parent;
  for (int i = 0; i < tree->level; i++) printk("  ");
  printk("%2.2X", dev->bus->number);
  printk(":%2.2X.%X", PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
  printk(" %4.4X:%4.4X", dev->vendor, dev->device);
  if (dev->irq)
    printk(" IRQ%u INT%c#", dev->irq, 'A'+dev->pin);
  if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
    printk(" [bridge");
    if (dev->transparent)
      printk(" (subtractive decode)");
    printk("]");
  }
  if (dev == dev->bus->self) {
    printk(" [");
    if (pci_is_root_bus(dev->bus))
      printk("root ");
    printk("bus]");
  }
  printk("\n");
  if (dev->vendor==0x1073 && dev->device==0x0012) { // YAMAHA YMF754
  }
  return 0;
}

extern struct pci_bus *root_bus;

extern int pci_direct_probe (void);

int
linux_pcimain () {
  //printk("skipping pcimain??\n");  return 0;
  if (pci_direct_probe()) {
    pcibios_scan_root(0);
  }

  struct pci_dev *dev = NULL;
  struct pci_bus *bus = root_bus;
  struct pcitree tree;
  tree.level = 0;
  tree.parent = NULL;
  printk("*** PCI TREE ************\n");
  //pciwalk_cb(bus->self, &tree);
  pci_walk_bus(bus, pciwalk_cb, &tree);

  return 0;
}
