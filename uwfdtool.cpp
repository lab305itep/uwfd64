#include <stdio.h>
#include <stdlib.h>
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
	void A16Read(int addr);
	void A16Write(int addr, int val);
	void A32Read(int addr);
	void A32Write(int addr, int val);
	void A64Read(long long addr);
	void A64Write(long long addr, int val);
	void DACSet(int serial = -1, int val = 0x2000);
	void I2CWrite(int serial, int addr, int ival);
	void I2CRead(int serial, int addr);	
	inline int IsOK(void) { return a16 && a32; };
	void ICXWrite(int serial, int addr, int ival);
	void ICXRead(int serial, int addr);
	void Init(int serial = -1);
	void List(void);
	void Prog(int serial = -1, char *fname = NULL);
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

void uwfd64_tool::A16Read(int addr)
{
	if (addr > 0x100 * A16STEP - 4) {
		printf("Address 0x%X is out of range: 0 - 0x%X\n", addr, 0x100 * A16STEP - 4);
	} else {
		printf("A16[0xA000 + 0x%X] = %X\n", addr, a16[addr/2]);
	}
}

void uwfd64_tool::A16Write(int addr, int val)
{
	if (addr > 0x100 * A16STEP - 4) {
		printf("Address 0x%X is out of range: 0 - 0x%X\n", addr, 0x100 * A16STEP - 4);
	} else {
		a16[addr/2] = val;
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
	case 1:
		irc = 0;
		for (i=0; i<cnt; i++) irc += ptr->ConfigureMasterClock(0, 0);
		break;
	default:
		irc = -10;
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
		printf("Module %d@%d ICX[%X] = %X\n", serial, ptr->GetGA(), addr, ptr->I2CRead(addr));
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

void uwfd64_tool::List(void)
{
	int i;
	printf("%d modules found:\n", N);
	if (N) {
		printf("No Serial  GA A16  A32      A64              Version  Done\n");
		for (i=0; i<N; i++) printf("%2d %3d:%3d %2d %4.4X %8.8X %16.16LX %8.8X %3s\n", 
			i + 1, array[i]->GetBatch(), array[i]->GetSerial(), array[i]->GetGA(), 
			A16BASE + array[i]->GetGA() * A16STEP, A32BASE + array[i]->GetGA() * A32STEP, 
			A64BASE + array[i]->GetGA() * A64STEP, array[i]->GetVersion(), 
			array[i]->IsDone() ? "Yes" : "No ");
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

void uwfd64_tool::Test(int serial, int type, int cnt)
{
	int i;
	uwfd64 *ptr;
	if (serial < 0) {
		for (i=0; i<N; i++) printf("Serial %d test %d done with %d/%d errors.\n",
			array[i]->GetSerial(), type, DoTest(array[i], type, cnt), cnt);
	} else {
		ptr = FindSerial(serial);
		if (ptr == NULL) {
			printf("Module %d not found.\n", serial);
			return;
		}
		printf("Serial %d test %d done with %d/%d errors.\n",
			ptr->GetSerial(), type, DoTest(ptr, type, cnt), cnt);
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
	printf("H - print this Help;\n");
	printf("I num|* - Init;\n");
	printf("L - List modules found;\n");
	printf("P num|* [filename.bin] - Program. Use filename.bin or just pulse prog pin;\n");
	printf("Q - Quit;\n");
	printf("T num|* type [cnt] - Test with type t and repeat counter cnt;\n");
	printf("\t0 - write/read 32-bit register with random numbers;\n");
	printf("\t1 - configure and read back master clock multiplexer TI CDCUN1208LP;\n");
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
		tool->Init(serial);
		break;
	case 'L':	// List
        	tool->List();
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

