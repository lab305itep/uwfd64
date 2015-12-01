#ifndef RECFORMAT_H
#define RECFORMAT_H

struct rec_header_struct {
	int len;
	int cnt;
	int ip;
	int type;
	int time;
};

#define REC_BEGIN	1
#define REC_END		999
#define REC_WFDDATA	0x10000

#endif /* RECFORMAT_H */

