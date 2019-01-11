#include <stdio.h>
#include <stdlib.h> //malloc
#include <string.h> //strdup
#include  <sys/types.h> //fork

#include "nrfs_parse/configParser.h"
#include "nrfs_mount/mountRaid1.h"
#include "nrfs_mount/mountRaid5.h"

int mountHandler(diskList_t* dList, int mountN, basicInfo_t* basicInfo)
{
    if(mountN == dList->logLen)
        return 0;

    pid_t pid;
    pid = fork();
    //if pid == 0 it is child process
    if(pid == 0){
        char** argv = malloc(5*sizeof(char*));
        argv[4] = (char*)basicInfo;
        argv[3] = (char*)&((dList->disks)[mountN]);
        argv[2] = strdup("-f");
        argv[1] = (dList->disks)[mountN].mountpoint;
        argv[0] = strdup("net_raid_client");
        if((dList->disks)[mountN].raid == 1){
            mainRunRaid1(argv);
        } else if((dList->disks)[mountN].raid == 5){
            mainRunRaid5(argv);
        }

        free(argv[0]);
        free(argv[2]);
        free(argv);
    } else {
        mountHandler(dList, mountN + 1, basicInfo);
    }

    return 0;
}

int main(int argc, char* argv[])
{
    char* configPath = strdup(argv[1]);

    //-------------BASIC INFO SETUP-------------
    basicInfo_t* basicInfo = malloc(sizeof(basicInfo_t));
    basicInfo->errorlog = malloc(VAL_SZ);
    basicInfo->cache_size = malloc(VAL_SZ);
    basicInfo->cache_replacment = malloc(VAL_SZ);

    //-------------DISK LIST SETUP-------------
    diskList_t* dList = malloc(sizeof(diskList_t));
    dList->logLen = 1;
    dList->disks = malloc(sizeof(disk_t));

    parseConfig(basicInfo, dList, configPath);

    mountHandler(dList, 0, basicInfo);

    //----------------FREE ALL ALLOCATED----------------
    free(configPath);
    
    //FREE BASIC INFO
    free(basicInfo->errorlog);
    free(basicInfo->cache_size);
    free(basicInfo->cache_replacment);
    free(basicInfo);

    //FREE DISK LIST
    int i;
    for(i = 0; i < dList->logLen; i++){
        free((dList->disks)[i].diskname);
        free((dList->disks)[i].mountpoint);

        free((dList->disks)[i].hotswap->ip);
        free((dList->disks)[i].hotswap);

        int j;
        for(j = 0 ; j < (dList->disks)[i].servList->logLen; j++){

            free((dList->disks)[i].servList->servers[j].ip);
        }
        free((dList->disks)[i].servList->servers);
        free((dList->disks)[i].servList);
    }
    free(dList->disks);
    free(dList);

    return 0;
}
