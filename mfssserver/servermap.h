
/*
Author :  WeiNing
Date   :  2014-11-26
Descripted:   file map for file system .
 */


#ifndef  _SYNC_SERVER_MP_H_
#define  _SYNC_SERVER_MP_H_



#define MAP_FILE_NAME  "./operationFromChg"
int  syncserver_open_mmap();
int  syncserver_close_mmap();
int sign_operchnglog_from_mapfile(int inode, int result);
void masterconn_changelog_del_syncserve();
#endif
