#!/usr/bin/env python3
"""Testes de integração P2P; gera resultados.csv e preserva logs."""

import csv
import hashlib
import os
import pathlib
import shutil
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = ROOT / 'peer'


def sha(path):
    h = hashlib.sha256()

    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)

    return h.hexdigest()


def test(peers, block, size):

    with tempfile.TemporaryDirectory(prefix='p2p_case_') as tmp:

        d = pathlib.Path(tmp)

        src = d / 'arquivo.bin'
        src.write_bytes(os.urandom(size))

        procs = []
        logs = []

        def start(args, name):

            log = open(d / f'{name}.log', 'w+')
            logs.append(log)

            p = subprocess.Popen(
                [str(EXE), *args],
                stdout=log,
                stderr=subprocess.STDOUT
            )

            procs.append(p)

            return p

        try:

            # Peer A: Seeder inicial
            start(
                [
                    '--ip', '127.0.0.1',
                    '--port', '25000',
                    '--file', str(src),
                    '--block', str(block),
                    '--dir', str(d / 'a')
                ],
                'a'
            )

            time.sleep(0.3)

            # Peer B
            start(
                [
                    '--ip', '127.0.0.1',
                    '--port', '25001',
                    '--neighbor', '127.0.0.1:25000',
                    '--dir', str(d / 'b')
                ],
                'b'
            )

            # Peers adicionais
            if peers == 4:

                # Peer C
                start(
                    [
                        '--ip', '127.0.0.1',
                        '--port', '25002',
                        '--neighbor', '127.0.0.1:25000',
                        '--neighbor', '127.0.0.1:25001',
                        '--dir', str(d / 'c')
                    ],
                    'c'
                )

                # Peer D
                start(
                    [
                        '--ip', '127.0.0.1',
                        '--port', '25003',
                        '--neighbor', '127.0.0.1:25001',
                        '--neighbor', '127.0.0.1:25002',
                        '--dir', str(d / 'd')
                    ],
                    'd'
                )

            # Início da medição
            begin = time.monotonic()

            deadline = begin + max(25, size / 200000)

            if peers == 2:
                nomes = ['b']
            else:
                nomes = ['b', 'c', 'd']

            targets = [
                d / nome / 'arquivo.bin'
                for nome in nomes
            ]

            # Aguarda a conclusão das transferências
            while time.monotonic() < deadline:

                if all(x.is_file() for x in targets):
                    break

                if any(
                        p.poll() not in (None, 0)
                        for p in procs
                ):
                    break

                time.sleep(0.05)

            elapsed = time.monotonic() - begin

            # Verificação de integridade
            hash_original = sha(src)

            success = all(
                x.is_file()
                and sha(x) == hash_original
                and x.stat().st_size == size
                for x in targets
            )

            return {
                'peers': peers,
                'bloco_bytes': block,
                'arquivo_bytes': size,
                'tempo_s': round(elapsed, 3),
                'integridade': success
            }

        finally:

            # Encerra os processos
            for p in procs:
                if p.poll() is None:
                    p.terminate()

            for p in procs:
                try:
                    p.wait(timeout=3)

                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()

            # Fecha os arquivos de log
            for f in logs:
                f.close()

            # Cria pasta permanente de logs
            pasta_logs = ROOT / 'resultados' / 'logs'
            pasta_logs.mkdir(parents=True, exist_ok=True)

            identificador = (
                f'{peers}peers_'
                f'{block}bytes_'
                f'{size}arquivo'
            )

            destino = pasta_logs / identificador
            destino.mkdir(exist_ok=True)

            # Copia os logs antes de excluir
            # o diretório temporário
            quantidade = 0

            for arquivo_log in d.glob('*.log'):

                shutil.copy2(
                    arquivo_log,
                    destino / arquivo_log.name
                )

                quantidade += 1

            print(
                f'Logs preservados: {quantidade} '
                f'na pasta {destino}',
                flush=True
            )


if __name__ == '__main__':

    cases = [
        (2, 1024, 10 * 1024),
        (2, 4096, 20 * 1024),
        (2, 1024, 1024 * 1024),
        (2, 4096, 5 * 1024 * 1024),
        (2, 1024, 10 * 1024 * 1024),
        (2, 4096, 20 * 1024 * 1024),
        (4, 1024, 1024 * 1024),
        (4, 4096, 5 * 1024 * 1024)
    ]

    rows = []

    for c in cases:

        result = test(*c)

        rows.append(result)

        print(result, flush=True)

    with open(
            ROOT / 'resultados' / 'resultados.csv',
            'w',
            newline=''
    ) as f:

        w = csv.DictWriter(
            f,
            fieldnames=list(rows[0])
        )

        w.writeheader()
        w.writerows(rows)

    if not all(r['integridade'] for r in rows):
        raise SystemExit(
            'Falha em um ou mais testes'
        )

    print('\nTodos os testes foram concluídos com sucesso!')