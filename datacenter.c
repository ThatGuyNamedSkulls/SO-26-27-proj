/**
Alteraçoes ao codigo base:

1. Ex3:
  a ordem e sempre "reservations_lock -> servers[i].lock (por id crescente) -> types_lock -> output_lock"
  assim nunca ha um ciclo de tarefas a espera umas das outras (which is good)
  init/destroy criam e destroem os mutexes, o rwlock e a variavel de condiçao

2. D: os caminhos passam a absolutos (absolute_path) porque a VM corre dentro da sua proprio pasta,
  o types_lock e um rwlock: o D escreve, o R e o L so leem (as said on instrucoes)

3. R: verifica os itens -> regista o id como CREATING -> aloca nos servidores -> PENDING
  se a alocaçao falhar a reserva e retirada, nao fica nada reservado (fixed)

4. A: marca-a RUNNING (para so um A a arrancar), copia os inputs sem ter locks,
  depois faz fork das VMs e retorna (o ciclo de comandos nao fica bloqueado yay)

5. o configure arranca a funcao reaper, o destroy espera que todas as VMs terminem e depois para-a

6. L: tem os locks enquanto imprime para mostrar o estado, salta as reservas CREATING
*/

#include "datacenter.h"
#include "datacenter_utils.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

void datacenter_init(DataCenter *dc) {
  dc->servers = NULL;
  dc->num_servers = 0;
  dc->host_cores = 1.0;

  dc->num_vm_types = 0;

  dc->num_reservations = 0;
  dc->running_vms = 0;
  dc->shutting_down = 0;

  pthread_rwlock_init(&dc->types_lock, NULL);
  pthread_mutex_init(&dc->reservations_lock, NULL);
  pthread_cond_init(&dc->vms_changed, NULL);
  pthread_mutex_init(&dc->output_lock, NULL);

  dc->configured = 0;
}

void datacenter_destroy(DataCenter *dc) {
  if (dc->configured) {
    pthread_mutex_lock(&dc->reservations_lock);

    // Espera que todos os processos das VMs terminem.
    while (dc->running_vms > 0)
      pthread_cond_wait(&dc->vms_changed, &dc->reservations_lock);

    // Avisa a tarefa reaper para parar.
    dc->shutting_down = 1;
    pthread_cond_broadcast(&dc->vms_changed);

    pthread_mutex_unlock(&dc->reservations_lock);

    pthread_join(dc->reaper, NULL);
  }

  // As reservas que nunca foram executadas ainda sao donas das suas VMs.
  for (size_t i = 0; i < dc->num_reservations; i++) {
    Reservation *res = dc->reservations[i];

    for (size_t j = 0; j < res->num_vms; j++)
      free(res->vms[j]);

    free(res);
  }
  dc->num_reservations = 0;

  for (size_t i = 0; i < dc->num_servers; i++)
    pthread_mutex_destroy(&dc->servers[i].lock);

  free(dc->servers); // dar free na memory very important
  dc->servers = NULL;
  dc->num_servers = 0;

  pthread_rwlock_destroy(&dc->types_lock);
  pthread_mutex_destroy(&dc->reservations_lock);
  pthread_cond_destroy(&dc->vms_changed);
  pthread_mutex_destroy(&dc->output_lock);
}

int datacenter_configure(DataCenter *dc, size_t num_servers, Resources *resources, double host_cores){
  if(dc->configured){
    fprintf(stderr, "Data Center is already configured!\n");
    return 1;
  }

  // CPU e HOST_CORES sao numeros de cores, por isso tem de ser inteiros.
  if (num_servers == 0 || resources->ram == 0 || resources->disk == 0 ||
      resources->cpu <= 0.0 || !resources_cpu_is_integer(resources->cpu) ||
      host_cores <= 0.0 || !resources_cpu_is_integer(host_cores)) {
    fprintf(stderr, "Invalid configuration values.\n");
    return 1;
  }

  dc->servers = (Server*)malloc(num_servers*sizeof(Server));

  if(dc->servers == NULL){
    fprintf(stderr, "Failed to allocate memory for servers.\n");
    return 1;
  }

  for (size_t i = 0; i < num_servers; i++) {
    dc->servers[i].id = i+1;
    dc->servers[i].total = *resources;
    dc->servers[i].available = *resources;
    dc->servers[i].num_hosted_vms = 0;
    pthread_mutex_init(&dc->servers[i].lock, NULL);
  }

  dc->num_servers = num_servers;
  dc->host_cores = host_cores;

  // Tarefa que espera pelos processos das VMs e as destroi quando terminam.
  if (pthread_create(&dc->reaper, NULL, reaper_thread, dc) != 0) {
    fprintf(stderr, "Failed to create the reaper thread.\n");
    return 1;
  }

  dc->configured = 1;

  return 0;
}

int datacenter_define_VM(DataCenter *dc, VMType *type){
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  if (!file_exists(type->exec_path)) {
    fprintf(stderr, "Invalid executable path.\n");
    return 1;
  }

  if (!path_exists(type->input_folder)) {
    fprintf(stderr, "Invalid input folder.\n");
    return 1;
  }

  // As vCPUs sao um numero de CPUs virtuais (inteiro); assim as somas e
  // subtraçoes de cpu nos servidores sao sempre exatas.
  if (!resources_cpu_is_integer(type->required.cpu)) {
    fprintf(stderr, "Invalid number of vCPUs.\n");
    return 1;
  }

  // Caminhos absolutos, porque cada processo de VM corre dentro da sua pasta.
  VMType new_type = *type;

  if (absolute_path(type->exec_path, new_type.exec_path, MAX_PATH_SIZE) != 0 ||
      absolute_path(type->input_folder, new_type.input_folder, MAX_PATH_SIZE) != 0) {
    fprintf(stderr, "Invalid VM paths.\n");
    return 1;
  }

  int status = 0;

  pthread_rwlock_wrlock(&dc->types_lock);

  if(VMType_exists(dc, new_type.id) != NULL){
    fprintf(stderr, "VM type with id %s already exists.\n", new_type.id);
    status = 1;
  } else if(dc->num_vm_types >= MAX_VM_TYPES){
    fprintf(stderr, "Maximum of VM types reached.\n");
    status = 1;
  } else {
    dc->vm_types[dc->num_vm_types++] = new_type;
  }

  pthread_rwlock_unlock(&dc->types_lock);

  return status;
}

int datacenter_reserve(DataCenter *dc, Reservation *reservation) {
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  if (reservation_check_items(dc, reservation) != 0)
    return 1;

  Reservation *res = malloc(sizeof(Reservation));

  if (res == NULL) {
    fprintf(stderr, "Failed to allocate reservation.\n");
    return 1;
  }

  *res = *reservation;
  res->num_vms = 0;

  // Fica com o id para que nenhuma outra tarefa o possa usar (evita bugs).
  if (reservation_register(dc, res) != 0) {
    free(res);
    return 1;
  }

  // Se alguma VM nao couber, a reservation desaparece.
  if (reservation_allocate(dc, res) != 0) {
    pthread_mutex_lock(&dc->reservations_lock);
    reservation_remove(dc, res);
    pthread_mutex_unlock(&dc->reservations_lock);
    return 1;
  }

  pthread_mutex_lock(&dc->reservations_lock);
  res->state = RES_STATE_PENDING;
  pthread_mutex_unlock(&dc->reservations_lock);

  return 0;
}

int datacenter_execute(DataCenter *dc, const char *reservation_id) {
  if(!dc->configured){
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  pthread_mutex_lock(&dc->reservations_lock);

  Reservation *res = find_reservation(dc, reservation_id);

  if (res == NULL || res->state == RES_STATE_CREATING) {
    pthread_mutex_unlock(&dc->reservations_lock);
    fprintf(stderr, "Reservation ID %s not found.\n", reservation_id);
    return 1;
  }

  if (res->state != RES_STATE_PENDING) {
    pthread_mutex_unlock(&dc->reservations_lock);
    fprintf(stderr, "Reservation ID %s is not in a pending state.\n", reservation_id);
    return 1;
  }

  // A partir daqui nenhum outro 'A' pode arrancar esta reserva.
  res->state = RES_STATE_RUNNING;

  pthread_mutex_unlock(&dc->reservations_lock);

  // A copy pode demorar, por isso e feita sem ter nenhum lock.
  if (prepare_vm_dirs(res) != 0) {
    pthread_mutex_lock(&dc->reservations_lock);
    res->state = RES_STATE_PENDING;
    pthread_mutex_unlock(&dc->reservations_lock);
    return 1;
  }

  pthread_mutex_lock(&dc->reservations_lock);
  int status = spawn_all_vms(dc, res);
  pthread_mutex_unlock(&dc->reservations_lock);

  return status;
}

int datacenter_list(DataCenter *dc) {
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  // A mesma ordem que em todo o lado: reservas -> servidores -> tipos -> output.
  pthread_mutex_lock(&dc->reservations_lock);
  for (size_t i = 0; i < dc->num_servers; i++)
    pthread_mutex_lock(&dc->servers[i].lock);
  pthread_rwlock_rdlock(&dc->types_lock);
  pthread_mutex_lock(&dc->output_lock);

  // e agora começa as formataçoes requeridas, claude foi usado aqui para garantir que tudo
  // esta de acordo com o esperado
  printf("=================================== DATA CENTER STATUS ===================================\n");
  printf("\n[ PHYSICAL SERVERS ] (%zu total)\n", dc->num_servers);
  printf("------------------------------------------------------------------------------------------\n");

  for (size_t i = 0; i < dc->num_servers; i++) {
    Server *server = &dc->servers[i];

    Resources used = server->total;
    resources_sub(&used, server->available);

    printf(" Server #%zu\n", server->id);
    printf("   ├─ Resources Total     : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            server->total.cpu, server->total.ram, server->total.disk);
    printf("   ├─ Resources Used      : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            used.cpu, used.ram, used.disk);
    printf("   ├─ Resources Available : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            server->available.cpu, server->available.ram, server->available.disk);
    printf("   └─ Hosted VMs (%zu):\n", server->num_hosted_vms);

    if (server->num_hosted_vms == 0) {
      printf("        (None)\n");
    } else {
      for (size_t j = 0; j < server->num_hosted_vms; j++) {
        VM *vm = server->hosted_vms[j];
        const char *prefix = (j == server->num_hosted_vms - 1) ? "└──" : "├──";

        printf("        %s VM ID : %-16s | Type: %-10s | State: %-10s",
                prefix,
                vm->id,
                vm->type ? vm->type->id : "Unknown",
                vm_state_to_string(vm->state));

        if (vm->state == VM_STATE_RUNNING) {
          printf(" | PID: %d", (int)vm->pid);
        }
        printf("\n");
      }
    }
    printf("\n");
  }

  printf("[ DEFINED VM TYPES ] (%zu registered)\n", dc->num_vm_types);
  printf("------------------------------------------------------------------------------------------\n");
  if (dc->num_vm_types == 0) {
    printf(" (None)\n\n");
  } else {
      for (size_t i = 0; i < dc->num_vm_types; i++) {
        VMType *vt = &dc->vm_types[i];
        printf("  * Type ID: %-12s | Requirements: %.2f vCPU, %zu GB RAM, %zu GB Disk\n",
                vt->id, vt->required.cpu, vt->required.ram, vt->required.disk);
        printf("    ├─ Executable : %s\n", vt->exec_path);
        printf("    └─ Input Dir  : %s\n\n", vt->input_folder);
      }
      printf("\n");
  }

  // As reservas que o 'R' ainda esta a criar nao sa mostradas.
  size_t visible = 0;
  for (size_t i = 0; i < dc->num_reservations; i++) {
    if (dc->reservations[i]->state != RES_STATE_CREATING)
      visible++;
  }

  printf("[ RESERVATIONS ] (%zu total)\n", visible);
  printf("------------------------------------------------------------------------------------------\n");
  if (visible == 0) {
    printf(" (None)\n\n");
  } else {
    for (size_t i = 0; i < dc->num_reservations; i++) {
      Reservation *res = dc->reservations[i];

      if (res->state == RES_STATE_CREATING)
        continue;

      printf("  * Reservation ID: %-12s | State: %-8s | Total VMs: %zu\n",
              res->id, res_state_to_string(res->state), res->num_vms);

      printf("    ├─ Requested Pairs:\n");
      for (size_t k = 0; k < res->items_count; k++) {
        printf("    │    • %-12s : %zu unit(s)\n", res->items[k].vm_type_id, res->items[k].amount);
      }

      printf("    └─ Allocated VMs:\n");
      if (res->num_vms == 0) {
        printf("         (None)\n");
      } else {
          for (size_t k = 0; k < res->num_vms; k++) {
            VM *vm = res->vms[k];
            const char *prefix = (k == res->num_vms - 1) ? "└──" : "├──";
            printf("         %s ID: %-15s | Host Server: #%zu\n",
                    prefix,
                    vm->id,
                    vm->server->id);
          }
      }
      printf("\n");
    }
  }

  printf("================================================================--------------------------\n\n");

  pthread_mutex_unlock(&dc->output_lock);
  pthread_rwlock_unlock(&dc->types_lock);
  for (size_t i = dc->num_servers; i-- > 0;)
    pthread_mutex_unlock(&dc->servers[i].lock);
  pthread_mutex_unlock(&dc->reservations_lock);

  return 0;
}

void datacenter_wait(unsigned int delay_ms){
  struct timespec delay = (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
  nanosleep(&delay, NULL);
}

void datacenter_print(DataCenter *dc, const char *message) {
  pthread_mutex_lock(&dc->output_lock);
  printf("%s\n", message);
  pthread_mutex_unlock(&dc->output_lock);
}
