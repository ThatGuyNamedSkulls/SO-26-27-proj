#ifndef RESOURCES__H
#define RESOURCES__H

#include <stddef.h>

typedef struct {
    size_t ram;  // RAM em GB
    size_t disk; // Espaço em disco em GB
    double cpu;  // Número de cores/vCPUs
} Resources;

/**
 * Verifica se um valor de CPU (double) está suficientemente perto de um
 * inteiro, tendo em conta os erros de precisão dos números de vírgula
 * flutuante.
 *
 * @param cpu Valor de CPU a verificar.
 *
 * @return 1 se o valor for inteiro (dentro da precisão permitida),
 * @return 0 caso contrário.
 */
int resources_cpu_is_integer(double cpu);

/**
 * Verifica se um conjunto de recursos chega para satisfazer outro.
 *
 * @param available Recursos disponíveis.
 * @param required Recursos necessários.
 *
 * @return 1 se os recursos disponíveis chegarem, 0 caso contrário.
 */
int resources_can_fit(Resources available, Resources required);

/**
 * Soma um conjunto de recursos a outro.
 *
 * @param dst Conjunto de recursos a atualizar.
 * @param src Recursos a somar.
 */
void resources_add(Resources *dst, Resources src);

/**
 * Subtrai um conjunto de recursos a outro.
 *
 * Assume que dst tem pelo menos os recursos de src.
 *
 * @param dst Conjunto de recursos a atualizar.
 * @param src Recursos a subtrair.
 */
void resources_sub(Resources *dst, Resources src);

#endif
