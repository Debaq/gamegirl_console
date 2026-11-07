# Cruza las ROMs de la SD con la base de LaunchBox (Metadata.xml, se baja una vez de
# https://gamesdb.launchbox-app.com/Metadata.zip) y guarda por consola roms/<carpeta>/_notas.json:
#   {"archivo.gba": [nota 1-5, votos, año, "género"], ...}
# La consola solo lee ese archivo: no necesita internet. Volver a correrlo solo si se agregan juegos.
#
# Uso: python3 -I notas.py Metadata.xml SALIDA_RAIZ carpeta=ruta [carpeta=ruta ...] [--informe]
#   SALIDA_RAIZ/<carpeta>/_notas.json se escribe para cada carpeta.
#   --ids=ARCHIVO guarda además {carpeta: {rom: DatabaseID}} (lo usa fichas.py; no va a la SD).
import difflib, json, os, re, sys, unicodedata
import xml.etree.ElementTree as ET

PLAT = {'gb': ['Nintendo Game Boy', 'Nintendo Game Boy Color'],
        'gbc': ['Nintendo Game Boy Color', 'Nintendo Game Boy'],
        'gba': ['Nintendo Game Boy Advance'],
        'snes': ['Super Nintendo Entertainment System'],
        'nes': ['Nintendo Entertainment System'],
        'homebrew': ['Nintendo Game Boy', 'Nintendo Game Boy Color', 'Nintendo Entertainment System',
                     'Nintendo Game Boy Advance']}
EXT = re.compile(r'\.(gba|gb|gbc|nes|smc|sfc)$', re.I)

# Nombres de archivo que no calzan solos (español, hacks con nombre propio, abreviaturas).
MANUAL = {
    'zelda dx espanol': "The Legend of Zelda: Link's Awakening DX",
    'pokemon amarillo': 'Pokémon Yellow Version: Special Pikachu Edition',
    'castlevania aria of sorrow': 'Castlevania: Aria of Sorrow',
    'super mario advance 4 - super mario bros. 3': 'Super Mario Advance 4: Super Mario Bros. 3',
    'mother 1 + 2': 'Mother 1+2',
}


def clave(s):
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode().lower()
    s = s.replace('&', ' and ').replace('+', ' and ')
    s = re.sub(r'\b(\w[\w\s]*?), the\b', r'the \1', s)
    s = re.sub(r'^the\s+', '', s)
    s = re.sub(r'\s+the(?=\s*(-|:|$))', '', s)  # "Addams Family The - ..." (nombres de GB)
    s = re.sub(r'\bversion\b|\bedition\b|\bedicion\b', '', s)
    return re.sub(r'[^a-z0-9]', '', s)


ROMANOS = {'i': '1', 'ii': '2', 'iii': '3', 'iv': '4', 'v': '5', 'vi': '6', 'vii': '7', 'viii': '8', 'ix': '9', 'x': '10'}


def numeros(s):
    """Números de un título (romanos pasados a arábigos): un cruce por parecido debe tener los mismos."""
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode().lower()
    out = []
    for w in re.findall(r'[a-z0-9]+', s):
        if w.isdigit():
            out.append(str(int(w)))
        elif w in ROMANOS and w not in ('i', 'v', 'x'):  # i, v y x sueltas suelen ser letras, no números
            out.append(ROMANOS[w])
    return out


def limpiar_archivo(f):
    b = EXT.sub('', f)
    b = re.sub(r'\s*(\([^()]*\)|\[[^\[\]]*\])', '', b).strip()
    return b


def leer_base(ruta, plataformas):
    juegos, alternos = {}, {}
    for _, el in ET.iterparse(ruta, events=('end',)):
        if el.tag == 'Game':
            p = el.findtext('Platform')
            if p in plataformas:
                r = el.findtext('CommunityRating')
                anio = el.findtext('ReleaseYear') or (el.findtext('ReleaseDate') or '')[:4]
                juegos[el.findtext('DatabaseID')] = {
                    'nombre': el.findtext('Name') or '', 'plat': p,
                    'nota': float(r) if r else None, 'votos': int(el.findtext('CommunityRatingCount') or 0),
                    'anio': int(anio) if anio and anio.isdigit() else 0,
                    'genero': (el.findtext('Genres') or '').split(';')[0].strip()}
            el.clear()
        elif el.tag == 'GameAlternateName':
            i = el.findtext('DatabaseID')
            if i in juegos:
                alternos.setdefault(i, []).append(el.findtext('AlternateName') or '')
            el.clear()
    return juegos, alternos


def main():
    xml, raiz = sys.argv[1], sys.argv[2]
    carpetas = dict(a.split('=', 1) for a in sys.argv[3:] if '=' in a and not a.startswith('--'))
    informe = '--informe' in sys.argv
    ruta_ids = next((a[6:] for a in sys.argv if a.startswith('--ids=')), None)
    ids = {}
    todas = {p for c in carpetas for p in PLAT[c]}
    juegos, alternos = leer_base(xml, todas)
    # índice por plataforma: clave -> id (si se repite, el que tiene más votos)
    indice, nums = {}, {}
    for i, j in juegos.items():
        for n in [j['nombre']] + alternos.get(i, []):
            k = clave(n)
            if not k:
                continue
            d = indice.setdefault(j['plat'], {})
            if k not in d or juegos[d[k]]['votos'] < j['votos']:
                d[k] = i
                nums[(j['plat'], k)] = numeros(n)
    for carpeta, ruta in carpetas.items():
        notas, sin, dudosos = {}, [], []
        for f in sorted(os.listdir(ruta)):
            if not EXT.search(f):
                continue
            base = limpiar_archivo(f)
            buscado = MANUAL.get(base.lower(), base)
            k = clave(buscado)
            hallado = None
            plats = PLAT[carpeta]
            if carpeta == 'homebrew':  # la plataforma la dice la extensión (un .gbc no es de GBA)
                e = f.rsplit('.', 1)[-1].lower()
                plats = PLAT['nes'] if e == 'nes' else PLAT['gba'] if e == 'gba' else PLAT['gb']
            for p in plats:
                d = indice.get(p, {})
                if k in d:
                    hallado = d[k]
                    break
            mis_nums = numeros(buscado)
            if not hallado and carpeta != 'homebrew' and len(k) >= 12:
                # nombre cortado (los de GB vienen truncados a ~40 letras): el único que empiece igual
                for p in PLAT[carpeta]:
                    d = indice.get(p, {})
                    c = [x for x in d if x.startswith(k) and nums[(p, x)] == mis_nums]
                    if len({d[x] for x in c}) == 1:
                        hallado = d[c[0]]
                        dudosos.append((f, juegos[hallado]['nombre']))
                        break
            if not hallado and carpeta != 'homebrew' and len(k) >= 6:
                for p in PLAT[carpeta]:
                    d = indice.get(p, {})
                    m = [x for x in difflib.get_close_matches(k, d.keys(), n=5, cutoff=0.9)
                         if nums[(p, x)] == mis_nums]
                    if m:
                        hallado = d[m[0]]
                        dudosos.append((f, juegos[hallado]['nombre']))
                        break
            if hallado and carpeta == 'homebrew' and juegos[hallado]['anio'] < 2005:
                hallado = None  # homebrew con nombre genérico ("Pong", "Snake") que calzó con un juego comercial viejo
            if hallado:
                ids.setdefault(carpeta, {})[f] = hallado
            j = juegos.get(hallado) if hallado else None
            if j and j['nota']:
                notas[f] = [round(j['nota'], 1), j['votos'], j['anio'], j['genero']]
            else:
                sin.append(f)
        os.makedirs(os.path.join(raiz, carpeta), exist_ok=True)
        with open(os.path.join(raiz, carpeta, '_notas.json'), 'w') as o:
            json.dump(notas, o, ensure_ascii=False, separators=(',', ':'))
        total = len(notas) + len(sin)
        print(f'{carpeta}: {len(notas)} de {total} con nota ({100 * len(notas) // max(total, 1)} %), '
              f'{len(dudosos)} por parecido')
        if informe:
            for f, n in dudosos[:15]:
                print(f'   ~ {f}  =>  {n}')
            print('   sin nota (muestra):', ', '.join(sin[:12]))
    if ruta_ids:
        with open(ruta_ids, 'w') as o:
            json.dump(ids, o, ensure_ascii=False)


main()
