#!/usr/bin/env python3
"""
Flash de la montre par nom de partition (table commune montre/partitions.csv) : avec quatre applications, « idf.py
flash » d'Echoes, du PC DOS ou du Game Boy écrirait dans la première partition d'application (« montre ») ; cet outil
écrit chaque image à l'adresse de sa partition, et refuse une image plus grande que sa partition.

  python montre/flasher.py COM8 tout          chargeur, table, otadata (démarrage sur montre), les quatre applications,
                                               paquet léger du monde, BIOS du PC
  python montre/flasher.py COM8 montre        une partie seulement : montre, echoes, pcdos, gameboy, monde, bios, chargeur
  python montre/flasher.py COM8 echoes pcdos  plusieurs parties à la fois

Après un changement de la table (31/10/2026 : partition « gameboy »), flasher « chargeur » (table et otadata) avec la
nouvelle application.

À lancer avec le Python de l'ESP-IDF (idf.cmd en a l'environnement) : il appelle « python -m esptool ».
"""
import os
import subprocess
import sys

RACINE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
TABLE = os.path.join(RACINE, 'montre', 'partitions.csv')
CONSTRUIT = os.path.join(RACINE, 'montre', 'build')
IMAGES = {
    'montre': os.path.join(CONSTRUIT, 'montre.bin'),
    'echoes': os.path.join(RACINE, 'engine', 'platform', 'esp32', 'build', 'echoes.bin'),
    'pcdos': os.path.join(RACINE, 'pcdos', 'esp', 'build', 'tiny386.bin'),
    'gameboy': os.path.join(RACINE, 'gameboy', 'build', 'gameboy.bin'),
    'monde': os.path.join(RACINE, 'engine', 'data', 'monde_esp32.pak'),
    'bios.bin': os.path.join(RACINE, 'pcdos', 'esp', 'rom', 'bios.bin'),
    'vgabios.bin': os.path.join(RACINE, 'pcdos', 'esp', 'rom', 'vgabios.bin'),
}


def adresses():
    a = {}
    for l in open(TABLE, encoding='utf-8'):
        c = [x.strip() for x in l.split('#')[0].split(',')]
        if len(c) > 3 and c[3]:
            a[c[0]] = c[3]
    return a


def tailles():
    """Taille de chaque partition (octets)"""
    t = {}
    for l in open(TABLE, encoding='utf-8'):
        c = [x.strip() for x in l.split('#')[0].split(',')]
        if len(c) > 4 and c[4]:
            v = c[4].upper()
            k = 1024 * 1024 if v.endswith('M') else 1024 if v.endswith('K') else 1
            t[c[0]] = int(v.rstrip('MK'), 0) * k
    return t


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    port, parts = sys.argv[1], sys.argv[2:]
    a = adresses()
    args = []
    if 'tout' in parts:
        parts = ['chargeur', 'montre', 'echoes', 'pcdos', 'gameboy', 'monde', 'bios']
    for p in parts:
        if p == 'chargeur':   # chargeur de démarrage, table, otadata initiale (démarrage sur « montre »)
            args += ['0x0', os.path.join(CONSTRUIT, 'bootloader', 'bootloader.bin'),
                     '0x8000', os.path.join(CONSTRUIT, 'partition_table', 'partition-table.bin'),
                     a['otadata'], os.path.join(CONSTRUIT, 'ota_data_initial.bin')]
        elif p == 'bios':
            args += [a['bios.bin'], IMAGES['bios.bin'], a['vgabios.bin'], IMAGES['vgabios.bin']]
        elif p in IMAGES and p in a:
            args += [a[p], IMAGES[p]]
        else:
            sys.exit(f'partie inconnue : {p}')
    for k in range(1, len(args), 2):
        if not os.path.isfile(args[k]):
            sys.exit(f'introuvable : {args[k]} (compiler d\'abord)')
    cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '-p', port, '-b', '921600', 'write_flash'] + args
    print(' '.join(cmd))
    sys.exit(subprocess.call(cmd))


if __name__ == '__main__':
    main()
