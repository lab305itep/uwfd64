/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support UWFD64 modules. Test tool.
*/

#include <libconfig.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <readline/history.h>
#include <readline/readline.h>
#include "libvmemap.h"
#include "uwfd64.h"

#define WAIT4DONE	1000	// 10 s

class uwfd64_tool {
private:
	uwfd64 *array[20];
	int N;
	unsigned short *a16;
	unsigned int *a32;
	int DoTest(uwfd64 *ptr, int type, int cnt);
	uwfd64 *FindSerial(int num);
public:
	uwfd64_tool(void);
	~uwfd64_tool(void);
	void A16Dump(int addr, int len);
	void A16Read(int addr);
	void A16Write(int addr, int val);
	void A32Dump(int addr, int len);
	void A32Read(int addr);
	void A32Write(int addr, int val);
	void A64Dump(long long addr, int len);
	void A64Read(long long addr);
	void A64Write(long long addr, int val);
	void ADCRead(int serial, int num, int addr);	
	void ADCWrite(int serial, int num, int addr, int ival);
	void DACSet(int serial = -1, int val = 0x2000);
	void I2CRead(int serial, int addr);	
	void I2CWrite(int serial, int addr, int ival);
	inline int IsOK(void) { return a16 && a32; };
	void ICXDump(int serial, int addr, int len);
	void ICXRead(int serial, int addr);
	void ICXWrite(int serial, int addr, int ival);
	void Inhibit(int serial, int what);
	void Init(int serial = -1);
	void L2CRead(int serial, int num, int addr);	
	void L2CWrite(int serial, int num, int addr, int ival);
	void List(void);
	void Prog(int serial = -1, char *fname = NULL);
	void ReadConfig(char *fname);
	void SoftTrigger(int serial, int freq);
	void Test(int serial = -1, int type = 0, int cnt = 1000000);
};

uwfd64_tool::uwfd64_tool(void)
{
	uwfd64 *ptr;	
	int i;

	N = 0;

	a16 = (unsigned short *) vmemap_open(A16UNIT, A16BASE, 0x100 * A16STEP, VME_A16, VME_USER | VME_DATA, VME_D16);
	a32 = vmemap_open(A32UNIT, A32BASE, 0x100 * A32STEP, VME_A32, VME_USER | VME_DATA, VME_D32);
	if (!IsOK()) return;

	for (i = 0; i < 255 && N < 20; i++) {
		ptr = new uwfd64(i, N + 2, a16, a32);
		if (!ptr->IsHere()) continue;
		array[N] = ptr;
		N++;
	}
}

uwfd64_tool::~uwfd64_tool(void)
{
	int i;
	for (i = 0; i < N; i++) delete array[i];
	if (a16) vmemap_close((unsigned int *)a16);
	if (a32) vmemap_close(a32);
}

void uwfd64_tool::A16Dump(int addr, int len)
{
	int i;
	if (addr + len > 0x100 * A16STEP) {
		printf("Address 0x%X is out of range: 0 - 0x%X\n", addr + len, 0x100 * A16STEP);
	} else {
		for (i = 0; i < len; i += 2) {
			if (!(i & 0x1E)) printf("A16[0xA000 + 0x%X]: ", addr + i);
			printf("%4.4X ", a16[(addr + i) / 2]);
			if ((i & 0x1E) == 0x1E) printf("\n");
		}
		if (i & 0x1E) printf("\n");
	}
}

void uwfd64_tool::A16Read(int addr)
{
	if (addr > 0x100 * A16STEP - 2) {
		printf("Address 0x%X is out of range: 0 - 0x%X\n", addr, 0x100 * A16STEP - 4);
	} else {
		printf("A16[0xA000 + 0x%X] = %X\n", addr, a16[addr/2]);
	}
}

void uwfd64_tool::A16Write(int addr, int val)
{
	if (addr > 0x100 * A16STEP - 2) {
		printf("Address 0x%X is out of range: 0 - 0x%X\n", addr, 0x100 * A16STEP - 4);
	} else {
		a16[addr/2] = val;
	}
}

void uwfd64_tool::A32Dump(int addr, int len)
{
	int i;
	if (addr + len > 0x100 * A32STEP) {
		printf("Address 0x%X is out of range: 0 - 0x%X\n", addr + len, 0x100 * A32STEP);
	} else {
		for (i = 0; i < len; i += 4) {
			if (!(i & 0x1C)) printf("A32[0xAA000000 + 0x%X]: ", addr + i);
			printf("%8.8X ", a32[(addr + i) / 4]);
			if ((i & 0x1C) == 0x1C) printf("\n");
		}
		if (i & 0x1C) printf("\n");
	}
}

void uwfd64_tool::A32Read(int addr)
{
	if (addr > 0x100 * A32STEP - 4) {
		printf("Address 0x%X is out of range: 0 - 0x%X\n", addr, 0x100 * A32STEP - 4);
	} else {
		printf("A32[0xAA000000 + 0x%X] = %X\n", addr, a32[addr/4]);
	}
}

void uwfd64_tool::A32Write(int addr, int val)
{
	if (addr > 0x100 * A32STEP - 4) {
		printf("Address 0x%X is out of range: 0 - 0x%X\n", addr, 0x100 * A32STEP - 4);
	} else {
		a32[addr/4] = val;
	}
}

void uwfd64_tool::A64Dump(long long addr, int len)
{
	int i;
	unsigned int *buf;
	if (addr + len > 0x100 * A64STEP) {
		printf("Address 0x%LX is out of range: 0 - 0x%LX\n", addr + len, 0x100 * A64STEP);
		return;
	}
	buf = (unsigned int *)malloc(len);
	if (!buf) {
		printf("No memory.\n");
		return;
	}
	if (vmemap_a64_blkread(A64UNIT, A64BASE + addr, buf, len)) {
		printf("Read error.\n");
		return;
	}
	for (i = 0; i < len; i += 4) {
		if (!(i & 0x1C)) printf("A64[0xAAAAAA00_00000000 + 0x%LX]: ", addr + i);
		printf("%8.8X ", buf[i / 4]);
		if ((i & 0x1C) == 0x1C) printf("\n");
	}
	if (i & 0x1C) printf("\n");
}

void uwfd64_tool::A64Read(long long addr)
{
	if (addr > 0x100 * A64STEP - 4) {
		printf("Address 0x%LX is out of range: 0 - 0x%LX\n", addr, 0x100 * A64STEP - 4);
	} else {
		printf("A64[0xAAAAAA00_00000000 + 0x%LX] = %X\n", addr, vmemap_a64_read(A64UNIT, A64BASE + addr));
	}
}

void uwfd64_tool::A64Write(long long addr, int val)
{
	if (addr > 0x100 * A64STEP - 4) {
		printf("Address 0x%LX is out of range: 0 - 0x%LX\n", addr, 0x100 * A64STEP - 4);
	} else {
		vmemap_a64_write(A64UNIT, A64BASE + addr, val);
	}
}

void uwfd64_tool::ADCRead(int serial, int num, int addr)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) printf("Module %d@%d ADC[%X:%X] = %X\n", 
			array[i]->GetSerial(), array[i]->GetGA(), num, addr, array[i]->ADCRead(num, addr));
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		printf("Module %d@%d ADC[%X:%X] = %X\n", serial, ptr->GetGA(), num, addr, ptr->ADCRead(num, addr));
	}
}

void uwfd64_tool::ADCWrite(int serial, int num, int addr, int val)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->ADCWrite(num, addr, val);
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->ADCWrite(num, addr, val);
	}
}

void uwfd64_tool::DACSet(int serial, int val)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->DACSet(val);
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->DACSet(val);
	}
}

int uwfd64_tool::DoTest(uwfd64 *ptr, int type, int cnt)
{
	int irc;
	int i;
	switch(type) {
	case 0:
		irc = ptr->TestReg32(cnt);
		break;
	case 1:
		irc = 0;
		for (i=0; i<cnt; i++) irc += ptr->ConfigureMasterClock(0, 0);
		break;
	case 2:
		irc = ptr->TestSDRAM(cnt);
		break;
	case 3:
		irc = ptr->TestSlaveReg16(cnt);
		break;
	case 4:
		irc = ptr->TestADCReg(cnt);
		break;
	case 5:		
		irc = 0;
		for (i=0; i<cnt; i++) irc += ptr->ConfigureSlaveClock(i & 3, "Si5338-125MHz.h");
		break;
	default:
		irc = -10;
		break;
	}
	return irc;
}

void uwfd64_tool::I2CRead(int serial, int addr)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) printf("Module %d@%d ICX[%X] = %X\n", 
			array[i]->GetSerial(), array[i]->GetGA(), addr, array[i]->I2CRead(addr));
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		printf("Module %d@%d I2C[%X] = %X\n", serial, ptr->GetGA(), addr, ptr->I2CRead(addr));
	}
}

void uwfd64_tool::I2CWrite(int serial, int addr, int val)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->I2CWrite(addr, val);
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->I2CWrite(addr, val);
	}
}

void uwfd64_tool::ICXDump(int serial, int addr, int len)
{
	int i, j;
	uwfd64 *ptr;
	if (serial < 0) {
		for (j=0; j<N; j++) {
			for (i = 0; i < len; i++) {
				if (!(i & 0xF)) printf("ICX[%3d:%X]: ", array[j]->GetSerial(), addr + i);
				printf("%4.4X ", array[j]->ICXRead(addr + i) & 0xFFFF);
				if ((i & 0xF) == 0xF) printf("\n");
			}
			if (i & 0xF) printf("\n");
		}
	} else {
		ptr = FindSerial(serial);
		for (i = 0; i < len; i++) {
			if (!(i & 0xF)) printf("ICX[%3d:%X]: ", ptr->GetSerial(), addr + i);
			printf("%4.4X ", ptr->ICXRead(addr + i) & 0xFFFF);
			if ((i & 0xF) == 0xF) printf("\n");
		}
		if (i & 0xF) printf("\n");
	}
}

void uwfd64_tool::ICXRead(int serial, int addr)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) printf("Module %d@%d ICX[%X] = %X\n", 
			array[i]->GetSerial(), array[i]->GetGA(), addr, array[i]->ICXRead(addr));
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		printf("Module %d@%d ICX[%X] = %X\n", serial, ptr->GetGA(), addr, ptr->ICXRead(addr));
	}
}

void uwfd64_tool::ICXWrite(int serial, int addr, int val)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->ICXWrite(addr, val);
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->ICXWrite(addr, val);
	}
}

uwfd64 *uwfd64_tool::FindSerial(int num)
{
	int i;
	for (i=0; i<N; i++) if (array[i]->GetSerial() == num) break;
	if (i == N) return NULL;
	return array[i];
}

void uwfd64_tool::Inhibit(int serial, int what)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->Inhibit(what);
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->Inhibit(what);
	}
}

void uwfd64_tool::Init(int serial)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->Init();
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->Init();
	}
}

void uwfd64_tool::L2CRead(int serial, int num, int addr)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) printf("Module %d@%d Si5338[%X:%X] = %X\n", 
			array[i]->GetSerial(), array[i]->GetGA(), num, addr, array[i]->L2CRead(num, addr));
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		printf("Module %d@%d Si5338[%X:%X] = %X\n", serial, ptr->GetGA(), num, addr, ptr->L2CRead(num, addr));
	}
}

void uwfd64_tool::L2CWrite(int serial, int num, int addr, int val)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->L2CWrite(num, addr, val);
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->L2CWrite(num, addr, val);
	}
}

void uwfd64_tool::List(void)
{
	int i, j;
	printf("%d modules found:\n", N);
	if (N) {
		printf("No Serial  GA A16  A32      A64              Version  S0   S1   S2   S3   Done\n");
		for (i=0; i<N; i++) {
			j = array[i]->GetVersion();
			printf("%2d %3d:%3d %2d %4.4X %8.8X %16.16LX %8.8X:%4.4X:%4.4X:%4.4X:%4.4X %3s\n", 
				i + 1, array[i]->GetBatch(), array[i]->GetSerial(), array[i]->GetGA(), 
				array[i]->GetBase16(), array[i]->GetBase32(), array[i]->GetBase64(), 
				j, (j == -1) ? 0xFFFF : array[i]->GetSlaveVersion(0), (j == -1) ? 0xFFFF : array[i]->GetSlaveVersion(1), 
				(j == -1) ? 0xFFFF : array[i]->GetSlaveVersion(2), (j == -1) ? 0xFFFF : array[i]->GetSlaveVersion(3), array[i]->IsDone() ? "Yes" : "No ");
			if (j == -1) continue;
			printf("ADC: ");
			for (j=0; j<16; j++) printf("%4.4X ", array[i]->GetADCID(j));
			printf("\nSi5338:");
			for (j=0; j<4; j++) printf("%1.1X%2.2X%2.2X%2.2X%2.2X ", array[i]->L2CRead(j, 0),
				array[i]->L2CRead(j, 2), array[i]->L2CRead(j, 3), array[i]->L2CRead(j, 4), array[i]->L2CRead(j, 5));
			printf("\n");
		}
	}
}

void uwfd64_tool::Prog(int serial, char *fname)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->Prog(fname);
		for (i=0; i<N; i++) array[i]->IsDone(WAIT4DONE);	// 10 s
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->Prog(fname);
		ptr->IsDone(WAIT4DONE);	// 10 s
	}
}

void uwfd64_tool::ReadConfig(char *fname)
{
	config_t cnf;
    	long tmp;
    	double dtmp;
    	char *stmp;
    	char str[1024];
	int i;

	config_init(&cnf);
	if (config_read_file(&cnf, fname) != CONFIG_TRUE) {
        	printf("Configuration error in file %s at line %d: %s\n", 
        		fname, config_error_line(&cnf), config_error_text(&cnf));
            	return;
    	}
//		Read common settings
//	int MasterClockMux;	// master clock multiplexer setting 
	if (config_lookup_int(&cnf, "Def.MasterClockMux", &tmp)) {
		tmp &= MAIN_MUX_MASK;
		for (i=0; i<N; i++) array[i]->Conf.MasterClockMux = tmp;
	}
//	int MasterTrigMux;	// master trigger multiplexer setting 
	if (config_lookup_int(&cnf, "Def.MasterTrigMux", &tmp)) {
		tmp &= MAIN_MUX_MASK;
		for (i=0; i<N; i++) array[i]->Conf.MasterTrigMux = tmp;
	}
//	int MasterInhMux;	// master inhibit multiplexer setting
	if (config_lookup_int(&cnf, "Def.MasterInhMux", &tmp)) {
		tmp &= MAIN_MUX_MASK;
		for (i=0; i<N; i++) array[i]->Conf.MasterInhMux = tmp;
	}
//	int MasterClockDiv;	// Mater clock divider (0 - 1, 1 - 2, 2 - 4, 3 - 8)
	if (config_lookup_int(&cnf, "Def.MasterClockDiv", &tmp)) {
		tmp &= 3;
		for (i=0; i<N; i++) array[i]->Conf.MasterClockDiv = tmp;
	}
//	int MasterClockErc;	// Mater clock edge control (0 - fast, 1 - medium, 2 - slow)
	if (config_lookup_int(&cnf, "Def.MasterClockErc", &tmp)) {
		tmp %= 3;
		for (i=0; i<N; i++) array[i]->Conf.MasterClockErc = tmp;
	}
//	char SlaveClockFile[MAX_PATH_LEN];	// Si5338 .h configuration file
	if (config_lookup_string(&cnf, "Def.SlaveClockFile", (const char **) &stmp)) 
		for (i=0; i<N; i++) strncpy(array[i]->Conf.SlaveClockFile, stmp, MAX_PATH_LEN);
//	int DAC;		// DAC setting
	if (config_lookup_int(&cnf, "Def.DAC", &tmp)) {
		tmp &= 0xFFFF;
		for (i=0; i<N; i++) array[i]->Conf.DAC = tmp;
	}
//	int TrigGenMask;	// Mask of slave xilinxes participating in trigger generation
	if (config_lookup_int(&cnf, "Def.TrigGenMask", &tmp)) {
		for (i=0; i<N; i++) array[i]->Conf.TrigGenMask = tmp;
	}
//	int TrigOrTime;		// Number of clocks to OR trigger sources
	if (config_lookup_int(&cnf, "Def.TrigOrTime", &tmp)) {
		for (i=0; i<N; i++) array[i]->Conf.TrigOrTime = tmp;
	}
//	int TrigBlkTime;	// Number of clocks to block trigger production
	if (config_lookup_int(&cnf, "Def.TrigBlkTime", &tmp)) {
		for (i=0; i<N; i++) array[i]->Conf.TrigBlkTime = tmp;
	}
//	int TrigUserWord;	// 15-bit user word to be put to trigger block
	if (config_lookup_int(&cnf, "Def.TrigUserWord", &tmp)) {
		for (i=0; i<N; i++) array[i]->Conf.TrigUserWord = tmp;
	}
//	int FifoBegin;		// Main FIFO start address in 8k blocks
	if (config_lookup_int(&cnf, "Def.FifoBegin", &tmp)) {
		tmp &= 0xFFFF;
		for (i=0; i<N; i++) array[i]->Conf.FifoBegin = tmp;
	}
//	int FifoEnd;		// Main FIFO end address in 8k blocks
	if (config_lookup_int(&cnf, "Def.FifoEnd", &tmp)) {
		tmp &= 0xFFFF;
		for (i=0; i<N; i++) array[i]->Conf.FifoEnd = tmp;
	}

//		Read individual settings
	for (i=0; i<N; i++) {
//	int MasterClockMux;	// master clock multiplexer setting 
		sprintf(str, "Dev%3.3d.MasterClockMux", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			tmp &= MAIN_MUX_MASK;
			array[i]->Conf.MasterClockMux = tmp;
		}
//	int MasterTrigMux;	// master trigger multiplexer setting 
		sprintf(str, "Dev%3.3d.MasterTrigMux", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			tmp &= MAIN_MUX_MASK;
			array[i]->Conf.MasterTrigMux = tmp;
		}
//	int MasterInhMux;	// master inhibit multiplexer setting
		sprintf(str, "Dev%3.3d.MasterInhMux", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			tmp &= MAIN_MUX_MASK;
			array[i]->Conf.MasterInhMux = tmp;
		}
//	int MasterClockDiv;	// Mater clock divider (0 - 1, 1 - 2, 2 - 4, 3 - 8)
		sprintf(str, "Dev%3.3d.MasterClockDiv", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			tmp &= 3;
			array[i]->Conf.MasterClockDiv = tmp;
		}
//	int MasterClockErc;	// Mater clock edge control (0 - fast, 1 - medium, 2 - slow)
		sprintf(str, "Dev%3.3d.MasterClockErc", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			tmp %= 3;
			array[i]->Conf.MasterClockErc = tmp;
		}
//	char SlaveClockFile[MAX_PATH_LEN];	// Si5338 .h configuration file
		sprintf(str, "Dev%3.3d.SlaveClockFile", array[i]->GetSerial());
		if (config_lookup_string(&cnf, str, (const char **) &stmp)) 
			strncpy(array[i]->Conf.SlaveClockFile, stmp, MAX_PATH_LEN);
//	int DAC;		// DAC setting
		sprintf(str, "Dev%3.3d.DAC", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			tmp &= 0xFFFF;
			array[i]->Conf.DAC = tmp;
		}
//	int TrigGenMask;	// Mask of slave xilinxes participating in trigger generation
		sprintf(str, "Dev%3.3d.TrigGenMask", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			array[i]->Conf.TrigGenMask = tmp;
		}
//	int TrigOrTime;		// Number of clocks to OR trigger sources
		sprintf(str, "Dev%3.3d.TrigOrTime", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			array[i]->Conf.TrigOrTime = tmp;
		}
//	int TrigBlkTime;	// Number of clocks to block trigger production
		sprintf(str, "Dev%3.3d.TrigBlkTime", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			array[i]->Conf.TrigBlkTime = tmp;
		}
//	int TrigUserWord;	// 15-bit user word to be put to trigger block
		sprintf(str, "Dev%3.3d.TrigUserWord", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			array[i]->Conf.TrigUserWord = tmp;
		}
//	int FifoBegin;		// Main FIFO start address in 8k blocks
		sprintf(str, "Dev%3.3d.FifoBegin", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			tmp &= 0xFFFF;
			array[i]->Conf.FifoBegin = tmp;
		}
//	int FifoEnd;		// Main FIFO end address in 8k blocks
		sprintf(str, "Dev%3.3d.FifoEnd", array[i]->GetSerial());
		if (config_lookup_int(&cnf, str, &tmp)) {
			tmp &= 0xFFFF;
			array[i]->Conf.FifoEnd = tmp;
		}
	}
}

void uwfd64_tool::SoftTrigger(int serial, int freq)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->SoftTrigger(freq);
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->SoftTrigger(freq);
	}
}

void uwfd64_tool::Test(int serial, int type, int cnt)
{
	int i;
	int irc;
	double dt;
	uwfd64 *ptr;
	struct timeval t[2];

	if (serial < 0) {
		for (i=0; i<N; i++) {
			gettimeofday(&t[0], NULL);
			irc = DoTest(array[i], type, cnt);
			gettimeofday(&t[1], NULL);
			dt = t[1].tv_sec - t[0].tv_sec + (t[1].tv_usec - t[0].tv_usec) * 0.000001;
			printf("Serial %d test %d done with %d/%d errors in %8.3f s.\n",
				array[i]->GetSerial(), type, irc, cnt, dt);
		}
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		gettimeofday(&t[0], NULL);
		irc = DoTest(ptr, type, cnt);
		gettimeofday(&t[1], NULL);
		dt = t[1].tv_sec - t[0].tv_sec + (t[1].tv_usec - t[0].tv_usec) * 0.000001;
		printf("Serial %d test %d done with %d/%d errors in %8.3f s.\n",
			ptr->GetSerial(), type, irc, cnt, dt);
	}
}

//*************************************************************************************************************************************//

void Help(void)
{
	printf("\t\tCommand tool for UWFD64 debugging\n");
	printf("Numbers can be decimal, hex (0xX) or octal (0). num|* - module serial number, * - all modules.\n");
	printf("A addr [data] - read/write VME A16 (address is counted from 0xA000);\n");
	printf("B addr [data] - read/write VME A32 (address is counted from 0xAA000000);\n");
	printf("C addr [data] - read/write VME A64 (address is counted from 0xAAAAAA00_00000000);\n");
	printf("D num|* val - set common baseline DAC to val;\n");
	printf("E addr [len] - dump VME A16 (address is counted from 0xA000);\n");
	printf("F addr [len] - dump VME A32 (address is counted from 0xAA000000);\n");
	printf("G addr [len] - dump VME A64 (address is counted from 0xAAAAAA00_00000000);\n");
	printf("H - print this Help;\n");
	printf("I num|* [configfile] - Init, use current configuration or configfile if present;\n");
	printf("J num|* addr [len] - dump Slave Xilinxes 16-bit registers;\n");
	printf("K num|* freq - start soft trigger with frequency freq in Hz. freq = 0 - stop soft trigger; freq < 0 - do a single pulse;\n");
	printf("L - List modules found;\n");
	printf("M num|* 0|1 - clear/set inhibit;\n");
	printf("P num|* [filename.bin] - Program. Use filename.bin or just pulse prog pin;\n");
	printf("Q - Quit;\n");
	printf("R num|* adc addr [data] - read/write ADC Registers;\n");
	printf("S num|* xil addr [data] - read/write Si5338 registers;\n");
	printf("T num|* type [cnt] - Test with type t and repeat counter cnt;\n");
	printf("\t0 - write/read 32-bit register with random numbers;\n");
	printf("\t1 - configure and read back master clock multiplexer TI CDCUN1208LP;\n");
	printf("\t2 - test SDRAM;\n");
	printf("\t3 - write/read 16-bit register in slave Xilinxes with random numbers;\n");
	printf("\t4 - write/read 8-bit pattern 1 LSB register in ADCs with random numbers;\n");
	printf("\t5 - configure and read back slave clock controller SiLabs Si5338.\n");
	printf("U num|* addr [data] - read/write 16-bit word to clock CDCUN1208LP chip using I2C;\n");
	printf("X num|* addr [data] - send/receive data from slave Xilinxes via SPI. addr - SPI address.\n");
}

int Process(char *cmd, uwfd64_tool *tool)
{
	const char DELIM[] = " \t\n\r:=";
    	char *tok;
	int serial;
	int ival;
	int addr;
	int num;
	long long laddr;

    	tok = strtok(cmd, DELIM);
    	if (tok == NULL || strlen(tok) == 0) return 0;
    	switch(toupper(tok[0])) {
	case 'A':	// A16
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok) {
			ival = strtol(tok, NULL, 0);
			tool->A16Write(addr, ival);
		} else {
			tool->A16Read(addr);
		}
		break;
	case 'B':	// A32
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok) {
			ival = strtoul(tok, NULL, 0);
			tool->A32Write(addr, ival);
		} else {
			tool->A32Read(addr);
		}
		break;
	case 'C':	// A64
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		laddr = strtoll(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok) {
			ival = strtoul(tok, NULL, 0);
			tool->A64Write(laddr, ival);
		} else {
			tool->A64Read(laddr);
		}
		break;
	case 'D':	// DAC
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need value to be set.\n");
	    		Help();
	    		break;
		}
		ival = strtol(tok, NULL, 0);
		tool->DACSet(serial, ival);
		break;
	case 'E':	// A16 dump
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		tool->A16Dump(addr, (tok) ? strtol(tok, NULL, 0) : 0x10);
		break;
	case 'F':	// A32 dump
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		tool->A32Dump(addr, (tok) ? strtol(tok, NULL, 0) : 0x400);
		break;
	case 'G':	// A64 dump
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		laddr = strtoll(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		tool->A64Dump(laddr, (tok) ? strtol(tok, NULL, 0) : 0x400);
		break;
	case 'H':	// Help
        	Help();
        	break;
	case 'I':	// Init
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok) tool->ReadConfig(tok);
		tool->Init(serial);
		break;
	case 'J':	// ICX dump
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need start address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		ival = (tok) ? strtol(tok, NULL, 0) : 0x10;
		tool->ICXDump(serial, addr, ival);
		break;
	case 'K':	// Soft trigger
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need frequency.\n");
	    		Help();
	    		break;
		}
		ival = strtol(tok, NULL, 0);
		tool->SoftTrigger(serial, ival);
		break;
	case 'L':	// List
        	tool->List();
        	break;
	case 'M':	// Inhibit
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need what to do: 0/1.\n");
	    		Help();
	    		break;
		}
		ival = strtol(tok, NULL, 0);
		tool->Inhibit(serial, ival);
		break;
	case 'P':	// Prog
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		tool->Prog(serial, tok);
		break;
	case 'Q':	// Quit
        	return 1;
	case 'R':	// ADC Registers
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need ADC number (0-15) or *.\n");
	    		Help();
	    		break;
		}
		num = strtol(tok, NULL, 0) & 0xF;
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok) {
			ival = strtol(tok, NULL, 0);
			tool->ADCWrite(serial, num, addr, ival);
		} else {
			tool->ADCRead(serial, num, addr);
		}
		break;		
	case 'S':	// Si5338 Registers
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need ADC number (0-15) or *.\n");
	    		Help();
	    		break;
		}
		num = strtol(tok, NULL, 0) & 3;
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok) {
			ival = strtol(tok, NULL, 0);
			tool->L2CWrite(serial, num, addr, ival);
		} else {
			tool->L2CRead(serial, num, addr);
		}
		break;		
	case 'T':	// Test
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need test type.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		ival = (tok) ? strtol(tok, NULL, 0) : 10000;
		tool->Test(serial, addr, ival);
		break;
	case 'U':	// I2C
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok) {
			ival = strtol(tok, NULL, 0);
			tool->I2CWrite(serial, addr, ival);
		} else {
			tool->I2CRead(serial, addr);
		}
		break;		
	case 'X':	// ICX
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok) {
			ival = strtol(tok, NULL, 0);
			tool->ICXWrite(serial, addr, ival);
		} else {
			tool->ICXRead(serial, addr);
		}
		break;
	default:
		break;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int j;
    	char *cmd = NULL;
    	char tok[256];
    	char *ptr;
	uwfd64_tool *tool;

	tool = new uwfd64_tool();
	if (!tool->IsOK()) {
		printf("Can not open VME.\n");
		return -10;
	}

    	if (argc > 1) {
		ptr = argv[1];
		for (;ptr[0] != '\0';) {
	    		for (j=0; j < sizeof(tok)-1; j++) {
	        		if (ptr[j] == ';') {
	    	    			tok[j] = '\0';
		    			ptr += j+1;
		    			break;
				} else if (ptr[j] == '\0') {
	    	    			tok[j] = '\0';
		    			ptr += j;
		    			break;
				} else {
		    			tok[j] = ptr[j];
				}
	    		}
	    		if (j < sizeof(tok)-1) {
				Process(tok, tool);
	    		} else {
				printf("The single operation is too long: %s\n", ptr);
				break;
	    		}
		}
    	} else {
		read_history(".uwfdtool_history");
		for(;;) {
	    		if (cmd) free(cmd);
	    		cmd = readline("UWFDTool (H-help)>");
	    		if (cmd == NULL) {
                		printf("\n");
                		break;
            		}
	    		if (strlen(cmd) == 0) continue;
	    		add_history(cmd);
	    		if (Process(cmd, tool)) break;
		}
		if (cmd) free(cmd);
		write_history(".uwfdtool_history");
    	}

	delete tool;
	return 0;
}

