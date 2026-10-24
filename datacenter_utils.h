#ifndef DATACENTER_UTILS__H
#define DATACENTER_UTILS__H

#include "datacenter.h"

/**
 * Verifica se existe um tipo de VM com o id dado.
 * Quem chama tem de ter o types_lock (para leitura ou escrita).
 *
 * @param dc Ponteiro para o centro de dados.
 * @param type_id Id do tipo de VM a procurar.
 *
 * @return Ponteiro para o VMType, se existir.
 * @return NULL se o tipo nao existir.
 */
VMType *VMType_exists(DataCenter *dc, const char* type_id);

/**
 * Procura uma reserva pelo id (em qualquer estado).
 * Quem chama tem de ter o reservations_lock.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param reservation_id Id da reserva a procurar.
 *
 * @return Ponteiro para a reserva, ou NULL se nao existir.
 */
Reservation *find_reservation(DataCenter *dc, const char *reservation_id);

/**
 * Verifica os parametros de cada item da reserva (quantidade, id do
 * servidor e tipo de VM) e guarda em cada item o seu VMType. Fecha o
 * types_lock para leitura.
 *
 * @param dc Centro de dados onde a reserva vai ser feita.
 * @param reservation Reserva a verificar.
 *
 * @return 0 se todos os itens forem validos.
 * @return 1 caso contrario (o erro e impresso).
 */
int reservation_check_items(DataCenter *dc, Reservation *reservation);

/**
 * Acrescenta uma reserva no estado RES_STATE_CREATING a tabela de reservas,
 * para que nenhuma outra tarefa possa usar o mesmo id. Fecha o
 * reservations_lock.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param reservation Reserva (alocada com malloc).
 *
 * @return 0 em caso de sucesso.
 * @return 1 se o id ja existir ou a tabela estiver cheia.
 */
int reservation_register(DataCenter *dc, Reservation *reservation);

/**
 * Retira uma reserva da tabela de reservas e liberta-a.
 * Quem chama tem de ter o reservations_lock.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param reservation Reserva a retirar.
 */
void reservation_remove(DataCenter *dc, Reservation *reservation);

/**
 * Fecha os trincos dos servidores usados pela reserva (por id crescente),
 * verifica que todas as VMs pedidas cabem e, so se couberem, cria-as e
 * desconta os seus recursos. Tudo ou nada.
 *
 * @param dc Centro de dados onde a reserva e feita.
 * @param reservation Reserva a alocar.
 *
 * @return 0 se todas as VMs foram alocadas.
 * @return 1 caso contrario (nada e alterado).
 */
int reservation_allocate(DataCenter *dc, Reservation *reservation);

/**
 * Cria a pasta de cada VM da reserva (CLOUDIST_ROOT/<id_reserva>/<id_vm>),
 * guarda o seu caminho na VM e copia recursivamente para la a diretoria de
 * inputs do tipo da VM.
 *
 * @param res Ponteiro para a reserva cujas pastas sao preparadas.
 *
 * @return 0 em caso de sucesso.
 * @return 1 em caso de erro.
 */
int prepare_vm_dirs(Reservation *res);

/**
 * Faz um fork por cada VM da reserva. Cada filho limita os seus recursos e
 * faz exec da aplicaçao da VM atraves do cpulimit.
 * Quem chama tem de ter o reservations_lock.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param res Ponteiro para a reserva cujas VMs vao ser lançadas.
 *
 * @return 0 em caso de sucesso.
 * @return 1 se algum fork() falhar (as VMs que nao arrancaram sao destruidas).
 */
int spawn_all_vms(DataCenter *dc, Reservation *res);

/**
 * Destroi uma VM: devolve os recursos ao servidor, retira-a do servidor e
 * da reserva, e liberta-a. Se a reserva ficar sem VMs, tambem e retirada.
 * Quem chama tem de ter o reservations_lock.
 *
 * @param dc Ponteiro para o centro de dados.
 * @param res Reserva da VM.
 * @param vm VM a destruir.
 *
 * @return 1 se a reserva tambem foi retirada, 0 caso contrario.
 */
int vm_destroy(DataCenter *dc, Reservation *res, VM *vm);

/**
 * Tarefa reaper: espera (wait()) pelos processos das VMs e destroi cada VM
 * assim que o seu processo termina. Dorme na variavel de condiçao
 * vms_changed enquanto nao ha VMs a correr.
 *
 * @param arg Ponteiro para o centro de dados.
 *
 * @return NULL.
 */
void *reaper_thread(void *arg);

/**
 * Converte o estado de uma VM numa string legivel. Usada pelo
 * datacenter_list().
 *
 * @param state O VMState a converter.
 *
 * @return String estatica que descreve o estado.
 */
const char *vm_state_to_string(VMState state);

/**
 * Converte o estado de uma reserva numa string legivel. Usada pelo
 * datacenter_list().
 *
 * @param state O ReservationState a converter.
 *
 * @return String estatica que descreve o estado.
 */
const char *res_state_to_string(ReservationState state);

#endif // DATACENTER_UTILS__H
