
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
#include "mountRaid1.h"
#include "../utilz/utilz.h"

#define CHUNK_SZ 4096
#define COMMAND_SZ 4
#define N_SERVERS 3
#define LOG_MAXSZ 128


int* allSFD;
char** argvv;
struct cache_st* cache;

/** Read data from an open file using open(), pread() and close() **/
static int r1_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi);

/** Write data to an open file  using open(), pwrite() and close() **/
static int r1_write(const char* path, const char* buf, 
	size_t size, off_t offset, struct fuse_file_info* fi);

/** Get file attributes. **/
static int r1_getattr(const char *path, struct stat *stbuf);

//-------------------------<<--<<--<<--IS-REG-->>-->>-->>-----------------------------------
/** File create operation using open() **/
static int r1_create(const char* path, mode_t mode, struct fuse_file_info * fi)
{
	int serv;
	for(serv = N_SERVERS - 1; serv > 0; serv--){
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
static int r1_open(const char *path, struct fuse_file_info *fi)
{
	char* xattr1 = malloc(82);
	char* xattr2 = malloc(82);

	int serv;
	for(serv = N_SERVERS - 1; serv > 0; serv--){
		sendCommandPathChunk("1opn", path, (void*)&(fi->flags), sizeof(fi->flags), allSFD[serv], CHUNK_SZ, COMMAND_SZ);

		if(serv == 1){
			read(allSFD[serv], xattr1, 82);
		} else {
			read(allSFD[serv], xattr2, 82);
		}
		int res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

		if(res >= 0){
			fi->fh = res;
		} else {
			free(xattr1);
			free(xattr2);
			return -1; 
		}
	}

	if(strncmp(xattr1, xattr1 + 41, 41) != 0 || strncmp(xattr1, xattr2, 41) != 0){
		// allSFD[1] has problem and should be restored from allSFD[2]
		writeOnLog("data was damaged, starting restore operation", argvv, LOG_MAXSZ);
		// ------------------read from 2
		printf("problem on 1\n");
		struct stat* stbuf = malloc(sizeof(struct stat)); //
		char* newPath = malloc(strlen(path) + 1);
		strncpy(newPath, path, strlen(path) + 1);
		strncpy(newPath, "2", 1);

		if(r1_getattr(newPath, stbuf) < 0){
			perror("getting attribute from server 2 failed\n");
		} 
		char* buf = malloc(CHUNK_SZ);
		int res = 0;
		size_t bytesRead = 0;
		size_t bytesSend = 0;
		while(bytesRead < stbuf->st_size && bytesSend < stbuf->st_size){
			strncpy(newPath, "2", 1);
			res = r1_read(newPath, buf, CHUNK_SZ, bytesRead, fi);
			if(res < 0){
				perror("getting info from file from server 2 failed\n");
			} else {
				bytesRead += (size_t)res;
			}

			// ------------------write to 1
			strncpy(newPath, "1", 1);
			size_t sendSz = CHUNK_SZ;
			if(CHUNK_SZ > stbuf->st_size - bytesSend) sendSz = stbuf->st_size - bytesSend;
			res = r1_write(newPath, buf, sendSz, bytesSend, fi);
			if(res < 0){
				perror("writing info to file from server 1 failed\n");
			} else {
				bytesSend += (size_t)res;
			}
		}
		free(stbuf);
		free(newPath);
		free(buf);
	} else if(strncmp(xattr2, xattr2 + 41, 41) != 0){
		// allSFD[2] has problem and should be restored from allSFD[1]
		writeOnLog("data was damaged, starting restore operation", argvv, LOG_MAXSZ);
		printf("problem on 2\n");
		// ------------------read from 1
		struct stat* stbuf = malloc(sizeof(struct stat)); //
		char* newPath = malloc(strlen(path) + 1);
		strncpy(newPath, path, strlen(path) + 1);
		strncpy(newPath, "1", 1);

		if(r1_getattr(newPath, stbuf) < 0){
			perror("getting attribute from server 1 failed\n");
		} 
		char* buf = malloc(CHUNK_SZ);
		int res = 0;
		size_t bytesRead = 0;
		size_t bytesSend = 0;
		while(bytesRead < stbuf->st_size && bytesSend < stbuf->st_size){
			strncpy(newPath, "1", 1);
			res = r1_read(newPath, buf, CHUNK_SZ, bytesRead, fi);
			if(res < 0){
				perror("getting info from file from server 1 failed\n");
			} else {
				bytesRead += (size_t)res;
			}

			// ------------------write to 2
			strncpy(newPath, "2", 1);
			size_t sendSz = CHUNK_SZ;
			if(CHUNK_SZ > stbuf->st_size - bytesSend) sendSz = stbuf->st_size - bytesSend;
			res = r1_write(newPath, buf, sendSz, bytesSend, fi);
			if(res < 0){
				perror("writing info to file from server 2 failed\n");
			} else {
				bytesSend += (size_t)res;
			}
		}
		free(stbuf);
		free(newPath);
		free(buf);
	}

	free(xattr1);
	free(xattr2);

	return 0;
}

//int cnt = 0;
//int szcnt = 0;

/** Read data from an open file using open(), pread() and close() **/
static int r1_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
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
		int serv = 1;
		if(path[0] == '1'){
			serv = 1;
		} else if(path[0] == '2'){
			serv = 2;
		}

		int chunkCnt = 0;
		char* chunk = malloc(CHUNK_SZ);
		memcpy(chunk + chunkCnt, &(fi->fh), sizeof(int));
		chunkCnt += sizeof(int);
		int realSZ = CHUNK_SZ;
		memcpy(chunk + chunkCnt, &realSZ, sizeof(size_t));
		chunkCnt += sizeof(size_t);
		memcpy(chunk + chunkCnt, &offset, sizeof(off_t));
		chunkCnt += sizeof(off_t);

		size_t chunk_szzz = CHUNK_SZ;
		memcpy(chunk + chunkCnt, &chunk_szzz, sizeof(size_t));
		chunkCnt += sizeof(size_t);

		// PATH | FD | SIZE | OFFSET
		sendCommandPathChunk("1rea", path, chunk, chunkCnt, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

		printf("offset: %d res: %d\n", (int)offset, res);
		if(res >= 0){
			int bytesReceived = 0;
			int bytesToReceive = res;
			while(bytesReceived < bytesToReceive){
				if(bytesToReceive - bytesReceived < CHUNK_SZ){
					read(allSFD[serv], buf + bytesReceived, bytesToReceive - bytesReceived);
					bytesReceived += bytesToReceive - bytesReceived;
				} else {
					read(allSFD[serv], buf + bytesReceived, CHUNK_SZ);
					bytesReceived += CHUNK_SZ;
				}
			}
		}

		if(res == -1){
			res = -errno;
		}

		if(res > 0) addInCache(path, (size_t)res, offset, buf, cache);

		printf("cache: %d\n", (int)cache->logLen);

		return res;
	}
	return res;
}


int writeHelper(const char* path, const char* buf, 
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

	size_t chunk_szzz = CHUNK_SZ;
	memcpy(chunkToSend + chunkCnt, &chunk_szzz, sizeof(size_t));
	chunkCnt += sizeof(size_t);

	sendCommandPathChunk("1wrt", path, chunkToSend, chunkCnt, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
	free(chunkToSend);

	size_t bytesToSend = size;
	size_t bytesSend = 0;
	while(bytesSend < bytesToSend){
		if(bytesToSend - bytesSend < CHUNK_SZ){
			write(allSFD[serv], buf + bytesSend, bytesToSend - bytesSend);
			bytesSend += bytesToSend - bytesSend;
		} else {
			write(allSFD[serv], buf + bytesSend, CHUNK_SZ);
			bytesSend += CHUNK_SZ;
		}
	}

	res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

	if(res == -1){
		return -errno;
	}
	return res;
}


/** Write data to an open file  using open(), pwrite() and close() **/
static int r1_write(const char* path, const char* buf, 
	size_t size, off_t offset, struct fuse_file_info* fi)
{
	int res;
	int serv;

	if(path[0] == '1'){
		res = writeHelper(path, buf, size, offset, fi, 1);
	} else if(path[0] == '2'){
		res = writeHelper(path, buf, size, offset, fi, 2);
	} else {
		for(serv = N_SERVERS - 1; serv > 0; serv --){
			res = writeHelper(path, buf, size, offset, fi, serv);
			printf("offset: %d\n", (int)offset);
			if(res == -1){
				return -errno;
			}
		}
	}
	if(res == -1){
		return -errno;
	}

	return res;
}


/** File release operation using close() **/
static int r1_release(const char * path, struct fuse_file_info * fi)
{
	int serv;
	for(serv = N_SERVERS -1; serv > 0; serv--){
		sendCommandPathChunk("1rel", path, (void*)&(fi->fh), sizeof(uint64_t), 
			allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);
	}
	return 0;
}


/** File unlink operation using unlink() **/
static int r1_unlink(const char* path)
{
	int serv;
	int res;
	for(serv = N_SERVERS -1; serv > 0; serv--){
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
static int r1_mkdir(const char * path, mode_t mode)
{
	int serv;
	int res;
	for(serv = 1; serv < N_SERVERS; serv++){
		
		sendCommandPathChunk("1mkd", path, &mode, sizeof(mode_t), allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

		if(res < 0){
			return -errno;
		}
	}

	return 0;
}


/** Open directory **/
static int r1_opendir(const char * path, struct fuse_file_info * fi)
{
	sendCommandPathChunk("1odr", path, NULL, 0, allSFD[1], CHUNK_SZ, COMMAND_SZ);

	return 0;
}


/** Read directory **/
static int r1_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
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
static int r1_releasedir(const char * path, struct fuse_file_info * fi)
{
	int res;
	int serv = 1;
	sendCommandPathChunk("1rld", path, NULL, 0, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
	res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

	if(res < 0){
		return -1;
	}

	return 0;
}


/** Remove a directory */
static int r1_rmdir(const char* path)
{
	int serv;
	int res;
	for(serv = 1; serv < N_SERVERS; serv++){
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
static int r1_getattr(const char *path, struct stat *stbuf)
{
	int serv = 1;
	if(path[0] == '2'){
		serv = 2;
	}

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
	free(buf);

	// if error occurred
	if(res < 0){
		errno = err;
		return -errno;
	}

	return 0;
}


static int r1_rename(const char *oldPath, const char *newPath)
{
	int serv;
	int res;
	for(serv = 1; serv < N_SERVERS; serv++){
	    sendCommandPathChunk("1rnm", oldPath, (void*)newPath, strlen(newPath) + 1, allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);

		if(res < 0){
			return -1;
		}
	}
	return 0;
}


static int r1_chmod(const char *path, mode_t mode)
{
	int serv;
	int res;
	for(serv = 1; serv < N_SERVERS; serv++){
		sendCommandPathChunk("1chm", path, &mode, sizeof(size_t), allSFD[serv], CHUNK_SZ, COMMAND_SZ);
		res = handleRetVal(allSFD[serv], CHUNK_SZ, LOG_MAXSZ, argvv);
		if (res == -1){
			return -errno;
		}
	}
	return 0;
}


static struct fuse_operations my_oper1 = {
	.create = r1_create,
	.open = r1_open,
	.read = r1_read,
	.write = r1_write,
	.release = r1_release,
	.unlink = r1_unlink,
	.mkdir = r1_mkdir,
	.opendir = r1_opendir,
	.readdir = r1_readdir,
	.releasedir = r1_releasedir,
	.rmdir = r1_rmdir,
	.getattr = r1_getattr,
	.rename = r1_rename,
	.chmod = r1_chmod,
};


//--------------------<<--<<--<<--MAIN-->>-->>-->>--------------------
int mainRunRaid1(char** argv)
{
	/** init some globals **/
	allSFD = malloc(N_SERVERS*sizeof(int));
	argvv = argv;

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
