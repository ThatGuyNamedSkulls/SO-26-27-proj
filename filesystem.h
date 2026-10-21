#ifndef FILESYSTEM__H
#define FILESYSTEM__H

#include <stddef.h>
#include <dirent.h>

/**
 * Checks whether a path exists and is a directory.
 *
 * @param path Directory path.
 *
 * @return 1 if it exists and is a directory, 0 otherwise.
 */
int path_exists(const char *path);

/**
 * Checks whether a path exists and is a regular file.
 *
 * @param path File path.
 *
 * @return 1 if it exists and is a regular file, 0 otherwise.
 */
int file_exists(const char *path);

/**
 * Converts the given path into an absolute path, resolving symbolic links,
 * relative components ('.' and '..'), and redundant separators. The resolved
 * path is copied into the provided buffer.
 *
 * @param path Path to resolve.
 * @param buffer Destination buffer where the absolute path will be stored.
 * @param size Size of the destination buffer, in bytes.
 *
 * @return 0 if the path was successfully resolved and copied to the buffer
 * @return 1 if the path could not be resolved or the buffer is too small.
 */
int absolute_path(const char *path, char *buffer, size_t size);

/**
 * Lists the entries of a directory whose name ends with ".conf", sorted
 * alphabetically.
 *
 * @param dir_path Directory to search.
 * @param entries Pointer where the array of entries is stored. Each entry and
 * the array itself must be freed by the caller with free().
 *
 * @return Number of entries found.
 * @return -1 if the directory could not be read.
 */
int list_conf_files(const char *dir_path, struct dirent ***entries);

#endif // FILESYSTEM__H