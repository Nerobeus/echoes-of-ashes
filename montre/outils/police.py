#!/usr/bin/env python3
"""
Polices bitmap de l'app montre (main/police.h et police.c), rendues avec Pillow depuis les polices du jeu (Oxanium, OFL 1.1,
serveur/polices de l'Atelier) : 4 bits de couverture par pixel (anticrénelage), ASCII et Latin-1 (accents français),
plus œ Œ ’ … « ».

  python police.py [dossier des TTF] [sortie]
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

TTF = sys.argv[1] if len(sys.argv) > 1 else r'D:\Documents\Stellar Exodus\tileset-generator-v4\serveur\polices'
SORTIE = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'main', 'police')   # police.h et police.c

# (nom C, fichier, taille en px, jeu de caractères)
TEXTE = [chr(c) for c in range(32, 127)] + [chr(c) for c in range(0xA0, 0x100)] + ['œ', 'Œ', '’', '…', '–']
CHIFFRES = list('0123456789:')
POLICES = [
    ('P_TEXTE', 'Oxanium-400.ttf', 18, TEXTE),
    ('P_TITRE', 'Oxanium-600.ttf', 26, TEXTE),
    ('P_HEURE', 'Oxanium-600.ttf', 72, CHIFFRES),
    # thème des menus d'Echoes (main/theme.c) : FONT_DISP 22 et 30, FONT_MONO 18
    ('P_RANG', 'Oxanium-600.ttf', 22, TEXTE),
    ('P_GRAND', 'Oxanium-600.ttf', 30, TEXTE),
    ('P_MONO', 'ShareTechMono-400.ttf', 18, TEXTE),
]


def rendre(fichier, taille, jeu):
    f = ImageFont.truetype(os.path.join(TTF, fichier), taille)
    asc, desc = f.getmetrics()
    glyphes = []
    for ch in sorted(set(jeu), key=ord):
        x0, y0, x1, y1 = f.getbbox(ch)
        av = round(f.getlength(ch))
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        px = []
        if w and h:
            im = Image.new('L', (w, h), 0)
            ImageDraw.Draw(im).text((-x0, -y0), ch, font=f, fill=255)
            px = [v >> 4 for v in im.tobytes()]
        glyphes.append((ord(ch), w, h, x0, y0, av, px))
    return asc + desc, asc, glyphes


def main():
    titre = '/* Polices bitmap de l\'app montre (outils/police.py) : Oxanium et Share Tech Mono (SIL OFL 1.1), 4 bits par pixel */'
    tete = [titre, '#ifndef POLICE_H', '#define POLICE_H', '#include <stdint.h>', '',
            'typedef struct { uint16_t code; uint8_t w, h; int8_t dx, dy; uint8_t av; uint32_t off; } glyphe_t;',
            'typedef struct { const glyphe_t *g; int n; const uint8_t *px; uint8_t hauteur, base; } police_t;', '']
    out = [titre, '#include "police.h"', '']
    for nom, fichier, taille, jeu in POLICES:
        haut, base, glyphes = rendre(fichier, taille, jeu)
        donnees, table = bytearray(), []
        for code, w, h, dx, dy, av, px in glyphes:
            off = len(donnees)
            for i in range(0, len(px), 2):   # deux pixels par octet, le premier dans les bits forts
                donnees.append(px[i] << 4 | (px[i + 1] if i + 1 < len(px) else 0))
            table.append(f'{{{code},{w},{h},{dx},{dy},{av},{off}}}')
        n = nom.lower()
        out.append(f'static const glyphe_t {n}_g[] = {{')
        for i in range(0, len(table), 8):
            out.append('    ' + ','.join(table[i:i + 8]) + ',')
        out.append('};')
        out.append(f'static const uint8_t {n}_px[] = {{')
        for i in range(0, len(donnees), 24):
            out.append('    ' + ','.join(str(b) for b in donnees[i:i + 24]) + ',')
        out.append('};')
        out.append(f'const police_t {nom} = {{ {n}_g, {len(table)}, {n}_px, {haut}, {base} }};')
        out.append('')
        tete.append(f'extern const police_t {nom};   /* {fichier}, {taille} px */')
        print(f'{nom} : {fichier} {taille} px, {len(table)} glyphes, {len(donnees)} octets')
    tete += ['', '#endif']
    open(SORTIE + '.h', 'w', encoding='utf-8').write('\n'.join(tete) + '\n')
    open(SORTIE + '.c', 'w', encoding='utf-8').write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
