#!/usr/bin/env python3
"""
Essais sur la montre par l'USB (firmware Echoes en jeu, commandes SDX de platform/esp32/main/sdx.c) :

  capture_montre.py ecran sortie.png             capture de l'image affichée (SDX ECRAN)
  capture_montre.py course K A [s] [png]         course d'essai pilotée sur la piste K (A = 0 : sans l'allègement du
                                                 rendu en course), journal des i/s pendant s secondes (40), capture à
                                                 la fin si png est donné
  capture_montre.py journal [s]                  journal de la carte pendant s secondes (20)

  --port COMx : port de la montre (sinon le premier ESP32-S3 en USB)

Le port s'ouvre sans toucher à DTR / RTS (la carte redémarrerait). Les lignes « i/s » de la console (toutes les 2 s)
sont relevées et résumées : moyenne, plus basse, plus haute.
"""
import argparse
import re
import sys
import time

import serial
import serial.tools.list_ports
import numpy as np
from PIL import Image


def port_auto():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x303A:
            return p.device
    sys.exit("pas d'ESP32-S3 branché en USB (Espressif 303A) : --port COMx")


def ouvrir(port):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.05
    s.dtr = s.rts = False
    s.open()
    return s


class Lien:
    def __init__(self, s):
        self.s, self.reste = s, b''

    def envoyer(self, t):
        self.s.write(t.encode('ascii') + b'\n')
        self.s.flush()

    def ligne(self, delai):
        fin = time.time() + delai
        while b'\n' not in self.reste:
            if time.time() > fin:
                return None
            self.reste += self.s.read(65536)
        l, self.reste = self.reste.split(b'\n', 1)
        return l.decode('utf-8', 'replace').rstrip('\r')


RE_IPS = re.compile(r'([0-9.]+) i/s \| (\d+) triangles')
RE_ANSI = re.compile(r'\x1b\[[0-9;]*m')


def journal(lien, duree, afficher=True):
    """Lignes de la console pendant duree secondes ; renvoie les (i/s, triangles) relevés"""
    fin, ips = time.time() + duree, []
    while time.time() < fin:
        l = lien.ligne(0.5)
        if l is None:
            continue
        l = RE_ANSI.sub('', l)
        m = RE_IPS.search(l)
        if m:
            ips.append((float(m.group(1)), int(m.group(2))))
        if afficher and (m or 'cœur' in l or 'chunks' in l or 'SDX' in l or 'E (' in l or 'W (' in l):
            print(l)
    return ips


def ecran(lien, sortie, taille=466):
    """Bandes « SDX EB y x0 l h » suivies de l × h pixels RGB565 bruts (octets inversés, comme pour l'écran), jusqu'à
    « SDX ECRAN FIN » (platform/esp32/main/main.c, capture_ecran)"""
    lien.s.reset_input_buffer()
    lien.reste = b''
    lien.envoyer('SDX ECRAN')
    t0, fin, buf, n = time.time(), time.time() + 30, b'', 0
    rgb = np.zeros((taille, taille, 3), np.uint8)
    while time.time() < fin:
        buf += lien.s.read(1 << 16)
        while True:
            i = buf.find(b'SDX E')
            if i < 0:
                buf = buf[-8:]
                break
            j = buf.find(b'\n', i)
            if j < 0:
                buf = buf[i:]
                break
            t = buf[i:j].decode('ascii', 'replace').split()
            if t[1:3] == ['ECRAN', 'FIN']:
                Image.fromarray(rgb).save(sortie)
                print(f'capture : {sortie} ({n} bandes, {time.time() - t0:.1f} s)')
                return True
            if t[1] != 'EB' or len(t) < 6:
                buf = buf[j + 1:]
                continue
            y, x0, w, h = (int(v) for v in t[2:6])
            if len(buf) < j + 1 + w * h * 2:
                buf = buf[i:]
                break
            v = np.frombuffer(buf[j + 1:j + 1 + w * h * 2], '>u2').reshape(h, w).astype(np.uint32)
            buf = buf[j + 1 + w * h * 2:]
            rgb[y:y + h, x0:x0 + w, 0] = (v >> 11 & 31) * 255 // 31
            rgb[y:y + h, x0:x0 + w, 1] = (v >> 5 & 63) * 255 // 63
            rgb[y:y + h, x0:x0 + w, 2] = (v & 31) * 255 // 31
            n += 1
    print('capture : pas de fin (jeu en pause, ou firmware sans SDX ECRAN)')
    return False


def resume(ips):
    if not ips:
        print('aucune mesure i/s relevée')
        return
    v = [a for a, _ in ips]
    t = [b for _, b in ips]
    print(f'i/s : moyenne {sum(v) / len(v):.1f}, plus basse {min(v):.1f}, plus haute {max(v):.1f} ({len(v)} mesures de 2 s) ; '
          f'triangles : moyenne {sum(t) / len(t):.0f}, au plus {max(t)}')


def main():
    a = argparse.ArgumentParser()
    a.add_argument('--port')
    a.add_argument('action', choices=['ecran', 'course', 'journal'])
    a.add_argument('args', nargs='*')
    o = a.parse_args()
    lien = Lien(ouvrir(o.port or port_auto()))
    if o.action == 'ecran':
        ecran(lien, o.args[0] if o.args else 'ecran_montre.png')
    elif o.action == 'journal':
        resume(journal(lien, float(o.args[0]) if o.args else 20))
    else:
        k, al = int(o.args[0]), int(o.args[1])
        duree = float(o.args[2]) if len(o.args) > 2 else 40
        lien.envoyer(f'SDX COURSE {k} {al}')
        ips = journal(lien, duree)
        resume(ips[1:])   # la première mesure couvre le départ (chargement de la piste)
        if len(o.args) > 3:
            ecran(lien, o.args[3])


if __name__ == '__main__':
    main()
