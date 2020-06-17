#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/inotify.h>
#include <poll.h>
#include <sys/time.h>

#include "main.h"

int main(int argc, char **argv) {
  char working_dir[PATH_MAX];
  if (getcwd(working_dir, PATH_MAX - 1) == NULL) {
    perror("Couldn't determine current directory\n");
    exit(EXIT_FAILURE);
  }
  
  if (argc <= 1) {
    perror("Command required\n");
    exit(EXIT_FAILURE);
  }

  Filter *filter = init_filters();

  int inotify_descriptor = inotify_init1(IN_NONBLOCK);
  if (inotify_descriptor == -1) {
    perror("Inotify init failed. Exiting");
    exit(EXIT_FAILURE);
  }

  add_watch(inotify_descriptor, working_dir);

  pollfd poll_fd = {.fd = inotify_descriptor, .events = POLLIN};

  WdHash *hash = wd_hash_create();

  int polls_count;
  while(1) {
    polls_count = poll(&poll_fd, 1, -1);

    if (polls_count < 0) {
      if (errno == EINTR) continue;
      perror("Poll");
      exit(EXIT_FAILURE);
    }

    if (polls_count > 0) {
      if (poll_fd.revents & POLLIN) {
        handle_events(inotify_descriptor, argv[1], hash);
      }
    }
  }

  return 0;
}

static void handle_events(int fd, char *command, WdHash *hash) {
  char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
  const struct inotify_event *event;
  ssize_t length;
  char *ptr;

  int fire = 0;
  length = read(fd, buf, sizeof(buf));

  if (length == -1 && errno != EAGAIN) {
    perror("read");
    exit(EXIT_FAILURE);
  }

  if (length == 0) return;

  for(ptr = buf; ptr < buf + length; ptr += sizeof(struct inotify_event) + event->len) {
    event = (const struct inotify_event*) ptr;

    if (event->mask & IN_ISDIR) {
      if (event->mask & IN_CREATE) {
        // TODO: Can't do this. Fails for inner folders. Need to build a full path first.
        int new_wd = add_watch(fd, event->name);
        WdNode *parent = wd_hash_get(hash, event->wd);
        WdNode *new_node = wd_node_create(new_wd, (char*)event->name);
        if (parent != NULL) {
          new_node->parent = parent;
          wd_node_add_child(parent, new_node);
        }

        wd_hash_set(hash, new_node);
      }
      if (event->mask & IN_DELETE) { fire = 1; }

    } else {
      WdNode *node = wd_hash_get(hash, event->wd);

#if 0
      if (node) {
        printf("%s", node->working_dir);
        if (node->parent) {
          printf("/%s/", node->parent->working_dir);
        }
      }
      printf("%s\n", event->name);
#endif
      // if (event->len) printf("%s\n", event->name); TODO: handle exceptions
      fire = 1;
    }
  }

  if (fire) {
    system(command);
    fire = 0;
  }
}

static int add_watch(int fd, char const *working_dir) {
  int watch = inotify_add_watch(fd, working_dir, IN_CREATE | IN_MODIFY | IN_DELETE);
  if (watch == -1) { fprintf(stderr, "Cannot watch %s.\n", working_dir); }

  return watch;
}

static Filters* init_filters() {
  FILE *gitignore = fopen(".gitignore", "r");
  if (gitignore == NULL) {
    return NULL;
  }

  Filter *filter = (Filters*)malloc(sizeof(Filter));
  size_t count = 0;
  char **filters = (char**)malloc(0);

  char buffer[4096]; // unlikely long filter line; TODO: check boundaries in the loop
  size_t length = 0;
  char ch;

  while ((ch = fgetc(gitignore)) != EOF) {
    if (ch == '\n' && length > 0) {
      buffer[length++] = 0;

      // TODO: Don't you know about possible failures?
      filters = (char**)realloc(filters, (count + 1) * sizeof(char*));
      filters[count] = (char*)malloc(length * sizeof(char));
      memcpy(filters[count], buffer, length);
      count++;

      length = 0;
    } else {
      buffer[length++] = ch;
    }
  }

  // code above cleaner this way. that's why.
  filter->count = count;
  filter->filters = filters;

  return filter;
}

// IN PROGRESS. DO NOT USE YET
WdNode *wd_node_create(int fd, char *working_dir) {
  WdNode *node = (WdNode*)malloc(sizeof(WdNode));
  node->fd = fd;
  node->parent = NULL;

  ssize_t clen = strlen(working_dir);
  node->working_dir = (char*)malloc(sizeof(char) * clen);
  memcpy(node->working_dir, working_dir, clen);

  node->childs = (WdNode**)malloc(0);
  node->childs_count = 0;

  return node;
}

void wd_node_free(WdNode* node) {
  if (node == NULL) return;

  free(node->childs);
  free(node->working_dir);
  free(node);
}

void wd_node_add_child(WdNode *parent, WdNode *child) {
  parent->childs_count++;
  parent->childs = (WdNode**)realloc(parent->childs, parent->childs_count * sizeof(WdNode*));
  parent->childs[parent->childs_count] = child;
}

WdHash *wd_hash_create() {
  WdHash *hash = (WdHash*)malloc(sizeof(WdHash));

  hash->nodes_count = 10; // default. will exponentially increase
  hash->nodes = (WdNode**)malloc(sizeof(WdHash*) * hash->nodes_count);
  memset(hash->nodes, 0, sizeof(WdHash*) * hash->nodes_count); // I wounder if this will be optimized out
  hash->occupied = 0;

  return hash;
}

WdNode *wd_hash_get(WdHash* hash, int fd) {
  int bucket = fd % hash->nodes_count;
  WdNode *node = hash->nodes[bucket];
  if (node && node->fd == fd) return node;

  for (;bucket < hash->nodes_count; bucket++) {
    node = hash->nodes[bucket];
    if (node && node->fd == fd) return node;
  }

  return NULL;
}

static void wd_hash_expand(WdHash *hash) {
    hash->nodes_count *= 2;
    hash->nodes = (WdNode**)realloc(hash->nodes, sizeof(WdNode*) * hash->nodes_count);
}

void wd_hash_set(WdHash* hash, WdNode *node) {
  int bucket = node->fd % hash->nodes_count;
  int is_free = 0;
  for (; bucket < hash->nodes_count; bucket++) {
    if (&hash->nodes[bucket] == NULL) {
      is_free = 1;
      break;
    }
  }

  if (bucket == hash->nodes_count && !is_free) {
    // TODO: this might be unnecessary because of the check below
    wd_hash_expand(hash);
    bucket++;
  }

  hash->nodes[bucket] = node;
  hash->occupied++;

  if (hash->occupied / hash->nodes_count > 0.75) {
    wd_hash_expand(hash);
  }
}

void wd_hash_del(WdHash* hash, int fd) {
  WdNode *node = wd_hash_get(hash, fd);
  if (node == NULL) return;

  for (int i = 0; i < node->childs_count; i++) {
    wd_hash_del(hash, node->childs[i]->fd);
  }

  wd_node_free(node);
}
