/************************************************************
  Copyright (C), 2014, Nari.
  FileName:     pool.c
  Author:       mawenxiang
  Version :     v1.0      
  Date:         2014-10 
  Description:  该源文件接口，为文件同步全量和增量的主要接口，
      实现了线程池同步文件高并发，高吞吐量的基本要求。
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
#include <signal.h>

#include "list.h"

#include "common.h"
#include "list.h"
#include "pool.h"
#include "work.h"
#include "libfile.h"
#include "servermap.h"

/*
  MAX_QUEUE_NUM_TEST       当调用copy_run判定是否队列已经满，不适合调用的测试宏，定义即测试数目
  TEST_INIT_THREAD_NUM     初始化时，线程数目，测试使用，定义即测试数目
  TEST_MAX_ADDALL_SIZE     全量，一次性添加到队列中的最大数目，测试使用 ，定义即测试数目
  TEST_ONE_COPY_FILE_SIZE  one_copy_file_size是否是测试值，定义即使用测试值
  TEST_ONE_COPY_FILE_NUM   one_copy_file_num是否是测试值，定义即使用测试值
  上述宏，定义了，即测试所用
*/
/*
#define MAX_QUEUE_NUM_TEST
#define TEST_INIT_THREAD_NUM 
#define TEST_MAX_ADDALL_SIZE
#define TEST_ONE_COPY_FILE_SIZE 
#define TEST_ONE_COPY_FILE_NUM
*/



/*
  one_copy_file_size 给一个线程里面的任务，总共文件大小
  FILE_SIZE_FIVE_G 定义5G大小的字节
  FILE_SIZE_TEN_G  定义10G大小的字节

   1 G = 1024 * 1024 * 1024 = 1073741824 
   5 G = 5368709120
   10 G = 10737418240

  one_copy_file_num  多少文件数目，切换给一个线程
*/
#define FILE_SIZE_FIVE_G  5368709120  
#define FILE_SIZE_TEN_G   10737418240

#ifdef  TEST_ONE_COPY_FILE_SIZE
static off_t one_copy_file_size = 2;
#else
static off_t one_copy_file_size = FILE_SIZE_FIVE_G;
#endif


#ifdef  TEST_ONE_COPY_FILE_NUM
static off_t one_copy_file_num  = 2;
#else
static off_t one_copy_file_num  = 5000;
#endif


/*
  全量使用
  IS_EXIT  打开线程退出标志
  NOT_EXIT 关闭线程退出标志
*/
#define IS_EXIT  1
#define NOT_EXIT 2



/*
  队列允许存在最大的任务数目
*/
#ifdef  MAX_QUEUE_NUM_TEST
#define MAX_QUEUE_NUM 10000
#else
#define MAX_QUEUE_NUM 1000
#endif

/*
  初始化时，线程数目
  INIT_THREAD_NUM   初始化线程数目
  MAX_THREAD_NUM    最大线程数目
*/
#ifdef  TEST_INIT_THREAD_NUM  
#define INIT_THREAD_NUM 20
#else  
#define INIT_THREAD_NUM 10
#endif

#define MAX_THREAD_NUM 25


/*
  全量，增量，一次性添加到队列中的最大数目
*/
#ifdef TEST_MAX_ADDALL_SIZE
#define MAX_ADDALL_SIZE 1000000
#else
#define MAX_ADDALL_SIZE 100000      
#endif



/*
  是否是第一次，若是则初始化
  first_t 标记是否是第一次copy_run接口类型
  IS_FIRST 是第一次
  NO_FIRST 不是第一次
*/
typedef char first_t;
#define IS_FIRST '1'
#define NO_FIRST '0'


/*
  fileoperate_t文件操作类型
  是否能进行文件操作判定宏
  CAN_FILE_OPERATE     能进行文件操作
  CAN_NOT_FILE_OPERATE 不能进行文件操作
*/
typedef int fileoperate_t;
#define CAN_FILE_OPERATE      1 
#define CAN_NOT_FILE_OPERATE  0

/*
  外界标记接口
*/
#if DUG_SIGN_USER 
int sign_operchnglog_from_mapfile(int inode, int result)
{
    return inode;
}
#endif


typedef struct growstruct{
    Table_From_Grow *job_p;           //增量的任务的首地址
    int size;                         //该增量任务中一共具有子任务数目
    int used_size;                    //剩下未取走完成的子任务数目
    int copied_size;                  //剩下未拷贝完成的子任务数目
    char init_eq_path[MAX_PATH_BUF];  //源头挂载目录
    char init_dst_path[MAX_PATH_BUF]; //目的挂载目录
} Grow_Struct;


/* 
*线程池里所有运行和等待的任务都是一个CThread_worker 
*由于所有任务都在链表里，所以是一个链表结构  
*/  
typedef struct worker
{  

    int (*process) (CTask *job); //回调函数，任务运行时会调用此函数，注意也可声明成其它形式

    CTask job;                   //回调函数的参数  

    sfflag_t flag;               //标记是该任务是全量还是增量

    uint32_t inode;              //增量使用，保存inode信息

    void *void_p;                //增量使用，备用指针，指向增量Grow_Struct的结构体内容

    off_t file_size;             //记录该文件的大小，单位字节

    struct list_head list;        
} CThread_worker;



/* 
* 文件记录，结构体
*/  
typedef struct newfile
{
    int (*process) (CTask *job);   //回调函数，任务运行时会调用此函数，注意也可声明成其它形式
    CTask job;                     //回调函数的参数

    uint32_t inode;                //增量使用，保存inode信息
    off_t file_size;               //记录该文件的大小，单位字节

    struct list_head list; 
} New_file;

struct list_head file_queue_head; 


/*
  任务添加线程使用的链表结构
*/
typedef struct addthread_queue_inode
{
    //struct list_head *queue_member;
    //struct list_head queue_member_head;
    struct list_head *queue_member_head;
    struct list_head list;
} Addthread_queue_inode;


struct list_head addthread_queue_inode_head;


pthread_mutex_t addthread_lock  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  addthread_ready = PTHREAD_COND_INITIALIZER;

pthread_mutex_t for_wait_lock   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  for_wait_ready  = PTHREAD_COND_INITIALIZER;

pthread_mutex_t exit_lock   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  exit_ready  = PTHREAD_COND_INITIALIZER; 


//公共表接口
typedef struct public_table {
    char Oper_sign;
    char Link_sign;
    char sourcepath[MAX_PATH_BUF];
    char equalpath[MAX_PATH_BUF];
    char destpath[MAX_PATH_BUF];
} Public_Table;


//该结构体用来传递参数给任务添加线程
typedef struct tablefrom {
    sfflag_t flag;              //全量、增量标志
    Table_To *table_to_p;       //传出内容的指针
    pthread_t *p_t;             //任务添加线程的id

    Table_From_Grow *table;     //增量使用，该指正指向的位置注意用后要销毁
    int max_num;                //增量使用

    time_t st_timer;            //全量使用
    time_t ed_timer;            //全量使用
    char srcpath[MAX_PATH_BUF]; 
    char eqpath[MAX_PATH_BUF];  
    char dstpath[MAX_PATH_BUF]; 
} Table_From;


/*线程池结构*/  
typedef struct threadpool
{ 
    /*工作线程互斥锁*/
    pthread_mutex_t routinethread_lock;  

    /*工作线程条件变量*/
    pthread_cond_t routinethread_ready;  

    /*添加任务线程互斥锁*/
    pthread_mutex_t work_lock;  

    /*添加任务线程条件变量*/
    pthread_cond_t work_ready;

    /*链表结构，线程池中所有等待任务*/  
    struct list_head queue_head; 

    /*是否销毁线程池*/
    int shutdown;

    /*要销毁的线程的数量*/
    int del_thread_num;

    //初始化工作线程id数组的指针
    pthread_t threadid[MAX_THREAD_NUM];  

    /*线程池中允许的活动最多线程数目*/  
    int max_thread_num;  

    /*线程池中允许的活动最少线程数目*/  
    int min_thread_num;  

    /*线程池中当前活动线程数目*/  
    int cur_thread_num;  

    /*当前等待队列的任务数目*/  
    unsigned long int cur_queue_size;  

    /*全量，将要退出去时，还剩下的未完成的任务数目*/    
    unsigned long int cur_ready_queue_num;

    //全量，一次性往队列中添加任务数目最大值
    unsigned long int max_addall_size;
    
    //线程是否退出的标志
    int is_exit;
    
} CThread_pool;  



//控制是否是第一次调用接口,IS_FIRST表示第一次，NO_FIRST表示非第一次
first_t is_first = IS_FIRST;

//遍历list时使用临时变量
CThread_pool *pool = NULL; 

//用来统计添加全量任务个数，控制开关
int count = 0;
 
//任务添加线程，判断是否需要阻塞
void addallfile_is_full_and_broadcast(unsigned long int *cot, unsigned long int *max);

//往任务队列中添加任务
void put_worker(CThread_worker *worker, struct list_head *head);

//分类操作，选择相应的回调函数，备用接口
file_oper_function_t sort_oper_ex(uint8_t type, uint8_t *opername);

//分类操作，并初始化操作需要的元素
int sort_oper_all(New_file *work, char Linkflag, char Operflag, sfflag_t flag);

//增量添加文件
Grow_Struct * add_growfile_old(Table_From *table, struct list_head *head);

//获取公共表里面有多少条记录
int get_table_num(Public_Table *table[]);

//传给给外界的内容放在Table_To结构体中
int save_table_to(Table_To *table_p, int *mem_inode, int failed_num, sfflag_t flag);

//增量任务添加线程
int add_growfile_thread(Table_From *table, struct list_head *head);

//将源路径拷贝到worker任务里面去
void put_src_path(CThread_worker *work, char *path, char *filename);

//获取增量的任务
sign_t get_growing_work(CThread_worker *work_p);

//销毁相关资源
int destroy_add_growfile_thread(Grow_Struct *grow_struct_p);

//执行相应操作后，标记成功与否
void sign(int inode,int *mem, int failed_num);

//专门用来释放Table_To 内存，还有Table_To中table指向的字符内存
void free_Table_To(Table_To *tab);

//专门用来释放Table_From 内存，还有Table_From中p_t指向的字符内存
void free_Table_From(Table_From *tab);

//判断是否是第一次，若是则进行初始化，反之。
int copy_run_first_init();

//接口传进来的内存保存在结构体中
int saveto_table_from(Table_From * table_from_p, sfflag_t flag, Table_From_Grow table[], time_t st_timer, \
                         time_t ed_timer, Table_To *tmp_to, int max_num, pthread_t *add_pid,  \
                         const char *srcpath, const char *eqpath, const char *dstpath);

//任务添加线程启动,成功返回0，失败返回-1
int start_add_thread(pthread_t  *add_pid, void *(*run)(void *), Table_From *from_tmp);

Table_To * thread_return(Table_From *from_tmp, sfflag_t flag, pthread_t *p_t, Table_To *table_to_p);

//等待该增量任务完成
void wait_grow_done(pthread_mutex_t *lock_p, pthread_cond_t *cond_t);


//动态调整线程数目
void dynamic_thread_growfile(pthread_mutex_t *mutex_p, int num);

//初始化线程池  
int poolthread_init(int min_thread_num, int max_thread_num)
{  
    int i = 0;  
    
    if ( 0 == max_thread_num  || min_thread_num > max_thread_num )
    {
        syslog(LOG_ERR , "The max_thread_num is zero, or max_thread_num < min_thread_num!\n");  
        return -1;
    }

    pool = (CThread_pool *) malloc (sizeof (CThread_pool));  
    pthread_mutex_init(&addthread_lock,  NULL);  
    pthread_cond_init (&addthread_ready, NULL);  
    pthread_mutex_init(&(pool->work_lock),   NULL);  
    pthread_cond_init (&(pool->work_ready),  NULL);  

    //线程池共有变量初始化
    pool->min_thread_num = min_thread_num;  
    pool->max_thread_num = max_thread_num;  
    pool->cur_queue_size = 0;  
    pool->cur_thread_num = 0;
    pool->shutdown = 0;  
    pool->del_thread_num = 0;
    pool->max_addall_size = MAX_ADDALL_SIZE;
    pool->is_exit = NOT_EXIT;
    //INIT_LIST_HEAD(&(pool->queue_head));
  
    //初始化工作线程，线程数目用宏控制，线程threadid内容地方，放入到pool->threadid中
    //pool->threadid =(pthread_t *) malloc (max_thread_num * sizeof (pthread_t));  
    for (i = 0; i < min_thread_num; i++)  
    { 
        if (0 == pthread_create (&(pool->threadid[i]), NULL, thread_routine,NULL))
        {
            (pool->cur_thread_num)++;
        }
        else 
        {
            syslog(LOG_WARNING , "create %d working thread failed!\n", i + 1);
            return -1;  
        }
    }  
    syslog(LOG_INFO , "init %d working thread sucessed!\n", pool->cur_thread_num);
    
    return 0;
}  


char * sf_itoa(int num, char *str, int radix)
{
    /*索引表*/
    char sf_index[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    /*中间变量*/
    unsigned unum;
    int i=0,j,k;

    /*确定unum的值*/
    if (radix == 10 && num<0)/*十进制负数*/
    {
        unum = (unsigned) - num;
        str[i++] = '-';
    }
    else
    { 
        /*其他情况*/
        unum = (unsigned)num;
    }

    /*转换*/
    do
    {
        str[i++] = sf_index[unum%(unsigned)radix];
        unum /= radix;
    } while (unum);

    str[i] = '\0';
    /*逆序*/
    if (str[0] == '-')
    {
        k = 1;/*十进制负数*/
    }
    else
    { 
        k = 0;
    }

    char temp;
    for(j = k; j <= (i - 1)/2; j++)
    {
        temp = str[j];
        str[j] = str[i-1+k-j];
        str[i-1+k-j] = temp;
    }
    return str;
}

//去掉字符串末尾的'\n'字符
void del_line_char(char *str, int str_size)
{
    *(str + str_size - 1) = '\0';
}

/*
 用来测试增量的文件接口
*/
void test_for_growfile(long int num, const char *srcpath, const char *eqpath, const char *dstpath)
{
    int i = 0;
    Table_From_Grow growj[num];
    memset(growj, 0, num * sizeof(Table_From_Grow));

    uint8_t filepathname[100];
    for(i = 0; i < num; i++)
    {
        char str[3] = {'\0'};
        char str1[100] = {'\0'};
        sf_itoa(i, str, 10);
        strcat(str1, "/");
        strcat(str1, str);

        memset(filepathname, '\0', sizeof(filepathname));
        strcat(filepathname, str1);        
        strcpy(growj[i].filepath, filepathname);
        growj[i].type = 'f';
        growj[i].inode = 1026;
        memset(growj[i].operationname, '\0', sizeof(growj[i].operationname));
        strcpy(growj[i].operationname, "RELEASE");
    }

    //copy_run(SF_GROW, growj, 1024, 0, 0, "/root/inode/from", "/root/inode/from", "/root/inode/to");
    //copy_run(SF_GROW, growj, 10, 0, 0, "/root/inode/from", "/root/inode/from", "/root/inode/to");
    copy_run(SF_GROW, growj, num, 0, 0, srcpath, eqpath, dstpath);
    sleep( 3600 );
}


//用来测试全量的文件接口
void test_for_allfile(const char *path)
{
/*
    sfflag_t flag = SF_ALL;
    
    time_t t1 = 1414105100;
    time_t t2 = 1414509900;

    copy_run(flag, NULL, 0, t1, t2, path);
*/
    sfflag_t flag = SF_ALL_NOTIME;
    time_t t1 = 0;
    time_t t2 = 0;
    copy_run(flag, NULL, 0, t1, t2, NULL, path, NULL);
}

/*
  任务添加线程即将要退出
*/
void addallfile_will_exit_and_broadcast(unsigned long int  *cot, unsigned long int *max)
{
    //将要退出
    if (*cot <= *max)
    {
        //当前队列中子任务数目
        pthread_mutex_lock( &addthread_lock );

        //记录等待任务数目
        pool->cur_queue_size = pool->cur_queue_size + (*cot);

        //打开要退出标志
        pool->is_exit = IS_EXIT;

        //广播告诉所有线程可以开始工作
        pthread_cond_broadcast(&addthread_ready);
        pthread_mutex_unlock( &addthread_lock );

        //让自己阻塞，等待最后一个完成工作的线程给自己发信号
        pthread_mutex_lock(&for_wait_lock);  
        pthread_cond_wait(&for_wait_ready, &for_wait_lock);  
        pthread_mutex_unlock(&for_wait_lock);  

        *cot = 0; 
    }
    else 
    {
        syslog(LOG_ERR, "cot <= max is abnormal in addallfile_is_full_and_broadcast!\n");
        return ;
    }
    return ;
}


/*
  任务添加线程，判断是否需要阻塞
*/
void addallfile_is_full_and_broadcast(unsigned long int  *cot, unsigned long int *max)
{
    //正在进行中
    if (*cot == *max)
    {   
        //当前队列中子任务数目
        pthread_mutex_lock( &addthread_lock );
        pool->cur_queue_size = pool->cur_queue_size + (*cot);
        pthread_mutex_unlock( &addthread_lock );

        //广播告诉所有线程可以开始工作
        pthread_cond_broadcast(&addthread_ready); 

#if TEST_ALL_DY_THREADNUM
        //在没有发送条件变量信号之前，动态调整线程数量,不需要，全量即按照最大线程数目来处理
        dynamic_add_or_del_thread();
#endif
        *cot = 0; 
        //为了让自己阻塞   
        pthread_mutex_lock(&(pool->work_lock));   
        pthread_cond_wait(&(pool->work_ready), &(pool->work_lock));  
        pthread_mutex_unlock(&(pool->work_lock)); 
    }
    return ;
}


/*
  任务添加线程，切割大的队列
*/
typedef int sfwait_t;  
#define FOR_ALL_WAIT      0
#define FOR_ALL_EXIT      1
#define FOR_GROW_WAIT     2

void add_newfilequeue_broadcast(sfwait_t flag)
{
    //判定是否已经到了最大值，到了最大值则创建新工作队列

    //list使用的临时变量
    struct list_head *file_pos = NULL, *file_n = NULL;
    New_file *file_list_tmp = NULL; 

    off_t new_queuefile_size = 0;
    off_t new_queuefile_num  = 0;
    off_t gz_queuefile_num  = 0;

    /*开始均衡计算*/
    list_for_each_safe(file_pos, file_n, &file_queue_head)
    {
        //取文件记录
        file_list_tmp = list_entry(file_pos, struct newfile, list);

        new_queuefile_num++;

        new_queuefile_size += (file_list_tmp->file_size);

        if ( (new_queuefile_size >= one_copy_file_size) || (new_queuefile_num >= one_copy_file_num) )
        {
            struct list_head *file_queue_head_tmp = (struct list_head *)malloc( sizeof(struct list_head) );
            memset(file_queue_head_tmp, 0, sizeof(struct list_head));

            //分割链表
            list_cut_position(file_queue_head_tmp, &file_queue_head, &(file_list_tmp->list));

            Addthread_queue_inode *addthread_queue_inode_new = (Addthread_queue_inode *)malloc(sizeof(Addthread_queue_inode));
            addthread_queue_inode_new->queue_member_head = file_queue_head_tmp;

            //添加
            pthread_mutex_lock(&(addthread_lock));
            list_add_tail( &(addthread_queue_inode_new->list), &addthread_queue_inode_head ); 
            pthread_mutex_unlock(&(addthread_lock)); 

            new_queuefile_size = 0;
            new_queuefile_num  = 0;
            gz_queuefile_num++;
        }
    }

    if (0 < new_queuefile_num) //剩下的任务
    {
        gz_queuefile_num++;

        struct list_head *file_queue_head_tmp2 = (struct list_head *)malloc( sizeof(struct list_head) );

        list_cut_position(file_queue_head_tmp2, &file_queue_head, &(file_list_tmp->list));

        Addthread_queue_inode *addthread_queue_inode_new2 = (Addthread_queue_inode *)malloc(sizeof(Addthread_queue_inode));
        addthread_queue_inode_new2->queue_member_head = file_queue_head_tmp2;

        pthread_mutex_lock(&(addthread_lock));
        {
            //添加
            list_add_tail( &(addthread_queue_inode_new2->list), &addthread_queue_inode_head );

            //更新当前队列中任务数目
            pool->cur_queue_size = pool->cur_queue_size + gz_queuefile_num;
        }
        pthread_mutex_unlock(&(addthread_lock)); 
    }
    else 
    {
        pthread_mutex_lock(&(addthread_lock));
        {
            //更新当前队列中任务数目
            pool->cur_queue_size = pool->cur_queue_size + gz_queuefile_num;
        }
        pthread_mutex_unlock(&(addthread_lock)); 
    }

#if TEST_ALL_DY_THREADNUM
    //在没有发送条件变量信号之前，动态调整线程数量,不需要，全量即按照最大线程数目来处理
    dynamic_add_or_del_thread();
#endif

    if (FOR_ALL_WAIT == flag)
    {
        //广播告诉所有线程可以开始工作
        pthread_cond_broadcast(&addthread_ready); 

        //为了让自己阻塞
        pthread_mutex_lock(&for_wait_lock);                          
        pthread_cond_wait(&for_wait_ready, &for_wait_lock);       
        pthread_mutex_unlock(&for_wait_lock);  
    }
    else if (FOR_ALL_EXIT == flag)
    {
        //打开要退出标志
        pool->is_exit = IS_EXIT;

        //广播告诉所有线程可以开始工作
        pthread_cond_broadcast(&addthread_ready);

        //让自己阻塞，等待最后一个完成工作的线程给自己发信号
        pthread_mutex_lock(&exit_lock);  
        pthread_cond_wait(&exit_ready, &exit_lock); 
        pthread_mutex_unlock(&exit_lock);
    }
    else if(FOR_GROW_WAIT == flag)
    {

        //广播告诉所有线程可以开始工作
        pthread_cond_broadcast(&addthread_ready); 

        #if 0
        //为了让自己阻塞
        pthread_mutex_lock(&for_wait_lock);                          
        pthread_cond_wait(&for_wait_ready, &for_wait_lock);       
        pthread_mutex_unlock(&for_wait_lock);
        #endif
    }
    else 
    { 
        syslog(LOG_ERR, "flag is error in add_newfilequeue_broadcast!\n");
    }

    return ;
}



//往任务队列中添加任务
void put_worker(CThread_worker *worker, struct list_head *head)
{
    pthread_mutex_lock(&addthread_lock);
    list_add_tail( &(worker->list), head ); 
    pthread_mutex_unlock(&addthread_lock);
    return ;
}

int add_filepath_all(New_file *file, char *srcpath, char *eqpath, char *dstpath) 
{
    if ( 0 != check_path(srcpath) ) 
    {
        syslog(LOG_ERR, "srcpath is error in add_filepath_all!\n");
        return -1;
    }

    if ( 0 != check_path(eqpath) )  
    {
        syslog(LOG_ERR, "eqpath is error in add_filepath_all!\n");
        return -1;
    }

    if ( 0 != check_path(dstpath) ) 
    {
        syslog(LOG_ERR, "dstpath is error in add_filepath_all!\n");
        return -1;
    }

    if (srcpath != strstr(srcpath, eqpath))
    {
        syslog(LOG_ERR, "strstr is error in get_dstpath!\n");
        return -1;
    }
    
    int length = strlen(srcpath) - strlen(eqpath);
    char str[length];
    memset(str, '\0', sizeof(str));
    strcat(str, srcpath + strlen(eqpath) + 1);

    int len = strlen(dstpath) + strlen("/") + strlen(str);
    char *p = (char *)malloc( len + 1 );
    memset(p, '\0', len + 1);

    strcat(p, dstpath);
    strcat(p, "/");
    strcat(p, str);
    (file->job).dst_path_p    = p;
    (file->job).dst_path_size = len;
    
    int len2 = strlen(srcpath);
    char *p2 = (char *)malloc( len2 + 1 );
    memset(p2, '\0', len2);
    strcat(p2, srcpath);
    (file->job).src_path_p    = p2;
    (file->job).src_path_size = len2;

    return 0;
}

int add_flag_all(New_file *file, sfflag_t flag)
{
    if (-1 == check_sf_flag(flag))
    {
        syslog(LOG_ERR, "flag is abnormal in add_flag_all\n");
    }

    (file->job).flag = flag;
    return 0;
}

int add_something_to_queue(struct list_head *list, struct list_head *head)
{
    list_add_tail(list, head);
    return 0;
}

int add_work_to_queue_all(New_file *file, struct list_head *head, pthread_mutex_t lock)
{
    pthread_mutex_lock(&lock);
    list_add_tail( &(file->list), head ); 
    pthread_mutex_unlock(&lock);

    return 0;
}

int add_filesize_all(New_file *file, off_t size)
{
    if (size < 0)
    {
        syslog(LOG_ERR, "add_filesize_all is err!\n");
        return -1;
    }

    file->file_size = size; 

    return 0;
}
int add_put_worker_all(New_file *file, sfflag_t flag, char Linkflag, char Operflag, char *scrpath, char *eqpath, char *dstpath, off_t size, struct list_head *head)
{
    int rtn = 0;

    if ( -1 == add_flag_all(file, flag) )
    {
        syslog(LOG_ERR, "add_flag_all is abnormal!\n");
        rtn = -1;
    }
                
    if ( -1 == sort_oper_all(file, Linkflag, Operflag, flag) )
    {
        syslog(LOG_ERR, "sort_oper_all is abnormal!\n");
        rtn = -1;
    }

    if ( -1 == add_filepath_all(file, scrpath, eqpath, dstpath) )
    {
        syslog(LOG_ERR, "add_filepath_all is abnormal!\n");
        rtn = -1;
    }

    if ( -1 == add_filesize_all(file, size) )
    {
        syslog(LOG_ERR, "add_filesize_all is abnormal!\n");
        rtn = -1;
    }

    if ( -1 == add_something_to_queue(&(file->list), head) )
    {
        syslog(LOG_ERR, "add_work_to_queue is abnormal!\n");
        rtn = -1;
    }

    return rtn;
}


int add_allfile(sfflag_t flag, char *src_path, char *eq_path, char * dst_path, struct list_head *head, time_t *starttimer, time_t *endtimer, unsigned long int *num)
{
    DIR *d; //声明一个句柄
    struct dirent *file; //readdir函数的返回值就存放在这个结构体中
    struct stat sb;
    char path[MAX_PATH_BUF];
    char sf_filetmp[MAX_PATH_BUF];
    int rtn_lstat;

    time_t firstsec = 0;
    time_t endsec = 0;

    memset(sf_filetmp, '\0', sizeof(sf_filetmp));
    memset(path, '\0', sizeof(path));
    if ( NULL == init_path(src_path, path) )
    {
        syslog(LOG_ERR, "init path failed!\n");
        return -1;
    }

    if (!(d = opendir(path)))
    {
        syslog(LOG_ERR, "opendir the path %s failed!\n", path);
        return -1;
    }

    while ((file = readdir(d)) != NULL)
    {
        //把当前目录.，上一级目录..及隐藏文件都去掉，避免死循环遍历目录
        if(strncmp(file->d_name, ".", 1) == 0) continue;

        //判断是否需要阻塞,发信号给工作线程
        //addallfile_is_full_and_broadcast(&count, num);
        if (count >= *num ) 
        {
            add_newfilequeue_broadcast(FOR_ALL_WAIT);
            count = 0;
        }

        strcat(sf_filetmp, path);
        strcat(sf_filetmp, "/");
        strcat(sf_filetmp, file->d_name);

        //获得计算机时间
        firstsec = *starttimer;
        endsec   = *endtimer;

        //比较时间是否符合
        if ((rtn_lstat = (lstat(sf_filetmp, &sb))) >= 0) 
        {
            New_file * newfile = (New_file *)malloc( sizeof(New_file) );
            memset(newfile, '\0', sizeof(*newfile));

            //临时表
            Public_Table something;
            memset(&something, '\0', sizeof(something));

            ++count;

            //判断该文件类型
            if ( S_ISLNK(sb.st_mode) && ( ((&(sb.st_mtim))->tv_sec >= firstsec) && ((&(sb.st_mtim))->tv_sec <= endsec)) ) //符号链接
            {
                struct stat Is_link;
                if (stat(sf_filetmp, &Is_link) >= 0) 
                {
                    if (S_ISDIR(Is_link.st_mode))      //符号链接目录
                    {
                        something.Link_sign = 'S';
                        something.Oper_sign = 'C';
                    }
                    else if (S_ISREG(Is_link.st_mode)) //符号连接文件
                    {
                        something.Link_sign = 'S';
                        something.Oper_sign = 'c';
                    }
                }
                else 
                {
                    syslog(LOG_ERR, "get %s stat failed!\n", sf_filetmp);
                }
                strcat(something.sourcepath, sf_filetmp);
                strcat(something.equalpath,  eq_path);
                strcat(something.destpath,  dst_path);

                if ( -1 == add_put_worker_all(newfile, flag, something.Link_sign, \
                                              something.Oper_sign, something.sourcepath, \
                                              something.equalpath, something.destpath, 0, head) )
                {
                    syslog(LOG_ERR, "add_put_worker_all is error!\n");
                }

                memset(sf_filetmp, '\0', sizeof(sf_filetmp));
            }
            else if (S_ISDIR(sb.st_mode)) //目录
            {
                if ( ((&(sb.st_mtim))->tv_sec >= firstsec) && ((&(sb.st_mtim))->tv_sec <= endsec) )
                {
                    something.Link_sign = 'N';
                    something.Oper_sign = 'C';
                    strcat(something.sourcepath, sf_filetmp);
                    strcat(something.equalpath,  eq_path);
                    strcat(something.destpath,  dst_path);

                if ( -1 == add_put_worker_all(newfile, flag, something.Link_sign, \
                                              something.Oper_sign, something.sourcepath, \
                                              something.equalpath, something.destpath, 0, head) )
                {
                    syslog(LOG_ERR, "add_put_worker_all is error!\n");
                }
                
                }
                add_allfile(flag, sf_filetmp, eq_path, dst_path, head, starttimer, endtimer, num);
                memset(sf_filetmp, '\0', sizeof(sf_filetmp));
            }
            //普通文件
            else if ( S_ISREG(sb.st_mode) && ( ((&(sb.st_mtim))->tv_sec >= firstsec) && ((&(sb.st_mtim))->tv_sec <= endsec)) ) 
            {
                something.Link_sign = 'N';
                something.Oper_sign = 'c';
                strcat(something.sourcepath, sf_filetmp);
                strcat(something.equalpath,  eq_path);
                strcat(something.destpath,  dst_path);

                if ( -1 == add_put_worker_all(newfile, flag, something.Link_sign, \
                                        something.Oper_sign, something.sourcepath, \
                                        something.equalpath, something.destpath, sb.st_size, head) )
                {
                    syslog(LOG_ERR, "add_put_worker_all is error!\n");
                }

                memset(sf_filetmp, '\0', sizeof(sf_filetmp));
            }
        }
        else if (rtn_lstat < 0)
        {
            syslog(LOG_ERR, "lstat %s file failed!\n", sf_filetmp);
        }
        memset(sf_filetmp,'\0',sizeof(sf_filetmp));
    }

    //列表处理完成，工作线程可以开始工作
    add_newfilequeue_broadcast(FOR_ALL_EXIT);

    closedir(d);
    return 0;
}



/*
   分类操作，选择相应的回调函数，备用接口 
   失败返回NULL
   成功返回 对应的文件操作接口名字
*/
file_oper_function_t sort_oper_ex(uint8_t type, uint8_t *opername)
{
    file_oper_function_t process_tmp;

    if ('d' == type) //目录
    {
        if ( 0 == strcmp(opername, "RELEASE") ) //创建
        {
            process_tmp = Create_Dir_N;
        }
        else if ( 0 == strcmp(opername, "UNLINK") ) //删除 
        {
            process_tmp = Del_Dir;
        }
        else 
        {
            syslog(LOG_ERR, "There is a work that opername is abnormal!\n");
            process_tmp = NULL;
        }
    }
    else if ('f' == type) //文件
    {
        if ( 0 == strcmp(opername, "RELEASE") )
        {
            process_tmp = Create_File_N;
        }
        else if ( 0 == strcmp(opername, "UNLINK") )
        {
            process_tmp = Del_File;
        }
        else 
        {
            syslog(LOG_ERR, "There is a work that opername is abnormal!\n");
            process_tmp = NULL;
        }
    }
    else
    {
        syslog(LOG_ERR, "There is a  work that type is abnormal!\n");
        process_tmp = NULL;
    }

    return process_tmp;
}

//分类操作，并初始化操作需要的元素
int sort_oper_all(New_file *work, char Linkflag, char Operflag, sfflag_t flag)
{
    if (NULL == work) return -1;

    switch (Operflag) 
    {
        case 'C':
            switch (Linkflag)
            {
                case 'S':
                    work->process = Create_Dir_S;
                    break;
                case 'N':
                    work->process = Create_Dir_N;
                    break;
                default:
                    //syslog
                    break;
            }
            break;
        case 'c':
            switch (Linkflag)
            {
                case 'S':
                    work->process = Create_File_S;
                    break;
                case 'L':
                    work->process = Create_File_L;
                    break;
                case 'N':
                    work->process = Create_File_N;
                    break;
                default:
                    //syslog
                    break;
            }
            break;
        case 'D':
            work->process = Del_Dir;
            break;
        case 'd':
            work->process = Del_File;
            break;
        case 'a':
            work->process = Alter_File;
            break;
        case 'M':
            work->process = Move_Dir;
            break;
        case 'm':
            work->process = Move_File;
            break;
        default:
            //syslog
            return -1;
    }
    return 0;
}

//取float一定范围的数字
double about(double num)
{
    double float_num = 0.0;
   
    if      ( num < 0.0                ) return      -1;     
    else if ( num > 0.0  &&  0.1 >= num) float_num = 0.0;
    else if ( num > 0.1  &&  0.2 >= num) float_num = 0.1;
    else if ( num > 0.2  &&  0.3 >= num) float_num = 0.2;
    else if ( num > 0.3  &&  0.4 >= num) float_num = 0.3;
    else if ( num > 0.4  &&  0.5 >= num) float_num = 0.4;
    else if ( num > 0.5  &&  0.6 >= num) float_num = 0.5;
    else if ( num > 0.6  &&  0.7 >= num) float_num = 0.6;
    else if ( num > 0.7  &&  0.8 >= num) float_num = 0.7;
    else if ( num > 0.8  &&  0.9 >= num) float_num = 0.8;
    else if ( num > 0.9  &&  1.0 >= num) float_num = 0.9;
    else if ( num > 1.0                ) float_num = 1.0;
    
    return float_num;
} 

//现在有的线程数/最大工作线程数目 比重，即线程利用率
double percent_thread_num(int cur_thread_num, int max_thread_num)
{
    double per = 0;

    per = about((double)cur_thread_num/max_thread_num);

    return per;
}

// 队列/最大工作线程数目 的比重
double percent_queue_num(int cur_queue_size, int max_thread_num)
{
    double per = 0;

    per = about((double)cur_queue_size/max_thread_num);   

    return per;
}


//线程数目
int get_need_thread_num_old()
{
    int need_thread_num = 0;//需要线程数目
    int add_thread_num = 0; //增加线程数目
    int del_thread_num = 0; //删除线程数目
    int rtn_thread_num = 0; //返回的线程数目，大于0增加线程，等于0不做任务操作，小于0减少线程

    pthread_mutex_lock(&addthread_lock);    
    unsigned long int cur_queue_size = pool->cur_queue_size;
    pthread_mutex_unlock(&addthread_lock);

    int cur_thread_num = pool->cur_thread_num;
    int max_thread_num = pool->max_thread_num;   

    double percent_que_num = percent_queue_num(cur_queue_size, max_thread_num);  // 队列/最大工作线程数目 的比重
    double percent_thr_num = percent_thread_num(cur_thread_num, max_thread_num); //现在有的线程数/最大工作线程数目 比重，即线程利用率

    //当前活动线程数目大于最大值，则减少线程数目至最大值
    if (cur_thread_num > max_thread_num)
    {
        syslog(LOG_ERR, "'cur_thread_num > max_thread_num' is a problem!\n");
    }

    //任务数目大于当前线程数目，则增加线程
    if (cur_queue_size > cur_thread_num)
    {
        if (1.0 <= percent_que_num ) //任务数目大于或等于线程数目最大值，则分配最大值线程数目
        {
            need_thread_num = max_thread_num; 
            add_thread_num = need_thread_num - cur_thread_num;
            rtn_thread_num = add_thread_num;
        }
        else if (1.0 > percent_que_num) //任务数目大于线程数目最大值，则按区间分配合理的线程数目
        {
            need_thread_num = (percent_que_num + 0.1)*max_thread_num;
            add_thread_num = need_thread_num - cur_thread_num;
            rtn_thread_num = add_thread_num;
        } 
        else 
        {
            syslog(LOG_ERR, "get_need_thread_num() is abnormal\n");
        }   

    }
    //任务数目小于当前线程数目，则适当操作
    else if (cur_queue_size < cur_thread_num)
    {
        if (percent_que_num == percent_thr_num )      //同一层次
        {
            rtn_thread_num = 0;
        }
        else if (percent_que_num < percent_thr_num)   //不同层次
        {
            need_thread_num = (percent_que_num + 0.1)*max_thread_num;
            del_thread_num = need_thread_num - cur_thread_num;
            rtn_thread_num = del_thread_num;
        }
        else 
        {
            syslog(LOG_ERR, "get_need_thread_num() is abnormal\n");
        }
    }
    //任务数目等于当前线程数目,不做任何动作
    else if (cur_queue_size == cur_thread_num) 
    {
        rtn_thread_num = 0;
    } 
    return rtn_thread_num;
}


//线程数目
int get_need_thread_num()
{
    int need_thread_num = 0;//需要线程数目
    int add_thread_num = 0; //增加线程数目
    int del_thread_num = 0; //删除线程数目
    int rtn_thread_num = 0; //返回的线程数目，大于0增加线程，等于0不做任务操作，小于0减少线程

    pthread_mutex_lock(&addthread_lock);    
    unsigned long int cur_queue_size = pool->cur_queue_size;
    pthread_mutex_unlock(&addthread_lock);

    int cur_thread_num = pool->cur_thread_num;
    int max_thread_num = pool->max_thread_num;   

    //当前活动线程数目大于最大值，则减少线程数目至最大值
    if (cur_thread_num > max_thread_num)
    {
        syslog(LOG_ERR, "'cur_thread_num > max_thread_num' is a problem!\n");
        del_thread_num = cur_thread_num - max_thread_num;
        rtn_thread_num = del_thread_num;
        return rtn_thread_num;
    }

    //任务数目大于当前线程数目，则增加线程
    if (cur_queue_size > cur_thread_num)
    {
        if (max_thread_num <= cur_queue_size) //任务数目大于或等于线程数目最大值，则分配最大值线程数目
        {
            need_thread_num = max_thread_num; 
            add_thread_num = need_thread_num - cur_thread_num;
            rtn_thread_num = add_thread_num;
        }
        else if (cur_queue_size < max_thread_num) //任务数目小于线程数目最大值，则按区间分配合理的线程数目
        {
            need_thread_num = cur_queue_size;
            add_thread_num = need_thread_num - cur_thread_num;
            rtn_thread_num = add_thread_num;
        } 
        else 
        {
            syslog(LOG_ERR, "get_need_thread_num() is abnormal\n");
        }   

    }
    //任务数目小于当前线程数目，则适当操作
    else if (cur_queue_size < cur_thread_num)
    {
        if ( cur_thread_num <= (pool->min_thread_num) )
        {
            rtn_thread_num = 0;
        }
        else
        {
            need_thread_num = cur_queue_size;
            del_thread_num = need_thread_num - cur_thread_num;
            rtn_thread_num = del_thread_num;
        }
    }
    //任务数目等于当前线程数目,不做任何动作
    else if (cur_queue_size == cur_thread_num)
    {
        rtn_thread_num = 0;
    }
    return rtn_thread_num;
}


#if 0
/*
  1024个线程内存空间供，动态增加减少线程使用
  idle_flag为0 ，该空间空闲
  idle_flag为1 ，该空间已经被占用
*/
typedef struct threadid_mem{
    pthread_t threadid_m;
    int idle_flag; 
} Threadid_mem;
Threadid_mem thread_list[1024];
/*
  在给定的内存空间中，寻找可利用的 线程空间
  成功返回 可利用空间的首地址
  失败返回 NULL
*/
pthread_t * get_idle_threadidmem(Threadid_mem *pth_list_addr, int pth_list_num)
{
    int count = 0;
    pthread_t *p;
    for(count = 0; count < pth_list_num; count++)
    {
        if (0 == (pth_list_addr[count].idle_flag))
        {
            p = &(pth_list_addr[count]);
            return p;
        }
    }

    syslog(LOG_ERR, "get_idle_threadidmem is abnormal!\n");
    return NULL;
}
#endif

//增加线程
int add_thread(int num)
{
    int i = 0;
    pthread_t threadid[num];
    
    memset(&threadid, '\0', sizeof(pthread_t) * num);

    if (num < 0) return -1;
    
    for (i = 0; i < num; i++)  
    {  
        if ( 0 == pthread_create(&(threadid[i]), NULL, thread_routine, NULL) )
        {
            pthread_mutex_lock( &addthread_lock );
            pool->cur_thread_num++;
            pthread_mutex_unlock( &addthread_lock );
        }
        else 
        {
            syslog(LOG_ERR, "add_thread is abnormal!\n");
        } 
    }  
    return 0;
}


//减少线程
int del_thread(int num)
{
    if (num < 0 ) return -1;

    pthread_mutex_lock (&addthread_lock); 
    if (pool->shutdown)  
    {
        pthread_mutex_unlock (&addthread_lock); 
        return -1; //防止两次调用  
    }
    pool->shutdown = 1;  

    pool->del_thread_num = num;  
    pthread_mutex_unlock( &addthread_lock );

    //唤醒所有等待线程，线程池要销毁了  
    pthread_cond_broadcast (&addthread_ready);  
    
    return 0;  
}  


/*
 根据总共的任务数目，调整线程数目
*/
void dynamic_add_or_del_thread()
{
    int need_num = 0;
    need_num = get_need_thread_num(); 

    if (need_num > 0)  //增加线程
    {
        add_thread( abs(need_num) );
    }
    else if (need_num < 0)  //减少线程
    {
        del_thread( abs(need_num) );
    }
    else if (0 == need_num)  //不错任何操作
    {
        //do nothing
    }

}


//获取公共表里面有多少条记录
int get_table_num(Public_Table *table[])
{
    int length = 0;
    
    if (NULL == table) return -1;

    while (NULL != table[length]) length++;

    return length;
}


//等待该增量任务完成
void wait_grow_done(pthread_mutex_t *lock_p, pthread_cond_t *cond_t)
{
    //等待该增量任务完成
    pthread_mutex_lock( lock_p);
    pthread_cond_wait( cond_t, lock_p);
    pthread_mutex_unlock( lock_p );
}


//传给给外界的内容放在Table_To结构体中
int save_table_to(Table_To *table_p, int *mem_inode, int failed_num, sfflag_t flag)
{
    if (NULL == table_p || NULL == mem_inode) return -1;
    if ( !(SF_GROW == flag || SF_ALL == flag || SF_ALL_NOTIME == flag) ) return -1;
    if (failed_num < 0)  return -1;

    table_p->table = mem_inode;
    table_p->size = failed_num;
    table_p->flag = flag;
    return 0;
}


//销毁相关资源
int destroy_add_growfile_thread(Grow_Struct *grow_struct_p)
{
    if ( NULL == grow_struct_p ) return -1;
    free( grow_struct_p );         //注意释放内存的先后次序
    grow_struct_p = NULL;
    return 0;
}

//动态调整线程数目
void dynamic_thread_growfile(pthread_mutex_t *mutex_p, int num)
{
    //动态调整线程数目
    pthread_mutex_lock( mutex_p );                              
    pool->cur_queue_size = pool->cur_queue_size + num;
    pthread_mutex_unlock( mutex_p );
    dynamic_add_or_del_thread(); 
}


//增量任务添加函数
Grow_Struct * add_growfile_old(Table_From *table, struct list_head *head)
{
    int i = 0;
    if (NULL == table || NULL == head) return NULL; 

    //外界传入的内容拷贝到另外一块区域
    Table_From_Grow *p = (Table_From_Grow *)malloc( (table->max_num) * (sizeof(Table_From_Grow)) );
    memset(p, '\0', (table->max_num) * (sizeof(Table_From_Grow)));

#if CONFIG_IS_HIDE
    //tmp路径
    char tmp[strlen(conf_grow.from_path) + 1 + 1];
    memset(tmp, '\0', strlen(conf_grow.from_path) + 1 + 1);
    strcpy(tmp, conf_grow.from_path);
    judge_path_del(tmp);
#endif

    for( i = 0; i < (table->max_num); i++)
    {
        //整个结构体赋值
        p[i] = ((table->table)[i]);

        //换成本地绝对路径
        memset(p[i].filepath, '\0', sizeof(p[i].filepath));
        strcat(p[i].filepath, ((table->table)[i]).filepath);
    }

    //分配父增量内存，并初始化
    CThread_worker *father_worker = (CThread_worker *)malloc( sizeof(CThread_worker) );
    memset(father_worker, '\0', sizeof(*father_worker));
    father_worker->flag = SF_GROW;                                          //增量标记    

    //分配子增量扩增内存
    Grow_Struct *grow_add = (Grow_Struct *)malloc( sizeof(Grow_Struct) );

    //扩增内容区域赋值
    grow_add->job_p = p;                    //任务内容内存首地址
    grow_add->size = table->max_num;        //该增量任务中一共具有的子任务数目
    grow_add->used_size = table->max_num;   //剩下未取走完成的子任务数目
    grow_add->copied_size = table->max_num; //剩下未拷贝完成的子任务数目

    memset(grow_add->init_eq_path, '\0',    sizeof(grow_add->init_eq_path));
    memset(grow_add->init_dst_path,   '\0', sizeof(grow_add->init_dst_path  ));
    strcpy(grow_add->init_eq_path,    table->eqpath);
    strcpy(grow_add->init_dst_path,   table->dstpath);
    
    ((father_worker->void_p)) = grow_add;

    //增量任务添加 
    pthread_mutex_lock(&addthread_lock);
    pool->cur_queue_size = pool->cur_queue_size + table->max_num;
    list_add_tail( &(father_worker->list), head ); 
    pthread_mutex_unlock(&addthread_lock);

    return  ((Grow_Struct *)(father_worker->void_p));

}


//增量任务添加线程
int add_growfile_thread(Table_From *table, struct list_head *head)
{
    Table_To *tmp_to_pp = table->table_to_p;
    Grow_Struct *grow_struct_p;

    //增加父增量任务
    grow_struct_p = add_growfile_old(table, head);
    if (NULL == grow_struct_p) return -1;

#if  TEST_GROW_DY_THREADNUM 
    //动态调整线程数目
    dynamic_add_or_del_thread(); 
#endif
    //向工作线程发信号
    pthread_cond_broadcast( &addthread_ready ); 

    tmp_to_pp->flag = SF_GROW;
    tmp_to_pp->size = 0;
    tmp_to_pp->table = NULL;
    
    pthread_exit(NULL);

    return 0;
}


int add_growfile(sfflag_t flag, Table_From_Grow *table_addr, int table_num, char *src_path, char *eq_path, char * dst_path, struct list_head *head, unsigned long int *num)
{
    int i = 0;
    int max = table_num;

    //外界传入的内容拷贝到另外一块区域
    Table_From_Grow *p = (Table_From_Grow *)malloc( (max) * (sizeof(Table_From_Grow)) );
    memset(p, '\0', (max) * (sizeof(Table_From_Grow)));
    for( i = 0; i < (max); i++)
    {
        //整个结构体赋值
        p[i] = (table_addr[i]);

        //换成本地绝对路径
        memset(p[i].filepath, '\0', sizeof(p[i].filepath));
        strcat(p[i].filepath, (table_addr[i]).filepath);
    }

    char src_path_tmp[4097] = {'\0'};
    char eq_path_tmp[4097]  = {'\0'};
    char dst_path_tmp[4097] = {'\0'};

    strcat(src_path_tmp, src_path);
    strcat(eq_path_tmp,  eq_path );
    strcat(dst_path_tmp, dst_path);

    judge_path_del(src_path_tmp);
    judge_path_del(eq_path_tmp );
    judge_path_del(dst_path_tmp);

    int src_path_tmp_length = strlen(src_path_tmp);
    int eq_path_tmp_length  = strlen(eq_path_tmp );
    int dst_path_tmp_length = strlen(dst_path_tmp);

    i = 0;

    for( i = 0; i < (max); i++)
    {
        New_file *file = (New_file *)malloc( sizeof(New_file) );

        (file->job).flag = flag;

        (file->job).src_path_size = src_path_tmp_length + strlen(p[i].filepath) + 1;
        (file->job).src_path_p = (char *)malloc( (file->job).src_path_size );
        memset((file->job).src_path_p, '\0', (file->job).src_path_size);
        strcat((file->job).src_path_p, src_path_tmp);
        strcat((file->job).src_path_p, p[i].filepath);

        (file->job).dst_path_size = dst_path_tmp_length + (strlen((file->job).src_path_p) - eq_path_tmp_length) + 1;
        (file->job).dst_path_p = (char *)malloc( (file->job).dst_path_size );
        memset((file->job).dst_path_p, '\0', (file->job).dst_path_size);
        strcat((file->job).dst_path_p, dst_path_tmp);
        strcat((file->job).dst_path_p, (file->job).src_path_p + eq_path_tmp_length);

        file->inode = p[i].inode;

        file->process = sort_oper_ex(p[i].type, p[i].operationname);

#if DISPLAY_DEBUG_SYSLOG_GROW
        syslog(LOG_ERR, "the new file, inode=%d, process=%p, src_path_p=%s, src_path_size=%d,\
                              dst_path_p=%s, dst_path_size=%d, type=%c, opername=%s!\n",\
                              file->inode, file->process, (file->job).src_path_p, (file->job).src_path_size,\
                              (file->job).dst_path_p, (file->job).dst_path_size,p[i].type, p[i].operationname);
#endif

        if ( -1 == add_something_to_queue(&(file->list), head) )
        {
            syslog(LOG_ERR, "add_work_to_queue is abnormal!\n");
        }        

        if (i >= *num) 
        {
            add_newfilequeue_broadcast(FOR_GROW_WAIT);
        }
    }

    //释放零临时拷贝内存
    free(p);
    p = NULL;
    
    add_newfilequeue_broadcast(FOR_GROW_WAIT);

    return 0;
}


//分类调用任务添加接口
void *pool_add_worker(void *arg)  
{  
#if DISPLAY_DEBUG_SYSLOG_GROW
    syslog(LOG_ERR , "This is an adding worker thread.The id of this thread is 0x%x.\n", pthread_self());
#endif
    Table_From *from_tmp = (Table_From *)arg;
    char pathsrc[MAX_PATH_BUF] = {'\0'};
    char patheq[MAX_PATH_BUF] = {'\0'};
    char pathdst[MAX_PATH_BUF] = {'\0'};

    if (NULL == arg) return NULL;

    //增量：接受命令，进行相应操作
    if (SF_GROW == from_tmp->flag ) 
    {
        strcpy(pathsrc, from_tmp->srcpath);
        strcpy(patheq, from_tmp->eqpath);
        strcpy(pathdst, from_tmp->dstpath);
/*
        if ( 0 != add_growfile_thread(from_tmp, &(pool->queue_head)) )
        {
            syslog(LOG_ERR, "Using add_growfileadd_growfile is failed!\n");
        }
*/
        //增量
        if ( 0 != add_growfile(from_tmp->flag, from_tmp->table, from_tmp->max_num, pathsrc, patheq, pathdst, &file_queue_head, &(pool->max_addall_size)) )
        {
            free_Table_From(from_tmp);
            syslog(LOG_ERR, "Using add_allfile is failed!\n");
        }
    }
    //全量：接受时间戳，搜索文件列表，添加文件列表任务
    else if (SF_ALL == from_tmp->flag || SF_ALL_NOTIME == from_tmp->flag)
    {
        strcpy(pathsrc, from_tmp->srcpath);
        strcpy(patheq,  from_tmp->eqpath);
        strcpy(pathdst, from_tmp->dstpath);
        if( SF_ALL_NOTIME == from_tmp->flag)
        {
            from_tmp->st_timer = 0;
            from_tmp->ed_timer = time( (time_t*)NULL ) + 3600*24*365; //把现在的时间往后推迟一年，不可能比较到的时间点 
        }

        //全量添加
        if ( 0 != add_allfile(from_tmp->flag, pathsrc, patheq, pathdst, &file_queue_head, &(from_tmp->st_timer), \
                                              &(from_tmp->ed_timer), &(pool->max_addall_size)) )
        {
            free_Table_From(from_tmp);
            syslog(LOG_ERR, "Using add_allfile is failed!\n");
        }
    }
    else syslog(LOG_ERR, "from_tmp->flag is abnormal!\n");

    return NULL;
}  
  
//销毁线程池，等待队列中的任务不会再被执行，但是正在运行的线程会一直把任务运行完后再退出
int pool_destroy ()  
{  
#if DISPLAY_DEBUG_SYSLOG_GROW
    syslog(LOG_ERR , "This is an destroying worker thread.The id of this thread is 0x%x.\n", pthread_self());
#endif
    //list使用的临时变量
    struct list_head *pos = NULL,*n = NULL;
    CThread_worker *list_tmp = NULL; 

    //防止两次调用  
    if (pool->shutdown)  return -1;
    pool->shutdown = 1;  
  
    //唤醒所有等待线程，线程池要销毁
    pthread_mutex_lock( &addthread_lock);
    pool->del_thread_num = pool->cur_thread_num;
    pthread_mutex_unlock( &addthread_lock);
    pthread_cond_broadcast(&addthread_ready);  

/*
    //存放初始化工作线程id的内存的释放
    free(pool->threadid);  
    pool->threadid = NULL;
*/
  
    //销毁任务队列
    while (!list_empty(&(pool->queue_head)))
    {
        list_for_each_safe(pos, n, &(pool->queue_head))
        {
            list_tmp = list_entry(pos, struct worker, list);
            list_del(pos);
            free(list_tmp); 
            list_tmp = NULL;
        }
    }

    //条件变量和互斥量也别忘了销毁  
    pthread_mutex_destroy(&addthread_lock);  
    pthread_cond_destroy(&addthread_ready);  
       
    free (pool);  
    //销毁后指针置空是个好习惯  
    pool=NULL;  

    return 0;  
}  

//获取源路径
char *get_srcpath(char *outpath, const char *path, const char *filename)
{
    if (NULL == outpath || NULL == path || NULL == path ) return NULL;

    memcpy(outpath, path, strlen(path));
    if ( '/' == *(outpath + strlen(path) -1) )
    {
        memcpy(outpath + strlen(path), filename, strlen(filename)); 
    }
    else
    {
        *(outpath + strlen(path)) = '/';
        memcpy(outpath + strlen(path) + 1, filename, strlen(filename)); 
    }

    return outpath;
}

//将源路径拷贝到worker任务里面去
void put_src_path(CThread_worker *work, char *path, char *filename)
{
    char str[strlen(path) + strlen(filename) + 2];     //考虑到可能的'/'，所以加2
    memset(str, '\0', sizeof(str));

    strcpy( (work->job).src_path_p, get_srcpath(str, path, filename) );   

}

//检查子增量的路径是否是绝对路径
int check_work_path(const char *initpath, const char *filepath)
{
    if ('/' != *(filepath + strlen(initpath))) return -1;
    return 0;
}

//获取增量的任务，找到具体操作文件接口，增、全标记记录进去，inode保存
sign_t get_growing_work(CThread_worker *work_p)
{
    if (NULL == work_p) return SIGN_FAIL;
    sign_t i = 0;

    Table_From_Grow buf = (((Grow_Struct *)(work_p->void_p))->job_p)[((Grow_Struct *)(work_p->void_p))->size - \
                  ((Grow_Struct *)(work_p->void_p))->used_size];  //used_size,该增量任务中一共完成子任务数目

    //校验operation，type，filepath
    if ('d' != buf.type && 'f' != buf.type)     return SIGN_PARAMETER_TYPE_ERR;
    if ( (0 != strcmp(buf.operationname, "RELEASE")) && (0 != strcmp(buf.operationname, "RELEASE")) )  return SIGN_PARAMETER_OPERNAME_ERR;
    if ('/' != *(buf.filepath + 0))      return SIGN_PARAMETER_FILEPATH_ERR;

    //找到具体操作文件接口
    work_p->process = sort_oper_ex(buf.type, buf.operationname);
    if (NULL == work_p->process) return SIGN_FAIL;

/*
    //src_path拷贝
    memset((work_p->job).src_path, '\0', sizeof((work_p->job).src_path));
    strcat((work_p->job).src_path, ((Grow_Struct *)(work_p->void_p))->init_eq_path);
    strcat((work_p->job).src_path, buf.filepath);

    //eq_path拷贝
    memset((work_p->job).eq_path, '\0', sizeof((work_p->job).eq_path));
    strcat((work_p->job).eq_path, ((Grow_Struct *)(work_p->void_p))->init_eq_path);

    //dst_path拷贝
    memset((work_p->job).dst_path, '\0', sizeof((work_p->job).dst_path));
    strcat((work_p->job).dst_path, ((Grow_Struct *)(work_p->void_p))->init_dst_path);
*/

    //增、全标记记录进去
    (work_p->job).flag = work_p->flag;

    //inode保存
    work_p->inode = buf.inode;

    return SIGN_SUCCESS;
}


//执行相应操作后，标记成功与否
void sign(int inode,int *mem, int failed_num)
{
    mem[failed_num -  1] = inode;
}

/*
  执行线程遍历调用字节链表中的任务
  返回 -1 链表没有内容
*/
int list_for_eachdo_and_freefile(struct list_head *do_head)
{
    //list使用的临时变量
    struct list_head *do_pos = NULL, *do_n = NULL;
    New_file *do_list_tmp = NULL; 
    New_file do_worker;

    int j ;
    
    //if (do_head->next == NULL) return -1;
    if (list_empty(do_head)) return -1;

    list_for_each_safe(do_pos, do_n, do_head)
    { 
        //取任务
        do_list_tmp = list_entry(do_pos, struct newfile, list);
        do_worker = *do_list_tmp; 

#if DISPLAY_DEBUG_SYSLOG_GROW
        syslog(LOG_ERR, "the oper file, inode=%d, process=%p, src_path_p=%s, src_path_size=%d,\
                              dst_path_p=%s, dst_path_size=%d!\n",\
                              do_list_tmp->inode, do_list_tmp->process, (do_list_tmp->job).src_path_p, (do_list_tmp->job).src_path_size,\
                              (do_list_tmp->job).dst_path_p, (do_list_tmp->job).dst_path_size);
#endif

        if (-1 == check_sf_flag((do_worker.job).flag))
        {
            syslog(LOG_ERR, "check flag is abnormal in list_for_eachdo_and_freefile!\n");
            sign_operchnglog_from_mapfile(do_worker.inode, SIGN_FAIL);

            list_del(do_pos);
            do_pos = NULL;

            free( (do_worker.job).src_path_p );
            free( (do_worker.job).dst_path_p );
            (do_worker.job).src_path_p = NULL;
            (do_worker.job).dst_path_p = NULL;

            free(do_list_tmp);
            do_list_tmp = NULL;

            continue;
        }

        if (-1 == check_operfile_process(do_worker.process))
        {
            syslog(LOG_ERR, "do_worker.process is abnormal!\n");
            if (SF_GROW == (do_worker.job).flag)
            {
                sign_operchnglog_from_mapfile(do_worker.inode, SIGN_FAIL);
            }

            list_del(do_pos);
            do_pos = NULL;

            free( (do_worker.job).src_path_p );
            free( (do_worker.job).dst_path_p );
            (do_worker.job).src_path_p = NULL;
            (do_worker.job).dst_path_p = NULL;

            free(do_list_tmp);
            do_list_tmp = NULL;

            continue;
        }    
        
        if (0 == strlen((do_worker.job).dst_path_p) || NULL == (do_worker.job).dst_path_p ||\
            0 == strlen((do_worker.job).src_path_p) || NULL == (do_worker.job).src_path_p) 
        {
            if (0 == strlen((do_worker.job).dst_path_p) || NULL == (do_worker.job).dst_path_p)
            {
                syslog(LOG_ERR, "(do_worker.job).dst_path_p is abnormal!\n");
            }
            if (0 == strlen((do_worker.job).src_path_p) || NULL == (do_worker.job).src_path_p) 
            {
                syslog(LOG_ERR, "(do_worker.job).src_path_p is abnormal!\n");
            }            

            if (SF_GROW == (do_worker.job).flag)
            {
                sign_operchnglog_from_mapfile(do_worker.inode, SIGN_FAIL);
            }
            list_del(do_pos);
            do_pos = NULL;

            free( (do_worker.job).src_path_p );
            free( (do_worker.job).dst_path_p );
            (do_worker.job).src_path_p = NULL;
            (do_worker.job).dst_path_p = NULL;

            free(do_list_tmp);
            do_list_tmp = NULL;

            continue;
        }

        j = (*(do_worker.process))( &(do_worker.job) );

        if (SF_GROW == (do_worker.job).flag)
        {
            if (0 == j) 
            {
                sign_operchnglog_from_mapfile(do_worker.inode, SIGN_SUCCESS);
            }
            else if (-1 == j)
            {
                sign_operchnglog_from_mapfile(do_worker.inode, SIGN_FAIL);

                char dst_path[4097] = {'\0'};
                memcpy(dst_path, (do_worker.job).dst_path_p, (do_worker.job).dst_path_size);
                syslog(LOG_ERR, "operatering %s file is error for growing!\n", dst_path);
            }
        }
        else if ( (SF_ALL == (do_worker.job).flag || SF_ALL_NOTIME == (do_worker.job).flag) && (-1 == j))
        {
            char dst_path[4097] = {'\0'};
            memcpy(dst_path, (do_worker.job).dst_path_p, (do_worker.job).dst_path_size);
            syslog(LOG_ERR, "operatering %s file is error for all!\n", dst_path);
        }

        list_del(do_pos);
        do_pos = NULL;

        free( (do_worker.job).src_path_p );
        free( (do_worker.job).dst_path_p );
        (do_worker.job).src_path_p = NULL;
        (do_worker.job).dst_path_p = NULL;

        free(do_list_tmp);
        do_list_tmp = NULL;

        continue;
    }

    return 0;
}

//工作线程函数
void * thread_routine(void *arg) 
{   
    //使工作线程成分离态
    pthread_detach( pthread_self() ); 
    
#if DISPLAY_DEBUG_SYSLOG_GROW
    printf("starting thread 0x%x\n", pthread_self());  
#endif
/*
    //list使用的临时变量
    struct list_head *pos = NULL, *n = NULL;
    New_file *list_tmp = NULL; 
*/
    struct list_head *addthread_pos = NULL,*addthread_n = NULL;
    struct addthread_queue_inode *addthread_list_tmp = NULL;
    struct addthread_queue_inode  addthread_inode;
    
    memset(&addthread_inode, 0, sizeof(addthread_inode));

    while (1)  
    {
        memset(&addthread_inode, 0, sizeof(addthread_inode));      

        pthread_mutex_lock (&addthread_lock);         
        //若任务链表中无任务，并且不销毁线程池，则处于阻塞状态; 注意pthread_cond_wait是一个原子操作，等待前会解锁，唤醒后会加锁
//syslog(LOG_INFO, "starting thread 0x%x,%d\n", pthread_self(),001); 
        if ( (list_empty(&addthread_queue_inode_head)) && !pool->shutdown )   // 无数据,且不销毁线程池
        {
//syslog(LOG_INFO, "starting thread 0x%x,%d\n", pthread_self(),002); 
            //把等待队列归0
            pool->cur_queue_size = 0;

            while ( (list_empty(&addthread_queue_inode_head)) && !pool->shutdown )
            {   
//syslog(LOG_INFO,"starting thread 0x%x,%d\n", pthread_self(),003); 
                if (IS_EXIT == pool->is_exit)   //因为IS_EXIT只有全量使用，所以下面if分支只对全量起作用
                {   
                    pool->cur_thread_num--;     
//syslog(LOG_INFO,"cur_thread_num is %d\n", pool->cur_thread_num);  
                    if (0 == pool->cur_thread_num)
                    {
                        pthread_cond_signal(&exit_ready);
                    }
                }

                pthread_mutex_unlock(&addthread_lock); 
                //发信号给任务添加线程
#if 0

                pthread_cond_broadcast(&for_wait_ready); 
#endif          
                //阻塞
                pthread_mutex_lock(&addthread_lock); 

syslog(LOG_INFO,"starting thread 0x%x,%d\n", pthread_self(),004); 
                pthread_cond_wait(&addthread_ready, &addthread_lock);  
syslog(LOG_INFO,"starting thread 0x%x,%d\n", pthread_self(),005); 
            } 
        }


        if (!pool->shutdown) 
        {
            list_for_each_safe(addthread_pos, addthread_n, &addthread_queue_inode_head)
            {   
//syslog(LOG_INFO,"starting thread 0x%x,%d\n", pthread_self(),006); 
                addthread_list_tmp = list_entry(addthread_pos, struct addthread_queue_inode, list);
                addthread_inode = *addthread_list_tmp;
                list_del(addthread_pos);
                addthread_pos = NULL;
                free(addthread_list_tmp);
                addthread_list_tmp = NULL;

                (pool->cur_queue_size)--;
  
                break; 
            }
        }

        if ((!pool->shutdown) && NULL != addthread_inode.queue_member_head && (!(list_empty( &(addthread_inode.queue_member_head) ))))  //也可以通过其他prev，queue_member_head的next/prev来判断
        {

//syslog(LOG_INFO,"thread 0x%x is starting to work!\n", pthread_self ()); 
                pthread_mutex_unlock(&addthread_lock); 

                if (-1 == list_for_eachdo_and_freefile(addthread_inode.queue_member_head))
                {
                    syslog(LOG_ERR, "the list is empty in list_for_eachdo_and_freefile!\n");
                }

                //释放头节点
                free(addthread_inode.queue_member_head);
                addthread_inode.queue_member_head = NULL;

                continue;
        }
        //是否是要回收部分工作线程
        else if ( (pool->shutdown) && (0 < pool->del_thread_num) )   //回收线程池,且删除数目不为零
        {
            pool->del_thread_num--;
            pool->cur_thread_num--;

            if (0 < pool->del_thread_num)
            {
            }
            else if (0 ==  pool->del_thread_num) 
            {
                pool->shutdown = 0; //关闭回收标志
            }
            else
            {
                syslog(LOG_ERR, "pool->del_thread_num is error!\n");
            }

            //遇到break,continue,return,exit,goto等跳转语句，千万不要忘记先解锁
            pthread_mutex_unlock (&addthread_lock); 
            pthread_exit (NULL); 
        } 
        //回收部分线程之后，关闭回收标志
        else if ( (pool->shutdown) && (0 == pool->del_thread_num) )   //回收线程池,且删除数目为零,则关闭回收标志
        {
            pool->shutdown = 0;
            pthread_mutex_unlock (&addthread_lock); 
            continue;
        }
        else 
        {        
            syslog(LOG_ERR, "runing worker thread is abnormal!\n");
            pthread_mutex_unlock (&addthread_lock);
            continue;
        }
        
    }
    //这一句应该是不可达的  
    pthread_exit (NULL);  
}  

//专门用来释放Table_To 内存，还有Table_To中table指向的字符内存
void free_Table_To(Table_To *tab)
{
    free(tab->table);
    tab->table = NULL;
    free(tab);
    tab = NULL;
}

//专门用来释放Table_From 内存，还有Table_From中p_t指向的字符内存
void free_Table_From(Table_From *tab)
{
    free(tab->p_t);
    tab->p_t = NULL;
    free(tab);
    tab = NULL;
}


int addthread_queue_init(struct list_head *head)
{
    INIT_LIST_HEAD(head);
    return 0;
}

int file_queue_init(struct list_head *head)
{
    INIT_LIST_HEAD(head);
    return 0;
}

int change_to_not_first()
{
    is_first = NO_FIRST; //标记之后即非第一次
    return 0;
}

int change_to_first()
{
    is_first = IS_FIRST; //标记为第一次
    return 0;
}

//初始化路径 互斥锁
int init_path_mutex()
{
    pthread_mutex_init(&(path_lock), NULL);
    return 0;
}

int open_syncfile_log()
{
    openlog("syncfile", LOG_CONS | LOG_PID, 0);
    return 0;
}


int check_firstsign()
{
    if ( !(IS_FIRST == is_first || NO_FIRST == is_first) ) return -1;
    return 0;    
}

//判断是否是第一次，若是则进行初始化，反之。
int copy_run_first_init()
{
#if DISPLAY_DEBUG_SYSLOG_GROW
    syslog(LOG_ERR, "entering copy_run_init!\n");
#endif
    if (-1 == check_firstsign())
    {
        syslog(LOG_ERR, "is_first is error in copy_run_first_init!\n");
        return -1;
    }

    if (IS_FIRST == is_first) //第一次初始化
    {
        //初始化路径 互斥锁
        if (-1 == init_path_mutex())
        {
            syslog(LOG_ERR, "init_path_mutex is error!\n");
            return -1;
        }
#if 0
        if (-1 == open_syncfile_log())
        {
            syslog(LOG_ERR, "open_syncfile_log is error!\n");
            return -1;
        }
#endif
        //初始化链表
        if (-1 == addthread_queue_init(&addthread_queue_inode_head))
        {
            syslog(LOG_ERR, "addthread_queue_init is error!\n");
            return -1;
        }

        if (-1 == file_queue_init(&file_queue_head))
        {
            syslog(LOG_ERR, "file_queue_init is error!\n");
            return -1;
        }

        //线程池初始化
        if (-1 == poolthread_init(INIT_THREAD_NUM, MAX_THREAD_NUM))
        {
            syslog(LOG_ERR, "poolthread_init is error!\n");
            return -1;
        }

        //标记之后为非第一次
        if (-1 == change_to_not_first())
        {
            syslog(LOG_ERR, "chang_to_not_first is error!\n");
            return -1;
        }
#if 0
        sleep( 3 ); //启动中
#endif

#if DISPLAY_DEBUG_SYSLOG_GROW
    syslog(LOG_ERR, "first initing is sucessed!\n");
#endif
        return 0;
    }
    return 0;
}

/*
  检查flag参数是否正确
*/
int check_sf_flag(sfflag_t flag)
{
    if ( !(SF_GROW == flag || SF_ALL == flag || SF_ALL_NOTIME == flag) )
    {
        return -1;
    }
    return 0;
}

//接口传进来的内存保存在结构体中
int saveto_table_from(Table_From * table_from_p, sfflag_t flag, Table_From_Grow table[], time_t st_timer, \
                         time_t ed_timer, Table_To *tmp_to, int max_num, pthread_t *add_pid,  \
                         const char *srcpath, const char *eqpath, const char *dstpath)
{
    //参数校验
    if (NULL == table_from_p || NULL == add_pid || NULL == tmp_to) 
    {
        syslog(LOG_ERR, "table_from_p ,or add_pid ,or tmp_to is abnormal in saveto_table_from!\n");
        return -1;
    }

    //flag校验
    if ( -1 == check_sf_flag(flag) ) 
    {
        syslog(LOG_ERR, "flag is abnormal in saveto_table_from!\n");
        return -1;
    }

    if (SF_GROW == flag)
    {
        //增量，具体参数校验
        if (NULL == table || max_num <= 0) 
        {
            syslog(LOG_ERR, "table ,or max_num is abnormal in saveto_table_from!\n");
            return -1;
        }
    }
    else if (SF_ALL == flag || SF_ALL_NOTIME == flag)
    {
        //全量调用有时间，具体参数校验
        if (SF_ALL == flag)
        {
            if ( st_timer <= 0 || ed_timer <= 0)
            {
                syslog(LOG_ERR, "st_timer , or ed_timer is abnormal in saveto_table_from!\n");
                return -1;
            }
        }
    }

    //校验path类参数
    if ( (NULL != srcpath) && ( '/' != *(srcpath + 0)))
    {
        syslog(LOG_ERR, "the srcpath is error in function saveto_table_from!\n");
    }
    if ( (NULL != eqpath) && ( '/' != *(eqpath + 0)))
    {
        syslog(LOG_ERR, "the eqpath is error in function saveto_table_from!\n");
    }
    if ( (NULL != dstpath) && ( '/' != *(dstpath + 0)))
    {
        syslog(LOG_ERR, "the dstpath is error in function saveto_table_from!\n");
    }

    //增量，全量的公共变量赋值
    table_from_p->flag = flag;
    table_from_p->table = table;
    table_from_p->st_timer = st_timer;
    table_from_p->ed_timer = ed_timer;
    table_from_p->table_to_p = tmp_to;

    table_from_p->max_num = max_num;
    table_from_p->p_t     = add_pid; 

    memset(table_from_p->srcpath, '\0', sizeof(table_from_p->srcpath));
    strcpy(table_from_p->srcpath, srcpath);

    memset(table_from_p->eqpath, '\0', sizeof(table_from_p->eqpath));
    strcpy(table_from_p->eqpath, eqpath);

    memset(table_from_p->dstpath, '\0', sizeof(table_from_p->dstpath));
    strcpy(table_from_p->dstpath, dstpath);
    
    return 0;    
}

//任务添加线程启动,成功返回0，失败返回-1
int start_add_thread(pthread_t  *add_pid, void *(*run)(void *), Table_From *from_tmp)
{
    if (NULL == add_pid || NULL == run || NULL == from_tmp) return -1;


    run((void *)from_tmp);

#if 0
    if( 0 != pthread_create(add_pid, NULL, run, (void *)from_tmp) )
    {
        syslog(LOG_ERR , "Create add working thread failed!\n");  
        pool_destroy();
        syslog(LOG_INFO , "the pool of threads id destroied, the process is will exited!\n");  
        return -1;
    }
#endif

    return 0;
}


Table_To * thread_return(Table_From *from_tmp, sfflag_t flag, pthread_t *p_t, Table_To *table_to_p)
{
    void **thread_ptr = NULL;

    if ( !(SF_GROW == flag || SF_ALL == flag || SF_ALL_NOTIME == flag) ) return NULL;  
    if (NULL == from_tmp || NULL == p_t || NULL == table_to_p) return NULL;
        
    if (SF_GROW == flag)
    {
        //增量
        table_to_p->flag = flag;
        table_to_p->size = 0;
        table_to_p->table = NULL;
#if 0
        //增量，等待任务添加线程结束
        if ( (pthread_join((*p_t), thread_ptr)) ) return NULL;
#endif
        return table_to_p;
    }
    else if (SF_ALL == flag || SF_ALL_NOTIME == flag)
    {
        //全量
        table_to_p->flag = flag;
        table_to_p->size = 0;
        table_to_p->table = NULL;
        if ( (pthread_join((*p_t), thread_ptr)) ) return NULL;
        return table_to_p;
    }
    return NULL;
}



/*
  检查路径
  放回 0  正确
  返回 -1 path为NULL
  返回 -2 path不为绝对路径
  返回 -3 path长度为0
  返回 -4 path长度大于4096
*/
int check_path(const char *path)
{
    int rtn = 0;
    if (NULL == path)
    {
        rtn = -1;
    }
    if ('/' != *path)
    {
        rtn = -2;
    }
    if (0 == strlen(path))
    {
        rtn = -3;
    }

    if (strlen(path) > 4096)
    {
        rtn -4;
    }    

    return rtn;
}

/*
  检查路径，打印log
  返回0   正确
  返回-1 检查发现错误
*/
int check_path_log(const char *path, const char *process)
{
    int chi = check_path(path);
    int rtn = 0;
    
    if (-1 == chi)
    {
        syslog(LOG_ERR, "the path is NULL in %s!\n", process);
        rtn = -1;
    }
    if (-2 == chi)
    {
        syslog(LOG_ERR, "the path is not absolute path in %s!\n", process);
        rtn = -1;
    }
    if (-3 == chi)
    {
        syslog(LOG_ERR, "the length of the path is zero in %s!\n", process);
        rtn = -1;
    }

    if (-4 == chi)
    {
        syslog(LOG_ERR, "the length of the path is so big in %s!\n", process);
        rtn = -1;
    }    

    return rtn;
}



/*
  外界调用接口
  flag: SF_ALL表示全量，SF_GROW表示增量
  table: 外界传进来的任务内容首地址,增量使用
  max_num: table数组中有效元素的个数，增量使用
  st_timer 开始时间，全量使用
  ed_timer 结束时间，全量使用
  srcpath 拷贝源路径，全量使用
  dstpath 拷贝目的目录，全量使用，该目录可用NULL，标识选择默认目录
  返回：传递Table_to给外界
*/
Table_To copy_run(sfflag_t flag, Table_From_Grow table[], int max_num, \
                                     time_t st_timer, time_t ed_timer, \
                     const char *srcpath, const char *eqpath, const char *dstpath)
{

#if DISPLAY_DEBUG_SYSLOG_GROW
    int count = 0;
    for (count = 0; count < max_num; count++)
    {
        syslog(LOG_ERR, "the entering is %d,type=%c,operationname=%s,    \
                filepath=%s, srcpath=%s, eqpath=%s, dstpath=%s!\n", count,\
                      (table[count]).type, (table[count]).operationname, \
                      (table[count]).filepath, srcpath, eqpath, dstpath);
    }
    syslog(LOG_ERR, "entering function is %s!\n", "copy_run");
#endif

    pthread_t  add_pid;
    Table_From *from_tmp = NULL;
    Table_To   tmp_to;
    Table_To   *rtn_to   = NULL;

    from_tmp = (Table_From *)malloc( sizeof(Table_From) );

    memset(from_tmp, '\0', sizeof(Table_From));

    //校验函数参数
    if (SF_GROW == flag)
    {
        //增量参数校验
        if (NULL == table) 
        {
            syslog(LOG_ERR, "the table of copy_run is abnormal!\n");
            tmp_to.flag = SF_GROW_TABLE_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }
        if (max_num <= 0)
        {
            syslog(LOG_ERR, "the max_num of copy_run is abnormal!\n");  
            tmp_to.flag = SF_GROW_MAXNUM_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }

        if (1 == strlen(srcpath) && '/' == *(srcpath + 0))
        {
            syslog(LOG_ERR, "srcpath is '/',error!\n");
            tmp_to.flag = SF_GROW_SRCPATH_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }
        if (-1 == check_path_log(srcpath, "copy_run")) 
        {
            syslog(LOG_ERR, "the srcpath of copy_run is abnormal!\n");
            tmp_to.flag = SF_GROW_SRCPATH_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }

        if (-1 == check_path_log(dstpath, "copy_run")) 
        {
            syslog(LOG_ERR, "the dstpath of copy_run is abnormal!\n");
            tmp_to.flag = SF_GROW_DSTPATH_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }

        
        if (1 == strlen(eqpath) && '/' == *(eqpath + 0))
        {
            syslog(LOG_ERR, "eqpath is '/', error!\n");
            tmp_to.flag = SF_GROW_EQPATH_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }
        if (-1 == check_path_log(eqpath, "copy_run")) 
        {
            syslog(LOG_ERR, "the eqpath of copy_run is abnormal!\n");
            tmp_to.flag = SF_GROW_EQPATH_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }
        

        
    }
    else if (SF_ALL == flag || SF_ALL_NOTIME == flag)
    {
        //全量有时间参数调用参数校验
        if (SF_ALL == flag)
        {
            if (st_timer < 0) 
            {
                syslog(LOG_ERR, "the st_timer of copy_run is abnormal!\n");
                tmp_to.flag = SF_ALL_STTIMER_ERR;
                tmp_to.size = 0;
                tmp_to.table = NULL;
                return tmp_to;
            }
            if (ed_timer < 0) 
            {
                syslog(LOG_ERR, "the ed_timer of copy_run is abnormal!\n");
                tmp_to.flag = SF_ALL_EDTIMER_ERR;
                tmp_to.size = 0;
                tmp_to.table = NULL;
                return tmp_to;
            }
        }
        //全量，不管是否有时间参数，校验srcpath,dstpath
        if (-1 == check_path_log(srcpath, "copy_run")) 
        {
            syslog(LOG_ERR, "the srcpath of copy_run is abnormal!\n");
            tmp_to.flag = SF_ALL_SRCPATH_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }
        if ( (NULL != dstpath) && ('/' != *(dstpath + 0)) )
        {
            syslog(LOG_ERR, "the dstpath of copy_run is abnormal!\n");
            tmp_to.flag = SF_ALL_DSTPATH_ERR;
            tmp_to.size = 0;
            tmp_to.table = NULL;
            return tmp_to;
        }
    }
    else 
    {
       syslog(LOG_ERR, "The flag of copy_run is abnormal!\n");
        tmp_to.flag = SF_FLAG_ERR;
        tmp_to.size = 0;
        tmp_to.table = NULL;
        return tmp_to;
    }

    //接口传进来的内存保存在结构体Table_From中
    if ( -1 == saveto_table_from(from_tmp, flag, table, st_timer, ed_timer, \
                            &tmp_to, max_num, &add_pid, srcpath, eqpath, dstpath) ) 
    {
        syslog(LOG_ERR, "using saveto_table_from is failed!\n");
        tmp_to.flag = SF_ENTER_PARAMETER_SAVE_ERR;
        tmp_to.size = 0;
        tmp_to.table = NULL;
        return tmp_to;
    }

    //初始化 链表，线程池
    if ( -1 == copy_run_first_init() ) 
    {
        syslog(LOG_ERR, "using copy_run_first_init is failed!\n");
        tmp_to.flag = SF_POOL_INIT_ERR;
        tmp_to.size = 0;
        tmp_to.table = NULL;
        return tmp_to;
    }

    //查看队列是否已满
    if (pool->cur_queue_size > MAX_QUEUE_NUM)
    {
        syslog(LOG_ERR, "The working queue is full!Please use copy_run for waiting a minute...\n");
        tmp_to.flag = SF_FULL;
        tmp_to.size = 0;
        tmp_to.table = NULL;
        return tmp_to;
    }

    //任务添加线程启动
    if ( -1 == start_add_thread(&add_pid, pool_add_worker, from_tmp) ) 
    {
        syslog(LOG_ERR, "using start_add_thread is failed!\n");
        tmp_to.flag = SF_FAIL;
        tmp_to.size = 0;
        tmp_to.table = NULL;
        return tmp_to;
    }

    //线程返回
    rtn_to = thread_return(from_tmp, from_tmp->flag, &add_pid, from_tmp->table_to_p); //from_tmp->table_to_p即&tmp_to，也即rtn_to
    if (NULL == rtn_to)
    {
        syslog(LOG_ERR, "using thread_return is failed!\n");
        tmp_to.flag = SF_FAIL;
        tmp_to.size = 0;
        tmp_to.table = NULL;
        return tmp_to;
    }

    free(from_tmp);
    from_tmp = NULL;

    return *rtn_to;  
}

//校验year
int check_year(int year)
{
    if (year <= 0)
    {
        return -1;
    }
    return 0;    
}

//校验mon
int check_mon(int mon)
{
    if ( !(mon <= 12 && mon >= 1) )
    {
        return -1;
    }
    return 0;
}

//校验day
int check_day(int day)
{
    if ( !(day <= 31 && day >=1) )
    {
        return -1;
    }
    return 0;
}

//校验hour
int check_hour(int hour)
{
    if ( !(hour <= 23 && hour >= 0) )
    {
        return -1;
    }
    return 0;
}

//校验min
int check_min(int min)
{
    if ( !(min <= 59 && min >= 0) )
    {
        return -1;
    }
    return 0;
}

//校验sec
int check_sec(int sec)
{
    if ( !(sec <= 59 && sec >=0) )
    {
        return -1;
    } 
    return 0;
}


/*
  把字符串形式的时间转换成time_t格式的时间
*/
int time_change(const char *start_time, const char *end_time, time_t *st_timer_p, time_t *ed_timer_p)
{
    struct tm s_time;
    struct tm e_time;

    //参数校验
    if (strlen(start_time) != 12 && strlen(start_time) != 14)
    {
        syslog(LOG_ERR, "the length of start_time is err!\n");
        return -1;
    }
    if (strlen(end_time) != 12 && strlen(end_time) != 14)
    {
        syslog(LOG_ERR, "the length of end_time is err!\n");
        return -1;
    }
    
    //开始时间
    char s_year_tmp[5];
    memset(s_year_tmp, '\0', sizeof(s_year_tmp));
    memcpy(s_year_tmp, start_time + 0, 4);

    char s_mon_tmp[3];
    memset(s_mon_tmp, '\0', sizeof(s_mon_tmp));
    memcpy(s_mon_tmp, start_time + 4, 2);

    char s_day_tmp[3];
    memset(s_day_tmp, '\0', sizeof(s_day_tmp));
    memcpy(s_day_tmp, start_time + 6, 2);
    
    char s_hour_tmp[3];
    memset(s_hour_tmp, '\0', sizeof(s_hour_tmp));
    memcpy(s_hour_tmp, start_time + 8, 2);
    
    char s_min_tmp[3];
    memset(s_min_tmp, '\0', sizeof(s_min_tmp));
    memcpy(s_min_tmp, start_time + 10, 2);
    
    char s_sec_tmp[3];
    if (14 == strlen(start_time))
    {
        memset(s_sec_tmp, '\0', sizeof(s_sec_tmp));
        memcpy(s_sec_tmp, start_time + 12, 2);
    }

    //结束时间    
    char e_year_tmp[5];
    memset(e_year_tmp, '\0', sizeof(e_year_tmp));
    memcpy(e_year_tmp, end_time + 0, 4);
    
    char e_mon_tmp[3];
    memset(e_mon_tmp, '\0', sizeof(e_mon_tmp));
    memcpy(e_mon_tmp, end_time + 4, 2);
    
    char e_day_tmp[3];
    memset(e_day_tmp, '\0', sizeof(e_day_tmp));
    memcpy(e_day_tmp, end_time + 6, 2);

    char e_hour_tmp[3];
    memset(e_hour_tmp, '\0', sizeof(e_hour_tmp));
    memcpy(e_hour_tmp, end_time + 8, 2);

    char e_min_tmp[3];
    memset(e_min_tmp, '\0', sizeof(e_min_tmp));
    memcpy(e_min_tmp, end_time + 10, 2);

    char e_sec_tmp[3];
    if (14 == strlen(end_time))
    {
    memset(e_sec_tmp, '\0', sizeof(e_sec_tmp));
    memcpy(e_sec_tmp, end_time + 12, 2);    
    }

    //开始时间,时间从字符串转变成整形，并时间校验
    int s_year = atoi(s_year_tmp); 
    if (-1 == check_year(s_year)) 
    {
       syslog(LOG_ERR, "the year of starting is err!");
       return -1;
    }

    int s_mon  = atoi(s_mon_tmp); 
    if (-1 == check_mon(s_mon)) 
    {
        syslog(LOG_ERR, "the mon of starting is err!");
        return -1;
    }
    
    int s_day  = atoi(s_day_tmp); 
    if (-1 == check_day(s_day)) 
    {
        syslog(LOG_ERR, "the day of starting is err!");
        return -1;
    }
    
    int s_hour = atoi(s_hour_tmp); 
    if (-1 == check_hour(s_hour)) 
    {
        syslog(LOG_ERR, "the hour of starting is err!");
        return -1;
    }
    
    int s_min  = atoi(s_min_tmp); 
    if (-1 == check_min(s_min)) 
    {
        syslog(LOG_ERR, "the min of starting is err!");
        return -1;
    }        
    
    int s_sec;
    if (14 == strlen(start_time))
    {
        s_sec  = atoi(s_sec_tmp); 
    }
    else if (12 == strlen(start_time))
    {
        s_sec = 0;
    }
    if (-1 == check_sec(s_sec)) 
    {
        syslog(LOG_ERR, "the sec of starting is err!");
        return -1;
    }
    

    //结束时间,时间从字符串转变成整形，并时间校验
    int e_year = atoi(e_year_tmp); 
    if (-1 == check_year(e_year)) 
    {
        syslog(LOG_ERR, "the year of ending is err!");
        return -1;
    }
    
    int e_mon  = atoi(e_mon_tmp); 
    if (-1 == check_mon(e_mon)) 
    {
        syslog(LOG_ERR, "the mon of ending is err!");
        return -1;
    }
    
    int e_day  = atoi(e_day_tmp); 
    if (-1 == check_day(e_day)) 
    {
       syslog(LOG_ERR, "the day of ending is err!");
       return -1;
    }
    
    int e_hour = atoi(e_hour_tmp); 
    if (-1 == check_hour(e_hour)) 
    {
        syslog(LOG_ERR, "the hour of ending is err!");
        return -1;
    }
    
    int e_min  = atoi(e_min_tmp); 
    if (-1 == check_min(e_min)) 
    {
       syslog(LOG_ERR, "the min of ending is err!");
       return -1;
    }
    
    int e_sec;
    if (14 == strlen(end_time))
    {
        e_sec  = atoi(e_sec_tmp); 
    }
    else if(12 == strlen(end_time))
    {
        s_sec = 0;
    }
    if (-1 == check_sec(e_sec)) 
    {
        syslog(LOG_ERR, "the sec of ending is err!");
        return -1;
    }
    
    //开始时间，放入struct tm结构体
    s_time.tm_sec  = s_sec;
    s_time.tm_min  = s_min;
    s_time.tm_hour = s_hour;
    s_time.tm_mday = s_day;
    s_time.tm_mon  = s_mon -1;
    s_time.tm_year = s_year - 1900;
    
    //结束时间，放入struct tm结构体
    e_time.tm_sec  = e_sec;
    e_time.tm_min  = e_min;
    e_time.tm_hour = e_hour;
    e_time.tm_mday = e_day;
    e_time.tm_mon  = e_mon -1;
    e_time.tm_year = e_year - 1900;
    
    //把struct tm形式的时间转换成 time_t格式
    *st_timer_p = mktime(&s_time);
    *ed_timer_p = mktime(&e_time);
    
    return 0;
}
 

 
/*
说明：该命令支持相对路径，绝对路径

  主函数入口，把全量做成命令的形式
  -s srcpath 必选项，后面要跟参数
  -d dstpath 必选项，后面要跟参数
  -q eq_path 必选项，后面要跟参数，说明同步目录的结构
  -f startime -t时，必选项，后面跟字符串格式的时间，比如201410102123(32)括号中为可选的内容。
  -e endtime  -t时，必选项，后面跟字符串格式的时间，比如201410102123(32)括号中为可选的内容。
  -n 必须使用参数，无时间参数调用方式，后面不跟参数
  -t 必须使用参数，有时间参数调用方式，后面不跟参数
  上述 -n -t参数中必须选择一个参数
  
返回值：
  -1 输入类型错误，n,t只能有一个
  -2 不能不选择类型
  -3 全量，有时间调用，时间参数不能为空
  -4 源路径参数必须有
  -5 目的路径参数必须有
  -6 源路径不存在
  -7 说明同步目录结构的参数必须有
  -8 源路径、目的目录不能相等
  -9 初始化源路径失败
  -10 初始化目的路径失败
  -11 初始化等于目录失败
  -12 相等目录必须有
  -13 相等路径不存在
  -14 源目录不能小于相等路径
  -15 时间有问题
*/
#if IS_ALL_CMD
int copy_cmd(int argc, char **argv)  
{  
    //选项字符
    int oc;  
    
    //开始时间，结束时间，time_t结构
    time_t st_t;
    time_t ed_t;    

    //源路径
    char srcpath[MAX_PATH_BUF];
    memset(srcpath, '\0' ,sizeof(srcpath));

    //目的路径
    char dstpath[MAX_PATH_BUF];
    memset(dstpath, '\0' ,sizeof(dstpath));
    
    //说明同步目录的结构
    char eqpath[MAX_PATH_BUF];
    memset(eqpath, '\0' ,sizeof(eqpath));

    //开始时间，格式如 201410101230(45)，括号内内容为秒，为可选
    char start_time[15];
    memset(start_time, '\0', sizeof(start_time));
    
    //结束时间，格式如开始时间
    char end_time[15];
    memset(end_time, '\0', sizeof(end_time));

    //调用类型
    sfflag_t flag_notime = 0,flag_time = 0, flag;

    //解析命令内容
    while( (oc = getopt(argc, argv, "ntf:e:s:d:q:")) != -1 )
    {
        switch(oc)
        {
            case 'n':
                flag_notime = SF_ALL_NOTIME;
                break;
            case 't':
                flag_time = SF_ALL;
                break;
            case 'f':
                strcat(start_time, optarg);
                break;
            case 'e':
                strcat(end_time, optarg);
                break;
            case 's':
                strcat(srcpath, optarg);
                break;
            case 'd':
                strcat(dstpath, optarg);
                break;
            case 'q':
                strcat(eqpath, optarg);
                break;
        }
    }

    //输入类型错误，n,t只能有一个
    if (flag_time != 0 && flag_notime !=0)  
    {
       syslog(LOG_ERR, "-t or -n, all using is error!\n");
       return -1;
    }

    //不能不选择类型
    if (flag_time == 0 && flag_notime == 0) 
    {
        syslog(LOG_ERR, "-t or -n, there is must one!\n");
        return -2;
    }
    
    //类型参数赋值
    if (flag_time != 0) flag = flag_time;
    else if (flag_notime != 0) flag= flag_notime;

    if (SF_ALL == flag)
    {
        //全量，有时间调用，时间参数不能为空
        if ( 0 == strlen(start_time) || 0 == strlen(end_time) ) 
        {
            syslog(LOG_ERR, "start_time or end_time is err in main!\n");
            return -3;
        }

        if (-1 == time_change(start_time, end_time, &st_t, &ed_t))
        {
            syslog(LOG_ERR, "changing time is error!\n");
            return -15; //时间有问题
        }
    }
    else if(SF_ALL_NOTIME == flag)
    {
        st_t = 0;
        ed_t = 0;
    }    

    //源路径参数必须有
    if (0 == strlen(srcpath)) 
    {
        syslog(LOG_ERR, "there is must srcpath!\n");
        return -4;
    }

    //目的路径参数必须有
    if (0 == strlen(dstpath))
    {
        syslog(LOG_ERR, "there is must srcpath!\n");
        return -5; 
    }

    //相等路径参数必须有
    if (0 == strlen(eqpath))
    {
        syslog(LOG_ERR, "there is must eqpath");
        return -12;
    }

    //说明同步目录结构的参数必须有
    if (0 == strlen(eqpath))
    {
        syslog(LOG_ERR, "there is must eqpath\n");
        return -7;
    }

    //若是相对路径，转换成绝对路径
    char real_srcpath[MAX_PATH_BUF] = {'\0'};
    char real_dstpath[MAX_PATH_BUF] = {'\0'};
    char real_eqpath[MAX_PATH_BUF]  = {'\0'};
    if ('/' != *(srcpath + 0))
    {
        realpath(srcpath, real_srcpath);
    }
    else
    {
        strcpy(real_srcpath, srcpath);
    }
    
    if ('/' != *(dstpath + 0))
    {
        realpath(dstpath, real_dstpath);
    }
    else
    {
        strcpy(real_dstpath, dstpath);
    }
    
    if ('/' != *(eqpath + 0))
    {
        realpath(eqpath, real_eqpath);
    }
    else
    {
        strcpy(real_eqpath, eqpath);
    }

    if( NULL == judge_path_del(real_srcpath) ) return -9;
    if( NULL == judge_path_del(real_dstpath) ) return -10;
    if( NULL == judge_path_del(real_eqpath) ) return -11;
    
    //源路径、目的路径不能一样
    if (0 == strcmp(real_srcpath, real_dstpath))
    {
        syslog(LOG_ERR, "srcpath not equal dstpath!");
        return -8;    
    }


    //源路径是否存在
    if ( -1 == access(real_srcpath, F_OK) ) 
    {
        syslog(LOG_ERR, "there is not %s path!\n", real_srcpath);
        return -6;
    }
    
    //相等路径是否存在
    if ( -1 == access(real_eqpath, F_OK) ) 
    {
        syslog(LOG_ERR, "there is not %s path!\n", real_eqpath);
        return -13;
    }

    //源目录不能小于相等路径
    if ( strlen(real_eqpath) > strlen(real_srcpath) )
    {
        syslog(LOG_ERR, "eqpath > srcpath is error!\n");
        return -14;
    }

    copy_run(flag, NULL, 0, st_t, ed_t, real_srcpath, real_eqpath, real_dstpath);

    return 0;
}
#endif 


#if TEST_POOLFILE_HAS_MAIN
int main(int argc, char *argv[])
{

    copy_cmd(argc, argv);
    exit(0);

}
#endif


