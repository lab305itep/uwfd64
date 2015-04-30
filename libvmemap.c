/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support for vme_user driver.
*/
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "libvmemap.h"

//#define DEBUG

#define MAXUNITS 8
static struct vmemap_struct Map[MAXUNITS] = 
	{{-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}};

/* Open VME and map particular window.
   Return pointer to mapped region. NULL on error */
unsigned int *vmemap_open(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr, 	// VME address
	unsigned long long size, 	// size of mapped region
	unsigned int aspace, 		// VME address space
	unsigned int cycle, 		// VME cycle type
	unsigned int dwidth		// VME data width
) {
	struct vme_master master;
	int rc;
    	unsigned offset;
	char str[128];

	if (unit >= MAXUNITS) return NULL;
	if (Map[unit].ptr != NULL) munmap(Map[unit].rptr, Map[unit].size);
	Map[unit].ptr = NULL;

	if (Map[unit].fd < 0) {	
		sprintf(str, "/dev/bus/vme/m%1.1d", unit);
		Map[unit].fd = open(str, O_RDWR);
		if (Map[unit].fd < 0) return NULL;
	}

#ifdef DEBUG
	printf("Unit %d - %s opened fd = %d\n", unit, str, Map[unit].fd);
#endif

    	offset = (vme_addr & 0xFFFF);
    	// We first adjust the window
	master.enable = 1;
    	master.aspace = aspace;
    	master.cycle = cycle;
    	master.dwidth = dwidth;
    	master.vme_addr = vme_addr - offset;
    	master.size = size + offset;
    	// Workaround for "Invalid PCI bound alignment"
    	if (master.size & 0xFFFF)
    	{
        	master.size += 0x10000 - (master.size & 0xFFFF);
    	}
	Map[unit].size = master.size;

    	rc = ioctl(Map[unit].fd, VME_SET_MASTER, &master);
    	if (rc != 0) {
		close(Map[unit].fd);
		Map[unit].fd = -1;
#ifdef DEBUG
		printf("Ioctl error %m\n");
#endif
        	return NULL;
    	}

    	Map[unit].rptr = mmap(NULL, master.size, PROT_READ | PROT_WRITE, MAP_SHARED, Map[unit].fd, 0);

    	if (Map[unit].rptr == MAP_FAILED) {
		Map[unit].ptr = NULL;
		close(Map[unit].fd);
		Map[unit].fd = -1;
        	return NULL;
	}
	Map[unit].ptr = (unsigned int*)((char*)Map[unit].rptr + offset);
	return Map[unit].ptr;
}

/* Unmap the region and close driver */
void vmemap_close(
	unsigned int *ptr		// Pointer returned by vmemap
) {
	int i;
	for (i=0; i<MAXUNITS; i++) if (Map[i].ptr == ptr) break;
	if (i == MAXUNITS) return;	// nothing to do
#ifdef DEBUG
	printf("unmapping entry %d: %p - %p (%Ld) fd = %d\n", i, Map[i].ptr, Map[i].rptr, Map[i].size, Map[i].fd);
#endif
	munmap(Map[i].rptr, Map[i].size);
	Map[i].ptr = NULL;
	if (Map[i].fd >= 0) close(Map[i].fd);
	Map[i].fd = -1;
}

/*	Open DMA channel. Return file descriptor	*/
int vmedma_open(void)
{
	return open("/dev/bus/vme/dma0", O_RDWR);
}

/*	Close dma channel.	*/
void vmedma_close(int fd)
{
	close(fd);
}

/* Read A64D32. Return the value if OK, -1 on error */
int vmemap_a64_read(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr 	// VME address
) {
	unsigned int *ptr;
	int r;
	ptr = vmemap_open(unit, vme_addr, sizeof(int), VME_A64, VME_USER | VME_DATA, VME_D32); 
	if (!ptr) return -1;
	r = *ptr;
	vmemap_close(ptr);
	return r;
}

/* Write A64D32. Return 0 if OK, negative number on error */
int vmemap_a64_write(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr, 	// VME address
	int data			// the data
) {
	unsigned int *ptr;
	ptr = vmemap_open(unit, vme_addr, sizeof(int), VME_A64, VME_USER | VME_DATA, VME_D32); 
	if (!ptr) return -1;
	*ptr = data;
	vmemap_close(ptr);
	return 0;
}

/* Read block A64D32. Return 0 if OK, -1 on error */
int vmemap_a64_blkread(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr, 	// VME address
	unsigned int *data,		// buffer for data
	int len				// length in bytes
) {
	unsigned int *ptr;
	int i;
	ptr = vmemap_open(unit, vme_addr, len, VME_A64, VME_USER | VME_DATA, VME_D32); 
	if (!ptr) return -1;
	for (i=0; i<len/4; i++) data[i] = ptr[i];
	vmemap_close(ptr);
	return 0;
}


/* Write block A64D32. Return 0 if OK, negative number on error */
int vmemap_a64_blkwrite(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr, 	// VME address
	unsigned int *data,		// the data
	int len				// length in bytes
) {
	unsigned int *ptr;
	int i;
	ptr = vmemap_open(unit, vme_addr, len, VME_A64, VME_USER | VME_DATA, VME_D32); 
	if (!ptr) return -1;
	for (i=0; i<len/4; i++) ptr[i] = data[i];
	vmemap_close(ptr);
	return 0;
}

/* Read/Write block A64D32 using DMA. Return 0 if OK, -1 on error */
int vmemap_a64_dma(
	int fd,				// DMA file 
	unsigned long long vme_addr, 	// VME address
	unsigned int *data,		// buffer for data
	int len,			// length in bytes
	int rw				// rw = 0 - read, rw = 1 - write
) {
	struct vme_dma_op dma;

	dma.aspace = VME_A64;
	dma.cycle = VME_USER | VME_DATA | VME_BLT;
	dma.dwidth = VME_D32;
	dma.vme_addr = vme_addr;
	dma.buf_vaddr = (unsigned long) data;
	dma.count = len;
	dma.write = rw;
	printf("vma_addr = %LX   user_addr = %LX   len = %X\n", dma.vme_addr, dma.buf_vaddr, dma.count);
	if (ioctl(fd, VME_DMA_OP, &dma) != len) return -1;
	return 0;
}

/* Sleep number of usec using nanosleep */
void vmemap_usleep(
	int usec
) {
	struct timespec tm;
	
	tm.tv_sec = usec / 1000000;
	tm.tv_nsec = (usec % 1000000) * 1000;
	nanosleep(&tm, NULL);
}

