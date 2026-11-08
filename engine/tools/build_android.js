// Construit l'app Android : montre Wear OS (par défaut) ou téléphone (--telephone). Le cœur est compilé par le NDK pour
// les ABI ARM (armeabi-v7a, arm64-v8a), NativeActivity sans code Java, paquet monde et musiques dans les assets sans
// compression. Pas de Gradle : clang du NDK, aapt2, 7-Zip, zipalign et apksigner (clé de débogage d'Android Studio).
// Produit build/echoes_montre.apk ou build/echoes_telephone.apk.
// Deux bibliothèques, deux icônes : libechoes.so (« Echoes of Ashes ») et libechoes_natif.so (« Echoes 466 », écran rond
// en 3D native 466 × 466, sans pixels doublés). libechoes.so : sur la montre, l'écran rond en 3D 233 × 233 doublée comme
// sur l'ESP32 ; sur le téléphone, le plein écran (ENG_DYN : image à la moitié de la définition de l'écran, en portrait,
// ENG_WORKERS fils, flou de profondeur ENG_DOF).
// Au lancement, l'app prend au serveur de l'Atelier (node serveur.js, même Wi-Fi) le paquet et les musiques s'ils sont
// plus récents que ceux de l'APK (platform/android/maj_android.c) : assets monde.pak.date et musique.bin.date (date des fichiers
// mis dans l'APK) et serveur.txt (adresses de ce PC, essayées avant la recherche sur le réseau).
//
//   node tools/build_android.js [--telephone] [--x86] [--installer] [--enfant] [--sans-monde] [--serveur=IP:PORT] [data/monde.pak]
//     --telephone  app téléphone (Android 9 et plus, portrait, cercle centré ; ABI x86_64 comprise pour les émulateurs)
//     --x86        ajoute l'ABI x86_64 (émulateur Wear OS du SDK)
//     --sans-monde APK léger sans paquet ni musiques : tout vient du serveur au premier lancement (puis la dernière copie)
//     --autonome   APK seul, sans le PC : monde et musiques de l'APK, ni serveur de l'Atelier, ni réglages (pas d'icône
//                  « Echoes réglages », pas d'accès au réseau) ; une autre app (paquet Android en .autonome),
//                  build/echoes_telephone_autonome.apk (ou echoes_montre_autonome.apk)
//     --serveur    adresse du serveur de l'Atelier essayée en premier (par défaut : celles de ce PC, port 8080)
//     --enfant     édition enfant (ECHOES_ENFANT : seul un paquet de l'édition enfant se lance) : data/enfant/monde.pak,
//                  build/echoes_montre_enfant.apk (ou echoes_telephone_enfant.apk), une autre app (paquet Android en .enfant)
//     --installer  installe par adb (montre : débogage Wi-Fi, adb pair puis adb connect ; téléphone : débogage USB).
//                  Plusieurs appareils branchés : choisir avec la variable ANDROID_SERIAL (voir adb devices)
//
// Chemins : ANDROID_HOME (sinon D:\Android\Sdk), JAVA_HOME (sinon le JDK d'Android Studio), 7-Zip dans Program Files.
const { execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const root = path.join(__dirname, '..');
const args = process.argv.slice(2);
const installer = args.includes('--installer'), telephone = args.includes('--telephone'), enfant = args.includes('--enfant');
const x86 = telephone || args.includes('--x86'), sansMonde = args.includes('--sans-monde'), autonome = args.includes('--autonome');
if (autonome && sansMonde) throw new Error('--autonome et --sans-monde ensemble : l\'APK autonome garde son monde');
const serveur = (args.find(a => a.startsWith('--serveur=')) || '').slice(10);
const pak = path.resolve(args.find(a => !a.startsWith('--')) || path.join(root, 'data', ...(enfant ? ['enfant'] : []), 'monde.pak'));
const plat = path.join(root, 'platform', 'android');
const manifest0 = path.join(plat, ...(telephone ? ['telephone'] : []), 'AndroidManifest.xml');
const APPLI = telephone ? 'fr.maetzindustries.echoes.mobile' : 'fr.maetzindustries.echoes';   // paquet Android du manifeste
const suffixe = (enfant ? '_enfant' : '') + (autonome ? '_autonome' : '');
const out = path.join(root, 'build', (telephone ? 'android_telephone' : 'android') + suffixe);

const sdk = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT || 'D:\\Android\\Sdk';
const newest = dir => fs.readdirSync(dir).sort((a, b) => a.localeCompare(b, 'en', { numeric: true })).pop();
const ndk = path.join(sdk, 'ndk', newest(path.join(sdk, 'ndk')));
const bt = path.join(sdk, 'build-tools', newest(path.join(sdk, 'build-tools')));
const jar = fs.readdirSync(path.join(sdk, 'platforms')).map(p => path.join(sdk, 'platforms', p, 'android.jar')).filter(fs.existsSync)
    .sort((a, b) => a.localeCompare(b, 'en', { numeric: true })).pop();
const clang = path.join(ndk, 'toolchains', 'llvm', 'prebuilt', 'windows-x86_64', 'bin', 'clang.exe');
const glue = path.join(ndk, 'sources', 'android', 'native_app_glue');
const java = process.env.JAVA_HOME || 'C:\\Program Files\\Android\\Android Studio\\jbr';
const sevenZip = ['C:\\Program Files\\7-Zip\\7z.exe', 'D:\\Program Files\\7-Zip\\7z.exe'].find(fs.existsSync);
if (!sevenZip) throw new Error('7-Zip introuvable (7z.exe) : il range les bibliothèques sans compression dans l\'APK');
const MIN_SDK = telephone ? 28 : 30;    // téléphone : Android 9 (AAudio avec usage « jeu ») ; montre : Wear OS 3 (Galaxy Watch4 et suivantes)
const run = (exe, a, opt = {}) => execFileSync(exe, a, { stdio: 'inherit', ...opt });

fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(path.join(out, 'assets'), { recursive: true });
// APK autonome : le manifeste sans l'activité des réglages ni la permission réseau
let manifest = manifest0;
if (autonome) {
    let m = fs.readFileSync(manifest0, 'utf8');
    m = m.replace(/\s*<uses-permission android:name="android\.permission\.INTERNET" \/>/, '').replace(/\s*android:usesCleartextTraffic="true"/, '')
        .replace(/\s*<!--[\s\S]*?-->/g, c => /Reglages/.test(c) ? '' : c)   /* commentaires qui parlent des réglages */
        .replace(/\s*<activity\s+android:name="fr\.maetzindustries\.echoes\.Reglages"[\s\S]*?<\/activity>/, '');
    if (/Reglages|INTERNET/.test(m.replace(/<!--[\s\S]*?-->/g, ''))) throw new Error('manifeste autonome : réglages ou réseau encore là');
    manifest = path.join(out, 'AndroidManifest.xml');
    fs.writeFileSync(manifest, m);
}

// 1. Bibliothèques natives, deux par ABI : 3D doublée et 3D native
const src = ['core/world.c', 'core/game.c', 'core/render.c', 'core/ui.c', 'core/hud.c', 'core/battle.c', 'core/script.c', 'core/audio.c', 'core/course.c', 'core/music.c', 'core/interieur.c', 'core/accueil.c', 'core/police.c', 'core/coffres.c', 'core/pilote.c', 'core/carte.c', 'core/carriere.c', 'core/garage.c', 'core/panneaux.c', 'core/repliques.c', 'core/arene.c', 'platform/android/main_android.c', 'platform/android/maj_android.c']
    .map(f => path.join(root, f)).concat(path.join(glue, 'android_native_app_glue.c'));
const ABI = { 'arm64-v8a': 'aarch64-linux-android', 'armeabi-v7a': 'armv7a-linux-androideabi', ...(x86 ? { 'x86_64': 'x86_64-linux-android' } : {}) };
for (const [abi, triple] of Object.entries(ABI)) {
    const dir = path.join(out, 'lib', abi);
    fs.mkdirSync(dir, { recursive: true });
    const MODES = [['libechoes.so', telephone ? ['-DENG_DYN=1', '-DENG_ROUND=0', '-DENG_DOF=1', '-DENG_WORKERS=4'] : []],
        ['libechoes_natif.so', ['-DENG_W=466', '-DENG_H_PX=466']]];
    for (const [lib, mode] of MODES) {
        run(clang, ['--target=' + triple + MIN_SDK, '-std=gnu99', '-O2', '-fno-strict-aliasing', '-fPIC', '-shared', '-Wall', '-Wno-unused-parameter',
            '-I' + glue, ...mode, ...(enfant ? ['-DECHOES_ENFANT=1'] : []), ...(autonome ? ['-DECHOES_AUTONOME=1'] : []),
            '-u', 'ANativeActivity_onCreate', '-Wl,-z,max-page-size=16384', '-Wl,--build-id=none', '-s',
            '-o', path.join(dir, lib), ...src, '-landroid', '-llog', '-laaudio', '-lm']);
        console.log(abi + ' : ' + lib + ' ' + Math.round(fs.statSync(path.join(dir, lib)).size / 1024) + ' Ko');
    }
}

// 2. Assets : paquet et musiques (le moteur les lit en place, d'où le rangement sans compression), avec leur date (ms)
const asset = (src, nom) => {
    fs.copyFileSync(src, path.join(out, 'assets', nom));
    fs.writeFileSync(path.join(out, 'assets', nom + '.date'), String(Math.floor(fs.statSync(src).mtimeMs)));
};
const mus = path.join(root, 'data', 'musique.bin');
if (sansMonde) console.log('APK sans monde : paquet et musiques pris au serveur de l\'Atelier au premier lancement');
else {
    asset(pak, 'monde.pak');
    if (fs.existsSync(mus)) asset(mus, 'musique.bin');
    else console.log('pas de data/musique.bin : musiques prises au serveur de l\'Atelier s\'il en a');
}
// Adresses du serveur de l'Atelier : celle donnée, puis celles de ce PC sur le réseau local (pas dans l'APK autonome)
if (autonome) console.log('APK autonome : monde et musiques de l\'APK, sans serveur de l\'Atelier');
else {
const adresses = [serveur, ...Object.values(os.networkInterfaces()).flat().filter(a => a.family === 'IPv4' && !a.internal).map(a => a.address + ':8080')].filter(Boolean);
fs.writeFileSync(path.join(out, 'assets', 'serveur.txt'), adresses.join('\n') + '\n');
console.log('serveur de l\'Atelier cherché à : ' + adresses.join(', ') + ', puis sur le réseau Wi-Fi');
}

// 3. Ressources, manifeste et assets, puis les bibliothèques, alignement et signature
const aapt2 = path.join(bt, 'aapt2.exe');
run(aapt2, ['compile', '--dir', path.join(plat, 'res'), '-o', path.join(out, 'res.zip')]);
const brut = path.join(out, 'brut.apk');
run(aapt2, ['link', '-o', brut, '-I', jar, '--manifest', manifest, path.join(out, 'res.zip'),
    '-A', path.join(out, 'assets'), '-0', 'pak', '-0', 'bin', '--min-sdk-version', String(MIN_SDK), '--target-sdk-version', '35',
    // Édition enfant : une autre app, installable à côté de l'édition 18+ (sauvegardes à part)
    // APK autonome : une autre app encore, à côté de celle qui suit l'Atelier
    ...(enfant || autonome ? ['--rename-manifest-package', APPLI + (enfant ? '.enfant' : '') + (autonome ? '.autonome' : '')] : [])]);
// Classes Java (platform/android/java : Natif, deuxième icône, qui hérite de NativeActivity ; Reglages, troisième
// icône) : javac puis d8 en classes.dex
const classes = path.join(out, 'classes');
const fichiers = (dir, ext) => fs.readdirSync(dir, { withFileTypes: true }).flatMap(e =>
    e.isDirectory() ? fichiers(path.join(dir, e.name), ext) : e.name.endsWith(ext) ? [path.join(dir, e.name)] : []);
run(path.join(java, 'bin', 'javac.exe'), ['-source', '11', '-target', '11', '-nowarn', '-encoding', 'UTF-8', '-classpath', jar, '-d', classes,
    ...fichiers(path.join(plat, 'java'), '.java')], { stdio: ['ignore', 'inherit', 'inherit'] });
run(path.join(java, 'bin', 'java.exe'), ['-cp', path.join(bt, 'lib', 'd8.jar'), 'com.android.tools.r8.D8', '--release', '--min-api', String(MIN_SDK),
    '--lib', jar, '--output', out, ...fichiers(classes, '.class')], { stdio: ['ignore', 'inherit', 'inherit'], timeout: 120000 });
run(sevenZip, ['a', '-tzip', '-mx=0', brut, 'lib', 'classes.dex'], { cwd: out, stdio: 'ignore' });
const aligne = path.join(out, 'aligne.apk');
run(path.join(bt, 'zipalign.exe'), ['-f', '-P', '16', '4', brut, aligne]);
const apk = path.join(root, 'build', (telephone ? 'echoes_telephone' : 'echoes_montre') + suffixe + '.apk');
// L'ancienne APK effacée d'abord : réécrite en place, elle gardait sa date de création (celle qu'affiche l'explorateur)
fs.rmSync(apk, { force: true });
run(path.join(java, 'bin', 'java.exe'), ['--enable-native-access=ALL-UNNAMED', '-jar', path.join(bt, 'lib', 'apksigner.jar'), 'sign',
    '--ks', path.join(os.homedir(), '.android', 'debug.keystore'), '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
    '--ks-key-alias', 'androiddebugkey', '--out', apk, aligne], { stdio: ['ignore', 'inherit', 'inherit'], timeout: 120000 });
fs.rmSync(apk + '.idsig', { force: true });
console.log(path.relative(root, apk) + ' ' + (fs.statSync(apk).size / 1048576).toFixed(1) + ' Mo' + (enfant ? ' (édition enfant)' : '') + (autonome ? ' (autonome)' : ''));
// Dossier de travail effacé : il gardait le monde en trois copies de plus (assets/monde.pak, brut.apk, aligne.apk), soit
// 5 Go pour cinq versions de l'app ; la construction suivante le refait de toute façon entièrement (début de ce script)
fs.rmSync(out, { recursive: true, force: true });

// Installation : l'appareil de ANDROID_SERIAL, sinon le premier qui n'est pas un émulateur (un émulateur ouvert à côté
// d'un téléphone branché ne gêne pas), sinon le seul branché
if (installer) {
    const adb = path.join(sdk, 'platform-tools', 'adb.exe');
    const liste = execFileSync(adb, ['devices'], { encoding: 'utf8' }).split('\n').slice(1).map(l => l.trim().split(/\s+/)).filter(c => c[1] === 'device').map(c => c[0]);
    const cible = process.env.ANDROID_SERIAL || liste.find(s => !s.startsWith('emulator-')) || liste[0];
    if (!cible) throw new Error('aucun appareil adb : brancher le téléphone (débogage USB) ou connecter la montre (adb connect)');
    console.log('installation sur ' + cible);
    run(adb, ['-s', cible, 'install', '-r', apk]);
}
