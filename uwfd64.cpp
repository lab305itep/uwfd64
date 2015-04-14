/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support UWFD64 modules.
*/
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "libvmemap.h"
#include "uwfd64.h"

//	Constructor - only set addresses here
uwfd64::uwfd64(int sernum, int gnum, unsigned short *space_a16, unsigned int *space_a32)
{
	serial = sernum;
	ga = gnum;
	a16 = (struct uwfd64_a16_reg *)((char *)space_a16 + serial * A16STEP);
	a32 = (struct uwfd64_a32_reg *)((char *)space_a32 + ga * A32STEP);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Read ADC chip 8-bit registers via SPI
//	num - 0-15 ADC chip number
//	addr - register address
//	return register value, negative on error.
int uwfd64::ADCRead(int num, int addr)
{
    	int xil;

    	xil = ICX_SLAVE_STEP * ((num >> 2) & 3);
    	if (ICXWrite(xil + ICX_SLAVE_SPI_CSR, SPI_CSR_CS << (num & 3))) goto err;	// frame begin
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, (addr >> 8) | 0x80)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, addr & 0xFF)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_CSR, SPI_CSR_CS << (num & 3) + SPI_CSR_DIR)) goto err;	// switch to input data
    	if (ICXWrite(xil + ICX_SLAVE_SPI_DAT, 0)) goto err;
    	if (ICXWrite(xil + ICX_SLAVE_SPI_CSR, 0)) goto err;			// frame end
    	return ICXRead(xil + ICX_SLAVE_SPI_DAT);
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
	for (i=0; i<3; i++) if(I2CRead(i) != CDCUN_OUT_DISABLE) errcnt++;
	for (i=3; i<8; i++) if(I2CRead(i) != s) errcnt++;
	
	return errcnt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Write common offset DAC via SPI. No way to read.
//	Return 0 if OK, -10 on timeout
int uwfd64::DACSet(int val)
{
	int i;
	a32->dac.csr = SPI_CSR_CS;		// crystall select
	a32->dac.dat = (val >> 8) & 0xFF;	// High byte
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
//	Do module initialization.
int uwfd64::Init(void)
{
	int i, s;
	int errcnt;
	
	errcnt = 0;
	// Set base address for A32 - emulate geographic and its parity
	s = 0;
	for (i=0; i<5; i++) if (ga & (1 << i)) s++;
	s = 1 - (s & 1);
	a16->c2x = (CPLD_C2X_RESET + ga * CPLD_C2X_GA + s * CPLD_C2X_PARITY) << 8;
	// Init main CSR
	a32->csr.out = 0;
	Reset();
	// Setup I2C on main xilinx
	a32->i2c.presc[0] = I2C_PRESC & 0xFF;
	a32->i2c.presc[1] = (I2C_PRESC >> 8) & 0xFF;
	a32->i2c.ctr = I2C_CTR_CORE_ENABLE;
	ConfigureMasterClock(0, 0, 0);	// internal clock, no divider.
	// Set DAC to middle range
	if(DACSet(0x2000)) errcnt++;
	// Set I2C on slave Xilinxes
	for (i=0; i<4; i++) {
		ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_PRCL, I2C_PRESC & 0xFF);
		ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_PRCH, (I2C_PRESC >> 8) & 0xFF);
		ICXWrite(ICX_SLAVE_STEP * i + ICX_SLAVE_I2C_CTR, I2C_CTR_CORE_ENABLE);
	}
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
//	Read Si5338 via I2C
//	num - slave Xilinx address
//	addr - register
//	Return 16-bit value if OK, -10 on error
int uwfd64::L2CRead(int num, int addr)
{
	int i;
	int val;

	// send chip address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, SI5338_ADDR)) goto err;;
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR, I2C_SR_START + I2C_SR_WRITE)) goto err;
    	for (i = 0; i < L2C_TIMEOUT; i++) if (!(ICXRead(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_CSR) & I2C_SR_TRANSFER_IN_PRG)) break;
	if (i == L2C_TIMEOUT) goto err;
	// send register address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, addr & 0x7F)) goto err;;
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
	// send register address
	if (ICXWrite(ICX_SLAVE_STEP * (num & 3) + ICX_SLAVE_I2C_DAT, addr & 0x7F)) goto err;;
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
//	Module soft reset
void uwfd64::Reset(void) 
{
	a32->csr.out |= MAIN_CSR_RESET;
	a32->csr.out &= ~MAIN_CSR_RESET;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	Configure Si5338
//	num - slave Xilinx number
//	fname - Si5338 text configuration file
//	Return 0 on OK, negative on error.
int uwfd64::Si5338Configure(int num, char *fname)
{

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
	int errcnt;
	int i, j, k, m;
	int seed;
	unsigned int *buf;
	unsigned long long vme_addr;
	
	buf = (unsigned int *) malloc(MBYTE);
	if (!buf) return -1;
	errcnt = 0;	
	for (i = 0; i < cnt; i += j) {	// cycle over passes
		j = cnt - i;
		if (j > MEMSIZE/MBYTE) j = MEMSIZE/MBYTE;
		seed = time(NULL);
		// writing
		srand48(seed);
		vme_addr = GetBase64();
		for (k = 0; k < j; k++) {
			for(m = 0; m < MBYTE / sizeof(int); m++) buf[m] = mrand48();
			irc = vmemap_a64_blkwrite(A64UNIT, vme_addr, buf, MBYTE);
//			printf("Writing: i=%d j=%d k=%d irc=%d\n", i, j, k, irc);
			if (irc) {
				free(buf);
				return -1;
			}
			vme_addr += MBYTE;
		}
		// reading
		srand48(seed);
		vme_addr = GetBase64();
		for (k = 0; k < j; k++) {
			irc = vmemap_a64_blkread(A64UNIT, vme_addr, buf, MBYTE);
//			printf("Reading: i=%d j=%d k=%d irc=%d\n", i, j, k, irc);
			if (irc) {
				free(buf);
				return -1;
			}
			for(m = 0; m < MBYTE / sizeof(int); m++) if (buf[m] != mrand48()) errcnt++;
			vme_addr += MBYTE;
		}
	}
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

