// Compile le moteur pour RetroFW 2.3 (11/11/2026 : LDK Game ; aussi les autres consoles Ingenic JZ4760 de RetroFW) :
// binaire MIPS32 statique (Zig, musl, flottants matériels : la JZ4760 a une FPU, RetroFW est compilé en -mips32
// hard-float), en écran rectangulaire (ENG_RECT : 3D 320 × 240 en plein écran, interface de la montre au milieu, voir
// core/eng.h et platform/retrofw/main_retrofw.c), puis le dossier à copier sur la carte de la console et le paquet IPK
// que GMenuNX installe.
//
//   node tools/build_retrofw.js [--enfant | --ile] [--sans-ipk] [--simu]
//     --enfant    édition enfant (ECHOES_ENFANT, data/enfant) : dossier games/choeur_enfant, paquet à part
//     --ile       l'Île de Nacre du jeu des héroïnes (11/11/2026, demande de l'utilisateur ; CHOEUR_ILE, data/ile de
//                 tools/maj_monde.js --ile) : le même programme, dossier games/ile, ile.dge, lien ile.lnk, sauvegardes à part
//     --sans-ipk  sans le paquet IPK (le dossier carte_sd suffit, copié par l'USB)
//     --simu      aussi le simulateur pour PC (build/retrofw/choeur_simu.exe : même programme, écran en BMP, boutons
//                 d'un script ; voir l'en-tête de platform/retrofw/main_retrofw.c)
//   -> build/retrofw/choeur.dge (le programme), build/retrofw/carte_sd/ (à copier à la racine de la carte de la console :
//      games/choeur/, .gmenunx/sections/games/choeur.lnk ; fichiers du monde en liens physiques vers data/),
//      build/retrofw/choeur.ipk (tout ensemble, à ouvrir depuis GMenuNX)
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const root = path.join(__dirname, '..');
const python = path.join(root, '..', 'tools', '.venv', 'Scripts', 'python.exe');
const args = process.argv.slice(2);
const enfant = args.includes('--enfant'), ile = !enfant && args.includes('--ile'), ipk = !args.includes('--sans-ipk'), simu = args.includes('--simu');
const suf = enfant ? '_enfant' : '';
const nom = ile ? 'ile' : 'choeur' + suf;   // dossier games/<nom>, programme <nom>.dge, lien <nom>.lnk
const donnees = path.join(root, 'data', ...(enfant ? ['enfant'] : ile ? ['ile'] : []));
const out = path.join(root, 'build', 'retrofw');
fs.mkdirSync(out, { recursive: true });
// Icône de l'île (32 × 32) : lagon, plage, volcan vert, cocotier
const ICONE_ILE = [
    'import sys',
    'from PIL import Image, ImageDraw',
    'k = 8; S = 32 * k',
    'im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)',
    'd.ellipse((0, 0, S - 1, S - 1), fill=(10, 79, 150, 255))',
    'd.ellipse((2 * k, 9 * k, 30 * k, 31 * k), fill=(31, 208, 214, 255))',
    'd.ellipse((5 * k, 15 * k, 27 * k, 27 * k), fill=(240, 222, 170, 255))',
    'd.polygon([(8 * k, 22 * k), (15 * k, 7 * k), (18 * k, 7 * k), (25 * k, 22 * k)], fill=(46, 150, 70, 255))',
    'd.polygon([(14 * k, 9 * k), (15 * k, 7 * k), (18 * k, 7 * k), (19 * k, 9 * k)], fill=(120, 90, 60, 255))',
    'd.line([(22 * k, 25 * k), (23 * k, 17 * k), (25 * k, 13 * k)], fill=(120, 80, 40, 255), width=k)',
    'for a, b in [((25, 13), (19, 12)), ((25, 13), (30, 11)), ((25, 13), (21, 17)), ((25, 13), (29, 17))]:',
    '    d.line([(a[0] * k, a[1] * k), (b[0] * k, b[1] * k)], fill=(30, 120, 50, 255), width=k)',
    'im.resize((32, 32), Image.LANCZOS).save(sys.argv[1])',
].join('\n');

const src = ['core/world.c', 'core/game.c', 'core/render.c', 'core/ui.c', 'core/hud.c', 'core/battle.c', 'core/script.c', 'core/audio.c', 'core/course.c', 'core/music.c', 'core/interieur.c', 'core/accueil.c', 'core/police.c', 'core/coffres.c', 'core/pilote.c', 'core/carte.c', 'core/carriere.c', 'core/garage.c', 'core/panneaux.c', 'core/repliques.c', 'core/arene.c', 'core/ile.c', 'platform/retrofw/main_retrofw.c', 'platform/retrofw/banc.c'].map(f => path.join(root, f));
const stamp = (d => d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0') + ' ' + String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0'))(new Date());
const defs = ['-DENG_RECT=1', '-DENG_ROUND=0', '-DENG_W=320', '-DENG_H_PX=240', '-DENG_WORKERS=1', '-DCHOEUR_VERSION="' + stamp + '"', ...(enfant ? ['-DECHOES_ENFANT=1'] : []), ...(ile ? ['-DCHOEUR_ILE=1'] : [])];   // (version : en tête de journal.txt)
const zig = (target, sortie, plus) => execFileSync(python, ['-m', 'ziglang', 'cc', '-target', target, ...plus, '-std=c99', '-O2', '-fno-strict-aliasing',
    '-Wall', '-Wno-unused-parameter', ...defs, '-o', sortie, ...src], { stdio: 'inherit' });

// 1. Le programme : MIPS32 (révision 1, comme RetroFW), flottants matériels, musl lié en statique (aucune bibliothèque de
//    la console : ni uClibc, ni SDL)
//    (avec ses symboles et ses lignes, pour lire profil.bin : tools/profil_retrofw.py ; copie dans <nom>.elf, la même que
//    sur la console : l'objcopy de Zig ne dépouille pas le MIPS, et la console ignore les sections de débogage)
const dge = path.join(out, nom + '.dge'), elf = path.join(out, nom + '.elf');
zig('mipsel-linux-musleabihf', elf, ['-mcpu=mips32', '-static', '-g']);
fs.copyFileSync(elf, dge);
console.log(path.relative(root, dge) + ' ' + Math.round(fs.statSync(dge).size / 1024) + ' Ko');
if (simu) {
    const exe = path.join(out, nom + '_simu.exe');
    zig('x86_64-windows-gnu', exe, ['-DRETROFW_SIMU']);
    console.log(path.relative(root, exe));
}

// 2. Les fichiers du monde (ceux de la carte SD de la montre) ; le paquet léger de la flash devient monde_leger.pak
const MONDE = [['monde_leger.pak', 'monde_esp32.pak'], ['monde.pak', 'monde.pak'], ['histoire.scn', 'histoire.scn'], ['interieurs.bin', 'interieurs.bin']];
const fichiers = [];
for (const [dst, s] of MONDE) {
    const f = path.join(donnees, s);
    if (!fs.existsSync(f)) { console.error('manque ' + f + ' (node tools/maj_monde.js' + (enfant ? ' --enfant' : ile ? ' --ile' : '') + ')'); process.exit(1); }
    fichiers.push([dst, f]);
}
{   // paquet léger et monde.pak du même export (le moteur compare la taille attendue : SDPK)
    const dl = fs.statSync(path.join(donnees, 'monde_esp32.pak')).mtimeMs, dm = fs.statSync(path.join(donnees, 'monde.pak')).mtimeMs;
    if (Math.abs(dl - dm) > 30 * 60 * 1000) console.warn('attention : monde_esp32.pak et monde.pak n\'ont pas été faits ensemble (plus de 30 min d\'écart)');
}
const musique = [path.join(donnees, 'musique.bin'), path.join(root, 'data', 'musique.bin')].find(f => fs.existsSync(f));
if (musique) fichiers.push(['musique.bin', musique]); else console.warn('pas de musique.bin : jeu sans musique');

// 3. Icône (32 × 32, celle de l'app Android), mode d'emploi (bouton Y de GMenuNX), lien du menu
const icone = path.join(out, nom + '.png');
if (ile) execFileSync(python, ['-I', '-c', ICONE_ILE, icone]);   // (l'île n'a pas d'icône à elle : dessinée ici)
else execFileSync(python, ['-I', '-c', 'import sys; from PIL import Image; Image.open(sys.argv[1]).convert("RGBA").resize((32, 32), Image.LANCZOS).save(sys.argv[2])',
    path.join(root, 'platform', 'android', 'res', 'mipmap-xxhdpi', 'ic_launcher.png'), icone]);
const titre = ile ? 'Ile de Nacre' : 'Le Choeur des Cendres' + (enfant ? ' (enfant)' : '');
const dossierJeu = '/home/retrofw/games/' + nom;
const lien = [
    'title=' + titre,
    'description=' + (ile ? 'Jeu des héroïnes : l\'île tropicale' : 'RPG en 3D façon PS1' + (enfant ? ', édition enfant' : '')),
    'exec=' + dossierJeu + '/' + nom + '.dge',
    'icon=' + dossierJeu + '/' + nom + '.png',
    'manual=' + dossierJeu + '/' + nom + '.man.txt',
    'clock=' + (enfant ? 600 : 654),   // MHz (528 par défaut, 600 au plus selon le wiki de RetroFW ; 652 : la plus haute fréquence stable de la LDK de l'utilisateur, 11/11/2026, réglée à 654 dans GMenuNX : la PLL avance par 6 MHz ; édition enfant, celle qu'on publie : 600, une autre console peut ne pas tenir 654)
    ''].join('\n');
// Banc d'essai (même programme, params=banc) : trois scènes de 15 s, banc.txt et profil_banc.bin, aucune sauvegarde
const lienBanc = lien.replace('title=' + titre, 'title=' + (enfant ? 'Banc (enfant)' : ile ? "Ile : banc d'essai" : "Choeur : banc d'essai")).replace('description=', 'description=Mesure 45 s, puis retour. ') + 'params=banc\n';
const manuel = [
    ile ? 'L\'ÎLE DE NACRE' : 'LE CHŒUR DES CENDRES' + (enfant ? ' (édition enfant)' : ''),
    '',
    'Commandes',
    '  Croix          se déplacer (en courant) ; choisir dans les menus',
    '  Y tenu         au pas ; en véhicule, gaz à moitié ; vaisseau : descendre',
    '  X              vaisseau : monter',
    '  A              OK : parler, valider, monter dans un véhicule',
    '  B              RETOUR ; hors des menus : ouvrir le menu',
    '  START          menu (pause en course)',
    '  L / R          tourner la caméra ; carte du menu : zoom',
    '  En véhicule    R accélère, L freine et recule, la croix braque',
    '                 (menu SYSTÈME > CONDUITE LDK : GÂCHETTES ou CROIX)',
    '  SELECT + R     images par seconde (en haut à gauche)',
    '  SELECT + L     échanger rouge et bleu (si les couleurs sont fausses)',
    '  SELECT + START tenus 1 s : quitter (partie sauvegardée si possible)',
    '',
    'Aussi : menu SYSTÈME > VERS RETROFW : QUITTER.',
    'Sauvegardes : /home/retrofw/' + (ile ? '.ile' : enfant ? '.choeur_enfant' : '.choeur') + ' (et journal.txt en cas de problème).',
    ''].join('\n');

// 4. Dossier à copier à la racine de la carte (partition de RetroFW vue par l'USB : /home/retrofw)
const carte = path.join(out, 'carte_sd');
const jeu = path.join(carte, 'games', nom), sections = path.join(carte, '.gmenunx', 'sections', 'games');
fs.mkdirSync(jeu, { recursive: true }); fs.mkdirSync(sections, { recursive: true });
const lier = (a, b) => {   // lien physique (même disque : rien de recopié), sinon copie
    try { fs.unlinkSync(b); } catch (e) { /* absent */ }
    try { fs.linkSync(a, b); } catch (e) { fs.copyFileSync(a, b); }
};
fs.copyFileSync(dge, path.join(jeu, nom + '.dge'));
fs.copyFileSync(icone, path.join(jeu, nom + '.png'));
fs.writeFileSync(path.join(jeu, nom + '.man.txt'), manuel);
fs.writeFileSync(path.join(sections, nom + '.lnk'), lien);
if (!enfant) fs.writeFileSync(path.join(sections, nom + '_banc.lnk'), lienBanc);   // banc d'essai : trois scènes mesurées, puis retour (main_retrofw.c) ; pas dans l'édition publiée
for (const [dst, f] of fichiers) lier(f, path.join(jeu, dst));
const total = fichiers.reduce((s, [, f]) => s + fs.statSync(f).size, 0);
console.log(path.relative(root, carte) + ' : games/' + nom + ' (' + Math.round(total / 1048576) + ' Mo de monde), .gmenunx/sections/games/' + nom + '.lnk');

// 5. Paquet IPK (opkg : archive ar de debian-binary, control.tar.gz, data.tar.gz ; chemins depuis la racine, sous
//    /home/retrofw, le reste du système étant en lecture seule)
if (!ipk) process.exit(0);
const maintenant = Math.floor(Date.now() / 1000);
const d = new Date(), version = String(d.getFullYear()) + String(d.getMonth() + 1).padStart(2, '0') + String(d.getDate()).padStart(2, '0');
function enTete(nomTar, taille, mode, type) {   // en-tête ustar de 512 octets
    const h = Buffer.alloc(512);
    if (Buffer.byteLength(nomTar) > 99) throw new Error('nom trop long : ' + nomTar);
    h.write(nomTar, 0);
    const oct = (v, n) => v.toString(8).padStart(n - 1, '0') + '\0';
    h.write(oct(mode, 8), 100); h.write(oct(0, 8), 108); h.write(oct(0, 8), 116);
    h.write(oct(taille, 12), 124); h.write(oct(maintenant, 12), 136);
    h.write('        ', 148); h.write(type, 156);
    h.write('ustar\u000000', 257, 'latin1'); h.write('root', 265); h.write('root', 297);
    let somme = 0;
    for (const b of h) somme += b;
    h.write(somme.toString(8).padStart(6, '0') + '\0 ', 148, 'latin1');
    return h;
}
async function tarGz(sortie, entrees) {   // entrees : [nom, { fichier | contenu | dossier, mode }] ; écrit en flux (le monde fait des centaines de Mo)
    const gz = zlib.createGzip({ level: 1 }), o = fs.createWriteStream(sortie);
    gz.pipe(o);
    const ecrire = b => new Promise(r => (gz.write(b) ? r() : gz.once('drain', r)));
    const morceau = Buffer.alloc(4 << 20);
    for (const [n, e] of entrees) {
        if (e.dossier) { await ecrire(enTete(n, 0, 0o755, '5')); continue; }
        const taille = e.fichier ? fs.statSync(e.fichier).size : e.contenu.length;
        await ecrire(enTete(n, taille, e.mode || 0o644, '0'));
        if (e.fichier) {
            const fd = fs.openSync(e.fichier, 'r');
            for (let pos = 0; pos < taille;) { const l = fs.readSync(fd, morceau, 0, morceau.length, pos); await ecrire(Buffer.from(morceau.subarray(0, l))); pos += l; }
            fs.closeSync(fd);
        } else await ecrire(e.contenu);
        if (taille % 512) await ecrire(Buffer.alloc(512 - taille % 512));
    }
    await ecrire(Buffer.alloc(1024));
    await new Promise(r => { o.on('close', r); gz.end(); });
}
function ar(sortie, membres) {   // membres : [nom, fichier]
    const fd = fs.openSync(sortie, 'w'), morceau = Buffer.alloc(4 << 20);
    fs.writeSync(fd, '!<arch>\n');
    for (const [n, f] of membres) {
        const taille = fs.statSync(f).size;
        fs.writeSync(fd, Buffer.from(n.padEnd(16) + String(maintenant).padEnd(12) + '0'.padEnd(6) + '0'.padEnd(6) + '100644'.padEnd(8) + String(taille).padEnd(10) + '`\n', 'latin1'));
        const fi = fs.openSync(f, 'r');
        for (let pos = 0; pos < taille;) { const l = fs.readSync(fi, morceau, 0, morceau.length, pos); fs.writeSync(fd, morceau, 0, l); pos += l; }
        fs.closeSync(fi);
        if (taille % 2) fs.writeSync(fd, '\n');
    }
    fs.closeSync(fd);
}
(async () => {
    const tmp = path.join(out, 'ipk_' + nom);
    fs.mkdirSync(tmp, { recursive: true });
    const control = [
        'Package: ' + (ile ? 'ile-de-nacre' : 'choeur-des-cendres' + (enfant ? '-enfant' : '')),
        'Version: ' + version,
        'Description: ' + titre + (ile ? ', jeu des heroines' : ', RPG en 3D facon PS1'),
        'Section: games',
        'Priority: optional',
        'Maintainer: Nerobeus',
        'Architecture: mipsel',
        ...(ile ? [] : ['Homepage: https://github.com/Nerobeus/echoes-of-ashes', 'Source: https://github.com/Nerobeus/echoes-of-ashes']),   // (l'île n'est pas dans le dépôt public)
        ''].join('\n');
    const lnk = '/home/retrofw/.gmenunx/sections/games/' + nom + '.lnk';
    await tarGz(path.join(tmp, 'control.tar.gz'), [['./', { dossier: 1 }], ['./control', { contenu: Buffer.from(control) }], ['./conffiles', { contenu: Buffer.from(lnk + '\n') }]]);
    const g = './home/retrofw/games/' + nom + '/', s = './home/retrofw/.gmenunx/sections/games/';
    const entrees = ['./', './home/', './home/retrofw/', './home/retrofw/games/', g, './home/retrofw/.gmenunx/', './home/retrofw/.gmenunx/sections/', s].map(n => [n, { dossier: 1 }]);
    entrees.push([g + nom + '.dge', { fichier: dge, mode: 0o755 }], [g + nom + '.png', { fichier: icone }], [g + nom + '.man.txt', { contenu: Buffer.from(manuel) }],
        [s + nom + '.lnk', { contenu: Buffer.from(lien) }], ...(enfant ? [] : [[s + nom + '_banc.lnk', { contenu: Buffer.from(lienBanc) }]]), ...fichiers.map(([n, f]) => [g + n, { fichier: f }]));
    await tarGz(path.join(tmp, 'data.tar.gz'), entrees);
    fs.writeFileSync(path.join(tmp, 'debian-binary'), '2.0\n');
    const paquet = path.join(out, nom + '.ipk');
    ar(paquet, [['debian-binary', path.join(tmp, 'debian-binary')], ['control.tar.gz', path.join(tmp, 'control.tar.gz')], ['data.tar.gz', path.join(tmp, 'data.tar.gz')]]);
    fs.rmSync(tmp, { recursive: true, force: true });
    console.log(path.relative(root, paquet) + ' ' + Math.round(fs.statSync(paquet).size / 1048576) + ' Mo (version ' + version + ')');
})();
