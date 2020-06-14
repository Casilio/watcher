#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/inotify.h>
#include <poll.h>
#include <time.h>

#define UNUSED(a) a

// TODO: Come up with a more meaningful name
typedef struct Context {
  int fire;
  char work_dir[PATH_MAX];
  char tags_file[PATH_MAX];
} Context;

Context context = {0};

static void handle_events(int fd, int wd, Context context, char *command);

int main(int argc, char **argv) {
  if (getcwd(context.work_dir, PATH_MAX - 1) == NULL) {
    perror("Couldn't determine current directory\n");
    exit(EXIT_FAILURE);
  }
  
  strcpy(context.tags_file, context.work_dir);
  strcat(context.tags_file, "/tags");

  if (argc <= 1) {
    perror("Command required\n");
    exit(EXIT_FAILURE);
  }
 
  // fire on startup
  context.fire = 1;

  int inotify_descriptor = inotify_init1(IN_NONBLOCK);
  if (inotify_descriptor == -1) {
    perror("Inotify init failed. Exiting");
    exit(EXIT_FAILURE);
  }

  int watch_descriptor = inotify_add_watch(inotify_descriptor, context.work_dir, IN_CREATE | IN_MODIFY | IN_DELETE);
  if (watch_descriptor == -1) {
    fprintf(stderr, "Cannot watch %s. Exiting.\n", context.work_dir);
    exit(EXIT_FAILURE);
  }
  
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
        handle_events(inotify_descriptor, watch_descriptor, context, argv[1]);
      }
    }

  }

  return 0;
}

static void handle_events(int fd, int wd, Context context, char *command) {
  char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
  const struct inotify_event *event;
  ssize_t length;
  char *ptr;
  time_t last_fire = time(0);

  while(1) {
    length = read(fd, buf, sizeof(buf));

    if (length == -1 && errno != EAGAIN) {
      perror("read");
      exit(EXIT_FAILURE);
    }

    if (length == 0) break;

    for(ptr = buf; ptr < buf + length; ptr += sizeof(struct inotify_event) + event->len) {
      event = (const struct inotify_event*) ptr;
      if (event->mask & IN_ISDIR) continue;

      // if (event->len) printf("%s\n", event->name); TODO: handle exceptions
      context.fire = 1;
    }

    if (context.fire && time(0) - last_fire > 1) {
      system(command);
      context.fire = 0;
      last_fire = time(0);
    }
  }
}

