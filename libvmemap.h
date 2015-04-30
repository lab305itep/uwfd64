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

/* Read block A64D32 using DMA. Return 0 if OK, -1 on error */
int vmemap_a64_dmaread(
	unsigned long long vme_addr, 	// VME address
	unsigned int *data,		// buffer for data
	int len				// length in bytes
);

/* Write block A64D32 using DMA. Return 0 if OK, negative number on error */
int vmemap_a64_dmawrite(
	unsigned long long vme_addr, 	// VME address
	unsigned int *data,		// the data
	int len				// length in bytes
);


/* Sleep number of usec using nanosleep */
void vmemap_usleep(
	int usec
);

#ifdef __cplusplus
}
#endif

#endif /* LIBVMEMAP_H */

