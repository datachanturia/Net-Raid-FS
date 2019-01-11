
#define FUSE_USE_VERSION 26

#include <fuse.h>
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

#include "../nrfs_parse/configParser.h"
#include "mountRaid5.h"
#include "../utilz/utilz.h"

#define CHUNK_SZ 4096
#define COMMAND_SZ 4
#define LOG_MAXSZ 128


int* allSFD;
char** argvv;
struct cache_st* cache;
int n_servers;
int chunkSz5;

/** Read data from an open file using open(), pread() and close() **/
static int r5_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi);

/** Write data to an open file  using open(), pwrite() and close() **/
static int r5_write(const char* path, const char* buf, 
	size_t size, off_t offset, struct fuse_file_info* fi);

/** Get file attributes. **/
static int r5_getattr(const char *path, struct stat *stbuf);

//-------------------------<<--<<--<<--IS-REG-->>-->>-->>-----------------------------------
/** File create operation using open() **/
static int r5_create(const char* path, mode_t mode, struct fuse_file_info * fi)
{
	int serv;
	for(serv = n_servers - 1; serv > 0; serv--){
		char* toSend = malloc(sizeof(mode_t) + sizeof(fi->flags));
		memcpy(toSend, &mode, sizeof(mode_t));
		memcpy(toSend + sizeof(mode_t), &(fi->flags), sizeof(fi->flags));

		sendCommandPathChunk("1cre", path, toSend, sizeof(mode_t) + sizeof(fi->flags), allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		free(toSend);

		int res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);
		if(res >= 0){
			fi->fh = res;
		} else {
			return -1;
		}
	}
	return 0;
}


/** File open operation using open() **/
static int r5_open(const char *path, struct fuse_file_info *fi)
{
	int serv;
	for(serv = n_servers - 1; serv > 0; serv--){
		sendCommandPathChunk("5opn", path, (void*)&(fi->flags), sizeof(fi->flags), allSFD[serv], CHUNK_SZ, COMMAND_SZ);

		int res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

		if(res >= 0){
			fi->fh = res;
		} else {
			return -1; 
		}
	}

	return 0;
}


/** Read data from an open file using open(), pread() and close() **/
static int r5_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	int resSum = 0;
	int res = 0;
	struct cache_t* curNode = cache->cacheStart;
	int rz = checkInCache(path, size, offset, curNode, buf, 0, cache);
	//printf("cnt: %d sz: %d\n", cnt, szcnt + rz);
	if(rz > 0){
		//cnt += 1;
		//szcnt += rz;
		//printf("%.*s", rz, buf);
		return rz;
	} else {
		int xorServ = (n_servers - 1) - ((offset)/((n_servers - 2)*chunkSz5))%(n_servers - 1);
		int serv;
		//-------------------------------------------------------
		int i;
		int cnt = 0;
		for(i = 0; i < n_servers - 1; i++){
			serv = (serv + 1)%(n_servers - 1);
			if(serv == 0) serv = n_servers - 1;

			if(serv != xorServ){
				int chunkCnt = 0;
				char* chunk = malloc(chunkSz5);
				memcpy(chunk + chunkCnt, &(fi->fh), sizeof(int));
				chunkCnt += sizeof(int);
				int realSZ = chunkSz5;
				memcpy(chunk + chunkCnt, &realSZ, sizeof(size_t));
				chunkCnt += sizeof(size_t);
				memcpy(chunk + chunkCnt, &offset, sizeof(off_t));
				chunkCnt += sizeof(off_t);

				size_t chunk_szzz = chunkSz5;
				memcpy(chunk + chunkCnt, &chunk_szzz, sizeof(size_t));
				chunkCnt += sizeof(size_t);

				// PATH | FD | SIZE | OFFSET
				sendCommandPathChunk("1rea", path, chunk, chunkCnt, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
				res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);
				printf("res: %d\n", res);

				printf("offset: %d res: %d\n", (int)offset, res);
				if(res >= 0){
					int bytesReceived = 0;
					int bytesToReceive = res;
					while(bytesReceived < bytesToReceive){
						if(bytesToReceive - bytesReceived < chunkSz5){
							read(allSFD[serv], buf + bytesReceived + cnt*chunkSz5, bytesToReceive - bytesReceived);
							bytesReceived += bytesToReceive - bytesReceived;
						} else {
							read(allSFD[serv], buf + bytesReceived + cnt*chunkSz5, chunkSz5);
							bytesReceived += chunkSz5;
						}
					}
				}

				if(res == -1){
					res = -errno;
					return res;
				}
				resSum += res;
				cnt += 1;
			}
		}
		//-------------------------------------------------------

		if(res > 0) addInCache(path, (size_t)res, offset, buf, cache);

		printf("cache: %d\n", (int)cache->logLen);
	}

	return (n_servers - 2)*chunkSz5;
}

int writeHelper5(const char* path, const char* buf, 
	size_t size, off_t offset, struct fuse_file_info* fi, int serv)
{
	int res;

	int chunkCnt = 0;
	char* chunkToSend = malloc(CHUNK_SZ);
	memcpy(chunkToSend + chunkCnt, &(fi->fh), sizeof(int));
	chunkCnt += sizeof(int);
	memcpy(chunkToSend + chunkCnt, &size, sizeof(size_t));
	chunkCnt += sizeof(size_t);
	memcpy(chunkToSend + chunkCnt, &offset, sizeof(off_t));
	chunkCnt += sizeof(off_t);

	size_t chunk_szzz = (size_t)chunkSz5;
	memcpy(chunkToSend + chunkCnt, &chunk_szzz, sizeof(size_t));
	chunkCnt += sizeof(size_t);

	sendCommandPathChunk("1wrt", path, chunkToSend, chunkCnt, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
	free(chunkToSend);

	size_t bytesToSend = size;
	size_t bytesSend = 0;
	while(bytesSend < bytesToSend){
		if(bytesToSend - bytesSend < chunkSz5){
			write(allSFD[serv], buf + bytesSend, bytesToSend - bytesSend);
			bytesSend += bytesToSend - bytesSend;
		} else {
			write(allSFD[serv], buf + bytesSend, chunkSz5);
			bytesSend += chunkSz5;
		}
	}

	res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

	if(res == -1){
		return -errno;
	}
	return res;
}


/** Write data to an open file  using open(), pwrite() and close() **/
static int r5_write(const char* path, const char* buf, 
	size_t size, off_t offset, struct fuse_file_info* fi)
{
	size_t realSize = size;

	char* myBuf = malloc((n_servers-2)*chunkSz5);
	memset(myBuf, 0, (n_servers-2)*chunkSz5);
	if(size >= (n_servers-2)*chunkSz5){
		size = (n_servers-2)*chunkSz5;
		realSize = size;
		memcpy(myBuf, buf, size);
	} else {
		memcpy(myBuf, buf, size);
		size = (n_servers-2)*chunkSz5;
	}

	int res;
	int resSum = 0;

	int xorServ = (n_servers - 1) - ((offset)/((n_servers - 2)*chunkSz5))%(n_servers - 1);
	char* xorBuf = malloc(chunkSz5);
	memset(xorBuf, 0, chunkSz5);
	int j;
	for(j = 0; j < n_servers - 2; j++){
		int k;
		for(k = 0; k < chunkSz5; k++){
			xorBuf[k] ^= myBuf[(j*chunkSz5)+ k];
		}
	}

	int serv = xorServ;
	int i;
	int nCn = 0;
	for(i = 0; i < n_servers - 1; i++){
		serv = (serv + 1)%(n_servers - 1);
		if(serv == 0) serv = n_servers - 1;

		if(serv == xorServ){
			res = writeHelper5(path, xorBuf, chunkSz5, offset, fi, serv);
			printf("offset: %d\n", (int)offset);
		} else {
			res = writeHelper5(path, myBuf + i*chunkSz5, chunkSz5, offset, fi, serv);
			printf("offset: %d\n", (int)offset);
		}

		if(res == -1){
			free(xorBuf);
			free(myBuf);
			return -errno;
		}
		resSum += res;
		if(serv != xorServ) nCn++;
	}

	free(xorBuf);
	free(myBuf);

	if(resSum >= realSize) return realSize;
	return resSum;
}


/** File release operation using close() **/
static int r5_release(const char * path, struct fuse_file_info * fi)
{
	int serv;
	for(serv = n_servers -1; serv > 0; serv--){
		sendCommandPathChunk("1rel", path, (void*)&(fi->fh), sizeof(uint64_t), 
			allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);
	}
	return 0;
}


/** File unlink operation using unlink() **/
static int r5_unlink(const char* path)
{
	int serv;
	int res;
	for(serv = n_servers -1; serv > 0; serv--){
		sendCommandPathChunk("1unl", path, NULL, 0, allSFD[serv], CHUNK_SZ, COMMAND_SZ);

		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);
		if(res < 0){
			return res;
		}
	}

	deletePath(cache, path);

	return res;
}


//-------------------------<<--<<--<<--IS-DIR-->>-->>-->>-----------------------------------
/** Create a directory **/
static int r5_mkdir(const char * path, mode_t mode)
{
	int serv;
	int res;
	for(serv = 1; serv < n_servers; serv++){
		
		sendCommandPathChunk("1mkd", path, &mode, sizeof(mode_t), allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

		if(res < 0){
			return -errno;
		}
	}

	return 0;
}


/** Open directory **/
static int r5_opendir(const char * path, struct fuse_file_info * fi)
{
	sendCommandPathChunk("1odr", path, NULL, 0, allSFD[1], CHUNK_SZ, COMMAND_SZ);

	return 0;
}


/** Read directory **/
static int r5_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	sendCommandPathChunk("1rdr", path, NULL, 0, allSFD[1], CHUNK_SZ, COMMAND_SZ);

    char* rbuf = malloc(CHUNK_SZ);
    while(1){
    	read (allSFD[1], rbuf, CHUNK_SZ);
    	if(rbuf[CHUNK_SZ - 1] == 'e') break;
    	filler(buf, rbuf, NULL, 0);
    }
    free(rbuf);
	
	return 0;
}


/** Release directory **/
static int r5_releasedir(const char * path, struct fuse_file_info * fi)
{
	int res;
	sendCommandPathChunk("1rld", path, NULL, 0, allSFD[1], CHUNK_SZ, COMMAND_SZ);
	res = handleRetVal(allSFD[1], CHUNK_SZ, LOG_MAXSZ, argvv);

	if(res < 0){
		return -1;
	}

	return 0;
}


/** Remove a directory */
static int r5_rmdir(const char* path)
{
	int serv;
	int res;
	for(serv = 1; serv < n_servers; serv++){
		sendCommandPathChunk("1rmd", path, NULL, 0, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);
		if(res < 0){
			return -1;
		}
	}
	return 0;
}


//---------------------<<--<<--<<--GETATTR-RENAME-CHMOD-->>-->>-->>-------------------------------
/** Get file attributes. **/
static int r5_getattr(const char *path, struct stat *stbuf)
{
	int serv = 1;
	sendCommandPathChunk("1gat", path, NULL, 0, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
	
	//-------------------------------------------------------
	char* buf = malloc(CHUNK_SZ);
	read (allSFD[serv], buf, CHUNK_SZ);

	int bufCnt = 0;

	int res = *(int*)(buf + bufCnt);
	bufCnt += sizeof(int);

	int err = *(int*)(buf + bufCnt);
	bufCnt += sizeof(int);

	memset(stbuf, 0, sizeof(struct stat));
	memcpy(stbuf, buf + bufCnt, sizeof(struct stat));
	stbuf->st_size *= n_servers - 2;
	stbuf->st_blocks *= n_servers - 2;
	free(buf);

	// if error occurred
	if(res < 0){
		errno = err;
		return -errno;
	}

	return 0;
}


static int r5_rename(const char *oldPath, const char *newPath)
{
	int serv;
	int res;
	for(serv = 1; serv < n_servers; serv++){
	    sendCommandPathChunk("1rnm", oldPath, (void*)newPath, strlen(newPath) + 1, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

		if(res < 0){
			return -1;
		}
	}
	return 0;
}


static int r5_chmod(const char *path, mode_t mode)
{
	int serv;
	int res;
	for(serv = 1; serv < n_servers; serv++){
		sendCommandPathChunk("1chm", path, &mode, sizeof(size_t), allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);
		if (res == -1){
			return -errno;
		}
	}
	return 0;
}


static struct fuse_operations my_oper1 = {
	.create = r5_create,
	.open = r5_open,
	.read = r5_read,
	.write = r5_write,
	.release = r5_release,
	.unlink = r5_unlink,
	.mkdir = r5_mkdir,
	.opendir = r5_opendir,
	.readdir = r5_readdir,
	.releasedir = r5_releasedir,
	.rmdir = r5_rmdir,
	.getattr = r5_getattr,
	.rename = r5_rename,
	.chmod = r5_chmod,
};


//--------------------<<--<<--<<--MAIN-->>-->>-->>--------------------
int mainRunRaid5(char** argv)
{
	/** init some globals **/
	n_servers = 1 + ((disk_t*)argv[3])->servList->logLen;
	allSFD = malloc(n_servers*sizeof(int));
	argvv = argv;
	chunkSz5 = CHUNK_SZ/(n_servers - 2);

	//TODO free cache
	char* cacheRSize = malloc(strlen(((basicInfo_t*)argv[4])->cache_size));
	memcpy(cacheRSize, ((basicInfo_t*)argv[4])->cache_size, strlen(((basicInfo_t*)argv[4])->cache_size));
	memcpy(cacheRSize + strlen(((basicInfo_t*)argv[4])->cache_size) - 1, "\0", 1);
	cache = malloc(sizeof(struct cache_st));
	cache->logLen = 0;
	cache->maxLen = 1000000*((int) strtol(cacheRSize, (char **)NULL, 10));
	cache->cacheStart = NULL;
	free(cacheRSize);

	clientConnect((disk_t*)argv[3], allSFD, LOG_MAXSZ, argvv);
	int res = fuse_main(3, argv, &my_oper1, NULL);

	if(res >= 0){
		writeOnLog("successfully mounted", argvv, LOG_MAXSZ);
	} else {
		writeOnLog("mount operation failed", argvv, LOG_MAXSZ);
	}

	return res;
}
