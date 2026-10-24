#ifndef FILESYSTEM__H
#define FILESYSTEM__H

#include <stddef.h>

/**
 * Verifica se um caminho existe e é uma diretoria.
 *
 * @param path Caminho da diretoria.
 *
 * @return 1 se existir e for uma diretoria, 0 caso contrário.
 */
int path_exists(const char *path);

/**
 * Verifica se um caminho existe e é um ficheiro regular.
 *
 * @param path Caminho do ficheiro.
 *
 * @return 1 se existir e for um ficheiro regular, 0 caso contrário.
 */
int file_exists(const char *path);

/**
 * Converte o caminho dado num caminho absoluto, resolvendo links simbólicos,
 * componentes relativos ('.' e '..') e separadores repetidos. O caminho
 * resolvido é copiado para o buffer dado.
 *
 * @param path Caminho a resolver.
 * @param buffer Buffer onde é guardado o caminho absoluto.
 * @param size Tamanho do buffer, em bytes.
 *
 * @return 0 se o caminho foi resolvido e copiado para o buffer.
 * @return 1 se não foi possível resolver o caminho ou o buffer é pequeno.
 */
int absolute_path(const char *path, char *buffer, size_t size);

/**
 * Lista os nomes dos ficheiros de uma diretoria que terminam em ".conf",
 * por ordem alfabética.
 *
 * @param dir_path Diretoria onde procurar.
 * @param names Ponteiro onde é guardado o array de nomes. Tem de ser
 * libertado por quem chama, com free_file_list().
 *
 * @return Número de nomes encontrados.
 * @return -1 se não foi possível ler a diretoria.
 */
int list_conf_files(const char *dir_path, char ***names);

/**
 * Liberta um array de nomes devolvido por list_conf_files().
 *
 * @param names Array de nomes.
 * @param count Número de nomes no array.
 */
void free_file_list(char **names, size_t count);

/**
 * Cria uma diretoria e todas as diretorias-mãe que faltem (como "mkdir -p").
 * Diretorias que já existem não contam como erro.
 *
 * @param path Diretoria a criar.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int make_dirs(const char *path);

/**
 * Copia recursivamente o conteúdo de uma diretoria para outra, mantendo as
 * subdiretorias e as permissões dos ficheiros. O destino já tem de existir.
 *
 * @param src Diretoria de origem.
 * @param dst Diretoria de destino.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int copy_dir(const char *src, const char *dst);

#endif // FILESYSTEM__H
