#!/usr/bin/env python3
"""
Essais sur la montre par l'USB (firmware Echoes en jeu, commandes SDX de platform/esp32/main/sdx.c) :

  capture_montre.py ecran sortie.png             capture de l'image affichée (SDX ECRAN)
  capture_montre.py course K A [s] [png]         course d'essai pilotée sur la piste K (A = 0 : sans l'allègement du
                                                 rendu en course), journal des i/s pendant s secondes (40), capture à
                                                 la fin si png est donné
  capture_montre.py journal [s]                  journal de la carte pendant s secondes (20)
  capture_montre.py essai x z cap pluie heure s [journal.txt] [png]
                                                 essai de fluidité (SDX ESSAI, eng_essai_monde de game.c) : Sera posée en
                                                 x, z, pluie 0 / 1, heure 0 à 1 (-1 : celle du moment), elle court vers le
                                                 cap pendant s secondes ; journal (avec le profil d'un firmware
                                                 -DECHOES_PROFIL=1) écrit dans journal.txt, capture au milieu
  capture_montre.py xp n                         essais de mesure du firmware de profilage (SDX XP, main.c et render.c) :
                                                 1 = en série, 8 = cœur 0 sans bandes, 2 = textures en 2 × 2 texels, 0 = rien
  capture_montre.py demarrage [s] [journal.txt]  journal du démarrage : rouvre le port dès qu'il revient (après un flash
                                                 ou un redémarrage), lignes du journal pendant s secondes (40)
  capture_montre.py app NOM [--echoes]           lance une application (SDX APP : ile, echoes, pcdos, gameboy…) ; attend que
                                                 le port revienne (après une copie, la montre redémarre) ; --echoes : Echoes
                                                 lancé d'abord (l'app montre ne lit pas les commandes)

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
    s.write_timeout = 5   # l'app montre (Echoes en veille) ne lit pas la console : pas d'envoi bloqué pour toujours
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
JOURNAL_FICHIER = None   # essai : toutes les lignes de la console y sont ajoutées


def journal(lien, duree, afficher=True):
    """Lignes de la console pendant duree secondes ; renvoie les (i/s, triangles) relevés"""
    fin, ips = time.time() + duree, []
    while time.time() < fin:
        l = lien.ligne(0.5)
        if l is None:
            continue
        l = RE_ANSI.sub('', l)
        if JOURNAL_FICHIER:
            with open(JOURNAL_FICHIER, 'a', encoding='utf-8') as f:
                f.write(l + chr(10))
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


def lancer_app(port, nom, echoes):
    """SDX APP nom, répété jusqu'à la réponse : la montre peut encore redémarrer (fin d'une copie) ou démarrer Echoes"""
    if echoes:
        from envoi_sd import lancer_echoes
        lancer_echoes(port or port_auto())
    fin = time.time() + 60
    while True:
        try:
            lien = Lien(ouvrir(port or port_auto()))
            break
        except (serial.SerialException, SystemExit):
            if time.time() > fin:
                sys.exit('montre introuvable sur l\'USB')
            time.sleep(0.5)
    fin = time.time() + 45
    while time.time() < fin:
        try:
            lien.envoyer('SDX APP ' + nom)
        except serial.SerialTimeoutException:
            sys.exit('la montre ne lit pas les commandes (app montre ?) : --echoes')
        fin2 = time.time() + 3
        while time.time() < fin2:
            l = lien.ligne(0.5)
            if l and 'SDX APP OK' in l:
                print(nom + ' lancé sur la montre', flush=True)
                return
            if l and 'SDX ERR' in l:
                sys.exit(l[l.index('SDX ERR'):])
    sys.exit('pas de réponse de la montre (Echoes en route ?)')


def main():
    a = argparse.ArgumentParser()
    a.add_argument('--port')
    a.add_argument('action', choices=['ecran', 'course', 'journal', 'essai', 'xp', 'demarrage', 'etat', 'app'])
    a.add_argument('args', nargs='*')
    a.add_argument('--echoes', action='store_true')
    o = a.parse_args()
    if o.action == 'app':   # accueil Île de Nacre (tools/atelier_combat/nacre.js) : « Lancer l'île sur la montre »
        lancer_app(o.port, o.args[0] if o.args else 'ile', o.echoes)
        return
    if o.action == 'demarrage':
        global JOURNAL_FICHIER
        JOURNAL_FICHIER = o.args[1] if len(o.args) > 1 else None
        fin = time.time() + 60
        while True:
            try:
                lien = Lien(ouvrir(o.port or port_auto()))
                break
            except (serial.SerialException, SystemExit):
                if time.time() > fin:
                    sys.exit('port absent')
                time.sleep(0.05)
        journal(lien, float(o.args[0]) if o.args else 40, afficher=False)
        return
    lien = Lien(ouvrir(o.port or port_auto()))
    if o.action == 'ecran':
        ecran(lien, o.args[0] if o.args else 'ecran_montre.png')
    elif o.action == 'xp':
        lien.envoyer(f'SDX XP {int(o.args[0])}')
        fin = time.time() + 3
        while time.time() < fin:
            l = lien.ligne(0.5)
            if l and 'SDX XP' in l:
                print(l)
                return
        print('pas de réponse (firmware sans SDX XP ?)')
    elif o.action == 'etat':   # état des rencontres de la partie en cours (SDX ETAT, eng_etat_rencontres) : etat [n] [secondes entre deux]
        n, pas = (int(o.args[0]) if o.args else 1), (float(o.args[1]) if len(o.args) > 1 else 2)
        for i in range(n):
            lien.envoyer('SDX ETAT')
            fin = time.time() + 4
            while time.time() < fin:
                l = lien.ligne(0.5)
                if l and 'SDX ETAT' in l:
                    print(time.strftime('%H:%M:%S'), l[l.index('SDX ETAT') + 9:])
                    break
            else:
                print('pas de réponse (firmware sans SDX ETAT ?)')
            if i + 1 < n:
                time.sleep(pas)
    elif o.action == 'journal':
        resume(journal(lien, float(o.args[0]) if o.args else 20))
    elif o.action == 'essai':
        x, z, cap, pluie, heure, s = (float(v) for v in o.args[:6])
        lien.envoyer(f'SDX ESSAI {x} {z} {cap} {int(pluie)} {heure} {s}')
        JOURNAL_FICHIER = o.args[6] if len(o.args) > 6 else None
        ips = journal(lien, s / 2)
        if len(o.args) > 7:
            ecran(lien, o.args[7])
        ips += journal(lien, s / 2 - 1)
        resume(ips[1:])   # la première mesure couvre le chargement du lieu
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
