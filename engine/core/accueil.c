/*
 * Ouverture du jeu (comme celle de l'émulateur) : écran titre (CONTINUER, NOUVELLE PARTIE, CHARGER, et RÉGLAGES si la
 * plateforme en a : eng_config_t.reglages, app Android), choix d'une des
 * PARTIES sauvegardées, introduction du monde en cartons (section OUVR, onglet Quêtes de l'Atelier, entrée
 * « Introduction »), puis le choix de l'héroïne (« Qui es-tu ? » : Sera et les personnages cochés « Héroïne au choix »).
 *
 * Pendant l'ouverture, le monde est arrêté (eng_update s'arrête après l'interface) et Sera est cachée : la 3D montre le
 * lieu du fond ou du carton, la caméra tourne lentement autour de lui, à son heure (lumière, néons de Halo). Un carton
 * sur le noir : des cendres qui tombent, une lueur de braise. Gestes : toucher (choisir, carton suivant), glisser à
 * gauche ou à droite (héroïne voisine), maintenir (passer l'introduction) ; touches : haut / bas / gauche / droite, OK,
 * retour.
 */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "internal.h"
#include "ui.h"
#include "audio.h"

#define CX 233.0f
#define CY 233.0f
#define WHITE 0xFFFFFF
#define INK 0x05040A
#define DIM 0x9AA6B8

/* Durées (s) : fondu d'entrée de l'écran titre ; entrée, lecture (base, par caractère, plafond) et sortie d'un carton ;
   appui qui passe l'introduction ; fondu au noir vers le jeu */
#define T_TITRE 1.6f
#define T_IN 0.9f
#define T_LIRE0 2.2f
#define T_LIRE_C 0.05f
#define T_LIRE_MAX 9.0f
#define T_OUT 0.8f
#define T_PASSER 0.8f
#define T_FONDU 0.7f
#define ORBITE 0.05f             /* rad/s : la caméra tourne autour du lieu montré */
#define HEURE_AUBE 0.27f         /* écran du choix de l'héroïne : le départ, à l'aube */
#define HER_DIR_S 1.3f           /* l'héroïne montrée tourne d'un huitième de tour toutes les 1,3 s */
#define HER_PIEDS 318.0f         /* ligne de ses pieds à l'écran */
#define HER_H 168.0f             /* hauteur de sa cellule à l'écran (px) */
#define HER_MAX 128              /* cellule la plus grande dessinée */
#define NCENDRES 40

enum { AP_TITRE, AP_PARTIES, AP_INTRO, AP_HEROINE, AP_APPS, AP_REGL };
enum { AC_CONTINUER, AC_NOUVELLE, AC_CHARGER, AC_REGLAGES, AC_APPS, AC_COURSE };
/* Nom d'une entrée de l'écran titre (RÉGLAGES renommée par la plateforme : TRANSFERT de l'ESP32) */
/* Voile sur toute l'image : marges du téléphone comprises (ui_voile_image) ; sur l'écran rond, le canevas */
static void voile_image(uint16_t c, int a) {
#if ENG_DYN || ENG_RECT
    ui_voile_image(c, a);   /* (écran rectangulaire : toute l'image) */
#else
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, c, a);
#endif
}
/* Fondu au noir de l'écran rond (le cercle), ou de toute l'image rectangulaire (ENG_RECT) */
static void fondu_noir(int a) {
#if ENG_RECT
    ui_voile_image(ui_rgb(0), a);
#else
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(0), a);
#endif
}

static const char *nom_option(const char *const *nom, int o) { return o == AC_REGLAGES && E.cfg.reglages_nom ? E.cfg.reglages_nom : nom[o]; }
enum { B_OPT, B_PARTIE, B_RETOUR, B_PREC, B_SUIV, B_CHOISIR, B_APP, B_REG_MOINS, B_REG_PLUS, B_CLIGNE, B_CMOINS, B_CPLUS, B_CVALIDER };   /* CVALIDER : mode course en plein écran, la ligne choisie */

static struct {
    int on, page, sel, nouvelle;        /* nouvelle : page des parties d'une nouvelle partie (sinon : charger) */
    float t, t0;                        /* temps de l'ouverture, début de la page */
    int carte; float ct0;               /* carton en cours, son début */
    float fx, fz, heure;                /* lieu montré (fx < 0 : noir), son heure */
    int her; float ht0;                 /* héroïne montrée (0 : Sera, k : ouv_her[k - 1]), début de sa vue */
    int partie;                         /* partie de la nouvelle partie */
    int fondu; float ft0;               /* fondu au noir, puis la partie commence */
    int changer;                        /* code CODE_HEROINE : choix de l'héroïne en pleine partie (accueil_heroine_changer) */
    int down, geste; float x0, y0, cx, cy, dt0;   /* doigt : départ, position, début ; geste déjà pris */
    float vu;                           /* dernière entrée (doigt, touche) : l'écran est calme une seconde après */
    uint32_t prev;
    float yaw0, zoom0, heure0;          /* caméra et heure du départ, rendues au jeu */
    partie_info_t info[PARTIES]; int ok[PARTIES];
    int opts[6], nopts;                 /* entrées de l'écran titre */
    int premier;                        /* mode course : première ligne visible d'une liste (coupes, épreuves, garage) */
} A;
static struct { float x, y, w, h; int act, arg; } AB[24] ENG_FROID;   /* (plein écran : jusqu'à sept lignes du mode course, deux zones chacune) */
static int nab;
static uint16_t C1, C2;
/* Image de l'héroïne montrée : deux tampons alternés (une image se prépare pendant que l'autre se dessine) */
static uint16_t HPX[2][HER_MAX * HER_MAX] ENG_FROID;
static uint8_t HMK[2][HER_MAX * HER_MAX] ENG_FROID;
static int hbuf;

int accueil_on(void) { return A.on; }
/* Mode course (pages AP_C*, carriere.c) */
int accueil_carriere_page(void) { return A.on && A.page >= AP_CMENU; }
/* Voile du monde derrière la page (0 : aucun, 1 : noir). Fait par l'ambiance du rendu (render_ambiance) au lieu d'un
   rectangle plein écran par-dessus, qui mêlait à chaque image 217 000 pixels (466 × 466) : l'interface de l'écran titre
   prenait 69 ms par image sur la montre (01/11/2026). 1 : pas de 3D du tout, bandes noires (pages presque opaques :
   parties, applications, réglages ; cartons noirs ou illustrés de l'introduction) */
float accueil_voile(void) {
    if (!A.on) return 0;
    switch (A.page) {
    case AP_TITRE: return 90 / 255.0f;
    case AP_HEROINE: return 110 / 255.0f;
    case AP_PARTIES: case AP_APPS: case AP_REGL: return 1;
    case AP_INTRO: return E.w.ouv_cartes[A.carte].x < 0 ? 1 : 0;
    case AP_CRES: case AP_CFIN: return 150 / 255.0f;
    }
    return A.page >= AP_CMENU ? 120 / 255.0f : 0;   /* mode course : le circuit derrière */
}
/* Page de menu immobile depuis une seconde (ni doigt, ni touche, ni fondu, ni carton) : la plateforme peut ralentir
   la cadence (ESP32 : ECRAN_CALME_IPS de main.c), seules la caméra et les cendres y bougent, lentement */
int accueil_calme(void) {
    return A.on && !A.fondu && !A.down && A.t >= T_TITRE && A.t - A.vu >= 1
        && (A.page == AP_TITRE || A.page == AP_PARTIES || A.page == AP_APPS || A.page == AP_REGL || A.page >= AP_CMENU);
}
int eng_ecran_calme(void) { return !AG.on && (accueil_calme() || carte_calme()); }   /* carte du menu immobile aussi (carte.c) */
float accueil_heure(void) { return A.heure; }
const char *accueil_musique(void) { return E.w.ouv_musique; }

int heroine_de(const char *nom) {
    for (int k = 0; nom && nom[0] && k < E.w.ouv_nher; k++) if (!strcmp(E.w.persos[E.w.ouv_her[k]].nom, nom)) return E.w.ouv_her[k];
    for (int p = 0; nom && nom[0] && p < E.w.npersos; p++) if (E.w.persos[p].sheet >= 0 && !strcmp(E.w.persos[p].nom, nom)) return p;
    return -1;
}
/* Héroïnes proposées à « Qui es-tu ? » : celles de OUVR, sauf une héroïne 18+ (OUV8 : AMBRE) tant que le 18+ n'est pas
   déverrouillé, et jamais dans l'édition enfant (accHer de l'émulateur) ; her_n : leur nombre, her_perso(k) : la k-ième
   (1 à her_n ; 0 : Sera). Une sauvegarde où elle a été choisie se charge toujours (heroine_de) */
static int her_vue(int i) { return !E.w.ouv_her18[i] || (E.w.edition != 1 && !verrou18()); }
static int her_n(void) { int n = 0; for (int i = 0; i < E.w.ouv_nher; i++) n += her_vue(i); return n; }
static int her_perso(int k) { for (int i = 0; k > 0 && i < E.w.ouv_nher; i++) if (her_vue(i) && --k == 0) return E.w.ouv_her[i]; return -1; }
static const char *her_nom(int k) { const int p = her_perso(k); return p >= 0 ? E.w.persos[p].nom : PARTY[0].name; }
static int her_rang(int p) { for (int k = 1; p >= 0 && k <= her_n(); k++) if (her_perso(k) == p) return k; return 0; }   /* rang de l'héroïne p (0 : Sera) */
/* Île du jeu des héroïnes (ile.c) : pas de Sera parmi les héroïnes (sa planche est celle d'une héroïne de l'île), la première
   proposée est la 1 */
static int her_k0(void) { return E.w.ile[0] && her_n() > 0; }

/* ---------- Lieu montré ---------- */
/* Sur la carte k (l'ouverture : la carte 0 ; le mode course : la carte d'un circuit) */
static void montrer_k(int k, float x, float z, float heure) {
    A.heure = heure < 0 ? A.heure0 : heure;
    if (x < 0) { A.fx = -1; return; }
    if (E.w.carte != k) { world_carte(k, x, z); A.fx = -1; }
    if (A.fx < 0 || fabsf(x - A.fx) > 0.5f || fabsf(z - A.fz) > 0.5f) {
        A.fx = x; A.fz = z;
        E.sera.x = x; E.sera.z = z; E.sera_y_ok = 0;
        E.stream_hold = 1;   /* chunks chargés à la mise à jour suivante (l'image préparée entre-temps n'a pas de 3D) */
    }
}
static void montrer(float x, float z, float heure) { montrer_k(0, x, z, heure); }   /* lieux de l'ouverture : carte 0 */
static void parties_lire(void) {
    for (int p = 0; p < PARTIES; p++) A.ok[p] = hud_partie_info(p, &A.info[p]);
    A.nopts = 0;
    int any = 0; for (int p = 0; p < PARTIES; p++) any |= A.ok[p];
    if (any) A.opts[A.nopts++] = AC_CONTINUER;
    A.opts[A.nopts++] = AC_NOUVELLE;
    if (any) A.opts[A.nopts++] = AC_CHARGER;
    if (carr_disponible()) A.opts[A.nopts++] = AC_COURSE;   /* mode course : carrière, course libre */
    if (E.cfg.reglages) A.opts[A.nopts++] = AC_REGLAGES;
    if (E.cfg.napps > 0 || E.cfg.nregls > 0) A.opts[A.nopts++] = AC_APPS;
}
static int derniere_partie(void) {
    int b = -1;
    for (int p = 0; p < PARTIES; p++) if (A.ok[p] && (b < 0 || A.info[p].ordre > A.info[b].ordre)) b = p;
    return b;
}
static float carte_duree(int k) {
    int n = 0; for (const char *s = E.w.ouv_cartes[k].texte; *s; s++) n += (*s & 0xC0) != 0x80;
    const float l = T_LIRE0 + n * T_LIRE_C;
    return T_IN + (l < T_LIRE_MAX ? l : T_LIRE_MAX) + T_OUT;
}
static void page(int p) {
    A.page = p; A.t0 = A.t; A.down = 0; A.geste = 1;   /* le doigt posé pendant le changement ne compte plus */
    if (p == AP_TITRE) { A.sel = 0; montrer(E.w.ouv_fond[0], E.w.ouv_fond[1], E.w.ouv_fond[2]); }
    else if (p == AP_REGL) A.sel = 0;
    else if (p == AP_APPS) { A.sel = 0; montrer(E.w.ouv_fond[0], E.w.ouv_fond[1], E.w.ouv_fond[2]); }
    else if (p == AP_PARTIES) A.sel = A.nouvelle ? 0 : (derniere_partie() > 0 ? derniere_partie() : 0);
    else if (p == AP_INTRO) { A.carte = 0; A.ct0 = A.t; montrer(E.w.ouv_cartes[0].x, E.w.ouv_cartes[0].z, E.w.ouv_cartes[0].heure); }
    else if (p == AP_HEROINE) { A.her = her_k0(); A.ht0 = A.t; montrer(E.w.start_x, E.w.start_z, HEURE_AUBE); }
}

static void charger(int p);
static void carr_entrer(int p);
static void changer_fin(int ok);
static void debut(int titre) {
    memset(&A, 0, sizeof A);
    A.on = 1; A.fx = -1;
    /* caméra et heure de toute nouvelle partie : celles d'origine, relevées au démarrage (on peut venir d'une partie quittée) */
    static float yaw0, zoom0, heure0; static int vus;
    if (!vus) { vus = 1; yaw0 = E.yaw; zoom0 = E.zoom; heure0 = E.heure; }
    A.yaw0 = yaw0; A.zoom0 = zoom0; A.heure0 = heure0; A.heure = E.heure;
    eng_set_zoom(E.w.zoom_min);   /* vue large du lieu */
    parties_lire();
    page(!titre && E.cfg.napps > 0 && !E.cfg.montre ? AP_APPS : AP_TITRE);   /* lancé depuis l'app montre : l'écran titre du jeu */
    if (E.cfg.reprendre) { E.cfg.reprendre = 0; if (derniere_partie() >= 0) charger(derniere_partie()); }   /* retour de veille */
}
void accueil_debut(void) { debut(0); }
void accueil_titre(void) { debut(1); }   /* SYSTÈME › QUITTER (05/11/2026) : l'écran titre, sur toutes les plateformes */

/* La partie commence : Sera (ou l'héroïne k) au départ, caméra et heure d'origine */
static void jouer(int k) {
    hud_nouvelle(A.partie);
    E.heroine = her_perso(k);
    ile_equipe(1);   /* île : l'héroïne et ses deux compagnes, au niveau 1 */
    ile_rodeurs_reset();   /* île : ni rôdeur ni nuit commencée */
    ile_offrande_reset();  /* ni séquence d'une succube */
    ile_enleve_reset();    /* ni habitant enlevé */
    if (E.w.carte) world_carte(0, E.w.start_x, E.w.start_z);
    E.sera.x = E.w.start_x; E.sera.z = E.w.start_z; E.sera.dir = 0.35f + PI_F; E.sera_y_ok = 0;
    E.yaw = A.yaw0; E.heure = A.heure0; eng_set_zoom(A.zoom0);
    E.stream_hold = 1;
    A.on = 0;
    BT.fade_t = E.clock;   /* sortie du noir, comme après un combat */
}
void accueil_fin(void) { if (A.on) { A.partie = 0; jouer(her_k0()); } }
/* Code CODE_HEROINE (04/11/2026, demande de l'utilisateur : « changer d'héroïne à tout moment ») : la page « Qui es-tu ? »
   en pleine partie, sur le monde arrêté là où il est (rien de montré ailleurs) ; CHOISIR change l'héroïne (E.heroine) sans
   recommencer la partie (sa sauvegarde suivante la garde), RETOUR garde la même ; caméra, zoom et heure rendus
   (accHerChanger de l'émulateur) */
static void changer_fin(int ok) {
    if (ok) { E.heroine = her_perso(A.her); ile_equipe(0); hud_toast("Héroïne changée"); }   /* (pas son nom : les noms sont échangés à l'écran) */
    E.yaw = A.yaw0; E.heure = A.heure0; eng_set_zoom(A.zoom0);
    A.on = 0; A.changer = 0; A.fondu = 0;
    BT.fade_t = E.clock;   /* sortie du noir, comme après un combat */
}
int accueil_heroine_changer(void) {
    if (A.on || her_n() < 1) return 0;
    memset(&A, 0, sizeof A);
    A.on = 1; A.changer = 1; A.fx = E.sera.x; A.fz = E.sera.z;
    A.yaw0 = E.yaw; A.zoom0 = E.zoom; A.heure0 = A.heure = E.heure;
    A.page = AP_HEROINE; A.geste = 1; A.her = her_rang(E.heroine);
    return 1;
}
void eng_skip_opening(void) { if (!AG.on) accueil_fin(); }
static void charger(int p) {
    E.heure = A.heure0; eng_set_zoom(A.zoom0);
    E.yaw = A.yaw0;
    if (!hud_charger(p)) return;
    E.sera_y_ok = 0; E.stream_hold = 1;
    A.on = 0;
    BT.fade_t = E.clock;
}
/* Nouvelle partie : une partie vide (sinon la page des parties), l'introduction s'il y en a, puis le choix */
static void nouvelle(void) {
    for (int p = 0; p < PARTIES; p++) if (!A.ok[p]) { A.partie = p; page(E.w.ouv_ncartes ? AP_INTRO : AP_HEROINE); return; }
    A.nouvelle = 1; page(AP_PARTIES);
}
static void apres_intro(void) { page(AP_HEROINE); }
static void choisir(void) { son_jouer(SON_VALIDER, 0.5f, 1); A.fondu = 1; A.ft0 = A.t; }

static void valider_titre(int k) {
    son_jouer(SON_VALIDER, 0.45f, 1);
    if (A.opts[k] == AC_CONTINUER) charger(derniere_partie());
    else if (A.opts[k] == AC_NOUVELLE) nouvelle();
    else if (A.opts[k] == AC_REGLAGES) E.cfg.reglages();
    else if (A.opts[k] == AC_APPS) page(AP_APPS);
    else if (A.opts[k] == AC_COURSE) carr_entrer(AP_CMENU);
    else { A.nouvelle = 0; page(AP_PARTIES); }
}
static void valider_partie(int p) {
    son_jouer(SON_VALIDER, 0.45f, 1);
    if (!A.nouvelle) { if (A.ok[p]) charger(p); return; }
    A.partie = p; page(E.w.ouv_ncartes ? AP_INTRO : AP_HEROINE);
}
static void carte_suivante(void) {
    if (++A.carte >= E.w.ouv_ncartes) { apres_intro(); return; }
    A.ct0 = A.t;
    montrer(E.w.ouv_cartes[A.carte].x, E.w.ouv_cartes[A.carte].z, E.w.ouv_cartes[A.carte].heure);
}
static void her_pas(int d) { const int k0 = her_k0(), n = her_n() + 1 - k0; A.her = k0 + (A.her - k0 + d + n) % n; A.ht0 = A.t; son_jouer(SON_MENU, 0.35f, 1); }
/* Application de la plateforme (eng_config_t.apps) : elle prend la main (l'ESP32 redémarre dans l'autre programme) */
/* Rangées du menu des applications : 0 le jeu (son écran titre), les eng_config_t.apps, puis RÉGLAGES s'il y en a */
static int napps_menu(void) { const int n = 1 + E.cfg.napps + (E.cfg.nregls > 0); return n < 4 ? n : 4; }
static void lancer_app(int k) {
    if (k < 0 || k >= napps_menu()) return;
    son_jouer(SON_VALIDER, 0.45f, 1);
    if (!k) page(AP_TITRE);
    else if (k > E.cfg.napps) page(AP_REGL);
    else if (E.cfg.apps[k - 1].lancer) E.cfg.apps[k - 1].lancer();
}
static void regler(int k, int sens) {
    if (k < 0 || k >= E.cfg.nregls || !E.cfg.regls[k].changer) return;
    A.sel = k;
    E.cfg.regls[k].changer(sens);
    son_jouer(SON_MENU, 0.35f, sens > 0 ? 1.1f : 0.9f);
}
static void carr_retour(void);
static void carr_action(int act, int arg);
static void carr_touches(uint32_t k);
static void carr_maj(float dt);
static void retour(void) {
    if (A.page >= AP_CMENU) { carr_retour(); return; }
    son_jouer(SON_ANNULER, 0.45f, 1);
    if (A.changer) { changer_fin(0); return; }   /* code CODE_HEROINE : retour à la partie, même héroïne */
    if (A.page == AP_REGL) page(AP_APPS);
    else if (A.page == AP_PARTIES || A.page == AP_HEROINE || A.page == AP_APPS) page(AP_TITRE);
    else if (A.page == AP_INTRO) apres_intro();
}
static void action(int act, int arg) {
    switch (act) {
    case B_OPT: if (arg == A.sel) valider_titre(arg); else A.sel = arg; break;
    case B_PARTIE: if (arg == A.sel) valider_partie(arg); else A.sel = arg; break;
    case B_RETOUR: retour(); break;
    case B_PREC: her_pas(-1); break;
    case B_SUIV: her_pas(1); break;
    case B_CHOISIR: choisir(); break;
    case B_APP: if (arg == A.sel) lancer_app(arg); else A.sel = arg; break;
    case B_REG_MOINS: regler(arg, -1); break;
    case B_REG_PLUS: regler(arg, 1); break;
    case B_CLIGNE: case B_CMOINS: case B_CPLUS: case B_CVALIDER: carr_action(act, arg); break;
    }
}

void accueil_update(float dt, const eng_input_t *in) {
    A.t += dt;
    if (in->touch || in->keys) A.vu = A.t;
    C1 = ui_rgb(hud_theme_c1()); C2 = ui_rgb(hud_theme_c2());
    /* La caméra tourne lentement autour du lieu montré */
    E.yaw = A.yaw0 + A.t * ORBITE;
    if (A.fondu) {
        /* nouvelle partie choisie : son premier point de sauvegarde, le départ (REPRENDRE après une défaite y ramène au pis ;
           pas pour accueil_fin, qui passe l'ouverture sans choix de partie : la partie 1 garderait sinon un départ par-dessus) */
        if (A.t - A.ft0 >= T_FONDU) { if (A.changer) changer_fin(1); else { jouer(A.her); hud_sauve_debut(); } }
        A.prev = in->keys;
        return;
    }
    if (A.page == AP_INTRO && A.t - A.ct0 >= carte_duree(A.carte)) carte_suivante();
    if (A.page >= AP_CMENU) carr_maj(dt);   /* mode course : temps de la carrière, liste du garage glissée */
    /* Doigt : un toucher sur un bouton (ou un carton) ; glisser à gauche ou à droite (héroïne) ; maintenir (passer) */
    if (in->touch && !A.down) { A.down = 1; A.geste = 0; A.x0 = A.cx = in->touch_x; A.y0 = A.cy = in->touch_y; A.dt0 = A.t; }
    else if (in->touch) { A.cx = in->touch_x; A.cy = in->touch_y; }
    if (A.down && in->touch && !A.geste) {
        const float dx = A.cx - A.x0, dy = A.cy - A.y0;
        if (A.page == AP_HEROINE && fabsf(dx) > 50 && fabsf(dx) > fabsf(dy)) { her_pas(dx < 0 ? 1 : -1); A.geste = 1; }
        else if (A.page == AP_INTRO && A.t - A.dt0 > T_PASSER && hypotf(dx, dy) < 30) { apres_intro(); A.geste = 1; }
    }
    if (A.down && !in->touch) {
        A.down = 0;
        if (!A.geste && hypotf(A.cx - A.x0, A.cy - A.y0) < 20) {
            int hit = -1;
            for (int i = 0; i < nab; i++)
                if (A.cx >= AB[i].x && A.cx <= AB[i].x + AB[i].w && A.cy >= AB[i].y && A.cy <= AB[i].y + AB[i].h) { hit = i; break; }
            if (hit >= 0) action(AB[hit].act, AB[hit].arg);
            else if (A.page == AP_INTRO) {   /* carton suivant : sa fin tout de suite (ou son texte entier s'il entrait) */
                const float t = A.t - A.ct0, d = carte_duree(A.carte);
                if (t < T_IN) A.ct0 = A.t - T_IN; else if (t < d - T_OUT) A.ct0 = A.t - (d - T_OUT);
            }
        }
    }
    const uint32_t k = in->keys & ~A.prev;
    A.prev = in->keys;
    if (k & ENG_KEY_BACK) { retour(); return; }
    if (A.page >= AP_CMENU) { carr_touches(k); return; }
    switch (A.page) {
    case AP_TITRE:
        if (k & (ENG_KEY_DOWN | ENG_KEY_RIGHT)) A.sel = (A.sel + 1) % A.nopts;
        if (k & (ENG_KEY_UP | ENG_KEY_LEFT)) A.sel = (A.sel + A.nopts - 1) % A.nopts;
        if (k & ENG_KEY_OK) valider_titre(A.sel);
        break;
    case AP_PARTIES:
        if ((k & ENG_KEY_DOWN) && A.sel < PARTIES - 1) A.sel++;
        if ((k & ENG_KEY_UP) && A.sel > 0) A.sel--;
        if (k & ENG_KEY_OK) valider_partie(A.sel);
        break;
    case AP_APPS:
        if ((k & ENG_KEY_DOWN) && A.sel < napps_menu() - 1) A.sel++;
        if ((k & ENG_KEY_UP) && A.sel > 0) A.sel--;
        if (k & ENG_KEY_OK) lancer_app(A.sel);
        break;
    case AP_REGL:
        if ((k & ENG_KEY_DOWN) && A.sel < E.cfg.nregls - 1) A.sel++;
        if ((k & ENG_KEY_UP) && A.sel > 0) A.sel--;
        if (k & ENG_KEY_LEFT) regler(A.sel, -1);
        if (k & (ENG_KEY_RIGHT | ENG_KEY_OK)) regler(A.sel, 1);
        break;
    case AP_INTRO:
        if (k & ENG_KEY_OK) carte_suivante();
        break;
    case AP_HEROINE:
        if (k & ENG_KEY_LEFT) her_pas(-1);
        if (k & ENG_KEY_RIGHT) her_pas(1);
        if (k & ENG_KEY_OK) choisir();
        break;
    }
}

/* ---------- Dessin ---------- */
/* Zone tactile dans le repère de l'image (décalée comme le dessin : cadre centré du plein écran) */
static void bouton(float x, float y, float w, float h, int act, int arg) { if (nab < (int)(sizeof AB / sizeof AB[0])) { AB[nab].x = x + ui_tx(); AB[nab].y = y + ui_ty(); AB[nab].w = w; AB[nab].h = h; AB[nab].act = act; AB[nab].arg = arg; nab++; } }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
/* Tirage fixe de la cendre i (0..1), le même que celui de l'émulateur */
static float h01(int i, int k) {
    uint32_t x = (uint32_t)i * 2654435761u ^ (uint32_t)k * 2246822519u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return (float)(x & 0xFFFF) / 65536.0f;
}
static void cendres(float a) {
    for (int i = 0; i < NCENDRES; i++) {
        const float v = 14 + 26 * h01(i, 1), x = h01(i, 0) * 466 + sinf(A.t * 0.4f + i) * 18;
        const float y = fmodf(h01(i, 2) * 520 + A.t * v, 520) - 30, r = 1 + 2 * h01(i, 3);
        const int braise = i % 7 == 0;
        ui_circle(x, y, r, ui_rgb(braise ? 0xFF7A3A : 0x8A8A92), (int)((70 + 120 * h01(i, 4)) * a));
    }
}
static void vignette(void) {
    if (ENG_DYN || ENG_RECT) return;   /* plein écran, écran rectangulaire : pas de bord rond */
    ui_ring(CX, CY, 222, 30, ui_rgb(INK), 170);
    ui_ring(CX, CY, 196, 22, ui_rgb(INK), 80);
}
/* Texte coupé aux espaces en lignes de max_w pixels au plus (6 au plus) */
static int couper(const ui_font_t *f, const char *s, float max_w, char l[6][128]) {
    int n = 0; l[0][0] = 0;
    for (;;) {
        while (*s == ' ') s++;
        if (!*s) break;
        const char *e = ui_fin_mot(s);
        const size_t m = (size_t)(e - s) < 63 ? (size_t)(e - s) : 63, ln = strlen(l[n]);
        char essai[128];
        const char *w0 = s;
        s = e;
        if (ln + m + 2 > sizeof essai) { if (n == 5) break; memcpy(l[++n], w0, m); l[n][m] = 0; continue; }   /* ligne pleine */
        memcpy(essai, l[n], ln); if (ln) essai[ln] = ' '; memcpy(essai + ln + (ln > 0), w0, m); essai[ln + (ln > 0) + m] = 0;
        if (ln && ui_text_width(f, essai) > max_w) { if (n == 5) break; memcpy(l[++n], w0, m); l[n][m] = 0; }
        else memcpy(l[n], essai, strlen(essai) + 1);
    }
    return l[0][0] ? n + 1 : 0;
}
static void texte_centre(int style, int size, const char *s, float max_w, float ymid, float lh, uint16_t c, int a) {
    char l[6][128];
    const ui_font_t *f = ui_font(style, size);
    const int n = couper(f, s, max_w, l);
    for (int i = 0; i < n; i++) ui_text(f, CX, ymid - (n - 1) * lh / 2 + i * lh + size * 0.35f, UI_CENTER, c, a, l[i]);
}
static void rangee(float x, float y, float w, float h, int sel, const char *lab, const char *val, uint16_t lc) {
    float q[12]; ui_chamfer_path(x, y, w, h, 10, q);
    const uint16_t col = sel ? C2 : C1;
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.24f : 0.10f), 238);
    ui_outline(q, 6, sel ? 2 : 1, col, sel ? 255 : 128);
    if (val) {
        ui_text(ui_font(FONT_MONO, 20), x + w - 14, y + h / 2 + 7, UI_RIGHT, col, 255, val);
        const float vw = ui_text_width(ui_font(FONT_MONO, 20), val) + 16;
        ui_text(ui_fit(FONT_DISP, lab, w - vw - 32, 22, 16), x + 16, y + h / 2 + 8, UI_LEFT, lc, 255, lab);
    } else ui_text(ui_fit(FONT_DISP, lab, w - 32, 22, 16), x + w / 2, y + h / 2 + 8, UI_CENTER, lc, 255, lab);
}
static void bouton_retour(void) {
    float q[12]; ui_hex_path(CX, 428, 72, 54, q);
    ui_poly(q, 6, ui_rgb(0x060E1A), 242); ui_outline(q, 6, 2, ui_rgb(DIM), 255);
    ui_icon("back", CX, 428, 26, ui_rgb(DIM), 255);
    bouton(CX - 36, 401, 72, 54, B_RETOUR, 0);
}
static void duree_txt(float s, char *t, int n) { const int m = (int)(s / 60); snprintf(t, n, "%d h %02d", m / 60, m % 60); }

static void draw_titre(void) {
    const float tp = A.t;   /* fondu d'entrée : seulement au tout début */
    vignette();   /* le voile du monde (90 / 255) : accueil_voile, par l'ambiance du rendu */
    cendres(0.6f);
    const ui_font_t *f = ui_fit(FONT_DISP, E.w.ouv_titre, 380, 40, 22);
    ui_text(f, CX + 2, 174, UI_CENTER, ui_rgb(INK), 200, E.w.ouv_titre);
    ui_text(f, CX, 172, UI_CENTER, ui_rgb(WHITE), 255, E.w.ouv_titre);
    ui_line(96, 188, 370, 188, 2, C2, 255);
    if (E.w.ouv_sous[0]) ui_text(ui_fit(FONT_MONO, E.w.ouv_sous, 320, 18, 14), CX, 212, UI_CENTER, C1, 255, E.w.ouv_sous);
    if (E.w.edition == 1) ui_text(ui_fit(FONT_MONO, "ÉDITION ENFANT", 220, 16, 12), CX, 128, UI_CENTER, C1, 255, "ÉDITION ENFANT");
    else if (E.w.edition == 2 && E.deverrou18) ui_text(ui_fit(FONT_MONO, "18+", 220, 16, 12), CX, 128, UI_CENTER, C2, 255, "18+");
    static const char *const NOM[6] = { "CONTINUER", "NOUVELLE PARTIE", "CHARGER", "RÉGLAGES", "APPLICATIONS", "COURSE" };
    /* Rangées : les entrées du jeu (CONTINUER, NOUVELLE PARTIE, CHARGER, COURSE), resserrées au-delà de trois ; plus de
       quatre entrées en tout (COURSE et l'entrée de la plateforme, RÉGLAGES ou TRANSFERT ou APPLICATIONS) : celle de la
       plateforme devient un bouton hexagonal sous la ligne d'aide (01/11/2026, mode course) */
    int nr = 0;
    for (int i = 0; i < A.nopts; i++) nr += A.nopts <= 4 || (A.opts[i] != AC_REGLAGES && A.opts[i] != AC_APPS);
    const int quatre = nr > 3;
    for (int i = 0, r = 0, ic = 0; i < A.nopts; i++) {
        if (A.nopts > 4 && (A.opts[i] == AC_REGLAGES || A.opts[i] == AC_APPS)) {   /* bouton hexagonal */
            const int ni = A.nopts - nr;
            const float x = CX + (ic - (ni - 1) / 2.0f) * 60, y = 440;   /* sous la ligne d'aide */
            float q[12]; ui_hex_path(x, y, 52, 38, q);
            ui_poly(q, 6, ui_mix(ui_rgb(INK), i == A.sel ? C2 : C1, i == A.sel ? 0.24f : 0.10f), 238);
            ui_outline(q, 6, i == A.sel ? 2 : 1, i == A.sel ? C2 : C1, i == A.sel ? 255 : 160);
            ui_icon("sys", x, y, 26, i == A.sel ? C2 : C1, 255);
            bouton(x - 26, y - 19, 52, 38, B_OPT, i);
            ic++;
            continue;
        }
        const float y = A.nopts > 4 ? 222 + r * 42 : quatre ? 228 + r * 44 : 240 + r * 52, h = quatre ? 38 : 42;   /* (bouton de la plateforme dessous : un peu plus haut) */
        rangee(CX - 125, y, 250, h, i == A.sel, nom_option(NOM, A.opts[i]), NULL, ui_rgb(WHITE));
        bouton(CX - 125, y, 250, h, B_OPT, i);
        r++;
    }
    char t[128], d[32];
    const int o = A.opts[A.sel], p = derniere_partie();
    if (o == AC_CONTINUER && p >= 0) { duree_txt(A.info[p].temps, d, sizeof d); snprintf(t, sizeof t, "Partie %d · %s · niv. %d · %s", p + 1, A.info[p].lieu, A.info[p].niveau, d); }
    else if (o == AC_NOUVELLE) snprintf(t, sizeof t, "%s", E.w.ouv_ncartes ? "L'histoire du monde, puis ton héroïne" : "Choisir son héroïne");
    else if (o == AC_REGLAGES) snprintf(t, sizeof t, "%s", E.cfg.reglages_aide ? E.cfg.reglages_aide : "Serveur, édition, monde à jour");
    else if (o == AC_APPS) snprintf(t, sizeof t, "Les autres programmes de la montre");
    else if (o == AC_COURSE) snprintf(t, sizeof t, "Carrière et course libre, sur les circuits");
    else snprintf(t, sizeof t, "Reprendre une des %d parties", PARTIES);
    if (A.nopts > 4 && (o == AC_REGLAGES || o == AC_APPS)) snprintf(t, sizeof t, "%s", nom_option(NOM, o));   /* le bouton : son nom */
    ui_text(ui_fit(FONT_MONO, t, 300, 18, 14), CX, A.nopts > 4 ? 404 : 410, UI_CENTER, ui_rgb(DIM), 255, t);
    if (tp < T_TITRE) fondu_noir((int)(255 * (1 - tp / T_TITRE)));
}

static void draw_parties(void) {   /* sur fond noir, sans 3D (accueil_voile) */
    ui_circle(CX, CY, 150, ui_rgb(0x0D1428), 110);
    ui_ring(CX, CY, 215, 1, C1, 46);
    ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, A.nouvelle ? "NOUVELLE PARTIE" : "CHARGER");
    const char *sub = A.nouvelle ? "Choisir la partie à remplacer" : "Choisir une partie";
    ui_text(ui_fit(FONT_MONO, sub, 300, 20, 16), CX, 138, UI_CENTER, ui_rgb(DIM), 255, sub);
    char t[128], v[24], d[32];
    for (int p = 0; p < PARTIES; p++) {
        const float y = 156 + p * 52, dy = y + 46 - CY, hw = sqrtf(233 * 233 - dy * dy) - 40;
        if (A.ok[p]) { snprintf(t, sizeof t, "PARTIE %d · %s", p + 1, A.info[p].heroine[0] ? A.info[p].heroine : PARTY[0].name); snprintf(v, sizeof v, "NIV %d", A.info[p].niveau); }
        else { snprintf(t, sizeof t, "PARTIE %d", p + 1); snprintf(v, sizeof v, "VIDE"); }
        rangee(CX - hw, y, 2 * hw, 46, p == A.sel, t, v, A.ok[p] ? ui_rgb(WHITE) : ui_rgb(DIM));
        bouton(CX - hw, y, 2 * hw, 46, B_PARTIE, p);
    }
    const int p = A.sel;
    if (A.ok[p]) { duree_txt(A.info[p].temps, d, sizeof d); snprintf(t, sizeof t, "%s · %s de jeu · n° %u", A.info[p].lieu, d, (unsigned)A.info[p].seq); }
    else snprintf(t, sizeof t, "%s", A.nouvelle ? "Partie libre" : "Aucune sauvegarde");
    ui_text(ui_fit(FONT_MONO, t, 300, 20, 16), CX, 384, UI_CENTER, ui_rgb(DIM), 255, t);
    bouton_retour();
}

/* Applications de la plateforme : une rangée chacune, l'aide de celle choisie en dessous */
static void draw_apps(void) {   /* sur fond noir, sans 3D (accueil_voile) */
    ui_circle(CX, CY, 150, ui_rgb(0x0D1428), 110);
    ui_ring(CX, CY, 215, 1, C1, 46);
    ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, "APPLICATIONS");
    ui_text(ui_fit(FONT_MONO, "Toucher deux fois pour lancer", 300, 20, 16), CX, 138, UI_CENTER, ui_rgb(DIM), 255,
            "Toucher deux fois pour lancer");
    /* Le jeu, les applications de la plateforme, puis RÉGLAGES (4 rangées au plus) */
    const int n = napps_menu();
    for (int k = 0; k < n; k++) {
        const float y = 156 + k * 52, dy = y + 46 - CY, hw = sqrtf(233 * 233 - dy * dy) - 40;
        const char *nom = !k ? (E.w.ouv_titre[0] ? E.w.ouv_titre : "LE CHŒUR DES CENDRES") : k > E.cfg.napps ? "RÉGLAGES" : E.cfg.apps[k - 1].nom;
        rangee(CX - hw, y, 2 * hw, 46, k == A.sel, nom, NULL, ui_rgb(WHITE));
        bouton(CX - hw, y, 2 * hw, 46, B_APP, k);
    }
    const char *aide = !A.sel ? "Le jeu" : A.sel > E.cfg.napps ? "Volume, luminosité, veille de la montre"
                     : E.cfg.apps[A.sel - 1].aide ? E.cfg.apps[A.sel - 1].aide : "";
    ui_text(ui_fit(FONT_MONO, aide, 300, 20, 16), CX, 384, UI_CENTER, ui_rgb(DIM), 255, aide);
    bouton_retour();
}

/* Réglages de la montre : une rangée par réglage, moitié gauche = un cran de moins, moitié droite = un de plus */
static void draw_regl(void) {   /* sur fond noir, sans 3D (accueil_voile) */
    ui_circle(CX, CY, 150, ui_rgb(0x0D1428), 110);
    ui_ring(CX, CY, 215, 1, C1, 46);
    ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, "RÉGLAGES");
    ui_text(ui_fit(FONT_MONO, "Gauche : moins · droite : plus", 300, 20, 16), CX, 138, UI_CENTER, ui_rgb(DIM), 255,
            "Gauche : moins · droite : plus");
    const int n = E.cfg.nregls < 5 ? E.cfg.nregls : 5;
    const float y0 = n > 4 ? 152 : 156, pas = n > 4 ? 44 : 52, h = n > 4 ? 40 : 46;   /* cinq rangées : resserrées */
    for (int k = 0; k < n; k++) {
        const float y = y0 + k * pas, dy = y + h - CY, hw = sqrtf(233 * 233 - dy * dy) - 40;
        const char *v = E.cfg.regls[k].valeur ? E.cfg.regls[k].valeur() : "";
        rangee(CX - hw, y, 2 * hw, h, k == A.sel, E.cfg.regls[k].nom, v, ui_rgb(WHITE));
        bouton(CX - hw, y, hw, h, B_REG_MOINS, k);
        bouton(CX, y, hw, h, B_REG_PLUS, k);
    }
    ui_text(ui_fit(FONT_MONO, "Gardés sur la carte SD", 300, 20, 16), CX, 384, UI_CENTER, ui_rgb(DIM), 255, "Gardés sur la carte SD");
    bouton_retour();
}

/* Bandeau du texte d'un carton, centré sur la hauteur y, au-dessus d'un lieu ou d'une image */
static void bandeau_carton(const char *s, float y, float a, float at) {
    char l[6][128];
    const int n = couper(ui_font(FONT_BODY, 24), s, 330, l);
    const float h = n * 28.0f + 28, y0 = y - h / 2;   /* bandeau à la hauteur du texte */
    ui_rect(0, y0, ENG_SCREEN, h, ui_rgb(INK), (int)(170 * a));
    ui_line(40, y0, 426, y0, 2, C2, (int)(255 * a));
    ui_line(40, y0 + h, 426, y0 + h, 2, C2, (int)(255 * a));
    cendres(0.5f * a);
    texte_centre(FONT_BODY, 24, s, 330, y, 28, ui_rgb(WHITE), (int)(255 * at));
}

static void draw_intro(void) {
    const int k = A.carte;
    const float t = A.t - A.ct0, d = carte_duree(k);
    const float a = clampf(fminf(t / T_IN, (d - t) / T_OUT), 0, 1), at = clampf(fminf((t - 0.25f) / T_IN, (d - t) / T_OUT), 0, 1);
    const char *s = E.w.ouv_cartes[k].texte;
    if (E.w.ouv_cartes[k].x < 0 && E.w.ouv_img[k] >= 0) {   /* image (OUVI) : plein écran, bandeau du texte en bas */
        int iw, ih;
        const uint16_t *pal;
        const uint8_t *ix = img_ix(E.w.ouv_img[k], &iw, &ih, &pal, 1);   /* NULL une image, le temps de la lire ; fond noir sans 3D (accueil_voile) */
        if (ix) {
            const float sc = (ENG_SCREEN - 2) / (float)iw;
            ui_image_pal(ix, pal, iw, ih, CX - iw * sc / 2, CY - ih * sc / 2, sc, CX, CY, 233);
            fondu_noir((int)(255 * (1 - a)));
        }
        bandeau_carton(s, 350, a, at);
    } else if (E.w.ouv_cartes[k].x < 0) {   /* noir (sans 3D : accueil_voile) : braise au bas de l'écran, cendres qui tombent, texte au milieu */
        ui_circle(CX, 560, 250, ui_rgb(0x6A1A08), (int)(70 * a));
        ui_circle(CX, 600, 250, ui_rgb(0xC4442F), (int)(40 * a));
        cendres(a);
        texte_centre(FONT_BODY, 28, s, 340, CY, 36, ui_rgb(WHITE), (int)(255 * at));
    } else {                          /* un lieu : bandeau du texte sous lui */
        vignette();
        fondu_noir((int)(255 * (1 - a)));
        bandeau_carton(s, 300, a, at);
    }
    char c[64]; snprintf(c, sizeof c, "%d / %d · MAINTENIR : PASSER", k + 1, E.w.ouv_ncartes);
    ui_text(ui_font(FONT_MONO, 18), CX, 444, UI_CENTER, ui_rgb(DIM), 150, c);
}

/* Image de l'héroïne k (cellule de sa marche, direction row, image fr) dans le tampon libre ; 0 : sans planche */
static int her_image(int k, int row, int fr, int *w, int *h, int *pieds) {
    const int p = her_perso(k), base = p >= 0 ? E.w.persos[p].sheet : E.w.psheet;
    if (base < 0) return 0;
    const int sh = anim_sheet(base, ANIM_WALK);
    const sheet_t *s = &E.w.sheets[sh];
    const uint8_t *rm = E.w.remap[p >= 0 ? E.w.persos[p].remap : 0];
    const int fw = s->fw < HER_MAX ? s->fw : HER_MAX, fh = s->fh < HER_MAX ? s->fh : HER_MAX, sw = s->fw * s->frames;
    fr %= s->frames;
    const uint8_t *blk = sheet_block(sh, row, 2);   /* planche lue à la demande : tout de suite */
    if (!blk) return 0;
    hbuf ^= 1;
    for (int y = 0; y < fh; y++) for (int x = 0; x < fw; x++) {
        const uint8_t id = blk[y * sw + fr * s->fw + x];
        const uint8_t *c = E.w.pal[rm[id]];
        HPX[hbuf][y * fw + x] = ui_rgb(((uint32_t)c[0] << 16) | (c[1] << 8) | c[2]);
        HMK[hbuf][y * fw + x] = id != 0;
    }
    *w = fw; *h = fh; *pieds = s->fh - s->foot[row & 7];
    return 1;
}
static void draw_heroine(void) {   /* voile du monde (110 / 255) : accueil_voile */
    vignette();
    cendres(0.4f);
    texte_centre(FONT_BODY, 24, E.w.ouv_question[0] ? E.w.ouv_question : "Qui es-tu ?", 320, 104, 28, ui_rgb(WHITE), 255);
    /* Elle marche sur place en tournant sur elle-même */
    static const uint8_t TOUR[8] = { 0, 7, 6, 5, 4, 3, 2, 1 };   /* S, SO, O, NO, N, NE, E, SE */
    const float ht = A.t - A.ht0;
    int w, h, pieds;
    ui_circle(CX, 250, 92, ui_mix(ui_rgb(INK), C1, 0.18f), 120);
    ui_ellipse_ring(CX, HER_PIEDS, 56, 13, 2, C1, 170);
    if (her_image(A.her, TOUR[(int)(ht / HER_DIR_S) & 7], (int)(ht / 0.13f), &w, &h, &pieds)) {
        const float s = clampf(HER_H / (float)(h > 0 ? h : 1), 1, 5);
        ui_image(HPX[hbuf], HMK[hbuf], w, h, CX - w * s / 2, HER_PIEDS - pieds * s, s, CX, CY, 233);
    }
    /* Flèches de part et d'autre */
    for (int d = -1; d <= 1; d += 2) {
        const float x = CX + d * 168, y = 250;
        float q[12]; ui_hex_path(x, y, 56, 48, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 220); ui_outline(q, 6, 2, C1, 200);
        const float tri[6] = { x + d * 9, y, x - d * 7, y - 11, x - d * 7, y + 11 };
        ui_poly(tri, 3, C1, 255);
        bouton(x - 28, y - 24, 56, 48, d < 0 ? B_PREC : B_SUIV, 0);
    }
    ui_text(ui_fit(FONT_DISP, her_nom(A.her), 260, 30, 20), CX, 354, UI_CENTER, ui_rgb(WHITE), 255, her_nom(A.her));
    char c[32]; snprintf(c, sizeof c, "%d / %d", A.her + 1 - her_k0(), her_n() + 1 - her_k0());
    ui_text(ui_font(FONT_MONO, 18), CX, 376, UI_CENTER, C1, 255, c);
    rangee(CX - 95, 390, 190, 42, 1, "CHOISIR", NULL, ui_rgb(WHITE));
    bouton(CX - 95, 390, 190, 42, B_CHOISIR, 0);
}


/* ---------- Mode course : les pages de la carrière (carriere.c ; emulateur/carriere.js de l'Atelier, à garder identiques) ----------
   AP_CMENU (les trois carrières, la course libre), AP_CCARR (une carrière : COUPES, ÉPREUVES, DÉFI DU JOUR, GARAGE),
   AP_CCOUPES (ses coupes : médaille, course en cours, FERMÉE), AP_CCOUPE (la coupe : ses trois courses, la difficulté,
   le départ), AP_CEPRS (les épreuves), AP_CEPR (une épreuve : sorte, circuit, record, difficulté, départ), AP_CDEFI (le
   défi du jour), AP_CRES (résultats d'une course : continuer, recommencer), AP_CFIN (fin d'une coupe : classement,
   médaille, prime), AP_CRECOMP (duel gagné : son véhicule ou les livrées), AP_CGARAGE (véhicules : prendre ou acheter),
   AP_CVEH (un véhicule à soi : couleur, améliorations), AP_CLIBRE (course libre : circuit, véhicule, épreuve, niveau,
   tours). Sur le monde voilé, le circuit derrière ; toucher une ligne la choisit, la toucher encore la valide ; moitiés
   gauche et droite d'une ligne réglable : moins, plus. Listes (coupes, épreuves, garage) : quatre lignes visibles,
   glissées au doigt. Touches : haut / bas (lignes), gauche / droite (réglage), OK, retour */
static const char *const MED_NOM[4] = { "—", "BRONZE", "ARGENT", "OR" };
static const char *const NIV_NOM[3] = { "FACILE", "NORMAL", "DIFFICILE" };
static const char *const AMEL_NOM[3] = { "VITESSE", "ACCÉLÉRATION", "ADHÉRENCE" };
static const char *const SORTE_AIDE[CS_N] = { "Trois adversaires", "Seule contre ton fantôme", "Le meilleur tour compte, jusqu'à TERMINER",
                                              "Le dernier sort à chaque tour", "Bats le rival de la région, sans armes" };
static const int LIB_SORTES[4] = { CS_COURSE, CS_CLM, CS_TOUR, CS_ELIM };
/* course libre : rangs dans les listes, sorte (LIB_SORTES), niveau, tours (0 : ceux du circuit) ; épreuve ouverte et sa
   difficulté ; véhicule du garage (rang dans CARRD.veh) et sa couleur (rang parmi les débloquées) */
static struct { int c, v, s, niv, tours; } LIB = { 0, 0, 0, 1, 0 };
static struct { int k, niv; } EPS = { 0, 1 };
static struct { int i, coul; } VEH = { 0, 0 };
/* Page RÉGLAGES ouverte depuis la pause d'une course (hud.c, pause_faire ; 02/11/2026) : sans l'ouverture (A.on reste 0 :
   le monde, la musique et la caméra restent ceux de la course), dessinée par-dessus la pause ; la page du mode course
   d'avant est gardée et reprise au retour */
static struct { int on, page, sel, premier; } RP;
typedef struct { char lab[32], val[16]; int act, dim, regle; } cligne_t;
#define CL_MAX 40
static cligne_t CLU[CL_MAX] ENG_FROID, CLB[CL_MAX] ENG_FROID;   /* lignes des pages : mise à jour, dessin (en PSRAM, pas sur la pile) */
static void chrono_txt(float t, char *s, int n) {
    if (!(t > 0)) { snprintf(s, n, "—"); return; }
    const int c = (int)floorf(t * 100);
    snprintf(s, n, "%d:%02d.%02d", c / 6000, c / 100 % 60, c % 100);
}
static const char *place_txt(int n) { static char t[8]; if (n == 1) return "1RE"; if (n >= 2 && n <= 4) { snprintf(t, sizeof t, "%dE", n); return t; } return "—"; }
static const char *nom_perso(int p) { return p >= 0 && p < E.w.npersos && E.w.persos[p].nom ? E.w.persos[p].nom : "—"; }
static void montrer_circuit(int ci) {
    if (ci < 0 || ci >= CARRD.ncircuits) return;
    const carr_circuit_t *c = &CARRD.circuits[ci];
    const piste_t *P = course_piste(c->piste);
    if (P) montrer_k(c->carte, P->xd, P->zd, c->heure);
}
static int lib_circuit(void) { int L[CARR_CIRCUITS]; const int n = carr_circuits_libres(L); return n ? L[(LIB.c % n + n) % n] : 0; }
static int lib_vehicule(void) { int V[CARR_VEH]; const int n = carr_vehicules_libres(V); return n ? V[(LIB.v % n + n) % n] : -1; }
static int liste_page(int p) { return p == AP_CCOUPES || p == AP_CEPRS || p == AP_CGARAGE || p == AP_CREGL || p == AP_CCARR; }
/* Coupes et épreuves de la catégorie de la page (rangs ; la ligne i de la liste est le rang L[i]) */
static int cat_coupes(int *L) { return carr_cat_coupes(carr_cat(), L); }
static int cat_eprs(int *L) { return carr_cat_eprs(carr_cat(), L); }
static int apercu_page(int p) { return p == AP_CGARAGE || p == AP_CVEH; }
/* Record d'une épreuve (texte) : temps (contre-la-montre, tour rapide) ou place */
static void record_txt(int sorte, int rec, char *s, int n) {
    if (!rec) snprintf(s, n, "—");
    else if (sorte == CS_CLM || sorte == CS_TOUR) chrono_txt(rec / 100.0f, s, n);
    else snprintf(s, n, "%s", place_txt(rec));
}
static int date_jour(void) { const carr_defi_t *d = carr_defi(); return d->jour; }
/* Lignes de la page (lignes de l'émulateur) */
static int c_lignes(cligne_t *L) {
    int n = 0;
    memset(L, 0, sizeof(cligne_t) * CL_MAX);
    const carr_save_t *c = carr_cur();
    switch (A.page) {
    case AP_CMENU:
        for (int k = 0; k < CARRIERES; k++) {
            const carr_save_t *i = carr_info(k);
            snprintf(L[n].lab, sizeof L[n].lab, "CARRIÈRE %d", k + 1);
            if (i) snprintf(L[n].val, sizeof L[n].val, "%d / %d CAT.", carr_cats_ouvertes(i), CARRD.ncats); else snprintf(L[n].val, sizeof L[n].val, "VIDE");
            L[n].act = 1; L[n].dim = !i; n++;
        }
        snprintf(L[n].lab, sizeof L[n].lab, "COURSE LIBRE"); L[n].act = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "RÉGLAGES"); L[n].act = 1; n++;   /* (02/11/2026 : AP_CREGL) */
        break;
    case AP_CREGL:   /* les réglages qui ont un sens en course (hud.c : hud_regl_*), chacun réglable */
        for (int i = 0; i < hud_regl_n() && n < CL_MAX; i++) {
            snprintf(L[n].lab, sizeof L[n].lab, "%s", hud_regl_nom(i));
            hud_regl_valeur(i, L[n].val, sizeof L[n].val);
            L[n].regle = 1; n++;
        }
        break;
    case AP_CCARR: {   /* les catégories (03/11/2026), puis le défi du jour */
        for (int k = 0; k < CARRD.ncats && n < CL_MAX; k++) {
            const int o = carr_cat_ouverte(k), kc = carr_cat_classe(c, k);
            snprintf(L[n].lab, sizeof L[n].lab, "%s", CARRD.cats[k].nom);
            snprintf(L[n].val, sizeof L[n].val, "%s", !o ? "FERMÉE" : kc >= 0 ? CARRD.classes[kc].nom : "—");
            L[n].act = 1; L[n].dim = !o; n++;
        }
        const int j = date_jour();
        snprintf(L[n].lab, sizeof L[n].lab, "DÉFI DU JOUR"); snprintf(L[n].val, sizeof L[n].val, "%s", j < 0 ? "—" : c->defi_jour == j ? "FAIT" : "À FAIRE");
        L[n].act = 1; L[n].dim = j < 0; n++;
        break;
    }
    case AP_CCAT: {   /* une catégorie : ses coupes, ses épreuves, son garage */
        int K[CARR_EPR];
        const int nc = cat_coupes(K);
        int m = 0, me = 0;
        for (int i = 0; i < nc; i++) m += c->coupe_med[K[i]] > 0;
        snprintf(L[n].lab, sizeof L[n].lab, "COUPES"); snprintf(L[n].val, sizeof L[n].val, "%d / %d", m, nc); L[n].act = 1; n++;
        const int ne = cat_eprs(K);
        for (int i = 0; i < ne; i++) me += c->epr[K[i]].med > 0;
        if (ne) { snprintf(L[n].lab, sizeof L[n].lab, "ÉPREUVES"); snprintf(L[n].val, sizeof L[n].val, "%d / %d", me, ne); L[n].act = 1; n++; }
        snprintf(L[n].lab, sizeof L[n].lab, "GARAGE"); L[n].act = 1; n++;
        break;
    }
    case AP_CCOUPES: {
        int K[CARR_COUPES];
        const int nc = cat_coupes(K);
        for (int i = 0; i < nc && n < CL_MAX; i++) {
            const int k = K[i], o = carr_ouverte(k);
            snprintf(L[n].lab, sizeof L[n].lab, "%s", CARRD.coupes[k].nom);
            if (c->coupe == k) snprintf(L[n].val, sizeof L[n].val, "%d / 3", c->course + 1);
            else snprintf(L[n].val, sizeof L[n].val, "%s", c->coupe_med[k] ? MED_NOM[c->coupe_med[k] & 3] : o ? "—" : "FERMÉE");
            L[n].act = 1; L[n].dim = !o; n++;
        }
        break;
    }
    case AP_CCOUPE: {
        const carr_coupe_t *cp = &CARRD.coupes[c->coupe < CARRD.ncoupes ? c->coupe : 0];
        for (int i = 0; i < 3; i++) {
            snprintf(L[n].lab, sizeof L[n].lab, "%s", CARRD.circuits[cp->circuits[i]].nom);
            snprintf(L[n].val, sizeof L[n].val, "%s", i < c->course ? place_txt(c->places[i]) : i == c->course ? "SUIVANTE" : "");
            L[n].dim = i > c->course; n++;
        }
        snprintf(L[n].lab, sizeof L[n].lab, "DIFFICULTÉ"); snprintf(L[n].val, sizeof L[n].val, "%s", NIV_NOM[c->coupe_niv % 3]);
        L[n].regle = c->course == 0; L[n].dim = c->course > 0; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "DÉPART"); snprintf(L[n].val, sizeof L[n].val, "COURSE %d / 3", c->course + 1); L[n].act = 1; n++;
        break;
    }
    case AP_CEPRS: {
        int K[CARR_EPR];
        const int ne = cat_eprs(K);
        for (int i = 0; i < ne && n < CL_MAX; i++) {
            const int k = K[i], o = carr_epr_ouverte(k);
            snprintf(L[n].lab, sizeof L[n].lab, "%s", CARRD.epr[k].nom);
            snprintf(L[n].val, sizeof L[n].val, "%s", c->epr[k].med ? MED_NOM[c->epr[k].med & 3] : o ? "—" : "FERMÉE");
            L[n].act = 1; L[n].dim = !o; n++;
        }
        break;
    }
    case AP_CEPR: case AP_CDEFI: {
        const int defi = A.page == AP_CDEFI;
        const carr_defi_t *d = carr_defi();
        if (defi && d->jour < 0) { snprintf(L[n].lab, sizeof L[n].lab, "DATE INCONNUE"); L[n].dim = 1; n++; break; }
        const carr_epr_t *e = &CARRD.epr[EPS.k < CARRD.nepr ? EPS.k : 0];
        const int s = defi ? d->sorte : e->sorte, ci = defi ? d->circuit : e->circuit, cl = defi ? d->classe : e->classe;
        const int tours = s == CS_TOUR ? 9 : s == CS_ELIM ? 3 : !defi && e->tours ? e->tours : CARRD.circuits[ci].tours;
        snprintf(L[n].lab, sizeof L[n].lab, "%s", carr_sorte_nom(s));
        if (s == CS_TOUR) snprintf(L[n].val, sizeof L[n].val, "%d ESSAIS", tours); else snprintf(L[n].val, sizeof L[n].val, "%d TOURS", tours);
        n++;
        snprintf(L[n].lab, sizeof L[n].lab, "%s", CARRD.circuits[ci].nom); snprintf(L[n].val, sizeof L[n].val, "%s", CARRD.classes[cl].nom); n++;
        snprintf(L[n].lab, sizeof L[n].lab, "%s", defi ? "AUJOURD'HUI" : "RECORD");
        if (defi) snprintf(L[n].val, sizeof L[n].val, "%s", c->defi_jour == d->jour ? MED_NOM[c->defi_med & 3] : "—");
        else record_txt(s, c->epr[EPS.k].rec, L[n].val, sizeof L[n].val);
        L[n].dim = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "DIFFICULTÉ"); snprintf(L[n].val, sizeof L[n].val, "%s", NIV_NOM[(defi ? d->niveau : EPS.niv) % 3]);
        L[n].regle = !defi; L[n].dim = defi; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "DÉPART"); L[n].act = 1; n++;
        break;
    }
    case AP_CRES: {
        const carr_res_t *r = carr_res();
        const int m = carriere_mode();
        if (r->sorte != CS_TOUR) { snprintf(L[n].lab, sizeof L[n].lab, "TEMPS"); chrono_txt(r->temps, L[n].val, sizeof L[n].val); n++; }
        snprintf(L[n].lab, sizeof L[n].lab, "MEILLEUR TOUR"); chrono_txt(r->meilleur, L[n].val, sizeof L[n].val); n++;
        if (m == 1) { snprintf(L[n].lab, sizeof L[n].lab, "POINTS · CRÉDITS"); snprintf(L[n].val, sizeof L[n].val, "+%d · +%d", r->points, r->credits); n++; }
        else if (m == 3 || m == 4) { snprintf(L[n].lab, sizeof L[n].lab, "MÉDAILLE · CRÉDITS"); snprintf(L[n].val, sizeof L[n].val, "%s · +%d", r->med ? MED_NOM[r->med & 3] : "—", r->credits + r->bonus); n++; }
        snprintf(L[n].lab, sizeof L[n].lab, "CONTINUER"); L[n].act = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "RECOMMENCER"); L[n].act = 1; n++;
        break;
    }
    case AP_CFIN: {
        const carr_fin_t *f = carr_fin();
        int o[4] = { 0, 1, 2, 3 };
        for (int a = 1; a < 4; a++) for (int b = a; b > 0 && (f->pts[o[b]] > f->pts[o[b - 1]] || (f->pts[o[b]] == f->pts[o[b - 1]] && o[b] < o[b - 1])); b--) { const int t = o[b]; o[b] = o[b - 1]; o[b - 1] = t; }
        for (int i = 0; i < 4; i++) {
            snprintf(L[n].lab, sizeof L[n].lab, "%d. %s", i + 1, o[i] ? nom_perso(carr_course_rivaux()[o[i] - 1]) : "TOI");
            snprintf(L[n].val, sizeof L[n].val, "%d PTS", f->pts[o[i]]); L[n].dim = o[i] != 0; n++;
        }
        snprintf(L[n].lab, sizeof L[n].lab, "CONTINUER"); L[n].act = 1; n++;
        break;
    }
    case AP_CRECOMP: {
        const carr_epr_t *e = &CARRD.epr[EPS.k < CARRD.nepr ? EPS.k : 0];
        snprintf(L[n].lab, sizeof L[n].lab, "%s", nom_perso(e->rival)); snprintf(L[n].val, sizeof L[n].val, "VÉHICULE"); L[n].act = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "LIVRÉES"); snprintf(L[n].val, sizeof L[n].val, "TOUTES"); L[n].act = 1; n++;
        break;
    }
    case AP_CGARAGE: {
        int G[CARR_VEH]; const int ng = carr_garage_liste(G), vc = carr_cat_veh(carr_cat());
        for (int i = 0; i < ng && n < CL_MAX; i++) {
            const carr_veh_t *v = &CARRD.veh[G[i]];
            snprintf(L[n].lab, sizeof L[n].lab, "%s", nom_perso(v->perso));
            if (carr_possede(v->perso)) snprintf(L[n].val, sizeof L[n].val, "%s", vc == G[i] ? "CHOISI" : "À TOI");
            else snprintf(L[n].val, sizeof L[n].val, "%u CR", (unsigned)v->prix);
            L[n].act = 1; L[n].dim = !carr_possede(v->perso) && v->prix > c->credits; n++;
        }
        break;
    }
    case AP_CVEH: {
        int C[8]; const int nc = carr_couleurs(VEH.i, C);
        snprintf(L[n].lab, sizeof L[n].lab, "COULEUR"); snprintf(L[n].val, sizeof L[n].val, "%d / %d", VEH.coul % nc + 1, nc); L[n].regle = 1; n++;
        for (int s = 0; s < 3; s++) {
            const int lv = carr_amelio(VEH.i, s);
            snprintf(L[n].lab, sizeof L[n].lab, "%s", AMEL_NOM[s]);
            if (lv >= 3) snprintf(L[n].val, sizeof L[n].val, "3 / 3");
            else snprintf(L[n].val, sizeof L[n].val, "%u/3 · %u CR", (unsigned)lv & 3u, (unsigned)CARRD.amelio_prix[lv] % 100000u);
            L[n].act = 1; L[n].dim = lv >= 3 || (uint32_t)CARRD.amelio_prix[lv] > c->credits; n++;
        }
        break;
    }
    case AP_CLIBRE: {
        const int ci = lib_circuit(), pv = lib_vehicule(), s = LIB_SORTES[LIB.s & 3];
        snprintf(L[n].lab, sizeof L[n].lab, "%s", CARRD.circuits[ci].nom); snprintf(L[n].val, sizeof L[n].val, "< >"); L[n].regle = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "%s", nom_perso(pv)); snprintf(L[n].val, sizeof L[n].val, "< >"); L[n].regle = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "%s", carr_sorte_nom(s)); snprintf(L[n].val, sizeof L[n].val, "< >"); L[n].regle = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "NIVEAU"); snprintf(L[n].val, sizeof L[n].val, "%s", NIV_NOM[LIB.niv]); L[n].regle = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "TOURS"); snprintf(L[n].val, sizeof L[n].val, "%d", LIB.tours ? LIB.tours : s == CS_TOUR ? 9 : s == CS_ELIM ? 3 : CARRD.circuits[ci].tours); L[n].regle = 1; n++;
        snprintf(L[n].lab, sizeof L[n].lab, "DÉPART"); L[n].act = 1; n++;
        break;
    }
    }
    return n;
}
/* Titre, sous-titre, aide (textes de l'émulateur) */
static void c_textes(char *titre, char *sous, char *aide, int n) {
    const carr_save_t *c = carr_cur();
    const int s = A.sel;
    titre[0] = sous[0] = aide[0] = 0;
    switch (A.page) {
    case AP_CMENU: {
        snprintf(titre, n, "COURSE"); snprintf(sous, n, "Trois carrières et la course libre");
        const carr_save_t *i = s < CARRIERES ? carr_info(s) : NULL;
        char d[32];
        if (s == CARRIERES) snprintf(aide, n, "Un circuit, un véhicule, une épreuve");
        else if (s == CARRIERES + 1) snprintf(aide, n, "Son, direction, commandes, écran");
        else if (i) { duree_txt(i->temps, d, sizeof d); snprintf(aide, n, "%d médaille(s) · %u CR · %s", carr_medailles(i), (unsigned)i->credits, d); }
        else snprintf(aide, n, "Nouvelle carrière");
        break;
    }
    case AP_CCARR: {
        snprintf(titre, n, "CARRIÈRE %d", carr_k() + 1);
        snprintf(sous, n, "%d CATÉGORIE(S) · %u CR", carr_cats_ouvertes(c), (unsigned)c->credits);
        if (s < CARRD.ncats) {   /* une catégorie : ce qu'on y court, ou ce qui l'ouvre */
            const carr_cat_t *t = &CARRD.cats[s];
            if (carr_cat_ouverte(s)) snprintf(aide, n, "%s", t->aide);
            else if (t->ouvre_cat >= 0 && t->ouvre_classe >= 0) snprintf(aide, n, "%s %s : toutes les coupes en bronze", CARRD.cats[t->ouvre_cat].nom, CARRD.classes[t->ouvre_classe].nom);
        } else {
            const carr_defi_t *d = carr_defi();
            if (d->jour < 0) snprintf(aide, n, "Date de la montre inconnue");
            else snprintf(aide, n, "%s · %s · %s", CARRD.cats[d->cat].nom, carr_sorte_nom(d->sorte), CARRD.circuits[d->circuit].nom);
        }
        break;
    }
    case AP_CCAT: {
        const int k = carr_cat(), kc = carr_cat_classe(c, k), pv = carr_veh_perso(k);
        snprintf(titre, n, "%s", CARRD.cats[k].nom);
        snprintf(sous, n, "CLASSE %s · %u CR", kc >= 0 ? CARRD.classes[kc].nom : "—", (unsigned)c->credits);
        cligne_t *L = CLB; const int nl = c_lignes(L);
        const char *lab = s < nl ? L[s].lab : "";
        if (!strcmp(lab, "COUPES")) snprintf(aide, n, "Trois courses, des points, une médaille");
        else if (!strcmp(lab, "ÉPREUVES")) snprintf(aide, n, "Contre-la-montre, tour rapide, élimination%s", k == 0 ? ", duels" : "");
        else snprintf(aide, n, "%s", pv >= 0 ? nom_perso(pv) : "Aucun véhicule");
        break;
    }
    case AP_CCOUPES: {
        int K[CARR_COUPES];
        const int nc = cat_coupes(K), kh = carr_cat_classe(c, carr_cat());
        snprintf(titre, n, "COUPES · %s", CARRD.cats[carr_cat()].nom);
        snprintf(sous, n, "CLASSE %s · %u CR", kh >= 0 ? CARRD.classes[kh].nom : "—", (unsigned)c->credits);
        if (s < nc) {
            const int k = K[s];
            const carr_coupe_t *cp = &CARRD.coupes[k];
            if (!carr_ouverte(k)) {
                if (cp->classe > kh && cp->classe > 0) snprintf(aide, n, "Classe %s : toutes les coupes en bronze", CARRD.classes[cp->classe - 1].nom);
                else snprintf(aide, n, "Le bronze à la coupe d'avant");
            } else if (c->coupe_pts[k]) snprintf(aide, n, "%s · 3 courses · record %d pts", CARRD.classes[cp->classe].nom, c->coupe_pts[k]);
            else snprintf(aide, n, "%s · 3 courses", CARRD.classes[cp->classe].nom);
        }
        break;
    }
    case AP_CCOUPE: {
        const carr_coupe_t *cp = &CARRD.coupes[c->coupe < CARRD.ncoupes ? c->coupe : 0];
        int r[3] = { c->pts[1], c->pts[2], c->pts[3] };
        for (int a = 1; a < 3; a++) for (int b = a; b > 0 && r[b] > r[b - 1]; b--) { const int t = r[b]; r[b] = r[b - 1]; r[b - 1] = t; }
        snprintf(titre, n, "%s", cp->nom); snprintf(sous, n, "CLASSE %s · %s", CARRD.classes[cp->classe].nom, NIV_NOM[c->coupe_niv % 3]);
        if (s == 3 && c->course == 0) snprintf(aide, n, "Plus difficile : plus de crédits");
        else snprintf(aide, n, "Toi %d pts · rivaux %d · %d · %d", c->pts[0], r[0], r[1], r[2]);
        break;
    }
    case AP_CEPRS: {
        int K[CARR_EPR];
        const int ne = cat_eprs(K), kh = carr_cat_classe(c, carr_cat());
        snprintf(titre, n, "ÉPREUVES · %s", CARRD.cats[carr_cat()].nom);
        snprintf(sous, n, "CLASSE %s · %u CR", kh >= 0 ? CARRD.classes[kh].nom : "—", (unsigned)c->credits);
        if (s < ne) {
            const carr_epr_t *e = &CARRD.epr[K[s]];
            if (carr_epr_ouverte(K[s])) snprintf(aide, n, "%s · %s", carr_sorte_nom(e->sorte), CARRD.circuits[e->circuit].nom);
            else if (e->classe > kh) snprintf(aide, n, "Classe %s", CARRD.classes[e->classe].nom);
            else snprintf(aide, n, "Les coupes de la région en bronze");
        }
        break;
    }
    case AP_CEPR: case AP_CDEFI: {
        const int defi = A.page == AP_CDEFI;
        const carr_defi_t *d = carr_defi();
        const carr_epr_t *e = &CARRD.epr[EPS.k < CARRD.nepr ? EPS.k : 0];
        const int so = defi ? d->sorte : e->sorte, ci = defi ? d->circuit : e->circuit, cl = defi ? d->classe : e->classe;
        snprintf(titre, n, "%s", defi ? "DÉFI DU JOUR" : e->nom);
        if (defi && d->jour < 0) { snprintf(aide, n, "Règle l'heure de la montre"); break; }
        if (defi) snprintf(sous, n, "%02d/%02d · crédits × %.0f", d->jour % 32, d->jour / 32, CARRD.defi_gain);
        else if (c->epr[EPS.k].med) snprintf(sous, n, "MÉDAILLE : %s (%s)", MED_NOM[c->epr[EPS.k].med & 3], NIV_NOM[c->epr[EPS.k].niv % 3]);
        else snprintf(sous, n, "PAS ENCORE DE MÉDAILLE");
        const float ref = CARRD.circuits[ci].ref[cl < CARR_CLASSES ? cl : 0];
        char t[16];
        if (s == 1 && (so == CS_CLM || so == CS_TOUR) && ref > 0) {
            const int tours = so == CS_CLM ? (!defi && e->tours ? e->tours : CARRD.circuits[ci].tours) : 1;
            chrono_txt((so == CS_CLM ? ref * tours + CARRD.clm_depart : ref) * CARRD.diff_temps[(defi ? d->niveau : EPS.niv) % 3], t, sizeof t);
            snprintf(aide, n, "Or : %s", t);
        } else if (s == 1 && so == CS_DUEL) snprintf(aide, n, "Rival : %s", nom_perso(e->rival));
        else if (s == 3 && !defi) snprintf(aide, n, "Plus difficile : plus de crédits");
        else snprintf(aide, n, "%s", SORTE_AIDE[so]);
        break;
    }
    case AP_CRES: {
        const carr_res_t *r = carr_res();
        const int solo = r->sorte == CS_CLM || r->sorte == CS_TOUR;
        if (r->abandon) snprintf(titre, n, "ABANDON");
        else if (solo) snprintf(titre, n, "%s", r->med ? (r->med == 3 ? "MÉDAILLE D'OR" : r->med == 2 ? "MÉDAILLE D'ARGENT" : "MÉDAILLE DE BRONZE") : "PAS DE MÉDAILLE");
        else snprintf(titre, n, "%s PLACE", place_txt(r->place));
        snprintf(sous, n, "%s", CARRD.circuits[carr_course_circuit()].nom);
        char t[16]; chrono_txt(r->ref, t, sizeof t);
        if (r->abandon) snprintf(aide, n, "La course ne compte pas");
        else if (r->bonus) snprintf(aide, n, "Défi du jour réussi : +%d CR", r->bonus);
        else if (solo && r->ref > 0) snprintf(aide, n, "Référence %s", t);
        else if (r->sorte == CS_ELIM || r->sorte == CS_DUEL) snprintf(aide, n, "%s", r->place == 1 ? "Victoire" : "Recommence quand tu veux");
        else if (r->ref > 0 && r->med_t) snprintf(aide, n, "Référence %s · médaille %s", t, r->med_t == 1 ? "bronze" : r->med_t == 2 ? "argent" : "or");
        else if (r->ref > 0) snprintf(aide, n, "Référence %s · pas de médaille", t);
        else snprintf(aide, n, "Tour de référence à mesurer");
        break;
    }
    case AP_CFIN: {
        const carr_fin_t *f = carr_fin();
        snprintf(titre, n, "%s", CARRD.coupes[f->coupe].nom);
        if (f->med) snprintf(sous, n, "MÉDAILLE : %s", MED_NOM[f->med & 3]); else snprintf(sous, n, "PAS DE MÉDAILLE");
        if (f->ouvre[0] && f->prime) snprintf(aide, n, "%s · +%d CR", f->ouvre, f->prime);
        else if (f->ouvre[0]) snprintf(aide, n, "%s", f->ouvre);
        else if (f->prime) snprintf(aide, n, "Prime : +%d CR", f->prime);
        else snprintf(aide, n, "%s", f->med ? "Coupe gagnée" : "Le bronze ouvre la suite");
        break;
    }
    case AP_CRECOMP: {
        const carr_epr_t *e = &CARRD.epr[EPS.k < CARRD.nepr ? EPS.k : 0];
        snprintf(titre, n, "RIVAL BATTU"); snprintf(sous, n, "%s", e->nom);
        if (s == 0) snprintf(aide, n, "Son véhicule rejoint ton garage");
        else snprintf(aide, n, "Toutes les couleurs de %s", carr_veh_perso(carr_cat()) >= 0 ? nom_perso(carr_veh_perso(carr_cat())) : "ton véhicule");
        break;
    }
    case AP_CGARAGE: {
        int G[CARR_VEH]; const int ng = carr_garage_liste(G);
        snprintf(titre, n, "GARAGE");
        if (s < ng) {
            const int vi = G[s];
            const carr_veh_t *v = &CARRD.veh[vi];
            snprintf(sous, n, "%u CR · V %.1f · ACC %.1f · ADH %.2f", (unsigned)c->credits, carr_veh_stat(vi, 0), carr_veh_stat(vi, 1), carr_veh_stat(vi, 2));
            if (carr_possede(v->perso)) snprintf(aide, n, "%s", carr_cat_veh(carr_cat()) == vi ? "Toucher encore : couleur, améliorations" : "Toucher encore : le prendre");
            else if (v->prix <= c->credits) snprintf(aide, n, "Toucher encore : acheter");
            else snprintf(aide, n, "Il manque %u CR", (unsigned)(v->prix - c->credits));
        } else snprintf(sous, n, "%u CR", (unsigned)c->credits);
        break;
    }
    case AP_CVEH: {
        const int vi = VEH.i;
        snprintf(titre, n, "%s", nom_perso(CARRD.veh[vi].perso));
        snprintf(sous, n, "%u CR · V %.1f · ACC %.1f · ADH %.2f", (unsigned)c->credits, carr_veh_stat(vi, 0), carr_veh_stat(vi, 1), carr_veh_stat(vi, 2));
        if (s == 0) {
            const int nv = E.w.persos[CARRD.veh[vi].perso].nvar, vk = c->victoires[vi];
            int C[8]; const int nc = carr_couleurs(vi, C);
            if (nc > nv) snprintf(aide, n, "Toutes les livrées · %d victoire(s)", vk);
            else snprintf(aide, n, "%d victoire(s) · livrée suivante à %d", vk, nc * CARRD.livree_vict);
        } else {
            const int lv = carr_amelio(vi, s - 1);
            if (lv >= 3) snprintf(aide, n, "Au plus haut");
            else if ((uint32_t)CARRD.amelio_prix[lv] <= c->credits) snprintf(aide, n, "Toucher encore : +%.0f %% (%d CR)", CARRD.amelio_pas[s - 1] * 100, CARRD.amelio_prix[lv]);
            else snprintf(aide, n, "Il manque %u CR", (unsigned)(CARRD.amelio_prix[lv] - c->credits));
        }
        break;
    }
    case AP_CREGL:
        snprintf(titre, n, "RÉGLAGES"); snprintf(sous, n, "Pour la course et l'histoire");
        snprintf(aide, n, "%s", hud_regl_aide(s));
        break;
    case AP_CLIBRE: {
        int L[CARR_CIRCUITS]; const int nl = carr_circuits_libres(L);
        snprintf(titre, n, "COURSE LIBRE"); snprintf(sous, n, "Classe %s · hors carrière", CARRD.classes[0].nom);
        if (s == 0) snprintf(aide, n, "%d circuit(s) ouvert(s)", nl);
        else if (s == 2) snprintf(aide, n, "%s", SORTE_AIDE[LIB_SORTES[LIB.s & 3]]);
        else if (s == 5) snprintf(aide, n, "Le fantôme du contre-la-montre se garde");
        else snprintf(aide, n, "Gauche : moins · droite : plus");
        break;
    }
    }
}
/* Plein écran (téléphone, 02/11/2026) : les pages du mode course refaites pour le portrait (draw_carriere_portrait) :
   en-tête et retour en haut, lignes sur toute la largeur, aide, RETOUR et VALIDER en bas ; positions en pixels du canevas */
#define C_Y0 136.0f        /* première ligne */
#define C_Y0_AP 330.0f     /* première ligne sous l'aperçu d'un véhicule (garage) */
#define C_PAS 82.0f        /* d'une ligne à la suivante */
#define C_H 72.0f          /* hauteur d'une ligne */
#define C_PIED 230.0f      /* pied : aide, RETOUR, VALIDER */
/* Lignes visibles d'une liste (coupes, épreuves, garage) : quatre sur la montre ; plein écran, autant que la hauteur en laisse */
static int c_vis(void) {
#if ENG_DYN
    const int v = (int)((ui_h - C_PIED - 16 - (apercu_page(A.page) ? C_Y0_AP : C_Y0) + (C_PAS - C_H)) / C_PAS);
    return v < 1 ? 1 : v;
#else
    return 4;
#endif
}
/* Liste : la ligne choisie visible (c_vis lignes) */
static void liste_suivre(int nl) { const int v = c_vis(); int p = A.premier; if (A.sel < p) p = A.sel; if (A.sel > p + v - 1) p = A.sel - v + 1; if (p > nl - v) p = nl - v; A.premier = p < 0 ? 0 : p; }
/* Arrivée sur une page : lieu montré, sélection */
static void carr_entrer(int p) {
    A.page = p; A.t0 = A.t; A.down = 0; A.geste = 1; A.premier = 0;
    const carr_save_t *c = carr_cur();
    cligne_t *L = CLU;
    if (p == AP_CMENU) { carr_page_menu(); carr_infos_lire(); A.sel = 0; montrer_circuit(0); }
    else if (p == AP_CCARR) { A.sel = carr_cat(); liste_suivre(CARRD.ncats + 1); }
    else if (p == AP_CCAT) A.sel = 0;
    else if (p == AP_CCOUPES) {
        int K[CARR_COUPES]; const int nc = cat_coupes(K);
        A.sel = 0;
        int dans = 0; for (int i = 0; i < nc; i++) if (K[i] == c->coupe) { A.sel = i; dans = 1; }
        if (!dans) for (int i = 0; i < nc; i++) if (carr_ouverte(K[i]) && !c->coupe_med[K[i]]) { A.sel = i; break; }
        liste_suivre(nc);
    }
    else if (p == AP_CCOUPE) { A.sel = 4; montrer_circuit(CARRD.coupes[c->coupe].circuits[c->course < 3 ? c->course : 2]); }
    else if (p == AP_CEPRS) {
        int K[CARR_EPR]; const int ne = cat_eprs(K);
        A.sel = 0; for (int i = 0; i < ne; i++) if (K[i] == EPS.k) A.sel = i;
        liste_suivre(ne);
        if (ne) montrer_circuit(CARRD.epr[K[A.sel]].circuit);
    }
    else if (p == AP_CEPR) { A.sel = 4; montrer_circuit(CARRD.epr[EPS.k].circuit); }
    else if (p == AP_CDEFI) { const carr_defi_t *d = carr_defi(); A.sel = d->jour < 0 ? 0 : 4; if (d->jour >= 0) montrer_circuit(d->circuit); }
    else if (p == AP_CRES) { const int n = c_lignes(L); A.sel = 0; for (int i = 0; i < n; i++) if (L[i].act) { A.sel = i; break; } }
    else if (p == AP_CFIN) A.sel = 4;
    else if (p == AP_CRECOMP) A.sel = 0;
    else if (p == AP_CGARAGE) {
        int G[CARR_VEH]; const int ng = carr_garage_liste(G), vc = carr_cat_veh(carr_cat());
        A.sel = 0; for (int i = 0; i < ng; i++) if (G[i] == vc) A.sel = i;
        A.premier = A.sel > 0 ? A.sel - 1 : 0; liste_suivre(ng);
    }
    else if (p == AP_CVEH) {
        int C[8]; const int nc = carr_couleurs(VEH.i, C);
        A.sel = 0; VEH.coul = 0; for (int k = 0; k < nc; k++) if (C[k] == c->couleur[VEH.i]) VEH.coul = k;
    }
    else if (p == AP_CLIBRE) { A.sel = 5; montrer_circuit(lib_circuit()); }
    else if (p == AP_CREGL) A.sel = 0;
}
/* Une course de la carrière part (carriere.c, lancer) : l'ouverture s'efface, sans rien oublier de son état */
void accueil_course_partie(void) { A.on = 0; eng_set_zoom(A.zoom0); BT.fade_t = E.clock; }
/* Fin d'une course : l'ouverture reprend la main sur une page du mode course (résultats), le circuit derrière */
void accueil_carriere(int p) {
    if (!A.on) { A.on = 1; A.fondu = 0; A.yaw0 = E.yaw; eng_set_zoom(E.w.zoom_min); }
    carr_entrer(p);
    if (p == AP_CRES) montrer_circuit(carr_course_circuit());
}
static void carr_quitter_titre(void) {
    carr_quitter();
    page(AP_TITRE);   /* le lieu du fond, sur la carte 0 */
}
static void carr_choisir(int row) {
    cligne_t *L = CLU;
    const int n = c_lignes(L);
    carr_save_t *c = carr_cur();
    if (row < 0 || row >= n) return;
    switch (A.page) {
    case AP_CMENU:
        son_jouer(SON_VALIDER, 0.45f, 1);
        if (row == CARRIERES) { carr_entrer(AP_CLIBRE); return; }
        if (row == CARRIERES + 1) { carr_entrer(AP_CREGL); return; }
        carr_ouvrir(row); carr_entrer(AP_CCARR);
        break;
    case AP_CCARR:
        if (L[row].dim) { son_jouer(SON_ANNULER, 0.45f, 1); return; }   /* catégorie fermée, ou date inconnue */
        son_jouer(SON_VALIDER, 0.45f, 1);
        if (row < CARRD.ncats) { carr_cat_choisir(row); carr_entrer(AP_CCAT); }
        else carr_entrer(AP_CDEFI);
        break;
    case AP_CCAT:
        son_jouer(SON_VALIDER, 0.45f, 1);
        carr_entrer(!strcmp(L[row].lab, "COUPES") ? AP_CCOUPES : !strcmp(L[row].lab, "ÉPREUVES") ? AP_CEPRS : AP_CGARAGE);
        break;
    case AP_CCOUPES: {
        int K[CARR_COUPES]; const int nc = cat_coupes(K);
        if (row >= nc || !carr_ouverte(K[row])) { son_jouer(SON_ANNULER, 0.45f, 1); return; }
        son_jouer(SON_VALIDER, 0.45f, 1);
        carr_coupe_commencer(K[row]); carr_entrer(AP_CCOUPE);
        break;
    }
    case AP_CCOUPE: if (row == 4) { son_jouer(SON_VALIDER, 0.45f, 1); if (!carr_lancer_coupe()) son_jouer(SON_ANNULER, 0.45f, 1); } break;
    case AP_CEPRS: {
        int K[CARR_EPR]; const int ne = cat_eprs(K);
        if (row >= ne || !carr_epr_ouverte(K[row])) { son_jouer(SON_ANNULER, 0.45f, 1); return; }
        son_jouer(SON_VALIDER, 0.45f, 1);
        EPS.k = K[row]; EPS.niv = c->epr[EPS.k].med ? c->epr[EPS.k].niv : CARRD.epr[EPS.k].niveau;
        carr_entrer(AP_CEPR);
        break;
    }
    case AP_CEPR: if (row == 4) { son_jouer(SON_VALIDER, 0.45f, 1); if (!carr_lancer_epr(EPS.k, EPS.niv)) son_jouer(SON_ANNULER, 0.45f, 1); } break;
    case AP_CDEFI: if (row == 4) { son_jouer(SON_VALIDER, 0.45f, 1); if (!carr_lancer_defi()) son_jouer(SON_ANNULER, 0.45f, 1); } break;
    case AP_CRES:
        son_jouer(SON_VALIDER, 0.45f, 1);
        if (!strcmp(L[row].lab, "RECOMMENCER")) { carr_relancer(); return; }
        carr_entrer(carr_valider());
        break;
    case AP_CFIN: son_jouer(SON_VALIDER, 0.45f, 1); carr_entrer(AP_CCOUPES); break;
    case AP_CRECOMP: son_jouer(SON_VALIDER, 0.5f, 1); carr_recompense(row == 0); carr_entrer(AP_CEPR); break;
    case AP_CGARAGE: {
        int G[CARR_VEH]; const int ng = carr_garage_liste(G);
        if (row >= ng) return;
        const carr_veh_t *v = &CARRD.veh[G[row]];
        if (carr_possede(v->perso) && carr_cat_veh(carr_cat()) == G[row]) { son_jouer(SON_VALIDER, 0.45f, 1); VEH.i = G[row]; carr_entrer(AP_CVEH); return; }
        const int r = carr_garage_choisir(G[row]);
        son_jouer(r ? SON_VALIDER : SON_ANNULER, r == 2 ? 0.5f : 0.45f, 1);
        break;
    }
    case AP_CVEH: if (row >= 1 && row <= 3) { const int r = carr_amelio_acheter(VEH.i, row - 1); son_jouer(r ? SON_VALIDER : SON_ANNULER, r ? 0.5f : 0.45f, 1); } break;
    case AP_CLIBRE: if (row == 5) { son_jouer(SON_VALIDER, 0.45f, 1); carr_lancer_libre_s(lib_circuit(), lib_vehicule(), LIB.niv, LIB.tours, LIB_SORTES[LIB.s & 3]); } break;
    }
}
static void carr_regler(int row, int d) {
    if (A.page == AP_CREGL) { if (row < hud_regl_n()) { A.sel = row; hud_regl_changer(row, d); } return; }   /* (son : hud_regl_changer) */
    if (A.page == AP_CCOUPE) {   /* difficulté de la coupe, avant sa première course */
        carr_save_t *c = carr_cur();
        if (row != 3 || c->course) return;
        const int v = c->coupe_niv + d; c->coupe_niv = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v); carr_ecrire();
    } else if (A.page == AP_CEPR) { if (row != 3) return; EPS.niv = EPS.niv + d < 0 ? 0 : EPS.niv + d > 2 ? 2 : EPS.niv + d; }
    else if (A.page == AP_CVEH) {
        int C[8]; const int nc = carr_couleurs(VEH.i, C);
        if (row != 0) return;
        VEH.coul = ((VEH.coul + d) % nc + nc) % nc; carr_couleur(VEH.i, C[VEH.coul]);
    } else if (A.page == AP_CLIBRE) {
        int L[CARR_CIRCUITS], V[CARR_VEH];
        const int nl = carr_circuits_libres(L), nv = carr_vehicules_libres(V);
        if (row == 0 && nl) { LIB.c = (LIB.c + d + nl) % nl; LIB.tours = 0; montrer_circuit(lib_circuit()); }
        else if (row == 1 && nv) LIB.v = (LIB.v + d + nv) % nv;
        else if (row == 2) { LIB.s = (LIB.s + d + 4) % 4; LIB.tours = 0; }
        else if (row == 3) LIB.niv = LIB.niv + d < 0 ? 0 : LIB.niv + d > 2 ? 2 : LIB.niv + d;
        else if (row == 4) {
            const int s = LIB_SORTES[LIB.s & 3], t = (LIB.tours ? LIB.tours : s == CS_TOUR ? 9 : s == CS_ELIM ? 3 : CARRD.circuits[lib_circuit()].tours) + d;
            LIB.tours = t < (s == CS_ELIM ? 2 : 1) ? (s == CS_ELIM ? 2 : 1) : t > 9 ? 9 : t;
        }
    } else return;
    A.sel = row; son_jouer(SON_MENU, 0.35f, d > 0 ? 1.1f : 0.9f);
}
static void carr_retour(void) {
    son_jouer(SON_ANNULER, 0.45f, 1);
    switch (A.page) {
    case AP_CMENU: carr_quitter_titre(); break;
    case AP_CCARR: case AP_CLIBRE: carr_entrer(AP_CMENU); break;
    case AP_CCAT: carr_entrer(AP_CCARR); break;
    case AP_CDEFI: carr_entrer(AP_CCARR); A.sel = CARRD.ncats; liste_suivre(CARRD.ncats + 1); break;
    case AP_CCOUPES: case AP_CEPRS: case AP_CGARAGE: {   /* la catégorie, sur la ligne d'où l'on venait */
        const char *lab = A.page == AP_CCOUPES ? "COUPES" : A.page == AP_CEPRS ? "ÉPREUVES" : "GARAGE";
        carr_entrer(AP_CCAT);
        cligne_t *L = CLU; const int nl = c_lignes(L);
        for (int i = 0; i < nl; i++) if (!strcmp(L[i].lab, lab)) A.sel = i;
        break;
    }
    case AP_CCOUPE: carr_entrer(AP_CCOUPES); break;
    case AP_CEPR: carr_entrer(AP_CEPRS); break;
    case AP_CVEH: carr_entrer(AP_CGARAGE); break;
    case AP_CRES: carr_entrer(carr_valider()); break;
    case AP_CFIN: carr_entrer(AP_CCOUPES); break;
    case AP_CRECOMP: carr_entrer(AP_CEPR); break;
    case AP_CREGL:   /* réglages gardés s'ils ont changé ; retour à la page COURSE, ou à la pause d'une course */
        reglages_garder();
        if (RP.on) { RP.on = 0; A.page = RP.page; A.sel = RP.sel; A.premier = RP.premier; }
        else { carr_entrer(AP_CMENU); A.sel = CARRIERES + 1; }
        break;
    }
}
static void carr_action(int act, int arg) {
    cligne_t *L = CLU;
    const int n = c_lignes(L);
    if (act == B_CLIGNE) { if (arg < n && arg == A.sel && L[arg].act) carr_choisir(arg); else if (arg < n && (L[arg].act || L[arg].regle)) A.sel = arg; }
    else if (act == B_CVALIDER) { if (A.sel < n && L[A.sel].act) carr_choisir(A.sel); }
    else if (act == B_CMOINS) carr_regler(arg, -1);
    else if (act == B_CPLUS) carr_regler(arg, 1);
    if (A.page == AP_CEPRS) { int K[CARR_EPR]; const int ne = cat_eprs(K); if (A.sel < ne) montrer_circuit(CARRD.epr[K[A.sel]].circuit); }
}
static void carr_touches(uint32_t k) {
    cligne_t *L = CLU;
    const int n = c_lignes(L);
    if (!n) return;
    for (int s = -1; s <= 1; s += 2) if (k & (s > 0 ? ENG_KEY_DOWN : ENG_KEY_UP))
        for (int q = 1; q <= n; q++) { const int i = ((A.sel + s * q) % n + n) % n; if (L[i].act || L[i].regle) { A.sel = i; break; } }
    if (A.sel < n && L[A.sel].regle && (k & (ENG_KEY_LEFT | ENG_KEY_RIGHT))) carr_regler(A.sel, (k & ENG_KEY_LEFT) ? -1 : 1);
    else if ((k & ENG_KEY_OK) && A.sel < n && L[A.sel].act) carr_choisir(A.sel);
    /* la ligne choisie visible après une touche seulement (04/11/2026 : appelée à chaque image, elle ramenait la liste
       glissée au doigt sur la ligne choisie, et le défilement des RÉGLAGES ne marchait pas) */
    if (k && liste_page(A.page)) liste_suivre(c_lignes(L));
    if (A.page == AP_CEPRS && (k & (ENG_KEY_UP | ENG_KEY_DOWN))) { int K[CARR_EPR]; const int ne = cat_eprs(K); if (A.sel < ne) montrer_circuit(CARRD.epr[K[A.sel]].circuit); }

}
/* Temps de la carrière ; listes glissées au doigt (une ligne par 44 px) */
static void liste_glisser(void);
static void carr_maj(float dt) {
    carriere_temps(dt);
    liste_glisser();
}
static void liste_glisser(void) {
    if (liste_page(A.page) && A.down && !A.geste) {
        cligne_t *L = CLU;
        const int n = c_lignes(L);
        const float dy = A.cy - A.y0;
        if (fabsf(dy) > 44) { int p = A.premier + (dy < 0 ? 1 : -1); if (p > n - c_vis()) p = n - c_vis(); A.premier = p < 0 ? 0 : p; A.y0 = A.cy; A.geste = 1;
            if (A.sel < A.premier) A.sel = A.premier; else if (A.sel > A.premier + c_vis() - 1) A.sel = A.premier + c_vis() - 1; }   /* la ligne choisie suit la liste */
    }
}
static void draw_carriere(void) {
    cligne_t *L = CLB;   /* (le dessin a les siennes) */
    char titre[64], sous[96], aide[96];
    const int nl = c_lignes(L);
    c_textes(titre, sous, aide, 96);
    const int ap = apercu_page(A.page), liste = liste_page(A.page);
    vignette();
    cendres(0.3f);
    ui_text(ui_fit(FONT_DISP, titre, 320, 30, 18), CX, ap ? 86 : 112, UI_CENTER, ui_rgb(WHITE), 255, titre);
    ui_text(ui_fit(FONT_MONO, sous, 300, 20, 13), CX, ap ? 108 : 138, UI_CENTER, ui_rgb(DIM), 255, sous);
    float y0 = 156, pas = 52, h = 46, fh = 0, fb = 0;   /* flèches d'une liste : en haut, en bas */
    int debut = 0, n = nl;
    if (ap) {
        y0 = 200; pas = 44; h = 40; fh = 182; fb = 386;
        if (liste) { debut = A.premier; n = nl - debut < 4 ? nl - debut : 4; }
        int G[CARR_VEH]; const int ng = carr_garage_liste(G);
        const int vi = A.page == AP_CVEH ? VEH.i : A.sel < ng ? G[A.sel] : -1;
        const uint16_t *px; const uint8_t *mk; int w, hh;
        int C[8]; const int nc = vi >= 0 ? carr_couleurs(vi, C) : 0;
        const int var = A.page == AP_CVEH && nc ? C[VEH.coul % nc] : vi >= 0 && carr_cur() ? carr_cur()->couleur[vi] : 0;
        if (vi >= 0 && hud_apercu_vehicule_v(CARRD.veh[vi].perso, var, (int)(A.t / 0.5f) & 7, 64, &px, &mk, &w, &hh)) {
            const int big = w > hh ? w : hh;
            float s = 64.0f / (big > 0 ? big : 1); if (s > 3) s = 3;
            ui_image(px, mk, w, hh, CX - w * s / 2, 156 - hh * s / 2, s, CX, CY, 233);
        }
    } else if (liste) { y0 = 166; pas = 44; h = 40; debut = A.premier; n = nl - debut < 4 ? nl - debut : 4; fh = 152; fb = 352; }
    else if (nl > 5) { y0 = 144; pas = 38; h = 34; }
    else if (nl > 4) { y0 = 152; pas = 44; h = 40; }
    for (int i = 0; i < n; i++) {
        const int k = debut + i;
        const float y = y0 + i * pas, dy = y + h - CY, hw = sqrtf(233 * 233 - dy * dy) - 40;
        rangee(CX - hw, y, 2 * hw, h, k == A.sel && (L[k].act || L[k].regle), L[k].lab, L[k].val[0] ? L[k].val : NULL, L[k].dim ? ui_rgb(DIM) : ui_rgb(WHITE));
        if (L[k].regle) { bouton(CX - hw, y, hw, h, B_CMOINS, k); bouton(CX, y, hw, h, B_CPLUS, k); }
        else bouton(CX - hw, y, 2 * hw, h, B_CLIGNE, k);
    }
    if (liste && nl > 4) {   /* flèches de la liste */
        const float t1[6] = { CX, fh, CX - 8, fh + 8, CX + 8, fh + 8 }, t2[6] = { CX, fb, CX - 8, fb - 8, CX + 8, fb - 8 };
        ui_poly(t1, 3, C1, debut > 0 ? 255 : 64);
        ui_poly(t2, 3, C1, debut + 4 < nl ? 255 : 64);
    }
    ui_text(ui_fit(FONT_MONO, aide, 300, 18, 13), CX, ap ? 400 : 384, UI_CENTER, ui_rgb(DIM), 255, aide);
    bouton_retour();
}

#if ENG_DYN
/* Plein écran (portrait) : le titre au tiers haut, les entrées en bas, sous le pouce (maquette
   engine/maquettes/interface_portrait.html) */
static void draw_titre_portrait(void) {
    const float W = (float)ui_w, H = (float)ui_h, cx = W / 2, yt = H * 0.27f;
    cendres(0.6f);   /* voile du monde : accueil_voile, par l'ambiance du rendu */
    const ui_font_t *f = ui_fit(FONT_DISP, E.w.ouv_titre, W - 60, 40, 22);
    if (E.w.edition == 1) ui_text(ui_fit(FONT_MONO, "ÉDITION ENFANT", 220, 16, 12), cx, yt - 60, UI_CENTER, C1, 255, "ÉDITION ENFANT");
    ui_text(f, cx + 2, yt + 2, UI_CENTER, ui_rgb(INK), 200, E.w.ouv_titre);
    ui_text(f, cx, yt, UI_CENTER, ui_rgb(WHITE), 255, E.w.ouv_titre);
    ui_line(cx - 170, yt + 18, cx + 170, yt + 18, 3, C2, 255);
    if (E.w.ouv_sous[0]) ui_text(ui_fit(FONT_MONO, E.w.ouv_sous, W - 60, 20, 14), cx, yt + 46, UI_CENTER, C1, 255, E.w.ouv_sous);
    static const char *const NOM[6] = { "CONTINUER", "NOUVELLE PARTIE", "CHARGER", "RÉGLAGES", "APPLICATIONS", "COURSE" };
    const float h = 84, gap = 16, w = W - 140, y0 = H - 150 - A.nopts * h - (A.nopts - 1) * gap;
    for (int i = 0; i < A.nopts; i++) {
        const float y = y0 + i * (h + gap);
        rangee(cx - w / 2, y, w, h, i == A.sel, nom_option(NOM, A.opts[i]), NULL, ui_rgb(WHITE));
        bouton(cx - w / 2, y, w, h, B_OPT, i);
    }
    char t[128], d[32];
    const int o = A.opts[A.sel], p = derniere_partie();
    if (o == AC_CONTINUER && p >= 0) { duree_txt(A.info[p].temps, d, sizeof d); snprintf(t, sizeof t, "Partie %d · %s · niv. %d · %s", p + 1, A.info[p].lieu, A.info[p].niveau, d); }
    else if (o == AC_NOUVELLE) snprintf(t, sizeof t, "%s", E.w.ouv_ncartes ? "L'histoire du monde, puis ton héroïne" : "Choisir son héroïne");
    else if (o == AC_REGLAGES) snprintf(t, sizeof t, "%s", E.cfg.reglages_aide ? E.cfg.reglages_aide : "Serveur, édition, monde à jour");
    else if (o == AC_APPS) snprintf(t, sizeof t, "Les autres programmes de la montre");
    else if (o == AC_COURSE) snprintf(t, sizeof t, "Carrière et course libre, sur les circuits");
    else snprintf(t, sizeof t, "Reprendre une des %d parties", PARTIES);
    ui_text(ui_fit(FONT_MONO, t, W - 60, 18, 14), cx, H - 90, UI_CENTER, ui_rgb(DIM), 255, t);
    if (A.t < T_TITRE) voile_image(ui_rgb(0), (int)(255 * (1 - A.t / T_TITRE)));
}
/* Ligne du mode course en plein écran : toute la largeur ; réglable (regle) : flèches aux deux bouts, la valeur à droite
   (« < > » des lignes de la course libre : les flèches suffisent) */
static void rangee_p(float y, int sel, const cligne_t *l) {
    const float x = 24, w = (float)ui_w - 48, h = C_H;
    const uint16_t col = sel ? C2 : C1, lc = l->dim ? ui_rgb(DIM) : ui_rgb(WHITE);
    float q[12]; ui_chamfer_path(x, y, w, h, 10, q);
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.24f : 0.10f), 238);
    ui_outline(q, 6, sel ? 2 : 1, col, sel ? 255 : 128);
    const float m = l->regle ? 44 : 16;   /* marge des textes : place des flèches */
    if (l->regle) for (int sg = -1; sg <= 1; sg += 2) {
        const float ax = sg < 0 ? x + 22 : x + w - 22, ay = y + h / 2;
        const float tri[6] = { ax + sg * 8, ay, ax - sg * 6, ay - 11, ax - sg * 6, ay + 11 };
        ui_poly(tri, 3, C1, l->dim ? 70 : 255);
    }
    const char *val = l->val[0] && strcmp(l->val, "< >") ? l->val : NULL;
    const float vw = val ? ui_text_width(ui_font(FONT_MONO, 20), val) + 16 : 0;
    if (val) ui_text(ui_font(FONT_MONO, 20), x + w - m, y + h / 2 + 7, UI_RIGHT, col, 255, val);
    if (val || l->regle) ui_text(ui_fit(FONT_DISP, l->lab, w - vw - 2 * m, 22, 16), x + m, y + h / 2 + 8, UI_LEFT, lc, 255, l->lab);
    else ui_text(ui_fit(FONT_DISP, l->lab, w - 32, 22, 16), x + w / 2, y + h / 2 + 8, UI_CENTER, lc, 255, l->lab);
}
/* Pages du mode course en plein écran (02/11/2026 ; avant : celles de la montre dans un cadre de 466 × 466 centré) : en
   haut, ‹ et le titre (toucher : retour), le sous-titre ; l'aperçu du véhicule (garage) ; les lignes sur toute la largeur
   (toucher : choisir, toucher encore : valider ; réglables : moitié gauche moins, moitié droite plus ; listes glissées au
   doigt) ; en bas, l'aide, RETOUR et VALIDER (la ligne choisie). Mêmes lignes et mêmes actions que la montre (c_lignes) */
static void draw_carriere_portrait(void) {
    cligne_t *L = CLB;   /* (le dessin a les siennes) */
    char titre[64], sous[96], aide[96];
    const int nl = c_lignes(L);
    c_textes(titre, sous, aide, 96);
    const float W = (float)ui_w, H = (float)ui_h;
    const int ap = apercu_page(A.page), liste = liste_page(A.page);
    /* En-tête */
    ui_icon("back", 36, 44, 26, C1, 255);
    ui_icon(A.page == AP_CREGL ? "sys" : "flag", 68, 44, 26, C2, 255);
    ui_text(ui_fit(FONT_DISP, titre, W - 120, 30, 18), 94, 54, UI_LEFT, ui_rgb(WHITE), 255, titre);
    bouton(12, 8, 300, 76, B_RETOUR, 0);
    if (sous[0]) ui_text(ui_fit(FONT_MONO, sous, W - 48, 20, 14), 24, 100, UI_LEFT, ui_rgb(DIM), 255, sous);
    /* Aperçu du véhicule (garage : celui de la ligne choisie ; un véhicule à soi : dans sa couleur), qui tourne */
    if (ap) {
        int G[CARR_VEH]; const int ng = carr_garage_liste(G);
        const int vi = A.page == AP_CVEH ? VEH.i : A.sel < ng ? G[A.sel] : -1;
        const uint16_t *px; const uint8_t *mk; int w, hh;
        int C[8]; const int nc = vi >= 0 ? carr_couleurs(vi, C) : 0;
        const int var = A.page == AP_CVEH && nc ? C[VEH.coul % nc] : vi >= 0 && carr_cur() ? carr_cur()->couleur[vi] : 0;
        if (vi >= 0 && hud_apercu_vehicule_v(CARRD.veh[vi].perso, var, (int)(A.t / 0.5f) & 7, 96, &px, &mk, &w, &hh)) {
            const int big = w > hh ? w : hh;
            float s = 176.0f / (big > 0 ? big : 1); if (s > 4) s = 4;
            ui_image(px, mk, w, hh, W / 2 - w * s / 2, 222 - hh * s / 2, s, W / 2, 222, W + H);
        }
    }
    /* Lignes (listes : celles qui tiennent, flèches au-dessus et au-dessous quand il y en a d'autres) */
    const float y0 = ap ? C_Y0_AP : C_Y0;
    const int vis = c_vis(), debut = liste ? A.premier : 0, n = liste ? (nl - debut < vis ? nl - debut : vis) : nl;
    for (int i = 0; i < n; i++) {
        const int k = debut + i;
        const float y = y0 + i * C_PAS;
        rangee_p(y, k == A.sel && (L[k].act || L[k].regle), &L[k]);
        if (L[k].regle) { bouton(24, y, (W - 48) / 2, C_H, B_CMOINS, k); bouton(W / 2, y, (W - 48) / 2, C_H, B_CPLUS, k); }
        else bouton(24, y, W - 48, C_H, B_CLIGNE, k);
    }
    if (liste && nl > vis) {
        const float fh = y0 - 6, fb = y0 + vis * C_PAS - (C_PAS - C_H) + 6;
        const float t1[6] = { W / 2, fh - 8, W / 2 - 10, fh + 2, W / 2 + 10, fh + 2 }, t2[6] = { W / 2, fb + 8, W / 2 - 10, fb - 2, W / 2 + 10, fb - 2 };
        ui_poly(t1, 3, C1, debut > 0 ? 255 : 64);
        ui_poly(t2, 3, C1, debut + vis < nl ? 255 : 64);
    }
    /* Pied : l'aide (deux lignes au plus), RETOUR, VALIDER (la ligne choisie, si elle se valide) */
    ui_rect(0, H - C_PIED, W, C_PIED, ui_rgb(0x0E0A1E), 245);
    ui_line(0, H - C_PIED, W, H - C_PIED, 2, C1, 255);
    if (aide[0]) {
        char l[6][128];
        const ui_font_t *f = ui_font(FONT_MONO, 20);
        const int na = couper(f, aide, W - 48, l);
        for (int i = 0; i < na && i < 2; i++) ui_text(f, 24, H - C_PIED + 40 + i * 26, UI_LEFT, ui_rgb(DIM), 255, l[i]);
    }
    const float bw = (W - 48 - 14) / 2, by = H - 40 - 84;
    rangee(24, by, bw, 84, 0, "RETOUR", NULL, ui_rgb(DIM));
    ui_icon("back", 24 + 30, by + 42, 26, ui_rgb(DIM), 255);
    bouton(24, by, bw, 84, B_RETOUR, 0);
    const int ok = A.sel < nl && L[A.sel].act;
    if (ok) {
        const char *lab = !strcmp(L[A.sel].lab, "DÉPART") || !strcmp(L[A.sel].lab, "CONTINUER") || !strcmp(L[A.sel].lab, "RECOMMENCER") ? L[A.sel].lab : "VALIDER";
        rangee(24 + bw + 14, by, bw, 84, 1, lab, NULL, ui_rgb(WHITE));
        bouton(24 + bw + 14, by, bw, 84, B_CVALIDER, 0);
    }
}
#endif

void accueil_build(void) {
    nab = 0;
    C1 = ui_rgb(hud_theme_c1()); C2 = ui_rgb(hud_theme_c2());
#if ENG_DYN
    const float W = (float)ui_w, H = (float)ui_h;
    /* Plein écran : le titre et les pages du mode course refaits pour le portrait ; les autres pages dans un cadre de
       466 × 466 centré, sur un fond qui couvre l'image */
    if (A.page == AP_TITRE) draw_titre_portrait();
    else if (A.page >= AP_CMENU) draw_carriere_portrait();
    else {   /* fond : accueil_voile (pages opaques sans 3D, monde assombri derrière l'héroïne) */
        ui_translate((W - ENG_SCREEN) / 2, (H - ENG_SCREEN) / 2);
    }
    if (A.page != AP_TITRE && A.page < AP_CMENU)
#endif
    switch (A.page) {
    case AP_TITRE: draw_titre(); break;
    case AP_PARTIES: draw_parties(); break;
    case AP_INTRO: draw_intro(); break;
    case AP_HEROINE: draw_heroine(); break;
    case AP_APPS: draw_apps(); break;
    case AP_REGL: draw_regl(); break;
    default: if (A.page >= AP_CMENU) draw_carriere(); break;
    }
    ui_translate(0, 0);
    if (A.fondu) voile_image(ui_rgb(0), (int)(255 * clampf((A.t - A.ft0) / T_FONDU, 0, 1)));
    if (E.stream_hold && A.page != AP_PARTIES && A.page != AP_APPS && A.page != AP_REGL && A.page < AP_CMENU && !(A.page == AP_INTRO && E.w.ouv_cartes[A.carte].x < 0)) voile_image(ui_rgb(0), 255);   /* chunks en route : noir */
}

/* ---------- Page RÉGLAGES depuis la pause d'une course (RP) ---------- */
int accueil_regl_on(void) { return RP.on; }
void accueil_regl_ouvrir(void) {
    if (RP.on) return;
    RP.on = 1; RP.page = A.page; RP.sel = A.sel; RP.premier = A.premier;
    carr_entrer(AP_CREGL);
    A.prev = 0xFFFFFFFFu;   /* la touche qui l'a ouverte, encore tenue, n'agit pas */
}
/* Entrées (celles de accueil_update pour une page du mode course : toucher une ligne, glisser la liste, touches) ; retour,
   ou la touche menu : la pause */
void accueil_regl_update(float dt, const eng_input_t *in) {
    (void)dt;
    if (in->touch && !A.down) { A.down = 1; A.geste = 0; A.x0 = A.cx = in->touch_x; A.y0 = A.cy = in->touch_y; A.dt0 = A.t; }
    else if (in->touch) { A.cx = in->touch_x; A.cy = in->touch_y; }
    liste_glisser();
    if (A.down && !in->touch) {
        A.down = 0;
        if (!A.geste && hypotf(A.cx - A.x0, A.cy - A.y0) < 20)
            for (int i = 0; i < nab; i++)
                if (A.cx >= AB[i].x && A.cx <= AB[i].x + AB[i].w && A.cy >= AB[i].y && A.cy <= AB[i].y + AB[i].h) { action(AB[i].act, AB[i].arg); break; }
    }
    if (!RP.on) return;
    const uint32_t k = in->keys & ~A.prev;
    A.prev = in->keys;
    if (k & (ENG_KEY_BACK | ENG_KEY_MENU)) { retour(); return; }
    carr_touches(k);
}
/* Dessin par-dessus la pause : un voile (pas celui du rendu, accueil_voile : l'ouverture n'est pas là), puis la page */
void accueil_regl_build(void) {
    nab = 0;
    C1 = ui_rgb(hud_theme_c1()); C2 = ui_rgb(hud_theme_c2());
    voile_image(ui_rgb(INK), 222);
#if ENG_DYN
    draw_carriere_portrait();
#else
    draw_carriere();
#endif
}
