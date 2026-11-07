# Arma una ficha por juego (roms/<carpeta>/_fichas/<rom sin extensión>.txt) con:
#   título, año, estudio, editora, género, jugadores, director, música, ventas, nota y un resumen en español.
# Fuentes: LaunchBox (Metadata.xml, ya cruzado por notas.py --ids), Wikipedia/Wikidata (resumen en español,
# director, compositor, unidades vendidas) y, si no hay artículo en español, el resumen de LaunchBox traducido
# offline (datos_notas/traducir.py, modelo de Argos). Todo lo bajado o traducido queda en caché: correrlo de nuevo
# no repite nada. Se ejecuta con el python del entorno que tiene ctranslate2:
#   datos_notas/venv/bin/python fichas.py DATOS SALIDA_RAIZ
#     DATOS = luckfox_consola/datos_notas (ids.json, launchbox/Metadata.xml, traducir.py, cachés)
#     SALIDA_RAIZ = luckfox_consola/sd/roms
# Formato de la ficha (UTF-8): líneas "Clave: valor", una línea vacía y el resumen (párrafos separados por
# línea vacía). La consola la lee solo al abrirla.
import json, os, re, sys, time, unicodedata, urllib.error, urllib.parse, urllib.request
import xml.etree.ElementTree as ET

DATOS, SALIDA = sys.argv[1], sys.argv[2]
AGENTE = 'consola-retro-fichas/1.0 (uso personal, una sola descarga)'
GENEROS = {'Action': 'Acción', 'Adventure': 'Aventura', 'Platform': 'Plataformas', 'Role-Playing': 'RPG',
           'Puzzle': 'Puzles', 'Sports': 'Deportes', 'Racing': 'Carreras', 'Shooter': 'Disparos',
           'Fighting': 'Lucha', 'Strategy': 'Estrategia', 'Board Game': 'Juego de mesa', 'Casino': 'Casino',
           'Education': 'Educativo', 'Educational': 'Educativo', 'Simulation': 'Simulación',
           "Beat 'em Up": "Beat 'em up", 'Music': 'Música', 'Party': 'Fiesta', 'Pinball': 'Pinball',
           'Quiz': 'Preguntas', 'Stealth': 'Sigilo', 'Horror': 'Terror', 'Compilation': 'Recopilación',
           'Life Simulation': 'Simulación de vida', 'Construction and Management Simulation': 'Construcción y gestión',
           'Flight Simulator': 'Simulador de vuelo', 'Vehicle Simulation': 'Simulación de vehículos',
           'Visual Novel': 'Novela visual', 'Sandbox': 'Mundo abierto', 'Card Game': 'Cartas', 'Other': 'Otros',
           'Shoot \'em up': 'Shoot \'em up', 'Rhythm': 'Ritmo', 'Metroidvania': 'Metroidvania'}


def cache(nombre):
    ruta = os.path.join(DATOS, nombre)
    return ruta, (json.load(open(ruta)) if os.path.exists(ruta) else {})


def guardar(ruta, d):
    with open(ruta + '.tmp', 'w') as o:
        json.dump(d, o, ensure_ascii=False)
    os.replace(ruta + '.tmp', ruta)


_ultima = [0.0]


def api(url, params):
    """GET a la API con un máximo de ~1 consulta por segundo; ante 429/5xx espera lo que pida el servidor."""
    q = url + '?' + urllib.parse.urlencode(params)
    for intento in range(8):
        espera = 1.0 - (time.time() - _ultima[0])
        if espera > 0:
            time.sleep(espera)
        _ultima[0] = time.time()
        try:
            req = urllib.request.Request(q, headers={'User-Agent': AGENTE})
            with urllib.request.urlopen(req, timeout=30) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            ultimo = e
            ra = e.headers.get('Retry-After')
            pausa = int(ra) if ra and ra.isdigit() else 10 * (intento + 1)
            print(f'  servidor respondió {e.code}: espero {pausa} s', flush=True)
            time.sleep(pausa)
        except Exception as e:  # red: reintentar con espera
            ultimo = e
            time.sleep(5 * (intento + 1))
    raise ultimo


def lotes(xs, n):
    xs = list(xs)
    for i in range(0, len(xs), n):
        yield xs[i:i + n]


# ------------------------------------------------------------------ 1) LaunchBox
ids = json.load(open(os.path.join(DATOS, 'ids.json')))
quiero = {i for c in ids.values() for i in c.values()}
LB = {}
for _, el in ET.iterparse(os.path.join(DATOS, 'launchbox', 'Metadata.xml'), events=('end',)):
    if el.tag == 'Game':
        i = el.findtext('DatabaseID')
        if i in quiero:
            LB[i] = {k: (el.findtext(k) or '').strip() for k in
                     ('Name', 'ReleaseDate', 'ReleaseYear', 'Developer', 'Publisher', 'Genres', 'MaxPlayers',
                      'Cooperative', 'Overview', 'WikipediaURL', 'CommunityRating', 'CommunityRatingCount')}
        el.clear()
print(f'LaunchBox: {len(LB)} juegos, {sum(1 for g in LB.values() if g["WikipediaURL"])} con Wikipedia')

# ------------------------------------------------------------------ 2) Wikipedia en inglés -> Wikidata y artículo en español
ruta_w, W = cache('wiki_en.json')  # título en -> {"qid":..., "es":...}
titulos = {}
for i, g in LB.items():
    u = g['WikipediaURL']
    m = re.match(r'https?://en\.(?:m\.)?wikipedia\.org/wiki/(.+)', u)
    if m:
        titulos[i] = urllib.parse.unquote(m.group(1).split('#')[0]).replace('_', ' ')
def destino(q, t):
    """Sigue normalizaciones y redirecciones de la respuesta de la API hasta el título final."""
    fwd = {x['from']: x['to'] for k in ('normalized', 'redirects') for x in q.get(k, [])}
    vistos = set()
    while t in fwd and t not in vistos:
        vistos.add(t)
        t = fwd[t]
    return t


faltan = sorted({t for t in titulos.values() if not W.get(t)})  # incluye los que quedaron vacíos
for lote in lotes(faltan, 50):
    r = api('https://en.wikipedia.org/w/api.php', {'action': 'query', 'format': 'json', 'formatversion': 2,
            'redirects': 1, 'prop': 'pageprops|langlinks', 'ppprop': 'wikibase_item', 'lllang': 'es',
            'lllimit': 'max', 'titles': '|'.join(lote)})
    q = r.get('query', {})
    por_titulo = {p['title']: {'qid': p.get('pageprops', {}).get('wikibase_item'),
                               'es': (p.get('langlinks') or [{}])[0].get('title')} for p in q.get('pages', [])}
    for t in lote:
        W[t] = por_titulo.get(destino(q, t), {})
    guardar(ruta_w, W)
print(f'Wikipedia: {sum(1 for v in W.values() if v.get("es"))} con artículo en español')

ruta_e, ES = cache('wiki_es.json')  # título es -> resumen
faltan = sorted({v['es'] for v in W.values() if v.get('es') and not ES.get(v['es'])})
for lote in lotes(faltan, 20):
    r = api('https://es.wikipedia.org/w/api.php', {'action': 'query', 'format': 'json', 'formatversion': 2,
            'redirects': 1, 'prop': 'extracts', 'exintro': 1, 'explaintext': 1, 'exlimit': 20,
            'titles': '|'.join(lote)})
    q = r.get('query', {})
    por_titulo = {p['title']: p.get('extract', '') for p in q.get('pages', [])}
    for t in lote:
        ES[t] = por_titulo.get(destino(q, t), '')
    guardar(ruta_e, ES)

ruta_d, WD = cache('wikidata.json')  # qid -> {"director": [...], "musica": [...], "ventas": n}
faltan = sorted({v['qid'] for v in W.values() if v.get('qid') and v['qid'] not in WD})
etiquetas_q = set()
for lote in lotes(faltan, 50):
    r = api('https://www.wikidata.org/w/api.php', {'action': 'wbgetentities', 'format': 'json',
            'props': 'claims', 'ids': '|'.join(lote)})
    for q, ent in r.get('entities', {}).items():
        c = ent.get('claims', {})
        def valores(prop):
            out = []
            for x in c.get(prop, []):
                v = x.get('mainsnak', {}).get('datavalue', {}).get('value')
                if isinstance(v, dict) and 'id' in v:
                    out.append(v['id'])
            return out
        ventas = 0
        for x in c.get('P2664', []):  # unidades vendidas: la mayor cifra declarada
            v = x.get('mainsnak', {}).get('datavalue', {}).get('value')
            if isinstance(v, dict) and 'amount' in v:
                ventas = max(ventas, int(float(v['amount'])))
        WD[q] = {'director': valores('P57'), 'musica': valores('P86'), 'ventas': ventas}
    guardar(ruta_d, WD)
ruta_l, ETQ = cache('wikidata_etiquetas.json')  # qid de persona -> nombre
faltan = sorted({p for v in WD.values() for p in v['director'] + v['musica'] if p not in ETQ})
for lote in lotes(faltan, 50):
    r = api('https://www.wikidata.org/w/api.php', {'action': 'wbgetentities', 'format': 'json',
            'props': 'labels', 'languages': 'es|en', 'ids': '|'.join(lote)})
    for q, ent in r.get('entities', {}).items():
        l = ent.get('labels', {})
        ETQ[q] = (l.get('es') or l.get('en') or {}).get('value', '')
    guardar(ruta_l, ETQ)
print(f'Wikidata: {sum(1 for v in WD.values() if v["ventas"])} con ventas, '
      f'{sum(1 for v in WD.values() if v["director"])} con director, {sum(1 for v in WD.values() if v["musica"])} con música')


# ------------------------------------------------------------------ 2b) ventas: listas de superventas de Wikipedia
LISTAS = ['List of best-selling Game Boy video games', 'List of best-selling Game Boy Advance video games',
          'List of best-selling Super Nintendo Entertainment System video games',
          'List of best-selling Nintendo Entertainment System video games']
ruta_v, VENT = cache('superventas.json')  # qid -> [ventas, "varias versiones" (bool)]
if not VENT:
    filas = []  # (título del artículo, ventas, combinado)
    for lista in LISTAS:
        w = api('https://en.wikipedia.org/w/api.php', {'action': 'parse', 'format': 'json', 'formatversion': 2,
                'redirects': 1, 'prop': 'wikitext', 'page': lista}).get('parse', {}).get('wikitext', '')
        for fila in w.split('\n|-'):
            celdas = [c.strip() for c in re.split(r'\n\|(?!\})', fila) if c.strip()]
            if len(celdas) < 5:
                continue
            m = re.search(r'\[\[([^\]|#]+)(?:\|([^\]]*))?\]\]', celdas[0])
            if not m:
                continue
            numeros = [int(x.replace(',', '')) for c in celdas[1:]
                       for x in re.findall(r'(?<![\d-])(\d{1,3}(?:,\d{3})+)(?![\d-])', re.sub(r'<ref.*?(</ref>|/>)', '', c))]
            if not numeros:
                continue
            visible = m.group(2) or m.group(1)
            filas.append((m.group(1).strip(), max(numeros), "' and ''" in visible or ' and ' in visible))
    qid_de = {}
    for lote in lotes(sorted({f[0] for f in filas}), 50):
        r = api('https://en.wikipedia.org/w/api.php', {'action': 'query', 'format': 'json', 'formatversion': 2,
                'redirects': 1, 'prop': 'pageprops', 'ppprop': 'wikibase_item', 'titles': '|'.join(lote)})
        q = r.get('query', {})
        alias = {}
        for k in ('normalized', 'redirects'):
            for x in q.get(k, []):
                alias[x['to']] = alias.get(x['from'], x['from'])
        for p in q.get('pages', []):
            qq = p.get('pageprops', {}).get('wikibase_item')
            t = p['title']
            qid_de[t] = qq
            while t in alias:
                t = alias[t]
                qid_de[t] = qq
    for t, n, comb in filas:
        q = qid_de.get(t)
        if q and n > VENT.get(q, [0])[0]:
            VENT[q] = [n, comb]
    guardar(ruta_v, VENT)
print(f'Superventas: {len(VENT)} juegos en las listas de Wikipedia')

# ------------------------------------------------------------------ 3) resumen: Wikipedia en español o LaunchBox traducido
def sin_marcas_raras(t):
    """Letras latinas fuera del español (ō, ū, ł, š...) a su forma simple: "Kōji Kondō" -> "Koji Kondo"."""
    return ''.join(c if ord(c) < 0x100 or not unicodedata.decomposition(c)
                   else unicodedata.normalize('NFKD', c)[0] for c in t)


def texto_latino(t):
    """Quita lo que la fuente de la consola no puede mostrar: paréntesis con japonés/chino ("(メトロイド..., Metoroido?)"),
    espacios invisibles y otros símbolos fuera del alfabeto latino."""
    t = sin_marcas_raras(t)
    t = re.sub(r'[\u200b-\u200f\u2060\ufeff]', '', t)
    t = re.sub(r'\s*\([^()]*[\u3000-\u9fff\uac00-\ud7af\uff00-\uffef][^()]*\)', '', t)
    t = re.sub(r'[^\x00-\u024f\u2018-\u201d\u2013\u2014\u2026\n]', '', t)
    t = re.sub(r'[ \t]{2,}', ' ', t)
    return re.sub(r' +([,.;:])', r'\1', t).strip()


def limpiar_es(t, maximo=1100):
    t = texto_latino(t)
    pars = [p.strip() for p in t.split('\n') if p.strip()]
    out = ''
    for p in pars:
        if len(out) + len(p) > maximo and out:
            break
        out += ('\n\n' if out else '') + p
    return out


def recortar(t, maximo=1400):
    t = re.sub(r'\r', '', t).strip()
    if len(t) <= maximo:
        return t
    corte = t.rfind('.', 0, maximo)
    return t[:corte + 1] if corte > 200 else t[:maximo] + '…'


resumen_es = {}
for i, g in LB.items():
    t = titulos.get(i)
    es = W.get(t, {}).get('es') if t else None
    if es and len(ES.get(es, '')) > 80:
        resumen_es[i] = ('Wikipedia', limpiar_es(ES[es]))

ruta_t, TR = cache('traducciones.json')  # DatabaseID -> resumen traducido
pendientes = [i for i, g in LB.items() if i not in resumen_es and g['Overview'] and i not in TR]
if pendientes:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from traducir import traducir_lote
    t0 = time.time()
    def no_traducir(g):  # el título y sus partes ("The Legend of Zelda", "A Link to the Past")
        n = g['Name']
        partes = [n] + [p.strip() for p in re.split(r':| - | & | and |/', n)]
        return [p for p in partes if p] + [p[4:] for p in partes if p.startswith('The ')]
    for n, lote in enumerate(lotes(pendientes, 16)):
        res = traducir_lote([recortar(LB[i]['Overview']) for i in lote], [no_traducir(LB[i]) for i in lote])
        for i, r in zip(lote, res):
            TR[i] = r
        if n % 10 == 0:
            guardar(ruta_t, TR)
            hechas = min((n + 1) * 16, len(pendientes))
            print(f'  traducidos {hechas} de {len(pendientes)} ({time.time() - t0:.0f} s)', flush=True)
    guardar(ruta_t, TR)


# ------------------------------------------------------------------ 4) fichas
def anio(g):
    return (g['ReleaseDate'] or '')[:4] or g['ReleaseYear']


def ventas_txt(n):
    if n >= 1_000_000:
        return f'{n / 1_000_000:.2f}'.rstrip('0').rstrip('.').replace('.', ',') + ' millones'
    return f'{n:,}'.replace(',', '.') + ' unidades'


total = 0
for carpeta, roms in ids.items():
    d = os.path.join(SALIDA, carpeta, '_fichas')
    os.makedirs(d, exist_ok=True)
    for rom, i in roms.items():
        g = LB.get(i)
        if not g:
            continue
        lineas = [f'Titulo: {g["Name"]}']
        if anio(g): lineas.append(f'Año: {anio(g)}')
        if g['Developer']: lineas.append(f'Estudio: {g["Developer"]}')
        if g['Publisher'] and g['Publisher'] != g['Developer']: lineas.append(f'Editora: {g["Publisher"]}')
        if g['Genres']:
            lineas.append('Género: ' + ', '.join(GENEROS.get(x.strip(), x.strip()) for x in g['Genres'].split(';')))
        if g['MaxPlayers'] and g['MaxPlayers'] != '0':
            j = g['MaxPlayers'] + (' (cooperativo)' if g['Cooperative'] == 'true' and g['MaxPlayers'] != '1' else '')
            lineas.append(f'Jugadores: {j}')
        wd = WD.get(W.get(titulos.get(i), {}).get('qid') or '', {})
        if wd.get('director'): lineas.append('Director: ' + ', '.join(ETQ.get(p, '') for p in wd['director'] if ETQ.get(p)))
        if wd.get('musica'): lineas.append('Música: ' + ', '.join(ETQ.get(p, '') for p in wd['musica'] if ETQ.get(p)))
        qid = W.get(titulos.get(i), {}).get('qid') or ''
        if qid in VENT:  # las listas de superventas son más completas que Wikidata
            n, comb = VENT[qid]
            lineas.append(f'Ventas: {ventas_txt(n)}' + (' (contando todas sus versiones)' if comb else ''))
        elif wd.get('ventas'):
            lineas.append(f'Ventas: {ventas_txt(wd["ventas"])}')
        if g['CommunityRating']:
            lineas.append(f'Nota: {float(g["CommunityRating"]):.1f}'.replace('.', ',') +
                          f' de 5 ({g["CommunityRatingCount"]} votos)')
        if i in resumen_es:
            fuente, texto = resumen_es[i]
            lineas.append('Fuente: Wikipedia en español')
        elif TR.get(i):
            texto = texto_latino(TR[i])
            lineas.append('Fuente: LaunchBox (traducido automáticamente)')
        else:
            texto = ''
        lineas = [l for l in lineas if not l.endswith(': ')]
        with open(os.path.join(d, re.sub(r'\.[^.]+$', '', rom) + '.txt'), 'w') as o:
            o.write(sin_marcas_raras('\n'.join(lineas)) + '\n\n' + texto.strip() + '\n')
        total += 1
print(f'{total} fichas escritas en {SALIDA}/<consola>/_fichas/')
