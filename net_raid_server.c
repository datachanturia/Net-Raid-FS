#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h> //strncpy
#include <dirent.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include "utilz/sha1.h"

#define BACKLOG 10

#define CHUNK_SZ 4096
#define COMMAND_SZ 4

DIR* dr;


int getFileHash(const char* path, char* myhexres)
{
    int btcheck = -1;
    int fd = open(path, 0);
    if(fd < 0) return -1;

    char* result = malloc(21);
    char* hexresult = malloc(41);
    memset(hexresult, 0, 41);

    size_t offset;
    SHA1_CTX ctx;

    SHA1Init(&ctx);

    int bytes;
    char* buf = malloc(CHUNK_SZ);
    while((bytes = read(fd, buf, CHUNK_SZ)) != 0){
        btcheck = 0;
        SHA1Update(&ctx, buf, bytes);
    }
    SHA1Final(result, &ctx);
    close(fd);
    free(buf);

    for(offset = 0; offset < 20; offset++) {
        sprintf((hexresult + (2*offset)), "%02x", result[offset]&0xff);
    }
    free(result);
    if(btcheck == -1) memset(hexresult, 0, 41);

    memcpy(myhexres, hexresult, 41);
    free(hexresult);
    return 0;
}


/** sets SHA1 hash of file as xattr **/
int setXhash(const char* path)
{
    int btcheck = -1;
    int fd = open(path, 0);
    if(fd < 0) return -1;

    char* result = malloc(21);
    char* hexresult = malloc(41);

    size_t offset;
    SHA1_CTX ctx;

    SHA1Init(&ctx);

    int bytes;
    char* buf = malloc(CHUNK_SZ);
    while((bytes = read(fd, buf, CHUNK_SZ)) != 0){
        btcheck = 0;
        SHA1Update(&ctx, buf, bytes);
    }
    SHA1Final(result, &ctx);
    close(fd);
    free(buf);

    for(offset = 0; offset < 20; offset++) {
        sprintf((hexresult + (2*offset)), "%02x", result[offset]&0xff);
    }
    strncpy(hexresult + 40, "\0", 1);

    if(btcheck == -1) memset(hexresult, 0, 41); //file was empty so hash is 0
    int setRes = setxattr(path, "user.name", hexresult, 41, 0);
    free(hexresult);
    free(result);
    if(setRes != 0){
        printf("%s\n", strerror(errno));
        return -1;
    }

    return 0;
}


void sendRetvalErrChunk(int cfd, int retVal, int size, void* chunk)
{
    int err = errno;
    int rVal = retVal;
    int sze = size;
    int bufCnt = 0;

    printf("res: %d\n", rVal);

    char* myBuf = malloc(CHUNK_SZ);
    memcpy(myBuf + bufCnt, &rVal, sizeof(int));
    bufCnt += sizeof(int);
    memcpy(myBuf + bufCnt, &err, sizeof(int));
    bufCnt += sizeof(int);
    memcpy(myBuf + bufCnt, &sze, sizeof(int));
    bufCnt += sizeof(int);
    memcpy(myBuf + bufCnt, chunk, size);

    write(cfd, myBuf, CHUNK_SZ);
    free(myBuf);
}


//------------------------<<--<<--<<-R1->>-->>-->>------------------------
void r1_getattr(char* pth, int cfd, struct stat* stbuf)
{
    int res = lstat(pth, stbuf);
    int err = errno;

    int bufCnt = 0;
    char* wbuf = malloc(CHUNK_SZ);

    memcpy(wbuf + bufCnt, &(res), sizeof(int));
    bufCnt += sizeof(int);

    memcpy(wbuf + bufCnt, &(err), sizeof(int));
    bufCnt += sizeof(int);

    if(S_ISDIR(stbuf->st_mode)){
        stbuf->st_mode |= 0775;
    } else if(S_ISREG(stbuf->st_mode)){
        stbuf->st_mode |= 0664;
    }

    memcpy(wbuf + bufCnt, stbuf, sizeof(struct stat));

    write(cfd, wbuf, CHUNK_SZ);

    free(wbuf);
}


int r1_readdir(char* pth, int cfd, DIR* dirnt)
{
    struct dirent *dir;
    char* wbuf = malloc(CHUNK_SZ);

    int oneSz = NAME_MAX + 1;
    while (1) {
        //printf("%s\n", dir->d_name);
        if((dir = readdir(dirnt)) == NULL){
            strncpy(wbuf + CHUNK_SZ - 1, "e", 1);
            write(cfd, wbuf, CHUNK_SZ);
            break;
        }
        strncpy(wbuf, dir->d_name, oneSz);
        write(cfd, wbuf, CHUNK_SZ);
    }

    free(wbuf);

    return 0;
}


int handleR1(char* buf, int cfd, char* storageName)
{
    int i;
    for(i = CHUNK_SZ; i > 0; i--){
        if(buf[i] == '1'){
            strncpy(buf + i, "\0", 1);
            break;
        }
    }

    printf("\n%s\n", buf);

    char* rInfo = malloc(CHUNK_SZ - COMMAND_SZ);
    memcpy(rInfo, buf + COMMAND_SZ, CHUNK_SZ - COMMAND_SZ);

    char* pth = malloc(NAME_MAX + 1);
    strncpy(pth, storageName, strlen(storageName));
    strncpy(pth + strlen(storageName), rInfo, strlen(rInfo) + 1);
    strncpy(pth + strlen(storageName), "/", 1);
    strncpy(pth + strlen(storageName) + strlen(rInfo), "\0", 1);

    printf("%s\n", pth);

    if(strncmp(buf + 1, "cre", 3) == 0){
        sendRetvalErrChunk(cfd, open(pth, *(int*)(rInfo + strlen(rInfo) + 1 + sizeof(mode_t)), 
            0664 | S_IFREG), 0, NULL);
    } else if(strncmp(buf + 1, "opn", 3) == 0){
        int res2;
        int res = open(pth, *(int*)(rInfo + strlen(rInfo) + 1));
        char* myxattr = malloc(41*2);
        res2 = getxattr(pth, "user.name", (void*)myxattr, 41);
        if(res2 < 0){
            memset(myxattr, 0, 41);
        }
        char* xattr2 = malloc(41);
        res2 = getFileHash((const char*)pth, xattr2);
        if(res2 < 0){
            memset(xattr2, 0, 41);
        }
        memcpy(myxattr + 41, xattr2, 41);
        write(cfd, myxattr, 41*2);
        free(myxattr);
        free(xattr2);
        sendRetvalErrChunk(cfd, res, 0, NULL);
    } else if(strncmp(buf + 1, "wrt", 3) == 0){
        size_t bytesReceived = 0;
        size_t bytesToReceive = *(size_t*)(rInfo + strlen(rInfo) + 1 + sizeof(int));
        int fd = *(int*)(rInfo + strlen(rInfo) + 1);
        char* bufToWrite = malloc(bytesToReceive);
        off_t offset = *(off_t*)(rInfo + strlen(rInfo) + 1 + sizeof(int) + sizeof(size_t));
        size_t chunk_szzz = *(size_t*)(rInfo + strlen(rInfo) + 1 
            + sizeof(int) + sizeof(size_t) + sizeof(off_t));

        if(bytesToReceive >= 0){
            while(bytesReceived < bytesToReceive){
                if(bytesToReceive - bytesReceived < chunk_szzz){
                    read(cfd, bufToWrite + bytesReceived, bytesToReceive - bytesReceived);
                    bytesReceived += bytesToReceive - bytesReceived;
                } else {
                    read(cfd, bufToWrite + bytesReceived, chunk_szzz);
                    bytesReceived += chunk_szzz;
                }
            }
        }
        //---------------------------------------------------
        int my_fd = open(pth, O_WRONLY);
        int res = pwrite(my_fd, bufToWrite, bytesToReceive, offset);
        if(rInfo[0] != '/')
            ftruncate(my_fd, offset + (off_t)res);
        close(my_fd);
        //---------------------------------------------------
        if(res > 0){
            if(setXhash((const char*)pth) == -1) res = -1;
        }
        sendRetvalErrChunk(cfd, res, 0, NULL);
        free(bufToWrite);
    } else if(strncmp(buf + 1, "rea", 3) == 0){
        char* bufRead = malloc((int)(*(size_t*)(rInfo + strlen(rInfo) + 1 + sizeof(int))));
        //---------------------------------------------------
        int my_fd = open(pth, O_RDONLY);
        int res = (int)pread(my_fd, bufRead, *(size_t*)(rInfo + strlen(rInfo) + 1 + sizeof(int)),
            *(off_t*)(rInfo + strlen(rInfo) + 1 + sizeof(int) + sizeof(size_t)));
        close(my_fd);
        //---------------------------------------------------
        sendRetvalErrChunk(cfd, res, 0, NULL);
        size_t chunk_szzz = *(size_t*)(rInfo + strlen(rInfo) + 1 
            + sizeof(int) + sizeof(size_t) + sizeof(off_t));
        if(res >= 0){
            int bytesSend = 0;
            int bytesToSend = res;
            while(bytesSend < bytesToSend){
                if(bytesToSend - bytesSend < chunk_szzz){
                    write(cfd, bufRead + bytesSend, bytesToSend - bytesSend);
                    bytesSend += bytesToSend - bytesSend;
                } else {
                    write(cfd, bufRead + bytesSend, chunk_szzz);
                    bytesSend += chunk_szzz;
                }
            }
        }
        free(bufRead);
    } else if(strncmp(buf + 1, "rel", 3) == 0){
        sendRetvalErrChunk(cfd, close(*(uint64_t*)(rInfo + strlen(rInfo) + 1)), 0, NULL);
    } else if(strncmp(buf + 1, "unl", 3) == 0){
        sendRetvalErrChunk(cfd, unlink(pth), 0, NULL);
    } else if(strncmp(buf + 1, "mkd", 3) == 0){
        sendRetvalErrChunk(cfd, mkdir((const char*)pth, 
            *(mode_t*)(rInfo + strlen(rInfo) + 1)), 0, NULL);
    } else if(strncmp(buf + 1, "odr", 3) == 0){
        dr = opendir(pth);
    } else if(strncmp(buf + 1, "rdr", 3) == 0){
        DIR* dirnt = opendir(pth);
        r1_readdir(pth, cfd, dirnt);
    } else if(strncmp(buf + 1, "rld", 3) == 0){
        sendRetvalErrChunk(cfd, closedir(dr), 0, NULL);
    } else if(strncmp(buf + 1, "rmd", 3) == 0){
        sendRetvalErrChunk(cfd, rmdir(pth), 0, NULL);
    } else if(strncmp(buf + 1, "gat", 3) == 0){
        struct stat* stbuf = malloc(sizeof(struct stat));
        r1_getattr(pth, cfd, stbuf);
        free(stbuf);
    } else if(strncmp(buf + 1, "rnm", 3) == 0){
        char* newPth = malloc(NAME_MAX + 1);
        strncpy(newPth, storageName, strlen(storageName));
        strcpy(newPth + strlen(storageName), rInfo + strlen(rInfo) + 1);
        
        sendRetvalErrChunk(cfd, rename(pth, newPth), 0, NULL);

        free(newPth);
    } else if(strncmp(buf + 1, "chm", 3) == 0){
        sendRetvalErrChunk(cfd, chmod(pth, *(size_t*)(rInfo + strlen(rInfo) + 1)), 0, NULL);
    }

    free(rInfo);
    free(pth);

    return 0;
}

int handleR5(char* buf, int cfd, char* storageName)
{
    int i;
    for(i = CHUNK_SZ; i > 0; i--){
        if(buf[i] == '1'){
            strncpy(buf + i, "\0", 1);
            break;
        }
    }

    printf("\n%s\n", buf);

    char* rInfo = malloc(CHUNK_SZ - COMMAND_SZ);
    memcpy(rInfo, buf + COMMAND_SZ, CHUNK_SZ - COMMAND_SZ);

    char* pth = malloc(NAME_MAX + 1);
    strncpy(pth, storageName, strlen(storageName));
    strncpy(pth + strlen(storageName), rInfo, strlen(rInfo) + 1);
    strncpy(pth + strlen(storageName), "/", 1);
    strncpy(pth + strlen(storageName) + strlen(rInfo), "\0", 1);

    printf("%s\n", pth);

    if(strncmp(buf + 1, "opn", 3) == 0){
        int res = open(pth, *(int*)(rInfo + strlen(rInfo) + 1));
        sendRetvalErrChunk(cfd, res, 0, NULL);
    }

    free(rInfo);
    free(pth);

    return 0;
}

void client_handler(int cfd, char* storageName)
{
    char* buf = malloc(CHUNK_SZ);
    int data_size;
    while (1) {
        data_size = read (cfd, buf, CHUNK_SZ);
        if (data_size <= 0)
            break;
        if(buf[0] == '1'){
            handleR1(buf, cfd, storageName);
        } else if(buf[0] == '5'){
            handleR5(buf, cfd, storageName);
        }
    }
    close(cfd);
    free(buf);
}


int main(int argc, char* argv[])
{
    int sfd, cfd;
    struct sockaddr_in addr;
    struct sockaddr_in peer_addr;

    int ip;
    int port = (int) strtol(argv[2], (char **)NULL, 10);

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    inet_pton(AF_INET, argv[1], &ip);
    addr.sin_addr.s_addr = ip;

    bind(sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));
    listen(sfd, BACKLOG);
    
    while (1) 
    {
        int peer_addr_size = sizeof(struct sockaddr_in);
        cfd = accept(sfd, (struct sockaddr *) &peer_addr, &peer_addr_size);

        switch(fork()) {
            case -1:
                exit(100);
            case 0:
                close(sfd);

                client_handler(cfd, argv[3]);

                exit(0);
            default:
                close(cfd);
        }
    }
    close(sfd);
}
