// Compile le cœur en WebAssembly (Zig, cible wasm32-wasi en mode « reactor »), en deux variantes : 3D en
// 233 × 233 doublée et 3D native 466 × 466. Produit build/moteur.html, une page autonome qui intègre les deux
// modules et le paquet.
//
//   node tools/build_web.js [--enfant] [--pc] [data/monde.pak] [build/moteur.html]
//     --enfant  édition enfant (ECHOES_ENFANT : seul un paquet de l'édition enfant se lance) : data/enfant/monde.pak
//               -> build/moteur_enfant.html, sauvegardes à part dans le stockage du navigateur
//     --pc      version autonome pour PC (platform/web/pc.template.html) : jeu seul, sans la montre simulée, 3D native,
//               joystick de conduite au départ, monde et musiques compressés en gzip dans la page (un seul fichier, ouvert
//               d'un double-clic, sans serveur) -> build/choeur_pc.html (build/choeur_pc_enfant.html avec --enfant)
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const root = path.join(__dirname, '..');
const python = path.join(root, '..', 'tools', '.venv', 'Scripts', 'python.exe');
const enfant = process.argv.includes('--enfant'), pc = process.argv.includes('--pc'), pos = process.argv.slice(2).filter(a => !a.startsWith('--'));
const pak = path.resolve(pos[0] || path.join(root, 'data', ...(enfant ? ['enfant'] : []), 'monde.pak'));
const out = path.join(root, 'build');
fs.mkdirSync(out, { recursive: true });

const src = ['core/world.c', 'core/game.c', 'core/render.c', 'core/ui.c', 'core/hud.c', 'core/battle.c', 'core/script.c', 'core/audio.c', 'core/course.c', 'core/music.c', 'core/interieur.c', 'core/accueil.c', 'core/police.c', 'core/coffres.c', 'core/pilote.c', 'core/carte.c', 'core/carriere.c', 'core/garage.c', 'core/panneaux.c', 'core/repliques.c', 'core/arene.c', 'platform/web/main_web.c'].map(f => path.join(root, f));
const ed = enfant ? ['-DECHOES_ENFANT=1'] : [], suf = enfant ? '_enfant' : '';
// Page écrite par morceaux : le modèle, et chaque binaire à la place de sa marque, en base64 par tranches de 3 Mo (un
// multiple de 3 octets : les tranches bout à bout font le même base64). Avant, la page entière était une seule chaîne de
// deux octets par caractère (le modèle a des « œ ») : 3 Go pour le monde de 1 024 (02/11/2026)
function ecrirePage(sortie, modele, binaires) {
    const fd = fs.openSync(sortie, 'w');
    let taille = 0, i0 = 0;
    const ecrire = s => { taille += fs.writeSync(fd, s); };
    const places = binaires.map(([marque, buf]) => [modele.indexOf(marque), marque, buf]).filter(p => p[0] >= 0).sort((a, b) => a[0] - b[0]);
    for (const [i, marque, buf] of places) {
        ecrire(modele.slice(i0, i));
        for (let o = 0; o < buf.length; o += 3 << 20) ecrire(buf.subarray(o, o + (3 << 20)).toString('base64'));
        i0 = i + marque.length;
    }
    ecrire(modele.slice(i0));
    fs.closeSync(fd);
    return taille;
}
const build = (name, defs) => {
    const wasm = path.join(out, name);
    execFileSync(python, ['-m', 'ziglang', 'cc', '-target', 'wasm32-wasi', '-mexec-model=reactor', '-std=c99', '-O2',
        '-fno-strict-aliasing', '-Wall', '-Wno-unused-parameter', '-Wl,--strip-all', ...ed, ...defs, '-o', wasm, ...src], { stdio: 'inherit' });
    return wasm;
};
if (pc) {   // version autonome pour PC : un seul module (3D native), paquet et musiques en gzip dans la page
    const wasm = build('choeur_pc' + suf + '.wasm', ['-DENG_W=466', '-DENG_H_PX=466']);
    let page = fs.readFileSync(path.join(root, 'platform', 'web', 'pc.template.html'), 'utf8');
    if (enfant) page = page.split("'echoes-pc-sauve-'").join("'echoes-pc-enfant-sauve-'");
    const mus = [path.join(root, 'data', ...(enfant ? ['enfant'] : []), 'musique.bin'), path.join(root, 'data', 'musique.bin')].find(f => fs.existsSync(f));
    const gz = f => zlib.gzipSync(fs.readFileSync(f), { level: 9 });
    const sortie = path.resolve(pos[1] || path.join(out, 'choeur_pc' + suf + '.html'));
    const taille = ecrirePage(sortie, page, [['@@WASM@@', fs.readFileSync(wasm)], ['@@PAK_GZ@@', gz(pak)], ['@@MUSIQUE_GZ@@', mus ? gz(mus) : Buffer.alloc(0)]]);
    console.log(path.basename(sortie) + ' ' + Math.round(taille / 1048576) + ' Mo (' + path.basename(pak) + (mus ? ', musiques' : ', sans musique') + ')' + (enfant ? ' (édition enfant)' : ''));
    process.exit(0);
}
const x2 = build('moteur' + suf + '_x2.wasm', []);
const natif = build('moteur' + suf + '_natif.wasm', ['-DENG_W=466', '-DENG_H_PX=466']);

let html = fs.readFileSync(path.join(root, 'platform', 'web', 'moteur.template.html'), 'utf8');
// Édition enfant : ses sauvegardes ne se mêlent pas à celles de l'édition 18+ (même navigateur, même origine)
if (enfant) html = html.split('\'echoes-sauve-\'').join('\'echoes-enfant-sauve-\'');
const page = path.resolve(pos[1] || path.join(out, 'moteur' + suf + '.html'));
const taille = ecrirePage(page, html, [['@@WASM_X2@@', fs.readFileSync(x2)], ['@@WASM_NATIF@@', fs.readFileSync(natif)], ['@@PAK@@', fs.readFileSync(pak)]]);
// Musiques à côté de la page (elle les cherche par HTTP : page servie, pas ouverte en fichier local)
const mus = path.join(root, 'data', 'musique.bin');
if (fs.existsSync(mus)) fs.copyFileSync(mus, path.join(path.dirname(page), 'musique.bin'));
console.log(path.basename(x2) + ' ' + Math.round(fs.statSync(x2).size / 1024) + ' Ko, ' + path.basename(natif) + ' ' + Math.round(fs.statSync(natif).size / 1024)
    + ' Ko, ' + path.basename(page) + ' ' + Math.round(taille / 1024) + ' Ko' + (enfant ? ' (édition enfant)' : ''));
