
HEADERS = utilz/utilz.h nrfs_parse/configParser.h nrfs_mount/mountRaid1.h nrfs_mount/mountRaid5.h utilz/sha1.h

default: program

program.o: utilz/utilz.c utilz/sha1.c net_raid_client.c nrfs_parse/configParser.c $(HEADERS)
	gcc -c utilz/sha1.c -o utilz/sha1.o
	gcc -c utilz/utilz.c -o utilz/utilz.o
	gcc -c net_raid_client.c -o net_raid_client.o
	gcc -c nrfs_parse/configParser.c -o nrfs_parse/configParser.o

program: utilz/utilz.o utilz/sha1.o net_raid_client.o nrfs_parse/configParser.o nrfs_mount/mountRaid1.c
	gcc -Wall utilz/utilz.o net_raid_client.o nrfs_parse/configParser.o nrfs_mount/mountRaid1.c `pkg-config fuse --cflags --libs` nrfs_mount/mountRaid5.c `pkg-config fuse --cflags --libs` -o net_raid_client
	gcc utilz/sha1.o net_raid_server.c -o net_raid_server
