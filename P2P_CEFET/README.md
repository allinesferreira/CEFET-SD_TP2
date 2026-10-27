# Trabalho Prático 2 — Sistema P2P (C++17/Linux)

## Dependências

Ubuntu/Debian: `sudo apt install build-essential libssl-dev python3`

## Compilação

`make`

## Execução com 2 Peers (terminais separados)

1. Crie um arquivo: `head -c 1048576 /dev/urandom > exemplo.bin`
2. Seeder: `./peer --port 5000 --file exemplo.bin --block 1024 --dir /tmp/p2p-seed`
3. Leecher: `./peer --port 5001 --neighbor 127.0.0.1:5000 --dir /tmp/p2p-b`
4. Verifique: `sha256sum exemplo.bin /tmp/p2p-b/exemplo.bin`

## Execução com 4 Peers

Inicie, em terminais distintos:

- `./peer --port 5000 --file exemplo.bin --dir /tmp/p2p-a`
- `./peer --port 5001 --neighbor 127.0.0.1:5000 --neighbor 127.0.0.1:5002 --dir /tmp/p2p-b`
- `./peer --port 5002 --neighbor 127.0.0.1:5000 --neighbor 127.0.0.1:5001 --dir /tmp/p2p-c`
- `./peer --port 5003 --neighbor 127.0.0.1:5001 --neighbor 127.0.0.1:5002 --dir /tmp/p2p-d`

**Observação:** os leechers finalizam após receber o arquivo. Para observar redistribuição por peers intermediários, use arquivo maior e inicie o Peer D enquanto B e C ainda transferem. Os vizinhos devem ser configurados estaticamente; a descoberta automática (tracker) não está implementada.

## Protocolo TCP

Cada conexão contém uma solicitação de 1 byte: `M` (metadados), `H` (mapa de blocos), `G` (bloco). Em `G`, segue um inteiro unsigned de 32 bits em network byte order com o índice. Resposta: tamanho unsigned de 32 bits em network byte order, seguido dos bytes do conteúdo. Zero indica indisponibilidade. Metadados textuais P2PMETA1: nome seguro, tamanho em bytes, tamanho de bloco, SHA-256 total, um SHA-256 por bloco. O cliente valida SHA-256 por bloco antes de publicá-lo.

## Limitações conhecidas

- Suporta IPv4 numérico e um arquivo por processo; não usa TLS ou autenticação.
- Os blocos são gravados em arquivo `.part`; não há retomada automática após reinício.
- Peers leechers encerram após o download, portanto não permanecem semeando após concluir; durante o download, cada bloco validado fica imediatamente disponível.
- Para experimentos de redistribuição, os processos devem se sobrepor temporalmente.
- A rotina de servidor usa uma thread por conexão e `select` no socket de escuta; é uma solução concorrente, não um event loop totalmente não bloqueante.
- A inicialização do leecher exige um vizinho que forneça metadados.

## Testes automatizados

Execute `python3 scripts/testes.py` após `make`. O script testa dois peers e quatro peers, verifica SHA-256 e registra resultados em `resultados.csv`. Não execute duas instâncias do script simultaneamente (portas fixas).

## Relatório

O relatório em `docs/Relatorio_P2P.pdf` contém arquitetura, decisões de implementação e metodologia. Complete a URL do repositório e utilize os resultados medidos nos testes reais antes de entregar.

### Permanência como Seeder após concluir

O parâmetro `--linger SEGUNDOS` (padrão: 30) controla por quanto tempo um Leecher concluído continua servindo blocos. Exemplo: `--linger 120`. Isso permite que outros Leechers ainda baixem blocos dos intermediários.
