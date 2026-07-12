CC      := gcc
CFLAGS  := -g -Wall -Wextra -Wno-unused-parameter -IUtils
LDFLAGS := -lpthread

ROOT := $(CURDIR)

NAMING_SRCS := NamingServer/naming_server.c NamingServer/trie.c NamingServer/lru.c NamingServer/utils.c
STORAGE_SRCS := StorageServer1/storage_server.c StorageServer1/utils.c
CLIENT_SRCS  := Client/client.c Client/utils.c

.PHONY: all clean

all: naming_server storage_server client

naming_server: $(NAMING_SRCS) Utils/common.h NamingServer/*.h
	$(CC) $(CFLAGS) $(NAMING_SRCS) -o $@ $(LDFLAGS)

storage_server: $(STORAGE_SRCS) Utils/common.h StorageServer1/*.h
	$(CC) $(CFLAGS) $(STORAGE_SRCS) -o $@ $(LDFLAGS)

client: $(CLIENT_SRCS) Utils/common.h Client/*.h
	$(CC) $(CFLAGS) $(CLIENT_SRCS) -o $@ $(LDFLAGS) -lreadline

clean:
	rm -f naming_server storage_server client
	rm -f nfs.log *.zip
