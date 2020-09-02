/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support for vme_user driver.
*/
#ifndef LIBVMEMAP_H
#define LIBVMEMAP_H

#include "vme_user.h"

struct vmemap_struct {
	int fd;				// file descriptor
	unsigned int *ptr;		// pointer returned
	unsigned int *rptr;		// real pointer
	unsigned long long size;	// size
};

#ifdef __cplusplus
extern "C" {
#endif

/* Open VME and map particular window.
   Return pointer to mapped region. NULL on error */
unsigned *vmemap_open(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr, 	// VME address
	unsigned long long size, 	// size of mapped region
	unsigned int aspace, 		// VME address space
	unsigned int cycle, 		// VME cycle type
	unsigned int dwidth		// VME data width
);

/* Unmap the region and close driver */
void vmemap_close(
	unsigned int *ptr		// Pointer returned by vmemap
);

/*	Open DMA channel. Return file descriptor	*/
int vmedma_open(void);

/*	Close dma channel.	*/
void vmedma_close(int fd);

/* Read A64D32. Return the value if OK, -1 on error */
int vmemap_a64_read(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr 	// VME address
);

/* Write A64D32. Return 0 if OK, negative number on error */
int vmemap_a64_write(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr, 	// VME address
	int data			// the data
);

/* Read block A64D32. Return 0 if OK, -1 on error */
int vmemap_a64_blkread(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr, 	// VME address
	unsigned int *data,		// buffer for data
	int len				// length in bytes
);

/* Write block A64D32. Return 0 if OK, negative number on error */
int vmemap_a64_blkwrite(
	unsigned int unit, 		// Tundra master window.
	unsigned long long vme_addr, 	// VME address
	unsigned int *data,		// the data
	int len				// length in bytes
);

/* Read/write block A64D32 using DMA. Return 0 if OK, -1 on error */
int vmemap_a64_dma(
	int fd,				// DMA file 
	unsigned long long vme_addr, 	// VME address
	unsigned int *data,		// buffer for data
	int len,			// length in bytes
	int rw				// rw = 0 - read, rw = 1 - write
);

/* Read/write block A32D32 using DMA & BLT. Return 0 if OK, -1 on error */
int vmemap_a32_dma(
	int fd,				// DMA file 
	unsigned long vme_addr, 	// VME address
	unsigned int *data,		// buffer for data
	int len,			// length in bytes
	int rw				// rw = 0 - read, rw = 1 - write
);

/* Sleep number of usec using nanosleep */
void vmemap_usleep(
	int usec
);

#ifdef __cplusplus
}
#endif

#endif /* LIBVMEMAP_H */

