#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/inotify.h>
#include <poll.h>
#include <sys/time.h>

typedef struct Filter {
  ssize_t count;
  char **filters;
  int *idx;
} Filters;

static void handle_events(int fd, char *command);
static int add_watch(int fd, char const*work_dir);
static Filters* init_filters();

int main(int argc, char **argv) {
  char work_dir[PATH_MAX];
  if (getcwd(work_dir, PATH_MAX - 1) == NULL) {
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

  add_watch(inotify_descriptor, work_dir);
  
  struct pollfd poll_fd = {.fd = inotify_descriptor, .events = POLLIN};

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
        handle_events(inotify_descriptor, argv[1]);
      }
    }

  }

  return 0;
}

static void handle_events(int fd, char *command) {
  char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
  const struct inotify_event *event;
  ssize_t length;
  char *ptr;

  int fire = 0;
  while(1) {
    length = read(fd, buf, sizeof(buf));

    if (length == -1 && errno != EAGAIN) {
      perror("read");
      exit(EXIT_FAILURE);
    }

    if (length == 0) return;

    for(ptr = buf; ptr < buf + length; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*) ptr;
      // TODO:  Follow up watch descriptors to build a full path
      // so it will be possible to filter out some files
      printf("%s\n", event->name); 
                                    

      if (event->mask & IN_ISDIR) {
        if (event->mask & IN_CREATE) { add_watch(fd, event->name); }
      } else {
        // if (event->len) printf("%s\n", event->name); TODO: handle exceptions
        fire = 1;
      }
    }

    if (fire) {
      system(command);
      fire = 0;
    }
  }
}

static int add_watch(int fd, char const *work_dir) {
  int watch = inotify_add_watch(fd, work_dir, IN_CREATE | IN_MODIFY | IN_DELETE);
  if (watch == -1) { fprintf(stderr, "Cannot watch %s.\n", work_dir); }

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

