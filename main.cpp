#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <ftw.h>
#include <time.h>
#include <string.h>

#define UNUSED(a) a

// TODO: Come up with a more meaningful name
typedef struct Context {
  int fire;
  time_t last_fire;
  char work_dir[PATH_MAX];
  char tags_file[PATH_MAX];
} Context;

Context context = {0};

int process_file(const char *fpath, const struct stat *sb, int typeflag) {
  UNUSED(fpath);
  UNUSED(typeflag);

  if (sb->st_mtime > context.last_fire) {
    if (strcmp(fpath, context.tags_file) != 0) { 
      printf("Triggered by: %s\n", fpath);
      context.fire = 1;
      return 1;
    }
  }

  return 0;
}

int main(int argc, char **argv) {
  getcwd(context.work_dir, PATH_MAX - 1);

  if (!context.work_dir) {
    fprintf(stderr, "Couldn't determine current directory\n");
    return 1;
  }
  
  strcpy(context.tags_file, context.work_dir);
  strcat(context.tags_file, "/tags");

  if (argc <= 1) {
    fprintf(stderr, "Command required\n");
    return 1;
  }
 
  // fire on startup
  context.fire = 1;
  context.last_fire = time(0);

  while(1) {
    // TODO: inotify ???
    ftw(context.work_dir, process_file, 20);

    if (context.fire) {
      time(&context.last_fire);
      system(argv[1]);
      context.fire = 0;
    }

    sleep(1);
  }

  return 0;
}

