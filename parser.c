#include "parser.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>

#include "constants.h"
#include "datacenter.h"

/**
 * Salta espaços até encontrar um carácter que não seja espaço.
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param first Ponteiro onde é guardado o primeiro carácter que não é espaço.
 *
 * @return 0 em caso de sucesso.
 * @return -1 em caso de falha.
 */
static int skip_spaces(int fd, char *first){
  ssize_t bytes_read = 0;
  char ch;

  while (1) {
    bytes_read = read(fd, &ch, 1);

    if (bytes_read <= 0)
      return -1;

    if (ch != ' '){
      *first = ch;
      break;
    }
  }
  return 0;
}

/**
 * Lê uma string de um descritor de ficheiro.
 *
 * Pára quando encontra um espaço ou uma mudança de linha.
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param buffer Buffer onde se guarda a string.
 * @param max Número máximo de bytes a ler.
 * @param delimiter Ponteiro onde é guardado o carácter que terminou a string.
 *
 * @return 0 em caso de sucesso.
 * @return -1 em caso de erro ou fim de ficheiro.
 */
static int read_string(int fd, char *buffer, size_t max, char *delimiter){
  char ch;
  size_t i = 0;

  if (skip_spaces(fd, &ch) != 0) {
    return -1;
  }

  while (1) {
    if (ch == ' ' || ch == '\n') {
      *delimiter = ch;
      break;
    }

    if (i >= max - 1) {
      return -1;
    }

    buffer[i++] = ch;

    if (read(fd, &ch, 1) != 1) {
      *delimiter = '\0';
      break;
    }
  }

  buffer[i] = '\0';

  return 0;
}

/**
 * Lê um valor size_t de um descritor de ficheiro.
 *
 * Lê caracteres até encontrar um que não seja dígito (ou o fim do ficheiro)
 * e converte-os para size_t.
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param value Ponteiro onde é guardado o valor.
 * @param delimiter Ponteiro onde é guardado o carácter que terminou o número.
 *
 * @return 0 em caso de sucesso.
 * @return 1 se o valor for maior que SIZE_MAX.
 * @return -1 em caso de erro.
 */
static int read_size_t(int fd, size_t *value, char *delimiter) {
  char buf[32];
  size_t i = 0;
  char ch;

  if (skip_spaces(fd, &ch) != 0) {
    return -1;
  }

  while (1) {
    if (ch < '0' || ch > '9') {
      *delimiter = ch;
      break;
    }

    if (i >= sizeof(buf) - 1) {
      return -1;
    }

    buf[i++] = ch;

    if (read(fd, &ch, 1) != 1) {
      *delimiter = '\0';
      break;
    }
  }

  if (i == 0) {
    return -1;
  }

  buf[i] = '\0';

  errno = 0;
  char *end;
  unsigned long long tmp = strtoull(buf, &end, 10);

  if (errno != 0 || *end != '\0' || tmp > SIZE_MAX) {
    return -1;
  }

  *value = (size_t)tmp;

  return 0;
}

/**
 * Lê um valor de vírgula flutuante de um descritor de ficheiro.
 *
 * Salta os espaços iniciais, lê o próximo token até um espaço, mudança de
 * linha ou fim de ficheiro, e converte-o para double.
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param value Ponteiro onde é guardado o valor lido.
 * @param delimiter Ponteiro onde é guardado o carácter que terminou o número.
 *
 * @return 0 em caso de sucesso.
 * @return -1 se não foi possível ler um valor válido.
 */
static int read_double(int fd, double *value, char *delimiter){
  char buffer[64];

  if (read_string(fd, buffer, sizeof(buffer), delimiter) != 0)
    return -1;

  errno = 0;
  char *endptr;

  *value = strtod(buffer, &endptr);

  if (errno != 0 || *endptr != '\0')
    return -1;

  return 0;
}

/**
 * Lê a especificação de recursos.
 *
 * Lê três valores: RAM, disco e cores.
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param resources Ponteiro onde são guardados os recursos.
 * @param delimiter Ponteiro onde é guardado o último carácter lido.
 *
 * @return 0 em caso de sucesso.
 * @return -1 em caso de erro.
 */
static int parse_resources(int fd, Resources *resources, char *delimiter) {
  if (read_size_t(fd, &resources->ram, delimiter) != 0 || *delimiter != ' ') {
    return -1;
  }

  if (read_size_t(fd, &resources->disk, delimiter) != 0 || *delimiter != ' ') {
    return -1;
  }

  if (read_double(fd, &resources->cpu, delimiter) != 0 || resources->cpu <= 0.0) {
    return -1;
  }

  return 0;
}

/**
 * Descarta o resto da linha atual.
 *
 * Lê e ignora caracteres até encontrar uma mudança de linha.
 *
 * @param fd Descritor de ficheiro de onde se lê.
 * @param last_char Último carácter lido do fd.
 */
static void cleanup(int fd, char last_char) {
  char ch;

  // O parser consome os delimitadores, por isso fazemos esta verificação.
  if(last_char == '\n' || last_char == '\0'){
    return;
  }

  while (read(fd, &ch, 1) == 1 && ch != '\n')
    ;
}

/**
 * Converte um carácter num comando.
 *
 * @param ch Carácter que representa um comando.
 *
 * @return O comando correspondente.
 * @return CMD_INVALID se o carácter não for um comando.
 */
static Command char_to_command(char ch){
  switch (ch) {
    case 'D':
      return CMD_DEFINE;
    case 'R':
      return CMD_RESERVE;
    case 'A':
      return CMD_EXECUTE;
    case 'E':
      return CMD_WAIT;
    case 'L':
      return CMD_LIST;
    case 'H':
      return CMD_HELP;
    default:
      return CMD_INVALID;
  }
}

static int command_has_arguments(Command command){
  return command != CMD_LIST && command != CMD_HELP;
}

/**
 * Verifica se o carácter a seguir ao comando é válido.
 *
 * @param command Comando a verificar.
 * @param delimiter Carácter a seguir ao comando.
 *
 * @return 0 se for válido.
 * @return -1 caso contrário.
 */
static int validate_command_delimiter(Command command, char delimiter){
  if (command_has_arguments(command)) {
    return delimiter == ' ' ? 0 : -1;
  }

  return (delimiter == '\n' || delimiter == '\0') ? 0 : -1;
}

int parse_size_t_arg(const char *str, size_t *out) {
  char *end;
  errno = 0;

  if (str == NULL || str[0] == '\0' || str[0] == '-')
    return 1;

  unsigned long long value = strtoull(str, &end, 10);

  if (errno != 0 || *end != '\0')
    return 1;

  *out = (size_t)value;
  return 0;
}

int parse_double_arg(const char *str, double *out) {
  char *end;
  errno = 0;

  double value = strtod(str, &end);

  if (errno != 0 || *end != '\0')
    return 1;

  *out = value;
  return 0;
}

Command get_next_command(int fd){
  char ch;
  char delimiter;

  if (read(fd, &ch, 1) != 1) {
    return EOC;
  }

  if (ch == '#') {
    cleanup(fd, ch);
    return CMD_EMPTY;
  }

  if (ch == '\n') {
    return CMD_EMPTY;
  }

  Command command = char_to_command(ch);

  if (command == CMD_INVALID) {
    cleanup(fd, ch);
    return CMD_INVALID;
  }

  ssize_t bytes = read(fd, &delimiter, 1);

  if (bytes == 0) {
    delimiter = '\0';
  } else if (bytes < 0) {
    return CMD_INVALID;
  }

  if (validate_command_delimiter(command, delimiter) != 0) {
    cleanup(fd, delimiter);
    return CMD_INVALID;
  }

  return command;
}

int parse_define(int fd, VMType *vm){
  char delimiter;
  VMType tmp;

  // Lê o id do tipo de VM.
  if(read_string(fd, tmp.id, MAX_STRING_SIZE, &delimiter) != 0 || delimiter != ' '){
    cleanup(fd, delimiter);
    return 1;
  }

  // Lê a diretoria de inputs.
  if(read_string(fd, tmp.input_folder, MAX_PATH_SIZE, &delimiter) != 0 || delimiter != ' '){
    cleanup(fd, delimiter);
    return 1;
  }

  // Lê o caminho do executável.
  if(read_string(fd, tmp.exec_path, MAX_PATH_SIZE, &delimiter) != 0 || delimiter != ' '){
    cleanup(fd, delimiter);
    return 1;
  }

  // Lê os recursos da VM.
  if(parse_resources(fd, &tmp.required, &delimiter) != 0 || (delimiter != '\n' && delimiter != '\0')) {
    cleanup(fd, delimiter);
    return 1;
  }

  *vm = tmp;
  return 0;
}

size_t parse_reserve(int fd, Reservation *reservation, size_t max_pairs){
  char delimiter;
  Reservation tmp = {0};

  // Lê o id da reserva.
  if(read_string(fd, tmp.id, MAX_STRING_SIZE, &delimiter) != 0 || delimiter != ' '){
    cleanup(fd, delimiter);
    return 0;
  }

  // Lê os grupos <TIPO_VM> <QTD> <SERVIDOR>.
  tmp.items_count = 0;
  while(tmp.items_count < max_pairs){
    if(read_string(fd, tmp.items[tmp.items_count].vm_type_id, MAX_STRING_SIZE, &delimiter) != 0 || delimiter != ' '){
      cleanup(fd, delimiter);
      return 0;
    }

    if(read_size_t(fd, &tmp.items[tmp.items_count].amount, &delimiter) != 0){
      cleanup(fd, delimiter);
      return 0;
    }

    if(read_size_t(fd, &tmp.items[tmp.items_count].server_id, &delimiter) != 0){
      cleanup(fd, delimiter);
      return 0;
    }

    tmp.items_count++;

    // Verifica se os grupos acabaram.
    if(delimiter == '\n' || delimiter == '\0'){
      break;
    }
  }

  if (tmp.items_count == max_pairs && delimiter != '\n' && delimiter != '\0') {
    cleanup(fd, delimiter);
    return 0;
  }

  *reservation = tmp;
  return reservation->items_count;
}

int parse_execute(int fd, char *id){
  char delimiter;
  char buf[MAX_STRING_SIZE];

  if(read_string(fd, buf, MAX_STRING_SIZE, &delimiter) != 0 || (delimiter != '\n' && delimiter != '\0')){
    cleanup(fd, delimiter);
    return 1;
  }

  strncpy(id, buf, MAX_STRING_SIZE);
  return 0;
}

int parse_wait(int fd, unsigned int *delay){
  char delimiter;
  size_t tmp;

  if(read_size_t(fd, &tmp, &delimiter) != 0 || (delimiter != '\n' && delimiter != '\0')){
    cleanup(fd, delimiter);
    return 1;
  }

  *delay = (unsigned int)tmp;
  return 0;
}
