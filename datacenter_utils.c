/**
Alterações ao código base:

1. reservas (R):
  reservation_check_items - verifica a quantidade, o id do servidor e o tipo de VM de cada item (types_lock para leitura)
  reservation_register - põe a reserva na tabela como CREATING, assim nenhuma outra tarefa pode usar o mesmo id
  reservation_allocate - fecha só os trincos dos servidores que usa, por id crescente (sem interblocagem, como nos filósofos),
  simula em cópias e só cria as VMs se couberem todas (tudo ou nada)
  reservation_remove - tira-a da tabela e liberta-a

2. execução (A):
  prepare_vm_dirs - cria /tmp/CloudIST-<nº aluno>/<res>/<vm> e copia para lá a diretoria de inputs (copy_dir)
  spawn_all_vms - um fork por VM, o filho faz chdir para a sua pasta, setrlimit (RAM e disco) e
  execvp do cpulimit -q -f -l <limite> -- <exec> (procura o cpulimit no PATH), o pai guarda o pid e marca-a RUNNING
  vm_cpu_limit - vCPU / (CPU por servidor x servidores) x cores do host x 100

3. fim das VMs:
  reaper_thread - espera com wait() por qualquer processo de VM, depois o vm_destroy devolve os recursos,
  retira a VM e, quando a reserva fica sem VMs, retira também a reserva
  dorme na variável de condição vms_changed enquanto não há VMs a correr
*/

#include "datacenter_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>

VMType *VMType_exists(DataCenter *dc, const char* type_id){
	for(size_t i = 0; i < dc->num_vm_types; i++){
		if(strncmp(type_id, dc->vm_types[i].id, MAX_STRING_SIZE) == 0){
			return &dc->vm_types[i];
		}
	}
	return NULL;
}

Reservation *find_reservation(DataCenter *dc, const char *reservation_id){
	for(size_t i = 0; i < dc->num_reservations; i++){
		if(strncmp(reservation_id, dc->reservations[i]->id, MAX_STRING_SIZE) == 0){
			return dc->reservations[i];
		}
	}
	return NULL;
}

int reservation_check_items(DataCenter *dc, Reservation *reservation) {
	size_t vms_count = 0;
	int status = 0;

	pthread_rwlock_rdlock(&dc->types_lock);

	for (size_t i = 0; i < reservation->items_count; i++) {
		ReservationItem *item = &reservation->items[i];

		if (item->amount == 0) {
			fprintf(stderr, "Reservation item must request at least one VM.\n");
			status = 1;
			break;
		}

		if (item->server_id == 0 || item->server_id > dc->num_servers) {
			fprintf(stderr, "Invalid server in reservation.\n");
			status = 1;
			break;
		}

		if (item->amount > MAX_RESERVATION_VMS - vms_count) {
			fprintf(stderr, "Reached max of VMS for a reservation.\n");
			status = 1;
			break;
		}
		vms_count += item->amount;

		// Guarda o vm_type no item para usar depois (os tipos nunca são apagados).
		item->vm_type = VMType_exists(dc, item->vm_type_id);

		if (item->vm_type == NULL) {
			fprintf(stderr, "VM of type %s does not exist\n", item->vm_type_id);
			status = 1;
			break;
		}
	}

	pthread_rwlock_unlock(&dc->types_lock);
	return status;
}

int reservation_register(DataCenter *dc, Reservation *reservation) {
	int status = 0;

	pthread_mutex_lock(&dc->reservations_lock);

	if (find_reservation(dc, reservation->id) != NULL) {
		fprintf(stderr, "Reservation with id %s already exists.\n", reservation->id);
		status = 1;
	} else if (dc->num_reservations >= MAX_RESERVATIONS) {
		fprintf(stderr, "Max number of reservations reached\n");
		status = 1;
	} else {
		reservation->state = RES_STATE_CREATING;
		dc->reservations[dc->num_reservations++] = reservation;
	}

	pthread_mutex_unlock(&dc->reservations_lock);
	return status;
}

void reservation_remove(DataCenter *dc, Reservation *reservation) {
	for (size_t i = 0; i < dc->num_reservations; i++) {
		if (dc->reservations[i] == reservation) {
			// Desloca as reservas seguintes uma posição para a esquerda.
			memmove(&dc->reservations[i],
							&dc->reservations[i + 1],
							(dc->num_reservations - i - 1) * sizeof(Reservation *));
			dc->num_reservations--;
			break;
		}
	}

	free(reservation);
}

/**
 * Junta os ids dos servidores usados por uma reserva, sem repetições e
 * ordenados por id crescente (a ordem pela qual os trincos são fechados).
 *
 * @param reservation Reserva a alocar.
 * @param ids Array onde são guardados os ids (MAX_RESERVATIONS_ITEMS posições).
 *
 * @return Número de servidores diferentes.
 */
static size_t reservation_servers(Reservation *reservation, size_t *ids) {
	size_t count = 0;

	for (size_t i = 0; i < reservation->items_count; i++) {
		size_t id = reservation->items[i].server_id;
		size_t pos = 0;

		while (pos < count && ids[pos] < id)
			pos++;

		if (pos < count && ids[pos] == id)
			continue; // Já está na lista.

		memmove(&ids[pos + 1], &ids[pos], (count - pos) * sizeof(size_t));
		ids[pos] = id;
		count++;
	}

	return count;
}

/**
 * Encontra a posição de um id de servidor no array criado por
 * reservation_servers.
 *
 * @param ids Array de ids de servidores.
 * @param count Número de ids.
 * @param id Id do servidor a procurar.
 *
 * @return Posição do id.
 */
static size_t server_index(const size_t *ids, size_t count, size_t id) {
	size_t i = 0;

	while (i < count && ids[i] != id)
		i++;

	return i;
}

/**
 * Desfaz as VMs criadas até agora pelo reservation_allocate.
 * Quem chama tem de ter os trincos dos servidores usados pela reserva.
 *
 * @param reservation Ponteiro para a reserva a desfazer.
 */
static void reservation_rollback(Reservation *reservation){
	while (reservation->num_vms > 0) {
		VM *vm = reservation->vms[reservation->num_vms - 1];

		Server *server = vm->server;

		// Devolve os recursos
		resources_add(&server->available, vm->type->required);

		// Retira a VM da lista do servidor (é sempre a última)
		server->num_hosted_vms--;

		server->hosted_vms[server->num_hosted_vms] = NULL;

		// Retira a VM da reserva
		reservation->num_vms--;

		reservation->vms[reservation->num_vms] = NULL;

		free(vm);
	}
}

int reservation_allocate(DataCenter *dc, Reservation *reservation) {
	size_t ids[MAX_RESERVATIONS_ITEMS];
	size_t num_ids = reservation_servers(reservation, ids);

	Resources available[MAX_RESERVATIONS_ITEMS];
	size_t hosted_count[MAX_RESERVATIONS_ITEMS];
	int status = 0;

	// Fecha só os trincos dos servidores usados, sempre por id crescente (sem interblocagem).
	for (size_t i = 0; i < num_ids; i++) {
		Server *server = &dc->servers[ids[i] - 1];

		pthread_mutex_lock(&server->lock);
		available[i] = server->available;
		hosted_count[i] = server->num_hosted_vms;
	}

	// 1. Simula a colocação de todas as VMs nas cópias.
	for (size_t i = 0; i < reservation->items_count && status == 0; i++) {
		ReservationItem *item = &reservation->items[i];
		size_t s = server_index(ids, num_ids, item->server_id);

		for (size_t j = 0; j < item->amount; j++) {
			if (hosted_count[s] >= MAX_HOSTED_VMS) {
				fprintf(stderr,
								"Server \"%zu\" reached the maximum number of hosted VMs.\n",
								item->server_id);
				status = 1;
				break;
			}

			if (!resources_can_fit(available[s], item->vm_type->required)) {
				fprintf(stderr, "Server \"%zu\" does not have enough resources.\n", item->server_id);
				status = 1;
				break;
			}

			resources_sub(&available[s], item->vm_type->required);
			hosted_count[s]++;
		}
	}

	// 2. Cabe tudo: cria as VMs e desconta os recursos a sério.
	for (size_t i = 0; i < reservation->items_count && status == 0; i++) {
		ReservationItem *item = &reservation->items[i];
		Server *server = &dc->servers[item->server_id - 1];

		for (size_t j = 0; j < item->amount; j++) {
			VM *new_vm = calloc(1, sizeof(VM));
			if (new_vm == NULL) {
				fprintf(stderr, "Failed to allocate VM.\n");
				reservation_rollback(reservation);
				status = 1;
				break;
			}

			snprintf(new_vm->id,
				MAX_VM_ID_STRING,
				"%s_%zu",
				reservation->id,
				reservation->num_vms + 1
			);

			new_vm->type = item->vm_type;
			new_vm->server = server;
			new_vm->state = VM_STATE_RESERVED;

			resources_sub(&server->available, item->vm_type->required);

			server->hosted_vms[server->num_hosted_vms++] = new_vm;
			reservation->vms[reservation->num_vms++] = new_vm;
		}
	}

	for (size_t i = num_ids; i-- > 0;) {
		pthread_mutex_unlock(&dc->servers[ids[i] - 1].lock);
	}

	return status;
}

int prepare_vm_dirs(Reservation *res) {
	for (size_t i = 0; i < res->num_vms; i++) {
		VM *vm = res->vms[i];

		int len = snprintf(vm->dir, sizeof(vm->dir), "%s/%s/%s", CLOUDIST_ROOT, res->id, vm->id);

		if (len < 0 || (size_t)len >= sizeof(vm->dir)) {
			fprintf(stderr, "VM folder path too long for %s.\n", vm->id);
			return 1;
		}

		if (make_dirs(vm->dir) != 0) {
			fprintf(stderr, "Failed to create folder %s.\n", vm->dir);
			return 1;
		}

		if (copy_dir(vm->type->input_folder, vm->dir) != 0) {
			fprintf(stderr, "Failed to copy %s to %s.\n", vm->type->input_folder, vm->dir);
			return 1;
		}
	}

	return 0;
}

/**
 * Calcula a percentagem do cpulimit de uma VM:
 * vCPU-da-VM / (CPUs-por-servidor x servidores) x cores-do-host x 100.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param vm VM a lançar.
 *
 * @return Percentagem de tempo de CPU (pelo menos 1).
 */
static int vm_cpu_limit(DataCenter *dc, VM *vm) {
	double total_vcpus = dc->servers[0].total.cpu * (double)dc->num_servers;
	double percent = vm->type->required.cpu / total_vcpus * dc->host_cores * 100.0;
	int limit = (int)(percent + 0.5);

	return limit < 1 ? 1 : limit;
}

/**
 * Imprime uma mensagem de erro e termina o processo filho. Usa write() e
 * _exit() porque o filho é uma cópia de um processo com várias tarefas.
 *
 * @param message Mensagem a imprimir.
 */
static void child_fail(const char *message) {
	ssize_t ignored = write(STDERR_FILENO, message, strlen(message));
	(void)ignored;
	_exit(127);
}

/**
 * Corre no processo filho depois do fork(): entra na pasta da VM, limita a
 * RAM e o disco com setrlimit e faz exec da aplicação da VM através do
 * cpulimit. Nunca retorna.
 *
 * @param vm Ponteiro para a VM a lançar.
 * @param cpu_limit Percentagem do cpulimit, em string.
 */
static void spawn_vm_child(VM *vm, char *cpu_limit) {
	// A aplicação usa caminhos relativos para os seus inputs.
	if (chdir(vm->dir) != 0)
		child_fail("Failed to enter the VM folder.\n");

	struct rlimit lim;

	// RAM (GB -> bytes), não alterável pelo processo.
	lim.rlim_cur = (rlim_t)vm->type->required.ram << 30;
	lim.rlim_max = lim.rlim_cur;
	if (setrlimit(RLIMIT_AS, &lim) != 0)
		child_fail("Failed to limit the VM RAM.\n");

	// Disco: tamanho máximo de cada ficheiro (GB -> bytes).
	lim.rlim_cur = (rlim_t)vm->type->required.disk << 30;
	lim.rlim_max = lim.rlim_cur;
	if (setrlimit(RLIMIT_FSIZE, &lim) != 0)
		child_fail("Failed to limit the VM disk.\n");

	// Por convenção o argv[0] é o nome do programa.
	char *args[] = {CPULIMIT_NAME, "-q", "-f", "-l", cpu_limit, "--", vm->type->exec_path, NULL};

	// O execvp procura o cpulimit no PATH, como o terminal faz (pode estar em /usr/bin ou em ~/bin).
	execvp(CPULIMIT_NAME, args);

	// O exec só retorna em caso de erro.
	child_fail("Failed to exec cpulimit.\n");
}

int spawn_all_vms(DataCenter *dc, Reservation *res) {
	int failed = 0;

	for (size_t i = 0; i < res->num_vms; i++) {
		VM *vm = res->vms[i];
		char cpu_limit[16];

		snprintf(cpu_limit, sizeof(cpu_limit), "%d", vm_cpu_limit(dc, vm));

		pid_t pid = fork();

		if (pid < 0) {
			fprintf(stderr, "Failed to fork VM %s.\n", vm->id);
			failed = 1;
			break;
		}

		if (pid == 0)
			spawn_vm_child(vm, cpu_limit); // Filho: nunca retorna.

		vm->pid = pid;
		vm->state = VM_STATE_RUNNING;
		dc->running_vms++;
	}

	if (failed) {
		// Destrói as VMs que não arrancaram (do fim para o início, porque o
		// vm_destroy desloca o array).
		for (size_t i = res->num_vms; i-- > 0;) {
			if (res->vms[i]->state == VM_STATE_RESERVED && vm_destroy(dc, res, res->vms[i]))
				break; // A reserva foi retirada.
		}
	}

	// Acorda a tarefa reaper.
	pthread_cond_broadcast(&dc->vms_changed);

	return failed;
}

int vm_destroy(DataCenter *dc, Reservation *res, VM *vm) {
	Server *server = vm->server;

	pthread_mutex_lock(&server->lock);

	// Devolve os recursos da VM ao servidor.
	resources_add(&server->available, vm->type->required);

	// Retira a VM da lista de VMs do servidor.
	for (size_t j = 0; j < server->num_hosted_vms; j++) {
		if (server->hosted_vms[j] == vm) {
			memmove(&server->hosted_vms[j],
							&server->hosted_vms[j + 1],
							(server->num_hosted_vms - j - 1) * sizeof(VM *));
			server->num_hosted_vms--;
			break;
		}
	}

	pthread_mutex_unlock(&server->lock);

	// Retira a VM da reserva.
	for (size_t i = 0; i < res->num_vms; i++) {
		if (res->vms[i] == vm) {
			memmove(&res->vms[i], &res->vms[i + 1], (res->num_vms - i - 1) * sizeof(VM *));
			res->num_vms--;
			break;
		}
	}

	free(vm);

	// Todas as VMs da reserva terminaram: a reserva é apagada.
	if (res->num_vms == 0) {
		reservation_remove(dc, res);
		return 1;
	}

	return 0;
}

/**
 * Procura uma VM a correr pelo pid do seu processo.
 * Quem chama tem de ter o reservations_lock.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param pid Pid devolvido pelo wait().
 * @param res_out Onde é guardada a reserva da VM.
 * @param vm_out Onde é guardada a VM.
 *
 * @return 1 se encontrar, 0 caso contrário.
 */
static int find_vm_by_pid(DataCenter *dc, pid_t pid, Reservation **res_out, VM **vm_out) {
	for (size_t i = 0; i < dc->num_reservations; i++) {
		Reservation *res = dc->reservations[i];

		for (size_t j = 0; j < res->num_vms; j++) {
			if (res->vms[j]->state == VM_STATE_RUNNING && res->vms[j]->pid == pid) {
				*res_out = res;
				*vm_out = res->vms[j];
				return 1;
			}
		}
	}

	return 0;
}

void *reaper_thread(void *arg) {
	DataCenter *dc = arg;

	while (1) {
		pthread_mutex_lock(&dc->reservations_lock);

		// Dorme enquanto não há nada para esperar.
		while (dc->running_vms == 0 && !dc->shutting_down)
			pthread_cond_wait(&dc->vms_changed, &dc->reservations_lock);

		if (dc->running_vms == 0) { // A terminar e sem VMs a correr.
			pthread_mutex_unlock(&dc->reservations_lock);
			break;
		}

		pthread_mutex_unlock(&dc->reservations_lock);

		// Bloqueia (sem ter nenhum trinco) até algum processo de VM terminar.
		int status;
		pid_t pid = wait(&status);

		pthread_mutex_lock(&dc->reservations_lock);

		if (pid < 0) {
			// Já não há filhos para esperar: na prática nada está a correr.
			fprintf(stderr, "Failed to wait for VMs.\n");
			dc->running_vms = 0;
			pthread_cond_broadcast(&dc->vms_changed);
			pthread_mutex_unlock(&dc->reservations_lock);
			continue;
		}

		Reservation *res;
		VM *vm;

		if (find_vm_by_pid(dc, pid, &res, &vm)) {
			if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
				fprintf(stderr, "VM %s exited with status %d.\n", vm->id, WEXITSTATUS(status));
			else if (WIFSIGNALED(status))
				fprintf(stderr, "VM %s was killed by signal %d.\n", vm->id, WTERMSIG(status));

			vm_destroy(dc, res, vm);
			dc->running_vms--;
			pthread_cond_broadcast(&dc->vms_changed);
		}

		pthread_mutex_unlock(&dc->reservations_lock);
	}

	return NULL;
}

const char *vm_state_to_string(VMState state) {
    switch (state) {
        case VM_STATE_RESERVED:   return "RESERVED";
        case VM_STATE_RUNNING:    return "RUNNING";
        default:                  return "UNKNOWN";
    }
}

const char *res_state_to_string(ReservationState state) {
    switch (state) {
			case RES_STATE_CREATING: return "CREATING";
			case RES_STATE_PENDING:  return "PENDING";
			case RES_STATE_RUNNING:  return "RUNNING";
			default:                 return "UNKNOWN";
    }
}
