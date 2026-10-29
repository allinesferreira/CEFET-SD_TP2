#!/bin/bash
# uso: scripts/demo.sh [tamanho do arquivo em bytes] [tamanho do bloco em bytes]
set -u
cd "$(dirname "$0")/.."

TAMANHO=${1:-20971520}
BLOCO=${2:-1024}
HOST=127.0.0.1
PASTA=$(mktemp -d /tmp/p2p_demo.XXXXXX)
declare -A PID

encerrar() {
    for nome in "${!PID[@]}"; do
        kill -INT "${PID[$nome]}" 2>/dev/null
    done
    wait 2>/dev/null
    PID=()
}
trap 'encerrar; rm -rf "$PASTA"' EXIT

falhar() {
    echo "FALHA: $1"
    exit 1
}

esperar() {
    local nome=$1 condicao=$2 limite=$((SECONDS + 120))
    until eval "$condicao"; do
        kill -0 "${PID[$nome]}" 2>/dev/null || falhar "peer $nome encerrou: $(tail -1 "$PASTA/$nome.log")"
        [ $SECONDS -lt $limite ] || falhar "tempo esgotado esperando o peer $nome"
        sleep 0.05
    done
}

iniciar() {
    local nome=$1 porta=$2
    shift 2
    ./peer --ip $HOST --port "$porta" --dir "$PASTA/$nome" "$@" 2> "$PASTA/$nome.log" &
    PID[$nome]=$!
    esperar "$nome" "grep -q 'Servidor ouvindo' '$PASTA/$nome.log'"
}

[ -x ./peer ] || falhar "executável peer não encontrado; rode make antes"

echo "Arquivo de $TAMANHO bytes em blocos de $BLOCO bytes; 4 peers em $HOST:5000-5003"
echo "A é o seeder; B conhece A; C conhece A e B; D conhece só B e C"
head -c "$TAMANHO" /dev/urandom > "$PASTA/arquivo.bin"

iniciar a 5000 --file "$PASTA/arquivo.bin" --block "$BLOCO"
esperar a "grep -q 'Seeder ativo' '$PASTA/a.log'"
iniciar b 5001 --neighbor $HOST:5000
iniciar c 5002 --neighbor $HOST:5000 --neighbor $HOST:5001
iniciar d 5003 --neighbor $HOST:5001 --neighbor $HOST:5002
for nome in b c d; do
    esperar "$nome" "[ -f '$PASTA/$nome/arquivo.bin' ]"
done

original=$(sha256sum < "$PASTA/arquivo.bin" | cut -d' ' -f1)
echo
echo "== Integridade, tamanho e metadados"
for nome in b c d; do
    arquivo=$PASTA/$nome/arquivo.bin
    [ "$(sha256sum < "$arquivo" | cut -d' ' -f1)" = "$original" ] || falhar "SHA-256 de $nome diferente do original"
    [ "$(stat -c %s "$arquivo")" = "$TAMANHO" ] || falhar "tamanho de $nome diferente do original"
    cmp -s "$PASTA/a/arquivo.bin.meta" "$PASTA/$nome/arquivo.bin.meta" || falhar "metadados de $nome diferentes"
    echo "$nome: SHA-256 igual ao original, $TAMANHO bytes, metadados iguais aos do seeder"
done

echo
echo "== Status dos blocos de D"
grep -E 'STATUS|SUCESSO' "$PASTA/d.log" | sed 's/ SHA256=[0-9a-f]*//'

encerrar
echo
echo "== Origem dos blocos de cada leecher"
for nome in b c d; do
    echo "$nome: $(grep -o 'RESUMO origem=[^ ]* blocos=[0-9]*' "$PASTA/$nome.log" | sed 's/RESUMO //' | tr '\n' ' ')"
done
grep -q "RESUMO origem=$HOST:5000 " "$PASTA/d.log" && falhar "D recebeu blocos do seeder, que não é seu vizinho"

echo
echo "Demonstração concluída: os três leechers remontaram o arquivo, e D o recebeu inteiro de outros leechers."
