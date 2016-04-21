/*************************************************
  Copyright (C), 2014, Nari.
  File name:    libfile.h
  Author:       mawnexiang      
  Version:      v1.0       
  Date:         2014-10
  Description: 
*************************************************/

#ifndef _LIBFILE_H_
#define _LIBFILE_H_

char filetmp[MAX_PATH_BUF];

typedef struct Get_File {

    struct list_head list;
    char filename[MAX_PATH_BUF];
    char sign;

} Get_File_t;

typedef struct filetime {
    int year;
    int mon;
    int day;
    int hour;
    int min;
    int sec;
} Filetime;

char * init_path(char *inpath,char *outpath);
int trave_dir(char * src_path, struct list_head *head, struct tm *firsttim, struct tm *endtim);
struct list_head * get_allfile(const char *path, Filetime starttimer, Filetime endtimer);

#endif

