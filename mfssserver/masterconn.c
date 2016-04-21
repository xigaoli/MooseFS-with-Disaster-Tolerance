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




#include "config.h"

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>

#include "MFSCommunication.h"
#include "datapack.h"
#include "masterconn.h"
#include "crc.h"
#include "cfg.h"
#include "main.h"
#include "slogger.h"
#include "massert.h"
#include "sockets.h"
#include "common.h"
#include "pool.h"

#include "servermap.h"


#define  SSSLOG  syslog

#define  HASH_HEAD_SIZE  (100)




//主控数据结构
static masterconn *masterconnsingleton=NULL;

// from config
static uint32_t BackLogsNumber;
static uint32_t BackMetaCopies;
static char *MasterHost;
static char *MasterPort;
static char *BindHost;
static uint32_t Timeout;
static void* reconnect_hook;
//static void* download_hook;
//static uint64_t lastlogversion=0;

static uint32_t stats_bytesout=0;
static uint32_t stats_bytesin=0;
static char *chg_linedata[12]={0};
//static char *syncfiles[MaxSyncNum]={0};
//operation list head and tail
//static operationtable *operhead;
//static operationtable *opertail;

static operationtable *inode_hash_head [HASH_HEAD_SIZE];

//客户端挂载点
char  * MmountToPath;
char *  MmountFromPath;



void masterconn_stats(uint32_t *bin,uint32_t *bout) {
	*bin = stats_bytesin;
	*bout = stats_bytesout;
	stats_bytesin = 0;
	stats_bytesout = 0;
}


/**
Func Desc: 建立一个网络数据包
*/


uint8_t* masterconn_createpacket(masterconn *eptr,uint32_t type,uint32_t size) {
	packetstruct *outpacket;
	uint8_t *ptr;
	uint32_t psize;

	outpacket=(packetstruct*)malloc(sizeof(packetstruct));
	passert(outpacket);
	psize = size+8;
	outpacket->packet=malloc(psize);
	passert(outpacket->packet);
	outpacket->bytesleft = psize;
	ptr = outpacket->packet;
	put32bit(&ptr,type);
	put32bit(&ptr,size);
	outpacket->startptr = (uint8_t*)(outpacket->packet);
	outpacket->next = NULL;
	*(eptr->outputtail) = outpacket;
	eptr->outputtail = &(outpacket->next);
	return ptr;
}



/**
Func Desc: 发送注册包到Master
*/

void masterconn_sendregister(masterconn *eptr) {
	uint8_t *buff;

	//eptr->downloading=0;
	//if (eptr->mainfd==NULL) {
	//	mfs_errlog_silent(LOG_NOTICE,"error opening metafile");
	//	eptr->mode=KILL;
	//}
	
	eptr->logfd=NULL;

	mfs_syslog(LOG_NOTICE,"masterconn send register");
	buff = masterconn_createpacket(eptr,SSTOMA_REGISTER,1+4+2);
	put8bit(&buff,1);
	put16bit(&buff,VERSMAJ);
	put8bit(&buff,VERSMID);
	put8bit(&buff,VERSMIN);
	put16bit(&buff,Timeout);
	
}

/**
Func Desc: 向Master发送请求changelog 数据包
*/

void masterconn_chgdownload(){
	uint8_t *ptr;
	masterconn *eptr=masterconnsingleton;
	if ((eptr->mode==HEADER || eptr->mode==DATA) ) {
		//SSSLOG(LOG_NOTICE,"sending chg download require packet ");
		ptr = masterconn_createpacket(eptr,SSTOMA_DOWNLOAD_DATA,0);
		//put8bit(&ptr,filenum);
		
	}

}




void masterconn_beforeclose(masterconn *eptr) {
	//if (eptr->mainfd!=NULL) {
	//	fclose(eptr->mainfd);
	//	eptr->mainfd=NULL;
	//}
	if (eptr->logfd) {
		fclose(eptr->logfd);
		eptr->logfd = NULL;
	}
}


/**
Func Desc: 发送请求路径包
*/

void operationlist_name_requirepath(masterconn *eptr,operationtable *datanode) {
	uint8_t *ptr;
	if ((eptr->mode==HEADER || eptr->mode==DATA) ) {
		SSSLOG(LOG_NOTICE,"requirepath:sending inode=%d, type=%c, opername=%s, to name require ", datanode->inode, 
			datanode->type, datanode->operationname);
		ptr = masterconn_createpacket(eptr,SSTOMA_ID_DATA,4);
		put32bit(&ptr,datanode->inode);		
	}
}



void operationlist_hash_init(void) 
{

	memset(inode_hash_head, 0, HASH_HEAD_SIZE *sizeof( operationtable *));
	
}



/**
Func Desc: 用于调试节点列表信息
*/
void  debug_print_list(operationtable *datanode)
{

	if(!datanode)
	{
		SSSLOG(LOG_NOTICE, "null hash table");
		return ;
	}

	while(datanode) {
		SSSLOG(LOG_NOTICE, "print list : inode=%d, addr=%p", datanode->inode, datanode);
		datanode= datanode->next;
	}



}



operationtable * operationlist_nownode(int inode, char * operationname, char * filepath) 
{
	operationtable *datanode = (operationtable*)malloc(sizeof(operationtable));
	passert(datanode);
	memset(datanode,0,sizeof(operationtable));

	datanode->inode = inode;	
	strncpy(datanode->operationname,operationname, MAX_OPER_LENGTH-1);
	//strncpy(datanode->filepath,filepath, MAX_PATH_SIZE-1);
	//此处由CREATE创建的filepath仅仅是filename，由WRITE创建的filepath为NULL，故不应使用。真正的filepath会由
	//函数masterconn_savepath(masterconn * eptr, const uint8_t * data, uint32_t length)创建。

	datanode->lasttime=main_time();
	datanode->type='f';	

	return datanode;
	
}	
/**
Func Desc: 添加ChangLog节点到链表中
*/

void operationlist_addnode(operationtable *datanode) 
{
	int  index = datanode->inode % HASH_HEAD_SIZE;

//	SSSLOG(LOG_NOTICE, "addnode: inode=%d, addr=%p", datanode->inode, datanode);
	operationtable * temp = inode_hash_head[index];
	
	//SSSLOG(LOG_NOTICE, "lookup: inode=%d", datainode);

	while(temp) {
		if(temp->inode==datanode->inode) {
		//	inode exists, do nothing and return
			return;	
		}	
		temp=temp->next;
	}

	datanode->next = inode_hash_head[index];
	inode_hash_head[index] = datanode;

//	debug_print_list(inode_hash_head[index] );
	return ;
}


/**
Func Desc: 在链表中查找节点
*/

operationtable * operationlist_lookup(uint32_t datainode)
{
	int  index = datainode % HASH_HEAD_SIZE;
	operationtable * temp = inode_hash_head[index];

	//SSSLOG(LOG_NOTICE, "lookup: inode=%d", datainode);

	while(temp) {
		if(temp->inode==datainode) {
	//		SSSLOG(LOG_NOTICE, "addr=%p", temp);
			return temp;	
		}	
		temp=temp->next;
	}

	return NULL;	
}


/**
Func Desc: 释放链表中的一个节点
*/

void  operationlist_del_inode(int inodeno)
{
	int  index = -1;
	operationtable ** temp = NULL;
	operationtable  * p = NULL;
	
	index = inodeno % HASH_HEAD_SIZE;

	temp =&(inode_hash_head[index]);


	while(*temp)
	{
		if((*temp)->inode == inodeno)
		{
			p = (*temp);
			(*temp)= (*temp)->next;
			free(p);
			break;
		}

		temp = &((*temp)->next);

	}

	return ;

/*	
	operationtable * prev = temp;


	while(temp) {
		if(temp->inode  != inodeno) {
			prev = temp;
			temp=temp->next;			
		}else{
			prev->next  = temp ->next;
			SSSLOG(LOG_NOTICE, "del inode=%d, addr=%p", temp->inode, temp);			
			free(temp);
			debug_print_list(inode_hash_head[index] );
			return ;
		}	
	}

	return ;	

	*/
}


void opertaionlist_hashinode_delall()
{
	int  index ;
	operationtable * curr  = NULL;
	operationtable * next  = NULL;

	for(index =0; index < HASH_HEAD_SIZE; index++)
	{
		curr = inode_hash_head[index];
		while(curr){
			next = curr->next;
			free(curr);
			curr = next;
		}
	}

	memset(inode_hash_head, 0, HASH_HEAD_SIZE *sizeof( operationtable *));
	
}



void masterconn_operation_merge(masterconn *eptr,int inode, char * operationname, char * filepath) 
{
	operationtable * tmp = NULL;
	operationtable * now = NULL;

	if(inode <=0 )
		return ;

	tmp = operationlist_lookup(inode);
		 

	//如果是Create操作，建立新的节点
	if(!strcmp(operationname,"CREATE") || !strcmp(operationname,"WRITE")) 
	{
		
		if(tmp){
			//passert(0);  re incoming CREATE messeage!
			return ;
		}
	
		now = operationlist_nownode(inode, operationname, filepath);
		operationlist_addnode(now);
		return ;
	}


	if(!tmp){
		SSSLOG(LOG_NOTICE,"merge : not lookup inode ");
		return ;
	}

	
	//the unlink should be treated in 2 situation, the first is the operation is 
	//still in the list, in this situation only need to delete the node from the list.
	//the second is the operation is copied so add the unlink to the list
	//如果是unlink 操作，从链表中删除这个节点
	 if(!strcmp(operationname,"UNLINK")) 
	{		
		operationlist_del_inode(tmp->inode);		
	}
	//其他情况修改节点
	else	if(!strcmp(operationname,"RELEASE")) {
		strncpy(tmp->operationname, operationname, MAX_OPER_LENGTH-1);
		operationlist_name_requirepath(eptr,tmp);
	}
	
	return ;

}



/**
Func Desc: 分析ChangeLog数据
*/
void masterconn_chg_resolve(masterconn *eptr,const uint8_t *data,uint32_t length) {
	//split each field from an changelog
//	print_list("in  chg resolve  before");
	//char *chgdataperline=(char*)malloc(length+1);
	//passert(chgdataperline);
	//strcpy(chgdataperline,(char*)data);
	char *delimperline = "|(,): ";
	memset(chg_linedata,0,length);
	chg_linedata[0]=strtok(data, delimperline);
	for(int i=1;chg_linedata[i++] = strtok(NULL, delimperline););	

	int type = 0;
	int inode  = 0;
	int parentinode = 0;
	char * operationname = NULL; 
	char * filepath = NULL;
	
	//fill the datanode according to different operation
	//fill the operation table
	if(!strcmp(chg_linedata[1],"RELEASE"))
	{
		inode=atol(chg_linedata[2]);
		operationname = chg_linedata[1];
		SSSLOG(LOG_NOTICE,"MFS (RELEASE) inode =%d",inode);	
		masterconn_operation_merge(eptr,inode, operationname, filepath);		
	}	
	else if(!strcmp(chg_linedata[1],"CREATE")) {		
		SSSLOG(LOG_NOTICE,"MFS (CREATE) inode=%s, name=%s, type=%s",chg_linedata[9], chg_linedata[3], chg_linedata[4]);		
		type=chg_linedata[4][0];
		
		if(type=='f') {
			parentinode=atol(chg_linedata[2]);
			inode=atol(chg_linedata[9]);
			operationname=chg_linedata[1];
			filepath=chg_linedata[3];
			masterconn_operation_merge(eptr,inode, operationname, filepath);			
		}
		
	}	
	else if(!strcmp(chg_linedata[1],"WRITE")) 
	{
		inode=atol(chg_linedata[2]);	
		operationname =chg_linedata[1];				
		SSSLOG(LOG_NOTICE,"MFS (WRITE) inode= %s, chunkid=%s",chg_linedata[2], chg_linedata[5]);
		masterconn_operation_merge(eptr,inode, operationname, NULL);
		
	}
	else if(!strcmp(chg_linedata[1],"UNLINK")) 
	{			
		SSSLOG(LOG_NOTICE,"MFS (UNLINK) inode=%s, name=%s",chg_linedata[4], chg_linedata[3]);
		inode=atol(chg_linedata[4]);
		parentinode=atol(chg_linedata[2]);		
		operationname =chg_linedata[1];
		filepath=chg_linedata[3];		
		masterconn_operation_merge(eptr,inode, operationname, filepath);
		
	}
	else if(!strcmp(chg_linedata[1],"APPEND")) 
	{		
		inode=atol(chg_linedata[2]);
		type='f';
		operationname=chg_linedata[1];
		SSSLOG(LOG_NOTICE,"MFS APPEND (%d))",inode);
		masterconn_operation_merge(eptr,inode, operationname, NULL);
	}	
	else if(!strcmp(chg_linedata[1],"MOVE")) {
		SSSLOG(LOG_NOTICE,"MFS MOVE (%s))",chg_linedata[2]);
		//divide into three operation
		//the first del	ete	
		inode=(uint32_t)atol(chg_linedata[6]);
		parentinode=(uint32_t)atol(chg_linedata[2]);		
		type='f';
		filepath=chg_linedata[3];
		masterconn_operation_merge(eptr,inode, "UNLINK", filepath);

		
		//the second create
		inode=(uint32_t)atol(chg_linedata[6]);
		parentinode=(uint32_t)atol(chg_linedata[4]);
		type='f';		
		filepath=chg_linedata[5];	
		masterconn_operation_merge(eptr,inode, "CREATE", filepath);	
		masterconn_operation_merge(eptr,inode,"RELEASE", NULL);	
		
	}	


}




/**
Func Desc: 处理网络接收的ChangLog数据
*/

void masterconn_download_data(masterconn *eptr,const uint8_t *data,uint32_t length) {
	uint8_t *ptr;
	char * start = data;

	//int len = strlen(data);
	//SSSLOG(LOG_NOTICE,"geting chg download length=%d, data=%s",length,data);

	if(!data){
		SSSLOG(LOG_NOTICE,"error data packet");
		return ;
	}	
	
	
	if (length<4/*||leng!=length*/) {
		SSSLOG(LOG_NOTICE,"MATOSS_DOWNLOAD_DATA - wrong size (%"PRIu32"/16+data)",length);
		eptr->mode = KILL;
		return;
	}

	if(length == 4){
		SSSLOG(LOG_NOTICE,"there is no MFS changelog (%"PRIu32"): %s",length,data);
		return ;
	}

	while(1){
		char * context = strstr(start, "\n");

		if(context != NULL){
			* context = 0;
			masterconn_chg_resolve(eptr,(uint8_t*)start,context -start);
			start = context+1;
		}else{
			//masterconn_chg_resolve(eptr,(uint8_t*)start,strlen(start));
			break;
		}
			
	}
	
	ptr = masterconn_createpacket(eptr,SSTOMA_DOWNLOAD_END,0);


}

/**
Func Desc: 保存inode的路径和文件名信息
*/

void masterconn_savepath(masterconn *eptr,const uint8_t *data,uint32_t length)
{
	int ret = 0;	

	uint32_t inode;
	uint8_t type;
	char filepath[4096];

	FILE * fp = NULL;
	
	passert(data);

	inode=get32bit(&data);
	type=get8bit(&data);


	memset(filepath, 0, 4096);	
	strncpy(filepath, data, length-5);

	//using for delete node from the list
	operationtable *temp=operationlist_lookup(inode);
	if(temp == NULL){
		SSSLOG(LOG_NOTICE,"savepath: not lookup node =%d",inode);
		return ;
	}

	//strcat(filepath, temp->filepath);	
	SSSLOG(LOG_NOTICE,"savepath:inode=%d, path=%s , type=%c, opername =%s", inode, filepath, temp->type, temp->operationname);
	strcpy( temp->filepath, filepath);

	temp->flag = 1;
	ret = add_operchnglog_to_mapfile(temp);
	if(0 == ret){
		operationlist_del_inode(temp->inode);
		SSSLOG(LOG_NOTICE,"savepath: OK");
		
		
	}else{
		SSSLOG(LOG_NOTICE,"savepath:write node failed");
		
	}
		
	return ;
	
}


/**
Func Desc: Master发送的网络包处理
*/

void masterconn_gotpacket(masterconn *eptr,uint32_t type,const uint8_t *data,uint32_t length) {
	switch (type) {
		case ANTOAN_NOP:
			SSSLOG(LOG_NOTICE,"got NOP message (type:%"PRIu32")",type);
			break;
		case ANTOAN_UNKNOWN_COMMAND: // for future use
			break;
		case ANTOAN_BAD_COMMAND_SIZE: // for future use
			break;
		//get the increment of changelog 
		case MATOSS_DOWNLOAD_DATA:
			masterconn_download_data(eptr,data,length);
			break;
		//there is no increment of changelog
		case MATOSS_DOWNLOAD_END:
			//SSSLOG(LOG_NOTICE,"there is no chg change");
			break;
		//get the path from the inode pointed to file
		case MATOSS_ID_DATA:
			masterconn_savepath(eptr,data,length);
			break;
		default:
			SSSLOG(LOG_NOTICE,"got unknown message (type:%"PRIu32")",type);
			eptr->mode = KILL;
			break;
	}
}


/**
Func Desc: 终止连接的相关处理
*/
static void masterconn_term(void) {
	packetstruct *pptr,*paptr;
	masterconn *eptr = masterconnsingleton;

	SSSLOG(LOG_NOTICE,"masterconn_term");

	if (eptr->mode!=FREE) {
		tcpclose(eptr->sock);
		if (eptr->mode!=CONNECTING) {
			if (eptr->inputpacket.packet) {
				free(eptr->inputpacket.packet);
			}
			pptr = eptr->outputhead;
			while (pptr) {
				if (pptr->packet) {
					free(pptr->packet);
				}
				paptr = pptr;
				pptr = pptr->next;
				free(paptr);
			}
		}
	}
	
	//free the memory we applied
	for(int i=0;i<3;i++)
		free(chg_linedata[i]);

	free(eptr);
	free(MasterHost);
	free(MasterPort);
	free(BindHost);

	free(MmountToPath);
	free(MmountFromPath);
	
	opertaionlist_hashinode_delall();
	
	masterconnsingleton = NULL;

	syncserver_close_mmap();
}



/**
Func Desc: 网络已连接
*/

void masterconn_connected(masterconn *eptr) {
	tcpnodelay(eptr->sock);
	eptr->mode=HEADER;
	eptr->inputpacket.next = NULL;
	eptr->inputpacket.bytesleft = 8;
	eptr->inputpacket.startptr = eptr->hdrbuff;
	eptr->inputpacket.packet = NULL;
	eptr->outputhead = NULL;
	eptr->outputtail = &(eptr->outputhead);

	masterconn_sendregister(eptr);
	//if (lastlogversion==0) {
		//masterconn_metadownloadinit();
	//}
	
	eptr->lastread = eptr->lastwrite = main_time();
}

int masterconn_initconnect(masterconn *eptr) {
	int status;
	if (eptr->masteraddrvalid==0) {
		uint32_t mip,bip;
		uint16_t mport;
		if (tcpresolve(BindHost,NULL,&bip,NULL,1)>=0) {
			eptr->bindip = bip;
		} else {
			eptr->bindip = 0;
		}
		if (tcpresolve(MasterHost,MasterPort,&mip,&mport,0)>=0) {
			eptr->masterip = mip;
			eptr->masterport = mport;
			eptr->masteraddrvalid = 1;
		} else {
			mfs_arg_syslog(LOG_WARNING,"can't resolve master host/port (%s:%s)",MasterHost,MasterPort);
			return -1;
		}
	}
	eptr->sock=tcpsocket();
	if (eptr->sock<0) {
		mfs_errlog(LOG_WARNING,"create socket, error");
		return -1;
	}
	if (tcpnonblock(eptr->sock)<0) {
		mfs_errlog(LOG_WARNING,"set nonblock, error");
		tcpclose(eptr->sock);
		eptr->sock = -1;
		return -1;
	}
	if (eptr->bindip>0) {
		if (tcpnumbind(eptr->sock,eptr->bindip,0)<0) {
			mfs_errlog(LOG_WARNING,"can't bind socket to given ip");
			tcpclose(eptr->sock);
			eptr->sock = -1;
			return -1;
		}
	}
	status = tcpnumconnect(eptr->sock,eptr->masterip,eptr->masterport);
	if (status<0) {
		mfs_errlog(LOG_WARNING,"connect failed, error");
		tcpclose(eptr->sock);
		eptr->sock = -1;
		eptr->masteraddrvalid = 0;
		return -1;
	}
	if (status==0) {
		SSSLOG(LOG_NOTICE,"connected to Master immediately");
		masterconn_connected(eptr);
	} else {
		eptr->mode = CONNECTING;
		SSSLOG(LOG_NOTICE,"connecting ...");
	}
	return 0;
}

void masterconn_connecttest(masterconn *eptr) {
	int status;

	status = tcpgetstatus(eptr->sock);
	if (status) {
		mfs_arg_errlog_silent(LOG_WARNING,"connection failed, ip=%s, port=%d,error", MasterHost, MasterPort);
		tcpclose(eptr->sock);
		eptr->sock = -1;
		eptr->mode = FREE;
		eptr->masteraddrvalid = 0;
	} else {
		SSSLOG(LOG_NOTICE,"connected to Master");
		//if(eptr->mainfd==NULL)
		//	eptr->mainfd = fopen("operationFromChg","a+");
			
		masterconn_connected(eptr);
	}
}

void masterconn_read(masterconn *eptr) {
	int32_t i;
	uint32_t type,size;
	const uint8_t *ptr;
	for (;;) {
		i=read(eptr->sock,eptr->inputpacket.startptr,eptr->inputpacket.bytesleft);
		if (i==0) {
			SSSLOG(LOG_NOTICE,"connection was reset by Master");
			eptr->mode = KILL;
			return;
		}
		if (i<0) {
			if (errno!=EAGAIN) {
				mfs_errlog_silent(LOG_NOTICE,"read from Master error");
				eptr->mode = KILL;
			}
			return;
		}
		stats_bytesin+=i;
		eptr->inputpacket.startptr+=i;
		eptr->inputpacket.bytesleft-=i;

		if (eptr->inputpacket.bytesleft>0) {
			return;
		}

		if (eptr->mode==HEADER) {
			ptr = eptr->hdrbuff+4;
			size = get32bit(&ptr);

			if (size>0) {
				if (size>MAX_PACKET_SIZE) {
					SSSLOG(LOG_WARNING,"Master packet too long (%"PRIu32"/%u)",size,MAX_PACKET_SIZE);
					eptr->mode = KILL;
					return;
				}
				eptr->inputpacket.packet = malloc(size);
				passert(eptr->inputpacket.packet);
				eptr->inputpacket.bytesleft = size;
				eptr->inputpacket.startptr = eptr->inputpacket.packet;
				eptr->mode = DATA;
				continue;
			}
			eptr->mode = DATA;
		}

		if (eptr->mode==DATA) {
			ptr = eptr->hdrbuff;
			type = get32bit(&ptr);
			size = get32bit(&ptr);

			eptr->mode=HEADER;
			eptr->inputpacket.bytesleft = 8;
			eptr->inputpacket.startptr = eptr->hdrbuff;

			masterconn_gotpacket(eptr,type,eptr->inputpacket.packet,size);

			if (eptr->inputpacket.packet) {
				free(eptr->inputpacket.packet);
			}
			eptr->inputpacket.packet=NULL;
		}
	}
}

void masterconn_write(masterconn *eptr) {
	packetstruct *pack;
	int32_t i;
	for (;;) {
		pack = eptr->outputhead;
		if (pack==NULL) {
			return;
		}
		i=write(eptr->sock,pack->startptr,pack->bytesleft);
		if (i<0) {
			if (errno!=EAGAIN) {
				mfs_errlog_silent(LOG_NOTICE,"write to Master error");
				eptr->mode = KILL;
			}
			return;
		}
		stats_bytesout+=i;
		pack->startptr+=i;
		pack->bytesleft-=i;
		if (pack->bytesleft>0) {
			return;
		}
		free(pack->packet);
		eptr->outputhead = pack->next;
		if (eptr->outputhead==NULL) {
			eptr->outputtail = &(eptr->outputhead);
		}
		free(pack);
	}
}


void masterconn_desc(struct pollfd *pdesc,uint32_t *ndesc) {
	uint32_t pos = *ndesc;
	masterconn *eptr = masterconnsingleton;

	eptr->pdescpos = -1;
	if (eptr->mode==FREE || eptr->sock<0) {
		return;
	}
	if (eptr->mode==HEADER || eptr->mode==DATA) {
		pdesc[pos].fd = eptr->sock;
		pdesc[pos].events = POLLIN;
		eptr->pdescpos = pos;
		pos++;
	}
	if (((eptr->mode==HEADER || eptr->mode==DATA) && eptr->outputhead!=NULL) || eptr->mode==CONNECTING) {
		if (eptr->pdescpos>=0) {
			pdesc[eptr->pdescpos].events |= POLLOUT;
		} else {
			pdesc[pos].fd = eptr->sock;
			pdesc[pos].events = POLLOUT;
			eptr->pdescpos = pos;
			pos++;
		}
	}
	*ndesc = pos;
}

void masterconn_serve(struct pollfd *pdesc) {
	uint32_t now=main_time();
	packetstruct *pptr,*paptr;
	masterconn *eptr = masterconnsingleton;

	if (eptr->pdescpos>=0 && (pdesc[eptr->pdescpos].revents & (POLLHUP | POLLERR))) {
		if (eptr->mode==CONNECTING) {
			masterconn_connecttest(eptr);
		} else {
			eptr->mode = KILL;
		}
	}
	if (eptr->mode==CONNECTING) {
		if (eptr->sock>=0 && eptr->pdescpos>=0 && (pdesc[eptr->pdescpos].revents & POLLOUT)) { // FD_ISSET(eptr->sock,wset)) {
			masterconn_connecttest(eptr);
		}
	} else {
		if (eptr->pdescpos>=0) {
			if ((eptr->mode==HEADER || eptr->mode==DATA) && (pdesc[eptr->pdescpos].revents & POLLIN)) { // FD_ISSET(eptr->sock,rset)) {
				eptr->lastread = now;
				masterconn_read(eptr);
			}
			if ((eptr->mode==HEADER || eptr->mode==DATA) && (pdesc[eptr->pdescpos].revents & POLLOUT)) { // FD_ISSET(eptr->sock,wset)) {
				eptr->lastwrite = now;
				masterconn_write(eptr);
			}
			if ((eptr->mode==HEADER || eptr->mode==DATA) && eptr->lastread+Timeout<now) {
				eptr->mode = KILL;
			}
			if ((eptr->mode==HEADER || eptr->mode==DATA) && eptr->lastwrite+(Timeout/3)<now && eptr->outputhead==NULL) {
				masterconn_createpacket(eptr,ANTOAN_NOP,0);
			}
		}
	}
	if (eptr->mode == KILL) {
		masterconn_beforeclose(eptr);
		tcpclose(eptr->sock);
		if (eptr->inputpacket.packet) {
			free(eptr->inputpacket.packet);
		}
		pptr = eptr->outputhead;
		while (pptr) {
			if (pptr->packet) {
				free(pptr->packet);
			}
			paptr = pptr;
			pptr = pptr->next;
			free(paptr);
		}
		eptr->mode = FREE;
	}
}

void masterconn_reconnect(void) {
	masterconn *eptr = masterconnsingleton;
	if (eptr->mode==FREE) {
		masterconn_initconnect(eptr);
	}
}

void masterconn_reload(void) {
	masterconn *eptr = masterconnsingleton;
	uint32_t ReconnectionDelay;
	uint32_t MetaDLFreq;

	free(MasterHost);
	free(MasterPort);
	free(BindHost);

	free(MmountToPath);
	free(MmountFromPath);

	MasterHost = cfg_getstr("MASTER_HOST","mfsmaster");
	MasterPort = cfg_getstr("MASTER_PORT","9418");
	BindHost = cfg_getstr("BIND_HOST","*");


	MmountToPath= cfg_getstr("MOUNT_TO_PATH","/");;
	MmountFromPath= cfg_getstr("MOUNT_FROM_PATH","/");

	eptr->masteraddrvalid = 0;
	if (eptr->mode!=FREE) {
		eptr->mode = KILL;
	}

	Timeout = cfg_getuint32("MASTER_TIMEOUT",60);
	BackLogsNumber = cfg_getuint32("BACK_LOGS",50);
	BackMetaCopies = cfg_getuint32("BACK_META_KEEP_PREVIOUS",3);

	ReconnectionDelay = cfg_getuint32("MASTER_RECONNECTION_DELAY",5);
	MetaDLFreq = cfg_getuint32("META_DOWNLOAD_FREQ",24);

	if (Timeout>65536) {
		Timeout=65535;
	}
	if (Timeout<10) {
		Timeout=10;
	}
	if (BackLogsNumber<5) {
		BackLogsNumber=5;
	}
	if (BackLogsNumber>10000) {
		BackLogsNumber=10000;
	}
	if (MetaDLFreq>(BackLogsNumber/2)) {
		MetaDLFreq=BackLogsNumber/2;
	}
	if (BackMetaCopies>99) {
		BackMetaCopies=99;
	}

	main_timechange(reconnect_hook,TIMEMODE_RUN_LATE,ReconnectionDelay,0);
	//main_timechange(download_hook,TIMEMODE_RUN_LATE,MetaDLFreq*3600,630);
}






/**
Func Desc: 线程调用备份工具
*/
static int  sync_copying = 0;
static operationtable   mfs_sync_table [MAX_COPY_TABLE_NUM] ; 
void *masterconn_sync_copy(void *arg) 
{
	int num = 0;		
	masterconn *eptr = (masterconn *)arg;

	if(sync_copying)
		return NULL;

	sync_copying = 1;

	memset(mfs_sync_table, 0, MAX_COPY_TABLE_NUM* OPERTABLE_NODE_SIZE);
	num = read_operchnglog_table(mfs_sync_table);   	
	
	//调用备份拷贝工具
	
	if(num != 0){
		SSSLOG(LOG_NOTICE,"masterconn_sync: copy run num=%d", num);
		copy_run( SF_GROW, mfs_sync_table , num, 0, 0, MmountFromPath, MmountFromPath, MmountToPath);
	}	

	sync_copying = 0;

	return NULL;
}




/**
Func Desc: 备份服务器的初始化
*/


int masterconn_init(void) {
	uint32_t ReconnectionDelay;
	//uint32_t MetaDLFreq;
	masterconn *eptr;

	ReconnectionDelay = cfg_getuint32("MASTER_RECONNECTION_DELAY",5);
	MasterHost = cfg_getstr("MASTER_HOST","mfsmaster");
	MasterPort = cfg_getstr("MASTER_PORT","9418");
	BindHost = cfg_getstr("BIND_HOST","*");
	Timeout = cfg_getuint32("MASTER_TIMEOUT",60);
	BackLogsNumber = cfg_getuint32("BACK_LOGS",50);
	BackMetaCopies = cfg_getuint32("BACK_META_KEEP_PREVIOUS",3);

	MmountToPath= cfg_getstr("MOUNT_TO_PATH","/mnt/mfsto");;
	MmountFromPath= cfg_getstr("MOUNT_FROM_PATH","/mnt/mfsfrom");
	
	//MetaDLFreq = cfg_getuint32("META_DOWNLOAD_FREQ",24);

	if (Timeout>65536) {
		Timeout=65535;
	}
	if (Timeout<10) {
		Timeout=10;
	}
	if (BackLogsNumber<5) {
		BackLogsNumber=5;
	}
	if (BackLogsNumber>10000) {
		BackLogsNumber=10000;
	}
	/*if (MetaDLFreq>(BackLogsNumber/2)) {
		MetaDLFreq=BackLogsNumber/2;
	}*/
	eptr = masterconnsingleton = malloc(sizeof(masterconn));
	passert(eptr);

	eptr->masteraddrvalid = 0;
	eptr->mode = FREE;
	eptr->pdescpos = -1;
	eptr->logfd = NULL;
//	eptr->mainfd = fopen("operationFromChg","a+");
	eptr->dloffset = 0;
	eptr->oldmode = 0;

	//init the operationlist
	operationlist_hash_init();

	syncserver_open_mmap();


	//masterconn_findlastlogversion();
	if (masterconn_initconnect(eptr)<0) {
		return -1;
	}
	reconnect_hook = main_timeregister(TIMEMODE_RUN_LATE,ReconnectionDelay,0,masterconn_reconnect);
	//download_hook = main_timeregister(TIMEMODE_RUN_LATE,MetaDLFreq*3600,630,masterconn_metadownloadinit);
	main_destructregister(masterconn_term);
	main_pollregister(masterconn_desc,masterconn_serve);
	main_reloadregister(masterconn_reload);
	main_timeregister(TIMEMODE_RUN_LATE,1,0,masterconn_chgdownload);
	main_timeregister(TIMEMODE_RUN_LATE,3,0,masterconn_changelog_del_syncserve);
	main_timeregister(TIMEMODE_RUN_LATE,1,0,masterconn_sync_copy);
	return 0;

}
