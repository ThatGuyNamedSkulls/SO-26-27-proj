#!/bin/sh
# Escreve o que a VM vê: os seus limites, o comando cpulimit que a lançou,
# a sua diretoria de trabalho e o ficheiro de input copiado.
out="${CLOUDIST_TEST_OUT:?output path is not set}"
cat /proc/self/limits > "$out"
printf 'parent=%s\n' "$(tr '\0' ' ' < /proc/$PPID/cmdline)" >> "$out"
printf 'cwd=%s\n' "$(pwd -P)" >> "$out"
printf 'payload=%s\n' "$(cat data/payload.txt)" >> "$out"
