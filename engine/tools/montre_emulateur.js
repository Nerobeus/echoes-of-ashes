// Lance le jeu dans un émulateur du SDK : la montre virtuelle Wear OS (AVD « echoes_montre », ronde 454 × 454) ou, avec
// --telephone, le téléphone virtuel (AVD « echoes_telephone », Pixel 6). Construit l'APK (ABI x86_64 comprise), démarre
// l'émulateur s'il ne tourne pas, attend la fin du démarrage d'Android, installe et lance l'app.
//
//   node tools/montre_emulateur.js [--telephone] [--sans-construire] [--natif] [--enfant]
//     --telephone        app et émulateur téléphone (build/echoes_telephone.apk)
//     --sans-construire  réinstalle l'APK existant
//     --natif            lance l'icône « Echoes 466 » (3D native 466 × 466) au lieu de « Echoes of Ashes »
//     --enfant           app de l'édition enfant (build_android.js --enfant)
//
// Dans la fenêtre de l'émulateur : clic = toucher, Échap ou bouton retour = retour, flèches, Entrée = valider, M = menu ;
// lunette de la montre : « ... » (contrôles étendus) > Rotary input. Journal du jeu : adb logcat -s echoes
const { execFileSync, spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const args = process.argv.slice(2);
const telephone = args.includes('--telephone'), enfant = args.includes('--enfant');   // --enfant : app de l'édition enfant
const AVD = telephone ? 'echoes_telephone' : 'echoes_montre';
const appli = (telephone ? 'fr.maetzindustries.echoes.mobile' : 'fr.maetzindustries.echoes') + (enfant ? '.enfant' : '');
const sdk = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT || 'D:\\Android\\Sdk';
const adbExe = path.join(sdk, 'platform-tools', 'adb.exe'), emulator = path.join(sdk, 'emulator', 'emulator.exe');
const apk = path.join(root, 'build', (telephone ? 'echoes_telephone' : 'echoes_montre') + (enfant ? '_enfant' : '') + '.apk');
const pause = ms => new Promise(r => setTimeout(r, ms));
const attendre = async (texte, test, secondes) => {
    process.stdout.write(texte);
    for (let t = 0; t < secondes; t += 2) { if (test()) { console.log(' ok'); return; } process.stdout.write('.'); await pause(2000); }
    throw new Error('délai dépassé : ' + texte);
};

(async () => {
    if (!args.includes('--sans-construire'))
        execFileSync(process.execPath, [path.join(__dirname, 'build_android.js'), telephone ? '--telephone' : '--x86', ...args.filter(a => a === '--enfant' || a === '--sans-monde' || a.startsWith('--serveur='))], { stdio: 'inherit' });
    if (!fs.existsSync(apk)) throw new Error('pas d\'APK : ' + apk);

    // L'émulateur voulu : celui dont le nom d'AVD correspond (la montre et le téléphone peuvent tourner ensemble)
    execFileSync(adbExe, ['start-server'], { stdio: 'ignore' });
    const appareil = () => execFileSync(adbExe, ['devices'], { encoding: 'utf8' }).split('\n').map(l => l.split('\t')[0].trim())
        .filter(s => s.startsWith('emulator-')).find(s => {
            try { return execFileSync(adbExe, ['-s', s, 'emu', 'avd', 'name'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).split(/\r?\n/)[0].trim() === AVD; }
            catch { return false; }
        });
    const adb = (...a) => { try { return execFileSync(adbExe, ['-s', appareil(), ...a], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim(); } catch { return ''; } };
    if (adb('shell', 'getprop', 'sys.boot_completed') !== '1') {
        if (!appareil()) {
            console.log('démarrage de l\'émulateur ' + AVD);
            spawn(emulator, ['-avd', AVD, '-no-boot-anim'], { detached: true, stdio: 'ignore', env: { ...process.env, ANDROID_SDK_ROOT: sdk } }).unref();
        }
        await attendre('Android démarre', () => adb('shell', 'getprop', 'sys.boot_completed') === '1', 300);
    }
    // Premier démarrage de la montre : écran « Starting... » de Wear OS par-dessus tout le reste
    if (!telephone)
        await attendre('interface de la montre prête', () => !/TrayInitialization|Starting/.test(adb('shell', 'dumpsys', 'window').split('\n').find(l => l.includes('mCurrentFocus')) || 'Starting'), 180);
    console.log('installation de ' + path.relative(root, apk));
    execFileSync(adbExe, ['-s', appareil(), 'install', '-r', apk], { stdio: 'inherit' });
    // Icône « Echoes 466 » : l'activité Natif (même nom de classe dans toutes les apps)
    const activite = args.includes('--natif') ? 'fr.maetzindustries.echoes.Natif' : 'android.app.NativeActivity';
    adb('shell', 'am', 'start', '-S', '-n', appli + '/' + activite);
    console.log('Echoes of Ashes lancé sur ' + AVD + ' (journal : adb -s ' + appareil() + ' logcat -s echoes)');
})().catch(e => { console.error(e.message); process.exit(1); });
