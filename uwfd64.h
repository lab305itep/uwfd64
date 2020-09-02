/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support UWFD64 modules.
*/
#ifndef UWFD64_H
#define UWFD64_H

#include <libconfig.h>

#define MAX_PATH_LEN	1024

//************************************************************************************************************************************************************************//
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
#define CPLD_C2X_GAMASK 0x1F
#define CPLD_C2X_PARITY 0x20
#define CPLD_C2X_RESET  0x80

//************************************************************************************************************************************************************************//
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

//              This is a CSR regiter, with main purpose to make commutations between
//              CSR[2:0]  defines connection of trigger:
//                      0 - I->I        internal trigger to channels
//                      1 - I->FI       internal trigger to channels and front panel
//                      2 - I->BI       internal trigger to channels and back panel
//                      3 - I->FBI      internal trigger to channels, front and back panels
//                      4 - F->I                front panel trigger to channels
//                      5 - F->BI       front panel trigger to channels and back panel
//                      6 - B->I                back panel trigger to channels
//                      7 - B->FI       back panel trigger to channels and front panel
//              CSR[3]   if 1, channels do not accept trigger
//              CSR[6:4]  defines connection of INH similarly to trigger connections
//              CSR[7]   if 1, channels are insensitive to INH
//              CSR[10:8] defines connection of clocks similarly to trigger connections
//                      in addition, programming of CDCUN required:
//                      for modes 0-3 IN1 of CDCUN must be selected
//                      for modes 4-7 IN2 of CDCUN must be selected
//		CSR[11]	test pulse generation enable (makes ~100 Hz ~1 us pulses on ICX[7])
//		CSR[12]	enable trigger+inhibit mixture on FP[5:4]
//		CSR[13]	enable token sychro blocks (type=5).
//              CSR[15:14]       general purpose CSR outputs
//              CSR[30:16]       user word to be put to trigger block written to memory  
//              CSR+4 [31:0] are general inputs from the upper hierarchy, unused so far
//

struct uwfd64_csr_reg {
	volatile unsigned int out;
	volatile unsigned int in;
};

#define MAIN_MUX_I2I	0
#define MAIN_MUX_I2FI	1
#define MAIN_MUX_I2BI	2
#define MAIN_MUX_I2FBI	3
#define MAIN_MUX_F2I	4
#define MAIN_MUX_F2BI	5
#define MAIN_MUX_B2I	6
#define MAIN_MUX_B2FI	7
#define MAIN_MUX_MASK	7

#define MAIN_CSR_TRG	1
#define MAIN_CSR_BLKTRG	8
#define MAIN_CSR_INH	0x10
#define MAIN_CSR_BLKINH	0x80
#define MAIN_CSR_CLK	0x100
#define MAIN_CSR_TPULSE	0x800
#define MAIN_CSR_AUXOUT 0x1000
#define MAIN_CSR_TOKSYNC 0x2000
#define MAIN_CSR_USER	0x10000
#define MAIN_CSR_USER_MASK	0x7FFF0000
#define MAIN_CSR_RESET 	0x80000000U

//	Trigger generation module
//              Registers:
//              0 CSR (RW):
//                      3:0   - corrsponding chan FPGA trigger enable
//                      6:4     - 1 + time in clocks to accumulate OR of all trigger sources
//                      15:7  - 1 + trigger blocking time (125MHz ticks), actual blocking time 
//                                              cannot be less than token transmission time 14*TOKEN_CLKDIV clocks
//                      28:16 - period in ms of the soft trigger generation, zero value means no periodical soft trigger, not generated on INH
//                      30:29 - unused
//                      31              - inhibit, set on power on
//              1 TRGCNT        (R) :
//                      counts issued master triggers when not inhibited, 11 LSB are used as trigger token
//                      any write to this register produced master soft trigger
//              2 MISCNT        (R) :
//                      counts triggers that appear during blocking time (when not inhibited)
//              3 GTIME (R) :
//                      middle bits of 45 bit global 125 MHz time counter. One time unit is 1.024 us.
//                      Counting paused on active inhibit
//                      Any write to GTIME or MISCNT cases reset of all 3 counters
//
//              Block sent to memory fifo
//              0       10SC CCCL LLLL LLLL - S - soft trigger, CCCC - channel mask which produced the trigger, 
//                                                                               L=7 - data length in 16-bit words not including CW 
//      1       0ttt pnnn nnnn nnnn - ttt - trigger block type (3'b010 = 2 - trigger info block)
//                                                                               n - 11-bit trigger token = trigger number LSB, p - token parity = NOT (xor n)
//              2       0uuu uuuu uuuu uuuu - user word
//              3       0 GTIME[14:0]             - lower GTIME
//              4       0 GTIME[29:15]            - middle GTIME
//              5       0 GTIME[44:30]            - higher GTIME
//              6  	0 TRIGCNT[14:0]        - lower trigger counter, 11 LSB coinside with token
//              7  	0 TRIGCNT[29:15]       - higher trigger counter

struct uwfd64_trig_reg {
	volatile unsigned int csr;
	volatile unsigned int cnt;
	volatile unsigned int miscnt;
	volatile unsigned int gtime;
};

#define TRIG_CSR_CHAN		1
#define TRIG_CSR_CHAN_MASK	0xF
#define TRIG_CSR_SRCOR		0x10
#define TRIG_CSR_SRCOR_MASK	0x7F0
#define TRIG_CSR_BLOCK		0x80
#define TRIG_CSR_BLOCK_MASK	0xFF80
#define TRIG_CSR_SOFT		0x10000
#define TRIG_CSR_SOFT_MASK	0x1FFF0000
#define TRIG_CSR_AUXIN		0x20000000
#define TRIG_CSR_TRIG2FIFO	0x40000000
#define TRIG_CSR_INHIBIT	0x80000000U

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

#define I2C_TIMEOUT		500
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

//      Registers:      (bits not mentioned are not used)
//              0:      CSR (RW)
//                              CSR31   (RW)    Recieve FIFO enable/reset, when 0 fifos do not accept data and
//                                              no arbitration is performed, all pointers are kept initialized
//                              CSR30   (RWC) Full reset of the module (MCB, WBRAM fsm, FIFOs) -- auto cleared
//                              CSR29   (RWC) MCB and WBRAM fsm reset -- auto cleared
//                              CSR28   (RW)  enable debug, when 1 WADR reads debug lines rather than last written block addr
//                              CSR27   (R)     arbitter paused because of errors or memory full
//                              CSR26   (R)     RADR invalid: specified RADR is beyond limits set by LIMR
//                              CSR[23:20] (R)  same as CSR[7:4] for fifo 4
//                              CSR[19:16] (R)  same as CSR[7:4] for fifo 3
//                              CSR[15:12] (R)  same as CSR[7:4] for fifo 2
//                              CSR[11:8]  (R)  same as CSR[7:4] for fifo 1
//                              CSR7    (R)     (sticky) fifo 0 underrun error: CW recieved when not expected
//                              CSR6    (R)     (sticky) fifo 0 overrun error: no CW recieved when expected
//                              CSR5    (R)     (sticky) fifo 0 missed a block because it's full
//                              CSR4    (R)     fifo 0 empty
//                              CSR3    (R)     arbitter underrun error: CW recieved earlier than expected, RECIEVING STOPPED
//                              CSR2    (R)     arbitter overrunrun error: no CW recieved when expected, RECIEVING STOPPED
//                              CSR1    (R)   SDRAM GTP area full -  no more blocks can be written from GTP
//                              CSR0    (R)   SDRAM GTP area emty -  no new data available for read
//
//              4:              RADR    (RW)
//                              RADR[28:2]      must be set by readout procedure to indicate the last physical address that was already read by it
//                              RADR[1:0]       will be ignored and 00 used instead
//
//              8:              LIMR    (RW)
//                              LIMR[31:16]     upper 16bit of the address of the first 8K block following the recieving area
//                              LIMR[15:0]      upper 16bit of the address of the first 8K block of the recieving area
//                              writing to this reg must be done when fifo is disabled with CSR31=0, otherwise writing is ignored
//
//              C:              WADR    (R)
//                              WADR[28:2]      physical addres of the first free cell after recieved block is written
//                              WADR[1:0]       always reads 00
//
//                              with CSR28=1 reads debug lines as indicated in memory.v

struct uwfd64_fifo_reg {
	volatile unsigned int csr;
	volatile unsigned int rptr;
	volatile unsigned int win;
	volatile unsigned int wptr;
};

#define FIFO_CSR_EMPTY		1
#define FIFO_CSR_FULL   	2 
#define FIFO_CSR_OVERRUN	4
#define FIFO_CSR_UNDERRUN	8
#define FIFO_CSR_SHIFT		4
#define FIFO_CSR_ERROR		0xEEEEEE
#define FIFO_CSR_RADRERROR	0x4000000
#define FIFO_CSR_PAUSE		0x8000000
#define FIFO_CSR_DEBUG  	0x10000000
#define FIFO_CSR_SRESET 	0x20000000
#define FIFO_CSR_HRESET 	0x40000000
#define FIFO_CSR_ENABLE 	0x80000000

struct uwfd64_a32_reg {
	struct uwfd64_csr_reg csr;	// CSR
	struct uwfd64_inout_reg ver;	// Version & test register
	struct uwfd64_trig_reg trig;	// Trigger production control
	struct uwfd64_fifo_reg fifo;	// Memory fifo controller
	struct uwfd64_spi_reg icx;	// inter Xilinx communication
	struct uwfd64_spi_reg dac;	// comon level DAC
	struct uwfd64_i2c_reg i2c;	// I2C master clock control (TI CDCUN1208LP)
};

#define UWFD64_A32_FIFO	0x8000		// shift to FIFO access to SDRAM in A32 address space

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

//************************************************************************************************************************************************************************//
//
//	A64D32/D64 address space - module SDRAM. Hardware byte swap.
//	Module address space is max 4 GByte at AAAAAA00_00000000 + (GADDR << 32)
#define A64BASE 0xAAAAAA0000000000ULL
#define A64STEP 0x100000000ULL
#define A64UNIT 2
#define MEMSIZE 0x20000000	// bytes
#define MBYTE   0x100000
//#define MBYTE   0x20

//************************************************************************************************************************************************************************//
//
//	Slave Xilinxes address map
//
#define ICX_SLAVE_STEP		0x2000
#define ICX_SLAVE_CSR_IN	0
#define ICX_SLAVE_CSR_OUT	1
#define ICX_SLAVE_VER_IN	2
#define ICX_SLAVE_VER_OUT	3
#define ICX_SLAVE_ERR1		4
#define ICX_SLAVE_ERR2		5
#define ICX_SLAVE_SPI_DAT	6
#define ICX_SLAVE_SPI_CSR	7
#define ICX_SLAVE_I2C_PRCL	8
#define ICX_SLAVE_I2C_PRCH	9
#define ICX_SLAVE_I2C_CTR	10
#define ICX_SLAVE_I2C_DAT	11
#define ICX_SLAVE_I2C_CSR	12

#define ICX_SLAVE_COEF		16	// coefficiences for trigger production

#define ICX_SLAVE_MTMASK	32	// main trigger channel mask (1 = disable)
#define ICX_SLAVE_STMASK	33	// self trigger channel mask (1 = disable)
#define ICX_SLAVE_SUMASK	34	// summ channel mask (1 = disable)
#define ICX_SLAVE_INVMASK	35	// invert channel mask (0 = pulses go up)
#define ICX_SLAVE_MTTHR		36	// master trigger zero suppression threshold
#define ICX_SLAVE_STTHR		37	// self trigger threshold
#define ICX_SLAVE_SUTHR		38	// master trigger sum 64 production threshold
#define ICX_SLAVE_STPRC		39      // selftrigger prescale
#define ICX_SLAVE_WINLEN	40      // window length for both triggers and trigger history
#define ICX_SLAVE_MTWINBEG      41      // master trigger window begin
#define ICX_SLAVE_STWINBEG      42      // self trigger window begin
#define ICX_SLAVE_SUWINBEG      43      // trigger history window begin
#define ICX_SLAVE_SUDELAY       44      // delay of local sum for adding to other X's
#define ICX_SLAVE_MTZBEG	45	// zero suppression window begin (from MTWINBEG)
#define ICX_SLAVE_MTZEND	46	// zero suppression window end (from MTWINBEG)

#define ICX_SLAVE_PED		48	// pedestals

#define ICX_SLAVE_ADC		64	// start of ADC recievers reg array
#define ICX_SLAVE_ADC_STEP	16	// shift to the next ADC in this array
#define ICX_SLAVE_ADC_CSR	0	// relative shifts to ADC reciever regs in each ADC part
#define ICX_SLAVE_ADC_CFRQ	1	// ADC clock freq counter, high bits
#define ICX_SLAVE_ADC_CERR	2	// 4 regs of ADC test data error counters
#define ICX_SLAVE_ADC_IBS	6	// ADC bitslip sticky indicators
#define ICX_SLAVE_ADC_CINS	7	// 9 regs ADC bit line instability counters, frame and 8 bit lines

#define SLAVE_CSR_TMODE		1
#define SLAVE_CSR_TLEN		0x10
#define SLAVE_CSR_TSTART	0x80
#define SLAVE_CSR_PEDMODE	0x1000
#define SLAVE_CSR_PEDINHIBIT	0x2000
#define SLAVE_CSR_HISTENABLE	0x4000
#define SLAVE_CSR_RAW		0x8000


#define ICX_ERR2_HISTMISS	1
#define ICX_ERR2_ARBOVR		0x4000
#define ICX_ERR2_ARBUNDR	0x8000

#define SLAVE_ADCCSR_DMASK	0x1FF	// bit mask for IODELAY increments
#define SLAVE_ADCCSR_DINC	0x200	// IODELAY increment command
#define SLAVE_ADCCSR_DRST	0x400	// IODELAY reset command
#define SLAVE_ADCCSR_DCAL	0x800	// IODELAY calibrate command
#define SLAVE_ADCCSR_BSRST	0x1000	// SERDES bitslip logic reset
#define SLAVE_ADCCSR_BSENB	0x2000	// enable individual bitline bitslip
#define SLAVE_ADCCSR_MBSENB	0x4000	// enable master bitslip for all bits based on frame

#define L2C_TIMEOUT		100

//	ADC registers
#define ADC_REG_CFG		0
#define ADC_CFG_SDO		0x81
#define	ADC_CFG_LSB		0x42
#define ADC_CFG_RESET		0x24

#define ADC_REG_ID		1
#define ADC_REG_GRADE		2

#define ADC_REG_MASK		5
#define ADC_MASK_FCO		0x20
#define ADC_MASK_DCO		0x10
#define ADC_MASK_D		8
#define ADC_MASK_C		4
#define ADC_MASK_B		2
#define ADC_MASK_A		1

#define ADC_REG_PWR		8
#define ADC_PWR_PIN		0x20
#define ADC_PWR_DOWN		1
#define ADC_PWR_STANDBY		2
#define ADC_PWR_RESET		3

#define ADC_REG_CLK		9
#define ADC_CLK_STBON		1

#define ADC_REG_DIV		11

#define ADC_REG_ECTL		12
#define ADC_ECTL_CHOP		4

#define ADC_REG_TEST		13
#define ADC_TEST_ALTER		0x40
#define ADC_TEST_ONCE		0x80
#define ADC_TEST_AONCE		0xC0
#define ADC_TEST_RLONG		0x20
#define ADC_TEST_RSHORT		0x10
#define ADC_TEST_MSHORT		1
#define ADC_TEST_FSPOS		2
#define ADC_TEST_FSNEG		3
#define ADC_TEST_CHECKBRD	4
#define ADC_TEST_PN23		5
#define ADC_TEST_PN9		6
#define ADC_TEST_WTOGGLE	7
#define ADC_TEST_USER		8
#define ADC_TEST_BTOGGLE	9
#define ADC_TEST_SYNC		10
#define ADC_TEST_BIT		11
#define ADC_TEST_MIXED		12

#define ADC_REG_OFFSET		0x10

#define ADC_REG_OUTPUT		0x14
#define ADC_OUTPUT_IEEE		0x40
#define ADC_OUTPUT_INVERT	4
#define ADC_OUTPUT_COMPL	1

#define ADC_REG_ADJUST		0x15
#define ADC_ADJUST_200		0x10
#define ADC_ADJUST_100		0x20
#define ADC_ADJUST_2X		1

#define ADC_REG_PHASE		0x16

#define ADC_REG_VREF		0x18
#define ADC_VREF_1_00VPP	0
#define ADC_VREF_1_14VPP	1
#define ADC_VREF_1_33VPP	2
#define ADC_VREF_1_60VPP	3
#define ADC_VREF_2_00VPP	4

#define ADC_REG_PAT1L		0x19
#define ADC_REG_PAT1H		0x1A
#define ADC_REG_PAT2L		0x1B
#define ADC_REG_PAT2H		0x1C

#define ADC_REG_OCTL		0x21
#define ADC_OCTL_LSB		0x80
#define ADC_OCTL_SDRBIT		0
#define ADC_OCTL_SDRBYTE	0x10
#define ADC_OCTL_DDRBIT		0x20
#define ADC_OCTL_DDRBYTE	0x30
#define ADC_OCTL_SDRONE		0x40

#define ADC_REG_CCTL		0x22
#define ADC_CCTL_RESET		2
#define ADC_CCTL_PWDOWN		1

#define ADC_REG_OVERRIDE	0xFF
#define ADC_OVERRIDE_FIRE	1

#define ADC_REG_CONTROL		0x100
#define ADC_CONTROL_OVERRIDE	0x40
#define ADC_CONTROL_12BIT	0x20
#define ADC_CONTROL_10BIT	0x30
#define ADC_CONTROL_20MSPS	0
#define ADC_CONTROL_40MSPS	1
#define ADC_CONTROL_50MSPS	2
#define ADC_CONTROL_65MSPS	3
#define ADC_CONTROL_80MSPS	4
#define ADC_CONTROL_105MSPS	5
#define ADC_CONTROL_125MSPS	6

#define ADC_REG_IOCTL2		0x101
#define ADC_IOCTL12_SDIOPD	1

#define ADC_REG_IOCTL3		0x102
#define ADC_IOCTL3_VCMPD	8

#define ADC_REG_SYNC		0x109
#define ADC_SYNC_ENABLE		2
#define ADC_REG_NEXTONLY	1


// Si5338 registers
#define SI5338_ADDR		0xE0

#define SI5338_REG_FCALOVRL	45
#define SI5338_REG_FCALOVRM	46
#define SI5338_REG_FCALOVRH	47
#define SI5338_FCALOVRH_CONST	0x14

#define SI5338_REG_CTRL		49
#define SI5338_CTRL_FCALOVR	0x80
#define SI5338_CTRL_VCOGAIN	0x10
#define SI5338_CTRL_RSEL	4
#define SI5338_CTRL_BWSEL	1

#define SI5338_REG_STATUS	218
#define SI5338_STATUS_LOL	0x10
#define SI5338_STATUS_FDBK	8
#define SI5338_STATUS_CLKIN	4
#define SI5338_STATUS_SYSCAL	1

#define SI5338_REG_RESET	226
#define SI5338_RESET_MS		4

#define SI5338_REG_OUT		230
#define SI5338_OUT_DISABLE_ALL	0x1F
#define SI5338_OUT_DISABLE_G	0x10
#define SI5338_OUT_DISABLE_D	8
#define SI5338_OUT_DISABLE_C	4
#define SI5338_OUT_DISABLE_B	2
#define SI5338_OUT_DISABLE_A	1

#define SI5338_REG_FCALL	235
#define SI5338_REG_FCALM	236
#define SI5338_REG_FCALH	237

#define SI5338_REG_LOL		241
#define SI5338_LOL_DISABLE	0x80
#define SI5338_LOL_CONST	0x65

#define SI5338_REG_SRESET	246
#define SI5338_SRESET_RESET	2

#define SI5338_REG_STKSTAT	247

#define SI5338_REG_PAGE		255
#define SI5338_PAGE_SEL		1

#define SI5338_TIMEOUT		100

//************************************************************************************************************************************************************************//
//	Only supported so far transports here
enum UWFD64_BLK_TRANSPORT {
	UWFD64_BLK_AUTO = -1,		// block transport auto select trying controller and firmware
	UWFD64_BLK_A64_BLT = 0,		// block transfere in A64 address space, DMA, 32 bit data
	UWFD64_BLK_A64_MAP = 1,		// A64 mapped, no DMA, 32-bit data
	UWFD64_BLK_A32_BLT = 100	// block transfere in A32 address space, DMA, 32 bit data
};

struct uwfd64_module_config {
	enum UWFD64_BLK_TRANSPORT blk_transp;	// transport for block operations
	int MasterClockMux;	// master clock multiplexer setting 
	int MasterTrigMux;	// master trigger multiplexer setting 
	int MasterInhMux;	// master inhibit multiplexer setting
	int MasterClockDiv;	// Mater clock divider (0 - 1, 1 - 2, 2 - 4, 3 - 8)
	int MasterClockErc;	// Mater clock edge control (0 - fast, 1 - medium, 2 - slow)
	int AuxTrigOut;		// Enable trigger+inhibit output on FP auxillary pair
	int AuxTrigIn;		// Enable auxillary trigger input
	int MasterTrig2FIFO;	// Enable trigger blocks (type 2) to data FIFO
	int TokenSync;		// Enable type 5 records
	int DAC;		// DAC setting
	int TrigGenMask;	// Mask of slave xilinxes participating in trigger generation
	int TrigOrTime;		// Number of clocks to OR trigger sources
	int TrigBlkTime;	// Number of clocks to block trigger production
	int TrigUserWord;	// 15-bit user word to be put to trigger block
	int FifoBegin;		// Main FIFO start address in 8k blocks
	int FifoEnd;		// Main FIFO end address in 8k blocks
	int IODelay;		// ADC data delay in calibrated delay taps
	int TrigHistMask;	// Mask for slave Xilinxes history block
	int ZeroSupThreshold;	// Threshold for zero suppression in master trigger
	int SelfTrigThreshold;	// Threshold for self trigger
	int MasterTrigThreshold;	// Threshold for master trigger production
	int SelfTriggerPrescale;	// Self trigger prescale
	int WinLen;		// Waveform window length
	int TrigWinBegin;	// Master trigger window begin delay
	int SelfWinBegin;	// Self trigger window begin delay
	int SumWinBegin;	// Trigger sum window begin delay
	int SumDelay;		// Delay local channels for global sum
	int ZSWinBegin;		// Zero suppression window begin (from TrigWinBegin)		
	int ZSWinEnd;		// Zero suppression window end (from TrigWinBegin)
	float TrigCoef[64];	// Coefficients for trigger production
	short int MainTrigMask[4];	// Mask channels from master trigger
	short int SelfTrigMask[4];	// Mask channels from self trigger
	short int TrigSumMask[4];	// Mask channels from trigger production sum
	short int InvertMask[4];	// Mask channels for invertion
	char SlaveClockFile[MAX_PATH_LEN];	// Si5338 .h configuration file
};

//************************************************************************************************************************************************************************//
class uwfd64_tool;

class uwfd64 {
private:
	int serial;
	int ga;
	struct uwfd64_a16_reg *a16;
	struct uwfd64_a32_reg *a32;
	int dma_fd;
	
	struct uwfd64_module_config Conf;
public:
	uwfd64(int sernum, int gnum, unsigned short *space_a16, unsigned int *space_a32, int fd, config_t *cnf = NULL);
	int ADCRead(int num, int addr);
	int ADCWrite(int num, int addr, int val);
	int ADCCheckSeq(int time, int xilmask);
	int ADCAdjust(int adcmask);
	int BlockTransfer(unsigned int fifo_addr, unsigned int *data, int len, int wr);
	int ConfigureMasterClock(int sel, int div, int erc = 0);
	int ConfigureSlaveClock(int num, const char *fname);
	int ConfigureSlaveXilinx(int num);
	void EnableFifo(int what);
	int DACSet(int val);
	int GetADCID(int num);
	inline int GetBase16(void) { return A16BASE + serial * A16STEP; };
	inline unsigned int GetBase32(void) { return A32BASE + ga * A32STEP; };
	inline unsigned long long GetBase64(void) { return A64BASE + ga * A64STEP; };
	inline int GetBatch(void) { return (a16->bnum >> 8) & 0xFF; };
	int GetFromFifo(void *buf, int size);
	inline int GetGA(void) { return ga; };
	inline int GetSerial(void) { return serial; };
	inline int GetVersion(void) { return a32->ver.in; };
	inline int GetSlaveVersion(int num) { return ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_VER_IN) & 0xFFFF; };
	int I2CRead(int addr);
	int I2CWrite(int addr, int val);
	int ICXRead(int addr);
	int ICXWrite(int addr, int val);
	void Inhibit(int what);
	int Init(void);
	int IsHere(void);
	int IsDone(int wait = 0);
	int L2CBlkRead(int num, int addr, char *buf, int len);
	int L2CBlkWrite(int num, int addr, char *buf, int len);
	int L2CRead(int num, int addr);
	int L2CWrite(int num, int addr, int val);
	int Prog(char *fname = NULL);
	void ReadConfig(config_t *cnf);
	void Reset(void);
	void ResetFifo(int mask = FIFO_CSR_HRESET | FIFO_CSR_SRESET);
	inline void ResetTrigCnt(void) { a32->trig.gtime = 0; };
	void SoftTrigger(int freq);
	int TestAllChannels(int cnt);
	int TestADCPhase(int cnt);
	int TestADCReg(int cnt);
	int TestFifo(int cnt);
	int TestRandomRead(int cnt);
	int TestReg32(int cnt);
	int TestSDRAM(int cnt);
	int TestSlaveReg16(int cnt);
	void WriteUserWord(int num);
	void ZeroTrigger(void);

	friend class uwfd64_tool;
};



#endif /* UWFD64_H */


