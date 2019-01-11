
#include "configParser.h"


int manageCopying(char* dest, char* src, char* sample){
    char* end = strdup("\0");
    strncpy(dest, src + strlen(sample) + 3, 
        strlen(src) - strlen(sample) - 3);
    strncpy(dest + strlen(src) - strlen(sample) - 3 - 1,
        end, 1);

    free(end);
    return 0;
}


int handleBasic(basicInfo_t* basicInfo, char* line){
    char* compSubString = malloc(7);
    strncpy(compSubString, line, 7);

    char* erl = strdup("errorlog");
    char* csz = strdup("cache_size");
    char* cre = strdup("cache_replacment");
    char* tim = strdup("timeout");

    if(strncmp(erl, line, strlen(erl)) == 0){
        manageCopying(basicInfo->errorlog, line, erl);
    } else if(strncmp(csz, line, strlen(csz)) == 0){
        manageCopying(basicInfo->cache_size, line, csz);
    } else if(strncmp(cre, line, strlen(cre)) == 0){
        manageCopying(basicInfo->cache_replacment, line, cre);
    } else if(strncmp(tim, line, strlen(tim)) == 0){
        char* end = strdup("\0");
        char* timeC = malloc(VAL_SZ);
        strncpy(timeC, line + strlen(tim) + 3, 
            strlen(line) - strlen(tim) - 3);
        strncpy(timeC + strlen(line) - strlen(tim) - 3 - 1,
            end, 1);

        basicInfo->timeout = (int) strtol(timeC, (char **)NULL, 10);
        
        free(end);
        free(timeC);
    }

    free(erl);
    free(csz);
    free(cre);
    free(tim);
    free(compSubString);

    return 0;
}


/*
    we pass line in "key = val" and first part of line
    in format "key = " 
    function returns val int format 
*/
int getIntValuePart(char* line, char* key){
    char* val = malloc(strlen(line) - strlen(key));
    strncpy(val, line + strlen(key), strlen(line) - strlen(key) - 1);
    strncpy(val + strlen(line) - strlen(key) - 1, 
        line + strlen(line), 1);
    int retval = (int) strtol(val, (char **)NULL, 10);
    free(val);
    return retval;
}


/*
    we pass line in "key = val" and first part of line
    in format "key = " 
    function allocates memory for val and returns val 
*/
char* getValuePart(char* line, char* key){
    char* retval = malloc(strlen(line) - strlen(key));
    strncpy(retval, line + strlen(key), strlen(line) - strlen(key) - 1);
    strncpy(retval + strlen(line) - strlen(key) - 1, 
        line + strlen(line), 1);
    return retval;
}


/*
    we pass address string in format: "127.0.0.1:10001"
    and method allocates structure and returns its pointer
    struct format:  mySt->ip = 127.0.0.1;
                    mySt->port = 10001;
*/
myAddress_t* parseAddress(char* address){
    myAddress_t* newAddress = malloc(sizeof(myAddress_t));

    int i;
    for(i = 0; i < strlen(address); i++){
        if(strncmp(&address[i], ":", 1) == 0){
            break;
        }
    }

    newAddress->ip = malloc(i + 1);
    strncpy(newAddress->ip, address, i);
    strncpy(newAddress->ip + i, address + strlen(address), 1);

    char* strPort = malloc(strlen(address) - i);
    strncpy(strPort, address + i + 1, strlen(address) - i);

    int portt = (int) strtol(strPort, (char **)NULL, 10);
    newAddress->port = portt;

    free(strPort);
    free(address);
    return newAddress;
}


servList_t* parseAdressList(char* serversLine){
    servList_t* sList = malloc(sizeof(servList_t));
    sList->logLen = 1;
    sList->servers = malloc(sizeof(myAddress_t));

    int i;
    int prevIndx = 0;
    for(i = 0; i < strlen(serversLine) + 1; i++){
        if(strncmp(&serversLine[i], ",", 1) == 0 || strncmp(&serversLine[i], "\0", 1) == 0){
            char* adrz = malloc(i - prevIndx + 1);
            strncpy(adrz, serversLine + prevIndx, i - prevIndx);
            strncpy(adrz + i - prevIndx, serversLine + strlen(serversLine), 1);
            
            myAddress_t* parsedServer = parseAddress(adrz);
            memcpy(&((sList->servers)[sList->logLen - 1]), parsedServer,
                sizeof(myAddress_t));
            free(parsedServer);

            if(strncmp(&serversLine[i], ",", 1) == 0){
                sList->logLen += 1;
                sList->servers = realloc(sList->servers, 
                    (sList->logLen)*sizeof(myAddress_t));
                prevIndx = i + 2;
            }
        }
    }

    return sList;
}


int handleDisks(diskList_t* dList, char* line, int nblock){

    char* dsk = strdup("diskname = ");
    char* mtp = strdup("mountpoint = ");
    char* rad = strdup("raid = ");
    char* srv = strdup("servers = ");
    char* hts = strdup("hotswap = ");

    if(nblock > dList->logLen){
        dList->disks = realloc(dList->disks, nblock*sizeof(disk_t));
        dList->logLen += 1;
    }

    if(strncmp(dsk, line, strlen(dsk)) == 0){
        (dList->disks)[nblock - 1].diskname =  getValuePart(line, dsk);
    } else if(strncmp(mtp, line, strlen(mtp)) == 0){
        (dList->disks)[nblock - 1].mountpoint = getValuePart(line, mtp);
    } else if(strncmp(rad, line, strlen(rad)) == 0){
        (dList->disks)[nblock - 1].raid = getIntValuePart(line, rad);
    } else if(strncmp(srv, line, strlen(srv)) == 0){
        char* serversLine = getValuePart(line, srv);
        (dList->disks)[nblock - 1].servList = parseAdressList(serversLine);
        //free(serversLine);
    } else if(strncmp(hts, line, strlen(hts)) == 0){
        char* adrs = getValuePart(line, hts);
        (dList->disks)[nblock - 1].hotswap = parseAddress(adrs);
        //free(adrs);
    }

    free(dsk);
    free(mtp);
    free(rad);
    free(srv);
    free(hts);
    return 0;
}


int lineHandler(basicInfo_t* basicInfo, diskList_t* dList, char* line, int nblock){
    if(nblock == 0){
        handleBasic(basicInfo, line);
    } else {
        handleDisks(dList, line, nblock);
    }

    return 0;
}


int parseConfig(basicInfo_t* basicInfo, diskList_t* dList, char* configPath){
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen(configPath, "r");
    if (fp == NULL)
        return -1;

    int blockcounter = 0;
    int linecounter = 0;
    while ((read = getline(&line, &len, fp)) != -1) {
        lineHandler(basicInfo, dList, line, blockcounter);
        if(line[0] == '\n') blockcounter += 1;
        linecounter += 1;
        //printf("%s", line);
    }

    fclose(fp);
    if (line)
        free(line);

    return 0;
}
