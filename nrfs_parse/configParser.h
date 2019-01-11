
#ifndef CPARSER_H
#define CPARSER_H

#include <stdio.h>
#include <stdlib.h> //malloc
#include <string.h> //strdup

#define VAL_SZ 64


typedef struct {
	char* errorlog;
	char* cache_size;
	char* cache_replacment;
	int timeout;
} basicInfo_t;


typedef struct {
	char* ip;
	int port;
} myAddress_t;


typedef struct {
	int logLen;
	myAddress_t* servers;
} servList_t;


typedef struct {
	char* diskname;
	char* mountpoint;
	int raid;
	myAddress_t* hotswap;
	servList_t* servList;
} disk_t;


typedef struct {
	int logLen;
	disk_t* disks;
} diskList_t;

int parseConfig(basicInfo_t* basicInfo, diskList_t* dList, char* configPath);


#endif