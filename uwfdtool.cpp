/*
	Moscow, ITEP, I. Alekseev, D. Kalinkin, D. Svirida
	Support UWFD64 modules. Test tool.
*/

#define _FILE_OFFSET_BITS 64
#include <libconfig.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <readline/history.h>
#include <readline/readline.h>
#include "libvmemap.h"
#include "log.h"
#include "recformat.h"
#include "uwfd64.h"
          
#define WAIT4DONE	1000	// 10 s
#define PS_ACTTIME	5	// s, Pseudo cycle active time
#define PS_WAIT		100000	// us, Pseudo cycle active passive time
#define BSIZE		0xC0000	// 3/4 MBYTE

class uwfd64_tool;
int Process(char *cmd, uwfd64_tool *tool);
int CheckCmd(void);
int TcpOpen(FILE **f, char *name);

volatile sig_atomic_t StopFlag;

void catch_stop(int sig)
{
	StopFlag = 1;
	signal(sig, catch_stop);
}

class uwfd64_tool {
private:
	uwfd64 *array[20];
	int N;
	unsigned short *a16;
	unsigned int *a32;
	int dma_fd;
	int DoTest(uwfd64 *ptr, int type, int cnt);
	uwfd64 *FindSerial(int num);
	int Status;
public:
	uwfd64_tool(const char *ini_file_name = NULL);
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
	inline void ClearStatus(void) { Status = 0;};
	void Adjust(int serial, int adc);
	void DACSet(int serial = -1, int val = 0x2000);
	inline int GetStatus(void) { return Status; };
	void I2CRead(int serial, int addr);	
	void I2CWrite(int serial, int addr, int ival);
	inline int IsOK(void) { return a16 && a32 && (dma_fd >= 0); };
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
	inline void SetStatus(void) { Status = 1;};
	void SoftTrigger(int serial, int freq);
	void Test(int serial = -1, int type = 0, int cnt = 1000000);
	void WriteFile(int serial, char *fname, int size);
	void WriteNFile(int serial, char *fname, int size, int flag);
	void ZeroTrigger(int serial);
};

uwfd64_tool::uwfd64_tool(const char *ini_file_name)
{
	uwfd64 *ptr;	
	config_t cnf;
	config_t *pcnf;
	config_setting_t *cptr;
	int i, num;

	N = 0;
	Status = 0;

	a16 = (unsigned short *) vmemap_open(A16UNIT, A16BASE, 0x100 * A16STEP, VME_A16, VME_USER | VME_DATA, VME_D16);
	a32 = vmemap_open(A32UNIT, A32BASE, 0x100 * A32STEP, VME_A32, VME_USER | VME_DATA, VME_D32);
	dma_fd = vmedma_open();
	if (!IsOK()) return;
	
	if (ini_file_name) {
		pcnf = &cnf;
		config_init(pcnf);
		if (config_read_file(pcnf, ini_file_name) != CONFIG_TRUE) {
        		printf("Configuration error in file %s at line %d: %s\n", ini_file_name, config_error_line(pcnf), config_error_text(pcnf));
            		pcnf = NULL;
    		}
	} else {
		pcnf = NULL;
	}

	LogInit(pcnf);
	cptr = (pcnf) ? config_lookup(pcnf, "ModuleList") : NULL;

	for (i = 0; i < 255 && N < 20; i++) {
		num = (cptr) ? config_setting_get_int_elem(cptr, i) : i;
		if (cptr && !num) break;
		ptr = new uwfd64(num, N + 2, a16, a32, dma_fd, pcnf);
		if (!ptr->IsHere()) {
			delete ptr;
			continue;
		}
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
	if (dma_fd >= 0) vmedma_close(dma_fd);
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

void uwfd64_tool::A64Dump(long long addr, int leng)
{
	int i, j;
	unsigned int *buf;
	unsigned short *sbuf;
	int len, blen;

	if (leng < 0) len = -leng; else len = leng;

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

	if (leng >= 0) {
		for (i = 0; i < len; i += 4) {
			if (!(i & 0x1C)) printf("A64[0xAAAAAA00_00000000 + 0x%LX]: ", addr + i);
			printf("%8.8X ", buf[i / 4]);
			if ((i & 0x1C) == 0x1C) printf("\n");
		}
		if (i & 0x1C) printf("\n");
	} else {
		// dump as block structure	
		sbuf = (unsigned short *)buf;
		for (i = 0; i < len; i += 2) {
			if (sbuf[i/2] & 0x8000) {
				blen = sbuf[i/2] & 0x1FF;
				printf("Block @ 0x%LX: Len = %3.3X ", addr + i, blen);		
				if (blen) {
					printf("Chan = %2.2X BlkType = %1X, Token = %3.3X Err = %d Par = %d Data:", (sbuf[i/2] >> 9) & 0x3F, (sbuf[i/2 + 1] >> 12) & 0x7, 
							sbuf[i/2 + 1] & 0x3FF, (sbuf[i/2 + 1] >> 10) & 0x1, (sbuf[i/2 + 1] >> 11) & 0x1);
					for (j=2; j<6 && j<blen + 1; j++) printf(" %4.4X", sbuf[i/2 + j]);
					printf(" ... ");
					for (j= (blen > 8) ? blen - 3 : 6; j<blen+1; j++) printf(" %4.4X", sbuf[i/2 + j]);
				} 
				printf(" Next CW: %s\n", (sbuf[i/2 + blen + 1]) ? "Yes" : "No");
			}		
		}
	}
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
	ClearStatus();
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
	ClearStatus();
}

void uwfd64_tool::Adjust(int serial, int adc)
{
	int i, irc;
	uwfd64 *ptr;
	irc = 0;
	if (serial < 0) {
		if (adc < 0) {
			for (i=0; i<N; i++) if (array[i]->ADCAdjust(0x3FFFF)) irc++;
		} else {
			for (i=0; i<N; i++) if (array[i]->ADCAdjust((1 << adc) | 0x30000)) irc++;
		}
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		if (adc < 0) {
			if(ptr->ADCAdjust(0x3FFFF)) irc++;
		} else {
			if(ptr->ADCAdjust((1 << adc) | 0x30000)) irc++;
		}
	}
	if (!irc) ClearStatus();
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
	ClearStatus();
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
	case 6:
		irc = ptr->TestFifo(cnt);
		break;
	case 7:
		irc = ptr->TestRandomRead(cnt);
		break;	
	case 8:
		irc = ptr->TestAllChannels(cnt);
		break;	
	default:
		irc = -10;
		break;
	}
	return irc;
}

uwfd64 *uwfd64_tool::FindSerial(int num)
{
	int i;
	for (i=0; i<N; i++) if (array[i]->GetSerial() == num) break;
	if (i == N) return NULL;
	return array[i];
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
	ClearStatus();
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
	ClearStatus();
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
	ClearStatus();
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
	ClearStatus();
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
	ClearStatus();
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
	ClearStatus();
}

void uwfd64_tool::Init(int serial)
{
	int i;
	uwfd64 *ptr;
	int errcnt;
	int irc;

	errcnt = 0;
	if (serial < 0) {
		printf("Init: ");
		for (i=0; i<N; i++) {
			irc = array[i]->Init();
			errcnt += irc;
			printf("%3d:%3s ", array[i]->GetSerial(), (irc) ? "Bad" : "OK");
			fflush(stdout);
		}
		printf("\n");
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		errcnt += ptr->Init();
	}
	if (errcnt) {
		printf("Initialization: %d errors found.\n", errcnt);
	} else {
		printf("Initialization done. No errors.\n");
		ClearStatus();
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
	ClearStatus();
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
	ClearStatus();
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
	int i, irc, errcnt;
	uwfd64 *ptr;
	
	errcnt = 0;
	if (serial < 0) {
		printf("Modules: ");
		for (i=0; i<N; i++) {
			array[i]->Prog(fname);
			printf("%3d ", array[i]->GetSerial());
			fflush(stdout);
		}
		printf("\n   Done: ");
		for (i=0; i<N; i++) {
			irc = array[i]->IsDone(WAIT4DONE);
			printf("%3s ", (irc) ? "Yes" : "No");	// 10 s
			fflush(stdout);
			if (!irc) errcnt++;
		}
		printf("\n");
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->Prog(fname);
		irc = ptr->IsDone(WAIT4DONE);
		printf("Done: %4s\n", (irc) ? "Yes" : "No");	// 10 s
		if (!irc) errcnt++;
	}
	if (!errcnt) ClearStatus();
}

void uwfd64_tool::ReadConfig(char *fname)
{
	config_t cnf;
	int i;

	config_init(&cnf);
	if (config_read_file(&cnf, fname) != CONFIG_TRUE) {
        	printf("Configuration error in file %s at line %d: %s\n", 
        		fname, config_error_line(&cnf), config_error_text(&cnf));
            	return;
    	}
	for (i=0; i<N; i++) array[i]->ReadConfig(&cnf);
	ClearStatus();
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
	ClearStatus();
}

void uwfd64_tool::Test(int serial, int type, int cnt)
{
	int i;
	int irc, errcnt;
	double dt;
	uwfd64 *ptr;
	struct timeval t[2];

	errcnt = 0;
	if (serial < 0) {
		for (i=0; i<N; i++) {
			gettimeofday(&t[0], NULL);
			irc = DoTest(array[i], type, cnt);
			gettimeofday(&t[1], NULL);
			dt = t[1].tv_sec - t[0].tv_sec + (t[1].tv_usec - t[0].tv_usec) * 0.000001;
			printf("Serial %d test %d done with %d/%d errors in %8.3f s.\n",
				array[i]->GetSerial(), type, irc, cnt, dt);
			if (irc) errcnt++;
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
		if (irc) errcnt++;
	}
	if (!errcnt) ClearStatus();
}

void uwfd64_tool::WriteFile(int serial, char *fname, int size)
{
	uwfd64 *ptr;
	FILE *f;
	long long i;
	long long S;
	int irc;
	void *buf;

	ptr = FindSerial(serial);
	if (ptr == NULL) {
		printf("Module %d not found.\n", serial);
		return;
	}

	buf = malloc(MBYTE);
	if (!buf) {
		printf("No memory: %m.\n");
		return;
	}

	f = fopen(fname, "wb");
	if (!f) {
		printf("Can not open file %s: %m.\n", fname);
		free(buf);
		return;
	}
	
	StopFlag = 0;
	ptr->EnableFifo(0);	// clear FIFO
	ptr->ResetFifo(FIFO_CSR_SRESET);
	ptr->EnableFifo(1);	// enable FIFO

	S = (long long) size * MBYTE;
	
	for (i=0; i<S && (!StopFlag); i += irc) {
		irc = ptr->GetFromFifo(buf, MBYTE);
		if (irc < 0) {
			printf("File write error %d\n", -irc);
			break;
		}
		if (irc == 0) {
			vmemap_usleep(10000);	// nothing was there - sleep some time
		} else {
			if (fwrite(buf, irc, 1, f) != 1) {
				printf("File write error: %m.\n");
				break;
			}
		}
	}

	ptr->EnableFifo(0);
	free(buf);
	fclose(f);
	printf("%Ld bytes written to file %s\n", i, fname);
	ClearStatus();
}

void uwfd64_tool::WriteNFile(int serial, char *fname, int size, int flag)
{
	uwfd64 *ptr;
	FILE *f;
	long long i;
	long long S;
	int j;
	int irc, jrc;
	void *buf;
	struct rec_header_struct header;
	char cmd[1024];
	int active[20];		// if array element is active
	int oldtime;
	int iflag;
	int iCycleCnt;
	uwfd64 *fptr;

	memset(&active, 0, sizeof(active));
	if (serial >= 0) {
		for (j = 0; j < N; j++) if (serial == array[j]->GetSerial()) break;
		if (j == N) {
			printf("Module %d not found.\n", serial);
			return;
		}
		active[j] = 1;
		fptr = array[j];
	} else {
		fptr = NULL;
		for (i = 0; i < 20; i++) if (array[i] && array[i]->GetVersion() > 0) {
			active[i] = 1;
			fptr = array[i];
		}
	}
	
	buf = malloc(BSIZE);
	if (!buf) {
		printf("No memory: %m.\n");
		return;
	}
	
	jrc = TcpOpen(&f, fname);	// if file name is host.address:port this will return proper stream
	if (jrc < 0) {
		free(buf);
		return;
	}
	if (!jrc) {
		f = fopen(fname, "wb");
		if (!f) {
			printf("Can not open file %s: %m.\n", fname);
			free(buf);
			return;
		}
	}
	
	header.len = sizeof(header);
	header.cnt = 0;
	header.ip = INADDR_LOOPBACK;		// 127.0.0.1 - loopback
	header.type = REC_BEGIN;
	header.time = time(NULL);
	if (fwrite(&header, sizeof(header), 1, f) != 1) {
		printf("File write error: %m.\n");
		fclose(f);
		return;
	}

	printf("Taking data (Q to stop)> ");
	fflush(stdout);
	StopFlag = 0;
	ClearStatus();
	if (flag == 'P') fptr->Inhibit(1);
	for (j = 0; j < N; j++) if (active[j]) {
		ptr = array[j];
		ptr->EnableFifo(0);	// clear FIFO
		ptr->ResetFifo(FIFO_CSR_SRESET);
		ptr->EnableFifo(1);	// enable FIFO
	}
	S = (long long) size * MBYTE;
	oldtime = time(NULL);
	iflag = 0;
	if (flag == 'P') {
//		fptr->ResetTrigCnt();
		fptr->WriteUserWord(iCycleCnt);
		fptr->Inhibit(0);
	}
	iCycleCnt = 0;
	
	for (i=0; (i < S || size <= 0) && (!StopFlag); i += irc) {
		if (CheckCmd() && fgets(cmd, sizeof(cmd), stdin)) {
			if (!isatty(STDIN_FILENO)) {
				printf(cmd);
				fflush(stdout);
			}
			if (Process(cmd, this)) break;
			printf("Taking data (Q to stop)> ");
			fflush(stdout);
		}
		if (flag == 'P' && time(NULL) - oldtime > PS_ACTTIME) {
			fptr->Inhibit(1);
			oldtime = time(NULL);
			iflag = 1;
			vmemap_usleep(PS_WAIT);
		}

		irc = 0;
		for (j = 0; j < N; j++) if (active[j]) {
			ptr = array[j];
			jrc = ptr->GetFromFifo(buf, BSIZE);
			if (jrc < 0) {
				printf("Module %d FIFO error %d\n", ptr->GetSerial(), -jrc);
				goto err;
			}
			irc += jrc;
			if (jrc > 0) {
				header.len = jrc + sizeof(header);
				header.cnt++;
				header.type = REC_WFDDATA + ptr->GetSerial();
				header.time = time(NULL);
				if (fwrite(&header, sizeof(header), 1, f) != 1) {
					printf("File write error: %m.\n");
					goto err;
				}
				if (fwrite(buf, jrc, 1, f) != 1) {
					printf("File write error: %m.\n");
					goto err;
				}
				fflush(f);
			}
		}
		if (iflag) {
			header.len = sizeof(header);
			header.cnt++;
			header.type = REC_PSEOC;
			header.time = time(NULL);
			if (fwrite(&header, sizeof(header), 1, f) != 1) {
				printf("File write error: %m.\n");
				goto err;
			}
//			fptr->ResetTrigCnt();
			fptr->Inhibit(0);
			iflag = 0;
			iCycleCnt++;
			fptr->WriteUserWord(iCycleCnt);
		}
		if (irc == 0) vmemap_usleep(10000);	// nothing was there - sleep some time
	}
	if (flag == 'P') {
		header.len = sizeof(header);
		header.cnt++;
		header.type = REC_PSEOC;
		header.time = time(NULL);
		if (fwrite(&header, sizeof(header), 1, f) != 1) {
			printf("File write error: %m.\n");
			goto err;
		}
		fptr->Inhibit(1);
		fflush(f);
	}

	header.len = sizeof(header);
	header.cnt++;
	header.type = REC_END;
	header.time = time(NULL);
	if (fwrite(&header, sizeof(header), 1, f) != 1) printf("File write error: %m.\n");

err:
	for (j = 0; j < N; j++) if (active[j]) array[j]->EnableFifo(0);
	free(buf);
	fclose(f);
	printf("%Ld bytes written to file %s\n", i, fname);
	SetStatus();	// this is possibly not error, but DSINK needs to know that we are not in aquisition state
}

void uwfd64_tool::ZeroTrigger(int serial)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) array[i]->ZeroTrigger();
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		ptr->ZeroTrigger();
	}
	ClearStatus();
}

//*************************************************************************************************************************************//
//	Check if we have a line from stdin. Use select.
int CheckCmd(void)
{
	fd_set set;
	int irc;
	
	struct timeval tmzero;
	FD_ZERO(&set);
	FD_SET(STDIN_FILENO, &set);
	tmzero.tv_sec = 0;
	tmzero.tv_usec = 0;
	irc = select(FD_SETSIZE, &set, NULL, NULL, &tmzero);
	if (irc < 0) return 0;
	return FD_ISSET(STDIN_FILENO, &set);
}

int TcpOpen(FILE **f, char *name)
{
	char *ptr;
	struct hostent *host;
	struct sockaddr_in hostname;
	int port;
	int fd;
	
	*f = NULL;
	
	ptr = strchr(name, ':');
	if (!ptr) return 0;
	*ptr = '\0';
	ptr++;
	
	host = gethostbyname(name);
	if (!host) {
		printf("host %s not found.\n", name);
		return -1;
	}
	port = strtol(ptr, NULL, 0);
	
	hostname.sin_family = AF_INET;
	hostname.sin_port = htons(port);
	hostname.sin_addr = *(struct in_addr *) host->h_addr;

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		printf("Can not create socket %m\n");
		return -1;
	}

	if (connect(fd, (struct sockaddr *)&hostname, sizeof(hostname))) {
		printf("Connection to %s:%d failed %m\n", name, port);
		return -1;
	}
	
	*f = fdopen(fd, "wb");
	if (!*f) {
		printf("fdopen error %m\n");
		return -1;
	}
	
	return 1;
}

void Help(void)
{
	printf("\t\tCommand tool for UWFD64 debugging\n");
	printf("Usage uwfdtool [options] [command]\n");
	printf("\tOptions:\n");
	printf("-c file_name - use configuration file file_name. No default configuration.\n");
	printf("\t\tConfiguration for modules could be set later in Init command.\n");
	printf("-h - print this message.\n"); 
	printf("\tCommand - set of interactive mode commands put in quotes.\n");
	printf("Multiple commands can be combined in a command separated by semicolons.\n");
	printf("\t\t\tInteractive commands:\n");
	printf("Ctrl^C could be pressed to stop some long commands\n");
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
	printf("K num|* freq - start soft trigger with period freq in ms. freq = 0 - stop soft trigger; freq < 0 - do a single pulse;\n");
	printf("L - List modules found;\n");
	printf("M num|* 0|1 - clear/set inhibit;\n");
	printf("N num size fname - get size Mbytes of data to file fname;\n");
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
	printf("\t5 - configure and read back slave clock controller SiLabs Si5338;\n");
	printf("\t6 - get blocks to fifo and check format (length and CW);\n");
	printf("\t7 - fill the 1st Mbyte with random numbers and do random address reads;\n");
	printf("\t8 - check all channels by applying various DAC settings (amplification, noise, slope).\n");
	printf("U num|* addr [data] - read/write 16-bit word to clock CDCUN1208LP chip using I2C;\n");
	printf("V num|* [nadc] - scan and adjust input data delays for module num adc nadc or all adc's if omitted\n");
	printf("W ms - wait ms milliseconds.\n");
	printf("X num|* addr [data] - send/receive data from slave Xilinxes via SPI. addr - SPI address.\n");
	printf("Y[P] num|* size|* fname - get size Mbytes of data to new format file fname;\n");
	printf("Z num|*. - reset trigger/token counters in triggen\n");
	printf("? - get return status of the last command\n");
}

int Process(char *cmd, uwfd64_tool *tool)
{
	const char DELIM[] = " \t\n\r=";
    	char *tok;
	int serial;
	int ival;
	int addr;
	int num;
	int flag;
	long long laddr;

    	tok = strtok(cmd, DELIM);
    	if (tok == NULL || strlen(tok) == 0) return 0;
	flag = toupper(tok[1]);
    	switch(toupper(tok[0])) {
	case 'A':	// A16
	    	tool->SetStatus();
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
		tool->ClearStatus();
		break;
	case 'B':	// A32
	    	tool->SetStatus();
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
		tool->ClearStatus();
		break;
	case 'C':	// A64
	    	tool->SetStatus();
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
		tool->ClearStatus();
		break;
	case 'D':	// DAC
	    	tool->SetStatus();
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
	    	tool->SetStatus();
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		tool->A16Dump(addr, (tok) ? strtol(tok, NULL, 0) : 0x10);
		tool->ClearStatus();
		break;
	case 'F':	// A32 dump
	    	tool->SetStatus();
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		addr = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		tool->A32Dump(addr, (tok) ? strtol(tok, NULL, 0) : 0x400);
		tool->ClearStatus();
		break;
	case 'G':	// A64 dump
	    	tool->SetStatus();
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need address.\n");
	    		Help();
	    		break;
		}
		laddr = strtoll(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		tool->A64Dump(laddr, (tok) ? strtol(tok, NULL, 0) : 0x400);
		tool->ClearStatus();
		break;
	case 'H':	// Help
        	Help();
        	tool->ClearStatus();
        	break;
	case 'I':	// Init
	    	tool->SetStatus();
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
	    	tool->SetStatus();
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
	    	tool->SetStatus();
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
        	tool->ClearStatus();
        	break;
	case 'M':	// Inhibit
	    	tool->SetStatus();
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
	case 'N':	// Write data to file
	    	tool->SetStatus();
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number.\n");
	    		Help();
	    		break;
		}
		serial = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need amount of data to get.\n");
	    		Help();
	    		break;
		}
		ival = strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need filename.\n");
	    		Help();
	    		break;
		}
		tool->WriteFile(serial, tok, ival);
		break;
	case 'P':	// Prog
	    	tool->SetStatus();
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
	    	tool->SetStatus();
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
	    	tool->SetStatus();
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
	    	tool->SetStatus();
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
	    	tool->SetStatus();
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
	case 'V':	// scan/adjust ADC delays
	    	tool->SetStatus();
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			ival = -1;
		} else {
			ival = strtol(tok, NULL, 0);
		}
		tool->Adjust(serial, ival);
		break;
	case 'W':	// wait ms
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			ival = 0;
		} else {
			ival = strtol(tok, NULL, 0);
		}
		vmemap_usleep(1000*ival);	// ms
	    	tool->ClearStatus();
		break;
	case 'X':	// ICX
	    	tool->SetStatus();
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
	case 'Y':	// Write data to file - new format, more than one module
	    	tool->SetStatus();
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need amount of data to get.\n");
	    		Help();
	    		break;
		}
		ival = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need filename.\n");
	    		Help();
	    		break;
		}
		tool->WriteNFile(serial, tok, ival, flag);
		break;
	case 'Z':
	    	tool->SetStatus();
		tok = strtok(NULL, DELIM);
		if (tok == NULL) {
			printf("Need module serial number or *.\n");
	    		Help();
	    		break;
		}
		serial = (tok[0] == '*') ? -1 : strtol(tok, NULL, 0);
		tool->ZeroTrigger(serial);
		break;
	case '?':
		printf("__%4.4d\n", tool->GetStatus());
		break;
	default:
	    	tool->SetStatus();
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
	char *ini_file_name;
	int c;
	
	ini_file_name = (char *) "uwfdtool.conf";
	for (;;) {
		c = getopt(argc, argv, "c:h");
		if (c == -1) break;
		switch (c) {
		case 'c':
			ini_file_name = optarg;
			break;
		case 'h':
		default:
			Help();
			return 0;
		}
	}

	if (!isatty(STDIN_FILENO)) {
		setvbuf(stdin, NULL, _IONBF, 0);
		printf("Non-interactive mode\n");
		fflush(stdout);
	}

	tool = new uwfd64_tool(ini_file_name);
//	printf("The tool created...\n");
	if (!tool->IsOK()) {
		printf("Can not open VME.\n");
		return -10;
	}

    	if (optind < argc) {
		ptr = argv[optind];
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
		signal(SIGINT, catch_stop);
		signal(SIGPIPE, catch_stop);
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
		signal(SIGPIPE, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		write_history(".uwfdtool_history");
    	}

	delete tool;
	return 0;
}

