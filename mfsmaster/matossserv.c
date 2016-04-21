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

#include "MFSCommunication.h"

#include "datapack.h"
#include "matossserv.h"
#include "crc.h"
#include "cfg.h"
#include "main.h"
#include "sockets.h"
#include "slogger.h"
#include "massert.h"
#include "filesystem.h"


#include <pthread.h>//mutex use

#define MaxPacketSize 1500000
#define OLD_CHANGES_BLOCK_SIZE 5000
#define MAXLOGLINESIZE 2000000U
//I'M ADD
#define Buf 1024
// matossserventry.mode
enum{KILL,HEADER,DATA};

typedef struct packetstruct {
	struct packetstruct *next;
	uint8_t *startptr;
	uint32_t bytesleft;
	uint8_t *packet;
} packetstruct;

typedef struct matossserventry {
	uint8_t mode;
	int sock;
	int32_t pdescpos;
	uint32_t lastread,lastwrite;
	uint8_t hdrbuff[8];
	packetstruct inputpacket;
	packetstruct *outputhead,**outputtail;

	uint16_t timeout;

	char *servstrip;		// human readable version of servip
	uint32_t version;
	uint32_t servip;

	int mainfd;

	struct matossserventry *next;
} matossserventry;




static matossserventry *matossservhead=NULL;
static int lsock;
static int32_t lsockpdescpos;



// from config
static char *ListenHost;
static char *ListenPort;
static uint16_t ChangelogSecondsToRemember;

//I'M ADD	
static char changelogdata[MAXLOGLINESIZE*10]="\0";
static char chglogsend[MAXLOGLINESIZE*10]="\0";
const uint32_t SentFileNum = 100;//sent SENTFILENUM files once a time
static uint32_t logsize=0;
static uint32_t chgcnt=0;
static uint32_t ChgSendNum; //一次性发送changelog值，从配置文件读取,默认10
static FILE *fd;

static uint8_t isEndOfBakFileTrans=0; //Indicates if the backup file transfer is completed.
static uint8_t isWritingLog=0;// Indicates if the log is being writing; if writing(1), the read operation will not be available.

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;//mutex


//static fsnode *root;
extern fsnode* fsnodes_id_to_node(uint32_t id);
extern void fsnodes_getpath(fsedge *e,uint16_t *pleng,uint8_t **path);


char* matossserv_makestrip(uint32_t ip) {
	uint8_t *ptr,pt[4];
	uint32_t l,i;
	char *optr;
	ptr = pt;
	put32bit(&ptr,ip);
	l=0;
	for (i=0 ; i<4 ; i++) {
		if (pt[i]>=100) {
			l+=3;
		} else if (pt[i]>=10) {
			l+=2;
		} else {
			l+=1;
		}
	}
	l+=4;
	optr = malloc(l);
	passert(optr);
	snprintf(optr,l,"%"PRIu8".%"PRIu8".%"PRIu8".%"PRIu8,pt[0],pt[1],pt[2],pt[3]);
	optr[l-1]=0;
	return optr;
}

uint8_t* matossserv_createpacket(matossserventry *eptr,uint32_t type,uint32_t size) {
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


void matossserv_register(matossserventry *eptr,const uint8_t *data,uint32_t length) {
	uint8_t rversion;
	//uint64_t minversion;
	syslog(LOG_NOTICE,"SSTOMA_REGISTER successful");
	syslog(LOG_NOTICE,"tcp accept new socket id =%d,%s",eptr->sock,eptr->servstrip);

	if (eptr->version>0) {
		syslog(LOG_WARNING,"got register message from registered sserver !!!");
		eptr->mode=KILL;
		return;
	}
	if (length<1) {
		syslog(LOG_NOTICE,"SSTOMA_REGISTER - wrong size (%"PRIu32")",length);
		eptr->mode=KILL;
		return;
	} else {
		rversion = get8bit(&data);
		if (rversion==1) {
			if (length!=7) {
				syslog(LOG_NOTICE,"SSTOMA_REGISTER (ver 1) - wrong size (%"PRIu32"/7)",length);
				eptr->mode=KILL;
				return;
			}
			eptr->version = get32bit(&data);
			eptr->timeout = get16bit(&data);
		} 
		else {
			syslog(LOG_NOTICE,"SSTOMA_REGISTER - wrong version (%"PRIu8"/1)",rversion);
			eptr->mode=KILL;
			return;
		}
		if (eptr->timeout<10) {
			syslog(LOG_NOTICE,"SSTOMA_REGISTER communication timeout too small (%"PRIu16" seconds - should be at least 10 seconds)",eptr->timeout);
			if (eptr->timeout<3) {
				eptr->timeout=3;
			}
//			eptr->mode=KILL;
			return;
		}
	}
}



void matossserv_beforeclose(matossserventry *eptr) {
	if (eptr->mainfd>=0) {
		close(eptr->mainfd);
		eptr->mainfd=-1;
	}
}
/*
matossserv_download_data: 将changelog逐条发送。
首先从文件中取数据(依照队列原则)，每次读10条。
然后将数据广播至网络(消息为MATOSS_DOWNLOAD_DATA)
当文件读完后，从缓冲中读取数据，直到发送完成。
当数据发送完成时对方将返回DOWNLOAD_END，交给matossserv_download_end函数处理即可。

*/
void matossserv_download_data(matossserventry *eptr,const uint8_t *data,uint32_t length) {
	uint8_t *ptr;
	char str[Buf];
    uint32_t count;
	memset(str,0,sizeof(str));
	
	if (length!=0) {
		syslog(LOG_NOTICE,"MLTOMA_DOWNLOAD_DATA - wrong size (%"PRIu32"/12)",length);
		eptr->mode=KILL;
		return;
	}
	   //先从文件中读取操作
	   	if(1==isWritingLog)return;//If the log is writting, then return without doing anything.
	   	pthread_mutex_lock(&mutex);//lock file
		syslog(LOG_NOTICE,"- File Changelog.back is LOCKED by reader -");
	   if(fd==NULL)
	   	{
	      fd=fopen("changelog.back","r");
	      if(!fd)
	   	  {
			syslog(LOG_NOTICE,"File_read_fail-");
	      }
	   	}
	   if(fd)
	   	{
	   	    for(count=0;count<SentFileNum;count++) //从文件中每次读SentFileNum行发送
	   	    	{
					fgets(str,Buf,fd);
					if(strlen(str)!=0) //判断是否是最后一行
						{
							strcat(chglogsend,str);
							memset(str,0,sizeof(str));
						}
					else{
							isEndOfBakFileTrans = 1;
							break;
						}
				}
	   		//while(!feof(fd))	    	
			if(strlen(chglogsend)>10)
				{
				     int len = strlen(chglogsend);
					 ptr=matossserv_createpacket(eptr,MATOSS_DOWNLOAD_DATA,len+1);
//	            	 		  put32bit(&ptr,4+strlen(chglogsend));
	              	 memcpy(ptr,chglogsend,len);
					 ptr[len]=0;
					 syslog(LOG_NOTICE,"MATOSS_CHANGELOG_DATA_FILE -(%"PRIu32"):%s",len,chglogsend);
					 memset(chglogsend,0,sizeof(chglogsend));
				}
	   	}
		pthread_mutex_unlock(&mutex);//lock file
		syslog(LOG_NOTICE,"- File Changelog.back is UNLOCKED by reader -");
	   /*
	   //从内存中读取
	   //改动检查逻辑:先置文件结束位isEndOfBakFileTrans为1，
	    //然后检查内存，再在downloadend中将其置0.
	    */
	   if(1 == isEndOfBakFileTrans && strlen(changelogdata)!=0) //合并的内容不为空
	{
		int len = strlen(changelogdata);
		ptr=matossserv_createpacket(eptr,MATOSS_DOWNLOAD_DATA,len+1);
		//put32bit(&ptr,4+logsize);
		memcpy(ptr,changelogdata,len);
		ptr[len]=0;
		syslog(LOG_NOTICE,"MATOSS_CHANGELOG_DATA_BUFF -	(%"PRIu32"):%s",len,changelogdata);
		//发送后立刻清空数组
		
		
		memset(changelogdata,0,sizeof(changelogdata));
		
	}
	
   else 
	{
		ptr=matossserv_createpacket(eptr,MATOSS_DOWNLOAD_END,0);
		syslog(LOG_NOTICE,"MATOSS_NO_SEND_BUFF - ");
	}

	    
	
    
}

/**void fsnode_getpath(fsedge*e,uint16_t *pleng,uint8_t **path)
{
	uint32_t size;
	uint8_t *ret;
	fsnode *p;

	p = e->parent;
	size = e->nleng;
	while (p!=root && p->parents) {
		size += p->parents->nleng+1;	// get first parent !!!
		p = p->parents->parent;		// when folders can be hardlinked it's the only way to obtain path (one of them)
	}
	if (size>65535) {
		syslog(LOG_WARNING,"path too long !!! - truncate");
		size=65535;
	}
	*pleng = size;
	ret = malloc(size);
	passert(ret);
	size -= e->nleng;
	memcpy(ret+size,p->parents->name,p->parents->nleng);
	if (size>0) {
		ret[--size]='/';
	}
	p = e->parent;
	while (p!=root && p->parents) {
		if (size>=p->parents->nleng) {
			size-=p->parents->nleng;
			memcpy(ret+size,p->parents->name,p->parents->nleng);
		} else {
			if (size>0) {
				memcpy(ret,p->parents->name+(p->parents->nleng-size),size);
				size=0;
			}
		}
		if (size>0) {
			ret[--size]='/';
		}
		p = p->parents->parent;
	}
	*path = ret;
	*/
//}


/*
matossserv_inode_path: 根据inode值返回节点路径(含文件名与后缀)
注意删除数据时的inode与其余的inode不同，删除数据时，inode的parent包含了完整路径，不需要再求路径。
其他情况下则需要调用fsnode_getpath函数获取路径。

*/
void matossserv_inode_path(matossserventry *eptr,const uint8_t *data,uint32_t length)
{
	   fsnode *m;
	   char pathname[65535]="";
	   char path[65536]="";
	   uint16_t pleng=0;
	   uint8_t *ptr;
	  
	  uint32_t ID;
			 if(NULL==data)
				  {
					  syslog(LOG_NOTICE,"ERROR-inode- ");
					  return;
				  }
			 ID=get32bit(&data);
   	   m=fsnodes_id_to_node(ID);
	   if(!m)
		{
			syslog(LOG_NOTICE,"ERROR-inode- ");
			
		   ptr=matossserv_createpacket(eptr,MATOSS_ID_DATA,5+strlen(""));
		   //put32bit(&ptr,9+strlen(pathname));
		   put32bit(&ptr,m->id);
		   put8bit(&ptr,m->type);
		   return;
		}
	   else
	   {
		fsnode * m1 = m;
		
		if(NULL==m1->parents->parent){
			strcat(path,(char *)m1->parents->name);
			pleng=m1->parents->nleng;
			}
		else
			{
			char * pathtmp = NULL;
			fsnodes_getpath(m1->parents,&pleng,&pathtmp);
			strncpy(path,pathtmp,pleng);
			free(pathtmp);
		}
		memset(pathname,0,sizeof(pathname));
		pathname[0]='/';
	   //从path中提取路径
	   strncpy(pathname+1,path,pleng);
	   
	   ptr=matossserv_createpacket(eptr,MATOSS_ID_DATA,5+strlen(pathname));
	   //put32bit(&ptr,9+strlen(pathname));
	   put32bit(&ptr,m->id);
	   put8bit(&ptr,m->type);
	   memcpy(ptr,pathname,strlen(pathname));
	   
	//  while (f!=root && f->parents)
	//  for (e=f->parents ; e ; e=e->nextparent)
	//   	{
	//    fsnodes_getpath(m->parents,&pleng,&path);
	//    pathname=fsnodes_escape_name(f->parents->nleng,f->parents->name);
	//    e=f->parents;
	//    p=e->parent;
	    syslog(LOG_NOTICE,"* inode_path :%s",pathname);
	//    f = f->parents->parent; 
	   }
	 //  free(f);

}


/*
matossserv_download_end，发送结束时调用该函数。
当数据发送结束时，有两种情况，第一种情况是文件发送了10条后显示该10条已被收到。
第二种情况是数据全部发完，应当清空文件和缓冲区。
用isEndOfBakFileTrans变量指示发送是否全部完成。

*/
void matossserv_download_end(matossserventry *eptr,const uint8_t *data,uint32_t length) {

if (length!=0) {
		syslog(LOG_NOTICE,"MLTOMA_DOWNLOAD_DATA - wrong size (%"PRIu32"/12)",length);
		eptr->mode=KILL;
		return;
	}
if (isWritingLog == 1) {
		syslog(LOG_NOTICE,"MLTOMA_DOWNLOAD_DATA - file writing");
		return;
	}

			//当文件发送完，清空文件
			if(1 == isEndOfBakFileTrans)			
			   {	
			   pthread_mutex_lock(&mutex);//lock file
			   syslog(LOG_NOTICE,"- File Changelog.back is LOCKED by delete -");
					system(":>changelog.back");		
				    isEndOfBakFileTrans=0;		
					 //清空大小
					logsize=0;
					//清空数组
					memset(changelogdata,0,sizeof(changelogdata));
					
					//清空计数值
					//chgcnt=0;
					if(fd!=NULL)
						{
						 fclose(fd);
						 fd=NULL;
					    }
					syslog(LOG_NOTICE,"MATOSS_CLEAN_DATA - ");
					pthread_mutex_unlock(&mutex);//lock file
					syslog(LOG_NOTICE,"- File Changelog.back is UNLOCKED by reader -");
			   }
			else
				{
				        //清空文件发送缓存
						//memset(chglogsend,0,sizeof(chglogsend));	
						

				}

}

int changelog_checkchangelogValid(char* logstr)
{
	//return value 1 means log needs to be stored.
	//and value 0 means log does NOT need to be stored.

	int sepPos=0;
	int len = strlen(logstr);
	for(sepPos=0;sepPos<len;sepPos++)
	{
		if('|' == logstr[sepPos])
		{
			break;
		}
	}
	if(sepPos>=len-1)
		{
		//not a valid log
			return 0;
		}
	//|CREATE
	//|WRITE
	//|RELEASE
	//|UNLINK
	char oper_name[20];
	memset(oper_name,0,sizeof(oper_name));
	
	for(int i=0;i<len-sepPos;i++)
	{
		if('|' == logstr[sepPos+i+1] || 0 == logstr[sepPos+i+1] || '(' == logstr[sepPos+i+1] || ')' == logstr[sepPos+i+1])
		{
			break;
		}
		oper_name[i]=logstr[sepPos+i+1];//copy string data
	}
	//syslog(LOG_NOTICE,"MFS changelog check - %s",oper_name);
	if((strcmp(oper_name,"CREATE") !=0) && (strcmp(oper_name,"RELEASE") !=0) && (strcmp(oper_name,"UNLINK") !=0) && (strcmp(oper_name,"WRITE") !=0))
	{
		return 0;
		//return 1 for testing
		//return 1;
	}
	else
	{
		//string is CREATE or RELEASE or UNLINK
		return 1;
	}
	
	return 1;	
}

/*
matossserv_getchangelog: 将changelog存入文件和缓冲区。
当缓冲区到一定大小时，将缓冲区内容追加至文件(changelog.back)尾部。

*/
void matossserv_getchangelog(uint64_t version,uint8_t *logstr,uint32_t logstrsize) {

	syslog(LOG_NOTICE,"_mfs_changelog_master - %s",logstr);

	////////////optimize - check if changelog is valid
	if(0==changelog_checkchangelogValid(logstr))
	{
		syslog(LOG_NOTICE,"changelog is not valuable - not logged");
		return;
	}
	////////////end optimize
	
	logsize=logsize+logstrsize;
	strcat(changelogdata,(char *)logstr);
	strcat(changelogdata,"\n");
    chgcnt=chgcnt+1;
	FILE* fd1=NULL;
	if(chgcnt>ChgSendNum){
		isWritingLog=1;
		pthread_mutex_lock(&mutex);//lock file
		syslog(LOG_NOTICE,"- File Changelog.back is locked -");
		fd1= fopen("changelog.back","a+");
	    if (!fd1){
	       syslog(LOG_NOTICE,"lost MFS change %"PRIu64": %s",version,(char *)logstr);
	        }     
	    if (fd1) 
		  {
		    
			    fprintf(fd1,"%s",changelogdata);
				isEndOfBakFileTrans=0;//Set end file =1, indicates file is not end.
				//syslog(LOG_NOTICE,"WRITE_TO_FILE_errno - %s",strerror(errno));
				
			    //fflush(fd1);
				syslog(LOG_NOTICE,"WRITE_TO_FILE - ");
				memset(changelogdata,0,sizeof(changelogdata));
				logsize=0;
				chgcnt=0;
			    fclose(fd1);
				fd1=NULL;
		      }

			pthread_mutex_unlock(&mutex);//lock file
			syslog(LOG_NOTICE,"- File Changelog.back is unlocked -");

			isWritingLog=0;
		}
		
	//strncpy(chglogsend,changelogdata,logsize);
	//chgcnt=0;
	//syslog(LOG_NOTICE,"MATOSS_SEND_DATA -  (%"PRIu32"):%s",logsize,chglogsend);
	//清空changelogdata
    //memset(changelogdata,0,sizeof(changelogdata));
		
	 	
}

/*
matossserv_gotpacket，收到消息时在此处转发并交由各消息处理程序处理。
SSTOMA_ID_DATA: 请求inode路径。
SSTOMA_DOWNLOAD_DATA:转发数据
SSTOMA_DOWNLOAD_END: 发送结束




*/
void matossserv_gotpacket(matossserventry *eptr,uint32_t type,const uint8_t *data,uint32_t length) {
	switch (type) {
		case ANTOAN_NOP:
			break;
		case ANTOAN_UNKNOWN_COMMAND: // for future use
			break;
		case ANTOAN_BAD_COMMAND_SIZE: // for future use
			break;
		case SSTOMA_REGISTER:
			matossserv_register(eptr,data,length);
			break;
			
		case SSTOMA_ID_DATA:
			matossserv_inode_path(eptr,data,length); //根据inode返回path
			break;
		//收到ss的chg请求，发送消息
			
		/*case SSTOMA_DOWNLOAD_START:
			matossserv_download_start(eptr,data,length);
			break;*/
		case SSTOMA_DOWNLOAD_DATA:
			matossserv_download_data(eptr,data,length);
			break;
		case SSTOMA_DOWNLOAD_END:
			//清空发送数组
			matossserv_download_end(eptr,data,length);
			break;
		default:
			syslog(LOG_NOTICE,"master <-> sservers module: got unknown message (type:%"PRIu32")",type);
			eptr->mode=KILL;
	}
}

void matossserv_term(void) {
	matossserventry *eptr,*eaptr;
	packetstruct *pptr,*paptr;
	syslog(LOG_INFO,"master <-> metaloggers module: closing %s:%s",ListenHost,ListenPort);
	tcpclose(lsock);

	eptr = matossservhead;
	while (eptr) {
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
		eaptr = eptr;
		eptr = eptr->next;
		free(eaptr);
	}
	matossservhead=NULL;

	free(ListenHost);
	free(ListenPort);
}

void matossserv_read(matossserventry *eptr) {
	int32_t i;
	uint32_t type,size;
	const uint8_t *ptr;
	for (;;) {
		i=read(eptr->sock,eptr->inputpacket.startptr,eptr->inputpacket.bytesleft);
		if (i==0) {
			syslog(LOG_NOTICE,"connection with SS(%s) has been closed by peer",eptr->servstrip);
			eptr->mode = KILL;
			return;
		}
		if (i<0) {
			if (errno!=EAGAIN) {
				mfs_arg_errlog_silent(LOG_NOTICE,"read from SS(%s) error",eptr->servstrip);
				eptr->mode = KILL;
			}
			return;
		}
		eptr->inputpacket.startptr+=i;
		eptr->inputpacket.bytesleft-=i;

		if (eptr->inputpacket.bytesleft>0) {
			return;
		}

		if (eptr->mode==HEADER) {
			ptr = eptr->hdrbuff+4;
			size = get32bit(&ptr);

			if (size>0) {
				if (size>MaxPacketSize) {
					syslog(LOG_WARNING,"SS(%s) packet too long (%"PRIu32"/%u)",eptr->servstrip,size,MaxPacketSize);
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

			matossserv_gotpacket(eptr,type,eptr->inputpacket.packet,size);

			if (eptr->inputpacket.packet) {
				free(eptr->inputpacket.packet);
			}
			eptr->inputpacket.packet=NULL;
		}
	}
}

void matossserv_write(matossserventry *eptr) {
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
				mfs_arg_errlog_silent(LOG_NOTICE,"write to SS(%s) error",eptr->servstrip);
				eptr->mode = KILL;
			}
			return;
		}
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

void matossserv_desc(struct pollfd *pdesc,uint32_t *ndesc) {
	uint32_t pos = *ndesc;
	matossserventry *eptr;
	pdesc[pos].fd = lsock;
	pdesc[pos].events = POLLIN;
	lsockpdescpos = pos;
	pos++;
	for (eptr=matossservhead ; eptr ; eptr=eptr->next) {
		pdesc[pos].fd = eptr->sock;
		pdesc[pos].events = POLLIN;
		eptr->pdescpos = pos;
		if (eptr->outputhead!=NULL) {
			pdesc[pos].events |= POLLOUT;
		}
		pos++;
	}
	*ndesc = pos;
}

void matossserv_serve(struct pollfd *pdesc) {
	uint32_t now=main_time();
	matossserventry *eptr,**kptr;
	packetstruct *pptr,*paptr;
	int ns;
	static uint64_t lastaction = 0;
	uint64_t unow;
	uint32_t timeoutadd;

	if (lastaction==0) {
		lastaction = main_precise_utime();
	}

	if (lsockpdescpos>=0 && (pdesc[lsockpdescpos].revents & POLLIN)) {
		ns=tcpaccept(lsock);
		if (ns<0) {
			mfs_errlog_silent(LOG_NOTICE,"Master<->SS socket: accept error");
		} else {
			tcpnonblock(ns);
			tcpnodelay(ns);
			eptr = malloc(sizeof(matossserventry));
			passert(eptr);
			eptr->next = matossservhead;
			matossservhead = eptr;
			eptr->sock = ns;
			eptr->pdescpos = -1;
			eptr->mode = HEADER;
			eptr->lastread = now;
			eptr->lastwrite = now;
			eptr->inputpacket.next = NULL;
			eptr->inputpacket.bytesleft = 8;
			eptr->inputpacket.startptr = eptr->hdrbuff;
			eptr->inputpacket.packet = NULL;
			eptr->outputhead = NULL;
			eptr->outputtail = &(eptr->outputhead);
			eptr->timeout = 10;

			tcpgetpeer(eptr->sock,&(eptr->servip),NULL);
			eptr->servstrip = matossserv_makestrip(eptr->servip);
			eptr->version=0;
			eptr->mainfd=-1;
		}
	}

// read
	for (eptr=matossservhead ; eptr ; eptr=eptr->next) {
		if (eptr->pdescpos>=0) {
			if (pdesc[eptr->pdescpos].revents & (POLLERR|POLLHUP)) {
				eptr->mode = KILL;
			}
			if ((pdesc[eptr->pdescpos].revents & POLLIN) && eptr->mode!=KILL) {
				eptr->lastread = now;
				matossserv_read(eptr);
			}
		}
	}

// timeout fix
	unow = main_precise_utime();
	timeoutadd = (unow-lastaction)/1000000;
	if (timeoutadd) {
		for (eptr=matossservhead ; eptr ; eptr=eptr->next) {
			eptr->lastread += timeoutadd;
		}
	}
	lastaction = unow;

// write
	for (eptr=matossservhead ; eptr ; eptr=eptr->next) {
		if ((uint32_t)(eptr->lastwrite+(eptr->timeout/3))<(uint32_t)now && eptr->outputhead==NULL) {
			matossserv_createpacket(eptr,ANTOAN_NOP,0);
		}
		if (eptr->pdescpos>=0) {
			if ((((pdesc[eptr->pdescpos].events & POLLOUT)==0 && (eptr->outputhead)) || (pdesc[eptr->pdescpos].revents & POLLOUT)) && eptr->mode!=KILL) {
				eptr->lastwrite = now;
				matossserv_write(eptr);
			}
		}
		if ((uint32_t)(eptr->lastread+eptr->timeout)<(uint32_t)now) {
			eptr->mode = KILL;
		}
	}

// close
	kptr = &matossservhead;
	while ((eptr=*kptr)) {
		if (eptr->mode == KILL) {
			matossserv_beforeclose(eptr);
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
			if (eptr->servstrip) {
				free(eptr->servstrip);
			}
			*kptr = eptr->next;
			free(eptr);
		} else {
			kptr = &(eptr->next);
		}
	}
}

void matossserv_reload(void) {
	char *oldListenHost,*oldListenPort;
	int newlsock;

	oldListenHost = ListenHost;
	oldListenPort = ListenPort;
	ListenHost = cfg_getstr("MATOSS_LISTEN_HOST","*");
	ListenPort = cfg_getstr("MATOSS_LISTEN_PORT","9418");
	if (strcmp(oldListenHost,ListenHost)==0 && strcmp(oldListenPort,ListenPort)==0) {
		free(oldListenHost);
		free(oldListenPort);
		mfs_arg_syslog(LOG_NOTICE,"master <-> metaloggers module: socket address hasn't changed (%s:%s)",ListenHost,ListenPort);
		return;
	}

	newlsock = tcpsocket();
	if (newlsock<0) {
		mfs_errlog(LOG_WARNING,"master <-> metaloggers module: socket address has changed, but can't create new socket");
		free(ListenHost);
		free(ListenPort);
		ListenHost = oldListenHost;
		ListenPort = oldListenPort;
		return;
	}
	tcpnonblock(newlsock);
	tcpnodelay(newlsock);
	tcpreuseaddr(newlsock);
	if (tcpsetacceptfilter(newlsock)<0 && errno!=ENOTSUP) {
		mfs_errlog_silent(LOG_NOTICE,"master <-> metaloggers module: can't set accept filter");
	}
	if (tcpstrlisten(newlsock,ListenHost,ListenPort,100)<0) {
		mfs_arg_errlog(LOG_ERR,"master <-> metaloggers module: socket address has changed, but can't listen on socket (%s:%s)",ListenHost,ListenPort);
		free(ListenHost);
		free(ListenPort);
		ListenHost = oldListenHost;
		ListenPort = oldListenPort;
		tcpclose(newlsock);
		return;
	}
	mfs_arg_syslog(LOG_NOTICE,"master <-> metaloggers module: socket address has changed, now listen on %s:%s",ListenHost,ListenPort);
	free(oldListenHost);
	free(oldListenPort);
	tcpclose(lsock);
	lsock = newlsock;

	ChangelogSecondsToRemember = cfg_getuint16("MATOSS_LOG_PRESERVE_SECONDS",600);
	if (ChangelogSecondsToRemember>3600) {
		syslog(LOG_WARNING,"Number of seconds of change logs to be preserved in master is too big (%"PRIu16") - decreasing to 3600 seconds",ChangelogSecondsToRemember);
		ChangelogSecondsToRemember=3600;
	}
}

int matossserv_init(void) {
	ListenHost = cfg_getstr("MATOSS_LISTEN_HOST","*");
	ListenPort = cfg_getstr("MATOSS_LISTEN_PORT","9418");

	char* tmp = cfg_getstr("MATOSS_CHANGELOG_NUM","50");
    ChgSendNum = atoi(tmp); //一次性发送50个changelog
    free(tmp);
	
    pthread_mutex_init(&mutex,NULL);//init mutex
	FILE* fd1=NULL;
	fd1= fopen("changelog.back","a+");
	fclose(fd1);

	
	lsock = tcpsocket();
	if (lsock<0) {
		mfs_errlog(LOG_ERR,"master <-> ssserver module: can't create socket");
		return -1;
	}
	tcpnonblock(lsock);
	tcpnodelay(lsock);
	tcpreuseaddr(lsock);
	if (tcpsetacceptfilter(lsock)<0 && errno!=ENOTSUP) {
		mfs_errlog_silent(LOG_NOTICE,"master <-> ssserver module: can't set accept filter");
	}
	if (tcpstrlisten(lsock,ListenHost,ListenPort,100)<0) {
		mfs_arg_errlog(LOG_ERR,"master <-> ssserver module: can't listen on %s:%s",ListenHost,ListenPort);
		return -1;
	}
	mfs_arg_syslog(LOG_NOTICE,"master <-> ssserver module: listen on %s:%s",ListenHost,ListenPort);

	matossservhead = NULL;
	ChangelogSecondsToRemember = cfg_getuint16("MATOSS_LOG_PRESERVE_SECONDS",600);
	if (ChangelogSecondsToRemember>3600) {
		syslog(LOG_WARNING,"Number of seconds of change logs to be preserved in master is too big (%"PRIu16") - decreasing to 3600 seconds",ChangelogSecondsToRemember);
		ChangelogSecondsToRemember=3600;
	}
	main_reloadregister(matossserv_reload);
	main_destructregister(matossserv_term);
	main_pollregister(matossserv_desc,matossserv_serve);
	//main_timeregister(TIMEMODE_SKIP_LATE,3600,0,matossserv_status);
	return 0;
}
