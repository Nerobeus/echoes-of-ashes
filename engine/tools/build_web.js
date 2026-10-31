// Compile le cœur en WebAssembly (Zig, cible wasm32-wasi en mode « reactor »), en deux variantes : 3D en
// 233 × 233 doublée et 3D native 466 × 466. Produit build/moteur.html, une page autonome qui intègre les deux
// modules et le paquet.
//
//   node tools/build_web.js [--enfant] [data/monde.pak] [build/moteur.html]
//     --enfant  édition enfant (ECHOES_ENFANT : seul un paquet de l'édition enfant se lance) : data/enfant/monde.pak
//               -> build/moteur_enfant.html, sauvegardes à part dans le stockage du navigateur
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const python = path.join(root, '..', 'tools', '.venv', 'Scripts', 'python.exe');
const enfant = process.argv.includes('--enfant'), pos = process.argv.slice(2).filter(a => !a.startsWith('--'));
const pak = path.resolve(pos[0] || path.join(root, 'data', ...(enfant ? ['enfant'] : []), 'monde.pak'));
const out = path.join(root, 'build');
fs.mkdirSync(out, { recursive: true });

const src = ['core/world.c', 'core/game.c', 'core/render.c', 'core/ui.c', 'core/hud.c', 'core/battle.c', 'core/script.c', 'core/audio.c', 'core/course.c', 'core/music.c', 'core/interieur.c', 'core/accueil.c', 'core/police.c', 'platform/web/main_web.c'].map(f => path.join(root, f));
const ed = enfant ? ['-DECHOES_ENFANT=1'] : [], suf = enfant ? '_enfant' : '';
const build = (name, defs) => {
    const wasm = path.join(out, name);
    execFileSync(python, ['-m', 'ziglang', 'cc', '-target', 'wasm32-wasi', '-mexec-model=reactor', '-std=c99', '-O2',
        '-fno-strict-aliasing', '-Wall', '-Wno-unused-parameter', '-Wl,--strip-all', ...ed, ...defs, '-o', wasm, ...src], { stdio: 'inherit' });
    return wasm;
};
const x2 = build('moteur' + suf + '_x2.wasm', []);
const natif = build('moteur' + suf + '_natif.wasm', ['-DENG_W=466', '-DENG_H_PX=466']);

let html = fs.readFileSync(path.join(root, 'platform', 'web', 'moteur.template.html'), 'utf8');
// Édition enfant : ses sauvegardes ne se mêlent pas à celles de l'édition 18+ (même navigateur, même origine)
if (enfant) html = html.split('\'echoes-sauve-\'').join('\'echoes-enfant-sauve-\'');
html = html.replace('@@WASM_X2@@', fs.readFileSync(x2).toString('base64'))
    .replace('@@WASM_NATIF@@', fs.readFileSync(natif).toString('base64'))
    .replace('@@PAK@@', fs.readFileSync(pak).toString('base64'));
const page = path.resolve(pos[1] || path.join(out, 'moteur' + suf + '.html'));
fs.writeFileSync(page, html);
// Musiques à côté de la page (elle les cherche par HTTP : page servie, pas ouverte en fichier local)
const mus = path.join(root, 'data', 'musique.bin');
if (fs.existsSync(mus)) fs.copyFileSync(mus, path.join(path.dirname(page), 'musique.bin'));
console.log(path.basename(x2) + ' ' + Math.round(fs.statSync(x2).size / 1024) + ' Ko, ' + path.basename(natif) + ' ' + Math.round(fs.statSync(natif).size / 1024)
    + ' Ko, ' + path.basename(page) + ' ' + Math.round(html.length / 1024) + ' Ko' + (enfant ? ' (édition enfant)' : ''));
