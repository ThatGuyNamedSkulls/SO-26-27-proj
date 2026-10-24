#define MAX_STRING_SIZE 40
#define MAX_PATH_SIZE 1024

// Pasta onde ficam as pastas das VMs. Inclui o número de aluno porque o /tmp
// é partilhado por todos os utilizadores do cluster (indicação do professor).
#define CLOUDIST_ROOT "/tmp/CloudIST-1117611"
#define CPULIMIT_NAME "cpulimit"        // Programa que limita o CPU das VMs (o execvp procura-o no PATH)

#define MAX_VM_TYPES           128
#define MAX_VM_ID_STRING       MAX_STRING_SIZE + 4 // 4 = "_" + 3 dígitos
#define MAX_HOSTED_VMS         128

#define MAX_RESERVATIONS       256
#define MAX_RESERVATIONS_ITEMS 32
#define MAX_RESERVATION_VMS    128
