#ifndef SBEMU_LINUX_PCI_H
#define SBEMU_LINUX_PCI_H

#include "kernel.h"
#include "pci_regs.h"
#include "pci_ids.h"
#include "au_cards/pcibios.h"

struct device {
  u64 coherent_dma_mask;
  u64 __dma_mask;
  u64 *dma_mask;
  void *platform_data;
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

  //  	if (!dev->dma_mask || !dma_supported(dev, mask))
  //  		return -EIO;
  if (!dev->dma_mask)
    dev->dma_mask = &dev->__dma_mask;
  *dev->dma_mask = mask;
  return 0;
}

/*
 * Set both the DMA mask and the coherent DMA mask to the same thing.
 * Note that we don't check the return value from dma_set_coherent_mask()
 * as the DMA API guarantees that the coherent DMA mask can be set to
 * the same or smaller than the streaming DMA mask.
 */
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
        int rc = dma_set_mask(dev, mask);
        if (rc == 0)
                dma_set_coherent_mask(dev, mask);
        return rc;
}

/**
 * struct pci_device_id - PCI device ID structure
 * @vendor:             Vendor ID to match (or PCI_ANY_ID)
 * @device:             Device ID to match (or PCI_ANY_ID)
 * @subvendor:          Subsystem vendor ID to match (or PCI_ANY_ID)
 * @subdevice:          Subsystem device ID to match (or PCI_ANY_ID)
 * @class:              Device class, subclass, and "interface" to match.
 *                      See Appendix D of the PCI Local Bus Spec or
 *                      include/linux/pci_ids.h for a full list of classes.
 *                      Most drivers do not need to specify class/class_mask
 *                      as vendor/device is normally sufficient.
 * @class_mask:         Limit which sub-fields of the class field are compared.
 *                      See drivers/scsi/sym53c8xx_2/ for example of usage.
 * @driver_data:        Data private to the driver.
 *                      Most drivers don't need to use driver_data field.
 *                      Best practice is to use driver_data as an index
 *                      into a static list of equivalent device types,
 *                      instead of using it as a pointer.
 * @override_only:      Match only when dev->driver_override is this driver.
 */
struct pci_device_id {
        __u32 vendor, device;           /* Vendor and device ID or PCI_ANY_ID*/
        __u32 subvendor, subdevice;     /* Subsystem ID's or PCI_ANY_ID */
        __u32 class, class_mask;        /* (class,subclass,prog-if) triplet */
        kernel_ulong_t driver_data;     /* Data private to the driver */
        __u32 override_only;
};

struct pci_dev {
  struct device dev;
  struct pci_config_s *pcibios_dev;
  u8 revision;
  u16 vendor;
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

static inline void pci_read_config_word (struct pci_dev *pcidev, uint16_t addr, unsigned short *wordp) {
  *wordp = pcibios_ReadConfig_Word(pcidev->pcibios_dev, addr);
}

static inline void pci_write_config_word (struct pci_dev *pcidev, uint16_t addr, unsigned short word) {
  pcibios_WriteConfig_Word(pcidev->pcibios_dev, addr, word);
}

static inline void pci_read_config_byte (struct pci_dev *pcidev, uint16_t addr, unsigned char *wordp) {
  *wordp = pcibios_ReadConfig_Byte(pcidev->pcibios_dev, addr);
}

static inline void pci_write_config_byte (struct pci_dev *pcidev, uint16_t addr, unsigned char word) {
  pcibios_WriteConfig_Byte(pcidev->pcibios_dev, addr, word);
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
    //printk("bar %u len %u (0x%X)\n", bar_index, len, len);
    pcibios_WriteConfig_Dword(pcidev->pcibios_dev, 0x10 + bar_index * 4, addr);
    pcidev->barlen[bar_index] = len;
    pcidev->barflags |= (1<<bar_index);
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
static inline int pcim_enable_device (struct pci_dev *pcidev) {
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
