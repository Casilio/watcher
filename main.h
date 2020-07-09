#ifndef __MAIN_H
#define __MAIN_H

#include <stdlib.h>

// inotify event contains only filename without full path.
// I need to make possible to track down full path using 
// provided watch file descriptor.
// Below I'll define simple hash and a few other types for this purpose.

typedef struct WdNode {
  int fd;
  WdNode *parent;
  char *working_dir;
  WdNode **childs;
  ssize_t childs_count;
} WdNode;

WdNode *wd_node_create(int fd, char *working_dir);
void wd_node_free(WdNode*);
void wd_node_add_child(WdNode *parent, WdNode *child);
// Don't need a hash function since inotify already gives me a number
// so simple modulo and a little bit of traversal will do.
// Although I'll need to test this. Not sure it'll be sufficient.
typedef struct WdHash {
  WdNode **nodes;
  ssize_t nodes_count;
  ssize_t occupied;
} WdHash;

WdHash *wd_hash_create();
WdNode *wd_hash_get(WdHash* hash, int fd);
void wd_hash_set(WdHash* hash, WdNode *);
void wd_hash_del(WdHash* hash, int fd);

// Some random declarations
typedef struct Filter {
  ssize_t count;
  char **filters;
  int *idx;
} Filters;

#endif

