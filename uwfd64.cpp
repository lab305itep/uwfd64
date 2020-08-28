/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support UWFD64 modules.
*/

#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "libvmemap.h"
#include "log.h"
#include "uwfd64.h"

//	Constructor - only set addresses here
uwfd64::uwfd64(int sernum, int gnum, unsigned short *space_a16, unsigned int *space_a32, int fd, config_t *cnf)
{
	int s, i;

	serial = sernum;
	ga = gnum;
	a16 = (struct uwfd64_a16_reg *)((char *)space_a16 + serial * A16STEP);
	a32 = (struct uwfd64_a32_reg *)((char *)space_a32 + ga * A32STEP);
	dma_fd = fd;
	// initial configuration - empty
	memset(&Conf, 0, sizeof(Conf));
	Conf.FifoEnd = 1;	// minimum FIFO size of 8kBytes
	if (cnf) ReadConfig(cnf);
	// Set base address for A32 - emulate geographic and its parity
	s = 0;
	for (i=0; i<5; i++) if (ga & (1 << i)) s++;
	s = 1 - (s & 1);
	a16->c2x = (CPLD_C2X_RESET + ga * CPLD_C2X_GA + s * CPLD_C2X_PARITY) << 8;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Read ADC chip 8-bit registers via SPI
//	num - 0-15 ADC chip number
//	addr - register address
//	return register value, negative on error.
int uwfd64::ADCRead(int num, int addr)
{
    	int xil;
	int r;

    	xil = ICX_SLAVE_STEP * ((num >> 2) & 3);
    	if (ICXWrite(xil + ICX_SLAVE_SPI_CSR, SPI_CSR_CS << (num & 3))) goto err;	// frame begin
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, (addr + SPI_ADDR_DIR) >> 8)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, addr & 0xFF)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_CSR, (SPI_CSR_CS << (num & 3)) + SPI_CSR_DIR)) goto err;	// switch to input data
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, 0)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_CSR, 0)) goto err;			// frame end
	r = ICXRead(xil + ICX_SLAVE_SPI_DAT);
    	return r;
err:
	ICXWrite(xil + ICX_SLAVE_SPI_CSR, 0);
	return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write ADC chip 8-bit registers via SPI
//	num - 0-15 ADC chip number
//	addr - register address
//	val - value to be written
//	return 0 if OK, negative on error.
int uwfd64::ADCWrite(int num, int addr, int val)
{
    	int xil;

    	xil = ICX_SLAVE_STEP * ((num >> 2) & 3);
    	if (ICXWrite(xil + ICX_SLAVE_SPI_CSR, SPI_CSR_CS << (num & 3))) goto err;	// frame begin
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, (addr >> 8) & 0x7F)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, addr & 0xFF)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, val & 0xFF)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_CSR, 0)) goto err;			// frame end
    	return 0;
err:
	ICXWrite(xil + ICX_SLAVE_SPI_CSR, 0);
	return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Executes sequense for various checks (test data error, adc freq, bit instabilities)
//	time - measurement time = 2**(16 + 2*time) 125 MHz clocks
//	xilmask - bit mask of xilinxes to be touched
//	return 0 if OK, negative on error.
int uwfd64::ADCCheckSeq(int time, int xilmask = 0xF)
{
	int i, j, xil, ttype;

	// start measurement
	for (i=0; i<4; i++) {
		if (!(xilmask & (1 << i))) continue;
		xil = ICX_SLAVE_STEP * i;
		if ((ttype = ICXRead(xil + ICX_SLAVE_CSR_IN)) < 0) return -1;
		ttype &= 0xF;
    		if (ICXWrite(xil + ICX_SLAVE_CSR_OUT, SLAVE_CSR_TSTART | (time << 4) | ttype)) return -1;			
    		if (ICXWrite(xil + ICX_SLAVE_CSR_OUT, (time << 4) | ttype)) return -1;			
	}
	// check measurement done
	for (i=0; i<4; i++) {
		if (!(xilmask & (1 << i))) continue;
		xil = ICX_SLAVE_STEP * i;
    		for (j=0; j<(1<<2*(time+1)); j++) { 
			if (ICXRead(xil + ICX_SLAVE_CSR_IN) & SLAVE_CSR_TSTART) break; 
			vmemap_usleep(600); 
		}
		if (j == (1<<2*(time+1))) return -2;			
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Scans and adjusts ADC input delays
//	adcmask - 16 bit mask of module ADCs to be adjusted
//	adsmask[16] = 1 produces extensive output
//	adsmask[17] = 1 executes delay scan, otherwise delay setting only
//	return 0 if OK, negative on error.
int uwfd64::ADCAdjust(int adcmask = 0xFFFF)
{
	int i, j, k, xil, xilmask, adc, irc;
	const int ftime = 2;	// freq meas time 8 msec or less (ftime =< 2)
	const int itime = 2;	// instab meas time 8 msec
	const int ctime = 3;	// pseudo-random test time 32 msec
	

	// Don't touch x's wich are not involved
	xilmask = 0;
	for (i=0; i<4; i++) if (adcmask & (0xF << (4*i))) xilmask |= (1 << i);

	// Check ADC frequency
	if (irc = ADCCheckSeq(ftime, xilmask)) return irc;
	// read freq result
	if (adcmask & 0x10000) {
		printf("ADC\t");
		for (i=0; i<16; i++) {
			if (!(adcmask & (1 << i))) continue;
			printf("       %1X  ", i);
		}
		printf("\n");
	}
	if (adcmask & 0x10000) printf("Frq dev\t");
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		irc = ICXRead(adc + ICX_SLAVE_ADC_CFRQ);
		if (adcmask & 0x10000) printf("%8.1f%% ", (irc - (1 << (2*ftime+11))) / ((double)(1 << (2*ftime+11))) * 100.0);
		// check frequency is sane (actually must be well below 100 ppm)
		if (labs(irc - (1 << (2*ftime+11))) > 2) {
			if (adcmask & 0x10000) printf("\n");
			return -10-i;
		}
	}	
	if (adcmask & 0x10000) printf("\n");

	// reset IODELAY and SERDES bitslip and disable it, also program ADC itself to produce 000111 data pattern
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		// 0xE380 = b111000111000(0000), same as frame in bytewise 1xFrame mode, MSbits are transmitted
		if (ADCWrite(i, ADC_REG_PAT1L, 0x80)) return -3;		 
		if (ADCWrite(i, ADC_REG_PAT1H, 0xE3)) return -3;		 
		if (ADCWrite(i, ADC_REG_TEST, ADC_TEST_USER)) return -3;		 
		// IODELAY calibrate, SERDES reset (and disable bitslip)
		if (ICXWrite(adc + ICX_SLAVE_ADC_CSR, SLAVE_ADCCSR_DCAL | SLAVE_ADCCSR_BSRST)) return -3;
		// IODELAY reset (and disable bitslip)
		if (ICXWrite(adc + ICX_SLAVE_ADC_CSR, SLAVE_ADCCSR_DRST)) return -3;
	}
	
	// Test stability while incrementing IODELAY 256 steps
	if (adcmask & 0x20000) for (j=0; j<80; j++) {
		// seq instability measurement
		if (irc = ADCCheckSeq(itime, xilmask)) return irc;
		// read counters and increment IODELAY
		if (adcmask & 0x10000) {
			if (j == Conf.IODelay-5 || j == Conf.IODelay+6) {
				printf("\t");
				for (i=0; i<16; i++) if (adcmask & (1 << i)) printf("========= ");
				printf("\n");
			}
			printf("%5d\t", j);
		}
		for (i=0; i<16; i++) {
			if (!(adcmask & (1 << i))) continue;
			adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
			for (k=0; k<9; k++) {			
				irc = ICXRead(adc + ICX_SLAVE_ADC_CINS + k);		 
				if (adcmask & 0x10000) {
					printf("%c", (irc) ? '*' : '.');
				}
				if (irc && (j >= Conf.IODelay-5) && (j <= Conf.IODelay+5)) {
					if (adcmask & 0x10000) printf("\n");
					return -30-i;
				}
			}
			if (adcmask & 0x10000) printf(" ");
			// increment IODELAY for all bits and frame
			if (ICXWrite(adc + ICX_SLAVE_ADC_CSR, SLAVE_ADCCSR_DINC | 0x1FF)) return -3;
		}
		if (adcmask & 0x10000) printf("\n");
	}
	
	// Set required delay
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		// IODELAY reset (and disable bitslip)
		if (ICXWrite(adc + ICX_SLAVE_ADC_CSR, SLAVE_ADCCSR_DRST)) return -3;
		for (j=0; j<Conf.IODelay; j++) {
			if (ICXWrite(adc + ICX_SLAVE_ADC_CSR, SLAVE_ADCCSR_DINC | 0x1FF)) return -3;
		}
	}

	// Allow bitslip and check that it comes to eqilibrium
	// allow frame bitslip	(should settle fast)
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		if (ICXWrite(adc + ICX_SLAVE_ADC_CSR, SLAVE_ADCCSR_MBSENB)) return -3;
	}
	// allow individual bitslips	(should not happen at all after master bitslip)
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		if (ICXWrite(adc + ICX_SLAVE_ADC_CSR, SLAVE_ADCCSR_BSENB)) return -3;
	
	}
	// clear bitslip capture reg		
	if (irc = ADCCheckSeq(1, xilmask)) return irc;		// and wait 2 ms

	// check bitslips settled	
	if (adcmask & 0x10000) printf("  BS\t");
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		irc = ICXRead(adc + ICX_SLAVE_ADC_IBS);
		if (adcmask & 0x10000) printf("%8X  ", irc);
		if (irc) return -50-i;
	}
	if (adcmask & 0x10000) printf("\n");

	// disable individual bitslips, frame bitslip should never happen
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		// allow frame bitslip		
		if (ICXWrite(adc + ICX_SLAVE_ADC_CSR, SLAVE_ADCCSR_MBSENB)) return -3;
	}

	// Check ADC's with pseudo-random sequence
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		// switch ADC to PN23 test mode
		if (ADCWrite(i, ADC_REG_TEST, ADC_TEST_PN23)) return -3;		 
	}
	for (i=0; i<4; i++) {
		if (!(xilmask & (1 << i))) continue;
		xil = ICX_SLAVE_STEP * i;
		// set check type in xil's
    		if (ICXWrite(xil + ICX_SLAVE_CSR_OUT, ADC_TEST_PN23)) return -3;			
	}
	// check sequence
	if (irc = ADCCheckSeq(ctime, xilmask)) return irc;		
	// check errors
	if (adcmask & 0x10000) printf(" PN23\t");
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		for (k=0; k<4; k++) {
			irc = ICXRead(adc + ICX_SLAVE_ADC_CERR + k);
			if (adcmask & 0x10000) printf(" %1X", irc & 0xF);
			if (irc) {
				if (adcmask & 0x10000) printf("\n");
				return -70-i;
			}
		}
		if (adcmask & 0x10000) printf("  ");
	}
	if (adcmask & 0x10000) printf("\n");

	// Switch ADC to normal mode
	for (i=0; i<16; i++) {
		if (!(adcmask & (1 << i))) continue;
		if (ADCWrite(i, ADC_REG_TEST, 0)) return -3;
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Configure Master clock multiplexer CDCUN1208LP
//	sel - input clock select: 0 - internal, 1 - external
//	div - input clock divider: 0 - 1, 1 - 2, 2 - 4, 3 - 8.
//	erc - output spped: 0 - CDCUN_OUT_ERC_FAST, 1 - CDCUN_OUT_ERC_MEDIUM, 2 - CDCUN_OUT_ERC_SLOW 
//	Return 0 if OK, or number of errors met
int uwfd64::ConfigureMasterClock(int sel, int div, int erc)
{
	int errcnt;
	int w, s;
	errcnt = 0;
	int i;
		// Write
	if(I2CWrite(CDCUN_CTRL_ADDR, CDCUN_CTRL_RESET)) errcnt++;	// Disable clock
	w = (div & 3) << 4;						// divider
	w += (sel) ? CDCUN_INPUT_MUX_IN2 : CDCUN_INPUT_MUX_IN1;		// select
	if(I2CWrite(CDCUN_INPUT_ADDR, w)) errcnt++;			// Input control
	for (i=0; i<3; i++) if(I2CWrite(i, CDCUN_OUT_DISABLE)) errcnt++;	// Disable empty outputs
	switch(erc) {
		case 1: s = CDCUN_OUT_ERC_MEDIUM; break;
		case 2: s = CDCUN_OUT_ERC_SLOW; break;
		default: s = CDCUN_OUT_ERC_FAST; break;
	}
	s += CDCUN_OUT_DIFF_ON;
	for (i=3; i<8; i++) if(I2CWrite(i, s)) errcnt++;	// Set outputs
	if(I2CWrite(CDCUN_CTRL_ADDR, 0)) errcnt++;	// Enable clock
		// Verify
	if(I2CRead(CDCUN_CTRL_ADDR)) errcnt++;
	if(I2CRead(CDCUN_INPUT_ADDR) != w) errcnt++;
	for (i=0; i<3; i++) if(I2CRead(i) != CDCUN_OUT_DISABLE) { printf("should be out disable: %X %X\n", I2CRead(i), CDCUN_OUT_DISABLE);errcnt++; }
	for (i=3; i<8; i++) if(I2CRead(i) != s) { printf("should be out select: %X %X\n", I2CRead(i), s);errcnt++; }
	
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Configure Slave clock Si5338 using text file with configuration
//	num - Xilinx number (0-3)
//	fname - path to Si5338 configuration file .h
//	Return 0 if OK, or negative on error
//
//   	Configure Si5338 using register file and algorithm from fig. 9 of manual
//    	parsing .h file generated by Silabs ClockBuilder Desktop
//	file structure must follow the pattaern:
//	....
//	#define name NREGS to program, only one line of 3 words starting with #
//	....
//	{ RegN, RegVal, RegMask }  exactly NREGS lines like this
//	....
int uwfd64::ConfigureSlaveClock(int num, const char *fname)
{
	unsigned char RegVal;
    	unsigned char RegMask;
    	int Reg, nRegs, page;
    	char str[1024];
    	char *tok;
    	FILE *conf;
    	const char DELIM[]=" \t\r,#{}";
    	int i, j;
    	unsigned char val;
    	int errcnt;

	errcnt = 0;
//	Read the file
    	conf = fopen(fname, "rt");
    	if (!conf) return -10;
// find #define
    	nRegs = -1;
    	for(i=0;;i++) {
		if (!fgets(str, sizeof(str), conf)) break;
		if (str[0] != '#') continue;
		tok = strtok(str, DELIM);	// "define"
		tok = strtok(NULL, DELIM);	// name
		tok = strtok(NULL, DELIM);	// NREGS
		if (!tok || !strlen(tok)) break;
		if (!isdigit(tok[0])) break;
		nRegs = strtol(tok, NULL, 0);
		break;
    	}
    	if (nRegs <= 0 || nRegs > 511) return -20;
//	Set OEB_ALL = 1; reg230[4]
    	if(L2CWrite(num, SI5338_REG_OUT, SI5338_OUT_DISABLE_ALL)) errcnt++;	// disable all
//	Set DIS_LOL = 1; reg241[7] (manual doesn't reqire to write 0x65)
	if(L2CWrite(num, SI5338_REG_LOL, SI5338_LOL_DISABLE + SI5338_LOL_CONST)) errcnt++;
//	select page 0 for safety
    	if(L2CWrite(num, SI5338_REG_PAGE, 0)) errcnt++;
// pages are switched by register writes
    	page = 0;
    	j = 0;
// parsing file and programming
    	for(i=0;;i++) {
		if (!fgets(str, sizeof(str), conf)) break;
		if (str[0] != '{') continue;
//		printf("str = {%d: %s}\n", j, str);
    	// parsing 3 numbers from lines starting with "{"
		tok = strtok(str, DELIM);
		if (!tok || !strlen(tok)) break;
		if (!isdigit(tok[0])) break;
		Reg = strtol(tok, NULL, 0);
		if (Reg<0 || Reg>511) break;
		tok = strtok(NULL, DELIM);
		if (!tok || !strlen(tok)) break;
		if (!isdigit(tok[0])) break;
		RegVal = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (!tok || !strlen(tok)) break;
		if (!isdigit(tok[0])) break;
		RegMask = strtol(tok, NULL, 0);
		if (Reg == 255) page = RegVal & 1;
//		printf("Reg = %d, RegVal = %2X, RegMask = %2X\n", Reg, RegVal, RegMask);
    	// programming
		switch(RegMask) {
		case 0:			// we should ignore this register
//			printf("Reg %d skip\n", Reg);
	    		break;
		case 0xFF:		// we can directly write to this register
	    		if(L2CWrite(num, Reg, RegVal)) errcnt++;
//			printf("Reg %d direct write %2.2X\n", Reg, RegVal);
	    		break;
		default:		// we need read-modify-write
	    		val = L2CRead(num, Reg) & (~RegMask);
	    		val |= RegVal & RegMask;
	    		if(L2CWrite(num, Reg, val)) errcnt++;
//			printf("Reg %d mask write %2.2X\n", Reg, val);
	    		break;
		}
		val = L2CRead(num, Reg);
//		printf("Reg %d Read %2.2X (errcnt %d)\n", Reg, val, errcnt);
		if ((val & RegMask) != (RegVal & RegMask)) errcnt++;
		j++;
    	}
    	fclose(conf);
//    	printf("j: %d, nRegs: %d\n", j, nRegs);
    	if (j != nRegs) return -30;
//	printf("errors: %d\n", errcnt);
    	if(L2CWrite(num, SI5338_REG_PAGE, 0)) errcnt++; // back to page 0 for safety
//	Validate input clock
    	for (i = 0; i < SI5338_TIMEOUT; i++) {
		if (!(L2CRead(num, SI5338_REG_STATUS) & SI5338_STATUS_CLKIN)) break;
		vmemap_usleep(100);
    	}
	if (i == SI5338_TIMEOUT) return -40;
//	Set FCAL_OVRD_EN=0; reg49[7]
    	val = L2CRead(num, SI5338_REG_CTRL) & (~SI5338_CTRL_FCALOVR);
    	if(L2CWrite(num, SI5338_REG_CTRL, val)) errcnt++;
//	Initiate PLL lock SOFT_RESET=1; reg246[1]
	if(L2CWrite(num, SI5338_REG_SRESET, SI5338_SRESET_RESET)) errcnt++;
    	vmemap_usleep(25000);
//	restart LOL DIS_LOL=0; reg241[7]; reg241 = 0x65
	if(L2CWrite(num, SI5338_REG_LOL, SI5338_LOL_CONST)) errcnt++;
//	Validate PLL lock (no PLL_LOL, no LOS_CLKIN, no SYS_CAL)
    	for (i = 0; i < SI5338_TIMEOUT; i++) {
		if (!(L2CRead(num, SI5338_REG_STATUS) & (SI5338_STATUS_LOL | SI5338_STATUS_CLKIN | SI5338_STATUS_SYSCAL))) break;
		vmemap_usleep(100);
    	}
    	if (i == SI5338_TIMEOUT) return -50;
//	Copy FCAL
    	val = (L2CRead(num, SI5338_REG_FCALH) & 3) + SI5338_FCALOVRH_CONST;
    	if (L2CWrite(num, SI5338_REG_FCALOVRH, val)) errcnt++;
    	val = L2CRead(num, SI5338_REG_FCALM);
    	if (L2CWrite(num, SI5338_REG_FCALOVRM, val)) errcnt++;
    	val = L2CRead(num, SI5338_REG_FCALL);
    	if (L2CWrite(num, SI5338_REG_FCALOVRL, val)) errcnt++;
//	Set FCAL_OVRD_EN=1; reg49[7]
    	val = L2CRead(num, SI5338_REG_CTRL) | SI5338_CTRL_FCALOVR;
    	if(L2CWrite(num, SI5338_REG_CTRL, val)) errcnt++;
//	Enable Outputs
//	Set OEB_ALL = 0; reg230[4]
    	if (L2CWrite(num, SI5338_REG_OUT, 0)) errcnt++;		// enable all    
    	
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Configure Slave Xilinx with already read configuration
//	num - Xilinx number (0-3)
//	Return 0 if OK, or negative on error
//
int uwfd64::ConfigureSlaveXilinx(int num)
{
	int errcnt;
	int i;
	int val;

	errcnt = 0;
	// coefficiences for trigger production
	for (i=0; i<16; i++) {
		val = 0x8000 * Conf.TrigCoef[16*num + i];
		if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_COEF + i, val)) errcnt++;
	}
	// main trigger channel mask (1 = disable)
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_MTMASK, Conf.MainTrigMask[num])) errcnt++;
	// self trigger channel mask (1 = disable)
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_STMASK, Conf.SelfTrigMask[num])) errcnt++;
	// summ channel mask (1 = disable)
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_SUMASK, Conf.TrigSumMask[num])) errcnt++;
	// invert channel mask (0 = pulses go up)
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_INVMASK, Conf.InvertMask[num])) errcnt++;
	// master trigger zero suppression threshold
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_MTTHR, Conf.ZeroSupThreshold)) errcnt++;
	// self trigger threshold
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_STTHR, Conf.SelfTrigThreshold)) errcnt++;
	// master trigger sum 64 production threshold
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_SUTHR, Conf.MasterTrigThreshold)) errcnt++;
	// selftrigger prescale
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_STPRC, Conf.SelfTriggerPrescale)) errcnt++;
	// window length for both triggers and trigger history
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_WINLEN, Conf.WinLen)) errcnt++;
	// master trigger window begin
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_MTWINBEG, Conf.TrigWinBegin)) errcnt++;
	// self trigger window begin
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_STWINBEG, Conf.SelfWinBegin)) errcnt++;
	// trigger history window begin
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_SUWINBEG, Conf.SumWinBegin)) errcnt++;
	// delay of local sum for adding to other X's
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_SUDELAY, Conf.SumDelay)) errcnt++;
	// zero suppression window begin (from MTWINBEG)
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_MTZBEG, Conf.ZSWinBegin)) errcnt++;
	// zero suppression window end (from MTWINBEG)	if(ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_MTMASK, Conf.MainTrigMask[i])) errcnt++;
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_MTZEND, Conf.ZSWinEnd)) errcnt++;
	// CSR
	if(ICXWrite(ICX_SLAVE_STEP * num + ICX_SLAVE_CSR_OUT, (Conf.TrigHistMask & (1 << num)) ? SLAVE_CSR_HISTENABLE : 0)) errcnt++;
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write common offset DAC via SPI. No way to read.
//	Return 0 if OK, -10 on timeout
int uwfd64::DACSet(int val)
{
	int i;
	a32->dac.csr = SPI_CSR_CS;		// crystall select
	a32->dac.dat = (val >> 8) & 0x3F;	// High byte
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->dac.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
	a32->dac.dat = val & 0xFF;		// Low byte
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->dac.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
fin:
	a32->dac.csr = 0;			// Deselect
	return (i == SPI_TIMEOUT) ? -10 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Enable (what != 0) / disable Fifo
void uwfd64::EnableFifo(int what)
{
	if (what) {
		a32->fifo.csr |= FIFO_CSR_ENABLE;
	} else {
		a32->fifo.csr &= ~FIFO_CSR_ENABLE;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Read ADC ID : regs 1 and 2.
//	num - ADC number (1-15)
//	Return 16-bit value ID:GRADE if OK, -10 on error
int uwfd64::GetADCID(int num)
{
	int id, grade;
	id = ADCRead(num, ADC_REG_ID);
	if (id < 0) return id;
	grade = ADCRead(num, ADC_REG_GRADE);
	if (grade < 0) return grade;
	return (id << 8) + grade;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Try to get data from FIFO
//	buf - buffer for data
//	size - buffer size
//	Return number of bytes got, 0 - no data, negative on errors
int uwfd64::GetFromFifo(void *buf, int size)
{
	int fifobot, fifotop, fifolen;
	int rptr, wptr, len;
	
	rptr = a32->fifo.rptr;
	wptr = a32->fifo.wptr;

	if (rptr == wptr) return 0;

	fifolen = a32->fifo.win;
	fifobot = (fifolen & 0xFFFF) << 13;
	fifotop = (fifolen >> 3) & 0x1FFFE000;
	fifolen = fifotop - fifobot;

	len = wptr - rptr;
	if (len < 0) len += fifolen;
	if (len < 0) return -1;
	if (len > size) len = size;
	if (rptr + len > fifotop) len = fifotop - rptr;

	if (vmemap_a64_dma(dma_fd, GetBase64() + rptr, (unsigned int *)buf, len, 0)) return -2;
	rptr += len;
	if (rptr == fifotop) rptr = fifobot;
	a32->fifo.rptr = rptr;
	
	return len;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Read CDCUN via I2C
//	Return 16-bit value if OK, -10 on error
int uwfd64::I2CRead(int addr)
{
	int i;
	int val;
	// send chip address
	a32->i2c.dat = CDCUN_ADDR;
	a32->i2c.csr = I2C_SR_START + I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send register address
	a32->i2c.dat = addr & 0x7F;
	a32->i2c.csr = I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send chip address again with direction bit set
	a32->i2c.dat = CDCUN_ADDR + I2C_DAT_DDIR;
	a32->i2c.csr = I2C_SR_START + I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	// read high byte
	a32->i2c.csr = I2C_SR_READ;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	val = (a32->i2c.dat & 0xFF) << 8;
	// read low byte
	a32->i2c.csr = I2C_SR_READ + I2C_SR_STOP + I2C_SR_ACK;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	val += a32->i2c.dat & 0xFF;
	return val;
err:
printf("timeout err in ICRead adr=%d\n", addr);
	a32->i2c.csr = I2C_SR_STOP;
	return -10;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write CDCUN via I2C
//	Return 0, -10 on error
int uwfd64::I2CWrite(int addr, int val)
{
	int i;
	// send chip address
	a32->i2c.dat = CDCUN_ADDR;
	a32->i2c.csr = I2C_SR_START + I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send register address
	a32->i2c.dat = addr & 0x7F;
	a32->i2c.csr = I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send high byte
	a32->i2c.dat = (val >> 8) & 0xFF;
	a32->i2c.csr = I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send low byte
	a32->i2c.dat = val & 0xFF;
	a32->i2c.csr = I2C_SR_WRITE + I2C_SR_STOP;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == I2C_TIMEOUT) goto err;
	return 0;
err:
printf("timeout err in I2CWrite adr=%d data %X\n", addr, val);
	a32->i2c.csr = I2C_SR_STOP;
	return -10;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Read from slave Xilinxes.
//	Return 16-bit value if OK, -10 on timeout
int uwfd64::ICXRead(int addr)
{
	int i;
	int val;
	a32->icx.csr = SPI_CSR_CS;		// crystall select
	addr |= SPI_ADDR_DIR;
	a32->icx.dat = (addr >> 8) & 0xFF;	// High byte of the address
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->icx.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
	a32->icx.dat = addr & 0xFF;		// Low byte of the address
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->icx.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
	a32->icx.csr = SPI_CSR_CS + SPI_CSR_DIR; // crystall select & input direction
	a32->icx.dat = 0;			// Clock High byte of the data
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->icx.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
	val = (a32->icx.dat & 0xFF) << 8;
	a32->icx.dat = 0;			// Clock Low byte of the data
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->icx.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
	val += a32->icx.dat & 0xFF;
fin:
	a32->icx.csr = 0;			// Deselect
	return (i == SPI_TIMEOUT) ? -10 : val;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write to slave Xilinxes.
//	Return 0 if OK, -10 on timeout
int uwfd64::ICXWrite(int addr, int val)
{
	int i;
	a32->icx.csr = SPI_CSR_CS;		// crystall select
	addr &= ~SPI_ADDR_DIR;
	a32->icx.dat = (addr >> 8) & 0xFF;	// High byte of the address
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->icx.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
	a32->icx.dat = addr & 0xFF;		// Low byte of the address
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->icx.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
	a32->icx.dat = (val >> 8) & 0xFF;	// High byte of the data
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->icx.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
	a32->icx.dat = val & 0xFF;		// Low byte of the data
    	for (i = 0; i < SPI_TIMEOUT; i++) if (!(a32->icx.csr & SPI_CSR_BUSY)) break;
	if (i == SPI_TIMEOUT) goto fin;
fin:
	a32->icx.csr = 0;			// Deselect
	return (i == SPI_TIMEOUT) ? -10 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	set (what != 0) / clear (what = 0) inhibit
void uwfd64::Inhibit(int what)
{
	int tmp;	
	tmp = a32->trig.csr;
	a32->trig.csr = (what) ? (tmp | TRIG_CSR_INHIBIT) : (tmp & (~TRIG_CSR_INHIBIT));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Do module initialization.
int uwfd64::Init(void)
{
	int i;
	int errcnt;
	
	errcnt = 0;
	// Setup I2C on main xilinx
	a32->i2c.presc[0] = I2C_PRESC & 0xFF;
	a32->i2c.presc[1] = (I2C_PRESC >> 8) & 0xFF;
	a32->i2c.ctr = I2C_CTR_CORE_ENABLE;
	errcnt = ConfigureMasterClock((Conf.MasterClockDiv & 4) ? 1 : 0, Conf.MasterClockDiv, Conf.MasterClockErc);
	if (errcnt) printf("Init %d ConfigureMasterClock failed.\n", serial);
	// Init main CSR
	a32->csr.out = MAIN_CSR_TRG * (Conf.MasterTrigMux & MAIN_MUX_MASK) + MAIN_CSR_INH * (Conf.MasterInhMux & MAIN_MUX_MASK) + 
		MAIN_CSR_CLK * (Conf.MasterClockMux & MAIN_MUX_MASK) + ((MAIN_CSR_USER * Conf.TrigUserWord) & MAIN_CSR_USER_MASK) +
		MAIN_CSR_AUXOUT * (Conf.AuxTrigOut & 1) + MAIN_CSR_TOKSYNC * (Conf.TokenSync & 1);
	Reset();
	// Init Main trigger source
	a32->trig.csr = TRIG_CSR_INHIBIT + TRIG_CSR_AUXIN * Conf.AuxTrigIn + TRIG_CSR_TRIG2FIFO * Conf.MasterTrig2FIFO 
		+ ((TRIG_CSR_BLOCK * Conf.TrigBlkTime) & TRIG_CSR_BLOCK_MASK) 
		+ ((TRIG_CSR_SRCOR * Conf.TrigOrTime) & TRIG_CSR_SRCOR_MASK) + ((TRIG_CSR_CHAN * Conf.TrigGenMask) & TRIG_CSR_CHAN_MASK);
	a32->trig.gtime = 0;	// reset counters
	// Init SDRAM FIFO
	a32->fifo.csr = FIFO_CSR_HRESET | FIFO_CSR_SRESET;
	a32->fifo.win = (Conf.FifoBegin & 0xFFFF) + ((Conf.FifoEnd & 0xFFFF) << 16);
	// Set DAC to middle range
	if(DACSet(Conf.DAC)) {
	    printf("Init %d DACSet failed.\n", serial);
	    errcnt++;
	}
	// ADC power down
	for (i=0; i<16; i++) if(ADCWrite(i, ADC_REG_PWR, ADC_PWR_DOWN)) {
	    printf("Init %d ADCWrite[%d] failed.\n", serial, i);
	    errcnt++;
	}
	// Set I2C on slave Xilinxes
	for (i=0; i<4; i++) {
		if(ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_PRCL, I2C_PRESC & 0xFF)) {
		    printf("Init %d ICXWrite[0x%X] failed.\n", serial, ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_PRCL);
		    errcnt++;
		}
		if(ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_PRCH, (I2C_PRESC >> 8) & 0xFF)) {
		    printf("Init %d ICXWrite[0x%X] failed.\n", serial, ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_PRCH);
		    errcnt++;
		}
		if(ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_CTR, I2C_CTR_CORE_ENABLE)) {
		    printf("Init %d ICXWrite[0x%X] failed.\n", serial, ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_CTR);
		    errcnt++;
		}
		if(ConfigureSlaveClock(i, Conf.SlaveClockFile)) {
		    printf("Init %d ConfigureSlaveClock[%d] failed.\n", serial, i);
		    errcnt++;
		}
	}
	// ADC power up
	for (i=0; i<16; i++) if(ADCWrite(i, ADC_REG_PWR, 0)) errcnt++;
	// ADC power reset on
	for (i=0; i<16; i++) if(ADCWrite(i, ADC_REG_PWR, ADC_PWR_RESET)) errcnt++;
	// ADC power reset off
	for (i=0; i<16; i++) if(ADCWrite(i, ADC_REG_PWR, 0)) errcnt++;
	// ADC software Reset
	for (i=0; i<16; i++) if(ADCWrite(i, ADC_REG_CFG, ADC_CFG_RESET)) errcnt++;
	// ADC output offset binary
	for (i=0; i<16; i++) if(ADCWrite(i, ADC_REG_OUTPUT, 0)) errcnt++;
	// ADC input adjust
	if (ADCAdjust()) {
	    printf("Init %d ADCAdjust failed.\n", serial);
	    errcnt++;
	}
	for (i=0; i<4; i++) errcnt += ConfigureSlaveXilinx(i);
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Check for DONE. If wait > 0 - wait for wait*0.01 s.
//	Return true when done.
int uwfd64::IsDone(int wait)
{
	int i;
    	for (i=0; i < wait; i++) {
	// Done obtained ?
		if ((a16->csr >> 8) & CPLD_CSR_XDONE) break;
		vmemap_usleep(10000);	// 10 ms
    	}
	return ((a16->csr >> 8) & CPLD_CSR_XDONE) ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Check if the module is present
int uwfd64::IsHere(void)
{
	int rc;
	
	rc = (a16->snum >> 8) & 0xFF;
	return (rc == serial);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Read block of bytes from Si5338 via I2C
//	num - slave Xilinx address
//	addr - register
//	buf - buffer for data read
//	len - number of bytes to read
//	Return 0 if OK, -10 on error
int uwfd64::L2CBlkRead(int num, int addr, char *buf, int len)
{
	int i, j;
	char cmd;

	// send chip address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, SI5338_ADDR)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_START + I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// check acknowledge
	if (ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_RXACK) goto err;
	// send register address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, addr)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_WRITE + I2C_SR_STOP)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// send chip address again with direction bit
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, SI5338_ADDR + I2C_DAT_DDIR)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_START + I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// get data bytes
	for (j=0; j<len; j++) {
		cmd = I2C_SR_READ;
		if (j == len - 1) cmd += I2C_SR_STOP + I2C_SR_ACK;
		if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, cmd)) goto err;
    		for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
		if (i == L2C_TIMEOUT) goto err;
		buf[j] = ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT);
	}
	return 0;
err:
	ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_STOP);
	return -10;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write block of bytes to Si5338 via I2C
//	num - slave Xilinx address
//	addr - register
//	buf - data to be written
//	len - number of bytes to write
//	Return 0, -10 on error
int uwfd64::L2CBlkWrite(int num, int addr, char *buf, int len)
{
	int i, j;
	char cmd;
	// send chip address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, SI5338_ADDR)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_START + I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// check acknowledge
	if (ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_RXACK) goto err;
	// send register address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, addr)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// send data bytes
	for (j=0; j<len; j++) {
		if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, buf[j])) goto err;;
		cmd = I2C_SR_WRITE;
		if (j == len - 1) cmd += I2C_SR_STOP;
		if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, cmd)) goto err;
    		for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
		if (i == L2C_TIMEOUT) goto err;
	}
	return 0;
err:
	ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_STOP);
	return -10;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Read Si5338 via I2C
//	num - slave Xilinx address
//	addr - register
//	Return 16-bit value if OK, -10 on error
int uwfd64::L2CRead(int num, int addr)
{
	int i;

	// send chip address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, SI5338_ADDR)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_START + I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// check acknowledge
	if ((ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_RXACK)) goto err;
	// send register address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, addr)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_WRITE + I2C_SR_STOP)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// send chip address again with direction bit
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, SI5338_ADDR + I2C_DAT_DDIR)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_START + I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// get data byte
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_READ + I2C_SR_STOP + I2C_SR_ACK)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	return ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT);
err:
	ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_STOP);
	return -10;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write Si5338 via I2C
//	num - slave Xilinx address
//	addr - register
//	Return 0, -10 on error
int uwfd64::L2CWrite(int num, int addr, int val)
{
	int i;
	// send chip address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, SI5338_ADDR)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_START + I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// check acknowledge
	if (ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_RXACK) goto err;
	// send register address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, addr)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// send data byte
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, val & 0xFF)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_WRITE + I2C_SR_STOP)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	return 0;
err:
	ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_STOP);
	return -10;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Prog Xilinxes with binary file fname
//	Pulse prog only if fname = NULL
//	No wait for DONE here.
//	Return:
//	0   - OK;
//	-10 - module not present
//	-20 - Can not open file
//	-30 - Algorithm error
int uwfd64::Prog(char *fname)
{
	int i, j, todo;
    	FILE * f;
    	unsigned char buf[4096];

	if (!IsHere()) return -10;	

	if (fname) {
    		f = fopen(fname, "rb");
    		if (!f) return -20;

		a16->csr = (CPLD_CSR_XSLAVE + CPLD_CSR_XPROG) << 8;	// pulse PROG
    		// remove PROG, xilinx acess enabled
		a16->csr = CPLD_CSR_XSLAVE << 8;
    		// wait for INIT
    		for (i=0; i<1000; i++) if ((a16->csr >> 8) & CPLD_CSR_XINIT) break;
    		if (i == 1000) return -30;
    		// Load data
    		for (i=0; ; i += todo) {
			// read buffer
			todo = sizeof(buf);
			// or up to end of file
			todo = fread(buf, 1, todo, f);
			for (j=0; j<sizeof(buf); j++) a16->sdat = buf[j] << 8; 
			if (feof(f)) break;
    		}
    		fclose(f);
	} else {
		// assert PROG, FLASH and Xilinx disabled = Xilinx SPI master
		a16->csr = CPLD_CSR_XPROG << 8;
	}
	// remove everything
	a16->csr = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Get Def and module configuration data from config_t structure
void uwfd64::ReadConfig(config_t *cnf)
{
    	long tmp;
    	double dtmp;
    	char *stmp;
    	char str[1024];
	char sect[16];
	int i, j;
	config_setting_t *ptr;
	
	for (i=0; i<2; i++) {
		if (i) {
			sprintf(sect, "Dev%3.3d", GetSerial());
		} else {
			strcpy(sect, "Def");
		}
//	int MasterClockMux;	// master clock multiplexer setting 
		sprintf(str, "%s.MasterClockMux", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= MAIN_MUX_MASK;
			Conf.MasterClockMux = tmp;
		}
//	int MasterTrigMux;	// master trigger multiplexer setting 
		sprintf(str, "%s.MasterTrigMux", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= MAIN_MUX_MASK;
			Conf.MasterTrigMux = tmp;
		}
//	int MasterInhMux;	// master inhibit multiplexer setting
		sprintf(str, "%s.MasterInhMux", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= MAIN_MUX_MASK;
			Conf.MasterInhMux = tmp;
		}
//	int MasterClockDiv;	// Mater clock divider (0 - 1, 1 - 2, 2 - 4, 3 - 8)
		sprintf(str, "%s.MasterClockDiv", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 3;
			Conf.MasterClockDiv = tmp;
		}
//	int MasterClockErc;	// Mater clock edge control (0 - fast, 1 - medium, 2 - slow)
		sprintf(str, "%s.MasterClockErc", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp %= 3;
			Conf.MasterClockErc = tmp;
		}
//	int AuxTrigOut;		// Enable trigger+inhibit output on FP auxillary pair
		sprintf(str, "%s.AuxTrigOut", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp = (tmp) ? 1 : 0;
			Conf.AuxTrigOut = tmp;
		}
//	int AuxTrigIn;		// Enable auxillary trigger input
		sprintf(str, "%s.AuxTrigIn", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp = (tmp) ? 1 : 0;
			Conf.AuxTrigIn = tmp;
		}
//	int MasterTrig2FIFO;	// Enable trigger blocks (type 2) to data FIFO
		sprintf(str, "%s.MasterTrig2FIFO", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp = (tmp) ? 1 : 0;
			Conf.MasterTrig2FIFO = tmp;
		}
//	int TokenSync;		// Enable type 5 records
		sprintf(str, "%s.TokenSync", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp = (tmp) ? 1 : 0;
			Conf.TokenSync = tmp;
		}
//	int DAC;		// DAC setting
		sprintf(str, "%s.DAC", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0xFFFF;
			Conf.DAC = tmp;
		}
//	int TrigGenMask;	// Mask of slave xilinxes participating in trigger generation
		sprintf(str, "%s.TrigGenMask", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			Conf.TrigGenMask = tmp;
		}
//	int TrigOrTime;		// Number of clocks to OR trigger sources
		sprintf(str, "%s.TrigOrTime", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			Conf.TrigOrTime = tmp;
		}
//	int TrigBlkTime;	// Number of clocks to block trigger production
		sprintf(str, "%s.TrigBlkTime", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			Conf.TrigBlkTime = tmp;
		}
//	int TrigUserWord;	// 15-bit user word to be put to trigger block
		sprintf(str, "%s.TrigUserWord", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			Conf.TrigUserWord = tmp;
		}
//	int FifoBegin;		// Main FIFO start address in 8k blocks
		sprintf(str, "%s.FifoBegin", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0xFFFF;
			Conf.FifoBegin = tmp;
		}
//	int FifoEnd;		// Main FIFO end address in 8k blocks
		sprintf(str, "%s.FifoEnd", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0xFFFF;
			Conf.FifoEnd = tmp;
		}
//	int IODelay;		// ADC data delay in calibrated delay taps
		sprintf(str, "%s.IODelay", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0x3F;
			Conf.IODelay = tmp;
		}
//	char SlaveClockFile[MAX_PATH_LEN];	// Si5338 .h configuration file
		sprintf(str, "%s.SlaveClockFile", sect);
		if (config_lookup_string(cnf, str, (const char **) &stmp)) 
			strncpy(Conf.SlaveClockFile, stmp, MAX_PATH_LEN);
//	int TrigHistMask;	// Mask for slave Xilinxes history block
		sprintf(str, "%s.TrigHistMask", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0xF;
			Conf.TrigHistMask = tmp;
		}
//	int ZeroSupThreshold;	// Threshold for zero suppression in master trigger
		sprintf(str, "%s.ZeroSupThreshold", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0xFFF;
			Conf.ZeroSupThreshold = tmp;
		}
//	int SelfTrigThreshold;	// Threshold for self trigger
		sprintf(str, "%s.SelfTrigThreshold", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0xFFF;
			Conf.SelfTrigThreshold = tmp;
		}
//	int MasterTrigThreshold;	// Threshold for master trigger production
		sprintf(str, "%s.MasterTrigThreshold", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0xFFFF;
			Conf.MasterTrigThreshold = tmp;
		}
//	int SelfTriggerPrescale;	// Self trigger prescale
		sprintf(str, "%s.SelfTriggerPrescale", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0xFFFF;
			Conf.SelfTriggerPrescale = tmp;
		}
//	int WinLen;		// Waveform window length
		sprintf(str, "%s.WinLen", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0x1FF;
			Conf.WinLen = tmp;
		}
//	int TrigWinBegin;	// Master trigger window begin delay
		sprintf(str, "%s.TrigWinBegin", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0x3FF;
			Conf.TrigWinBegin = tmp;
		}
//	int SelfWinBegin;	// Self trigger window begin delay
		sprintf(str, "%s.SelfWinBegin", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0x3FF;
			Conf.SelfWinBegin = tmp;
		}
//	int SumWinBegin;	// Trigger sum window begin delay
		sprintf(str, "%s.SumWinBegin", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0x3FF;
			Conf.SumWinBegin = tmp;
		}
//	int SumDelay;		// Delay local channels for global sum
		sprintf(str, "%s.SumDelay", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0x1F;
			Conf.SumDelay = tmp;
		}
//	int ZSWinBegin;		// Zero suppression window begin (from TrigWinBegin)		
		sprintf(str, "%s.ZSWinBegin", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0x1FF;
			Conf.ZSWinBegin = tmp;
		}
//	int ZSWinEnd;		// Zero suppression window end (from TrigWinBegin)
		sprintf(str, "%s.ZSWinEnd", sect);
		if (config_lookup_int(cnf, str, &tmp)) {
			tmp &= 0x1FF;
			Conf.ZSWinEnd = tmp;
		}
//	float TrigCoef[64];	// Coefficients for trigger production
		sprintf(str, "%s.TrigCoef", sect);
		ptr = config_lookup(cnf, str);
		if (ptr) for (j=0; j<64; j++) Conf.TrigCoef[j] = config_setting_get_float_elem(ptr, j);
//	short int MainTrigMask[4];	// Mask channels from master trigger
		sprintf(str, "%s.MainTrigMask", sect);
		ptr = config_lookup(cnf, str);
		if (ptr) for (j=0; j<4; j++) Conf.MainTrigMask[j] = config_setting_get_int_elem(ptr, j);
//	short int SelfTrigMask[4];	// Mask channels from self trigger
		sprintf(str, "%s.SelfTrigMask", sect);
		ptr = config_lookup(cnf, str);
		if (ptr) for (j=0; j<4; j++) Conf.SelfTrigMask[j] = config_setting_get_int_elem(ptr, j);
//	short int TrigSumMask[4];	// Mask channels from trigger production sum
		sprintf(str, "%s.TrigSumMask", sect);
		ptr = config_lookup(cnf, str);
		if (ptr) for (j=0; j<4; j++) Conf.TrigSumMask[j] = config_setting_get_int_elem(ptr, j);
//	short int InvertMask[4];	// Mask channels for invertion
		sprintf(str, "%s.InvertMask", sect);
		ptr = config_lookup(cnf, str);
		if (ptr) for (j=0; j<4; j++) Conf.InvertMask[j] = config_setting_get_int_elem(ptr, j);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Module soft reset
void uwfd64::Reset(void) 
{
	a32->csr.out |= MAIN_CSR_RESET;
	a32->csr.out &= ~MAIN_CSR_RESET;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Fifo reset. 
//	mask = FIFO_CSR_HRESET - hard reset
//	mask = FIFO_CSR_SRESET - soft reset
void uwfd64::ResetFifo(int mask) 
{
	a32->fifo.csr |= mask & (FIFO_CSR_HRESET | FIFO_CSR_SRESET); 
	vmemap_usleep(2000);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Set or pulse soft trigger
//	freq > 0 - soft trigger period in ms
//	freq = 0 - forbid sof trigger
//	freq < 0 - single pulse
void uwfd64::SoftTrigger(int freq) 
{
	int tmp;
	if (freq < 0) {
		a32->trig.cnt = 0;
	} else {
		tmp = a32->trig.csr & (~TRIG_CSR_SOFT_MASK);
		tmp |=  (freq * TRIG_CSR_SOFT) & TRIG_CSR_SOFT_MASK;
		a32->trig.csr = tmp;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Do ADC data receive test similar to one dane during phase adjustment
int uwfd64::TestADCPhase(int cnt)
{
	int i, j, k, errcnt, irc, adc;
	errcnt = 0;
	// program ADC itself to produce 000111 data pattern
	for (i=0; i<16; i++) {
		adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
		// 0xE380 = b111000111000(0000), same as frame in bytewise 1xFrame mode, MSbits are transmitted
		if (ADCWrite(i, ADC_REG_PAT1L, 0x80)) return -3;		 
		if (ADCWrite(i, ADC_REG_PAT1H, 0xE3)) return -3;		 
		if (ADCWrite(i, ADC_REG_TEST, ADC_TEST_USER)) return -3;		 
	}

	for (j=0; j<cnt; j++) {
		// seq instability measurement
		if (irc = ADCCheckSeq(2)) return irc;
		// read counters and increment IODELAY
		for (i=0; i<16; i++) {
			adc = ICX_SLAVE_STEP * (i >> 2) + ICX_SLAVE_ADC + ICX_SLAVE_ADC_STEP * (i & 3);
			for (k=0; k<9; k++) {			
				irc = ICXRead(adc + ICX_SLAVE_ADC_CINS + k);
				printf("%c", (irc) ? '*' : '.');
				if (irc) errcnt++;
			}
			printf(" ");
			// increment IODELAY for all bits and frame
		}
		printf("\n");
	}
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write/read random number test of the ADC user pattern 1 LSB register
//	cnt - repeat counter
//	Return number of errors
int uwfd64::TestADCReg(int cnt)
{
	int i, j, r, w, errcnt;
	errcnt = 0;
	srand48(time(NULL));
	for (i=0; i < cnt; i++) for (j=0; j<15; j++) {
		w = mrand48() & 0xFF;
		ADCWrite(j, ADC_REG_PAT1L, w);
		r = ADCRead(j, ADC_REG_PAT1L) & 0xFF;
		if (w != r) errcnt++;
	}
	return errcnt;
}

void lslope(short * y, int len, double * slope, double * chi2) {
	double s=0;
	double sx=0;
	double sx2=0;
	double sy=0;
	double sxy=0;
	double a, b;
	int i;

	if (!len) {
		*slope = 0;
		*chi2 = 0;
		return;
	}
	for  (i=0; i<len; i++) {
		sx += (double)i;
		sx2 += (double)i * (double)i;
		sy += (double)y[i];
		sxy += (double)i * (double)y[i];
	}
	b = ((double)len * sxy - sx * sy)/((double)len * sx2 - sx * sx);
	a = (sy - b * sx)/(double)len;
		
	for  (i=0; i<len; i++) {
		s += (a + b * (double)i - (double)y[i]) * (a + b * (double)i - (double)y[i]);
	}
	*slope = b;
	*chi2 = s / (double)len;
	return;
}

// Colors
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

int analyse(short *buf, int len, int ttype, int token, int blklen, double *slope, double *chi2) {

	int pres[69];
	int i, j, rlen, blktype, chn, errflag, errcnt=0;

	memset(slope, 0, 68*sizeof(double));
	memset(chi2, 0, 68*sizeof(double));
	memset(pres, 0, sizeof(pres));

	// analyse
	for (j=0; j<len/sizeof(short); ) {
		// control word
		if (buf[j] & 0x8000) {
			rlen = buf[j] & 0x1FF;
			if (rlen == 0) {	// this is alignment
				j++;
				continue;
			}
			chn = (buf[j] >> 9) & 0x3F;
			blktype = (buf[j+1] >> 12) & 7;
			if (ttype < 0) {		// only selftriggers
				if (blktype != 0) {
					printf("Wrong blktype=%d encountered for selftrigger test, channel %2.2X\n", blktype, chn);
					errcnt++;
				} else {	// selftrigger block
					// extend sign in data
					for (i=0; i<rlen-2; i++) if (buf[j+i+3] & 0x4000) buf[j+i+3] |= 0x8000;
					lslope(&buf[j+3], rlen-2, &slope[chn], &chi2[chn]);
					pres[chn] ++;
				}
			} else {		// master trigger, trigger block or history block
				if (((token & 0x3FF) != (buf[j+1] & 0x3FF)) || (buf[j+1] & 0x400)) {
					printf("Token error in channel %2.2X: %3.3X(%3.3X)\n", chn, buf[j+1] & 0x7FF, token & 0x3FF);
					errcnt ++;
				}
				switch (blktype) {
				case 2:		// trigger block
					if (rlen != 7) {
						printf("Wrong length of trigger block %d != 7\n", rlen);
						errcnt ++;
						break;
					}
					if (!(((chn & 0xF) == 0xF) || ((chn & 0x10) == 0x10)) || ((chn & 0x20) != 0) ) {
						printf("Wrong trigger source %2.2X (0xF or 0x10)\n", chn);
						errcnt ++;
					}
					pres[68]++;
					break;
				case 4:		// history block
					if (rlen != blklen) {
						printf("Wrong length of history block %d != %d\n", rlen, blklen);
						errcnt ++;
						break;
					}
					// extend sign in data
					for (i=0; i<rlen-2; i++) if (buf[j+i+3] & 0x4000) buf[j+i+3] |= 0x8000;
					lslope(&buf[j+3], rlen-2, &slope[64 + (chn >> 4)], &chi2[64 + (chn >> 4)]);
					pres[64 + (chn >> 4)]++;
					break;
				case 1:		// channel master trigger
					if (rlen != blklen) {
						printf("Wrong length of master data block %d != %d\n", rlen, blklen);
						errcnt ++;
						break;
					}
					// extend sign in data
					for (i=0; i<rlen-2; i++) if (buf[j+i+3] & 0x4000) buf[j+i+3] |= 0x8000;
					lslope(&buf[j+3], rlen-2, &slope[chn], &chi2[chn]);
					pres[chn]++;
					break;
				default:		// wrong type
					printf("Wrong blktype=%d encountered for master trigger test, channel %2.2X\n", blktype, chn);
					errcnt++;
					break;
				}
			}
			j += rlen + 1;
		} else {
			j++;
			errcnt++;
		}
	}
	errflag = 0;
	for  (i=0; i<64; i++) {
		if (pres[i] != 1) {
			errcnt++;
			errflag = 1;
		}
	}
	if (ttype > 0) for  ( ; i<69; i++) {
		if (pres[i] != 1) {
			errcnt++;
			errflag = 1;
		}
	}
	if (errflag) {
		printf("%sNot all channels present or channel encountered >1 times%s\n", KRED, KNRM);
		for  (i=0; i< ((ttype<0) ? 64 : 69); i++) {
			if (i%32 == 0) printf("Channels %2d-%2d: ", i, i+31);
			printf("%s%6.1d%s", (pres[i] != 1) ? KRED : KNRM, pres[i], KNRM);
			if (i%32 == 31) printf("\n");
		}
		printf("\n");
	}
	return errcnt;

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Test All channels by applying fastest possible pedestal change
//	cnt - repeat counter, only sign matters, repetition count is alwys 1
//	positive cnt means use of master trigger, self triggers are not blocked
//	negative cnt means use of self trigger, master trigger is blocked by insensitivity in trigger register
//	Return number of errors
//
//	!!! to be used with t8.conf
//	
int uwfd64::TestAllChannels(int cnt)
{

	const int PED_HIGH = 0x200;		// highest pedestal setting to comply with linearity	
	const int PED_LOW = 0x4000-PED_HIGH;	// lowest pedestal setting to comply with linearity	
	const int PED_MID = 0x2000;		// middle DAC setting
	const int maxpeddev = 25;		// maximum allowed pedestal deviation from mid val
	const double fullscalenorm = 0.608;	// average full scale
	const double fullscaledev = 0.015;	// maximum allowed deviation from full scale
	const int maxnonlin = 3;		// maximum difference between halfscales
	const double maxnoise = 0.3;		// maximum averaged square of noise

	int i, j;
	int peds[3][64]; 
	double tmp;
	int token, fifobot, errcnt, len, blklen;
	short *buf;
	double slope[68], chi2[68]; 

	buf = (short *) malloc(MBYTE);
	if (!buf) return -1;

	// set inhibit
	Inhibit(1);

	errcnt = 0;

	// check pedestals at min, mid, max
	
	// switch pedestal calculators to freely follow the level
	for (i=0; i<4; i++) ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_CSR_OUT, 0);
	// set lowest possible pedestal
	DACSet(PED_LOW);
	// 80 usec to settle + 2*32 usec to recalculate
	vmemap_usleep(2000);
	for (i=0; i<64; i++) peds[0][i] = ICXRead(ICX_SLAVE_STEP * (i/16) + ICX_SLAVE_PED + (i%16));
	// set middle pedestal
	DACSet(PED_MID);
	// 80 usec to settle + 2*32 usec to recalculate
	vmemap_usleep(2000);
	for (i=0; i<64; i++) peds[1][i] = ICXRead(ICX_SLAVE_STEP * (i/16) + ICX_SLAVE_PED + (i%16));
	// set highest possible pedestal
	DACSet(PED_HIGH);
	// 80 usec to settle + 2*32 usec to recalculate
	vmemap_usleep(2000);
	for (i=0; i<64; i++) peds[2][i] = ICXRead(ICX_SLAVE_STEP * (i/16) + ICX_SLAVE_PED + (i%16));

	printf("                %s", KYEL);
	for (i=0; i<32; i++) printf("%6d", i);
	printf("%s\n", KNRM);	
	// print pedestal deviations
	for (i=0, tmp=0; i<64; i++) tmp += (peds[1][i] - 0x800); 
	printf("%sPedestal deviations from middle value (<%d), average = %4.1f%s\n", KCYN, maxpeddev, tmp/64., KNRM);
	for  (i=0; i<64; i++) {
		if (i%32 == 0) printf("Channels %2d-%2d: ", i, i+31);
		printf("%s%6d%s", (abs(peds[1][i] - 0x800) > maxpeddev) ? KRED : KNRM, peds[1][i] - 0x800, KNRM);
		if (i%32 == 31) printf("\n");
		if (abs(peds[1][i] - 0x800) > maxpeddev) errcnt++;
	}
	// full scale
	for (i=0, tmp=0; i<64; i++) tmp += (double)(peds[2][i] - peds[0][i])/(double)(PED_LOW-PED_HIGH)*4.; 
	printf("%sFull scale (ADChigh-ADClow)/(PEDHigh-PEDlow) (within %6.3f from %6.3f), average = %6.3f%s\n", KCYN, fullscaledev, fullscalenorm, tmp/64., KNRM);
	for  (i=0; i<64; i++) {
		if (i%32 == 0) printf("Channels %2d-%2d: ", i, i+31);
		tmp = (double)(peds[2][i] - peds[0][i])/(double)(PED_LOW-PED_HIGH)*4.;
		printf("%s%6.3f%s", (fabs(tmp - fullscalenorm) > fullscaledev) ? KRED : KNRM, tmp, KNRM);
		if (i%32 == 31) printf("\n");
		if (fabs(tmp - fullscalenorm) > fullscaledev) errcnt++;
	}
	// nonlinearity
	printf("%sNonlinearity (ADChigh-ADCmid)-(ADCmid-ADClow) (<%d)%s\n", KCYN, maxnonlin, KNRM);
	for  (i=0; i<64; i++) {
		if (i%32 == 0) printf("Channels %2d-%2d: ", i, i+31);
		printf("%s%6d%s", (abs(peds[2][i] + peds[0][i] - 2*peds[1][i]) > maxnonlin) ? KRED : KNRM, peds[2][i] + peds[0][i] - 2*peds[1][i], KNRM);
		if (i%32 == 31) printf("\n");
		if (abs(peds[2][i] + peds[0][i] - 2*peds[1][i]) > maxnonlin) errcnt++;
	}

	// set middle pedestal
	DACSet(PED_MID);
	vmemap_usleep(20000);

	// memorise last token
	token = a32->trig.cnt;
	// get data block length (from first X) as appears in the control word
	blklen = ICXRead(ICX_SLAVE_WINLEN) + 2;

	// check noise with soft trigger
	// fix pedestals and enable trigger history in all 4 X's
	for (i=0; i<4; i++) ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_CSR_OUT, SLAVE_CSR_PEDINHIBIT | SLAVE_CSR_HISTENABLE);
	// clear memory and define beginning of the data
	a32->fifo.csr &= ~FIFO_CSR_ENABLE;
	fifobot = (a32->fifo.win & 0xFFFF) << 13;
	// allow mwmory to get data (we always start from bot)
	a32->fifo.csr |= FIFO_CSR_ENABLE;
	SoftTrigger(-1);
	vmemap_usleep(5000);
	// check errors
	if ((j=a32->fifo.csr) & FIFO_CSR_ERROR) {
		printf("error in fifoCSR: %8.8X\n", j);
		free(buf);
		return -2;
	}
	len = a32->fifo.wptr - fifobot;		// we never wrap
	if (len > MBYTE) len = MBYTE;		// just for sure
	// read data		
	if (vmemap_a64_dma(dma_fd, GetBase64() + fifobot, (unsigned int *)buf, len, 0)) {
		free(buf);
		return -4;
	}
	// clear memory		
	a32->fifo.csr &= ~FIFO_CSR_ENABLE;
	// analyse
	errcnt += analyse(buf, len, 1, token++, blklen, slope, chi2);

	tmp = 0;
	for (i=0; i<64; i++) tmp += chi2[i];
	printf("%sNoise (<%4.2f), average=%6.3f%s\n", KCYN, maxnoise, tmp/64., KNRM);
	for  (i=0; i<64; i++) {
		if (i%32 == 0) printf("Channels %2d-%2d: ", i, i+31);
		printf("%s%6.3f%s", (chi2[i] > maxnoise) ? KRED : KNRM, chi2[i], KNRM);
		if (i%32 == 31) printf("\n");
		if (chi2[i] > maxnoise) errcnt++;
	}
	printf("History   0-3 : ");
	for  (i=0; i<4; i++) {
		printf("%s%6.3f%s", (0) ? KRED : KNRM, chi2[64+i], KNRM);
	}
	printf("\n");

	// Allow master trigger from all 4 X's if cnt > 0 (selftriggers otherwise), remove INH
	if (cnt > 0)	
		a32->trig.csr |= TRIG_CSR_CHAN_MASK;
	else
		a32->trig.csr &= ~TRIG_CSR_CHAN_MASK;
	Inhibit(0);

	// allow memory to get data (we always start from bot)
	a32->fifo.csr |= FIFO_CSR_ENABLE;

	// start ramp signal
	DACSet(PED_HIGH);
	vmemap_usleep(100);
	// make reverse pulse to cross zero fast		
	DACSet(PED_LOW);
	// check errors, data should be there by this time
	if ((j=a32->fifo.csr) & FIFO_CSR_ERROR) {
		printf("error in fifoCSR: %8.8X\n", j);
		free(buf);
		return -2;
	}
	len = a32->fifo.wptr - fifobot;		// we never wrap
	if (len > MBYTE) len = MBYTE;		// just for sure
	// read data		
	if (vmemap_a64_dma(dma_fd, GetBase64() + fifobot, (unsigned int *)buf, len, 0)) {
		free(buf);
		return -4;
	}
	// clear memory		
	a32->fifo.csr &= ~FIFO_CSR_ENABLE;
	// slowly settle normal ped
	DACSet(PED_MID+40);
	vmemap_usleep(100);
	DACSet(PED_MID+20);
	vmemap_usleep(50);
	DACSet(PED_MID+10);
	vmemap_usleep(20);
	DACSet(PED_MID+5);
	DACSet(PED_MID+2);
	vmemap_usleep(1000);

	errcnt += analyse(buf, len, cnt, token, blklen, slope, chi2);

	tmp = 0;
	for (i=0; i<64; i++) tmp += slope[i];
	printf("%sSignal slope, ADCu/clk, average=%6.3f%s\n", KCYN, tmp/64., KNRM);
	for  (i=0; i<64; i++) {
		if (i%32 == 0) printf("Channels %2d-%2d: ", i, i+31);
		printf("%s%6.3f%s", (0) ? KRED : KNRM, slope[i], KNRM);
		if (i%32 == 31) printf("\n");
	}
	printf("History   0-3 : ");
	for  (i=0; i<4; i++) {
		printf("%s%6.3f%s", (0) ? KRED : KNRM, slope[64+i], KNRM);
	}
	printf("\n");
	free(buf);
	return errcnt;

}



////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Get data via FIFO. Check block format
//	cnt - number of blocks to get
//	Return number of errors
int uwfd64::TestFifo(int cnt)
{
	int errcnt, eflag;
	unsigned short *buf;
	int i, j, k, len, rlen, raddr, waddr;
	int blkcnt;
	int fifobot, fifotop, fifosize;
	
	buf = (unsigned short *) malloc(MBYTE);
	if (!buf) return -1;
	errcnt = 0;
	rlen  = 0;
//		reset FIFO and get fifo parameters
	a32->fifo.csr &= ~FIFO_CSR_ENABLE;
	ResetFifo(FIFO_CSR_HRESET);
	i = a32->fifo.win;
	j = i & 0xFFFF;
	i = (i >> 16) & 0xFFFF;
	if (j >= i) {
		free(buf);	
		return -1;	// wrong sizes
	}
	fifobot = j << 13;
	fifotop = i << 13;
	fifosize = fifotop - fifobot;
	a32->fifo.csr |= FIFO_CSR_ENABLE;

	for (i = 0; i < cnt; i += blkcnt) {
		blkcnt = 0;
		eflag = 0;
		if ((k=a32->fifo.csr) & FIFO_CSR_ERROR) {
// printf("error in fifoCSR: %8.8X(%8.8X)\n", a32->fifo.csr, k);
// if (k & 8) {printf("debug = %8.8X\n", a32->fifo.win);}
			free(buf);
			return -2;
		}
		if (a32->fifo.csr & FIFO_CSR_EMPTY) continue;
		raddr = a32->fifo.rptr;
		waddr = a32->fifo.wptr;
		len = waddr - raddr;
		if (len < 0) len += fifosize;
		if (len < 0) {
			free(buf);
			return -3;
		}
		if (len > MBYTE) len = MBYTE;
		if (raddr + len > fifotop) len = fifotop - raddr;
//		printf("waddr = %X, raddr = %X, len = %X waddr-raddr = %X\n", waddr, raddr, len, waddr - raddr);
		if (vmemap_a64_dma(dma_fd, GetBase64() + raddr, (unsigned int *)buf, len, 0)) {
//		if (vmemap_a64_blkread(A64UNIT, GetBase64() + raddr, (unsigned int *)buf, len)) {
			free(buf);
			a32->fifo.csr = 0x90000000;	// en debug
			printf("dma read error:%m  debug = %8.8X\n", a32->fifo.csr);
			a32->fifo.csr = 0x80000000;	// dis debug
			return -4;
		}
//		printf("\nraddr = 0x%X  len = %d\n", raddr, len);
		for (k = 0; k < len / sizeof(short); k++) {
			if (buf[k] & 0x8000) {
//	control word
				if (rlen) {
					errcnt++;
					eflag++;
					printf("Error %6d: rlen = %4d  d = %4.4X @raddr = %X, addr = %X, len = %X\n", errcnt, rlen, buf[k], raddr, raddr + 2*k, len);
				}
				rlen = buf[k] & 0x1FF;
				blkcnt++;
//				printf("\nerrcnt = %d\n", errcnt);
			} else if (rlen) {
//	ordinary word
				rlen--;
			} else {
				errcnt++;
				eflag++;
				printf("Error %6d: rlen = %4d  d = %4.4X @raddr = %X, addr = %X, len = %X\n", errcnt, rlen, buf[k], raddr, raddr + 2*k, len);
			}
//			printf("%4.4X ", buf[k]);
		}
		if (eflag) {
			for (k = 0; k < len / sizeof(short) && k < 0x80; k++) {	// don't want to print too much
				if (!(k & 0xF)) printf("%6X: ", raddr + 2*k);
				printf("%4.4X ", buf[k]);
				if ((k & 0xF) == 0xF) printf("\n");
			}
			if (k & 0xF) printf("\n\n");
			// reread and print the same block
			if (vmemap_a64_blkread(A64UNIT, GetBase64() + raddr, (unsigned int *)buf, len)) {
				free(buf);
				return -4;
			}
			for (k = 0; k < len / sizeof(short) && k < 0x80; k++) {	// don't want to print too much
				if (!(k & 0xF)) printf("%6X: ", raddr + 2*k);
				printf("%4.4X ", buf[k]);
				if ((k & 0xF) == 0xF) printf("\n");
			}
			if (k & 0xF) printf("\n");
		}
		raddr += len;
		if (raddr == fifotop) raddr = fifobot;
		a32->fifo.rptr = raddr;
	}

	free(buf);
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write 1 MByte of the SDRAM memory and do random reads
//	cnt - number of reads
//	Return number of errors. Negative number is returned on access error.
int uwfd64::TestRandomRead(int cnt)
{
	int irc;
	int errcnt;
	int i, m;
	unsigned int *buf;
	unsigned int *ptr;
	unsigned int val;
	unsigned long long vme_addr;
	
	buf = (unsigned int *) malloc(MBYTE);
	if (!buf) return -1;
	errcnt = 0;	
	srand48(time(NULL));
	vme_addr = GetBase64();
	// Fill
	for(m = 0; m < MBYTE / sizeof(int); m++) buf[m] = mrand48();
	irc = vmemap_a64_dma(dma_fd, vme_addr, buf, MBYTE, 1);
	if (irc) {
		free(buf);
		return -2;
	}
	// Test
	ptr = vmemap_open(A64UNIT, vme_addr, MBYTE, VME_A64, VME_USER | VME_DATA, VME_D32);
	if (!ptr) {
		free(buf);
		return -3;
	}
	for (m = 0; m < cnt; m++) {
		i = mrand48() & (MBYTE / sizeof(int) - 1);
		val = ptr[i];
		if (val != buf[i]) {
			if (errcnt < 100) Log(DEBUG, "%8.8X: %8.8X (write) != %8.8X (read)\n", 
				i * sizeof(int), buf[i], val);
			errcnt++;
		}
	}
	vmemap_close(ptr);
	free(buf);
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write/read random number test of the dedicated test register
//	cnt - repeat counter
//	Return number of errors
int uwfd64::TestReg32(int cnt)
{
	int i, r, w, errcnt;
	errcnt = 0;
	srand48(time(NULL));
	for (i=0; i < cnt; i++) {
		w = mrand48();
		a32->ver.out = w;
		r = a32->ver.out;
		if (w != r) errcnt++;
	}
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write/read random number test of the SDRAM memory
//	cnt - amount of memory in Mbytes to test
//	Return number of errors. Negative number is returned on access error.
int uwfd64::TestSDRAM(int cnt)
{
	int irc;
	int errcnt, fcnt;
	int i, j, k, m;
	int seed;
	int flag;
	unsigned int *buf;
	unsigned int val;
	unsigned long long vme_addr;
	
	buf = (unsigned int *) malloc(MBYTE);
	if (!buf) return -1;
	errcnt = 0;
	fcnt = 0;	
	for (i = 0; i < cnt; i += j) {	// cycle over passes
		j = cnt - i;
		if (j > MEMSIZE/MBYTE) j = MEMSIZE/MBYTE;
		seed = time(NULL);
		// writing
		srand48(seed);
		vme_addr = GetBase64();
		for (k = 0; k < j; k++) {
			for(m = 0; m < MBYTE / sizeof(int); m++) buf[m] = mrand48();
			irc = vmemap_a64_dma(dma_fd, vme_addr, buf, MBYTE, 1);
//			printf("Writing: i=%d j=%d k=%d irc=%d\n", i, j, k, irc);
			if (irc) {
				free(buf);
				Log(ERROR, "VME DMA write error %m\n");
				return -2;
			}
			vme_addr += MBYTE;
		}
		// reading
		srand48(seed);
		vme_addr = GetBase64();
		for (k = 0; k < j; k++) {
			irc = vmemap_a64_dma(dma_fd, vme_addr, buf, MBYTE, 0);
//			irc = vmemap_a64_blkread(A64UNIT, vme_addr, buf, MBYTE);
//			printf("Reading: i=%d j=%d k=%d irc=%d\n", i, j, k, irc);
			if (irc) {
				free(buf);
				Log(ERROR, "VME DMA read error %m\n");
				return -3;
			}
			flag = 0;
			fcnt = errcnt;
			for(m = 0; m < MBYTE / sizeof(int); m++) {
				val = mrand48();
				if (buf[m] != val) {
					errcnt++;
					flag = 1;
					if (errcnt < 100) Log(DEBUG, "%8.8X: %8.8X (write) != %8.8X (read)\n", 
						k * MBYTE + m * sizeof(int), val, buf[m]);
				}
			}
			if (flag && fcnt < 100) {
				for(m = 0; m < MBYTE / sizeof(int); m++) {
					if ((m & 7) == 0) Log(DEBUG, "%8.8X: ", k * MBYTE + m * sizeof(int));
					Log(DEBUG, "%8.8X ", buf[m]);
					if ((m & 7) == 7) Log(DEBUG, "\n");
				}
				if (m & 7) Log(DEBUG, "\n");
//				goto fin;
			}
			vme_addr += MBYTE;
		}
	}
//fin:
	free(buf);
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write/read random number test of the dedicated test register in slave Xilinxes
//	cnt - repeat counter
//	Return number of errors
int uwfd64::TestSlaveReg16(int cnt)
{
	int i, j, r, w, errcnt;
	errcnt = 0;
	srand48(time(NULL));
	for (i=0; i < cnt; i++) for (j=0; j<3; j++) {
		w = mrand48() & 0xFFFF;
		ICXWrite(ICX_SLAVE_STEP * j + ICX_SLAVE_VER_OUT, w);
		r = ICXRead(ICX_SLAVE_STEP * j + ICX_SLAVE_VER_OUT) & 0xFFFF;
		if (w != r) errcnt++;
	}
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write User word which is added to trigger block information
void uwfd64::WriteUserWord(int num)
{
	int tmp;
	tmp = a32->csr.out;
	tmp &= ~MAIN_CSR_USER_MASK;
	tmp |= (num * MAIN_CSR_USER) & MAIN_CSR_USER_MASK;
	a32->csr.out = tmp;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Reset trigger counter and token in riggen module
void uwfd64::ZeroTrigger(void)
{
	a32->trig.gtime = 0;
}

