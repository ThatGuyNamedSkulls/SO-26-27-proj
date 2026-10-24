/**
Alterações ao código base:

1. adicionei isto:
	Mudei a organização da função main, separei-a em 3 funções diferentes (contando com a main)
	o parser dos comandos foi para process_comands(dc, fd). Tive de mudar o case EOC também porque
	o fim de um ficheiro já não é o fim do programa. Parece-me bem porque não gostava da
	main gigante, apesar de o process_comands também ainda ser bem grande mas tá ok

	process_file junta o caminho completo à dir; verifica o tamanho com o snprintf, que por si só
	devolve quantos chars queria escrever, se len >= size, ignora; depois temos o open(path, rdonly),
	que fecha no fim, e o free_file_list(names, num_files) que liberta a memória alocada pelo list_conf_files

2. worker threads (Ex3):
	os nomes ordenados dos ficheiros vão para uma FileQueue, cada worker tira o próximo ficheiro (next_file, protegido por um mutex)
	até não haver mais, assim no máximo MAX_THREADS ficheiros são processados ao mesmo tempo
	com 1 thread os ficheiros continuam a ser processados por ordem alfabética
	as mensagens passam pelo datacenter_print para o output das threads não se misturar

3. args: <SERVERS> <RAM> <DISK> <CPU> <HOST_CORES> <INPUT_DIR> <MAX_THREADS> (como nos testes públicos)
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "parser.h"
#include "datacenter.h"
#include "filesystem.h"
#include "constants.h"

/**
 * Lista de ficheiros ".conf" partilhada pelas worker threads.
 */
typedef struct {
	DataCenter *dc;
	const char *input_dir;
	char **names;              // Nomes dos ficheiros, ordenados.
	size_t num_files;
	size_t next_file;          // Índice do próximo ficheiro a processar.
	pthread_mutex_t lock;      // Protege o next_file.
} FileQueue;

/**
 * Lê e executa todos os comandos de um descritor de ficheiro até chegar ao
 * fim dos comandos.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param fd Descritor de ficheiro de onde se leem os comandos.
 */
static void process_commands(DataCenter *dc, int fd){
	while(1){
		switch (get_next_command(fd)){
			case CMD_DEFINE: {
				VMType vmtype;

				if (parse_define(fd, &vmtype) != 0) {
					fprintf(stderr, "Invalid define command. See H (help) for usage.\n");
					continue;
				}

				if(datacenter_define_VM(dc, &vmtype) != 0){
					fprintf(stderr, "Failed to define VM.\n");
					continue;
				}

				datacenter_print(dc, "VM successfully defined!");

				break;
			}

			case CMD_RESERVE: {
				Reservation reservation = {0};

				size_t num_items = parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);

				if (num_items == 0) {
					fprintf(stderr, "Invalid reserve command. See H (help) for usage.\n");
					continue;
				}

				if (datacenter_reserve(dc, &reservation) != 0) {
					fprintf(stderr, "Failed to reserve VMs.\n");
					continue;
				}

				datacenter_print(dc, "Reservation made successfully!");

				break;
			}

			case CMD_EXECUTE: {
				char id[MAX_STRING_SIZE];

				if(parse_execute(fd, id) != 0){
					fprintf(stderr, "Invalid execute command. See H (help) for usage.\n");
					continue;
				}

				if (datacenter_execute(dc, id) != 0) {
					fprintf(stderr, "Failed to execute reservation.\n");
					continue;
				}

				datacenter_print(dc, "Finished reservation execution!");

				break;
			}

			case CMD_LIST:
				if (datacenter_list(dc) != 0) {
					fprintf(stderr, "Failed to list VMs.\n");
					continue;
				}

				break;

			case CMD_WAIT: {
				unsigned int delay;

				if(parse_wait(fd, &delay) != 0){
					fprintf(stderr, "Invalid wait command. See H (help) for usage.\n");
					continue;
				}

				datacenter_wait(delay);
				break;
			}

			case CMD_INVALID:
				fprintf(stderr, "Invalid Command. See H (help) for usage.\n");
				break;

			case CMD_HELP:
				datacenter_print(dc,
					"Spaces between arguments are allowed, but not after command end.\n"
					"Available commands:\n"
					" D <VM_TYPE_ID> <INPUT_FOLDER> <EXECUTABLE_PATH> <RAM_NEEDED> <DISK_NEEDED> <VCPU_NEEDED_COUNT>\n"
					" R <RESERVATION_ID> [<VM_TYPE_ID> <COUNT> <SERVER_ID>]+\n"
					" A <RESERVATION_ID>\n"
					" L\n"
					" E <DELAY_MS>\n"
					" H"
				);
				break;

			case CMD_EMPTY:
				break;

			case EOC:
				return;
		}
	}
}

/**
 * Abre um ficheiro ".conf" e executa os seus comandos.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param input_dir Diretoria onde está o ficheiro.
 * @param name Nome do ficheiro.
 */
static void process_file(DataCenter *dc, const char *input_dir, const char *name){
	char path[MAX_PATH_SIZE];
	int len = snprintf(path, sizeof(path), "%s/%s", input_dir, name);

	if (len < 0 || (size_t)len >= sizeof(path) || !file_exists(path)) {
		fprintf(stderr, "Skipping invalid input file %s.\n", path);
		return;
	}

	// O_CLOEXEC: os processos das VMs criados com fork/exec não ficam com este ficheiro aberto.
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "Failed to open input file %s.\n", path);
		return;
	}

	process_commands(dc, fd);

	close(fd);
}

/**
 * Worker thread: vai tirando o próximo ficheiro da fila até não haver mais.
 *
 * @param arg Ponteiro para a FileQueue partilhada.
 *
 * @return NULL.
 */
static void *worker_thread(void *arg){
	FileQueue *queue = arg;

	while (1) {
		pthread_mutex_lock(&queue->lock);

		if (queue->next_file >= queue->num_files) {
			pthread_mutex_unlock(&queue->lock);
			break;
		}

		size_t file_index = queue->next_file++;

		pthread_mutex_unlock(&queue->lock);

		// O ficheiro é processado sem ter o trinco da fila.
		process_file(queue->dc, queue->input_dir, queue->names[file_index]);
	}

	return NULL;
}

/**
 * Processa todos os ficheiros ".conf" de uma diretoria, usando no máximo
 * max_threads worker threads. Os ficheiros são distribuídos por ordem
 * alfabética.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param input_dir Diretoria com os ficheiros ".conf".
 * @param max_threads Número máximo de worker threads.
 *
 * @return 0 em caso de sucesso.
 * @return 1 se não foi possível ler a diretoria.
 */
static int process_input_dir(DataCenter *dc, const char *input_dir, size_t max_threads){
	char **names;
	int num_files = list_conf_files(input_dir, &names);

	if (num_files < 0) {
		fprintf(stderr, "Failed to read input directory %s.\n", input_dir);
		return 1;
	}

	FileQueue queue = {
		.dc = dc,
		.input_dir = input_dir,
		.names = names,
		.num_files = (size_t)num_files,
		.next_file = 0
	};
	pthread_mutex_init(&queue.lock, NULL);

	// Não vale a pena ter mais threads do que ficheiros.
	size_t num_threads = max_threads < queue.num_files ? max_threads : queue.num_files;
	pthread_t *threads = NULL;
	size_t created = 0;

	if (num_threads > 0)
		threads = malloc(num_threads * sizeof(pthread_t));

	if (threads != NULL) {
		for (size_t i = 0; i < num_threads; i++) {
			if (pthread_create(&threads[i], NULL, worker_thread, &queue) != 0) {
				fprintf(stderr, "Failed to create worker thread.\n");
				break;
			}
			created++;
		}
	}

	// Se não foi possível criar nenhuma thread, a tarefa principal faz o trabalho.
	if (created == 0)
		worker_thread(&queue);

	for (size_t i = 0; i < created; i++)
		pthread_join(threads[i], NULL);

	free(threads);
	pthread_mutex_destroy(&queue.lock);
	free_file_list(names, queue.num_files);
	return 0;
}

int main(int argc, char **argv){
	DataCenter dc;
	datacenter_init(&dc);

	if (argc != 8) {
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <host_cores> <input_dir> <max_threads>\n", argv[0]);
    return 1;
  }

	size_t servers;
	size_t ram;
	size_t disk;
	double cpu;
	double host_cores;
	size_t max_threads;
	const char *input_dir = argv[6];

	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0 ||
			parse_double_arg(argv[5], &host_cores) != 0 ||
			parse_size_t_arg(argv[7], &max_threads) != 0 ||
			max_threads == 0) {
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}

	if (!path_exists(input_dir)) {
		fprintf(stderr, "Invalid input directory.\n");
		return 1;
	}

	Resources resources = {
    .ram = ram,
    .disk = disk,
    .cpu = cpu
	};

	if(datacenter_configure(&dc, servers, &resources, host_cores) != 0){
		fprintf(stderr, "Failed to configure Data Center.\n");
		return 1;
	}

	int status = process_input_dir(&dc, input_dir, max_threads);

	// Espera pelas VMs que estão a correr antes de libertar tudo.
	datacenter_destroy(&dc);
	return status;
}
