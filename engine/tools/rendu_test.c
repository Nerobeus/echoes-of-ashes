/*
 * Test du cœur sur PC : charge monde.pak, joue un scénario d'entrées, puis écrit la dernière image en BMP
 * (466 × 466, telle qu'envoyée à l'écran, interface comprise).
 *
 *   rendu_test monde.pak sortie.bmp [scénario]
 *
 * Scénario : étapes séparées par des virgules, une image chacune (touches relâchées à l'image suivante)
 *   menu, ok, back, up, down, left, right   touches
 *   tap:x:y                                 toucher bref en coordonnées natives (466)
 *   hold:x:y:n                              doigt posé n images
 *   drag:x:y0:y1:n                          doigt glissé de (x, y0) à (x, y1) en n images, puis levé
 *   rim:a0:a1                               glisser le long du bord, angles en degrés (0 = haut, sens horaire)
 *   pos:x:z:lacet                           placer Sera et la caméra
 *   vue:x:z:lacet[:zoom]                    caméra de scène posée en (x, z) (captures ; lacet au pas de 45°)
 *   circulation:n, croiser:sorte[:n[:f.bmp]], pietons:n, trafmax:n, passages
 *                                           trafic et piétons (voir circulation, croiser, pietons plus bas) ; nombre de
 *                                           véhicules du trafic ; passage de toutes les enceintes accordé
 *   portes, porte:k, entrer:k               lister les portes (PORT) ; Sera devant la porte k ; entrer par elle (forcée)
 *   ipos:x:z:lacet                          dans un intérieur : Sera en (x, z) du plan ; alarme : déclencher l'alarme
 *   escalier:k                              dans un intérieur : Sera au milieu de la zone de l'escalier k (passe au niveau voisin)
 *   pinch:d0:d1                             deux doigts autour du centre, écart de d0 à d1 px (zoom)
 *   wait:n                                  n images sans entrée
 *   carte, cartevue:x:z:s, carteetat        menu CARTE ouvert (carte.c) ; sa vue : centre (cases), zoom (pixels par case) ;
 *                                           son état (plan, tampon, places du cache, tuiles lues)
 *   brouillard, brouvoir:x:z:r, brouvide    brouillard de la carte du menu (carte.c) : blocs vus par carte ; vus autour de
 *                                           (x, z) jusqu'à r cases ; rien de vu
 *   carteobj                                objectifs de la carte du menu : quête suivie, puis les autres (script_objectifs)
 *   cartetele:x:z                           carte ouverte, un toucher en (x, z) du plan (cases) : pilote ou téléport
 *                                           (la boîte de choix ou le refus ; ok : TÉLÉPORTER, back : ANNULER)
 *   esvh:p                                  sauvegardes de la partie p réécrites en ESVH (sans l'arène ni le brouillard)
 *   mise:NOM                               mise en scène (game.c, MISE) jouée comme serveur/essai-mise.js de l'Atelier :
 *                                           acteur NOM, caméra, marches, fondu, secousse, course de Sera, fuite ; états
 *                                           aux mêmes instants (comparer avec la sortie de l'émulateur)
 *   police, recherche:n[:m], poursuite:n[:k], arreter, bmp:fichier
 *                                           police (police.c) : état ; niveau n imposé (motif m : 1 vol, 2 effraction) ;
 *                                           n images (k = 1 : gaz tenu), état toutes les 30 ; la scène de l'arrestation ;
 *                                           l'image en cours en BMP (captures en cours de scénario)
 *   fuir:n[:1]                              fuyard au volant (gaz tenu, conduite assistée ; :1 libre), n images au plus :
 *                                           semée, prise (combat contre la police) ou toujours recherchée, et après combien
 *   lire:n                                  n images, OK toutes les 12 tant qu'une réplique est ouverte
 *   repl:N[:T], replphase:p, repllots       répliques par zone (repliques.c) : N PNJ sans répliques à eux près de Sera,
 *                                           parlés T fois chacun (liste, redites, octets lus) ; phase p de l'histoire
 *                                           (drapeaux des phases de REPZ) ; lots, lot de la zone de Sera (paquet d'essai :
 *                                           tools/repliques_paquet.js)
 *   ennemis[:mot], evzones:x:z:r            fiches de combat (ENNE) ; zones du scénario autour de (x, z), enceintes (ENCE)
 *   course:n                                course sur la piste (PIST) : n images pilotées, classement
 *   essai:k:a                               course d'essai de la montre (eng_essai_course, SDX COURSE k a)
 *   carr:k, cpage:p                         mode course : la carrière k (ouverte ou créée), page p (0 menu, 1 carrière,
 *                                           2 coupe, 3 résultats, 4 fin de coupe, 5 garage, 6 course libre ; 13 RÉGLAGES)
 *   ccoupe:c, clibre:i, cref:i:k            course de la coupe c (la suivante), course libre sur le circuit i, tour de
 *                                           référence du circuit i dans la classe k (seul, piloté)
 *   cpilote:n                               course en cours : le joueur piloté comme un adversaire, n images au plus ;
 *                                           coût du rendu, classement, temps, meilleur tour ; puis la page des résultats
 *   cepr:k[:v], clibres:i:s, cdefi          étape 2 du mode course : épreuve k de la carrière (difficulté v) ; course libre
 *                                           sur le circuit i, épreuve s (0 course, 1 contre-la-montre, 2 tour rapide, 3
 *                                           élimination) ; défi du jour (date:j:m d'abord : la date de la montre)
 *   cterminer, crecommencer, cvalider       pause du tour rapide : terminer ; pause : recommencer ; CONTINUER des résultats
 *   crecomp:v, camelio:i:s, ccredits:n      duel gagné : son véhicule (1) ou les livrées (0) ; amélioration s du véhicule i ;
 *                                           crédits de la carrière ; cdebloquer:k : les coupes des classes 0 à k en or ;
 *                                           cetat : données (CARR), épreuves, carrière, dernier résultat, fantôme
 *   heure:h                                 heure du cycle jour / nuit (0 minuit, 0,5 midi)
 *   horloge:s                               heure réelle en secondes depuis minuit (montre : arc du bas, cycle RÉEL)
 *   pluie:x                                 pluie imposée d'intensité x (0 à 1 ; 0 : temps sec)
 *   cendre                                  pluie de cendre : intensité, PV de l'équipe, exemplaires présents et à l'abri
 *   piste:k                                 Sera au départ de la piste k (PIS2), piste active
 *   menucourse, organisateur                menu de course ; Sera devant l'organisateur (monde de course)
 *   taper:NNNN                              le code tapé dans SYSTÈME › CODE (6669 : le menu des véhicules offerts)
 *   vehicules, vehicule:MOT[:c], boutique[:n]
 *                                           véhicules proposés (planche, palettes, performances, prix) et le menu offert ;
 *                                           le modèle dont le nom contient MOT (couleur c), puis PRENDRE ; le menu à vendre
 *                                           (n crédits) ; garage : état du véhicule de Sera (garage.c) ; esvf:p : sauvegardes
 *                                           de la partie p réécrites en ESVF (sans le véhicule)
 *   parler:NOM, drapeau:k                   Sera devant le personnage NOM (puis « ok » : lui parler) ; lever le drapeau k
 *   finscript                               le script en cours s'arrête (celui du lancement : parler sans lui)
 *   pilote:n                                course en cours : n images pilotées (W tenu, vers l'axe), puis l'état
 *   boost                                   course en cours : le joueur accélère comme sur une plaque (halo de l'écran)
 *   musique:nom                             instruction « musique » du scénario (auto, aucune ou un nom de piste)
 *   pouces:g:v:n                            en véhicule : pouce gauche glissé de g px vers le haut (gaz), pouce
 *                                           droit de v px vers la droite (volant), tenus n images
 *   joy:dx:dy:n                             en véhicule, commandes JOYSTICK (gardées ensuite) : doigt posé au centre du
 *                                           joystick, glissé de dx, dy px (dy < 0 : vers le haut), tenu n images ;
 *                                           commandes:doubles | commandes:joystick : le réglage seul
 *   lac:n[:m], riviere:n[:m]                véhicule ECHOES_VEH (sinon une voiture) près de l'eau la plus proche, n images
 *                                           de gaz vers elle, puis m sans rien (150 ; gué, naufrage, sortie de Sera)
 *
 * ECHOES_VEH=NOM : véhicule de l'étape course: quand aucun n'est placé (carte de l'histoire).
 * ECHOES_APPS=1 : une application de plus (comme sur la montre) : écran titre à cinq entrées (bouton de la plateforme).
 * ECHOES_MUSIC=musique.bin : musiques lues dans ce fichier comme sur la carte SD ; ECHOES_WAV=sortie.wav : le son de
 * chaque image (24 kHz mono, comme le firmware) y est écrit, musique comprise.
 *   battle:k                                combat contre l'ennemi k de ENNE (-1 : la Sentinelle de l'épave)
 *   equipe:m                                équipe imposée (bit i : PARTY[i], comme l'instruction « equipe » : la bande,
 *                                           au combat les 4 premiers, imposés) ; libre : comme « equipe libre »
 *   recrues:m                               comme « rejoindre » de chacun (bit i : PARTY[i] ; 4 = JADE… 8 = AZUR) : dans la
 *                                           bande, au combat tant qu'il reste une place ; basculer:i : menu ÉQUIPE, le membre
 *                                           i au combat ou en réserve
 *   tour:m                                  combat : c'est au membre m de choisir (éventail de commandes)
 *   enc                                     rencontre tirée pour le terrain sous Sera (comme au bout de la marche)
 *   hp:k:v                                  PV du membre k de l'équipe à v (essai de la défaite, du KO)
 *   tenue:m:k                               le membre m porte la tenue k de TENU (-1 : la sienne)
 *   job:m:j                                 le membre m prend le job j de JOBS (comme l'instruction « job »)
 *   donner:i:n                              n exemplaires de plus de l'objet i dans le sac
 *   eqsac:p                                 un exemplaire de plus de chaque équipement du palier p (EQPL) dans le sac
 *   equip:m:s, equiptexte                   menu ÉQUIP. ouvert : membre m (-1 : la liste des membres), emplacement s (0 arme,
 *                                           1 armure, 2 accessoire ; -1 : ses emplacements) ; état du menu (lignes, écarts)
 *                                           ECHOES_ECRAN=540x1204 (compilé avec ENG_DYN=1) : plein écran du téléphone
 *   arme:m:i                                le membre m porte l'objet d'équipement i comme arme (255 : aucune)
 *   attaque:m                               combat : le membre m attaque l'ennemi (effet de son arme, sans dégâts)
 *   technique:m:k                           combat : le membre m lance sa technique k sur l'ennemi (cut-in d'abord s'il en a)
 *   cutt:t                                  combat : le cut-in en cours ramené à t secondes (capture)
 *   cutimg:k:p                              combat : le cut-in en cours montre l'image k de IMAG (p = 1 : paire face / dos k, k + 1)
 *   ennemi                                  combat : l'ennemi joue son tour (un sort, une attaque de son répertoire ENAT)
 *   eattaque:k, repertoire[:mot]            combat : l'ennemi joue l'attaque k de son répertoire (-1 : au tirage) ;
 *                                           attaques (ENAT) des ennemis dont le nom contient ce mot
 *   gagner                                  combat : l'ennemi tombe (puis le tableau de fin de combat)
 *   code:NNNN                               édition unique : le code du contenu 18+ (verrou18 avant et après) ; code:0 verrouille
 *   noms, apparence                         héroïne choisie (heroine:k d'abord) : ce qui s'affiche (noms échangés de
 *                                           phrases d'essai, des membres, la réplique en cours) ; la recrue de son nom en
 *                                           Sera (planches, auras, ses personnages : présence, planche, couleurs, ombre)
 *   dire:QUI:TEXTE                          réplique de QUI ouverte comme celle d'un script (TEXTE sans virgule)
 *   scenes[:k:i]                            illustration de chaque scène (scene_image) ; k:i : variante d'essai, la scène k
 *                                           prend l'image i avec l'héroïne choisie (comme la fin de SCNI)
 *   balayage                                combat : le tour de l'ennemi en cours devient un balayage de toute l'équipe
 *   fxt:t                                   combat : l'action en cours ramenée à t secondes (capture d'un effet)
 *   fx:k                                    combat : l'action en cours prend l'effet k (FX_IMPACT à FX_BALAYAGE ; couleur
 *                                           du premier type d'arme qui le porte, ou celle de l'effet puissant, sinon la couleur 1 du thème)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../core/eng.h"
#include "../core/internal.h"
#include "../core/audio.h"
#ifdef _WIN32
#include <windows.h>
static uint32_t clock_pc(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (uint32_t)(c.QuadPart * 1000000 / f.QuadPart); }
#else
static uint32_t clock_pc(void) { return (uint32_t)(clock() * 1000000.0 / CLOCKS_PER_SEC); }
#endif

/* Mémoire demandée par le moteur (ECHOES_MEM=1 : bilan après eng_init, 2 : et chaque réservation de 64 Ko et plus) :
   « lente » = PSRAM sur l'ESP32, « rapide » = SRAM interne s'il en reste, sinon PSRAM aussi.
   ECHOES_PSRAM=8011 : la carte simulée (Ko de PSRAM pour le moteur : 8 015 Ko au démarrage du firmware, moins ce qu'il
   prend avant eng_init ; relevé du 28/10/2026), 8 octets de gestion par bloc comme le tas de l'ESP-IDF ; ECHOES_SRAM :
   SRAM interne pour les réservations rapides (131 Ko par défaut : 143 libres avant eng_init, moins la réserve de 12 Ko
   de main.c). Au-delà, la réservation échoue, comme sur la carte. */
#define PATR_FILE_RT 0.9f   /* écart de la file d'une patrouille posée devant Sera (PATR_FILE de battle.c) */
static size_t mem_lente, mem_rapide, psram_pris, sram_pris;
static int en_jeu;   /* démarrage fini (eng_init, musique) : une réservation est une faute (01/11/2026 : tout au démarrage) */
static void *alloc_pc(size_t size, int fast) {
    if (en_jeu) printf("RÉSERVATION PENDANT LE JEU : %zu octets%s (tout doit l'être au démarrage)\n", size, fast ? " rapides" : "");
    const char *ps = getenv("ECHOES_PSRAM"), *ss = getenv("ECHOES_SRAM");
    if (ps) {
        const size_t psram = (size_t)atoi(ps) * 1024, sram = (size_t)(ss ? atoi(ss) : 131) * 1024;
        if (fast && sram_pris + size <= sram) sram_pris += size;
        else if (psram_pris + size + 8 <= psram) psram_pris += size + 8;
        else {
            if (getenv("ECHOES_MEM")) printf("  ÉCHEC : %zu Ko demandés, %zu Ko de PSRAM libres sur la carte simulée\n", size / 1024, (psram - psram_pris) / 1024);
            return NULL;
        }
    }
    if (fast) mem_rapide += size; else mem_lente += size;
    if (size >= 64 * 1024 && getenv("ECHOES_MEM") && getenv("ECHOES_MEM")[0] == '2') printf("  réserve %zu Ko%s (total lent %zu Ko)\n", size / 1024, fast ? " rapide" : "", mem_lente / 1024);
    return malloc(size);
}
static void montre_essai(void) { printf("retour à l'app montre (essai)\n"); }   /* ECHOES_APPS : la ligne MONTRE */
static int save_write_pc(int slot, const void *d, int n) {
    char name[32]; snprintf(name, sizeof name, "build/sauve%d.bin", slot);
    FILE *f = fopen(name, "wb"); if (!f) return -1;
    int w = (int)fwrite(d, 1, n, f); fclose(f); return w;
}
static int save_read_pc(int slot, void *d, int cap) {
    char name[32]; snprintf(name, sizeof name, "build/sauve%d.bin", slot);
    FILE *f = fopen(name, "rb"); if (!f) return -1;
    int r = (int)fread(d, 1, cap, f); fclose(f); return r;
}
static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

static FILE *wav_file;   /* ECHOES_WAV : son de chaque image */
static uint32_t wav_n;
static int frame_no;
#if ENG_DYN
/* Plein écran du téléphone (compilé avec -DENG_DYN=1 -DENG_ROUND=0) : image de ECHOES_ECRAN (540x1204 par défaut, celle
   du XCover 7), marges des barres du système ECHOES_MARGES (100:144) ; 3D puis interface, bande par bande */
static uint16_t img[ENG_W * ENG_H_PX];
#else
static uint16_t img[ENG_SCREEN * ENG_SCREEN], band[ENG_SCREEN * ENG_NATIVE_BAND];

/* Plus grande corde du cercle dans les lignes [y, y + h), colonnes paires (même calcul que le firmware) */
static void band_window(int y, int h, int *x0, int *x1) {
    float r = ENG_SCREEN / 2.0f, best = 0;
    for (int j = y; j < y + h; j++) { float dy = j + 0.5f - r, v = r * r - dy * dy; if (v > best) best = v; }
    float hw = sqrtf(best);
    int a = (int)floorf(r - hw) & ~1, b = ((int)ceilf(r + hw) + 1) & ~1;
    *x0 = a < 0 ? 0 : a; *x1 = b > ENG_SCREEN ? ENG_SCREEN : b;
}
#endif

/* ECHOES_PLANCHES=1 : places du cache des planches (carte SD) servies à l'image ou à la précédente, par classe de taille,
   au plus fort sur tout le scénario (classes et nombres de places : ST_SIZE, ST_COUNT de world.c, recopiés ici) */
static const int PL_COUNT_E[6] = { 24, 16, 8, 16, 6, 2 }, PL_COUNT_I[6] = { 12, 8, 12, 8, 6, 14 };   /* Echoes, île (core/ile.c) */
#define PL_COUNT (E.w.ile[0] ? PL_COUNT_I : PL_COUNT_E)
static int pl_pic[6];
static void planches_mesure(void) {
    if (!E.w.st_who || !getenv("ECHOES_PLANCHES")) return;
    for (int c = 0, i0 = 0; c < 6; i0 += PL_COUNT[c], c++) {
        int n = 0;
        for (int i = i0; i < i0 + PL_COUNT[c] && i < E.w.st_nsl; i++) if (E.w.st_who[i] >= 0 && E.w.st_used[i] + 1 >= E.w.sframe) n++;
        if (n > pl_pic[c]) pl_pic[c] = n;
    }
}

static float fx_fix = -1;   /* fxt : instant imposé à l'action en cours jusqu'à l'étape suivante (images de mesure comprises) */
static uint32_t us_build;   /* préparation de la dernière image (eng_build_frame : cœur 0 sur la carte) */
static uint32_t us_update;  /* jeu de la dernière image (eng_update) */
unsigned long eng_px_mel;
unsigned long eng_hud_us[8];
unsigned long eng_ui_us[8], eng_ui_n[8];
uint32_t (*eng_compte_us)(void);
unsigned long eng_bat_ecartes, eng_bat_vus, eng_bat_polys, eng_bat_polys_sautes;
unsigned long eng_nsous, eng_nsous_av, eng_px_aff, eng_px_persp, eng_px_mini, eng_px_lent, eng_px_trans;   /* sous-plages du brouillard lissé, et ce qu'en faisait le découpage d'avant (render.c, -DENG_COMPTE) */
static float cam_yaw = 3.14159f;
static void frame(const eng_input_t *in) {
    const uint32_t tu0 = clock_pc();
    eng_update(1 / 30.0f, in);
    us_update = clock_pc() - tu0;
    if (fx_fix >= 0 && BT.phase == BP_ANIM) BT.an.t0 = E.clock - fx_fix;
    if (wav_file) {   /* une image de son : 800 échantillons à 24 kHz */
        int16_t s[800];
        eng_audio_mix(s, 800, 24000);
        fwrite(s, 2, 800, wav_file); wav_n += 800;
    }
    const uint32_t tb0 = clock_pc();
    eng_build_frame(frame_no++ / 30.0f);
    us_build = clock_pc() - tb0;
    planches_mesure();
    eng_present();
#if ENG_DYN
    int fw, fh;
    eng_screen_size(&fw, &fh);
    for (int y = 0; y < fh; y += ENG_NATIVE_BAND) {
        const int h = fh - y < ENG_NATIVE_BAND ? fh - y : ENG_NATIVE_BAND;
        eng_render_band_3d(0, y, h, img + (size_t)y * fw, NULL);
        eng_render_band_ui(0, y, h, img + (size_t)y * fw);
    }
#else
    for (int y = 0; y < ENG_SCREEN; y += ENG_NATIVE_BAND) {
        int h = ENG_SCREEN - y < ENG_NATIVE_BAND ? ENG_SCREEN - y : ENG_NATIVE_BAND;
        /* Bande rognée au cercle, comme sur la carte, puis replacée dans l'image */
        int x0, x1;
        band_window(y, h, &x0, &x1);
        eng_render_band_on((y / ENG_NATIVE_BAND) & 1, y, h, band, x0, x1);   /* bandes alternées entre les deux contextes, comme sur la carte */
        for (int j = 0; j < h; j++) {
            memset(img + (y + j) * ENG_SCREEN, 0, ENG_SCREEN * 2);
            memcpy(img + (y + j) * ENG_SCREEN + x0, band + j * (x1 - x0), (size_t)(x1 - x0) * 2);
        }
    }
#endif
}

/* Dernière image en BMP 24 bits (466 × 466, lignes complétées à 4 octets) ; 1 : échec */
static int ecrire_bmp(const char *nom) {
    FILE *o = fopen(nom, "wb");
    if (!o) { perror(nom); return 1; }
#if ENG_DYN
    int W, HH;
    eng_screen_size(&W, &HH);
#else
    const int W = ENG_SCREEN, HH = ENG_SCREEN;
#endif
    uint32_t row = (W * 3 + 3) & ~3u, size = 54 + row * HH;
    fputc('B', o); fputc('M', o); put32(o, size); put32(o, 0); put32(o, 54);
    put32(o, 40); put32(o, W); put32(o, HH); put16(o, 1); put16(o, 24); put32(o, 0); put32(o, row * HH);
    put32(o, 2835); put32(o, 2835); put32(o, 0); put32(o, 0);
    for (int y = HH - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            uint16_t c = img[y * W + x];
            int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            fputc((b << 3) | (b >> 2), o); fputc((g << 2) | (g >> 4), o); fputc((r << 3) | (r >> 2), o);
        }
        for (uint32_t k = W * 3; k < row; k++) fputc(0, o);
    }
    fclose(o);
    return 0;
}
/* État de la police (police.c) : recherche, voitures poursuivantes, agents, arrestation */
/* Conduite assistée et pilote automatique (pilote.c) : Sera monte dans une voiture posée là où elle est, tournée selon dir
   (ECHOES_VEH : ce véhicule) ; 0 : aucune */
static int monter_voiture(float dir) {
    int p = -1;
    if (E.ride >= 0) return 1;
    for (int q = 0; q < E.w.npersos && p < 0; q++)
        if ((E.w.persos[q].flags & PERS_VEHICULE) && (getenv("ECHOES_VEH") ? !strcmp(E.w.persos[q].nom, getenv("ECHOES_VEH")) : VEH_TYPE(E.w.persos[q].flags) == VEH_VOITURE)) p = q;
    if (p < 0) return 0;
    const int k = vehicle_spawn(p, E.sera.x, E.sera.z, dir);
    return k >= 0 && vehicle_board(k);
}
/* Trajectoire du dernier pilote automatique (champs:fichier.ppm la dessine sur la grille grossière) */
#define TRAJ_MAX 20000
static float traj[TRAJ_MAX][2];
static int n_traj;
/* Sur la route : une case de route ou de rue à moins de 0,6 case du centre du véhicule (voie de droite d'un axe étroit) */
static int sur_route(float x, float z) {
    for (int k = 0; k < 5; k++) {
        const float px = x + (k == 1 ? 0.6f : k == 2 ? -0.6f : 0), pz = z + (k == 3 ? 0.6f : k == 4 ? -0.6f : 0);
        const int i = (int)floorf(px), j = (int)floorf(pz);
        const chunk_t *c = chunk_of_cell(i, j);
        if (c && (c->flags[cell_local(i, j)] & (F_ROAD | F_STREET))) return 1;
    }
    return 0;
}
/* Véhicule de Sera (garage.c) : son personnage, son exemplaire, conduit ou non, volé ou non, recherche, places */
static void garage_etat(void) {
    int k = -1, anciens = 0;
    for (int i = E.nnpc_base; i < E.nnpc; i++) { if (E.npcs[i].mien == 1) k = i; anciens += E.npcs[i].mien == 2; }
    const int p = garage_perso();
    printf("garage : %s (%d), exemplaire %d (palette %d, volé %d, en %.2f %.2f), anciens à retirer %d, conduit %d%s, recherche %d, Sera %.2f %.2f (carte %d), crédits %d, exemplaires %d / %d"
           " ; script %d, mise %d, fuite %d, alarme %d, dedans %d\n",
           p >= 0 ? E.w.persos[p].nom : "aucun", p, k, k >= 0 ? E.npcs[k].var : -1, k >= 0 ? E.npcs[k].vole : -1, k >= 0 ? E.npcs[k].x : 0, k >= 0 ? E.npcs[k].z : 0,
           anciens, E.ride, E.ride >= 0 && E.ride == k ? " (le sien)" : "", POL.niveau, E.sera.x, E.sera.z, E.w.carte, E.credits, E.nnpc, E.npc_cap,
           script_running(), MISE.cam_on * 4 + MISE.sera_on * 2 + police_bloque(), MISE.fuite_on, E.alarme.on, E.in.on);
}
static void police_etat(const char *quand) {
    static const char *MODE[4] = { "réseau", "droit", "ralliement", "garée" }, *ETAT[3] = { "chasse", "place", "départ" };
    printf("%s police : niveau %d, motif %d, %s, vue %d, semer %.1f / %.0f s, arrestation %d (%.1f s), Sera %s en %.1f %.1f, crédits %d\n", quand,
           POL.niveau, POL.motif, POL.clan ? "milice" : "Ordre", POL.vue, POL.semer, police_semer_s(), POL.arr, POL.arr_tt,
           E.ride >= 0 ? (E.npcs[E.ride].vole ? "dans un véhicule volé" : "en véhicule") : "à pied", E.sera.x, E.sera.z, E.credits);
    if (POL.arr >= ARR_CELLULE || (E.in.on && POL.prison >= 0 && POL.arr))
        printf("    prison : %s (bâtiment %d), dedans %d, étage %d, cellule %.1f %.1f - %.1f %.1f (bloc des cellules %d), Sera en %.2f %.2f, garde %d, nuits %d, heure %.2f h\n",
               POL.prison >= 0 ? E.w.bats[POL.prison].nom : "-", POL.prison, E.in.on, E.in.etage, POL.cel[0] - IO, POL.cel[1] - IO, POL.cel[2] - IO, POL.cel[3] - IO,
               E.in.ncel, E.sera.x - IO, E.sera.z - IO, POL.garde, POL.nuits, E.heure * 24);
    for (int k = 0; k < TRAFFIC_MAX; k++) {
        const traffic_t *t = &E.traffic[k];
        if (t->on && t->poursuite) printf("    voiture %d : %s, %s (%s), v %.1f, à %.1f cases, en %.1f %.1f, case %d %d -> %d %d\n", k, E.w.persos[t->perso].nom, MODE[t->mode & 3], t->lane ? "axe" : "rue", t->v, hypotf(t->x - E.sera.x, t->z - E.sera.z), t->x, t->z, t->ci, t->cj, t->ni, t->nj);
    }
    for (int k = 0; k < POL_AGENTS; k++) {
        const agent_t *a = &POL.ag[k];
        if (a->on) printf("    agent %d : %s, %s, allure %d, à %.1f cases\n", k, E.w.persos[a->perso].nom, ETAT[a->etat % 3], a->allure, hypotf(a->x - E.sera.x, a->z - E.sera.z));
    }
    if (POL.repit > 0 || POL.alerte > 0 || POL.enc) printf("    répit %.1f s, alerte %.1f s, interception %d (%.1f s)\n", POL.repit, POL.alerte, POL.enc, POL.enc_t);
    if (BT.on) {   /* combat contre les forces de l'ordre (police_combat) */
        printf("    combat%s : phase %d, niveau %d (équipe %d), %d ennemis :", BT.police ? " contre la police" : "", BT.phase, BT.lvl, E.level, BT.nen);
        for (int k = 0; k < BT.nen; k++) printf(" %s (fiche %s, PV %d/%d, ATQ %d, VIT %d)", BT.en[k].name, E.w.persos[E.w.enemies[BT.en[k].ek].perso].nom, BT.en[k].hp, BT.en[k].max, BT.en[k].atk, BT.en[k].spd);
        printf("\n");
    }
}

static FILE *scn_file, *mus_file, *sd_file, *intr_file;
static int intr_read_pc(uint32_t off, void *dst, uint32_t len) {
    return fseek(intr_file, (long)off, SEEK_SET) || fread(dst, 1, len, intr_file) != len ? -1 : 0;
}
static uint32_t sd_lus;   /* octets lus dans monde.pak (mode carte SD) */
static int sd_read_pc(uint32_t off, void *dst, uint32_t len) {
    sd_lus += len;
    return fseek(sd_file, (long)off, SEEK_SET) || fread(dst, 1, len, sd_file) != len ? -1 : 0;
}
static int mus_read_pc(uint32_t off, void *dst, uint32_t len) {
    return fseek(mus_file, (long)off, SEEK_SET) || fread(dst, 1, len, mus_file) != len ? -1 : 0;
}
static int scn_read_pc(uint32_t off, void *dst, uint32_t len) {
    return fseek(scn_file, (long)off, SEEK_SET) || fread(dst, 1, len, scn_file) != len ? -1 : 0;
}

/* ---------- Circulation (01/11/2026) : essais du trafic ----------
   circulation:n        n images : passages d'un axe à une rue et retour, demi-tours (près d'une rue : à une jonction),
                        arrêts, temps de eng_update (moyenne, maximum)
   croiser:sorte[:n[:fichier.bmp]]   deux véhicules du trafic face à face, 12 cases l'un de l'autre : sorte chemin (axe de
                        moins de 1,5 case de large, sinon le plus proche : chemins de 2 cases depuis le 01/11/2026), route
                        (1,5 et plus) ou rue (rue droite de 3 cases de large), la plus
                        proche de Sera ; n images (300) : se croisent-ils, écart de côté au croisement, arrêts,
                        demi-tours ; l'image du croisement en BMP
   Compilé avec -DAVANT : pour le moteur d'avant la circulation unique (sans les points de voie des rues) */
static int rt_rue(int i, int j) {
    if (i < 0 || j < 0 || i >= E.w.n || j >= E.w.n) return 0;
    const chunk_t *c = chunk_of_cell(i, j);
    return c && (c->flags[cell_local(i, j)] & F_STREET);
}
static int rt_rue_proche(float x, float z, float r) {
    for (int dj = -(int)r; dj <= (int)r; dj++) for (int di = -(int)r; di <= (int)r; di++)
        if (di * di + dj * dj <= r * r && rt_rue((int)floorf(x) + di, (int)floorf(z) + dj)) return 1;
    return 0;
}
static float rt_rpt(const route_t *r, int i, int c) { float v; memcpy(&v, r->p + i * 8 + c * 4, 4); return v; }
static int perso_trafic(void) {   /* le véhicule du trafic le plus lent (le plus long à freiner devant un autre : il s'y arrête) */
    int best = -1;
    float bv = 1e9f;
    for (int p = 0; p < E.w.npersos; p++) {
        const int f = E.w.persos[p].flags, ty = VEH_TYPE(f);
        float v, a;
        if (!(f & PERS_VEHICULE) || !(f & PERS_TRAFIC) || ty == VEH_BATEAU || ty == VEH_VAISSEAU || E.w.persos[p].sheet < 0) continue;
        veh_params(p, &v, &a);
        if (v < bv) { bv = v; best = p; }
    }
    if (best >= 0) printf("véhicule d'essai : %s (%.1f cases/s)\n", E.w.persos[best].nom, bv);
    return best;
}
static void circulation(int n) {
    const eng_input_t rien = { 0 };
    int pon[TRAFFIC_MAX] = { 0 }, pl[TRAFFIC_MAX] = { 0 }, ps[TRAFFIC_MAX] = { 0 }, pr[TRAFFIC_MAX] = { 0 };
    float pd[TRAFFIC_MAX] = { 0 }, pv[TRAFFIC_MAX] = { 0 }, px[TRAFFIC_MAX] = { 0 }, pz[TRAFFIC_MAX] = { 0 };
    int rl = 0, lr = 0, demi = 0, demi_j = 0, arrets = 0, vus = 0, rue_im = 0, lignes = 0;
    double tot = 0;
    uint32_t mx = 0;
    for (int f = 0; f < n; f++) {
        const uint32_t a0 = clock_pc();
        eng_update(1 / 30.0f, &rien);
        const uint32_t du = clock_pc() - a0;
        tot += du; if (du > mx) mx = du;
        if (getenv("ECHOES_CIRC_TRACE") && du > 2000) printf("  image %d : eng_update %u µs\n", f, (unsigned)du);
        for (int k = 0; k < TRAFFIC_MAX; k++) {
            const traffic_t *t = &E.traffic[k];
            if (t->on && pon[k] && hypotf(t->x - px[k], t->z - pz[k]) < 1.5f && !t->poursuite) {
                vus++; rue_im += !t->lane;
                if (pl[k] && !t->lane) lr++;
                if (!pl[k] && t->lane) rl++;
                int dt = 0;
                if (pl[k] && t->lane && t->road == pr[k] && t->sgn != ps[k]) dt = 1;
                if (!pl[k] && !t->lane && cosf(t->dir - pd[k]) < -0.7f) dt = 1;
                if (dt) {
                    const int j = rt_rue_proche(t->x, t->z, 3.5f);
                    demi++; demi_j += j;
                    if (lignes++ < 12) printf("  demi-tour du trafic %d en %.1f %.1f (%s)%s%s\n", k, t->x, t->z, t->lane ? "axe" : "rue", j ? ", une rue à moins de 3,5 cases" : "", pv[k] < 0.05f ? ", bloqué" : ", impasse");
                }
                if (t->v < 0.05f && pv[k] >= 0.05f) arrets++;
            }
            pon[k] = t->on; pl[k] = t->lane; ps[k] = t->sgn; pr[k] = t->road; pd[k] = t->dir; pv[k] = t->v; px[k] = t->x; pz[k] = t->z;
        }
    }
    printf("circulation : %d images, %d véhicules-images (%d dans les rues) ; axe -> rue %d, rue -> axe %d ; demi-tours %d (dont %d près d'une rue) ; arrêts %d ; eng_update %.0f µs en moyenne, %u au plus\n",
           n, vus, rue_im, lr, rl, demi, demi_j, arrets, n ? tot / n : 0, (unsigned)mx);
}
/* pietons:n : n images ; les exemplaires (ni véhicule ni objet) à moins de 24 cases de Sera : places sur la chaussée
   (route ou rue) au début et à la fin, images sur la chaussée (en marche : une traversée ; arrêtés : plantés) */
static int rt_chaussee(float x, float z) {
    const int i = (int)floorf(x), j = (int)floorf(z);
    if (i < 0 || j < 0 || i >= E.w.n || j >= E.w.n) return 0;
    const chunk_t *c = chunk_of_cell(i, j);
    return c && (c->flags[cell_local(i, j)] & (F_ROAD | F_STREET));
}
static int rt_pieton(const npc_t *n) {
    const perso_t *pe = &E.w.persos[n->perso];
    return npc_present(n) && !(pe->flags & PERS_VEHICULE) && !PERS_EST_OBJET(pe->flags) && fabsf(n->x - E.sera.x) < 24 && fabsf(n->z - E.sera.z) < 24;
}
static void pietons(int n) {
    const eng_input_t rien = { 0 };
    int p0 = 0, p1 = 0, vus = 0, sur = 0, plantes = 0, np = 0, lignes = 0;
    for (int k = 0; k < E.nnpc; k++) if (rt_pieton(&E.npcs[k])) { np++; if (rt_chaussee(E.npcs[k].hx, E.npcs[k].hz)) p0++; }
    for (int f = 0; f < n; f++) {
        frame(&rien);
        for (int k = 0; k < E.nnpc; k++) {
            const npc_t *q = &E.npcs[k];
            if (!rt_pieton(q)) continue;
            vus++;
            if (!rt_chaussee(q->x, q->z)) continue;
            sur++;
            if (getenv("ECHOES_PIETONS_TRACE") && f % 30 == 0) printf("  image %d : %s en %.2f %.2f (place %.1f %.1f, cible %.1f %.1f)%s\n", f, E.w.persos[q->perso].nom, q->x, q->z, q->hx, q->hz, q->tx, q->tz, q->walking ? ", marche" : "");
            static uint8_t dit[MAX_NPCS + 64];
            if (!q->walking) {
                plantes++;
                if (k < MAX_NPCS + 64 && !dit[k] && lignes++ < 12) {
                    dit[k] = 1;
                    printf("  planté sur la chaussée : %s en %.1f %.1f (place %.1f %.1f)%s%s%s, attente %.1f, Sera en %.1f %.1f\n", E.w.persos[q->perso].nom, q->x, q->z, q->hx, q->hz, q->chef ? ", suiveur" : "", q->meneur ? ", meneur" : "",
                           (E.w.persos[q->perso].flags & PERS_FLANE) ? ", flâne" : ", immobile", q->wait, E.sera.x, E.sera.z);
                }
            }
        }
    }
    for (int k = 0; k < E.nnpc; k++) if (rt_pieton(&E.npcs[k]) && rt_chaussee(E.npcs[k].hx, E.npcs[k].hz)) {
        p1++;
        if (lignes++ < 16) printf("  place sur la chaussée : %s en %.1f %.1f\n", E.w.persos[E.npcs[k].perso].nom, E.npcs[k].hx, E.npcs[k].hz);
    }
    printf("piétons : %d exemplaires autour de Sera ; places sur la chaussée %d au début, %d après %d images ; %d images sur la chaussée sur %d (%.2f %%), dont %d arrêtés\n",
           np, p0, p1, n, sur, vus, vus ? 100.0 * sur / vus : 0, plantes);
}
/* repl:N[:T] : répliques génériques (repliques.c ; paquet sans REPZ : l'ancienne liste au hasard) de N PNJ sans répliques à
   eux, les plus proches de Sera et présents (80 cases au plus), parlés T fois chacun à la suite (T tours) ; liste, puis
   redites : réplique déjà entendue pendant l'essai, et entendue parmi les 10 précédentes (deux voisins) */
static void repl_essai(int n, int tours) {
    enum { RN = 512, VU = 4096 };
    static int ks[RN]; static float ds[RN]; static char vus[VU][112]; static int der[10];
    int m = 0, nv = 0, redites = 0, voisins = 0, paroles = 0;
    for (int k = 0; k < E.nnpc && m < RN; k++) {
        const npc_t *q = &E.npcs[k]; const perso_t *pe = &E.w.persos[q->perso];
        if (pe->nlines || (pe->flags & PERS_VEHICULE) || PERS_EST_OBJET(pe->flags) || !npc_present(q)) continue;
        const float d = hypotf(q->x - E.sera.x, q->z - E.sera.z);
        if (d <= 80) { ks[m] = k; ds[m++] = d; }
    }
    for (int i = 0; i < m; i++) for (int j = i + 1; j < m; j++) if (ds[j] < ds[i]) { float t = ds[i]; ds[i] = ds[j]; ds[j] = t; int u = ks[i]; ks[i] = ks[j]; ks[j] = u; }
    if (n > m) n = m;
    const int z = repl_zone();
    printf("répliques : %d PNJ sans répliques à eux près de Sera (%.0f %.0f), %d pris, zone %s, phase %d%s\n", m, E.sera.x, E.sera.z, n,
           z >= 0 ? repl_lot_nom(z) : "(REPL au hasard)", repl_phase(), verrou18() ? ", 18+ verrouillé" : "");
    for (int i = 0; i < 10; i++) der[i] = -1;
    for (int t = 0; t < tours; t++) for (int i = 0; i < n; i++) {
        char out[256];
        const int k = ks[i], metier = int_metier_pnj(k);
        if (!repl_dire(k, metier, out, sizeof out)) snprintf(out, sizeof out, "…");
        int v = -1, rep = 0;
        for (int a = 0; a < nv; a++) if (!strncmp(vus[a], out, sizeof vus[a] - 1)) { v = a; break; }
        if (v >= 0) { rep = 1; redites++; for (int a = 0; a < 10; a++) if (der[a] == v) { voisins++; rep = 2; break; } }
        else if (nv < VU) { snprintf(vus[nv], sizeof vus[nv], "%s", out); v = nv++; }
        der[paroles % 10] = v; paroles++;
        printf("  %3d. %-26.26s %5.1f g%-5u %s%s\n", paroles, pnj_nom(k), ds[i], (unsigned)E.npcs[k].graine, out, rep == 2 ? "   [REDITE PROCHE]" : rep ? "   [redite]" : "");
    }
    int lots, lot; uint32_t tampon, lus, lectures;
    repl_etat(&lots, &lot, &tampon, &lus, &lectures);
    printf("répliques : %d paroles, %d différentes, %d redites (%.1f %%), dont %d parmi les 10 précédentes ; lots %d, tampon %u octets, %u octets lus en %u lectures\n",
           paroles, nv, redites, paroles ? 100.0 * redites / paroles : 0, voisins, lots, (unsigned)tampon, (unsigned)lus, (unsigned)lectures);
}
static void croiser(const char *arg) {
    char sorte[16] = "", bmp[200] = "";
    int n = 300;
    sscanf(arg, "%15[^:]:%d:%199s", sorte, &n, bmp);
    const int p = perso_trafic(), rue = !strcmp(sorte, "rue"), chemin = !strcmp(sorte, "chemin");
    if (p < 0) { printf("croiser : aucun véhicule du trafic\n"); return; }
    for (int k = 0; k < TRAFFIC_MAX; k++) E.traffic[k].on = 0;
    E.w.traf_max = 2;   /* pas d'autre véhicule */
    traffic_t *A = &E.traffic[0], *B = &E.traffic[1];
    memset(A, 0, sizeof *A); memset(B, 0, sizeof *B);
    float vmax, acc, ux = 0, uz = 0, mx = 0, mz = 0, larg = 0;
    veh_params(p, &vmax, &acc);
    if (rue) {   /* rue droite sur 14 cases, de même largeur, la plus proche de Sera */
        int bi = 0, bj = 0, bdi = 0, bdj = 0;
        float bd = 1e9f;
        for (int dj = -36; dj <= 36; dj++) for (int di = -36; di <= 36; di++) {
            const int i = (int)E.sera.x + di, j = (int)E.sera.z + dj;
            const float d = hypotf((float)di, (float)dj);
            if (d >= bd || !rt_rue(i, j)) continue;
            for (int a = 0; a < 4; a++) {
                const int ddi = a == 0 ? 1 : a == 1 ? -1 : 0, ddj = a == 2 ? 1 : a == 3 ? -1 : 0;
                int ok = 1, w0 = -1;   /* 14 cases de rue de suite, de même largeur en travers (5 au plus), Sera au milieu */
                for (int s = 0; s < 14 && ok; s++) {
                    const int ci = i + ddi * s, cj = j + ddj * s;
                    int wl = 0, wr = 0;
                    while (wl < 3 && rt_rue(ci - ddj * (wl + 1), cj - ddi * (wl + 1))) wl++;
                    while (wr < 3 && rt_rue(ci + ddj * (wr + 1), cj + ddi * (wr + 1))) wr++;
                    ok = rt_rue(ci, cj) && wl + wr + 1 <= 5 && (w0 < 0 || w0 == wl + wr + 1) && (wl == wr || wl + wr == 0 || abs(wl - wr) == 1);
                    if (w0 < 0) w0 = wl + wr + 1;
                    if (ok && wl != wr && wl + wr) ok = 0;   /* sur l'axe de la rue (largeur impaire) */
                }
                if (ok) { bd = d; bi = i; bj = j; bdi = ddi; bdj = ddj; larg = (float)w0; break; }
            }
        }
        if (bd > 1e8f) { printf("croiser : pas de rue droite près de Sera\n"); return; }
        A->perso = B->perso = p;
#ifdef AVANT
        A->ci = bi; A->cj = bj; A->ni = bi + bdi; A->nj = bj + bdj; A->x = bi + 0.5f; A->z = bj + 0.5f;
        B->ci = bi + 12 * bdi; B->cj = bj + 12 * bdj; B->ni = B->ci - bdi; B->nj = B->cj - bdj; B->x = B->ci + 0.5f; B->z = B->cj + 0.5f;
#else
        traffic_rue_poser(A, bi, bj, bi + bdi, bj + bdj);
        traffic_rue_poser(B, bi + 12 * bdi, bj + 12 * bdj, bi + 11 * bdi, bj + 11 * bdj);
#endif
        A->dir = atan2f((float)bdi, (float)bdj); B->dir = atan2f((float)-bdi, (float)-bdj);
        ux = (float)bdi; uz = (float)bdj; mx = bi + 0.5f + 6 * bdi; mz = bj + 0.5f + 6 * bdj;
        A->v = vmax * 0.45f * 0.6f; B->v = 0;   /* B part de l'arrêt */
        printf("croiser : rue de (%d, %d) vers (%d, %d), %.0f case(s) de large\n", bi, bj, bi + 12 * bdi, bj + 12 * bdj, larg);
    } else {   /* axe de la largeur voulue le plus proche de Sera, 12 cases devant le point choisi ; chemin : moins de 1,5
                  case (paquets d'avant le 01/11/2026), sinon l'axe le plus proche quelle que soit sa largeur (les chemins
                  ont 2 cases depuis, comme la route : le lieu choisi décide) */
        int bq = -1, ba = 0, bb = 0;
        float bd = 1e9f;
        for (int q = 0; q < 2 * E.w.nroutes; q++) {
            if (q == E.w.nroutes && bq >= 0) break;
            const route_t *r = &E.w.routes[q % E.w.nroutes];
            if (q < E.w.nroutes && chemin != (r->hw * 2 < 1.5f)) continue;
            for (int a = 0; a + 1 < r->n; a++) {
                const float d = hypotf(rt_rpt(r, a, 0) - E.sera.x, rt_rpt(r, a, 1) - E.sera.z);
                if (d >= bd) continue;
                float L = 0;
                int b = a;
                while (b + 1 < r->n && L < 12) { L += hypotf(rt_rpt(r, b + 1, 0) - rt_rpt(r, b, 0), rt_rpt(r, b + 1, 1) - rt_rpt(r, b, 1)); b++; }
                if (L >= 12) { bd = d; bq = q % E.w.nroutes; ba = a; bb = b; }
            }
        }
        if (bq < 0) { printf("croiser : pas d'axe de %s\n", sorte); return; }
        const route_t *r = &E.w.routes[bq];
        A->lane = B->lane = 1; A->road = B->road = bq; A->perso = B->perso = p;
        A->seg = ba; A->sgn = 1; A->k = 0;
        B->seg = bb - 1; B->sgn = -1; B->k = 0;
        A->v = vmax * 0.45f; B->v = 0; larg = r->hw * 2;
        const float ax = rt_rpt(r, ba, 0), az = rt_rpt(r, ba, 1), bx = rt_rpt(r, bb, 0), bz = rt_rpt(r, bb, 1), l = hypotf(bx - ax, bz - az);
        ux = (bx - ax) / l; uz = (bz - az) / l; mx = (ax + bx) / 2; mz = (az + bz) / 2;
        A->x = ax; A->z = az; B->x = bx; B->z = bz;
        printf("croiser : axe %d (%.1f case de large), de (%.1f, %.1f) à (%.1f, %.1f)\n", bq, larg, ax, az, bx, bz);
    }
    A->on = B->on = 1;
    /* Sera sur le côté, hors de la chaussée, la caméra vers le milieu */
    for (float s = 4; s <= 10; s += 1) {
        float sx = mx + uz * s, sz = mz - ux * s;
        if (rt_rue_proche(sx, sz, 0.5f)) { sx = mx - uz * s; sz = mz + ux * s; }
        if (!rt_rue_proche(sx, sz, 0.5f)) { E.sera.x = sx; E.sera.z = sz; break; }
    }
    E.yaw = atan2f(mx - E.sera.x, mz - E.sera.z);
    mise_camera(0, mx + ux * 4, mz + uz * 4, atan2f(ux, uz), 1.6f, 0.05f);   /* caméra de scène vers le croisement (B attend), dans l'axe de la voie */
    const eng_input_t rien = { 0 };
    int croise = 0, arretA = 0, arretB = 0, demi = 0, sA = A->sgn, sB = B->sgn, f = 0, capt = 0, attend = 1;
    float lat = 0, dmin = 1e9f, dA = A->dir, dB = B->dir, serre = 0;
    for (; f < n && A->on && B->on; f++) {
        frame(&rien);
        if (attend && hypotf(B->x - A->x, B->z - A->z) > 2.2f && f < 150) { B->v = 0; B->stuck = 0; }   /* B attend (un piéton devant lui) qu'A arrive en face */
        else attend = 0;
        const float dx = B->x - A->x, dz = B->z - A->z, pr = dx * ux + dz * uz;
        if (hypotf(dx, dz) < dmin) dmin = hypotf(dx, dz);
        if (!croise && pr < 0) { croise = f + 1; lat = fabsf(dx * uz - dz * ux); }
        if (!capt && bmp[0] && (pr < 0.6f || (!attend && hypotf(dx, dz) < 2.5f && A->v < 0.05f && B->v < 0.05f))) { capt = 1; if (!ecrire_bmp(bmp)) printf("image : %s\n", bmp); }   /* au croisement, ou face à face arrêtés */
        if (getenv("ECHOES_CROISER_TRACE") && hypotf(dx, dz) < 3) printf("  %d : A %.2f %.2f v %.2f dir %.2f, B %.2f %.2f v %.2f dir %.2f\n", f, A->x, A->z, A->v, A->dir, B->x, B->z, B->v, B->dir);
        const int pres = !croise || f < croise + 45;   /* autour du croisement (pas le bout de la rue, bien après) */
        arretA += pres && A->v < 0.05f; arretB += pres && B->v < 0.05f;
        if (pres && ((A->lane && A->sgn != sA) || (!A->lane && cosf(A->dir - dA) < -0.7f))) demi++;
        if (pres && ((B->lane && B->sgn != sB) || (!B->lane && cosf(B->dir - dB) < -0.7f))) demi++;
        sA = A->sgn; sB = B->sgn; dA = A->dir; dB = B->dir;
#ifndef AVANT
        if (A->serre > serre) serre = A->serre;
#endif
    }
    printf("croiser (%s, %.1f case) : %s ; écart de côté au croisement %.2f case, distance mini %.2f ; images arrêtées A %d, B %d ; demi-tours %d ; serré sur le bas-côté %.2f ; %d images\n",
           sorte, larg, croise ? "ils se croisent" : "ILS NE SE CROISENT PAS", lat, dmin, arretA, arretB, demi, serre, f);
}

#define audit_dans_bat point_dans_bat
/* (l'ancienne fonction du seul chunk du point) */
static int audit_dans_bat0(float x, float y, float z) {
    const chunk_t *c = chunk_of_cell((int)floorf(x), (int)floorf(z));
    if (!c) return -1;
    for (uint32_t b = 0; b < c->nbld; b++) {
        const pak_building_t *pb = &c->bld[b];
        if (hypotf(x - pb->cx, z - pb->cz) > pb->r + 1) continue;
        float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f, y1 = -1e9f;
        for (uint32_t m = 0; m < pb->count; m++) {
            const pak_poly_t *pp = (const pak_poly_t *)(c->poly_base + c->poly_off[pb->first + m]);
            for (int v = 0; v < pp->nv; v++) {
                if (pp->v[v].x < x0) x0 = pp->v[v].x;
                if (pp->v[v].x > x1) x1 = pp->v[v].x;
                if (pp->v[v].z < z0) z0 = pp->v[v].z;
                if (pp->v[v].z > z1) z1 = pp->v[v].z;
                if (pp->v[v].y > y1) y1 = pp->v[v].y;
            }
        }
        if (x > x0 && x < x1 && z > z0 && z < z1 && y < y1) return 1;
    }
    return 0;
}

/* Le point est-il vraiment dans un rectangle de collision (sans la marge du corps de Sera : la frôler n'est pas y être) ? sceneaudit */
static int audit_dans_solide(float x, float z) {
    const chunk_t *c = chunk_of_cell((int)floorf(x), (int)floorf(z));
    if (!c) return 0;
    for (uint32_t k = 0; k < c->nsolid; k++) { const float *r = c->solids + k * 4; if (x > r[0] && x < r[2] && z > r[1] && z < r[3]) return 1; }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage : rendu_test monde.pak sortie.bmp [scénario]\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *pak = malloc(len);
    if (fread(pak, 1, len, f) != (size_t)len) { fprintf(stderr, "lecture incomplète\n"); return 1; }
    fclose(f);

    eng_config_t cfg = { 0, alloc_pc, save_write_pc, save_read_pc, NULL, clock_pc, NULL, 0, NULL, 0, NULL, 0 };
    cfg.swap565 = getenv("ECHOES_SWAP") != NULL;   /* ECHOES_SWAP=1 : octets de l'écran de la montre (chemins swp du rendu ; couleurs du BMP fausses) */
    /* ECHOES_APPS=1 : une application de plus (comme MONTRE, lancée depuis l'app montre) : l'entrée APPLICATIONS de l'écran titre */
    static const eng_app_t app_essai[1] = { { "MONTRE", "Cadran, notifications, applications", NULL } };
    if (getenv("ECHOES_APPS")) { cfg.apps = app_essai; cfg.napps = 1; cfg.montre = montre_essai; }   /* (lancé depuis l'app montre : l'écran titre d'abord) */
#if ENG_DYN
    {   /* plein écran du téléphone : taille de l'image et marges des barres du système, avant eng_init (main_android.c) */
        int ew = 540, eh = 1204, mh = 100, mb = 144;
        if (getenv("ECHOES_ECRAN")) sscanf(getenv("ECHOES_ECRAN"), "%dx%d", &ew, &eh);
        if (getenv("ECHOES_MARGES")) sscanf(getenv("ECHOES_MARGES"), "%d:%d", &mh, &mb);
        eng_set_screen(ew, eh); eng_set_safe(mh, mb);
    }
#endif
    /* ECHOES_SD=monde.pak : le paquet donné est le paquet léger de la flash (tools/paquet_sd.js) ; chunks, planches et
       textures du sol sont lus dans ce fichier, comme sur la carte SD de l'ESP32 */
    const char *sd = getenv("ECHOES_SD");
    if (sd && (sd_file = fopen(sd, "rb"))) {
        fseek(sd_file, 0, SEEK_END); cfg.pak_size = (uint32_t)ftell(sd_file); cfg.pak_read = sd_read_pc;
        printf("carte SD : %s (%u octets)\n", sd, (unsigned)cfg.pak_size);
    }
    /* ECHOES_INTR=interieurs.bin : intérieurs lus dans ce fichier, comme sur la carte SD (sinon la section INTR) */
    const char *intr = getenv("ECHOES_INTR");
    if (intr && (intr_file = fopen(intr, "rb"))) {
        fseek(intr_file, 0, SEEK_END); cfg.intr_size = (uint32_t)ftell(intr_file); cfg.intr_read = intr_read_pc;
    }
    /* ECHOES_SCN=histoire.scn : scénario lu dans ce fichier, comme sur la carte SD (sinon la section SCEN du paquet) */
    const char *scn = getenv("ECHOES_SCN");
    if (scn && (scn_file = fopen(scn, "rb"))) {
        fseek(scn_file, 0, SEEK_END); cfg.scen_size = (uint32_t)ftell(scn_file); cfg.scen_read = scn_read_pc;
        printf("scénario : %s (%u octets)\n", scn, (unsigned)cfg.scen_size);
    }
    /* ECHOES_COMMUN=monde_esp32.pak : le paquet donné est le paquet propre d'un monde découpé (maj_monde.js --ile :
       monde_propre.pak), ses sections communes (PRIS) prises dans ce paquet-ci (sur la montre : le paquet léger d'Echoes
       en flash) ; le paquet propre compte dans la PSRAM de la carte simulée (il y est chargé, avec 64 Ko de marge) */
    const char *commun = getenv("ECHOES_COMMUN");
    if (commun) {
        FILE *fc = fopen(commun, "rb");
        if (!fc) { perror(commun); return 1; }
        fseek(fc, 0, SEEK_END); const long lc = ftell(fc); fseek(fc, 0, SEEK_SET);
        uint8_t *pc = malloc(lc);
        if (fread(pc, 1, lc, fc) != (size_t)lc) { fprintf(stderr, "lecture incomplète\n"); return 1; }
        fclose(fc);
        cfg.pak_commun = pc; cfg.pak_commun_len = (size_t)lc;
        psram_pris += (size_t)len + 65536;
        printf("paquet commun : %s (%ld octets), paquet propre %ld octets\n", commun, lc, len);
    }
    if (eng_init(pak, len, &cfg)) { fprintf(stderr, "eng_init : %s\n", eng_error()); if (getenv("ECHOES_MEM")) printf("eng_init : %s\n", eng_error()); return 1; }
    if (getenv("ECHOES_MEM")) printf("mémoire du moteur après eng_init : %zu Ko lente (PSRAM sur l'ESP32), %zu Ko rapide\n", mem_lente / 1024, mem_rapide / 1024);
    if (getenv("ECHOES_MEM") && getenv("ECHOES_PSRAM")) printf("carte simulée : %zu Ko de PSRAM pris, %zu Ko libres ; %zu Ko de SRAM\n", psram_pris / 1024, (size_t)atoi(getenv("ECHOES_PSRAM")) - psram_pris / 1024, sram_pris / 1024);
    /* Ouverture (écran titre, introduction, héroïne) : seulement si le scénario commence par « accueil » ; sinon nouvelle
       partie tout de suite, avec Sera */
    if (!(argc >= 4 && !strncmp(argv[3], "accueil", 7))) accueil_fin();
    const char *mus = getenv("ECHOES_MUSIC"), *wav = getenv("ECHOES_WAV");
    if (mus && (mus_file = fopen(mus, "rb"))) {
        fseek(mus_file, 0, SEEK_END);
        eng_music_attach((uint32_t)ftell(mus_file), mus_read_pc);
    }
    en_jeu = 1;   /* toute réservation est faite au démarrage : au-delà, alloc_pc la signale */
    if (wav && (wav_file = fopen(wav, "wb"))) fseek(wav_file, 44, SEEK_SET);

    eng_input_t none = { 0 }, in;
    frame(&none);
    char script[2048] = "";
    if (argc >= 4) snprintf(script, sizeof script, "%s", argv[3]);
    for (char *tok = strtok(script, ","); tok; tok = strtok(NULL, ",")) {
        memset(&in, 0, sizeof in);
        fx_fix = -1;
        float a, b, c; int n, m2 = 150;
        if (!strcmp(tok, "accueil")) { printf("ouverture : %d cartons, %d héroïnes au choix (+ Sera), titre « %s »%c", E.w.ouv_ncartes, E.w.ouv_nher, E.w.ouv_titre, 10); continue; }
        else if (sscanf(tok, "heroine:%d", &n) == 1) {   /* héroïne k du choix (0 : Sera) */
            E.heroine = n > 0 && n <= E.w.ouv_nher ? E.w.ouv_her[n - 1] : -1;
            printf("héroïne : %s%c", E.heroine >= 0 ? E.w.persos[E.heroine].nom : "SERA", 10);
            continue;
        }
        else if (!strncmp(tok, "tenue:", 6)) {   /* Sera porte la tenue de ce nom (variante de couleur comprise) */
            const int t = tenue_named(0, tok + 6);
            E.tenue_eq[0] = (int8_t)t;
            printf("tenue %s : %d, variante %d%c", tok + 6, t, t >= 0 ? E.w.tenues[t].vremap : -1, 10);
            continue;
        }
        else if (!strcmp(tok, "menu")) in.keys = ENG_KEY_MENU;
        else if (!strcmp(tok, "ok")) in.keys = ENG_KEY_OK;
        else if (!strcmp(tok, "etatparler")) { printf("parler : cible %d (%s), script %d, dialogue %d, choix %d, ui_block %d, scène %d%c", E.talk, E.talk >= 0 ? E.w.persos[E.npcs[E.talk].perso].nom : "-", script_running(), hud_in_dialog(), hud_in_choice(), E.ui_block, E.scene, 10); continue; }
        else if (!strcmp(tok, "evdone")) { printf("EVDONE"); for (int k = 0; k < SC_MAX_EVENTS; k++) if (E.ev_done[k >> 3] >> (k & 7) & 1) printf(" %d", k); printf("%cQUETES", 10); for (int q = 0; q < E.w.sc_nq; q++) printf(" %d", E.quests[q]); printf("%c", 10); continue; }
        else if (sscanf(tok, "evbit:%d:%f", &n, &a) == 2 && n >= 0 && n < SC_MAX_EVENTS) { if (a > 0) E.ev_done[n >> 3] |= (uint8_t)(1 << (n & 7)); else E.ev_done[n >> 3] &= (uint8_t)~(1 << (n & 7)); continue; }   /* marque « déjà fait » de l'événement n (réparation d'une sauvegarde) */
        else if (!strcmp(tok, "ecrire")) { printf("sauvegarde écrite : %d%c", hud_sauve_auto(1), 10); continue; }   /* la partie en cours, écrite tout de suite (emplacement suivant) */
        else if (sscanf(tok, "chrono:%d", &n) == 1) {   /* chrono:n : n images (l'essai de fluidité en cours, par exemple) ; temps par image sur ce PC : jeu, préparation, 3D, interface, et le total (moyenne, 95e centile, pire) */
            static uint32_t tt[4096]; double su = 0, sb = 0, s3 = 0, si = 0; long spx = 0, stri = 0; int nn = n > 4096 ? 4096 : n;
#ifdef ENG_COMPTE
            eng_compte_us = clock_pc;
#endif
            eng_nsous = eng_nsous_av = 0;
            for (int k = 0; k < nn; k++) {
                frame(&none);
                const eng_stats_t *st = eng_stats();
                su += us_update; sb += us_build; s3 += st->us_3d; si += st->us_ui; spx += st->px; stri += st->tris;
                tt[k] = us_update + us_build + st->us_3d + st->us_ui;
#ifdef ENG_COMPTE
                if (k % 60 == 59) { printf("  fenêtre %2d s : %5.0f px affines, %5.0f en perspective, %5.0f texture réduite, %5.0f chemin lent dont %5.0f fondu (par image)%c", (k + 1) / 30, eng_px_aff / 60.0, eng_px_persp / 60.0, eng_px_mini / 60.0, eng_px_lent / 60.0, eng_px_trans / 60.0, 10); eng_px_aff = eng_px_persp = eng_px_mini = eng_px_lent = eng_px_trans = 0; }
#endif
            }
            for (int a8 = 1; a8 < nn; a8++) { const uint32_t v8 = tt[a8]; int b8 = a8 - 1; while (b8 >= 0 && tt[b8] > v8) { tt[b8 + 1] = tt[b8]; b8--; } tt[b8 + 1] = v8; }
            printf("chrono %d images : jeu %.2f ms, préparation %.2f, 3D %.2f, interface %.2f ; total moyen %.2f, 95e centile %.2f, pire %.2f ms ; %ld pixels, %ld triangles par image%c",
                   nn, su / nn / 1000, sb / nn / 1000, s3 / nn / 1000, si / nn / 1000, (su + sb + s3 + si) / nn / 1000, tt[nn * 95 / 100] / 1000.0, tt[nn - 1] / 1000.0, spx / nn, stri / nn, 10);
            if (eng_bat_vus) printf("  bâtiments du rayon de vue : %.1f par image (%.0f polygones), écartés entiers : %.1f (%.0f polygones)%c", (double)eng_bat_vus / nn, (double)eng_bat_polys / nn, (double)eng_bat_ecartes / nn, (double)eng_bat_polys_sautes / nn, 10);
            eng_bat_ecartes = eng_bat_vus = eng_bat_polys = eng_bat_polys_sautes = 0;
            printf("  dessin de l'interface (µs par image, passages op × bande) : polygones %.1f (%.0f), masques %.1f (%.0f), images %.1f (%.0f), masques tournés %.1f (%.0f), halos %.1f (%.0f)%c",
                   eng_ui_us[0] / (double)nn, eng_ui_n[0] / (double)nn, eng_ui_us[1] / (double)nn, eng_ui_n[1] / (double)nn, eng_ui_us[2] / (double)nn, eng_ui_n[2] / (double)nn, eng_ui_us[3] / (double)nn, eng_ui_n[3] / (double)nn, eng_ui_us[5] / (double)nn, eng_ui_n[5] / (double)nn, 10);
            memset(eng_ui_us, 0, sizeof eng_ui_us); memset(eng_ui_n, 0, sizeof eng_ui_n);
            if (eng_hud_us[1] + eng_hud_us[2]) printf("  interface (µs par image) : alarme, pilote %.1f, jauges et boussole %.1f, guidage %.1f, astres %.1f, objectif %.1f%c", eng_hud_us[0] / (double)nn, eng_hud_us[1] / (double)nn, eng_hud_us[2] / (double)nn, eng_hud_us[3] / (double)nn, eng_hud_us[4] / (double)nn, 10);
            memset(eng_hud_us, 0, sizeof eng_hud_us);
            if (eng_px_mel) printf("  pixels mêlés par le brouillard lissé : %.0f par image%c", (double)eng_px_mel / nn, 10);
            eng_px_mel = 0;
            if (eng_nsous_av) printf("  sous-plages du brouillard lissé par image : %.0f (découpage d'avant : %.0f)%c", (double)eng_nsous / nn, (double)eng_nsous_av / nn, 10);
            continue;
        }
        else if (!strncmp(tok, "essaimonde:", 11)) { float v[6] = { 0, 0, 0, 0, -1, 30 }; sscanf(tok + 11, "%f:%f:%f:%f:%f:%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]); printf("essai de fluidité : %d%c", eng_essai_monde(v[0], v[1], v[2], (int)v[3], v[4], v[5]), 10); continue; }   /* comme SDX ESSAI */
        else if (!strcmp(tok, "back")) in.keys = ENG_KEY_BACK;
        else if (!strcmp(tok, "up")) in.keys = ENG_KEY_UP;
        else if (!strcmp(tok, "down")) in.keys = ENG_KEY_DOWN;
        else if (!strcmp(tok, "left")) in.keys = ENG_KEY_LEFT;
        else if (!strcmp(tok, "right")) in.keys = ENG_KEY_RIGHT;
        else if (sscanf(tok, "tap:%f:%f", &a, &b) == 2) { in.touch = 1; in.touch_x = a; in.touch_y = b; }
        else if (sscanf(tok, "hold:%f:%f:%d", &a, &b, &n) == 3) {
            in.touch = 1; in.touch_x = a; in.touch_y = b;
            for (int k = 0; k < n - 1; k++) frame(&in);
            printf("doigt en %.0f %.0f (Sera à l'écran en %.0f %.0f) : %s\n", a, b, E.scr_sera.cx, E.scr_sera.cy,
                   E.sera.state == 2 ? "elle court" : E.sera.state == 1 ? "elle marche" : "immobile");
        } else if (sscanf(tok, "pouces:%f:%f:%d", &a, &b, &n) == 3) {
            /* Conduite à deux mains : pouce gauche posé en (110, 320), glissé de a px vers le haut ; pouce droit posé en
               (350, 330), glissé de b px vers la droite (en 5 images) ; tenus n images, puis levés ; état toutes les 15 */
            const float d0 = E.sera.dir;
            for (int k = 0; k < n; k++) {
                const float t = k < 5 ? k / 5.0f : 1;
                in.touch = 1; in.touch_x = 110; in.touch_y = 320 - a * t;
                in.touch2 = 1; in.touch2_x = 350 + b * t; in.touch2_y = 330;
                if (k & 1) {   /* l'ordre des doigts change d'une image à l'autre : chacun doit garder son rôle */
                    in.touch_x = 350 + b * t; in.touch_y = 330; in.touch2_x = 110; in.touch2_y = 320 - a * t;
                }
                frame(&in);
                if ((k + 1) % 15 == 0) printf("  %3d : th %.2f st %.2f, v %.2f, cap %+.0f deg\n", k + 1, E.drive_th, E.drive_st, E.ride_v, (E.sera.dir - d0) * 57.3f);
            }
            memset(&in, 0, sizeof in);
        } else if (sscanf(tok, "pointer:%f:%f:%d", &a, &b, &n) == 3) {
            /* SYSTÈME › COMMANDES : POINTER ; doigt posé en (a, b) de l'écran n images (n négatif : un toucher d'une image,
               puis -n images sans doigt) ; état toutes les 15 images : position, cap, vitesse, point visé (pilote.c) */
            E.commandes = COMMANDES_POINTER;
            const int tenu = n > 0; if (!tenu) n = -n;
            const float x0 = E.sera.x, z0 = E.sera.z;
            for (int k = 0; k < n; k++) {
                in.touch = tenu || k == 0; in.touch_x = a; in.touch_y = b;
                frame(&in);
                float cx = 0, cz = 0; const int c = pilote_doigt_cible(&cx, &cz);
                if ((k + 1) % 15 == 0 || k == n - 1)
                    printf("  %3d : véhicule en %.2f %.2f (parcouru %.2f), cap %.0f deg, v %.2f, th %.2f st %.2f ; point %s %.2f %.2f (à %.2f)\n", k + 1, E.sera.x, E.sera.z,
                           hypotf(E.sera.x - x0, E.sera.z - z0), E.sera.dir * 57.3f, E.ride_v, E.drive_th, E.drive_st, c ? "visé" : "aucun", cx, cz, c ? hypotf(cx - E.sera.x, cz - E.sera.z) : 0);
            }
            memset(&in, 0, sizeof in);
        } else if (sscanf(tok, "joy:%f:%f:%d", &a, &b, &n) == 3) {
            /* Joystick (SYSTÈME › COMMANDES) : doigt posé au centre, glissé de (a, b) px en 5 images, tenu n images, puis
               levé ; état toutes les 15 images. n négatif : le doigt reste posé pour l'étape bmp: qui suit (pas d'image sans
               doigt après l'étape), puis jusqu'à la prochaine étape joy: */
            const int tenir = n < 0;
            if (tenir) n = -n;
            if (!monter_voiture(E.yaw)) { printf("joy : pas de voiture\n"); continue; }
            const float d0 = E.sera.dir;
            float jx = 233, jy = 372;
            hud_joy_centre(&jx, &jy);   /* (plein écran : en bas de l'image) */
            E.commandes = COMMANDES_JOYSTICK;
            for (int k = 0; k < n; k++) {
                const float t = k < 5 ? k / 5.0f : 1;
                in.touch = 1; in.touch_x = jx + a * t; in.touch_y = jy + b * t;
                frame(&in);
                if ((k + 1) % 15 == 0) printf("  %3d : th %.2f st %.2f, v %.2f, cap %+.0f deg\n", k + 1, E.drive_th, E.drive_st, E.ride_v, (E.sera.dir - d0) * 57.3f);
            }
            if (tenir) continue;
            memset(&in, 0, sizeof in);
        } else if (sscanf(tok, "drag:%f:%f:%f:%d", &a, &b, &c, &n) == 4) {
            for (int k = 0; k <= n; k++) { in.touch = 1; in.touch_x = a; in.touch_y = b + (c - b) * k / n; frame(&in); }
            memset(&in, 0, sizeof in);
        } else if (sscanf(tok, "rim:%f:%f", &a, &b) == 2) {
            for (int k = 0; k <= 12; k++) {
                float t = (a + (b - a) * k / 12) * PI_F / 180;
                in.touch = 1; in.touch_x = 233 + sinf(t) * 210; in.touch_y = 233 - cosf(t) * 210;
                frame(&in);
            }
        } else if (sscanf(tok, "pinch:%f:%f", &a, &b) == 2) {
            for (int k = 0; k <= 12; k++) {
                float d = (a + (b - a) * k / 12) / 2;
                in.touch = in.touch2 = 1; in.touch_x = 233 - d; in.touch2_x = 233 + d; in.touch_y = in.touch2_y = 233;
                frame(&in);
            }
            memset(&in, 0, sizeof in);
        } else if (sscanf(tok, "pos:%f:%f:%f", &a, &b, &c) == 3) { E.sera.x = a; E.sera.z = b; E.yaw = c; }
        else if (sscanf(tok, "lacet:%f", &a) == 1) E.yaw = a;   /* lacet de la caméra (pas magnétiques : cam_pas) */
        else if (!strcmp(tok, "lacet")) { printf("lacet de la caméra : %.3f rad (%.1f°)\n", E.yaw, E.yaw * 180 / PI_F); continue; }
        else if (!strcmp(tok, "quetes")) { hud_quetes_ouvrir(); continue; }   /* menu QUÊTES (niveau conseillé de chaque quête) */
        else if (!strcmp(tok, "silhouettes")) {
            /* Hauteur réelle de la silhouette (pixels opaques, image 0 de l'attente, direction de face) de chaque personnage
               non véhicule, × sa taille, rapportée à celle de Sera : ce que le rendu montre à profondeur égale */
            /* médiane des 8 directions, image 0 (même mesure que le calibrage SNRM de l'émulateur : silPart) */
            #define SIL_H(sh, out) do { const sheet_t *S_ = &E.w.sheets[sh]; int v_[8], n_ = 0; \
                for (int d_ = 0; d_ < 8; d_++) { const uint8_t *b_ = sheet_block(sh, d_, 2); int lo_ = 9999, hi_ = -1; \
                    if (b_) for (int y_ = 0; y_ < S_->fh; y_++) for (int x_ = 0; x_ < S_->fw; x_++) if (b_[y_ * S_->fw * S_->frames + x_]) { if (y_ < lo_) lo_ = y_; if (y_ > hi_) hi_ = y_; } \
                    if (hi_ >= lo_) { int j_ = n_++; while (j_ > 0 && v_[j_ - 1] > hi_ - lo_ + 1) { v_[j_] = v_[j_ - 1]; j_--; } v_[j_] = hi_ - lo_ + 1; } } \
                out = n_ ? v_[n_ >> 1] : 0; } while (0)
            const int SA = getenv("SIL_MARCHE") ? ANIM_WALK : ANIM_IDLE;   /* SIL_MARCHE=1 : planches de marche */
            const int ps = member_sheet(0), pa = anim_sheet(ps, SA);
            int hs; SIL_H(pa, hs);
            printf("SERA : silhouette %d px / %d (planche %d), calibrage %.2f, taille %.2f\n", hs, E.w.sheets[pa].fh, pa, E.w.sheets[pa].norm, joueur_taille());
            int nh = 0;
            for (int k = 0; k < E.w.npersos; k++) {
                const perso_t *pe = &E.w.persos[k];
                if (pe->sheet < 0 || (pe->flags & PERS_VEHICULE)) continue;
                const int sa = anim_sheet(pe->sheet, SA);
                int h; SIL_H(sa, h);
                /* le rendu ramène la cellule (fh) de chaque planche à une hauteur fixe (add_character : k = s·E.w.fh/fh·t) : la
                   hauteur à l'écran suit la part de la cellule que remplit la silhouette */
                /* × la normalisation des planches (SNRM) ; Sera : × la taille de sa planche */
                /* r : à l'écran, × Sera ; e : silhouette calibrée ÷ celle de Sera, sans les tailles (1 quand les deux planches
                   sont calibrées sur l'humain type : seule la taille voulue fait alors la différence) */
                const float r = hs > 0 ? (float)h / E.w.sheets[sa].fh * pe->taille * E.w.sheets[sa].norm / ((float)hs / E.w.sheets[pa].fh * E.w.sheets[pa].norm * joueur_taille()) : 0;
                const float e = pe->taille > 0 ? r * joueur_taille() / pe->taille : 0;
                const int hors = e < 0.95f || e > 1.05f; nh += hors;
                if (getenv("SIL_TOUT") || hors) printf("  %-24s %3d px / %3d × %.2f (calibrage %.2f) = %.2f × Sera, écart %.2f\n", pe->nom, h, E.w.sheets[sa].fh, pe->taille, E.w.sheets[sa].norm, r, e);   /* SIL_TOUT=1 : tous */
            }
            printf("%d personnages à plus de 5 %% de leur taille voulue (écart hors de 0,95 à 1,05)\n", nh);
            /* Tenues debout (les poses : nage, accroupie, à genoux, KO… prennent le calibrage de leur porteur) */
            int nt = 0;
            for (int t = 0; t < E.w.ntenues; t++) {
                const tenue_t *T = &E.w.tenues[t];
                if (T->sheet < 0 || T->sheet >= E.w.nsheets) continue;
                const int sa = anim_sheet(T->sheet, SA); int h; SIL_H(sa, h);
                const float e = hs > 0 ? (float)h / E.w.sheets[sa].fh * E.w.sheets[sa].norm / ((float)hs / E.w.sheets[pa].fh * E.w.sheets[pa].norm) : 0;
                const char *pz[] = { "SQUAT", "GENOU", "KO", "ASSIS", "COUCH", "ALLONG", "NAGE", "BIKINI", "MAILLOT", "PLAGE" };
                int pose = T->ctx == TENUE_NAGE; for (int q = 0; q < 10; q++) if (strstr(T->nom, pz[q])) pose = 1;
                const int hors = !pose && (e < 0.95f || e > 1.05f); nt += hors;
                if (getenv("SIL_TOUT") || hors) printf("  tenue %-20s de %-14s %3d px / %3d (calibrage %.2f), écart %.2f%s\n", T->nom, T->owner == TENUE_JOUEUR ? "SERA" : T->owner >= 0 && T->owner < E.w.npersos ? E.w.persos[T->owner].nom : "?", h, E.w.sheets[sa].fh, E.w.sheets[sa].norm, e, pose ? " (pose : calibrage du porteur)" : "");
            }
            printf("%d tenues debout à plus de 5 %% de l'humain type\n", nt);
            #undef SIL_H
            continue;
        }
        else if (!strncmp(tok, "tailles", 7)) {   /* tailles:MOT : la taille que le rendu donne à Sera et aux personnages dont le nom contient MOT */
            const int ps = member_sheet(0);
            printf("SERA : planche %d, taille de planche %.2f, cellule %d px (rendu : taille de sa planche)\n", ps, ps >= 0 ? E.w.sheets[ps].taille : 0, ps >= 0 ? E.w.sheets[ps].fh : 0);
            const char *mot = tok[7] == ':' ? tok + 8 : "";
            for (int k = 0; k < E.w.npersos; k++) if (strstr(E.w.persos[k].nom, mot)) {
                const perso_t *pe = &E.w.persos[k];
                printf("  %-24s taille %.2f (rendu) ; planche %d : taille %.2f, cellule %d px\n", pe->nom, pe->taille, pe->sheet, pe->sheet >= 0 ? E.w.sheets[pe->sheet].taille : 0, pe->sheet >= 0 ? E.w.sheets[pe->sheet].fh : 0);
            }
            continue;
        }
        else if (!strcmp(tok, "systeme")) printf("SYSTÈME : %s…\n", hud_systeme_essai(-1, 0));   /* menu SYSTÈME ouvert, ses premières lignes */
        else if (sscanf(tok, "sysligne:%d", &n) == 1) {   /* ligne logique n du menu SYSTÈME touchée (19 : HEURE, 18 : QUITTER) ; l'heure et l'écran */
            hud_systeme_essai(n, 1);
            printf("SYSTÈME %d : heure %.3f (%d h %02d), cycle %d, titre %d\n", n, E.heure, (int)(E.heure * 24), (int)(E.heure * 1440) % 60, E.cycle, accueil_on());
        }
        else if (!strcmp(tok, "carte")) hud_carte_ouvrir();   /* menu CARTE ouvert (carte.c), centré sur Sera ; une image */
        else if (sscanf(tok, "cartevue:%f:%f:%f", &a, &b, &c) == 3) carte_vue(a, b, c);   /* carte du menu : centre (cases), zoom (pixels par case) */
        else if (!strcmp(tok, "carteetat")) { static char ce[512]; carte_etat(ce, sizeof ce); printf("%s\n", ce); continue; }
        else if (!strcmp(tok, "brouillard")) { static char be[512]; brouillard_etat(be, sizeof be); printf("%s (Sera en %.1f %.1f, carte %d : bloc vu %d)\n", be, E.sera.x, E.sera.z, E.w.carte, brouillard_vu(E.sera.x, E.sera.z, E.w.carte)); continue; }
        else if (sscanf(tok, "brouvoir:%f:%f:%f", &a, &b, &c) == 3) { brouillard_voir(a, b, E.w.carte, c); continue; }
        else if (!strcmp(tok, "brouvide")) { brouillard_effacer(); continue; }
        else if (!strcmp(tok, "carteobj")) {   /* objectifs de la carte du menu */
            float ox, oz, qx[OBJ_CARTE], qz[OBJ_CARTE]; uint8_t qg[OBJ_CARTE];
            const int ob = objective_pos(&ox, &oz), no = script_objectifs(qx, qz, qg, OBJ_CARTE);
            printf("objectifs : quête suivie %d (%s) %s", E.qtrack, E.qtrack >= 0 ? script_quest_name(E.qtrack) : "-", ob ? "" : "sans repère ici");
            if (ob) printf("en %.1f %.1f (vu %d)", ox, oz, brouillard_vu(ox, oz, E.w.carte));
            printf(" ; %d autres :", no);
            for (int i = 0; i < no; i++) printf(" %.1f %.1f%s%s%s ;", qx[i], qz[i], qg[i] & OBJ_PRINC ? " principale" : "", qg[i] & OBJ_PERSO ? " personne" : "", brouillard_vu(qx[i], qz[i], E.w.carte) ? "" : " (brouillard)");
            printf("\n");
            continue;
        }
        else if (sscanf(tok, "cartetele:%f:%f", &a, &b) == 2) {   /* un toucher sur la carte du menu en ce point du plan */
            static char ch[256];
            if (!carte_plein()) hud_carte_ouvrir();
            carte_touche_essai(a, b); hud_carte_touche();
            printf("carte, toucher en %.1f %.1f (vu %d) : %s ; message : %s\n", a, b, brouillard_vu(a, b, E.w.carte), hud_choix_texte(ch, sizeof ch) ? ch : "aucun choix", hud_toast_txt());
        }
        else if (!strcmp(tok, "objectif")) {   /* guidage de la quête suivie : point visé (la personne à aller voir, objective_perso) */
            float ox, oz;
            float px = 0, py = 0, gx = 0, gy = 0;
            const int pv = objective_pos(&ox, &oz) && project(ox, height_at(ox, oz) + 2.0f * E.w.sprite_scale / E.w.ech, oz, &px, &py) && project(ox, height_at(ox, oz), oz, &gx, &gy);
            if (pv) printf("  à l'écran : tête visée %.0f %.0f, pieds %.0f %.0f\n", px, py, gx, gy);
            if (objective_pos(&ox, &oz)) printf("objectif de « %s » : %.1f %.1f, à %.1f cases de Sera (%.1f %.1f)\n", E.qtrack >= 0 ? script_quest_name(E.qtrack) : "-", ox, oz, hypotf(ox - E.sera.x, oz - E.sera.z), E.sera.x, E.sera.z);
            else printf("objectif : aucun\n");
            continue;
        }
        else if (!strcmp(tok, "guidage")) {   /* lieux du menu GUIDAGE sur la carte où est Sera, par groupe (nom, distance) */
            static char gl[16384];
            const int k = hud_guide_liste(gl, sizeof gl);
            printf("guidage, carte %d (%s) : %d lieux\n%s\n", E.w.carte, E.w.carte ? E.w.cartes[E.w.carte].nom : "PLANÈTE", k, gl);
            continue;
        }
        else if (!strncmp(tok, "guidemenu", 9)) hud_guide_ouvrir(tok[9] == ':' ? atoi(tok + 10) : -1);   /* menu GUIDAGE ouvert (guidemenu:g : sur le groupe g) */
        else if (!strncmp(tok, "guider:", 7)) {   /* guider:MOT : suivre le lieu de cette carte dont le nom contient MOT (le plus proche) */
            int g = -1; float bd = 1e9f;
            for (int k = 0; k < E.w.nguides; k++) if (E.w.guides[k].carte == E.w.carte && strstr(E.w.guides[k].nom, tok + 7)) {
                const float d = hypotf(E.w.guides[k].x - E.sera.x, E.w.guides[k].z - E.sera.z);
                if (d < bd) { bd = d; g = k; }
            }
            E.guide = g;
            if (g >= 0) printf("guidage vers %s (lieu %d : %.1f %.1f, rayon %.1f), à %.1f cases\n", E.w.guides[g].nom, g, E.w.guides[g].x, E.w.guides[g].z, E.w.guides[g].r, bd);
            else printf("guidage : aucun lieu « %s » sur cette carte\n", tok + 7);
            continue;
        }
        else if (sscanf(tok, "pres:%f", &a) == 1) {   /* Sera à a cases au sud du lieu suivi, la caméra tournée vers lui (repère de près, arrivée) */
            if (E.guide >= 0 && E.guide < E.w.nguides) eng_teleport(E.w.guides[E.guide].x, E.w.guides[E.guide].z + a, PI_F);
        }
        else if (!strcmp(tok, "guide")) {   /* lieu suivi (après l'arrivée : aucun) et dernier message */
            printf("lieu suivi : %s ; message : %s\n", E.guide >= 0 ? E.w.guides[E.guide].nom : "aucun", hud_toast_txt());
            continue;
        }
        else if (!strncmp(tok, "pnj:", 4) || !strncmp(tok, "placer:", 7)) {
            /* pnj:NOM : Sera posée sur l'exemplaire de ce nom (contre lui) ; placer:NOM : à côté (sera_placer, comme
               « teleporter ») */
            const int sur = tok[1] == 'n';
            const char *nom = tok + (sur ? 4 : 7);
            for (int k = 0; k < E.nnpc; k++) if (!strcmp(E.w.persos[E.npcs[k].perso].nom, nom)) {
                eng_teleport(E.npcs[k].x, E.npcs[k].z, E.yaw);
                for (int im = 0; im < 3; im++) frame(&in);   /* chunks autour */
                if (!sur) sera_placer(E.npcs[k].x, E.npcs[k].z, 6);
                printf("%s en %.2f %.2f, Sera en %.2f %.2f (%.2f case)%c", nom, E.npcs[k].x, E.npcs[k].z, E.sera.x, E.sera.z,
                       hypotf(E.sera.x - E.npcs[k].x, E.sera.z - E.npcs[k].z), 10);
                break;
            }
            continue;
        }
        else if (!strcmp(tok, "ou")) { printf("Sera en %.2f %.2f%c", E.sera.x, E.sera.z, 10); continue; }
        else if (sscanf(tok, "zonepieds:%d", &n) == 1) {   /* zonepieds:n : Sera court n images (tourne quand elle est bloquée) ; zones sous ses pieds (part des images) */
            int h[33] = { 0 }, bloque = 0; float cap = 0.5f, ox = E.sera.x, oz = E.sera.z;
            eng_input_t rien5 = { 0 };
            for (int k = 0; k < n; k++) {
                E.has_dest = 1; E.dest_court = 1; E.dest_x = E.sera.x + sinf(cap) * 8; E.dest_z = E.sera.z + cosf(cap) * 8;
                frame(&rien5); if (BT.on) BT.on = 0;
                for (int s5 = 0; s5 < PATR_MAX; s5++) PATR[s5].on = 0;
                bloque = hypotf(E.sera.x - ox, E.sera.z - oz) < 0.01f ? bloque + 1 : 0; ox = E.sera.x; oz = E.sera.z;
                if (bloque > 8) { cap += 1.1f; bloque = 0; }
                const int t = encounter_zone(E.sera.x, E.sera.z); h[t < 0 ? 32 : t]++;
            }
            { float hmax = -1e9f, hmin = 1e9f; for (int k = 0; k < 120; k++) { E.has_dest = 1; E.dest_court = 1; E.dest_x = E.sera.x + 8; E.dest_z = E.sera.z; frame(&rien5); const float gy = ground_y(E.sera.x, E.sera.z); if (gy > hmax) hmax = gy; if (gy < hmin) hmin = gy; if (k % 20 == 0) printf("  tout droit %d : %.1f %.1f, sol %.2f, zone %d%c", k, E.sera.x, E.sera.z, gy, encounter_zone(E.sera.x, E.sera.z), 10); } printf("  sol de %.2f à %.2f%c", hmin, hmax, 10); }
            printf("zones sous Sera (%d images) :", n); for (int t = 0; t < 33; t++) if (h[t]) printf(" %d %d%%", t == 32 ? -1 : t, 100 * h[t] / n); printf("%c", 10);
        }
        else if (sscanf(tok, "caseecran:%f:%f", &a, &b) == 2) {   /* caseecran:sx:sy : la case du sol sous ce point de l'écran (pixels natifs), ses données, et build/cams/tuiles.ppm : ses 5 × 5 tuiles de sol (cases d'eau cerclées de rouge) */
            extern int eng_essai_projeter(float x, float z, float *sx, float *sy, float *h);
            int bi = -1, bj = -1; float bd = 1e9f;
            for (int j = (int)E.sera.z - 20; j <= (int)E.sera.z + 20; j++) for (int i = (int)E.sera.x - 20; i <= (int)E.sera.x + 20; i++) {
                float sx, sy, h; if (!eng_essai_projeter(i + 0.5f, j + 0.5f, &sx, &sy, &h)) continue;
                const float d = hypotf(sx - a, sy - b); if (d < bd) { bd = d; bi = i; bj = j; }
            }
            if (bi < 0) { printf("caseecran : rien%c", 10); continue; }
            printf("caseecran %.0f %.0f : case %d %d (à %.1f px)%c", a, b, bi, bj, bd, 10);
            for (int dj = -2; dj <= 2; dj++) { printf("  "); for (int di = -2; di <= 2; di++) { const chunk_t *c6 = chunk_of_cell(bi + di, bj + dj); const int l6 = cell_local(bi + di, bj + dj);
                if (c6) printf("[tuile %5d eau %d wh %5.2f h %4.2f] ", c6->tile[l6], c6->water[l6], c6->water_h[l6] < -1e8f ? -9.0f : c6->water_h[l6], hv(bi + di, bj + dj)); } printf("%c", 10); }
            FILE *pf = fopen("build/cams/tuiles.ppm", "wb");
            if (pf) {
                const int T = 32, W5 = 5 * T;
                fprintf(pf, "P6 %d %d 255%c", W5, W5, 10);
                for (int y = 0; y < W5; y++) for (int x = 0; x < W5; x++) {
                    const int di = x / T - 2, dj = y / T - 2, tx = x % T, ty = y % T;
                    const chunk_t *c6 = chunk_of_cell(bi + di, bj + dj); const int l6 = cell_local(bi + di, bj + dj);
                    unsigned char rgb[3] = { 255, 0, 255 };
                    if (c6) {
                        const int t6 = c6->tile[l6];
                        const uint8_t *px6 = t6 >= (int)E.w.g0 && t6 < TILE_BIG ? (ground_need(t6 - (int)E.w.g0, 1), ground_tex(t6 - (int)E.w.g0)) : NULL;
                        if (px6) { const int v6 = px6[ty * T + tx]; rgb[0] = E.w.pal[v6][0]; rgb[1] = E.w.pal[v6][1]; rgb[2] = E.w.pal[v6][2]; }
                        if (c6->water[l6] && (tx == 0 || ty == 0 || tx == T - 1 || ty == T - 1)) { rgb[0] = 255; rgb[1] = 0; rgb[2] = 0; }
                    }
                    fwrite(rgb, 1, 3, pf);
                }
                fclose(pf);
            }
        }
        else if (!strcmp(tok, "renccartes")) {   /* rencontres de chaque carte : zones de la grille (part des cases), ennemis possibles de chacune (masque zones) ; puis retour sur la carte 0 */
            for (int c = 0; c < E.w.ncartes; c++) {
                world_carte(c, c ? E.w.cartes[c].sx : E.w.start_x, c ? E.w.cartes[c].sz : E.w.start_z);
                int h[33] = { 0 }, tot = 0;
                const int pas = E.w.n > 64 ? E.w.n / 64 : 1;
                (void)pas;
                for (int cz = 0; cz < E.w.n; cz += 16) for (int cx = 0; cx < E.w.n; cx += 16) {   /* chunk par chunk (la fenêtre chargée autour de lui), une case sur deux */
                    world_stream(cx + 8.0f, cz + 8.0f, 1);
                    for (int z = cz; z < cz + 16 && z < E.w.n; z += 2) for (int x = cx; x < cx + 16 && x < E.w.n; x += 2) { if (!sol_marchable(x + 0.5f, z + 0.5f)) continue; const int t = encounter_zone(x + 0.5f, z + 0.5f); h[t < 0 ? 32 : t]++; tot++; }   /* cases où l'on marche */
                }
                printf("carte %d (%s) : %d de côté, grille %d, ennemis %d ; zones :", c, E.w.cartes[c].nom, E.w.n, E.w.zone_n, E.w.nenemies);
                for (int t = 0; t < 33; t++) if (h[t]) {
                    int ne = 0; for (int k = 0; k < E.w.nenemies; k++) ne += t < 32 && (E.w.enemies[k].zones >> t & 1);
                    printf(" %s%d %d%% (%d ennemis)", t == 32 ? "aucune" : "", t == 32 ? 0 : t, 100 * h[t] / (tot ? tot : 1), ne);
                }
                printf("%c", 10);
            }
            world_carte(0, E.w.start_x, E.w.start_z);
        }
        else if (sscanf(tok, "carte:%d", &n) == 1) {   /* passer sur la carte n (CART), à son départ */
            if (n >= 0 && n < E.w.ncartes) world_carte(n, n ? E.w.cartes[n].sx : E.w.start_x, n ? E.w.cartes[n].sz : E.w.start_z);
        } else if (!strcmp(tok, "cartes")) {
            for (int k = 0; k < E.w.ncartes; k++) printf("carte %d : %s, %d × %d, ciel %d\n", k, E.w.cartes[k].nom, E.w.cartes[k].n, E.w.cartes[k].n, E.w.cartes[k].ciel);
            for (int k = 0; k < E.w.npass; k++) { const passage_t *P = &E.w.pass[k]; printf("passage %d : carte %d (%.1f, %.1f) -> carte %d (%.1f, %.1f)\n", k, P->carte, P->x, P->z, P->vers, P->ax, P->az); }
        }
        else if (sscanf(tok, "porte:%d", &n) == 1 && n >= 0 && n < E.w.nportes) {   /* Sera devant la porte n, face à elle */
            const int_porte_t *d = &E.w.portes[n];
            eng_teleport(d->x + d->nx * 0.5f, d->z + d->nz * 0.5f, atan2f(-d->nx, -d->nz));
            E.sera.dir = atan2f(-d->nx, -d->nz);
            printf("porte %d : bâtiment %d, mode %d, niveau %d, (%.1f %.1f)\n", n, d->bat, E.w.bats[d->bat].mode, E.w.bats[d->bat].niveau, d->x, d->z);
        }
        else if (sscanf(tok, "approche:%d", &n) == 1 && n >= 0 && n < E.w.nportes) {   /* Sera à 3 cases devant la porte n, face à elle */
            const int_porte_t *d = &E.w.portes[n];
            eng_teleport(d->x + d->nx * 3, d->z + d->nz * 3, atan2f(-d->nx, -d->nz));
        }
        else if (!strcmp(tok, "solides")) {   /* rectangles de collision à moins de 3 cases de Sera */
            const chunk_t *c = chunk_of_cell((int)E.sera.x, (int)E.sera.z);
            for (uint32_t k = 0; c && k < c->nsolid; k++) {
                const float *r = &c->solids[k * 4];
                if (r[0] < E.sera.x + 3 && r[2] > E.sera.x - 3 && r[1] < E.sera.z + 3 && r[3] > E.sera.z - 3) printf("  solide %.2f %.2f - %.2f %.2f%c", r[0], r[1], r[2], r[3], 10);
            }
            printf("  marge %.2f, ech %.2f%c", 0.35f / E.w.ech, E.w.ech, 10);
        }
        else if (!strcmp(tok, "etat")) {
            float bd = 1e9f; int bk = -1;
            for (int k = 0; k < E.w.nportes; k++) { const float l = hypotf(E.w.portes[k].x - E.sera.x, E.w.portes[k].z - E.sera.z); if (l < bd) { bd = l; bk = k; } }
            printf("état : Sera %.2f %.2f, porte devant %d, porte la plus proche %d à %.2f, dedans %d\n", E.sera.x, E.sera.z, E.porte, bk, bd, E.in.on);
            if (E.in.on) {
                printf("  intérieur : %d personnage(s) (exemplaires %d à %d), interlocuteur %d, commerce %d\n", E.in.npnj, E.in.npc0, E.in.npc0 + E.in.npcn - 1, E.talk, E.w.bats[E.in.bat].comm);
                for (int i = 0; i < E.in.npnj; i++) printf("    %s, métier %d, en %.2f %.2f (exemplaire en %.2f %.2f%s)\n", E.w.persos[E.in.pnj[i].perso].nom, E.in.pnj[i].metier, E.in.pnj[i].x, E.in.pnj[i].z, i < E.in.npcn ? E.npcs[E.in.npc0 + i].x - IO : 0, i < E.in.npcn ? E.npcs[E.in.npc0 + i].z - IO : 0, i < E.in.npcn && E.npcs[E.in.npc0 + i].walking ? ", marche" : "");
            }
        }
        else if (sscanf(tok, "entrer:%d", &n) == 1) {   /* entrer par la porte n (forcée s'il le faut) */
            const int ok = int_entrer_porte(n, 1);
            printf("entrer %d : %s, %d polygones, %d objets, %d nœuds, grille %d × %d\n", n, ok ? "dedans" : "échec", E.in.npoly, E.in.nobj, E.in.nnode, E.in.gw, E.in.gh);
        }
        else if (!strncmp(tok, "piresrue:", 9)) {   /* piresrue:x0:z0:x1:z1:pas[:zoom] : primitives et sommets au plus dehors, quatre lacets par point ; zoom : caméra de scène (la plus large : 0,8) */
            float x0, z0, x1, z1, pas, zc = 0;
            if (sscanf(tok + 9, "%f:%f:%f:%f:%f:%f", &x0, &z0, &x1, &z1, &pas, &zc) >= 5) {
                int mp = 0, mv = 0, np = 0; float px = 0, pz = 0, vx = 0, vz = 0;
                for (float z = z0; z <= z1; z += pas) for (float x = x0; x <= x1; x += pas) {
                    E.sera.x = x; E.sera.z = z; E.sera_y_ok = 0;
                    for (int y = 0; y < 4; y++) {
                        E.yaw = y * 1.5708f;
                        if (zc > 0) mise_camera(0, x, z, E.yaw, zc, 0.05f);
                        for (int k = 0; k < 3; k++) frame(&none);
                        const eng_stats_t *st = eng_stats();
                        np++;
                        if (st->prims > mp) { mp = st->prims; px = x; pz = z; }
                        if (st->verts > mv) { mv = st->verts; vx = x; vz = z; }
                    }
                }
                printf("dehors : %d vues, %d primitives au plus (%.0f, %.0f), %d sommets au plus (%.0f, %.0f)%c", np, mp, px, pz, mv, vx, vz, 10);
            }
            continue;
        }
        else if (sscanf(tok, "maxstats:%d", &n) == 1) {   /* maxstats:n : n images, primitives, sommets, opérations et points d'interface au plus */
            int mp = 0, mv = 0, mo = 0, mt = 0;
            for (int k = 0; k < n; k++) {
                frame(&none);
                const eng_stats_t *st = eng_stats();
                if (st->prims > mp) mp = st->prims;
                if (st->verts > mv) mv = st->verts;
                if (st->ui_ops > mo) mo = st->ui_ops;
                if (st->ui_pts > mt) mt = st->ui_pts;
            }
            printf("au plus sur %d images : %d primitives, %d sommets, %d opérations, %d points%c", n, mp, mv, mo, mt, 10);
            continue;
        }
        else if (!strcmp(tok, "pires")) {   /* pires : primitives et sommets au plus, dans chaque intérieur, vu de sa porte et du milieu de sa grille (à compiler avec -DMAX_PRIMS=30000 -DMAX_VERTS=120000) */
            int mp = 0, mv = 0, pp = -1, pv = -1, nb = 0, ni = 0;
            for (int q = 0; q < E.w.nportes; q++) {
                if (!int_entrer_porte(q, 1) || !E.in.on) continue;
                ni++;
                for (int v = 0; v < 2; v++) {
                    if (v == 1) { float ix, iz, yw; int_position(&ix, &iz, &yw); (void)ix; (void)iz; E.yaw = yw + 3.14159f; }
                    for (int k = 0; k < 2; k++) frame(&none);
                    const eng_stats_t *st = eng_stats();
                    if (st->prims > mp) { mp = st->prims; pp = q; }
                    if (st->verts > mv) { mv = st->verts; pv = q; }
                    nb += st->prims > 3000;
                }
                int_reset();
            }
            printf("intérieurs : %d visités, %d primitives au plus (porte %d), %d sommets au plus (porte %d), %d vues au-delà de 3000 primitives%c", ni, mp, pp, mv, pv, nb, 10);
            continue;
        }
        else if (sscanf(tok, "escalier:%d", &n) == 1 && E.in.on && n >= 0 && n < E.in.nesc) {
            const int_esc_t *e = &E.in.esc[n];
            E.sera.x = IO + (e->x0 + e->x1) / 2; E.sera.z = IO + (e->z0 + e->z1) / 2;
            printf("escalier %d (vers %+d) du niveau %d\n", n, e->vers, E.in.etage);
        }
        else if (sscanf(tok, "ipos:%f:%f:%f", &a, &b, &c) == 3) { E.sera.x = IO + a; E.sera.z = IO + b; E.yaw = c; }   /* dans l'intérieur (repère du plan) */
        else if (!strcmp(tok, "iechelle") && E.in.on) {
            /* Dans un intérieur : hauteur à l'écran (pieds et tête projetés) de Sera et de chaque personnage de l'intérieur,
               profondeur depuis la caméra (la perspective rapetisse qui est plus loin) ; hauteur des meubles en mètres
               (écart des y de leurs polygones ÷ échelle humaine f) */
            const float fe = E.in.f > 0 ? E.in.f : 1, Hs = 2.0f / E.w.ech * E.w.sprite_scale;
            float x0, y0, x1, y1;
            printf("intérieur %d (bâtiment %d, %s) : échelle humaine f = %.3f ; caméra à %.2f ; Sera fait %.2f unités de cellule (%.2f m)\n",
                   E.w.bats[E.in.bat].intr, E.in.bat, E.w.bats[E.in.bat].nom, fe, E.cam_dist, Hs, Hs / fe);
            for (int k = -1; k < E.nnpc; k++) {
                if (k >= 0 && k < E.in.npc0) continue;
                const float px = k < 0 ? E.sera.x : E.npcs[k].x, pz = k < 0 ? E.sera.z : E.npcs[k].z, gy = ground_y(px, pz);
                const float t = k < 0 ? 1 : E.w.persos[E.npcs[k].perso].taille;
                if (!project(px, gy, pz, &x0, &y0) || !project(px, gy + Hs * t, pz, &x1, &y1)) continue;
                const float dx = px - E.cam.x, dy = gy - E.cam.y, dz = pz - E.cam.z;
                printf("  %-22s en %.2f %.2f : %.0f px de haut, à %.2f de la caméra\n", k < 0 ? "SERA" : E.w.persos[E.npcs[k].perso].nom, px - IO, pz - IO, y0 - y1, sqrtf(dx * dx + dy * dy + dz * dz));
            }
            for (int o = 0; o < E.in.nobj; o++) {
                const int_obj_t *ob = &E.in.obj[o];
                if (!ob->mesh) continue;
                float lo = 1e9f, hi = -1e9f;
                for (int p = ob->p0; p < ob->p0 + ob->np; p++) { const pak_poly_t *pp = int_poly(&E.in, p); for (int v = 0; v < pp->nv; v++) { if (pp->v[v].y < lo) lo = pp->v[v].y; if (pp->v[v].y > hi) hi = pp->v[v].y; } }
                printf("  meuble %d en %.2f %.2f : %.2f m de haut (%.2f × Sera)\n", o, (ob->x0 + ob->x1) / 2, (ob->z0 + ob->z1) / 2, (hi - lo) / fe, (hi - lo) / (Hs * 0.92f));
            }
        }
        else if (!strcmp(tok, "sansz")) { extern int eng_sans_z; eng_sans_z = !eng_sans_z; printf("tampon de profondeur %s\n", eng_sans_z ? "coupé" : "remis"); continue; }
        else if (sscanf(tok, "affiches:%f", &a) == 1) {   /* affiches collées (rang POLY_COLLE) des bâtiments à moins de a cases de Sera : centre, normale */
            int na = 0;
            for (int s = 0; s < CWIN * CWIN; s++) {
                const chunk_t *ch = E.w.slot[s];
                if (ch->cx < 0) continue;
                for (uint32_t k = 0; k < ch->nbld; k++) for (uint32_t m = 0; m < ch->bld[k].count; m++) {
                    const pak_poly_t *pp = chunk_poly(ch, ch->bld[k].first + m);
                    if (POLY_RANK(pp) != POLY_COLLE || pp->nv < 3) continue;
                    float x = 0, y = 0, z = 0;
                    for (int v = 0; v < pp->nv; v++) { x += pp->v[v].x; y += pp->v[v].y; z += pp->v[v].z; }
                    x /= pp->nv; y /= pp->nv; z /= pp->nv;
                    if (hypotf(x - E.sera.x, z - E.sera.z) > a) continue;
                    if (na++ < 40) printf("  affiche en %.2f %.2f (h %.2f), normale %.2f %.2f, polygone précédent rang %d\n", x, z, y, pp->n[0], pp->n[2], m ? POLY_RANK(chunk_poly(ch, ch->bld[k].first + m - 1)) : -1);
                }
            }
            printf("%d affiches à moins de %.0f cases\n", na, a);
            continue;
        }
        else if (!strcmp(tok, "iaffiches") && E.in.on) {   /* affiches au mur de l'intérieur (objets à pan de mur, o->mur) : leur fondu et celui de leur mur */
            int na = 0;
            for (int i = 0; i < E.in.nobj; i++) {
                const int_obj_t *o = &E.in.obj[i];
                if (!o->mur) continue;
                const int_obj_t *W = &E.in.obj[o->mur - 1];
                int fm = 0; for (int k = W->p0; k < W->p0 + W->np; k++) if (E.in.fade[k] > fm) fm = E.in.fade[k];
                if (na++ < 20) printf("  affiche %d en %.2f %.2f : fondu %d, son mur %d (%.2f %.2f - %.2f %.2f) jusqu'à %d\n", i, (o->x0 + o->x1) / 2, (o->z0 + o->z1) / 2, E.in.fade[o->p0], o->mur - 1, W->x0, W->z0, W->x1, W->z1, fm);
            }
            printf("%d affiches au mur\n", na);
            continue;
        }
        else if (!strcmp(tok, "iaffvue") && E.in.on) {   /* affiches au mur vues de la caméra : faces rejetées (face arrière), fondu, pan de mur choisi (int_draw_colle) et le sien */
            int na = 0, nrej = 0, nfon = 0;
            for (int i = 0; i < E.in.nobj; i++) {
                const int_obj_t *o = &E.in.obj[i];
                if (!o->mur || !o->np) continue;
                const int_obj_t *W = &E.in.obj[o->mur - 1];
                const pak_poly_t *c = int_poly(&E.in, o->p0);
                float x = 0, z = 0; for (int v = 0; v < c->nv; v++) { x += c->v[v].x; z += c->v[v].z; } x /= c->nv; z /= c->nv;
                const int wp = int_affiche_pan(&E.in, o);
                const float dcam = (E.cam.x - c->v[0].x) * c->n[0] + (E.cam.y - c->v[0].y) * c->n[1] + (E.cam.z - c->v[0].z) * c->n[2];
                const int rej = (c->flags & POLY_CULL) && dcam <= 0;
                nrej += rej; nfon += E.in.fade[o->p0] > 0; na++;
                if (rej || E.in.fade[o->p0] > 0) printf("  affiche %d (%.1f %.1f) normale %.2f %.2f : %s, fondu %d, pan trouvé %s (fondu %d)%c", i, x - IO, z - IO, c->n[0], c->n[2], rej ? "REJETÉE" : "vue", E.in.fade[o->p0], wp >= 0 ? "oui" : "NON", wp >= 0 ? E.in.fade[wp] : -1, 10);
            }
            printf("affiches : %d, rejetées %d, en fondu %d (lacet %.0f°)%c", na, nrej, nfon, E.yaw * 180 / PI_F, 10);
            continue;
        }
        else if (!strcmp(tok, "ieclaire") && E.in.on) E.in.sombre = 0;   /* intérieur éclairé (captures du cinéma, toujours éteint) */
        else if (!strncmp(tok, "tenir:", 6)) {   /* tenir:touche:n : la croix tenue n images (up, down, left, right), puis Sera : place, hauteur, niveau */
            char tk[16] = { 0 }; int nn = 0;
            if (sscanf(tok + 6, "%15[a-z]:%d", tk, &nn) == 2) {
                eng_input_t k2 = { 0 };
                k2.keys = !strcmp(tk, "up") ? ENG_KEY_UP : !strcmp(tk, "down") ? ENG_KEY_DOWN : !strcmp(tk, "left") ? ENG_KEY_LEFT : ENG_KEY_RIGHT;
                for (int k = 0; k < nn; k++) frame(&k2);
                printf("tenir %s %d : Sera en %.2f %.2f, sol %.2f, hauteur %.2f, niveau %d, case %d\n", tk, nn, E.sera.x - (E.in.on ? IO : 0), E.sera.z - (E.in.on ? IO : 0),
                       E.in.on ? int_sol_y(E.sera.x, E.sera.z) : 0, ground_y(E.sera.x, E.sera.z), E.in.on ? E.in.etage : -1, E.in.on ? int_case(E.sera.x, E.sera.z) : -1);
            }
            continue;
        }
        else if (!strcmp(tok, "igrille") && E.in.on) {
            /* Grille de marche de l'intérieur (une ligne par rangée, z croissant) : # mur ou meuble, . libre, s seuil, 3 escalier
               qui monte, 4 trémie, 5 eau ; S : Sera ; p : personnage ; hauteur du sol (int_sol_y) en chiffres a-z au-dessus de 0
               (0,1 m chacun, à l'échelle humaine) */
            const interieur_t *I = &E.in;
            printf("grille %d × %d, pas %.3f, niveau %d, échelle %.3f\n", I->gw, I->gh, I->res, I->etage, I->f);
            for (int j = 0; j < I->gh; j++) {
                char ligne[1024]; int l = 0;
                for (int i = 0; i < I->gw && l < 1020; i++) {
                    const float x = IO + (i + 0.5f) * I->res - 2, z = IO + (j + 0.5f) * I->res - 2;
                    const int v = I->grid[j * I->gw + i];
                    char ch = v == 0 ? '#' : v == 1 ? '.' : v == 2 ? 's' : (char)('0' + v);
                    const float h = int_sol_y(x, z) / (I->f > 0 ? I->f : 1);
                    if (v && h > 0.05f) ch = (char)('a' + (int)fminf(25, h / 0.1f));
                    if (fabsf(x - E.sera.x) < I->res / 2 && fabsf(z - E.sera.z) < I->res / 2) ch = 'S';
                    for (int k = I->npc0; k < I->npc0 + I->npcn; k++) if (fabsf(x - E.npcs[k].x) < I->res / 2 && fabsf(z - E.npcs[k].z) < I->res / 2) ch = 'p';
                    ligne[l++] = ch;
                }
                ligne[l] = 0;
                printf("%s\n", ligne);
            }
            continue;
        }
        else if (!strcmp(tok, "sorties")) {
            /* Sortie de chaque porte de la carte courante (interieur.c, int_point_sortie, 03/11/2026) : le point d'avant (0,4 case
               devant celui de la porte) dans la marge du corps d'un mur (Sera coincée) ou dans un solide (repli là où elle était
               entrée), et le point de maintenant */
            int nb = 0, marge = 0, solide = 0, sans = 0; float amax = 0;
            for (int k = 0; k < E.w.nportes; k++) {
                const int_porte_t *d = &E.w.portes[k];
                if (E.w.bats[d->bat].carte != E.w.carte) continue;
                const float x = d->x + d->nx * 0.4f, z = d->z + d->nz * 0.4f;
                world_stream(x, z, 1);
                const chunk_t *c = chunk_of_cell((int)floorf(x), (int)floorf(z));
                int dans = 0;
                for (uint32_t q = 0; c && q < c->nsolid; q++) { const float *r = &c->solids[q * 4]; if (x > r[0] && x < r[2] && z > r[1] && z < r[3]) dans = 1; }
                nb++;
                if (dans) solide++; else if (!sera_libre(x, z)) { marge++; if (marge <= 12) printf("  porte %d (%s) : point de sortie dans la marge d'un mur en %.2f %.2f\n", k, E.w.bats[d->bat].nom, x, z); }
                float nx2, nz2;
                if (!int_point_sortie(d, &nx2, &nz2)) { sans++; printf("  porte %d (%s) : aucun point libre devant elle\n", k, E.w.bats[d->bat].nom); }
                else { const float a = (nx2 - d->x) * d->nx + (nz2 - d->z) * d->nz; if (a > amax) amax = a; }
            }
            printf("sorties : %d portes ; avant : %d dans la marge d'un mur (coincée), %d dans un solide (repli à l'entrée) ; maintenant : %d sans point libre devant (repli), recul max %.2f case\n", nb, marge, solide, sans, amax);
            continue;
        }
        else if (!strcmp(tok, "portes")) {
            printf("%d bâtiments visitables, %d portes, %d intérieurs\n", E.w.nbats, E.w.nportes, E.w.intr_n);
            for (int k = 0; k < E.w.nportes && k < 400; k++) { const int_porte_t *d = &E.w.portes[k]; if (!d->di) printf("  porte %d : bâtiment %d mode %d (%.1f %.1f) %s\n", k, d->bat, E.w.bats[d->bat].mode, d->x, d->z, E.w.bats[d->bat].nom); }
        }
        else if (!strcmp(tok, "coffre")) {   /* coffre d'essai (coffres.c) : dedans, à 0,8 m devant Sera ; dehors, un trésor à une case devant elle */
            const float fx = sinf(E.sera.dir), fz = cosf(E.sera.dir);
            if (E.in.on) {
                const float fe = E.in.f > 0 ? E.in.f : 1;
                const int_bat_t *bt = &E.w.bats[E.in.bat];
                E.in.ncof = 1; E.in.cof_sorte = 0; E.in.cof[0] = E.sera.x - IO + fx * 0.8f * fe; E.in.cof[1] = E.sera.z - IO + fz * 0.8f * fe;
                E.cof_ouv[E.in.bat >> 3] &= (uint8_t)~(1u << (E.in.bat & 7));
                printf("coffre d'essai : %s, mode %d (2 fermé, 3 scellé), gamme %d-%d, niveau %d : palier %d%c", bt->nom, bt->mode, bt->gmin, bt->gmax, E.level, coffre_palier(E.in.bat), 10);
            } else {
                static uint8_t tres[52];
                const float x = E.sera.x + fx * 1.0f / E.w.ech, z = E.sera.z + fz * 1.0f / E.w.ech;
                const int po = item_potion();
                memset(tres, 0, sizeof tres); memcpy(tres, &x, 4); memcpy(tres + 4, &z, 4);
                tres[8] = (uint8_t)E.w.carte; tres[9] = po >= 0 ? 1 : 0; tres[10] = tres[11] = 0xFF; tres[12] = 250;   /* 250 crédits, aucun drapeau */
                memcpy(tres + 16, "TRÉSOR D'ESSAI", strlen("TRÉSOR D'ESSAI"));
                tres[40] = (uint8_t)(po >= 0 ? po : 255); tres[41] = 2;
                for (int k = 1; k < 6; k++) tres[40 + k * 2] = 255;
                E.w.tres = tres; E.w.ntres = 1; E.tres_ouv[0] &= (uint8_t)~1u;
                printf("trésor d'essai en %.2f %.2f%c", x, z, 10);
            }
        }
        else if (!strcmp(tok, "coffre_etat")) printf("coffre à portée %d, panneau %d, crédits %d, interlocuteur %d%c", E.coffre, hud_butin_on(), E.credits, E.talk, 10);
        else if (!strcmp(tok, "marques")) {   /* marques des personnages (panneaux.c) : calcul refait et mesuré, puis chacun, avec son exemplaire le plus proche */
            static const char *MQN[5] = { "", "! (ouvre)", "? (étape)", "! principale", "? principale" };
            int nm[5] = { 0 };
            marques_refaire();
            const uint32_t t0 = clock_pc(); (void)marque_de(0); const uint32_t t1 = clock_pc();
            printf("marques : %d ouvertures dans le scénario, calcul %u µs (mesuré %u µs)\n", ouvertures_n(), (unsigned)marques_us(), (unsigned)(t1 - t0));
            for (int p = 0; p < E.w.npersos; p++) {
                const int m = marque_de(p);
                if (!m) continue;
                nm[m]++;
                float bd = 1e9f; int bi = -1;
                for (int i = 0; i < E.nnpc; i++) if (E.npcs[i].perso == p && npc_present(&E.npcs[i])) { const float d = hypotf(E.npcs[i].x - E.sera.x, E.npcs[i].z - E.sera.z); if (d < bd) { bd = d; bi = i; } }
                if (bi >= 0) printf("  %s %s : exemplaire %d en %.1f %.1f, à %.1f cases\n", MQN[m], E.w.persos[p].nom, bi, E.npcs[bi].x, E.npcs[bi].z, bd);
                else printf("  %s %s : pas d'exemplaire présent sur cette carte\n", MQN[m], E.w.persos[p].nom);
            }
            printf("  bilan : %d « ! », %d « ? », %d « ! » principales, %d « ? » principales\n", nm[1], nm[2], nm[3], nm[4]);
            continue;
        }
        else if (!strcmp(tok, "marquesvues")) { static char mv[2048]; hud_marques_vues(mv, sizeof mv); printf("marques : %s\n", mv); continue; }   /* badges dessinés à la dernière image */
        else if (!strcmp(tok, "panneaux")) {   /* panneaux de quêtes (PANQ) : lieu, carte, quêtes ouvrables */
            printf("panneaux de quêtes : %d\n", panneau_n());
            for (int k = 0; k < panneau_n(); k++) { float px, pz; int pc; panneau_lieu(k, &px, &pz, &pc); printf("  %d. %s, carte %d, %.1f %.1f : %d quête(s)\n", k, panneau_nom(k), pc, px, pz, panneau_quetes(k)); }
            continue;
        }
        else if (sscanf(tok, "panneau:%d", &n) == 1) {   /* Sera à une case au sud du panneau n (sur sa carte), tournée vers lui ; son écran ouvert */
            float px, pz; int pc;
            if (!panneau_lieu(n, &px, &pz, &pc)) { printf("panneau %d : aucun\n", n); continue; }
            if (pc != E.w.carte) world_carte(pc, px, pz + 1.0f / E.w.ech);
            eng_teleport(px, pz + 1.0f / E.w.ech, PI_F);
            frame(&none); frame(&none);
            static char pt[4096];
            hud_panneau_ouvrir(n); hud_panneau_texte(pt, sizeof pt);
            printf("%s (à portée : %d)\n", pt, E.panneau);
        }
        else if (sscanf(tok, "panchoix:%d", &n) == 1) {   /* la ligne n de l'écran du panneau choisie : guidage (ou quête d'affichage) */
            float gx = 0, gz = 0;
            hud_panneau_choisir(n);
            const int g = panneau_guide(&gx, &gz);
            printf("panneau, ligne %d : guidage %s %s (%.1f %.1f, à %.1f cases), script %s ; message : %s\n", n, g ? "vers" : "aucun ici,", panneau_guide_nom(), gx, gz, hypotf(gx - E.sera.x, gz - E.sera.z), script_running() ? "en cours" : "aucun", hud_toast_txt());
        }
        else if (sscanf(tok, "coffre_tirages:%d", &n) == 1 && E.in.on && n > 0) {   /* n fouilles du coffre de l'intérieur (sans rien garder) */
            static int c0[MAX_ITEMS], acc[MAX_ITEMS];
            const int cr0 = E.credits, bat = E.in.bat;
            for (int i = 0; i < n_items; i++) { c0[i] = ITEMS[i].count; acc[i] = 0; }
            for (int k = 0; k < n; k++) {
                E.cof_ouv[bat >> 3] &= (uint8_t)~(1u << (bat & 7)); E.coffre = COFFRE_INT; coffre_ouvrir();
                for (int i = 0; i < n_items; i++) { acc[i] += ITEMS[i].count - c0[i]; ITEMS[i].count = c0[i]; }
            }
            int eq = 0; long soins = 0;
            for (int i = 0; i < n_items; i++) {
                if (acc[i] > 0) printf("  %s : %d (%s)%c", ITEMS[i].name, acc[i], eq_item(i) ? "équipement" : "soin", 10);
                if (eq_item(i)) eq += acc[i]; else soins += (long)acc[i] * ITEMS[i].prix;
            }
            printf("%d fouilles au palier %d : %.0f crédits, soins %.0f, %.1f %% avec un équipement%c", n, coffre_palier(bat), (E.credits - cr0) / (float)n, soins / (float)n, 100.0f * eq / n, 10);
            E.credits = cr0; E.cof_ouv[bat >> 3] &= (uint8_t)~(1u << (bat & 7));
        }
        else if (!strcmp(tok, "alarme")) { E.alarme.on = 1; E.alarme.t = 0; E.alarme.duree = 20; E.alarme.x = E.in.on ? E.in.rx : E.sera.x; E.alarme.z = E.in.on ? E.in.rz : E.sera.z; E.alarme.amende = 100; }
        else if (sscanf(tok, "pluie:%f", &a) == 1) { E.weather = a > 0 ? 2 : 1; E.rain = a; }
        else if (!strcmp(tok, "cendre")) {   /* pluie de cendre (game.c, cendre_update) : intensité, PV de l'équipe, passants à l'abri */
            int abri = 0, pres = 0, passants = 0, proches = 0; float pd = 1e9f, px = 0, pz = 0;
            for (int k = 0; k < E.nnpc; k++) {
                const npc_t *q = &E.npcs[k];
                const int fl = E.w.persos[q->perso].flags;
                abri += q->abri; pres += npc_present(q);
                if ((fl & PERS_FLANE) && PERS_GENRE(fl) && !q->meneur && !q->chef && !q->depart && !q->fuit) {   /* passant (abri_maj) */
                    const float d = hypotf(q->x - E.sera.x, q->z - E.sera.z);
                    passants++; proches += d < 24.0f / E.w.ech;
                    if (d < pd) { pd = d; px = q->x; pz = q->z; }
                }
            }
            printf("cendre %.2f dehors %.2f, exposition %.1f s, dernier coup il y a %.2f s :", E.rain, cendre_dehors(), E.cendre_t, E.clock - E.cendre_coup);
            for (int m = 0; m < NMEMB; m++) if (in_team(m)) printf(" %s %d/%d", PARTY[m].name, PARTY[m].hp, PARTY[m].max);
            printf(", %d exemplaires présents, %d passants (%d à moins de 24 m, le plus proche en %.1f %.1f à %.1f cases), %d à l'abri",
                   pres, passants, proches, px, pz, pd, abri);
            for (int i = 0; i < n_items; i++) if (ITEMS[i].name && !strcmp(ITEMS[i].name, "MASQUE À FILTRE")) printf(", masque (objet %d) × %d", i, ITEMS[i].count);
            printf("%c", 10);
        }
        else if (sscanf(tok, "heure:%f", &a) == 1) { E.heure = a; E.cycle = 0; }   /* cycle jour / nuit : 0 minuit, 0,5 midi */
        else if (sscanf(tok, "horloge:%d", &n) == 1) { eng_set_horloge(n); if (n >= 0) E.cycle = 3; }   /* heure réelle (montre), cycle RÉEL */
        else if (!strcmp(tok, "verrou")) { eng_set_date(29, 10, 4); eng_set_batterie(78, 0); eng_verrouiller(); }   /* montre verrouillée (cadran) */
        else if (sscanf(tok, "arbre:%d", &n) == 1) {
            /* Sera 1,2 case derrière le n-ième grand décor (plus de 1,2 de haut) des chunks chargés, vu de la caméra */
            int found = 0;
            for (int s = 0; s < CWIN * CWIN && !found; s++) {
                const chunk_t *ch = E.w.slot[s];
                if (ch->cx < 0) continue;
                for (uint32_t k = 0; k < ch->nplant && !found; k++) {
                    const pak_plant_t *p = &ch->plants[k];
                    if (p->size <= 1.2f || hypotf(p->x - E.sera.x, p->z - E.sera.z) > 12 || n-- > 0) continue;
                    E.sera.x = p->x + sinf(E.yaw) * 1.2f; E.sera.z = p->z + cosf(E.yaw) * 1.2f; found = 1;
                    printf("arbre en %.1f %.1f (hauteur %.1f), Sera derrière\n", p->x, p->z, p->size);
                }
            }
        }
        else if (sscanf(tok, "wait:%d", &n) == 1) { for (int k = 0; k < n - 1; k++) frame(&none); }
        else if (!strncmp(tok, "cam:", 4)) {   /* cam:x:z : caméra de scène vers (x, z), coupe franche (chunks là-bas : world_stream) */
            float cx2, cz2;
            if (sscanf(tok + 4, "%f:%f", &cx2, &cz2) == 2) { mise_camera(0, cx2, cz2, -10, 0, 0.05f); frame(&none); }
        }
        else if (!strncmp(tok, "mise:", 5)) {   /* le script de serveur/essai-mise.js, par les fonctions de game.c */
            int p = -1;
            for (int q = 0; q < E.w.npersos && p < 0; q++) if (E.w.persos[q].nom && !strcmp(E.w.persos[q].nom, tok + 5)) p = q;
            if (p < 0) { fprintf(stderr, "personnage inconnu : %s\n", tok + 5); return 1; }
            const float x0 = E.sera.x, z0 = E.sera.z, deg = PI_F / 180;
            const float inst[] = { 0.5f, 1.2f, 2.5f, 3.3f, 3.8f, 5.0f, 6.5f, 8.0f, 10, 14 };
            int ph = 0, ii = 0;
            float t = 0, tw = 0;   /* tw : fin de l'attente en cours (caméra, fondu) */
            acteur_poser(p, x0 + 4, z0 + 4, atan2f(-4, -4));
            mise_camera(0, x0 + 2, z0 + 2, 45 * deg, 1.8f, 1); tw = 1;
            while (ii < 10) {
                const int k = acteur_de(p);
                if (t >= tw && !mise_marche(-2) && !MISE.fuite_on) switch (ph++) {   /* instruction suivante (comme le script) */
                    case 0: MISE.act[k].tx = x0 + 1; MISE.act[k].tz = z0 + 1; MISE.act[k].v = 1; MISE.act[k].walking = 1; break;
                    case 1: E.sera.dir = atan2f(MISE.act[k].x - E.sera.x, MISE.act[k].z - E.sera.z); MISE.sec_f = 2; MISE.sec_t = 0.5f;
                            MISE.fondu_a = MISE.fondu; MISE.fondu_b = 1; MISE.fondu_t = 0; MISE.fondu_d = 0.5f; tw = t + 0.5f; break;
                    case 2: MISE.fondu_a = MISE.fondu; MISE.fondu_b = 0; MISE.fondu_t = 0; MISE.fondu_d = 0.5f; tw = t + 0.5f; break;
                    case 3: MISE.sera_on = 1; MISE.sera_tx = x0 - 2; MISE.sera_tz = z0; MISE.sera_v = 2; break;
                    case 4: mise_camera(1, 0, 0, 0, 0, 0.8f); tw = t + 0.8f; break;
                    case 5: mise_fuite(x0 - 6, z0, 1.5f, 8, p, 2); break;
                    case 6: printf("fuite %s\n", MISE.fuite_ok ? "réussie" : "ratée"); mise_fin(0); break;
                    default: break;
                }
                frame(&none); t += 1 / 30.0f;
                if (t >= inst[ii]) {
                    printf("%.1f s cam %s %.2f %.2f %.2f %.2f | sera %.2f %.2f %d | fondu %.2f | fuite %.1f |", inst[ii], MISE.cam_on ? "oui" : "non", MISE.cam[0], MISE.cam[1], MISE.cam[2], MISE.cam[3],
                           E.sera.x, E.sera.z, E.sera.state, MISE.fondu, MISE.fuite_on ? MISE.fuite_t : -1);
                    for (int a = 0; a < ACT_MAX; a++) if (MISE.act[a].on) printf(" [%.2f %.2f %s%s]", MISE.act[a].x, MISE.act[a].z, MISE.act[a].walking ? (MISE.act[a].court ? "run" : "walk") : "idle", MISE.act[a].chasse ? " chasse" : "");
                    printf("\n");
                    ii++;
                }
            }
            continue;
        }
        else if (!strncmp(tok, "cible:", 6)) {   /* cible:NOM:ETAPE : cible de chasse (CIBL) à 3 cases devant la caméra, quête 0 à
                                                    l'étape 2 ; la quête 0 mise à ETAPE (comme serveur/essai-cibles.js) */
            char nom[64]; snprintf(nom, sizeof nom, "%s", tok + 6);
            char *dp = strrchr(nom, ':'); const int st = dp ? atoi(dp + 1) : 2; if (dp) *dp = 0;
            int p = -1;
            for (int pq = 0; pq < E.w.npersos && p < 0; pq++) if (E.w.persos[pq].nom && !strcmp(E.w.persos[pq].nom, nom)) p = pq;
            if (p < 0 || !E.w.sc_nq) { fprintf(stderr, "personnage inconnu ou scénario sans quête : %s\n", nom); return 1; }
            cible_t *cb = &E.w.cibles[0]; E.w.ncibles = 1;
            cb->perso = (uint8_t)p; cb->carte = (uint8_t)E.w.carte; cb->quest = 0; cb->step = 2;
            cb->x = E.sera.x + E.cam.sy * 3; cb->z = E.sera.z + E.cam.cy * 3;
            E.quests[0] = (uint8_t)st;
            for (int k = 0; k < 3; k++) frame(&none);
            printf("cible %s en %.2f %.2f, quête 0 à l'étape %d (cible à l'étape 2)\n", nom, cb->x, cb->z, st);
            continue;
        }
        else if (!strncmp(tok, "palette:", 8)) {   /* palette:NOM : palette d'un combat du scénario contre NOM (celle de ce
                                                      qu'on voit : palette_vue de battle.c), puis 12 rencontres (au hasard) */
            int p = -1, k = -1;
            for (int pq = 0; pq < E.w.npersos && p < 0; pq++) if (E.w.persos[pq].nom && !strcmp(E.w.persos[pq].nom, tok + 8)) p = pq;
            for (int q = 0; q < E.w.nenemies && k < 0; q++) if (E.w.enemies[q].perso == p) k = q;
            if (k < 0) { fprintf(stderr, "ennemi inconnu : %s\n", tok + 8); return 1; }
            if (!E.w.ncibles) for (int j = 0; j < E.nnpc; j++) if (E.npcs[j].perso == p && perso_present(p)) {   /* sans cible : à côté d'un exemplaire posé */
                E.sera.x = E.npcs[j].x + 2; E.sera.z = E.npcs[j].z;
                printf("exemplaire posé en %.1f %.1f, palette %d ; ", E.npcs[j].x, E.npcs[j].z, E.npcs[j].var);
                break;
            }
            printf("%s (%d palettes en plus de la base) : combat du scénario", tok + 8, E.w.persos[p].nvar);
            for (int r = 0; r < 4; r++) { battle_start(k, 5, 1); printf(" %d", BT.en[0].var); BT.on = 0; }
            printf(" ; rencontres");
            for (int r = 0; r < 12; r++) { battle_start(k, 5, 0); printf(" %d", BT.en[0].var); BT.on = 0; }
            printf("\n");
            continue;
        }
        else if (!strncmp(tok, "hscript:", 8)) {   /* hscript:21.5 : comme l'instruction « heure » (OP_HOUR de script.c), en heures */
            const float h = (float)atof(tok + 8) / 24;
            if (E.cycle != 3) E.heure = h - floorf(h);
            for (int k = 0; k < 3; k++) frame(&none);
            printf("heure %.4f\n", E.heure);
            continue;
        }
        else if (sscanf(tok, "trafmax:%d", &n) == 1) { E.w.traf_max = n; printf("trafic : %d véhicules au plus\n", n); }
        else if (!strcmp(tok, "passages")) {   /* passage de toutes les enceintes (ENCE) accordé : Lucerne sans interception */
            const uint8_t *ep = E.w.ence;
            for (int k = 0; ep && k < E.w.nence; k++) {
                const int fl = ep[8] | ep[9] << 8, na = ep[10] | ep[11] << 8, np = ep[12] | ep[13] << 8;
                if (fl < SC_MAX_FLAGS) E.flags[fl >> 3] |= (uint8_t)(1 << (fl & 7));
                ep += 40 + na * 4 + np * 16;
            }
        }
        else if (sscanf(tok, "circulation:%d", &n) == 1) { circulation(n); continue; }
        else if (sscanf(tok, "pietons:%d", &n) == 1) { pietons(n); continue; }
        else if (sscanf(tok, "vue:%f:%f:%f", &a, &b, &c) == 3) {   /* vue:x:z:lacet[:zoom[:hauteur]] : caméra de scène posée là (captures) */
            float zv = 1.2f, hv = 0;
            sscanf(tok, "vue:%*f:%*f:%*f:%f:%f", &zv, &hv);
            mise_camera(0, a, b, c, zv, 0.05f);
            if (hv > 0) MISE.cam_b[4] = hv;
            for (int k = 0; k < 4; k++) frame(&none);
        }
        else if (!strncmp(tok, "croiser:", 8)) { croiser(tok + 8); continue; }
        else if (!strcmp(tok, "voiture")) {
            /* Sera 3 cases derrière le premier véhicule du trafic sur un axe, caméra dans son sens de marche */
            int k = 0;
            while (k < TRAFFIC_MAX && !(E.traffic[k].on && E.traffic[k].lane)) k++;
            if (k == TRAFFIC_MAX) { printf("aucun véhicule sur un axe\n"); continue; }
            const traffic_t *t = &E.traffic[k];
            E.sera.x = t->x - sinf(t->dir) * 3; E.sera.z = t->z - cosf(t->dir) * 3; E.yaw = t->dir;
            printf("derrière le trafic %d en %.1f %.1f\n", k, t->x, t->z);
        }
        else if (sscanf(tok, "son:%d", &n) == 1) {
            /* Joue le son n, mixe 0,5 s à 24 kHz : crête et énergie */
            static int16_t buf[12000];
            son_jouer(n, 1, 1);
            eng_audio_mix(buf, 12000, 24000);
            double e = 0; int pk = 0;
            for (int i = 0; i < 12000; i++) { e += (double)buf[i] * buf[i]; if (abs(buf[i]) > pk) pk = abs(buf[i]); }
            printf("son %d : crête %d, efficace %.0f\n", n, pk, sqrt(e / 12000));
        }
        else if (!strcmp(tok, "mix")) {
            /* Mixe 0,2 s à 24 kHz de ce qui joue déjà (boucles des moteurs comprises) : crête et énergie */
            static int16_t buf[4800];
            eng_audio_mix(buf, 4800, 24000);
            double e = 0; int pk = 0;
            for (int i = 0; i < 4800; i++) { e += (double)buf[i] * buf[i]; if (abs(buf[i]) > pk) pk = abs(buf[i]); }
            printf("mix : crête %d, efficace %.0f (véhicule conduit %d, course %d)\n", pk, sqrt(e / 4800), E.ride, CR.on);
        }
        else if (!strcmp(tok, "vol")) {
            int k = 0;
            while (k < TRAFFIC_MAX && !E.traffic[k].on) k++;
            printf("vol du trafic %d : %s\n", k, k < TRAFFIC_MAX && vehicle_steal(k) ? "conduit" : "échec");
        }
        else if (sscanf(tok, "casse:%d", &n) == 1) {
            /* Monte dans le premier véhicule placé, se met 4 cases avant le n-ième décor solide (drapeau 4) des chunks
               chargés, face à lui, et fonce dessus (au plus 3 s) : le décor casse-t-il ? */
            int k = 0;
            while (k < E.nnpc && !(E.w.persos[E.npcs[k].perso].flags & PERS_VEHICULE)) k++;
            if (k == E.nnpc) { printf("aucun véhicule placé\n"); continue; }
            E.sera.x = E.npcs[k].x; E.sera.z = E.npcs[k].z;
            if (!vehicle_board(k)) { printf("montée impossible\n"); continue; }
            world_stream(E.sera.x, E.sera.z, 1);
            const pak_plant_t *cible = NULL;
            for (int s = 0; s < CWIN * CWIN && !cible; s++) {
                const chunk_t *ch = E.w.slot[s];
                if (ch->cx < 0) continue;
                for (uint32_t m = 0; m < ch->nplant && !cible; m++) if ((ch->plants[m].flags & 4) && n-- <= 0) cible = &ch->plants[m];
            }
            if (!cible) { printf("aucun décor solide\n"); continue; }
            const float px = cible->x, pz = cible->z, a = atan2f(px - E.sera.x, pz - E.sera.z);
            E.sera.x = px - sinf(a) * 4; E.sera.z = pz - cosf(a) * 4; E.sera.dir = E.yaw = a; E.ride_v = 0;
            world_stream(E.sera.x, E.sera.z, 1);
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            float t, dx, dz; int f2 = 0;
            while (f2 < 90 && !decor_casse(px, pz, &t, &dx, &dz)) { frame(&up); f2++; }
            printf("décor %s en %.1f %.1f : %s après %d images, v %.2f, Sera à %.1f case\n", cible->flags & 8 ? "rocher" : "arbre", px, pz,
                   f2 < 90 ? "cassé" : "intact", f2, E.ride_v, hypotf(E.sera.x - px, E.sera.z - pz));
            /* Sera 4 cases à côté du décor, caméra vers lui : la chute se voit de profil */
            E.ride_v = 0; E.sera.x = px - cosf(a) * 4; E.sera.z = pz + sinf(a) * 4; E.yaw = a + PI_F / 2;
        }
        else if ((m2 = 150, sscanf(tok, "lac:%d:%d", &n, &m2)) >= 1 || sscanf(tok, "riviere:%d:%d", &n, &m2) >= 1) {
            /* Véhicule ECHOES_VEH (sinon la première voiture) à 4 cases du bord de l'eau la plus proche (lac de plus de 2 m,
               ou rivière), tourné vers elle : n images gaz tenu, puis 150 sans rien ; état toutes les 15 images */
            const int riv = tok[0] == 'r';
            int p = -1;
            for (int q = 0; q < E.w.npersos && p < 0; q++)
                if ((E.w.persos[q].flags & PERS_VEHICULE) && (getenv("ECHOES_VEH") ? !strcmp(E.w.persos[q].nom, getenv("ECHOES_VEH")) : VEH_TYPE(E.w.persos[q].flags) == VEH_VOITURE)) p = q;
            if (p < 0) { printf("aucun véhicule\n"); continue; }
            int li = -1, lj = -1; float bd = 1e9f;
            for (int j = (int)E.sera.z - 40; j <= (int)E.sera.z + 40; j++)
                for (int i = (int)E.sera.x - 40; i <= (int)E.sera.x + 40; i++) {
                    const chunk_t *ch = chunk_of_cell(i, j);
                    if (!ch) continue;
                    const uint8_t fl = ch->flags[cell_local(i, j)];
                    if ((fl & F_ROAD) || !(riv ? (fl & F_RIVER) : (fl & F_WATER) && !(fl & F_RIVER) && eau_prof(i + 0.5f, j + 0.5f) > 2 / E.w.ech)) continue;
                    const float d = hypotf(i + 0.5f - E.sera.x, j + 0.5f - E.sera.z);
                    if (d < bd) { bd = d; li = i; lj = j; }
                }
            if (li < 0) { printf("pas d'eau à moins de 40 cases\n"); continue; }
            float ux = E.sera.x - (li + 0.5f), uz = E.sera.z - (lj + 0.5f), ul = hypotf(ux, uz), sx = li + 0.5f, sz = lj + 0.5f;
            if (ul < 0.01f) { ux = 1; uz = 0; ul = 1; }
            ux /= ul; uz /= ul;
            while (eau_prof(sx, sz) > 0) { sx += ux * 0.25f; sz += uz * 0.25f; }   /* jusqu'à la rive */
            sx += ux * 4; sz += uz * 4;
            const int k = vehicle_spawn(p, sx, sz, atan2f(-ux, -uz));
            E.sera.x = sx; E.sera.z = sz; world_stream(sx, sz, 1);
            if (k < 0 || !vehicle_board(k)) { printf("montée impossible\n"); continue; }
            E.yaw = E.sera.dir;
            printf("%s : %s (roule %d) en %.1f %.1f, eau en %d %d\n", riv ? "rivière" : "lac", E.w.persos[p].nom, veh_rolls(p), sx, sz, li, lj);
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            for (int f2 = 1; f2 <= n + m2; f2++) {
                frame(f2 <= n ? &up : &none);
                if (f2 % 15 == 0) printf("  %3d : %s en %.1f %.1f, v %.2f, eau %.2f m, coule %.2f, alt %.2f m, nage %d\n", f2, E.ride >= 0 ? "conduit" : "à pied",
                                         E.sera.x, E.sera.z, E.ride_v, eau_prof(E.sera.x, E.sera.z) * E.w.ech, E.ride_coule, E.ride_alt * E.w.ech, E.nage);
                if (f2 == n + m2 || (E.ride < 0 && f2 % 15 == 0)) break;
            }
        }
        else if (sscanf(tok, "conduite:%d", &n) == 1) {
            /* Monte dans le premier véhicule placé, roule n images vers le haut, puis lâche : vitesse toutes les 5 images */
            int k = 0;
            while (k < E.nnpc && !(E.w.persos[E.npcs[k].perso].flags & PERS_VEHICULE)) k++;
            if (k == E.nnpc) { printf("aucun véhicule placé\n"); continue; }
            E.sera.x = E.npcs[k].x; E.sera.z = E.npcs[k].z;
            if (!vehicle_board(k)) { printf("montée impossible\n"); continue; }
            printf("conduite : %s en %.1f %.1f\n", E.w.persos[E.npcs[k].perso].nom, E.sera.x, E.sera.z);
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            for (int f2 = 1; f2 <= n + 30; f2++) { frame(f2 <= n ? &up : &none); if (f2 % 5 == 0) printf("%s%.2f", f2 == 5 ? "  v :" : " ", E.ride_v); }
            printf("\n");
        }
        else if (sscanf(tok, "vol:%d", &n) == 1) {
            /* Vol permanent (système solaire) : gaz tenu n images ; état toutes les 60 images (à bord, position, vitesse,
               altitude, carte), arrêt à la première rencontre ; puis le coût du rendu (moyenne et pire image) */
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            double s3 = 0, sui = 0, pire = 0, tris = 0; int nf = 0;
            for (int f2 = 1; f2 <= n; f2++) {
                frame(&up);
                if (BT.on) { char gn[72]; bt_groupe_nom(gn, sizeof gn); printf("  %4d : rencontre, %s niveau %d\n", f2, gn, BT.lvl); break; }
                const eng_stats_t *st = eng_stats();
                s3 += st->us_3d; sui += st->us_ui; tris += st->tris; nf++;
                if (st->us_3d + st->us_ui > pire) pire = st->us_3d + st->us_ui;
                if (f2 % 60 == 0) printf("  %4d : carte %d, %s, en %.1f %.1f, v %.2f, alt %.2f, cap %.2f\n", f2, E.w.carte, E.ride >= 0 ? "à bord" : "à pied", E.sera.x, E.sera.z, E.ride_v, E.ride_alt, E.sera.dir);
            }
            if (nf) printf("  rendu en vol (%d images) : 3D %.2f ms, interface %.2f ms, pire %.2f ms ; %.0f triangles\n", nf, s3 / nf / 1000, sui / nf / 1000, pire / 1000, tris / nf);
        }
        else if (sscanf(tok, "cap:%f", &a) == 1) { E.sera.dir = a; E.yaw = a; if (E.ride >= 0) E.npcs[E.ride].dir = a; continue; }   /* Sera (et son véhicule) tournés, caméra derrière */
        else if (!strcmp(tok, "astres")) {   /* astres de la carte du système (ASTR), visités ou non */
            for (int k = 0; k < E.w.nastres; k++) { const astre_t *A = &E.w.astres[k]; printf("astre %d : %s, carte %d, vers %d, visité %d, (%.1f, %.1f) r %.1f, orbite (%.1f, %.1f)\n", k, A->nom, A->carte, A->vers, astre_visite(k), A->x, A->z, A->r, A->ax, A->az); }
            printf("vol permanent : %d, à bord : %d, alt %.2f, Sera en %.1f %.1f cap %.2f\n", vol_permanent(), E.ride, E.ride_alt, E.sera.x, E.sera.z, E.sera.dir);
            continue;
        }
        else if (sscanf(tok, "course:%d", &n) == 1) {
            /* Monte dans le premier véhicule placé, va sur la ligne de départ, lance la course et pilote n images (W tenu,
               braquage vers l'axe 3 cases devant) : état toutes les 60 images, classement à la fin */
            int k = 0;
            while (k < E.nnpc && !(E.w.persos[E.npcs[k].perso].flags & PERS_VEHICULE)) k++;
            if (k == E.nnpc && getenv("ECHOES_VEH")) {   /* ECHOES_VEH=NOM : un exemplaire de ce véhicule (aucun placé : carte de l'histoire) */
                for (int p = 0; p < E.w.npersos && k == E.nnpc; p++)
                    if ((E.w.persos[p].flags & PERS_VEHICULE) && !strcmp(E.w.persos[p].nom, getenv("ECHOES_VEH"))) { const int sv = vehicle_spawn(p, E.sera.x, E.sera.z, 0); if (sv >= 0) k = sv; }
            }
            if (k == E.nnpc || !PISTE.n) { printf("course : %s\n", PISTE.n ? "aucun véhicule placé" : "pas de piste (PIS2)"); continue; }
            E.sera.x = E.npcs[k].x; E.sera.z = E.npcs[k].z;
            vehicle_board(k); E.ride_phase = 0; E.ride_alt = 0;
            E.sera.x = PISTE.X[PISTE.dep]; E.sera.z = PISTE.Z[PISTE.dep]; world_stream(E.sera.x, E.sera.z, 1);
            frame(&none);
            const int ok = course_ok(), dep = course_start(E.npcs[k].perso, PISTE.tours, NULL, getenv("ECHOES_RIVAUX") ? atoi(getenv("ECHOES_RIVAUX")) : COURSE_N - 1, 1, 1);   /* ECHOES_RIVAUX : adversaires */
            printf("course : drapeau %d, départ %d (%s, piste %d points, %.1f cases)\n", ok, dep, E.w.persos[E.npcs[k].perso].nom, PISTE.n, PISTE.L);
            int ntq = 0, tq0 = 0; float slipmax = 0;   /* tête-à-queue du joueur, plus fort dérapage */
            int pf_n = 0, pf_maxf = 0; double pf_bu = 0, pf_fx = 0, pf_3d = 0, pf_ui = 0, pf_tris = 0, pf_px = 0, pf_spr = 0, pf_ops = 0, pf_cat[5] = { 0 }, pf_max = 0;
            for (int f2 = 1; f2 <= n && CR.on; f2++) {
                const racer_t *j = &CR.r[CR.n - 1];
                if (j->spin > 0 && !tq0) { ntq++; printf("  tête-à-queue à l'image %d : dérapage %.2f rad, rotation %.1f rad/s, v %.1f\n", f2, j->slip, j->w, j->spd); }
                tq0 = j->spin > 0; if (fabsf(j->slip) > slipmax) slipmax = fabsf(j->slip);
                const int e = (j->seg + 6) % PISTE.n;
                float ang = atan2f(PISTE.X[e] - j->x, PISTE.Z[e] - j->z) - j->dir;
                ang = atan2f(sinf(ang), cosf(ang));
                eng_input_t dr = { 0 };
                dr.keys = ENG_KEY_UP | (ang > 0.12f ? ENG_KEY_RIGHT : ang < -0.12f ? ENG_KEY_LEFT : 0);
                /* ECHOES_PILOTE=gaz : gaz seul, sans direction ; =hasard : gauche, droite ou rien tiré toutes les 15 images
                   (pilote très imprécis) */
                const char *pm = getenv("ECHOES_PILOTE");
                if (pm && !strcmp(pm, "gaz")) dr.keys = ENG_KEY_UP;
                else if (pm && !strcmp(pm, "hasard")) {
                    static uint32_t hs = 12345, gk;
                    if (f2 % 15 == 1) { hs = hs * 1103515245u + 12345u; gk = (hs >> 16) % 3; }
                    dr.keys = ENG_KEY_UP | (gk == 1 ? ENG_KEY_LEFT : gk == 2 ? ENG_KEY_RIGHT : 0);
                }
                if (CR.phase == CP_RESULTATS) dr.keys = 0;
                frame(&dr);
                if (CR.phase == CP_COURSE) {   /* coût du rendu pendant la course : moyennes et pire image */
                    const eng_stats_t *st = eng_stats();
                    const double u = st->us_3d + st->us_ui;
                    int nfx = 0; for (int q = 0; q < COURSE_FX; q++) nfx += CR.fx[q].t < CR.fx[q].life;
                    pf_n++; pf_bu += us_build; pf_fx += nfx; pf_3d += st->us_3d; pf_ui += st->us_ui; pf_tris += st->tris; pf_px += st->px; pf_spr += st->sprites; pf_ops += st->ui_ops;
                    for (int c = 0; c < 5; c++) pf_cat[c] += st->cat_tris[c];
                    if (u > pf_max) { pf_max = u; pf_maxf = f2; }
                }
                if (f2 % 60 == 0) printf("  %4d : phase %d, place %d, tour %d, prog %.1f, v %.2f\n", f2, CR.phase, course_rank(NULL), j->tour, j->prog, j->v);
            }
            printf("  joueur : %d tête-à-queue, dérapage maxi %.2f rad\n", ntq, slipmax);
            if (pf_n) printf("  rendu en course (%d images) : préparation %.2f ms, effets %.1f, 3D %.2f ms, interface %.2f ms, pire %.2f ms (image %d) ; triangles %.0f (sol %.0f, eau %.0f, plantes %.0f, routes %.0f, bâtiments et décor %.0f), pixels %.0f, sprites %.1f, interface %.0f opérations\n",
                             pf_n, pf_bu / pf_n / 1000, pf_fx / pf_n, pf_3d / pf_n / 1000, pf_ui / pf_n / 1000, pf_max / 1000, pf_maxf, pf_tris / pf_n, pf_cat[0] / pf_n, pf_cat[1] / pf_n, pf_cat[2] / pf_n, pf_cat[3] / pf_n, pf_cat[4] / pf_n, pf_px / pf_n, pf_spr / pf_n, pf_ops / pf_n);
            if (CR.on) {
                int o[COURSE_N]; course_rank(o);
                for (int i = 0; i < CR.n; i++) printf("  %d. %s%s %.2f s (tour %d)\n", i + 1, E.w.persos[CR.r[o[i]].perso].nom, CR.r[o[i]].joueur ? " (joueur)" : "", CR.r[o[i]].fin, CR.r[o[i]].tour);
            }
        }
        else if (sscanf(tok, "carr:%d", &n) == 1) {   /* mode course : la carrière n */
            if (!accueil_on()) accueil_debut();
            carr_infos_lire(); carr_ouvrir(n); accueil_carriere(AP_CCARR);
            const carr_save_t *c = carr_cur();
            printf("carrière %d : %d catégorie(s) ouverte(s) sur %d, %u crédits, véhicule d'OUVERTE %s, coupe %d course %d, %d médaille(s)\n", n + 1, carr_cats_ouvertes(c), CARRD.ncats, (unsigned)c->credits,
                   carr_veh_perso(0) >= 0 ? E.w.persos[carr_veh_perso(0)].nom : "-", c->coupe, c->course, carr_medailles(c));
        }
        else if (sscanf(tok, "ccat:%d", &n) == 1) {   /* catégorie n de la carrière en cours (sa page ; son véhicule de départ s'il n'en a aucun) */
            if (!carr_cur()) { carr_infos_lire(); carr_ouvrir(0); }
            if (!accueil_on()) accueil_debut();
            carr_cat_choisir(n); accueil_carriere(AP_CCAT);
            const int kc = carr_cat_classe(carr_cur(), n), vp = carr_veh_perso(n);
            printf("catégorie %d %s : ouverte %d, classe %s, véhicule %s\n", n, n < CARRD.ncats ? CARRD.cats[n].nom : "?", carr_cat_ouverte(n), kc >= 0 ? CARRD.classes[kc].nom : "-", vp >= 0 ? E.w.persos[vp].nom : "-");
        }
        else if (sscanf(tok, "cpage:%d", &n) == 1) { if (!accueil_on()) accueil_debut(); accueil_carriere(AP_CMENU + n); }
        else if (sscanf(tok, "ccoupe:%d", &n) == 1) {
            if (!carr_cur()) { carr_infos_lire(); carr_ouvrir(0); }
            carr_coupe_commencer(n);
            printf("coupe %d, course %d : départ %d\n", n, carr_cur()->course, carr_lancer_coupe());
        }
        else if (sscanf(tok, "clibre:%d", &n) == 1) {
            int V[CARR_VEH]; carr_infos_lire(); const int nv = carr_vehicules_libres(V);
            printf("course libre, circuit %d : départ %d\n", n, nv ? carr_lancer_libre(n, V[0], 1, 0) : 0);
        }
        else if (!strncmp(tok, "cref:", 5)) { int i = 0, k = 0; sscanf(tok + 5, "%d:%d", &i, &k); printf("tour de référence, circuit %d, classe %d : départ %d\n", i, k, carr_essai_ref(i, k)); }
        else if (!strncmp(tok, "crefv:", 6)) {   /* tour de référence du circuit i pour une classe d'essai (vitesse de tous v, des adversaires vr) */
            int i = 0; float v = 1, vr = 10; sscanf(tok + 6, "%d:%f:%f", &i, &v, &vr);
            const carr_classe_t garde = CARRD.classes[0];
            CARRD.classes[0].v = v; CARRD.classes[0].v_rivaux = vr;
            printf("tour de référence, circuit %d, classe d'essai %.2f / %.2f : départ %d\n", i, v, vr, carr_essai_ref(i, 0));
            CARRD.classes[0] = garde;
        }
        /* Étape 2 du mode course : épreuves, défi du jour, garage */
        else if (!strncmp(tok, "cepr:", 5)) {   /* épreuve k (difficulté v, sinon la sienne) de la carrière en cours */
            int k = 0, v = -1; sscanf(tok + 5, "%d:%d", &k, &v);
            if (!carr_cur()) { carr_infos_lire(); carr_ouvrir(0); }
            printf("épreuve %d (%s, %s), ouverte %d : départ %d\n", k, k < CARRD.nepr ? CARRD.epr[k].nom : "?", k < CARRD.nepr ? carr_sorte_nom(CARRD.epr[k].sorte) : "?",
                   carr_epr_ouverte(k), carr_lancer_epr(k, v >= 0 ? v : k < CARRD.nepr ? CARRD.epr[k].niveau : 1));
        }
        else if (!strncmp(tok, "clibres:", 8)) {   /* course libre sur le circuit i, épreuve s (CS_*) */
            int i = 0, so = 0, V[CARR_VEH]; sscanf(tok + 8, "%d:%d", &i, &so); carr_infos_lire(); const int nv = carr_vehicules_libres(V);
            printf("course libre, circuit %d, %s : départ %d\n", i, carr_sorte_nom(so), nv ? carr_lancer_libre_s(i, V[0], 1, 0, so) : 0);
        }
        else if (!strcmp(tok, "cdefi")) { if (!carr_cur()) { carr_infos_lire(); carr_ouvrir(0); } const carr_defi_t *d = carr_defi(); printf("défi du jour %d : %s, circuit %d, classe %d, niveau %d ; départ %d\n", d->jour, carr_sorte_nom(d->sorte), d->circuit, d->classe, d->niveau, carr_lancer_defi()); }
        else if (!strncmp(tok, "date:", 5)) { int j = 1, m = 1; sscanf(tok + 5, "%d:%d", &j, &m); eng_set_date(j, m, 0); printf("date %02d/%02d : jour %d\n", j, m, eng_date_jour()); }
        else if (!strcmp(tok, "cterminer")) printf("terminer : %d\n", carr_terminer());
        else if (!strcmp(tok, "cbandeau")) { char t0[48] = "-", t1[48] = "-", t2[48] = "-"; int c0 = 0, c1 = 0, c2 = 0; course_bandeau(0, t0, sizeof t0, &c0); course_bandeau(1, t1, sizeof t1, &c1); course_bandeau(2, t2, sizeof t2, &c2); printf("bandeau : %s | %s (%d) | %s\n", t0, t1, c1, t2); }
        else if (!strcmp(tok, "crecommencer")) printf("recommencer : %d\n", carr_recommencer());
        else if (sscanf(tok, "crecomp:%d", &n) == 1) printf("récompense du duel (%s) : %d\n", n ? "véhicule" : "livrées", carr_recompense(n));
        else if (!strcmp(tok, "cvalider")) printf("validé : page %d\n", carr_valider() - AP_CMENU);
        else if (!strncmp(tok, "camelio:", 8)) { int i = 0, so = 0; sscanf(tok + 8, "%d:%d", &i, &so); printf("amélioration %d du véhicule %d : %d (niveau %d, %.2f)\n", so, i, carr_amelio_acheter(i, so), carr_amelio(i, so), carr_veh_stat(i, so)); }
        else if (!strncmp(tok, "ccredits:", 9)) { carr_save_t *c = carr_cur(); if (c) { c->credits = (uint32_t)atoi(tok + 9); carr_ecrire(); } }
        else if (sscanf(tok, "cdebloquer:%d", &n) == 1) {   /* essais : les coupes des classes 0..n en or, la classe n ouverte */
            carr_save_t *c = carr_cur(); if (!c) { carr_infos_lire(); carr_ouvrir(0); c = carr_cur(); }
            for (int k = 0; k < CARRD.ncoupes; k++) if (CARRD.coupes[k].classe <= n) c->coupe_med[k] = 3;
            for (int q = 0; q < CARRD.nclasses && q <= n; q++) c->classe[CARRD.classes[q].cat] = (uint8_t)q;   /* (catégories : la plus haute de chacune, au plus n) */
            carr_ecrire();
            int o = 0; for (int k = 0; k < CARRD.nepr; k++) o += carr_epr_ouverte(k);
            printf("carrière débloquée jusqu'à la classe %d : %d catégorie(s) ouverte(s), %d épreuve(s) ouverte(s) sur %d\n", n, carr_cats_ouvertes(c), o, CARRD.nepr);
        }
        else if (!strcmp(tok, "cetat")) {   /* état de la carrière (données, épreuves, garage) */
            const carr_save_t *c = carr_cur();
            printf("CARR version %d : %d catégories, %d classes, %d circuits, %d coupes, %d véhicules, %d épreuves\n", CARRD.version, CARRD.ncats, CARRD.nclasses, CARRD.ncircuits, CARRD.ncoupes, CARRD.nveh, CARRD.nepr);
            for (int k = 0; k < CARRD.ncats; k++) {
                int nv = 0, V[CARR_COUPES]; for (int i = 0; i < CARRD.nveh; i++) nv += carr_veh_dans(i, k);
                printf("  catégorie %d %s : ouverte par %d/%d, %d coupe(s), %d épreuve(s), %d véhicule(s)%s\n", k, CARRD.cats[k].nom, CARRD.cats[k].ouvre_cat, CARRD.cats[k].ouvre_classe, carr_cat_coupes(k, V), carr_cat_eprs(k, V), nv, c && carr_cat_ouverte(k) ? ", ouverte" : "");
            }
            for (int k = 0; k < CARRD.nclasses; k++) printf("  classe %d %s (catégorie %d) : v %.2f, rivaux %.2f, gains %.2f\n", k, CARRD.classes[k].nom, CARRD.classes[k].cat, CARRD.classes[k].v, CARRD.classes[k].v_rivaux, CARRD.classes[k].gains);
            for (int k = 0; k < CARRD.ncircuits; k++) { printf("  circuit %d %s : pluie %.2f, réf.", k, CARRD.circuits[k].nom, CARRD.circuits[k].pluie); for (int q = 0; q < CARRD.nclasses; q++) printf(" %.2f", CARRD.circuits[k].ref[q]); printf("\n"); }
            for (int i = 0; i < CARRD.nveh; i++) printf("  véhicule %d %s : catégorie %d, départ %d, prix %u\n", i, E.w.persos[CARRD.veh[i].perso].nom, CARRD.veh[i].cat, CARRD.veh[i].depart, (unsigned)CARRD.veh[i].prix);
            for (int k = 0; k < CARRD.nepr; k++) printf("  épreuve %d %s : %s, circuit %d, classe %d, tours %d, rival %s, niveau %d%s%s\n", k, CARRD.epr[k].nom, carr_sorte_nom(CARRD.epr[k].sorte), CARRD.epr[k].circuit, CARRD.epr[k].classe, CARRD.epr[k].tours,
                                                        CARRD.epr[k].rival >= 0 ? E.w.persos[CARRD.epr[k].rival].nom : "-", CARRD.epr[k].niveau, c && carr_epr_ouverte(k) ? ", ouverte" : "", c && c->epr[k].med ? ", médaille" : "");
            for (int i = 0; i < CARRD.nveh; i++) if (CARRD.veh[i].recompense || CARRD.veh[i].v > 0) printf("  véhicule %d %s : récompense %d, v %.2f, acc %.2f, adh %.2f\n", i, E.w.persos[CARRD.veh[i].perso].nom, CARRD.veh[i].recompense, CARRD.veh[i].v, CARRD.veh[i].acc, CARRD.veh[i].grip);
            if (c) printf("  carrière %d : catégorie %d, %u CR, véhicule %s, défi %d (médaille %d), duels %02X, %d médaille(s)\n", carr_k() + 1, carr_cat(), (unsigned)c->credits, carr_veh_perso(carr_cat()) >= 0 ? E.w.persos[carr_veh_perso(carr_cat())].nom : "-", c->defi_jour, c->defi_med, c->duels, carr_medailles(c));

            const carr_res_t *r = carr_res();
            printf("  dernier résultat : %s, place %d, temps %.2f, meilleur %.2f, médaille %d, crédits %d (+%d), record %d, récompense %d, référence %.2f ; fantôme %d (%.2f s)\n",
                   carr_sorte_nom(r->sorte), r->place, r->temps, r->meilleur, r->med, r->credits, r->bonus, r->record, r->recomp, r->ref, course_fantome_ok(), course_fantome_temps());
        }
        else if (sscanf(tok, "cpilote:%d", &n) == 1) {   /* course en cours, le joueur piloté comme un adversaire */
            course_joueur_ia(1);
            int pf_n = 0, nfr = 0; double pf_bu = 0, pf_3d = 0, pf_ui = 0, pf_tris = 0, pf_route = 0, pf_sol = 0, pf_max = 0, pf_route_max = 0, pf_tris_max = 0;
            eng_input_t rien = { 0 };
            for (int f2 = 1; f2 <= n && CR.on && CR.phase != CP_RESULTATS; f2++, nfr++) {
                frame(&rien);
                if (CR.phase == CP_COURSE) {
                    const eng_stats_t *st = eng_stats();
                    const double u = us_build + st->us_3d + st->us_ui;
                    pf_n++; pf_bu += us_build; pf_3d += st->us_3d; pf_ui += st->us_ui; pf_tris += st->tris; pf_route += st->cat_tris[3]; pf_sol += st->cat_tris[0];
                    if (u > pf_max) pf_max = u;
                    if (st->cat_tris[3] > pf_route_max) pf_route_max = st->cat_tris[3];
                    if (st->tris > pf_tris_max) pf_tris_max = st->tris;
                }
            }
            course_joueur_ia(0);
            if (pf_n) printf("  rendu en course (%d images) : préparation %.2f ms, 3D %.2f ms, interface %.2f ms, pire image %.2f ms ; triangles %.0f (pire %.0f), dont route %.0f (pire %.0f) et sol %.0f\n",
                             pf_n, pf_bu / pf_n / 1000, pf_3d / pf_n / 1000, pf_ui / pf_n / 1000, pf_max / 1000, pf_tris / pf_n, pf_tris_max, pf_route / pf_n, pf_route_max, pf_sol / pf_n);
            if (CR.on) {
                int o[COURSE_N]; course_rank(o);
                for (int i = 0; i < CR.n; i++) { const racer_t *r = &CR.r[o[i]]; printf("  %d. %s%s %.2f s, meilleur tour %.2f s (tour %d)%s\n", i + 1, E.w.persos[r->perso].nom, r->joueur ? " (joueur)" : "", r->fin, r->meilleur, r->tour, r->elim ? " éliminé" : ""); }
                printf("  %d images, piste %s (%.1f cases, %d tours), phase %d\n", nfr, PISTE.nom, PISTE.L, CR.tours, CR.phase);
            }
        }
        else if (!strncmp(tok, "essai:", 6)) {   /* course d'essai pilotée de la montre (SDX COURSE k a) */
            int k = 0, al = 1; sscanf(tok + 6, "%d:%d", &k, &al);
            printf("essai : %d\n", eng_essai_course(k, al));
        }
        else if (sscanf(tok, "battle:%d", &n) == 1) battle_start(n, 0, 0);
        else if (!strncmp(tok, "evboss", 6)) {
            /* evboss[:k] : Sera au centre de la zone du k-ième événement de zone de la planète qui contient un combat (le
               premier par défaut), sa quête mise à son étape, sa condition remplie, pas encore fait ; il part à l'image suivante */
            int vis = 0, kk = 0; sscanf(tok, "evboss:%d", &kk);
            for (int k = 0; k < E.w.sc_nev; k++) {
                const sc_event_t *ev = &E.w.sc_ev[k];
                if (ev->trigger != 1 || ev->carte != E.w.carte || !script_ev_combat(k) || vis++ < kk) continue;
                if (ev->quest < E.w.sc_nq) E.quests[ev->quest] = (uint8_t)ev->step;
                if (ev->cond < SC_MAX_FLAGS) { if (ev->flags & 2) E.flags[ev->cond >> 3] &= (uint8_t)~(1 << (ev->cond & 7)); else E.flags[ev->cond >> 3] |= (uint8_t)(1 << (ev->cond & 7)); }
                E.ev_done[k >> 3] &= (uint8_t)~(1 << (k & 7));
                eng_teleport(ev->x, ev->z, E.yaw);
                printf("événement %d (quête %d, étape %d) en %.1f %.1f, rayon %.1f\n", k, ev->quest, ev->step, ev->x, ev->z, ev->r);
                break;
            }
        }
        else if (sscanf(tok, "avance:%d", &n) == 1) {   /* OK toutes les 8 images jusqu'au combat (n fois au plus) */
            eng_input_t okk = { 0 }; okk.keys = ENG_KEY_OK;
            for (int i = 0; i < n && !BT.on; i++) { frame(&okk); for (int j = 0; j < 7 && !BT.on; j++) frame(&none); }
            if (BT.on) { char gn[72]; bt_groupe_nom(gn, sizeof gn); printf("combat du scénario : %s, niveau %d\n", gn, BT.lvl); } else printf("pas de combat\n");
        }
        else if (!strcmp(tok, "perdre") && BT.on) {   /* toute l'équipe à terre : la fin du tour mène à la défaite (BP_LOSE) */
            for (int m = 0; m < NMEMB; m++) PARTY[m].hp = 0;
            BT.phase = BP_ANIM; BT.an.who = BT_ENEMY; BT.actor = BT_ENEMY; BT.an.hit = 1; BT.an.fx = FX_LASER; BT.an.t0 = E.clock - 5; BT.an.ntarget = 0;
        }
        else if (!strcmp(tok, "sauves")) {   /* point de sauvegarde de la partie en cours, durée de la dernière écriture */
            partie_info_t pi; const int ok = hud_partie_info(E.partie, &pi);
            printf("partie %d : %s n° %u, %s, %.0f s de jeu ; dernière écriture %d µs ; quêtes 0-3 : %d %d %d %d ; script %s ; Sera en %.1f %.1f\n", E.partie + 1, ok ? "point" : "aucun point",
                   (unsigned)pi.seq, pi.lieu, pi.temps, hud_sauve_us(), E.quests[0], E.quests[1], E.quests[2], E.quests[3], script_running() ? "en cours" : "aucun", E.sera.x, E.sera.z);
        }
        else if (!strcmp(tok, "autosauve")) printf("sauvegarde automatique : %d (%d µs)\n", hud_sauve_auto(0), hud_sauve_us());
        else if (!strncmp(tok, "groupe:", 7) || !strncmp(tok, "sbires:", 7)) {
            /* groupe:K1:K2[:K3[:K4]] : combat contre ces ennemis de ENNE (parts égales, rencontre) ; sbires:B:S1[:S2…] : le boss B
               et ses sbires (combat du scénario) ; au niveau de la fiche du premier */
            int ks[BT_EMAX], nk = sscanf(tok + 7, "%d:%d:%d:%d", &ks[0], &ks[1], &ks[2], &ks[3]);
            const int sb = tok[0] == 's';
            if (nk > 0) battle_start_groupe(ks, nk, 0, sb, sb ? GROUPE_SBIRES : GROUPE_PARTS, NULL);
            for (int k = 0; k < BT.nen; k++) printf("  ennemi %c : %s (fiche %d), PV %d, ATQ %d, VIT %d, part %.2f%s\n", '1' + k, BT.en[k].name, BT.en[k].ek,
                                                   BT.en[k].hp, BT.en[k].atk, BT.en[k].spd, BT.en[k].part, BT.en[k].sbire ? ", sbire" : "");
        }
        else if (sscanf(tok, "viser:%d", &n) == 1 && BT.on) {   /* toucher l'ennemi n du groupe (pendant le choix d'une action) */
            const screen_pos_t *p = &E.scr_enemy[n % BT_EMAX];
            if (p->vis) battle_tap(p->cx, p->cy);
            printf("viser %d : ennemi visé %d\n", n, BT.cible);
        }
        else if (sscanf(tok, "marcherenc:%d", &n) == 1) {   /* marcherenc:n : Sera marche n images en tournant (obstacles), sans cible ; rencontres déclenchées (combats gagnés aussitôt) */
            int nb = 0, pat = 0, vus[PATR_MAX] = { 0 }; float dd = 0, ox = E.sera.x, oz = E.sera.z;
            for (int k = 0; k < n; k++) {
                eng_input_t k2 = { 0 };
                k2.keys = (k / 90) % 4 == 0 ? ENG_KEY_UP : (k / 90) % 4 == 1 ? ENG_KEY_RIGHT : (k / 90) % 4 == 2 ? ENG_KEY_DOWN : ENG_KEY_LEFT;
                frame(&k2);
                dd += hypotf(E.sera.x - ox, E.sera.z - oz); ox = E.sera.x; oz = E.sera.z;
                if (BT.on) { nb++; printf("  rencontre %d après %.0f cases : %s\n", nb, dd, BT.en[0].name); BT.on = 0; }
                for (int s = 0; s < PATR_MAX; s++) { if (PATR[s].on && !vus[s]) { pat++; printf("  groupe de %d paru après %.0f cases : %s, à %.1f cases%c", PATR[s].n, dd, E.w.persos[E.w.enemies[PATR[s].ek[0]].perso].nom, hypotf(PATR[s].x[0] - E.sera.x, PATR[s].z[0] - E.sera.z), 10); } vus[s] = PATR[s].on; }
            }
            printf("marche : %.0f cases, %d groupes parus, %d combats, mode %d, histoire %d\n", dd, pat, nb, E.renc_mode, script_niveau(E.sera.x, E.sera.z));
        }
        else if (!strcmp(tok, "passage")) { if (E.w.nence && E.w.ence) { const int f = E.w.ence[8] | E.w.ence[9] << 8; if (f < SC_MAX_FLAGS) E.flags[f >> 3] |= (uint8_t)(1 << (f & 7)); printf("passage : drapeau %d levé%c", f, 10); } continue; }   /* le passage de la première enceinte (Lucerne) : les gardes laissent entrer */
        else if (!strcmp(tok, "coupes")) { extern int eng_essai_fondus(void); int nm = 0; if (E.in.on) for (int i = 0; i < E.in.npoly; i++) nm += E.in.fade[i] != 0; printf("coupes : %d bâtiment(s) coupé(s), %d face(s) de mur coupée(s), sera %.2f %.2f%c", eng_essai_fondus(), nm, E.sera.x, E.sera.z, 10); continue; }   /* bâtiments coupés (couverture de l'héroïne, 10/11/2026) */
        else if (!strcmp(tok, "ile")) {   /* île : drapeaux de l'histoire (section ILE), calme (pas de rencontre), équipe */
            int nm = 0; for (int m = 0; m < NMEMB; m++) if (E.team & (1 << m)) nm++;
            printf("île : monstres %u (%s), esprits %u (%s), calme %d, équipe %d (%s), script %d%c", E.w.ile_monstres,
                E.w.ile_monstres < SC_MAX_FLAGS && (E.flags[E.w.ile_monstres >> 3] >> (E.w.ile_monstres & 7) & 1) ? "levé" : "baissé",
                E.w.ile_esprits, E.w.ile_esprits < SC_MAX_FLAGS && (E.flags[E.w.ile_esprits >> 3] >> (E.w.ile_esprits & 7) & 1) ? "levé" : "baissé",
                ile_calme(), nm, PARTY[0].name ? PARTY[0].name : "?", script_running(), 10);
            /* (11/11/2026) héroïnes du studio, lames et leur héroïne, compagnes */
            printf("  héroïnes %d :", E.w.ile_nher); for (int k = 0; k < E.w.ile_nher; k++) printf(" %s=%u%s", E.w.ile_her[k].id, E.w.ile_her[k].perso, E.w.ile_her[k].genre ? "(m)" : "");
            int nh = 0; for (int k = 0; k < E.w.ile_nlames; k++) nh += E.w.ile_lame[k].perso != 0xFFFF;
            printf("%c  lames %d (%d avec leur héroïne) :", 10, E.w.ile_nlames, nh);
            for (int k = 0; k < E.w.ile_nlames; k++) if (E.w.ile_lame[k].perso != 0xFFFF) printf(" %s (%s) -> %s, drapeau %u", E.w.ile_lame[k].nom, E.w.ile_lame[k].id, E.w.persos[E.w.ile_lame[k].perso].nom, E.w.ile_lame[k].drapeau);
            printf("%c  compagnes :", 10); for (int m = 1; m < NMEMB; m++) if (E.team & (1 << m)) printf(" %s (PV %d, ATQ %d)", PARTY[m].name ? PARTY[m].name : "?", PARTY[m].max, PARTY[m].atk);
            printf("%c", 10);
            continue;
        }
        else if (!strncmp(tok, "genre:", 6)) {   /* genre:N : genre de l'héroïne jouée dans la table de l'île (0 féminin, 1 masculin), texte {f|m} (11/11/2026) */
            int nk = 0; for (int k = 0; k < E.w.ile_nher; k++) if ((int)E.w.ile_her[k].perso == E.heroine) { E.w.ile_her[k].genre = (uint8_t)atoi(tok + 6); nk++; }
            printf("genre : %d entrée(s) de l'héroïne %d mises à %d%c", nk, E.heroine, atoi(tok + 6), 10);
            continue;
        }
        else if (!strncmp(tok, "filtre:", 7)) {   /* filtre:TEXTE : le texte tel qu'il s'affiche (genre, noms échangés) ; TEXTE sans virgule */
            extern const char *hud_texte_affiche(const char *s, char *buf, int cap);
            static char fb[256];
            printf("filtre : %s -> %s%c", tok + 7, hud_texte_affiche(tok + 7, fb, (int)sizeof fb), 10);
            continue;
        }
        else if (!strcmp(tok, "rencetat")) { static char eb[640]; eng_etat_rencontres(eb, sizeof eb); printf("etat : %s%c", eb, 10); continue; }   /* état des rencontres (SDX ETAT ; « etat » : celui des intérieurs) */
        else if (sscanf(tok, "manette:%d:%f", &n, &a) == 2) {   /* manette:n:touches : Sera court n images au stick (touches ENG_KEY_*, stick à fond), comme la Poké Ball Plus ; groupes, combats */
            const int keys = (int)a; int parus = 0, comb = 0, av[PATR_MAX] = { 0 }, bloq = 0; float dd = 0, ox = E.sera.x, oz = E.sera.z;
            for (int k = 0; k < n; k++) {
                eng_input_t m = { 0 }; m.manette = 1; m.keys = keys;
                m.stick_x = (keys & ENG_KEY_RIGHT) ? 0.71f : (keys & ENG_KEY_LEFT) ? -0.71f : 0; m.stick_y = (keys & ENG_KEY_DOWN) ? 0.71f : (keys & ENG_KEY_UP) ? -0.71f : 0;
                if (!m.stick_x) m.stick_y *= 1.41f; if (!m.stick_y) m.stick_x *= 1.41f;
                frame(&m);
                const float p3 = hypotf(E.sera.x - ox, E.sera.z - oz); dd += p3; ox = E.sera.x; oz = E.sera.z;
                bloq = p3 < 0.01f ? bloq + 1 : 0;
                if (bloq > 30) { E.yaw += PI_F / 4; bloq = 0; }   /* bloquée : la caméra tourne d'un pas (le joueur ferait le tour) */
                if (BT.on) { comb++; BT.on = 0; for (int s2 = 0; s2 < PATR_MAX; s2++) PATR[s2].on = 0; E.stream_hold = 1; }
                for (int s2 = 0; s2 < PATR_MAX; s2++) { if (!PATR[s2].on) { av[s2] = 0; continue; } if (!av[s2]) { av[s2] = 1; parus++; } }
            }
            printf("manette %d : %.0f cases en %.0f s, %d groupes parus, %d combats, yaw %.2f%c", keys, dd, n / 30.0f, parus, comb, E.yaw, 10); continue;
        }
        else if (sscanf(tok, "marchevue:%d:%f", &n, &a) == 2) {   /* marchevue:n:cap : Sera court tout droit (cap en degrés ; un doigt posé loin, elle tourne si elle est bloquée), n images ; groupes d'ennemis parus, vus à l'écran (le chef dans le disque 15 images au moins), combats (gagnés aussitôt) */
            extern int eng_essai_projeter(float x, float z, float *sx, float *sy, float *h);
            int parus = 0, vus3 = 0, comb = 0, comb_vus = 0, bloque = 0, vf[PATR_MAX] = { 0 }, av[PATR_MAX] = { 0 }, cap4[PATR_MAX] = { 0 }; float cap = a * PI_F / 180, dd = 0, ox = E.sera.x, oz = E.sera.z;
            eng_input_t rien4 = { 0 };
            for (int k = 0; k < n; k++) {
                E.has_dest = 1; E.dest_court = 1; E.dest_x = E.sera.x + sinf(cap) * 8; E.dest_z = E.sera.z + cosf(cap) * 8;
                frame(&rien4);
                const float pas3 = hypotf(E.sera.x - ox, E.sera.z - oz);
                dd += pas3; ox = E.sera.x; oz = E.sera.z;
                bloque = pas3 < 0.01f ? bloque + 1 : 0;
                if (bloque > 8) { cap += 1.1f; bloque = 0; }
                if (BT.on) { comb++; if (BT.patr >= 0 && BT.patr < PATR_MAX && vf[BT.patr] >= 15) comb_vus++; printf("  combat %d après %.0f cases : %s, vu %.1f s avant%c", comb, dd, BT.en[0].name, BT.patr >= 0 && BT.patr < PATR_MAX ? vf[BT.patr] / 30.0f : 0.0f, 10); if (BT.patr >= 0 && BT.patr < PATR_MAX) PATR[BT.patr].on = 0; BT.on = 0; }
                for (int s = 0; s < PATR_MAX; s++) {
                    if (!PATR[s].on) { av[s] = 0; continue; }
                    if (!av[s]) { av[s] = 1; vf[s] = 0; cap4[s] = 0; parus++; printf("  groupe paru après %.0f cases : %s%c", dd, E.w.persos[E.w.enemies[PATR[s].ek[0]].perso].nom, 10); }
                    float sx4, sy4, h4;
                    if (eng_essai_projeter(PATR[s].x[0], PATR[s].z[0], &sx4, &sy4, &h4) && hypotf(sx4 - 233, sy4 - h4 / 2 - 233) < 233 && hypotf(PATR[s].x[0] - E.sera.x, PATR[s].z[0] - E.sera.z) < 14 && ++vf[s] == 15) vus3++;
                    if (hypotf(PATR[s].x[0] - E.sera.x, PATR[s].z[0] - E.sera.z) < 6 && !cap4[s]) { char nb4[64]; snprintf(nb4, sizeof nb4, "build/cams/groupe_%d.bmp", parus % 8); ecrire_bmp(nb4); cap4[s] = 1; }
                }
            }
            printf("marchevue : %.0f cases en %.0f s, %d groupes parus, %d vus à l'écran, %d combats dont %d vus avant, mode %d, en %.0f %.0f%c", dd, n / 30.0f, parus, vus3, comb, comb_vus, E.renc_mode, E.sera.x, E.sera.z, 10);
        }
        else if (sscanf(tok, "semer:%f:%d", &a, &n) == 2) {   /* semer:d:n : un groupe à d cases derrière Sera, qui court droit devant elle, n images au plus : semé, ou rattrapée */
            const int s = patrouille_poser(1);
            if (s < 0) printf("semer : pas de groupe posé%c", 10);
            else {
                patr_t *p = &PATR[s];
                float cap = atan2f(E.cam.sy, E.cam.cy), ox5 = E.sera.x, oz5 = E.sera.z, dd5 = 0; int bl5 = 0;
                for (int i = 0; i < p->n; i++) { p->x[i] = E.sera.x - sinf(cap) * (a + 0.9f * i); p->z[i] = E.sera.z - cosf(cap) * (a + 0.9f * i); p->dir[i] = cap; }
                p->etat = 1;
                eng_input_t rien5 = { 0 };
                int k = 0, fin = 0;
                for (; k < n && !fin; k++) {
                    E.has_dest = 1; E.dest_court = 1; E.dest_x = E.sera.x + sinf(cap) * 8; E.dest_z = E.sera.z + cosf(cap) * 8;
                    frame(&rien5);
                    { const float p5 = hypotf(E.sera.x - ox5, E.sera.z - oz5); dd5 += p5; ox5 = E.sera.x; oz5 = E.sera.z; bl5 = p5 < 0.01f ? bl5 + 1 : 0; if (bl5 > 4) { cap += 0.9f; bl5 = 0; } }
                    if (BT.on) fin = 2; else if (!p->on || p->etat != 1) fin = 1;
                    if (k % 90 == 0 && !fin) printf("  %.0f s : écart %.1f cases%c", k / 30.0f, hypotf(p->x[0] - E.sera.x, p->z[0] - E.sera.z), 10);
                }
                printf("semer : %s après %.1f s et %.0f cases (départ à %.0f cases, %s)%c", fin == 1 ? "semé" : fin == 2 ? "RATTRAPÉE" : "encore poursuivie", k / 30.0f, dd5, a, E.w.persos[E.w.enemies[p->ek[0]].perso].nom, 10);
            }
        }
        else if (sscanf(tok, "objetmenu:%d", &n) == 1 && BT.on) {   /* objetmenu:i : l'objet i du sac choisi au menu ; on reste au choix de la cible (ensuite : viser:k, etatobjet) */
            ITEMS[n].count += 2; BT.actor = 0; BT.phase = BP_ITEM; BT.mpage = 0;
            uint8_t l9[MAX_ITEMS]; int fi9, np9; const int n9 = battle_objets(l9);
            int sl9 = -1, c9 = sorts_page(n9, &fi9, &np9);
            for (int t9 = 0; t9 < 40 && sl9 < 0; t9++) {
                for (int k9 = 0; k9 < c9; k9++) if (l9[fi9 + k9] == n) sl9 = k9;
                if (sl9 < 0) { battle_command(BC_SUITE, 0); c9 = sorts_page(n9, &fi9, &np9); }
            }
            battle_command(BC_POTION, sl9);
            frame(&none); frame(&none);
        }
        else if (!strcmp(tok, "etatobjet") && BT.on) {
            printf("etatobjet : phase %d (ALLIE %d, action %d), cible %d, ennemi 0 : %d/%d%c", BT.phase, BP_ALLIE, BP_ANIM, BT.allie, BT.en[0].hp, BT.en[0].max, 10);
        }
        else if (sscanf(tok, "faible:%d:%f", &n, &a) == 2 && BT.on) {   /* faible:k:masque : faiblesses de l'ennemi k ; résistances : fin de l'essai suivant */
            BT.en[n].faible = (uint16_t)a; BT.en[n].resiste = 0;
        }
        else if (sscanf(tok, "machine:%d", &n) == 1 && BT.on) { BT.en[n].resiste = 1u << MG_SOIN; }
        else if (!strcmp(tok, "elements")) {   /* bilan des éléments : armes à élément, sorts et attaques élémentaires des ennemis (nombre d'ennemis par élément) */
            int ar[MAGIE_N] = { 0 }, es[MAGIE_N] = { 0 }, ea[MAGIE_N] = { 0 }, na = 0;
            for (int i = 0; i < n_items && i < MAX_ITEMS; i++) {
                const eqitem_t *q = eq_item(i);
                if (!q || q->cat >= E.w.neqtypes) continue;
                const int el = eq_el_de(i);
                if (el >= 0) { ar[el]++; printf("  %s : %s %s%s%c", ITEMS[i].name, E.w.eqtypes[q->cat].slot == EQ_ARME ? "arme" : "équipement", ELEM_NOM[el], el != q->el - 1 ? " (forge)" : "", 10); }
                else if (E.w.eqtypes[q->cat].slot == EQ_ARME) na++;
            }
            for (int k = 0; k < E.w.nenemies; k++) {
                const enemy_t *en = &E.w.enemies[k];
                int vs[MAGIE_N] = { 0 }, va[MAGIE_N] = { 0 };
                for (int s2 = 0; s2 < E.w.nsorts && s2 < 64; s2++) if ((en->sorts >> s2 & 1) && !E.w.sorts[s2].soin) vs[FX_ELEM(E.w.sorts[s2].fx)] = 1;
                for (int i = 0; i < en->natq; i++) { const int fx = en->atq[i * ENAT_ATQ + 20]; if (fx >= FX_MAGIE && fx < FX_N) va[FX_ELEM(fx)] = 1; }
                for (int e = 0; e < MAGIE_N; e++) { es[e] += vs[e]; ea[e] += va[e]; }
            }
            printf("armes sans élément : %d%c", na, 10);
            for (int e = 0; e < MAGIE_N; e++) printf("%-8s armes et équipements %2d, ennemis : sorts %2d, attaques %2d%c", ELEM_NOM[e], ar[e], es[e], ea[e], 10);
            printf("ennemis : %d%c", E.w.nenemies, 10);
        }
        else if (sscanf(tok, "resiste:%d:%f", &n, &a) == 2 && BT.on) { BT.en[n].resiste = (uint16_t)a; }   /* resiste:k:masque : résistances de l'ennemi k */
        else if (sscanf(tok, "elem:%d:%f", &n, &a) == 2 && n >= 0 && n < MAX_ITEMS) {   /* elem:i:e : élément e (MG_*, -1 : aucun) de l'objet i (paquet d'avant les éléments des armes) */
            E.w.eqit[n].el = (uint8_t)((int)a + 1); printf("objet %d (%s) : élément %d%c", n, n < n_items ? ITEMS[n].name : "?", (int)a, 10);
        }
        else if (sscanf(tok, "frappe:%d:%f", &n, &a) == 2 && BT.on && n >= 0 && n < NMEMB) {   /* frappe:m:n : n attaques du membre m sur l'ennemi 0 (élément de son arme, faiblesses), dégâts moyens */
            long tot = 0; int nb = 0;
            for (int r = 0; r < (int)a; r++) {
                BT.en[0].hp = BT.en[0].max; const int h0 = BT.en[0].hp;
                BT.actor = n; BT.phase = BP_CMD; battle_command(BC_ATTACK, 0);
                BT.locked = 1; battle_tap(E.scr_enemy[0].cx, E.scr_enemy[0].cy);
                for (int i = 0; i < 600 && BT.phase == BP_ANIM && !BT.an.hit; i++) frame(&none);
                if (BT.an.hit) { tot += h0 - BT.en[0].hp; nb++; }
            }
            BT.en[0].hp = BT.en[0].max;
            printf("frappe : membre %d (ATQ %d), arme %d, élément %d, ennemi faible 0x%x résiste 0x%x : dégâts moyens %.1f sur %d%c", n, PARTY[n].atk, E.eq[n][EQ_ARME],
                   arme_element(n), BT.en[0].faible, BT.en[0].resiste, nb ? (double)tot / nb : 0.0, nb, 10);
        }
        else if (!strncmp(tok, "lancerobj:", 10)) {   /* lancerobj:i:degats:effet:groupe : l'objet i du sac devient un objet à lancer (paquet d'avant ces objets) */
            int i8, d8, f8, g8;
            if (sscanf(tok + 10, "%d:%d:%d:%d", &i8, &d8, &f8, &g8) == 4) { ITEMS[i8].type = OBJ_LANCER; ITEMS[i8].hp = d8; ITEMS[i8].mp = f8; ITEMS[i8].team = g8; ITEMS[i8].releve = 0; printf("%s devient à lancer%c", ITEMS[i8].name, 10); }
        }
        else if (sscanf(tok, "objetennemi:%d:%f", &n, &a) == 2 && BT.on) {   /* objetennemi:i:k : l'objet i du sac sur l'ennemi k (k < 0 : le groupe, objet de groupe), en passant par le menu ; PV avant, après */
            const int i7 = n, k7 = (int)a;
            ITEMS[i7].count += 2; BT.actor = 0;
            BT.phase = BP_CMD; battle_command(BC_ITEMS, 0);
            uint8_t l7[MAX_ITEMS]; int fi7, np7; const int n7 = battle_objets(l7);
            int sl7 = -1, c7 = sorts_page(n7, &fi7, &np7);
            for (int t7 = 0; t7 < 40 && sl7 < 0; t7++) {
                for (int k8 = 0; k8 < c7; k8++) if (l7[fi7 + k8] == i7) sl7 = k8;
                if (sl7 < 0) { battle_command(BC_SUITE, 0); c7 = sorts_page(n7, &fi7, &np7); }
            }
            int h7[BT_EMAX]; for (int k8 = 0; k8 < BT.nen; k8++) h7[k8] = BT.en[k8].hp;
            const int av7 = ITEMS[i7].count, tp7 = ITEMS[i7].type;
            battle_command(BC_POTION, sl7);
            printf("%s (%s) : phase %s, proposé %d%c", ITEMS[i7].name, tp7 == OBJ_LANCER ? "lancé" : "soin", BT.phase == BP_ALLIE ? "ALLIE" : "partie", BT.phase == BP_ALLIE ? BT.allie : -1, 10);
            if (BT.phase == BP_ALLIE) {
                for (int t7 = 0; t7 < 16 && k7 >= 0 && BT.allie != BT_ENEMY + k7; t7++) battle_command(BC_ALLIE_SUIV, 0);
                printf("  cible %d (BT_ENEMY %d), BT.cible %d%c", BT.allie, BT_ENEMY, BT.cible, 10);
                frame(&none); frame(&none); ecrire_bmp("build/cams/objet_cible.bmp");
                battle_command(BC_ALLIE_OK, 0);
            }
            for (int f7 = 0; f7 < 26; f7++) frame(&none);
            ecrire_bmp("build/cams/objet_coup.bmp");
            for (int f7 = 0; f7 < 60 && BT.phase == BP_ANIM; f7++) frame(&none);
            printf("  sac %d -> %d, ", av7, ITEMS[i7].count);
            for (int k8 = 0; k8 < BT.nen; k8++) printf("ennemi %d (max %d) : %d -> %d  ", k8, BT.en[k8].max, h7[k8], BT.en[k8].hp);
            printf("%c", 10);
        }
        else if (sscanf(tok, "aterre:%d", &n) == 1 && BT.on && n >= 0 && n < NMEMB) {   /* aterre:m : le membre m à terre (relève en combat : objetcombat:m:i avec la SERINGUE DE RAPPEL) */
            PARTY[n].hp = 0;
            int r6 = -1; for (int i = 0; i < n_items && r6 < 0; i++) if (ITEMS[i].releve) r6 = i;
            printf("%s à terre ; objet qui relève : %d (%s)%c", PARTY[n].name, r6, r6 >= 0 ? ITEMS[r6].name : "-", 10);
        }
        else if (sscanf(tok, "objetcombat:%d:%f", &n, &a) == 2 && BT.on) {   /* objetcombat:m:i : le membre m, le choix de l'objet i du sac : le menu, le camarade proposé, le soin */
            ITEMS[(int)a].count += 2;
            BT.actor = n; BT.phase = BP_CMD; battle_command(BC_ITEMS, 0);
            uint8_t l6[MAX_ITEMS]; int fi6, np6; const int n6 = battle_objets(l6);
            printf("objets en combat : %d, page 0 :", n6);
            const int c6 = sorts_page(n6, &fi6, &np6);
            int sl6 = -1;
            for (int k6 = 0; k6 < c6; k6++) { printf(" %s×%d", ITEMS[l6[fi6 + k6]].name, ITEMS[l6[fi6 + k6]].count); if (l6[fi6 + k6] == (int)a) sl6 = k6; }
            printf("%c", 10);
            while (sl6 < 0 && BT.mpage < 40) { battle_command(BC_SUITE, 0); const int c7 = sorts_page(n6, &fi6, &np6); for (int k6 = 0; k6 < c7; k6++) if (l6[fi6 + k6] == (int)a) sl6 = k6; }
            const int av6 = ITEMS[(int)a].count;
            battle_command(BC_POTION, sl6);
            printf("  phase %s, proposé %s%c", BT.phase == BP_ALLIE ? "ALLIE" : "autre", BT.phase == BP_ALLIE ? PARTY[BT.allie].name : "-", 10);
            if (BT.phase == BP_ALLIE) { frame(&none); ecrire_bmp("build/cams/objet_allie.bmp"); const int ci6 = BT.allie; const int h6 = PARTY[ci6].hp, m6 = PARTY[ci6].mp; battle_command(BC_ALLIE_OK, 0); for (int f6 = 0; f6 < 40; f6++) frame(&none); printf("  %s : PV %d -> %d, PM %d -> %d, objet %d -> %d%c", PARTY[ci6].name, h6, PARTY[ci6].hp, m6, PARTY[ci6].mp, av6, ITEMS[(int)a].count, 10); }
        }
        else if (!strcmp(tok, "guidepass")) {   /* d'ici, le passage à prendre vers chaque carte */
            for (int c6 = 0; c6 < E.w.ncartes; c6++) { float x6 = 0, z6 = 0; const int ok6 = passage_vers(c6, &x6, &z6); printf("  vers la carte %d : %s %.1f %.1f (à %.0f cases)%c", c6, ok6 ? "passage en" : c6 == E.w.carte ? "ici" : "aucun chemin ouvert", x6, z6, ok6 ? hypotf(x6 - E.sera.x, z6 - E.sera.z) : 0.0f, 10); }
        }
        else if (!strcmp(tok, "sures")) {   /* zones sûres de la carte, et si Sera est dans l'une */
            int ns = 0;
            for (int i = 0; i < E.w.nsures; i++) { const uint8_t *s = E.w.sures + i * 16; float q[3]; memcpy(q, s + 4, 12); if (s[0] == E.w.carte) { ns++; printf("  zone sûre : %.0f %.0f, rayon %.1f%c", q[0], q[1], q[2], 10); } }
            printf("sures : %d zones en tout, %d sur la carte %d ; Sera en %.0f %.0f : %s%c", E.w.nsures, ns, E.w.carte, E.sera.x, E.sera.z, zone_sure(E.sera.x, E.sera.z) ? "à l'abri" : "dehors", 10);
        }
        else if (sscanf(tok, "surechasse:%f:%d", &a, &n) == 2) {   /* surechasse:d:n : un groupe à d cases de Sera, immobile dans une zone sûre, n images : il ne doit ni la chasser ni l'atteindre (né dehors : dans une zone sûre, rien ne naît) */
            const float sx6 = E.sera.x, sz6 = E.sera.z;
            int s = -1;
            for (int e6 = 0; e6 < 200 && s < 0; e6++) { E.sera.x = sx6 + 60 * sinf(e6 * 0.7f) + e6 * 0.5f; E.sera.z = sz6 + 60 * cosf(e6 * 0.7f); if (!zone_sure(E.sera.x, E.sera.z)) s = patrouille_poser(1); }
            E.sera.x = sx6; E.sera.z = sz6;
            if (s < 0) printf("surechasse : pas de groupe posé%c", 10);
            else {
                patr_t *p = &PATR[s];
                for (int i = 0; i < p->n; i++) { p->x[i] = E.sera.x + E.cam.sy * (a + 0.9f * i); p->z[i] = E.sera.z + E.cam.cy * (a + 0.9f * i); }
                p->hx = p->tx = p->x[0]; p->hz = p->tz = p->z[0]; p->etat = 1;
                eng_input_t rien6 = { 0 };
                float dmin = 1e9f; int k = 0;
                for (; k < n && !BT.on; k++) { frame(&rien6); const float dd6 = hypotf(p->x[0] - E.sera.x, p->z[0] - E.sera.z); if (dd6 < dmin) dmin = dd6; }
                printf("surechasse : %s, état %d après %d images, plus près %.1f cases%c", BT.on ? "COMBAT" : "pas de combat", p->etat, k, dmin, 10);
            }
        }
        else if (!strcmp(tok, "renctaille")) {   /* tailles des groupes de rencontre tirées ici (1000 tirages) */
            int h[BT_EMAX + 1] = { 0 }, nm = equipe_nombre();
            const int k = encounter_pick();
            for (int i = 0; k >= 0 && i < 1000; i++) h[encounter_taille(k, script_niveau(E.sera.x, E.sera.z), nm)]++;
            printf("rencontre ici : ennemi %d, histoire %d, %d membres ; tailles 1 : %d, 2 : %d, 3 : %d, 4 : %d (sur 1000)\n", k, script_niveau(E.sera.x, E.sera.z), nm, h[1], h[2], h[3], h[4]);
        }
        else if (!strncmp(tok, "patrouille", 10)) {
            /* patrouille : une patrouille hostile autour de Sera (derrière la caméra) ; patrouille:d : à d cases devant elle, en
               file vers elle ; patrouilles : l'état des patrouilles */
            if (tok[10] != 's') {
                const int s = patrouille_poser(1);
                printf("patrouille posée : %d\n", s);
                if (s >= 0 && sscanf(tok, "patrouille:%f", &a) == 1) {
                    patr_t *p = &PATR[s];
                    for (int i = 0; i < p->n; i++) { p->x[i] = E.sera.x + E.cam.sy * (a + PATR_FILE_RT * i); p->z[i] = E.sera.z + E.cam.cy * (a + PATR_FILE_RT * i); p->dir[i] = atan2f(-E.cam.sy, -E.cam.cy); }
                    p->hx = p->tx = p->x[0]; p->hz = p->tz = p->z[0];
                }
            }
            for (int s = 0; s < PATR_MAX; s++) if (PATR[s].on) {
                printf("  patrouille %d : %d ennemis (%s…), niveau %d, en %.1f %.1f, état %d, à %.1f cases de Sera\n", s, PATR[s].n, E.w.persos[E.w.enemies[PATR[s].ek[0]].perso].nom,
                       PATR[s].lvl, PATR[s].x[0], PATR[s].z[0], PATR[s].etat, hypotf(PATR[s].x[0] - E.sera.x, PATR[s].z[0] - E.sera.z));
            }
        }
        else if (sscanf(tok, "niveau:%d", &n) == 1) party_set_level(n);
        else if (!strcmp(tok, "eqpl")) {   /* paliers de l'équipement (EQPL) et ce que vend chaque boutique au niveau actuel */
            int nv = 0, nb = 0;
            printf("EQPL : %d paliers (marge %d), niveaux :", E.w.npal, E.w.pal_avance);
            for (int p = 1; p <= E.w.npal; p++) printf(" %d", E.w.pal_niv[p]);
            for (int k = 0; k < n_items; k++) { nv += (E.w.eq_fl[k] & EQ_VENDU) != 0; nb += (E.w.eq_fl[k] & EQ_BUTIN) != 0; }
            printf(" ; %d vendus, %d en butin ; niveau %d, histoire %d : boutiques jusqu'au niveau %d\n", nv, nb, E.level, script_niveau_histoire(), eq_limite_vente());
            for (int bi = 0, vus = 0; bi < E.w.nbats && vus < 40; bi++) {
                const int cm = E.w.bats[bi].comm - 1;
                if (cm < 0 || cm >= E.w.ncomms || !E.w.comms[cm].eq) continue;
                uint8_t l[MAX_ITEMS]; char rs[96];
                const int nl = boutique_liste(cm, bi, l, MAX_ITEMS);
                printf("  bâtiment %d (%s, carte %d) : gamme %d à %d, types %08x, %d objets", bi, E.w.bats[bi].nom, E.w.bats[bi].carte, E.w.bats[bi].gmin, E.w.bats[bi].gmax, (unsigned)E.w.comms[cm].eq, nl);
                for (int i = 0, ne = 0; i < nl && ne < 3; i++) if (eq_item(l[i])) { eq_resume(l[i], rs, sizeof rs); printf("%s %s [P%d %d CR : %s]", ne ? "," : " :", ITEMS[l[i]].name, E.w.eq_pal[l[i]], ITEMS[l[i]].prix, rs); ne++; }
                printf("\n"); vus++;
            }
        }
        else if (!strncmp(tok, "butin:", 6)) {   /* butin:K:L:N : N fins de combat contre l'ennemi K au niveau L */
            int ek = 0, lv = 1, nt = 100, hist[PAL_MAX] = { 0 }, rien = 0, dernier = -1;
            sscanf(tok + 6, "%d:%d:%d", &ek, &lv, &nt);
            battle_start(ek, lv, 0);
            for (int i = 0; i < nt; i++) { const int k = drop_equip(); if (k < 0) rien++; else { hist[E.w.eq_pal[k]]++; dernier = k; } }
            printf("butin de %s niv %d (palier %d) : %d / %d sans équipement ;", BT.en[0].name, lv, palier_de(lv), rien, nt);
            for (int p = 1; p <= E.w.npal; p++) if (hist[p]) printf(" P%d ×%d", p, hist[p]);
            printf("%s%s\n", dernier >= 0 ? " ; dernier : " : "", dernier >= 0 ? ITEMS[dernier].name : "");
            BT.on = 0;
        }
        else if (!strcmp(tok, "gagner") && BT.on) { for (int k = 0; k < BT.nen; k++) { BT.en[k].hp = 0; BT.en[k].mort_t = E.clock; } BT.phase = BP_DYING; BT.t0 = E.clock; }
        else if (sscanf(tok, "equipe:%d", &n) == 1) { equipe_scenario(0, n); printf("équipe : au combat %#x, bande %#x, imposés %#x\n", E.team, E.recrues, E.impose); }
        else if (sscanf(tok, "recrues:%d", &n) == 1) {   /* comme « rejoindre » de chacun (au combat tant qu'il reste une place) */
            equipe_scenario(1, n); printf("recrues : au combat %#x, bande %#x, imposés %#x\n", E.team, E.recrues, E.impose);
        }
        else if (!strcmp(tok, "libre")) { equipe_scenario(3, 0); printf("équipe libre : imposés %#x\n", E.impose); }
        else if (sscanf(tok, "basculer:%d", &n) == 1) {   /* menu ÉQUIPE : le membre n au combat ou en réserve */
            const int r = equipe_basculer(n);
            printf("basculer %d : %d, au combat %#x, bande %#x\n", n, r, E.team, E.recrues);
        }
        else if (sscanf(tok, "tour:%d", &n) == 1 && BT.on && n >= 0 && n < NMEMB) { BT.actor = n; BT.phase = BP_CMD; BT.locked = 0; }
        else if (sscanf(tok, "piste:%d", &n) == 1) {
            /* Sera sur la ligne de départ de la piste n (PIS2), qui devient la piste active */
            const piste_t *pp = course_piste(n);
            if (!pp) { printf("piste %d : aucune (%d en tout)\n", n, course_npistes()); continue; }
            if (pp->carte != E.w.carte) world_carte(pp->carte, pp->xd, pp->zd);   /* (piste d'une carte de course) */
            E.sera.x = pp->xd; E.sera.z = pp->zd; course_choisir(E.sera.x, E.sera.z); world_stream(E.sera.x, E.sera.z, 1);
            printf("piste %d / %d : %d points, %.1f cases, départ en %.1f %.1f\n", n, course_npistes(), pp->n, pp->L, E.sera.x, E.sera.z);
        }
        else if (!strcmp(tok, "menucourse")) printf("menu de course : %d\n", hud_course_menu(-1, -1, -1, -1));
        /* Véhicule de Sera (garage.c ; menu des véhicules de hud.c) : taper:NNNN, vehicules, vehicule:MOT[:c], boutique[:n],
           garage (voir l'aide en tête) */
        else if (!strncmp(tok, "taper:", 6)) { hud_code_taper(tok + 6); printf("code %s : menu des véhicules %d\n", tok + 6, hud_vehicules_open()); }
        else if (!strcmp(tok, "vehicules")) {
            int nv = 0;
            for (int p = 0; p < E.w.npersos; p++) if (E.w.persos[p].flags & PERS_VEHICULE) {   /* tous les véhicules, proposés ou non */
                const perso_t *pe = &E.w.persos[p]; float v, ac; veh_params(p, &v, &ac);
                const int ok = veh_proposable(p);
                printf("  %s %3d %-26s type %d, planche %3d, %d palette(s), v %4.1f, acc %4.1f, adh %.2f, %6d CR%s%s, service %d%s\n", ok ? "+" : "-", p, pe->nom, VEH_TYPE(pe->flags), pe->sheet, pe->nvar,
                       v, ac, E.w.veh_grip[p], veh_prix(p), E.w.veh_course[p] ? ", course" : "", (pe->flags & PERS_TRAFIC) ? ", trafic" : "", E.w.veh_service[p], perso_present(p) ? "" : ", absent");
                nv += ok;
            }
            printf("%d véhicules proposés ; menu : %d\n", nv, hud_vehicules(0, NULL, 0));
        }
        else if (!strncmp(tok, "vehicule:", 9)) {
            char mot[32]; int c2 = 0;
            if (sscanf(tok + 9, "%31[^:]:%d", mot, &c2) < 1) continue;
            if (!hud_vehicules_open() && !hud_vehicules(0, NULL, 0)) { printf("menu des véhicules refusé\n"); continue; }
            if (!hud_vehicules_choisir(mot, c2)) { printf("pas de véhicule %s\n", mot); continue; }
            eng_input_t k2 = { 0 }; k2.keys = ENG_KEY_OK; frame(&k2);   /* PRENDRE */
            garage_etat();
            continue;
        }
        else if (!strncmp(tok, "boutique", 8)) {
            if (tok[8] == ':') E.credits = atoi(tok + 9);
            const int ob = hud_vehicules(1, NULL, 0);
            printf("boutique : %d (crédits %d)%s%s\n", ob, E.credits, ob ? "" : " : ", ob ? "" : hud_toast_txt());
        }
        else if (!strcmp(tok, "garage")) garage_etat();
        else if (!strcmp(tok, "descendre")) printf("descendre : %d\n", vehicle_unboard());
        else if (!strcmp(tok, "liberer")) { int_liberer(); printf("sortie de l'intérieur : Sera %.2f %.2f, dedans %d\n", E.sera.x, E.sera.z, E.in.on); }
        else if (!strcmp(tok, "organisateur")) {
            /* Sera devant l'exemplaire du personnage du premier événement « parler » (monde de course), tournée vers lui */
            int k = -1;
            for (int e = 0; e < E.w.sc_nev && k < 0; e++)
                for (int m = 0; E.w.sc_ev[e].trigger == 2 && m < E.nnpc && k < 0; m++) if (E.npcs[m].perso == E.w.sc_ev[e].perso) k = m;
            if (k < 0) { printf("pas d'organisateur\n"); continue; }
            E.sera.x = E.npcs[k].x - 1.2f; E.sera.z = E.npcs[k].z; E.yaw = PI_F / 2;
            printf("organisateur %s en %.1f %.1f\n", E.w.persos[E.npcs[k].perso].nom, E.npcs[k].x, E.npcs[k].z);
        }
        else if (sscanf(tok, "drapeau:%d", &n) == 1 && n >= 0 && n < SC_MAX_FLAGS) E.flags[n >> 3] |= (uint8_t)(1 << (n & 7));
        else if (!strcmp(tok, "enceintes")) {   /* drapeau du passage de chaque enceinte levé (ENCE : Lucerne ouverte, police.c) */
            const uint8_t *q = E.w.ence;
            for (int k = 0; q && k < E.w.nence; k++) {
                const int fl = q[8] | q[9] << 8, na = q[10] | q[11] << 8, np = q[12] | q[13] << 8;
                if (fl < SC_MAX_FLAGS) E.flags[fl >> 3] |= (uint8_t)(1 << (fl & 7));
                q += 40 + na * 4 + np * 16;   /* ENC_OCT de police.c */
            }
        }
        else if (!strncmp(tok, "taille:", 7)) {   /* taille:MOT:pct : taille à l'écran des personnages dont le nom contient MOT (essai sans régénérer) */
            char mot[48] = ""; float pct = 100;
            if (sscanf(tok + 7, "%47[^:]:%f", mot, &pct) == 2)
                for (int p = 0; p < E.w.npersos; p++) if (strstr(E.w.persos[p].nom, mot)) { E.w.persos[p].taille = pct / 100; printf("taille : %s %.0f %%\n", E.w.persos[p].nom, pct); }
        }
        else if (!strncmp(tok, "acteur:", 7)) {   /* acteur:MOT:dx:dz : un acteur du premier personnage dont le nom contient MOT, à (dx, dz) de Sera, tourné vers la caméra */
            char mot[48] = ""; float dx = 0.8f, dz = 0;
            sscanf(tok + 7, "%47[^:]:%f:%f", mot, &dx, &dz);
            int p = -1;
            for (int q = 0; q < E.w.npersos && p < 0; q++) if (E.w.persos[q].nom && strstr(E.w.persos[q].nom, mot) && E.w.persos[q].sheet >= 0) p = q;
            if (p >= 0) { acteur_poser(p, E.sera.x + dx, E.sera.z + dz, E.yaw + PI_F); printf("acteur %s à %.1f %.1f de Sera\n", E.w.persos[p].nom, dx, dz); }
        }
        else if (!strncmp(tok, "present:", 8)) {   /* present:NOM : le drapeau de présence (PVIS) du personnage NOM mis pour qu'il soit là */
            for (int p = 0; p < E.w.npersos; p++) {
                const perso_t *pe = &E.w.persos[p];
                if (!strstr(pe->nom, tok + 8) || pe->cond >= SC_MAX_FLAGS) continue;   /* une partie du nom (sans accent : l'argument n'est pas en UTF-8 sous Windows) */
                if (pe->neg) E.flags[pe->cond >> 3] &= (uint8_t)~(1 << (pe->cond & 7)); else E.flags[pe->cond >> 3] |= (uint8_t)(1 << (pe->cond & 7));
                printf("présent : %s (drapeau %d%s)\n", pe->nom, pe->cond, pe->neg ? ", baissé" : "");
            }
        }
        else if (!strcmp(tok, "finscript")) script_abort();
        else if (!strncmp(tok, "parler:", 7)) {
            int k = -1;
            for (int m = 0; m < E.nnpc && k < 0; m++) if (!strcmp(E.w.persos[E.npcs[m].perso].nom, tok + 7)) k = m;
            if (k < 0) { printf("parler : %s absent\n", tok + 7); continue; }
            E.sera.x = E.npcs[k].x - 1.2f; E.sera.z = E.npcs[k].z; E.yaw = PI_F / 2;
            world_stream(E.sera.x, E.sera.z, 1);
            printf("devant %s en %.1f %.1f\n", tok + 7, E.npcs[k].x, E.npcs[k].z);
        }
        else if (!strcmp(tok, "boost")) { if (CR.on) CR.r[CR.n - 1].boost = 0.8f; }   /* COURSE_PLAQUE_T de course.c */
        else if (sscanf(tok, "pilote:%d", &n) == 1) {
            /* Course en cours (menu de course validé) : n images pilotées comme course:n, puis l'état */
            for (int f2 = 1; f2 <= n && CR.on && CR.phase != CP_RESULTATS; f2++) {
                const racer_t *j = &CR.r[CR.n - 1];
                const int e = (j->seg + 6) % PISTE.n;
                float ang = atan2f(PISTE.X[e] - j->x, PISTE.Z[e] - j->z) - j->dir;
                ang = atan2f(sinf(ang), cosf(ang));
                eng_input_t dr = { 0 };
                dr.keys = ENG_KEY_UP | (ang > 0.12f ? ENG_KEY_RIGHT : ang < -0.12f ? ENG_KEY_LEFT : 0);
                frame(&dr);
            }
            if (CR.on) {
                int o[COURSE_N]; course_rank(o);
                printf("course : phase %d, piste %s, à battre %.0f s, boost %.2f, halo %.2f\n", CR.phase, PISTE.nom, CR.chrono, CR.r[CR.n - 1].boost, CR.r[CR.n - 1].halo);
                for (int i = 0; i < CR.n; i++) printf("  %d. %s%s %.2f s (tour %d)\n", i + 1, E.w.persos[CR.r[o[i]].perso].nom, CR.r[o[i]].joueur ? " (joueur)" : "", CR.r[o[i]].fin, CR.r[o[i]].tour);
            } else printf("course finie : place %d\n", course_place());
        }
        else if (!strcmp(tok, "enc")) {   /* rencontre tirée ici, avec son groupe */
            int k = encounter_pick(), g[BT_EMAX], nm = equipe_nombre();
            const int ng = k >= 0 ? encounter_groupe(k, E.sera.x, E.sera.z, encounter_taille(k, script_niveau(E.sera.x, E.sera.z), nm), g) : 0;
            printf("rencontre : ennemi %d, groupe de %d\n", k, ng);
            if (k >= 0) battle_start_groupe(g, ng, encounter_level(), 0, GROUPE_PARTS, NULL);
        }
        else if (sscanf(tok, "hp:%d:%f", &n, &a) == 2 && n >= 0 && n < NMEMB) PARTY[n].hp = (int)a;
        else if (sscanf(tok, "ehp:%d", &n) == 1) BT.en[BT.cible > 0 ? BT.cible : 0].hp = n;   /* PV de l'ennemi visé */
        else if (sscanf(tok, "tenue:%d:%f", &n, &a) == 2 && n >= 0 && n < NMEMB) E.tenue_eq[n] = (int8_t)a;
        else if (sscanf(tok, "job:%d:%f", &n, &a) == 2 && n >= 0 && n < NMEMB) party_set_job(n, (int)a);
        else if (sscanf(tok, "donner:%d:%f", &n, &a) == 2 && n >= 0 && n < n_items) ITEMS[n].count += (int)a;
        else if (sscanf(tok, "eqsac:%d", &n) == 1) {   /* un exemplaire de plus de chaque équipement du palier n (EQPL) */
            int nb = 0;
            for (int i = 0; i < n_items; i++) if (eq_item(i) && E.w.eq_pal[i] == n) { ITEMS[i].count++; nb++; }
            printf("sac : %d équipements du palier %d\n", nb, n);
            continue;
        }
        else if (sscanf(tok, "equip:%d:%f", &n, &a) == 2) {   /* menu ÉQUIP. : membre n (-1 : les membres), emplacement a (-1 : ses emplacements) */
            static char et[4096];
            hud_equip_ouvrir(n, (int)a); frame(&in); frame(&none);
            hud_equip_texte(et, sizeof et); printf("%s\n", et);
            continue;
        }
        else if (!strcmp(tok, "equiptexte")) { static char et[4096]; hud_equip_texte(et, sizeof et); printf("%s\n", et); continue; }
        /* Magie (03/11/2026) : « magie » les sorts de chaque membre de la bande (job à son niveau, équipement, appris), ceux qu'il
           a appris, le cristal porté et ses points, le prochain sort de son job ; « cristal:m:MOT » le premier équipement dont le nom
           contient MOT à l'accessoire du membre m (sans passer par le sac) ; « cristalpts:i:p » l'objet i devient un cristal de p
           points (essai sur un paquet d'avant les cristaux) ; « apts:m:s:p » p points au sort s du membre m ; « statut:m:v » STATUT */
        else if (!strcmp(tok, "magie")) {
            for (int i = 0; i < NMEMB; i++) {
                if (!((E.recrues | 1) >> i & 1)) continue;
                int l[MAX_SORTS], s, p, mx, bn = 1000, best = -1;
                const int ns = sorts_of(i, l), j = E.job[i] < E.w.njobs ? E.job[i] : -1;
                printf("%s (%s, niv %d, MAG %d) : %d sort(s)", PARTY[i].name, j >= 0 ? E.w.jobs[j].nom : "-", E.level, PARTY[i].mag, ns);
                for (int k = 0; k < ns; k++) printf("%s%s", k ? ", " : " : ", E.w.sorts[l[k]].nom);
                printf(" ; appris :"); for (int k = 0; k < E.w.nsorts; k++) if (sort_appris(i, k)) printf(" %s", E.w.sorts[k].nom);
                if (cristal_de(i, &s, &p, &mx)) printf(" ; cristal : %s %d/%d", E.w.sorts[s].nom, p, mx);
                for (int x = 0; j >= 0 && x < E.w.nsorts; x++) { const int nv = sort_niv_job(j, x); if ((E.w.jobs[j].sorts >> x & 1) && nv > E.level && nv < bn) { bn = nv; best = x; } }
                if (best >= 0) printf(" ; prochain : %s au niveau %d", E.w.sorts[best].nom, bn);
                printf("\n");
            }
            continue;
        }
        else if (!strncmp(tok, "cristal:", 8)) {
            const int m = atoi(tok + 8); const char *mot = strchr(tok + 8, ':'); int it = -1;
            for (int i = 0; mot && i < n_items && it < 0; i++) if (eq_item(i) && strstr(ITEMS[i].name, mot + 1)) it = i;
            if (it < 0 || m < 0 || m >= NMEMB) { fprintf(stderr, "cristal inconnu : %s\n", tok); return 1; }
            E.eq[m][EQ_ACCESS] = (uint8_t)it; party_refresh(m);
            printf("accessoire du membre %d : %s (objet %d, points %d)\n", m, ITEMS[it].name, it, eq_item(it)->pts);
        }
        else if (sscanf(tok, "cristalpts:%d:%f", &n, &a) == 2 && eq_item(n)) { E.w.eqit[n].pts = (uint8_t)a; printf("%s : cristal de %d points\n", ITEMS[n].name, (int)a); }
        else if (sscanf(tok, "apts:%d:%f:%f", &n, &a, &b) == 3 && n >= 0 && n < NMEMB && a >= 0 && a < MAX_SORTS) E.apts[n][(int)a] = (uint8_t)b;
        else if (sscanf(tok, "repartir:%d", &n) == 1) { hud_nouvelle(n); printf("nouvelle partie dans l'emplacement %d\n", n); }   /* NOUVELLE PARTIE, après une partie quittée */
        else if (!strcmp(tok, "echosouverts")) { for (int k = 0; k < E.w.sc_nev; k++) if ((E.w.sc_ev[k].flags & 8) && E.w.sc_ev[k].cond < SC_MAX_FLAGS) E.flags[E.w.sc_ev[k].cond >> 3] |= (uint8_t)(1 << (E.w.sc_ev[k].cond & 7)); }   /* les drapeaux qui ouvrent les Échos */
        else if (!strcmp(tok, "chassesouvertes")) { for (int k = 0; k < E.w.ncibles; k++) if (E.w.cibles[k].quest & 0x8000) { const int cd = E.w.sc_ev[E.w.cibles[k].quest & 0x7FFF].cond; if (cd < SC_MAX_FLAGS) E.flags[cd >> 3] |= (uint8_t)(1 << (cd & 7)); } }   /* les rangs des grandes chasses */
        else if (sscanf(tok, "journal:%d", &n) == 1) { hud_journal_ouvrir(n); }
        else if (!strcmp(tok, "stats")) {   /* niveau et statistiques de chaque membre (gains d'ATQ et de DÉF : GAIN_ATQ, GAIN_DEF) */
            printf("niveau %d, %d CR, partie %d, script %d, %d quêtes ouvertes, équipe %x :", E.level, E.credits, E.partie, script_running(), ({ int qn = 0; for (int q = 0; q < E.w.sc_nq; q++) qn += E.quests[q] != 0; qn; }), (unsigned)E.team);
            for (int k = 0; k < NMEMB; k++) printf(" %s PV %d ATQ %d MAG %d DEF %d |", PARTY[k].name, PARTY[k].max, PARTY[k].atk, PARTY[k].mag, PARTY[k].def);
            printf("%c", 10);
        }
        else if (sscanf(tok, "statut:%d:%f", &n, &a) == 2) { hud_statut_ouvrir(n, (int)a); }
        else if (sscanf(tok, "expmanque:%d", &n) == 1) { E.exp = exp_next(E.level) - n; if (E.exp < 0) E.exp = 0; }   /* il manque n points d'expérience pour le niveau suivant */
        else if (sscanf(tok, "magasin:%d", &n) == 1) {   /* boutique du commerce de l'intérieur (0 achat, 1 vente), puis son état */
            static char st[4096];
            printf("boutique : %d ligne(s)\n", hud_shop_ouvrir(n)); frame(&none);
            hud_shop_texte(st, sizeof st); printf("%s\n", st);
            continue;
        }
        else if (!strcmp(tok, "magasintexte")) { static char st[4096]; hud_shop_texte(st, sizeof st); printf("%s\n", st); continue; }
        else if (sscanf(tok, "credits:%d", &n) == 1) { E.credits = n; continue; }
        else if (sscanf(tok, "arme:%d:%f", &n, &a) == 2 && n >= 0 && n < NMEMB) {   /* arme portée sans passer par le sac */
            E.eq[n][EQ_ARME] = (uint8_t)a; party_refresh(n);
            const eqitem_t *r = eq_item((int)a);
            printf("arme du membre %d : objet %d, effet %d\n", n, (int)a, r ? E.w.eqtypes[r->cat].effet : -1);
        }
        else if (sscanf(tok, "armure:%d:%f", &n, &a) == 2 && n >= 0 && n < NMEMB) {   /* armure portée sans passer par le sac (bikini : sa tenue) */
            E.eq[n][EQ_ARMURE] = (uint8_t)a; party_refresh(n);
            const int t = tenue_of(n, 0);
            printf("armure du membre %d : objet %d (%s), affinité %d, DÉF %d, tenue %d (%s), variante %d, cut-ins de la tenue %d\n", n, (int)a,
                   (int)a < n_items ? ITEMS[(int)a].name : "?", eq_aff(n, (int)a), PARTY[n].def, t, t >= 0 ? E.w.tenues[t].nom : "la sienne",
                   t >= 0 ? E.w.tenues[t].vremap : 0, tenue_base(t));
        }
        else if (sscanf(tok, "attaque:%d", &n) == 1 && BT.on && n >= 0 && n < NMEMB) {   /* le membre n attaque l'ennemi */
            BT.actor = n; BT.phase = BP_CMD; battle_command(BC_ATTACK, 0);
            BT.locked = 1; battle_tap(E.scr_enemy[0].cx, E.scr_enemy[0].cy);
            BT.an.hit = 1;   /* pas de dégâts : l'ennemi reste debout d'une attaque à l'autre */
        }
        else if (sscanf(tok, "pasvue:%d:%f", &n, &a) == 2 && BT.on && n >= 0 && n < NMEMB) {   /* pas du membre n à son tour : avance image par image, capture toutes les a images (build/cams/pas_i.bmp) */
            for (int i = 0; i < NMEMB; i++) BT.avance[i] = 0;
            BT.actor = n; BT.phase = BP_CMD;
            for (int i = 0; i < 30; i++) {
                if (i % (int)a == 0) { char nb5[64]; snprintf(nb5, sizeof nb5, "build/cams/pas_%d.bmp", i / (int)a); ecrire_bmp(nb5); }
                printf("pas %d : avance %.3f%c", i, BT.avance[n], 10);
                frame(&none);
            }
            BT.phase = BP_ITEM; frame(&none); printf("menu des objets : avance %.3f%c", BT.avance[n], 10);
            BT.actor = (n + 1) % NMEMB; for (int i = 0; i < 12; i++) { frame(&none); if (i % 3 == 0) printf("au suivant %d : avance %.3f%c", i, BT.avance[n], 10); }
        }
        else if (sscanf(tok, "elanvue:%d:%f", &n, &a) == 2 && BT.on && n >= 0 && n < NMEMB) {   /* élan du membre n, image par image : t, déplacement, capture toutes les a images (build/cams/elan_i.bmp) */
            BT.actor = n; BT.phase = BP_CMD; battle_command(BC_ATTACK, 0);
            BT.locked = 1; battle_tap(E.scr_enemy[0].cx, E.scr_enemy[0].cy);
            BT.an.hit = 1;
            for (int i = 0; i < 200 && BT.phase == BP_ANIM; i++) {
                if (i % (int)a == 0) { char nb5[64]; snprintf(nb5, sizeof nb5, "build/cams/elan_%d.bmp", i / (int)a); ecrire_bmp(nb5); printf("elan %d : t %.2f, déplacement %.2f, membre à l'écran %d %d%c", i, E.clock - BT.an.t0, BT.off[n], (int)E.scr_party[n].cx, (int)E.scr_party[n].cy, 10); }
                frame(&none);
            }
        }
        else if (sscanf(tok, "technique:%d:%f", &n, &a) == 2 && BT.on && n >= 0 && n < NMEMB) {   /* technique k du membre n */
            BT.actor = n; BT.phase = BP_TECH; PARTY[n].mp = PARTY[n].maxmp; battle_command(BC_TECH, (int)a);
            if (BT.phase == BP_TARGET) { BT.locked = 1; battle_tap(E.scr_enemy[0].cx, E.scr_enemy[0].cy); }
            if (BT.phase == BP_ALLIE) battle_command(BC_ALLIE_OK, 0);   /* à qui ? la cible proposée (11/11/2026) */
            printf("technique %d du membre %d : phase %d, cut-in %d\n", (int)a, n, BT.phase, BT.cut.img);
        }
        else if (!strcmp(tok, "arenes")) {   /* arènes de combat de chaque carte (AREN ; battle.c, arene_choisir) */
            const uint8_t *p8 = E.w.aren, *f8 = p8 ? p8 + E.w.aren_sz : NULL;
            if (!p8) printf("arènes : pas de section AREN (celle de la carte)%c", 10);
            else {
                const int nc8 = p8[0] | p8[1] << 8; p8 += 2;
                for (int k8 = 0; k8 < nc8 && p8 + 2 <= f8; k8++) { const int n8 = p8[0] | p8[1] << 8; p8 += 2 + n8 * 4; printf("  carte %d : %d arènes%c", k8, n8, 10); }
            }
        }
        else if (sscanf(tok, "arene:%f:%f", &a, &b) == 2) {   /* arene:x:z : Sera en (x, z), un combat (le premier ennemi) ; l'arène prise, sa distance */
            E.sera.x = a; E.sera.z = b; BT.on = 0;   /* un combat déjà en cours : remplacé */
            battle_start(0, 20, 0);
            printf("arène : Sera en %.1f %.1f, arène %.1f %.1f (%.1f cases)%c", a, b, E.w.arena_x, E.w.arena_z, hypotf(E.w.arena_x - a, E.w.arena_z - b), 10);
        }
        else if (sscanf(tok, "planches:%d", &n) == 1 && n >= 0 && n < NMEMB) {   /* planches du membre n par animation : cadre, taille, normalisation, ancrage (ordre d'affichage en combat) */
            const int b9 = member_sheet(n), mv9 = member_sheet_mouv(n);
            static const char *NA[] = { "attente", "marche", "course", "attaque", "touché", "défense", "victoire", "KO", "magie", "tir" };
            static const int AA[] = { ANIM_IDLE, ANIM_WALK, ANIM_RUN, ANIM_ATTACK, ANIM_HURT, ANIM_DEFEND, ANIM_VICTORY, ANIM_KO, ANIM_CAST, ANIM_TIR };
            for (int k = 0; k < 10; k++) {
                const int bb = k == 1 || k == 2 ? mv9 : b9;
                if (!has_anim(bb, AA[k])) continue;
                const sheet_t *S9 = &E.w.sheets[anim_sheet(bb, AA[k])];
                printf("  %s %-8s planche %d : cadre %dx%d, taille %.2f, norm %.3f, ancrage %d (bas %.1f px)%c", PARTY[n].name, NA[k], anim_sheet(bb, AA[k]), S9->fw, S9->fh, S9->taille, S9->norm, S9->has_pv,
                       S9->has_pv ? S9->fh - S9->pv[1][1] / 2.0f : 0.0f, 10);
                if (S9->has_pv) { printf("    bas par direction :"); for (int r9 = 0; r9 < 8; r9++) printf(" %.1f", S9->fh - S9->pv[r9][1] / 2.0f); printf("%c", 10); }
            }
        }
        else if (!strncmp(tok, "cibler:", 7) && BT.on) {
            /* cibler:m:quoi:u : le membre m choisit ATTAQUE (quoi = a), la technique k (tk) ou le sort k de sa liste (sk), puis la cible
               u (membre, ou 100 + k pour l'ennemi k) au toucher ; la phase « À qui ? » (cible proposée, SUIVANT) et le résultat
               (11/11/2026 : choix de la cible de toute action) */
            int m8 = 0, u8 = 0, k8 = 0; char q8[8] = "";
            if (sscanf(tok + 7, "%d:%7[^:]:%d", &m8, q8, &u8) == 3 && m8 >= 0 && m8 < NMEMB) {
                BT.actor = m8; PARTY[m8].mp = PARTY[m8].maxmp; for (int i = 0; i < NMEMB; i++) BT.avance[i] = 0;
                if (q8[0] == 'a') { BT.phase = BP_CMD; battle_command(BC_ATTACK, 0); }
                else if (q8[0] == 't') { k8 = atoi(q8 + 1); BT.phase = BP_TECH; battle_command(BC_TECH, k8); }
                else if (q8[0] == 's') {
                    int l8[MAX_SORTS]; const int ns8 = sorts_of(m8, l8); k8 = atoi(q8 + 1);
                    BT.phase = BP_TECH; battle_command(BC_MAGIES, 0); BT.mpage = 0;
                    int fi8, np8, c8 = sorts_page(ns8, &fi8, &np8);
                    for (int g8 = 0; k8 >= fi8 + c8 && g8 < 20; g8++) { battle_command(BC_SUITE, 0); c8 = sorts_page(ns8, &fi8, &np8); }
                    printf("sort %d (%s) du membre %d%c", k8, k8 < ns8 ? E.w.sorts[l8[k8]].nom : "?", m8, 10);
                    battle_command(BC_SORT, k8 - fi8);
                }
                printf("cibler : phase %s, sorte %d, proposé %d (%s)%c", BT.phase == BP_ALLIE ? "À QUI" : BT.phase == BP_ANIM || BT.phase == BP_CUTIN ? "partie" : "autre", BT.pend_sorte, BT.phase == BP_ALLIE ? BT.allie : -1,
                       BT.phase != BP_ALLIE ? "-" : BT.allie >= BT_ENEMY ? BT.en[BT.allie - BT_ENEMY].name : PARTY[BT.allie].name, 10);
                if (BT.phase == BP_ALLIE) {
                    frame(&none); ecrire_bmp("build/cams/cibler.bmp");
                    battle_command(BC_ALLIE_SUIV, 0);
                    printf("  SUIVANT : %d (%s)%c", BT.allie, BT.allie >= BT_ENEMY ? BT.en[BT.allie - BT_ENEMY].name : PARTY[BT.allie].name, 10);
                    const screen_pos_t *c8p = u8 >= 100 ? &E.scr_enemy[u8 - 100] : &E.scr_party[u8];
                    frame(&none);
                    battle_tap(c8p->cx, u8 >= 100 ? c8p->cy : (c8p->top + c8p->foot) / 2);
                    printf("  toucher %d : phase %s, cibles", u8, BT.phase == BP_ANIM ? "action" : BT.phase == BP_CUTIN ? "cut-in" : BT.phase == BP_ALLIE ? "À QUI" : "autre");
                    for (int i = 0; i < BT.an.ntarget; i++) printf(" %d", BT.an.targets[i]);
                    printf(", ennemi visé %d%c", BT.cible, 10);
                }
            }
        }
        else if (sscanf(tok, "cutt:%f", &a) == 1 && BT.on && BT.phase == BP_CUTIN) BT.cut.t0 = E.clock - a;
        else if (sscanf(tok, "cutimg:%d:%f", &n, &a) == 2 && BT.on && BT.phase == BP_CUTIN) { BT.cut.img = n; BT.cut.paire = a > 0; BT.cut.plein = 0; }
        else if (!strncmp(tok, "code:", 5)) {
            const int v0 = verrou18();
            if (!strcmp(tok + 5, "0")) edition_verrouiller(); else edition_code(tok + 5);
            int nm = 0; for (int i = 0; i < E.w.npersos; i++) nm += E.w.persos[i].masque;
            printf("code %s : édition %d, verrou %d -> %d, %d personnages masqués\n", tok + 5, E.w.edition, v0, verrou18(), nm);
        }
        else if (!strcmp(tok, "ennemi") && BT.on) { int u = BT_ENEMY; for (int k = 0; k < BT.nen; k++) if (BT.en[k].hp > 0) { u = BT_ENEMY + k; break; } BT.actor = u; BT.phase = BP_ENEMY; BT.t0 = E.clock - 1; }   /* le premier ennemi debout joue */
        else if (!strcmp(tok, "balayage") && BT.on && BT.phase == BP_ANIM && BT.an.who >= BT_ENEMY) {   /* tour de l'ennemi changé en balayage */
            BT.an.all = 1; BT.an.fx = FX_BALAYAGE; BT.an.col = 0xFF7A3Du; BT.an.ntarget = 0;
            for (int k = 0; k < NMEMB && BT.an.ntarget < 4; k++) if (in_team(k) && PARTY[k].hp > 0) BT.an.targets[BT.an.ntarget++] = k;
        }
        else if (sscanf(tok, "fx:%d", &n) == 1 && BT.on && BT.phase == BP_ANIM && n >= 0 && n < FX_N) {
            BT.an.fx = n; BT.an.col = BT_C1;
            for (int k = 0; k < E.w.neqtypes; k++) if (E.w.eqtypes[k].slot == EQ_ARME && E.w.eqtypes[k].effet == n) { BT.an.col = E.w.eqtypes[k].couleur; break; }
            if (fx_couleur(n)) { BT.an.col = fx_couleur(n); BT.an.k = 1.4f; }   /* effet puissant : joué comme une technique */
            if (BT.an.who >= BT_ENEMY) BT.an.col = n == FX_BALAYAGE ? 0xFF7A3Du : 0xFF3B4Eu;
            /* élan au contact : FX_DASH > 0 (battle.c) */
            BT.an.melee = BT.an.who < BT_ENEMY && (n == FX_IMPACT || n == FX_TAILLE || n == FX_FOUET || n == FX_FRAPPE); BT.an.cast = n == FX_ARCANE || n >= FX_MAGIE;
        }
        else if (sscanf(tok, "fxt:%f", &a) == 1 && BT.on && BT.phase == BP_ANIM) {
            /* action en cours ramenée à a secondes (l'horloge du moteur suit le temps réel), sans dégâts ni sons */
            BT.an.t0 = E.clock - a; BT.an.hit = 1; BT.an.cue = 3; BT.an.scue = 3; BT.shake = 0; fx_fix = a;
        }
        else if (sscanf(tok, "musforce:%d", &n) == 1) { E.mus_force = (uint16_t)n; printf("musique imposée : chaîne %d%c", n, 10); }   /* comme une partie sauvegardée avec cette chaîne (d'un autre scénario) */
        else if (!strncmp(tok, "musique:", 8)) {   /* comme l'instruction : la chaîne du scénario qui porte ce nom */
            const char *nm = tok + 8;
            E.mus_force = !strcmp(nm, "auto") ? 65535 : !strcmp(nm, "aucune") ? 65534 : 65535;
            for (int s = 0; s < E.w.sc_nstr && E.mus_force == 65535 && strcmp(nm, "auto"); s++) {
                char nb[24]; script_string(s, nb, sizeof nb);
                if (!strcmp(nb, nm)) E.mus_force = (uint16_t)s;
            }
        }   /* PV de l'ennemi du combat en cours (ehp:0 : vaincu au prochain tour) */
        else if (sscanf(tok, "quete:%d:%f", &n, &a) == 2 && n >= 0 && n < SC_MAX_QUESTS) { E.quests[n] = (uint8_t)a; E.qtrack = n; }   /* état d'une quête, suivie */
        /* Police (police.c) : état ; recherche:n[:m] (niveau n imposé, motif m : 1 vol, 2 effraction, Sera signalée là où
           elle est) ; poursuite:n[:k] (n images, k = 1 gaz tenu ; état toutes les 30 images, arrêt au début d'une
           arrestation) ; arreter (la scène tout de suite) ; bmp:fichier (l'image en cours) */
        else if (!strcmp(tok, "police")) { police_etat(""); continue; }
        /* Pilote automatique (pilote.c) : pilote:x:z[:n] ou pilote:NOM[:n] (le lieu du guidage de ce nom le plus proche) :
           Sera monte dans une voiture là où elle est (cap : celui de la caméra), le pilote part vers ce point ; n images
           (5400 par défaut) de mise à jour seule, sans rendu ; état toutes les 150 images, puis le bilan (arrivée, cases
           hors route, marche arrière, temps de eng_update). assist:n[:libre] : conduite assistée (ou libre), gaz tenu sans
           braquer n images ; état toutes les 15 images. tele : lieux visités du téléport ; choix : la boîte de choix en
           cours ; pres_trafic : Sera à côté du premier véhicule du trafic (OK : la confirmation du vol) ; conduite:libre |
           assistee ; esve:p : les sauvegardes de la partie p réécrites au format ESVE (d'avant les lieux visités),
           charger:p : la partie p chargée */
        else if (!strncmp(tok, "pilote:", 7)) {
            float px = 0, pz = 0; int g = -1, nmax = 5400;
            if (sscanf(tok, "pilote:%f:%f:%d", &px, &pz, &nmax) < 2) {
                char nom[48] = "";
                sscanf(tok + 7, "%47[^:]:%d", nom, &nmax);
                float bd = 1e9f;
                for (int k = 0; k < E.w.nguides; k++) if (E.w.guides[k].carte == E.w.carte && strstr(E.w.guides[k].nom, nom)) {
                    const float d = hypotf(E.w.guides[k].x - E.sera.x, E.w.guides[k].z - E.sera.z);
                    if (d < bd) { bd = d; g = k; }
                }
                if (g < 0) { printf("pilote : pas de lieu « %s »\n", nom); continue; }
                px = E.w.guides[g].x; pz = E.w.guides[g].z;
            }
            world_stream(E.sera.x, E.sera.z, 1);
            if (!monter_voiture(E.yaw)) { printf("pilote : pas de voiture\n"); continue; }
            const float x0 = E.sera.x, z0 = E.sera.z;
            const uint32_t t0 = clock_pc();
            const int ok = pilote_lancer(px, pz, g, g >= 0 ? E.w.guides[g].nom : "POINT");
            printf("pilote de %.1f %.1f vers %.1f %.1f (%s, %.0f cases à vol d'oiseau) : %s, plan en %u µs\n", x0, z0, px, pz, g >= 0 ? E.w.guides[g].nom : "point",
                   hypotf(px - x0, pz - z0), ok ? "parti" : hud_toast_txt(), (unsigned)(clock_pc() - t0));
            if (!ok) continue;
            eng_input_t rien = { 0 }, okk = { 0 };
            okk.keys = ENG_KEY_OK;
            double su = 0; uint32_t um = 0; int hors = 0, arrive = -1, recul = 0, scenes = 0, f2;
            float parcours = 0, ox = E.sera.x, oz = E.sera.z;
            const int trace = getenv("ECHOES_PILOTE_TRACE") ? atoi(getenv("ECHOES_PILOTE_TRACE")) : 0;
            for (f2 = 1; f2 <= nmax && PIL.on; f2++) {
                /* une réplique ou un choix du scénario (zone traversée) : OK toutes les 10 images, comme un joueur qui lit */
                const int lire = (hud_in_dialog() || hud_in_choice()) && f2 % 10 == 0;
                scenes += lire;
                const uint32_t a0 = clock_pc();
                eng_update(1 / 30.0f, lire ? &okk : &rien);
                const uint32_t du = clock_pc() - a0;
                if (trace && f2 % trace == 0) printf("    %5d : %.2f %.2f cap %.2f v %.2f, visé %.2f %.2f, reste %.1f, recul %.2f bloqué %.2f, script %d%s\n", f2, E.sera.x, E.sera.z, E.sera.dir, E.ride_v,
                                                     PIL.ax, PIL.az, PIL.reste, PIL.recul_t, PIL.bloque_t, script_running(), sur_route(E.sera.x, E.sera.z) ? "" : " HORS ROUTE");
                su += du; if (du > um) um = du;
                parcours += hypotf(E.sera.x - ox, E.sera.z - oz); ox = E.sera.x; oz = E.sera.z;
                if (n_traj < TRAJ_MAX) { traj[n_traj][0] = E.sera.x; traj[n_traj][1] = E.sera.z; n_traj++; }
                hors += !sur_route(E.sera.x, E.sera.z); recul += PIL.recul_t > 0;
                if (PIL.on == 2 && arrive < 0) arrive = f2;
                if (f2 % 150 == 0) printf("  %5d : en %.1f %.1f, v %.2f, reste %.0f, visé %.1f %.1f%s\n", f2, E.sera.x, E.sera.z, E.ride_v, PIL.reste, PIL.ax, PIL.az, sur_route(E.sera.x, E.sera.z) ? "" : " (hors route)");
            }
            printf("  bilan : %s, « %s » après %d images (%.0f s), %.0f cases parcourues, à %.1f cases du but ; %d images hors route, %d en marche arrière,"
                   " %d répliques lues ; eng_update %.0f µs en moyenne, %u au plus ; champ fin %d µs au plus\n", PIL.on ? "toujours en route" : arrive >= 0 ? "arrivé" : "coupé", hud_toast_txt(), f2 - 1, (f2 - 1) / 30.0f, parcours,
                   hypotf(px - E.sera.x, pz - E.sera.z), hors, recul, scenes, f2 > 1 ? su / (f2 - 1) : 0, (unsigned)um, PIL.us_fin_max);
            for (int k = 0; k < 3; k++) frame(&none);
            continue;
        }
        else if (sscanf(tok, "assist:%d", &n) == 1) {
            const int libre = strstr(tok, "libre") != NULL, avant = E.conduite;
            E.conduite = libre ? CONDUITE_LIBRE : CONDUITE_ASSISTEE;
            world_stream(E.sera.x, E.sera.z, 1);
            if (!monter_voiture(E.yaw)) { printf("assist : pas de voiture\n"); continue; }
            eng_input_t up = { 0 }; up.keys = ENG_KEY_UP;
            int hors = 0, aide = 0, f2;
            printf("conduite %s de %.1f %.1f, cap %.2f\n", libre ? "libre" : "assistée", E.sera.x, E.sera.z, E.sera.dir);
            for (f2 = 1; f2 <= n; f2++) {
                eng_update(1 / 30.0f, &up);
                hors += !sur_route(E.sera.x, E.sera.z); aide += PIL.assist != 0;
                if (f2 % 15 == 0) printf("  %4d : en %.1f %.1f, cap %.2f, v %.2f, %s, aide %d (%.2f)\n", f2, E.sera.x, E.sera.z, E.sera.dir, E.ride_v,
                                         sur_route(E.sera.x, E.sera.z) ? "route" : "HORS ROUTE", PIL.assist, PIL.autorite);
            }
            printf("  bilan : %d images hors route sur %d, aide %d images\n", hors, n, aide);
            E.conduite = avant;
            for (int k = 0; k < 3; k++) frame(&none);
            continue;
        }
        else if (!strncmp(tok, "champs:", 7)) {
            /* La grille grossière du pilote (1 pixel par case) : voie grise, urbaine brune, fermée noire, plus claire près du but ;
               la fenêtre fine (cases de route : vertes, plus claires près du but), la trajectoire en rouge */
            int gn, gs, fi0, fj0, fn; const uint8_t *cl; const uint16_t *gd, *fd;
            if (!pilote_champs(&gn, &gs, &cl, &gd, &fi0, &fj0, &fn, &fd)) { printf("pas de champs%c", 10); continue; }
            const int N = E.w.n;
            static uint8_t im[1024 * 1024 * 3];
            if (N > 1024) continue;
            for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
                const int q = (z / gs) * gn + x / gs, c = cl[q >> 2] >> ((q & 3) * 2) & 3, g = gd[q];
                const int l = g == 0xFFFF ? 0 : 255 - (g > 600 ? 600 : g) * 200 / 600;
                uint8_t *o = im + (z * N + x) * 3;
                o[0] = (uint8_t)(c == 1 ? l * 3 / 4 : c == 2 ? l / 2 : 10); o[1] = (uint8_t)(c == 1 ? l * 3 / 4 : c == 2 ? l / 3 : 10); o[2] = (uint8_t)(c == 1 ? l * 3 / 4 : c == 2 ? l / 5 : 10);
                const int li = x - fi0, lj = z - fj0;
                if (li >= 0 && lj >= 0 && li < fn && lj < fn && fd[lj * fn + li] < 0xFFFD) { o[0] = 0; o[1] = (uint8_t)(255 - (fd[lj * fn + li] % 64) * 2); o[2] = 60; }
            }
            for (int k = 0; k < n_traj; k++) { const int x = (int)traj[k][0], z = (int)traj[k][1]; if (x >= 0 && z >= 0 && x < N && z < N) { uint8_t *o = im + (z * N + x) * 3; o[0] = 255; o[1] = 30; o[2] = 30; } }
            FILE *pf = fopen(tok + 7, "wb");
            if (pf) { fprintf(pf, "P6 %d %d 255%c", N, N, 10); fwrite(im, 3, (size_t)N * N, pf); fclose(pf); printf("champs : %s%c", tok + 7, 10); }
            continue;
        }
        else if (!strcmp(tok, "tele")) { static char tv[4096]; hud_tele_vus(tv, sizeof tv); printf("téléport : %s\n", tv); continue; }
        else if (!strncmp(tok, "cases:", 6)) {   /* cases:x0:z0:w:h : drapeaux des cases (R route, S rue, H sous un bâtiment, ~ eau, # solide au centre, . autre) */
            int x0, z0, w, h;
            if (sscanf(tok, "cases:%d:%d:%d:%d", &x0, &z0, &w, &h) != 4) continue;
            world_stream(x0 + w / 2.0f, z0 + h / 2.0f, 1);
            for (int j = z0; j < z0 + h; j++) {
                printf("  %4d ", j);
                for (int i = x0; i < x0 + w; i++) {
                    const chunk_t *ch = chunk_of_cell(i, j);
                    if (!ch) { putchar(' '); continue; }
                    const int l = cell_local(i, j); const uint8_t fl = ch->flags[l];
                    int so = 0;
                    for (uint32_t k = 0; k < ch->nsolid && !so; k++) { const float *r = &ch->solids[k * 4]; so = i + 0.5f > r[0] && i + 0.5f < r[2] && j + 0.5f > r[1] && j + 0.5f < r[3]; }
                    putchar(ch->hidden[l] ? 'H' : so ? '#' : (fl & F_ROAD) ? 'R' : (fl & F_STREET) ? 'S' : (fl & (F_WATER | F_RIVER)) ? '~' : '.');
                }
                putchar(10);
            }
            continue;
        }
        else if (!strcmp(tok, "autour")) {   /* personnages et trafic à moins de 3 cases de Sera (devant : f > 0) */
            const float hx = sinf(E.sera.dir), hz = cosf(E.sera.dir);
            for (int k = 0; k < E.nnpc; k++) {
                const npc_t *nn = &E.npcs[k];
                const float dx = nn->x - E.sera.x, dz = nn->z - E.sera.z;
                if (k != E.ride && fabsf(dx) < 3 && fabsf(dz) < 3) printf("  pnj %d %s en %.2f %.2f : devant %.2f, côté %.2f, présent %d, marche %d\n", k, E.w.persos[nn->perso].nom, nn->x, nn->z,
                                                                       dx * hx + dz * hz, dx * hz - dz * hx, npc_present(nn), nn->walking);
            }
            for (int k = 0; k < TRAFFIC_MAX; k++) if (E.traffic[k].on && hypotf(E.traffic[k].x - E.sera.x, E.traffic[k].z - E.sera.z) < 3) printf("  trafic %d en %.2f %.2f\n", k, E.traffic[k].x, E.traffic[k].z);
            continue;
        }
        else if (!strcmp(tok, "choix")) { char tc[256]; printf("choix : %s ; message : %s\n", hud_choix_texte(tc, sizeof tc) ? tc : "aucun", hud_toast_txt()); continue; }
        /* Arène (arene.c) : le choix du rang comme après COMBATTRE (sans le bâtiment) ; arenes:n : rangs gagnés (bits) ; puis
           « choix », « ok » (premier choix), « down » ; arenetat : crédits, rangs, combat, PV entamés */
        else if (!strcmp(tok, "arene")) { arene_proposer(); continue; }
        else if (sscanf(tok, "arenes:%d", &n) == 1) { E.arene_gagnes = n; continue; }
        else if (!strcmp(tok, "arenetat")) { printf("arène : crédits %d, rangs gagnés 0x%x, combat %s, niveau %d, PV", E.credits, E.arene_gagnes, BT.on ? "en cours" : "non", E.level);
            for (int m = 0; m < NMEMB; m++) if (PARTY[m].hp != PARTY[m].max) printf(" %s %d/%d", PARTY[m].name, PARTY[m].hp, PARTY[m].max);
            printf("\n"); continue; }
        else if (!strcmp(tok, "pres_trafic")) {
            int k = 0;
            while (k < TRAFFIC_MAX && !E.traffic[k].on) k++;
            if (k == TRAFFIC_MAX) { printf("aucun véhicule du trafic\n"); continue; }
            const traffic_t *t = &E.traffic[k];
            E.sera.x = t->x + cosf(t->dir) * 1.4f; E.sera.z = t->z - sinf(t->dir) * 1.4f; E.has_dest = 0;
            printf("à côté du trafic %d (%s) en %.1f %.1f\n", k, E.w.persos[t->perso].nom, t->x, t->z);
        }
        else if (!strcmp(tok, "conduite:libre")) E.conduite = CONDUITE_LIBRE;
        else if (!strcmp(tok, "conduite:assistee")) E.conduite = CONDUITE_ASSISTEE;
        else if (!strcmp(tok, "commandes:doubles")) E.commandes = COMMANDES_DOUBLES;
        else if (!strcmp(tok, "commandes:joystick")) E.commandes = COMMANDES_JOYSTICK;
        else if (!strcmp(tok, "commandes:pointer")) E.commandes = COMMANDES_POINTER;
        else if (sscanf(tok, "charger:%d", &n) == 1) printf("partie %d chargée : %d ; conduite %d\n", n + 1, hud_charger(n), E.conduite);
        /* Mixage de SYSTÈME › SON : mixage:canal:niveau (CANAL_* d'audio.h, 0 à 1) le règle, mixage l'affiche ; sauver :
           sauvegarde de la partie en cours (eng_sauvegarder) */
        else if (sscanf(tok, "mixage:%d:%f", &n, &a) == 2) audio_canal(n, a);
        else if (!strcmp(tok, "sauver")) printf("sauvegarde : %d\n", eng_sauvegarder());
        else if (!strcmp(tok, "mixage")) printf("mixage : général %.2f, musique %.2f, effets %.2f, moteurs %.2f, interface %.2f\n", eng_volume(),
                                             audio_canal_niveau(CANAL_MUSIQUE), audio_canal_niveau(CANAL_EFFETS), audio_canal_niveau(CANAL_MOTEURS), audio_canal_niveau(CANAL_INTERFACE));
        else if (sscanf(tok, "esvh:%d", &n) == 1) {
            /* ESVI -> ESVH : sans le brouillard qui suit la sauvegarde ni l'arène (16 octets avant la somme de contrôle) */
            for (int sl = n * 2; sl <= n * 2 + 1; sl++) {
                static uint8_t sb[65536];
                char nf[32]; snprintf(nf, sizeof nf, "build/sauve%d.bin", sl);
                FILE *sf = fopen(nf, "rb"); if (!sf) continue;
                const int nl = (int)fread(sb, 1, sizeof sb, sf); fclose(sf);
                const int tete = nl - (int)brouillard_taille();   /* sizeof(save_t) */
                if (nl < 64 || memcmp(sb, "ESVI", 4) || tete < 64) { printf("%s : pas une ESVI\n", nf); continue; }
                const int fin = tete - 4 - 16;
                memcpy(sb, "ESVH", 4);
                uint32_t sc = 0xFFFFFFFFu;
                for (int i = 0; i < fin; i++) { sc ^= sb[i]; for (int k2 = 0; k2 < 8; k2++) sc = (sc >> 1) ^ (0xEDB88320u & -(sc & 1)); }
                sc = ~sc; memcpy(sb + fin, &sc, 4);
                sf = fopen(nf, "wb"); if (sf) { fwrite(sb, 1, fin + 4, sf); fclose(sf); printf("%s : réécrite en ESVH (%d octets, %d avant)\n", nf, fin + 4, nl); }
            }
            continue;
        }
        else if (sscanf(tok, "esve:%d", &n) == 1 || sscanf(tok, "esvf:%d", &n) == 1) {
            /* ESVG -> ESVF : sans le véhicule de Sera (garage_sv_t, avant la somme de contrôle) ; -> ESVE : sans non plus le
               bloc des lieux visités et de la conduite (36 octets) */
            const int vers_e = tok[3] == 'e';
            for (int sl = n * 2; sl <= n * 2 + 1; sl++) {
                static uint8_t sb[65536];
                char nf[32]; snprintf(nf, sizeof nf, "build/sauve%d.bin", sl);
                FILE *sf = fopen(nf, "rb"); if (!sf) continue;
                const int nl = (int)fread(sb, 1, sizeof sb, sf); fclose(sf);
                const int g = !memcmp(sb, "ESVG", 4), f = !memcmp(sb, "ESVF", 4);
                if (nl < 64 || (!g && !(f && vers_e))) { printf("%s : pas une ESVG%s\n", nf, vers_e ? " ni une ESVF" : ""); continue; }
                const int fin = nl - 4 - (g ? (int)sizeof(garage_sv_t) : 0) - (vers_e ? 36 : 0);
                memcpy(sb, vers_e ? "ESVE" : "ESVF", 4);
                uint32_t sc = 0xFFFFFFFFu;
                for (int i = 0; i < fin; i++) { sc ^= sb[i]; for (int k2 = 0; k2 < 8; k2++) sc = (sc >> 1) ^ (0xEDB88320u & -(sc & 1)); }
                sc = ~sc; memcpy(sb + fin, &sc, 4);
                sf = fopen(nf, "wb"); if (sf) { fwrite(sb, 1, fin + 4, sf); fclose(sf); printf("%s : réécrite en %s (%d octets)\n", nf, vers_e ? "ESVE" : "ESVF", fin + 4); }
            }
            continue;
        }
        /* Donjons (js/donjons.js) : « ou » : carte, place, combat, script, objectif ; « q:k » : étape de la quête k ; « jouer:n » : n images, comme
           un joueur pressé : OK aux répliques, chaque combat gagné aussitôt et son tableau passé ; s'arrête à une boîte de choix (ensuite « down », « ok ») */
        /* « essai:L » : partie d'essai au niveau L (donjons) : la bande des quatre au combat, chacun équipé du meilleur de ce que vendent les boutiques à ce
           niveau, PV et PM pleins, 20 potions, 5 seringues, 8 éthers, crédits ; ensuite « q… », « pos:… », « sauver » */
        else if (sscanf(tok, "piece:%d:%f", &n, &a) == 2 && n >= 0 && n < VK_N) { E.veh_up[n] = (uint8_t)a; printf("pièce %d (%s) : niveau %d, véhicule de Sera %d, flotte %d, noyé ici %d%c", n, vk_nom(n), E.veh_up[n], garage_perso(), garage_perso() >= 0 ? veh_flotte(garage_perso()) : -1, garage_perso() >= 0 ? veh_noye(garage_perso(), E.sera.x, E.sera.z) : -1, 10); }   /* piece:k:n : pièce du véhicule de Sera posée (garage.c) */
        else if (!strcmp(tok, "conduite")) { float vm, ac; if (E.ride >= 0) { veh_params(E.npcs[E.ride].perso, &vm, &ac); printf("au volant : %s, v %.2f sur %.2f, altitude %.2f, coule %.2f, eau %.2f, en %.1f %.1f%c", E.w.persos[E.npcs[E.ride].perso].nom, E.ride_v, vm, E.ride_alt, E.ride_coule, eau_prof(E.sera.x, E.sera.z), E.sera.x, E.sera.z, 10); } else printf("à pied en %.1f %.1f%c", E.sera.x, E.sera.z, 10); }
        else if (!strcmp(tok, "concession")) { printf("dans la concession : %d%c", concession_ici(), 10); concession_ouvrir(); printf("menu des véhicules %d, %s%c", hud_vehicules_open(), hud_toast_txt(), 10); }   /* VÉHICULES de la concession (garage.c) */
        else if (!strcmp(tok, "prixvendus")) { for (int p = 0; p < E.w.npersos && p < MAX_PERSOS; p++) if (veh_proposable(p) && !E.w.veh_service[p]) printf("  %-22s type %d, %d CR%c", E.w.persos[p].nom, VEH_TYPE(E.w.persos[p].flags), veh_prix(p), 10); }
        else if (!strcmp(tok, "sortirint")) { float ix, iz, yw; if (E.in.on) { int_position(&ix, &iz, &yw); int_reset(); E.sera.x = ix; E.sera.z = iz; E.sera_y_ok = 0; printf("sortie en %.1f %.1f%c", ix, iz, 10); } }
        else if (sscanf(tok, "evq:%d:%d", &n, &m2) == 2) {   /* evq:Q:S[:i] : i-ième événement de la quête Q à l'étape S : quête à cette étape, sa condition remplie, pas encore fait ; Sera sur sa zone, ou le dialogue avec son personnage */
            int vis = 0, kk = 0; sscanf(tok, "evq:%*d:%*d:%d", &kk);
            for (int k = 0; k < E.w.sc_nev; k++) {
                const sc_event_t *ev = &E.w.sc_ev[k];
                if (ev->quest != n || ev->step != m2 || vis++ < kk) continue;
                E.quests[n] = (uint8_t)m2;
                if (ev->cond < SC_MAX_FLAGS) { if (ev->flags & 2) E.flags[ev->cond >> 3] &= (uint8_t)~(1 << (ev->cond & 7)); else E.flags[ev->cond >> 3] |= (uint8_t)(1 << (ev->cond & 7)); }
                E.ev_done[k >> 3] &= (uint8_t)~(1 << (k & 7));
                printf("événement %d : déclencheur %d, personnage %d (%s), en %.1f %.1f, rayon %.1f, carte %d%c", k, ev->trigger, ev->perso, ev->perso < E.w.npersos ? E.w.persos[ev->perso].nom : "-", ev->x, ev->z, ev->r, ev->carte, 10);
                if (ev->trigger == 2) { for (int q = 0; q < E.nnpc; q++) if (E.npcs[q].perso == ev->perso) { eng_teleport(E.npcs[q].x - 1.2f, E.npcs[q].z, E.yaw); break; } for (int f3 = 0; f3 < 4; f3++) frame(&none); { int q2 = -1; for (int q = 0; q < E.nnpc && q2 < 0; q++) if (E.npcs[q].perso == ev->perso) q2 = q; printf("parler : %d%c", q2 >= 0 ? script_talk(q2) : -1, 10); } }
                else eng_teleport(ev->x, ev->z, E.yaw);
                break;
            }
        }
        else if (!strcmp(tok, "pieds")) {   /* pieds : pour chaque ennemi et chaque animation (attente, marche, course), écart entre l'ancrage (PIVO) et le pixel opaque le plus bas, par direction : vide sous les pieds, en pixels de planche et à l'écran */
            static const char *NA[16] = { "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "a10", "a11", "a12", "a13", "a14", "a15" };
            int nbad = 0;
            for (int e = 0; e < E.w.nenemies; e++) {
                const perso_t *pe = &E.w.persos[E.w.enemies[e].perso];
                for (int a3 = 0; a3 < ANIM_N; a3++) {
                    if (a3 != ANIM_IDLE && !has_anim(pe->sheet, a3)) continue;
                    const int s3 = anim_sheet(pe->sheet, a3); const sheet_t *sh3 = &E.w.sheets[s3];
                    int pire = -999, pd = 0, moins = 999;
                    for (int d = 0; d < 8; d++) {
                        const uint8_t *blk = sheet_block(s3, d, 1); if (!blk) continue;
                        int bas = -1;
                        for (int y = sh3->fh - 1; y >= 0 && bas < 0; y--) for (int x = 0; x < sh3->fw * sh3->frames; x++) if (blk[y * sh3->fw * sh3->frames + x]) { bas = y; break; }
                        if (bas < 0) continue;
                        const int gap = (sh3->has_pv ? sh3->pv[d][1] / 2 : sh3->fh) - (bas + 1);
                        if (gap > pire) { pire = gap; pd = d; }
                        if (gap < moins) moins = gap;
                    }
                    const float ech3 = (pe->taille > 0 ? pe->taille : sh3->taille) * sh3->norm * E.w.fh / (float)sh3->fh;
                    if (pire >= 2) nbad++;
                    printf("  %-24s %-8s cellule %3d x %3d, %d images, ancrage %s, vide au plus %3d (dir %d), au moins %3d, soit %.1f px (échelle %.2f)%c", pe->nom, NA[a3], sh3->fw, sh3->fh, sh3->frames, sh3->has_pv ? "PIVO" : "aucun", pire, pd, moins, pire * ech3, ech3, 10);
                }
            }
            printf("pieds : %d animations à plus de 2 pixels%c", nbad, 10);
            continue;
        }
        else if (sscanf(tok, "camyaw:%f", &a) == 1) cam_yaw = a * PI_F / 180;   /* scenecams : lacet de Sera à l'arrivée (degrés) */
        else if (sscanf(tok, "sceneaudit:%d:%d", &n, &m2) == 2) {   /* sceneaudit:k0:k1 : joue les événements k0 à k1 - 1 à l'allure d'un joueur (une réplique toutes les 1,5 s) et relève tout ce qui gêne : MOUV (chaque mouvement de caméra), LIGNE (chaque réplique : où est celui qui parle), OBST (quelqu'un dans un obstacle), CHEV (deux personnages l'un dans l'autre), AUD (bilan) */
            extern int script_lancer_essai(int k);
            extern int eng_essai_planche_absente;
            eng_input_t rien3 = { 0 };
            for (int k = n; k < m2 && k < E.w.sc_nev; k++) {
                const sc_event_t *ev = &E.w.sc_ev[k];
                script_abort(); BT.on = 0; police_reset();
                if (E.in.on) int_reset();
                if (ev->carte != E.w.carte) world_carte(ev->carte, ev->x > 0 ? ev->x : E.w.cartes[ev->carte].sx, ev->x > 0 ? ev->z : E.w.cartes[ev->carte].sz);
                if (ev->quest < E.w.sc_nq) E.quests[ev->quest] = (uint8_t)ev->step;
                if (ev->cond < SC_MAX_FLAGS) { if (ev->flags & 2) E.flags[ev->cond >> 3] &= (uint8_t)~(1 << (ev->cond & 7)); else E.flags[ev->cond >> 3] |= (uint8_t)(1 << (ev->cond & 7)); }
                E.ev_done[k >> 3] &= (uint8_t)~(1 << (k & 7));
                if (ev->perso >= 0 && ev->perso < E.w.npersos) {
                    const perso_t *pe3 = &E.w.persos[ev->perso];
                    if (pe3->cond < SC_MAX_FLAGS) { if (pe3->neg) E.flags[pe3->cond >> 3] &= (uint8_t)~(1 << (pe3->cond & 7)); else E.flags[pe3->cond >> 3] |= (uint8_t)(1 << (pe3->cond & 7)); }
                }
                E.yaw = cam_yaw;
                E.heure = 0.5f; E.cycle = 0;
                int npc_ev = -1;
                if (ev->trigger == 2) { for (int q = 0; q < E.nnpc; q++) if (E.npcs[q].perso == ev->perso) { npc_ev = q; eng_teleport(E.npcs[q].x, E.npcs[q].z + 1.2f, E.yaw); sera_placer(E.sera.x, E.sera.z, 8); break; } }
                else if (ev->trigger == 3) { for (int q = 0; q < E.w.nportes; q++) { const int b3 = E.w.portes[q].bat; if (b3 >= 0 && b3 < E.w.nbats && E.w.bats[b3].carte == E.w.carte && hypotf(E.w.bats[b3].cx - ev->x, E.w.bats[b3].cz - ev->z) < 4) { int_entrer_porte(q, 1); break; } } }
                else if (ev->x > 0) { const float r3 = (float)ev->r * 0.9f; eng_teleport(ev->x - sinf(E.yaw) * r3, ev->z - cosf(E.yaw) * r3, E.yaw); sera_placer(E.sera.x, E.sera.z, 8); }   /* (là où un joueur peut être, pas dans le mur) */
                for (int f3 = 0; f3 < 3; f3++) frame(&rien3);
                script_abort();
                if (!script_lancer_essai(k)) { printf("EV %d refusé%c", k, 10); continue; }
                float der[5] = { -1e9f, -1e9f, -1e9f, -1e9f, -1e9f }, pcx = 1e9f, pcz = 1e9f, pyaw = 0, vmax = 0, amax = 0, pxp = 0, pzp = 0;
                int f4 = 0, nmouv = 0, nlig = 0, cam_bat = 0, trou_ch = 0, trou_tex = 0, trou_pl = 0, att_l = -1, prec = 0, imgs = 0;
                int a_ob[ACT_MAX + 2] = { 0 }, a_ba[ACT_MAX + 2] = { 0 }, a_ma[ACT_MAX + 2] = { 0 };
                float a_x[ACT_MAX + 2] = { 0 }, a_z[ACT_MAX + 2] = { 0 };
                static int chev[ACT_MAX + 2][ACT_MAX + 2]; memset(chev, 0, sizeof chev);
                int chev_npc[ACT_MAX + 2] = { 0 };
                char der_txt[64] = "", cam_note[64] = "";
                eng_essai_planche_absente = 0;
                for (; f4 < 8000; f4++) {
                    eng_input_t k2 = { 0 };
                    if (CR.on) break;
                    if (hud_course_menu_open()) { extern void hud_course_menu_fermer_essai(void); hud_course_menu_fermer_essai(); break; }
                    if (BT.on) { if (BT.phase != BP_DYING && BT.phase != BP_WIN && E.clock - BT.t0 > 0.3f) { for (int q = 0; q < BT.nen; q++) { BT.en[q].hp = 0; BT.en[q].mort_t = E.clock; } BT.phase = BP_DYING; } if (f4 % 12 == 0) k2.keys = ENG_KEY_OK; frame(&k2); pcx = 1e9f; continue; }
                    /* Chaque mouvement de caméra de la scène */
                    if (MISE.cam_on && MISE.cam_t < 0.05f && fabsf(der[0] - MISE.cam_b[0]) + fabsf(der[1] - MISE.cam_b[1]) + fabsf(der[2] - MISE.cam_b[2]) + fabsf(der[3] - MISE.cam_b[3]) > 0.01f) {
                        memcpy(der, MISE.cam_b, sizeof der);
                        const float dyw = remainderf(MISE.cam_b[2] - MISE.cam_a[2], 2 * PI_F) * 180 / PI_F;
                        printf("MOUV %d %d dist %.1f lacet %.0f zoom %.2f>%.2f duree %.2f noir %.2f %s%s%c", k, nmouv, hypotf(MISE.cam_b[0] - MISE.cam_a[0], MISE.cam_b[1] - MISE.cam_a[1]), dyw, MISE.cam_a[3], MISE.cam_b[3], MISE.cam_d, MISE.fondu, MISE.cam_retour ? "retour" : "plan", MISE.coupe || (MISE.cam_auto && hypotf(MISE.cam_b[0] - MISE.cam_a[0], MISE.cam_b[1] - MISE.cam_a[1]) > CAM_COUPE_AUTO) ? " coupe" : MISE.cam_auto ? " auto" : "", 10);
                        nmouv++;
                    }
                    char tj3[256];
                    if (hud_in_dialog() && (!MISE.cam_on || MISE.cam_t >= MISE.cam_d)) {
                        char qui3[48]; const char *tx3 = hud_replique_vue(qui3, sizeof qui3);
                        if (tx3 && strncmp(tx3, der_txt, 63)) { snprintf(der_txt, sizeof der_txt, "%s", tx3); att_l = 18; }
                        if (tx3 && att_l >= 0 && att_l-- == 0) {
                            extern int eng_essai_projeter(float x, float z, float *sx, float *sy, float *h);
                            float px3 = 0, pz3 = 0, sx3 = 0, sy3 = 0, h3 = 0; int tr3 = 0, eq3 = 0, se3 = 0;
                            if (!strcmp(qui3, "SERA") || (E.heroine >= 0 && !strcmp(qui3, E.w.persos[E.heroine].nom))) { px3 = E.sera.x; pz3 = E.sera.z; tr3 = 1; se3 = 1; }
                            for (int a3 = 0; a3 < ACT_MAX && !tr3; a3++) if (MISE.act[a3].on && MISE.act[a3].perso < E.w.npersos && !strcmp(E.w.persos[MISE.act[a3].perso].nom, qui3)) { px3 = MISE.act[a3].x; pz3 = MISE.act[a3].z; tr3 = 1; }
                            float nd3 = 1e9f;
                            for (int q3 = 0; q3 < E.nnpc && !tr3; q3++) if (E.npcs[q3].perso < E.w.npersos && !strcmp(E.w.persos[E.npcs[q3].perso].nom, qui3) && (npc_present(&E.npcs[q3]) || E.npcs[q3].perso == ev->perso)) { const float d3 = hypotf(E.npcs[q3].x - E.sera.x, E.npcs[q3].z - E.sera.z); if (d3 < nd3) { nd3 = d3; px3 = E.npcs[q3].x; pz3 = E.npcs[q3].z; } }
                            if (nd3 < 20) tr3 = 1;   /* (plus loin : ce n'est pas lui, c'est une voix) */
                            for (int m3 = 0; m3 < NMEMB; m3++) if (!strcmp(PARTY[m3].name, qui3)) eq3 = 1;
                            /* Le plus proche des autres présents dans la scène : celui à qui il parle */
                            float dautre = 1e9f;
                            if (tr3) {
                                if (!se3) dautre = hypotf(px3 - E.sera.x, pz3 - E.sera.z);
                                for (int a3 = 0; a3 < ACT_MAX; a3++) if (MISE.act[a3].on && strcmp(E.w.persos[MISE.act[a3].perso].nom, qui3)) { const float d3 = hypotf(MISE.act[a3].x - px3, MISE.act[a3].z - pz3); if (d3 < dautre) dautre = d3; }
                                if (npc_ev >= 0 && strcmp(E.w.persos[E.npcs[npc_ev].perso].nom, qui3)) { const float d3 = hypotf(E.npcs[npc_ev].x - px3, E.npcs[npc_ev].z - pz3); if (d3 < dautre) dautre = d3; }
                            }
                            const char *et3 = "absent";
                            if (tr3) {
                                et3 = "hors";
                                if (eng_essai_projeter(px3, pz3, &sx3, &sy3, &h3) && sy3 > 8 && sy3 - h3 < 172 - 6 && sx3 > 40 && sx3 < 426) {
                                    const int cache = sy3 > 56 && ((sx3 > 56 + 8 && sx3 < 168 - 8) || (sx3 > 298 + 8 && sx3 < 410 - 8));
                                    et3 = cache ? "portrait" : "vu";
                                }
                            }
                            /* Orientation : l'écart, en degrés, entre le regard de Sera et celui à qui elle a affaire (celui qui parle, sinon le plus proche), et entre celui qui parle et Sera */
                            float ori_s = -1, ori_p = -1;
                            { float cx3 = 0, cz3 = 0; int oc3 = 0;
                              if (!se3 && tr3) { cx3 = px3; cz3 = pz3; oc3 = 1; }
                              else { float db3 = 1e9f; for (int a3 = 0; a3 < ACT_MAX; a3++) if (MISE.act[a3].on) { const float d3 = hypotf(MISE.act[a3].x - E.sera.x, MISE.act[a3].z - E.sera.z); if (d3 < db3) { db3 = d3; cx3 = MISE.act[a3].x; cz3 = MISE.act[a3].z; oc3 = 1; } } if (npc_ev >= 0) { const float d3 = hypotf(E.npcs[npc_ev].x - E.sera.x, E.npcs[npc_ev].z - E.sera.z); if (d3 < db3) { cx3 = E.npcs[npc_ev].x; cz3 = E.npcs[npc_ev].z; oc3 = 1; } } }
                              if (oc3 && hypotf(cx3 - E.sera.x, cz3 - E.sera.z) > 0.05f) ori_s = fabsf(remainderf(E.sera.dir - atan2f(cx3 - E.sera.x, cz3 - E.sera.z), 2 * PI_F)) * 180 / PI_F; }
                            if (tr3 && !se3) {
                                float dp3 = 1e9f; for (int a3 = 0; a3 < ACT_MAX; a3++) if (MISE.act[a3].on && !strcmp(E.w.persos[MISE.act[a3].perso].nom, qui3)) dp3 = MISE.act[a3].dir;
                                if (dp3 > 1e8f) for (int q3 = 0; q3 < E.nnpc; q3++) if (E.npcs[q3].perso < E.w.npersos && fabsf(E.npcs[q3].x - px3) < 0.01f && fabsf(E.npcs[q3].z - pz3) < 0.01f) { dp3 = E.npcs[q3].dir; break; }
                                if (dp3 < 1e8f && hypotf(px3 - E.sera.x, pz3 - E.sera.z) > 0.05f) ori_p = fabsf(remainderf(dp3 - atan2f(E.sera.x - px3, E.sera.z - pz3), 2 * PI_F)) * 180 / PI_F;
                            }
                            printf("LIGNE %d %d %s %s sera %.1f autre %.1f prec %.1f orisera %.0f oriparle %.0f%s%c", k, nlig, et3, qui3, tr3 ? hypotf(px3 - E.sera.x, pz3 - E.sera.z) : -1.0f, dautre < 1e8f ? dautre : -1.0f, tr3 && prec ? hypotf(px3 - pxp, pz3 - pzp) : -1.0f, ori_s, ori_p, eq3 ? " equipe" : "", 10);
                            if (tr3) { pxp = px3; pzp = pz3; prec = 1; }
                            nlig++;
                        }
                    }
                    if ((hud_choix_texte(tj3, sizeof tj3) || hud_in_dialog()) && f4 % 45 == 0) k2.keys = ENG_KEY_OK;
                    frame(&k2);
                    if (!script_running() && !BT.on && !MISE.cam_on) break;
                    if (E.in.on) { pcx = 1e9f; continue; }   /* dedans : l'ordre exact, la caméra ne se pose pas dans un mur */
                    imgs++;
                    /* Vitesse de la caméra à l'écran (hors fondu au noir, et hors sortie du noir d'un téléport) */
                    if (pcx < 1e8f && MISE.fondu < 0.9f && E.clock - BT.fade_t > 0.1f) {
                        const float v3 = hypotf(E.cam.x - pcx, E.cam.z - pcz) * 30, a3 = fabsf(remainderf(E.cam.yaw - pyaw, 2 * PI_F)) * 180 / PI_F * 30;
                        if (v3 > vmax) vmax = v3;
                        if (a3 > amax) amax = a3;
                    }
                    pcx = E.cam.x; pcz = E.cam.z; pyaw = E.cam.yaw;
                    if (MISE.fondu < 0.9f) {
                        /* Le monde pas chargé là où l'on regarde */
                        const float fx3 = MISE.cam_on ? MISE.cam[0] : E.sera.x, fz3 = MISE.cam_on ? MISE.cam[1] : E.sera.z;
                        int tr = 0;
                        for (int dz3 = -1; dz3 <= 1; dz3++) for (int dx3 = -1; dx3 <= 1; dx3++) { const int ci = (int)floorf(fx3) + dx3 * CS, cj = (int)floorf(fz3) + dz3 * CS; if (ci >= 0 && cj >= 0 && ci < E.w.n && cj < E.w.n && !chunk_of_cell(ci, cj)) tr = 1; }
                        if (tr) trou_ch++;
                        if (eng_stats()->tex_miss > 0) trou_tex++;
                        if (eng_essai_planche_absente > 0) trou_pl++;
                        /* La caméra dans un bâtiment */
                        const int cb3 = audit_dans_bat(E.cam.x, E.cam.y, E.cam.z);
                        if (cb3 > 0) { if (!cam_bat) { snprintf(cam_note, sizeof cam_note, "%.1f %.1f y %.1f", E.cam.x, E.cam.z, E.cam.y); if (getenv("ECHOES_AUDIT_V")) { printf("  CAMDANS image %d : camera %.1f %.1f %.1f on %d retour %d t %.2f/%.2f vise %.1f %.1f lacet %.0f zoom %.2f sera %.1f %.1f etat %d%c", f4, E.cam.x, E.cam.z, E.cam.y, MISE.cam_on, MISE.cam_retour, MISE.cam_t, MISE.cam_d, MISE.cam[0], MISE.cam[1], MISE.cam[2] * 180 / PI_F, MISE.cam[3], E.sera.x, E.sera.z, E.sera.state, 10); ecrire_bmp("build/audit/camdans.bmp"); } } cam_bat++; }
                    }
                    eng_essai_planche_absente = 0;
                    /* Les personnages de la scène : Sera (rang ACT_MAX), les acteurs, celui à qui elle parle (ACT_MAX + 1) */
                    float qx[ACT_MAX + 2], qz[ACT_MAX + 2]; int qon[ACT_MAX + 2], qma[ACT_MAX + 2];
                    for (int a3 = 0; a3 < ACT_MAX; a3++) { qon[a3] = MISE.act[a3].on && MISE.act[a3].carte == E.w.carte; qx[a3] = MISE.act[a3].x; qz[a3] = MISE.act[a3].z; qma[a3] = MISE.act[a3].walking; }
                    qon[ACT_MAX] = E.ride < 0; qx[ACT_MAX] = E.sera.x; qz[ACT_MAX] = E.sera.z; qma[ACT_MAX] = E.sera.state != 0;
                    qon[ACT_MAX + 1] = npc_ev >= 0; if (npc_ev >= 0) { qx[ACT_MAX + 1] = E.npcs[npc_ev].x; qz[ACT_MAX + 1] = E.npcs[npc_ev].z; } qma[ACT_MAX + 1] = 0;
                    for (int a3 = 0; a3 < ACT_MAX + 2; a3++) {
                        if (!qon[a3]) continue;
                        if (audit_dans_solide(qx[a3], qz[a3])) {
                            if (!a_ob[a3]) { a_x[a3] = qx[a3]; a_z[a3] = qz[a3]; }
                            a_ob[a3]++; if (qma[a3]) a_ma[a3]++;
                            if (audit_dans_bat(qx[a3], ground_y(qx[a3], qz[a3]) + 0.5f, qz[a3]) > 0) a_ba[a3]++;
                        }
                        for (int b3 = a3 + 1; b3 < ACT_MAX + 2; b3++) if (qon[b3] && !(b3 == ACT_MAX + 1 && a3 < ACT_MAX && MISE.act[a3].perso == E.npcs[npc_ev].perso) && hypotf(qx[a3] - qx[b3], qz[a3] - qz[b3]) < 0.6f) { if (getenv("ECHOES_AUDIT_V") && !chev[a3][b3]) printf("  CHEVDEB image %d : %d %d en %.1f %.1f et %.1f %.1f%c", f4, a3, b3, qx[a3], qz[a3], qx[b3], qz[b3], 10); chev[a3][b3]++; }
                        if (a3 != ACT_MAX + 1) for (int q3 = 0; q3 < E.nnpc; q3++) {
                            if (q3 == npc_ev || !npc_present(&E.npcs[q3]) || fabsf(E.npcs[q3].x - qx[a3]) > 0.4f || fabsf(E.npcs[q3].z - qz[a3]) > 0.4f) continue;
                            if (hypotf(E.npcs[q3].x - qx[a3], E.npcs[q3].z - qz[a3]) < 0.4f) { chev_npc[a3]++; break; }
                        }
                    }
                }
                #define NOM_Q(i) ((i) == ACT_MAX ? "SERA" : (i) == ACT_MAX + 1 ? (npc_ev >= 0 ? E.w.persos[E.npcs[npc_ev].perso].nom : "?") : E.w.persos[MISE.act[i].perso].nom)
                for (int a3 = 0; a3 < ACT_MAX + 2; a3++) {
                    if (a_ob[a3] > 3) printf("OBST %d %s images %d bat %d marche %d en %.1f %.1f%c", k, a3 < ACT_MAX ? "acteur" : NOM_Q(a3), a_ob[a3], a_ba[a3], a_ma[a3], a_x[a3], a_z[a3], 10);
                    if (chev_npc[a3] > 3) printf("CHEVN %d %s images %d%c", k, a3 < ACT_MAX ? "acteur" : NOM_Q(a3), chev_npc[a3], 10);
                    for (int b3 = a3 + 1; b3 < ACT_MAX + 2; b3++) if (chev[a3][b3] > 3) printf("CHEV %d %s %s images %d%c", k, a3 < ACT_MAX ? "acteur" : NOM_Q(a3), b3 < ACT_MAX ? "acteur" : NOM_Q(b3), chev[a3][b3], 10);
                }
                #undef NOM_Q
                printf("AUD %d images %d carte %d trigger %d mouv %d lignes %d camvmax %.1f camamax %.0f cambat %d %s trouch %d troutex %d troupl %d%c", k, imgs, E.w.carte, ev->trigger, nmouv, nlig, vmax, amax, cam_bat, cam_note[0] ? cam_note : "-", trou_ch, trou_tex, trou_pl, 10);
                if (CR.on) course_abandon();
                script_abort(); BT.on = 0; police_reset();
            }
        }
        else if (sscanf(tok, "scenecams:%d:%d", &n, &m2) == 2) {   /* scenecams:k0:k1 : joue les événements k0 à k1 - 1 et capture chaque plan de caméra une fois posé (build/cams/k_n.bmp), réplique à l'écran ; ligne CAM par plan */
            extern int script_lancer_essai(int k);
            eng_input_t rien3 = { 0 };
            for (int k = n; k < m2 && k < E.w.sc_nev; k++) {
                const sc_event_t *ev = &E.w.sc_ev[k];
                script_abort(); BT.on = 0; police_reset();
                if (E.in.on) int_reset();
                if (ev->carte != E.w.carte) world_carte(ev->carte, ev->x > 0 ? ev->x : E.w.cartes[ev->carte].sx, ev->x > 0 ? ev->z : E.w.cartes[ev->carte].sz);
                if (ev->quest < E.w.sc_nq) E.quests[ev->quest] = (uint8_t)ev->step;
                if (ev->cond < SC_MAX_FLAGS) { if (ev->flags & 2) E.flags[ev->cond >> 3] &= (uint8_t)~(1 << (ev->cond & 7)); else E.flags[ev->cond >> 3] |= (uint8_t)(1 << (ev->cond & 7)); }
                E.ev_done[k >> 3] &= (uint8_t)~(1 << (k & 7));
                if (ev->perso >= 0 && ev->perso < E.w.npersos) {
                    const perso_t *pe3 = &E.w.persos[ev->perso];
                    if (pe3->cond < SC_MAX_FLAGS) { if (pe3->neg) E.flags[pe3->cond >> 3] &= (uint8_t)~(1 << (pe3->cond & 7)); else E.flags[pe3->cond >> 3] |= (uint8_t)(1 << (pe3->cond & 7)); }
                }
                E.yaw = cam_yaw;
                E.heure = 0.5f; E.cycle = 0;   /* de jour : l'angle se juge, sauf heure donnée par la scène */
                if (ev->trigger == 2) { for (int q = 0; q < E.nnpc; q++) if (E.npcs[q].perso == ev->perso) { eng_teleport(E.npcs[q].x, E.npcs[q].z + 1.2f, E.yaw); break; } }
                else if (ev->trigger == 3) { for (int q = 0; q < E.w.nportes; q++) { const int b3 = E.w.portes[q].bat; if (b3 >= 0 && b3 < E.w.nbats && E.w.bats[b3].carte == E.w.carte && hypotf(E.w.bats[b3].cx - ev->x, E.w.bats[b3].cz - ev->z) < 4) { int_entrer_porte(q, 1); break; } } }
                else if (ev->x > 0) { const float r3 = (float)ev->r * 0.9f; eng_teleport(ev->x - sinf(E.yaw) * r3, ev->z - cosf(E.yaw) * r3, E.yaw); }
                for (int f3 = 0; f3 < 3; f3++) frame(&rien3);
                script_abort();
                if (!script_lancer_essai(k)) { printf("EV %d refusé%c", k, 10); continue; }
                int ncam = 0, att = 0, att_f = 0, dlg_f = 0, f4 = 0; float der[4] = { -1e9f, -1e9f, -1e9f, -1e9f };
                char der_txt[64] = ""; int parle_att = -1;
                for (; f4 < 4000; f4++) {
                    eng_input_t k2 = { 0 };
                    if (CR.on) break;
                    if (hud_course_menu_open()) { extern void hud_course_menu_fermer_essai(void); hud_course_menu_fermer_essai(); break; }
                    if (BT.on) { if (BT.phase != BP_DYING && BT.phase != BP_WIN && E.clock - BT.t0 > 0.3f) { for (int q = 0; q < BT.nen; q++) { BT.en[q].hp = 0; BT.en[q].mort_t = E.clock; } BT.phase = BP_DYING; } if (f4 % 12 == 0) k2.keys = ENG_KEY_OK; frame(&k2); continue; }
                    if (MISE.cam_on && !MISE.cam_retour) {
                        if (fabsf(der[0] - MISE.cam_b[0]) + fabsf(der[1] - MISE.cam_b[1]) + fabsf(der[2] - MISE.cam_b[2]) + fabsf(der[3] - MISE.cam_b[3]) > 0.01f) { memcpy(der, MISE.cam_b, sizeof der); att = 1; att_f = dlg_f = 0; }
                        if (att && MISE.cam_t >= MISE.cam_d) { att_f++; if (hud_in_dialog() || hud_in_choice()) dlg_f++; }
                        /* caméra posée, fondu fini et dialogue ouvert depuis 0,5 s (décalage de l'image) ; 3 s au plus */
                        if (att && MISE.cam_t >= MISE.cam_d && ((MISE.fondu < 0.05f && dlg_f >= 15) || att_f >= 90)) {
                            char nb3[96]; snprintf(nb3, sizeof nb3, "build/cams/%03d_%d.bmp", k, ncam);
                            extern int eng_essai_sans_ui; eng_essai_sans_ui = getenv("ECHOES_SANS_UI") != NULL; frame(&rien3); frame(&rien3);
                            ecrire_bmp(nb3); eng_essai_sans_ui = 0;
                            extern int eng_essai_fondus(void);
                            printf("CAM %d %d carte %d x %.1f z %.1f lacet %.0f zoom %.2f sera %.1f %.1f dedans %d noir %.2f fondus %d dlg %d%c", k, ncam, E.w.carte, MISE.cam[0], MISE.cam[1], MISE.cam[2] * 180 / PI_F, MISE.cam[3], E.sera.x, E.sera.z, E.in.on, MISE.fondu, eng_essai_fondus(), hud_in_dialog() || hud_in_choice(), 10);
                            att = 0; ncam++;
                        }
                    }
                    if (att) { frame(&rien3); continue; }
                    char tj3[256];
                    /* Chaque réplique, une fois la caméra posée : celui qui parle est-il à l'écran ? (ligne PARLE) */
                    if (hud_in_dialog() && (!MISE.cam_on || MISE.cam_t >= MISE.cam_d)) {
                        char qui3[48]; const char *tx3 = hud_replique_vue(qui3, sizeof qui3);
                        if (tx3 && strncmp(tx3, der_txt, 63)) { snprintf(der_txt, sizeof der_txt, "%s", tx3); parle_att = 18; }
                        if (tx3 && parle_att >= 0 && parle_att-- == 0) {
                            extern int eng_essai_projeter(float x, float z, float *sx, float *sy, float *h);
                            float px3 = 0, pz3 = 0, sx3, sy3, h3; int tr3 = 0, eq3 = 0;
                            if (!strcmp(qui3, "SERA") || (E.heroine >= 0 && !strcmp(qui3, E.w.persos[E.heroine].nom))) { px3 = E.sera.x; pz3 = E.sera.z; tr3 = 1; }
                            for (int a3 = 0; a3 < ACT_MAX && !tr3; a3++) if (MISE.act[a3].on && MISE.act[a3].perso < E.w.npersos && !strcmp(E.w.persos[MISE.act[a3].perso].nom, qui3)) { px3 = MISE.act[a3].x; pz3 = MISE.act[a3].z; tr3 = 1; }
                            for (int q3 = 0; q3 < E.nnpc && !tr3; q3++) if (E.npcs[q3].perso < E.w.npersos && !strcmp(E.w.persos[E.npcs[q3].perso].nom, qui3) && (npc_present(&E.npcs[q3]) || E.npcs[q3].perso == ev->perso)) { px3 = E.npcs[q3].x; pz3 = E.npcs[q3].z; tr3 = 1; }
                            for (int m3 = 0; m3 < NMEMB; m3++) if (!strcmp(PARTY[m3].name, qui3)) eq3 = 1;
                            const char *et3 = "absent";
                            sx3 = sy3 = h3 = 0;
                            if (tr3) {
                                et3 = "hors";
                                if (eng_essai_projeter(px3, pz3, &sx3, &sy3, &h3) && sy3 > 8 && sy3 - h3 < 172 - 6 && sx3 > 40 && sx3 < 426) {
                                    const int cache = sy3 > 56 && ((sx3 > 56 + 8 && sx3 < 168 - 8) || (sx3 > 298 + 8 && sx3 < 410 - 8));
                                    et3 = cache ? "portrait" : "vu";
                                }
                            }
                            printf("PARLE %d %d %s %s %.0f %.0f %.0f%s%c", k, ncam, et3, qui3, sx3, sy3, h3, eq3 ? " equipe" : "", 10);
                        }
                    }
                    if ((hud_choix_texte(tj3, sizeof tj3) || hud_in_dialog()) && f4 % 20 == 0) k2.keys = ENG_KEY_OK;
                    frame(&k2);
                    if (!script_running() && !BT.on && !MISE.cam_on) break;
                }
                printf("EV %d fini : %d plans, %d images%c", k, ncam, f4, 10);
                if (CR.on) course_abandon();
                script_abort(); BT.on = 0; police_reset();
            }
            continue;
        }
        else if (sscanf(tok, "passage:%d", &n) == 1) { if (E.w.ence) { const int f = E.w.ence[n] | E.w.ence[n + 1] << 8; printf("drapeau %d levé%c", f, 10); if (f < SC_MAX_FLAGS) E.flags[f >> 3] |= (uint8_t)(1 << (f & 7)); } }   /* passage:o : le drapeau du passage de l'enceinte, à l'octet o de son entrée (ENCE) */
        else if (sscanf(tok, "essaidj:%d", &n) == 1) {
            party_set_level(n);
            equipe_scenario(0, 15); E.recrues = 15; E.team = 15;
            for (int m = 0; m < 4; m++) {
                for (int sl = 0; sl < 3; sl++) {
                    int best = EQ_AUCUN, bv = -1;
                    for (int i = 0; i < n_items; i++) {
                        const eqitem_t *r = eq_item(i);
                        if (!r || eq_slot(i) != sl || !eq_aff(m, i) || E.w.eq_pal[i] == 0 || E.w.pal_niv[E.w.eq_pal[i]] > n || (E.w.eq_fl[i] & EQ_ELITE)) continue;
                        const int v = (r->atq + r->mag / 2 + r->def * 4 + r->pv / 10) * (eq_aff(m, i) + 3);
                        if (v > bv) { bv = v; best = i; }
                    }
                    E.eq[m][sl] = (uint8_t)best;
                }
                party_refresh(m); PARTY[m].hp = PARTY[m].max; PARTY[m].mp = PARTY[m].maxmp;
            }
            for (int i = 0; i < n_items; i++) {
                if (!strcmp(ITEMS[i].name, "POTION")) ITEMS[i].count = 20;
                else if (!strcmp(ITEMS[i].name, "SERINGUE DE RAPPEL")) ITEMS[i].count = 5;
                else if (!strcmp(ITEMS[i].name, "ÉTHER")) ITEMS[i].count = 8;
            }
            E.credits = 8000;
            printf("  essai niveau %d : %s PV %d ATQ %d DEF %d | %s PV %d ATQ %d DEF %d\n", n, PARTY[0].name, PARTY[0].max, PARTY[0].atk, PARTY[0].def, PARTY[1].name, PARTY[1].max, PARTY[1].atk, PARTY[1].def);
            continue;
        }
        else if (!strcmp(tok, "ou")) { printf("  carte %d, Sera %.1f %.1f, combat %d, script %d, objectif %.1f %.1f\n", E.w.carte, E.sera.x, E.sera.z, BT.on, script_running(), E.obj[0] / 8.0f, E.obj[1] / 8.0f); continue; }
        else if (sscanf(tok, "q:%d", &n) == 1 && n >= 0 && n < E.w.sc_nq) { printf("  quête %d : étape %d, niveau %d, %d CR\n", n, E.quests[n], E.level, E.credits); continue; }
        else if (sscanf(tok, "jouer:%d", &n) == 1) {
            char tj[256];
            for (int f2 = 0; f2 < n; f2++) {
                if (hud_choix_texte(tj, sizeof tj)) { printf("  choix : %s\n", tj); break; }
                eng_input_t k2 = { 0 };
                if (BT.on && BT.phase != BP_DYING && BT.phase != BP_WIN && E.clock - BT.t0 > 0.5f) { for (int k = 0; k < BT.nen; k++) { BT.en[k].hp = 0; BT.en[k].mort_t = E.clock; } BT.phase = BP_DYING; BT.t0 = E.clock; }
                if ((hud_in_dialog() || BT.on) && f2 % 12 == 0) k2.keys = ENG_KEY_OK;
                frame(&k2);
            }
            continue;
        }
        else if (sscanf(tok, "lire:%d", &n) == 1) {   /* lire:n : n images, OK toutes les 12 images tant qu'une réplique est ouverte */
            for (int f2 = 0; f2 < n; f2++) { eng_input_t k2 = { 0 }; if (hud_in_dialog() && f2 % 12 == 0) k2.keys = ENG_KEY_OK; frame(&k2); }
            continue;
        }
        else if (sscanf(tok, "evzones:%f:%f:%f", &a, &b, &c) == 3) {   /* événements de zone de la carte 0 à moins de c cases de (a, b) */
            for (int k = 0; k < E.w.sc_nev; k++) {
                const sc_event_t *ev = &E.w.sc_ev[k];
                if (ev->trigger == 1 && !ev->carte && hypotf(ev->x - a, ev->z - b) < c)
                    printf("événement %d : zone en %.1f %.1f, rayon %.1f, condition %d%s, quête %d étape %d\n", k, ev->x, ev->z, ev->r, ev->cond, (ev->flags & 2) ? " (non)" : "", ev->quest, ev->step);
            }
            printf("enceintes (ENCE) : %d ; interdite ici : %d\n", E.w.nence, enceinte_interdite(E.sera.x, E.sera.z));
            continue;
        }
        else if (!strncmp(tok, "repertoire", 10)) {   /* repertoire[:mot] : attaques (ENAT) des ennemis dont le nom contient ce mot */
            const char *mot = tok[10] == ':' ? tok + 11 : "";
            for (int k = 0; k < E.w.nenemies; k++) {
                const enemy_t *en = &E.w.enemies[k];
                const char *nm = E.w.persos[en->perso].nom ? E.w.persos[en->perso].nom : "?";
                if (!strstr(nm, mot)) continue;
                printf("ennemi %d : %s, %d attaque(s)%s\n", k, nm, en->natq, en->natq ? "" : " : rafale laser et balayage thermique (règle d'avant)");
                for (int i = 0; i < en->natq; i++) {
                    const uint8_t *a = en->atq + i * ENAT_ATQ;
                    printf("  %d %-20s effet %2d %s poids %3d puissance %.3f%s couleur %02x%02x%02x\n", i, (const char *)a, a[20], a[21] & 1 ? "équipe" : "membre", a[22], (a[24] | a[25] << 8) / 1000.0, a[23] & 1 ? " signature" : "", a[26], a[27], a[28]);
                }
            }
            continue;
        }
        else if (sscanf(tok, "subir:%d:%f", &n, &a) == 2 && BT.on) {   /* subir:k:n : n attaques k du répertoire du premier ennemi (sans ses sorts), dégâts moyens sur Sera (seule au combat) */
            const int ek = BT.en[0].ek, mg = E.w.enemies[ek].magie;
            long tot = 0; int nb = 0;
            E.w.enemies[ek].magie = 0;
            for (int r = 0; r < (int)a; r++) {
                PARTY[0].hp = PARTY[0].max; const int h0 = PARTY[0].hp;
                bt_atq_force = n; BT.actor = BT_ENEMY; BT.phase = BP_ENEMY; BT.t0 = E.clock - 1;
                for (int i = 0; i < 600 && (BT.phase == BP_ENEMY || (BT.phase == BP_ANIM && !BT.an.hit)); i++) frame(&none);
                if (BT.an.hit) { tot += h0 - PARTY[0].hp; nb++; }
            }
            E.w.enemies[ek].magie = mg; bt_atq_force = -1; PARTY[0].hp = PARTY[0].max;
            printf("subir : attaque %d (effet %d), armure %d, accessoire %d, dégâts moyens %.1f sur %d%c", n, BT.an.act.fx, E.eq[0][EQ_ARMURE], E.eq[0][EQ_ACCESS], nb ? (double)tot / nb : 0.0, nb, 10);
        }
        else if (sscanf(tok, "noyau:%d:%f", &n, &a) == 2 && n >= 0 && n < n_items) {   /* noyau:i:e : l'objet i devient le noyau de l'élément e, deux au sac (paquet d'avant les noyaux) */
            ITEMS[n].noyau = (int)a + 1; ITEMS[n].count += 2; printf("objet %d (%s) : noyau %d, %d au sac%c", n, ITEMS[n].name, (int)a, ITEMS[n].count, 10);
        }
        else if (sscanf(tok, "eattaque:%d", &n) == 1 && BT.on) {   /* le premier ennemi debout joue l'attaque n de son répertoire (-1 : au tirage) */
            int u = BT_ENEMY; for (int k = 0; k < BT.nen; k++) if (BT.en[k].hp > 0) { u = BT_ENEMY + k; break; }
            bt_atq_force = n; BT.actor = u; BT.phase = BP_ENEMY; BT.t0 = E.clock - 1;
        }
        else if (!strncmp(tok, "ennemis", 7)) {   /* ennemis[:mot] : fiches de combat (ENNE) dont le nom contient ce mot */
            const char *mot = tok[7] == ':' ? tok + 8 : "";
            for (int k = 0; k < E.w.nenemies; k++) {
                const enemy_t *en = &E.w.enemies[k];
                const char *nm = E.w.persos[en->perso].nom ? E.w.persos[en->perso].nom : "?";
                if (strstr(nm, mot)) printf("ennemi %d : %s, niveau %d, PV %d, ATQ %d, VIT %d, poids %d, groupe %d, sorts %d, zones %08x\n", k, nm, en->lvl, en->hp, en->atk, en->spd, en->poids, en->groupe, en->sorts != 0, (unsigned)en->zones);
            }
            continue;
        }
        else if (!strcmp(tok, "villes")) {   /* villes du guidage (GUID) */
            for (int k = 0; k < E.w.nguides; k++) if (E.w.guides[k].kind == GUIDE_VILLE) printf("ville %s en %.1f %.1f (rayon %.1f)\n", E.w.guides[k].nom, E.w.guides[k].x, E.w.guides[k].z, E.w.guides[k].r);
            continue;
        }
        else if (sscanf(tok, "recherche:%d", &n) == 1) {
            int m = 1; sscanf(tok, "recherche:%*d:%d", &m);
            POL.niveau = n < 0 ? 0 : n > 3 ? 3 : n; POL.motif |= m; POL.vu_t = POL.t; POL.lx = E.sera.x; POL.lz = E.sera.z; POL.spawn_t = 0;
            if (m & POL_EFFRACTION) POL.amende_eff += 100;
        }
        else if (sscanf(tok, "poursuite:%d", &n) == 1) {
            int m = 0; sscanf(tok, "poursuite:%*d:%d", &m);
            eng_input_t up = { 0 }; up.keys = m == 1 ? ENG_KEY_UP : 0;
            for (int f2 = 1; f2 <= n; f2++) {
                frame(&up);
                if (f2 % 30 == 0 || POL.arr) { char q[24]; snprintf(q, sizeof q, "  %4d :", f2); police_etat(q); }
                if (POL.arr) break;
            }
            continue;
        }
        else if (sscanf(tok, "fuir:%d", &n) == 1) {
            /* fuir:n : un fuyard au volant, au plus n images : gaz tenu (la conduite assistée suit la voie) ; bloqué 0,4 s
               (contre un mur, le trafic, une voiture de police) : marche arrière 0,8 s en braquant, puis repart en braquant
               de l'autre côté 0,4 s ; à pied (véhicule perdu) : rien. Fin : prise par la police (arrestation ou combat),
               semée, ou le temps écoulé ; état toutes les 60 images. Bilan : « fuite : … après t s » (mesure de police.c) */
            /* fuir:n:1 : conduite libre (sans l'aide au maintien de voie : le joueur maladroit de la montre) */
            int libre = 0; sscanf(tok, "fuir:%*d:%d", &libre);
            const int cond0 = E.conduite;
            if (libre) E.conduite = CONDUITE_LIBRE;
            float bloque = 0, man = 0, parcours = 0, ox = E.sera.x, oz = E.sera.z;
            int f2, fin = 0, chocs = 0, sens = 1;
            for (f2 = 1; f2 <= n && !fin; f2++) {
                eng_input_t k2 = { 0 };
                if (E.ride >= 0 && !BT.on && !POL.arr) {
                    if (man > 0) { man -= 1 / 30.0f; k2.keys = man > 0.4f ? ENG_KEY_DOWN | (sens > 0 ? ENG_KEY_LEFT : ENG_KEY_RIGHT) : ENG_KEY_UP | (sens > 0 ? ENG_KEY_RIGHT : ENG_KEY_LEFT); }
                    else {
                        k2.keys = ENG_KEY_UP;
                        bloque = fabsf(E.ride_v) < 0.3f ? bloque + 1 / 30.0f : 0;
                        if (bloque > 0.4f) { bloque = 0; man = 1.2f; chocs++; sens = -sens; }
                    }
                }
                if (hud_in_dialog() && f2 % 10 == 0) k2.keys = ENG_KEY_OK;
                frame(&k2);
                parcours += hypotf(E.sera.x - ox, E.sera.z - oz); ox = E.sera.x; oz = E.sera.z;
                if (f2 % 60 == 0) { char q[24]; snprintf(q, sizeof q, "  %4d :", f2); police_etat(q); }
                fin = BT.on ? 1 : POL.arr ? 2 : !POL.niveau ? 3 : 0;
            }
            printf("fuite : %s après %.1f s, %.0f cases parcourues, %d manœuvres, %s\n", fin == 1 ? "combat contre la police" : fin == 2 ? "arrêtée" : fin == 3 ? "semée" : "toujours recherchée",
                   (f2 - 1) / 30.0f, parcours, chocs, E.ride >= 0 ? "au volant" : "à pied");
            E.conduite = cond0;
            continue;
        }
        else if (!strcmp(tok, "arreter")) police_arreter(0, 0, POL.clan);
        else if (!strncmp(tok, "bats:", 5)) {   /* bats:mot : bâtiments visitables dont le nom contient ce mot (PORT) */
            for (int i = 0; i < E.w.nbats; i++)
                if (strstr(E.w.bats[i].nom, tok + 5)) {
                    int pt = -1;
                    for (int k = 0; k < E.w.nportes && pt < 0; k++) if (E.w.portes[k].bat == i) pt = k;
                    printf("bâtiment %d : %s, carte %d, autorité %d, intérieur %d%s, en %.1f %.1f, porte %d, club %d\n", i, E.w.bats[i].nom, E.w.bats[i].carte,
                           E.w.bats[i].autorite, E.w.bats[i].intr, int_trop_grand(E.w.bats[i].intr) ? " (trop grand)" : "", E.w.bats[i].cx, E.w.bats[i].cz, pt, E.w.bats[i].club);
                }
            continue;
        }
        else if (!strncmp(tok, "scene:", 6)) {
            /* scene:préfixe : la scène de l'arrestation et de la prison jouée jusqu'au bout (OK toutes les 12 images dans un
               dialogue) ; état et capture préfixe-<phase>.bmp à chaque phase (1 s après son début ; la sortie : 1 s après) */
            static const char *PH[] = { "fin", "halte", "encadree", "dialogue", "fondu", "cellule", "garde", "paroles", "sortie", "retour" };
            int ph = -1, n = 0; float t0 = 0;
            char nomf[160];
            for (int f2 = 0; f2 < 3000 && (POL.arr || ph || E.clock - t0 < 1.05f); f2++) {
                eng_input_t k2 = { 0 };
                if (hud_in_dialog() && f2 % 12 == 0) k2.keys = ENG_KEY_OK;
                frame(&k2);
                if (POL.arr != ph) { ph = POL.arr; t0 = E.clock; n = 0; char q[32]; snprintf(q, sizeof q, "  [%s]", PH[ph % 10]); police_etat(q); }
                /* la cellule : quand le garde entre (le carton est parti) ; le garde : arrivé près d'elle, avant qu'il parle */
                const int pres = ph == ARR_GARDE && POL.garde >= 0 && POL.garde < E.nnpc && hypotf(E.npcs[POL.garde].x - E.sera.x, E.npcs[POL.garde].z - E.sera.z) < 0.95f * (E.in.f > 0 ? E.in.f : 1);
                if (ph == ARR_GARDE && n == 1 && pres) { n = 2; snprintf(nomf, sizeof nomf, "%s-garde.bmp", tok + 6); ecrire_bmp(nomf); police_etat("  [garde arrivé]"); }
                if (!n && E.clock - t0 >= (ph == ARR_HALTE || ph == ARR_FONDU || ph == ARR_SORTIE || ph == ARR_CELLULE ? 99 : ph == ARR_GARDE ? 0.15f : 1.0f)) {
                    n = 1; snprintf(nomf, sizeof nomf, "%s-%s.bmp", tok + 6, ph == ARR_GARDE ? "cellule" : PH[ph % 10]); ecrire_bmp(nomf);
                }
                if (!ph && E.clock - t0 >= 1.0f) { snprintf(nomf, sizeof nomf, "%s-libre.bmp", tok + 6); ecrire_bmp(nomf); police_etat("  [libre]"); break; }
            }
            continue;
        }
        else if (!strncmp(tok, "bmp:", 4)) { if (!ecrire_bmp(tok + 4)) printf("image : %s\n", tok + 4); continue; }
        else if (!strncmp(tok, "repl:", 5)) {   /* repl:N[:T] : répliques de N PNJ proches, T tours (repl_essai) */
            int a1 = 30, a2 = 1;
            sscanf(tok + 5, "%d:%d", &a1, &a2);
            repl_essai(a1, a2 > 0 ? a2 : 1);
            continue;
        }
        else if (sscanf(tok, "replphase:%d", &n) == 1) {   /* replphase:p : drapeaux des phases levés jusqu'à p (0 : prologue) */
            for (int k = 0, f; (f = repl_phase_drapeau(k)) >= 0; k++)
                if (f < SC_MAX_FLAGS) { if (k < n) E.flags[f >> 3] |= (uint8_t)(1 << (f & 7)); else E.flags[f >> 3] &= (uint8_t)~(1 << (f & 7)); }
            printf("répliques : phase %d\n", repl_phase());
            continue;
        }
        else if (!strcmp(tok, "repllots")) {   /* repllots : lots de REPZ, nombre de répliques, lot de la zone de Sera */
            int lots, lot; uint32_t tampon, lus, lectures;
            repl_etat(&lots, &lot, &tampon, &lus, &lectures);
            for (int k = 0; k < lots; k++) printf("  lot %2d %-22s %3d répliques%s\n", k, repl_lot_nom(k), repl_lot_lignes(k), k == repl_zone() ? "  <- zone de Sera" : "");
            printf("répliques : %d lots, tampon %u octets, phase %d, %u octets lus en %u lectures\n", lots, (unsigned)tampon, repl_phase(), (unsigned)lus, (unsigned)lectures);
            continue;
        }
        else if (!strcmp(tok, "noms")) {
            /* noms : ce qui s'affiche avec l'héroïne choisie (nom_echange de hud.c) : phrases d'essai, noms des membres, la
               réplique en cours */
            static const char *const ESSAI[] = { "SERA", "Sera tient bon.", "Il sera là demain.", "Serait-ce Sera ?", "Sera-t-elle prête ? Sera, oui.",
                "« Sera », dit Jade.", "JADE et SERA", "Seras-tu là, Jade ?", "Jadeite, Jade’s, l’Azur, d'Ambre", "SERA\xC2\xA0: Jade\xE2\x80\xAF!",
                "Le bras de Sera, la forge d'Ambre.", "Qu'Ambre parte lorsque Sera vient, jusqu’Ambre.", "LA LAME DE SERA, D'AMBRE", "Aide de Sera ou de  Sera" };
            char bt[200], q[48];
            printf("noms (héroïne %s) :\n", E.heroine >= 0 ? E.w.persos[E.heroine].nom : "aucune");
            for (size_t i = 0; i < sizeof ESSAI / sizeof ESSAI[0]; i++) printf("  « %s » -> « %s »\n", ESSAI[i], hud_texte_vu(ESSAI[i], bt, sizeof bt));
            for (int m = 0; m < NMEMB; m++) printf("  membre %d : %s -> %s\n", m, PARTY[m].name, hud_texte_vu(PARTY[m].name, bt, sizeof bt));
            const char *r = hud_replique_vue(q, sizeof q);
            if (r) printf("  réplique : %s : %s\n", q, r);
            continue;
        }
        else if (!strncmp(tok, "dire:", 5)) {
            /* dire:QUI:TEXTE : réplique de QUI ouverte comme celle d'un script (hud_say ; le joueur à gauche, les autres à
               droite avec leur portrait) ; TEXTE sans virgule */
            static char qui[24], txt[200];
            const char *d = strchr(tok + 5, ':');
            if (!d) continue;
            snprintf(qui, sizeof qui, "%.*s", (int)(d - tok - 5), tok + 5); snprintf(txt, sizeof txt, "%s", d + 1);
            hud_say(qui, txt, strcmp(qui, E.w.sc_player) ? qui : "");
            continue;
        }
        else if (!strcmp(tok, "apparence")) {
            /* apparence : la recrue de l'héroïne choisie montrée en Sera (perso_vue, member_sheet, eveil_aura) */
            const int rm = heroine_recrue();
            printf("planche du joueur %d, a lui %d, remplacement %d (verrou %d)\n", E.w.psheet, E.w.psheet0, E.w.psage, verrou18());
            printf("apparence : héroïne %s, recrue %d (%s), planche de Sera %d\n", E.heroine >= 0 ? E.w.persos[E.heroine].nom : "aucune", rm, rm > 0 ? PARTY[rm].name : "-", E.w.psheet);
            for (int m = 0; m < NMEMB; m++) printf("  membre %d %s : planche %d, en Sera %d, aura %06X\n", m, PARTY[m].name, member_sheet(m), membre_en_sera(m), (unsigned)eveil_aura(m));
            for (int p = 0; p < E.w.npersos; p++) if (rm > 0 && E.w.persos[p].membre == rm) {
                const perso_vue_t v = perso_vue(p, 0);
                int ex = 0; for (int k = 0; k < E.nnpc; k++) ex += E.npcs[k].perso == p;
                printf("  personnage %d %s : présent %d, exemplaires %d, planche %d (la sienne %d), couleurs %d, ombre %d, taille %.2f\n",
                       p, E.w.persos[p].nom, perso_present(p), ex, v.sheet, E.w.persos[p].sheet, v.remap, v.ombre, v.taille);
            }
            continue;
        }
        else if (!strncmp(tok, "scenes", 6)) {
            /* scenes[:k:i] : illustration de chaque scène (scene_image : la variante de l'héroïne choisie, sinon la sienne) ;
               k:i : variante d'essai (la scène k prend l'image i pour l'héroïne choisie), comme une fin de SCNI */
            static uint8_t var[6];
            int k2, i2;
            if (sscanf(tok, "scenes:%d:%d", &k2, &i2) == 2 && E.heroine >= 0) {
                var[0] = (uint8_t)k2; var[1] = (uint8_t)(k2 >> 8); var[2] = (uint8_t)E.heroine; var[3] = 0; var[4] = (uint8_t)i2; var[5] = (uint8_t)(i2 >> 8);
                E.w.scene_var = var; E.w.nscene_var = 1;
            }
            printf("scènes : %d, variantes %d ;", E.w.nscenes, E.w.nscene_var);
            for (int k = 0; k < E.w.nscenes; k++) if (scene_image(k) != E.w.scene_img[k] || k < 4) printf(" %d:%d->%d", k, E.w.scene_img[k], scene_image(k));
            printf("\n");
            continue;
        }
        else { fprintf(stderr, "étape inconnue : %s\n", tok); return 1; }
        frame(&in);
        frame(&none);
    }
    clock_t t0 = clock();
    double us3 = 0, usu = 0;   /* moyenne des bandes sur les 10 images (une seule varie du simple au double) */
    for (int k = 0; k < 10; k++) { frame(&none); us3 += eng_stats()->us_3d; usu += eng_stats()->us_ui; }
    double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 10;
    const eng_stats_t *st = eng_stats();
    printf("  bandes : 3D %.2f ms, interface %.2f ms (moyennes de 10 images)\n", us3 / 10000.0, usu / 10000.0);
    printf("%.2f ms par image sur PC (rendu %d x %d) ; primitives %d, sommets %d, triangles %d, pixels 3D %d, interface %d opérations, %d points\n",
           ms, ENG_W, ENG_H_PX, st->prims, st->verts, st->tris, st->px, st->ui_ops, st->ui_pts);
    if (getenv("ECHOES_PLANCHES")) printf("planches : places servies au plus fort (4, 8, 16, 32, 64, 128 Ko) %d/%d %d/%d %d/%d %d/%d %d/%d %d/%d\n",
        pl_pic[0], PL_COUNT[0], pl_pic[1], PL_COUNT[1], pl_pic[2], PL_COUNT[2], pl_pic[3], PL_COUNT[3], pl_pic[4], PL_COUNT[4], pl_pic[5], PL_COUNT[5]);
    printf("  triangles : sol %d, eau %d, plantes %d, routes %d, bâtiments %d ; dans le brouillard %d, hors du cercle %d\n",
           st->cat_tris[0], st->cat_tris[1], st->cat_tris[2], st->cat_tris[3], st->cat_tris[4], st->tris_fogged, st->tris_outside);

    /* Scénario : drapeaux levés, équipe (bit i : PARTY[i]), cœurs du sac, objectif, script en cours */
    printf("scénario : %d événements, %d chaînes, %u octets ; ", E.w.sc_nev, E.w.sc_nstr, (unsigned)E.w.sc_size);
    float ox = 0, oz = 0;
    const int ob = objective_pos(&ox, &oz);
    printf("drapeaux %#llx, équipe %#x, cœurs %d + %d, objectif %s %.1f %.1f, script %s\n", (unsigned long long)E.flags[0] | (unsigned long long)E.flags[1] << 8, E.team,
           item_loot() >= 0 ? ITEMS[item_loot()].count : 0, n_items > 5 ? ITEMS[5].count : 0, ob ? "en" : "aucun", ox, oz, script_running() ? "en cours" : "aucun");
    printf("  musique : %s\n", music_now());
    printf("  PV et PM (gardés d'un combat à l'autre) :");
    for (int i = 0; i < NMEMB; i++) if (in_band(i) || in_team(i)) printf(" %s %d/%d PV %d/%d PM%s", PARTY[i].name, PARTY[i].hp, PARTY[i].max, PARTY[i].mp, PARTY[i].maxmp, in_team(i) ? "" : " (réserve)");
    printf("\n");
    printf("  niveau %d, exp %d / %d, crédits %d ; sac :", E.level, E.exp, exp_next(E.level), E.credits);
    for (int i = 0; i < n_items; i++) if (ITEMS[i].count) printf(" %s × %d", ITEMS[i].name, ITEMS[i].count);
    printf("\n");
    for (int q = 0; q < script_quest_count(); q++)
        printf("  quête %d %s%s : état %d / %d étapes%s%s\n", q, script_quest_name(q), script_quest_type(q) ? " (secondaire)" : "", E.quests[q],
               script_quest_steps(q), q == E.qtrack ? ", suivie : " : "", q == E.qtrack ? script_quest_text(q, E.quests[q]) : "");
    if (BT.on) {
        static const char *PH[] = { "intro", "commande", "techniques", "objets", "cible", "ennemi", "action", "chute", "victoire", "défaite", "magie", "cut-in", "fuite", "cible de l objet" };
        char gn[72];
        bt_groupe_nom(gn, sizeof gn);
        printf("combat : %s, phase %s, tour de %d, ennemi visé %d ; équipe", gn, PH[BT.phase], BT.actor, BT.cible);
        for (int m = 0; m < NMEMB; m++) if (in_team(m)) printf(" %s %d PV (place %d%s)", PARTY[m].name, PARTY[m].hp, team_place(m), E.scr_party[m].vis ? ", vu" : "");
        printf(" ; récompense %d EXP, %d CR, butin %d × %d, équipement %d %d\n", BT.rw_xp, BT.rw_cr, BT.rw_loot, BT.rw_nloot, BT.rw_eq, BT.rw_eq2);
        for (int k = 0; k < BT.nen; k++) printf("  ennemi %c : %s %d/%d PV, ATQ %d, VIT %d, écran %s %.0f %.0f\n", '1' + k, BT.en[k].name, BT.en[k].hp, BT.en[k].max, BT.en[k].atk, BT.en[k].spd,
                                               E.scr_enemy[k].vis ? "vu" : "caché", E.scr_enemy[k].cx, E.scr_enemy[k].cy);
        for (int m = 0; m < NMEMB; m++) {   /* sorts de chaque membre au combat (job, équipement) */
            if (!in_team(m)) continue;
            int l[MAX_SORTS];
            const int ns = sorts_of(m, l);
            printf("  sorts de %s (MAG %d, ATQ %d, %d) :", PARTY[m].name, PARTY[m].mag, PARTY[m].atk, ns);
            for (int k = 0; k < ns; k++) printf(" %s (%d PM)", E.w.sorts[l[k]].nom, E.w.sorts[l[k]].pm);
            printf("\n");
        }
        printf("  échelles : ech %.2f, sprites %.2f, Sera %.2f, ennemi %.2f\n", E.w.ech, E.w.sprite_scale, member_sheet(0) >= 0 ? E.w.sheets[member_sheet(0)].taille : 0,
               BT.en[0].perso >= 0 ? E.w.persos[BT.en[0].perso].taille : 0);
    } else {
        printf("monde : Sera en %.2f %.2f\n", E.sera.x, E.sera.z);
        /* Tenues : celle de chaque membre (mise d'office ou portée), nage */
        printf("  jobs : %d, types d'équipement %d ;", E.w.njobs, E.w.neqtypes);
        for (int k = 0; k < E.w.neqtypes; k++) if (E.w.eqtypes[k].slot == EQ_ARME) printf(" %s (effet %d, %06X)", E.w.eqtypes[k].nom, E.w.eqtypes[k].effet, (unsigned)E.w.eqtypes[k].couleur);
        printf(" ;");
        for (int m = 0; m < NMEMB; m++) {
            printf(" %s%s %s [", PARTY[m].name, in_team(m) ? "" : in_band(m) ? " (réserve)" : " (hors bande)", E.job[m] < E.w.njobs ? E.w.jobs[E.job[m]].nom : "-");
            for (int k = 0; k < 3; k++) printf("%s%s", k ? ", " : "", E.eq[m][k] < n_items ? ITEMS[E.eq[m][k]].name : "-");
            printf("] PV %d ATQ %d DÉF %d VIT %d ;", PARTY[m].max, PARTY[m].atk, PARTY[m].def, PARTY[m].spd);
        }
        printf("\n");
        printf("  tenues : %d ; nage %d ;", E.w.ntenues, E.nage);
        for (int m = 0; m < NMEMB; m++) { const int t = tenue_of(m, 0); printf(" %s %s (planche %d)", PARTY[m].name, t >= 0 ? E.w.tenues[t].nom : "la sienne", member_sheet(m)); }
        printf("\n");
        for (int k = 0; k < TRAFFIC_MAX; k++)
            if (E.traffic[k].on) {
                const traffic_t *t = &E.traffic[k];
                printf("  trafic %d : %s en %.1f %.1f, %s", k, E.w.persos[t->perso].nom, t->x, t->z, t->walking ? "roule" : "arrêté");
                if (t->lane) {   /* côté de l'axe : > 0 à droite (droite de la marche : (hz, -hx)) */
                    const route_t *r = &E.w.routes[t->road];
                    const int a = t->sgn > 0 ? t->seg : t->seg + 1, b = t->sgn > 0 ? t->seg + 1 : t->seg;
                    float ax, az, bx, bz;
                    memcpy(&ax, r->p + a * 8, 4); memcpy(&az, r->p + a * 8 + 4, 4); memcpy(&bx, r->p + b * 8, 4); memcpy(&bz, r->p + b * 8 + 4, 4);
                    const float l = hypotf(bx - ax, bz - az);
                    printf(", axe %d, côté %.2f (demi-largeur %.2f), y %.2f", t->road, ((t->x - ax) * (bz - az) - (t->z - az) * (bx - ax)) / l, r->hw, ground_y(t->x, t->z));
                }
                printf("\n");
            }
    }
    if (ecrire_bmp(argv[2])) return 1;
    if (wav_file) {   /* en-tête WAV : 24 kHz, mono, 16 bits */
        fseek(wav_file, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, wav_file); put32(wav_file, 36 + wav_n * 2); fwrite("WAVEfmt ", 1, 8, wav_file);
        put32(wav_file, 16); put16(wav_file, 1); put16(wav_file, 1); put32(wav_file, 24000); put32(wav_file, 48000);
        put16(wav_file, 2); put16(wav_file, 16); fwrite("data", 1, 4, wav_file); put32(wav_file, wav_n * 2);
        fclose(wav_file);
        printf("son : %.1f s -> %s\n", wav_n / 24000.0, getenv("ECHOES_WAV"));
    }
    if (E.w.ncartes > 1) printf("carte %d (%s), Sera en %.1f %.1f\n", E.w.carte, E.w.cartes[E.w.carte].nom, E.sera.x, E.sera.z);
    if (sd_file) {
        int attente = 0;   /* blocs lointains dont la texture réduite n'est pas encore fabriquée */
        for (int s = 0; s < CWIN * CWIN; s++)
            if (E.w.slot[s] && E.w.slot[s]->lodbuf >= 0)
                for (int b = 0; b < (CS / 2) * (CS / 2); b++) attente += E.w.slot[s]->lod[b] == LOD_PENDING;
        printf("carte SD : %.2f Mo lus ; dernière image : %d polygones texturés du sol, %d sans leur texture ; %d blocs lointains en attente\n",
               sd_lus / 1048576.0, eng_stats()->tex_need, eng_stats()->tex_miss, attente);
    }
    return 0;
}
