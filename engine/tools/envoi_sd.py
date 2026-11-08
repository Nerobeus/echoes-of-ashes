"""Copie des fichiers du jeu sur la carte SD de l'ESP32 par l'USB, sans sortir la carte (réception :
engine/platform/esp32/main/sdx.c, dans le firmware du jeu).

    tools/.venv/Scripts/python.exe engine/tools/envoi_sd.py [--port COM7] [--dossier engine/data] [--force] [fichiers...]

Par défaut : monde.pak, interieurs.bin, histoire.scn et musique.bin de engine/data (de engine/data/enfant si le
firmware est celui de l'édition enfant), et le paquet léger monde_esp32.pak du même export, flashé (esptool) dans la
partition « monde ». Seuls les fichiers qui ont changé partent : la carte garde un manifeste
(taille et empreinte de chaque fichier reçu). Le jeu se met en pause pendant la copie, puis la carte redémarre et met
les nouveaux fichiers en place ; l'outil attend son retour et vérifie le manifeste.
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import time
import zlib

import serial
import serial.tools.list_ports

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # engine/
FICHIERS = ['monde.pak', 'interieurs.bin', 'histoire.scn', 'musique.bin']
LEGER = 'monde_esp32.pak'   # paquet léger de la flash (partition « monde »)
PARTITIONS = os.path.join(RACINE, 'platform', 'esp32', 'partitions.csv')
BLOC = 32 * 1024
ANSI = re.compile(r'\x1b\[[0-9;]*m')


def trouver_port():
    """Port série de l'USB natif de l'ESP32-S3 (Espressif, 303A)"""
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x303A:
            return p.device
    sys.exit("pas d'ESP32-S3 branché en USB (Espressif 303A) : --port COMx")


def ouvrir(port, attente=30):
    """Ouvre le port sans toucher à DTR / RTS (sinon la carte redémarre) ; réessaie le temps qu'elle revienne"""
    fin = time.time() + attente
    while True:
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 0.05
            s.dtr = s.rts = False
            s.open()
            return s
        except serial.SerialException:
            if time.time() > fin:
                raise
            time.sleep(0.5)


def lancer_echoes(port):
    """--echoes : démarrage sur Echoes (l'app montre ne lit pas le protocole : l'envoi resterait bloqué sur l'écriture). Juste
    avant d'ouvrir le lien : un Echoes qui ne peut pas démarrer n'attend la copie que 10 s (echec_demarrage de main.c)"""
    import subprocess
    espressif = os.path.join(os.environ.get('ECHOES_IDF', r'D:\Documents\Tuxdroid\esp32-dos-project'))
    idf = os.environ.get('IDF_PATH') or os.path.join(espressif, 'esp-idf')
    py = os.path.join(espressif, '.espressif', 'python_env', 'idf5.2_py3.10_env', 'Scripts', 'python.exe')
    subprocess.run([py, os.path.join(idf, 'components', 'app_update', 'otatool.py'), '-p', port, 'switch_ota_partition', '--name', 'echoes'],
                   env=dict(os.environ, IDF_PATH=idf), check=True, stdout=subprocess.DEVNULL)
    print('Echoes lancé sur la montre', flush=True)


class Lien:
    journal = False   # -v : lignes du journal de la carte affichées

    def __init__(self, s):
        self.s, self.reste = s, b''

    def envoyer(self, texte):
        self.s.write(texte.encode('ascii') + b'\n')

    def ligne(self, delai=15):
        """Prochaine ligne « SDX ... » (le journal du jeu passe par le même port : ignoré)"""
        fin = time.time() + delai
        while True:
            while b'\n' in self.reste:
                l, self.reste = self.reste.split(b'\n', 1)
                l = ANSI.sub('', l.decode('utf-8', 'replace')).strip()
                k = l.find('SDX')   # au démarrage, une réponse peut se coller au bout d'une ligne du journal
                if k >= 0:
                    return l[k:]
                if self.journal and l:
                    print('\n    carte : ' + l, flush=True)
            if time.time() > fin:
                return None
            # ce qui est arrivé, sinon un octet (read(4096) attendait la fin du délai pour une réponse de 9 octets)
            self.reste += self.s.read(max(1, self.s.in_waiting))

    def bonjour(self, attente=40):
        """SDX? jusqu'à la réponse : (dossier, Mo libres, manifeste {nom: (taille, empreinte)})"""
        fin = time.time() + attente
        while time.time() < fin:
            self.envoyer('SDX?')
            l = self.ligne(1.5)
            if l == 'SDX SANS-CARTE':
                self.regler_heure()   # l'horloge se règle sans carte (1,75" C sans lecteur)
                sys.exit('la carte ne voit pas de carte SD (insérée ? formatée en FAT32 ?)')
            if l and l.startswith('SDX OK '):
                _, _, dossier, libre = l.split()
                man = {}
                while True:
                    m = self.ligne(5)
                    if m is None or m == 'SDX .':
                        break
                    if m.startswith('SDX M '):
                        _, _, nom, taille, emp = m.split()
                        man[nom] = (int(taille), emp)
                # Les SDX? envoyés pendant que la carte démarrait ont chacun leur réponse : ces doublons sont lus ici
                # (avant le 03/11/2026, une ligne « SDX M » en trop passait pour la réponse au SDX PUT qui suivait)
                while self.ligne(0.5) is not None:
                    pass
                return dossier, int(libre), man
        sys.exit("la carte ne répond pas (firmware sans la copie par USB ? idf.cmd -p COMx flash)")

    def regler_heure(self):
        """Horloge de la montre (PCF85063) à l'heure locale du PC ; un ancien firmware ne répond pas : rien de grave"""
        t = time.localtime()
        self.envoyer(f'SDX HEURE {t.tm_year} {t.tm_mon} {t.tm_mday} {t.tm_hour} {t.tm_min} {t.tm_sec}')
        r = self.ligne(3)
        if r == 'SDX HEURE OK':
            print(f'horloge réglée à {time.strftime("%d/%m/%Y %H:%M:%S", t)}')
        else:
            print(f'horloge non réglée ({r or "pas de réponse : firmware sans SDX HEURE"})')


def flasher(port, chemin):
    """Paquet léger écrit dans la partition « monde » (adresse de partitions.csv) par esptool, puis redémarrage"""
    import subprocess
    adresse = None
    for l in open(PARTITIONS, encoding='utf-8'):
        c = [x.strip() for x in l.split(',')]
        if len(c) > 3 and c[0] == 'monde':
            adresse = c[3]
    if not adresse:
        sys.exit('partition « monde » introuvable dans ' + PARTITIONS)
    print(f'flash de {os.path.basename(chemin)} à {adresse}…')
    r = subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '-p', port, '--after', 'hard-reset',
                        'write-flash', adresse, chemin])
    if r.returncode:
        sys.exit('flash du paquet léger impossible')


EMPREINTES = {}


def empreinte(chemin):
    """Empreinte d'un fichier, calculée une fois (main la calcule avant d'ouvrir le lien, voir plus bas)"""
    if chemin not in EMPREINTES:
        EMPREINTES[chemin] = calculer_empreinte(chemin)
    return EMPREINTES[chemin]


def calculer_empreinte(chemin):
    h = hashlib.sha1()
    with open(chemin, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()[:16]


def echec(lien, texte):
    """Copie abandonnée : la carte efface ce qu'elle a reçu et le jeu reprend"""
    lien.envoyer('SDX ANNULE')
    lien.ligne(5)
    sys.exit(texte)


def envoyer_fichier(lien, chemin, nom, taille, emp):
    lien.envoyer(f'SDX PUT {nom} {taille} {emp}')
    r = lien.ligne(20)
    while r and (r.startswith(('SDX M ', 'SDX OK ')) or r == 'SDX .'):   # réponse en retard à un SDX? du bonjour
        r = lien.ligne(20)
    if r != 'SDX PRET':
        echec(lien, f'{nom} : la carte refuse ({r})')
    t0 = t_aff = time.time()
    envoye, t_usb, t_ack = 0, 0.0, 0.0
    with open(chemin, 'rb') as f:
        while envoye < taille:
            d = f.read(BLOC)
            paquet = struct.pack('<I', len(d)) + d + struct.pack('<I', zlib.crc32(d) & 0xFFFFFFFF)
            for essai in range(6):
                t1 = time.time()
                lien.s.write(paquet)
                t2 = time.time()
                r = lien.ligne(20)
                t_usb += t2 - t1
                t_ack += time.time() - t2
                if r == 'SDX B OK':
                    break
                if r != 'SDX B ERR':
                    echec(lien, f'\n{nom} : copie interrompue à {envoye / 1048576:.1f} Mo ({r or "plus de réponse"})')
            else:
                echec(lien, f'\n{nom} : bloc refusé 6 fois')
            envoye += len(d)
            if time.time() - t_aff > 0.5 or envoye == taille:
                t_aff = time.time()
                v = envoye / max(1e-3, t_aff - t0) / 1048576
                reste = (taille - envoye) / 1048576 / max(v, 1e-3)
                print(f'\r  {nom} : {envoye * 100 // taille:3d} %  {envoye / 1048576:7.1f} / {taille / 1048576:.1f} Mo'
                      f'  {v:4.2f} Mo/s  reste {int(reste) // 60} min {int(reste) % 60:02d} s ', end='', flush=True)
    r = lien.ligne(30)
    if r != f'SDX ECRIT {nom}':
        echec(lien, f'\n{nom} : fin de copie non confirmée ({r})')
    print(f'\n  {nom} écrit en {time.time() - t0:.0f} s (envoi USB {t_usb:.0f} s, attente de la carte {t_ack:.0f} s)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fichiers', nargs='*', help='noms dans le dossier (par défaut : ' + ', '.join(FICHIERS) + ')')
    ap.add_argument('--port', help='port série (par défaut : le premier ESP32-S3 en USB)')
    ap.add_argument('--dossier', help='dossier des fichiers (par défaut : engine/data, ou engine/data/enfant)')
    ap.add_argument('--force', action='store_true', help='envoyer même les fichiers que la carte a déjà')
    ap.add_argument('-v', action='store_true', help='afficher le journal de la carte et les temps')
    ap.add_argument('--flash', action='store_true', help='flasher le paquet léger même avec une liste de fichiers')
    ap.add_argument('--sans-flash', action='store_true', help='ne pas flasher le paquet léger')
    ap.add_argument('--echoes', action='store_true', help="lancer Echoes sur la montre, une fois les empreintes calculées (l'app montre ne lit pas le protocole)")
    ap.add_argument('--heure', action='store_true', help="régler l'horloge de la montre seulement (elle l'est aussi à chaque envoi)")
    ap.add_argument('--wifi', nargs=2, metavar=('RESEAU', 'MOT_DE_PASSE'), help='réseau Wi-Fi du mode Transfert, gardé dans la montre')
    ap.add_argument('--transfert', action='store_true', help='redémarrer la montre en mode Transfert (Wi-Fi : heure et fichiers)')
    ap.add_argument('--pcdos', action='store_true', help='fichiers du PC DOS (tiny386) vers /pcdos de la carte : par défaut '
                    'tout pcdos/carte/pcdos (tiny386.ini), sinon les fichiers donnés (chemins complets acceptés)')
    a = ap.parse_args()
    Lien.journal = a.v

    port = a.port or trouver_port()
    # Empreintes des fichiers du dossier par défaut calculées avant d'ouvrir le lien (03/11/2026) : un Echoes qui ne
    # peut pas démarrer (paquet léger flashé avant la copie du monde) n'attend la copie que 10 s (echec_demarrage de
    # main.c) ; les empreintes de 420 Mo calculées après la réponse de la carte les dépassaient
    if not (a.wifi or a.transfert or a.heure or a.pcdos):
        avance = a.dossier or os.path.join(RACINE, 'data')
        for nom in (a.fichiers or FICHIERS) + [LEGER]:
            chemin = nom if os.path.isabs(nom) else os.path.join(avance, nom)
            if os.path.isfile(chemin):
                empreinte(chemin)
    if a.echoes:
        lancer_echoes(port)
    lien = Lien(ouvrir(port))
    if a.wifi or a.transfert:   # sans la carte SD : réglages du mode Transfert
        if a.wifi:
            lien.envoyer(f'SDX WIFI {a.wifi[0]}\t{a.wifi[1]}')
            r = lien.ligne(5)
            print(f'réseau Wi-Fi « {a.wifi[0]} » gardé dans la montre' if r == 'SDX WIFI OK' else f'réseau non gardé ({r or "pas de réponse"})')
        if a.transfert:
            lien.envoyer('SDX TRANSFERT')
            r = lien.ligne(5)
            print('la montre redémarre en mode Transfert' if r == 'SDX TRANSFERT OK' else f'pas de mode Transfert ({r or "pas de réponse"})')
        return
    dossier_sd, libre, man = lien.bonjour()
    lien.regler_heure()
    if a.heure:
        return
    enfant = dossier_sd.endswith('_enfant')
    if a.pcdos:   # dossier du PC DOS à la racine de la carte (noms « pcdos/… » : sdx.c), jamais le paquet léger
        src = a.dossier or os.path.join(RACINE, '..', 'pcdos', 'carte', 'pcdos')
        liste, a.sans_flash = a.fichiers or sorted(n for n in os.listdir(src) if os.path.isfile(os.path.join(src, n))), True
    else:
        src = a.dossier or os.path.join(RACINE, 'data', 'enfant' if enfant else '')
        liste = a.fichiers or FICHIERS
    print(f'{port} : carte SD {dossier_sd}, {libre} Mo libres ; fichiers de {os.path.normpath(src)}')

    envois = []
    for nom in liste:
        chemin = nom if os.path.isabs(nom) else os.path.join(src, nom)
        distant = 'pcdos/' + os.path.basename(chemin) if a.pcdos else nom
        if not os.path.isfile(chemin):
            if a.fichiers:
                sys.exit(f'{chemin} : introuvable')
            continue
        taille, emp = os.path.getsize(chemin), empreinte(chemin)
        if not a.force and man.get(distant) == (taille, emp):
            print(f'  {distant} : déjà à jour')
            continue
        envois.append((chemin, distant, taille, emp))
    # Paquet léger de la flash (monde_esp32.pak) : du même export que monde.pak, sinon le jeu ne démarre pas ; flashé
    # s'il diffère de celui que le manifeste a noté (« flash:monde_esp32.pak »)
    leger, flash = os.path.join(src, LEGER), None
    if not a.sans_flash and (not a.fichiers or a.flash) and os.path.isfile(leger):
        t, e = os.path.getsize(leger), empreinte(leger)
        if a.force or a.flash or man.get('flash:' + LEGER) != (t, e):
            flash = (t, e)
        else:
            print(f'  {LEGER} (flash) : déjà à jour')
    if not envois and not flash:
        print('rien à envoyer')
        return
    if envois:
        total = sum(e[2] for e in envois)
        # Les nouveaux fichiers sont écrits à côté des anciens avant de les remplacer
        if total / 1048576 > libre - 16:
            sys.exit(f'place insuffisante sur la carte : {total / 1048576:.0f} Mo à écrire, {libre} Mo libres')
        print(f'{len(envois)} fichier(s), {total / 1048576:.1f} Mo ; le jeu se met en pause pendant la copie')
        for e in envois:
            envoyer_fichier(lien, *e)
        lien.envoyer('SDX FIN')
        r = lien.ligne(20)
        if r != 'SDX REDEMARRAGE':
            sys.exit(f'fin non confirmée ({r})')
        print('la carte redémarre et met les fichiers en place…')
        lien.s.close()
        time.sleep(3)
        lien = Lien(ouvrir(port, 60))
        _, _, man = lien.bonjour(60)
        faux = [n for _, n, t, e in envois if man.get(n) != (t, e)]
        if faux:
            sys.exit('pas en place après le redémarrage : ' + ', '.join(faux))
        print('fait : ' + ', '.join(n for _, n, _, _ in envois) + ' en place sur la carte')
    if flash:
        lien.s.close()
        flasher(port, leger)
        time.sleep(3)
        lien = Lien(ouvrir(port, 60))
        lien.bonjour(60)
        lien.envoyer(f'SDX NOTE flash:{LEGER} {flash[0]} {flash[1]}')
        if lien.ligne(10) != 'SDX NOTE OK':
            sys.exit(f'{LEGER} flashé, mais pas noté sur la carte')
        print(f'fait : {LEGER} flashé')

if __name__ == '__main__':
    main()
