# cloudIST

Este repositório contém a implementação do projeto cloudIST de Sistemas Operativos.
O programa simula um centro de dados que gere servidores, tipos de VM, reservas e a execução de VMs.

## Utilização

```sh
make
./cloudIST <SERVERS> <RAM> <DISK> <CPU> <HOST_CORES> <INPUT_DIR> <MAX_THREADS>
```

- `HOST_CORES`: cores reais de CPU usados para simular todo o centro de dados
  (servem para calcular a percentagem do `cpulimit`).
- `INPUT_DIR`: diretoria cujos ficheiros `.conf` são processados (por ordem alfabética).
- `MAX_THREADS`: número máximo de worker threads a processar ficheiros `.conf`.

É preciso ter o `cpulimit` instalado (`sudo apt install cpulimit`), em
`/usr/bin`, `/usr/local/bin` ou `/bin`.

## Estrutura do projeto

### Ficheiros de código

- `main.c`: ponto de entrada do programa, leitura dos argumentos, worker
  threads e ciclo de comandos de cada ficheiro `.conf`.
- `parser.c` / `parser.h`: leitura dos valores da linha de comandos e dos
  comandos lidos de um descritor de ficheiro.
- `datacenter.c` / `datacenter.h`: configuração do centro de dados, definição
  de tipos de VM, reservas, execução, listagem e esperas. O header documenta
  os trincos e a ordem pela qual são adquiridos.
- `datacenter_utils.c` / `datacenter_utils.h`: validação das reservas e
  alocação tudo-ou-nada, pastas das VMs, `fork`/`exec` das VMs com `cpulimit`
  e `setrlimit`, e a tarefa reaper que destrói cada VM quando o seu processo
  termina.
- `resources.c` / `resources.h`: operações sobre os recursos (CPU, RAM e disco).
- `filesystem.c` / `filesystem.h`: verificações no sistema de ficheiros,
  caminhos absolutos, listagem dos ficheiros `.conf` e cópia recursiva de
  diretorias.
- `constants.h`: limites usados pelo centro de dados, reservas, tipos de VM e
  identificadores.

### Testes

- `tests-opcionais/`: testes adicionais (`./tests-opcionais/run.sh`, ver o README da pasta).
