#ifndef SBEMU_LINUX_KERNEL_H
#define SBEMU_LINUX_KERNEL_H

#include <stddef.h>

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define ALIGN(x, a) __ALIGN_KERNEL((x), (a))
#define __ALIGN_KERNEL(x, a) __ALIGN_KERNEL_MASK(x, (__typeof__(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask) (((x) + (mask)) & ~(mask))

#include "dpmi/dbgutil.h"

#define printk(...) DBG_Logi(__VA_ARGS__)

#define PAGE_SIZE 4096

#include "au_cards/au_base.h"
//#define mdelay(m) pds_mdelay((m)*100)
//#define msleep(m) pds_mdelay((m)*100)
#define mdelay(m) pds_delay_10us((m)*100)
#define msleep(m) pds_delay_10us((m)*100)
#define udelay(u) pds_delay_1695ns(u)

#define usleep_range(x,y) pds_delay_10us((x)/10)

#define IRQ_NONE 0
#define IRQ_HANDLED 1

#ifndef BITS_PER_LONG
#define BITS_PER_LONG 32
#endif

typedef int irqreturn_t;
#define IRQF_SHARED 0

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))

#define DMA_MASK_NONE	0x0ULL

#define free_irq(x,y)

#define linux_outl(x,y) outl(y,x)

#define linux_writel(addr,value) PDS_PUTB_LE32((char *)(addr),value)
#define linux_readl(addr) PDS_GETB_LE32((char *)(addr))
#define linux_writew(addr,value) PDS_PUTB_LE16((char *)(addr), value)
#define linux_readw(addr) PDS_GETB_LE16((char *)(addr))
#define linux_writeb(addr,value) *((unsigned char *)(addr))=value
#define linux_readb(addr) PDS_GETB_8U((char *)(addr))

#define writel(value,addr) linux_writel(addr,value)
#define readl(addr) linux_readl(addr)
#define writew(value,addr) linux_writew(addr,value)
#define readw(addr) linux_readw(addr)
#define writeb(value,addr) linux_writeb(addr,value)
#define readb(addr) linux_readb(addr)


#endif
