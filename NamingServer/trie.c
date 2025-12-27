#include "trie.h"

extern TrieNode* trienodes;
extern int triCount;
extern int root;

void initializeTrie() {
    triCount = 1;
    trienodes = (TrieNode*)malloc(sizeof(TrieNode));
    if (!trienodes) {
        perror("Failed to allocate memory for trie");
        exit(EXIT_FAILURE);
    }

    root = 0;
    strcpy(trienodes[root].name, "/");
    trienodes[root].isFolder = true;
    trienodes[root].isFile = false;
    trienodes[root].deleted = false;
    trienodes[root].parent = -1;
    trienodes[root].childCount = 0;
    trienodes[root].readerCount = 0;
    trienodes[root].writerCount = 0;
    trienodes[root].mainServer = 0;
}

TrieNode* createTrieNode(const char* name, int parent, bool isFolder, bool isFile, int ss) {
    // Ensure enough space for new nodes
    trienodes = (TrieNode*)realloc(trienodes, (triCount + 1) * sizeof(TrieNode));
    if (!trienodes) {
        perror("Failed to reallocate memory for trie");
        exit(EXIT_FAILURE);
    }

    TrieNode* node = &trienodes[triCount];
    node->deleted = false;
    node->isFolder = isFolder;
    node->isFile = isFile;
    node->id = triCount;  // Assign before incrementing triCount
    node->parent = parent;
    node->childCount = 0;
    strncpy(node->name, name, PATHLEN - 1);
    node->name[PATHLEN - 1] = '\0';
    node->readerCount = 0;
    node->writerCount = 0;
    node->mainServer = ss;

    triCount++;  // Increment after setting the node's ID
    return node;
}

TrieNode* insert_path(const char* path, bool isFolder, int ss) {
    if (root == -1) {
        initializeTrie();
    }

    char pathCopy[PATHLEN];
    strncpy(pathCopy, path, PATHLEN - 1);
    pathCopy[PATHLEN - 1] = '\0';

    char* token = strtok(pathCopy, "/");
    int currentIndex = root;

    while (token) {
        bool found = false;
        for (int i = 0; i < trienodes[currentIndex].childCount; i++) {
            int childIndex = trienodes[currentIndex].children[i];
            if (strcmp(trienodes[childIndex].name, token) == 0) {
                currentIndex = childIndex;
                found = true;
                break;
            }
        }

        if (!found) {
            TrieNode* newNode = createTrieNode(token, currentIndex, isFolder, !isFolder, ss);
            trienodes[currentIndex].children[trienodes[currentIndex].childCount++] = newNode->id;
            currentIndex = newNode->id;
        }

        token = strtok(NULL, "/");
    }

    return &trienodes[currentIndex];
}

TrieNode* searchPath(const char* path) {
    if (root == -1) {
        return NULL;
    }

    char pathCopy[PATHLEN];
    strncpy(pathCopy, path, PATHLEN - 1);
    pathCopy[PATHLEN - 1] = '\0';

    char* token = strtok(pathCopy, "/");
    int currentIndex = root;

    while (token) {
        bool found = false;
        for (int i = 0; i < trienodes[currentIndex].childCount; i++) {
            int childIndex = trienodes[currentIndex].children[i];
            if (strcmp(trienodes[childIndex].name, token) == 0) {
                currentIndex = childIndex;
                found = true;
                break;
            }
        }

        if (!found) {
            return NULL;
        }
        token = strtok(NULL, "/");
    }
    return &trienodes[currentIndex];
}

void deleteSubtree(int nodeIndex) {
    TrieNode* node = &trienodes[nodeIndex];

    node->deleted = true;

    for (int i = 0; i < node->childCount; i++) {
        int childIndex = node->children[i];
        if (!trienodes[childIndex].deleted) {
            deleteSubtree(childIndex);
        }
    }
}

void deletePath(const char* path) {
    TrieNode* node = searchPath(path);
    if (!node) {
        printf("Path not found: %s\n", path);
        return;
    }
    deleteSubtree(node->id);
}

void printSubpathsRecursive(int nodeIndex, char* currentPath) {
    TrieNode* node = &trienodes[nodeIndex];

    char newPath[PATHLEN];
    if(strcmp(currentPath, "/") == 0) {
        snprintf(newPath, PATHLEN, "%s%s", currentPath, node->name);
    } else {
        snprintf(newPath, PATHLEN, "%s/%s", currentPath, node->name);
    }

    printf("%s\n", newPath);

    for (int i = 0; i < node->childCount; i++) {
        int childIndex = node->children[i];
        if (!trienodes[childIndex].deleted) {
            printSubpathsRecursive(childIndex, newPath);
        }
    }
}

void printSubpaths(const char* path) {
    TrieNode* node = searchPath(path);
    if (node == NULL) {
        printf("Path not found: %s\n", path);
        return;
    }

    for (int i = 0; i < node->childCount; i++) {
        int childIndex = node->children[i];
        if (!trienodes[childIndex].deleted) {
            printSubpathsRecursive(childIndex, path);
        }
    }
}

int countSubpaths(const char* path) {
    TrieNode* node = searchPath(path);
    if (!node) {
        printf("Path not found: %s\n", path);
        return -1;
    }

    return node->childCount;
}

void collectSubpaths(int nodeIndex, char* currentPath, char* result, ssize_t bufferSize) {
    TrieNode* node = &trienodes[nodeIndex];

    char newPath[PATHLEN];
    if (strcmp(currentPath, "/") == 0) {
        snprintf(newPath, PATHLEN, "%s%s", currentPath, node->name);
    } else {
        snprintf(newPath, PATHLEN, "%s/%s", currentPath, node->name);
    }

    // Ensure enough space in the buffer
    size_t newLength = strlen(result) + strlen(newPath) + 2; // +2 for '\n' and '\0'
    if (newLength > bufferSize) {
        bufferSize *= 2;
        result = (char*)realloc(result, bufferSize);
        if (!result) {
            perror("Failed to reallocate memory for concatenated subpaths");
            exit(EXIT_FAILURE);
        }
    }

    // Add the path to the result
    strcat(result, newPath);
    strcat(result, "\n");

    // Recursively process children
    for (int i = 0; i < node->childCount; i++) {
        int childIndex = node->children[i];
        if (!trienodes[childIndex].deleted) {
            collectSubpaths(childIndex, newPath, result, bufferSize);
        }
    }
}

char* concatenateSubpaths(const char* path) {
    TrieNode* node = searchPath(path);
    if (!node) {
        printf("Path not found: %s\n", path);
        return NULL;
    }

    // Initialize a dynamic buffer to hold the concatenated subpaths
    size_t bufferSize = 1024;
    char* result = (char*)malloc(bufferSize);
    if (!result) {
        perror("Failed to allocate memory for concatenated subpaths");
        return NULL;
    }

    result[0] = '\0'; // Start with an empty string

    // Start collecting subpaths from the given node
    for (int i = 0; i < node->childCount; i++) {
        int childIndex = node->children[i];
        if (!trienodes[childIndex].deleted) {
            collectSubpaths(childIndex, path, result, bufferSize);
        }
    }
    return result;
}
