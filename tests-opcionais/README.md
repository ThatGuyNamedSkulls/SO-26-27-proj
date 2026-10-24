# Testes opcionais

Testes adicionais aos testes públicos, para cenários que estes não cobrem.

## Como executar

A partir da raiz do repositório (é preciso ter o `cpulimit` instalado):

```sh
./tests-opcionais/run.sh
```

O script compila o `cloudIST`, corre cada cenário numa diretoria temporária e
imprime `PASSED`/`FAILED` por teste e um resumo no fim.

## Cenários

| Teste | O que verifica |
|---|---|
| `limits_and_cwd` | O processo da VM corre dentro de `/tmp/CloudIST-1117611/<reserva>/<vm>`, vê a cópia da diretoria de inputs, tem `RLIMIT_AS` = RAM e `RLIMIT_FSIZE` = disco (via `setrlimit`) e é lançado com `cpulimit -l <vCPU / (CPU x SERVERS) x HOST_CORES x 100>`. |
| `all_or_nothing` | Uma reserva que não cabe não reserva nenhuma VM nem consome recursos; a reserva seguinte que cabe é aceite. |
| `no_overbooking` | 16 threads a reservar em simultâneo no mesmo servidor (capacidade para 4 VMs): exatamente 4 reservas têm sucesso (repetido 15 vezes). |
| `unique_reservation_id` | 8 threads a criar em simultâneo a reserva `SAME`: só uma tem sucesso (repetido 15 vezes). |
| `resources_returned` | Enquanto a VM corre os recursos estão ocupados; quando termina são devolvidos, a reserva é apagada e uma nova reserva cabe. |
| `parallel_vms` | O comando `A` não bloqueia, as VMs de uma reserva correm em paralelo, cada VM é destruída quando termina e a reserva só desaparece depois da última. |

Os executáveis usados pelas VMs estão em `workers/` e a diretoria de inputs em
`input/`.
