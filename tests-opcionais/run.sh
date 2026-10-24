#!/usr/bin/env bash
# Testes opcionais do cloudIST. Correr a partir da raiz do repositório:
#   ./tests-opcionais/run.sh
set -uo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
cd "$repo_root"

if ! make >/dev/null; then
  echo "não foi possível compilar o cloudIST"
  exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

passed=0
failed=0

result() { # result <nome> <0|1> <mensagem>
  if [[ "$2" -eq 0 ]]; then
    echo "$1: PASSED"
    passed=$((passed + 1))
  else
    echo "$1: FAILED: $3"
    failed=$((failed + 1))
  fi
}

new_conf_dir() {
  rm -rf "$work/conf"
  mkdir -p "$work/conf"
}

W=tests-opcionais/workers
IN=tests-opcionais/input

# 1. Cada VM corre dentro da sua pasta, com limites de RAM/disco (setrlimit) e a
#    percentagem do cpulimit vCPU / (CPU x SERVERS) x HOST_CORES x 100.
#    2 servidores x 4 CPUs, 4 cores do host, VM com 2 vCPUs -> 2/8 x 4 x 100 = 100.
new_conf_dir
cat >"$work/conf/limits.conf" <<EOF
D VLIM $IN $W/limits.sh 2 3 2
R RLIM VLIM 1 2
A RLIM
E 1500
EOF
export CLOUDIST_TEST_OUT="$work/limits.out"
./cloudIST 2 8 8 4 4 "$work/conf" 1 >"$work/out1" 2>&1
ok=0
grep -q 'Max address space *2147483648 *2147483648' "$work/limits.out" 2>/dev/null || ok=1
grep -q 'Max file size *3221225472 *3221225472' "$work/limits.out" 2>/dev/null || ok=1
grep -q -- '-l 100 --' "$work/limits.out" 2>/dev/null || ok=1
grep -q '^cwd=/tmp/CloudIST/RLIM/RLIM_1$' "$work/limits.out" 2>/dev/null || ok=1
grep -q '^payload=optional fixture$' "$work/limits.out" 2>/dev/null || ok=1
result "limits_and_cwd" "$ok" "limites de RAM/disco, percentagem do cpulimit, pasta ou input copiado diferentes do esperado"

# 2. Uma reserva que não cabe não deixa nada reservado (tudo ou nada).
new_conf_dir
cat >"$work/conf/a.conf" <<EOF
D V2 $IN $W/sleep_short.sh 2 1 1
R BIG V2 1 1 V2 2 1
L
R SMALL V2 2 1
L
EOF
./cloudIST 1 4 10 4 1 "$work/conf" 1 >"$work/out2" 2>&1
ok=0
first=$(awk '/DATA CENTER STATUS/{n++} n==1' "$work/out2")
grep -q 'Failed to reserve VMs' "$work/out2" || ok=1
grep -q 'Hosted VMs (0):' <<<"$first" || ok=1
grep -q '\[ RESERVATIONS \] (0 total)' <<<"$first" || ok=1
grep -q 'Resources Available :   4.00 vCPUs |   4 GB RAM' <<<"$first" || ok=1
[[ $(grep -c 'Reservation made successfully!' "$work/out2") -eq 1 ]] || ok=1
result "all_or_nothing" "$ok" "uma reserva falhada deixou VMs ou recursos ocupados"

# 3. Muitas threads a reservar no mesmo servidor nunca ocupam mais do que ele tem.
#    Servidor com 4 GB de RAM, 16 ficheiros a pedir cada um 1 VM com 1 GB.
ok=0
for run in $(seq 1 15); do
  new_conf_dir
  for i in $(seq -w 1 16); do
    printf 'D T%s %s %s/sleep_short.sh 1 1 1\nR RES%s T%s 1 1\n' "$i" "$IN" "$W" "$i" "$i" >"$work/conf/$i.conf"
  done
  ./cloudIST 1 4 100 100 1 "$work/conf" 16 >"$work/out3" 2>&1
  n=$(grep -c 'Reservation made successfully!' "$work/out3")
  if [[ "$n" -ne 4 ]]; then
    ok=1
    break
  fi
done
result "no_overbooking" "$ok" "esperava exatamente 4 reservas com sucesso, houve ${n:-?}"

# 4. Duas threads não conseguem criar duas reservas com o mesmo id.
ok=0
for run in $(seq 1 15); do
  new_conf_dir
  for i in $(seq -w 1 8); do
    printf 'D U%s %s %s/sleep_short.sh 1 1 1\nR SAME U%s 1 1\n' "$i" "$IN" "$W" "$i" >"$work/conf/$i.conf"
  done
  ./cloudIST 1 100 100 100 1 "$work/conf" 8 >"$work/out4" 2>&1
  n=$(grep -c 'Reservation made successfully!' "$work/out4")
  if [[ "$n" -ne 1 ]]; then
    ok=1
    break
  fi
done
result "unique_reservation_id" "$ok" "esperava exatamente 1 reserva SAME, houve ${n:-?}"

# 5. Enquanto uma VM corre os seus recursos estão ocupados; quando termina são
#    devolvidos e a reserva é apagada, por isso cabe uma reserva nova.
new_conf_dir
cat >"$work/conf/a.conf" <<EOF
D VFULL $IN $W/sleep_short.sh 2 1 1
R FIRST VFULL 1 1
A FIRST
R SECOND VFULL 1 1
E 1500
R THIRD VFULL 1 1
L
EOF
./cloudIST 1 2 10 4 1 "$work/conf" 1 >"$work/out5" 2>&1
ok=0
[[ $(grep -c 'Reservation made successfully!' "$work/out5") -eq 2 ]] || ok=1
[[ $(grep -c 'Failed to reserve VMs' "$work/out5") -eq 1 ]] || ok=1
grep -q 'Reservation ID: THIRD' "$work/out5" || ok=1
! grep -q 'Reservation ID: FIRST' "$work/out5" || ok=1
result "resources_returned" "$ok" "os recursos não foram devolvidos quando a VM terminou"

# 6. O 'A' não bloqueia: duas VMs correm em paralelo; cada VM é destruída quando
#    termina e a reserva só desaparece depois da última.
new_conf_dir
cat >"$work/conf/a.conf" <<EOF
D VS $IN $W/sleep_short.sh 1 1 1
D VL $IN $W/sleep_long.sh 1 1 1
R TWO VS 1 1 VL 1 1
A TWO
L
E 1300
L
E 2000
L
EOF
start=$(date +%s%N)
./cloudIST 1 4 10 4 1 "$work/conf" 1 >"$work/out6" 2>&1
elapsed_ms=$(( ($(date +%s%N) - start) / 1000000 ))
ok=0
l1=$(awk '/DATA CENTER STATUS/{n++} n==1' "$work/out6")
l2=$(awk '/DATA CENTER STATUS/{n++} n==2' "$work/out6")
l3=$(awk '/DATA CENTER STATUS/{n++} n==3' "$work/out6")
[[ $(grep -c 'State: RUNNING' <<<"$l1" | head -1) -ge 2 ]] || ok=1
grep -q 'Hosted VMs (1):' <<<"$l2" || ok=1
grep -q 'Reservation ID: TWO' <<<"$l2" || ok=1
grep -q 'Hosted VMs (0):' <<<"$l3" || ok=1
grep -q '\[ RESERVATIONS \] (0 total)' <<<"$l3" || ok=1
(( elapsed_ms < 4500 )) || ok=1
result "parallel_vms" "$ok" "as VMs não correram em paralelo ou não foram destruídas uma a uma"

echo
echo "$passed passaram, $failed falharam"
[[ "$failed" -eq 0 ]]
