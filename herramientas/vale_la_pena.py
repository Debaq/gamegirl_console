# Genera roms/_vale_la_pena.json: grupos (ranking general y sagas en orden de salida) con la mejor versión
# disponible de cada juego, prefiriendo español. Valida que cada archivo exista en la carpeta de la SD.
# Uso: python3 -I armar_vale.py SALIDA.json
import json, os, re, sys
P = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))  # carpeta con los proyectos
CARP = {'gba': f'{P}/luckfox_consola/sd/roms/gba', 'snes': f'{P}/luckfox_consola/sd/roms/snes',
        'gb': f'{P}/maix_dock/sd/roms/gb', 'gbc': f'{P}/maix_dock/sd/roms/gbc', 'nes': f'{P}/maix_dock/sd/roms/nes',
        'homebrew': f'{P}/maix_dock/sd/roms/homebrew'}
EXT = re.compile(r'\.(gba|gb|gbc|nes|smc|sfc)$', re.I)
archivos = {s: {EXT.sub('', f).lower(): f for f in os.listdir(d) if EXT.search(f)} for s, d in CARP.items()}

ZELDA_LA = ('gb', 'Zelda DX Espanol')
G = [
 ('Lo mejor', [
  ('gba', 'Legend of Zelda, The - A Link to the Past & Four Swords'), ('snes', 'Super Metroid'),
  ('snes', 'Chrono Trigger'), ('gba', 'Final Fantasy VI Advance'), ('gba', 'Super Mario Advance 2 - Super Mario World'),
  ('gba', 'Castlevania Aria Of Sorrow'), ('gba', 'Metroid Fusion'), ZELDA_LA, ('gbc', 'Pokemon - Edicion Cristal'),
  ('gba', 'Super Mario Advance 4 - Super Mario Bros. 3'), ('gba', "Super Mario Advance 3 - Yoshi's Island"),
  ('gba', 'Mother 3'), ('gba', 'Golden Sun'), ('gba', 'Advance Wars 2 - Black Hole Rising'), ('gba', 'Fire Emblem'),
  ('snes', 'Super Mario RPG - Legend of the Seven Stars'), ('gba', 'Metroid - Zero Mission'),
  ('gba', 'Legend of Zelda, The - The Minish Cap'), ('gba', 'Mario & Luigi - Superstar Saga'),
  ('snes', 'Mega Man X'), ('snes', 'Kirby Super Star'), ('gba', 'Donkey Kong Country 2'), ('gba', 'Wario Land 4'),
  ('gba', 'Golden Sun - La Edad Perdida'), ('snes', 'Secret of Mana'), ('snes', 'Super Castlevania IV'),
  ('gba', 'Pokemon - Edicion Esmeralda'), ('gbc', 'Legend of Zelda The - Oracle of Seasons'),
  ('gba', 'Final Fantasy Tactics Advance'), ('snes', 'Super Mario Kart'), ('snes', 'Contra III - The Alien Wars'),
  ('gba', 'Megaman Zero'), ('snes', 'Super Punch-Out!!'), ('gba', 'Mother 1 + 2'), ('gba', 'Final Fantasy IV Advance'),
  ('gb', 'Tetris DX'), ('gbc', 'Wario Land 3'), ('gba', 'Harvest Moon - Friends of Mineral Town'),
  ('gba', 'Tactics Ogre - The Knight of Lodis'), ('snes', 'Star Fox'), ('gba', 'Kirby & the Amazing Mirror'),
  ('gb', 'Super Mario Land 2 - 6 Golden Coins'), ('gb', 'Metroid II - Return of Samus'),
  ('snes', 'Teenage Mutant Ninja Turtles IV - Turtles in Time'), ('gba', 'Sonic Advance 2'),
  ('gba', 'Boktai - The Sun Is in Your Hand'), ('snes', 'Illusion of Gaia'), ('gba', 'Drill Dozer'),
 ]),
 ('Zelda', [('gba', 'Legend of Zelda, The - A Link to the Past & Four Swords'), ZELDA_LA,
  ('gbc', 'Legend of Zelda The - Oracle of Seasons'), ('gbc', 'Legend of Zelda The - Oracle of Ages'),
  ('gba', 'Legend of Zelda, The - The Minish Cap')]),
 ('Mario', [('gbc', 'Super Mario Bros. Deluxe'), ('gba', 'Super Mario Advance'),
  ('gba', 'Super Mario Advance 4 - Super Mario Bros. 3'), ('gb', 'Super Mario Land'),
  ('gba', 'Super Mario Advance 2 - Super Mario World'), ('gb', 'Super Mario Land 2 - 6 Golden Coins'),
  ('gba', "Super Mario Advance 3 - Yoshi's Island"), ('snes', 'Super Mario RPG - Legend of the Seven Stars'),
  ('gba', 'Mario & Luigi - Superstar Saga'), ('gba', 'Mario vs. Donkey Kong')]),
 ('Mario: carreras y deportes', [('snes', 'Super Mario Kart'), ('gbc', 'Mario Golf'), ('gbc', 'Mario Tennis'),
  ('gba', 'Mario Kart Super Circuit'), ('gba', 'Mario Golf - Advance Tour'), ('gba', 'Mario Tennis Advance - Power Tour'),
  ('gba', 'Mario Party Advance')]),
 ('Metroid', [('gba', 'Metroid - Zero Mission'), ('gb', 'Metroid II - Return of Samus'), ('snes', 'Super Metroid'),
  ('gba', 'Metroid Fusion')]),
 ('Castlevania', [('gba', 'Castlevania'), ('gb', 'Castlevania Adventure The'), ('gb', 'Castlevania II - Belmonts Revenge'),
  ('snes', 'Super Castlevania IV'), ('snes', 'Castlevania - Dracula X'), ('gb', 'Castlevania - Legends'),
  ('gba', 'Castlevania - Circle of the Moon'), ('gba', 'Castlevania - Harmony of Dissonance'),
  ('gba', 'Castlevania Aria Of Sorrow')]),
 ('Pokemon', [('gb', 'Pokemon - Edicion Roja'), ('gb', 'Pokemon - Edicion Azul'), ('gb', 'Pokemon Amarillo'),
  ('gb', 'Pokemon - Edicion Oro'), ('gb', 'Pokemon - Edicion Plata'), ('gbc', 'Pokemon - Edicion Cristal'),
  ('gba', 'Pokemon - Rubi'), ('gba', 'Pokemon - Zafiro'), ('gba', 'Pokemon - Edicion Rojo Fuego'),
  ('gba', 'Pokemon - Edicion Verde Hoja'), ('gba', 'Pokemon - Edicion Esmeralda'),
  ('gba', 'Pokemon Mundo misterioso - Equipo de rescate rojo'), ('gb', 'Pokemon Pinball'),
  ('gba', 'Pokemon - Pinball Rubi & Zafiro'), ('gb', 'Pokemon Trading Card Game')]),
 ('Final Fantasy', [('gba', 'Final Fantasy I & II - Dawn of Souls'), ('gba', 'Final Fantasy IV Advance'),
  ('gba', 'Final Fantasy V Advance'), ('gba', 'Final Fantasy VI Advance'), ('gba', 'Final Fantasy Tactics Advance')]),
 ('Mana', [('gba', 'Sword of Mana'), ('snes', 'Secret of Mana')]),
 ('RPG de Square en SNES', [('snes', 'Chrono Trigger'), ('snes', 'Secret of Evermore'), ('snes', 'Final Fantasy - Mystic Quest')]),
 ('Mother', [('gba', 'Mother 1 + 2'), ('gba', 'Mother 3')]),
 ('Golden Sun', [('gba', 'Golden Sun'), ('gba', 'Golden Sun - La Edad Perdida')]),
 ('Fire Emblem', [('gba', 'Fire Emblem- Sword of Seals'), ('gba', 'Fire Emblem'), ('gba', 'Fire Emblem - The Sacred Stones')]),
 ('Advance Wars', [('gba', 'Advance Wars'), ('gba', 'Advance Wars 2 - Black Hole Rising')]),
 ('Tactics', [('gba', 'Tactics Ogre - The Knight of Lodis'), ('snes', 'Ogre Battle - The March of the Black Queen'),
  ('gba', 'Final Fantasy Tactics Advance'), ('gba', 'Onimusha Tactics')]),
 ('Breath of Fire', [('gba', 'Breath of Fire'), ('gba', 'Breath of Fire II')]),
 ('Quintet: ActRaiser y Gaia', [('snes', 'ActRaiser'), ('snes', 'Soul Blazer'), ('snes', 'Illusion of Gaia')]),
 ('Mega Man', [('gb', 'Megaman - Dr. Wilys Revenge'), ('gb', 'Megaman II'), ('gb', 'Megaman III'), ('gb', 'Megaman IV'),
  ('gb', 'Megaman V'), ('snes', 'Mega Man 7'), ('gba', 'Megaman & Bass')]),
 ('Mega Man X', [('snes', 'Mega Man X'), ('snes', 'Mega Man X2'), ('snes', 'Mega Man X3'), ('gb', 'Megaman Xtreme'),
  ('gbc', 'Megaman Xtreme 2')]),
 ('Mega Man Zero', [('gba', 'Megaman Zero'), ('gba', 'Megaman Zero 2'), ('gba', 'Megaman Zero 3'), ('gba', 'Megaman Zero 4')]),
 ('Mega Man Battle Network', [('gba', 'Megaman Battle Network'), ('gba', 'Megaman Battle Network 2'),
  ('gba', 'Megaman Battle Network 3 - Blue Version'), ('gba', 'Megaman Battle Network 4 - Red Sun'),
  ('gba', 'Megaman Battle Network 5 - Team Protoman'), ('gba', 'Megaman Battle Network 6 - Cybeast Gregar')]),
 ('Kirby', [('gb', 'Kirbys Dream Land'), ('snes', "Kirby's Dream Course"), ('gb', 'Kirbys Dream Land 2'),
  ('snes', 'Kirby Super Star'), ('snes', "Kirby's Dream Land 3"), ('gba', 'Kirby - Nightmare in Dream Land'),
  ('gba', 'Kirby & the Amazing Mirror')]),
 ('Donkey Kong', [('gb', 'Donkey Kong'), ('gba', 'Donkey Kong Country'), ('gb', 'Donkey Kong Land'),
  ('gba', 'Donkey Kong Country 2'), ('gb', 'Donkey Kong Land 2'), ('gba', 'Donkey Kong Country 3'),
  ('gb', 'Donkey Kong Land III'), ('gba', 'Mario vs. Donkey Kong')]),
 ('Wario', [('gb', 'Wario Land - Super Mario Land 3'), ('gb', 'Wario Land II'), ('gbc', 'Wario Land 3'),
  ('gba', 'Wario Land 4'), ('gba', 'WarioWare, Inc. - Minigame Mania'), ('gba', 'WarioWare - Twisted!'),
  ('gba', 'Drill Dozer')]),
 ('Street Fighter', [('snes', 'Street Fighter II - The World Warrior'), ('snes', 'Street Fighter II Turbo - Hyper Fighting'),
  ('snes', 'Super Street Fighter II - The New Challengers'), ('gba', 'Super Street Fighter II Turbo - Revival'),
  ('snes', 'Street Fighter Alpha 2'), ('gba', 'Street Fighter Alpha 3')]),
 ('Contra', [('snes', 'Contra III - The Alien Wars'), ('gba', 'Contra Advance - The Alien Wars EX')]),
 ('Sonic', [('gba', 'Sonic The Hedgehog - Genesis'), ('gba', 'Sonic Advance'), ('gba', 'Sonic Advance 2'),
  ('gba', 'Sonic Advance 3'), ('gba', 'Sonic Battle')]),
 ('Crash', [('gba', 'Crash Bandicoot XS'), ('gba', 'Crash Bandicoot 2 - N-Tranced'), ('gba', 'Crash Nitro Kart'),
  ('gba', 'Crash Bandicoot Fusion')]),
 ('Donkey Kong y Diddy: plataformas de Rare', [('gba', 'Banjo-Kazooie - La Venganza de Grunty')]),
 ('Harvest Moon', [('snes', 'Harvest Moon'), ('gb', 'Harvest Moon GB'), ('gb', 'Harvest Moon 2 GBC'),
  ('gbc', 'Harvest Moon 3 GBC'), ('gba', 'Harvest Moon - Friends of Mineral Town')]),
 ('Dragon Ball', [('gba', 'Dragon Ball Z - The Legacy of Goku'), ('gba', 'Dragon Ball Z - The Legacy of Goku II'),
  ('gba', "Dragon Ball Z - Buu's Fury"), ('gba', 'Dragon Ball Z - Supersonic Warriors'),
  ('gba', 'Dragon Ball - Advanced Adventure')]),
 ('Homebrew destacado', [('homebrew', 'Tobu Tobu Girl Deluxe'), ('homebrew', 'Deadeus'),
  ('homebrew', 'Libbet and the Magic Floor'), ('homebrew', 'Shock Lobster'), ('homebrew', 'Dangan GB2'),
  ('homebrew', 'Mona and the Witchs Hat'), ('homebrew', 'Black Castle'), ('gba', 'Celeste Classic')]),
]
G = [g for g in G if not g[0].startswith('Donkey Kong y Diddy')]  # grupo de un solo juego: fuera

salida, faltan = [], []
for nombre, juegos in G:
    lista = []
    for sis, t in juegos:
        f = archivos[sis].get(t.lower())
        if f: lista.append([sis, f])
        else: faltan.append(f'{nombre}: {sis}/{t}')
    if lista: salida.append({'nombre': nombre, 'juegos': lista})
json.dump({'grupos': salida}, open(sys.argv[1], 'w'), ensure_ascii=False, indent=1)
print(len(salida), 'grupos,', sum(len(g['juegos']) for g in salida), 'entradas')
for f in faltan: print('  NO ENCONTRADO', f)
