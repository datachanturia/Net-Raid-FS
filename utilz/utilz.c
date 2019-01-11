
#include "utilz.h"

//-----------------<<--<<--<<--CUR TIME--START-->>-->>-->>---------------------------
char* getCurTime()
{
	time_t current_time;
    char* c_time_string;
	/* Obtain current time. */
    current_time = time(NULL);

    if (current_time == ((time_t)-1))
    {
        perror("Failure to obtain the current time.\n");
        return NULL;
    }

    /* Convert to local time format. */
    c_time_string = ctime(&current_time);
    memcpy(c_time_string + strlen(c_time_string) - 1, "\0", 1);
    return c_time_string;
}
//-----------------<<--<<--<<--CUR TIME--END-->>-->>-->>---------------------------


//-----------------<<--<<--<<--LOGGING--START-->>-->>-->>---------------------------
int writeOnLog(char* str, char** mArgv, size_t log_max_sz)
{
	char* logStr = malloc(log_max_sz);
	memset(logStr, 0, log_max_sz);
	logStr = strcat(logStr, "[");
	logStr = strcat(logStr, getCurTime());
	logStr = strcat(logStr, "] ");
	logStr = strcat(logStr, ((disk_t*)mArgv[3])->diskname);
	logStr = strcat(logStr, " ");
	logStr = strcat(logStr, str);
	logStr = strcat(logStr, " ");
	logStr = strcat(logStr, "\n");

	FILE *fp = fopen(((basicInfo_t*)mArgv[4])->errorlog, "ab");
    if (fp != NULL)
    {
        fputs(logStr, fp);
        fclose(fp);
    }
	free(logStr);
	return 0;
}
//-----------------<<--<<--<<--LOGGING--END-->>-->>-->>---------------------------


//-------------------------<<--<<--<<--CACHE--START-->>-->>-->>-----------------------------------
int deletePath(struct cache_st* cache, const char* path){
    struct cache_t* curNode = cache->cacheStart;
    struct cache_t* prev = NULL;

    while(1){
        if(curNode == NULL){
            break;
        }

        if(strcmp(path, curNode->name) == 0){
            if(prev == NULL){
                cache->cacheStart = curNode->next;
                cache->logLen -= strlen(curNode->name) + 1 + curNode->size;
            } else {
                prev->next = curNode->next;
                cache->logLen -= strlen(curNode->name) + 1 + curNode->size;
            }
            
            free(curNode->name);
            free(curNode->chunk);
            free(curNode);
        }

        curNode = curNode->next;
        if(prev == NULL){
            prev = cache->cacheStart;
        } else {
            prev = prev->next;
        }
    }
    return 0;
}


int deleteLRUNode(struct cache_st* cache)
{
    struct cache_t* curNode = cache->cacheStart;
    struct cache_t* prev = NULL;

    struct cache_t* leastNode = cache->cacheStart;
    struct cache_t* prevNode = NULL;

    while(1){
        if(curNode == NULL){
            break;
        }

        if(difftime(leastNode->lastUse, curNode->lastUse) > 0){
            leastNode = curNode;
            prevNode = prev;
        }

        curNode = curNode->next;
        if(prev == NULL){
            prev = cache->cacheStart;
        } else {
            prev = prev->next;
        }
    }
    size_t byteCounter = 0;
    byteCounter += sizeof(struct cache_t);
    byteCounter += strlen(leastNode->name) + 1;
    byteCounter += leastNode->size;

    free(leastNode->name);
    free(leastNode->chunk);
    if(prevNode == NULL){
        cache->cacheStart = leastNode->next;
    } else {
        prevNode->next = leastNode->next;
    }
    free(leastNode);

    cache->logLen -= byteCounter;

    return 0;
}

int addInCache(const char* path, size_t size, off_t offset, char* buf, cache_st* cache)
{
    printf("adding in cache---\n");
    struct cache_t* curNode = cache->cacheStart;
    while(1){
        if(curNode == NULL){
            //genesis node
            size_t byteCounter = 0;

            struct cache_t* newNode = malloc(sizeof(struct cache_t));
            byteCounter += sizeof(struct cache_t);
            newNode->next = NULL;
            newNode->name = strdup(path);
            byteCounter += strlen(path) + 1;
            newNode->lastUse = time(NULL);
            newNode->offset = offset;
            if(size > cache->maxLen - cache->logLen - byteCounter){
                newNode->chunk = malloc(cache->maxLen - cache->logLen - byteCounter);
                memcpy(newNode->chunk, buf, cache->maxLen - cache->logLen - byteCounter);
                newNode->size = cache->maxLen - cache->logLen - byteCounter;
                byteCounter = cache->maxLen - cache->logLen;
                cache->logLen = cache->maxLen;
            } else {
                newNode->chunk = malloc(size);
                memcpy(newNode->chunk, buf, size);
                newNode->size = size;
                byteCounter += size;
                cache->logLen += byteCounter;
            }

            cache->cacheStart = newNode;
            return 0;
        }

        if(strcmp(curNode->name, (char*)path) == 0 && curNode->offset == offset){
            return 0; //nothing to add
        }

        if(curNode->next == NULL){
            //add node
            while(cache->logLen + size > cache->maxLen){
                if(cache->logLen <= 0){
                    break;
                }
                deleteLRUNode(cache);
            }

            size_t byteCounter = 0;
            struct cache_t* newNode = malloc(sizeof(struct cache_t));
            byteCounter += sizeof(struct cache_t);
            newNode->next = NULL;
            newNode->name = strdup(path);
            byteCounter += strlen(path) + 1;
            newNode->lastUse = time(NULL);
            newNode->offset = offset;
            if(size > cache->maxLen - cache->logLen - byteCounter){
                newNode->chunk = malloc(cache->maxLen - cache->logLen - byteCounter);
                memcpy(newNode->chunk, buf, cache->maxLen - cache->logLen - byteCounter);
                newNode->size = cache->maxLen - cache->logLen - byteCounter;
                byteCounter = cache->maxLen - cache->logLen;
                cache->logLen = cache->maxLen;

                cache->cacheStart = newNode;
            } else {
                newNode->chunk =  malloc(size);
                memcpy(newNode->chunk, buf, size);
                newNode->size = size;
                byteCounter += size;
                cache->logLen += byteCounter;

                curNode->next = newNode;
            }

            return 0;
        }

        curNode = curNode->next;
    }

    return 0;
}

// checks path, file in cache if found writes it to buf, else -1 returned
int checkInCache(const char* path, size_t size, off_t offset, struct cache_t* curNode, char* buf, int n, cache_st* cache)
{
    while(1){
        if(curNode == NULL){
            return -1;
        }

        if(strcmp(curNode->name, (char*)(path)) == 0){
            break;
        }

        if(curNode->next == NULL){
            return -1;
        }
        curNode = curNode->next;
    }

    if(curNode != NULL){
        if(offset >= curNode->offset + curNode->size || offset < curNode->offset){ 
            return checkInCache(path, size, offset, curNode->next, buf, 1, cache);
        } else {
            if(size > curNode->size - (offset - curNode->offset)){
                memcpy(buf, (char*)(curNode->chunk) + (offset - curNode->offset), 
                    curNode->size - (offset - curNode->offset));
                return (int)(curNode->size - (offset - curNode->offset));
            } else {
                memcpy(buf, (char*)(curNode->chunk) + (offset - curNode->offset), size);
                return (int)size;
            }
        }
    }
    return -1;
}
//-------------------------<<--<<--<<--CACHE--END-->>-->>-->>-----------------------------------


//-----------------<<--<<--<<--Send-&-Receive--Package--START-->>-->>-->>---------------------------
int handleRetVal(int cfd, int chunkSz, int log_max_sz, char** argvv)
{
    char* buffer = malloc(chunkSz);

    read(cfd, buffer, chunkSz);
    int res = *(int*)buffer;

    if(res < 0){
        errno = *(int*)(buffer + sizeof(int));
        char* error = strdup("error: ");
        writeOnLog(strcat(error, strerror(errno)), argvv, log_max_sz);
        free(error);
    }

    free(buffer);

    return res;
}


int sendCommandPathChunk(char* command, const char* path, void* toSend, 
    int sendSize, int cfd, int chunkSz, int command_sz)
{
    char* readString = malloc(chunkSz);

    strncpy(readString, command, command_sz);

    printf("\n\n%s %s - %s\n", "r1", command, path);
    //countLine1 += 1;

    strncpy(readString + command_sz, path, strlen(path) + 1);
    memcpy(readString + command_sz + strlen(path) + 1, toSend, sendSize);

    //append '1' to end of every chunk
    strncpy(readString + command_sz + sendSize + strlen(path) + 1, "1", 1);

    if(command_sz + strlen(path) + 1 + sendSize + 1 < chunkSz){
        int i;
        for(i = 0; i < chunkSz - (strlen(command) + 1 + sendSize + strlen(path) + 1); i++){
            // append '0'-s 
            strncpy(readString + strlen(command) + 1 + sendSize + strlen(path) + 1 + i, "0", 1);
        }
    }

    write(cfd, readString, chunkSz);

    free(readString);

    return 0;
}
//-----------------<<--<<--<<--Send-&-Receive--Package--END-->>-->>-->>---------------------------


//--------------------<<--<<--<<--CONNECT--TO--SERVER--START-->>-->>-->>--------------------
int connectToServer(char* rip, int port, int nth, int* allSFD, int log_max_sz, char** argvv)
{
    struct sockaddr_in addr;
    int ip;
    allSFD[nth] = socket(AF_INET, SOCK_STREAM, 0);

    inet_pton(AF_INET, rip, &ip);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip;

    connect(allSFD[nth], (struct sockaddr *) &addr, sizeof(struct sockaddr_in));

    writeOnLog(" open connection", argvv, log_max_sz);

    return 0;
}


int clientConnect(disk_t* myDisk, int* allSFD, int log_max_sz, char** argvv)
{
    int i;
    for(i = 0; i < myDisk->servList->logLen; i++){
        connectToServer(myDisk->servList->servers[i].ip, myDisk->servList->servers[i].port, 
            i + 1, allSFD, log_max_sz, argvv);
    }

    return 0;
}
//--------------------<<--<<--<<--CONNECT--TO--SERVER--END-->>-->>-->>--------------------
