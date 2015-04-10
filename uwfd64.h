/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support UWFD64 modules.
*/
#ifndef UWFD64_H
#define UWFD64_H
//
//					A16D16 space - CPLD. 
//
//	D16 assumed, but only high byte matter. No hardware byte swap.
// 	Module address space is max 8 regs at A000 + (SERIAL << 4)
#define A16BASE 0xA000
#define A16STEP 0x10
#define A16UNIT 0
// register map
//              BASE+0: CSR[7:0] (RW) Control & Status register
//                                      CSR0 (RW) FLASH_CS, 1 - CS asserted if FLASH enabled, default 0
//                                      CSR1 (RW) FLASH_ENB, 1 - FLASH access enabled from CPLD side, default 0
//                                      CSR4 (RW) Xilix Configuration select: 
//                                                      0 - Xilinx Master (M=01, CPLD don't drive CCLK & DIN), 
//                                                      1 - Xilinx Slave  (M=11, CCLK & DIN driven by CPLD), 
//                                                      default 0, overriden by CSR1 (FLASH enable has priority)
//                                      CSR5 (RW) Xilinx PROG, 1 - PROG asserted, default 0
//                                      CSR6 (R)  Xilix INIT
//                                      CSR7 (R)  Xilix DONE, 1 - Configured
//              BASE+2: SDAT[7:0] (RW) Serial data register
//                                      W - any write causes generation of 8 SERCLK and shifts written data MSB first to FD0 (FLASH enabled) or FD1 (Xilinx slave)
//                                      R - after 8 FLASHCLK contains received data from FD1 (FLASH enabled) or undefined (Xilinx Slave)
//    BASE+4: SERIAL[7:0] (R) Serial Number, BASE=0xA000 + (SERIAL << 4)
//    BASE+6: BATCH[7:0] (R) Batch Number, informative, not decoded in address
//    BASE+8: C2X pins: C2X[5:0] "Geographic address assigned (with parity)" - for VME CSR address space, C2X[7] - reset, active low
struct uwfd64_a16_reg {
	volatile unsigned short csr;	// CPLD CSR
	volatile unsigned short sdat;	// Serial data
	volatile unsigned short snum;	// Module serial number
	volatile unsigned short bnum;	// Module batch number
	volatile unsigned short c2x;	// Geographic number
	volatile unsigned short reserved[3];
};
#define CPLD_CSR_FCS	1
#define CPLD_CSR_FENB	2
#define CPLD_CSR_XSLAVE	0x10
#define CPLD_CSR_XPROG	0x20
#define CPLD_CSR_XINIT	0x40
#define CPLD_CSR_XDONE	0x80
#define CPLD_C2X_GA     1
#define CPLD_C2X_PARITY 0x20
#define CPLD_C2X_RESET  0x80

//
//					A32D32 address space - module registers. 
//
//	Module address space is max 64kBytes at AA000000 + (GADDR << 16)
//	Hardware byte swap.
#define A32BASE 0xAA000000U
#define A32STEP 0x10000
#define A32UNIT 3

struct uwfd64_inout_reg {
	volatile unsigned int in;
	volatile unsigned int out;
};

#define MAIN_CSR_RESET 0x80000000

struct uwfd64_spi_reg {
	volatile unsigned int dat;	// only 2 low bytes used
	volatile unsigned int csr;	// only 2 low bytes used
};
// CSR bits:
// 7:0  - ~spi_cs (active high)
// 8    - =0 - data bits from master, =1 - to master
// 14:9 - reserved
// 15   - busy - operation in progress, read only
// DAT bits:
// 7:0  - data byte
// 14:8 - reserved
// 15   - busy, the same as in CSR
#define SPI_CSR_CS	1
#define SPI_CSR_DIR	0x100
#define SPI_CSR_BUSY	0x8000
#define SPI_TIMEOUT	100
#define SPI_ADDR_DIR	0x8000

// #define I2C_MASTER_SLAVE_PRERlo 0x0     // Clock prescaler register
// #define I2C_MASTER_SLAVE_PRERhi 0x1     // Clock prescaler register
// #define I2C_MASTER_SLAVE_CTR    0x2     // Control register
// #define I2C_MASTER_SLAVE_TXR    0x3     // Transmit register
// #define I2C_MASTER_SLAVE_RXR    0x3     // Recive register
// #define I2C_MASTER_SLAVE_CR     0x4     // Control register
// #define I2C_MASTER_SLAVE_SR     0x4     // Status register
// #define I2C_MASTER_SLAVE_SLADR  0x7     // Slave address register

//	8 byte registers. only low byte of 32-bit word works
struct uwfd64_i2c_reg {
	volatile unsigned int presc[2];
	volatile unsigned int ctr;
	volatile unsigned int dat;
	volatile unsigned int csr;
	volatile unsigned int reserved[3];
};

#define I2C_TIMEOUT		100
#define I2C_PRESC               0xC0

#define I2C_CTR_CORE_ENABLE     0x80
#define I2C_CTR_INTR_ENABLE     0x40
#define I2C_CTR_SLAVE_ENABLE    0x20

#define I2C_DAT_DDIR            1

#define I2C_SR_START            0x80
#define I2C_SR_STOP             0x40
#define I2C_SR_READ             0x20
#define I2C_SR_WRITE            0x10
#define I2C_SR_ACK              0x08
#define I2C_SR_SL_CONT          0x02
#define I2C_SR_IACK             0x01

#define I2C_SR_RXACK            0x80
#define I2C_SR_BUSY             0x40
#define I2C_SR_ARB_LOST         0x20
#define I2C_SR_SLAVE_MODE       0x10
#define I2C_SR_SLAVE_DATA_AVAIL 0x08
#define I2C_SR_SLAVE_DATA_REQ   0x04
#define I2C_SR_TRANSFER_IN_PRG  0x02
#define I2C_SR_IRQ_FLAG         0x01

//              0:      CSR (RW)
//                              CSR31   Recieve FIFO enable/reset, when 0 fifos do not accept data and
//                                              no arbitration is performed, at rising edge all pointers are initialized
//                              CSR30   Full reset of the module (MCB, WBRAM fsm, FIFOs) -- auto cleared
//                              CSR29   MCB and WBRAM fsm reset -- auto cleared
//                              CSR28   enable debug, when 1 WADR reads debug lines rather than last written block addr
//                              CSR7    SDRAM GTP area full -  no more blocks can be written from GTP
//                              CSR6    SDRAM GTP area emty -  no new data available for read
//                              CSR4    OR of CSR[3:0]
//                              CSR[3:0] (sticky) Recieve FIFO 3-0 overflow - packet missed, only cleared by asserting CSR30
//
//              4:              RADR    (RW)
//                              RADR[28:2]      must be set by readout procedure to indicate the last physical address that was already read by it
//                              RADR[1:0]       will be ignored and 00 used instead
//
//              8:              LIMR    (RW)
//                              LIMR[31:16]     upper 16bit of the address of the first 8K block following the recieving area
//                              LIMR[15:0]      upper 16bit of the address of the first 8K block of the recieving area
//
//              C:              WADR    (R)
//                              WADR[28:2]      physical addres of the first free cell after recieved block is written
//                              WADR[1:0]       always reads 00
//
//                              with CSR28=1 reads debug lines as indicated below

struct uwfd64_fifo_reg {
	volatile unsigned int csr;
	volatile unsigned int rptr;
	volatile unsigned int win;
	volatile unsigned int wptr;
};

#define FIFO_CSR_COVF	   0
#define FIFO_CSR_OVF    0x10
#define FIFO_CSR_EMPTY  0x40
#define FIFO_CSR_FULL   0x80
#define FIFO_CSR_DEBUG  0x10000000
#define FIFO_CSR_SRESET 0x20000000
#define FIFO_CSR_HRESET 0x40000000
#define FIFO_CSR_ENABLE 0x80000000

struct uwfd64_a32_reg {
	struct uwfd64_inout_reg csr;	// CSR
	struct uwfd64_inout_reg trig;	// Trigger production control
	struct uwfd64_spi_reg icx;	// inter Xilinx communication
	struct uwfd64_spi_reg dac;	// comon level DAC
	struct uwfd64_i2c_reg i2c;	// I2C master clock control (TI CDCUN1208LP)
	struct uwfd64_fifo_reg fifo;	// Memory fifo controller
	struct uwfd64_inout_reg ver;	// Version & test register
};

//	CDCUN1208LP definitions
#define CDCUN_ADDR              0x50
//	Output control registers
#define CDCUN_OUT_ADDR_ACLKA	7
#define CDCUN_OUT_ADDR_ACLKB	6
#define CDCUN_OUT_ADDR_ACLKC	5
#define CDCUN_OUT_ADDR_ACLKD	4
#define CDCUN_OUT_ADDR_EOCLK	3
#define CDCUN_OUT_ERC_MEDIUM	0x380
#define CDCUN_OUT_ERC_FAST	0x200
#define CDCUN_OUT_ERC_SLOW	0
#define CDCUN_OUT_DIFF_ON	0x18
#define CDCUN_OUT_DIFF_OFF	0
#define CDCUN_OUT_TYPE_LVDS	0
#define CDCUN_OUT_TYPE_CMOS	2
#define CDCUN_OUT_TYPE_HCSL	6
#define CDCUN_OUT_DISABLE	1
//	Input control rregister
#define CDCUN_INPUT_ADDR	11
#define CDCUN_INPUT_DIV8	0x30
#define CDCUN_INPUT_DIV4	0x20
#define CDCUN_INPUT_DIV2	0x10
#define CDCUN_INPUT_DIV1	0
#define CDCUN_INPUT_TYPE_HCSL	0xC
#define CDCUN_INPUT_TYPE_CMOS	4
#define CDCUN_INPUT_TYPE_LVDS	0
#define CDCUN_INPUT_MUX_INSEL	3
#define CDCUN_INPUT_MUX_SMART	2
#define CDCUN_INPUT_MUX_IN2	1
#define CDCUN_INPUT_MUX_IN1	0
//	Reset and Power down
#define CDCUN_CTRL_ADDR		15
#define CDCUN_CTRL_RESET	2
#define CDCUN_CTRL_PWRDN	1

//	A64D32/D64 address space - module SDRAM. Hardware byte swap.
//	Module address space is max 4 GByte at AAAAAA00_00000000 + (GADDR << 32)
#define A64BASE 0xAAAAAA0000000000ULL
#define A64STEP 0x100000000ULL
#define A64UNIT 2

class uwfd64 {
private:
	int serial;
	int ga;
	struct uwfd64_a16_reg *a16;
	struct uwfd64_a32_reg *a32;
public:
	uwfd64(int sernum, int gnum, unsigned short *space_a16, unsigned int *space_a32);
	int ConfigureMasterClock(int sel, int div, int erc = 0);
	int DACSet(int val);
	int I2CRead(int addr);
	int I2CWrite(int addr, int val);
	int ICXRead(int addr);
	int ICXWrite(int addr, int val);
	int Init(void);
	int IsHere(void);
	int IsDone(int wait = 0);
	inline int GetBatch(void) { return (a16->bnum >> 8) & 0xFF; };
	inline int GetGA(void) { return ga; };
	inline int GetSerial(void) { return serial; };
	inline int GetVersion(void) { return a32->ver.in; };
	int Prog(char *fname = NULL);
	void Reset(void);
	int TestReg32(int cnt);
};

#endif /* UWFD64_H */


