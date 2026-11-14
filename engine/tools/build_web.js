// Compile le cœur en WebAssembly (Zig, cible wasm32-wasi en mode « reactor »), en deux variantes : 3D en
// 233 × 233 doublée et 3D native 466 × 466. Produit build/moteur.html, une page autonome qui intègre les deux
// modules et le paquet.
//
//   node tools/build_web.js [--enfant] [--pc] [data/monde.pak] [build/moteur.html]
//     --enfant  édition enfant (ECHOES_ENFANT : seul un paquet de l'édition enfant se lance) : data/enfant/monde.pak
//     --ile     monde de l'île (maj_monde.js --ile) : data/ile/monde.pak -> build/moteur_ile.html, sauvegardes à part
//               -> build/moteur_enfant.html, sauvegardes à part dans le stockage du navigateur
//     --pc      version autonome pour PC (platform/web/pc.template.html) : jeu seul, sans la montre simulée, en plein écran
//               rectangulaire (3D 640 × 360 et interface, ENG_RECT), touches du clavier au choix, monde et musiques
//               compressés en gzip dans la page (un seul fichier, ouvert
//               d'un double-clic, sans serveur) -> build/choeur_pc.html (build/choeur_pc_enfant.html avec --enfant)
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const root = path.join(__dirname, '..');
const python = path.join(root, '..', 'tools', '.venv', 'Scripts', 'python.exe');
const enfant = process.argv.includes('--enfant'), pc = process.argv.includes('--pc'), pos = process.argv.slice(2).filter(a => !a.startsWith('--'));
const ile = !enfant && process.argv.includes('--ile');
// --antigrav (13/11/2026) : la course anti-gravité (core/antigrav.c), son paquet (tools/antigrav/generer.js) ; avec --pc :
// build/antigrav_pc.html, sa page (titre, aide, libellés des touches), ses touches gardées à part
const antigrav = !enfant && !ile && process.argv.includes('--antigrav');
const pak = path.resolve(pos[0] || (antigrav ? path.join(root, 'data', 'antigrav', 'antigrav.pak') : path.join(root, 'data', ...(enfant ? ['enfant'] : ile ? ['ile'] : []), 'monde.pak')));
const out = path.join(root, 'build');
fs.mkdirSync(out, { recursive: true });

const src = ['core/world.c', 'core/game.c', 'core/render.c', 'core/ui.c', 'core/hud.c', 'core/battle.c', 'core/script.c', 'core/audio.c', 'core/course.c', 'core/music.c', 'core/interieur.c', 'core/accueil.c', 'core/police.c', 'core/coffres.c', 'core/pilote.c', 'core/carte.c', 'core/carriere.c', 'core/garage.c', 'core/panneaux.c', 'core/repliques.c', 'core/arene.c', 'core/ile.c', 'core/antigrav.c', 'platform/web/main_web.c'].map(f => path.join(root, f));
const ed = enfant ? ['-DECHOES_ENFANT=1'] : [], suf = enfant ? '_enfant' : ile ? '_ile' : antigrav ? '_antigrav' : '';
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
    // écran rectangulaire (11/11/2026, demande de l'utilisateur : « une version plein écran rectangle pour PC avec commande
    // clavier personnalisable ») : 3D 640 × 360 (16:9 : × 2 en 720p, × 3 en 1080p) et interface comme sur la LDK (ENG_RECT,
    // main_web.c : web_frame_pc) ; champ plus large que la montre : tampons du rendu doublés
    const wasm = build('choeur_pc' + suf + '.wasm', ['-DENG_RECT=1', '-DENG_PREMIUM=1', '-DENG_DOF=1', '-DENG_ROUND=0', '-DENG_W=1920', '-DENG_H_PX=1080', '-DENG_WORKERS=1', ...(antigrav ? ['-DENG_ROULIS=1'] : [])]);   /* (roulis de la caméra : vue cockpit) */
    let page = fs.readFileSync(path.join(root, 'platform', 'web', 'pc.template.html'), 'utf8');
    if (enfant) page = page.split("'echoes-pc-sauve-'").join("'echoes-pc-enfant-sauve-'");
    if (ile) page = page.split("'echoes-pc-sauve-'").join("'echoes-pc-ile-sauve-'");
    if (antigrav) {   // course anti-gravité : sa page (titre, aide), ses actions nommées pour la course, ses touches et réglages à part
        const R = [
            ['<title>Le Chœur des Cendres</title>', '<title>AntiGraV</title>'],
            ['<h1 id="titre-aide">Le Chœur des Cendres</h1>', '<h1 id="titre-aide">AntiGraV</h1>'],
            ['Version pour PC, dans le navigateur, en plein écran. Même moteur que la montre, le téléphone et la LDK Game.', 'Course de glisseurs façon PS1, dans le navigateur, en plein écran. Même moteur que la LDK Game.'],
            ["nom: 'Haut', detail: 'courir, choisir ; en véhicule : accélérer'", "nom: 'Poussée', detail: 'accélérer (aussi Valider) ; choisir'"],
            ["nom: 'Gauche', detail: 'en véhicule : braquer'", "nom: 'Gauche', detail: 'diriger ; écurie précédente'"],
            ["nom: 'Valider', detail: 'parler, entrer, monter dans un véhicule'", "nom: 'Valider', detail: 'courir, choisir ; en course : poussée'"],
            ["nom: 'Menu', detail: 'ouvrir le menu, pause en course'", "nom: 'Pause', detail: 'reprendre, recommencer, quitter'"],
            ["nom: 'Retour', detail: 'revenir ; hors des menus : ouvrir le menu'", "nom: 'Retour', detail: 'revenir ; en course : vue cockpit ou extérieure'"],
            ["nom: 'Caméra à gauche', detail: 'tourner la caméra ; carte du menu : zoom'", "nom: 'Aérofrein gauche', detail: 'virage serré à gauche, freinage'"],
            ["nom: 'Caméra à droite', detail: ''", "nom: 'Aérofrein droit', detail: ''"],
            ["nom: 'Monter', detail: 'vaisseau'", "nom: 'Arme', detail: 'utiliser l\\'arme ramassée sur une plaque d\\'armes (le rayon : tenir pour charger)'"],
            ["nom: 'Descendre', detail: 'vaisseau'", "nom: 'Absorber', detail: 'changer l\\'arme portée en énergie'"],
            ["'echoes-pc-touches'", "'antigrav-pc-touches'"], ["'echoes-pc-sauve-'", "'antigrav-pc-sauve-'"], ["'echoes-pc-aide'", "'antigrav-pc-aide'"], ["'echoes-pc-reglages'", "'antigrav-pc-reglages'"],
            // 30 images par seconde fixes, comme sur la LDK (13/11/2026, demande de l'utilisateur) : la page n'en joue jamais plus
            ['// ---------- Boucle : 60 images par seconde au plus ----------', '// ---------- Boucle : 30 images par seconde fixes (course anti-gravité) ----------'],
            ['prochaine = Math.max(prochaine + 1000 / 60, now - 1000 / 60);', 'prochaine = Math.max(prochaine + 1000 / 30, now - 1000 / 30);']
        ];
        for (const [a, b] of R) { if (!page.includes(a)) console.warn('page PC : « ' + a.slice(0, 50) + ' » introuvable'); page = page.split(a).join(b); }
        page = page.replace(/<h2>Souris<\/h2>\s*<dl>[\s\S]*?<\/dl>/, '');   // (la souris ne sert pas à la course)
        // actions sans objet en course : au pas, gâchettes, vaisseau, bas (lignes entières du tableau ACTIONS)
        page = page.replace(/^ *\{ id: '(pas|gaz|frein|bas)',[^\n]*\n/gm, '');   // (lignes en CRLF : « . » s'arrête au \r)
    }
    const mus = [path.join(root, 'data', ...(enfant ? ['enfant'] : antigrav ? ['antigrav'] : []), 'musique.bin'), path.join(root, 'data', 'musique.bin')].find(f => fs.existsSync(f));   // (course anti-gravité : sa banque, pistes_antigrav.json)
    const gz = f => zlib.gzipSync(fs.readFileSync(f), { level: 9 });
    const sortie = path.resolve(pos[1] || path.join(out, antigrav ? 'antigrav_pc.html' : 'choeur_pc' + suf + '.html'));
    const taille = ecrirePage(sortie, page, [['@@WASM@@', fs.readFileSync(wasm)], ['@@PAK_GZ@@', gz(pak)], ['@@MUSIQUE_GZ@@', mus ? gz(mus) : Buffer.alloc(0)]]);
    console.log(path.basename(sortie) + ' ' + Math.round(taille / 1048576) + ' Mo (' + path.basename(pak) + (mus ? ', musiques' : ', sans musique') + ')' + (enfant ? ' (édition enfant)' : ''));
    process.exit(0);
}
const x2 = build('moteur' + suf + '_x2.wasm', []);
const natif = build('moteur' + suf + '_natif.wasm', ['-DENG_W=466', '-DENG_H_PX=466']);

let html = fs.readFileSync(path.join(root, 'platform', 'web', 'moteur.template.html'), 'utf8');
// Édition enfant : ses sauvegardes ne se mêlent pas à celles de l'édition 18+ (même navigateur, même origine)
if (enfant) html = html.split('\'echoes-sauve-\'').join('\'echoes-enfant-sauve-\'');
if (ile) html = html.split('\'echoes-sauve-\'').join('\'echoes-ile-sauve-\'');   // île : ses parties à part
const page = path.resolve(pos[1] || path.join(out, 'moteur' + suf + '.html'));
const taille = ecrirePage(page, html, [['@@WASM_X2@@', fs.readFileSync(x2)], ['@@WASM_NATIF@@', fs.readFileSync(natif)], ['@@PAK@@', fs.readFileSync(pak)]]);
// Musiques à côté de la page (elle les cherche par HTTP : page servie, pas ouverte en fichier local)
const mus = path.join(root, 'data', 'musique.bin');
if (fs.existsSync(mus)) fs.copyFileSync(mus, path.join(path.dirname(page), 'musique.bin'));
console.log(path.basename(x2) + ' ' + Math.round(fs.statSync(x2).size / 1024) + ' Ko, ' + path.basename(natif) + ' ' + Math.round(fs.statSync(natif).size / 1024)
    + ' Ko, ' + path.basename(page) + ' ' + Math.round(taille / 1024) + ' Ko' + (enfant ? ' (édition enfant)' : ''));
