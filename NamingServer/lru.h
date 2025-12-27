#ifndef LRU_H
#define LRU_H
#include <time.h>
#include "../Utils/common.h"

typedef struct {
    char path[256];
    time_t last_access;
    int storage_server;
} CacheEntry;

void update_cache(const char* path, int server);
int check_cache(const char* path);

#endif