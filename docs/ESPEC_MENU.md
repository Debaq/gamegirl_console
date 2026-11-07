# ESPEC_MENU.md: Menú "Game Girl Color" (MaixPy K210) para portar a C

Fuente de verdad: ../maix_dock/menu.py. Pantalla lógica 480x320; colores RGB888 que MaixPy dibuja en RGB565. Los números de línea entre paréntesis son de menu.py.

## 1. Flujo general

### 1.1 Arranque
- main.py apaga el WiFi (IO8 en bajo, específico del K210) y llama menu.main().
- Orden de main():
  1. Escribe en el log "== arranque (causa: X) ==". Si hay informe de cuelgue previo, lo vuelca al log como "CUELGUE ..." y lo borra.
  2. Inicia pantalla y botones.
  3. Si START está apretado al encender, no abre el menú (va al REPL). En Linux equivale a salir sin menú.
  4. Reserva los búferes grandes: pantalla del menú 480x320, panel de pausa 300x304 y efectos de sonido. Es por fragmentación del heap del K210 y no hace falta en Linux.
  5. Si CFG["intro"] es verdadero, reproduce la intro. Si falla, sigue.
  6. Entra al bucle del menú.
- Si faltan los módulos de firmware (mando o lcd.ST7796), no abre nada.

### 1.2 Intro (dibujar_intro, líneas 362-420)
- Duración total T_FIN = 3.1 s. Cada frame se redibuja completo (fondo FONDO) con t = segundos desde el inicio.
- Cualquier botón, tecla o toque salta la intro.
- Con volumen > 0 suena el jingle intro_vN.raw (N = volumen 1..10) a 16000 Hz. Arranca justo antes del primer frame.
- Al terminar, espera a que acabe el jingle (salvo que se haya saltado). No lo corta.

Constantes de tiempo:
- T_LETRAS = (0.55, 0.75, 0.95, 1.15): caen G, A, M, E.
- T_DOCK = 1.35: entra "GIRL".
- T_ACORDE = 1.60: barra de colores.
- T_COLOR = (1.95, 2.07, 2.19, 2.31, 2.43): saltan C, O, L, O, R.
- T_BRILLO = (2.62, 2.70, 2.78, 2.86): destellos.
- T_FIN = 3.1.

Escenas en orden:
1. t < 0.45, encendido tipo TV (en esta fase no se dibuja nada más):
   - t < 0.25: rectángulo blanco (255,255,255) centrado, ancho int(480*t/0.25) (mínimo 2), alto 3, en y = 159.
   - 0.25 <= t < 0.45: franja de ancho 480 y alto int(320*(t-0.25)/0.2) (mínimo 3), centrada verticalmente, color (40,50,100).
2. t >= 0.45, estrellas:
   - 45 estrellas, i = 0..44: x0 = (i*197)%480, y = (i*89+13)%320, v = 1 + i%3.
   - x = int(x0 - v*t*60) % 480 (se mueven a la izquierda con wrap).
   - Brillo c = 90 + v*50, color (c, c, min(255, c+40)).
   - Tamaño 2x2 si v > 1; si v = 1, 1x1.
3. Letras G A M E. Cada una aparece cuando dt = t - T_LETRAS[i] >= 0.
   - x = X_LOGO + i*48, CELDA1 = 8 (letra de 40x56), INCL1 = 2, colores BLANCO.
   - dt < 0.2: cae. y = int(-70 + (Y_LOGO+70)*suave(dt/0.2)).
   - 0.2 <= dt < 0.45: rebota. y = Y_LOGO - int(14*(1-(dt-0.2)/0.25)*abs(sin((dt-0.2)*12))).
   - dt >= 0.45: y = Y_LOGO.
   - suave(x) = 1-(1-x)^3, con clamp a 0..1.
4. GIRL (desde dt = t - 1.35).
   - Entra desde la derecha: dx = int(480*(1-suave(dt/0.3))).
   - Letras en X_GIRL + i*48 + dx, y = Y_LOGO, color ROSADO.
5. Barra de colores (desde dt = t - 1.60).
   - w = int((X_COLOR - 18 - X_LOGO) * suave(dt/0.35)), a y = Y_COLOR + 14.
   - 4 rectángulos de alto 7: x = X_LOGO + i*w//4, ancho w//4 + 1, colores COLORES[i].
6. COLOR: letras i = 0..4, d = t - T_COLOR[i]. Si d < 0 no dibuja esa ni las siguientes.
   - x = X_COLOR + i*30, y = Y_COLOR - int(10*(1-suave(d/0.15))).
   - Celda 5, incl 1.
   - Colores de la letra: (mezcla(c, blanco, 0.45), c, mezcla(c, FONDO, 0.65)), con c = COLORES_LOGO[i].
7. Destellos (cuatro, i = 0..3).
   - Cada uno dura 0.3 s desde T_BRILLO[i].
   - Posiciones: (X_LOGO+4, Y_LOGO-6), (X_GIRL+190, Y_LOGO+4), (X_LOGO+120, Y_COLOR+30), (X_COLOR+150, Y_COLOR-4).
   - Cruz con radio r = int(12*(1-dt/0.3)) + 2.
     - Barra horizontal: rect(x-r, y-1, 2r, 3).
     - Barra vertical: rect(x-1, y-r, 3, 2r).
     - Color (255,255,220).

Constantes del logo de la intro:
- X_LOGO = 34, Y_LOGO = 104, X_GIRL = 34+4*48+24 = 250.
- X_COLOR = 34+400-150+5 = 289, Y_COLOR = 104+56+18 = 178.
- BLANCO = ((255,255,255) arriba, (165,175,230) abajo, (55,60,130) sombra).
- ROSADO = ((255,175,220), (230,50,145), (110,20,75)).
- COLORES = ((230,60,60), (140,190,40), (70,150,240), (250,200,40)).
- COLORES_LOGO = ((235,60,70), (190,90,230), (120,200,60), (250,200,40), (60,150,245)) para C, O, L, O, R.
- mezcla(a,b,k) = interpolación lineal por componente, con int() truncando.

Algoritmo de letra(img, ch, x, y, celda, incl, (arriba, abajo, sombra)) (líneas 339-351):
- La letra es un mapa 5x7. TRAMOS = segmentos horizontales continuos de "#" por fila (fila r, col inicial c0, largo n).
- Primero dibuja todas las sombras. Para cada tramo: rectángulo color sombra en x + (6-r)*incl + c0*celda + celda//2, y + r*celda + celda//2, tamaño n*celda por celda.
- Después dibuja el cuerpo. Para cada tramo, con xx = x + (6-r)*incl + c0*celda y yy = y + r*celda:
  - Mitad superior: m = celda//2. Rectángulo (xx, yy, n*celda, m) con color mezcla(arriba, abajo, r/7).
  - Mitad inferior: rectángulo (xx, yy+m, n*celda, celda-m) con color mezcla(arriba, abajo, (r+0.5)/7).
- Es cursiva por desplazamiento de 1 incl por fila. Las filas de abajo van más a la izquierda, porque (6-r) crece al subir.

### 1.3 Menú
- Ver secciones 2 a 4.
- Al encender, la pestaña activa es RECIENTES (0) si tiene juegos o si no hay consolas reales. Si no, es la primera consola real (índice 2).
- Si no hay ninguna consola con juegos, muestra "No hay juegos en la SD" (ver detalles finos).

### 1.4 Lanzar juego (lanzar, 1867-1905)
1. Calcula tipo = emulador_de(...).
2. agregar_reciente y guarda consola.json.
3. Sonido "abrir".
4. Dibuja pantalla_carga (ver 2.8).
5. Descarta la carátula en caché, espera a soltar todos los botones y espera a que termine de sonar el efecto.
6. Log "juego: <ruta> (<tipo>) mem libre N".
7. Ejecuta el juego (jugar_py si tipo es "py"; si no, jugar).
8. Al volver:
   - Log "salio: <ruta>".
   - Reinicia pantalla y botones.
   - Suena "error" si hubo excepción; si no, "volver".
   - Si la pestaña activa es RECIENTES, pone la selección en 0 (el recién jugado quedó primero).
   - Invalida la cabecera, espera a soltar y redibuja.
   - Si hubo error, muestra un recuadro rojo 3 s y redibuja sin él.
- Excepción durante el juego: aviso = "NombreExc: msg", log "error: <ruta> <aviso>".
- Ctrl-C dentro del juego equivale a salir al menú.
- Juegos .py (ejecutados con exec): no tienen pausa, y al terminar se reinicia todo.

### 1.5 Dentro de jugar()
- emu_cargar y luego un bucle: emu_correr(tipo) corre hasta que el hardware detecta la pausa (SELECT+START mantenidos ~0,25 s = 15 cuadros, sin A ni B). Luego abre menu_pausa:
  - "seguir": vuelve a emu_correr.
  - "reiniciar": libera el emulador, gc, vuelve a cargar la ROM y corre.
  - "salir": si la nota del juego es None, llama preguntar_gusto; luego sale del bucle. emu_liberar siempre se ejecuta.
- En el PC, Esc abre la pausa.

### 1.6 Apagado y log
- No hay apagado en el menú. El apagado físico es cortar energía.
- Log en /sd/logs/consola.log, formato "%7.1f texto\n" con uptime en segundos (ticks_ms/1000).
- Rotación: si pesa más de 256 KB (LOG_MAX), el viejo se renombra a consola.log.1 y se borra el .1 previo.
- Eventos registrados:
  - "== arranque (causa: X) ==" o "== arranque (firmware sin vigia) ==".
  - "CUELGUE <informe>" (informe tabulado con 8 espacios en las líneas siguientes).
  - "juego: <ruta> (<tipo>) mem libre N".
  - "salio: <ruta>".
  - "error: <ruta> <aviso>".
  - "nota: <Sistema>/<archivo> gusto|no gusto|sin opinion".
  - "estado guardado: <archivo> ok|ERROR".
  - "estado cargado: <archivo> ok|ERROR".
- Pantalla Registro: ver 3.7.

## 2. Layout en píxeles

### 2.1 Colores (RGB)
| Nombre | Valor |
|---|---|
| FONDO | (10,12,30) |
| PANEL | (26,30,52) |
| TEXTO | (232,234,244) |
| TENUE | (120,126,156) |
| TEXTO_PIE | (190,194,214) |
| COLOR_A (rosa botón A, barras de volumen llenas) | (235,70,110) |
| COLOR_B (amarillo B) | (250,200,40) |
| COLOR_PILDORA (fondo de pastilla SELECT/START) | (78,84,120) |
| ROSA (favorito, FAVORITOS, REGISTRO) | (255,105,180) |
| línea superior del pie | (40,45,75) |
| pestaña inactiva | (36,41,68) |
| barra vacía de volumen / scrollbar vacía | (60,64,92) |
| fila elegida en AJUSTES | (44,50,86) |
| caja de aviso de error | (170,40,40) |
| "(ninguno)" / mensaje de sin resultados | (230,90,90) |

Colores de acento por pestaña:
- NES (230,60,60), GAME BOY (140,190,40), GB COLOR (200,90,220), JUEGOS (70,150,240).
- GBA (105,90,220), HOMEBREW (70,200,170).
- RECIENTES (250,200,40), FAVORITOS = ROSA, AJUSTES (150,156,190).

### 2.2 Constantes de lista
- FILA_H=30, FILAS=7, LISTA_Y=58, MAX_CHARS=17.
- ANCHO=480, ALTO=320.
- PORTADA_ESPERA=350 ms, MAX_RECIENTES=12.

### 2.3 Fuente (draw_string de MaixPy)
- No hay fuente a medida. Es el bitmap interno de MaixPy: ASCII 0x20 a 0x7E (95 glifos), "Envy Code B 10pt".
- Cada glifo tiene 8 px de ancho y 12 de alto, un byte por fila, bit 7 = columna izquierda. La tabla está en fw/MaixPy/components/micropython/port/src/omv/img/font.c, array ascii[], líneas 18-209 (ruta bajo ../maix_dock/). Es copiable tal cual: 95x12 = 1140 bytes. No la copié aquí.
- Escalado: scale real. Alto de glifo = round(12*scale), ancho = round(8*scale). Fila fuente = round(y/scale), columna fuente = round(x/scale). Es muestreo de vecino más cercano sin suavizado.
- Escalas usadas: 1 (8x12), 1.5 (12x18), 2 (16x24), 3 (24x36, "Te gusto?").
- Importante: mono_space por defecto es False. El avance depende del flag:
  - mono_space=True: avance = round(8*scale).
  - mono_space=False (proporcional):
    - Antes de dibujar, x -= round(col_primera*scale), con col_primera la primera columna con píxeles del glifo.
    - Después, x += round((col_ultima+2)*scale), con col_ultima la última columna con píxeles.
    - El espacio (sin píxeles) avanza round(3*scale).
  - Los len(texto)*8 del código asumen ancho fijo, pero la mayoría de textos son proporcionales, así que no coinciden exactamente.
- Los textos con mono_space=True explícito son:
  - Letras dentro de los botones del pie.
  - Cabecera "BUSCAR EN ...".
  - Campo de texto del buscador.
  - Teclas del teclado.
  - "REGISTRO".
  - Valores de AJUSTES, valor numérico del volumen y valores en la pausa.
  - Pestaña vacía.
  - Nombre de consola bajo la carátula y su fallback.
- "N de N" y el resto de textos de scale 1 son proporcionales.
- Los títulos de lista (scale 2) son proporcionales, por eso 17 caracteres caben en 262 px.
- Para clonar idéntico hay que reimplementar ese algoritmo de avance; los glifos salen de font.c.
- Fuera de ' '..'~' se dibuja un rectángulo hueco. Los textos del menú evitan acentos a propósito ("Todavia", "Categoria", etc.). Hay que mantener los textos sin acentos.
- titulo(archivo) = nombre sin extensión (rsplit(".",1)[0]) con "_" cambiado a " ". Si mide más de 17 caracteres, usa los primeros 16 más "~".

### 2.4 Cabecera y pestañas
- Cabecera: franja 480x44 en PANEL (y=0..43), línea de acento 480x2 en y=44..45 (color de la pestaña activa).
- Se renderiza en una imagen de 480x46 que solo se rehace al cambiar de pestaña (cab_sis). Se invalida también tras cambiar volumen y tras volver de un juego.
- celda_logo(n) = 3 si n<=4, si no 2. El logo chico logo_chico va en (12, 22 - celda*7//2).
  - Letras GAME en x + i*paso, con paso = celda*6, color BLANCO, incl 1.
  - GIRL empieza en x2 = x + 4*paso + celda*3, color ROSADO.
- Pestañas (rects_pestanas(n)), n = número de pestañas (7 sin GBA, 8 con GBA; el orden está en 3.1):
  - TAB_H=34, TAB_Y=5.
  - x_min = 12 + 9*c*6 + 6 + 22, x_max = 480 - 22.
  - sep = 6 si n<=4, si no 4.
  - w = min(50, (x_max-x_min-(n-1)*sep)//n).
  - x0 = x_max - n*w - (n-1)*sep, paso w+sep.
  - Ejemplos: n=7 da celda 2, w=40, x0=154, paso 44. n=8 da w=35, x0=150, paso 39. n=4 da celda 3, w=50, x0=240, paso 56.
- Pestaña activa: rect_redondo r=4 del color de acento, ícono a color. Inactiva: color (36,41,68) y ícono "apagado" (cada color se mezcla 55% hacia PANEL con mezcla(c, PANEL, 0.55)).
- Flechas triangulares de 4 px (flecha) en TENUE, a y=22:
  - Izquierda: punta en x = rects[0].x - 10, dx=-1.
  - Derecha: punta en x = rects[-1].x + rects[-1].w + 8, dx=+1.
- flecha(x,y,dx): para k=0..3, rectángulo (x - dx*k, y-k, 1, 2k+1).
- rect_redondo(x,y,w,h,color,r):
  - Rectángulo (x+r, y, w-2r, h).
  - Rectángulo (x, y+r, w, h-2r).
  - Si r>1, rectángulo (x+1, y+1, w-2, h-2).
  - Las esquinas de r=3 o 4 quedan recortadas a 1 px.

### 2.5 Lista de juegos
- Filas y = 58 + fila*30, para fila 0..6.
- Resaltado de la fila elegida: rect (8, y-2, 270, 28) color de acento.
- Texto (scale 2, sin mono) en (16, y+1): TEXTO normal, FONDO si está elegida.
- Marca izquierda: rect (8, y-2, 4, 28) si hay marca.
  - Pestaña virtual: color de la consola del juego.
  - Pestaña real: ROSA si es favorito; TENUE si la nota es -1 (visible por "No gustaron: Visibles"); nada en otro caso.
- Ventana: primero = min(max(0, sel - 3), max(0, n - 7)). El cursor queda centrado cuando hay espacio y se pega a los extremos.
- Barra de scroll (si n > 7):
  - Pista: (282, 56, 4, 208) en PANEL.
  - Altura del pulgar th = max(12, 208*7//n).
  - ty = 56 + (208-th)*primero//(n-7), en color de acento.
- Marquesina del seleccionado: ver 3.8.

### 2.6 Carátula e info
- Marco: (292, 56, 178, 158) en color de acento. Interior: (294, 58, 174, 154) en PANEL.
- Imagen JPG centrada en el interior: x = 294 + max(0,(174-w)//2), y = 58 + max(0,(154-h)//2).
  - La carátula nominal es 170x150.
  - Si es mayor que el interior, queda clipeada.
- Sin carátula (archivo .jpg inexistente o ilegible): ícono del sistema centrado en (294+87, 58+77-8), más el nombre de la consola debajo.
  - Texto: x = px + (pw - len*8)//2, y = py + ph//2 + 22, color de la consola, scale 1, mono.
- Info debajo, y = 58+154+10 = 222:
  - Nombre de consola real (de ese juego): scale 2 mono, color de la consola.
  - Si es favorito: texto "fav" ROSA scale 1 en (px + len*16 + 8, y+4).
  - y+22: "%d de %d" % (sel+1, n) en TENUE scale 1. Si el redibujo es el que lee la SD, añade "  -  %d KB" (tamaño de archivo en KB).
  - y+38: según emulador:
    - NES: "Pantalla: " + valor (TEXTO).
    - GB/GBC: "Pantalla: " + valor.
    - Solo GB clásico: y+52 nombre de paleta en TENUE.
- Toda esa zona usa px=294 como x.
- La carátula es <dir>/<nombre sin extensión>.jpg.
  - Se lee de la SD solo cuando la selección lleva quieta 350 ms (ver detalle fino 7).
  - Mientras tanto se muestra el ícono y el KB no aparece.

### 2.7 Pie (dibujar_pie, 709-739)
- Franja (0, 296, 480, 24) en PANEL. Línea superior (0, 296, 480, 1) en (40,45,75).
- Items alineados desde x=10, botón en y=300, texto en y=302 (scale 1, proporcional, TEXTO_PIE).
- Cada item: botón, luego texto con 5 px de separación; avance total 5 + len(texto)*8 + 10. Sin texto: avance +3.
- El item "TXT" es solo texto, con avance len*8 + 12.
- Botones:
  - A/B: círculo de 16x16 con anchos por fila (6,10,12,14,14,16,16,16,16,16,16,14,14,12,10,6) centrados. Color COLOR_A o COLOR_B. Letra en (x+4,y+2), color FONDO, scale 1, mono. Ancho devuelto: 16.
  - UD (arriba/abajo): dos triángulos y barra, en TEXTO_PIE.
    - Para k=0..3: rect(x+4-k, y+1+k, 2k+1, 1), y para UD además rect(x+4-k, y+14-k, 2k+1, 1).
    - Barra rect(x+3, y+5, 3, 6 si UD, 9 si U). Ancho 9.
  - "+": texto "+" TENUE en (x, y+2), ancho 8.
  - Otros (START, SELECT): pastilla rect_redondo(x, y+1, w, 14, COLOR_PILDORA, r=3) con w = len*8 + 10; texto TEXTO en (x+5, y+2), scale 1, mono.
- Indicador de volumen (si volumen=True), con vx = 430:
  - Parlante: rect (vx, y+9, 4, 6).
  - Cono: para k=0..3, rect (vx+4+k, y+8-k, 1, 8+2k) en TEXTO_PIE.
  - 5 barras: para k=0..4, h = 4 + 2k, rect (vx+12+6k, y+18-h, 4, h). Lleno (COLOR_A) si volumen > 2k, vacío (60,64,92) si no.

Contenido del pie (items):
- Pestaña real con juegos: A Jugar, START Buscar, B (sin texto), + (sin texto), UD Letra, SELECT Favorito|Quitar.
- Pestaña real filtrada (visibles is not None): TXT "Filtro: <cat> "<busqueda>" (N)", START Cambiar, SELECT Fav.
  - La parte de categoría se omite si es "Todas"; la de búsqueda se omite si está vacía.
  - Se unen con un espacio entre las dos que existan.
- RECIENTES/FAVORITOS (virtual): A Jugar, SELECT Favorito|Quitar.
- Pestaña vacía: pie sin items (solo barra y volumen).
- AJUSTES: ver 3.6.
- Registro: sin indicador de volumen.

### 2.8 Pantalla de carga
- Fondo FONDO.
- Marco de acento rect_redondo(137, 37, 206, 246, r=6); interior PANEL rect_redondo(140, 40, 200, 240, r=5).
- Carátula centrada horizontalmente en el interior, en y = 52. Sin carátula: ícono del sistema en (240, 125).
- Nombre del juego (sin extensión, "_" a espacio) en hasta 2 líneas de 22 caracteres:
  - Centrado: x = 140 + (200 - len*8)//2.
  - y = 212 + k*14, scale 1, TEXTO.
- "Cargando..." centrado a y = 250, color de acento, scale 1.

### 2.9 Pantalla vacía (RECIENTES/FAVORITOS sin juegos)
- Ícono de la pestaña en (240,120), centrado.
- Línea 1, scale 2 mono, centrada con len*16, en y=152.
- Línea 2, scale 1 mono, centrada con len*8, en y=180, color TENUE.
- Textos:
  - RECIENTES: "Todavia no jugaste nada" / "Los ultimos juegos van a aparecer aqui".
  - FAVORITOS: "Sin favoritos" / "SELECT sobre un juego lo marca como favorito".

### 2.10 Íconos
- Cada ícono es una matriz de caracteres con paleta; "." es transparente. Cada celda se dibuja como un píxel de 2x2 (PX_ICONO=2).
- Centrado: x0 = cx - ancho*2//2, y0 = cy - alto*2//2.
- Los datos exactos están en menu.py líneas 485-601 (diccionario ICONOS); cópialos de ahí tal cual. Resumen de dimensiones y paleta:
  - NES: 16x8. Paleta g(205,205,212), d(45,45,55), r(225,45,55), k(110,110,125).
  - GAME BOY: 10x14. Paleta g(205,205,212), d(70,72,88), s(150,180,60), k(45,45,55), r(175,35,95).
  - GB COLOR: 10x14. Paleta p(160,95,215), d(55,40,80), m(255,120,190), y(250,210,60), c(80,190,240), k(45,35,60), w(240,230,250).
  - JUEGOS: 12x12. Paleta r(235,60,80), k(60,60,75), g(205,205,212), y(250,200,40).
  - GBA: 16x7. Paleta p(105,90,220), k(35,35,50), s(125,175,95), d(40,40,60), y(240,80,120).
  - HOMEBREW: 12x12. Paleta w(245,245,250), g(120,255,150).
  - RECIENTES: 12x12. Paleta k(45,45,55), w(245,245,250).
  - FAVORITOS: 13x12. Paleta y(255,236,110), o(235,170,40).
  - AJUSTES: 12x12. Paleta g(225,228,240), k(45,45,55).
- Se dibujan por tramos horizontales del mismo carácter (equivalente a pintar pixel a pixel).
- La clave del ícono es el nombre de la pestaña.

### 2.11 Fuente del logo (5x7), para la intro y el logo chico
- Letras definidas: G A M E I R L C O P U S, en matrices 5x7 de "#" y ".". Datos exactos en menu.py líneas 291-304.
- Glifos (filas separadas por espacio):
  - G: .###. #...# #.... #.### #...# #...# .###.
  - A: .###. #...# #...# ##### #...# #...# #...#
  - M: #...# ##.## #.#.# #.#.# #...# #...# #...#
  - E: ##### #.... #.... ####. #.... #.... #####
  - I: ##### ..#.. ..#.. ..#.. ..#.. ..#.. #####
  - R: ####. #...# #...# ####. #.#.. #..#. #...#
  - L: #.... #.... #.... #.... #.... #.... #####
  - C: .###. #...# #.... #.... #.... #...# .###.
  - O: .###. #...# #...# #...# #...# #...# .###.
  - P: ####. #...# #...# ####. #.... #.... #....
  - U: #...# #...# #...# #...# #...# #...# .###.
  - S: .#### #.... #.... .###. ....# ....# ####.
- P, U y S se usan en "PAUSA".

## 3. Pestañas, orden y fuentes de juegos

### 3.1 Pestañas, en este orden
1. RECIENTES (virtual). 2. FAVORITOS (virtual). 3. consolas reales, solo las que tengan al menos un juego: NES, GAME BOY, GB COLOR, JUEGOS (.py), GBA (solo si beta y módulo gba), HOMEBREW. 4. AJUSTES.

Carpetas y extensiones:
| Pestaña | Carpeta | Extensiones | Condición |
|---|---|---|---|
| NES | /sd/roms/nes | .nes | módulo nes |
| GAME BOY | /sd/roms/gb | .gb, .gbc | módulo gb |
| GB COLOR | /sd/roms/gbc | .gbc, .gb | módulo gbc |
| JUEGOS | /sd/juegos | .py | siempre |
| GBA | /sd/roms/gba | .gba | beta y módulo gba |
| HOMEBREW | /sd/roms/homebrew | .nes, .gb, .gbc (los que tengan módulo) | alguno |

- Una consola sin juegos (tras ocultos y notas) no aparece como pestaña.
- En el port a C (3 emuladores: FCEUmm, Gambatte, gpSP), la decisión de qué pestañas existen será por carpeta y extensión.

### 3.2 Listado y orden
- listar(sis): os.listdir, filtra por extensión (sin distinguir mayúsculas), quita los que estén en _ocultos.json de esa carpeta, y ordena por nombre en minúsculas (key = nombre.lower()).
- Dos listas por consola:
  - todos (sin ocultos de _ocultos.json).
  - juegos = todos menos los que tienen nota -1, salvo ver_no_gustaron.
  - conjunto = set de todos. Recientes y favoritos valen aunque el juego esté oculto por nota.
- _ocultos.json oculta en las pestañas reales y también en RECIENTES/FAVORITOS (porque esos juegos no están en conjunto).

### 3.3 Categorías
- CATEGORIAS = ("En espanol", "Plataformas", "Accion", "Aventura", "RPG", "Puzles", "Deportes", "Carreras", "Disparos", "Lucha", "Estrategia", "Mesa y cartas", "Otros").
- Cada consola carga <dir>/_categorias.json (dict archivo a categoría). Si falta, el dict es vacío.
- s["categorias"] = ["Todas"] + [c de CATEGORIAS, en ese orden, que tengan al menos un juego visible].
- Un juego sin entrada cuenta como "Otros" al filtrar, pero "Otros" solo se ofrece como opción si algún juego tiene "Otros" explícito en el JSON (presentes usa cats.get(j), que da None para los ausentes).
- Navegación de categorías: solo dentro del buscador (3.4).

### 3.4 Búsqueda (START en una pestaña real)
- START en pestañas virtuales no hace nada.
- Pantalla dibujar_busqueda, sobre FONDO:
  - Cabecera (0,0,480,44) en PANEL.
  - Título "BUSCAR EN " + nombre en (12,12), color de acento, scale 2 mono.
  - Campo de texto: rect (12,54,456,34) en PANEL; texto + "_" en (20,60), scale 2 mono, TEXTO.
  - Contador "%d juegos" en (330,66), TENUE, scale 1. Total = juegos de la consola si no hay filtro; si no, los del resultado.
  - Fila de categoría: rect (12,94,268,26). Acento si está seleccionada (cur==-1), PANEL si no.
    - Texto "< Categoria: X >" en (18,100), scale 1.5, color FONDO si seleccionada y TEXTO si no.
  - Teclado de 8 columnas: x0=12, y0=126, cw=34, ch=27. Celda i: fila i//8, col i%8.
    - Rect (12+col*34, 126+fila*27, 30, 23).
    - Caracteres: "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 " (37 celdas).
    - El espacio se muestra como "_".
    - Etiqueta centrada a x + (30 - len*12)//2, y+4, scale 1.5, mono. Seleccionada: fondo de acento y texto FONDO. Normal: fondo PANEL, texto TEXTO.
    - Después van las acciones "BORRAR" y "OK" en la fila siguiente (fila = (37+7)//8 = 5): anchas de 4*34-4 = 132. BORRAR en x=12, OK en x=148.
  - Vista previa: px = 12 + 8*34 + 10 = 294.
    - "Resultados:" en (294,100), TENUE.
    - Hasta 12 títulos en (294, 116 + k*14), formato titulo(j)[:24], scale 1.
    - Si no hay resultados: "(ninguno)" en (294,116), color (230,90,90).
  - Pie: A Escribir, B Borrar, U Categoria, START Listo, sin indicador de volumen.
- Navegación (cur = índice de celda, -1 = fila de categoría, 0..38 = celdas incl. BORRAR/OK; hay 39 celdas):
  - DER: cur = (cur+1) % 39, con wrap. IZQ: cur = (cur-1) % 39.
  - ABAJO: cur = min(38, cur+8). ARRIBA: si cur < 8, cur = -1; si no, cur -= 8.
  - En la fila de categoría (cur==-1): DER/IZQ cambian categoría con wrap, ABAJO vuelve a cur=0.
  - A: en cur==-1 avanza la categoría; si no, escribe la celda (máx. 20 caracteres), BORRAR borra el último, OK equivale a START.
  - B: borra el último carácter.
  - START: aplica el filtro (ver abajo).
- Auto-repetición de las 4 flechas (solo dentro del buscador): al apretar, el primer evento a los 300 ms; luego cada 90 ms.
- Filtro: el texto en minúsculas se divide en palabras; todas deben estar contenidas en el nombre del juego (sin extensión, minúsculas). Con categoría distinta de "Todas", además debe coincidir. Si no hay palabras y la categoría es "Todas", res=None (sin filtro).
- Al aceptar con START:
  - Si hay filtro y 0 resultados: suena "error" y se queda escribiendo.
  - Si no: guarda busqueda, categoria y visibles en la consola. La selección se queda en el mismo juego si sigue en la lista nueva, si no va a 0. Suena "volver".
- Con un filtro activo, la lista de la pestaña es visibles. START sobre el pie "Filtro:" reabre el buscador con el texto y la categoría guardados.
- Quitar todo el filtro (texto vacío y "Todas") deja visibles=None y vuelve a la lista normal.
- Sonidos dentro de la búsqueda: al entrar "buscar"; texto más corto "borrar"; texto distinto "tecla"; cambio de categoría "ajuste"; solo cursor "mover".

### 3.5 Recientes y favoritos
- MAX_RECIENTES=12. Se guardan como [sistema, archivo] en consola.json.
- Al lanzar: se quita cualquier entrada igual, se pone la nueva al principio y se recorta a 12. Se guarda al instante.
- Favoritos: sin límite, ordenados por nombre en minúsculas en la pestaña (en el JSON quedan en orden de añadido).
- Ambas pestañas ignoran entradas cuyo sistema no exista o cuyo archivo ya no esté en conjunto.
- Selección en una pestaña virtual: min(sel, len-1).
- SELECT alterna favorito del juego actual (en pestañas reales y virtuales).
  - Suena "ajuste" al marcar y "borrar" al quitar.
- En pestañas virtuales no hay búsqueda ni salto por letra.

### 3.6 AJUSTES
- Lista vertical (sin carátula). Se dibuja desde y0=54 con h = min(30, (320-30-54)//N).
- Fila elegida: rect_redondo(8, y, 464, h-2, (44,50,86), r=3) y barra de acento (8,y,4,h-2) del color de AJUSTES.
- Nombre en (20, y+6), scale 2, proporcional: TEXTO elegido, TENUE no.
- Valor:
  - Si longitud <= 10, scale 2 mono, alineado a la derecha: x = 456 - len*16, y+6.
  - Si es más larga, scale 1 mono en x = 456 - len*8, y+10.
  - Color: COLOR_A si está esperando confirmación; si no, TEXTO elegido o TENUE.
- Volumen: 10 barras en x0 = 456 - 140 = 316, paso 14, ancho 10, alto 6 + j, base en y+21; lleno COLOR_A (j < v), vacío (60,64,92). El número "%2d" va en (x0-40, y+6) scale 2 mono.
- Ajustes, en este orden (los condicionales entre paréntesis):
  1. Volumen.
  2. Balance (si el firmware tiene sonido.balance; en el port, si se implementa).
  3. Pantalla NES (si existe la pestaña NES).
  4. Pantalla GB (si hay GAME BOY, GB COLOR o HOMEBREW).
  5. Colores GB (si hay GAME BOY).
  6. Intro.
  7. Recientes (borrar).
  8. Favoritos (borrar).
  9. No gustaron.
  10. Beta (GBA), solo si el módulo gba existe.
  11. Registro.
- Valores y pasos (A = +1, B = -1; el cursor sube/baja con ARRIBA/ABAJO con wrap):
  - Volumen: entero 0..10 sin wrap (si no cambia, suena "error"). Al cambiar se recarga el archivo de efectos de ese volumen.
  - Balance: -5..5 sin wrap. Se muestra "Centro", "Izq N" o "Der N". Al cambiar espera a que termine el sonido actual y toca "melodia".
  - Pantalla NES: ciclo con wrap entre los 3 modos de 5.2.
  - Pantalla GB: ciclo entre los 3 modos de 5.3.
  - Colores GB: ciclo entre las 12 paletas.
  - Intro: alterna Si/No.
  - Recientes/Favoritos: valor "Borrar (N)". Con A: si la lista está vacía suena "error"; el primer A pone confirmar=clave y muestra "A: borrar todo" en COLOR_A; el segundo A borra todo y guarda (suena "borrar"). B cancela (suena "volver" si había confirmación y "error" si no).
  - No gustaron: alterna "Visibles (N)"/"Ocultos (N)" (N = notas con valor < 0); recalcula las listas.
  - Beta: Si/No. Recalcula consolas, regresa a AJUSTES con el mismo cursor. En el port no hace falta.
  - Registro: valor "Ver". A abre la pantalla de registro (suena "abrir"). B suena "error".
- Cualquier cambio de selección, de pestaña o de ajuste resetea confirmar.
- Pie en AJUSTES:
  - registro: UD Elegir, A Ver registro.
  - recientes/favoritos: UD Elegir, A Borrar, B Cancelar.
  - volumen: UD Elegir, A Subir, B Bajar.
  - balance: UD Elegir, A Derecha, B Izquierda.
  - no_gustaron: UD Elegir, A Ocultar|Mostrar (Ocultar si ya está visible).
  - resto: UD Elegir, A Siguiente, B Anterior.

### 3.7 Pantalla de Registro (ver_registro)
- Lee las últimas 300 líneas de consola.log, quita líneas vacías, quita el prefijo "/sd/roms/".
- Ajusta a 57 columnas, scale 1. Las continuaciones llevan 2 espacios de sangría.
- Una línea de CUELGUE usa una continuación de 8 espacios en el log; las líneas que empiezan con 8 espacios y no con un dígito son continuación y heredan el color de la anterior.
- Colores:
  - Contiene "CUELGUE": COLOR_A (y cuenta como cuelgue).
  - Contiene "arranque": COLOR_B si contiene "watchdog", si no TENUE.
  - Contiene "error:": COLOR_B.
  - Contiene "salio:": TENUE.
  - Resto: TEXTO.
- Diseño:
  - Cabecera (0,0,480,44) PANEL.
  - "REGISTRO" en (12,6), ROSA, scale 2 mono.
  - Contador "N cuelgue(s)" a la derecha en (468-len*8, 6): COLOR_A si n>0, si no TENUE.
  - Si el filtro "solo cuelgues" está activo: "solo cuelgues" en (364,18), COLOR_B.
  - Línea de info en (12,30), TENUE, recortada a 58 caracteres. Texto: "K210 2x400 MHz - 8 MB RAM - SD libre N MB - NES n GAME BOY n ...". En el port, usar los datos reales de la placa.
  - Línea ROSA (0,44,480,2).
  - Texto desde y=50, alto de línea 12; filas visibles nf = (320-30-50)//12 = 20.
  - Barra de desplazamiento en (474, 50, 3, 240) PANEL con pulgar ROSA (th = max(10, 240*nf//len)).
  - Pie: UD Mover, A Todo|Solo cuelgues, B Volver, sin volumen.
- Empieza mostrando lo más nuevo (última página).
- Controles: ABAJO +1 línea, ARRIBA -1; DER una página abajo (nf), IZQ una página arriba. Repetición a 300 ms y luego cada 60 ms. A alterna el filtro de cuelgues (si no hay, muestra "(no hay cuelgues registrados)"). B vuelve (suena "volver"). A suena "ajuste".
- Si no hay log: una línea "(todavia no hay registro)" en TENUE.

### 3.8 Marquesina
- Aplica solo a la fila seleccionada cuyo nombre completo (sin extensión, "_" a espacio) mide más de 17 caracteres.
- Esperar 1000 ms quieto. Luego avanza 1 carácter cada 160 ms sobre (texto + 5 espacios) seguido de texto repetido, mostrando MAX_CHARS caracteres.
- Cuando da la vuelta, vuelve a esperar 1 s.
- Redibuja solo esa fila (270x28) con el color de acento, la marca izquierda y el texto con scale 2 en (8,3). En pantalla va en (8, y-2).
- Cualquier dibujar() completo la reinicia (marq=None, y se vuelve a armar si hace falta).

## 4. Controles

### 4.1 Botones
Máscara de 8 botones: B, A, SELECT, START, ARRIBA, ABAJO, IZQ, DER. Los pines del K210 son irrelevantes en el port.

### 4.2 Teclado del PC (TECLAS, líneas 247-249)
| Tecla | Acción |
|---|---|
| w | ARRIBA |
| s | ABAJO |
| a | IZQ |
| d | DER |
| j | A |
| k | B |
| n | SELECT |
| m | START |

- tools/control.py traduce las flechas a w/a/s/d, z o j a "j" (A), x o k a "k" (B), espacio o n a "n" (SELECT), Enter o m a "m" (START). Esc dentro del juego abre la pausa. Ctrl-C (\x03) relanza KeyboardInterrupt.
- Una tecla cuenta como pulsación única: es un evento "nuevo" sin mantener.

### 4.3 Detección de pulsación
- nuevo = (b & ~antes) | teclas(): flanco de subida. Cada ciclo duerme 10 ms.
- Con nuevo == 0 y consolas presentes, se consulta el touch.
- El touch no existe en el port (sin touch). Ignorar las "zonas".

### 4.4 Menú principal
- IZQ/DER: pestaña anterior/siguiente con wrap, confirmar=None. Suena "pestana". Solo con flanco, sin repetición al mantener.
- ARRIBA/ABAJO: mueve la selección con wrap; suena "mover". Repetición: con ARRIBA/ABAJO mantenidos, el primer evento nuevo arma repetir_en = ahora+350; luego, mientras sigan apretados y pasado ese instante, genera un evento cada 70 ms.
  - Peculiar: la condición de re-armado es "b & ~antes" (cualquier botón nuevo, no solo ARRIBA/ABAJO).
- B + ARRIBA/ABAJO: salto de letra. Exige tener B mantenido (b & B) y un flanco de ARRIBA/ABAJO. Solo en pestañas reales (no virtuales). Suena "letra".
  - saltar_letra(juegos, sel, paso): inicial(nombre) = primera letra en mayúscula si está en A-Z, si no "#".
    - Hacia delante: busca desde sel+1, con wrap, el primer juego con inicial distinta de la actual.
    - Hacia atrás: busca hacia atrás, con wrap, la primera de inicial distinta; luego retrocede hasta el primero de esa letra.
  - Con la repetición, B+ARRIBA/ABAJO sigue saltando de letra.
- A: lanza el juego (llama lanzar).
- START: busca (solo pestañas reales; en virtuales no hace nada).
  - START se atiende antes del chequeo de lista vacía.
- SELECT: favorito (suena "ajuste"/"borrar").
- Lista vacía: ARRIBA/ABAJO/A/SELECT no hacen nada (devuelve "nada" y no redibuja).
- Cualquier "nuevo" que no sea nada de lo anterior no redibuja.
- Se redibuja la pantalla completa (dibujar()) tras cada acción que cambia algo.
- Cuando no hay nuevo evento, el bucle:
  - Lee la carátula pendiente si pasaron 350 ms sin botones y sin sonido sonando.
  - Avanza la marquesina.
  - Duerme 10 ms.

### 4.5 Búsqueda
Ver 3.4.

### 4.6 Registro
Ver 3.7.

### 4.7 Pausa
Ver 5.

### 4.8 "¿Te gustó?"
- A = le gustó (nota 1, suena "ajuste"), B = no le gustó (nota -1, suena "borrar"), START = no sé (nota 0, suena "volver"). Sin SELECT ni flechas. Ver 5.7.

## 5. Menú de pausa

### 5.1 Activación
- Gesto del hardware: SELECT+START sostenidos 15 cuadros (~0,25 s), sin A ni B. Mientras se arma, el juego no ve SELECT ni START.
- En el port a Linux: implementar el mismo gesto y detectarlo desde el bucle del frontend; hay que pausar el emulador.
- Al abrir: reinicia I2S, suena "pausa", espera a soltar todo.

### 5.2 Tamaños de pantalla NES
PANTALLAS_NES = (("Original",0,0,0), ("Llena",366,320,8), ("TV 4:3",428,320,8)). Uso: nes.pantalla(w, h, recorte_overscan). Original = 256x240 sin escalar, sin recorte (0,0,0). Por defecto en config: índice 2 (TV 4:3).

### 5.3 Tamaños de pantalla GB/GBC
PANTALLAS_GB = (("Original",320,288), ("Llena",356,320), ("Ancho",480,320)). Se pasa escala=2, ancho=w, alto=h.
- Original 320x288 = x2 exacto del 160x144.
- Llena es 320 de alto sin deformar (x2,22 aprox.).
- Ancho 480x320 estira.
- Por defecto: índice 1 (Llena).
- Escalado: vecino más cercano por tabla (escalar.c).
- Para GBA (descartado en el port): gba_pantalla 0 = Original 240x160 y 1 = Completa x2.

### 5.4 Paletas de GB clásico (12, en este orden; índice = gb_paleta)
Cada paleta tiene tres capas (OBJ0, OBJ1, fondo) con 4 tonos cada una (tono 0 es el más claro), en RGB888. "IGUAL" = las tres capas iguales. Las listas están en py_gb.c líneas 87-105.
1. Verde original: IGUAL(9BBC0F, 8BAC0F, 306230, 0F380F).
2. Grises: IGUAL(FFFFFF, A5A5A5, 525252, 000000).
3. GBC Cafe: IGUAL(FFFFFF, FFAD63, 843100, 000000).
4. GBC Rojo: OBJ0 (FFFFFF, 7BFF31, 008400, 000000); OBJ1 y fondo (FFFFFF, FF8484, 943A3A, 000000).
5. GBC Azul: OBJ0 y OBJ1 (FFFFFF, FF8484, 943A3A, 000000); fondo (FFFFFF, 63A5FF, 0000FF, 000000).
6. GBC Azul oscuro: OBJ0 (FFFFFF, FF8484, 943A3A, 000000); OBJ1 (FFFFFF, FFAD63, 843100, 000000); fondo (FFFFFF, 8C8CDE, 52528C, 000000).
7. GBC Verde: IGUAL(FFFFFF, 52FF00, FF4200, 000000).
8. GBC Verde oscuro: OBJ0 y OBJ1 (FFFFFF, FF8484, 943A3A, 000000); fondo (FFFFFF, 7BFF31, 0063C5, 000000).
9. GBC Pastel: IGUAL(FFFFA5, FF9494, 9494FF, 000000).
10. GBC Naranja: IGUAL(FFFFFF, FFFF00, FF0000, 000000).
11. GBC Amarillo: IGUAL(FFFFFF, FFFF00, 7B4A00, 000000).
12. GBC Invertido: IGUAL(000000, 008484, FFDE00, FFFFFF).
- Por defecto: gb_paleta=5 (GBC Azul oscuro).
- Solo aplica al emulador "gb" clásico (Peanut-GB) con ROM clásica. El emulador de color (gbc) no usa paleta (los .gbc tienen la suya). En el port, Gambatte puede traer sus propias paletas; es decisión de implementación.

### 5.5 VOL_PCT y volumen
VOL_PCT = (0, 3, 6, 10, 16, 24, 35, 50, 65, 80, 100), índice 0..10. Es la escala de volumen de los emuladores (porcentaje). Volumen por defecto 2.

### 5.6 Panel de pausa (dibujar_pausa)
- Panel de 300x304 en (90, 8) sobre la pantalla; se dibuja en una imagen aparte y se envía encima del juego (lo de alrededor sigue siendo el último cuadro del juego).
- Borde de acento de 3 px y fondo FONDO: rect completo de acento y rect interior (3,3,W-6,H-6) de FONDO. Acento = color de la consola del juego.
- Título "PAUSA" con la fuente 5x7 (celda 3, incl 1, BLANCO), letras en (16 + i*18, 12).
- Nombre del juego (sin extensión) en (16,40), TENUE, scale 1, primeros 34 caracteres.
- Opciones desde y=58, paso = min(28, (304-24-58)//N), donde N = número de opciones (con 8 o más opciones el paso baja de 28; con 8 queda en 27). Fila: rect redondo (8, y, 284, paso-2) r=3.
  - Elegida: fondo de acento, texto FONDO. No elegida: TEXTO sobre FONDO.
  - Texto en (18, y+5), scale 2.
  - Valor a la derecha: scale 1 mono, alineado a W-18 - len*8, en y+9.
- Volumen: 10 barras, x = W-18-(10-j)*9, ancho 6, alto 4+j, base y+20.
  - Llenas (j < v): FONDO si la fila está elegida, si no COLOR_A.
  - Vacías: mezcla(acento, FONDO, 0.6) si está elegida, si no (60,64,92).
- Pie del panel: "A elegir  -  IZQ/DER  -  B seguir" en (16, H-20), TENUE.

Opciones, en este orden:
1. Continuar (seguir).
2. Guardar y Cargar (solo gb y gbc; NES no). Valor de Guardar = aviso_estado ("", "Guardado", "Error"). Valor de Cargar = "Hay" si el archivo .est existe y pesa algo, o "Vacio".
3. Volumen (barras).
4. Balance (si el firmware lo tiene).
5. Pantalla: NES (modos NES), GBA (original/completa) o GB (modos GB).
6. Colores: solo si el emulador es "gb" clásico.
7. Favorito: valor "Si"/"No".
8. Reiniciar.
9. Salir al menu.

Controles de la pausa:
- ARRIBA/ABAJO mueven con wrap, suena "mover". Sin auto-repetición en la pausa.
- IZQ/DER cambian el valor (±1) en: volumen, balance, pantallas y paleta. Con valores cíclicos hay wrap; volumen y balance se saturan (suena "error" si no cambian).
- A: según la opción:
  - Continuar, Reiniciar, Salir: ejecuta.
  - Favorito: alterna (suena "ajuste"/"borrar").
  - Guardar: llama a guardar_estado(ruta).
  - Cargar: llama a cargar_estado(ruta). Si va bien, sale a "seguir" directamente; si falla, suena "error".
  - Resto (volumen, pantalla...): A = +1.
- B o START: seguir.
- Para el NES las opciones Guardar/Cargar no aparecen.
- Sonidos al salir de la pausa: seguir "seguir", reiniciar "abrir", salir "volver". Luego espera a soltar y a que termine el efecto.
- Estado guardado: un archivo por juego, junto a la ROM: <dir>/<archivo>.est (por ejemplo Juego.gb.est).
- Los cambios de volumen y balance se guardan inmediatamente en consola.json y el emulador recibe el nuevo volumen al reanudar (emu_correr lo lee de CFG).

### 5.7 "¿Te gustó?"
- Se pregunta solo al salir de la pausa con "Salir al menu", y solo si la nota es None (primera vez que se sale de ese juego). No se pregunta al salir por error.
- Panel de 300x304 en (90,8) con el mismo borde de acento.
  - "Te gusto?" scale 3 en (18,18), TEXTO.
  - Nombre (sin extensión) en dos líneas: [:34] en (18,58) y [34:68] en (18,72), TENUE scale 1.
  - Tres botones, cada uno en y = 100 + i*40, rect redondo (12, y, 276, 32) r=4 en PANEL.
    - Botón dibujado con boton_pie en (22, y+8).
    - Texto scale 2 en (22+w+12, y+8).
    - Opciones: A "Si, me gusto", B "No, ocultarlo", START "No se".
  - Pie "Los ocultos se ven en AJUSTES" en (18, H-22), TENUE.
- Respuesta: A = nota 1, B = nota -1, START = nota 0. Se guarda en notas["<Sistema>/<archivo>"] y se registra en el log. Si es -1, se recalculan las listas (el juego desaparece hasta activar "No gustaron: Visibles").

## 6. Configuración persistente

### 6.1 /sd/consola.json
Se carga al importar (c.update(json.load(f)) sobre los valores por defecto). Si falla la lectura, usa los defaults. Se reescribe entero con json.dump.

| Clave | Por defecto | Significado |
|---|---|---|
| volumen | 2 | 0..10, índice de VOL_PCT |
| balance | 0 | -5..5, firmware propio de sonido (L/R) |
| notas | {} | "<Sistema>/<archivo>" a 1, 0 o -1 |
| ver_no_gustaron | false | muestra los de nota -1 |
| beta | false | activa pestaña GBA |
| gba_pantalla | 1 | GBA |
| nes_pantalla | 2 | índice en PANTALLAS_NES |
| gb_pantalla | 1 | índice en PANTALLAS_GB (GB y GBC) |
| gb_paleta | 5 | índice de las 12 paletas |
| intro | true | reproducir la intro |
| recientes | [] | lista de [sistema, archivo], máx. 12, el más reciente primero |
| favoritos | [] | lista de [sistema, archivo] |

Cuándo se guarda:
- En cada cambio de ajuste.
- Al lanzar un juego (reciente).
- Al marcar o quitar un favorito.
- Al borrar recientes o favoritos.
- Al responder "¿Te gustó?".
- No se guarda al cambiar de pestaña, ni la selección, ni la búsqueda (son estado en memoria).

Notas:
- gb_pantalla, nes_pantalla y gb_paleta se usan con módulo % len(...), así que valores fuera de rango son seguros.
- El sistema se identifica por su nombre de pestaña ("NES", "GAME BOY", "GB COLOR", "HOMEBREW", "JUEGOS", "GBA").

### 6.2 _categorias.json (en cada carpeta de ROMs)
- Diccionario {"archivo.ext": "Categoría"} con las categorías de la lista CATEGORIAS. Lo genera tools/catalogar.py. Ejemplo: {"4-in-1 Funpak.gb":"Mesa y cartas", ...}.
- Existe en roms/gb, roms/gbc, roms/nes y roms/homebrew (no en /sd/juegos).

### 6.3 _ocultos.json
- Lista JSON de nombres de archivo (con extensión) que no se muestran (siguen en la SD). Existe en roms/gb, roms/gbc, roms/nes. Lo genera tools/ocultar_poco_jugados.py.
- Ejemplo: ["Adventure Island III.nes","Arcadia VI.nes", ...].
- Hay un _ocultos.txt con "archivo<TAB>motivo" (documentación humana). El menú no lo lee.
- Los homebrew no tienen _ocultos.json (no se tocan).
- En sd/ no hay consola.json de ejemplo; se crea solo al guardar.

## 7. Sonidos de interfaz y log

### 7.1 Formato
- Directorio /sd/sistema/sonido/ (existe en sd/sistema/sonido/):
  - ui.json: índice {nombre: [byte_inicio, largo_bytes]}, el mismo para todos los volúmenes.
  - ui_vN.raw (N = 1..10): todos los efectos concatenados a volumen N.
  - intro_vN.raw: jingle de la intro.
- Formato raw: PCM 16000 Hz, 32 bits por cuadro (16 bits bajos izquierda, 16 altos derecha, misma muestra en ambos). Un archivo por volumen para no escalar en Python.
- Con volumen 0, o sin módulo de sonido, no suena nada y no se cargan efectos.
- Los archivos existentes pueden usarse tal cual en el port, o regenerarse con tools/hacer_sonidos_ui.py.
- Los efectos solo se cargan cuando cambian volumen o balance (EFX.cargar).
- Existen también sd/sistema/intro.wav y estereo.wav (la intro usa el wav solo con firmware viejo sin módulo sonido).

### 7.2 Mapa evento a sonido
ui.json contiene: mover, letra, pestana, abrir, volver, buscar, tecla, borrar, ajuste, pausa, seguir, error, melodia.

| Evento | Sonido |
|---|---|
| ARRIBA/ABAJO en lista, ajustes, pausa; cursor en buscador | mover |
| Salto de letra (B+arriba/abajo) | letra |
| Cambio de pestaña (IZQ/DER o toque) | pestana |
| Lanzar juego; reiniciar desde pausa; abrir Registro | abrir |
| Volver del juego (sin error); salir de buscador; salir de Registro; salir de pausa con "Salir"; cancelar confirmación; START en "¿Te gustó?" | volver |
| Abrir buscador | buscar |
| Escribir letra en buscador | tecla |
| Borrar en buscador; quitar favorito; borrar listas; nota -1 | borrar |
| Cambiar ajuste; marcar favorito; cambiar categoría; A en Registro; guardar estado ok; nota 1 | ajuste |
| Abrir pausa | pausa |
| Continuar desde pausa | seguir |
| Error: volver de juego con excepción; búsqueda sin resultados; volumen/balance en el tope; A en lista vacía de borrado; cargar estado fallido; B en Registro de AJUSTES | error |
| Cambiar Balance | melodia (si existe y no está vacía; si no, "ajuste") |

### 7.3 Reglas
- Nunca cortar un sonido en curso: si suena otro, el nuevo se salta. (Con DMA I2S se oye chicharreo si se corta; el port con ALSA puede no necesitarlo, pero la semántica "se salta" es observable.)
- Un solo efecto a la vez.
- Los efectos terminan con apagado + 30 ms de ceros.
- El jingle de la intro tampoco se corta.
- Mientras algún sonido suena, no se lee la carátula (evita ruido eléctrico en el K210; en Linux no aplica).

## 8. Elección de emulador (emulador_de, 744-759)
Por nombre de pestaña y extensión, en este orden:
1. Pestaña GBA o extensión .gba: "gba".
2. Pestaña NES, o pestaña HOMEBREW con .nes: "nes".
3. Pestaña JUEGOS: "py" (ejecuta el script; sin pausa).
4. Pestaña GB COLOR: "gbc" (cualquier extensión).
5. En cualquier otra pestaña (GAME BOY, HOMEBREW con .gb/.gbc), si existe el módulo gbc y (extensión .gbc o tamaño > 2 MiB): "gbc".
6. Si no: "gb".

- ROM_GB_MAX = 2*1024*1024. Razón: gbc lee la ROM por bancos desde la SD (hasta 8 MB, caché de 118 bancos); el gb clásico solo carga la ROM entera en RAM.
- Las extensiones .gb y .gbc están permitidas en las dos pestañas GB (GAME BOY lista .gb y .gbc; GB COLOR lista .gbc y .gb), así que cualquier archivo puede aparecer en ambas.
- Se decide siempre con emulador_de; esto afecta la info bajo la carátula (qué "Pantalla:" y si muestra paleta), las opciones de la pausa (Colores solo en "gb"; Guardar/Cargar solo en gb/gbc) y la pantalla usada.
- Al lanzar NES: nes.init(nes.BOTONES, vol=volumen), luego nes.load. Al correr: i2s.set_sample_rate(44100), nes.pantalla(w,h,r), nes.volumen(v).
- Al lanzar GB/GBC: gb.run(escala=2, ancho=w, alto=h, paleta=..., volumen=v) (gb añade paleta, gbc no).
- Los juegos GB guardan partida automáticamente (SRAM .sav junto a la ROM); los estados se guardan a mano (.est).
- El NES no tiene save states todavía.

## 9. Detalles finos y peculiares
1. La fuente es proporcional por defecto. Casi todos los textos de lista, pie y carátula usan avance proporcional (mono_space=False). Los len*8 del código son estimaciones para centrar. Si el port usa una fuente de ancho fijo, los centrados y recortes saldrán distintos. Lo ideal es reimplementar el algoritmo de avance (ver 2.3).
2. Sin acentos ni ñ: todos los textos se escriben sin tildes ("Pantalla", "Categoria", "Todavia", "ultimos", "Te gusto?", "menu", "No se"). La fuente solo cubre ASCII; los nombres de archivo con otros caracteres se dibujan como rectángulos huecos. Considerar si el port debe filtrar.
3. Truncado: nombres de más de 17 caracteres se cortan a 16 + "~". La marquesina muestra el nombre completo. La búsqueda es sobre el nombre completo sin extensión, en minúsculas; el "_" no se convierte a espacio en la búsqueda (solo titulo y marquesina lo cambian al mostrar).
4. Orden: ordenación sin distinguir mayúsculas; el salto de letra agrupa todo lo que no es A-Z bajo "#".
5. Selección por pestaña: cada pestaña recuerda su propio índice seleccionado. En RECIENTES se resetea a 0 al volver de un juego.
6. Pestaña inicial: RECIENTES si hay recientes válidos (el último jugado), si no la primera consola.
7. Carátula diferida: el redibujo normal no lee la carátula; guarda portada_pend y, tras 350 ms sin botones apretados (ni sonido sonando), repite dibujar() con leer_sd=True. Ese redibujo es el que lee el JPG y el tamaño del archivo (KB). Mientras tanto se ve el ícono de la consola. Si la ruta ya es la de la última carátula cargada, se usa la caché sin esperar. En Linux se puede leer al instante, pero para ser idéntico hay que conservar el parpadeo ícono a carátula (es comportamiento visible).
8. Cambio de pestaña con IZQ/DER limpia confirmar, y también al mover el cursor en AJUSTES.
9. Orden de eventos: IZQ/DER se atiende antes que lista/ajustes. B+ARRIBA/ABAJO exige B mantenido.
10. Repetición de ARRIBA/ABAJO en menú: 350 ms primer retraso, 70 ms entre repeticiones. En búsqueda: 300/90 ms (4 flechas). En Registro: 300/60 ms (4 flechas). En pausa: ninguna. En ajustes la repetición del menú también aplica (ARRIBA/ABAJO), pero A/B no repiten.
11. Nota -1 oculta el juego (salvo "No gustaron: Visibles", que lo marca con barra TENUE). Los recientes y favoritos usan conjunto (todos, incluidos los de nota -1), así que un juego malo puede seguir en RECIENTES.
12. La búsqueda persiste por pestaña (busqueda, categoria, visibles se guardan en la consola) hasta que se limpie explícitamente o hasta reiniciar el menú. Al cambiar notas se recalcula visibles.
13. bajos: caché de nombres en minúsculas por consola, que se invalida con refrescar_notas.
14. El indicador de volumen del pie aparece en el menú y AJUSTES, no en búsqueda ni Registro.
15. Pausa: tocar fuera del panel equivale a B (solo touch; ignorable). El panel solo se redibuja al cambiar algo.
16. Error de lanzado: recuadro (30,110,420,100) en (170,40,40) con "No se pudo jugar:" scale 2 en (44,120) y hasta 3 líneas de 60 caracteres en (44, 152+n*16), scale 1, TEXTO. Se muestra 3 s y se redibuja sin él. Suena "error".
17. Sin consolas: pantalla con logo chico (en (12,11), celda 3), "No hay juegos en la SD" scale 2 en (30,120) y "/sd/roms/nes  /sd/roms/gb  /sd/juegos" scale 1 TENUE en (30,150). El bucle sigue activo pero sin pestañas ni acciones.
18. Recientes tras lanzar: se registran antes de cargar la ROM (incluso si falla).
19. El estado .est vive junto a la ROM, no en una carpeta de saves.
20. Orden de pestañas con GBA: rec, fav, NES, GB, GBC, JUEGOS, GBA, HOMEBREW, AJUSTES (JUEGOS siempre se agrega, aunque luego se oculta si no tiene .py). En el port, la pestaña JUEGOS (scripts .py) no tiene equivalente y puede omitirse o sustituirse.
21. No hay rótulos de texto en las pestañas, solo íconos. Al cambiar pestaña solo cambia el color de acento de la línea de cabecera, el marco de carátula y la barra de selección.
22. dibujar() redibuja todo el frame: no hay dirty rects salvo la marquesina (solo esa fila) y la cabecera cacheada. En C con 480x320 el redibujo completo es barato, pero la fila de marquesina evita parpadeo.
23. Volumen cero: no carga efectos ni jingle, y sigue mostrando "0" y barras vacías.
24. El menú comparte un búfer 480x320 con el GBA (préstamo de memoria): irrelevante en el port.
25. El panel de pausa y "¿Te gustó?" se dibujan en el mismo búfer reutilizado; tras salir, el menú se redibuja completo.
26. Salir al menú libera el emulador y redibuja la pestaña actual con la selección del juego recién jugado. La carátula se reinicia (portada=None).
27. START al arrancar salta el menú (lleva al REPL). En el port: opción de salir a shell.
28. El log escribe en cada evento; si falla, imprime por serie y sigue.

## Resumen (10 líneas)
1. No pude crear el archivo ESPEC_MENU.md (este agente es de solo lectura); el contenido completo está arriba y hay que guardarlo con ese nombre.
2. Menú de 480x320 con cabecera de 44 px (logo GAME GIRL chico más pestañas con íconos de 2x2), lista de 7 filas de 30 px, carátula de 174x154 y pie de 24 px con botones dibujados.
3. Intro de 3,1 s, escrita en detalle con todos los T_*: línea de encendido TV, estrellas, caída con rebote de GAME, entrada de GIRL, barra de colores, salto de COLOR y destellos.
4. Pestañas: RECIENTES, FAVORITOS, consolas con juegos (NES, GB, GBC, JUEGOS, GBA beta, HOMEBREW) y AJUSTES. Filtros _ocultos.json, _categorias.json y notas de "¿Te gustó?".
5. Fuente = bitmap interno de MaixPy 8x12 (Envy Code B) en font.c; mono_space por defecto es False, así que el texto es proporcional (algoritmo de avance en 2.3).
6. Íconos: datos exactos en menu.py líneas 485-601; fuente 5x7 del logo en líneas 291-304.
7. Controles: menú con repetición 350/70 ms, B+arriba/abajo para saltar de letra, START busca, SELECT favorito. Buscador con teclado de 8 columnas y repetición 300/90 ms. Pausa con SELECT+START mantenidos ~0,25 s.
8. Pausa: Continuar, Guardar/Cargar (GB/GBC), Volumen, Balance, Pantalla, Colores (solo GB clásico), Favorito, Reiniciar y Salir. Incluye 3 modos de pantalla NES, 3 de GB y 12 paletas de GB con sus colores.
9. Sonidos: 13 efectos (ui.json) y jingle en .raw de 16 kHz por volumen; hay mapa evento a sonido, y el log en /sd/logs/consola.log rota a 256 KB.
10. Elección de emulador: .nes va a nes; GB COLOR va a gbc; en GAME BOY/HOMEBREW los .gbc o los .gb mayores de 2 MiB van a gbc, y el resto a gb. consola.json guarda 12 claves. Detalles finos: 28 puntos en la sección 9.

Archivo previsto (NO escrito): ../luckfox_consola/consola/ESPEC_MENU.md. Archivos fuente relevantes: ../maix_dock/menu.py, ../maix_dock/fw/MaixPy/components/micropython/port/src/omv/img/font.c (fuente 8x12), ../maix_dock/fw/MaixPy/components/micropython/port/src/gb/py_gb.c (paletas), ../maix_dock/sd/sistema/sonido/ui.json.
