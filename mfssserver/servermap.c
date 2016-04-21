/*
	Author :  Weining
	Date Created:  2014-11-26

  Description:
       容灾项目的文件映射模块

 */


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <pthread.h> 

#include "common.h"
#include "masterconn.h"
#include "massert.h"
#include "servermap.h"

#define MAPLOG   syslog

uint32_t used_num = 0;

int file_fd_map = 0;

//映射地址
static	void* addr_map = NULL;


#define COPY_BIT   (0x80)
#define  DEL_FLAG    (0x40)


pthread_mutex_t map_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
Func Desc: 获取映射文件最大的大小
*/

int  get_max_file_size()
{
	int MaxFileSize = 0;

	MaxFileSize = OPERTABLE_NODE_SIZE;
	MaxFileSize *= MAX_FILE_MAP_TABLE_NUM;
	return  MaxFileSize;
}

/**
Func Desc: 关闭文件映射
*/

int  syncserver_close_mmap()
{
	int MaxFileSize = get_max_file_size();
	
	int ret = munmap(addr_map, MaxFileSize);
	if(-1 == ret)
	{
		MAPLOG(LOG_NOTICE,"munmap src faile!");
		return -1;
	}

	pthread_mutex_destroy(&map_mutex);

	addr_map= NULL;
	close(file_fd_map);
	return 0;
}

/**
Func Desc: 打开文件映射
*/

int  syncserver_open_mmap()
{
	int ret = 0;

	file_fd_map = open(MAP_FILE_NAME, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if(-1 == file_fd_map)
	{
		MAPLOG(LOG_NOTICE,"open file failed :%s!", strerror(errno));
		return -1;
	}

	int MaxFileSize = get_max_file_size();
	ret = ftruncate(file_fd_map, MaxFileSize);
	if(ret == -1){
		MAPLOG(LOG_NOTICE,"ftruncate failed :%s!", strerror(errno));
		return -1;
	}


	
	addr_map = mmap(NULL, MaxFileSize, PROT_READ|PROT_WRITE, MAP_SHARED, file_fd_map, SEEK_SET);
	if(NULL == addr_map)
	{
		MAPLOG(LOG_NOTICE,"mmap failed : %s", strerror(errno));
		return -1;
	}

	MAPLOG(LOG_NOTICE,"mmap ok : MaxSize = %d", MaxFileSize);
	return 0;
}


/**
Func Desc: 复制一个节点
*/

int mapfile_copy_oper_node(operationtable * dst,  operationtable * src)
{
	dst->inode = src->inode;
	dst->parentinode = src->parentinode;
	dst->type =  src->type;
	dst->lasttime =  src->lasttime;
	memcpy(dst->operationname, src->operationname, MAX_OPER_LENGTH);
	memcpy(dst->filepath, src->filepath, MAX_PATH_SIZE);
	return 0;
}

/**
Func Desc:     读取映射中的节点数据到参数table
Return Value:  读取的节点数量
*/
int read_operchnglog_table(operationtable *table)
{
	int index ;
	int n = 0;
	void * start = NULL;
	int read_max_num = MAX_COPY_TABLE_NUM -1;

	pthread_mutex_lock(&map_mutex);

//	MAPLOG(LOG_NOTICE,"read map file : used_num=%d !",used_num);

	if(read_max_num > used_num)
		read_max_num = used_num;

	//MAPLOG(LOG_NOTICE,"read map file : max_read_num=%d !",read_max_num);
	//从映射表的前面开始依次读取节点
	for(index=0; index<read_max_num; index++)
	{
		start = addr_map + index * OPERTABLE_NODE_SIZE;	
		operationtable * temp = (operationtable *)start;

		/*	
			if failure times >5 , it's treated as can't be copy , 
			and it will be 	never copied forever 	
		*/
		if(COPY_BIT & temp->flag )
			continue;

		temp->flag &= ~COPY_BIT;
		
		if(temp->flag < MAX_COPY_TIMES){
			//mapfile_copy_oper_node(table+index, temp);
			memcpy(table+n, temp, OPERTABLE_NODE_SIZE);
			temp->flag |= COPY_BIT;
			n++;
		}	
		
	}

	pthread_mutex_unlock(&map_mutex);

	return n;
}


/**
Func Desc: 添加一个节点到映射表中
*/

int add_operchnglog_to_mapfile(operationtable *addnode)
{
	int i;
	int ret = 0;
	void * start = NULL;
	operationtable * temp = NULL;

	pthread_mutex_lock(&map_mutex);
	
	if(used_num >= MAX_FILE_MAP_TABLE_NUM)
	{
		MAPLOG(LOG_NOTICE,"add map file : over max !");
		ret=  -1 ;
	}else{
		start = addr_map + used_num * OPERTABLE_NODE_SIZE;	
		MAPLOG(LOG_NOTICE,"add to  map : inode=%d, index=%d, used_num=%d",addnode->inode,used_num,used_num+1);
		operationtable * temp = (operationtable *)start;
		//passert(temp);

		memcpy( temp, addnode, OPERTABLE_NODE_SIZE);
		used_num++;
		msync(temp, OPERTABLE_NODE_SIZE, MS_ASYNC);
		ret=  0 ;
	}

	pthread_mutex_unlock(&map_mutex);
	return ret;
}

void dump_map()
{
	int index ;
	int num = 0;
	char tmp[6];
	char * inode_buf = NULL;
	
	operationtable * lookuped = NULL; 

	if(0 == used_num)
		return ;

	MAPLOG(LOG_NOTICE, "dump map  used_num=%d", used_num);

	inode_buf = (char * )malloc(used_num *20);
	memset(inode_buf, 0, used_num *20);
	
	for(index=0; index<used_num; index++)
	{
		lookuped = (operationtable *)(addr_map + index* OPERTABLE_NODE_SIZE);
		sprintf(tmp, "%d,", lookuped->inode);
		strcat(inode_buf, tmp);
	}

	MAPLOG(LOG_NOTICE, "map inode list: %s", inode_buf);
	free(inode_buf);

}

void masterconn_changelog_del_syncserve()
{
	int index ;
	int flush = 0;

	int del_num  = 0;
	//删除前块的数量
	int before_del_block_num = 0;
	int after_del_block_num = 0;
	
	void * map_addr = NULL;
	operationtable * lookuped = NULL; 

	//删除开始地址
	void * del_start_addr = NULL; 
	
	pthread_mutex_lock(&map_mutex);
	
	/*查找删除开始地址*/
	for(index=0; index<used_num; index++)
	{
		lookuped = (operationtable *)(addr_map + index* OPERTABLE_NODE_SIZE);
		if(lookuped->flag == DEL_FLAG)
		{
			del_start_addr = lookuped;
			before_del_block_num = index;			
			break;
		}		
	}
	
	
	if(NULL != del_start_addr)
	{
		/*统计删除连续的数量*/
		
		index ++;
		del_num = 1;	
				
		while(index <used_num)
		{
			lookuped = (operationtable *)(addr_map + index* OPERTABLE_NODE_SIZE);
			if(lookuped->flag == DEL_FLAG)
			{
				del_num++;
			}else{
				break;
			}
				
			index++;
		}


		
		//删除
		if(used_num == del_num)
		{
			MAPLOG(LOG_NOTICE,"map timer : del all del_num=%d", del_num);
			used_num = 0;
		}
		else{
			dump_map();
			
			MAPLOG(LOG_NOTICE,"map timer :  from start =%d , del num=%d, used_num=%d",
				before_del_block_num, del_num, used_num);

			//从删除的块后面第一个向前复制
			after_del_block_num = used_num - del_num - before_del_block_num;	
			if(0 !=  after_del_block_num)
			{
				int copylen =after_del_block_num * OPERTABLE_NODE_SIZE;				

				MAPLOG(LOG_NOTICE,"map timer : from start =%d , mov num=%d",
					(before_del_block_num+del_num) ,after_del_block_num);
			
				void *  copy_src_addr = del_start_addr +  del_num* OPERTABLE_NODE_SIZE;

				MAPLOG(LOG_NOTICE,"map timer :copy_src_addr ->inode=%d",
					((operationtable *) copy_src_addr)->inode);
								
				memmove(del_start_addr,  copy_src_addr, copylen);

				MAPLOG(LOG_NOTICE,"map timer :del_start_addr->inode=%d",
					((operationtable *) del_start_addr)->inode);
				
			}
			
			used_num -= del_num;

			dump_map();
			
		}
	}

	pthread_mutex_unlock(&map_mutex);
	return ;

}	


 int sign_operchnglog_from_mapfile(int inode, int result)
{
	int index = 0, ret ;
	int find = 0;
	void * start = NULL;
	int flush = 0;
	operationtable * lookuped = NULL; 

	pthread_mutex_lock(&map_mutex);
	
	for(index=0; index<used_num; index++)
	{
		start = addr_map + index* OPERTABLE_NODE_SIZE;
		lookuped = (operationtable *)start;
		if(lookuped->inode == inode){
			find = 1;			
			break;
		}			
	}

	if(0 == find){
		MAPLOG(LOG_NOTICE,"sign map -: not find inode =%d !", inode);
		//如果标记找不到inode是错误的，要用断言
		dump_map();
		passert(0);
		ret = -1;
	}else{
		if(SIGN_SUCCESS == result){
			lookuped->flag = DEL_FLAG;			
		}else{
			//标记失败或错误
			MAPLOG(LOG_NOTICE,"sign map -: error or fail = %d", result);
			lookuped->flag++;
		}

		flush = 1;
	}

	pthread_mutex_unlock(&map_mutex);

	if(flush)
		msync(start, 16, MS_ASYNC);
	
	return 0;

}


 #if 0
/**
Func Desc: 标记接口，如果备份文件成功则删除节点
*/

 int sign_operchnglog_from_mapfile2(int inode, int result)
 {
	int index = 0, ret ;
	int find = 0;
	void * start = NULL;
	int flush = 0;
	operationtable * lookuped = NULL; 

	pthread_mutex_lock(&map_mutex);
	
	for(index=0; index<used_num; index++)
	{
		start = addr_map + index* OPERTABLE_NODE_SIZE;
		lookuped = (operationtable *)start;
		if(lookuped->inode == inode){
			find = 1;			
			break;
		}			
	}

	if(0 == find){
		MAPLOG(LOG_NOTICE,"sign map : not find inode =%d !", inode);
		//如果标记找不到inode是错误的，要用断言
		//passert(0);
		ret = -1;
	}else{
		if(SIGN_SUCCESS == result){
			int copylen = (used_num - index -1)* OPERTABLE_NODE_SIZE;
			
			memcpy(start,  (start+OPERTABLE_NODE_SIZE), copylen);
			MAPLOG(LOG_NOTICE,"sign map : del success inode =%d , used_num=%d,index=%d,copylen=%d!",
				inode, used_num,index, copylen);
			used_num--;
			
		}else{
			//标记失败或错误
			MAPLOG(LOG_NOTICE,"sign map : error or fail = %d", result);
			lookuped->flag++;
		}

		flush = 1;
	}

	pthread_mutex_unlock(&map_mutex);

	if(flush)
		msync(start, 4, MS_ASYNC);
	
	return 0;

	
 }

 #endif

