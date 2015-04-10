/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support UWFD64 modules.
*/
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
//	Configure Master clock multiplexer CDCUN1208LP
//	sel - input clock select: 0 - internal, 1 - external
//	div - input clock divider: 0 - 1, 1 - 2, 2 - 4, 3 - 8.
//	Return 0 if OK, or number of errors met
int uwfd64::ConfigureMasterClock(int sel, int div)
{
	int errcnt;
	int w;
	errcnt = 0;
	int i;
		// Write
	if(I2CWrite(CDCUN_CTRL_ADDR, CDCUN_CTRL_RESET)) errcnt++;	// Disable clock
	w = (div & 3) << 4;						// divider
	w += (sel) ? CDCUN_INPUT_MUX_IN2 : CDCUN_INPUT_MUX_IN1;		// select
	if(I2CWrite(CDCUN_INPUT_ADDR, w)) errcnt++;			// Input control
	for (i=3; i<8; i++) if(I2CWrite(i, CDCUN_OUT_ERC_FAST + CDCUN_OUT_DIFF_ON)) errcnt++;	// Set outputs
	if(I2CWrite(CDCUN_CTRL_ADDR, 0)) errcnt++;	// Enable clock
		// Verify
	if(I2CRead(CDCUN_CTRL_ADDR)) errcnt++;
	if(I2CRead(CDCUN_INPUT_ADDR) != w) errcnt++;
	for (i=3; i<8; i++) if(I2CRead(i) != CDCUN_OUT_ERC_FAST + CDCUN_OUT_DIFF_ON) errcnt++;
	
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
//	Read CDCUN via I2C
//	Return 16-bit value if OK, -10 on error
int uwfd64::I2CRead(int addr)
{
	int i;
	int val;
	// send chip address
	a32->i2c.dat = CDCUN_ADDR;
	a32->i2c.csr = I2C_SR_START + I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send register address
	a32->i2c.dat = addr & 0x7F;
	a32->i2c.csr = I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send chip address again with direction bit set
	a32->i2c.dat = CDCUN_ADDR + I2C_DAT_DDIR;
	a32->i2c.csr = I2C_SR_START + I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
	if (i == I2C_TIMEOUT) goto err;
	// read high byte
	a32->i2c.csr = I2C_SR_READ;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
	if (i == I2C_TIMEOUT) goto err;
	val = (a32->i2c.dat && 0xFF) << 8;
	// read low byte
	a32->i2c.csr = I2C_SR_READ + I2C_SR_STOP + I2C_SR_ACK;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
	if (i == I2C_TIMEOUT) goto err;
	val += a32->i2c.dat && 0xFF;	
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
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send register address
	a32->i2c.dat = addr & 0x7F;
	a32->i2c.csr = I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send high byte
	a32->i2c.dat = (val >> 8) & 0xFF;
	a32->i2c.csr = I2C_SR_WRITE;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
	if (i == I2C_TIMEOUT) goto err;
	// send low byte
	a32->i2c.dat = val & 0xFF;
	a32->i2c.csr = I2C_SR_WRITE + I2C_SR_STOP;
    	for (i = 0; i < I2C_TIMEOUT; i++) if (!(a32->i2c.csr & I2C_SR_BUSY)) break;
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
	ConfigureMasterClock(0, 0);	// internal clock, no divider.
	// Set DAC to middle range
	if(DACSet(0x2000)) errcnt++;
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

