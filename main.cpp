#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/inotify.h>
#include <poll.h>
#include <sys/time.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <dirent.h>

#include "main.h"

char *ignore_dirs[] = {
  (char*)".",
  (char*)"..",
  (char*)".git"
};

static int add_watch(int fd, char const *working_dir) {
  int watch = inotify_add_watch(fd, working_dir, IN_CREATE | IN_MODIFY | IN_DELETE);
  if (watch == -1) { fprintf(stderr, "Cannot watch %s: %s.\n", working_dir, strerror(errno)); }

  return watch;
}

static char *fullpath(WdNode *node, char *buffer) {
  if (node->parent) {
    fullpath(node->parent, buffer);
  }
  sprintf(buffer + strlen(buffer), "%s/", node->working_dir);

  return buffer;
}

static int watch_dir(WdHash *hash, int ifd, int parent_wd, char *dirname) {
  char buffer[PATH_MAX];
  memset(buffer, 0, PATH_MAX * sizeof(char));

  WdNode *parent = wd_hash_get(hash, parent_wd);

  if (parent) { fullpath(parent, buffer); }
  snprintf(buffer + strlen(buffer), PATH_MAX, "%s", dirname);

  int new_wd = add_watch(ifd, buffer);
  if (new_wd == -1) return -1;

  WdNode *new_node = wd_node_create(new_wd, dirname);
  if (parent != NULL) {
    new_node->parent = parent;
    wd_node_add_child(parent, new_node);
  }

  wd_hash_set(hash, new_node);

  return new_wd;
}

static void add_watch_recursively(WdHash *hash, char* dir_name, int ifd, int parent_wd) {
  DIR *dir = opendir(dir_name);

  if (errno) {
    perror(strerror(errno));
    exit(EXIT_FAILURE);
  }
  chdir(dir_name);

  int wd = watch_dir(hash, ifd, parent_wd, dir_name);
  if (wd == -1) goto error;

  while (dirent *child = readdir(dir)) {
    int i = 0, skip = 0;
    while (ignore_dirs[i]) {
      if (strcmp(child->d_name, ignore_dirs[i]) == 0) skip = 1;
      i++;
    }

    if (skip) continue;

    if (child->d_type == DT_DIR) {
      add_watch_recursively(hash, child->d_name, ifd, wd);
    }
  }

error:
  chdir("..");
  closedir(dir);
}

static void handle_events(int fd, char *command, WdHash *hash, Filter* filter) {
  char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
  const struct inotify_event *event;
  ssize_t length;
  char *ptr;
  char buffer[PATH_MAX];

  int fire = 0;
  length = read(fd, buf, sizeof(buf));

  if (length == -1 && errno != EAGAIN) {
    perror("read");
    exit(EXIT_FAILURE);
  }

  if (length == 0) return;

  for(ptr = buf; ptr < buf + length; ptr += sizeof(struct inotify_event) + event->len) {
    event = (const struct inotify_event*) ptr;
    memset(buffer, 0, PATH_MAX * sizeof(char));

    if (event->mask & IN_ISDIR) {
      if (event->mask & IN_CREATE) {
        watch_dir(hash, fd, event->wd, (char*)event->name);
      }
      if (event->mask & IN_DELETE) { fire = 1; }

    } else {
      WdNode *node = wd_hash_get(hash, event->wd);

      if (node) { fullpath(node, buffer); }
      snprintf(buffer + strlen(buffer), PATH_MAX, "%s", event->name);

      int matched = 0;
      for (int i = 0; i < filter->count; i++) {
        if (fnmatch(filter->filters[i], buffer, 0) == 0) {
          matched = 1;
          break;
        }
      }

      if (!matched) {
        fire = 1;
      }
    }
  }

  if (fire) {
    system(command);
    fire = 0;
  }
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

  filter->count = count;
  filter->filters = filters;

  return filter;
}

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

  pollfd poll_fd = {.fd = inotify_descriptor, .events = POLLIN};

  WdHash *hash = wd_hash_create();

  add_watch_recursively(hash, working_dir, inotify_descriptor, 0);

  printf("Looking at you, %s\n", working_dir);
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
        handle_events(inotify_descriptor, argv[1], hash, filter);
      }
    }
  }

  return 0;
}

WdNode *wd_node_create(int fd, char *working_dir) {
  WdNode *node = (WdNode*)malloc(sizeof(WdNode));
  node->fd = fd;
  node->parent = NULL;

  ssize_t clen = strlen(working_dir) + 1;
  node->working_dir = (char*)malloc(sizeof(char) * clen);
  memcpy(node->working_dir, working_dir, clen);
  node->working_dir[clen] = 0;

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

WdNode *wd_hash_get(WdHash* hash, int wd) {
  int bucket = wd % hash->nodes_count;
  WdNode *node;

  for (;bucket < hash->nodes_count; bucket++) {
    node = hash->nodes[bucket];
    if (node && node->fd == wd) return node;
  }

  return NULL;
}

static void wd_hash_expand(WdHash *hash) {
    hash->nodes_count *= 2;
    hash->nodes = (WdNode**)realloc(hash->nodes, sizeof(WdNode*) * hash->nodes_count);
}

void wd_hash_set(WdHash* hash, WdNode *node) {
  int bucket = node->fd % hash->nodes_count;
  int free_bucket_exists = 0;
  for (; bucket < hash->nodes_count; bucket++) {
    if (hash->nodes[bucket] == NULL || hash->nodes[bucket]->fd == node->fd) {
      free_bucket_exists = 1;
      break;
    }
  }

  if (bucket == hash->nodes_count && !free_bucket_exists) {
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

