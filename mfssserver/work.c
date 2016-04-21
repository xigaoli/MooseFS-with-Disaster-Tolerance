/************************************************************
  Copyright (C), 2014, Nari.
  FileName:      work.c
  Author:        mawenxiang
  Version :      v1.0      
  Date:          2014-10 
  Description:  本源文件定义的函数，均为实现文件同
      步业务的功能，实现为线程池工作线程提供接口服务。    
***********************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/time.h>
#include <getopt.h>
#include <assert.h>  
#include <syslog.h>
#include <fcntl.h>

#include "common.h"
#include "work.h"


//统计某一个字符在字符串中的个数
size_t get_char_num(const char *path, char ch)
{
    int leng = 0;
    size_t count = 0;
    char *p = NULL;

    leng = strlen(path);
    char tmp[leng+1];
    memset(tmp, '\0', sizeof(tmp)); 
    strcat(tmp, path);
    p = tmp;

    while (*p)
    {
        if (*p == '/')
        {
            count++;
        }            
        p++;
    }
    p = NULL;

    return count;
}


#if CONFIG_IS_HIDE
//第一次使用即可，初始化拷贝源、目路径，之后若想改变拷贝路径，则调用该接口
Config * copy_to_where_init(const char *src, const char *dst)
{
    int length_src;
    int length_dst;

    length_src = strlen(src);
    length_dst = strlen(dst);

    char tmp_src[length_src];
    char tmp_dst[length_dst];

    memset(tmp_src, '\0', sizeof(tmp_src));
    memset(tmp_dst, '\0', sizeof(tmp_dst));

    strcpy(tmp_src, src);
    strcpy(tmp_dst, dst);   

    memset(conf.from_path, '\0', sizeof(conf.from_path));
    memset(conf.to_path,   '\0', sizeof(conf.to_path)  );  

    if ( *(tmp_src + sizeof(tmp_src) -1) == '/' )
    {       
        *(tmp_src + sizeof(tmp_src) - 1) = '\0'; 
    }       
    strcpy(conf.from_path, tmp_src);  

    if ( *(tmp_dst + sizeof(tmp_dst) -1) == '/' )
    {       
        *(tmp_dst + sizeof(tmp_dst) - 1) = '\0'; 
    }       
    strcpy(conf.to_path, tmp_dst);  
    
    return &conf;
}
#endif

#if 0
//加载配置脚本,失败返回-1，成功返回0，配置脚本路径为path
int load_config(char *path)
{
    FILE * file_p = NULL;   
    char str_tmp_one[MAX_STR_PATH];
    char str_tmp_two[MAX_STR_PATH];
    
    memset(&conf, '\0', sizeof(conf));

    if ( NULL == path || strlen(path) == 1 || strlen(path) == 0 )
    {
        return -1;
    }

    file_p = fopen(path, "r");
    if (NULL == file_p) 
    {
        //syslog
        return -1;  
    }    
    
    while ( fscanf(file_p, "%s %s", str_tmp_one, str_tmp_two) ==  2 )
    {
        if ( strcmp(str_tmp_one, "frompath") == 0 )  
        {
            if ( *(str_tmp_two+sizeof(str_tmp_two)-1) == '/' )
            {
                *(str_tmp_two + sizeof(str_tmp_two) - 1) = '\0';
            }
            strcpy(conf.from_path, str_tmp_two);
        }
        else if ( strcmp(str_tmp_one, "topath") == 0 )  
        {
            if ( *(str_tmp_two + sizeof(str_tmp_two) - 1) == '/' ) 
            {
                *(str_tmp_two + sizeof(str_tmp_two) - 1) = '\0';
            }
            strcpy(conf.to_path, str_tmp_two);
        }
    }
     
    fclose(file_p);

    return 0;
}
#endif

//获取文件长度
unsigned long long get_file_size(const char *path)  
{  
    unsigned long long filesize = -1;      
    struct stat statbuff;  

    if( stat(path, &statbuff) < 0 )
    {   
        return filesize;  
    }
    else
    {  
        filesize = statbuff.st_size;  
    }  

    return filesize; 
}  


/*
 判断路径最后一个字符是否为'/'，没有加上
 成功返回 path
 失败返回 NULL
*/
char * judge_path_add(char *path)
{
    if (NULL == path || 0 == strlen(path) || strlen(path) > 4096)
    {
        return NULL;
    }

    if ( *(path + strlen(path) - 1) != '/' )
    {
        *(path + strlen(path) - 1) = '/';
        return path;
    }
    else if ( *(path + strlen(path) - 1) == '/') 
    {
        return path;
    }
    
    return NULL;
}


/*
 判断路径最后一个字符是否为'/'，若有去掉
 成功返回 path
 失败返回 NULL
*/
char * judge_path_del(char *path)
{
    if (NULL == path || 0 == strlen(path) || strlen(path) > 4096)
    {
        return NULL;
    }

    if ( *(path + strlen(path) - 1) != '/' )
    {
        return path;
    }
    else if ( *(path + strlen(path) - 1) == '/') 
    {
        *(path + strlen(path) -1) = '\0';
        return path;
    }
    
    return NULL;
}


//替换路径
char * replace_path(char *outpath, char *inpath, char *eqpath, char *dstpath, char flag)
{
    char *tmp = NULL; 

    if ( inpath == NULL || 0 == strlen(inpath) ) return NULL;
    if ( !(SF_ALL == flag || SF_GROW ==  flag || SF_ALL_NOTIME) ) return NULL;
    if ( NULL == eqpath || 0 == strlen(eqpath) ) return NULL;

    tmp = strstr(inpath, eqpath);
    if (NULL == tmp) return NULL;
    
    memcpy(outpath, dstpath, strlen(dstpath));
    judge_path_del(outpath);
    memcpy(outpath + strlen(outpath) , tmp + strlen(eqpath), strlen(inpath) - strlen(eqpath) );

    return outpath;
}


/*
  检查文件操作接口是否正确
  typedef int (*file_oper_function_t)(CTask *job) ;
  返回0 正确
  返回-1 不正确
*/
int check_operfile_process(file_oper_function_t func)
{
    file_oper_function_t oper_func = func;
    int rtn = -1;

    if (Create_Dir_S == oper_func)
    {
        rtn = 0;
    }
    else if (Create_Dir_N == oper_func)
    {
        rtn = 0;
    }
    else if (Create_File_S == oper_func)
    {
        rtn = 0;
    }
    else if (Create_File_L == oper_func)
    {
        rtn = 0;
    }
    else if (Create_File_N == oper_func)
    {
        rtn = 0;
    }
    else if (Del_Dir == oper_func)
    {
        rtn = 0;
    }
    else if (Del_File == oper_func)
    {
        rtn = 0;
    }
    else if (Alter_File == oper_func)
    {
        rtn = 0;
    }
    else if (Move_Dir == oper_func)
    {
        rtn = 0;
    }
    else if (Move_File == oper_func)
    {
        rtn = 0;
    }
    
    return rtn;
}

//复制软链接目录
int Create_Dir_S(CTask * arg)
{   
    return 0;
}


//复制普通目录
int Create_Dir_N(CTask * arg)
{
    char write_path[MAX_STR_PATH];
    memset(write_path, '\0', sizeof(write_path));
    memcpy(write_path, (arg)->dst_path_p, (arg)->dst_path_size);

    /*
    if ( NULL == replace_path(write_path, arg->src_path, arg->eq_path, arg->dst_path,arg->flag) )
    {
        return -1;
    }
    */

    if ( create_dirs(write_path) == -1 )  
    {   
        syslog(LOG_ERR, "Create dir %s failed!\n", write_path);
        return -1; 
    }  

    return 0;
}

//取上层目录路径
char *top_dir(const char *inpath, char *outpath)
{
    int len;
    size_t count = 0;
    char *p = NULL;

    len = strlen(inpath);
    char tmp[len + 1];
    char path[len + 1];
    memset(tmp, '\0', sizeof(tmp));
    memset(path, '\0', sizeof(tmp));
    strcpy(path, inpath);
 
    //检查是否为绝对路径
    if (*path != '/') 
    {
        return NULL;
    }
    else
    {
        //去掉inpath最后的'/'
        if ( *(path + strlen(path) -1) == '/') 
        {
            *(path + strlen(path) -1) = '\0';
        }

        //统计'/'的个数
        count = get_char_num(path, '/');

        //若路径为根目录，或者上层目录就是根目录，则返回
        if ( 1 == strlen(path) )  
        {
            return NULL;
        }
        else if ( (strlen(path) > 1) && (1 == count))
        {
            return NULL;
        }
    
        //返回上层目录路径
        p = NULL;
        p = strrchr(path, '/');  
        *p = '\0';
        strcpy(outpath, path);

        return outpath;
    }
    return NULL;
}


//创建多级目录
int create_dirs(const char *path)
{
    int len;

    len = strlen(path);
    char tmppath[len+1];    
    char swappath[len+1];    

    memset(tmppath, '\0', sizeof(tmppath));
    memset(swappath, '\0', sizeof(swappath));
    strcpy(tmppath, path);

    if (NULL == path)
    {
        return -1;
    }
    
    //path路径不存在
    pthread_mutex_lock(&path_lock);
    if ( -1 == access(tmppath, F_OK) )  //若path路径不存在
    {
        pthread_mutex_unlock(&path_lock);
        //取上层路径
        if ( NULL == top_dir(tmppath, swappath) )
        {
            syslog(LOG_ERR, "Get top path %s failed!\n", tmppath);
            return -1;
        }
        else 
        {
            if (0 == create_dirs(swappath))
            {
                mkdir(tmppath, 0755);
                return 0;
            }
        } 
    }
    //path路径存在
    else 
    {
        pthread_mutex_unlock(&path_lock);
        return 0;
    }

    return -1;
}


//复制软连接
int Create_File_S(CTask * arg)
{

    return 0;
}


int Create_File_L(CTask * arg)
{

    return 0;
}

//弄好文件的偏移量之后，即可调用该函数复制文件
int continue_file(int readfd, int writefd, const char * readpath, const char * writepath)
{
#if DISPLAY_DEBUG_SYSLOG_GROW
    syslog(LOG_ERR, "entering continue_file fuction!\n");
#endif
    int i; 
    char buf[GOOD_READ_BUF_SIZE + 1];

    do      
    {       
        memset(buf, '\0', sizeof(buf));
        //读取读文件
        if ( !(i = read(readfd, buf, sizeof(buf) - 1)) ) 
        {   
            //读取到文件末尾
            break;
        }       
        /*
        else if (-1 == i)
        */
        else if (i < 0) 
        {       
            //读取文件失败
            syslog(LOG_ERR , "Read the file %s failed.\n", readpath);
            return -1;
        }       

/*
        //上次使用的话，strlen()，在复制dd创建的文件时候，无法同步文件
        if ( -1 == write(writefd, buf, strlen(buf)) )
*/
        //写入到写文件
        if ( -1 == write(writefd, buf, i) )
        {       
            //写入文件失败
            syslog(LOG_ERR , "Write the file %s failed.\n", writepath);
            return -1;
        }
    }while (1);
    
    return 0;
}


int continue_exist_file_grow(int readfd, int writefd, const char * readpath, const char * writepath)
{
    char readbuf[GOOD_READ_BUF_SIZE + 1];
    char writebuf[GOOD_WRITE_BUF_SIZE + 1];
    int rtn_read;
    int rtn_write;
    int i_write;
    int rtn_strcmp;
    int length_read;
    int length_write;
    off_t len;
  
    do
    {
        memset(readbuf, '\0', sizeof(readbuf));
        rtn_read = read(readfd, readbuf, sizeof(readbuf) - 1);
        if (0 < rtn_read)
        { 
            memset(writebuf, '\0', sizeof(writebuf));
            rtn_write = read(writefd, writebuf, sizeof(writebuf) - 1);
            if (0 < rtn_write)
            {   
                length_read = strlen(readbuf);
                length_write = strlen(writebuf);

                if (length_read == length_write)
                {
                    rtn_strcmp = strcmp(readbuf, writebuf);
                    if (0 == rtn_strcmp)
                    {

                    }
                    else 
                    {
						len = lseek(writefd, (0 - rtn_read), SEEK_CUR);
                        if (-1 == len)
                        {
                            syslog(LOG_ERR, "calling lseek is error!\n");
                            return -1;
                        }

                        i_write = write(writefd, readbuf, rtn_read);
                        if (i_write == rtn_read)
                        {
                        
                        }
                        else if (-1 == i_write)
                        {
                            syslog(LOG_ERR, "write to file %s failed!\n", writepath);
                            return -1;
                        }
                        else 
                        {
                            syslog(LOG_ERR, "write to file %s abnormal!\n", writepath);
                            return -1;
                        }
                    }
                    
                }
                else 
                {
					len = lseek(writefd, (0 - rtn_write), SEEK_CUR);
					if (-1 == len)
					{
						syslog(LOG_ERR, "calling lseek is error!\n");
						return -1;
					}

                    i_write = write(writefd, readbuf, rtn_read);
                    if (i_write == rtn_read)
                    {
                            
                    }
                    else if (-1 == i_write)
                    {
                        syslog(LOG_ERR, "write to file %s failed!\n", writepath);
                        return -1;
                    }
                    else 
                    {
                        syslog(LOG_ERR, "write to file %s abnormal!\n", writepath);
                        return -1;
                    }                    
                }
                
            }
            else if (0 == rtn_write)
            {
				i_write = write(writefd, readbuf, rtn_read);
				if (i_write == rtn_read)
				{
					
				}
				else if (-1 == i_write)
				{
				   syslog(LOG_ERR, "write to file %s failed!\n", writepath);
				   return -1;
				}
				else 
				{
					syslog(LOG_ERR, "write to file %s abnormal!\n", writepath);
					return -1;
				}
				
            }
            else if (-1 == rtn_write)
            {
                syslog(LOG_ERR , "read the file %s failed.\n", writepath);
                return -1;
            }
        }
        else if (0 == rtn_read)
        {
            off_t length = lseek(readfd, 0, SEEK_CUR);
            if (-1 == ftruncate(writefd, length))
            {
                syslog(LOG_ERR, "calling ftruncate is abormal!\n");
                return -1;
            }
            break;
        }
        else if (-1 == rtn_read)
        {
            syslog(LOG_ERR , "read the file %s failed.\n", readpath);
            return -1;
        }
    }while(1);

    
}


/*
    覆盖文件接口：能够做到截取文件，修改文件内容
    成功返回 0
    失败返回 -1
*/
int cover_file(int readfd, int writefd, const char * readpath, const char * writepath)
{
#if DISPLAY_DEBUG_SYSLOG_GROW
    syslog(LOG_ERR, "entering cover_file fuction, readpath=%s, writepath=%s!\n", readpath, writepath);
#endif
    int i,j = 2,k;               //j=2，只要初始化j时，j不为0即可。
    char readbuf[GOOD_READ_BUF_SIZE + 1];
    char writebuf[GOOD_WRITE_BUF_SIZE + 1];

    do      
    {       
        memset(readbuf,  '\0', sizeof(readbuf));
        //读取读文件
        if ( !(i = read(readfd, readbuf, sizeof(readbuf) - 1)) ) 
        {   
            //读取到文件末尾
            break;
        }
        else if (-1 == i)
        {       
            //读取文件失败
            syslog(LOG_ERR , "read the file %s failed.\n", readpath);
            return -1;
        }

        //若没有读取到写文件末尾
        if (0 != j)
        {
            memset(writebuf, '\0', sizeof(writebuf));
            //读取写文件
            if ( !(j = read(writefd, writebuf, sizeof(writebuf) - 1)) ) 
            {
                //读取到文件末尾
                j = 0; //标记读取到文件末尾
            } 
            else if (-1 == j) 
            { 
                //读取文件失败
                syslog(LOG_ERR , "read the file %s failed.\n", writepath);
                return -1;
            } 
 
            //比较2次读取文件内容
            k = strcmp(readbuf, writebuf);
            if (0 != k)
            {       
                //写入到写文件
                if ( -1 == write(writefd, readbuf, strlen(writebuf)) )
                {        
                    syslog(LOG_ERR , "write the file %s failed.\n", writepath);
                    return -1;
                } 
            }
        }
        //若已读取到写文件末尾
        else
        {
            if ( -1 == write(writefd, readbuf, strlen(writebuf)) )
            {        
                syslog(LOG_ERR , "write the file %s failed.\n", writepath);
                return -1;
            }  
        }
    }while (1);
    
    return 0;
}



int breakpoint_copy_file(int readfd, int writefd, const char * readpath, const char * writepath)
{
    off_t length_write;
    
    length_write = lseek(writefd, 0, SEEK_END);
    if (-1 == length_write)
    {
        syslog(LOG_ERR, "calling lseek is error!\n");
        return -1;
    }
    length_write = lseek(writefd, length_write - 1, SEEK_SET);
    if (-1 == length_write)
    {
        syslog(LOG_ERR, "calling lseek is error!\n");
        return -1;
    }

    
    off_t length_read;
    
    length_read = lseek(readfd, length_write, SEEK_SET);
    if(-1 == length_read)
    {
        syslog(LOG_ERR, "calling lseek is error!\n");
        return -1;
    }

    if (-1 == continue_file(readfd, writefd, readpath, writepath))
    {
        syslog(LOG_ERR, "calling continue_file is error!\n");
        return -1;
    }

    return 0;
}




//复制普通文件
int Create_File_N(CTask * arg)
{
#if DISPLAY_DEBUG_SYSLOG_GROW
    syslog(LOG_ERR, "entering Create_File_N fuction!\n");
#endif
    char write_path[MAX_STR_PATH];
    char read_path[MAX_STR_PATH];
    int  write_fd = 0;
    int  read_fd = 0;

    memset(&write_path, '\0', sizeof(write_path));
    memset(&read_path, '\0', sizeof(read_path));

    memcpy(write_path, arg->dst_path_p, arg->dst_path_size);
    memcpy(read_path,  arg->src_path_p, arg->src_path_size);

    //打开读文件
    if ( -1 == (read_fd = open(read_path, O_RDONLY)) )
    {
        syslog(LOG_ERR , "open the file %s failed.\n", read_path);
        return -1; 
    }

    //检查写文件是否存在，写文件不存在
    if ( -1 == access(write_path, F_OK) ) 
    { 
        //取上层路径
        if ( NULL == top_dir(write_path, read_path) )
        {
            return -1;
        }

        //检查上层路径是否存在，不存在则创建目录
        if ( -1 == access(read_path, F_OK) )
        {
            if ( -1 == create_dirs(read_path) )
            {
                return -1;
            }
        }
/*
        //创建文件
        if ( -1 == (write_fd = creat(write_path, 0644)))
        {
            syslog(LOG_ERR , "creat and open the file %s failed.\n", write_path);
            close(read_fd);
            return -1; 
        }
*/
        //打开写文件
        if ( -1 == (write_fd = open(write_path, O_RDWR | O_CREAT, 0644)) ) 
        {   
            syslog(LOG_ERR , "open the file %s failed.\n", write_path);
            close(read_fd);
            return -1; 
        }  

        //复制文件
        if ( -1 == continue_file(read_fd, write_fd, read_path, write_path) )
        {
            syslog(LOG_ERR, "continue the file %s,%s failed.\n", read_path, write_path);
            close(write_fd);
            close(read_fd);
            return -1;
        }

        close(write_fd);
        close(read_fd);

    } 
    //写文件存在
    else 
    {
        syslog(LOG_ERR, "the file %s is already existed, cover it!\n", write_path);
        //打开写文件，读写打开
        if ( -1 == (write_fd = open(write_path, O_RDWR, 0644)) ) 
        {
            syslog(LOG_ERR , "open the existed file %s failed.\n", write_path);
            close(read_fd);
        }

        if (-1 == continue_exist_file_grow(read_fd, write_fd, read_path, write_path))
        {
            syslog(LOG_ERR, "continue existed file %s failed!\n", write_path);
            close(write_fd);
            close(read_fd);
            return -1;
        }
        
/*
        //调用覆盖文件接口：能够做到截取文件，修改文件内容
        if ( -1 == cover_file(read_fd, write_fd, read_path, write_path) )
        {
            syslog(LOG_ERR, "cover the file %s,%s failed.\n", read_path, write_path);
            close(write_fd);
            close(read_fd);
            return -1;
        }
*/

/*
        //复制文件
        if ( -1 == continue_file(read_fd, write_fd, read_path, write_path) )
        {
            syslog(LOG_ERR, "cover the file %s,%s failed.\n", read_path, write_path);
            close(write_fd);
            close(read_fd);
            return -1;
        }
*/

        close(write_fd);
        close(read_fd);
    }

    return 0;
}


//删除目录
int Del_Dir(CTask * arg)
{

    return 0;
}

//删除文件
int Del_File(CTask * arg)
{

    return 0;
}

//修改文件
int Alter_File(CTask * arg)
{

    return 0; 
}

//移动目录
int Move_Dir(CTask * arg)
{

    return 0;
}

//移动文件
int Move_File(CTask * arg)
{

    return 0;
}











