#ifndef _SF_COMMON_H_ 
#define _SF_COMMON_H_ 

#include <sys/types.h>
#include <stdint.h> 


/*
 sfflag_t                      文件同步模块，标识增量或者全量的类型
 SF_FULL                       队列已满，请稍后在调用接口
 SF_FAIL                       失败
 SF_GROW                       增量调用
 SF_ALL                        全量调用
 SF_ALL_NOTIME                 全量调用无时间参数
 SF_ALL_SRCPATH_ERR            全量调用srcpath参数错误
 SF_ALL_DSTPATH_ERR            全量调用dstpath参数错误
 SF_FLAG_ERR                   flag参数错误
 SF_GROW_TABLE_ERR             增量调用时，table为NULL，导致的错误
 SF_GROW_MAXNUM_ERR            增量调用时，max_num小于等于0，导致的错误
 SF_GROW_SRCPATH_ERR           增量调用时，srcpath错误
 SF_GROW_DSTPATH_ERR           增量调用时，dstpath错误
 SF_GROW_EQPATH_ERR            增量调用时，eqpath错误
 SF_ALL_STTIMER_ERR            全量调用时，st_timer小于0，导致的错误
 SF_ALL_EDTIMER_ERR            全量调用时，ed_timer小于0，导致的错误
 SF_ENTER_PARAMETER_SAVE_ERR   copy_run接口调用，时候保存入参数失败
 SF_POOL_INIT_ERR              线程池初始化失败
*/
typedef char sfflag_t;
#define SF_ALL                 '0' 
#define SF_ALL_NOTIME          '5'
#define SF_GROW                '1' 
#define SF_FULL                '3'
#define SF_FAIL                '4'
#define SF_ALL_SRCPATH_ERR     '2'
#define SF_FLAG_ERR            '6'
#define SF_GROW_TABLE_ERR      '7'
#define SF_GROW_MAXNUM_ERR     '8'
#define SF_ALL_STTIMER_ERR     '9'
#define SF_ALL_EDTIMER_ERR     'a'
#define SF_ENTER_PARAMETER_SAVE_ERR 'b'
#define SF_POOL_INIT_ERR       'c'
#define SF_ALL_DSTPATH_ERR     'd'
#define SF_GROW_SRCPATH_ERR    'f'
#define SF_GROW_DSTPATH_ERR    'g'
#define SF_GROW_EQPATH_ERR     'h'


/*
 sign_t                       成功与否标记类型
 SIGN_FAIL                    成功                     
 SIGN_SUCCESS                 失败
 SIGN_PARAMETER_TYPE_ERR      Table_From_Grow成员type错误
 SIGN_PARAMETER_OPERNAME_ERR  Table_From_Grow成员operationname错误
 SIGN_PARAMETER_FILEPATH_ERR  Table_From_Grow成员filepath非绝对路径
*/
typedef int sign_t;
#define SIGN_FAIL                     0
#define SIGN_SUCCESS                  1
#define SIGN_PARAMETER_TYPE_ERR       2
#define SIGN_PARAMETER_OPERNAME_ERR   3
#define SIGN_PARAMETER_FILEPATH_ERR   4



#define MAX_PATH_SIZE               (4096)
/*
#define MAX_PATH_SIZE               (31)
*/
#define MAX_PATH_BUF                ( MAX_PATH_SIZE + 1)


#define MAX_OPER_LENGTH             (16)

/*
  CONFIG_IS_HIDE         Config结构式以及函数是否要
  TEST_SYSLOG_INFO_USER  打印syslog信息，测试使用
  DUG_SIGN_USER sign_operchnglog_from_mapfile，编译通过办法，测试使用

  TEST_ALL_DY_THREADNUM  屏蔽全量动态增加减少线程数目 0屏蔽,用于测试   1正常功能
  TEST_GROW_DY_THREADNUM 屏蔽增量动态增加减少线程数目 0屏蔽,用于测试   1正常功能
  IS_ALL_CMD              是否把全量的制作成命令
  TEST_POOLFILE_HAS_MAIN  pool.c文件里面是否定义测试使用main函数
  DEBUG_PRINTF_SYSLOG_TEST 自测时候，使用是否使用syslog/printf打印一些提示信息

*/

#define CONFIG_IS_HIDE                       0
#define TEST_SYSLOG_INFO_USER                1
#define DUG_SIGN_USER                        0

#define TEST_ALL_DY_THREADNUM                1
#define TEST_GROW_DY_THREADNUM               1
#define IS_ALL_CMD                           1
#define TEST_POOLFILE_HAS_MAIN               0
#define DEBUG_PRINTF_SYSLOG_TEST             1



/*  define for grow  */

#define DISPLAY_DEBUG_SYSLOG_GROW 0


typedef struct operationtable {
    uint32_t inode;
    uint32_t parentinode;
    
    uint8_t type;  //'d' or 'f'
    uint8_t flag;  // oper  time < 5;
    
    uint32_t lasttime;

    uint8_t operationname[MAX_OPER_LENGTH]; 
    uint8_t filepath[MAX_PATH_SIZE];
    
    struct operationtable *next;
    //and so on
} operationtable,Table_From_Grow;


#endif
