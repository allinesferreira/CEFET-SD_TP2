#!/usr/bin/env python3
import csv
import math
from pathlib import Path

from reportlab.graphics.charts.legends import Legend
from reportlab.graphics.charts.linecharts import HorizontalLineChart
from reportlab.graphics.shapes import Drawing, String
from reportlab.graphics.widgets.markers import makeMarker
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import cm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import KeepTogether, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

RAIZ = Path(__file__).resolve().parents[1]
RESULTADOS = RAIZ / 'resultados'
SAIDA = RAIZ / 'docs' / 'relatorio.pdf'

URL = 'https://github.com/allinesferreira/CEFET-SD_TP2'
AUTORES = 'Alline Santos Ferreira, Pedro Henrique Estevam Vaz de Melo e Rávilla Moreira'

KB = 1024
MB = 1024 * KB
CONFIGURACOES = [(2, 1024), (2, 4096), (4, 1024), (4, 4096)]
CORES = [colors.HexColor(c) for c in ('#1f4e79', '#5b9bd5', '#c55a11', '#f4b183')]


def registrar_fontes():
    pasta = Path('/usr/share/fonts/truetype/dejavu')
    if not (pasta / 'DejaVuSans.ttf').exists():
        return 'Helvetica', 'Helvetica-Bold', 'Courier'
    pdfmetrics.registerFont(TTFont('Texto', str(pasta / 'DejaVuSans.ttf')))
    pdfmetrics.registerFont(TTFont('TextoNegrito', str(pasta / 'DejaVuSans-Bold.ttf')))
    pdfmetrics.registerFont(TTFont('Codigo', str(pasta / 'DejaVuSansMono.ttf')))
    pdfmetrics.registerFontFamily('Texto', normal='Texto', bold='TextoNegrito')
    return 'Texto', 'TextoNegrito', 'Codigo'


FONTE, NEGRITO, CODIGO = registrar_fontes()
ESTILOS = {
    'titulo': ParagraphStyle('titulo', fontName=NEGRITO, fontSize=14, leading=19, alignment=TA_CENTER, spaceAfter=6),
    'subtitulo': ParagraphStyle('subtitulo', fontName=FONTE, fontSize=9.5, leading=13, alignment=TA_CENTER),
    'secao': ParagraphStyle('secao', fontName=NEGRITO, fontSize=11.5, leading=15, spaceBefore=10, spaceAfter=4,
                            keepWithNext=1),
    'subsecao': ParagraphStyle('subsecao', fontName=NEGRITO, fontSize=10, leading=14, spaceBefore=4, spaceAfter=2,
                               keepWithNext=1),
    'corpo': ParagraphStyle('corpo', fontName=FONTE, fontSize=9.8, leading=14, alignment=TA_JUSTIFY, spaceAfter=6),
    'tabela': ParagraphStyle('tabela', fontName=FONTE, fontSize=8.2, leading=10.4),
    'legenda': ParagraphStyle('legenda', fontName=FONTE, fontSize=8, leading=10.5, alignment=TA_CENTER,
                              spaceBefore=3, spaceAfter=8),
}


def ler_csv(nome):
    with open(RESULTADOS / nome, newline='') as f:
        return list(csv.DictReader(f))


def codigo(texto):
    return f'<font face="{CODIGO}">{texto}</font>'


def legivel(tamanho):
    return f'{tamanho // MB} MB' if tamanho >= MB else f'{tamanho // KB} KB'


def ms(valor):
    return f'{float(valor):.0f}'


def decimal(valor, casas=1):
    return f'{valor:.{casas}f}'.replace('.', ',')


def porcento(parte, total):
    return f'{100 * parte / total:.0f}%'


class Relatorio:
    def __init__(self):
        self.historia = []
        self.resultados = ler_csv('resultados.csv')
        self.execucoes = ler_csv('execucoes.csv')
        self.origens = ler_csv('origens.csv')
        self.falha = {linha['peer']: linha for linha in ler_csv('falha.csv')}
        self.repeticoes = len(self.execucoes) // len(self.resultados)

    def p(self, texto, estilo='corpo'):
        self.historia.append(Paragraph(texto, ESTILOS[estilo]))

    def tabela(self, linhas, larguras, legenda):
        celulas = [[Paragraph(str(v), ESTILOS['tabela']) for v in linha] for linha in linhas]
        t = Table(celulas, colWidths=larguras, repeatRows=1, hAlign='CENTER')
        t.setStyle(TableStyle([
            ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#e4ebf3')),
            ('GRID', (0, 0), (-1, -1), 0.3, colors.HexColor('#b6bec8')),
            ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
            ('TOPPADDING', (0, 0), (-1, -1), 2.5),
            ('BOTTOMPADDING', (0, 0), (-1, -1), 2.5),
        ]))
        self.historia.append(KeepTogether([t, Paragraph(legenda, ESTILOS['legenda'])]))

    def resultado(self, peers, bloco, tamanho):
        for r in self.resultados:
            if (int(r['peers']), int(r['bloco_bytes']), int(r['arquivo_bytes'])) == (peers, bloco, tamanho):
                return r
        raise KeyError((peers, bloco, tamanho))

    def tempo(self, peers, bloco, tamanho, leecher=None):
        coluna = f'tempo_medio_{leecher}_ms' if leecher else 'tempo_medio_ms'
        return float(self.resultado(peers, bloco, tamanho)[coluna])

    def blocos_recebidos(self, peer, bloco, tamanho):
        cenario = f'4peers_{bloco}bytes_{tamanho}arquivo'
        return {o['origem']: int(o['blocos']) for o in self.origens if o['cenario'] == cenario and o['peer'] == peer}

    def gerar(self):
        self.cabecalho()
        self.introducao()
        self.arquitetura()
        self.metodologia()
        self.resultados_testes()
        self.discussao()
        self.conclusao()
        doc = SimpleDocTemplate(str(SAIDA), pagesize=A4, leftMargin=2 * cm, rightMargin=2 * cm,
                                topMargin=1.7 * cm, bottomMargin=1.7 * cm,
                                title='Transferência de Arquivos Peer-to-Peer', author=AUTORES)
        doc.build(self.historia, onFirstPage=rodape, onLaterPages=rodape)

    def cabecalho(self):
        self.p('Sistemas Distribuídos — Trabalho Prático 2<br/>Transferência de Arquivos Peer-to-Peer', 'titulo')
        self.p('CEFET-MG · 2026/2 · Profa. Michelle Hanne', 'subtitulo')
        self.p(AUTORES, 'subtitulo')
        self.p(f'Código-fonte: <link href="{URL}" color="blue">{URL}</link>', 'subtitulo')
        self.historia.append(Spacer(1, 8))

    def introducao(self):
        self.p('1. Introdução', 'secao')
        self.p('Este trabalho implementa um sistema elementar de transferência de arquivos no modelo P2P. Cada peer '
               'é um processo que atua ao mesmo tempo como servidor, atendendo pedidos de blocos na sua porta, e '
               'como cliente, pedindo os blocos que faltam aos vizinhos configurados. O seeder inicial divide o '
               'arquivo em blocos de tamanho fixo; os leechers baixam esses blocos e, assim que um bloco é validado '
               'e gravado, passam a oferecê-lo aos outros peers. A implementação foi feita em C++17 sobre a API de '
               'sockets POSIX do Linux, com OpenSSL para o SHA-256. Não há tracker: os vizinhos de cada peer são '
               'passados na linha de comando. Foram avaliados os 24 cenários da Tabela 1 do enunciado, com '
               f'{self.repeticoes} repetições cada, além de um cenário com a queda de um vizinho.')

    def arquitetura(self):
        self.p('2. Arquitetura e decisões de projeto', 'secao')
        self.p('2.1. Peer simétrico e concorrência', 'subsecao')
        self.p(f'Cada processo recebe seu IP e porta ({codigo("--ip")}, {codigo("--port")}) e a lista de vizinhos '
               f'({codigo("--neighbor")}). O servidor roda em uma única thread com sockets não bloqueantes e '
               f'{codigo("poll()")}: aceita conexões, acumula os bytes de cada requisição até ela estar completa e '
               'envia a resposta conforme o socket aceita escrita. Assim, um cliente lento ou parado não impede o '
               'atendimento dos demais, e conexões ociosas por mais de 5 s são fechadas. Em teste, o servidor '
               'atendeu 300 conexões simultâneas com apenas duas threads no processo.')
        self.p('O lado cliente usa uma thread por vizinho. Cada uma consulta o mapa de blocos do vizinho e pede os '
               f'blocos que ele tem e que o peer ainda não possui. Dois vetores guardam o estado local: {codigo("have")} '
               f'marca os blocos gravados e validados e {codigo("claimed")} os que estão sendo baixados, para que '
               'duas threads não peçam o mesmo bloco; ambos são protegidos por um mutex. Um bloco só entra em '
               f'{codigo("have")} depois de gravado, e a partir daí o servidor já o entrega a quem pedir. As threads '
               'de download percorrem os índices em uma ordem aleatória própria: com ordem sequencial, leechers que '
               'baixam do mesmo vizinho ficavam com os mesmos blocos e quase não trocavam entre si (em uma medição '
               'com 4 peers e 20 MB, C recebeu menos de 1% dos blocos de B; com ordem aleatória, cerca de 35%).')
        self.p('2.2. Fragmentação, metadados e montagem', 'subsecao')
        self.p('Para um arquivo de F bytes e blocos de B bytes há N = ⌈F/B⌉ blocos; o último pode ser menor. O seeder '
               f'gera um arquivo de metadados textual ({codigo("P2PMETA1")}) com nome, tamanho, tamanho do bloco, '
               'SHA-256 do arquivo e um SHA-256 por bloco, uma informação por linha. O leecher obtém os metadados de '
               f'um vizinho, cria um arquivo {codigo(".part")} com o tamanho final e grava cada bloco direto na sua '
               f'posição com {codigo("pwrite")}. O hash de cada bloco recebido é conferido antes de ele ser aceito; '
               'um bloco corrompido é descartado e pedido de novo. Ao final, o SHA-256 do arquivo inteiro é comparado '
               f'com o dos metadados, o {codigo(".part")} é renomeado e o leecher grava sua cópia dos metadados. '
               'Como os metadados vêm de outro peer, o nome não pode conter diretórios e os números precisam estar '
               'dentro de limites fixos; metadados fora disso são recusados. Cada peer trava a própria pasta, e um '
               'segundo processo que tente usar a mesma pasta se recusa a iniciar.')
        self.p('2.3. Protocolo e monitoramento', 'subsecao')
        self.p('Cada requisição usa uma conexão TCP e começa com um comando de 1 byte seguido do endereço de quem '
               'pede (IPv4 em 4 bytes e porta em 2), usado nos logs do servidor. Toda resposta é um inteiro de 32 '
               'bits com o tamanho do conteúdo, seguido do conteúdo; tamanho zero indica que o pedido não pode ser '
               'atendido. Os inteiros estão em network byte order, e as rotinas de envio e recepção repetem '
               f'{codigo("send")}/{codigo("recv")} até completar a quantidade esperada de bytes.')
        self.tabela([
            ['Comando', 'Requisição', 'Resposta'],
            ['M (metadados)', 'M + endereço', 'texto P2PMETA1'],
            ['H (mapa de blocos)', 'H + endereço', 'N bytes, 1 se o peer tem o bloco'],
            ['G (bloco)', 'G + endereço + índice (uint32)', 'bytes do bloco'],
        ], [3.6 * cm, 5.2 * cm, 6.2 * cm], 'Tabela 1. Mensagens do protocolo.')
        self.p('Cada linha do log traz o tempo desde o início do processo. São registrados todos os blocos '
               f'recebidos e a origem ({codigo("RECEBIDO")}), todas as requisições atendidas ou negadas e o destino '
               f'({codigo("ENVIADO")}, {codigo("NEGADO")}), uma linha {codigo("STATUS")} por segundo com os blocos '
               'que o peer possui, vizinhos que deixam de responder e, ao encerrar, um resumo com os blocos recebidos '
               'de cada vizinho e enviados para cada um.')

    def metodologia(self):
        self.p('3. Metodologia dos testes', 'secao')
        self.p(f'O script {codigo("scripts/testes.py")} combina todos os parâmetros da Tabela 1 do enunciado: 2 e 4 '
               'peers, blocos de 1024 e 4096 bytes e arquivos de 10 KB, 20 KB, 1 MB, 5 MB, 10 MB e 20 MB, num total '
               f'de 24 cenários, cada um repetido {self.repeticoes} vezes com um arquivo aleatório novo '
               f'({len(self.execucoes)} execuções). Com 2 peers, A é o seeder e B o leecher. Com 4 peers, B conhece '
               'apenas A, C conhece A e B, e D conhece apenas B e C, de modo que todos os blocos de D precisam vir de '
               'outros leechers.')
        self.p('Em cada execução o script inicia o seeder, espera ele ficar ativo e inicia os leechers um de cada vez, '
               'esperando cada um abrir sua porta antes do próximo. Para cada leecher são verificados: (i) tamanho do '
               'arquivo remontado igual ao original; (ii) SHA-256 igual ao original; (iii) arquivo de metadados '
               'idêntico ao do seeder; (iv) pelo log, todos os blocos vieram de vizinhos configurados para aquele peer, '
               f'nenhum faltou e cada bloco aparece no log do vizinho de origem como {codigo("ENVIADO")} para aquele '
               'leecher. Cada peer mede o próprio tempo, do seu início até a gravação do último bloco, o que inclui a '
               'obtenção dos metadados; o tempo da execução é o do último leecher a terminar.')
        self.p(f'No cenário de falha (4 peers, blocos de 1 KB, 20 MB), B é encerrado com {codigo("SIGKILL")} quando D '
               'já tem 25% dos blocos. O teste exige que, depois da queda, D registre B como inacessível e continue '
               'recebendo blocos de C, e que C e D terminem com o arquivo íntegro. Tudo rodou em 127.0.0.1, em um '
               'notebook com AMD Ryzen 7 5700U (16 threads), 12 GB de RAM e Pop!_OS 24.04, funcionando na bateria, '
               f'com g++ 13.3 e {codigo("-O2")}.')

    def resultados_testes(self):
        self.p('4. Resultados', 'secao')
        ok = all(r[c] == 'True' for r in self.resultados for c in ('tamanho_ok', 'sha_ok', 'meta_ok', 'origens_ok'))
        self.p(f'{"Todas as" if ok else "Nem todas as"} {len(self.execucoes)} execuções passaram nas quatro '
               'verificações. A Tabela 2 traz o tempo médio e o desvio padrão de cada cenário, e a Figura 1 mostra '
               'os mesmos tempos em função do tamanho do arquivo.')

        linhas = [['Arquivo'] + [f'{peers} peers<br/>bloco {bloco} B' for peers, bloco in CONFIGURACOES]]
        for tamanho in self.tamanhos():
            linha = [legivel(tamanho)]
            for peers, bloco in CONFIGURACOES:
                r = self.resultado(peers, bloco, tamanho)
                linha.append(f'{ms(r["tempo_medio_ms"])} ± {ms(r["desvio_ms"])}')
            linhas.append(linha)
        self.tabela(linhas, [2.2 * cm] + [3.4 * cm] * 4,
                    f'Tabela 2. Tempo médio ± desvio padrão (ms), {self.repeticoes} execuções por cenário.')
        self.historia.append(KeepTogether([self.grafico(), Paragraph(
            'Figura 1. Tempo médio do último leecher por tamanho de arquivo.', ESTILOS['legenda'])]))

        linhas = [['Cenário (4 peers)', 'B (ms)', 'C (ms)', 'D (ms)', 'C ← B', 'D ← B', 'D ← C']]
        for bloco in (1024, 4096):
            for tamanho in (5 * MB, 10 * MB, 20 * MB):
                c = self.blocos_recebidos('c', bloco, tamanho)
                d = self.blocos_recebidos('d', bloco, tamanho)
                total = sum(c.values())
                linhas.append([f'bloco {bloco} B, {legivel(tamanho)}',
                               *(ms(self.tempo(4, bloco, tamanho, nome)) for nome in 'bcd'),
                               porcento(c.get('b', 0), total), porcento(d.get('b', 0), total),
                               porcento(d.get('c', 0), total)])
        self.tabela(linhas, [3.8 * cm] + [1.7 * cm] * 6,
                    f'Tabela 3. Tempo médio de cada leecher ({self.repeticoes} execuções) e origem dos blocos '
                    'recebidos por C e D (primeira execução).')

        c, d = self.falha['c'], self.falha['d']
        total = sum(int(d[f'blocos_de_{o}']) for o in 'abc')
        self.p(f'No cenário de falha, B foi encerrado quando D tinha {d["progresso_de_d_na_falha"]} de {total} blocos. '
               f'D {"registrou" if d["falha_detectada_por_d"] == "True" else "não registrou"} B como inacessível, '
               f'recebeu {d["blocos_de_c_para_d_apos_falha"]} blocos de C depois da queda e '
               f'terminou em {ms(d["tempo_ms"])} ms, com {d["blocos_de_b"]} blocos vindos de B e {d["blocos_de_c"]} '
               f'de C; C terminou em {ms(c["tempo_ms"])} ms, com {c["blocos_de_a"]} blocos de A e {c["blocos_de_b"]} '
               'de B. Os dois arquivos ficaram íntegros.')

    def tamanhos(self):
        return sorted({int(r['arquivo_bytes']) for r in self.resultados})

    def grafico(self):
        tamanhos = self.tamanhos()
        desenho = Drawing(16 * cm, 5.2 * cm)
        grafico = HorizontalLineChart()
        grafico.x, grafico.y = 1.4 * cm, 0.8 * cm
        grafico.width, grafico.height = 10.2 * cm, 3.9 * cm
        grafico.data = [[self.tempo(peers, bloco, t) for t in tamanhos] for peers, bloco in CONFIGURACOES]
        grafico.categoryAxis.categoryNames = [legivel(t) for t in tamanhos]
        grafico.valueAxis.valueMin = 0
        for eixo in (grafico.categoryAxis, grafico.valueAxis):
            eixo.labels.fontName = FONTE
            eixo.labels.fontSize = 7
        for i, cor in enumerate(CORES):
            grafico.lines[i].strokeColor = cor
            grafico.lines[i].strokeWidth = 1.4
            grafico.lines[i].symbol = makeMarker('FilledCircle', size=3, fillColor=cor, strokeColor=cor)
        desenho.add(grafico)
        desenho.add(String(0.3 * cm, 4.95 * cm, 'tempo (ms)', fontName=FONTE, fontSize=7))

        legenda = Legend()
        legenda.x, legenda.y = 12.3 * cm, 4.2 * cm
        legenda.fontName, legenda.fontSize = FONTE, 7
        legenda.alignment = 'right'
        legenda.columnMaximum = len(CONFIGURACOES)
        legenda.colorNamePairs = [(cor, f'{p} peers, bloco {b} B') for cor, (p, b) in zip(CORES, CONFIGURACOES)]
        desenho.add(legenda)
        return desenho

    def discussao(self):
        t = self.tempo
        grandes = [5 * MB, 10 * MB, 20 * MB]
        c_de_b = [100 * self.blocos_recebidos('c', b, s).get('b', 0) / math.ceil(s / b)
                  for b in (1024, 4096) for s in grandes]
        ganho_bloco = [t(p, 1024, s) / t(p, 4096, s) for p in (2, 4) for s in grandes]

        self.p('5. Discussão', 'secao')
        self.p('<b>Tamanho do arquivo.</b> A partir de 1 MB, o tempo cresce quase linearmente com o número de '
               f'blocos: com 2 peers e blocos de 1 KB, foram {ms(t(2, 1024, MB))} ms para 1 MB e '
               f'{ms(t(2, 1024, 20 * MB))} ms para 20 MB. Os arquivos de 10 e 20 KB terminam em poucos milissegundos '
               f'(no máximo {ms(max(t(p, b, s) for p, b in CONFIGURACOES for s in (10 * KB, 20 * KB)))} ms), e o '
               'tempo é dominado pela troca inicial de metadados.')
        self.p('<b>Tamanho do bloco.</b> Blocos de 4 KB foram de '
               f'{decimal(min(ganho_bloco))} a {decimal(max(ganho_bloco))} vezes mais rápidos nos arquivos de 5 MB '
               f'em diante (com 2 peers e 20 MB, de {ms(t(2, 1024, 20 * MB))} para {ms(t(2, 4096, 20 * MB))} ms). '
               'Cada bloco custa uma conexão TCP, uma requisição e uma verificação de hash; em 127.0.0.1 esse custo '
               'fixo pesa mais que o volume de dados, e blocos quatro vezes maiores significam quatro vezes menos '
               'requisições.')
        self.p('<b>Número de peers.</b> Com 4 peers, o tempo da execução foi um pouco maior que com 2 '
               f'(20 MB, blocos de 1 KB: {ms(t(4, 1024, 20 * MB))} contra {ms(t(2, 1024, 20 * MB))} ms). A Tabela 3 '
               'mostra que o mais lento é B: ele só tem A como vizinho, disputa o servidor de A com C e ainda atende '
               'C e D. Já C e D, que baixam de dois vizinhos ao mesmo tempo, terminaram em '
               f'{ms(t(4, 1024, 20 * MB, "c"))} e {ms(t(4, 1024, 20 * MB, "d"))} ms, antes do leecher do cenário com '
               '2 peers. Isso mostra o ganho de ter mais fontes e confirma a redistribuição: D recebeu blocos de B e '
               f'de C, e C recebeu de B entre {min(c_de_b):.0f}% e {max(c_de_b):.0f}% dos blocos.')
        self.p('<b>Limitações.</b> Os testes rodaram em uma única máquina, sem a latência, as perdas e o limite de '
               'banda de uma rede real; nesse ambiente o custo é dominado por CPU e chamadas de sistema. Só o '
               'servidor é não bloqueante: os downloads usam uma thread por vizinho com sockets bloqueantes e timeout '
               'de 3 s, o que basta para a concorrência pedida. Os peers descobrem blocos novos por consulta '
               'periódica, e não por aviso dos vizinhos, e o endereço de quem pede é declarado pelo próprio cliente, '
               'sem autenticação. A implementação aceita só IPv4, um arquivo por processo e não retoma downloads '
               'interrompidos.')

    def conclusao(self):
        self.p('6. Conclusão', 'secao')
        self.p('O sistema atende aos requisitos do enunciado: peers simétricos com servidor não bloqueante e '
               'downloads concorrentes, fragmentação em blocos de tamanho fixo, formato de metadados e protocolo '
               'próprios, registro local dos blocos e remontagem verificada por SHA-256. Os leechers repassam blocos '
               'durante o download, como mostram os logs e a Tabela 3, e o sistema se recuperou da queda de um '
               'vizinho. Como próximos passos, ficam manter uma conexão por vizinho em vez de uma por requisição, '
               'avisar os vizinhos quando um bloco novo chega, testar em máquinas diferentes e permitir retomar um '
               f'download a partir do {codigo(".part")}.')


def rodape(canvas, doc):
    canvas.saveState()
    canvas.setFont(FONTE, 7.5)
    canvas.setFillColor(colors.grey)
    canvas.drawCentredString(A4[0] / 2, 1.0 * cm, f'CEFET-MG · Sistemas Distribuídos · TP2 · página {doc.page}')
    canvas.restoreState()


if __name__ == '__main__':
    Relatorio().gerar()
    print('gerado', SAIDA)
