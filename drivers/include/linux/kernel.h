#ifndef SBEMU_LINUX_KERNEL_H
#define SBEMU_LINUX_KERNEL_H

#include <stddef.h>
#include "linux/types.h"

#ifndef NR_CPUS
#define NR_CPUS 1
#endif
typedef unsigned long cpumask_var_t;

#define USHRT_MAX       ((u16)(~0U))
#define SHRT_MAX        ((s16)(USHRT_MAX>>1))
#define SHRT_MIN        ((s16)(-SHRT_MAX - 1))
#define INT_MAX         ((int)(~0U>>1))
#define INT_MIN         (-INT_MAX - 1)
#define UINT_MAX        (~0U)
#define LONG_MAX        ((long)(~0UL>>1))
#define LONG_MIN        (-LONG_MAX - 1)
#define ULONG_MAX       (~0UL)
#define LLONG_MAX       ((long long)(~0ULL>>1))
#define LLONG_MIN       (-LLONG_MAX - 1)
#define ULLONG_MAX      (~0ULL)
//#define SIZE_MAX        (~(size_t)0)

#define U8_MAX          ((u8)~0U)
#define S8_MAX          ((s8)(U8_MAX>>1))
#define S8_MIN          ((s8)(-S8_MAX - 1))
#define U16_MAX         ((u16)~0U)
#define S16_MAX         ((s16)(U16_MAX>>1))
#define S16_MIN         ((s16)(-S16_MAX - 1))
#define U32_MAX         ((u32)~0U)
#define S32_MAX         ((s32)(U32_MAX>>1))
#define S32_MIN         ((s32)(-S32_MAX - 1))
#define U64_MAX         ((u64)~0ULL)
#define S64_MAX         ((s64)(U64_MAX>>1))
#define S64_MIN         ((s64)(-S64_MAX - 1))

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define ALIGN(x, a) __ALIGN_KERNEL((x), (a))
#define __ALIGN_KERNEL(x, a) __ALIGN_KERNEL_MASK(x, (__typeof__(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask) (((x) + (mask)) & ~(mask))

#define cpu_to_le32(x) x // assumes Little Endian CPU
#define le32_to_cpu(x) x // assumes Little Endian CPU
#define SHIFTCONSTANT_2(x) 2 // Number of channels

#define __KERNEL_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_UP __KERNEL_DIV_ROUND_UP

/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type,x) \
({      type __dummy; \
        typeof(x) __dummy2; \
        (void)(&__dummy == &__dummy2); \
        1; \
})

/*
 * Check at compile time that 'function' is a certain type, or is a pointer
 * to that type (needs to use typedef for the function type.)
 */
#define typecheck_fn(type,function) \
({      typeof(type) __tmp = function; \
        (void)__tmp; \
})

#define EXPORT_SYMBOL(x)

#ifndef BUILD_BUG_ON
/* Force a compilation error if condition is true */
#define BUILD_BUG_ON(condition) ((void)BUILD_BUG_ON_ZERO(condition))
/* Force a compilation error if condition is true, but also produce a
   result (of value 0 and type size_t), so the expression can be used
   e.g. in a structure initializer (or where-ever else comma expressions
   aren't permitted). */
#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))
#define BUILD_BUG_ON_NULL(e) ((void *)sizeof(struct { int:-!!(e); }))
#endif

#include "dpmi/dbgutil.h"

#define printk(...) DBG_Logi(__VA_ARGS__)
#define snd_printk(...) DBG_Logi(__VA_ARGS__)
#define KERN_ERR "ERROR: "

#define PAGE_SIZE 4096

#include "au_cards/au_base.h"
//#define mdelay(m) pds_mdelay((m)*100)
//#define msleep(m) pds_mdelay((m)*100)
#define mdelay(m) pds_delay_10us((m)*100)
#define msleep(m) pds_delay_10us((m)*100)
#define udelay(u) pds_delay_1695ns(u)

#define usleep_range(x,y) pds_delay_10us((x)/10)

#define schedule_timeout_uninterruptible(ticks) pds_delay_1695ns(ticks)

#define IRQ_NONE 0
#define IRQ_HANDLED 1

#ifndef BITS_PER_LONG
#define BITS_PER_LONG 32
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif

typedef int irqreturn_t;
#define IRQF_SHARED 0

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))

#define DMA_MASK_NONE	0x0ULL

#define free_irq(x,y)

#define linux_outb(x,y) outb(y,x)
#define linux_outw(x,y) outw(y,x)
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
