# Transferência de Arquivos P2P

Trabalho Prático 2 de Sistemas Distribuídos, CEFET-MG, 2026/2. Professora Michelle Hanne.

Cada peer é um processo que funciona ao mesmo tempo como servidor e cliente. O seeder divide o arquivo em blocos de tamanho fixo; os demais peers baixam os blocos dos vizinhos configurados e passam a servir cada bloco assim que ele é validado. A implementação usa C++17 e sockets POSIX.

- Enunciado: [docs/enunciado.md](docs/enunciado.md)
- Relatório: [docs/relatorio.pdf](docs/relatorio.pdf)
- Apresentação: [docs/apresentacao.pptx](docs/apresentacao.pptx)

## Estrutura

```
src/
  main.cpp              leitura dos argumentos e sinais
  peer.hpp, peer.cpp    respostas aos vizinhos, downloads e montagem do arquivo
  server.hpp/.cpp       servidor não bloqueante com poll()
  protocol.hpp/.cpp     codificação das requisições e respostas
  metadata.hpp/.cpp     formato P2PMETA1
  net.hpp/.cpp          endereços e envio/recepção sobre TCP
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
./peer --ip IP --port PORTA [--file ARQUIVO] [--neighbor IP:PORTA ...] [--dir PASTA] [--block BYTES] [--linger SEGUNDOS]
```

| Opção | Descrição | Padrão |
|---|---|---|
| `--ip` | endereço IPv4 do próprio peer, onde o servidor escuta | obrigatória |
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
./peer --ip 127.0.0.1 --port 5000 --file exemplo.bin --block 1024 --dir /tmp/p2p-a
./peer --ip 127.0.0.1 --port 5001 --neighbor 127.0.0.1:5000 --dir /tmp/p2p-b
sha256sum exemplo.bin /tmp/p2p-b/exemplo.bin
```

### Exemplo com 4 peers

Mesma topologia usada nos testes: A é o seeder, B conhece A, C conhece A e B, e D conhece apenas B e C. Assim, todos os blocos que D recebe vêm de outros leechers.

```
./peer --ip 127.0.0.1 --port 5000 --file exemplo.bin --dir /tmp/p2p-a
./peer --ip 127.0.0.1 --port 5001 --neighbor 127.0.0.1:5000 --dir /tmp/p2p-b
./peer --ip 127.0.0.1 --port 5002 --neighbor 127.0.0.1:5000 --neighbor 127.0.0.1:5001 --dir /tmp/p2p-c
./peer --ip 127.0.0.1 --port 5003 --neighbor 127.0.0.1:5001 --neighbor 127.0.0.1:5002 --dir /tmp/p2p-d
```

Cada comando roda em um terminal. Para D conseguir terminar, B e C precisam estar no ar, baixando ou dentro do tempo de `--linger`.

## Funcionamento

Cada processo tem duas partes que rodam ao mesmo tempo:

- **Servidor:** uma única thread com sockets não bloqueantes e `poll()`. Ela aceita conexões, lê as requisições conforme os bytes chegam e envia as respostas sem esperar por nenhum cliente específico. Conexões paradas por mais de 5 s são fechadas.
- **Downloads:** uma thread por vizinho. Cada uma consulta o mapa de blocos do vizinho e pede os blocos que ainda faltam. Um vetor `claimed` evita que duas threads baixem o mesmo bloco, e o vetor `have` registra os blocos já gravados e validados. Os dois são protegidos por um mutex.

Assim que um bloco entra em `have`, o servidor já o entrega para quem pedir, mesmo antes do arquivo estar completo.

## Protocolo

Cada requisição abre uma conexão TCP. Ela começa com um comando de 1 byte seguido do endereço de quem pede (IPv4 em 4 bytes e porta em 2 bytes), que o servidor usa nos logs. A resposta é sempre um inteiro de 32 bits sem sinal com o tamanho do conteúdo, seguido do conteúdo. Tamanho zero indica que o pedido não pode ser atendido. Todos os inteiros estão em network byte order.

| Comando | Requisição | Resposta |
|---|---|---|
| `M` | `M` + endereço | metadados no formato `P2PMETA1` |
| `H` | `H` + endereço | um byte por bloco: 1 se o peer tem o bloco, 0 se não tem |
| `G` | `G` + endereço + índice do bloco (uint32) | bytes do bloco |

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

## Logs

Cada peer escreve o log no stderr, uma mensagem por linha. Toda linha começa com o tempo em segundos desde o início do processo, por exemplo `[   0.152] RECEBIDO bloco=3 origem=127.0.0.1:5000 progresso=4/1024`.

| Linha | Significado |
|---|---|
| `SEED` / `LEECH` | papel do peer, nome do arquivo, quantidade de blocos e endereço |
| `META recebida de` | vizinho que forneceu os metadados |
| `RECEBIDO bloco=i origem=X progresso=n/total` | bloco validado e gravado, com o vizinho de onde veio |
| `ENVIADO metadados`, `ENVIADO mapa`, `ENVIADO bloco=i` | requisição atendida, com o `destino` |
| `NEGADO ...` | requisição que o peer não pôde atender, por exemplo um bloco que ainda não tem |
| `SUCESSO` / `FALHA` | resultado da verificação do SHA-256 do arquivo completo e tempo do download |

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
- Uma conexão TCP é aberta para cada requisição.
