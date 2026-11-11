"""Lit le profil de la console RetroFW (profil.bin, écrit par platform/retrofw/main_retrofw.c : « PRF1 », nombre, puis
paires pc / adresse de retour, un échantillon tous les ~10 ms de temps processeur) avec le programme et ses symboles
(build/retrofw/choeur.elf, de la même compilation que le choeur.dge copié sur la console) : temps par fonction, appelants
des petites fonctions de musl (memcpy, memset…), lignes les plus chaudes des fonctions qui pèsent le plus.

    tools/.venv/Scripts/python.exe engine/tools/profil_retrofw.py engine/build/retrofw/choeur.elf G:/.choeur/profil.bin [n]

pyelftools (pip install pyelftools dans tools/.venv) pour les symboles et les lignes (DWARF).
"""
import bisect
import collections
import struct
import sys

from elftools.elf.elffile import ELFFile


def symboles(elf):
    t = elf.get_section_by_name('.symtab')
    if not t:
        sys.exit('pas de table des symboles : prendre choeur.elf, pas choeur.dge')
    fs = sorted((s['st_value'], s['st_size'], s.name) for s in t.iter_symbols()
                if s['st_info']['type'] == 'STT_FUNC' and s['st_value'])
    return [f[0] for f in fs], fs


def nom_de(adresses, fs, pc):
    i = bisect.bisect_right(adresses, pc) - 1
    if i < 0:
        return '?'
    a, taille, nom = fs[i]
    return nom if not taille or pc < a + taille else '?'


def lignes(elf):
    """adresse -> (fichier, ligne), d'après la table des lignes DWARF"""
    if not elf.has_dwarf_info():
        return None
    dw = elf.get_dwarf_info()
    table = []
    for cu in dw.iter_CUs():
        lp = dw.line_program_for_CU(cu)
        if not lp:
            continue
        fichiers = lp['file_entry']
        v5 = lp.header.version >= 5
        for e in lp.get_entries():
            st = e.state
            if st is None or st.end_sequence:
                continue
            k = st.file if v5 else st.file - 1
            nomf = fichiers[k].name.decode() if 0 <= k < len(fichiers) else '?'
            table.append((st.address, nomf, st.line))
    table.sort()
    return table


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    n_aff = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    d = open(sys.argv[2], 'rb').read()
    if d[:4] != b'PRF1':
        sys.exit('profil.bin illisible')
    n = struct.unpack_from('<I', d, 4)[0]
    ech = [struct.unpack_from('<II', d, 8 + 8 * i) for i in range(n)]
    with open(sys.argv[1], 'rb') as f:
        elf = ELFFile(f)
        adresses, fs = symboles(elf)
        tl = lignes(elf)
    print(f'{n} échantillons (~{n / 100:.0f} s de temps processeur)')
    par_f = collections.Counter(nom_de(adresses, fs, pc) for pc, _ in ech)
    print('\nFonctions :')
    for nom, c in par_f.most_common(n_aff):
        print(f'  {100 * c / n:5.1f} %  {c:6d}  {nom}')
    # petites fonctions de la bibliothèque : qui les appelle (adresse de retour)
    petites = {'memcpy', 'memset', 'memmove', '__divdi3', '__udivdi3', '__moddi3', 'sqrtf', 'floorf', 'ceilf', 'lroundf', 'sinf', 'cosf', 'atan2f', 'fmodf', 'powf'}
    app = collections.Counter()
    for pc, ra in ech:
        nf = nom_de(adresses, fs, pc)
        if nf in petites:
            app[(nf, nom_de(adresses, fs, ra))] += 1
    if app:
        print('\nAppelants des fonctions de la bibliothèque :')
        for (nf, qui), c in app.most_common(15):
            print(f'  {100 * c / n:5.1f} %  {nf} <- {qui}')
    if tl:
        cles = [t[0] for t in tl]
        par_l = collections.Counter()
        for pc, _ in ech:
            i = bisect.bisect_right(cles, pc) - 1
            if i >= 0:
                par_l[(nom_de(adresses, fs, pc), tl[i][1].split('/')[-1].split('\\')[-1], tl[i][2])] += 1
        print('\nLignes les plus chaudes :')
        for (nf, fich, li), c in par_l.most_common(30):
            print(f'  {100 * c / n:5.1f} %  {fich}:{li}  ({nf})')


if __name__ == '__main__':
    main()
