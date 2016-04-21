/*************************************************
  Copyright (C), 2014, Nari.
  File name:     work.h
  Author:        mawnexiang      
  Version:       v1.0       
  Date:          2014-10
  Description:  本头文件声明的函数，均为实现文件同
      步业务的功能，实现为线程池工作线程提供接口服务。
 
 // 用于详细说明此程序文件完成的主要功能，与其他模块
 // 或函数的接口，输出值、取值范围、含义及参数间的控
 // 制、顺序、独立或依赖等关系
*************************************************/

#ifndef _WORK_H_ 
#define _WORK_H_ 

/*
  MAX_PATH_LENGTH     最大路径长度
  GOOD_BUF_SIZE       文件读取，写入最优buf大小
  GOOD_READ_BUF_SIZE  文件读取最优buf大小
  GOOD_WRITE_BUF_SIZE 文件写入最优buf大小
*/
#define MAX_PATH_LENGTH 4096

#define GOOD_BUF_SIZE        4096
#define GOOD_READ_BUF_SIZE   (GOOD_BUF_SIZE) 
#define GOOD_WRITE_BUF_SIZE  (GOOD_BUF_SIZE) 


#define MAX_PATH       4096
#define MAX_STR_PATH   (MAX_PATH + 1)
/*
#define MAX_PATH       31
#define MAX_STR_PATH   (MAX_PATH + 1)
*/


typedef struct task
{
    sfflag_t flag;                   //SF_ALL全量，SF_GROW增量
	/*
//准备去掉的内容-------
    char src_path[MAX_STR_PATH];
    char eq_path[MAX_STR_PATH];
    char dst_path[MAX_STR_PATH];
//---------------------
    */
    char *src_path_p;
    int src_path_size;        //src_path_p内存块的大小

    char *dst_path_p;   
    int dst_path_size;        //dst_path_p内存块的大小 
} CTask;



#if CONFIG_IS_HIDE
typedef struct config {
    char from_path[MAX_STR_PATH];
    char to_path[MAX_STR_PATH];
} Config;
#endif

#if CONFIG_IS_HIDE
Config conf_grow;
Config conf_all;
#endif

pthread_mutex_t path_lock;

size_t get_char_num(const char *path, char ch);

#if 0
int load_config(char *path);
#endif

unsigned long long get_file_size(const char *path);
char * judge_path(char *path);
char * replace_path(char *outpath, char *inpath, char *eqpath, char *dstpath, char flag);
char *top_dir(const char *inpath, char *outpath);

    

int create_dirs(const char *path);
int continue_file(int readfd, int writefd, const char * readpath, const char * writepath);

//判断路径最后一个字符是否为'/'，没有加上
char * judge_path_add(char *path);

//判断路径最后一个字符是否为'/'，若有去掉
char * judge_path_del(char *path);

/*
 typedef int (*file_oper_function_t)(CTask *job)  文件操作函数定义
*/
typedef int (*file_oper_function_t)(CTask *job);



/*
  检查文件操作接口是否正确
  typedef int (*file_oper_function_t)(CTask *job) ;
  返回0 正确
  返回-1 不正确
*/
int check_operfile_process(file_oper_function_t func);


/*
 以下接口，除Create_Dir_N/Create_File_N外，其他待后期升级开放
*/
int Create_Dir_S(CTask * arg);
int Create_Dir_N(CTask * arg);
int Create_File_S(CTask * arg);
int Create_File_L(CTask * arg);
int Create_File_N(CTask * arg);
int Del_Dir(CTask * arg);
int Del_File(CTask * arg);
int Alter_File(CTask * arg);
int Move_Dir(CTask *arg);
int Move_File(CTask *arg);

#endif

