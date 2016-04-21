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

#include "mfssyncfile.h"

#include "common.h"
#include "pool.h"
#include "work.h"
#include "libfile.h"



/*
  制作命令 mfssyncfile
*/
int main(int argc , char **argv)
{
   copy_cmd(argc, argv);

   //要分类各种退出调用exit()
   exit(0);
}


