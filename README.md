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
| `--dir` | pasta onde ficam o arquivo baixado e os metadados | `downloads/<porta>` |
| `--block` | tamanho do bloco em bytes, entre 256 e 1048576 (só vale para o seeder) | `1024` |
| `--linger` | segundos (0 a 3600) que um leecher continua servindo blocos depois de concluir | `30` |

Um peer sem `--file` é leecher e precisa de pelo menos um vizinho. O seeder fica ativo até receber Ctrl+C. Cada peer precisa de uma pasta própria: se outro peer já estiver usando a mesma pasta, o segundo se recusa a iniciar.

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
- **Downloads:** uma thread por vizinho. Cada uma consulta o mapa de blocos do vizinho e pede os blocos que ainda faltam, percorrendo os índices em ordem aleatória. Assim, leechers que baixam do mesmo vizinho ficam com partes diferentes do arquivo e conseguem trocar blocos entre si. Um vetor `claimed` evita que duas threads baixem o mesmo bloco, e o vetor `have` registra os blocos já gravados e validados. Os dois são protegidos por um mutex.

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

O seeder grava os metadados em `<pasta>/<arquivo>.meta` ao iniciar. O leecher confere o SHA-256 de cada bloco antes de marcá-lo como disponível. Quando todos os blocos chegam, confere o SHA-256 do arquivo inteiro, renomeia o `.part` para o nome original e também grava o `.meta`, ficando com o mesmo conteúdo que o seeder inicial.

## Logs

Cada peer escreve o log no stderr, uma mensagem por linha. Toda linha começa com o tempo em segundos desde o início do processo, por exemplo `[   0.152] RECEBIDO bloco=3 origem=127.0.0.1:5000 progresso=4/1024`. A exceção é a linha `ERRO: ...`, sem tempo, escrita quando um erro encerra o processo com código 1.

| Linha | Significado |
|---|---|
| `SEED` / `LEECH` | papel do peer, nome do arquivo, quantidade de blocos e endereço |
| `META recebida de` | vizinho que forneceu os metadados |
| `RECEBIDO bloco=i origem=X progresso=n/total` | bloco validado e gravado, com o vizinho de onde veio |
| `ENVIADO metadados`, `ENVIADO mapa`, `ENVIADO bloco=i` | requisição atendida, com o `destino` |
| `NEGADO ...` | requisição que o peer não pôde atender, por exemplo um bloco que ainda não tem |
| `INVALIDO bloco=i origem=X` | bloco recebido com SHA-256 diferente do esperado; é descartado e pedido de novo |
| `VIZINHO inacessivel X` / `VIZINHO acessivel X` | o vizinho deixou de responder ou voltou a responder |
| `STATUS blocos=n/total` | blocos que o peer possui; aparece no início, a cada segundo durante o download e ao concluir |
| `SUCESSO` / `FALHA` | resultado da verificação do SHA-256 do arquivo completo; `tempo_ms` vai do início do peer até a gravação do último bloco |
| `RESUMO origem=X` / `RESUMO destino=X` | ao encerrar, quantos blocos o peer recebeu de cada vizinho e enviou para cada um |

## Testes

```
make
python3 scripts/testes.py [--repeticoes N]
```

O script roda os estudos de caso em 127.0.0.1, combinando todos os parâmetros da Tabela 1 do enunciado: 2 e 4 peers, blocos de 1024 e 4096 bytes e arquivos de 10 KB, 20 KB, 1 MB, 5 MB, 10 MB e 20 MB, num total de 24 cenários. Cada cenário é executado `N` vezes (padrão: 5), sempre com um arquivo aleatório novo.

Em cada execução, o script inicia o seeder, espera ele ficar ativo e inicia os leechers, um de cada vez, esperando cada um abrir sua porta antes de iniciar o próximo. Depois aguarda todos concluírem. Depois confere, para cada leecher:

- **tamanho:** o arquivo remontado tem o mesmo tamanho do original;
- **integridade:** o SHA-256 é igual ao do original;
- **metadados:** o `.meta` gravado pelo leecher é idêntico ao do seeder;
- **origens:** pelo log do leecher, todos os blocos vieram de vizinhos configurados para aquele peer e nenhum bloco faltou; e cada bloco recebido de um vizinho aparece no log desse vizinho como `ENVIADO` para o leecher.

O tempo de uma execução é o maior `tempo_ms` entre os leechers; o tempo de cada leecher também fica registrado.

Ao final, o script roda um cenário de falha: 4 peers, blocos de 1024 bytes e arquivo de 20 MB, em que B é encerrado com `SIGKILL` quando D já tem 25% dos blocos. O teste passa se, depois da queda, D registrar que B ficou inacessível e continuar recebendo blocos de C, e se C e D terminarem com o arquivo íntegro.

Os resultados ficam em `resultados/`:

| Arquivo | Conteúdo |
|---|---|
| `resultados.csv` | por cenário: tempo médio, desvio padrão, mínimo, máximo, tempo médio de cada leecher e resultado das verificações |
| `execucoes.csv` | cada execução separadamente |
| `origens.csv` | quantos blocos cada leecher recebeu de cada vizinho, na primeira execução de cada cenário |
| `logs/<peers>peers_<bloco>bytes_<tamanho>arquivo/<peer>.log` | logs completos da primeira execução de cada cenário |
| `falha.csv` e `logs/falha_4peers_1024bytes_20971520arquivo/` | resultado e logs do cenário de falha |

As portas são fixas (25000 a 25003), então não rode duas instâncias do script ao mesmo tempo.

## Relatório

O relatório em `docs/relatorio.pdf` é gerado a partir dos arquivos de `resultados/`. Depois de rodar os testes:

```
pip install reportlab
python3 docs/gerar_relatorio.py
```

## Limitações

- Apenas IPv4 numérico e um arquivo por processo, sem autenticação nem TLS.
- Não há tracker: os vizinhos são definidos na linha de comando.
- Não há retomada: se o leecher for reiniciado, o download começa do zero.
- Uma conexão TCP é aberta para cada requisição.
- Só o servidor é não bloqueante. Os downloads usam sockets bloqueantes com timeout de 3 s, um por thread de vizinho.
- O endereço de quem pede é informado pelo próprio cliente na requisição e não é verificado.
