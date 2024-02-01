#ifndef SBEMU_LINUX_SLAB_H
#define SBEMU_LINUX_SLAB_H

extern void pds_memxch(char *,char *,unsigned int);
extern void *pds_malloc(unsigned int bufsize);
extern void *pds_zalloc(unsigned int bufsize);
extern void *pds_calloc(unsigned int nitems,unsigned int itemsize);
extern void *pds_realloc(void *bufptr,unsigned int bufsize);
extern void pds_free(void *bufptr);

#define kmalloc(size,flags) pds_malloc(size)
#define kcalloc(n,size,flags) pds_calloc(n,size)
#define kzalloc(size,flags) pds_zalloc(size) /* zero */
#define kfree(p) pds_free(p)
#define vmalloc(size) pds_malloc(size)
#define vfree(p) pds_free(p)

#endif
