#ifndef __DRV_DEFINE_H__
#define __DRV_DEFINE_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <asm/page.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/mm.h>

#define Drv_VERSION         "0x00000009"
#define DRIVER_AUTHOR       "Insyde"
#define DRIVER_DESC         "Insyde physical memory allocate driver"

// #define __DEBUG_MODE__
#ifdef __DEBUG_MODE__
  #define KDBG(m,...)      printk (KERN_DEBUG m, ##__VA_ARGS__)
#else
  #define KDBG(m,...)
#endif

#ifdef __STATIC_REGISTER
  #define NUMBER_MAJOR      231 //0xf2
  #define NUMBER_MINOR      0
#endif

static int gDeviceOpen = 0;
#ifndef __STATIC_REGISTER
  static int DrvMajor = 0, DrvMinor = 0;
  static dev_t DrvDev;
  static struct class *pDrvDevClass = NULL;
#endif
  
#ifndef LINUX_VERSION_CODE
  // Some linux distro, for example, Linpus with kernel 4.2.8 has duplicate version.h 
  // and it may have no LINUX_VERSION_CODE defined in another version.h
  // Include specific version.h while LINUX_VERSION_CODE isn't defined
  #include "/usr/include/linux/version.h"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
  static DEFINE_MUTEX( drv_mutex );
#endif

#pragma pack(1)

struct DrvDev_st {
  struct cdev cdev;
} st_DrvDev;

typedef struct _st_obj ST_OBJ;
struct _st_obj {
  ST_OBJ *pNext; // Next record, if "null" that's end of records.
  ST_OBJ *pLast; // Last record, if "null" that's first of records.
  unsigned int Index; // Allocate memory index for check and search with user mode information.
  unsigned long Size; // Allocated physical memory size
  unsigned long long KernelVirtualAddress; // Allocated physical memory virtual address in kernel space
  unsigned long long KernelLogicalAddress; // Allocated physical memory physical address in kernel space 
  unsigned char *pBuffer; // Virtual address for user space allocated memory
};
extern ST_OBJ *gpObjList;

#pragma pack()

inline unsigned long getargs (void *to, void *from, unsigned long n)
{
  return copy_from_user (to, from, n);
}

inline unsigned long setargs (void *to, void *from, unsigned long n)
{
  return copy_to_user(to, from, n);
}

static inline int DrvIoctl(unsigned int cmd, unsigned long arg);

static inline void* DrvKmalloc(size_t size)
{
  int    iOrder = -1;
  while ( (1 << ++iOrder)* PAGE_SIZE < size);
#ifdef __x86_64__
  return (void*)__get_free_pages( GFP_DMA32 | GFP_ATOMIC, (int)iOrder);
#else
  return (void*)__get_free_pages( GFP_ATOMIC, (int)iOrder);
#endif
}

static inline void DrvKFree(void *addr, size_t size)
{
  kfree(addr);
}

static inline void DrvFreePage(void *addr, size_t size)
{
  int    iOrder = -1;
  while ( (1 << ++iOrder)* PAGE_SIZE < size);
  free_pages((unsigned long)addr, iOrder);
}

static inline void DrvSleep(unsigned long ms)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION( 3, 0, 0 )
	msleep(ms);
#else
	usleep_range (100, 1000);
#endif
}

static int Drv_Open(struct inode *inode, struct file *file)
{
  int ret = 0;
  gDeviceOpen++;
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
  mutex_lock( &drv_mutex );
#endif
  ret = try_module_get(THIS_MODULE);
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
  mutex_unlock( &drv_mutex );
#endif

  if (ret == 0) {
    return DRV_BE_USED;
  }
  return DRV_SUCCESS;
}

static int Drv_Release(struct inode *inode, struct file *file)
{
  gDeviceOpen--;
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
  mutex_lock( &drv_mutex );
#endif
  module_put(THIS_MODULE);
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
  mutex_unlock( &drv_mutex );
#endif

  return DRV_SUCCESS;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
static long Drv_Ioctl(struct file *file, unsigned int num, unsigned long arg)
#else
static int Drv_Ioctl(struct inode *inode, struct file *file, unsigned int num, unsigned long arg)
#endif
{
  return DrvIoctl(num, arg);
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
static long Drv_Ioctl_Unlock(struct file *fp, unsigned int cmd, unsigned long arg)
{
    long            ret;
    mutex_lock( &drv_mutex );
    ret = Drv_Ioctl( fp, cmd, arg );
    mutex_unlock( &drv_mutex );
    return ret;
}
#endif

/*
 * Architectures vary in how they handle caching for addresses
 * outside of main memory.
 *
 */
#ifdef pgprot_noncached
static int uncached_access(struct file *file, phys_addr_t addr)
{
#if defined(CONFIG_IA64)
	/*
	 * On ia64, we ignore O_DSYNC because we cannot tolerate memory
	 * attribute aliases.
	 */
	return !(efi_mem_attributes(addr) & EFI_MEMORY_WB);
#elif defined(CONFIG_MIPS)
	{
		extern int __uncached_access(struct file *file,
					     unsigned long addr);

		return __uncached_access(file, addr);
	}
#else
	/*
	 * Accessing memory above the top the kernel knows about or through a
	 * file pointer
	 * that was marked O_DSYNC will be done non-cached.
	 */
	if (file->f_flags & O_DSYNC)
		return 1;
	return addr >= __pa(high_memory);
#endif
}
#endif

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
                              unsigned long size, pgprot_t vma_prot)
{
#ifdef pgprot_noncached
	phys_addr_t offset = pfn << PAGE_SHIFT;

	if (uncached_access(file, offset))
		return pgprot_noncached(vma_prot);
#endif
	return vma_prot;
}

#ifndef CONFIG_MMU
/* can't do an in-place private mapping if there's no MMU */
static inline int private_mapping_ok(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_MAYSHARE;
}
#else
static inline int private_mapping_ok(struct vm_area_struct *vma)
{
	return 1;
}
#endif

static const struct vm_operations_struct mmap_mem_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys
#endif
};

static int Drv_Map ( struct file *file, struct vm_area_struct *vma )
{
	size_t size = vma->vm_end - vma->vm_start;
	phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;

	/* Does it even fit in phys_addr_t? */
	if (offset >> PAGE_SHIFT != vma->vm_pgoff)
		return -EINVAL;

	/* It's illegal to wrap around the end of the physical address space. */
	if (offset + (phys_addr_t)size - 1 < offset)
		return -EINVAL;

	if (!private_mapping_ok(vma))
		return -ENOSYS;

	vma->vm_page_prot = phys_mem_access_prot(file, vma->vm_pgoff,
						 size,
						 vma->vm_page_prot);

	vma->vm_ops = &mmap_mem_ops;

	/* Remap-pfn-range will mark the range VM_IO */
	if (remap_pfn_range(vma,
			    vma->vm_start,
			    vma->vm_pgoff,
			    size,
			    vma->vm_page_prot)) {
		return -EAGAIN;
	}
	return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = Drv_Open,
    #if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
      .unlocked_ioctl = Drv_Ioctl_Unlock,
      .compat_ioctl = Drv_Ioctl_Unlock, // To allow 32-bits userland programs to make ioctl calls on a 64-bits kernel.
    #else
      .ioctl = Drv_Ioctl,
    #endif
    .release = Drv_Release,
    .mmap   = Drv_Map,
};

#ifdef __STATIC_REGISTER
//Driver initialization
static int __init Init_Drv(void)
{
  if (register_chrdev (NUMBER_MAJOR, DEVICE_NAME, &fops) < 0)
    return DRV_INITIAL_FAIL;

  KDBG ("IOCTL_ALLOCATE_MEMORY=0x%x\n", IOCTL_ALLOCATE_MEMORY);
  KDBG ("IOCTL_FREE_MEMORY=0x%x\n", IOCTL_FREE_MEMORY);
  KDBG ("IOCTL_WRITE_MEMORY=0x%x\n", IOCTL_WRITE_MEMORY);
  KDBG ("IOCTL_READ_MEMORY=0x%x\n", IOCTL_READ_MEMORY);
  KDBG ("IOCTL_READ_VERSION=0x%x\n", IOCTL_READ_VERSION);
  KDBG ("IOCTL_GET_ALLOCATED_QUENTITY=0x%x\n", IOCTL_GET_ALLOCATED_QUENTITY);

  return 0;
}
static void __exit Cleanup_Drv(void)
{
    unregister_chrdev (NUMBER_MAJOR, DEVICE_NAME);
}
#else

static void cleanup(int created)
{
    if (created) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
        class_device_destroy (pDrvDevClass, DrvDev);
#else
        device_destroy (pDrvDevClass, DrvDev);
#endif
        cdev_del(&st_DrvDev.cdev);
    }
    if (pDrvDevClass)
        class_destroy(pDrvDevClass);
    if (MKDEV (DrvMajor, 0) != -1)
        unregister_chrdev_region(MKDEV (DrvMajor, 0), 1);
}

//Driver initialization
static int __init Init_Drv(void)
{
  dev_t devno;
  struct device *class_dev = NULL;
  int created = 0;

  if (DrvMajor) {
    devno = MKDEV (DrvMajor, 0);
    if (register_chrdev_region (devno, 1, DEVICE_NAME) < 0)
      goto error;
  } else {
    if (alloc_chrdev_region (&devno, 0, 1, DEVICE_NAME) < 0)
      goto error;
  }

  DrvMajor = MAJOR (devno);
  DrvMinor = MINOR (devno);

  // Create device node
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,3,13)
  pDrvDevClass = class_create (THIS_MODULE, DEVICE_NAME);
#else
  pDrvDevClass = class_create (THIS_MODULE->name);
#endif
  if (IS_ERR (pDrvDevClass))
    goto error;

  DrvDev = MKDEV (DrvMajor, DrvMinor);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
  class_dev = class_device_create ((struct class *)pDrvDevClass, (struct class_device *)NULL, DrvDev, (struct device *)NULL, DEVICE_NAME);
#else
  class_dev = device_create (pDrvDevClass, NULL, DrvDev, NULL, DEVICE_NAME);
#endif

  created = 1;
  // Initial cdev
  cdev_init (&st_DrvDev.cdev, &fops);
  st_DrvDev.cdev.owner = THIS_MODULE;
  st_DrvDev.cdev.ops = &fops;

  // Regist device
  if (cdev_add (&st_DrvDev.cdev, devno, 1))
    goto error; 

  KDBG ("IOCTL_ALLOCATE_MEMORY=0x%x\n", IOCTL_ALLOCATE_MEMORY);
  KDBG ("IOCTL_FREE_MEMORY=0x%x\n", IOCTL_FREE_MEMORY);
  KDBG ("IOCTL_WRITE_MEMORY=0x%x\n", IOCTL_WRITE_MEMORY);
  KDBG ("IOCTL_READ_MEMORY=0x%x\n", IOCTL_READ_MEMORY);
  KDBG ("IOCTL_READ_VERSION=0x%x\n", IOCTL_READ_VERSION);
  KDBG ("IOCTL_GET_ALLOCATED_QUENTITY=0x%x\n", IOCTL_GET_ALLOCATED_QUENTITY);

  return DRV_SUCCESS;
error:
  cleanup(created);
  return DRV_INITIAL_FAIL;
}

static void __exit Cleanup_Drv(void)
{
  ST_OBJ *pObj = NULL;
  ST_OBJ *pNext = NULL;
  int    iOrder = -1;

  if (gDeviceOpen) {
    return;
  } else {
    if (gpObjList) {
      pObj = gpObjList;
      while(true) {
        pNext = pObj->pNext;
        while ( (1 << ++iOrder)* PAGE_SIZE < pObj->Size);
        free_pages ((unsigned long)pObj->pBuffer, iOrder);
        vfree(pObj);
        if (pNext==NULL) {
          break;
        } else {
          pObj = pNext;
        }
      }
      gpObjList = NULL;
    }
  }

  cleanup(1);
}
#endif


module_init( Init_Drv);
module_exit( Cleanup_Drv);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_VERSION( Drv_VERSION );
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,12,0)
MODULE_SUPPORTED_DEVICE( "Insyde" );
#endif
MODULE_LICENSE( "GPL" );

#ifdef __cplusplus
}
#endif


#endif
