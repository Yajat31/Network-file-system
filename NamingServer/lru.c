#include  "lru.h"

extern CacheEntry lru_cache[CACHE_SIZE];
extern int cache_count;

void update_cache(const char* path, int server) {
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(lru_cache[i].path, path) == 0) {
            lru_cache[i].last_access = time(NULL);
            lru_cache[i].storage_server = server;
            return;
        }
    }
    
    if (cache_count < CACHE_SIZE) {
        strcpy(lru_cache[cache_count].path, path);
        lru_cache[cache_count].last_access = time(NULL);
        lru_cache[cache_count].storage_server = server;
        cache_count++;
    } else {
        int lru_index = 0;
        time_t oldest_time = lru_cache[0].last_access;
        for (int i = 1; i < CACHE_SIZE; i++) {
            if (lru_cache[i].last_access < oldest_time) {
                oldest_time = lru_cache[i].last_access;
                lru_index = i;
            }
        }

        strcpy(lru_cache[lru_index].path, path);
        lru_cache[lru_index].last_access = time(NULL);
        lru_cache[lru_index].storage_server = server;
    }
}

int check_cache(const char* path) {
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(lru_cache[i].path, path) == 0) {
            lru_cache[i].last_access = time(NULL);
            return lru_cache[i].storage_server;
        }
    }
    return -1;
}