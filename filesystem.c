/**
Changes since defualt:

1. added 
  int list_conf_files(const char *dir_path, struct dirent ***entries){
    return scandir(dir_path, entries, is_conf_file, alphasort);
}
  scandir is POSIX function that opens the directory on the pc and reads the entrys 1 by 1 (using opendir/readdir),
  then filters with is_conf_file(entry) 

*/

#define _XOPEN_SOURCE 700

#include "filesystem.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define CONF_EXTENSION ".conf"

/**
 * Filter used by scandir(): accepts entries whose name ends with ".conf".
 *
 * @param entry Directory entry being checked.
 *
 * @return 1 if the entry should be kept, 0 otherwise.
 */
static int is_conf_file(const struct dirent *entry){
  size_t name_len = strlen(entry->d_name);
  size_t ext_len = strlen(CONF_EXTENSION);

  if (name_len <= ext_len)
    return 0;

  return strcmp(entry->d_name + name_len - ext_len, CONF_EXTENSION) == 0;
}

int path_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISDIR(st.st_mode);
}

int file_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISREG(st.st_mode);
}

int absolute_path(const char *path, char *buffer, size_t size){
  char *resolved = realpath(path, NULL);

  if (resolved == NULL)
    return 1;

  if (strlen(resolved) >= size) {
    free(resolved);
    return 1;
  }

  strcpy(buffer, resolved);

  free(resolved);
  return 0;
}

int list_conf_files(const char *dir_path, struct dirent ***entries){
  return scandir(dir_path, entries, is_conf_file, alphasort);
}