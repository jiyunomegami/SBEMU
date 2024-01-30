#ifndef SBEMU_LINUX_PCI_H
#define SBEMU_LINUX_PCI_H

#include "kernel.h"
#include "pci_regs.h"
#include "pci_ids.h"
#include "au_cards/pcibios.h"

struct device {
  u64 coherent_dma_mask;
  u64 *dma_mask;
};

#define dma_supported(x,y) 1

static inline int dma_set_coherent_mask(struct device *dev, u64 mask)
{
  if (!dma_supported(dev, mask))
    return -EIO;
  dev->coherent_dma_mask = mask;
  return 0;
}

static inline int dma_set_mask(struct device *dev, u64 mask)
{
  //struct dma_map_ops *ops = get_dma_ops(dev);

  //	if (ops->set_dma_mask)
  //		return ops->set_dma_mask(dev, mask);

  	if (!dev->dma_mask || !dma_supported(dev, mask))
  		return -EIO;
	*dev->dma_mask = mask;
	return 0;
}

struct pci_dev {
  struct device dev;
  struct pci_config_s *pcibios_dev;
  u16 device;
  u16 irq;
  u16 subsystem_vendor;
  u16 subsystem_device;
  u32 barflags;
  u32 barlen[6];
};

#define dev_info(dev,...) printk(__VA_ARGS__)
#define dev_alert(dev,...) printk(__VA_ARGS__)
#define dev_dbg(dev,...) printk(__VA_ARGS__)
#define dev_err(dev,...) printk(__VA_ARGS__)

static inline void pci_read_config_dword (struct pci_dev *pcidev, uint16_t addr, unsigned int *wordp) {
  *wordp = pcibios_ReadConfig_Dword(pcidev->pcibios_dev, addr);
}

static inline void pci_write_config_dword (struct pci_dev *pcidev, uint16_t addr, unsigned int word) {
  pcibios_WriteConfig_Dword(pcidev->pcibios_dev, addr, word);
}

static inline unsigned int pci_resource_start (struct pci_dev *pcidev, int bar_index) {
  uint32_t addr = pcibios_ReadConfig_Dword(pcidev->pcibios_dev, 0x10 + bar_index * 4);
  addr &= 0xfffffff8;
  return addr;
}

#if 1
// We should probably get the length only when creating the pci_dev structure
static inline unsigned int pci_resource_len (struct pci_dev *pcidev, int bar_index) {
  if (pcidev->barflags & (1<<bar_index)) {
    return pcidev->barlen[bar_index];
  } else {
    uint32_t addr, len;
    addr = pcibios_ReadConfig_Dword(pcidev->pcibios_dev, 0x10 + bar_index * 4);
    pcibios_WriteConfig_Dword(pcidev->pcibios_dev, 0x10 + bar_index * 4, 0xffffffff);
    len = pcibios_ReadConfig_Dword(pcidev->pcibios_dev, 0x10 + bar_index * 4);
    len = ~(len & 0xfffffff0) + 1;
    printk("bar %u len %u (0x%X)\n", bar_index, len, len);
    pcibios_WriteConfig_Dword(pcidev->pcibios_dev, 0x10 + bar_index * 4, addr);
    pcidev->barflags |= (1<<bar_index);
    pcidev->barlen[bar_index] = len;
    return len;
  }
}
#define ioremap(x,y) (unsigned long)(pds_dpmi_map_physical_memory(x, y))
#else
#define ioremap(x,y) (unsigned long)(pds_dpmi_map_physical_memory(x, 0x200000))
#endif

#define iounmap(x) pds_dpmi_unmap_physycal_memory(x)

#define pci_request_regions(x,nm) 0
#define pci_release_regions(x)

static inline int pci_enable_device (struct pci_dev *pcidev) {
  return 0; // XXX
}
static inline int pci_disable_device (struct pci_dev *pcidev) {
  return 0; // XXX
}
static inline int pci_set_master (struct pci_dev *pcidev) {
  pcibios_enable_memmap_set_master_all(pcidev->pcibios_dev);
  return 0;
}

#endif
