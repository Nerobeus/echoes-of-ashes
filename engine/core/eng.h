/*
 * Echoes of Ashes - moteur RPG façon PS1 pour ESP32-S3 (API publique du cœur)
 *
 * Le cœur est en C99 sans dépendance de plateforme : il lit un paquet monde (monde.pak, exporté par
 * l'émulateur, voir pak.h), fait avancer le jeu et dessine l'écran rond 466 × 466 par bandes en RGB565.
 * La plateforme (ESP32, navigateur, PC) fournit la mémoire, l'entrée, la sauvegarde et l'envoi des bandes.
 *
 * Image 3D interne ENG_W × ENG_H_PX : 233 × 233 doublée à l'envoi (par défaut), ou 466 × 466 native
 * (compiler avec ENG_W=466 ENG_H_PX=466). L'interface est toujours dessinée en 466 × 466.
 */
#ifndef ENG_H
#define ENG_H

#include <stdint.h>
#include <stddef.h>

#define ENG_SCREEN 466                 /* côté de l'écran natif (en plein écran : du cadre de l'interface ronde, centré) */
/* Plein écran (ENG_DYN=1, avec ENG_ROUND=0 : app Android sur téléphone) : image rectangulaire de la taille donnée à
   l'exécution par eng_set_screen, avant eng_init, au plus ENG_W × ENG_H_PX ; 3D rendue à cette taille (pas de pixels
   doublés : la plateforme agrandit l'image entière) ; dessin en trois temps, eng_render_band_3d, flou de profondeur
   par la plateforme (ENG_DOF : profondeur par pixel), puis eng_render_band_ui */
#ifndef ENG_DYN
#define ENG_DYN 0
#endif
#if ENG_DYN
#ifndef ENG_W
#define ENG_W 960
#endif
#ifndef ENG_H_PX
#define ENG_H_PX 1600
#endif
#else
#ifndef ENG_W
#define ENG_W 233
#endif
#ifndef ENG_H_PX
#define ENG_H_PX 233
#endif
#endif
#ifndef ENG_ROUND
#define ENG_ROUND 1                    /* écran rond : seuls les pixels du cercle sont dessinés */
#endif
#if ENG_DYN
#define ENG_SCALE 1
#else
#define ENG_SCALE (ENG_SCREEN / ENG_W) /* 2 : pixels doublés, 1 : rendu natif */
#endif
#ifndef ENG_WORKERS
#define ENG_WORKERS 2                  /* fils qui dessinent les bandes en même temps (les deux cœurs de l'ESP32) */
#endif
#ifndef ENG_DOF
#define ENG_DOF 0                      /* profondeur par pixel, pour le flou de profondeur (plein écran) */
#endif
/* Édition choisie à la compilation : 0 18+, 1 enfant (compiler avec ECHOES_ENFANT=1 : seuls les paquets de l'édition
   enfant se lancent, section EDIT de pak.h ; la plateforme range ses fichiers à part) */
#ifndef ECHOES_ENFANT
#define ECHOES_ENFANT 0
#endif
#define ENG_NATIVE_BAND 34             /* lignes natives par bande : 466 × 34 × 2 = 31 688 octets, sous la limite de 32 Ko par transaction SPI */
#define ENG_BAND (ENG_NATIVE_BAND / ENG_SCALE)

/* Entrée d'une image */
enum {
    ENG_KEY_UP = 1, ENG_KEY_DOWN = 2, ENG_KEY_LEFT = 4, ENG_KEY_RIGHT = 8,
    ENG_KEY_ROT_L = 16, ENG_KEY_ROT_R = 32,
    ENG_KEY_OK = 64,                   /* parler, valider (Entrée) */
    ENG_KEY_BACK = 128,                /* retour, menu (Échap) */
    ENG_KEY_MENU = 256,                /* bouton BOOT ou appui court sur PWR */
    ENG_KEY_ALT_UP = 512, ENG_KEY_ALT_DOWN = 1024   /* vaisseau : monter, descendre */
};
/* Effets de la manette (eng_config_t.effet_manette) : lumière et vibration selon ce qui se passe */
enum { ENG_EFFET_VALIDER, ENG_EFFET_COUP, ENG_EFFET_KO, ENG_EFFET_CRASH, ENG_EFFET_COMBAT, ENG_EFFET_VICTOIRE,
       ENG_EFFET_SOIN, ENG_EFFET_NIVEAU, ENG_EFFET_DEFAITE, ENG_EFFET_FRAPPE, ENG_EFFET_N };
typedef struct {
    uint32_t keys;
    int touch;                         /* 1 si un doigt est posé */
    float touch_x, touch_y;            /* position en pixels de l'écran natif (0..466) */
    int touch2;                        /* 1 si un deuxième doigt est posé (pinch : zoom) */
    float touch2_x, touch2_y;
    int manette;                       /* manette Bluetooth connectée (montre) : conduite au stick, rails de conduite cachés */
    float stick_x, stick_y;            /* son stick, -1 à 1 (droite et bas positifs) */
} eng_input_t;

/* Application de la plateforme (ESP32 : PC DOS, Transfert), listée par l'entrée APPLICATIONS de l'écran titre */
typedef struct {
    const char *nom, *aide;
    void (*lancer)(void);
} eng_app_t;

/* Réglage de la plateforme (ESP32 : volume, luminosité, veille), page RÉGLAGES du menu des applications : texte de la
   valeur, et changement d'un cran (sens -1 ou +1 ; la plateforme l'applique et le garde) */
typedef struct {
    const char *nom;
    const char *(*valeur)(void);
    void (*changer)(int sens);
} eng_reglage_t;

typedef struct {
    int swap565;                       /* 1 : octets RGB565 inversés (envoi SPI, poids fort d'abord) */
    void *(*alloc)(size_t size, int fast);   /* fast = 1 : mémoire interne rapide (SRAM) si possible */
    /* Sauvegarde : 3 parties de deux emplacements alternés (partie × 2 + séquence & 1 : 0 à 5). Renvoient le nombre
       d'octets, ou -1. Facultatifs. */
    int (*save_write)(int slot, const void *data, int len);
    int (*save_read)(int slot, void *data, int cap);
    void (*set_brightness)(int level);  /* 0..255, facultatif */
    uint32_t (*clock_us)(void);         /* horloge en microsecondes, facultative : mesure 3D et interface */
    /* Lit len octets du paquet à partir du décalage off (chunks du monde). Facultatif : sans elle, les chunks sont lus
       dans le paquet passé à eng_init, qui doit alors être entier. Renvoie 0 si tout va bien. Avec un paquet léger
       (sections SPRX et TEXS, tools/paquet_sd.js) passé à eng_init, elle lit monde.pak entier (carte SD) : chunks,
       pixels des planches et textures du sol, à la demande. */
    int (*pak_read)(uint32_t off, void *dst, uint32_t len);
    /* Scénario hors du paquet (carte SD : histoire.scn, même contenu que la section SCEN) : taille en octets et lecture
       de len octets à partir de off (0 si tout va bien). Facultatif : sans lui, le scénario est la section SCEN. */
    uint32_t scen_size;
    int (*scen_read)(uint32_t off, void *dst, uint32_t len);
    /* Intérieurs hors du paquet (carte SD : interieurs.bin, même contenu que la section INTR), comme le scénario.
       Facultatif : sans lui, la section INTR du paquet, qui doit alors être entier en mémoire. */
    uint32_t intr_size;
    int (*intr_read)(uint32_t off, void *dst, uint32_t len);
    /* Taille du fichier que lit pak_read (facultative) : comparée à celle que le paquet léger attend (SDPK) */
    uint32_t pak_size;
    /* Entrée RÉGLAGES de l'écran titre (facultative ; core/accueil.c) : réglages propres à la plateforme (app Android :
       serveur de l'Atelier, édition, régénération du monde). Sans elle, pas d'entrée (ESP32, page web, émulateur). */
    void (*reglages)(void);
    /* Nom et ligne d'aide de cette entrée (facultatifs : RÉGLAGES ; ESP32 : TRANSFERT, mode Wi-Fi hors jeu) */
    const char *reglages_nom, *reglages_aide;
    /* Entrée APPLICATIONS de l'écran titre (facultative) : page de napps applications de la plateforme */
    const eng_app_t *apps;
    int napps;
    /* Réglages de la montre (facultatifs) : rangée RÉGLAGES du menu des applications */
    const eng_reglage_t *regls;
    int nregls;
    /* Réglages de la plateforme montrés aussi en jeu, en bas du menu SYSTÈME après MONTRE (ESP32 : MANETTE) ; un
       toucher change d'un cran (+1) */
    const eng_reglage_t *regls_jeu;
    int nregls_jeu;
    /* Effet de la manette (facultatif ; ESP32 : Poké Ball Plus, manette_effet), un ENG_EFFET_* : validation (son
       SON_VALIDER), coup reçu et membre K.O. en combat, crash du véhicule (SON_CRASH), début du combat, victoire, soin
       (SON_SOIN), niveau gagné, défaite, coup porté à l'ennemi ; sans manette connectée, la plateforme ne fait rien */
    void (*effet_manette)(int effet);
    /* Retour à l'accueil de la montre (facultatif ; ESP32 : l'app « montre ») : ligne MONTRE du menu SYSTÈME, après la
       sauvegarde de la partie ; sans lui, la ligne ACCUEIL ramène au menu des applications de l'écran titre */
    void (*montre)(void);
    /* Reprendre la dernière partie sauvegardée sans l'écran titre (ESP32 : après une mise en veille passée par l'app
       montre) */
    int reprendre;
} eng_config_t;

typedef struct {
    int tris, px, sprites;             /* triangles, pixels 3D écrits, sprites de l'image */
    int segments;                      /* corrections de perspective (une division tous les 16 pixels) */
    int prims, verts;                  /* primitives dans la table d'ordre, sommets (MAX_PRIMS, MAX_VERTS de render.c) */
    int ui_ops, ui_pts;                /* opérations et points de l'interface (MAX_OPS, MAX_PTS de ui.c) */
    uint32_t us_3d, us_ui;             /* temps passé dans les bandes : 3D, interface (si clock_us est fourni) */
    int cat_tris[5];                   /* triangles par catégorie : sol, eau, plantes, routes, bâtiments et décor */
    int tris_fogged, tris_outside;     /* triangles écartés : entièrement dans le brouillard, hors du cercle */
    int chunk_loads;                   /* chunks chargés depuis l'image précédente */
    int tex_need, tex_miss;            /* carte SD : textures du sol de l'image (polygones), dont pas encore lues */
    uint32_t us_stream;                /* temps passé à les charger (si clock_us est fourni) */
} eng_stats_t;

/* Charge le paquet : tout ce qui précède les chunks (section CDAT) doit rester en mémoire ; les chunks sont lus par
   pak_read, ou dans ce même paquet s'il est entier.
   Renvoie 0 si tout va bien, sinon un code d'erreur négatif. */
int eng_init(const uint8_t *pak, size_t len, const eng_config_t *cfg);
const char *eng_error(void);

/* Fait avancer le jeu de dt secondes */
void eng_update(float dt, const eng_input_t *in);

/* Prépare l'image : caméra, table d'ordre, liste de l'interface, dans le tampon de préparation. */
void eng_build_frame(float time_s);

/* L'image préparée devient celle que dessine eng_render_native_band ; la suivante se prépare dans l'autre
   tampon. Sur deux cœurs : le cœur 0 enchaîne eng_update et eng_build_frame pendant que le cœur 1 dessine
   les bandes de l'image précédente ; eng_present ne doit être appelé que quand toutes ses bandes sont dessinées. */
void eng_present(void);

/* Dessine les lignes natives [y0, y0 + h) (h ≤ ENG_NATIVE_BAND, y0 et h pairs en mode doublé), colonnes
   [x0, x1) (paires), dans dst : (x1 - x0) × h pixels RGB565 contigus, 3D (doublée ou native) puis interface.
   Pour l'écran rond, la fenêtre peut se limiter à la plus grande corde du cercle dans ces lignes. */
void eng_render_native_band(int y0, int h, uint16_t *dst, int x0, int x1);
/* Même chose, dessinée par le cœur worker (0 ou 1) : les deux cœurs peuvent dessiner en même temps deux bandes
   différentes de l'image présentée (chacun a son propre contexte de bande). */
void eng_render_band_on(int worker, int y0, int h, uint16_t *dst, int x0, int x1);

#if ENG_DYN
/* Plein écran : taille de l'image (paire, bornée à ENG_W × ENG_H_PX), avant eng_init ; l'interface ronde de 466 × 466
   est centrée, les touchers restent donnés en pixels de l'image */
void eng_set_screen(int w, int h);
void eng_screen_size(int *w, int *h);
/* Marges des barres du système, en pixels de l'image (après eng_set_screen) : l'interface se place entre elles */
void eng_set_safe(int haut, int bas);
/* 3D des lignes [y0, y0 + h) sur toute la largeur (h ≤ ENG_NATIVE_BAND), dans dst, et la profondeur de chaque pixel
   dans depth (facultative ; 255 : ciel, voir eng_focus_depth), par le fil worker (0 à ENG_WORKERS - 1) ; puis
   l'interface des mêmes lignes par-dessus l'image (floutée entre-temps) */
void eng_render_band_3d(int worker, int y0, int h, uint16_t *dst, uint8_t *depth);
void eng_render_band_ui(int worker, int y0, int h, uint16_t *dst);
/* Profondeur de Sera dans l'échelle de depth : la distance nette du flou de profondeur */
int eng_focus_depth(void);
#endif

/* Statistiques de l'image présentée (préparation et bandes) */
const eng_stats_t *eng_stats(void);

/* Banc d'essai : place Sera en (x, z) et la caméra au lacet yaw (chunks autour chargés tout de suite) */
void eng_teleport(float x, float z, float yaw);
/* Banc d'essai : passe l'ouverture (écran titre, introduction, choix de l'héroïne), partie nouvelle, Sera au départ */
void eng_skip_opening(void);
/* Banc d'essai : déplace Sera sans charger les chunks (ils arrivent aux images suivantes, comme en marchant) */
void eng_move_to(float x, float z);
/* Course d'essai (mesure des images par seconde : SDX COURSE de la montre) : ouverture passée, script arrêté, course sur la
   piste k avec le premier véhicule de course et des rivaux d'autres planches, le joueur piloté vers l'axe de la piste ;
   allege = 0 : sans l'allègement du rendu en course (sol simplifié plus tôt, render.c), pour comparer. 0 : refusée */
int eng_essai_course(int piste, int allege);
/* Profilage du rendu des bandes (compilé avec ENG_PROFILE, sinon tout à zéro) : cycles des deux cœurs additionnés
   depuis le dernier appel, par poste ; out reçoit ENG_PROF_N valeurs (ou NULL : remise à zéro seulement) */
enum { PROF_CLEAR, PROF_SETUP, PROF_ROWS, PROF_PIX, PROF_SPRITE, PROF_SHADOW, PROF_SKY, PROF_TRIS, PROF_NROWS, PROF_NRUNS, PROF_N };
#define ENG_PROF_N PROF_N
void eng_profile(uint32_t *out);
/* Profilage de la préparation de l'image (eng_build_frame, cœur 0), mêmes règles : caméra et personnages proches, blocs
   lointains, cases, chunks (garde-corps, bâtiments, blocs), fondus, pluie et personnages, aplatissement et répartition
   par bande, ciel, interface */
enum { BPROF_DEBUT, BPROF_BLOCS, BPROF_CASES, BPROF_CHUNKS, BPROF_PERSOS, BPROF_TRI, BPROF_CIEL, BPROF_UI, BPROF_N };
void eng_profile_build(uint32_t *out);
int eng_profile_mem(void);
/* Côté du monde en cases */
int eng_world_size(void);
/* Zoom de la caméra (1 : distance d'origine), borné du zoom mini du paquet à 5 (ZOOM_MAX) */
/* Sons (core/audio.c) : la plateforme tire n échantillons mono 16 bits à sa fréquence rate, depuis sa propre tâche ;
   volume général de 0 à 1 (menu Système) */
void eng_audio_mix(int16_t *out, int n, int rate);
void eng_set_volume(float v);
/* Heure réelle (montre : horloge PCF85063) en secondes depuis minuit, heure locale ; -1 : inconnue (par défaut). Donnée
   par la plateforme chaque seconde : cycle jour / nuit « RÉEL » du menu Système, heure sur l'arc du bas */
void eng_set_horloge(int secondes);
/* Sauvegarde de la partie en cours dans son emplacement (veille, batterie vide) : 1 si elle est écrite, 0 si ce n'est
   pas le moment (écran titre, combat, course, scène, dialogue) ou si l'écriture échoue */
int eng_sauvegarder(void);
/* 1 sur l'écran titre (pas de partie en cours) */
int eng_ecran_titre(void);
/* Montre : date (jour 1-31, mois 1-12, jour de la semaine 0 dimanche - 6) et batterie (%, -1 inconnue ; en charge), pour
   le cadran du mode verrouillé */
void eng_set_date(int jour, int mois, int jsem);
void eng_set_batterie(int pct, int charge);
/* Mode verrouillé (réveil de la veille) : cadran de la montre dans le style du jeu, monde arrêté ; un glissement sur
   l'écran le déverrouille */
void eng_verrouiller(void);
int eng_verrouille(void);
/* Musiques (musique.bin de tools/audio/preparer_musique.py, sur la carte SD : /sd/echoes/musique.bin) : taille et
   lecture de len octets à partir de off (0 si tout va bien), lues à mesure par le jeu. Après eng_init ; sans elle, pas
   de musique. */
void eng_music_attach(uint32_t size, int (*read)(uint32_t off, void *dst, uint32_t len));
float eng_volume(void);
void eng_set_zoom(float z);
float eng_zoom(void);
/* 1 si les textures du sol ont été copiées en mémoire rapide, 0 si elles sont lues dans le paquet */
int eng_textures_in_ram(void);
/* Point de départ de Sera dans le monde */
void eng_start_position(float *x, float *z);

#endif
