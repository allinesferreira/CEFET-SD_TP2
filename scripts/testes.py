#!/usr/bin/env python3
import argparse
import csv
import hashlib
import math
import os
import pathlib
import re
import shutil
import statistics
import subprocess
import tempfile
import time
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parents[1]
PEER = ROOT / 'peer'
RESULTADOS = ROOT / 'resultados'

HOST = '127.0.0.1'
PORTA_BASE = 25000

KB = 1024
MB = 1024 * KB
PEERS = [2, 4]
BLOCOS = [1024, 4096]
ARQUIVOS = [10 * KB, 20 * KB, 1 * MB, 5 * MB, 10 * MB, 20 * MB]

TOPOLOGIAS = {
    2: {'a': [], 'b': ['a']},
    4: {'a': [], 'b': ['a'], 'c': ['a', 'b'], 'd': ['b', 'c']},
}

RE_RECEBIDO = re.compile(r'RECEBIDO bloco=\d+ origem=(\S+) progresso=(\d+)/(\d+)')
RE_SUCESSO = re.compile(r'SUCESSO .* tempo_ms=(\d+)')


def porta(nome):
    return PORTA_BASE + ord(nome) - ord('a')


def endereco(nome):
    return f'{HOST}:{porta(nome)}'


def nome_do_endereco(texto):
    return chr(ord('a') + int(texto.rsplit(':', 1)[1]) - PORTA_BASE)


def legivel(tamanho):
    return f'{tamanho // MB} MB' if tamanho >= MB else f'{tamanho // KB} KB'


def sha256(caminho):
    h = hashlib.sha256()
    with open(caminho, 'rb') as f:
        for parte in iter(lambda: f.read(1 << 16), b''):
            h.update(parte)
    return h.hexdigest()


class Execucao:
    def __init__(self, pasta, peers, bloco, tamanho):
        self.pasta = pasta
        self.topologia = TOPOLOGIAS[peers]
        self.leechers = [nome for nome, vizinhos in self.topologia.items() if vizinhos]
        self.bloco = bloco
        self.tamanho = tamanho
        self.original = pasta / 'arquivo.bin'
        self.processos = {}

    def log(self, nome):
        return self.pasta / f'{nome}.log'

    def baixado(self, nome):
        return self.pasta / nome / 'arquivo.bin'

    def iniciar(self):
        self.original.write_bytes(os.urandom(self.tamanho))
        self.sha_original = sha256(self.original)
        for nome, vizinhos in self.topologia.items():
            self.iniciar_peer(nome, vizinhos)
            if not vizinhos:
                self.esperar_texto(nome, 'Seeder ativo')

    def iniciar_peer(self, nome, vizinhos):
        args = [str(PEER), '--ip', HOST, '--port', str(porta(nome)), '--dir', str(self.pasta / nome)]
        if not vizinhos:
            args += ['--file', str(self.original), '--block', str(self.bloco)]
        for vizinho in vizinhos:
            args += ['--neighbor', endereco(vizinho)]
        with open(self.log(nome), 'w') as log:
            self.processos[nome] = subprocess.Popen(args, stderr=log)

    def esperar_texto(self, nome, texto, limite=10):
        fim = time.monotonic() + limite
        while time.monotonic() < fim:
            if texto in self.log(nome).read_text():
                return
            time.sleep(0.01)
        raise RuntimeError(f'peer {nome} não registrou "{texto}"')

    def esperar_downloads(self, nomes):
        fim = time.monotonic() + max(30, self.tamanho / 100_000)
        while time.monotonic() < fim:
            if all(self.baixado(nome).exists() for nome in nomes):
                return
            if any(self.processos[nome].poll() is not None for nome in nomes):
                return
            time.sleep(0.02)

    def esperar_progresso(self, nome, fracao):
        fim = time.monotonic() + max(30, self.tamanho / 100_000)
        while time.monotonic() < fim:
            progresso = RE_RECEBIDO.findall(self.log(nome).read_text())
            if progresso:
                _, recebidos, total = progresso[-1]
                if int(recebidos) >= fracao * int(total):
                    return int(recebidos)
            time.sleep(0.01)
        raise RuntimeError(f'peer {nome} não chegou a {fracao:.0%} do download')

    def encerrar(self):
        for p in self.processos.values():
            if p.poll() is None:
                p.terminate()
        for p in self.processos.values():
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()

    def verificar(self, nome):
        origens = Counter()
        tempo_ms = None
        for linha in self.log(nome).read_text().splitlines():
            if m := RE_RECEBIDO.search(linha):
                origens[nome_do_endereco(m.group(1))] += 1
            elif m := RE_SUCESSO.search(linha):
                tempo_ms = int(m.group(1))

        arquivo = self.baixado(nome)
        existe = arquivo.exists()
        return {
            'tamanho_ok': existe and arquivo.stat().st_size == self.tamanho,
            'sha_ok': existe and sha256(arquivo) == self.sha_original,
            'origens_ok': set(origens) <= set(self.topologia[nome]) and
                          sum(origens.values()) == math.ceil(self.tamanho / self.bloco),
            'tempo_ms': tempo_ms,
            'origens': origens,
        }

    def guardar_logs(self, destino):
        shutil.rmtree(destino, ignore_errors=True)
        destino.mkdir(parents=True)
        for nome in self.topologia:
            shutil.copy2(self.log(nome), destino / f'{nome}.log')


def executar_cenario(peers, bloco, tamanho, destino_logs=None):
    with tempfile.TemporaryDirectory(prefix='p2p_') as tmp:
        execucao = Execucao(pathlib.Path(tmp), peers, bloco, tamanho)
        try:
            execucao.iniciar()
            execucao.esperar_downloads(execucao.leechers)
        finally:
            execucao.encerrar()
        verificacoes = {nome: execucao.verificar(nome) for nome in execucao.leechers}
        if destino_logs:
            execucao.guardar_logs(destino_logs)
    return verificacoes


def executar_falha(destino_logs, bloco=1024, tamanho=20 * MB, fracao=0.25):
    with tempfile.TemporaryDirectory(prefix='p2p_falha_') as tmp:
        execucao = Execucao(pathlib.Path(tmp), 4, bloco, tamanho)
        try:
            execucao.iniciar()
            progresso_na_falha = execucao.esperar_progresso('d', fracao)
            execucao.processos['b'].kill()
            execucao.processos['b'].wait()
            execucao.esperar_downloads(['c', 'd'])
        finally:
            execucao.encerrar()
        verificacoes = {nome: execucao.verificar(nome) for nome in ['c', 'd']}
        detectada = f'VIZINHO inacessivel {endereco("b")}' in execucao.log('d').read_text()
        execucao.guardar_logs(destino_logs)

    linhas = []
    for nome, v in verificacoes.items():
        linhas.append({
            'peer': nome,
            'tamanho_ok': v['tamanho_ok'],
            'sha_ok': v['sha_ok'],
            'origens_ok': v['origens_ok'],
            'tempo_ms': v['tempo_ms'],
            **{f'blocos_de_{origem}': v['origens'][origem] for origem in 'abc'},
            'blocos_de_d_na_falha': progresso_na_falha,
            'falha_detectada_por_d': detectada,
        })
    return linhas


def resumir(verificacoes):
    tempos = [v['tempo_ms'] for v in verificacoes.values()]
    return {
        'tempo_ms': max(tempos) if None not in tempos else None,
        'tamanho_ok': all(v['tamanho_ok'] for v in verificacoes.values()),
        'sha_ok': all(v['sha_ok'] for v in verificacoes.values()),
        'origens_ok': all(v['origens_ok'] for v in verificacoes.values()),
    }


def agregar(execucoes):
    grupos = {}
    for e in execucoes:
        grupos.setdefault((e['peers'], e['bloco_bytes'], e['arquivo_bytes']), []).append(e)

    linhas = []
    for (peers, bloco, tamanho), grupo in grupos.items():
        tempos = [e['tempo_ms'] for e in grupo if e['tempo_ms'] is not None]
        linhas.append({
            'peers': peers,
            'bloco_bytes': bloco,
            'arquivo_bytes': tamanho,
            'blocos': math.ceil(tamanho / bloco),
            'execucoes': len(grupo),
            'tempo_medio_ms': round(float(statistics.mean(tempos)), 1) if tempos else '',
            'desvio_ms': round(statistics.stdev(tempos), 1) if len(tempos) > 1 else 0.0,
            'tempo_min_ms': min(tempos, default=''),
            'tempo_max_ms': max(tempos, default=''),
            'tamanho_ok': all(e['tamanho_ok'] for e in grupo),
            'sha_ok': all(e['sha_ok'] for e in grupo),
            'origens_ok': all(e['origens_ok'] for e in grupo),
        })
    return linhas


def salvar_csv(caminho, linhas):
    with open(caminho, 'w', newline='') as f:
        escritor = csv.DictWriter(f, fieldnames=list(linhas[0]))
        escritor.writeheader()
        escritor.writerows(linhas)


def main():
    parser = argparse.ArgumentParser(description='Estudos de caso do TP2 em 127.0.0.1.')
    parser.add_argument('--repeticoes', type=int, default=5, help='execuções por cenário (padrão: 5)')
    args = parser.parse_args()
    if not PEER.exists():
        raise SystemExit('executável peer não encontrado; rode make antes')

    RESULTADOS.mkdir(exist_ok=True)
    shutil.rmtree(RESULTADOS / 'logs', ignore_errors=True)
    execucoes = []
    origens = []

    for peers in PEERS:
        for bloco in BLOCOS:
            for tamanho in ARQUIVOS:
                cenario = f'{peers}peers_{bloco}bytes_{tamanho}arquivo'
                for repeticao in range(1, args.repeticoes + 1):
                    destino = RESULTADOS / 'logs' / cenario if repeticao == 1 else None
                    verificacoes = executar_cenario(peers, bloco, tamanho, destino)
                    resumo = resumir(verificacoes)
                    execucoes.append({
                        'peers': peers,
                        'bloco_bytes': bloco,
                        'arquivo_bytes': tamanho,
                        'repeticao': repeticao,
                        **resumo,
                    })
                    if repeticao == 1:
                        for nome, v in verificacoes.items():
                            for origem, blocos in sorted(v['origens'].items()):
                                origens.append({'cenario': cenario, 'peer': nome, 'origem': origem, 'blocos': blocos})

                    estado = 'ok' if resumo['tamanho_ok'] and resumo['sha_ok'] and resumo['origens_ok'] else 'FALHA'
                    print(f'{peers} peers, bloco {bloco}, {legivel(tamanho)}, '
                          f'execução {repeticao}/{args.repeticoes}: {resumo["tempo_ms"]} ms, {estado}', flush=True)

    resultados = agregar(execucoes)
    salvar_csv(RESULTADOS / 'execucoes.csv', execucoes)
    salvar_csv(RESULTADOS / 'resultados.csv', resultados)
    salvar_csv(RESULTADOS / 'origens.csv', origens)

    print('\nFalha de vizinho: 4 peers, bloco 1024, 20 MB, B encerrado com SIGKILL quando D tem 25% dos blocos')
    falha = executar_falha(RESULTADOS / 'logs' / 'falha_4peers_1024bytes_20971520arquivo')
    salvar_csv(RESULTADOS / 'falha.csv', falha)
    for linha in falha:
        print(f'  {linha["peer"]}: {linha["tempo_ms"]} ms, blocos de a/b/c = '
              f'{linha["blocos_de_a"]}/{linha["blocos_de_b"]}/{linha["blocos_de_c"]}, '
              f'{"ok" if linha["sha_ok"] and linha["tamanho_ok"] else "FALHA"}')
    print(f'  D tinha {falha[0]["blocos_de_d_na_falha"]} blocos na falha; '
          f'falha detectada por D: {"sim" if falha[0]["falha_detectada_por_d"] else "não"}')

    falhas = [r for r in resultados if not (r['tamanho_ok'] and r['sha_ok'] and r['origens_ok'])]
    falhas += [r for r in falha if not (r['tamanho_ok'] and r['sha_ok'] and r['falha_detectada_por_d'])]
    if falhas:
        raise SystemExit(f'{len(falhas)} verificação(ões) com falha')
    print(f'\n{len(resultados)} cenários, {len(execucoes)} execuções e o cenário de falha, todos íntegros.')


if __name__ == '__main__':
    main()
