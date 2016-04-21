/************************************************************
  Copyright (C), 2014, Nari.
  FileName:     libfile.c
  Author:       mawenxiang
  Version :     v1.0      
  Date:         2014-10 
  Description:  本头文件声明的函数，均为全量文件列表
      的内容提供服务的。    
***********************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include "list.h"

#include "common.h"
#include "libfile.h"


//初始化路径
char * init_path(char *inpath,char *outpath) 
{
    int length;

    if ( inpath == NULL )
    {
        return NULL;
    }    
    
    length = strlen(inpath);

    if ( *(inpath + length - 1) == '/' )
    {
        strncat(outpath, inpath, length - 1);
    }
    else
    {
        strcat(outpath, inpath);
    }

    return outpath;
}

/*
  遍历目录
*/
int trave_dir(char * src_path, struct list_head *head, struct tm *firsttim, struct tm *endtim)
{
    DIR *d; //声明一个句柄
    struct dirent *file; //readdir函数的返回值就存放在这个结构体中
    struct stat sb;    
    char path[MAX_PATH_BUF];

    time_t firstsec = 0;
    time_t endsec = 0;

    memset(path,'\0',sizeof(path));
    init_path(src_path,path);
   
    if (!(d = opendir(path)))
    {
        //printf("error opendir %s!!!\n",path);
        return -1;
    }

    while ((file = readdir(d)) != NULL)
    {
        //把当前目录.，上一级目录..及隐藏文件都去掉，避免死循环遍历目录
        if(strncmp(file->d_name, ".", 1) == 0)
            continue;

        strcat(filetmp, path);
        strcat(filetmp, "/");
        strcat(filetmp, file->d_name);
                  
        firstsec = mktime(firsttim);
        endsec = mktime(endtim);
         
        if ( (lstat(filetmp, &sb) >= 0) && ( ((&(sb.st_mtim))->tv_sec >= firstsec) && ((&(sb.st_mtim))->tv_sec <= endsec)) ) 
        {
            Get_File_t * f_p = (Get_File_t *)malloc( sizeof(Get_File_t) );
            memset(f_p->filename, '\0', sizeof(f_p->filename));

            //判断该文件类型
            if (S_ISLNK(sb.st_mode)) //符号链接
            {
                struct stat Is_link;    
                if (stat(filetmp, &Is_link) >= 0) 
                {
                    if (S_ISDIR(Is_link.st_mode)) //符号链接目录
                    {
                        f_p->sign = 'S';
                    }
                    else if (S_ISREG(Is_link.st_mode)) //符号连接文件
                    {
                        f_p->sign = 's';
                    }
                }
                else 
                {
                    //syslog
                }
                strcpy(f_p->filename, filetmp);
                list_add_tail( &(f_p->list), head ); 
                memset(filetmp, '\0', sizeof(filetmp));
            }
            else if (S_ISDIR(sb.st_mode)) //目录
            {
                f_p->sign = 'D';
                strcpy(f_p->filename, filetmp);
                list_add_tail( &(f_p->list), head ); 
                memset(filetmp, '\0', sizeof(filetmp));
                trave_dir(f_p->filename, head, firsttim, endtim);
             
            }
            else if (S_ISREG(sb.st_mode)) //普通文件
            {
                f_p->sign = 'F';
                strcpy(f_p->filename, filetmp);
                list_add_tail( &(f_p->list), head ); 
                memset(filetmp, '\0', sizeof(filetmp));
                
            }
            else
            {
                //syslog
            }
        }
        else 
        {
            //syslog
        }
        memset(filetmp,'\0',sizeof(filetmp));
    }
    closedir(d);
    return 0;
}

//获取某一个路径下，某一时间端的文件列表
struct list_head * get_allfile(const char *path, Filetime starttimer, Filetime endtimer)
{
    struct list_head *head;      //头部 
    char tmp_path[MAX_PATH_BUF];

    head = (struct list_head *)malloc(sizeof(struct list_head));   
/*
    struct tm start_time = {starttimer.sec, starttimer.min, starttimer.hour, starttimer.day, starttimer.mon -1, starttimer.year - 1900,0, 0, 0};
    struct tm end_time = {endtimer.sec  , endtimer.min,   endtimer.hour,   endtimer.day,   endtimer.mon -1,   endtimer.year - 1900,  0, 0, 0};
*/
    struct tm start_time;
    struct tm end_time;

    start_time.tm_sec =  starttimer.sec;
    start_time.tm_min = starttimer.min;
    start_time.tm_hour = starttimer.hour;
    start_time.tm_mday = starttimer.day;
    start_time.tm_mon = starttimer.mon -1;
    start_time.tm_year = starttimer.year - 1900;
/*
    start_time.tm_wday = 
    start_time.tm_yday = 
    start_time.tm_isdst = 
    start_time.tm_gmtoff = 
    start_time.tm_zone = 
*/
    end_time.tm_sec =  endtimer.sec;
    end_time.tm_min = endtimer.min;
    end_time.tm_hour = endtimer.hour;
    end_time.tm_mday = endtimer.day;
    end_time.tm_mon = endtimer.mon -1;
    end_time.tm_year = endtimer.year - 1900;

    memset(tmp_path, '\0', sizeof(tmp_path));
    strcpy(tmp_path, path);
    
    INIT_LIST_HEAD(head);    //初始化头部

    if (-1 == trave_dir(tmp_path, head, &start_time, &end_time)) 
    {
        return NULL;  
    }

    return head;
}











