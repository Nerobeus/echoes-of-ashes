"""Traduit un plantage de la console RetroFW (lignes « PLANTAGE » et « PILE » de journal.txt, écrites par plantage() de
platform/retrofw/main_retrofw.c) en fonctions et lignes du code, avec le programme et ses symboles de la même compilation
que celui de la console (build/retrofw/choeur.elf ou ile.elf : la version est en tête du journal).

    tools/.venv/Scripts/python.exe engine/tools/plantage_retrofw.py engine/build/retrofw/choeur.elf G:/.choeur/journal.txt

pc : l'instruction fautive ; ra : l'adresse de retour (l'appelant, si la fonction n'en a pas appelé d'autre) ; pile : les
mots de la pile qui pointent dans le code, du plus récent au plus ancien (adresses de retour probables, et quelques restes).
"""
import bisect
import re
import sys

from elftools.elf.elffile import ELFFile

sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])
from profil_retrofw import symboles, nom_de, lignes  # noqa: E402


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    elf = ELFFile(open(sys.argv[1], 'rb'))
    adresses, fs = symboles(elf)
    table = lignes(elf) or []
    cles = [t[0] for t in table]

    def ou(a):
        n = nom_de(adresses, fs, a)
        i = bisect.bisect_right(cles, a) - 1
        l = f'{table[i][1]}:{table[i][2]}' if i >= 0 else '?'
        return f'{a:08x}  {n}  ({l})'

    texte = open(sys.argv[2], encoding='utf-8', errors='replace').read()
    vu = False
    for ligne in texte.splitlines():
        if ligne.startswith('PLANTAGE'):
            vu = True
            print(ligne)
            m = dict(re.findall(r'(pc|ra|sp) ([0-9a-f]{8})', ligne))
            for k in ('pc', 'ra'):
                if k in m:
                    print(f'  {k} : {ou(int(m[k], 16))}')
        elif ligne.startswith('PILE'):
            print('  pile :')
            for a in ligne.split()[1:]:
                print('    ' + ou(int(a, 16)))
    if not vu:
        print('pas de ligne PLANTAGE dans ' + sys.argv[2])


if __name__ == '__main__':
    main()
