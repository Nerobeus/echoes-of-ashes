#!/usr/bin/env python3
"""
Fonds des cadrans de l'app montre (main/cadrans.c), dessinés ici sans Stable Diffusion : ciel, ville, lune, radar, globe,
coucher de soleil… plus le décor fixe (kanji en Yu Gothic, étiquettes, graduations). Le firmware dessine par-dessus ce
qui bouge (heure, aiguilles, balayage, jauges), aux places convenues dans cadrans.c.

Sortie dans montre/cadrans/ : cadran_<id>.cad (à copier dans /sd/echoes de la carte : envoi_sd.py --dossier
montre/cadrans cadran_neon.cad …, ou par le stockage USB de la montre) et apercu/<id>.png.

Format .cad : « CAD1 », largeur, hauteur, nombre de couleurs (u16), palette RGB565 (u16 chacun), puis un octet par pixel.

  python cadrans.py [id …]
"""
import math
import os
import random
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ICI = os.path.dirname(os.path.abspath(__file__))
SORTIE = os.path.join(ICI, '..', 'cadrans')
W = 466
S = 2                 # sur-échantillonnage (anticrénelage), réduit à la fin
N = W * S
POL = r'D:\Documents\Stellar Exodus\tileset-generator-v4\serveur\polices'
TECH = r'D:\3d objects\techwear'   # polices de démonstration, usage personnel seulement
JP = r'C:\Windows\Fonts\YuGothB.ttc'


def police(nom, taille):
    chemin = nom if os.path.isabs(nom) else os.path.join(POL, nom)
    return ImageFont.truetype(chemin, int(taille * S))


OX = lambda t: police('Oxanium-600.ttf', t)
OXL = lambda t: police('Oxanium-400.ttf', t)
MONO = lambda t: police('ShareTechMono-400.ttf', t)
KANJI = lambda t: police(JP, t)
ARMOR = lambda t: police(os.path.join(TECH, 'space-armor.regular.otf'), t)


def p(v):
    return int(round(v * S))


def calque():
    return Image.new('RGBA', (N, N), (0, 0, 0, 0))


def coller(fond, c, flou=0, force=1.0):
    """calque c posé sur fond, avec une lueur (copie floutée) si flou"""
    if flou:
        g = c.filter(ImageFilter.GaussianBlur(p(flou)))
        if force != 1.0:
            a = np.array(g)
            a[..., 3] = np.clip(a[..., 3] * force, 0, 255).astype(np.uint8)
            g = Image.fromarray(a)
        fond.alpha_composite(g)
    fond.alpha_composite(c)


def degrade_v(arrets):
    """dégradé vertical : [(y, (r, g, b)), …] en coordonnées d'écran"""
    ys = np.arange(N) / S
    img = np.zeros((N, N, 3), np.float32)
    for k in range(3):
        col = np.interp(ys, [a[0] for a in arrets], [a[1][k] for a in arrets])
        img[:, :, k] = col[:, None]
    return img


def radial(c0, c1, cx=233, cy=233, r=233):
    y, x = np.mgrid[0:N, 0:N] / S
    d = np.clip(np.hypot(x - cx, y - cy) / r, 0, 1)[..., None]
    return np.array(c0, np.float32) * (1 - d) + np.array(c1, np.float32) * d


def vers_image(a):
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')


def bruit(n, echelle, graine, octaves=4):
    """bruit de valeur lissé (n x n), dans 0..1"""
    rng = np.random.default_rng(graine)
    tot = np.zeros((n, n), np.float32)
    amp, somme = 1.0, 0.0
    for o in range(octaves):
        g = max(2, int(n / echelle * 2 ** o))
        b = Image.fromarray((rng.random((g, g)) * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC)
        tot += np.array(b, np.float32) / 255 * amp
        somme += amp
        amp *= 0.5
    return tot / somme


def texte(d, xy, s, f, c, ancre='mm'):
    d.text((p(xy[0]), p(xy[1])), s, font=f, fill=c, anchor=ancre)


def vertical(d, x, y, s, f, c, pas):
    for k, ch in enumerate(s):
        texte(d, (x, y + k * pas), ch, f, c)


def etoiles(img, n, graine, y_max=466, couleur=(220, 230, 255)):
    rng = random.Random(graine)
    d = ImageDraw.Draw(img)
    for _ in range(n):
        x, y = rng.uniform(0, W), rng.uniform(0, y_max)
        a = rng.randint(60, 255)
        r = rng.choice([0.5, 0.5, 0.8, 1.2])
        d.ellipse([p(x - r), p(y - r), p(x + r), p(y + r)], fill=couleur + (a,))


# ---------------------------------------------------------------------------------------------------------------- thèmes

def neon():
    """Ville néon sous la pluie (heure en haut, date sous elle, voiture en bas)"""
    img = vers_image(degrade_v([(0, (6, 3, 20)), (170, (28, 8, 52)), (300, (92, 18, 92)), (330, (20, 6, 30)), (466, (8, 3, 14))]))
    rng = random.Random(7)
    # ville lointaine, puis proche
    for couche, (base, hmin, hmax, coul, pfen) in enumerate([(310, 50, 150, (34, 14, 58), 0.16), (330, 70, 230, (13, 6, 24), 0.22)]):
        c = calque()
        d = ImageDraw.Draw(c)
        x = -10
        while x < W + 10:
            w = rng.uniform(18, 46)
            h = rng.uniform(hmin, hmax) * (1.25 if couche and (x < 120 or x > 330) else 0.8 if couche else 1)
            d.rectangle([p(x), p(base - h), p(x + w), p(base)], fill=coul + (255,))
            for fy in np.arange(base - h + 6, base - 6, 7):
                for fx in np.arange(x + 4, x + w - 4, 6):
                    if rng.random() < pfen:
                        fc = rng.choice([(255, 190, 90), (90, 220, 255), (255, 90, 200), (200, 200, 255)])
                        d.rectangle([p(fx), p(fy), p(fx + 2.2), p(fy + 3)], fill=fc + (rng.randint(120, 255),))
            x += w + rng.uniform(0, 6)
        coller(img, c, 3 if couche else 0, 0.5)
    # enseignes verticales
    for x, y0, s, col in [(62, 200, '東京', (255, 50, 170)), (404, 190, '電脳街', (40, 230, 255)), (118, 250, '夜', (255, 170, 40))]:
        c = calque()
        d = ImageDraw.Draw(c)
        hh = len(s) * 30 + 10
        d.rectangle([p(x - 16), p(y0 - 20), p(x + 16), p(y0 - 20 + hh)], outline=col + (255,), width=p(1.5))
        vertical(d, x, y0, s, KANJI(24), (255, 240, 250, 255), 30)
        coller(img, c, 5, 1.6)
    # rue mouillée : reflets, lignes de fuite
    c = calque()
    d = ImageDraw.Draw(c)
    for x, col in [(62, (255, 50, 170)), (404, (40, 230, 255)), (118, (255, 170, 40))]:
        for k in range(14):
            d.rectangle([p(x - 10 + rng.uniform(-3, 3)), p(336 + k * 8), p(x + 10), p(338 + k * 8)], fill=col + (90 - k * 6,))
    for k in range(-5, 6):
        d.line([p(233), p(332), p(233 + k * 110), p(470)], fill=(120, 60, 160, 70), width=p(1))
    coller(img, c, 2)
    # voiture de dos : carrosserie au liseré néon, lunette, feux en bandeau, néon dessous
    c = calque()
    d = ImageDraw.Draw(c)
    d.ellipse([p(120), p(398), p(346), p(430)], fill=(255, 40, 180, 150))
    coller(img, c, 10, 1.4)
    caisse = [(146, 412), (148, 382), (170, 370), (196, 348), (270, 348), (296, 370), (318, 382), (320, 412)]
    c = calque()
    d = ImageDraw.Draw(c)
    d.polygon([(p(x), p(y)) for x, y in caisse], fill=(26, 14, 44, 255))
    d.polygon([(p(x), p(y)) for x, y in [(202, 353), (264, 353), (284, 369), (182, 369)]], fill=(60, 40, 110, 255))
    d.line([(p(x), p(y)) for x, y in caisse], fill=(255, 80, 210, 255), width=p(1.6))
    d.rectangle([p(208), p(394), p(258), p(404)], fill=(220, 220, 240, 255))
    d.rectangle([p(146), p(408), p(320), p(414)], fill=(12, 8, 18, 255))
    d.rectangle([p(150), p(408), p(178), p(420)], fill=(10, 6, 14, 255))
    d.rectangle([p(288), p(408), p(316), p(420)], fill=(10, 6, 14, 255))
    coller(img, c, 3, 0.8)
    c = calque()
    d = ImageDraw.Draw(c)
    d.rectangle([p(154), p(380), p(312), p(386)], fill=(255, 50, 70, 255))
    coller(img, c, 6, 1.6)
    # pluie
    c = calque()
    d = ImageDraw.Draw(c)
    for _ in range(420):
        x, y, l = rng.uniform(-30, W), rng.uniform(0, W), rng.uniform(8, 22)
        d.line([p(x), p(y), p(x + l * 0.25), p(y + l)], fill=(170, 190, 255, rng.randint(25, 70)), width=1)
    coller(img, c)
    return img


def hud():
    """Chronographe analogique : graduations, deux compteurs BAT et TEMP, guichet de date en bas"""
    img = vers_image(radial((40, 44, 56), (6, 7, 10)))
    c = calque()
    d = ImageDraw.Draw(c)
    # brossé : anneaux fins
    for r in range(8, 230, 3):
        a = 10 + (r * 37) % 14
        d.ellipse([p(233 - r), p(233 - r), p(233 + r), p(233 + r)], outline=(255, 255, 255, a // 3), width=1)
    d.ellipse([p(3), p(3), p(463), p(463)], outline=(70, 76, 92, 255), width=p(10))
    for k in range(60):
        a = k * math.tau / 60
        r0 = 206 if k % 5 else 192
        w = 1.4 if k % 5 else 3.2
        col = (230, 236, 245, 255) if k % 5 == 0 else (130, 138, 155, 255)
        if k % 15 == 0:
            col = (40, 230, 255, 255)
        d.line([p(233 + math.sin(a) * r0), p(233 - math.cos(a) * r0), p(233 + math.sin(a) * 220), p(233 - math.cos(a) * 220)], fill=col, width=p(w))
    for k, s in [(0, '12'), (3, '3'), (6, '6'), (9, '9')]:
        a = k * math.tau / 12
        texte(d, (233 + math.sin(a) * 170, 233 - math.cos(a) * 170), s, OX(26), (230, 236, 245, 255))
    texte(d, (233, 118), 'ECHOES', OX(20), (230, 236, 245, 255))
    texte(d, (233, 140), 'CHRONO · HUD', MONO(11), (40, 230, 255, 255))
    # compteurs : arc de 270° de 0.625 à 1.375 tour
    for cx, nom, lab, rouge in [(150, 'BAT', ('0', '50', '100'), 'bas'), (316, 'TEMP', ('20', '50', '80'), 'haut')]:
        cy = 262
        d.ellipse([p(cx - 52), p(cy - 52), p(cx + 52), p(cy + 52)], fill=(14, 16, 22, 255), outline=(90, 96, 112, 255), width=p(2))
        for k in range(21):
            a = (0.625 + 0.75 * k / 20) * math.tau
            r0 = 40 if k % 5 else 35
            alerte = (k <= 3) if rouge == 'bas' else (k >= 17)
            col = (255, 70, 60, 255) if alerte else (200, 206, 220, 255)
            d.line([p(cx + math.sin(a) * r0), p(cy - math.cos(a) * r0), p(cx + math.sin(a) * 47), p(cy - math.cos(a) * 47)], fill=col, width=p(1.2 if k % 5 else 2))
        for k, s in enumerate(lab):
            a = (0.625 + 0.375 * k) * math.tau
            texte(d, (cx + math.sin(a) * 26, cy - math.cos(a) * 26), s, MONO(10), (150, 156, 172, 255))
        texte(d, (cx, cy + 30), nom, MONO(11), (40, 230, 255, 255))
    # guichet de date
    d.rounded_rectangle([p(193), p(338), p(273), p(366)], p(4), fill=(10, 12, 16, 255), outline=(120, 126, 140, 255), width=p(2))
    coller(img, c)
    return img


def lune():
    """Lune orbitale : étoiles, grande lune en bas à droite, orbite inclinée, étiquettes du terrain"""
    img = vers_image(radial((10, 16, 34), (2, 3, 8), 160, 120, 420))
    etoiles(img, 260, 3)
    cx, cy, r = 358, 372, 178   # orbite : centre (cx, cy - 300), rayons 250 et 62 (repris par cadrans.c)
    # orbite : moitié arrière (derrière la lune)
    orb = calque()
    d = ImageDraw.Draw(orb)
    d.ellipse([p(cx - 250), p(cy - 300 - 62), p(cx + 250), p(cy - 300 + 62)], outline=(60, 200, 255, 150), width=p(1.2))
    coller(img, orb, 2)
    # lune : sphère éclairée d'en haut à gauche, relief de bruit et cratères
    y, x = np.mgrid[0:N, 0:N] / S
    dx, dy = (x - cx) / r, (y - cy) / r
    d2 = dx * dx + dy * dy
    dedans = d2 < 1
    dz = np.sqrt(np.clip(1 - d2, 0, 1))
    lum = np.clip(dx * -0.55 + dy * -0.6 + dz * 0.6, 0, 1)
    rel = bruit(N, 90, 11, 5)
    base = 0.55 + 0.45 * rel
    rng = random.Random(5)
    cr = np.zeros((N, N), np.float32)
    for _ in range(46):
        a, b, rr = rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(0.03, 0.16)
        if a * a + b * b > 0.9:
            continue
        dd = np.hypot(dx - a, dy - b) / rr
        cr += np.where(dd < 1, -0.25 * (1 - dd ** 2), 0) + np.where((dd >= 1) & (dd < 1.25), 0.18 * (1.25 - dd) * 4, 0)
    v = np.clip((base + cr) * (0.12 + 0.95 * lum), 0, 1.2)
    col = np.stack([v * 200 + 12, v * 208 + 14, v * 225 + 22], -1)
    a = np.array(img).astype(np.float32)
    bord = np.clip((1 - np.sqrt(d2)) * r * S / 2, 0, 1)[..., None]
    a[..., :3] = np.where(dedans[..., None], a[..., :3] * (1 - bord) + col * bord, a[..., :3])
    img = Image.fromarray(a.astype(np.uint8))
    # halo du limbe
    c = calque()
    ImageDraw.Draw(c).ellipse([p(cx - r), p(cy - r), p(cx + r), p(cy + r)], outline=(120, 190, 255, 120), width=p(3))
    coller(img, c, 6)
    # orbite : moitié avant
    orb = calque()
    d = ImageDraw.Draw(orb)
    d.arc([p(cx - 250), p(cy - 300 - 62), p(cx + 250), p(cy - 300 + 62)], 0, 180, fill=(60, 200, 255, 200), width=p(1.4))
    coller(img, orb, 2)
    # étiquettes
    c = calque()
    d = ImageDraw.Draw(c)
    texte(d, (62, 214), 'TERRAIN N-12', MONO(15), (60, 200, 255, 255), 'lm')
    texte(d, (62, 234), 'LUNE ORBITALE', MONO(15), (200, 220, 240, 255), 'lm')
    d.line([p(62), p(248), p(200), p(248)], fill=(60, 200, 255, 160), width=p(1))
    vertical(d, 384, 84, '月面', KANJI(30), (220, 235, 255, 200), 36)
    coller(img, c, 2, 0.6)
    return img


def mecha():
    """Alerte rouge : bandes de danger, grille hexagonale, kanji 警告, trèfle de radiation"""
    img = vers_image(radial((52, 6, 8), (8, 0, 1)))
    c = calque()
    d = ImageDraw.Draw(c)
    # hexagones
    rh = 18
    for j in range(-1, 20):
        for i in range(-1, 18):
            hx, hy = i * rh * 1.5, j * rh * math.sqrt(3) + (i % 2) * rh * math.sqrt(3) / 2
            pts = [(p(hx + rh * math.cos(k * math.pi / 3)), p(hy + rh * math.sin(k * math.pi / 3))) for k in range(6)]
            d.polygon(pts, outline=(150, 20, 24, 60))
    coller(img, c)
    # bandes de danger en haut et en bas
    for y0, y1 in [(34, 62), (404, 432)]:
        c = calque()
        d = ImageDraw.Draw(c)
        d.rectangle([0, p(y0), N, p(y1)], fill=(20, 4, 4, 255))
        for k in range(-10, 40):
            x = k * 22
            d.polygon([(p(x), p(y1)), (p(x + 11), p(y1)), (p(x + 11 + (y1 - y0)), p(y0)), (p(x + (y1 - y0)), p(y0))], fill=(230, 40, 30, 255))
        coller(img, c)
    c = calque()
    d = ImageDraw.Draw(c)
    vertical(d, 64, 132, '警告', KANJI(66), (255, 30, 40, 70), 80)
    coller(img, c)
    # trèfle de radiation dans un triangle
    c = calque()
    d = ImageDraw.Draw(c)
    tx, ty = 382, 300
    d.polygon([(p(tx), p(ty - 34)), (p(tx + 34), p(ty + 26)), (p(tx - 34), p(ty + 26))], outline=(255, 190, 40, 255), width=p(3))
    for k in range(3):
        a0 = -90 + k * 120 - 30
        d.pieslice([p(tx - 18), p(ty - 18 + 6), p(tx + 18), p(ty + 18 + 6)], a0, a0 + 60, fill=(255, 190, 40, 255))
    d.ellipse([p(tx - 5), p(ty + 1), p(tx + 5), p(ty + 11)], fill=(255, 190, 40, 255))
    d.ellipse([p(tx - 3), p(ty + 3), p(tx + 3), p(ty + 9)], fill=(40, 4, 4, 255))
    coller(img, c, 4, 1.2)
    c = calque()
    d = ImageDraw.Draw(c)
    texte(d, (233, 96), 'G.E. UNITÉ 7A', MONO(17), (255, 220, 210, 255))
    texte(d, (233, 116), 'ZONE 3', MONO(13), (255, 90, 70, 255))
    texte(d, (233, 346), 'RADIATION ÉLEVÉE', OX(15), (255, 190, 40, 255))
    coller(img, c, 2, 0.7)
    return img


def radar():
    """Radar au phosphore vert : anneaux, relèvements, cases BAT et TEMP, case de l'heure en bas"""
    img = vers_image(radial((0, 30, 14), (0, 5, 3)))
    c = calque()
    d = ImageDraw.Draw(c)
    vert, vclair = (0, 150, 60, 255), (60, 255, 120, 255)
    for r in (70, 140, 205):
        d.ellipse([p(233 - r), p(233 - r), p(233 + r), p(233 + r)], outline=vert, width=p(1.2))
    d.line([p(28), p(233), p(438), p(233)], fill=(0, 120, 50, 200), width=p(1))
    d.line([p(233), p(28), p(233), p(438)], fill=(0, 120, 50, 200), width=p(1))
    for k in range(72):
        a = k * math.tau / 72
        r0 = 205 if k % 2 else 198
        d.line([p(233 + math.sin(a) * r0), p(233 - math.cos(a) * r0), p(233 + math.sin(a) * 214), p(233 - math.cos(a) * 214)], fill=vert, width=p(1))
    for k in range(12):
        a = k * math.tau / 12
        texte(d, (233 + math.sin(a) * 186, 233 - math.cos(a) * 186), f'{k * 30:03d}', MONO(11), vclair)
    for r in range(28, 205, 14):
        for k in range(0, 360, 15):
            a = math.radians(k)
            d.point((p(233 + math.sin(a) * r), p(233 - math.cos(a) * r)), fill=(0, 110, 45, 160))
    # cases : BAT et TEMP en haut, heure et date en bas
    for x0, lab in [(118, 'BAT'), (288, 'TEMP')]:
        d.rectangle([p(x0), p(108), p(x0 + 60), p(146)], fill=(0, 14, 6, 230), outline=vclair, width=p(1))
        texte(d, (x0 + 4, 114), lab, MONO(10), (0, 200, 90, 255), 'lm')
    d.rectangle([p(143), p(278), p(323), p(350)], fill=(0, 14, 6, 235), outline=vclair, width=p(1.2))
    d.rectangle([p(163), p(356), p(303), p(380)], fill=(0, 14, 6, 235), outline=vert, width=p(1))
    texte(d, (233, 72), 'SCAN', MONO(12), vclair)
    coller(img, c, 2, 0.8)
    # lignes de balayage de l'écran
    c = calque()
    d = ImageDraw.Draw(c)
    for y in range(0, N, p(3)):
        d.line([0, y, N, y], fill=(0, 0, 0, 50))
    coller(img, c)
    return img


def gear():
    """Plan d'un robot : grille de bleu d'architecte, tête de mecha au trait, kanji 機動兵装"""
    img = vers_image(radial((14, 30, 60), (4, 8, 20)))
    c = calque()
    d = ImageDraw.Draw(c)
    for k in range(0, W, 20):
        f = 70 if k % 100 == 0 else 30
        d.line([p(k), 0, p(k), N], fill=(80, 140, 220, f), width=1)
        d.line([0, p(k), N, p(k)], fill=(80, 140, 220, f), width=1)
    coller(img, c)
    # tête de robot (au trait), centre vers (300, 270)
    c = calque()
    d = ImageDraw.Draw(c)
    cy_ = (90, 230, 255, 255)

    def poly(pts, w=1.6, col=cy_, ferme=True):
        q = [(p(x), p(y)) for x, y in pts]
        d.line(q + ([q[0]] if ferme else []), fill=col, width=p(w), joint='curve')
    poly([(300, 188), (346, 208), (360, 262), (348, 316), (320, 340), (280, 340), (252, 316), (240, 262), (254, 208)])   # casque
    poly([(300, 196), (248, 140), (262, 136), (300, 178), (338, 136), (352, 140)], 1.8)                                  # antenne en V
    poly([(258, 250), (342, 250), (334, 270), (266, 270)])                                                               # visière
    poly([(272, 292), (328, 292), (320, 330), (280, 330)])                                                               # mentonnière
    for k in range(4):
        poly([(284 + k * 10, 300), (284 + k * 10, 322)], 1.2, ferme=False)
    poly([(240, 262), (214, 272), (214, 312), (252, 316)], 1.4, ferme=False)                                            # joues
    poly([(360, 262), (386, 272), (386, 312), (348, 316)], 1.4, ferme=False)
    poly([(196, 350), (404, 350), (420, 392), (180, 392)], 1.4)                                                          # épaules
    for x, y, l in [(360, 230, 'CAPTEUR'), (386, 296, 'BLINDAGE'), (338, 158, 'ANTENNE')]:
        texte(d, (x + 8, y - 6), l, MONO(9), (140, 200, 255, 200), 'lm')
    coller(img, c, 3, 1.1)
    # yeux orange
    c = calque()
    d = ImageDraw.Draw(c)
    d.polygon([(p(270), p(256)), (p(296), p(256)), (p(292), p(264)), (p(274), p(264))], fill=(255, 140, 30, 255))
    d.polygon([(p(304), p(256)), (p(330), p(256)), (p(326), p(264)), (p(308), p(264))], fill=(255, 140, 30, 255))
    coller(img, c, 5, 1.8)
    c = calque()
    d = ImageDraw.Draw(c)
    vertical(d, 76, 176, '機動兵装', KANJI(30), (235, 242, 255, 255), 38)
    d.line([p(98), p(160), p(98), p(330)], fill=(255, 140, 30, 200), width=p(1.5))
    texte(d, (233, 416), 'GEAR TYPE-04', ARMOR(14), (255, 150, 40, 255))
    coller(img, c, 2, 0.6)
    return img


def globe():
    """Globe filaire : terres en points, méridiens et parallèles, atmosphère, orbite"""
    img = vers_image(radial((6, 14, 28), (1, 3, 8)))
    etoiles(img, 200, 9)
    cx, cy, r = 233, 236, 124
    inc = math.radians(23)
    y, x = np.mgrid[0:N, 0:N] / S
    dx, dy = (x - cx) / r, (y - cy) / r
    d2 = dx * dx + dy * dy
    dedans = d2 < 1
    dz = np.sqrt(np.clip(1 - d2, 0, 1))
    lum = np.clip(-0.5 * dx - 0.4 * dy + 0.75 * dz, 0.08, 1)
    a = np.array(img).astype(np.float32)
    ocean = np.stack([4 + 10 * lum, 30 + 40 * lum, 44 + 50 * lum], -1)
    a[..., :3] = np.where(dedans[..., None], ocean, a[..., :3])
    img = Image.fromarray(a.astype(np.uint8))
    # terres : points sur le relief d'un bruit 3D (somme de sinus), seulement face visible
    c = calque()
    d = ImageDraw.Draw(c)

    def terre(lat, lon):
        v = (math.sin(lat * 2.1 + 0.7) * math.cos(lon * 1.3 + 0.2) + 0.6 * math.sin(lat * 4.3 + lon * 2.7 + 1.1)
             + 0.35 * math.cos(lat * 7.1 - lon * 5.3) + 0.25 * math.sin(lon * 3.9 + 2.0))
        return v > 0.55

    def proj(lat, lon, rot=0.4):
        X, Y, Z = math.cos(lat) * math.sin(lon + rot), math.sin(lat), math.cos(lat) * math.cos(lon + rot)
        Y, Z = Y * math.cos(inc) - Z * math.sin(inc), Y * math.sin(inc) + Z * math.cos(inc)
        return cx + X * r, cy - Y * r, Z

    nb = 9000    # points répartis également (spirale de Fibonacci)
    for k in range(nb):
        lat = math.asin(1 - 2 * (k + 0.5) / nb)
        lon = (k * math.pi * (3 - math.sqrt(5))) % math.tau
        if terre(lat, lon):
            px_, py_, z = proj(lat, lon)
            if z > 0.02:
                rr = 1.1 * (0.4 + 0.6 * z)
                d.ellipse([p(px_ - rr), p(py_ - rr), p(px_ + rr), p(py_ + rr)], fill=(60, 230, 200, int(120 + 135 * z)))
    for i in range(-75, 76, 15):     # parallèles
        pts = [proj(math.radians(i), math.radians(j)) for j in range(0, 361, 5)]
        for (x0, y0, z0), (x1, y1, z1) in zip(pts, pts[1:]):
            if z0 > 0 and z1 > 0:
                d.line([p(x0), p(y0), p(x1), p(y1)], fill=(60, 200, 210, 70), width=p(0.8))
    for j in range(0, 360, 20):      # méridiens
        pts = [proj(math.radians(i), math.radians(j)) for i in range(-90, 91, 5)]
        for (x0, y0, z0), (x1, y1, z1) in zip(pts, pts[1:]):
            if z0 > 0 and z1 > 0:
                d.line([p(x0), p(y0), p(x1), p(y1)], fill=(60, 200, 210, 70), width=p(0.8))
    coller(img, c, 1.5, 0.7)
    c = calque()
    d = ImageDraw.Draw(c)
    d.ellipse([p(cx - r), p(cy - r), p(cx + r), p(cy + r)], outline=(80, 220, 255, 200), width=p(3))
    coller(img, c, 7, 1.5)
    c = calque()
    d = ImageDraw.Draw(c)
    texte(d, (233, 414), 'LAT 48.85 N · LON 02.35 E', MONO(12), (80, 220, 210, 255))
    for k in range(4):   # repères de visée
        a = k * math.tau / 4 + math.tau / 8
        x0, y0 = cx + math.sin(a) * (r + 18), cy - math.cos(a) * (r + 18)
        d.line([p(x0), p(y0), p(cx + math.sin(a) * (r + 30)), p(cy - math.cos(a) * (r + 30))], fill=(80, 220, 210, 200), width=p(1.4))
    coller(img, c, 2, 0.6)
    return img


def synthwave():
    """Coucher de soleil rétro : soleil à bandes, montagnes, grille en perspective, « 終わりの後に »"""
    hz = 292
    img = vers_image(degrade_v([(0, (14, 4, 34)), (150, (70, 16, 92)), (250, (200, 50, 120)), (hz, (255, 120, 110)), (hz + 1, (24, 4, 40)), (466, (8, 0, 18))]))
    etoiles(img, 120, 4, 170)
    # soleil
    c = calque()
    sx, sy, sr = 233, 236, 104
    sol = vers_image(degrade_v([(sy - sr, (255, 236, 110)), (sy, (255, 140, 90)), (sy + sr, (255, 40, 150))]))
    m = Image.new('L', (N, N), 0)
    dm = ImageDraw.Draw(m)
    dm.ellipse([p(sx - sr), p(sy - sr), p(sx + sr), p(sy + sr)], fill=255)
    for k in range(7):
        yb = sy + 6 + k * 13
        dm.rectangle([0, p(yb), N, p(yb + 2 + k * 0.9)], fill=0)
    c.paste(sol, (0, 0), m)
    coller(img, c, 12, 1.2)
    # montagnes
    rng = random.Random(2)
    for couche, (haut, col, bord) in enumerate([(70, (70, 18, 96), (255, 80, 200)), (42, (34, 8, 56), (140, 60, 255))]):
        c = calque()
        d = ImageDraw.Draw(c)
        pts, x = [(0, hz)], 0
        while x < W:
            x += rng.uniform(18, 40)
            pts.append((x, hz - rng.uniform(0.2, 1) * haut * (1.3 if (x < 140 or x > 330) else 0.55)))
        pts.append((W, hz))
        d.polygon([(p(a), p(b)) for a, b in pts], fill=col + (255,))
        d.line([(p(a), p(b)) for a, b in pts[1:-1]], fill=bord + (200,), width=p(1))
        coller(img, c, 2 if couche == 0 else 0, 0.6)
    # grille
    c = calque()
    d = ImageDraw.Draw(c)
    for k in range(-14, 15):
        d.line([p(233 + k * 9), p(hz), p(233 + k * 120), p(480)], fill=(255, 40, 200, 220), width=p(1.2))
    yy, pas = hz + 3, 4
    while yy < 470:
        d.line([0, p(yy), N, p(yy)], fill=(255, 40, 200, 220), width=p(1.2))
        pas *= 1.28
        yy += pas
    coller(img, c, 3, 1.2)
    c = calque()
    d = ImageDraw.Draw(c)
    texte(d, (233, 398), '終わりの後に', KANJI(24), (255, 200, 240, 255))
    coller(img, c, 5, 1.5)
    return img


def mk4():
    """GEAR SYSTEM MK-IV : anneau de 60 segments (les secondes), arcs PUISSANCE et TEMP, équerres orange"""
    img = vers_image(radial((8, 30, 34), (1, 6, 8)))
    c = calque()
    d = ImageDraw.Draw(c)
    rh = 14
    for j in range(-1, 24):
        for i in range(-1, 24):
            hx, hy = i * rh * 1.5, j * rh * math.sqrt(3) + (i % 2) * rh * math.sqrt(3) / 2
            pts = [(p(hx + rh * math.cos(k * math.pi / 3)), p(hy + rh * math.sin(k * math.pi / 3))) for k in range(6)]
            d.polygon(pts, outline=(40, 160, 170, 28))
    coller(img, c)
    c = calque()
    d = ImageDraw.Draw(c)
    teal, orange = (40, 220, 210, 255), (255, 140, 40, 255)
    # emplacements des 60 segments (allumés par le firmware entre 206 et 224)
    for k in range(60):
        a0, a1 = k / 60 * 360 - 90 + 0.8, (k + 1) / 60 * 360 - 90 - 0.8
        d.arc([p(233 - 224), p(233 - 224), p(233 + 224), p(233 + 224)], a0, a1, fill=(20, 70, 72, 255), width=p(18))
    # rails des arcs intérieurs (gauche : PUISSANCE de 0.58 à 0.92 tour ; droite : TEMP de 0.08 à 0.42)
    for a0, a1 in [(0.58, 0.92), (0.08, 0.42)]:
        d.arc([p(233 - 188), p(233 - 188), p(233 + 188), p(233 + 188)], a0 * 360 - 90, a1 * 360 - 90, fill=(30, 50, 54, 255), width=p(9))
    texte(d, (78, 233), 'PUISSANCE', MONO(10), orange, 'mm')
    texte(d, (388, 233), 'TEMP', MONO(10), teal, 'mm')
    # équerres
    for sx_, sy_ in [(-1, -1), (1, -1), (-1, 1), (1, 1)]:
        x0, y0 = 233 + sx_ * 128, 233 + sy_ * 92
        d.line([p(x0), p(y0), p(x0 - sx_ * 22), p(y0)], fill=orange, width=p(2.5))
        d.line([p(x0), p(y0), p(x0), p(y0 - sy_ * 22)], fill=orange, width=p(2.5))
    texte(d, (233, 104), 'GEAR SYSTEM MK-IV', MONO(14), teal)
    texte(d, (233, 362), 'SN 04-0927 · SYNC', MONO(10), (40, 160, 160, 255))
    coller(img, c, 2, 0.8)
    return img


THEMES = {'neon': neon, 'hud': hud, 'lune': lune, 'mecha': mecha, 'radar': radar, 'gear': gear, 'globe': globe,
          'synthwave': synthwave, 'mk4': mk4}


def ecrire(img, nom):
    img = img.convert('RGB').resize((W, W), Image.LANCZOS)
    m = Image.new('L', (W, W), 0)
    ImageDraw.Draw(m).ellipse([0, 0, W - 1, W - 1], fill=255)
    noir = Image.new('RGB', (W, W), (0, 0, 0))
    img = Image.composite(img, noir, m)
    os.makedirs(os.path.join(SORTIE, 'apercu'), exist_ok=True)
    img.save(os.path.join(SORTIE, 'apercu', nom + '.png'))
    q = img.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:768]
    n = len(pal) // 3
    rgb565 = [((pal[3 * k] & 0xF8) << 8) | ((pal[3 * k + 1] & 0xFC) << 3) | (pal[3 * k + 2] >> 3) for k in range(n)]
    with open(os.path.join(SORTIE, f'cadran_{nom}.cad'), 'wb') as f:
        f.write(b'CAD1' + struct.pack('<HHH', W, W, n) + struct.pack(f'<{n}H', *rgb565) + q.tobytes())
    print(f'{nom} : {n} couleurs')


def main():
    for nom in sys.argv[1:] or THEMES:
        ecrire(THEMES[nom](), nom)


if __name__ == '__main__':
    main()
