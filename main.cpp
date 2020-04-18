#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <ftw.h>
#include <time.h>
#include <string.h>

#define UNUSED(a) a

int fire = 0;
time_t check_time = 0;
char current_dir[PATH_MAX];
char tags_file[PATH_MAX];

int process_file(const char *fpath, const struct stat *sb, int typeflag) {
  UNUSED(fpath);
  UNUSED(typeflag);

  if (sb->st_mtime > check_time) {
    if (strcmp(fpath, tags_file) != 0) { 
      fire = 1;
      return(1);
    }
  }

  return(0);
}

int main(int argc, char **argv) {
  getcwd(current_dir, PATH_MAX - 1);

  if (!current_dir) {
    fprintf(stderr, "Couldn't determine current directory\n");
    return(1);
  }
  
  strcpy(tags_file, current_dir);
  strcat(tags_file, "/tags");

  if (argc <= 1) {
    fprintf(stderr, "Command required\n");
    return(1);
  }
 
  check_time = time(0);
  system(argv[1]);
  while(1) {
    ftw(current_dir, process_file, 20);

    if (fire) {
      time(&check_time);
      system(argv[1]);
      fire = 0;
    }

    sleep(1);
  }

  return(0);
}

