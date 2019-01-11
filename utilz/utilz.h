#ifndef UTILZ_H
#define UTILZ_H

#include "../nrfs_parse/configParser.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h> //malloc
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <linux/limits.h>

typedef struct cache_t{
	char* name;
	size_t size;
	off_t offset;
	void* chunk;
	time_t lastUse;
	struct cache_t* next;
} cache_t;

typedef struct cache_st{
	size_t logLen;
	size_t maxLen;
	struct cache_t* cacheStart;
} cache_st;

char* getCurTime();

int writeOnLog(char* str, char** mArgv, size_t max_log_sz);

int deleteLRUNode(struct cache_st* cache);
int addInCache(const char* path, size_t size, off_t offset, char* buf, cache_st* cache);
int checkInCache(const char* path, size_t size, off_t offset, 
    struct cache_t* curNode, char* buf, int n, cache_st* cache);

int deletePath(struct cache_st* cache, const char* path);
int handleRetVal(int cfd, int chunkSz, int log_max_sz, char** argvv);
int sendCommandPathChunk(char* command, const char* path,
	void* toSend, int sendSize, int cfd, int chunkSz, int command_sz);

int connectToServer(char* rip, int port, int nth, int* allSFD, int log_max_sz, char** argvv);
int clientConnect(disk_t* myDisk, int* allSFD, int log_max_sz, char** argvv);

#endif