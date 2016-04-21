#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <memory.h>
#include<unistd.h>

int main(int argc, char* argv[])
{
	FILE * fp=NULL;
	if(argc<4)
	{
		printf("arguments number incorrect:\nargv[1]:path, argv[2]:minByte, argv[3]:maxByte");
	}
	else
	{
		//argv[1]:path
		//argv[2]:minByte
		//argv[3]:maxByte
		//
		
		char* path = argv[1];
		srand(getpid());
		fp= fopen(path,"w+");
		int minbyte = atoi(argv[2]);
		int maxbyte = atoi(argv[3]);
		printf("path:%s, minbyte:%d, maxbyte:%d\n",path, minbyte, maxbyte);
		if(minbyte>=maxbyte)
		{
			printf("arguments incorrect - minbyte>=maxbyte");
		}
		long writeCount = rand()+1;
		long writeBytes=((double)rand() / (RAND_MAX))*(maxbyte-minbyte)+minbyte;
		
		printf("writeByte:%d, writecount:%d\n", writeBytes, writeCount);
		char * str = (char*)malloc(sizeof(char)*writeBytes);
		memset(str,0,writeBytes);
		fwrite(str,writeBytes,1,fp);
		fclose(fp);
		free(str);
	}

	return 0;
}
