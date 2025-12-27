#ifndef TRIE_H_
#define TRIE_H_
#include <stdio.h>
#include <stdbool.h>
#include "../Utils/common.h"

#define MAXFILESIZE 10485760
#define PATHLEN 200
#define BACKUPSERVERS 2
#define DATAMAX 2048
#define MAXCHILDFILES 256
#define OPERATIONTIMEOUT 5

typedef struct TrieNode {
    bool deleted;
    bool isFolder;
    bool isFile;
    int id;
    int children[MAXCHILDFILES];
    int childCount;
    int parent;
    char name[PATHLEN];
    int readerCount;
    int writerCount;
    int mainServer;
} TrieNode;

void initializeTrie();
TrieNode* createTrieNode(const char* name, int parent, bool isFolder, bool isFile, int ss);
TrieNode* insert_path(const char* path, bool isFolder, int ss);
TrieNode* searchPath(const char* path);
void deletePath(const char* path);
int countSubpaths(const char* path);
void printSubpathsRecursive(int nodeIndex, char* currentPath);
void printSubpaths(const char* path);
void collectSubpaths(int nodeIndex, char* currentPath, char* result, ssize_t bufferSize);
char* concatenateSubpaths(const char* path);

#endif