# (Lo usa fichas.py. Necesita el entorno luckfox_consola/datos_notas/venv con ctranslate2 y sentencepiece,
#  y el modelo de Argos en datos_notas/modelo_en_es.)
# Traductor inglés -> español offline (modelo de Argos Translate con ctranslate2, sin internet).
import re, ctranslate2, sentencepiece
import os
# Modelo de Argos: por defecto en ../luckfox_consola/datos_notas (al lado de este repositorio); otra ruta con
# la variable MODELO_EN_ES.
_M = os.environ.get('MODELO_EN_ES') or os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', 'luckfox_consola', 'datos_notas', 'modelo_en_es', 'en_es'))
_tr = ctranslate2.Translator(f'{_M}/model', device='cpu', inter_threads=4)
_sp = sentencepiece.SentencePieceProcessor(model_file=f'{_M}/sentencepiece.model')

def oraciones(texto):
    partes = []
    for parrafo in re.split(r'\n\s*\n|\n', texto):
        parrafo = parrafo.strip()
        if parrafo:
            trozos, unidas = re.split(r'(?<=[.!?])\s+(?=[A-Z0-9"\'(])', parrafo), []
            for tr in trozos:  # no cortar después de abreviaturas ("Dr. Wily", "Mr. X", "vs. Y")
                if unidas and re.search(r'\b(Dr|Mr|Mrs|Ms|St|Jr|Sr|vs|Vol|No|Prof|Capt|Lt|Gen)\.$', unidas[-1]):
                    unidas[-1] += ' ' + tr
                else:
                    unidas.append(tr)
            partes.append(unidas)
    return partes

# Nombres propios que también son palabras en inglés: se cambian por una marca antes de traducir y se
# devuelven después ("Link" salía "El enlace").
PROTEGIDOS = ['Link', 'Shadow', 'Knuckles', 'Tails', 'Fox', 'Falco', 'Ness', 'Pit', 'Rock', 'Roll', 'Proto Man',
              'Mega Man', 'Bass', 'Zero', 'Dark Link', 'Master Sword', 'Hammer Bros', 'Boo', 'Star Fox', 'Golden Sun',
              'Wonder', 'Fire Emblem', 'Advance Wars', 'Game Boy', 'Game Boy Advance', 'Super Nintendo']
PROTEGIDOS.sort(key=len, reverse=True)
_PAT = re.compile(r'\b(' + '|'.join(re.escape(w) for w in PROTEGIDOS) + r')\b')

def _proteger(t, extras=()):
    usados = []
    def f(m):
        usados.append(m.group(1))
        return f'Zq{len(usados) - 1}x'
    # primero los nombres propios de este texto (título del juego y sus partes), de más largo a más corto
    for e in sorted({x for x in extras if len(x) >= 4}, key=len, reverse=True):
        t = re.sub(r'(?<![\w])(' + re.escape(e) + r')(?![\w])', f, t)
    return _PAT.sub(f, t), usados

def _devolver(t, usados):
    return re.sub(r'Zq\s?(\d+)\s?x', lambda m: usados[int(m.group(1))] if int(m.group(1)) < len(usados) else '', t)

def traducir_lote(textos, extras=None):
    """Lista de textos en inglés -> lista en español, conservando los párrafos.
    extras: por cada texto, nombres que no se deben traducir (p. ej. el título del juego)."""
    prot = [_proteger(t, extras[k] if extras else ()) for k, t in enumerate(textos)]
    res = _traducir([p[0] for p in prot])
    return [_devolver(r, p[1]) for r, p in zip(res, prot)]

def _traducir(textos):
    estructura, todas = [], []
    for t in textos:
        ps = oraciones(t)
        estructura.append([len(p) for p in ps])
        for p in ps: todas.extend(p)
    if not todas: return ['' for _ in textos]
    tok = _sp.encode(todas, out_type=str)
    res = _tr.translate_batch(tok, beam_size=2, max_batch_size=64)
    trad = [_sp.decode(r.hypotheses[0]) for r in res]
    out, i = [], 0
    for est in estructura:
        pars = []
        for n in est:
            pars.append(' '.join(trad[i:i + n])); i += n
        out.append('\n\n'.join(pars))
    return out
