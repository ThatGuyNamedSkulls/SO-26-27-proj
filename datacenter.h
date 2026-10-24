#ifndef DATACENTER__H
#define DATACENTER__H

#include <pthread.h>
#include <sys/types.h>

#include "resources.h"
#include "constants.h"
#include "filesystem.h"

/*
 * Resumo da sincronizaçao (os locks sao sempre adquiridos por esta ordem):
 *
 *   1. reservations_lock  - tabela de reservas, estado das reservas, parent id/estado
 *                           das VMs e running_vms.
 *   2. servers[i].lock    - recursos e lista de VMs do servidor i. Quando sao
 *                           precisos varos, fecham-se por id crescente.
 *   3. types_lock         - trinco de leitura-escrita da tabela de tipos de VM.
 *   4. output_lock        - stdout, para as mensagens e as listagens nao se misturarem.
 */

typedef struct VMType{
  char id[MAX_STRING_SIZE];          // Id do tipo de VM
  char input_folder[MAX_PATH_SIZE];  // Diretoria com os inputs do executavel (caminho absoluto)
  char exec_path[MAX_PATH_SIZE];     // Caminho do executavel (caminho absoluto)
  Resources required;
} VMType;

typedef enum {
  VM_STATE_RESERVED,   // colocada num servidor, processo ainda nao lançado (a espera do comando 'A')
  VM_STATE_RUNNING     // processo lançado (fork()/exec() feitos), o `pid` e valido
} VMState;

typedef struct Server Server;

typedef struct VM {
  char id[MAX_VM_ID_STRING]; // Id da VM
  VMType *type;              // Tipo da VM
  Server *server;            // Servidor onde a VM esta
  pid_t pid;                 // PID do processo da VM
  VMState state;             // Estado da VM
  char dir[MAX_PATH_SIZE];   // Pasta da VM
} VM;

typedef struct Server {
	size_t id;		       // Id do servidor (1, 2, 3, ...)
	Resources total;     // Recursos que o servidor tem.
	Resources available; // Recursos ainda disponiveis.

	VM *hosted_vms[MAX_HOSTED_VMS];
    size_t num_hosted_vms;

	pthread_mutex_t lock; // Protege available e hosted_vms.
} Server;

typedef enum {
    RES_STATE_CREATING,  // o 'R' ainda a esta a alocar (invisivel para 'A' e 'L')
    RES_STATE_PENDING,   // Reservada com 'R', a espera de 'A'
    RES_STATE_RUNNING    // Ja foi feito 'A', as suas VMs estao a correr
} ReservationState;

typedef struct ReservationPair {
    char vm_type_id[MAX_STRING_SIZE]; // Tipo de VM. ex: "V1"
    size_t amount;                    // Quantidade de VMs deste tipo a reservar.
    size_t server_id;                 // Servidor onde cada VM vai correr.

    VMType *vm_type;                   // Usado para nao voltar a procurar o tipo.
} ReservationItem;

typedef struct Reservation {
	char id[MAX_STRING_SIZE];                       // id da reserva.

	ReservationItem items[MAX_RESERVATIONS_ITEMS];  // Lista de itens da reserva
    size_t items_count;

	VM *vms[MAX_RESERVATION_VMS];                   // Lista das VMs desta reserva.
	size_t num_vms;                                 // Numero de VMs desta reserva.
	ReservationState state;                         // Estado da reserva.
} Reservation;

typedef struct DataCenter {
	Server *servers;			     // Lista de servidores.
	size_t num_servers;              // Numero de servidores.
	double host_cores;               // Cores reais usados para simular todo o centro de dados.

	VMType vm_types[MAX_VM_TYPES];
    size_t num_vm_types;
	pthread_rwlock_t types_lock;     // Protege vm_types e num_vm_types.

	Reservation *reservations[MAX_RESERVATIONS];
    size_t num_reservations;
	size_t running_vms;              // Numero de processos de VMs ainda nao recolhidos com wait.
	int shutting_down;               // Fica a 1 quando a tarefa reaper tem de parar.
	pthread_mutex_t reservations_lock;
	pthread_cond_t vms_changed;      // Assinalada quando running_vms muda ou ao terminar.

	pthread_t reaper;                // Tarefa que espera pelos processos das VMs.
	pthread_mutex_t output_lock;     // Protege o stdout.

	int configured;		      // Se o centro de dados ja foi configurado ou nao.
} DataCenter;

/**
 * Inicializa um centro de dados.
 *
 * @param dc Centro de dados a inicializar.
 */
void datacenter_init(DataCenter *dc);

/**
 * Destroi um centro de dados. Espera que todas as VMs terminem, para a
 * tarefa reaper e liberta toda a memoria.
 *
 * @param dc Centro de dados a destruir.
 */
void datacenter_destroy(DataCenter *dc);

/**
 * Configura um centro de dados e arranca a tarefa reaper.
 *
 * @param dc Ponteiro para o centro de dados a configurar.
 * @param num_servers Numero de servidores.
 * @param resources Ponteiro para os recursos de cada servidor.
 * @param host_cores Cores reais disponiveis para simular o centro de dados.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int datacenter_configure(DataCenter *dc, size_t num_servers, Resources *resources, double host_cores);

/**
 * Define um tipo de VM no centro de dados.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param type Ponteiro para o tipo de VM.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int datacenter_define_VM(DataCenter *dc, struct VMType *type);

/**
 * Reserva um conjunto de VMs. Ou se reservam todas as VMs ou nenhuma.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param reservation Ponteiro para a reserva (e copiada).
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int datacenter_reserve(DataCenter *dc, Reservation *reservation);

/**
 * Mostra o estado atual do centro de dados.
 *
 * @param dc Ponteiro para o centro de dados.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int datacenter_list(DataCenter *dc);

/**
 * Executa uma reserva, lançando um processo por VM. Retorna logo depois de
 * lançar os processos; a tarefa reaper destroi cada VM quando o seu processo
 * termina.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param reservation_id Id da reserva a executar.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int datacenter_execute(DataCenter *dc, const char *reservation_id);

/**
 * Executa o comando de espera.
 *
 * @param delay_ms Tempo de espera em ms.
 */
void datacenter_wait(unsigned int delay_ms);

/**
 * Imprime uma mensagem no stdout sem a misturar com o output das outras
 * tarefas.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param message Mensagem a imprimir (e acrescentada uma mudança de linha).
 */
void datacenter_print(DataCenter *dc, const char *message);

#endif // DATACENTER__H
