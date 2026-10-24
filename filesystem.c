/**
Alteraçoes ao codigo base:

1. adicionei isto:
  int list_conf_files(const char *dir_path, char ***names), *** pq e um pointer  para array (**) mas queremosdar return
  dele na variavel names por isso mais um *
  abre a diretoria com opendir e le as entradas uma a uma com readdir (errno = 0 antes, para distinguir erro de fim)
  so fica com as que fazem is_conf_file(name) devolver 1, copia cada nome com malloc + strcpy
  ordena-os com qsort + strcmp (compare_names)
  devolve o numero de nomes (ou -1 se der erro) e escreve o array em *names
  free_file_list(names, count) liberta tudo no fim

2. adicionei:
  make_dirs(path) - como o "mkdir -p", chama mkdir para cada nivel do caminho, se a pasta ja existir (path_exists) nao é erro
  copy_file(src, dst, mode) - copia com open/read/write, o write pode escrever menos por isso repete
  copy_dir(src, dst) - recursivo, o stat diz se é uma pasta (mkdir + copy_dir) ou um ficheiro (copy_file)
*/

#define _XOPEN_SOURCE 700

#include "filesystem.h"

#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "constants.h"

#define CONF_EXTENSION ".conf"
#define COPY_BUFFER_SIZE 4096
// Permissoes como nos slides (SF2): rwx para o dono, r-x para o grupo e para os restantes.
#define DIR_PERMISSIONS (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
// Os 9 bits de permissoes (rwx do dono, grupo e restantes), sem o tipo do ficheiro.
#define PERMISSIONS_MASK (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | \
                          S_IROTH | S_IWOTH | S_IXOTH)

/**
 * Verifica se um nome de ficheiro termina em ".conf".
 *
 * @param name Nome do ficheiro a verificar.
 *
 * @return 1 se o nome terminar em ".conf", 0 caso contrario.
 */
static int is_conf_file(const char *name){
  size_t name_len = strlen(name);
  size_t ext_len = strlen(CONF_EXTENSION);

  if (name_len <= ext_len)
    return 0; /** ".conf" sozinho ou nomes curtos sao rejeitados */

  return strcmp(name + name_len - ext_len, CONF_EXTENSION) == 0; /** aponta para os ultimos 5 caracteres do nome e ve se sao ".conf" */
}

/**
 * Compara dois nomes de ficheiros. Usada pelo qsort() para os ordenar
 * alfabeticamente.
 *
 * @param a Ponteiro para o primeiro nome (um char **).
 * @param b Ponteiro para o segundo nome (um char **).
 *
 * @return Negativo, zero ou positivo, como o strcmp().
 */
static int compare_names(const void *a, const void *b){
  const char *const *name_a = a;
  const char *const *name_b = b;

  return strcmp(*name_a, *name_b);
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

void free_file_list(char **names, size_t count){
  for (size_t i = 0; i < count; i++)
    free(names[i]);

  free(names);
}

int list_conf_files(const char *dir_path, char ***names){
  DIR *dir = opendir(dir_path);
  if (dir == NULL)
    return -1;

  char **list = NULL;
  size_t count = 0;
  size_t capacity = 0;
  int failed = 0;
  struct dirent *entry;

  while (1) {
    errno = 0; // Para distinguir o tipo de error
    entry = readdir(dir);

    if (entry == NULL) {
      if (errno != 0)
        failed = 1;
      break;
    }

    if (!is_conf_file(entry->d_name))
      continue;

    // Aumenta o array quando esta cheio.
    if (count == capacity) {
      capacity = (capacity == 0) ? 8 : capacity * 2;
      char **bigger = realloc(list, capacity * sizeof(char *));

      if (bigger == NULL) {
        failed = 1;
        break;
      }
      list = bigger;
    }

    // O d_name pertence ao readdir() e é reescrito na chamada seguinte, por isso copia-se.
    list[count] = malloc(strlen(entry->d_name) + 1);
    if (list[count] == NULL) {
      failed = 1;
      break;
    }
    strcpy(list[count], entry->d_name);
    count++;
  }

  closedir(dir);

  if (failed) {
    free_file_list(list, count);
    return -1;
  }

  qsort(list, count, sizeof(char *), compare_names);

  *names = list;
  return (int)count;
}

int make_dirs(const char *path){
  char buffer[MAX_PATH_SIZE];

  if (strlen(path) >= sizeof(buffer))
    return 1;

  strcpy(buffer, path);

  // Cria cada diretoria-mae, cortando o caminho em cada '/'.
  for (char *p = buffer + 1; *p != '\0'; p++) {
    if (*p != '/')
      continue;

    *p = '\0';
    if (mkdir(buffer, DIR_PERMISSIONS) != 0 && !path_exists(buffer))
      return 1;
    *p = '/';
  }

  if (mkdir(buffer, DIR_PERMISSIONS) != 0 && !path_exists(buffer))
    return 1;

  return 0;
}

/**
 * Junta uma diretoria e um nome em "<dir>/<nome>".
 *
 * @param buffer Buffer de destino.
 * @param size Tamanho do buffer, em bytes.
 * @param dir Caminho da diretoria.
 * @param name Nome a acrescentar.
 *
 * @return 0 em caso de sucesso.
 * @return 1 se o resultado nao couber no buffer.
 */
static int join_path(char *buffer, size_t size, const char *dir, const char *name){
  int len = snprintf(buffer, size, "%s/%s", dir, name);

  return (len < 0 || (size_t)len >= size) ? 1 : 0;
}

/**
 * Copia um ficheiro regular, criando o destino ou esvaziando-o se ja existir.
 *
 * @param src Caminho do ficheiro a copiar.
 * @param dst Caminho do ficheiro novo.
 * @param mode Permissoes do ficheiro novo.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
static int copy_file(const char *src, const char *dst, mode_t mode){
  // O_CLOEXEC: uma VM criada entretanto por outra tarefa nao herda estes ficheiros.
  int in = open(src, O_RDONLY | O_CLOEXEC);
  if (in < 0)
    return 1;

  int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
  if (out < 0) {
    close(in);
    return 1;
  }

  char buffer[COPY_BUFFER_SIZE];
  ssize_t bytes_read;
  int status = 0;

  while (status == 0 && (bytes_read = read(in, buffer, sizeof(buffer))) > 0) {
    // O write() pode escrever menos bytes do que os pedidos, por isso continua a escrever o resto.
    ssize_t written = 0;

    while (written < bytes_read) {
      ssize_t bytes_written = write(out, buffer + written, (size_t)(bytes_read - written));

      if (bytes_written < 0) {
        status = 1;
        break;
      }

      written += bytes_written;
    }
  }

  if (bytes_read < 0)
    status = 1;

  close(in);
  close(out);
  return status;
}

int copy_dir(const char *src, const char *dst){
  DIR *dir = opendir(src);
  if (dir == NULL)
    return 1;

  struct dirent *entry;
  int status = 0;

  while (status == 0 && (entry = readdir(dir)) != NULL) {
    // Salta as entradas da propria diretoria e da diretoria-mae.
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char src_path[MAX_PATH_SIZE];
    char dst_path[MAX_PATH_SIZE];

    if (join_path(src_path, sizeof(src_path), src, entry->d_name) != 0 ||
        join_path(dst_path, sizeof(dst_path), dst, entry->d_name) != 0) {
      status = 1;
      break;
    }

    struct stat st;
    if (stat(src_path, &st) != 0) {
      status = 1;
      break;
    }

    if (S_ISDIR(st.st_mode)) {
      if (mkdir(dst_path, st.st_mode & PERMISSIONS_MASK) != 0 && !path_exists(dst_path))
        status = 1;
      else
        status = copy_dir(src_path, dst_path);
    } else if (S_ISREG(st.st_mode)) {
      status = copy_file(src_path, dst_path, st.st_mode & PERMISSIONS_MASK);
    }
  }

  closedir(dir);
  return status;
}
