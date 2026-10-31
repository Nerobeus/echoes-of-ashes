// Met à jour le monde du jeu en une commande, depuis le dossier où le navigateur télécharge :
//   - Atelier v4, onglet Jouer, « Paquet moteur » -> monde.pak : copié dans engine/data, puis page du moteur
//   - ancien export « Monde émulateur » (monde.json, monde_sol.png, monde_batiments.png) : copié dans emulateur/,
//     intégré à emulateur.html (integrer-monde.js), paquet exporté depuis l'émulateur, puis page du moteur
// Le plus récent des deux l'emporte. L'histoire (section SCEN du paquet) est aussi écrite dans engine/data/histoire.scn :
// à copier dans echoes/ sur la carte SD, où le firmware la lit à mesure (sinon il prend la section du paquet en flash).
//
//   node engine/tools/maj_monde.js [dossier source]     (par défaut : le dossier Téléchargements)
//     --montre     reconstruit aussi l'app montre (build/echoes_montre.apk ; --montre --installer : sur la montre par adb)
//     --emulateur  reconstruit l'app montre et la lance sur la montre virtuelle Wear OS (tools/montre_emulateur.js)
//     --telephone  la même chose pour l'app téléphone (build/echoes_telephone.apk) : seule, ou avec --installer ou --emulateur
//     --enfant     édition enfant (Atelier v4, onglet Jouer, édition « Enfant ») : monde_enfant.pak (section EDIT vérifiée)
//                  -> engine/data/enfant/, page build/moteur_enfant.html, apps *_enfant.apk avec --montre ou --telephone ;
//                  carte SD : echoes_enfant/ ; firmware : platform/esp32/idf_enfant.cmd build
//
// Le navigateur renomme les doublons (« monde (1).json ») : on prend le fichier le plus récent de chaque sorte.
const { execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const emu = path.join(root, 'emulateur');
const flags = process.argv.slice(2).filter(a => a.startsWith('--'));
// Dossier de téléchargement de Windows (il peut être déplacé, ici D:\Téléchargements), demandé au shell en UTF-8
// (reg.exe écrit dans la page de code de la console et abîmerait les accents)
const telechargements = () => {
    try {
        const dir = execFileSync('powershell', ['-NoProfile', '-Command', "[Console]::OutputEncoding = [Text.Encoding]::UTF8; " +
            "(New-Object -ComObject Shell.Application).NameSpace('shell:Downloads').Self.Path"], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim();
        if (dir && fs.existsSync(dir)) return dir;
    } catch { /* pas Windows */ }
    return path.join(os.homedir(), 'Downloads');
};
const src = path.resolve(process.argv.slice(2).find(a => !a.startsWith('--')) || telechargements());

const newest = re => {
    const files = fs.readdirSync(src).filter(f => re.test(f)).map(f => ({ f, t: fs.statSync(path.join(src, f)).mtimeMs }));
    if (!files.length) return null;
    files.sort((a, b) => b.t - a.t);
    return path.join(src, files[0].f);
};
const date = f => new Date(fs.statSync(f).mtimeMs).toLocaleString('fr-FR');
const mtime = f => f ? fs.statSync(f).mtimeMs : -1;
const run = script => execFileSync(process.execPath, script, { stdio: 'inherit' });

// Édition enfant : son paquet (monde_enfant.pak) et ses données à part (engine/data/enfant)
const enfant = flags.includes('--enfant'), pakNom = enfant ? 'monde_enfant.pak' : 'monde.pak';
const data = path.join(root, 'engine', 'data', ...(enfant ? ['enfant'] : [])), dataRel = enfant ? 'engine/data/enfant' : 'engine/data';
const sdDir = enfant ? 'echoes_enfant' : 'echoes';
fs.mkdirSync(data, { recursive: true });
// Section d'un paquet (null si absente)
const sectionDe = (buf, tag) => {
    for (let o = 8; o + 8 <= buf.length; o += 8 + buf.readUInt32LE(o + 4))
        if (buf.toString('latin1', o, o + 4) === tag) return buf.subarray(o + 8, o + 8 + buf.readUInt32LE(o + 4));
    return null;
};
const editionDe = buf => { const e = sectionDe(buf, 'EDIT'); return e && e.length ? e[0] : 0; };

const pak = newest(enfant ? /^monde_enfant( \(\d+\))?\.pak$/ : /^monde( \(\d+\))?\.pak$/), json = enfant ? null : newest(/^monde( \(\d+\))?\.json$/);
if (!pak && !json) {
    console.error(`${pakNom} introuvable dans ${src} : exporte d'abord le monde (Atelier v4 : onglet Jouer, ${enfant ? 'édition « Enfant », ' : ''}« Paquet moteur »),
ou donne le dossier où le navigateur l'a enregistré : node engine/tools/maj_monde.js "D:\\mon dossier"${enfant ? ' --enfant' : ''}`);
    process.exit(1);
}
console.log(`Source : ${src}${enfant ? ' (édition enfant)' : ''}\n`);
if (pak && mtime(pak) >= mtime(json)) {
    // Le moteur de l'édition enfant ne lance que les paquets de l'édition enfant : autant le dire tout de suite
    const ed = editionDe(fs.readFileSync(pak));
    if (enfant && ed !== 1) { console.error(`${path.basename(pak)} n'est pas de l'édition enfant (section EDIT) : réexporter depuis l'Atelier avec l'édition « Enfant ».`); process.exit(1); }
    if (!enfant && ed === 1) console.log(`(${path.basename(pak)} est de l'édition enfant : le moteur 18+ le lance aussi)`);
    console.log(`[1/2] ${path.basename(pak)} (${date(pak)}) -> ${dataRel}/monde.pak`);
    fs.copyFileSync(pak, path.join(data, 'monde.pak'));
    console.log('\n[2/2] page du moteur');
    run([path.join(__dirname, 'build_web.js'), ...(enfant ? ['--enfant'] : [])]);
} else {
    const wanted = [
        ['monde.json', /^monde( \(\d+\))?\.json$/],
        ['monde_sol.png', /^monde_sol( \(\d+\))?\.png$/],
        ['monde_batiments.png', /^monde_batiments( \(\d+\))?\.png$/],
    ];
    const step = (n, text) => console.log(`\n[${n}/4] ${text}`);
    step(1, `copie depuis ${src}`);
    for (const [name, re] of wanted) {
        const f = newest(re);
        if (!f) { console.error(`  ${name} introuvable dans ${src} : exporte d'abord le monde (Atelier v4 : onglet Jouer, « Paquet moteur »).`); process.exit(1); }
        const dest = path.join(emu, name);
        if (path.resolve(f) !== path.resolve(dest)) fs.copyFileSync(f, dest);
        console.log(`  ${path.basename(f)} (${date(f)}) -> emulateur/${name}`);
    }
    step(2, 'intégration dans emulateur.html');
    run([path.join(emu, 'integrer-monde.js')]);
    step(3, 'export du paquet moteur (Edge sans fenêtre)');
    run([path.join(__dirname, 'exporter_paquet.js')]);
    step(4, 'page du moteur');
    run([path.join(__dirname, 'build_web.js')]);
}

// Histoire pour la carte SD : la section SCEN telle quelle (format SCN5, voir scenBytes dans l'émulateur)
const pakData = fs.readFileSync(path.join(data, 'monde.pak'));
const scn = sectionDe(pakData, 'SCEN');
if (scn && scn.toString('latin1', 0, 4) === 'SCN5') {
    fs.writeFileSync(path.join(data, 'histoire.scn'), scn);
    console.log(`\nHistoire : ${scn.readUInt32LE(28)} quêtes, ${scn.readUInt32LE(12)} événements, ${scn.readUInt32LE(8)} répliques, ${scn.length} octets -> ${dataRel}/histoire.scn
  (à copier sur la carte SD : ${sdDir}/histoire.scn)`);
} else console.log('\nHistoire : pas de section SCEN au format SCN5 dans le paquet (réexporter depuis l\'Atelier).');

// Intérieurs des bâtiments pour la carte SD : la section INTR telle quelle (format INT2, voir pak.h). L'ESP32 lit le
// monde sur la carte (monde.pak, entier) ; la flash (monde_esp32.pak, écrit par idf.py flash) n'a que le paquet léger
// (tools/paquet_sd.js : petites sections, index des planches et des textures du sol). monde.pak reste aussi entier pour
// la page web, les apps Android et le banc de rendu.
let intr = null;
for (let o = 8; o + 8 <= pakData.length; o += 8 + pakData.readUInt32LE(o + 4))
    if (pakData.toString('latin1', o, o + 4) === 'INTR') { intr = pakData.subarray(o + 8, o + 8 + pakData.readUInt32LE(o + 4)); break; }
const leger = require('./paquet_sd.js').paquetLeger(pakData), esp32 = leger.buf;
fs.writeFileSync(path.join(data, 'monde_esp32.pak'), esp32);
console.log(`\nMonde de l'ESP32 : ${dataRel}/monde.pak (${(pakData.length / 1048576).toFixed(1)} Mo) à copier sur la carte SD : ${sdDir}/monde.pak
  (planches, textures du sol et chunks y sont lus à la demande ; le paquet léger de la flash doit venir du même export)`);
if (intr && /^INT[12]$/.test(intr.toString('latin1', 0, 4))) {
    fs.writeFileSync(path.join(data, 'interieurs.bin'), intr);
    console.log(`\nIntérieurs : ${intr.readUInt32LE(4)} niveaux, ${(intr.length / 1024) | 0} Ko -> ${dataRel}/interieurs.bin
  (à copier sur la carte SD : ${sdDir}/interieurs.bin ; le paquet de la flash, ${dataRel}/monde_esp32.pak, ne les a pas)`);
} else console.log('\nIntérieurs : pas de section INTR dans le paquet (réexporter depuis l\'Atelier).');
// Partition « monde » de l'ESP32 : 13 Mo
console.log(`\nPaquet léger de la flash ESP32 : ${(esp32.length / 1048576).toFixed(2)} Mo -> ${dataRel}/monde_esp32.pak (${leger.planches} planches et ${leger.textures} textures du sol sur la carte)`);
if (esp32.length > 13 * 1024 * 1024) console.log(`ATTENTION : plus grand que la partition « monde » de l'ESP32 (13 Mo)`);

// Apps Android (montre Wear OS, téléphone) : le même paquet dans l'APK
const tel = flags.includes('--telephone'), quoi = (tel ? 'App téléphone' : 'App montre') + (enfant ? ' (édition enfant)' : '');
const passe = flags.filter(f => ['--natif', '--telephone', '--enfant', '--sans-monde'].includes(f) || f.startsWith('--serveur='));
if (flags.includes('--emulateur')) { console.log('\n' + quoi + ', sur l\'émulateur'); run([path.join(__dirname, 'montre_emulateur.js'), ...passe]); }
else if (tel || flags.includes('--montre')) { console.log('\n' + quoi); run([path.join(__dirname, 'build_android.js'), ...passe, ...flags.filter(f => f === '--installer')]); }

const e = enfant ? ' --enfant' : '';
console.log(`
Terminé${enfant ? ' (édition enfant)' : ''}. À ouvrir dans le navigateur :
  ${path.join(root, 'engine', 'build', enfant ? 'moteur_enfant.html' : 'moteur.html')}
Sur la carte (écrit aussi le paquet) :
  ${path.join(root, 'engine', 'platform', 'esp32', enfant ? 'idf_enfant.cmd' : 'idf.cmd')} -p COM7 flash monitor   (depuis engine\\platform\\esp32)
Sur la montre : node engine/tools/maj_monde.js${e} --montre --installer, ou la montre virtuelle : --emulateur
Sur le téléphone : --telephone --installer, ou le téléphone virtuel : --telephone --emulateur`);
