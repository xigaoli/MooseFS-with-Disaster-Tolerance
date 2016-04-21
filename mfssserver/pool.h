/************************************************************
  Copyright (C), 2014, Nari.
  FileName:     pool.h
  Author:       mawenxiang
  Version :     v1.0      
  Date:         2014-10 
  Description:  该源文件接口，为文件同步全量和增量的主要接口，
      实现了线程池同步文件高并发，高吞吐量的基本要求。  
***********************************************************/

#ifndef _POOL_H_
#define _POOL_H_

/*
typedef struct tablefromgrow {
    char *filepath;         //文件路径
    char type;              //类型('d' or 'f')
    int  inode;             //inode
    char operationname[16]; //操作名称
} Table_From_Grow;
*/

typedef struct tableto {
    char flag;   //全量、增量标志，SF_ALL全量，SF_GROW增量
    int *table;  //存放失败inode的地方
    int size;    //失败inode个数
} Table_To;

//初始化线程池  
int poolthread_init (int min_thread_num, int max_thread_num);

//数字转字符串
char * itoa(int num, char *str, int radix);

//去掉字符串末尾的'\n'字符
void del_line_char(char *str, int str_size);

//取float一定范围的数字
double about(double num);

//分类调用任务添加接口
void *pool_add_worker (void *arg);
  
//工作线程
void *thread_routine (void *arg); 

//全量添加文件
int add_allfile(sfflag_t flag, char * src_path, char *eq_path, char * dst_path, struct list_head *head,\
                             time_t *starttimer, time_t *endtimer, unsigned long int *num);

//线程数利用率
double percent_thread_num(int cur_thread_num, int max_thread_num);

//队列数利用率
double percent_queue_num(int cur_queue_size, int max_thread_num);

//线程数目
int get_need_thread_num();

//增加线程
int add_thread(int num);

//减少线程
int del_thread(int num);

//根据任务数目，调整线程数目
void dynamic_add_or_del_thread();

//到公共表取数据，之后向线程池中加入任务  
void *pool_add_worker(void *arg);
  
//销毁线程池，等待队列中的任务不会再被执行，但是正在运行的线程会一直 把任务运行完后再退出  
int pool_destroy();

//获取源路径
char *get_srcpath(char *outpath, const char *path, const char *filename);

//检查子增量的路径是否是绝对路径
int check_work_path(const char *init_path, const char *filepath);

//工作线程函数
void * thread_routine(void *arg);

//外界调用接口
Table_To copy_run(char flag, Table_From_Grow table[], int max_num, time_t st_timer, time_t ed_timer,  \
                       const char *srcpath, const char *eqpath, const char *dstpath);

//检查flag参数是否正确
int check_sf_flag(char flag);

//检查路径
int check_path(const char *path);

//校验year
int check_year(int year);

//校验mon
int check_mon(int mon);

//校验day
int check_day(int day);

//校验hour
int check_hour(int hour);

//校验min
int check_min(int min);

//校验sec
int check_sec(int sec);

//把字符串形式的时间转换成time_t格式的时间
int time_change(const char *start_time, const char *end_time, time_t *st_timer_p, time_t *ed_timer_p);

//命令接口，全量使用
int copy_cmd(int argc, char **argv);

#endif

 


