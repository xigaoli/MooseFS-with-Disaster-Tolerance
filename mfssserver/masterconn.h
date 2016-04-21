/*
   Copyright 2005-2010 Jakub Kruszona-Zawadzki, Gemius SA.

   This file is part of MooseFS.

   MooseFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   MooseFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with MooseFS.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MASTERCONN_H_
#define _MASTERCONN_H_

#include <inttypes.h>
#include <stdio.h>

//copy times of each file .
#define MAX_COPY_TIMES    (5)
#define MAX_PACKET_SIZE  (1500000)

#define META_DL_BLOCK 1000000
#define MAX_MSG_NUM (10)

#define MAX_BUF_LENG    (10000)


#define  MAX_COPY_TABLE_NUM           (512)
#define  MAX_FILE_MAP_TABLE_NUM    (81920)


#define  true   1
#define  false  0


//操作表中每个记录的大小
//#define OPERTABLE_NODE_SIZE   (sizeof(operationtable) -4)     // hide crash
#define OPERTABLE_NODE_SIZE   (sizeof(operationtable))


// mode
enum {FREE,CONNECTING,HEADER,DATA,KILL};

typedef struct packetstruct {
	struct packetstruct *next;
	uint8_t *startptr;
	uint32_t bytesleft;
	uint8_t *packet;
} packetstruct;



typedef struct masterconn {
	int mode;
	int sock;
	int32_t pdescpos;
	uint32_t lastread,lastwrite;
	uint8_t hdrbuff[8];
	packetstruct inputpacket;
	packetstruct *outputhead,**outputtail;
	uint32_t bindip;
	uint32_t masterip;
	uint16_t masterport;
	uint8_t masteraddrvalid;

	uint8_t downloadretrycnt;
	//uint8_t downloading;
	uint8_t oldmode;
	FILE *logfd;	// using stdio because this is text file
//	FILE *mainfd;	// using standard unix I/O because this is binary file
	uint64_t filesize;
	uint64_t dloffset;
	uint64_t dlstartuts;
} masterconn;



//void masterconn_stats(uint32_t *bin,uint32_t *bout);
//void masterconn_replicate_status(uint64_t chunkid,uint32_t version,uint8_t status);
int masterconn_init(void);

#endif
