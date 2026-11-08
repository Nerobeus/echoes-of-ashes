"""Moyenne des postes du profil de la console d'Echoes (firmware -DECHOES_PROFIL=1), journaux de capture_montre.py essai.
Usage : profil_moy.py journal1.txt [journal2.txt ...] ; la première mesure (chargement du lieu) est écartée."""
import re
import sys
from collections import OrderedDict

NUM = re.compile(r'([^,:;|()]+?)\s+(-?[0-9]+\.[0-9]+)')


def lire(p):
    blocs = OrderedDict()
    vus = {}
    for l in open(p, encoding='utf-8', errors='replace'):
        l = l.strip()
        m = re.search(r'echoes: (.*)', l)
        if not m:
            continue
        t = m.group(1)
        if ' : ' in t:
            tete, reste = t.split(' : ', 1)
        elif 'i/s' in t:
            tete, reste = 'i/s', t
        else:
            continue
        tete = tete.split('(')[0].strip()
        if tete.startswith('chunks'):
            continue
        vus[tete] = vus.get(tete, 0) + 1
        if vus[tete] == 1:
            continue   # première mesure : chargement du lieu
        if tete == 'i/s':
            mm = re.match(r'([0-9.]+) i/s \| (\d+) triangles.*?(\d+) pixels 3D, (\d+) op', reste)
            if mm:
                for k, v in zip(['i/s', 'triangles', 'pixels 3D', 'op. interface'], mm.groups()):
                    blocs.setdefault(('i/s', k), []).append(float(v))
            continue
        for nom, v in NUM.findall(reste):
            nom = nom.strip()
            blocs.setdefault((tete, nom), []).append(float(v))
    return {k: sum(v) / len(v) for k, v in blocs.items()}


js = [lire(p) for p in sys.argv[1:]]
cles = []
for j in js:
    for k in j:
        if k not in cles:
            cles.append(k)
print('%-60s' % '' + ''.join('%12s' % p.split('/')[-1].split('\\')[-1][:11] for p in sys.argv[1:]))
for k in cles:
    print('%-60s' % (k[0][:22] + ' | ' + k[1][:34]) + ''.join('%12s' % ('%.2f' % j[k] if k in j else '-') for j in js))
