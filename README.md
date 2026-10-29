# Transferência de Arquivos P2P

Trabalho Prático 2 de Sistemas Distribuídos, CEFET-MG, 2026/2. Professora Michelle Hanne.

Cada peer é um processo que funciona ao mesmo tempo como servidor e cliente. O seeder divide o arquivo em blocos de tamanho fixo; os demais peers baixam os blocos dos vizinhos configurados e passam a servir cada bloco assim que ele é validado. A implementação usa C++17, sockets POSIX e threads.

- Enunciado: [docs/enunciado.md](docs/enunciado.md)
- Relatório: [docs/relatorio.pdf](docs/relatorio.pdf)
- Apresentação: [docs/apresentacao.pptx](docs/apresentacao.pptx)

## Estrutura

```
src/
  main.cpp              leitura dos argumentos e sinais
  peer.hpp, peer.cpp    servidor, downloads dos vizinhos e montagem do arquivo
  metadata.hpp/.cpp     formato P2PMETA1
  net.hpp/.cpp          envio e recepção sobre TCP
  hash.hpp/.cpp         SHA-256
  log.hpp/.cpp          log compartilhado entre threads
scripts/testes.py       testes de integração com os estudos de caso
resultados/             tempos medidos e logs de cada peer por cenário
docs/                   enunciado, relatório, gerador do relatório e apresentação
Makefile
```

## Dependências

Linux com g++ (C++17), make e OpenSSL, usado no SHA-256. Os testes usam Python 3.

```
sudo apt install build-essential libssl-dev python3
```

## Compilação

```
make
```

O executável `peer` é gerado na raiz do repositório e os objetos intermediários em `build/`. `make clean` remove os dois.

## Uso

```
./peer --port PORTA [--file ARQUIVO] [--neighbor IP:PORTA ...] [--dir PASTA] [--block BYTES] [--linger SEGUNDOS]
```

| Opção | Descrição | Padrão |
|---|---|---|
| `--port` | porta em que o peer escuta | obrigatória |
| `--file` | arquivo completo; quando informado, o peer inicia como seeder | |
| `--neighbor` | vizinho no formato IP:PORTA; pode ser repetido | |
| `--dir` | pasta onde ficam o arquivo baixado e os metadados | `downloads` |
| `--block` | tamanho do bloco em bytes, entre 256 e 1048576 (só vale para o seeder) | `1024` |
| `--linger` | segundos que um leecher continua servindo blocos depois de concluir | `30` |

Um peer sem `--file` é leecher e precisa de pelo menos um vizinho. O seeder fica ativo até receber Ctrl+C.

### Exemplo com 2 peers

```
head -c 1048576 /dev/urandom > exemplo.bin
./peer --port 5000 --file exemplo.bin --block 1024 --dir /tmp/p2p-a
./peer --port 5001 --neighbor 127.0.0.1:5000 --dir /tmp/p2p-b
sha256sum exemplo.bin /tmp/p2p-b/exemplo.bin
```

### Exemplo com 4 peers

Mesma topologia usada nos testes: A é o seeder, B conhece A, C conhece A e B, e D conhece apenas B e C. Assim, todos os blocos que D recebe vêm de outros leechers.

```
./peer --port 5000 --file exemplo.bin --dir /tmp/p2p-a
./peer --port 5001 --neighbor 127.0.0.1:5000 --dir /tmp/p2p-b
./peer --port 5002 --neighbor 127.0.0.1:5000 --neighbor 127.0.0.1:5001 --dir /tmp/p2p-c
./peer --port 5003 --neighbor 127.0.0.1:5001 --neighbor 127.0.0.1:5002 --dir /tmp/p2p-d
```

Cada comando roda em um terminal. Para D conseguir terminar, B e C precisam estar no ar, baixando ou dentro do tempo de `--linger`.

## Protocolo

Cada requisição abre uma conexão TCP e envia um comando de 1 byte. A resposta é sempre um inteiro de 32 bits sem sinal, em network byte order, com o tamanho do conteúdo, seguido do conteúdo. Tamanho zero indica que o pedido não pode ser atendido.

| Comando | Requisição | Resposta |
|---|---|---|
| `M` | `M` | metadados no formato `P2PMETA1` |
| `H` | `H` | um byte por bloco: 1 se o peer tem o bloco, 0 se não tem |
| `G` | `G` + índice do bloco (uint32) | bytes do bloco |

O arquivo de metadados é texto, uma informação por linha:

```
P2PMETA1
exemplo.bin
1048576            tamanho do arquivo em bytes
1024               tamanho do bloco
<sha256 do arquivo>
<sha256 do bloco 0>
<sha256 do bloco 1>
...
```

O leecher confere o SHA-256 de cada bloco antes de marcá-lo como disponível. Quando todos os blocos chegam, confere o SHA-256 do arquivo inteiro e renomeia o `.part` para o nome original.

## Testes

```
make
python3 scripts/testes.py
```

O script executa os cenários com 2 e 4 peers, blocos de 1024 e 4096 bytes e arquivos de 10 KB a 20 MB, todos em 127.0.0.1. Em cada cenário ele gera um arquivo aleatório, inicia os peers e compara tamanho e SHA-256 dos arquivos recebidos com o original. Os resultados ficam em:

- `resultados/resultados.csv`: tempo e integridade por cenário
- `resultados/logs/<peers>peers_<bloco>bytes_<tamanho>arquivo/<peer>.log`: log de cada peer, com a origem de cada bloco recebido

As portas são fixas (25000 a 25003), então não rode duas instâncias do script ao mesmo tempo.

## Limitações

- Apenas IPv4 numérico e um arquivo por processo, sem autenticação nem TLS.
- Não há tracker: os vizinhos são definidos na linha de comando.
- Não há retomada: se o leecher for reiniciado, o download começa do zero.
- O servidor usa `select` no socket de escuta e uma thread por conexão aceita; não é um event loop totalmente não bloqueante.
