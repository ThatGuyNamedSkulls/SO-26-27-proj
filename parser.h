#ifndef PARSER__H
#define PARSER__H

#include <stddef.h>

#include "datacenter.h"

typedef enum {
  CMD_DEFINE,
  CMD_RESERVE,
  CMD_EXECUTE,
  CMD_LIST,
  CMD_WAIT,
  CMD_HELP,
  CMD_EMPTY,
  CMD_INVALID,
  EOC // Fim dos comandos
} Command;

/**
 * Converte a string (terminada em '\0') num inteiro decimal sem sinal.
 * A conversão só tem sucesso se a string for toda consumida e não houver
 * overflow nem erro de conversão.
 *
 * @param str String a converter.
 * @param out Ponteiro onde é guardado o valor convertido.
 *
 * @return 0 se a conversão tiver sucesso.
 * @return 1 caso contrário.
 */
int parse_size_t_arg(const char *str, size_t *out);

/**
 * Converte a string (terminada em '\0') num número de vírgula flutuante.
 * A conversão só tem sucesso se a string for toda consumida e não houver
 * overflow nem erro de conversão.
 *
 * @param str String a converter.
 * @param out Ponteiro onde é guardado o valor convertido.
 *
 * @return 0 se a conversão tiver sucesso.
 * @return 1 caso contrário.
 */
int parse_double_arg(const char *str, double *out);

/**
 * Lê uma linha e devolve o comando correspondente.
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @return Comando lido.
 */
Command get_next_command(int fd);

/**
 * Lê os argumentos de um comando D (definir).
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param vm Ponteiro onde é guardado o tipo de VM.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int parse_define(int fd, VMType *vm);

/**
 * Lê os argumentos de um comando R (reservar).
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param reservation Ponteiro onde são guardados os dados da reserva.
 * @param max_pairs Número máximo de pares <TIPO_VM> <QTD> <SERVIDOR> a ler.
 *
 * @return número de pares lidos (devolve 0 em caso de erro).
 */
size_t parse_reserve(int fd, Reservation *reservation, size_t max_pairs);

/**
 * Lê os argumentos de um comando A (arrancar).
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param id String onde é guardado o id da reserva a executar.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int parse_execute(int fd, char *id);

/**
 * Lê os argumentos de um comando E (esperar).
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param delay Tempo de espera que vai ser aplicado.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int parse_wait(int fd, unsigned int *delay);

#endif // PARSER__H
