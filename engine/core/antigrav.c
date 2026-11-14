/*
 * AntiGraV, course anti-gravité : variante « façon PS1 » du moteur (13/11/2026, demande de l'utilisateur : un jeu de course
 * anti-gravité du genre, avec ses mécaniques, sur des dessins originaux ; app à part, LDK Game et PC d'abord). Conception du jeu : docs/course-anti-gravite.md ; technique : engine/README.md, « Course anti-gravité ».
 *
 * Le paquet (tools/antigrav/generer.js) n'a que le jeu : ses pistes à thème (une en mémoire à la fois : piste_charger, avec la
 * palette de son thème, son ciel et son horizon), ses engins texturés, ses écuries, plus les polices, les icônes et les sons
 * d'Echoes. Pas de monde en cases : la piste est un ruban en 3D (une section par case de longueur : centre, droite avec le dévers,
 * demi-largeur) au-dessus d'un sol plat ; render.c la dessine (ag_image).
 *
 * Physique (physique, à pas fixe AG_PAS) : l'engin plane à AG_HOVER au-dessus de la piste et la suit (montées, descentes, dévers) ;
 * sa vitesse est dans le plan de la piste, distincte de son cap (il dérive) ; poussée, traînée, gravité le long de la pente ;
 * aérofreins gauche et droit (ENG_KEY_ROT_L, ROT_R : gâchettes L et R de la LDK, Q et E sur PC) qui serrent le virage de leur
 * côté et freinent ; murs au bord de la piste (rebond, vitesse perdue, secousse) ; plaques d'accélération ; adversaires sur une
 * trajectoire tirée de la courbure (ligne des sections), au niveau de leur écurie et de la difficulté, rapprochés du joueur.
 *
 * Écrans (ag_hud) : titre (démonstration), menu principal (course rapide, championnat, contre-la-montre, réglages), écurie (vitrine :
 * l'engin tourne sur lui-même), piste (plan, lieu, record ; la démonstration sur cette piste), réglages (mixage comme SYSTÈME › SON
 * d'Echoes, tours, adversaires, difficulté, tramage ; sauvegardés), départ, course (carte de la piste), arrivée, classement du
 * championnat. Pause : START.
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "internal.h"
#include "audio.h"
#include "ui.h"

ag_t AG ENG_FROID;   /* (en PSRAM sur l'ESP32 : la SRAM interne est comptée ; la montre ne joue pas la course anti-gravité) */

#define AG_HOVER 0.30f        /* hauteur de l'engin au-dessus de la piste (cases) */
#define AG_R 0.42f            /* demi-largeur d'un engin contre les murs ; deux engins se touchent à 2 × AG_R */
#define AG_VMAX 18.0f         /* vitesse maxi d'une écurie moyenne (cases/s ; une case ≈ 5 m : 324 km/h ; 15 jusqu'au 14/11/2026, demande de
                                 l'utilisateur : « accélérons la vitesse du jeu, il est trop facile ») */
#define AG_ACC 10.2f          /* poussée (cases/s²) : de 0 à 95 % de la vitesse maxi en 3 s environ */
#define AG_TRAINEE 0.45f      /* sans poussée : part de la vitesse perdue par seconde */
#define AG_VIRAGE 2.0f        /* rotation au plus (rad/s) à l'arrêt ; à la vitesse maxi, × (1 - AG_VIRAGE_VITE) */
#define AG_VIRAGE_VITE 0.32f
#define AG_VIRAGE_VIF 7.0f    /* la rotation rejoint la voulue à ce rythme (par seconde) */
#define AG_FREIN_VIRAGE 1.05f /* rotation de plus d'un aérofrein (rad/s), vers son côté */
#define AG_FREIN 0.42f        /* part de la vitesse perdue par seconde, par aérofrein */
#define AG_ADHERENCE 3.4f     /* vitesse de côté rattrapée par seconde (dérive) */
#define AG_FREIN_ADH 0.55f    /* un aérofrein serré : adhérence × AG_FREIN_ADH (on glisse dans le virage) */
#define AG_GRAVITE 7.0f       /* le long de la pente (cases/s²) */
#define AG_BOOST_T 1.3f       /* plaque d'accélération : durée (s), vitesse maxi × AG_BOOST_V, élan immédiat (cases/s) */
#define AG_BOOST_V 1.3f
#define AG_BOOST_ELAN 3.0f
#define AG_REBOND 0.35f       /* mur : part de la vitesse vers le mur renvoyée */
#define AG_KMH 18.0f          /* km/h par case/s (affichage) */
#define AG_ROULIS 0.38f       /* roulis montré : volant à fond (rad) ; aérofrein : AG_ROULIS_FREIN de plus */
#define AG_ROULIS_FREIN 0.22f
#define AG_ELASTIQUE 0.05f    /* les adversaires accélèrent ou ralentissent de 5 % au plus selon leur écart au joueur */
#define AG_DEMO_T 9.0f        /* démonstration : la caméra change d'engin suivi toutes les AG_DEMO_T s */
#define AG_CAM_AV 0.8f        /* caméra : point visé AG_CAM_HAUT au-dessus de l'engin et AG_CAM_AV devant, distance (plus AG_CAM_DV par
                                 case/s), pente ; 14/11/2026, retours de l'utilisateur : « la vue extérieure est trop éloignée » (1,1, 3,6 et
                                 0,04 avant), puis « plus près du sol pour améliorer les i/s, presque le même angle qu'en cockpit, juste
                                 légèrement relevé pour voir un peu au-dessus du vaisseau » (0,9, 0,3, 2,7, 0,025 et 0,22 avant) */
#define AG_CAM_HAUT 0.36f
#define AG_CAM_D 2.6f        /* (1,4 et 2,2 au premier essai : caméra à 0,8 case derrière le centre de l'engin, coupé en bas de l'image) */
#define AG_CAM_DV 0.012f
#define AG_CAM_PITCH 0.075f
/* Vue cockpit (14/11/2026, demande de l'utilisateur : « une vue cockpit, ça économiserait des polygones, et on pourrait dessiner ces
   cockpits avec Forge ») : l'œil du pilote à AG_CK_HAUT au-dessus du centre de l'engin et AG_CK_AV devant, le regard dans l'axe de
   l'engin (cap et pente, lissés à AG_CK_VIF par seconde), baissé de AG_CK_PITCH ; l'engin du joueur n'est pas dessiné (render.c) et le
   cadre du cockpit de son écurie (AGCK, images de Forge : tools/antigrav/forge_images.py cockpits) cache le bas de l'image */
#define AG_CK_HAUT 0.17f
#define AG_CK_AV 0.12f
#define AG_CK_PITCH 0.07f
#define AG_CK_VIF 14.0f
/* Roulis de la caméra en vue cockpit (render.c, ENG_ROULIS : versions de la course seulement) : le haut de l'engin (dévers de la
   piste) en haut de l'image, plus AG_CK_PENCHE du roulis montré de l'engin dans les virages ; AG_CK_ROULIS_MAX au plus */
#define AG_CK_PENCHE 0.5f
#define AG_CK_ROULIS_MAX 0.8f
#define AG_SAUVE 0            /* emplacement de la sauvegarde du jeu (réglages, records) */
/* Catégories de vitesse (14/11/2026, demande de l'utilisateur : « au moins 5 catégories, la plus rapide 2x plus rapide que l'actuelle »,
   l'actuelle trop rapide pour un enfant) : vitesse maxi, poussée, virage et aérofreins (× ceux de l'écurie) ; FLAMME : la vitesse du
   14/11 (AG_VMAX, 324 km/h) ; dans les deux plus rapides, virage plus vif (sinon les virages ne passent plus) */
static const struct { const char *nom; float v, a, w; } CLASSES[AG_CLASSES] = {
    { "CENDRE", 0.55f, 0.70f, 1.00f }, { "BRAISE", 0.75f, 0.85f, 1.00f }, { "FLAMME", 1.00f, 1.00f, 1.00f },
    { "FOUDRE", 1.45f, 1.35f, 1.20f }, { "NOVA", 2.00f, 1.80f, 1.40f } };
#define CL (CLASSES[AG.classe < 0 || AG.classe >= AG_CLASSES ? 2 : AG.classe])
#define RECORD(p) (AG.cond == AGC_INVERSE ? AG.record_inv : AG.record)[AG.classe][(p)]   /* (sens inverse : ses records à part) */
/* Armes (14/11/2026, demandes de l'utilisateur : « commencer à créer les armes », puis « les autres armes, missiles guidés, mines,
   rayon destructeur à chargement, etc. ») : ramassées sur les plaques d'armes, une à la fois, tirées au sort (arme_tirer : loin
   derrière, plus souvent un missile, le rayon ou le séisme ; en tête, des mines ou un bouclier) ; X de la LDK (ENG_KEY_ALT_UP ;
   « Arme » sur PC) s'en sert, le rayon se charge tant qu'on tient X ; Y (ENG_KEY_ALT_DOWN ; « Absorber » sur PC) change l'arme
   portée en énergie (AG_ABSORBE). Les adversaires s'en servent (ia_armes). Contre-la-montre : pas d'armes.
   SÉISME : une onde part AG_ONDE_AV devant le tireur et file le long de la piste à AG_ONDE_V fois la vitesse maxi de la catégorie, une
   bosse de AG_ONDE_L cases qui monte à AG_ONDE_A sur ses AG_ONDE_MONTE premières cases et s'éteint en AG_ONDE_D ; la piste se soulève
   sous elle (render.c, ag_onde_dep) ; les engins montent avec la chaussée soulevée, qui les lance en l'air (sa vitesse de montée, au
   plus AG_ONDE_LANCE ; retombée AG_SAUT_G) ; celui qu'elle rattrape perd jusqu'à AG_ONDE_FREIN de sa vitesse et AG_ONDE_DEG d'énergie.
   MISSILE : guidé le long de la piste vers l'engin le plus proche devant. MINES : AG_MINES_N posées derrière soi. RAYON : chargé
   jusqu'à AG_RAYON_CHARGE, un trait vers le premier engin dans l'axe de la piste devant. BOUCLIER : AG_BOUCLIER_T sans dégâts. TURBO :
   une accélération plus longue et plus forte qu'une plaque.
   Énergie (« la vie pour chaque vaisseau avec la destruction ») : 1 au départ ; murs (au-delà de AG_DEG_MUR_SEUIL cases/s vers le mur),
   frottement, chocs entre engins, armes, en course seulement ; dégâts du joueur × DEG_DIFF selon la difficulté ; à 0, l'engin explose
   (éclats, boule de feu, SON_AG_EXPLOSION) et sort de la course ; le joueur détruit : fin de sa course */
enum { ARME_AUCUNE, ARME_SEISME, ARME_MISSILE, ARME_MINES, ARME_RAYON, ARME_BOUCLIER, ARME_TURBO, ARME_N };
static const char *const ARMES[ARME_N] = { "", "SÉISME", "MISSILE", "MINES", "RAYON", "BOUCLIER", "TURBO" };
#define AG_DUEL_NIVEAU 0.05f   /* le champion d'un duel : plus rapide que les adversaires de la catégorie */
#define AG_ASC_K 1.05f         /* ascension : vitesse maxi × par palier (quart de tour) ; l'épreuve finit à l'or */
#define AG_ASC_KA 1.03f        /* … et poussée */
#define AG_AMEL_V 0.025f       /* garage, par cran : vitesse maxi, poussée, maniabilité, dégâts en moins */
#define AG_AMEL_A 0.06f
#define AG_AMEL_M 0.05f
#define AG_AMEL_B 0.12f
static void onde_tirer(ag_nav_t *n);
/* Tours selon la catégorie (14/11/2026, demande de l'utilisateur : « moins de tours dans les classes lentes, sinon les courses sont trop
   longues ») : la base (réglage TOURS, tours d'une épreuve) × TOURS_K, arrondie, 1 au moins ; avec 3 : 2, 2, 3, 4, 5 tours, des
   courses de 1,5 à 2,5 min */
static const float TOURS_K[AG_CLASSES] = { 0.67f, 0.67f, 1.0f, 1.34f, 1.67f };
static int tours_classe(int base) { const int t = (int)((float)base * TOURS_K[AG.classe >= 0 && AG.classe < AG_CLASSES ? AG.classe : 2] + 0.5f); return t < 1 ? 1 : t; }
/* Second engin d'une écurie (AGE2) : un peu plus rapide, moins maniable */
#define AG_VAR_V 1.02f
#define AG_VAR_A 0.98f
#define AG_VAR_M 0.95f
static void eclats_lancer(float x, float y, float z, const ag_nav_t *n, int nb, float vit);
static void carriere_charger(void);
static int classe_ouverte(int c);
static void tour_reference_calculer(void);
static void elimination_maj(void);
static int medaille_calculer(void);
static int medaille_coupe(void);
static void bilan_faire(int medaille);
static void epreuve_lancer(int l, int e, int m);
static int coupe_courses(const ag_epreuve_t *Ep);
static void calendrier_voir(void);
static void carriere_maj(const eng_input_t *in, float dt);
static void calendrier_maj(const eng_input_t *in, float dt);
static void ag_garage_maj(const eng_input_t *in, float dt);
static void epreuve_nom(const ag_epreuve_t *Ep, char *b, int n, int m);
static int champ_manche(void);
static int champ_total(void);
static void ecran_carriere(void);
static void ecran_calendrier(void);
static void ecran_garage(void);
static void ecran_bilan(void);
static int piste_ouverte(int k);
static int ecurie_ouverte(int e);
static void ondes_maj(float dt);
static void degats(ag_nav_t *n, float x);
static int arme_tirer(const ag_nav_t *n);
static void arme_utiliser(ag_nav_t *n);
static void absorber(ag_nav_t *n);
static void ia_armes(ag_nav_t *n, float dt);
static void armes_maj(float dt);
static void son_ici(const ag_nav_t *n, int son, float vol, float hauteur);
static float onde_haut(int k);
#define AG_ONDE_AV 8.0f      /* (2 au premier essai : la bosse naissait sous la caméra du tireur et lui bouchait la vue) */
#define AG_ONDE_MONTE 8.0f   /* chemin sur lequel la bosse monte à sa hauteur */
#define AG_ONDE_V 3.2f       /* (2 au premier essai : « l'attaque séisme est trop lente ») */
#define AG_ONDE_L 10.0f
#define AG_ONDE_A 1.2f
#define AG_ONDE_D 170.0f
#define AG_ONDE_FREIN 0.7f     /* (0,45 et 0,14 jusqu'au 14/11/2026 au soir : « pratiquement aucun effet sur les concurrents ») */
#define AG_ONDE_DEG 0.2f
#define AG_ONDE_FORCE 0.75f    /* force du coup au plus loin de l'onde (part de la pleine force ; elle baissait avec la hauteur de l'onde) */
#define AG_ONDE_ETOURDI 1.4f   /* engin désemparé : ni poussée ni direction, vrille vers un mur */
#define AG_ONDE_LANCE 6.5f   /* vitesse verticale que la chaussée soulevée donne au plus (cases/s) */
#define AG_SAUT_G 16.0f
#define AG_MISSILE_V 1.3f    /* × vitesse maxi de la catégorie, en plus de celle du tireur */
#define AG_MISSILE_T 4.0f
#define AG_MISSILE_GUIDE 5.0f   /* cases/s de côté vers sa cible */
#define AG_MISSILE_PORTEE 80.0f
#define AG_MISSILE_DEG 0.20f
#define AG_MINES_N 5
#define AG_MINE_T 45.0f
#define AG_MINE_ARME 0.8f    /* une mine ne touche pas son poseur avant (s) */
#define AG_MINE_DEG 0.14f
#define AG_RAYON_PORTEE 45.0f
#define AG_RAYON_DEG 0.12f   /* plus AG_RAYON_DEG_PLEIN × la charge (0,16 et 0,30 au premier essai : deux tirs chargés détruisaient) */
#define AG_RAYON_DEG_PLEIN 0.22f
#define AG_BOUCLIER_T 5.0f
#define AG_DEG_MUR 0.011f    /* par case/s vers le mur au-delà du seuil */
#define AG_DEG_MUR_SEUIL 3.0f
#define AG_DEG_FROTTE 0.015f /* par seconde contre un mur */
#define AG_RECHARGE_TOUR 0.15f   /* énergie rendue à chaque tour bouclé */
#define AG_MINE_IMMUN 1.0f   /* une grappe de mines ne touche qu'une fois (sinon jusqu'à quatre mines d'un coup) */
#define AG_DEG_CHOC 0.006f
#define AG_ABSORBE 0.22f
static const float DEG_DIFF[3] = { 0.55f, 1.0f, 1.3f };   /* dégâts du joueur selon la difficulté */
static const float NIVEAU_DIFF[3] = { 0.92f, 0.965f, 1.0f };   /* (0,88, 0,93, 0,975 jusqu'au 14/11/2026 : trop facile) */   /* vitesse des adversaires (× celle de leur écurie) selon la difficulté */
static const int POINTS[6] = { 10, 6, 4, 3, 2, 1 };             /* championnat : points des six premiers */

/* ---------- Petit outillage ---------- */
static inline uint32_t rd32a(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint16_t rd16a(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline float rdfa(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float wrapf(float a) { return atan2f(sinf(a), cosf(a)); }
static float norm3(float *v) { const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; } return l; }
static inline void cross3(const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static inline float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static uint32_t ag_rng = 0x9E3779B9u;
static float hasard(void) { ag_rng ^= ag_rng << 13; ag_rng ^= ag_rng >> 17; ag_rng ^= ag_rng << 5; return (float)(ag_rng >> 8) * (1.0f / 16777216.0f); }
static void copier_nom(char *dst, const uint8_t *src, int n) { memcpy(dst, src, (size_t)n); dst[n] = 0; }

static const uint8_t *sect(const uint8_t *pak, size_t len, const char *tag, uint32_t *size) {
    size_t o = 8;
    while (o + 8 <= len) {
        const uint32_t l = rd32a(pak + o + 4);
        if (!memcmp(pak + o, tag, 4)) { if (size) *size = l; return pak + o + 8; }
        o += 8 + l;
    }
    return NULL;
}
/* Images à palette (AGHZ, AGEM) : largeur, hauteur, ligne d'horizon, couleurs u16, couleurs × 3, pixels (bourrage à 4 octets) ;
   *q avance d'une image. La palette de l'interface (pal) : ui_rgb ; celle des horizons est refaite par render.c (le brouillard) */
static int image_lire(const uint8_t **q, const uint8_t *fin, ag_image_t *im) {
    const uint8_t *p = *q;
    if (p + 8 > fin) return 0;
    im->w = rd16a(p); im->h = rd16a(p + 2); im->base = rd16a(p + 4); im->ncoul = rd16a(p + 6);
    if (im->w < 8 || im->h < 1 || im->ncoul > 255) return 0;
    const size_t nc = ((size_t)im->ncoul * 3 + 3) & ~(size_t)3, npx = ((size_t)im->w * im->h + 3) & ~(size_t)3;
    if (p + 8 + nc + npx > fin) return 0;
    im->rgb = p + 8; im->px = p + 8 + nc;
    im->pal[0] = 0;
    for (int i = 0; i < 255; i++) im->pal[i + 1] = i < im->ncoul ? ui_rgb(((uint32_t)im->rgb[i * 3] << 16) | (im->rgb[i * 3 + 1] << 8) | im->rgb[i * 3 + 2]) : 0;
    *q = p + 8 + nc + npx;
    return 1;
}

/* ---------- Réglages et records : sauvegarde du jeu ---------- */
#define SAUVE_MAGIE 0x31524741u   /* « AGR1 » */
static void reglages_appliquer(void) {
    eng_set_volume(AG.reg.vol[0] / 20.0f);
    for (int c = 0; c < CANAUX; c++) audio_canal(c, AG.reg.vol[c + 1] / 20.0f);
    E.dither_on = AG.reg.tramage;
}
/* ---------- Carrière : avancement (sauvegarde AG_SAUVE + 2, « AGK1 ») et ce qui est ouvert ---------- */
#define CARRIERE_MAGIE 0x314B4741u
static void carriere_defaut(void) {
    ag_carriere_t *C = &AG.car;
    memset(C, 0, sizeof *C);
    C->ecuries = 3;   /* HALCYON, ORBITA */
    if (AG.nligues) { C->classes = (uint8_t)(1 << AG.ligues[0].classe); C->pistes = AG.ligues[0].pistes; C->armes = AG.ligues[0].armes; }
}
static void carriere_sauver(void) {
    if (!E.cfg.save_write) return;
    uint8_t b[8 + sizeof(ag_carriere_t)];
    const uint32_t m = CARRIERE_MAGIE;
    memset(b, 0, 8); memcpy(b, &m, 4); memcpy(b + 8, &AG.car, sizeof AG.car);
    E.cfg.save_write(AG_SAUVE + 2, b, (int)sizeof b);
}
static void carriere_charger(void) {
    carriere_defaut();
    uint8_t b[8 + sizeof(ag_carriere_t)];
    uint32_t m = 0;
    if (E.cfg.save_read && E.cfg.save_read(AG_SAUVE + 2, b, (int)sizeof b) == (int)sizeof b && (memcpy(&m, b, 4), m == CARRIERE_MAGIE)) {
        memcpy(&AG.car, b + 8, sizeof AG.car);
        ag_carriere_t *C = &AG.car;
        if (C->ligue_max >= AG_LIGUES_MAX) C->ligue_max = 0;
        if (C->ecurie >= AG_ECURIES_MAX) C->ecurie = 0;
        for (int i = 0; i < 4; i++) if (C->ameliore[i] > 3) C->ameliore[i] = 3;
        C->ecuries |= 3;
    }
}
static int piste_ouverte(int k) { return !AG.nligues || (k >= 0 && k < 16 && (AG.car.pistes >> k & 1)); }
static int classe_ouverte(int c) { return !AG.nligues || (c >= 0 && c < AG_CLASSES && (AG.car.classes >> c & 1)); }
static int ecurie_ouverte(int e) { return !AG.nligues || (e >= 0 && e < 8 && (AG.car.ecuries >> e & 1)); }
static void sauver(void) {
    if (!E.cfg.save_write) return;
    uint8_t b[16 + sizeof(ag_reglages_t) + 4 * AG_PISTES_MAX];
    memset(b, 0, sizeof b);
    const uint32_t m = SAUVE_MAGIE;
    memcpy(b, &m, 4); b[4] = (uint8_t)(AG.vue ? 1 : 0); b[5] = (uint8_t)(AG.classe + 1); b[6] = (uint8_t)(AG.sens ? 1 : 0); b[7] = (uint8_t)(AG.variante ? 1 : 0);
    memcpy(b + 16, &AG.reg, sizeof AG.reg); memcpy(b + 16 + sizeof AG.reg, AG.record[2], sizeof AG.record[2]);
    E.cfg.save_write(AG_SAUVE, b, (int)sizeof b);
    uint8_t c[8 + sizeof AG.record];
    const uint32_t mc = 0x31434741u;   /* « AGC1 » */
    memset(c, 0, 8); memcpy(c, &mc, 4); memcpy(c + 8, AG.record, sizeof AG.record);
    E.cfg.save_write(AG_SAUVE + 1, c, (int)sizeof c);
    uint8_t d[8 + sizeof AG.record_inv];
    const uint32_t mi = 0x31494741u;   /* « AGI1 » : records en sens inverse */
    memset(d, 0, 8); memcpy(d, &mi, 4); memcpy(d + 8, AG.record_inv, sizeof AG.record_inv);
    E.cfg.save_write(AG_SAUVE + 3, d, (int)sizeof d);
}
static void charger_sauvegarde(void) {
    AG.reg = (ag_reglages_t){ { 16, 14, 16, 12, 14 }, 3, AG_ADV_MAX, 1, 0 };
    memset(AG.record, 0, sizeof AG.record); memset(AG.record_inv, 0, sizeof AG.record_inv);
    AG.vue = 0; AG.classe = 2; AG.sens = 0;
    {
        uint8_t d[8 + sizeof AG.record_inv];
        uint32_t mi = 0;
        if (E.cfg.save_read && E.cfg.save_read(AG_SAUVE + 3, d, (int)sizeof d) == (int)sizeof d && (memcpy(&mi, d, 4), mi == 0x31494741u)) {
            memcpy(AG.record_inv, d + 8, sizeof AG.record_inv);
            for (int cl = 0; cl < AG_CLASSES; cl++) for (int k = 0; k < AG_PISTES_MAX; k++) if (!(AG.record_inv[cl][k] > 0 && AG.record_inv[cl][k] < 3600)) AG.record_inv[cl][k] = 0;
        }
    }
    uint8_t b[16 + sizeof(ag_reglages_t) + 4 * AG_PISTES_MAX];
    uint32_t m = 0;
    if (E.cfg.save_read && E.cfg.save_read(AG_SAUVE, b, (int)sizeof b) == (int)sizeof b && (memcpy(&m, b, 4), m == SAUVE_MAGIE)) {
        memcpy(&AG.reg, b + 16, sizeof AG.reg); memcpy(AG.record[2], b + 16 + sizeof AG.reg, sizeof AG.record[2]);
        AG.vue = b[4] == 1;
        if (b[5] >= 1 && b[5] <= AG_CLASSES) AG.classe = b[5] - 1;
        AG.sens = b[6] == 1; AG.variante = b[7] == 1;
        uint8_t c[8 + sizeof AG.record];
        uint32_t mc = 0;
        if (E.cfg.save_read(AG_SAUVE + 1, c, (int)sizeof c) == (int)sizeof c && (memcpy(&mc, c, 4), mc == 0x31434741u)) memcpy(AG.record, c + 8, sizeof AG.record);
        for (int i = 0; i < 5; i++) if (AG.reg.vol[i] > 20) AG.reg.vol[i] = 20;
        if (AG.reg.tours < 1 || AG.reg.tours > 9) AG.reg.tours = 3;
        if (AG.reg.adversaires < 1 || AG.reg.adversaires > AG_ADV_MAX) AG.reg.adversaires = AG_ADV_MAX;
        if (AG.reg.difficulte > 2) AG.reg.difficulte = 1;
        AG.reg.tramage = AG.reg.tramage ? 1 : 0;
        for (int cl = 0; cl < AG_CLASSES; cl++) for (int k = 0; k < AG_PISTES_MAX; k++) if (!(AG.record[cl][k] > 0 && AG.record[cl][k] < 3600)) AG.record[cl][k] = 0;
    }
    carriere_charger();
    /* Hasard et musique d'après l'heure (14/11/2026, retour de l'utilisateur : « les musiques ne changent jamais, j'ai toujours la
       même en boucle » : la série des courses repartait du même morceau à chaque lancement, et le hasard des courses était le même) */
    if (E.cfg.clock_us) {
        const uint32_t t = E.cfg.clock_us();
        AG.ncourses = (int)(t % 9973u);
        ag_rng ^= t * 2654435761u;
        if (!ag_rng) ag_rng = 0x9E3779B9u;
    }
    if (!classe_ouverte(AG.classe)) AG.classe = AG.nligues ? AG.ligues[0].classe : 2;
    reglages_appliquer();
}

/* ---------- Piste ---------- */
/* En-tête d'une piste (AGPS) : nom, lieu, sol, tours, horizon, textures des AGT_N rôles, ciel, lumière, teinte, couleurs du thème */
#define PH_TEX 56
#define PH_CIEL (PH_TEX + 6 * AGT_N)   /* (textures, leur moitié, leur quart) */
#define PH_LUM (PH_CIEL + 9)
#define PH_TEINTE (PH_LUM + 1)
#define PH_DRAP (PH_TEINTE + 3)   /* drapeaux du thème : 1 l'eau (AG.eau) */
#define PH_NC (PH_TEINTE + 5)
#define PH_COUL (PH_NC + 4)
static float plan_x0, plan_z0, plan_k;   /* plan : (x - plan_x0) × plan_k, (z - plan_z0) × plan_k dans un carré de côté 1 */
/* Charge la piste k : en-tête, palette du thème (index AG_PAL_COMMUN et suivants), sections et leur repère, trajectoire des
   adversaires, plaques, plan ; puis l'ambiance et l'horizon (render.c) */
static int piste_charger(int k) {
    if (k < 0 || k >= AG.npistes) return 0;
    const uint8_t *p = AG.pistes[k].bloc, *fin = p + AG.pistes[k].len;
    if (p + PH_COUL > fin) return 0;
    world_t *w = &E.w;
    AG.piste = k;
    copier_nom(AG.nom, p, 24); copier_nom(AG.lieu, p + 24, 24);
    AG.sol = rdfa(p + 48); AG.tours = p[52] ? p[52] : 3; AG.horizon = p[53] < AG.nhz ? p[53] : -1;
    for (int t = 0; t < AGT_N; t++) { AG.tex[t] = rd16a(p + PH_TEX + 2 * t); AG.tex_d[t] = rd16a(p + PH_TEX + 2 * (AGT_N + t)); AG.tex_q[t] = rd16a(p + PH_TEX + 2 * (2 * AGT_N + t)); }
    /* Textures réduites (table MIPS du monde, que submit_poly consulte : la moitié, le quart, selon les texels par pixel de la face) :
       les textures des rôles se suivent dans DECO (generer.js) */
    {
        static uint8_t tab[AGT_N * 4];
        int suite = 1;
        for (int t = 1; t < AGT_N; t++) if (AG.tex[t] != AG.tex[0] + t) suite = 0;
        w->nmips = 0;
        if (suite) {
            for (int t = 0; t < AGT_N; t++) {
                const int d = TILE_BIG + AG.tex_d[t], qq = TILE_BIG + AG.tex_q[t];
                tab[t * 4] = (uint8_t)d; tab[t * 4 + 1] = (uint8_t)(d >> 8); tab[t * 4 + 2] = (uint8_t)qq; tab[t * 4 + 3] = (uint8_t)(qq >> 8);
            }
            w->mip0 = (uint32_t)(TILE_BIG + AG.tex[0]); w->nmips = AGT_N; w->mips = tab;
        }
    }
    for (int c = 0; c < 3; c++) for (int i = 0; i < 3; i++) AG.ciel[c][i] = p[PH_CIEL + c * 3 + i];
    AG.lum = p[PH_LUM] / 100.0f;
    for (int i = 0; i < 3; i++) AG.teinte[i] = p[PH_TEINTE + i] / 100.0f;
    AG.eau = (p[PH_DRAP] & 1) != 0;
    const int nc = p[PH_NC];
    const uint8_t *q = p + PH_COUL;
    if (q + nc * 4 + 4 > fin || AG_PAL_COMMUN + nc > 256) return 0;
    for (int i = 0; i < nc; i++) { memcpy(w->pal[AG_PAL_COMMUN + i], q + i * 4, 3); w->full[AG_PAL_COMMUN + i] = q[i * 4 + 3]; }
    q += nc * 4;
    const uint32_t n = rd32a(q);
    q += 4;
    if (n < 8 || n > AG_SECT_MAX || q + n * 32 + 4 > fin) return 0;
    AG.n = (int)n;
    const int inv = (AG.cond & AGC_INVERSE) != 0;   /* sens inverse : la section i va en n - i (la ligne de départ reste la 0), sa droite retournée */
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = q + i * 32;
        ag_sect_t *S = &AG.s[inv ? (n - i) % n : i];
        memset(S, 0, sizeof *S);
        S->x = rdfa(e); S->y = rdfa(e + 4); S->z = rdfa(e + 8);
        S->rx = rdfa(e + 12); S->ry = rdfa(e + 16); S->rz = rdfa(e + 20);
        S->hw = rdfa(e + 24); S->drap = e[28]; S->paroi = e[29] / 8.0f;
        if (!(S->hw > 0.5f && S->hw < 20)) S->hw = 2;
        if (inv) { S->rx = -S->rx; S->ry = -S->ry; S->rz = -S->rz; }
    }
    q += n * 32;
    /* Repère orthonormé de chaque section (devant d'après les voisines, haut = devant × droite), abscisses */
    float L = 0;
    for (int i = 0; i < AG.n; i++) {
        ag_sect_t *S = &AG.s[i];
        const ag_sect_t *a = &AG.s[(i - 1 + AG.n) % AG.n], *b = &AG.s[(i + 1) % AG.n];
        float f[3] = { b->x - a->x, b->y - a->y, b->z - a->z }, r[3] = { S->rx, S->ry, S->rz }, u[3];
        norm3(f); cross3(f, r, u); norm3(u); cross3(u, f, r); norm3(r);
        S->fx = f[0]; S->fy = f[1]; S->fz = f[2]; S->rx = r[0]; S->ry = r[1]; S->rz = r[2]; S->ux = u[0]; S->uy = u[1]; S->uz = u[2];
        S->l = sqrtf((b->x - S->x) * (b->x - S->x) + (b->y - S->y) * (b->y - S->y) + (b->z - S->z) * (b->z - S->z));
        S->s = L; L += S->l;
    }
    AG.L = L;
    /* Trajectoire des adversaires : à l'intérieur des virages, d'après la courbure de la piste vue de haut, un peu en avance */
    for (int i = 0; i < AG.n; i++) {
        float c = 0, ws = 0;
        for (int j = 0; j <= 10; j++) {
            const ag_sect_t *a = &AG.s[(i + j - 3 + AG.n) % AG.n], *b = &AG.s[(i + j + 3) % AG.n];
            const float d = wrapf(atan2f(b->fx, b->fz) - atan2f(a->fx, a->fz)) / 6.0f, p2 = (float)(11 - j);
            c += d * p2; ws += p2;
        }
        AG.s[i].ligne = clampf(c / ws * 16.0f, -0.6f, 0.6f) * AG.s[i].hw;   /* virage à droite (cap croissant) : à droite */
    }
    /* Plaques */
    AG.npl = 0;
    const uint32_t np = rd32a(q);
    for (uint32_t i = 0; i < np && AG.npl < AG_PLAQUES_MAX && q + 4 + (i + 1) * 8 <= fin; i++) {
        const uint8_t *e = q + 4 + i * 8;
        ag_plaque_t *P = &AG.pl[AG.npl++];
        P->sec = rd16a(e) % AG.n; P->d = (int8_t)e[2] / 16.0f; P->hw = e[3] / 16.0f; P->len = e[4] ? e[4] : 2; P->sorte = e[5];
        if (inv) { P->sec = (AG.n - (P->sec + P->len - 1) % AG.n) % AG.n; P->d = -P->d; }
    }
    /* Terrain (« TER1 », facultatif) : x0, z0, pas f32, nx, nz u16, puis les hauteurs i16 */
    AG.ter = NULL; AG.dec = NULL; AG.ndec = 0; AG.vis = NULL; AG.nobst = 0;
    {
        const uint8_t *t = q + 4 + (size_t)np * 8;
        if (t + 20 <= fin && !memcmp(t, "TER1", 4)) {
            const int nx = rd16a(t + 16), nz = rd16a(t + 18);
            const float g = rdfa(t + 12);
            if (nx >= 2 && nz >= 2 && g > 0.5f && t + 20 + (size_t)nx * nz * 2 <= fin) {
                AG.ter = t + 20; AG.ter_nx = nx; AG.ter_nz = nz; AG.ter_x0 = rdfa(t + 4); AG.ter_z0 = rdfa(t + 8); AG.ter_g = g;
            }
            t += 20 + (size_t)nx * nz * 2;
        }
        /* Décors proches (« DEC1 », facultatif) : nombre u32, puis 24 octets chacun */
        if (t + 8 <= fin && !memcmp(t, "DEC1", 4)) {
            const uint32_t nd = rd32a(t + 4);
            if (t + 8 + (size_t)nd * 24 <= fin) { AG.dec = t + 8; AG.ndec = (int)nd; }
            t += 8 + (size_t)nd * 24;
        }
        /* Visibilité précalculée (« VIS1 », facultatif) : octets u32, sections par groupe et groupes u16, positions u32, données */
        if (t + 12 <= fin && !memcmp(t, "VIS1", 4)) {
            const uint32_t nb = rd32a(t + 4);
            const int g = rd16a(t + 8), ng = rd16a(t + 10);
            if (t + 8 + (size_t)nb <= fin && g > 0 && ng == (AG.n + g - 1) / g && 4 + 4 * ((size_t)ng + 1) <= nb && rd32a(t + 12 + 4 * (size_t)ng) <= nb - 4 - 4 * ((size_t)ng + 1)) {
                AG.vis_g = g; AG.vis_ng = ng; AG.vis_pos = t + 12; AG.vis = t + 12 + 4 * ((size_t)ng + 1); AG.vis_fin = t + 8 + nb;
            }
            t += 8 + (size_t)nb;
        }
        /* Obstacles des canyons (« OBS1 », facultatif) : nombre u32, puis 8 octets chacun */
        if (t + 8 <= fin && !memcmp(t, "OBS1", 4)) {
            const uint32_t no = rd32a(t + 4);
            for (uint32_t i = 0; i < no && AG.nobst < AG_OBST_MAX && t + 8 + (i + 1) * 8 <= fin; i++) {
                const uint8_t *e = t + 8 + i * 8;
                ag_obst_t *o = &AG.obst[AG.nobst];
                o->sec = rd16a(e); if (o->sec >= AG.n) continue;
                o->d = (int8_t)e[2] / 16.0f;
                if (inv) { o->sec = (AG.n - o->sec) % AG.n; o->d = -o->d; } o->r = e[3] / 32.0f; o->h = e[4] / 16.0f; o->cotes = e[5] < 3 ? 3 : e[5] > 8 ? 8 : e[5];
                o->a = (float)e[6] * (6.2831853f / 256); o->s = AG.s[o->sec].s;
                AG.nobst++;
            }
        }
    }
    /* Plan : 64 points de l'axe dans un carré de côté 1 */
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (int i = 0; i < AG.n; i++) { x0 = fminf(x0, AG.s[i].x); x1 = fmaxf(x1, AG.s[i].x); z0 = fminf(z0, AG.s[i].z); z1 = fmaxf(z1, AG.s[i].z); }
    plan_k = 1.0f / fmaxf(1, fmaxf(x1 - x0, z1 - z0));
    plan_x0 = (x0 + x1) / 2; plan_z0 = (z0 + z1) / 2;
    for (int i = 0; i < 64; i++) { const ag_sect_t *S = &AG.s[i * AG.n / 64]; AG.plan[i][0] = (S->x - plan_x0) * plan_k; AG.plan[i][1] = (S->z - plan_z0) * plan_k; }
    /* Nuit : ciel bleu nuit, lumière baissée, teinte bleutée ; brume : brouillard proche, de la couleur du ciel à l'horizon éclaircie
       (render.c : AG_BRUME_VUE) */
    if (AG.cond & AGC_NUIT) {
        static const uint8_t NUIT[3][3] = { { 4, 6, 18 }, { 24, 30, 64 }, { 16, 20, 44 } };
        for (int c = 0; c < 3; c++) for (int i = 0; i < 3; i++) AG.ciel[c][i] = (uint8_t)((AG.ciel[c][i] + 3 * NUIT[c][i]) / 4);
        AG.lum *= 0.6f; AG.teinte[0] *= 0.85f; AG.teinte[1] *= 0.92f; AG.teinte[2] *= 1.12f;
    }
    if (AG.cond & AGC_BRUME) for (int i = 0; i < 3; i++) AG.ciel[2][i] = (uint8_t)((AG.ciel[1][i] + 150) / 2);
    E.fog_on = (AG.cond & AGC_BRUME) != 0;
    tour_reference_calculer();
    ag_rendu_piste();
    return 1;
}

/* ---------- Chargement ---------- */
int ag_charger(const uint8_t *pak, size_t len) {
    uint32_t sz;
    AG.on = 0;   /* (paquet d'Echoes : le mode reste coupé, quoi qu'il y ait dans la mémoire) */
    const uint8_t *p = sect(pak, len, "AGPS", &sz);
    if (!p) return 0;
    world_t *w = &E.w;
    memset(&AG, 0, sizeof AG);
    AG.on = 1;
    /* Pistes (AGPS) : chaque bloc, lu à son chargement */
    if (sz < 4 || p[0] != 1) { E.error = "pistes : section AGPS illisible"; return -1; }
    {
        const uint8_t *q = p + 4, *fin = p + sz;
        for (int k = 0; k < p[1] && AG.npistes < AG_PISTES_MAX && q + 4 <= fin; k++) {
            const uint32_t l = rd32a(q);
            if (q + 4 + l > fin || l < PH_COUL) break;
            ag_piste_t *P = &AG.pistes[AG.npistes++];
            P->bloc = q + 4; P->len = l;
            copier_nom(P->nom, q + 4, 24); copier_nom(P->lieu, q + 28, 24);
            q += 4 + l;
        }
    }
    if (!AG.npistes) { E.error = "paquet sans piste"; return -1; }
    if ((p = sect(pak, len, "AGJE", &sz)) && sz >= 24) copier_nom(AG.jeu, p, 24); else snprintf(AG.jeu, sizeof AG.jeu, "AntiGraV");
    /* Horizons (AGHZ) et emblèmes des écuries (AGEM, facultatifs) */
    if ((p = sect(pak, len, "AGHZ", &sz)) && sz >= 4) {
        const uint8_t *q = p + 4, *fin = p + sz;
        for (int k = 0; k < p[0] && AG.nhz < AG_HZ_MAX; k++) if (!image_lire(&q, fin, &AG.hz[AG.nhz++])) { AG.nhz--; break; }
    }
    if ((p = sect(pak, len, "AGEM", &sz)) && sz >= 4) {
        const uint8_t *q = p + 4, *fin = p + sz;
        for (int k = 0; k < p[0] && AG.nem < AG_ECURIES_MAX; k++) if (!image_lire(&q, fin, &AG.em[AG.nem++])) { AG.nem--; break; }
    }
    /* Images du jeu (AGIM, facultatif) : logo, scènes de Forge, chacune avec son nom */
    AG.nim = 0;
    if ((p = sect(pak, len, "AGIM", &sz)) && sz >= 4) {
        const uint8_t *q = p + 4, *fin = p + sz;
        for (int k = 0; k < p[0] && AG.nim < AG_IM_MAX && q + 16 <= fin; k++) {
            copier_nom(AG.im_nom[AG.nim], q, 15); q += 16;
            if (!image_lire(&q, fin, &AG.im[AG.nim])) break;
            AG.nim++;
        }
    }
    /* Cockpits (AGCK, facultatif) : écuries u8, 0 × 3 ; chaque écurie : images u8, 0 × 3, ses images (la plus petite d'abord) */
    memset(AG.nck, 0, sizeof AG.nck);
    if ((p = sect(pak, len, "AGCK", &sz)) && sz >= 4) {
        const uint8_t *q = p + 4, *fin = p + sz;
        for (int e = 0; e < p[0] && q + 4 <= fin; e++) {
            const int ni = q[0];
            q += 4;
            for (int t = 0; t < ni; t++) {
                ag_image_t im;
                if (!image_lire(&q, fin, &im)) { e = p[0]; break; }
                if (e < AG_ECURIES_MAX && AG.nck[e] < AG_CK_TAILLES) AG.ck[e][AG.nck[e]++] = im;
            }
        }
    }
    /* Engins (AGNA version 3) : lus sur place ; un niveau de détail trop grand pour les tampons de render.c est écarté */
    if ((p = sect(pak, len, "AGNA", &sz)) && sz >= 4 && p[0] == 3) {
        const uint8_t *qa = p + 4, *fin = p + sz;
        for (int m = 0; m < p[1] && AG.nmod < AG_MODELES_MAX && qa + 20 <= fin; m++) {
            ag_modele_t *M = &AG.mod[AG.nmod];
            copier_nom(M->nom, qa, 16); M->nlod = 0;
            const int nl = qa[16];
            qa += 20;
            for (int l = 0; l < nl && qa + 8 <= fin; l++) {
                const int nv = rd16a(qa), nf = rd16a(qa + 2);
                const float dist = rdfa(qa + 4);
                qa += 8;
                if (qa + (size_t)nv * 12 + (size_t)nf * 20 > fin) { E.error = "engins : section AGNA tronquée"; return -1; }
                if (M->nlod < AG_LODS_MAX && nv > 0 && nf > 0 && nv <= AG_SOMMETS_MAX && nf <= AG_FACES_MAX) {
                    ag_lod_t *D = &M->lod[M->nlod++];
                    D->nv = nv; D->nf = nf; D->dist = dist > 0 ? dist : 1e9f; D->v = qa; D->f = qa + (size_t)nv * 12;
                }
                qa += (size_t)nv * 12 + (size_t)nf * 20;
            }
            if (M->nlod) AG.nmod++;
        }
    }
    if (!AG.nmod) { E.error = "paquet sans engin (AGNA 3)"; return -1; }
    /* Écuries (AGEC version 2 : 64 octets) */
    if ((p = sect(pak, len, "AGEC", &sz)) && sz >= 4)
        for (uint32_t k = 0; k < rd32a(p) && AG.nec < AG_ECURIES_MAX && 4 + (k + 1) * 64 <= sz; k++) {
            const uint8_t *e = p + 4 + k * 64;
            ag_ecurie_t *C = &AG.ec[AG.nec];
            copier_nom(C->nom, e, 16); copier_nom(C->devise, e + 40, 24);
            C->modele = e[16] < AG.nmod ? e[16] : 0; C->livree = e[17]; C->numero = e[18]; C->modele2 = C->livree2 = -1;
            C->embleme = AG.nec < AG.nem ? AG.nec : -1;
            memcpy(C->c, e + 20, AGC_N);
            C->v = rdfa(e + 28); C->a = rdfa(e + 32); C->m = rdfa(e + 36);
            if (!(C->v > 0.5f && C->v < 2)) C->v = 1;
            if (!(C->a > 0.5f && C->a < 2)) C->a = 1;
            if (!(C->m > 0.5f && C->m < 2)) C->m = 1;
            AG.nec++;
        }
    if (!AG.nec) { E.error = "paquet sans écurie (AGEC)"; return -1; }
    /* Second engin de chaque écurie (AGE2, facultatif) */
    if ((p = sect(pak, len, "AGE2", &sz)) && sz >= 4)
        for (int k = 0; k < p[0] && k < AG.nec && 4 + (k + 1) * 4 <= (int)sz; k++) {
            const uint8_t *e = p + 4 + k * 4;
            if (e[0] < AG.nmod) { AG.ec[k].modele2 = e[0]; AG.ec[k].livree2 = rd16a(e + 2); }
        }
    /* Carrière (AGCR, facultatif : sans elle, tout est ouvert) : version 1, ligues, puis chaque ligue (32 octets) et ses épreuves (16) */
    AG.nligues = 0;
    if ((p = sect(pak, len, "AGCR", &sz)) && sz >= 4 && p[0] == 1) {
        const uint8_t *q = p + 4, *fin = p + sz;
        for (int l = 0; l < p[1] && AG.nligues < AG_LIGUES_MAX && q + 32 <= fin; l++) {
            ag_ligue_t *Lg = &AG.ligues[AG.nligues];
            copier_nom(Lg->nom, q, 24);
            Lg->classe = q[24] < AG_CLASSES ? q[24] : 0; Lg->nep = q[25]; Lg->seuil = q[26]; Lg->pistes = rd16a(q + 28); Lg->armes = q[30];
            q += 32;
            if (Lg->nep > AG_EPREUVES_MAX || q + 16 * (size_t)Lg->nep > fin) break;
            for (int e = 0; e < Lg->nep; e++, q += 16) {
                ag_epreuve_t *Ep = &Lg->ep[e];
                Ep->type = q[0] < AGE_N ? q[0] : AGE_COURSE; Ep->piste = q[1]; Ep->tours = q[2] ? q[2] : 3; Ep->adv = q[3] > AG_ADV_MAX ? AG_ADV_MAX : q[3];
                Ep->cond = q[4]; Ep->rival = q[5];
                for (int i = 0; i < 3; i++) { Ep->obj[i] = q[6 + i]; Ep->cp[i] = q[9 + i]; Ep->cc[i] = q[12 + i]; }
            }
            AG.nligues++;
        }
    }
    /* Palette, textures (comme world.c) : tuile 0 = la palette (faces des engins), grandes textures après (DECO) */
    if (!(p = sect(pak, len, "PAL ", NULL))) { E.error = "section PAL absente"; return -1; }
    w->pal_count = 256;
    for (int i = 0; i < 256; i++) { memcpy(w->pal[i], p + 4 + i * 4, 3); w->full[i] = p[4 + i * 4 + 3]; }
    if (!(p = sect(pak, len, "TEX ", &sz))) { E.error = "section TEX absente"; return -1; }
    w->tex = p; w->tex_len = sz; w->big_off = 256;
    if (!(p = sect(pak, len, "DECO", NULL))) { E.error = "textures absentes (DECO)"; return -1; }
    w->deco_n = rd32a(p); w->deco = p + 4;
    w->g0 = 0x10000;   /* aucune texture de sol en cases */
    w->ech = 1; w->wl = -1000; w->sprite_scale = 1; w->zoom = w->zoom_min = 1;
    w->defi_k0 = 1; w->defi_k1 = 0;   /* aucun bandeau défilant */
    w->fog_rgb[0] = 40; w->fog_rgb[1] = 22; w->fog_rgb[2] = 52;
    w->sun[0] = 0.45f; w->sun[1] = 0.8f; w->sun[2] = 0.4f;
    if ((p = sect(pak, len, "AUDI", &sz))) audio_load(p, sz);
    E.fog_on = 0; E.dither_on = 0;   /* (pas de brouillard : 13/11/2026, demande de l'utilisateur, « supprimer la brume qui consomme
                                        de la puissance » ; la distance de vue est AG_VUE de render.c) */
    E.pitch = 0.2f; E.zoom = 1; E.cam_dist = 2.6f; E.talk = -2; E.ride = -1; E.mus_force = 65535;
    if (!(AG.s = E.cfg.alloc(sizeof(ag_sect_t) * AG_SECT_MAX, 0))) { E.error = "mémoire insuffisante (piste)"; return -1; }
    render_init();
    if (!E.error) ui_init();
    if (!E.error && !ag_rendu_reserver()) E.error = "mémoire insuffisante (scène de la course)";
    if (E.error) return -1;
    music_reserver();
    charger_sauvegarde();
    if (!piste_charger(0)) { E.error = "piste 0 illisible"; return -1; }
    AG.phase = AGP_TITRE; AG.joueur = -1; AG.ecurie = 0;
    ag_course_lancer(-1);   /* démonstration de l'écran titre : les adversaires seuls */
    return 1;
}

/* ---------- Repère de la piste ---------- */
static inline float plan_de(int i, float x, float y, float z) { const ag_sect_t *S = &AG.s[i]; return (x - S->x) * S->fx + (y - S->y) * S->fy + (z - S->z) * S->fz; }
int ag_cadre(int sec, float t, float *c, float *r, float *u, float *hw) {
    const ag_sect_t *a = &AG.s[sec % AG.n], *b = &AG.s[(sec + 1) % AG.n];
    c[0] = a->x + (b->x - a->x) * t; c[1] = a->y + (b->y - a->y) * t; c[2] = a->z + (b->z - a->z) * t;
    float f[3] = { a->fx + (b->fx - a->fx) * t, a->fy + (b->fy - a->fy) * t, a->fz + (b->fz - a->fz) * t };
    r[0] = a->rx + (b->rx - a->rx) * t; r[1] = a->ry + (b->ry - a->ry) * t; r[2] = a->rz + (b->rz - a->rz) * t;
    norm3(f); cross3(f, r, u); norm3(u); cross3(u, f, r); norm3(r);
    if (hw) *hw = a->hw + (b->hw - a->hw) * t;
    return 1;
}
/* Section sous le point : celle dont le plan (perpendiculaire à son devant) est derrière lui et le plan de la suivante devant */
static void localiser(ag_nav_t *n) {
    const int N = AG.n;
    int i = n->sec;
    for (int it = 0; it < 64; it++) {
        const int j = (i + 1) % N;
        if (plan_de(j, n->x, n->y, n->z) >= 0) { i = j; continue; }
        if (plan_de(i, n->x, n->y, n->z) < 0) { i = (i - 1 + N) % N; continue; }
        break;
    }
    const float a = plan_de(i, n->x, n->y, n->z), b = plan_de((i + 1) % N, n->x, n->y, n->z);
    n->sec = i; n->t = a - b > 1e-6f ? clampf(a / (a - b), 0, 1) : 0;
}

/* ---------- Départ ---------- */
static void poser(ag_nav_t *n, float s, float lat) {
    while (s < 0) s += AG.L;
    while (s >= AG.L) s -= AG.L;
    int i = 0;
    while (i + 1 < AG.n && AG.s[i + 1].s <= s) i++;
    const float t = AG.s[i].l > 0 ? (s - AG.s[i].s) / AG.s[i].l : 0;
    float c[3], r[3], u[3], f[3];
    ag_cadre(i, t, c, r, u, NULL);
    n->sec = i; n->t = t; n->d = lat; n->s_mod = s;
    n->x = c[0] + r[0] * lat + u[0] * AG_HOVER; n->y = c[1] + r[1] * lat + u[1] * AG_HOVER; n->z = c[2] + r[2] * lat + u[2] * AG_HOVER;
    cross3(r, u, f);   /* devant = droite × haut */
    n->hx = f[0]; n->hy = f[1]; n->hz = f[2];
    n->ux = u[0]; n->uy = u[1]; n->uz = u[2];
    n->vx = n->vy = n->vz = 0;
}
/* Course : ecurie du joueur (-1 : démonstration, rien que des adversaires) ; grille de deux colonnes, le joueur en dernier ;
   contre-la-montre : le joueur seul */
void ag_course_lancer(int ecurie) {
    const int ev = AG.ev_on && ecurie >= 0, t = AG.ev.type;
    const int seul = ecurie >= 0 && (ev ? t == AGE_CLM || t == AGE_ASCENSION : AG.mode == AGM_CLM);
    AG.nn = seul ? 1 : ev ? (t == AGE_DUEL ? 2 : AG.ev.adv + 1) : (ecurie >= 0 ? AG.reg.adversaires : AG_ADV_MAX) + 1;
    if (AG.nn > AG_ADV_MAX + 1) AG.nn = AG_ADV_MAX + 1;
    AG.joueur = ecurie >= 0 ? AG.nn - 1 : -1;
    if (ecurie >= 0) AG.ecurie = ecurie;
    AG.tours = ecurie >= 0 ? (ev ? (t == AGE_ASCENSION || t == AGE_ELIM ? 99 : tours_classe(AG.ev.tours)) : tours_classe(AG.reg.tours)) : 99;
    AG.armes_ok = ecurie < 0 ? 0x7E : ev ? ((AG.cond & AGC_SANS_ARMES) || t == AGE_CLM || t == AGE_ASCENSION ? 0 : AG.ligues[AG.car_l].armes)
                : AG.mode == AGM_CLM ? 0 : AG.nligues ? AG.car.armes : 0x7E;
    AG.elim_n = 0; AG.palier = 0;
    for (int k = 0; k < AG.npl; k++) AG.pl[k].repos = 0;
    for (int k = 0; k < AG_NAV_MAX; k++) AG.nav[k].etourdi = 0;
    for (int k = 0; k < AG.nn; k++) {
        ag_nav_t *n = &AG.nav[k];
        memset(n, 0, sizeof *n);
        n->joueur = k == AG.joueur;
        n->ecurie = n->joueur ? AG.ecurie % AG.nec : ev && t == AGE_DUEL && AG.ev.rival < AG.nec ? AG.ev.rival : (k + (ecurie >= 0 ? ecurie + 1 : 0)) % AG.nec;
        const ag_ecurie_t *C = &AG.ec[n->ecurie];
        n->vmax = AG_VMAX * C->v * CL.v; n->acc = AG_ACC * C->a * CL.a; n->man = C->m;
        n->variante = C->modele2 >= 0 && (n->joueur ? AG.variante : hasard() < 0.5f);   /* (second engin : le choix du joueur, au hasard pour les autres) */
        if (n->variante) { n->vmax *= AG_VAR_V; n->acc *= AG_VAR_A; n->man *= AG_VAR_M; }
        n->niveau = n->joueur ? 1 : NIVEAU_DIFF[AG.reg.difficulte] + 0.012f * (float)k + (ev && t == AGE_DUEL ? AG_DUEL_NIVEAU : 0);   /* le premier sur la grille est le plus lent */
        if (n->joueur && AG.nligues) {   /* améliorations du garage */
            n->vmax *= 1 + AG_AMEL_V * AG.car.ameliore[0]; n->acc *= 1 + AG_AMEL_A * AG.car.ameliore[1]; n->man *= 1 + AG_AMEL_M * AG.car.ameliore[2];
        }
        n->decal = n->joueur ? 0 : ((float)(k % 3) - 1) * 0.7f + (hasard() - 0.5f) * 0.4f;   /* trois trajectoires : pas de peloton à la file */
        const int ligne = k / 2, col = k % 2;
        const float recul = 3.5f + 2.6f * (float)ligne + (col ? 1.3f : 0);
        poser(n, AG.L - recul, seul ? 0 : col ? 0.95f : -0.95f);
        n->prog = -recul; n->tour = -1; n->plaque = -1; n->meilleur = -1; n->rang = k; n->energie = 1;
    }
    AG.chrono = 0; AG.phase_t = 0; AG.pause = 0; AG.cam_ok = 0; AG.annonce[0] = 0; AG.pas_reste = 0;
    memset(AG.onde, 0, sizeof AG.onde); memset(AG.proj, 0, sizeof AG.proj); memset(AG.mines, 0, sizeof AG.mines);
    memset(AG.eclats, 0, sizeof AG.eclats); memset(AG.boules, 0, sizeof AG.boules); AG.alerte_t = 0;
    if (ecurie >= 0) { AG.phase = AGP_DEPART; AG.ncourses++; AG.suivi = AG.joueur; }
    else AG.suivi = 0;
}
/* Vitrine de l'écran des écuries : l'engin seul, posé sur la piste après la ligne de départ */
static void vitrine(void) {
    AG.nn = 1; AG.joueur = -1; AG.suivi = 0;
    ag_nav_t *n = &AG.nav[0];
    memset(n, 0, sizeof *n);
    n->ecurie = AG.ecurie;
    n->variante = AG.variante && AG.ec[AG.ecurie].modele2 >= 0;
    n->vmax = AG_VMAX; n->acc = AG_ACC; n->man = 1; n->niveau = 1;
    poser(n, 6, 0);
    n->poussee = 0.7f; n->meilleur = -1; n->tour = -1; n->energie = 1;
}

/* ---------- Pilotage ---------- */
typedef struct { float gaz, volant, frein_g, frein_d; } cmd_t;
/* Adversaire : vise un point de sa trajectoire devant lui (d'autant plus loin qu'il va vite) */
static void piloter(const ag_nav_t *n, cmd_t *c) {
    const float vit = sqrtf(n->vx * n->vx + n->vy * n->vy + n->vz * n->vz);
    const int j = (n->sec + 4 + (int)(vit * 0.42f)) % AG.n;
    const ag_sect_t *S = &AG.s[j];
    float lat = clampf(S->ligne + n->decal, -S->hw + 0.7f, S->hw - 0.7f);
    {   /* obstacle devant (moins de 25 cases) sur son chemin (là où il est, ou là où il vise) : il vise le côté le plus large, à la hauteur
           de l'obstacle (le point visé d'ordinaire, plus loin, lui faisait couper à travers : 85 chocs en trois tours aux galeries) */
        const float sn = AG.s[n->sec].s + n->t * AG.s[n->sec].l;
        for (int k = 0; k < AG.nobst; k++) {
            const ag_obst_t *o = &AG.obst[k];
            float ds = o->s - sn;
            if (AG.L > 0) ds -= floorf(ds / AG.L) * AG.L;
            const float marge = o->r + AG_R + 0.7f;
            if (ds > 25 || ds < 0.3f || (fabsf(lat - o->d) > marge && fabsf(n->d - o->d) > marge)) continue;
            const float hw = AG.s[o->sec].hw, gauche = o->d - o->r + hw, droite = hw - (o->d + o->r);   /* places de chaque côté */
            lat = droite > gauche ? clampf(o->d + o->r + AG_R + 0.55f, -hw + 0.6f, hw - 0.6f) : clampf(o->d - o->r - AG_R - 0.55f, -hw + 0.6f, hw - 0.6f);
            S = &AG.s[(o->sec + 1) % AG.n];
            break;
        }
    }
    float dir[3] = { S->x + S->rx * lat - n->x, S->y + S->ry * lat - n->y, S->z + S->rz * lat - n->z };
    const float u[3] = { n->ux, n->uy, n->uz }, h[3] = { n->hx, n->hy, n->hz };
    float side[3];
    cross3(u, h, side);
    const float e = atan2f(dot3(dir, side), dot3(dir, h));
    c->gaz = 1; c->volant = clampf(e * 3.2f, -1, 1);
    c->frein_g = e < -0.3f ? 1 : 0; c->frein_d = e > 0.3f ? 1 : 0;
}

/* ---------- Physique ---------- */
static void choc_son(ag_nav_t *n, float force) {
    if (!n->joueur || force < 0.6f) return;
    son_jouer(force > 4 ? SON_CRASH : SON_CHOC, clampf(0.25f + force * 0.12f, 0, 1), 0.9f + hasard() * 0.2f);
    AG.secousse = fmaxf(AG.secousse, clampf(force * 0.12f, 0, 0.8f));
}
static void physique(ag_nav_t *n, const cmd_t *c, float dt) {
    if (n->detruit) return;
    const ag_ecurie_t *C = &AG.ec[n->ecurie];
    float u[3] = { n->ux, n->uy, n->uz }, h[3] = { n->hx, n->hy, n->hz }, side[3];
    float vel[3] = { n->vx, n->vy, n->vz };
    { const float k = dot3(h, u); for (int i = 0; i < 3; i++) h[i] -= u[i] * k; norm3(h); }   /* cap dans le plan de la piste */
    cross3(u, h, side);
    float vf = dot3(vel, h), vl = dot3(vel, side);
    const float vmax = fmaxf(1, n->vmax * (n->joueur ? 1 : n->niveau) * (n->boost > 0 ? AG_BOOST_V : 1));   /* (jamais nulle : division) */
    /* Rotation : volant, aérofreins */
    const float rapide = 1 - AG_VIRAGE_VITE * clampf(vf / vmax, 0, 1);
    const float wv = (c->volant * AG_VIRAGE * C->m * rapide + (c->frein_d - c->frein_g) * AG_FREIN_VIRAGE) * CL.w;
    n->w += (wv - n->w) * fminf(1, AG_VIRAGE_VIF * dt);
    { const float a = n->w * dt, ca = cosf(a), sa = sinf(a); for (int i = 0; i < 3; i++) { const float hn = h[i] * ca + side[i] * sa; side[i] = side[i] * ca - h[i] * sa; h[i] = hn; } }
    /* Poussée, traînée, aérofreins, accélération */
    if (c->gaz > 0 && vf < vmax) vf += n->acc * c->gaz * (1 - (vf / vmax) * (vf / vmax)) * dt;
    else if (vf > vmax) vf -= (vf - vmax) * 2.2f * dt;
    if (c->gaz <= 0) vf -= vf * AG_TRAINEE * dt;
    vf -= vf * AG_FREIN * (c->frein_g + c->frein_d) * dt;
    if (n->boost > 0) n->boost -= dt;
    /* Gravité le long de la pente */
    { const float g[3] = { u[0] * u[1] * AG_GRAVITE, -AG_GRAVITE + u[1] * u[1] * AG_GRAVITE, u[2] * u[1] * AG_GRAVITE }; vf += dot3(g, h) * dt; vl += dot3(g, side) * dt; }
    /* Dérive : la vitesse de côté se perd */
    const float adh = AG_ADHERENCE * C->m * ((c->frein_g > 0) != (c->frein_d > 0) ? AG_FREIN_ADH : 1);
    vl *= expf(-adh * dt);
    for (int i = 0; i < 3; i++) vel[i] = h[i] * vf + side[i] * vl;
    n->x += vel[0] * dt; n->y += vel[1] * dt; n->z += vel[2] * dt;
    /* Sur la piste : section, écart à l'axe, murs, hauteur */
    localiser(n);
    float pc[3], r[3], un[3], hw;
    ag_cadre(n->sec, n->t, pc, r, un, &hw);
    float d = (n->x - pc[0]) * r[0] + (n->y - pc[1]) * r[1] + (n->z - pc[2]) * r[2];
    const float lim = hw - AG_R;
    n->frotte = 0;
    if (d > lim || d < -lim) {
        const float sgn = d > 0 ? 1.0f : -1.0f, vn = dot3(vel, r) * sgn;   /* vitesse vers le mur */
        d = sgn * lim;
        if (vn > 0) {
            for (int i = 0; i < 3; i++) vel[i] -= r[i] * sgn * vn * (1 + AG_REBOND);
            const float sp = sqrtf(dot3(vel, vel)), perte = clampf(vn / fmaxf(sp, 1) * 0.8f, 0, 0.5f);
            for (int i = 0; i < 3; i++) vel[i] *= 1 - perte;
            for (int i = 0; i < 3; i++) h[i] -= r[i] * sgn * clampf(vn * 0.03f, 0, 0.25f);
            norm3(h);
            n->choc = fmaxf(n->choc, clampf(vn * 0.15f, 0, 1));
            choc_son(n, vn);
            if (vn > AG_DEG_MUR_SEUIL * CL.v) degats(n, (vn / CL.v - AG_DEG_MUR_SEUIL) * AG_DEG_MUR);   /* (rapporté à la catégorie : un même accrochage coûte autant partout) */
        }
        n->frotte = sqrtf(dot3(vel, vel)) > 2 ? 1 : 0;
        if (n->frotte) degats(n, AG_DEG_FROTTE * dt);
    }
    {   /* obstacles des canyons : un cercle de rayon r + AG_R dans le repère de la piste (abscisse, écart) ; l'engin est repoussé, sa vitesse
           vers l'obstacle renvoyée (comme un mur), des dégâts au-delà du seuil */
        const float sn = AG.s[n->sec].s + n->t * AG.s[n->sec].l;
        float f[3];
        cross3(r, un, f);
        for (int k = 0; k < AG.nobst; k++) {
            const ag_obst_t *o = &AG.obst[k];
            float ds = sn - o->s;
            if (AG.L > 0) ds -= floorf(ds / AG.L + 0.5f) * AG.L;
            const float dd = d - o->d, lim = o->r + AG_R;
            if (fabsf(ds) > lim || fabsf(dd) > lim) continue;
            const float l = sqrtf(ds * ds + dd * dd);
            if (l >= lim) continue;
            const float ns = l > 1e-4f ? ds / l : -1, nd = l > 1e-4f ? dd / l : 0, pen = lim - l;
            d += nd * pen;
            for (int i = 0; i < 3; i++) pc[i] += f[i] * ns * pen;
            const float vn = -(dot3(vel, f) * ns + dot3(vel, r) * nd);   /* vitesse vers l'obstacle */
#ifdef AG_TRACE_OBST
            if (vn > 0) fprintf(stderr, "OBST engin %d obstacle %d vn %.1f\n", (int)(n - AG.nav), k, vn);
#endif
            if (vn > 0) {
                for (int i = 0; i < 3; i++) vel[i] += (f[i] * ns + r[i] * nd) * vn * (1 + AG_REBOND);
                const float sp = sqrtf(dot3(vel, vel)), perte = clampf(vn / fmaxf(sp, 1) * 0.8f, 0, 0.6f);
                for (int i = 0; i < 3; i++) vel[i] *= 1 - perte;
                n->choc = fmaxf(n->choc, clampf(vn * 0.15f, 0, 1));
                choc_son(n, vn);
                if (vn > AG_DEG_MUR_SEUIL * CL.v) degats(n, (vn / CL.v - AG_DEG_MUR_SEUIL) * AG_DEG_MUR);
            }
        }
    }
    n->d = d;
    /* Haut lissé (le roulis du dévers vient vite, sans à-coup d'une section à l'autre), cap reprojeté */
    for (int i = 0; i < 3; i++) u[i] += (un[i] - u[i]) * fminf(1, 14 * dt);
    norm3(u);
    { const float k = dot3(vel, u); for (int i = 0; i < 3; i++) vel[i] -= u[i] * k; }
    { const float k = dot3(h, u); for (int i = 0; i < 3; i++) h[i] -= u[i] * k; norm3(h); }
    {   /* onde du SÉISME : l'engin monte avec la chaussée soulevée, qui le lance en l'air, puis retombe (bond : saut, saut_v) */
        const float dep = ag_onde_dep(n->sec) * (1 - n->t) + ag_onde_dep((n->sec + 1) % AG.n) * n->t, mont = (dep - n->dep_prec) / dt;
        n->dep_prec = dep;
        n->saut_v -= AG_SAUT_G * dt; n->saut += n->saut_v * dt;
        if (n->saut <= dep) { n->saut = dep; n->saut_v = clampf(mont, 0, AG_ONDE_LANCE); }
    }
    const float flot = AG_HOVER + n->saut + 0.025f * sinf(E.clock * 13.0f + (float)n->ecurie * 1.7f);
    n->x = pc[0] + r[0] * d + un[0] * flot; n->y = pc[1] + r[1] * d + un[1] * flot; n->z = pc[2] + r[2] * d + un[2] * flot;
    n->vx = vel[0]; n->vy = vel[1]; n->vz = vel[2];
    n->hx = h[0]; n->hy = h[1]; n->hz = h[2]; n->ux = u[0]; n->uy = u[1]; n->uz = u[2];
    n->vitesse = sqrtf(dot3(vel, vel));
    n->poussee += ((c->gaz > 0 ? 1.0f : 0) - n->poussee) * fminf(1, 10 * dt);   /* flamme des réacteurs (render.c) */
    const float roul = -(c->volant * AG_ROULIS + (c->frein_d - c->frein_g) * AG_ROULIS_FREIN);
    n->roulis += (roul - n->roulis) * fminf(1, 5 * dt);
    if (n->choc > 0) n->choc = fmaxf(0, n->choc - 2 * dt);
    /* Plaques */
    int sur = -1;
    for (int k = 0; k < AG.npl; k++) {
        const ag_plaque_t *P = &AG.pl[k];
        const int ds = (n->sec - P->sec + AG.n) % AG.n;
        if (ds < P->len && fabsf(d - P->d) < P->hw + 0.15f) { sur = k; break; }
    }
    if (sur >= 0 && sur != n->plaque) {
        const ag_plaque_t *P = &AG.pl[sur];
        if (P->sorte == 0) {   /* accélération */
            n->boost = AG_BOOST_T;
            n->vx += h[0] * AG_BOOST_ELAN * CL.v; n->vy += h[1] * AG_BOOST_ELAN * CL.v; n->vz += h[2] * AG_BOOST_ELAN * CL.v;
            if (n->joueur) son_jouer(SON_VENT, 0.8f, 1.3f);
        }
        if ((P->sorte != 0 || (AG.ev_on && AG.ev.type == AGE_COMBAT)) && !n->arme && P->repos <= 0) {   /* arme : une à la fois, parmi les permises (combat : sur toutes les plaques), pas au repos */
            const int a = arme_tirer(n);
            if (a) {
                AG.pl[sur].repos = AG_PLAQUE_REPOS;   /* (la plaque se rallume après AG_PLAQUE_REPOS s) */
                n->arme = a; n->t_arme = AG.chrono; n->charge = 0;
                n->arme_t = 1.2f + 2.5f * hasard();
                if (n->joueur) { son_jouer(SON_VALIDER, 0.7f, 1.4f); snprintf(AG.annonce, sizeof AG.annonce, "%s", ARMES[n->arme]); AG.annonce_t = 1.2f; }
            }
        }
    }
    n->plaque = sur;
    /* Progression, tours */
    const float s_cur = AG.s[n->sec].s + n->t * AG.s[n->sec].l;
    float ds = s_cur - n->s_mod;
    if (ds < -AG.L / 2) ds += AG.L; else if (ds > AG.L / 2) ds -= AG.L;
    n->s_mod = s_cur; n->prog += ds;
    const int tours = (int)floorf(n->prog / AG.L);
    if (AG.ev_on && AG.ev.type == AGE_ASCENSION && n->joueur && !n->fini && !n->detruit) {   /* ascension : un palier par quart de tour, la vitesse monte */
        const int q = (int)floorf(n->prog * 4 / AG.L);
        while (q > AG.palier) {
            AG.palier++;
            n->vmax *= AG_ASC_K; n->acc *= AG_ASC_KA;
            if (AG.palier >= AG.ev.obj[0]) { n->fini = 1; n->fin = AG.chrono; break; }   /* l'or : l'épreuve est gagnée */
            snprintf(AG.annonce, sizeof AG.annonce, "PALIER %d", AG.palier); AG.annonce_t = 1.0f;
            son_jouer(SON_VALIDER, 0.6f, 1.2f + 0.02f * (float)AG.palier);
        }
    }
    if (tours > n->tour && !n->fini) {
        if (n->tour >= 0) {
            const float t = AG.chrono - n->t_tour;
            if (n->meilleur < 0 || t < n->meilleur) n->meilleur = t;
        }
        if (AG.ev_on && AG.ev.type == AGE_ASCENSION && n->joueur) {   /* ascension : rien n'est rendu */
        } else if (n->tour >= 0 && n->energie < 1 && !n->detruit) {   /* tour bouclé : de l'énergie rendue */
            n->energie = fminf(1, n->energie + AG_RECHARGE_TOUR);
            if (n->joueur && AG.phase == AGP_COURSE && tours < AG.tours) son_jouer(SON_AG_ABSORBE, 0.5f, 1.2f);
        }
        n->tour = tours; n->t_tour = AG.chrono;
        if (n->tour >= AG.tours && AG.joueur >= 0) { n->fini = 1; n->fin = AG.chrono; }
        else if (n->joueur && n->tour == AG.tours - 1) { snprintf(AG.annonce, sizeof AG.annonce, "DERNIER TOUR"); AG.annonce_t = 2.0f; son_jouer(SON_VALIDER, 0.7f, 1); }
    }
}

/* Engins qui se touchent : écartés l'un de l'autre, vitesses le long du choc échangées en partie */
static void chocs(void) {
    for (int a = 0; a < AG.nn; a++) for (int b = a + 1; b < AG.nn; b++) {
        ag_nav_t *A = &AG.nav[a], *Bn = &AG.nav[b];
        if (A->detruit || Bn->detruit) continue;
        float d[3] = { Bn->x - A->x, Bn->y - A->y, Bn->z - A->z };
        const float l = sqrtf(dot3(d, d));
        if (l >= 2 * AG_R || l < 1e-4f) continue;
        for (int i = 0; i < 3; i++) d[i] /= l;
        const float pousse = (2 * AG_R - l) * 0.5f;
        A->x -= d[0] * pousse; A->y -= d[1] * pousse; A->z -= d[2] * pousse;
        Bn->x += d[0] * pousse; Bn->y += d[1] * pousse; Bn->z += d[2] * pousse;
        const float va = A->vx * d[0] + A->vy * d[1] + A->vz * d[2], vb = Bn->vx * d[0] + Bn->vy * d[1] + Bn->vz * d[2], rel = va - vb;
        if (rel <= 0) continue;
        const float j = rel * 0.8f;
        A->vx -= d[0] * j; A->vy -= d[1] * j; A->vz -= d[2] * j;
        Bn->vx += d[0] * j; Bn->vy += d[1] * j; Bn->vz += d[2] * j;
        if (A->joueur || Bn->joueur) choc_son(A->joueur ? A : Bn, rel * 1.5f);
        if (rel > 3 * CL.v) { degats(A, (rel / CL.v - 3) * AG_DEG_CHOC); degats(Bn, (rel / CL.v - 3) * AG_DEG_CHOC); }
    }
}
static void classer(void) {
    int ord[AG_NAV_MAX];
    for (int k = 0; k < AG.nn; k++) ord[k] = k;
    for (int i = 1; i < AG.nn; i++) for (int j = i; j > 0; j--) {
        const ag_nav_t *a = &AG.nav[ord[j - 1]], *b = &AG.nav[ord[j]];
        const int avant = a->detruit && !b->detruit ? 1 : b->detruit ? (a->detruit && b->t_detruit > a->t_detruit) :
                          b->fini && (!a->fini || b->fin < a->fin) ? 1 : !b->fini && !a->fini && b->prog > a->prog ? 1 : 0;
        if (!avant) break;
        const int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t;
    }
    for (int k = 0; k < AG.nn; k++) AG.nav[ord[k]].rang = k;
}

/* ---------- Caméra ---------- */
/* Course et démonstration : derrière l'engin suivi, basse, en retard sur ses virages ; vitrine : autour de l'engin */
/* Essais (simulateur de la LDK, commande « camera:d:p ») : caméra de poursuite reculée de d et baissée de p (rad), pour voir les
   volumes de dehors */
static float cam_essai_d, cam_essai_p;
void eng_ag_camera_essai(float d, float p) { cam_essai_d = d; cam_essai_p = p; }
/* Essais (commande « tunnel:k:d:h:l ») : caméra fixe devant l'entrée du k-ième tunnel de la piste (0 le premier), à d cases avant
   elle le long de la piste, h au-dessus de la chaussée, l sur le côté, qui regarde l'entrée (k < 0 : la caméra normale) */
static int cam_fixe;
static float cam_fixe_v[6];
void eng_ag_vue_tunnel(int k, float d, float h, float l) {
    cam_fixe = 0;
    for (int i = 0, n = 0; k >= 0 && i < AG.n; i++) {
        const ag_sect_t *S = &AG.s[i], *P = &AG.s[(i + AG.n - 1) % AG.n];
        if (!(S->drap & AGS_TUNNEL) || (P->drap & AGS_TUNNEL) || n++ != k) continue;
        const float cx = S->x - S->fx * d + S->ux * h + S->rx * l, cy = S->y - S->fy * d + S->uy * h + S->ry * l, cz = S->z - S->fz * d + S->uz * h + S->rz * l;
        const float tx = S->x + S->ux * 1.5f, ty = S->y + S->uy * 1.5f, tz = S->z + S->uz * 1.5f, dx = tx - cx, dy = ty - cy, dz = tz - cz;
        cam_fixe_v[0] = tx; cam_fixe_v[1] = ty; cam_fixe_v[2] = tz; cam_fixe_v[3] = atan2f(dx, dz); cam_fixe_v[4] = -atan2f(dy, sqrtf(dx * dx + dz * dz)); cam_fixe_v[5] = sqrtf(dx * dx + dy * dy + dz * dz);
        cam_fixe = 1;
        break;
    }
}
int ag_cockpit(void) {
    return AG.vue && AG.joueur >= 0 && AG.suivi == AG.joueur && !AG.nav[AG.joueur].detruit && (AG.phase == AGP_DEPART || AG.phase == AGP_COURSE);
}
void ag_camera(float *tx, float *ty, float *tz, float *yaw, float *pitch, float *dist) {
    const ag_nav_t *n = &AG.nav[AG.suivi >= 0 && AG.suivi < AG.nn ? AG.suivi : 0];
    if (cam_fixe) { *tx = cam_fixe_v[0]; *ty = cam_fixe_v[1]; *tz = cam_fixe_v[2]; *yaw = cam_fixe_v[3]; *pitch = cam_fixe_v[4]; *dist = cam_fixe_v[5]; return; }
    if (ag_cockpit()) {   /* l'œil du pilote (setup_camera : distance nulle, la caméra au point visé) */
        *tx = n->x + n->ux * AG_CK_HAUT + n->hx * AG_CK_AV; *ty = n->y + n->uy * AG_CK_HAUT + n->hy * AG_CK_AV; *tz = n->z + n->uz * AG_CK_HAUT + n->hz * AG_CK_AV;
        *yaw = AG.cam_yaw; *pitch = AG.cam_pitch; *dist = 0;
        return;
    }
    if (AG.phase == AGP_ECURIE || AG.phase == AGP_GARAGE) {
        *tx = n->x; *ty = n->y - 0.14f; *tz = n->z;   /* (point visé sous l'engin : il monte dans l'image, entre le titre et le panneau) */
        *yaw = E.clock * 0.55f; *pitch = 0.36f; *dist = 2.4f;
        return;
    }
    *tx = n->x + n->ux * AG_CAM_HAUT + n->hx * AG_CAM_AV; *ty = n->y + n->uy * AG_CAM_HAUT + n->hy * AG_CAM_AV; *tz = n->z + n->uz * AG_CAM_HAUT + n->hz * AG_CAM_AV;
    *yaw = AG.cam_yaw; *pitch = AG.cam_pitch + cam_essai_p;
    *dist = AG_CAM_D + AG_CAM_DV * n->vitesse + cam_essai_d;
}
void eng_ag_pilote(int on) { AG.essai = on; }
void eng_ag_carriere_essai(int l, int e) {   /* essais : l'épreuve e de la ligue l, la carrière toute ouverte */
    if (l < 0 || l >= AG.nligues || e < 0 || e >= AG.ligues[l].nep) return;
    AG.car.debut = 1; AG.car.ligue_max = (uint8_t)(AG.nligues - 1); AG.car.ecuries = 0xFF; AG.car.classes = 0x1F; AG.car.pistes = 0xFFFF; AG.car.armes = 0x7E;
    epreuve_lancer(l, e, 0);
}
void eng_ag_bilan(char *b, int n) {   /* essais : le joueur à la fin de la course */
    if (AG.joueur < 0) { snprintf(b, (size_t)n, "pas de course"); return; }
    const ag_nav_t *j = &AG.nav[AG.joueur];
    snprintf(b, (size_t)n, "piste %s catégorie %s : meilleur tour %.2f s, référence %.2f s (%d %%), rang %d/%d, coups %d, tours %d, énergie %.2f%s%s",
             AG.nom, CL.nom, (double)j->meilleur, (double)AG.t_ref, AG.t_ref > 0 && j->meilleur > 0 ? (int)(100 * j->meilleur / AG.t_ref + 0.5f) : 0,
             j->rang + 1, AG.nn, j->coups, j->tour, (double)j->energie, j->detruit ? (j->elimine ? ", éliminé" : ", détruit") : "", AG.ev_on ? (j->fini ? ", fini" : "") : "");
    if (AG.ev_on) { char m[64]; const size_t l = strlen(b); snprintf(m, sizeof m, ", palier %d, médaille %d", AG.palier, j->fini || j->detruit ? medaille_calculer() : -1); snprintf(b + l, (size_t)n - l, "%s", m); }
    { const size_t l = strlen(b); snprintf(b + l, (size_t)n - l, ", musique %s (courses %d)", music_now(), AG.ncourses); }
}
void eng_ag_arme_essai(int a) {   /* essais : le joueur porte l'arme a (ARME_*) */
    if (AG.joueur < 0 || a < 0 || a >= ARME_N) return;
    ag_nav_t *j = &AG.nav[AG.joueur];
    j->arme = a; j->t_arme = AG.chrono; j->charge = 0;
}
void eng_ag_classe_essai(int k) { AG.classe = k < 0 ? 0 : k >= AG_CLASSES ? AG_CLASSES - 1 : k; }
/* Banc d'essai de la LDK (main_retrofw.c, params=banc) : course rapide sur la piste donnée, dans la vue donnée (0 extérieure, 1
   cockpit ; pas sauvegardée), le joueur piloté comme un adversaire, sans le compte à rebours ; 0 si la piste n'existe pas */
int eng_ag_essai(int piste, int vue) {
    if (!AG.on || piste < 0 || piste >= AG.npistes) return 0;
    if (piste != AG.piste && !piste_charger(piste)) return 0;
    AG.mode = AGM_RAPIDE; AG.vue = vue ? 1 : 0; AG.essai = 1;
    ag_course_lancer(AG.ecurie);
    AG.phase = AGP_COURSE; AG.phase_t = 0; AG.chrono = 0;
    return 1;
}
static void camera_maj(float dt) {
    const ag_nav_t *n = &AG.nav[AG.suivi >= 0 && AG.suivi < AG.nn ? AG.suivi : 0];
    const float yv = atan2f(n->hx, n->hz);
    /* Pente : on regarde vers le haut d'une montée, vers le bas d'une descente (pitch > 0 : vers le bas) */
    if (ag_cockpit()) {   /* dans l'axe de l'engin : son cap et sa pente */
        const float pc = AG_CK_PITCH - asinf(clampf(n->hy, -1, 1));
        const int prem = !AG.cam_ok;
        if (prem) { AG.cam_yaw = yv; AG.cam_pitch = pc; AG.cam_ok = 1; }
        AG.cam_yaw += wrapf(yv - AG.cam_yaw) * fminf(1, AG_CK_VIF * dt);
        AG.cam_pitch += (pc - AG.cam_pitch) * fminf(1, AG_CK_VIF * dt);
        /* roulis : le haut de l'engin (penché de AG_CK_PENCHE de son roulis montré, comme son modèle dans render.c) vu de la caméra,
           droite (cos lacet, 0, -sin lacet) et haut (sin lacet sin tangage, cos tangage, cos lacet sin tangage) de to_view */
        float u[3] = { n->ux, n->uy, n->uz }, h[3] = { n->hx, n->hy, n->hz }, s[3], v[3];
        cross3(u, h, s);
        const float ro = -n->roulis * AG_CK_PENCHE, cr = cosf(ro), sr = sinf(ro);
        for (int i = 0; i < 3; i++) v[i] = u[i] * cr + s[i] * sr;
        const float cy = cosf(AG.cam_yaw), sy = sinf(AG.cam_yaw), cp = cosf(AG.cam_pitch), sp = sinf(AG.cam_pitch);
        const float rx = v[0] * cy - v[2] * sy, ry = v[0] * sy * sp + v[1] * cp + v[2] * cy * sp;
        const float rc = clampf(atan2f(rx, ry), -AG_CK_ROULIS_MAX, AG_CK_ROULIS_MAX);
        AG.cam_roll = prem ? rc : AG.cam_roll + (rc - AG.cam_roll) * fminf(1, AG_CK_VIF * dt);
        if (AG.secousse > 0) AG.secousse = fmaxf(0, AG.secousse - 2.5f * dt);
        return;
    }
    const float pente = asinf(clampf(AG.s[n->sec].fy, -1, 1)), pv = AG_CAM_PITCH - 0.75f * pente;
    if (!AG.cam_ok) { AG.cam_yaw = yv; AG.cam_pitch = pv; AG.cam_ok = 1; }
    AG.cam_yaw += wrapf(yv - AG.cam_yaw) * fminf(1, 6.5f * dt);
    AG.cam_pitch += (pv - AG.cam_pitch) * fminf(1, 4.0f * dt);
    if (AG.secousse > 0) AG.secousse = fmaxf(0, AG.secousse - 2.5f * dt);
}

/* ---------- Sons ---------- */
static void sons_maj(void) {
    const int vitrine_on = AG.phase == AGP_ECURIE || AG.phase == AGP_GARAGE, demo = AG.joueur < 0;
    for (int k = 0; k < AG.nn; k++) {
        const ag_nav_t *n = &AG.nav[k];
        if (n->detruit) continue;
        const ag_nav_t *j = &AG.nav[AG.suivi >= 0 ? AG.suivi : 0];
        const float dx = n->x - j->x, dy = n->y - j->y, dz = n->z - j->z, dd = sqrtf(dx * dx + dy * dy + dz * dz);
        const float vol = vitrine_on ? 0.25f : (k == AG.suivi ? 0.55f : 0.35f * clampf(1 - dd / 18, 0, 1)) * (demo ? 0.5f : 1);
        if (vol > 0.01f) son_boucle(CLE_COURSE + k, SON_MOTEUR_REACTEUR, vol, vitrine_on ? 0.6f : 0.65f + 0.7f * clampf(n->vitesse / (AG_VMAX * CL.v * 1.3f), 0, 1.2f) + (n->boost > 0 ? 0.15f : 0));
    }
    if (AG.joueur >= 0 && AG.nav[AG.joueur].frotte && AG.phase == AGP_COURSE) son_boucle(CLE_FROTTEMENT, SON_FROTTEMENT, 0.5f, 1);
    if (!vitrine_on) {
        const ag_nav_t *j = &AG.nav[AG.suivi >= 0 ? AG.suivi : 0];
        for (int k = 0; k < 4; k++) {   /* grondement de chaque onde du SÉISME, d'après la distance de son front le long de la piste */
            if (!AG.onde[k].on || AG.L <= 0) continue;
            float dd = AG.onde[k].s - j->prog;
            dd -= floorf(dd / AG.L + 0.5f) * AG.L;
            const float vol = 0.9f * clampf(1 - fabsf(dd) / 70, 0, 1) * clampf(onde_haut(k) / AG_ONDE_A * 1.5f, 0, 1) * (demo ? 0.5f : 1);
            if (vol > 0.02f) son_boucle(CLE_AG_ONDE + k, SON_AG_SEISME, vol, 0.9f + 0.1f * (float)k);
        }
        if (AG.joueur >= 0 && AG.nav[AG.joueur].arme == ARME_RAYON && AG.nav[AG.joueur].charge > 0)
            son_boucle(CLE_AG_CHARGE, SON_AG_CHARGE, 0.55f, 0.7f + 0.9f * AG.nav[AG.joueur].charge / AG_RAYON_CHARGE);
    }
    audio_fin_image();
}

/* ---------- Mise à jour ---------- */
#define APPUI(k) ((in->keys & (k)) && !(AG.prec & (k)))
static void commande_joueur(const eng_input_t *in, cmd_t *c) {
    const uint32_t k = in->keys;
    c->gaz = (k & (ENG_KEY_UP | ENG_KEY_OK)) || in->stick_y < -0.3f ? 1 : 0;
    float v = (k & ENG_KEY_RIGHT ? 1.0f : 0) - (k & ENG_KEY_LEFT ? 1.0f : 0);
    if (fabsf(in->stick_x) > 0.15f) v = clampf(in->stick_x, -1, 1);
    c->volant = v;
    c->frein_g = k & ENG_KEY_ROT_L ? 1 : 0; c->frein_d = k & ENG_KEY_ROT_R ? 1 : 0;
}
/* Physique à pas fixe (13/11/2026 : 30 i/s fixes) : AG_PAS par pas, deux par image à 30 i/s ; la course est la même quelle que soit la
   cadence, et le chrono avance avec les pas (temps des tours au soixantième de seconde). Une image trop longue ne joue pas plus de
   AG_PAS_MAX pas : le jeu ralentit au lieu de sauter */
#define AG_PAS (1.0f / 60)
#define AG_PAS_MAX 6
/* Traînée du réacteur : un point (derrière la coque, au milieu) tous les AG_TRACE_DT, dans un anneau de AG_TRACE_N ; render.c en
   fait un ruban translucide (13/11/2026, demande de l'utilisateur : « les traînées de propulseur derrière les vaisseaux ») */
#define AG_TRACE_DT 0.04f
static void trace_maj(ag_nav_t *n, float dt) {
    if ((n->trace_t += dt) < AG_TRACE_DT) return;
    n->trace_t = 0;
    float *p = n->trace[n->trace_i];
    p[0] = n->x - n->hx * 0.74f + n->ux * 0.12f; p[1] = n->y - n->hy * 0.74f + n->uy * 0.12f; p[2] = n->z - n->hz * 0.74f + n->uz * 0.12f;
    n->trace_i = (n->trace_i + 1) % AG_TRACE_N;
    if (n->trace_n < AG_TRACE_N) n->trace_n++;
}
static int pas_a_jouer(float dt) {
    AG.pas_reste += dt;
    int n = (int)(AG.pas_reste / AG_PAS);
    if (n > AG_PAS_MAX) { n = AG_PAS_MAX; AG.pas_reste = 0; }
    else AG.pas_reste -= (float)n * AG_PAS;
    return n;
}
static void pas(const eng_input_t *in) {
    const float dt = AG_PAS;
    AG.chrono += dt;
    for (int k = 0; k < AG.npl; k++) if (AG.pl[k].repos > 0) AG.pl[k].repos -= dt;
    for (int k = 0; k < AG.nn; k++) {
        ag_nav_t *n = &AG.nav[k];
        if (n->detruit) continue;
        cmd_t c = { 0, 0, 0, 0 };
        if (n->joueur && !n->fini && !AG.essai) commande_joueur(in, &c);
        else piloter(n, &c);
        if (n->etourdi > 0) {   /* désemparé (SÉISME) : ni poussée ni direction, volant à fond d'un côté */
            n->etourdi -= dt;
            c.gaz = 0; c.volant = n->vrille; c.frein_g = c.frein_d = 0;
        }
        /* Élastique : les adversaires se rapprochent du joueur sans le rattraper d'un coup */
        if (!n->joueur && AG.joueur >= 0) {
            const float base = NIVEAU_DIFF[AG.reg.difficulte] + 0.012f * (float)k + (AG.ev_on && AG.ev.type == AGE_DUEL ? AG_DUEL_NIVEAU : 0), ecart = AG.nav[AG.joueur].prog - n->prog;
            n->niveau += (clampf(base + clampf(ecart / 60.0f, -1, 1) * AG_ELASTIQUE, 0.8f, 1.08f) - n->niveau) * fminf(1, dt * 0.5f);
        }
        physique(n, &c, dt);
        trace_maj(n, dt);
        if (n->arme && (!n->joueur || AG.essai || n->fini)) ia_armes(n, dt);
        if (n->touche > 0) n->touche -= dt;
        if (n->immun > 0) n->immun -= dt;
        if (n->bouclier > 0) n->bouclier -= dt;
        if (n->rayon_t > 0) n->rayon_t -= dt;
    }
    ondes_maj(dt);
    armes_maj(dt);
    chocs();
}
/* ---------- Ondes du SÉISME ---------- */
static float onde_pos(float s) { return AG.L > 0 ? s - floorf(s / AG.L) * AG.L : 0; }   /* front sur la piste, de 0 à AG.L */
static float onde_haut(int k) { return AG_ONDE_A * fminf(1, AG.onde[k].fait / AG_ONDE_MONTE) * fmaxf(0, 1 - AG.onde[k].fait / AG_ONDE_D); }
static void onde_tirer(ag_nav_t *n) {
    int k = 0;
    while (k < 4 && AG.onde[k].on) k++;
    if (k == 4) return;
    const int qui = (int)(n - AG.nav);
    AG.onde[k].on = 1; AG.onde[k].tireur = qui; AG.onde[k].s = n->prog + AG_ONDE_AV; AG.onde[k].fait = 0;
    AG.onde[k].v = AG_VMAX * CL.v * AG_ONDE_V; AG.onde[k].touches = 1u << qui;
    son_ici(n, SON_EXPLOSION, 0.9f, 0.55f);   /* coup sourd au départ ; le grondement suit l'onde (sons_maj) */
}
float ag_onde_dep(int sec) {
    float h = 0;
    for (int k = 0; k < 4; k++) {
        if (!AG.onde[k].on) continue;
        float d = onde_pos(AG.onde[k].s) - AG.s[sec].s;
        if (d < -AG.L / 2) d += AG.L; else if (d >= AG.L / 2) d -= AG.L;
        if (d >= 0 && d <= AG_ONDE_L) h += onde_haut(k) * sinf(3.14159265f * d / AG_ONDE_L);
    }
    return h;
}
int ag_onde_zone(int sec) {
    for (int k = 0; k < 4; k++) {
        if (!AG.onde[k].on) continue;
        float d = onde_pos(AG.onde[k].s) - AG.s[sec].s;
        if (d < -AG.L / 2) d += AG.L; else if (d >= AG.L / 2) d -= AG.L;
        if (d >= -2 && d <= AG_ONDE_L + 2) return 1;
    }
    return 0;
}
static void ondes_maj(float dt) {
    for (int k = 0; k < 4; k++) {
        if (!AG.onde[k].on) continue;
        AG.onde[k].s += AG.onde[k].v * dt; AG.onde[k].fait += AG.onde[k].v * dt;
        if (AG.onde[k].fait > AG_ONDE_D) { AG.onde[k].on = 0; continue; }
        const float a = onde_haut(k) / AG_ONDE_A;
        for (int m = 0; m < AG.nn; m++) {
            ag_nav_t *n = &AG.nav[m];
            const float d = AG.onde[k].s - n->prog;
            if ((AG.onde[k].touches & (1u << m)) || d < 0 || d > AG_ONDE_L) continue;
            AG.onde[k].touches |= 1u << m;
            if (n->detruit || n->bouclier > 0) continue;
            if (AG.onde[k].tireur >= 0 && AG.onde[k].tireur < AG.nn && !AG.nav[AG.onde[k].tireur].fini) AG.nav[AG.onde[k].tireur].coups++;
            const float fo = AG_ONDE_FORCE + (1 - AG_ONDE_FORCE) * a, f = 1 - AG_ONDE_FREIN * fo;
            n->vx *= f; n->vy *= f; n->vz *= f;
            n->saut_v = fmaxf(n->saut_v, 5.5f * fo);   /* (la chaussée soulevée le lance encore : physique) */
            n->etourdi = AG_ONDE_ETOURDI * fo; n->vrille = hasard() < 0.5f ? -1.0f : 1.0f; n->choc = 1;
            degats(n, AG_ONDE_DEG * fo);
            eclats_lancer(n->x, n->y, n->z, n, 5, 3.0f);
            son_ici(n, SON_AG_IMPACT, 0.6f + 0.4f * fo, 0.6f);
            if (n->joueur) AG.secousse = fmaxf(AG.secousse, 0.9f * fo);
        }
    }
}
/* ---------- Armes, énergie, explosions ---------- */
static inline float wrapL(float d) { return AG.L > 0 ? d - floorf(d / AG.L + 0.5f) * AG.L : d; }   /* écart le long de la piste, de -L/2 à L/2 */
/* Section et fraction de l'abscisse s (de 0 à L) : recherche par dichotomie */
static void sec_de_s(float s, int *sec, float *t) {
    s -= floorf(s / AG.L) * AG.L;
    int a = 0, b = AG.n - 1;
    while (a < b) { const int m = (a + b + 1) / 2; if (AG.s[m].s <= s) a = m; else b = m - 1; }
    *sec = a; *t = AG.s[a].l > 0 ? clampf((s - AG.s[a].s) / AG.s[a].l, 0, 1) : 0;
}
/* Point de la piste à l'abscisse s, à l'écart d (borné à la chaussée), à h au-dessus (chaussée soulevée par les ondes comprise) */
static void point_piste(float s, float *d, float h, float *p, float *u, float *f) {
    int sec; float t, c[3], r[3], hw;
    sec_de_s(s, &sec, &t);
    ag_cadre(sec, t, c, r, u, &hw);
    *d = clampf(*d, -hw + 0.3f, hw - 0.3f);
    const float hh = h + ag_onde_dep(sec) * (1 - t) + ag_onde_dep((sec + 1) % AG.n) * t;
    for (int i = 0; i < 3; i++) p[i] = c[i] + r[i] * *d + u[i] * hh;
    if (f) cross3(r, u, f);
}
/* Son à la position de n : plein pour l'engin suivi, plus faible avec la distance pour les autres */
static void son_ici(const ag_nav_t *n, int son, float vol, float hauteur) {
    const ag_nav_t *j = &AG.nav[AG.suivi >= 0 ? AG.suivi : 0];
    const float dx = n->x - j->x, dy = n->y - j->y, dz = n->z - j->z, k = n == j ? 1 : clampf(1 - sqrtf(dx * dx + dy * dy + dz * dz) / 45, 0, 1);
    if (vol * k > 0.02f) son_jouer(son, vol * k * (AG.joueur < 0 ? 0.5f : 1), hauteur);
}
/* Couleur de la palette commune la plus proche (éclats, boules de feu : render.c a les siennes) */
static uint8_t pal_proche(int r, int g, int b) {
    int best = 1, bd = 1 << 30;
    for (int i = 1; i < AG_PAL_COMMUN; i++) { const int dr = E.w.pal[i][0] - r, dg = E.w.pal[i][1] - g, db = E.w.pal[i][2] - b, d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; best = i; } }
    return (uint8_t)best;
}
static void eclats_lancer(float x, float y, float z, const ag_nav_t *n, int nb, float vit) {
    const ag_ecurie_t *C = &AG.ec[n->ecurie];
    const uint8_t cols[6] = { C->c[AGC_COQUE], C->c[AGC_BANDE], C->c[AGC_SOMBRE], C->c[AGC_METAL], pal_proche(255, 150, 40), pal_proche(255, 230, 120) };
    for (int k = 0, i = 0; k < nb && i < AG_ECLATS_MAX; i++) {
        ag_eclat_t *e = &AG.eclats[i];
        if (e->vie > 0) continue;
        const float a = hasard() * 6.2832f, el = 0.2f + hasard() * 1.2f, v = vit * (0.4f + 0.6f * hasard());
        e->x = x; e->y = y; e->z = z; e->vx = cosf(a) * cosf(el) * v + n->vx * 0.4f; e->vy = sinf(el) * v; e->vz = sinf(a) * cosf(el) * v + n->vz * 0.4f;
        e->t = 0; e->vie = 0.8f + hasard() * 0.8f; e->a = hasard() * 6.28f; e->w = (hasard() - 0.5f) * 20; e->col = cols[k % 6];
        k++;
    }
}
static void boule(float x, float y, float z, float taille) {
    int i = 0;   /* une place libre, sinon la plus avancée */
    for (int k = 0; k < AG_BOULES_MAX; k++) { if (AG.boules[k].vie <= 0) { i = k; break; } if (AG.boules[k].t / AG.boules[k].vie > AG.boules[i].t / AG.boules[i].vie) i = k; }
    ag_boule_t *b = &AG.boules[i];
    b->x = x; b->y = y; b->z = z; b->t = 0; b->vie = 0.35f + 0.25f * taille; b->taille = taille;
}
static void detruire(ag_nav_t *n) {
    n->energie = 0; n->detruit = 1; n->t_detruit = AG.chrono; n->arme = 0; n->charge = 0; n->bouclier = 0;
    eclats_lancer(n->x, n->y, n->z, n, 24, 7);
    boule(n->x, n->y, n->z, 1.8f);
    son_ici(n, SON_AG_EXPLOSION, 1.0f, 0.9f + 0.2f * hasard());
    if (n->joueur) { snprintf(AG.annonce, sizeof AG.annonce, "DÉTRUIT"); AG.annonce_t = 3; AG.secousse = 1; }
    else { snprintf(AG.annonce, sizeof AG.annonce, "%s DÉTRUIT", AG.ec[n->ecurie].nom); AG.annonce_t = 1.6f; }
}
static void degats(ag_nav_t *n, float x) {
    if (n->detruit || x <= 0 || AG.phase != AGP_COURSE || AG.joueur < 0 || n->bouclier > 0) return;   /* (ni démonstration, ni départ, ni arrivée) */
    if (n->joueur) x *= DEG_DIFF[AG.reg.difficulte] * (AG.nligues ? 1 - AG_AMEL_B * AG.car.ameliore[3] : 1);   /* (blindage du garage) */
    if (n->joueur && AG.ev_on && AG.ev.type == AGE_ASCENSION) { const float k = powf(AG_ASC_K, (float)AG.palier); x *= k * k; }   /* (ascension : un choc coûte plus à mesure que la vitesse monte) */
    n->energie -= x;
    if (x > 0.02f) n->touche = fmaxf(n->touche, clampf(x * 3, 0.15f, 0.5f));
    if (n->energie <= 0) detruire(n);
}
/* Coup d'une arme : vitesse perdue (frein), bond, cap dévié, énergie ; le bouclier arrête tout */
static void impact(ag_nav_t *o, float deg, float frein, float saut, int tireur) {
    if (o->detruit) return;
    if (o->bouclier > 0) { son_ici(o, SON_AG_BOUCLIER, 0.6f, 1.5f); o->touche = 0.25f; return; }
    if (tireur >= 0 && tireur < AG.nn && &AG.nav[tireur] != o && !AG.nav[tireur].fini) AG.nav[tireur].coups++;
    o->vx *= 1 - frein; o->vy *= 1 - frein; o->vz *= 1 - frein;
    o->saut_v = fmaxf(o->saut_v, saut);
    {   /* cap dévié : tourné autour du haut */
        const float a = (hasard() - 0.5f) * 0.9f, ca = cosf(a), sa = sinf(a), h[3] = { o->hx, o->hy, o->hz }, u[3] = { o->ux, o->uy, o->uz };
        float s[3];
        cross3(u, h, s);
        o->hx = h[0] * ca + s[0] * sa; o->hy = h[1] * ca + s[1] * sa; o->hz = h[2] * ca + s[2] * sa;
    }
    o->choc = 1;
    son_ici(o, SON_AG_IMPACT, 0.95f, 0.9f + 0.2f * hasard());
    eclats_lancer(o->x, o->y, o->z, o, 6, 3.5f);
    boule(o->x, o->y, o->z, 0.8f);
    if (o->joueur) AG.secousse = fmaxf(AG.secousse, 0.9f);
    degats(o, deg);
}
/* Engin le plus proche dont l'écart le long de la piste est entre dmin et dmax (négatifs : derrière), et l'écart de côté au plus lat
   (lat < 0 : peu importe) ; -1 aucun */
static int voisin(const ag_nav_t *n, float dmin, float dmax, float lat) {
    int best = -1;
    float bd = 1e9f;
    for (int m = 0; m < AG.nn; m++) {
        const ag_nav_t *o = &AG.nav[m];
        if (o == n || o->detruit) continue;
        const float d = wrapL(o->prog - n->prog);
        if (d < dmin || d > dmax || (lat >= 0 && fabsf(o->d - n->d) > lat)) continue;
        if (fabsf(d) < bd) { bd = fabsf(d); best = m; }
    }
    return best;
}
static int arme_tirer(const ag_nav_t *n) {
    const float r = AG.nn > 1 ? (float)n->rang / (float)(AG.nn - 1) : 0.5f;   /* 0 en tête, 1 dernier */
    /* (14/11/2026, demande de l'utilisateur : « le séisme plus rare, et le boost ou le bouclier plus fréquents » : SÉISME 1 + r avant,
       BOUCLIER 1,6 - 0,6 r, TURBO 1,2) */
    float P[ARME_N] = { 0, 0.45f + 0.45f * r, 1.2f + 1.6f * r, 2.2f - 1.2f * r, 0.8f + 1.4f * r, 2.4f - 0.6f * r, 2.2f };
    float s = 0;
    for (int k = 1; k < ARME_N; k++) { if (!(AG.armes_ok >> k & 1)) P[k] = 0; s += P[k]; }   /* (armes permises : la carrière les découvre peu à peu) */
    if (s <= 0) return 0;
    float x = hasard() * s;
    for (int k = 1; k < ARME_N; k++) { if (P[k] > 0 && (x -= P[k]) <= 0) return k; }
    return 0;
}
static void missile_tirer(ag_nav_t *n) {
    int i = 0;
    while (i < AG_PROJ_MAX && AG.proj[i].on) i++;
    if (i == AG_PROJ_MAX) return;
    ag_proj_t *p = &AG.proj[i];
    memset(p, 0, sizeof *p);
    p->on = 1; p->tireur = (int)(n - AG.nav); p->cible = voisin(n, 3, AG_MISSILE_PORTEE, -1);
    p->prog = n->prog + 1.4f; p->d = n->d; p->v = n->vitesse + AG_VMAX * CL.v * AG_MISSILE_V;
    son_ici(n, SON_AG_MISSILE, 0.9f, 0.95f + 0.1f * hasard());
}
static void mines_poser(ag_nav_t *n) {
    static const float ECART[AG_MINES_N] = { 0, -0.55f, 0.55f, -0.25f, 0.25f };
    int i = 0;
    for (int k = 0; k < AG_MINES_N; k++) {
        while (i < AG_MINES_MAX && AG.mines[i].on) i++;
        if (i == AG_MINES_MAX) break;
        ag_mine_t *m = &AG.mines[i];
        m->on = 1; m->tireur = (int)(n - AG.nav); m->t = 0;
        m->s = n->s_mod - 2.2f - 1.6f * (float)k; m->d = n->d + ECART[k] * AG.s[n->sec].hw;
    }
    son_ici(n, SON_AG_MINE, 0.9f, 1);
}
static void rayon_tirer(ag_nav_t *n, float charge) {
    const float f = clampf(charge / AG_RAYON_CHARGE, 0.2f, 1);
    const int c = voisin(n, 0.5f, AG_RAYON_PORTEE, 0.7f + 0.5f * f);
    n->rayon_t = 0.4f; n->rayon_f = f;
    if (c >= 0) { const ag_nav_t *o = &AG.nav[c]; n->rayon_fin[0] = o->x; n->rayon_fin[1] = o->y; n->rayon_fin[2] = o->z; }
    else { float d = n->d, u[3]; point_piste(n->s_mod + AG_RAYON_PORTEE, &d, AG_HOVER, n->rayon_fin, u, NULL); }
    son_ici(n, SON_AG_RAYON, 1.0f, 1.15f - 0.3f * f);
    if (c >= 0) impact(&AG.nav[c], AG_RAYON_DEG + AG_RAYON_DEG_PLEIN * f, 0.35f + 0.3f * f, 3 + 3 * f, (int)(n - AG.nav));
}
static void arme_utiliser(ag_nav_t *n) {
    const ag_ecurie_t *C = &AG.ec[n->ecurie];
    (void)C;
    switch (n->arme) {
    case ARME_SEISME: onde_tirer(n); break;
    case ARME_MISSILE: missile_tirer(n); break;
    case ARME_MINES: mines_poser(n); break;
    case ARME_RAYON: rayon_tirer(n, n->charge); break;
    case ARME_BOUCLIER: n->bouclier = AG_BOUCLIER_T; son_ici(n, SON_AG_BOUCLIER, 0.9f, 1); break;
    case ARME_TURBO:
        n->boost = AG_BOOST_T * 2.2f;
        n->vx += n->hx * AG_BOOST_ELAN * CL.v * 1.6f; n->vy += n->hy * AG_BOOST_ELAN * CL.v * 1.6f; n->vz += n->hz * AG_BOOST_ELAN * CL.v * 1.6f;
        son_ici(n, SON_VENT, 0.9f, 1.0f);
        break;
    default: break;
    }
    n->arme = 0; n->charge = 0;
}
static void absorber(ag_nav_t *n) {
    if (!n->arme || AG.phase != AGP_COURSE) return;
    n->energie = fminf(1, n->energie + AG_ABSORBE); n->arme = 0; n->charge = 0;
    son_ici(n, SON_AG_ABSORBE, 0.8f, 1);
    if (n->joueur) { snprintf(AG.annonce, sizeof AG.annonce, "ÉNERGIE +%d", (int)(AG_ABSORBE * 100 + 0.5f)); AG.annonce_t = 1.0f; }
}
/* Adversaire armé : il s'en sert quand c'est utile (devant lui, derrière lui, ligne droite), sinon il attend ; à court d'énergie, il
   absorbe son arme (pas le bouclier) */
static void ia_armes(ag_nav_t *n, float dt) {
    if (n->arme == ARME_RAYON && n->charge > 0) {   /* il charge */
        n->charge += dt;
        if (n->charge >= AG_RAYON_CHARGE * (0.55f + 0.45f * clampf(n->niveau, 0, 1))) arme_utiliser(n);
        return;
    }
    if ((n->arme_t -= dt) > 0) return;
    if (n->energie < 0.3f && n->arme != ARME_BOUCLIER && AG.joueur >= 0) { absorber(n); return; }
    int ok = 0;
    switch (n->arme) {
    case ARME_SEISME: ok = voisin(n, 5, 60, -1) >= 0; break;
    case ARME_MISSILE: ok = voisin(n, 4, 70, -1) >= 0; break;
    case ARME_MINES: ok = voisin(n, -25, -3, -1) >= 0; break;
    case ARME_RAYON: if (voisin(n, 3, 35, 0.8f) >= 0) { n->charge = dt; return; } break;
    case ARME_BOUCLIER: {
        int menace = 0;
        for (int p = 0; p < AG_PROJ_MAX; p++) if (AG.proj[p].on && AG.proj[p].cible == (int)(n - AG.nav) && wrapL(n->prog - AG.proj[p].prog) < 25) menace = 1;
        ok = menace || n->energie < 0.4f || AG.chrono - n->t_arme > 12;
        break;
    }
    case ARME_TURBO: {
        float m = 0;
        for (int k = 0; k < 18; k++) m = fmaxf(m, fabsf(AG.s[(n->sec + k) % AG.n].ligne));
        ok = m < 0.45f;
        break;
    }
    default: break;
    }
    if (ok) arme_utiliser(n); else n->arme_t = 0.4f;
}
static void armes_maj(float dt) {
    /* Missiles : le long de la piste, guidés de côté vers leur cible ; touchent le premier engin sur leur chemin */
    for (int i = 0; i < AG_PROJ_MAX; i++) {
        ag_proj_t *p = &AG.proj[i];
        if (!p->on) continue;
        if ((p->t += dt) > AG_MISSILE_T) { p->on = 0; boule(p->x, p->y, p->z, 0.5f); continue; }
        p->prog += p->v * dt;
        if (p->cible >= 0) {
            const ag_nav_t *T = &AG.nav[p->cible];
            if (T->detruit || wrapL(T->prog - p->prog) < -2) p->cible = -1;
            else p->d += clampf(T->d - p->d, -AG_MISSILE_GUIDE * dt, AG_MISSILE_GUIDE * dt);
        }
        float pt[3], u[3], f[3];
        point_piste(p->prog, &p->d, AG_HOVER + 0.3f, pt, u, f);
        p->x = pt[0]; p->y = pt[1]; p->z = pt[2]; p->ux = u[0]; p->uy = u[1]; p->uz = u[2]; p->hx = f[0]; p->hy = f[1]; p->hz = f[2];
        for (int m = 0; m < AG.nn; m++) {
            ag_nav_t *o = &AG.nav[m];
            if (m == p->tireur || o->detruit || fabsf(wrapL(o->prog - p->prog)) > 0.9f || fabsf(o->d - p->d) > 0.9f) continue;
            impact(o, AG_MISSILE_DEG, 0.5f, 4.5f, p->tireur);
            p->on = 0;
            break;
        }
    }
    /* Mines : posées sur la chaussée (elle peut être soulevée), une seule fois */
    for (int i = 0; i < AG_MINES_MAX; i++) {
        ag_mine_t *mi = &AG.mines[i];
        if (!mi->on) continue;
        if ((mi->t += dt) > AG_MINE_T) { mi->on = 0; continue; }
        float pt[3], u[3];
        point_piste(mi->s, &mi->d, 0.12f, pt, u, NULL);
        mi->x = pt[0]; mi->y = pt[1]; mi->z = pt[2]; mi->ux = u[0]; mi->uy = u[1]; mi->uz = u[2];
        for (int m = 0; m < AG.nn; m++) {
            ag_nav_t *o = &AG.nav[m];
            if (o->detruit || o->immun > 0 || (m == mi->tireur && mi->t < AG_MINE_ARME) || fabsf(wrapL(o->s_mod - mi->s)) > 0.8f || fabsf(o->d - mi->d) > 0.8f) continue;
            impact(o, AG_MINE_DEG, 0.4f, 5.0f, mi->tireur);
            o->immun = AG_MINE_IMMUN;
            mi->on = 0;
            break;
        }
    }
    /* Éclats : lancés, tournent, retombent */
    for (int i = 0; i < AG_ECLATS_MAX; i++) {
        ag_eclat_t *e = &AG.eclats[i];
        if (e->vie <= 0) continue;
        if ((e->t += dt) > e->vie) { e->vie = 0; continue; }
        e->vy -= 14 * dt; e->x += e->vx * dt; e->y += e->vy * dt; e->z += e->vz * dt; e->a += e->w * dt;
    }
    for (int i = 0; i < AG_BOULES_MAX; i++) if (AG.boules[i].vie > 0 && (AG.boules[i].t += dt) > AG.boules[i].vie) AG.boules[i].vie = 0;
}
/* Démonstration (titre, menus, choix de la piste) : les adversaires seuls, la caméra passe de l'un à l'autre */
static void demo_maj(float dt, const eng_input_t *in) {
    if ((AG.phase_t += dt) > AG_DEMO_T) { AG.phase_t = 0; AG.suivi = (AG.suivi + 1) % AG.nn; AG.cam_ok = 0; }
    for (int k = pas_a_jouer(dt); k > 0; k--) pas(in);
}
static void aller(int phase) { AG.phase = phase; AG.sel = 0; AG.phase_t = 0; }
/* Fin d'une course : records ; championnat : les points */
static void course_finie(void) {
    int change = 0;
    for (int k = 0; k < AG.nn; k++) {
        const ag_nav_t *n = &AG.nav[k];
        if (n->joueur && n->meilleur > 0 && (!AG.cond || AG.cond == AGC_INVERSE) && !(AG.ev_on && AG.ev.type == AGE_ASCENSION) && (RECORD(AG.piste) <= 0 || n->meilleur < RECORD(AG.piste))) { RECORD(AG.piste) = n->meilleur; change = 1; }
        if (AG.mode == AGM_CHAMPIONNAT && n->rang < 6 && !n->detruit) AG.points[n->ecurie] += POINTS[n->rang];
    }
    if (change) sauver();
}
/* Menus : haut et bas choisissent une ligne parmi n, avec le son */
static int menu_choix(const eng_input_t *in, int n) {
    if (APPUI(ENG_KEY_UP)) { AG.sel = (AG.sel + n - 1) % n; son_jouer(SON_MENU, 0.5f, 1); }
    if (APPUI(ENG_KEY_DOWN)) { AG.sel = (AG.sel + 1) % n; son_jouer(SON_MENU, 0.5f, 1); }
    return AG.sel;
}
static void changer_piste(int k) {
    if (k == AG.piste) return;
    if (piste_charger(k)) ag_course_lancer(-1);
}
/* Réglages : lignes et réglage par gauche, droite */
enum { RG_GENERAL, RG_MUSIQUE, RG_EFFETS, RG_MOTEURS, RG_INTERFACE, RG_TOURS, RG_ADVERSAIRES, RG_DIFFICULTE, RG_VITESSE, RG_TRAMAGE, RG_VUE, RG_RETOUR, RG_N };
static void reglages_maj(const eng_input_t *in) {
    const int s = menu_choix(in, RG_N), sens = APPUI(ENG_KEY_RIGHT) ? 1 : APPUI(ENG_KEY_LEFT) ? -1 : 0;
    if (sens) {
        if (s <= RG_INTERFACE) { const int v = AG.reg.vol[s] + sens; AG.reg.vol[s] = (uint8_t)(v < 0 ? 0 : v > 20 ? 20 : v); }
        else if (s == RG_TOURS) AG.reg.tours = (uint8_t)(1 + (AG.reg.tours - 1 + sens + 9) % 9);
        else if (s == RG_ADVERSAIRES) AG.reg.adversaires = (uint8_t)(1 + (AG.reg.adversaires - 1 + sens + AG_ADV_MAX) % AG_ADV_MAX);
        else if (s == RG_DIFFICULTE) AG.reg.difficulte = (uint8_t)((AG.reg.difficulte + sens + 3) % 3);
        else if (s == RG_VITESSE) for (int i = 0; i < AG_CLASSES; i++) { AG.classe = (AG.classe + sens + AG_CLASSES) % AG_CLASSES; if (classe_ouverte(AG.classe)) break; }
        else if (s == RG_TRAMAGE) AG.reg.tramage = !AG.reg.tramage;
        else if (s == RG_VUE) { AG.vue = !AG.vue; AG.cam_ok = 0; }
        reglages_appliquer();
        if (s != RG_MUSIQUE && s != RG_GENERAL) son_jouer(s == RG_MOTEURS ? SON_MOTEUR_REACTEUR : SON_MENU, 0.6f, 1);
    }
    if (APPUI(ENG_KEY_BACK) || (APPUI(ENG_KEY_OK) && s == RG_RETOUR)) {
        sauver();
        son_jouer(SON_ANNULER, 0.6f, 1);
        if (AG.reglages_retour == AGP_COURSE) { AG.phase = AGP_COURSE; AG.pause = 1; AG.pause_sel = 2; }
        else { aller(AGP_MENU); AG.sel = 4; }
    }
}
/* Vue extérieure ou cockpit (bouton B en course), annoncée, gardée dans la sauvegarde */
static void vue_changer(void) {
    AG.vue = !AG.vue; AG.cam_ok = 0;
    snprintf(AG.annonce, sizeof AG.annonce, AG.vue ? "VUE COCKPIT" : "VUE EXTÉRIEURE"); AG.annonce_t = 1.0f;
    son_jouer(SON_MENU, 0.6f, 1);
    sauver();
}
void ag_update(float dt, const eng_input_t *in) {
    if (dt > 0.05f) dt = 0.05f;
    E.clock += dt;
    music_update(dt);
    if (AG.annonce_t > 0) AG.annonce_t -= dt;
    /* Pause : REPRENDRE, RECOMMENCER, RÉGLAGES, QUITTER */
    if (AG.pause) {
        if (APPUI(ENG_KEY_UP)) { AG.pause_sel = (AG.pause_sel + 3) % 4; son_jouer(SON_MENU, 0.5f, 1); }
        if (APPUI(ENG_KEY_DOWN)) { AG.pause_sel = (AG.pause_sel + 1) % 4; son_jouer(SON_MENU, 0.5f, 1); }
        if (APPUI(ENG_KEY_MENU) || APPUI(ENG_KEY_BACK)) { AG.pause = 0; son_jouer(SON_ANNULER, 0.6f, 1); }
        else if (APPUI(ENG_KEY_OK)) {
            son_jouer(SON_VALIDER, 0.6f, 1);
            if (AG.pause_sel == 0) AG.pause = 0;
            else if (AG.pause_sel == 1) ag_course_lancer(AG.ecurie);
            else if (AG.pause_sel == 2) { AG.pause = 0; AG.reglages_retour = AGP_COURSE; AG.phase = AGP_REGLAGES; AG.sel = 0; }
            else { AG.pause = 0; ag_course_lancer(-1); aller(AGP_MENU); }
        }
        AG.prec = in->keys; audio_fin_image();
        return;
    }
    switch (AG.phase) {
    case AGP_TITRE:
        if (APPUI(ENG_KEY_OK) || APPUI(ENG_KEY_MENU)) { son_jouer(SON_VALIDER, 0.7f, 1); aller(AGP_MENU); }
        demo_maj(dt, in);
        break;
    case AGP_MENU: {
        const int s = menu_choix(in, 5);
        if (APPUI(ENG_KEY_BACK)) { son_jouer(SON_ANNULER, 0.6f, 1); aller(AGP_TITRE); }
        else if (APPUI(ENG_KEY_OK)) {
            son_jouer(SON_VALIDER, 0.7f, 1);
            AG.ev_on = 0;
            if (AG.cond) { AG.cond = 0; piste_charger(AG.piste); ag_course_lancer(-1); }   /* (la piste de la carrière avait ses conditions) */
            if (s == 4) { AG.reglages_retour = AGP_MENU; aller(AGP_REGLAGES); }
            else if (s == 0) {
                if (!AG.car.debut) { AG.mode = AGM_CARRIERE; AG.ecurie = AG.car.ecurie; aller(AGP_ECURIE); vitrine(); }   /* première fois : l'écurie */
                else { aller(AGP_CARRIERE); AG.sel = AG.car.ligue_max; }
            } else {
                AG.mode = s - 1;
                if (!ecurie_ouverte(AG.ecurie)) AG.ecurie = 0;
                if (!classe_ouverte(AG.classe)) AG.classe = AG.nligues ? AG.ligues[0].classe : 2;
                aller(AGP_ECURIE); vitrine();
            }
        }
        if (AG.phase == AGP_MENU) demo_maj(dt, in);
        break;
    }
    case AGP_REGLAGES:
        reglages_maj(in);
        if (AG.reglages_retour == AGP_MENU && AG.phase == AGP_REGLAGES) demo_maj(dt, in);
        break;
    case AGP_ECURIE:
        if (APPUI(ENG_KEY_LEFT) || APPUI(ENG_KEY_RIGHT)) {   /* (les écuries ouvertes seulement) */
            const int sens = APPUI(ENG_KEY_LEFT) ? AG.nec - 1 : 1;
            for (int i = 0; i < AG.nec; i++) { AG.ecurie = (AG.ecurie + sens) % AG.nec; if (ecurie_ouverte(AG.ecurie)) break; }
            AG.nav[0].ecurie = AG.ecurie; AG.nav[0].variante = AG.variante && AG.ec[AG.ecurie].modele2 >= 0; son_jouer(SON_MENU, 0.5f, 1);
        }
        if ((APPUI(ENG_KEY_UP) || APPUI(ENG_KEY_DOWN)) && AG.ec[AG.ecurie].modele2 >= 0) {   /* son second engin */
            AG.variante = !AG.variante; AG.nav[0].variante = AG.variante; sauver(); son_jouer(SON_MENU, 0.5f, AG.variante ? 1.2f : 1);
        }
        if (APPUI(ENG_KEY_BACK)) { son_jouer(SON_ANNULER, 0.6f, 1); ag_course_lancer(-1); aller(AGP_MENU); AG.sel = AG.mode == AGM_CARRIERE ? 0 : AG.mode + 1; }
        else if (APPUI(ENG_KEY_OK)) {
            son_jouer(SON_VALIDER, 0.7f, 1);
            if (AG.mode == AGM_CARRIERE) {   /* carrière : l'écurie du pilote */
                AG.car.ecurie = (uint8_t)AG.ecurie; AG.car.debut = 1; carriere_sauver();
                ag_course_lancer(-1); aller(AGP_CARRIERE); AG.sel = AG.car.ligue_max;
            } else if (AG.mode == AGM_CHAMPIONNAT) {   /* championnat : les pistes ouvertes, dans l'ordre */
                AG.manche = 0; memset(AG.points, 0, sizeof AG.points);
                while (AG.manche + 1 < AG.npistes && !piste_ouverte(AG.manche)) AG.manche++;
                piste_charger(AG.manche); ag_course_lancer(AG.ecurie);
            } else {
                if (!piste_ouverte(AG.piste)) changer_piste(0);
                if ((AG.cond != 0) != (AG.sens != 0)) { AG.cond = AG.sens ? AGC_INVERSE : 0; piste_charger(AG.piste); }   /* (le sens choisi la dernière fois) */
                ag_course_lancer(-1); aller(AGP_PISTE);
            }
        }
        AG.phase_t += dt;
        break;
    case AGP_PISTE:
        if (APPUI(ENG_KEY_LEFT) || APPUI(ENG_KEY_RIGHT)) {   /* (les pistes ouvertes seulement) */
            const int sens = APPUI(ENG_KEY_LEFT) ? AG.npistes - 1 : 1;
            int k = AG.piste;
            for (int i = 0; i < AG.npistes; i++) { k = (k + sens) % AG.npistes; if (piste_ouverte(k)) break; }
            changer_piste(k); son_jouer(SON_MENU, 0.5f, 1);
        }
        if (APPUI(ENG_KEY_UP) || APPUI(ENG_KEY_DOWN)) {   /* sens de la piste : les sections dans l'autre ordre, plaques et piliers suivis (piste_charger) */
            AG.sens = !AG.sens; AG.cond = AG.sens ? AGC_INVERSE : 0;
            if (piste_charger(AG.piste)) ag_course_lancer(-1);
            sauver(); son_jouer(SON_MENU, 0.5f, AG.sens ? 0.8f : 1);
        }
        if (APPUI(ENG_KEY_BACK)) { son_jouer(SON_ANNULER, 0.6f, 1); aller(AGP_ECURIE); vitrine(); }
        else if (APPUI(ENG_KEY_OK)) { son_jouer(SON_VALIDER, 0.7f, 1); ag_course_lancer(AG.ecurie); }
        else demo_maj(dt, in);
        break;
    case AGP_DEPART: {
        if (APPUI(ENG_KEY_BACK)) vue_changer();
        const float t0 = AG.phase_t;
        AG.phase_t += dt;
        for (int s = 1; s <= 3; s++) if (t0 < (float)s && AG.phase_t >= (float)s) son_jouer(SON_BIP, 0.7f, s < 3 ? 1.0f : 1.5f);
        if (AG.phase_t >= 3) { AG.phase = AGP_COURSE; AG.phase_t = 0; AG.chrono = 0; }
        cmd_t c = { 0, 0, 0, 0 };
        for (int p = pas_a_jouer(dt); p > 0; p--) for (int k = 0; k < AG.nn; k++) physique(&AG.nav[k], &c, AG_PAS);
        break;
    }
    case AGP_COURSE:
    case AGP_ARRIVEE:
        AG.phase_t += dt;
        if (AG.phase == AGP_COURSE && APPUI(ENG_KEY_MENU)) { AG.pause = 1; AG.pause_sel = 0; son_jouer(SON_MENU, 0.6f, 1); break; }
        if (AG.phase == AGP_COURSE && APPUI(ENG_KEY_BACK)) vue_changer();
        if (AG.phase == AGP_COURSE && AG.joueur >= 0 && !AG.essai) {   /* armes du joueur : X (le rayon : tenu, il charge), Y absorbe */
            ag_nav_t *j = &AG.nav[AG.joueur];
            if (!j->detruit && !j->fini && j->arme) {
                if (j->arme == ARME_RAYON) { if (in->keys & ENG_KEY_ALT_UP) j->charge = fminf(AG_RAYON_CHARGE, j->charge + dt); else if (j->charge > 0) arme_utiliser(j); }
                else if (APPUI(ENG_KEY_ALT_UP)) arme_utiliser(j);
                if (APPUI(ENG_KEY_ALT_DOWN) && j->arme) absorber(j);
            }
        }
        for (int k = pas_a_jouer(dt); k > 0; k--) pas(in);
        if (AG.phase == AGP_COURSE) elimination_maj();
        if (AG.joueur >= 0 && AG.phase == AGP_COURSE) {   /* un missile vise le joueur : alarme */
            int vise = 0;
            for (int p = 0; p < AG_PROJ_MAX; p++) if (AG.proj[p].on && AG.proj[p].cible == AG.joueur) vise = 1;
            if (vise && (AG.alerte_t -= dt) <= 0) { son_jouer(SON_AG_ALERTE, 0.55f, 1); AG.alerte_t = 0.3f; }
            if (!vise) AG.alerte_t = 0;
        }
        if (AG.phase == AGP_COURSE && AG.joueur >= 0 && AG.nav[AG.joueur].detruit && AG.chrono - AG.nav[AG.joueur].t_detruit > 1.6f) { AG.phase = AGP_ARRIVEE; AG.phase_t = 0; }
        if (AG.phase == AGP_COURSE && AG.joueur >= 0 && AG.nav[AG.joueur].fini) { AG.phase = AGP_ARRIVEE; AG.phase_t = 0; son_jouer(SON_VALIDER, 0.8f, 0.8f); }
        if (AG.phase == AGP_ARRIVEE && AG.phase_t > 2.5f && APPUI(ENG_KEY_OK)) {
            son_jouer(SON_VALIDER, 0.6f, 1);
            classer(); course_finie();
            if (AG.mode == AGM_CHAMPIONNAT) aller(AGP_CLASSEMENT);
            else if (AG.ev_on) { bilan_faire(medaille_calculer()); ag_course_lancer(-1); aller(AGP_BILAN); }
            else { ag_course_lancer(-1); aller(AGP_PISTE); }
        }
        break;
    case AGP_CARRIERE: carriere_maj(in, dt); break;
    case AGP_CALENDRIER: calendrier_maj(in, dt); break;
    case AGP_GARAGE: ag_garage_maj(in, dt); break;
    case AGP_BILAN:
        AG.phase_t += dt;
        demo_maj(dt, in);
        if (AG.phase_t > 1.0f && APPUI(ENG_KEY_OK)) { son_jouer(SON_VALIDER, 0.6f, 1); aller(AGP_CALENDRIER); AG.sel = AG.car_e; calendrier_voir(); }
        break;
    case AGP_CLASSEMENT:
        AG.phase_t += dt;
        for (int k = pas_a_jouer(dt); k > 0; k--) pas(in);
        if (AG.phase_t > 1 && APPUI(ENG_KEY_OK)) {
            son_jouer(SON_VALIDER, 0.7f, 1);
            if (AG.ev_on) {   /* coupe de la carrière : la course suivante, puis le bilan */
                if (++AG.car_manche < coupe_courses(&AG.ev)) epreuve_lancer(AG.car_l, AG.car_e, AG.car_manche);
                else { bilan_faire(medaille_coupe()); ag_course_lancer(-1); aller(AGP_BILAN); }
            } else {
                do AG.manche++; while (AG.manche < AG.npistes && !piste_ouverte(AG.manche));
                if (AG.manche < AG.npistes) { piste_charger(AG.manche); ag_course_lancer(AG.ecurie); }
                else { ag_course_lancer(-1); aller(AGP_MENU); }
            }
        }
        break;
    }
    if (AG.phase != AGP_ARRIVEE && AG.phase != AGP_CLASSEMENT) classer();
    camera_maj(dt);
    sons_maj();
    AG.prec = in->keys;
}

/* Musique (banque propre : data/antigrav/musique.bin, assets/audio/musique/pistes_antigrav.json) : « menu » hors course, la série
   « course » (course_2…) pendant une course, une autre à chaque course (music.c) ; sans « menu » dans la banque, « course » partout */
const char *ag_musique(void) {
    return AG.phase == AGP_DEPART || AG.phase == AGP_COURSE || AG.phase == AGP_ARRIVEE ? "course" : "menu";
}

/* ---------- Carrière : épreuves, médailles, garage ---------- */
static const int MEDAILLE_CR[4] = { 0, 100, 200, 350 };    /* crédits d'une médaille (× 1 + ligue / 2 ; regagnée : un quart) */
static const int AMELIORE_PRIX[3] = { 300, 700, 1500 };
static const char *const AMELIORE_NOM[4] = { "VITESSE", "POUSSÉE", "MANIABILITÉ", "BLINDAGE" };
static const char *const EPREUVE_NOM[AGE_N] = { "COURSE", "CONTRE-LA-MONTRE", "ÉLIMINATION", "DUEL", "COMBAT", "ASCENSION", "COUPE" };
static const char *const MEDAILLE_NOM[4] = { "PAS DE TROPHÉE", "BRONZE", "ARGENT", "OR" };
static const uint32_t MEDAILLE_COUL[4] = { 0x3a3c4e, 0xd08a4a, 0xd8dcea, 0xffd040 };
static int coupe_courses(const ag_epreuve_t *Ep) { int n = 1; if (Ep->type == AGE_COUPE) while (n < 4 && Ep->cp[n - 1] != 255) n++; return n; }
/* Nom d'une épreuve : « COURSE · CRÊTE DE CENDRE · NUIT » (coupe : la course m) */
static void epreuve_nom(const ag_epreuve_t *Ep, char *b, int n, int m) {
    const int piste = Ep->type == AGE_COUPE && m > 0 ? Ep->cp[m - 1] : Ep->piste, cond = Ep->type == AGE_COUPE && m > 0 ? Ep->cc[m - 1] : Ep->cond;
    char c[64] = "";
    if (cond & AGC_INVERSE) strcat(c, " · SENS INVERSE");
    if (cond & AGC_NUIT) strcat(c, " · NUIT");
    if (cond & AGC_BRUME) strcat(c, " · BRUME");
    if (cond & AGC_SANS_ARMES) strcat(c, " · SANS ARMES");
    if (Ep->type == AGE_COUPE) snprintf(b, (size_t)n, "COUPE · COURSE %d/%d%s", m + 1, coupe_courses(Ep), c);
    else if (Ep->type == AGE_DUEL && Ep->rival < AG.nec) snprintf(b, (size_t)n, "DUEL CONTRE %s · %s", AG.ec[Ep->rival].nom, piste < AG.npistes ? AG.pistes[piste].nom : "?");
    else snprintf(b, (size_t)n, "%s · %s%s", EPREUVE_NOM[Ep->type], piste < AG.npistes ? AG.pistes[piste].nom : "?", c);
}
static int ligue_points(int l) { int p = 0; for (int e = 0; e < AG.ligues[l].nep; e++) if (AG.ligues[l].ep[e].type != AGE_COUPE) p += AG.car.medaille[l][e]; return p; }
static int ligue_points_max(int l) { int p = 0; for (int e = 0; e < AG.ligues[l].nep; e++) if (AG.ligues[l].ep[e].type != AGE_COUPE) p += 3; return p; }
/* Épreuve ouverte : sa ligue l'est ; les deux premières, puis une médaille sur l'une des deux précédentes ; la coupe : assez de points */
static int epreuve_ouverte(int l, int e) {
    if (l > AG.car.ligue_max || e >= AG.ligues[l].nep) return 0;
    if (AG.car.medaille[l][e]) return 1;   /* (déjà gagnée) */
    if (AG.ligues[l].ep[e].type == AGE_COUPE) return ligue_points(l) >= AG.ligues[l].seuil;
    return e < 2 || AG.car.medaille[l][e - 1] || AG.car.medaille[l][e - 2];
}
static int champ_manche(void) {
    if (AG.ev_on) return AG.car_manche;
    int m = 0; for (int k = 0; k < AG.manche && k < AG.npistes; k++) m += piste_ouverte(k);
    return m;
}
static int champ_total(void) {
    if (AG.ev_on) return coupe_courses(&AG.ev);
    int t = 0; for (int k = 0; k < AG.npistes; k++) t += piste_ouverte(k);
    return t ? t : 1;
}
/* Tour de référence (contre-la-montre de la carrière) : chaque section à la vitesse la plus haute qu'elle permet : au plus la vitesse
   maxi de la catégorie, dans un virage celle que donne l'accélération de côté AG_REF_LAT (courbure entre ses voisines), en accélérant
   et en freinant au plus de AG_REF_ACC et AG_REF_FREIN (passes avant et arrière, deux tours) ; réglé d'après les tours des
   adversaires au simulateur (ref_essai) */
#define AG_REF_LAT 40.0f
#define AG_REF_ACC 7.0f
#define AG_REF_FREIN 30.0f
static void tour_reference_calculer(void) {
    const float vmax = AG_VMAX * CL.v, lat = AG_REF_LAT * CL.v * CL.w, acc = AG_REF_ACC * CL.a, fr = AG_REF_FREIN * CL.v;
    for (int i = 0; i < AG.n; i++) {
        const ag_sect_t *a = &AG.s[(i - 1 + AG.n) % AG.n], *b = &AG.s[(i + 1) % AG.n];
        const float c = clampf(a->fx * b->fx + a->fy * b->fy + a->fz * b->fz, -1, 1), ang = acosf(c), d = a->l + AG.s[i].l;
        const float k = d > 1e-3f ? ang / d : 0;   /* courbure */
        AG.s[i].vref = k > 1e-4f ? fminf(vmax, sqrtf(lat / k)) : vmax;
    }
    for (int tour = 0; tour < 2; tour++) {
        for (int i = 0; i < AG.n; i++) { const int j = (i + 1) % AG.n; const float v = sqrtf(AG.s[i].vref * AG.s[i].vref + 2 * acc * AG.s[i].l); if (AG.s[j].vref > v) AG.s[j].vref = v; }
        for (int i = AG.n - 1; i >= 0; i--) { const int j = (i + 1) % AG.n; const float v = sqrtf(AG.s[j].vref * AG.s[j].vref + 2 * fr * AG.s[i].l); if (AG.s[i].vref > v) AG.s[i].vref = v; }
    }
    float t = 0;
    for (int i = 0; i < AG.n; i++) t += AG.s[i].l / fmaxf(1, AG.s[i].vref);
    AG.t_ref = t;
}
/* Élimination : quand l'engin de tête boucle un tour, le dernier encore en piste sort ; le dernier qui reste a gagné */
static void elimination_maj(void) {
    if (!AG.ev_on || AG.ev.type != AGE_ELIM || AG.joueur < 0) return;
    int actifs = 0, tete = -1;
    for (int k = 0; k < AG.nn; k++) if (!AG.nav[k].detruit) { actifs++; if (AG.nav[k].tour > tete) tete = AG.nav[k].tour; }
    ag_nav_t *j = &AG.nav[AG.joueur];
    if (actifs > 1 && tete > AG.elim_n) {
        classer();
        int der = -1;
        for (int k = 0; k < AG.nn; k++) if (!AG.nav[k].detruit && (der < 0 || AG.nav[k].rang > AG.nav[der].rang)) der = k;
        ag_nav_t *n = &AG.nav[der];
        n->detruit = 1; n->elimine = 1; n->t_detruit = AG.chrono; n->arme = 0; n->charge = 0;
        boule(n->x, n->y, n->z, 0.7f);
        son_ici(n, SON_AG_ABSORBE, 0.8f, 0.6f);
        if (n->joueur) snprintf(AG.annonce, sizeof AG.annonce, "ÉLIMINÉ");
        else snprintf(AG.annonce, sizeof AG.annonce, "%s ÉLIMINÉ", AG.ec[n->ecurie].nom);
        AG.annonce_t = 1.6f;
        AG.elim_n++;
        actifs--;
    }
    if (actifs == 1 && !j->detruit && !j->fini) { j->fini = 1; j->fin = AG.chrono; }   /* le dernier en piste */
}
/* Médaille de l'épreuve qui vient de finir : la place, le meilleur tour en % de la référence, les coups, les paliers */
static int medaille_calculer(void) {
    const ag_nav_t *j = &AG.nav[AG.joueur];
    const ag_epreuve_t *Ep = &AG.ev;
    int v, petit = 1;
    switch (Ep->type) {
    case AGE_CLM: v = j->meilleur > 0 && AG.t_ref > 0 ? (int)ceilf(100 * j->meilleur / AG.t_ref) : 999; break;
    case AGE_COMBAT: v = j->coups; petit = 0; break;
    case AGE_ASCENSION: v = AG.palier; petit = 0; break;
    default: v = j->detruit && !j->elimine ? 99 : j->rang + 1; break;
    }
    for (int m = 3; m >= 1; m--) { const int o = Ep->obj[3 - m]; if (petit ? v <= o : v >= o) return m; }
    return 0;
}
static int medaille_coupe(void) {   /* le rang de l'écurie du joueur aux points de la coupe */
    int rang = 1;
    for (int k = 0; k < AG.nn; k++) if (AG.nav[k].ecurie != AG.ecurie && AG.points[AG.nav[k].ecurie] > AG.points[AG.ecurie]) rang++;
    for (int m = 3; m >= 1; m--) if (rang <= AG.ev.obj[3 - m]) return m;
    return 0;
}
/* Bilan : médaille gardée si meilleure, crédits (une médaille regagnée : un quart), écurie gagnée au duel, ligue suivante après un
   podium à la coupe (sa catégorie, ses pistes, ses armes), épreuves nouvellement ouvertes */
static void bilan_faire(int medaille) {
    ag_carriere_t *C = &AG.car;
    const int l = AG.car_l, e = AG.car_e, avant = C->medaille[l][e];
    int ouvertes = 0;
    for (int i = 0; i < AG.ligues[l].nep; i++) ouvertes -= epreuve_ouverte(l, i);
    memset(&AG.bilan, 0, sizeof AG.bilan);
    AG.bilan.medaille = medaille; AG.bilan.ancienne = avant;
    const float k = 1 + 0.5f * (float)l;
    const int cr = medaille > avant ? (int)((float)MEDAILLE_CR[medaille] * k) : medaille > 0 ? (int)((float)MEDAILLE_CR[medaille] * k / 4) : 0;
    C->credits += (uint32_t)cr; AG.bilan.credits = cr;
    if (medaille > avant) C->medaille[l][e] = (uint8_t)medaille;
    const ag_epreuve_t *Ep = &AG.ligues[l].ep[e];
    if (Ep->type == AGE_DUEL && medaille == 3 && Ep->rival < AG.nec && !ecurie_ouverte(Ep->rival)) { C->ecuries |= (uint8_t)(1 << Ep->rival); AG.bilan.ecurie = Ep->rival + 1; }
    if (Ep->type == AGE_COUPE && medaille > 0) {
        if (l + 1 < AG.nligues && C->ligue_max < l + 1) {
            const ag_ligue_t *N = &AG.ligues[l + 1];
            C->ligue_max = (uint8_t)(l + 1); C->classes |= (uint8_t)(1 << N->classe); C->pistes |= N->pistes; C->armes |= N->armes;
            AG.bilan.promu = 1;
        } else if (l + 1 == AG.nligues) AG.bilan.fin = 1;
    }
    for (int i = 0; i < AG.ligues[l].nep; i++) ouvertes += epreuve_ouverte(l, i);
    AG.bilan.ouvertes = ouvertes;
    carriere_sauver();
}
/* Lancer l'épreuve e de la ligue l (coupe : sa course m) : la piste avec ses conditions, la catégorie de la ligue, l'écurie du pilote */
static void epreuve_lancer(int l, int e, int m) {
    const ag_epreuve_t *Ep = &AG.ligues[l].ep[e];
    AG.ev_on = 1; AG.car_l = l; AG.car_e = e; AG.car_manche = m; AG.ev = *Ep;
    if (Ep->type == AGE_COUPE && m == 0) memset(AG.points, 0, sizeof AG.points);
    const int piste = Ep->type == AGE_COUPE && m > 0 ? Ep->cp[m - 1] : Ep->piste;
    AG.cond = Ep->type == AGE_COUPE && m > 0 ? Ep->cc[m - 1] : Ep->cond;
    AG.classe = AG.ligues[l].classe;
    AG.mode = Ep->type == AGE_COUPE ? AGM_CHAMPIONNAT : AGM_RAPIDE;
    piste_charger(piste < AG.npistes ? piste : 0);
    ag_course_lancer(AG.car.ecurie < AG.nec ? AG.car.ecurie : 0);
}
/* Calendrier : la piste de l'épreuve choisie (avec ses conditions) court derrière, en démonstration */
static void calendrier_voir(void) {
    const ag_ligue_t *Lg = &AG.ligues[AG.car_l];
    if (AG.sel >= Lg->nep) return;
    const ag_epreuve_t *Ep = &Lg->ep[AG.sel];
    AG.cond = Ep->cond; AG.classe = Lg->classe;
    if (Ep->piste < AG.npistes) piste_charger(Ep->piste);
    ag_course_lancer(-1);
}
static void carriere_maj(const eng_input_t *in, float dt) {
    const int n = AG.nligues + 1;   /* les ligues, puis le garage */
    const int s = menu_choix(in, n);
    if (APPUI(ENG_KEY_BACK)) { son_jouer(SON_ANNULER, 0.6f, 1); AG.ev_on = 0; if (AG.cond) { AG.cond = 0; piste_charger(AG.piste); ag_course_lancer(-1); } aller(AGP_MENU); return; }
    if (APPUI(ENG_KEY_OK)) {
        if (s == AG.nligues) { son_jouer(SON_VALIDER, 0.7f, 1); AG.ecurie = AG.car.ecurie; aller(AGP_GARAGE); vitrine(); return; }
        if (s > AG.car.ligue_max) { son_jouer(SON_ANNULER, 0.6f, 1); return; }
        son_jouer(SON_VALIDER, 0.7f, 1);
        AG.car_l = s; aller(AGP_CALENDRIER);
        AG.sel = 0;
        for (int e = 0; e < AG.ligues[s].nep; e++) if (epreuve_ouverte(s, e) && !AG.car.medaille[s][e]) { AG.sel = e; break; }   /* la première à faire */
        calendrier_voir();
        return;
    }
    demo_maj(dt, in);
}
static void calendrier_maj(const eng_input_t *in, float dt) {
    const ag_ligue_t *Lg = &AG.ligues[AG.car_l];
    const int avant = AG.sel, s = menu_choix(in, Lg->nep);
    if (s != avant) calendrier_voir();
    if (APPUI(ENG_KEY_BACK)) { son_jouer(SON_ANNULER, 0.6f, 1); aller(AGP_CARRIERE); AG.sel = AG.car_l; return; }
    if (APPUI(ENG_KEY_OK)) {
        if (!epreuve_ouverte(AG.car_l, s)) { son_jouer(SON_ANNULER, 0.6f, 1); return; }
        son_jouer(SON_VALIDER, 0.7f, 1);
        epreuve_lancer(AG.car_l, s, 0);
        return;
    }
    demo_maj(dt, in);
}
/* Garage : l'écurie (gauche, droite parmi les ouvertes), les améliorations (A : acheter le cran suivant) */
static void ag_garage_maj(const eng_input_t *in, float dt) {
    const int s = menu_choix(in, 6);   /* écurie, quatre améliorations, retour */
    AG.phase_t += dt;
    if (s == 0 && (APPUI(ENG_KEY_LEFT) || APPUI(ENG_KEY_RIGHT))) {
        const int sens = APPUI(ENG_KEY_LEFT) ? AG.nec - 1 : 1;
        for (int i = 0; i < AG.nec; i++) { AG.ecurie = (AG.ecurie + sens) % AG.nec; if (ecurie_ouverte(AG.ecurie)) break; }
        AG.nav[0].ecurie = AG.ecurie; AG.car.ecurie = (uint8_t)AG.ecurie; carriere_sauver(); son_jouer(SON_MENU, 0.5f, 1);
    }
    if (APPUI(ENG_KEY_BACK) || (APPUI(ENG_KEY_OK) && s == 5)) { son_jouer(SON_ANNULER, 0.6f, 1); ag_course_lancer(-1); aller(AGP_CARRIERE); AG.sel = AG.nligues; return; }
    if (APPUI(ENG_KEY_OK) && s >= 1 && s <= 4) {
        uint8_t *c = &AG.car.ameliore[s - 1];
        if (*c < 3 && AG.car.credits >= (uint32_t)AMELIORE_PRIX[*c]) {
            AG.car.credits -= (uint32_t)AMELIORE_PRIX[*c]; (*c)++; carriere_sauver();
            son_jouer(SON_AG_ABSORBE, 0.8f, 1); snprintf(AG.annonce, sizeof AG.annonce, "%s %d/3", AMELIORE_NOM[s - 1], *c); AG.annonce_t = 1.2f;
        } else son_jouer(SON_ANNULER, 0.6f, 1);
    }
}

/* ---------- Interface ---------- */
#define CYAN 0x5ff0ffu
#define MAGENTA 0xff4fd8u
#define AMBRE 0xffc24au
#define BLANC 0xf4f6ffu
#define GRIS 0x9aa0b8u
#define FOND 0x080612u
static uint32_t pal_hex(int i) { return ((uint32_t)E.w.pal[i][0] << 16) | ((uint32_t)E.w.pal[i][1] << 8) | E.w.pal[i][2]; }
static uint32_t couleur_ecurie(int e, int place) { return pal_hex(AG.ec[e].c[place]); }
static void temps_txt(char *b, int n, float t) {
    if (t <= 0) { snprintf(b, (size_t)n, "-:--.--"); return; }
    const int cs = (int)(t * 100 + 0.5f);
    snprintf(b, (size_t)n, "%d:%02d.%02d", cs / 6000, cs / 100 % 60, cs % 100);
}
static void barre(float x, float y, float w, float v, uint32_t c) {
    ui_rect(x, y, w, 6, ui_rgb(0x20222e), 220);
    ui_rect(x, y, w * clampf(v, 0, 1), 6, ui_rgb(c), 255);
}
/* Bords de toute l'image dans le repère de l'interface (466 au milieu) */
#define BX0 ((float)-ui_ox)
#define BX1 ((float)(ui_img_w - ui_ox))
#define BY0 ((float)-ui_oy)
#define BY1 ((float)(ui_img_h - ui_oy))
static void bandeau(float y, float h) {
    ui_rect(BX0, y, BX1 - BX0, h, ui_rgb(FOND), 190);
    ui_line(BX0, y, BX1, y, 2, ui_rgb(CYAN), 255); ui_line(BX0, y + h, BX1, y + h, 2, ui_rgb(MAGENTA), 255);
}
/* Images du jeu (AGIM) : par nom ; plein écran (recadrée pour le couvrir) ; logo centré en cx, haut en y, au plus w de large */
static const ag_image_t *image_jeu(const char *nom) {
    for (int k = 0; k < AG.nim; k++) if (!strcmp(AG.im_nom[k], nom)) return &AG.im[k];
    return NULL;
}
static void image_plein(const ag_image_t *im, int a) {
    const float W = BX1 - BX0, H = BY1 - BY0, s = fmaxf(W / (float)im->w, H / (float)im->h);
    ui_sprite(im->px, im->w, im->pal, im->w, im->h, (BX0 + BX1 - (float)im->w * s) / 2, (BY0 + BY1 - (float)im->h * s) / 2, s, a, 233, 233, 4000);
}
static float logo(const ag_image_t *im, float cx, float y, float s) {   /* rend la hauteur dessinée */
    ui_sprite(im->px, im->w, im->pal, im->w, im->h, cx - (float)im->w * s / 2, y, s, 255, 233, 233, 4000);
    return (float)im->h * s;
}
static const char *const IM_CLASSE[AG_CLASSES] = { "ligue_cendre", "ligue_braise", "ligue_flamme", "ligue_foudre", "ligue_nova" };
static void titre_ecran(const char *t, const char *s) {
    bandeau(24, 64);
    const ag_image_t *lg = t == AG.jeu ? image_jeu("logo_petit") : NULL;
    if (lg) logo(lg, 233, 14, 2.0f);   /* (le logo à la place du nom du jeu : 2 unités par pixel, sa taille sur la LDK) */
    else ui_text(ui_fit(FONT_DISP, t, 420, 30, 18), 233, 64, UI_CENTER, ui_rgb(BLANC), 255, t);
    if (s) ui_text(ui_fit(FONT_MONO, s, 420, 18, 14), 233, lg ? 96 : 82, UI_CENTER, ui_rgb(CYAN), 255, s);
}
static void aide(const char *t) { ui_text(ui_fit(FONT_MONO, t, 460, 18, 12), 233, BY1 - 10, UI_CENTER, ui_rgb(GRIS), 220, t); }
/* Liste de lignes : la choisie sur un fond cyan */
static void liste(const char *const *l, int n, int sel, float y0, float dy) {
    for (int k = 0; k < n; k++) {
        const float y = y0 + dy * (float)k;
        if (k == sel) { ui_rect(123, y - 24, 220, 32, ui_rgb(CYAN), 60); ui_line(123, y + 8, 343, y + 8, 2, ui_rgb(CYAN), 255); }
        ui_text(ui_fit(FONT_DISP, l[k], 210, 22, 16), 233, y, UI_CENTER, ui_rgb(k == sel ? BLANC : GRIS), 255, l[k]);
    }
}
/* Emblème de l'écurie (AGEM), sinon un hexagone à ses couleurs avec son numéro */
static void embleme(int e, float cx, float cy, float taille) {
    const ag_ecurie_t *C = &AG.ec[e];
    if (C->embleme >= 0) {
        const ag_image_t *im = &AG.em[C->embleme];
        const float s = taille / (float)(im->w > im->h ? im->w : im->h);
        ui_sprite(im->px, im->w, im->pal, im->w, im->h, cx - (float)im->w * s / 2, cy - (float)im->h * s / 2, s, 255, cx, cy, 4000);
        return;
    }
    float hex[12];
    ui_hex_path(cx, cy, taille, taille * 0.86f, hex);
    ui_poly(hex, 6, ui_rgb(couleur_ecurie(e, AGC_COQUE)), 255);
    ui_outline(hex, 6, 3, ui_rgb(couleur_ecurie(e, AGC_BANDE)), 255);
    char b[12];
    snprintf(b, sizeof b, "%d", C->numero);
    ui_text(ui_font(FONT_DISP, taille > 40 ? 30 : 16), cx, cy + (taille > 40 ? 11 : 6), UI_CENTER, ui_rgb(couleur_ecurie(e, AGC_SOMBRE)), 255, b);
}
/* Plan de la piste (ses 64 points) dans un carré ; engins en points ; ligne de départ */
static void plan(float cx, float cy, float cote, int engins) {
    const float k = cote;
    for (int i = 0; i < 64; i++) {
        const int j = (i + 1) % 64;
        const float x0 = cx + AG.plan[i][0] * k, y0 = cy - AG.plan[i][1] * k, x1 = cx + AG.plan[j][0] * k, y1 = cy - AG.plan[j][1] * k;
        ui_line(x0, y0, x1, y1, cote > 120 ? 4 : 3, ui_rgb(0x1a1c2c), 230);
        ui_line(x0, y0, x1, y1, cote > 120 ? 2 : 1.5f, ui_rgb(CYAN), 255);
    }
    ui_circle(cx + AG.plan[0][0] * k, cy - AG.plan[0][1] * k, cote > 120 ? 5 : 3, ui_rgb(BLANC), 255);
    for (int m = 0; engins && m < AG.nn; m++) {
        const ag_nav_t *n = &AG.nav[m];
        const float x = cx + (n->x - plan_x0) * plan_k * k, y = cy - (n->z - plan_z0) * plan_k * k;
        ui_circle(x, y, n->joueur ? 4 : 2.5f, ui_rgb(n->joueur ? AMBRE : couleur_ecurie(n->ecurie, AGC_COQUE)), 255);
    }
}
/* Vue cockpit : la vitesse, la carte, la place et le tour sur les écrans du tableau de bord (render.c, ag_cockpit_ecran ; 14/11/2026,
   demande de l'utilisateur : « des emplacements d'affichage du hud de vitesse et de la carte comme un écran du cockpit ») ; 0 : pas de
   cockpit, l'interface habituelle */
static int ecrans_cockpit(const ag_nav_t *j) {
    float e[3][4];
    for (int k = 0; k < 3; k++) if (!ag_cockpit_ecran(k, &e[k][0], &e[k][1], &e[k][2], &e[k][3])) return 0;
    char b[48];
    /* vitesse : le nombre, KM/H, la barre au bas de l'écran */
    {
        const float x0 = e[0][0], y0 = e[0][1], w = e[0][2] - x0, h = e[0][3] - y0;
        snprintf(b, sizeof b, "%d", (int)(j->vitesse * AG_KMH + 0.5f));
        /* (le nombre au milieu, KM/H en petit dessous : côte à côte, KM/H débordait de l'écran ; 14/11/2026, retour de l'utilisateur) */
        ui_text(ui_fit(FONT_DISP, b, w * 0.8f, (int)(h * 0.5f), 8), x0 + w * 0.5f, y0 + h * 0.5f, UI_CENTER, ui_rgb(j->boost > 0 ? AMBRE : BLANC), 255, b);
        ui_text(ui_fit(FONT_MONO, "KM/H", w * 0.5f, (int)(h * 0.18f), 6), x0 + w * 0.5f, y0 + h * 0.72f, UI_CENTER, ui_rgb(GRIS), 255, "KM/H");
        const int nseg = 12;
        const float m = w * 0.08f, sw = (w - 2 * m) / (float)nseg, v = j->vitesse / (AG_VMAX * CL.v * AG_BOOST_V), sh = fmaxf(2, h * 0.12f);
        for (int s = 0; s < nseg; s++) {
            const int on = (float)s < v * nseg;
            ui_rect(x0 + m + (float)s * sw, y0 + h * 0.8f, sw * 0.8f, sh, ui_rgb(on ? (s >= 9 ? MAGENTA : s >= 6 ? AMBRE : CYAN) : 0x24263a), on ? 255 : 200);
        }
    }
    /* carte : la piste et les engins, au milieu de l'écran de gauche */
    {
        const float w = e[1][2] - e[1][0], h = e[1][3] - e[1][1];
        plan(e[1][0] + w / 2, e[1][1] + h / 2, fminf(w, h) * 0.86f, 1);
    }
    /* place et tour, à droite */
    {
        const float x0 = e[2][0], y0 = e[2][1], w = e[2][2] - x0, h = e[2][3] - y0, cx = x0 + w / 2;
        if (AG.nn > 1) {
            snprintf(b, sizeof b, "%d/%d", j->rang + 1, AG.nn);
            ui_text(ui_fit(FONT_DISP, b, w * 0.8f, (int)(h * 0.42f), 10), cx, y0 + h * 0.5f, UI_CENTER, ui_rgb(BLANC), 255, b);
        }
        snprintf(b, sizeof b, "TOUR %d/%d", j->tour + 1 < 1 ? 1 : j->tour + 1 > AG.tours ? AG.tours : j->tour + 1, AG.tours);
        ui_text(ui_fit(FONT_MONO, b, w * 0.84f, (int)(h * 0.2f), 8), cx, y0 + h * (AG.nn > 1 ? 0.82f : 0.6f), UI_CENTER, ui_rgb(CYAN), 255, b);
    }
    return 1;
}
/* Icône d'une arme (14/11/2026, demande de l'utilisateur : des icônes plutôt que le nom) : pictogramme vectoriel dans un cercle de
   rayon r, de la couleur de l'arme : onde du SÉISME, fusée du MISSILE, mine à pointes, éclair du RAYON, écu du BOUCLIER, chevrons
   du TURBO */
static const uint32_t ARME_COUL[ARME_N] = { 0, 0xff8a2a, 0xff4040, 0xffd23a, 0xff4fd8, 0x40e0ff, 0x5cff7a };
static void arme_icone(int a, float cx, float cy, float r, int alpha) {
    if (a <= 0 || a >= ARME_N) return;
    const uint16_t c = ui_rgb(ARME_COUL[a]), blanc = ui_rgb(BLANC);
    float p[16];
    #define PT(i, X, Y) (p[2 * (i)] = cx + (X) * r, p[2 * (i) + 1] = cy + (Y) * r)
    switch (a) {
    case ARME_SEISME: {   /* onde : zigzag au-dessus d'une ligne de sol */
        static const float Z[7][2] = { { -0.9f, 0.1f }, { -0.6f, -0.35f }, { -0.3f, 0.45f }, { 0, -0.65f }, { 0.3f, 0.45f }, { 0.6f, -0.35f }, { 0.9f, 0.1f } };
        for (int i = 0; i < 6; i++) ui_line(cx + Z[i][0] * r, cy + Z[i][1] * r, cx + Z[i + 1][0] * r, cy + Z[i + 1][1] * r, r * 0.17f, c, alpha);
        ui_line(cx - 0.85f * r, cy + 0.72f * r, cx + 0.85f * r, cy + 0.72f * r, r * 0.12f, blanc, alpha);
        break;
    }
    case ARME_MISSILE:   /* fusée pointée vers le haut, ailerons, flamme */
        PT(0, 0, -0.9f); PT(1, 0.24f, -0.45f); PT(2, 0.24f, 0.45f); PT(3, -0.24f, 0.45f); PT(4, -0.24f, -0.45f);
        ui_poly(p, 5, blanc, alpha);
        PT(0, 0.24f, 0.05f); PT(1, 0.58f, 0.55f); PT(2, 0.24f, 0.45f); ui_poly(p, 3, c, alpha);
        PT(0, -0.24f, 0.05f); PT(1, -0.24f, 0.45f); PT(2, -0.58f, 0.55f); ui_poly(p, 3, c, alpha);
        PT(0, -0.16f, 0.5f); PT(1, 0.16f, 0.5f); PT(2, 0, 0.95f); ui_poly(p, 3, ui_rgb(0xffb030), alpha);
        ui_circle(cx, cy - 0.3f * r, 0.1f * r, c, alpha);
        break;
    case ARME_MINES:   /* boule à huit pointes */
        for (int i = 0; i < 8; i++) { const float t = (float)i * 0.785398f; ui_line(cx + cosf(t) * 0.35f * r, cy + sinf(t) * 0.35f * r, cx + cosf(t) * 0.88f * r, cy + sinf(t) * 0.88f * r, r * 0.13f, c, alpha); }
        ui_circle(cx, cy, 0.5f * r, c, alpha);
        ui_circle(cx - 0.15f * r, cy - 0.15f * r, 0.14f * r, blanc, alpha);
        break;
    case ARME_RAYON:   /* éclair */
        PT(0, 0.12f, -0.95f); PT(1, -0.45f, 0.12f); PT(2, -0.04f, 0.12f); PT(3, -0.18f, 0.95f); PT(4, 0.48f, -0.18f); PT(5, 0.06f, -0.18f);
        ui_poly(p, 6, c, alpha); ui_outline(p, 6, r * 0.06f, blanc, alpha);
        break;
    case ARME_BOUCLIER:   /* écu */
        PT(0, -0.68f, -0.78f); PT(1, 0.68f, -0.78f); PT(2, 0.68f, 0.05f); PT(3, 0, 0.9f); PT(4, -0.68f, 0.05f);
        ui_poly(p, 5, c, alpha * 3 / 5); ui_outline(p, 5, r * 0.13f, c, alpha);
        ui_line(cx, cy - 0.55f * r, cx, cy + 0.55f * r, r * 0.1f, blanc, alpha);
        break;
    case ARME_TURBO:   /* deux chevrons vers le haut (l'avant) */
        for (int k = 0; k < 2; k++) {
            const float y = (k ? 0.45f : -0.2f) * r;
            PT(0, -0.75f, y / r + 0.25f); PT(1, 0, y / r - 0.45f); PT(2, 0.75f, y / r + 0.25f); PT(3, 0.75f, y / r + 0.55f); PT(4, 0, y / r - 0.15f); PT(5, -0.75f, y / r + 0.55f);
            ui_poly(p, 6, k ? c : blanc, alpha);
        }
        break;
    }
    #undef PT
}
static void ecran_course(void) {
    const ag_nav_t *j = &AG.nav[AG.joueur];
    const float X0 = BX0 + 18, X1 = BX1 - 18, Y0 = BY0 + 14, Y1 = BY1 - 14;
    char b[160];
    const int ck = ecrans_cockpit(j);
    if (j->arme > 0 && j->arme < ARME_N) {   /* arme portée : son icône dans un hexagone en haut au milieu (pulse en la ramassant) ; le rayon : sa charge dessous */
        float hex[12];
        const float pulse = 1 + 0.25f * clampf(1 - (AG.chrono - j->t_arme) * 3, 0, 1), cy = Y0 + 46;
        ui_hex_path(233, cy, 48 * pulse, 42 * pulse, hex);
        ui_poly(hex, 6, ui_rgb(FOND), 210);
        ui_outline(hex, 6, 2.5f, ui_rgb(ARME_COUL[j->arme]), 255);
        arme_icone(j->arme, 233, cy, 32 * pulse, 255);
        if (j->arme == ARME_RAYON && j->charge > 0) {
            const float f = clampf(j->charge / AG_RAYON_CHARGE, 0, 1);
            ui_rect(183, cy + 50, 100, 6, ui_rgb(0x24263a), 220);
            ui_rect(183, cy + 50, 100 * f, 6, ui_rgb(f >= 1 ? MAGENTA : CYAN), 255);
        }
    }
    {   /* missile qui vise le joueur */
        int vise = 0;
        for (int p = 0; p < AG_PROJ_MAX; p++) if (AG.proj[p].on && AG.proj[p].cible == AG.joueur) vise = 1;
        if (vise && fmodf(E.clock, 0.4f) < 0.25f) { arme_icone(ARME_MISSILE, 190, Y0 + 116, 14, 255); ui_text(ui_fit(FONT_DISP, "MISSILE", 160, 22, 14), 243, Y0 + 124, UI_CENTER, ui_rgb(MAGENTA), 255, "MISSILE"); }
    }
    {   /* énergie : dix crans en haut à gauche (vue extérieure : sous la place), rouge clignotant sous 30 % ; bouclier : liseré cyan */
        const float e = clampf(j->energie, 0, 1), x = ck ? X0 + 14 : X0, y = ck ? Y0 + 22 : Y0 + 70;   /* (vue cockpit : sur la vitre, sous le cadre) */
        const int faible = e < 0.3f, eteint = faible && fmodf(E.clock, 0.5f) < 0.2f;
        for (int s = 0; s < 10; s++) {
            const int on = (float)s < e * 10 - 0.01f;
            ui_rect(x + (float)s * 12, y, 10, 8, ui_rgb(on && !eteint ? (faible ? MAGENTA : e < 0.55f ? AMBRE : CYAN) : 0x24263a), on ? 255 : 200);
        }
        ui_text(ui_font(FONT_MONO, 12), x, y + 22, UI_LEFT, ui_rgb(faible ? MAGENTA : GRIS), 255, j->bouclier > 0 ? "BOUCLIER" : "ÉNERGIE");
        if (j->bouclier > 0) ui_outline((const float[8]){ x - 3, y - 3, x + 121, y - 3, x + 121, y + 11, x - 3, y + 11 }, 4, 2, ui_rgb(CYAN), 255);
    }
    if (j->touche > 0) {   /* coup reçu : bords magenta */
        const int a = (int)(170 * clampf(j->touche / 0.5f, 0, 1));
        for (int k = 0; k < 3; k++) { const float e = 5.0f * (float)(k + 1); ui_rect(BX0, BY0, e, BY1 - BY0, ui_rgb(MAGENTA), a / 3); ui_rect(BX1 - e, BY0, e, BY1 - BY0, ui_rgb(MAGENTA), a / 3); }
    }
    /* Place (pas en contre-la-montre) */
    if (AG.nn > 1 && !ck) {
        snprintf(b, sizeof b, "%d", j->rang + 1);
        const float wp = ui_text(ui_font(FONT_DISP, 48), X0, Y0 + 40, UI_LEFT, ui_rgb(BLANC), 255, b);
        snprintf(b, sizeof b, "/%d", AG.nn);
        ui_text(ui_font(FONT_DISP, 22), X0 + wp + 4, Y0 + 40, UI_LEFT, ui_rgb(GRIS), 255, b);
        ui_text(ui_font(FONT_MONO, 14), X0, Y0 + 58, UI_LEFT, ui_rgb(CYAN), 255, "PLACE");
    }
    /* Tour et temps */
    snprintf(b, sizeof b, "TOUR %d/%d", j->tour + 1 < 1 ? 1 : j->tour + 1 > AG.tours ? AG.tours : j->tour + 1, AG.tours);
    if (!ck) ui_text(ui_font(FONT_DISP, 24), X1, Y0 + 24, UI_RIGHT, ui_rgb(BLANC), 255, b);
    temps_txt(b, sizeof b, j->fini ? j->fin - j->t_tour : AG.chrono - (j->tour >= 0 ? j->t_tour : 0));
    const float yt = ck ? Y0 - 24 : Y0;   /* (vue cockpit : le tour est sur son écran, le temps remonte) */
    ui_text(ui_font(FONT_MONO, 20), X1, yt + 48, UI_RIGHT, ui_rgb(CYAN), 255, b);
    {
        char t[24];
        const float ref = j->meilleur > 0 ? j->meilleur : RECORD(AG.piste);
        temps_txt(t, sizeof t, ref);
        snprintf(b, sizeof b, "%s %s", j->meilleur > 0 ? "MEILLEUR" : "RECORD", t);
        if (ref > 0) ui_text(ui_font(FONT_MONO, 14), X1, yt + 68, UI_RIGHT, ui_rgb(GRIS), 255, b);
    }
    /* Vitesse (vue cockpit : sur ses écrans, ainsi que la carte) */
    if (!ck) {
    snprintf(b, sizeof b, "%d", (int)(j->vitesse * AG_KMH + 0.5f));
    ui_text(ui_font(FONT_DISP, 40), X1 - 50, Y1 - 12, UI_RIGHT, ui_rgb(j->boost > 0 ? AMBRE : BLANC), 255, b);
    ui_text(ui_font(FONT_MONO, 14), X1, Y1 - 12, UI_RIGHT, ui_rgb(GRIS), 255, "KM/H");
    const int nseg = 16;
    const float sw = 9, x0 = X1 - nseg * (sw + 2), yb = Y1 - 56, v = j->vitesse / (AG_VMAX * CL.v * AG_BOOST_V);
    for (int s = 0; s < nseg; s++) {
        const float h = 6 + 1.2f * (float)s, on = (float)s < v * nseg;
        ui_rect(x0 + (float)s * (sw + 2), yb - h, sw, h, ui_rgb(on ? (s >= 12 ? MAGENTA : s >= 8 ? AMBRE : CYAN) : 0x24263a), on ? 255 : 200);
    }
    /* Carte de la piste, en bas à gauche */
    plan(X0 + 50, Y1 - 52, 90, 1);
    }
    /* Accélération : liseré ambre aux bords de l'image */
    if (j->boost > 0) {
        const int a = (int)(140 * clampf(j->boost / AG_BOOST_T, 0, 1));
#if ENG_DYN
        ui_halo_bord(18, ui_rgb(AMBRE), a);
#else   /* (ui_halo_bord : plein écran seulement) trois bandes de plus en plus claires vers le bord */
        for (int k = 0; k < 3; k++) {
            const float e = 6.0f * (float)(k + 1);
            ui_rect(BX0, BY0, e, BY1 - BY0, ui_rgb(AMBRE), a / 3); ui_rect(BX1 - e, BY0, e, BY1 - BY0, ui_rgb(AMBRE), a / 3);
        }
#endif
    }
    if (AG.annonce_t > 0 && AG.annonce[0]) ui_text(ui_fit(FONT_DISP, AG.annonce, 360, 30, 18), 233, 150, UI_CENTER, ui_rgb(AMBRE), (int)(255 * clampf(AG.annonce_t * 2, 0, 1)), AG.annonce);
    if (AG.ev_on) {   /* carrière : l'objectif en cours, en bas au milieu */
        char o[160];
        const int t = AG.ev.type;
        o[0] = 0;
        if (t == AGE_COMBAT) snprintf(o, sizeof o, "COUPS %d · OR %d", j->coups, AG.ev.obj[0]);
        else if (t == AGE_ASCENSION) snprintf(o, sizeof o, "PALIER %d · OR %d", AG.palier, AG.ev.obj[0]);
        else if (t == AGE_CLM) { char r[16]; temps_txt(r, sizeof r, AG.t_ref * AG.ev.obj[0] / 100.0f); snprintf(o, sizeof o, "OR %s", r); }
        else if (t == AGE_ELIM) { int a = 0; for (int k = 0; k < AG.nn; k++) a += !AG.nav[k].detruit; snprintf(o, sizeof o, "ÉLIMINATION · %d EN PISTE", a); }
        if (o[0]) ui_text(ui_fit(FONT_MONO, o, 200, 14, 10), 233, Y1 - 6, UI_CENTER, ui_rgb(AMBRE), 230, o);
    }
    /* Départ donné : GO dans le panneau du compte à rebours, qui s'efface */
    if (AG.phase_t < 0.8f && AG.phase == AGP_COURSE) {
        const int a = (int)(255 * (1 - AG.phase_t / 0.8f));
        float hex[12];
        ui_hex_path(233, 190, 150, 92, hex);
        ui_poly(hex, 6, ui_rgb(FOND), a * 200 / 255);
        ui_outline(hex, 6, 3, ui_rgb(AMBRE), a);
        ui_text(ui_font(FONT_DISP, 40), 233, 205, UI_CENTER, ui_rgb(AMBRE), a, "GO");
    }
}
static void ecran_depart(void) {
    const int s = 3 - (int)floorf(AG.phase_t);
    char b[12];   /* (format-truncation de l'ESP-IDF : un %d peut faire 11 caractères) */
    snprintf(b, sizeof b, "%d", s < 1 ? 1 : s);
    /* Présentation de la piste, puis le compte à rebours dans un panneau hexagonal qui se resserre au fil de la seconde */
    titre_ecran(AG.nom, AG.lieu);
    {
        char cl[32];
        snprintf(cl, sizeof cl, "CATÉGORIE %s", CL.nom);
        ui_text(ui_fit(FONT_MONO, cl, 300, 16, 12), 233, 290, UI_CENTER, ui_rgb(AMBRE), 255, cl);
    }
    const float f = AG.phase_t - floorf(AG.phase_t), k = 1.15f - 0.25f * f;
    const uint32_t c = s <= 1 ? AMBRE : s == 2 ? MAGENTA : CYAN;
    float hex[12];
    ui_hex_path(233, 190, 120 * k, 92 * k, hex);
    ui_poly(hex, 6, ui_rgb(FOND), 200);
    ui_outline(hex, 6, 3, ui_rgb(c), 255);
    ui_text(ui_font(FONT_DISP, 40), 233, 205, UI_CENTER, ui_rgb(BLANC), 255, b);
    aide(AG.vue ? "B : VUE EXTÉRIEURE" : "B : VUE COCKPIT");
    if (AG.ev_on) {   /* carrière : l'épreuve et sa règle */
        char m[64];
        epreuve_nom(&AG.ev, m, sizeof m, AG.car_manche);
        ui_text(ui_fit(FONT_MONO, m, 400, 18, 12), 233, 262, UI_CENTER, ui_rgb(AMBRE), 255, m);
    } else if (AG.mode == AGM_CHAMPIONNAT) {
        char m[40];
        snprintf(m, sizeof m, "CHAMPIONNAT · COURSE %d/%d", champ_manche() + 1, champ_total());
        ui_text(ui_fit(FONT_MONO, m, 360, 18, 14), 233, 262, UI_CENTER, ui_rgb(AMBRE), 255, m);
    }
}
static void ecran_titre(void) {
    const ag_image_t *fond = image_jeu("titre"), *lg = image_jeu("logo");
    if (fond) image_plein(fond, 255);   /* l'illustration de Forge sur la démonstration */
    float y = 130;
    if (lg) {
        const float s = fminf(2.0f, (BX1 - BX0) * 0.86f / (float)lg->w);
        y = 40 + logo(lg, 233, 40, s) + 20;
    } else {
        bandeau(52, 92);
        ui_text(ui_fit(FONT_DISP, AG.jeu, 440, 40, 24), 233, 104, UI_CENTER, ui_rgb(BLANC), 255, AG.jeu);
    }
    if (fond) ui_rect(53, y - 16, 360, 22, ui_rgb(FOND), 170);
    ui_text(ui_fit(FONT_MONO, "LIGUE DES CIRCUITS DE LUCERNE", 420, 18, 14), 233, y, UI_CENTER, ui_rgb(CYAN), 255, "LIGUE DES CIRCUITS DE LUCERNE");
    if (fmodf(E.clock, 1.2f) < 0.8f) ui_text(ui_font(FONT_DISP, 22), 233, 400, UI_CENTER, ui_rgb(AMBRE), 255, "APPUIE SUR A");
}
static void ecran_menu(void) {
    static const char *const L[5] = { "CARRIÈRE", "COURSE RAPIDE", "CHAMPIONNAT", "CONTRE-LA-MONTRE", "RÉGLAGES" };
    static const char *const D[5] = { "DE LIGUE EN LIGUE, JUSQU'AU SYSTÈME SOLAIRE", "UNE PISTE, CONTRE LES AUTRES ÉCURIES", "TOUTES LES PISTES OUVERTES, AUX POINTS", "SEUL CONTRE LE CHRONO", "SON, COURSE, AFFICHAGE" };
    titre_ecran(AG.jeu, "MENU PRINCIPAL");
    ui_rect(113, 150, 240, 220, ui_rgb(FOND), 170);
    liste(L, 5, AG.sel, 186, 42);
    ui_text(ui_fit(FONT_MONO, D[AG.sel], 400, 18, 12), 233, 392, UI_CENTER, ui_rgb(CYAN), 255, D[AG.sel]);
    aide("HAUT, BAS : CHOISIR · A : VALIDER · B : RETOUR");
}
static void ecran_ecurie(void) {
    const ag_ecurie_t *C = &AG.ec[AG.ecurie];
    static const char *const M[4] = { "COURSE RAPIDE", "CHAMPIONNAT", "CONTRE-LA-MONTRE", "CARRIÈRE : TON ÉCURIE" };
    titre_ecran("ÉCURIE", M[AG.mode]);
    /* Fiche : emblème, nom, devise, barres ; l'engin tourne derrière (vitrine) */
    const float y = 300;
    ui_rect(BX0, y, BX1 - BX0, 120, ui_rgb(FOND), 200);
    ui_rect(BX0, y, BX1 - BX0, 4, ui_rgb(couleur_ecurie(AG.ecurie, AGC_COQUE)), 255);
    ui_rect(BX0, y + 4, BX1 - BX0, 2, ui_rgb(couleur_ecurie(AG.ecurie, AGC_BANDE)), 255);
    embleme(AG.ecurie, 70, y + 62, 84);
    char b[96];
    snprintf(b, sizeof b, "<  %s  >", C->nom);   /* (« ‹ » n'est pas dans la police) */
    ui_text(ui_fit(FONT_DISP, b, 260, 30, 18), 270, y + 36, UI_CENTER, ui_rgb(BLANC), 255, b);
    ui_text(ui_fit(FONT_MONO, C->devise, 260, 14, 10), 270, y + 56, UI_CENTER, ui_rgb(couleur_ecurie(AG.ecurie, AGC_BORD)), 255, C->devise);
    static const char *const NOMS[3] = { "VITESSE", "POUSSÉE", "MANIABILITÉ" };
    const int var = AG.variante && C->modele2 >= 0;
    const float vals[3] = { C->v * (var ? AG_VAR_V : 1), C->a * (var ? AG_VAR_A : 1), C->m * (var ? AG_VAR_M : 1) };
    if (C->modele2 >= 0) {   /* le modèle (deux par écurie) */
        snprintf(b, sizeof b, "MODÈLE %d/2 · %s", var + 1, AG.mod[var ? C->modele2 : C->modele].nom);
        ui_text(ui_fit(FONT_MONO, b, 300, 14, 10), 233, y - 10, UI_CENTER, ui_rgb(AMBRE), 255, b);
    }
    for (int k = 0; k < 3; k++) {
        ui_text(ui_font(FONT_MONO, 12), 150, y + 80 + (float)k * 14, UI_LEFT, ui_rgb(GRIS), 255, NOMS[k]);
        barre(260, y + 72 + (float)k * 14, 150, (vals[k] - 0.85f) / 0.3f, k == 0 ? CYAN : k == 1 ? AMBRE : MAGENTA);
    }
    aide(C->modele2 >= 0 ? "GAUCHE, DROITE : ÉCURIE · HAUT : MODÈLE · A : CHOISIR" : "GAUCHE, DROITE : ÉCURIE · A : CHOISIR · B : RETOUR");
}
static void ecran_piste(void) {
    titre_ecran("PISTE", AG.mode == AGM_CLM ? "CONTRE-LA-MONTRE" : "COURSE RAPIDE");
    /* Aperçu : le circuit entier vu de haut, en basse définition, qui tourne (render.c, ag_image : AGP_PISTE) ; sa fiche en bas */
    ui_rect(BX0, 330, BX1 - BX0, 100, ui_rgb(FOND), 200);
    char b[96], t[24];   /* (96 : format-truncation de l'ESP-IDF, qui compte deux %d de 11 caractères et le temps entier) */
    snprintf(b, sizeof b, "<  %s  >", AG.nom);
    ui_text(ui_fit(FONT_DISP, b, 300, 24, 16), 233, 358, UI_CENTER, ui_rgb(BLANC), 255, b);
    snprintf(b, sizeof b, "%s · %d / %d%s", AG.lieu, AG.piste + 1, AG.npistes, AG.cond == AGC_INVERSE ? " · SENS INVERSE" : "");
    ui_text(ui_fit(FONT_MONO, b, 300, 16, 12), 233, 378, UI_CENTER, ui_rgb(CYAN), 255, b);
    temps_txt(t, sizeof t, RECORD(AG.piste));
    snprintf(b, sizeof b, "%s · %d M · %d TOURS · RECORD %s", CL.nom, (int)(AG.L * 5 + 0.5f), tours_classe(AG.reg.tours), t);
    ui_text(ui_fit(FONT_MONO, b, 300, 14, 11), 233, 402, UI_CENTER, ui_rgb(AMBRE), 255, b);
    aide("GAUCHE, DROITE : PISTE · HAUT : SENS · A : COURIR");
}
static void ecran_reglages(void) {
    static const char *const L[RG_N] = { "VOLUME GÉNÉRAL", "MUSIQUE", "EFFETS", "MOTEURS", "INTERFACE", "TOURS", "ADVERSAIRES", "DIFFICULTÉ", "VITESSE", "TRAMAGE", "VUE", "RETOUR" };
    static const char *const DIFF[3] = { "FACILE", "NORMALE", "DIFFICILE" };
    if (AG.reglages_retour == AGP_COURSE) ui_voile_image(ui_rgb(0x000000), 150);
    titre_ecran("RÉGLAGES", "LE MIXAGE ET LA COURSE");
    ui_rect(53, 104, 360, 27 * RG_N + 12, ui_rgb(FOND), 200);
    for (int k = 0; k < RG_N; k++) {
        const float y = 128 + 27 * (float)k;
        const int on = k == AG.sel;
        if (on) ui_rect(53, y - 21, 360, 28, ui_rgb(CYAN), 50);
        ui_text(ui_fit(FONT_DISP, L[k], 170, 18, 14), 66, y, UI_LEFT, ui_rgb(on ? BLANC : GRIS), 255, L[k]);
        char b[24];
        if (k <= RG_INTERFACE) {   /* barre en 20 crans, comme SYSTÈME › SON d'Echoes */
            for (int c = 0; c < 20; c++) ui_rect(236 + (float)c * 8, y - 13, 6, 12, ui_rgb(c < AG.reg.vol[k] ? (k ? CYAN : AMBRE) : 0x24263a), 255);
            continue;
        }
        if (k == RG_TOURS) snprintf(b, sizeof b, "%d", AG.reg.tours);
        else if (k == RG_ADVERSAIRES) snprintf(b, sizeof b, "%d", AG.reg.adversaires);
        else if (k == RG_DIFFICULTE) snprintf(b, sizeof b, "%s", DIFF[AG.reg.difficulte]);
        else if (k == RG_VITESSE) snprintf(b, sizeof b, "%s", CL.nom);
        else if (k == RG_TRAMAGE) snprintf(b, sizeof b, "%s", AG.reg.tramage ? "OUI" : "NON");
        else if (k == RG_VUE) snprintf(b, sizeof b, "%s", AG.vue ? "COCKPIT" : "EXTÉRIEURE");
        else continue;
        ui_text(ui_fit(FONT_DISP, b, 150, 18, 14), 316, y, UI_CENTER, ui_rgb(on ? AMBRE : BLANC), 255, b);
    }
    aide("HAUT, BAS : CHOISIR · GAUCHE, DROITE : RÉGLER · B : RETOUR");
}
static void ecran_resultats(void) {
    const float a = clampf((AG.phase_t - 0.6f) * 2, 0, 1);
    const ag_nav_t *j = &AG.nav[AG.joueur];
    char b[64];
    if (j->detruit) snprintf(b, sizeof b, j->elimine ? "ÉLIMINÉ" : "DÉTRUIT");
    else if (AG.nn == 1) snprintf(b, sizeof b, "ARRIVÉE");
    else if (j->rang == 0) snprintf(b, sizeof b, "VICTOIRE");
    else snprintf(b, sizeof b, "ARRIVÉE · %de", j->rang + 1);
    ui_text(ui_fit(FONT_DISP, b, 400, 40, 22), 233, 96, UI_CENTER, ui_rgb(j->detruit ? MAGENTA : j->rang == 0 ? AMBRE : BLANC), 255, b);
    if (a <= 0) return;
    ui_rect(53, 116, 360, 54 + 26 * (float)AG.nn, ui_rgb(FOND), (int)(210 * a));
    for (int r = 0; r < AG.nn; r++) {
        int k = 0;
        while (k < AG.nn && AG.nav[k].rang != r) k++;
        if (k >= AG.nn) continue;
        const ag_nav_t *n = &AG.nav[k];
        const float y = 140 + 26 * (float)r;
        char t[24];
        if (n->detruit) snprintf(t, sizeof t, n->elimine ? "ÉLIMINÉ" : "DÉTRUIT"); else if (n->fini) temps_txt(t, sizeof t, n->fin); else snprintf(t, sizeof t, "EN COURSE");
        snprintf(b, sizeof b, "%d", r + 1);
        const uint32_t c = n->joueur ? AMBRE : BLANC;
        ui_rect(98, y - 16, 6, 18, ui_rgb(couleur_ecurie(n->ecurie, AGC_COQUE)), (int)(255 * a));
        ui_text(ui_font(FONT_DISP, 20), 74, y, UI_LEFT, ui_rgb(c), (int)(255 * a), b);
        ui_text(ui_font(FONT_DISP, 18), 112, y, UI_LEFT, ui_rgb(c), (int)(255 * a), AG.ec[n->ecurie].nom);
        if (AG.mode == AGM_CHAMPIONNAT && r < 6) { snprintf(b, sizeof b, "+%d", POINTS[r]); ui_text(ui_font(FONT_MONO, 14), 270, y, UI_RIGHT, ui_rgb(AMBRE), (int)(255 * a), b); }
        ui_text(ui_font(FONT_MONO, 16), 396, y, UI_RIGHT, ui_rgb(n->joueur ? AMBRE : CYAN), (int)(255 * a), t);
    }
    if (j->meilleur > 0) {
        char t[24];
        temps_txt(t, sizeof t, j->meilleur);
        const int rec = RECORD(AG.piste) <= 0 || j->meilleur <= RECORD(AG.piste);
        snprintf(b, sizeof b, "MEILLEUR TOUR %s%s", t, rec ? " · RECORD" : "");
        ui_text(ui_font(FONT_MONO, 14), 233, 152 + 26 * (float)AG.nn, UI_CENTER, ui_rgb(rec ? AMBRE : GRIS), (int)(255 * a), b);
    }
    if (AG.phase_t > 2.5f && fmodf(E.clock, 1.2f) < 0.8f) ui_text(ui_font(FONT_DISP, 20), 233, 440, UI_CENTER, ui_rgb(AMBRE), 255, "A : CONTINUER");
}
/* Médaille : l'icône du trophée de son métal (AGIM : trp0 bronze, trp1 argent, trp2 or), 2 unités par pixel ; sans elle, un disque de
   sa couleur ; aucune : un anneau */
static void medaille_dessin(float x, float y, float r, int m) {
    char nm[16];   /* (16 : format-truncation de l'ESP-IDF, qui compte %d à 11 caractères) */
    snprintf(nm, sizeof nm, "trp%d", m - 1);
    const ag_image_t *t = m > 0 ? image_jeu(nm) : NULL;
    if (t) { ui_sprite(t->px, t->w, t->pal, t->w, t->h, x - (float)t->w, y - (float)t->h, 2.0f, 255, 233, 233, 4000); return; }
    if (m > 0) { ui_circle(x, y, r, ui_rgb(MEDAILLE_COUL[m]), 255); ui_ring(x, y, r, 1.5f, ui_rgb(0x000000), 160); }
    else ui_ring(x, y, r, 1.5f, ui_rgb(GRIS), 160);
}
/* Cadenas : corps et anse */
static void cadenas(float x, float y) {
    ui_rect(x - 6, y - 5, 12, 10, ui_rgb(GRIS), 230);
    ui_ring(x, y - 6, 4.5f, 2, ui_rgb(GRIS), 230);
}
static void ecran_carriere(void) {
    char b[160];
    {   /* fond : l'image de la ligue choisie, ou du garage */
        const ag_image_t *f = image_jeu(AG.sel >= AG.nligues ? "garage" : IM_CLASSE[AG.ligues[AG.sel].classe]);
        if (f) { image_plein(f, 255); ui_voile_image(ui_rgb(0x000000), 70); }
    }
    snprintf(b, sizeof b, "%s · %u CR", AG.ec[AG.car.ecurie % AG.nec].nom, (unsigned)AG.car.credits);
    titre_ecran("CARRIÈRE", b);
    const int n = AG.nligues + 1;
    ui_rect(53, 104, 360, 34 * (float)n + 14, ui_rgb(FOND), 200);
    for (int k = 0; k < n; k++) {
        const float y = 132 + 34 * (float)k;
        if (k == AG.sel) { ui_rect(53, y - 24, 360, 32, ui_rgb(CYAN), 50); ui_line(53, y + 8, 413, y + 8, 2, ui_rgb(CYAN), 255); }
        if (k == AG.nligues) { ui_text(ui_font(FONT_DISP, 18), 233, y, UI_CENTER, ui_rgb(k == AG.sel ? BLANC : GRIS), 255, "GARAGE"); continue; }
        const ag_ligue_t *Lg = &AG.ligues[k];
        const int ouverte = k <= AG.car.ligue_max;
        ui_text(ui_fit(FONT_DISP, Lg->nom, 220, 18, 12), 70, y, UI_LEFT, ui_rgb(ouverte ? (k == AG.sel ? BLANC : BLANC) : GRIS), ouverte ? 255 : 170, Lg->nom);
        if (!ouverte) { cadenas(390, y - 6); continue; }
        int coupe = 0;
        for (int e = 0; e < Lg->nep; e++) if (Lg->ep[e].type == AGE_COUPE) coupe = AG.car.medaille[k][e];
        snprintf(b, sizeof b, "%d/%d", ligue_points(k), ligue_points_max(k));
        ui_text(ui_font(FONT_MONO, 14), 340, y, UI_RIGHT, ui_rgb(AMBRE), 255, b);
        medaille_dessin(390, y - 6, 9, coupe);
    }
    aide("A : OUVRIR · B : RETOUR");
}
static void ecran_calendrier(void) {
    const ag_ligue_t *Lg = &AG.ligues[AG.car_l];
    char b[160];
    snprintf(b, sizeof b, "CATÉGORIE %s · %d/%d POINTS", CLASSES[Lg->classe].nom, ligue_points(AG.car_l), ligue_points_max(AG.car_l));
    titre_ecran(Lg->nom, b);
    const float dy = Lg->nep > 7 ? 28 : 32;
    ui_rect(23, 100, 420, dy * (float)Lg->nep + 12, ui_rgb(FOND), 200);
    for (int e = 0; e < Lg->nep; e++) {
        const float y = 124 + dy * (float)e;
        const int ouverte = epreuve_ouverte(AG.car_l, e);
        if (e == AG.sel) { ui_rect(23, y - 20, 420, dy - 2, ui_rgb(CYAN), 50); ui_line(23, y + 6, 443, y + 6, 2, ui_rgb(CYAN), 255); }
        epreuve_nom(&Lg->ep[e], b, sizeof b, 0);
        if (Lg->ep[e].type == AGE_COUPE) snprintf(b, sizeof b, "COUPE DE LA LIGUE");
        ui_text(ui_fit(FONT_DISP, b, 340, 15, 10), 66, y, UI_LEFT, ui_rgb(ouverte ? (e == AG.sel ? BLANC : BLANC) : GRIS), ouverte ? 255 : 150, b);
        if (ouverte) medaille_dessin(44, y - 5, 8, AG.car.medaille[AG.car_l][e]); else cadenas(44, y - 5);
    }
    /* objectifs de l'épreuve choisie */
    const ag_epreuve_t *Ep = &Lg->ep[AG.sel];
    const float yo = 124 + dy * (float)Lg->nep + 8;
    char o[160];
    if (!epreuve_ouverte(AG.car_l, AG.sel)) {
        if (Ep->type == AGE_COUPE) snprintf(o, sizeof o, "IL FAUT %d POINTS DE TROPHÉES DANS LA LIGUE", Lg->seuil);
        else snprintf(o, sizeof o, "UN TROPHÉE SUR L'UNE DES DEUX ÉPREUVES D'AVANT");
    } else switch (Ep->type) {
    case AGE_CLM: {
        char t0[16], t1[16], t2[16];
        temps_txt(t0, sizeof t0, AG.t_ref * Ep->obj[0] / 100.0f); temps_txt(t1, sizeof t1, AG.t_ref * Ep->obj[1] / 100.0f); temps_txt(t2, sizeof t2, AG.t_ref * Ep->obj[2] / 100.0f);
        snprintf(o, sizeof o, "MEILLEUR TOUR · OR %s · ARGENT %s · BRONZE %s", t0, t1, t2);
        break;
    }
    case AGE_COMBAT: snprintf(o, sizeof o, "COUPS PORTÉS · OR %d · ARGENT %d · BRONZE %d", Ep->obj[0], Ep->obj[1], Ep->obj[2]); break;
    case AGE_ASCENSION: snprintf(o, sizeof o, "LA VITESSE MONTE À CHAQUE QUART DE TOUR · PALIERS : OR %d · ARGENT %d · BRONZE %d", Ep->obj[0], Ep->obj[1], Ep->obj[2]); break;
    case AGE_DUEL: snprintf(o, sizeof o, "BATS LE CHAMPION D'%s : SON ÉCURIE EST À TOI", Ep->rival < AG.nec ? AG.ec[Ep->rival].nom : "?"); break;
    case AGE_ELIM: snprintf(o, sizeof o, "À CHAQUE TOUR, LE DERNIER SORT · OR : LE DERNIER EN PISTE"); break;
    case AGE_COUPE: snprintf(o, sizeof o, "%d COURSES AUX POINTS · UN PODIUM OUVRE LA LIGUE SUIVANTE", coupe_courses(Ep)); break;
    default: snprintf(o, sizeof o, "OR : 1ER · ARGENT : 2E · BRONZE : 3E"); break;
    }
    ui_text(ui_fit(FONT_MONO, o, 420, 13, 9), 233, yo + 10, UI_CENTER, ui_rgb(AMBRE), 255, o);
    aide("A : COURIR · B : RETOUR");
}
static void ecran_garage(void) {
    char b[160];
    snprintf(b, sizeof b, "%u CRÉDITS", (unsigned)AG.car.credits);
    titre_ecran("GARAGE", b);
    const float y0 = 290;
    ui_rect(BX0, y0 - 6, BX1 - BX0, 150, ui_rgb(FOND), 200);
    ui_rect(BX0, y0 - 6, BX1 - BX0, 3, ui_rgb(couleur_ecurie(AG.ecurie, AGC_COQUE)), 255);
    for (int k = 0; k < 6; k++) {
        const float y = y0 + 16 + 22 * (float)k;
        if (k == AG.sel) ui_rect(60, y - 16, 346, 21, ui_rgb(CYAN), 50);
        const uint32_t c = k == AG.sel ? BLANC : GRIS;
        if (k == 0) { snprintf(b, sizeof b, "<  %s  >", AG.ec[AG.ecurie].nom); ui_text(ui_fit(FONT_DISP, b, 300, 16, 12), 233, y, UI_CENTER, ui_rgb(BLANC), 255, b); continue; }
        if (k == 5) { ui_text(ui_font(FONT_DISP, 16), 233, y, UI_CENTER, ui_rgb(c), 255, "RETOUR"); continue; }
        const int cran = AG.car.ameliore[k - 1];
        ui_text(ui_font(FONT_MONO, 14), 72, y, UI_LEFT, ui_rgb(c), 255, AMELIORE_NOM[k - 1]);
        for (int i = 0; i < 3; i++) ui_rect(220 + 22 * (float)i, y - 10, 16, 8, ui_rgb(i < cran ? CYAN : 0x24263a), 255);
        if (cran < 3) snprintf(b, sizeof b, "%d CR", AMELIORE_PRIX[cran]); else snprintf(b, sizeof b, "MAXI");
        ui_text(ui_font(FONT_MONO, 14), 396, y, UI_RIGHT, ui_rgb(cran < 3 && AG.car.credits >= (uint32_t)AMELIORE_PRIX[cran] ? AMBRE : GRIS), 255, b);
    }
    aide("A : ACHETER · GAUCHE, DROITE : ÉCURIE · B : RETOUR");
}
static void ecran_bilan(void) {
    const int m = AG.bilan.medaille;
    char b[160];
    {   /* fond : le podium pour une médaille, sinon l'image de la ligue */
        const ag_image_t *f = image_jeu(m ? "podium" : IM_CLASSE[AG.ligues[AG.car_l].classe]);
        if (f) { image_plein(f, 255); ui_voile_image(ui_rgb(0x000000), 60); }
    }
    titre_ecran(m ? "TROPHÉE" : "ÉPREUVE TERMINÉE", AG.ligues[AG.car_l].nom);
    const float a = clampf(AG.phase_t * 2, 0, 1);
    ui_rect(53, 98, 360, 330, ui_rgb(FOND), (int)(210 * a));
    {   /* le trophée qui tourne (16 vues, 10 par seconde), qui grandit en entrant */
        char nm[32];
        snprintf(nm, sizeof nm, "tro%d_%02d", m - 1, (int)(E.clock * 10) % 16);
        const ag_image_t *t = m > 0 ? image_jeu(nm) : NULL;
        if (t) { const float s = 2.0f * (0.5f + 0.5f * a); ui_sprite(t->px, t->w, t->pal, t->w, t->h, 233 - (float)t->w * s / 2, 198 - (float)t->h * s / 2, s, 255, 233, 233, 4000); }
        else medaille_dessin(233, 198, 34 * (0.6f + 0.4f * a), m);
    }
    ui_text(ui_fit(FONT_DISP, MEDAILLE_NOM[m], 300, 26, 16), 233, 318, UI_CENTER, ui_rgb(m ? MEDAILLE_COUL[m] : GRIS), 255, MEDAILLE_NOM[m]);
    float y = 344;
    if (AG.bilan.credits > 0) { snprintf(b, sizeof b, "+%d CRÉDITS%s", AG.bilan.credits, m <= AG.bilan.ancienne ? " (DÉJÀ GAGNÉ)" : ""); ui_text(ui_fit(FONT_MONO, b, 330, 16, 11), 233, y, UI_CENTER, ui_rgb(AMBRE), 255, b); y += 20; }
    if (AG.bilan.ecurie) { snprintf(b, sizeof b, "ÉCURIE %s : ELLE EST À TOI", AG.ec[AG.bilan.ecurie - 1].nom); ui_text(ui_fit(FONT_MONO, b, 330, 16, 11), 233, y, UI_CENTER, ui_rgb(CYAN), 255, b); y += 20; }
    if (AG.bilan.promu) {
        snprintf(b, sizeof b, "%s OUVERTE · CATÉGORIE %s", AG.ligues[AG.car_l + 1].nom, CLASSES[AG.ligues[AG.car_l + 1].classe].nom);
        ui_text(ui_fit(FONT_MONO, b, 330, 16, 11), 233, y, UI_CENTER, ui_rgb(CYAN), 255, b); y += 20;
    }
    if (AG.bilan.fin) { ui_text(ui_fit(FONT_MONO, "CHAMPION DU SYSTÈME SOLAIRE", 330, 16, 11), 233, y, UI_CENTER, ui_rgb(MAGENTA), 255, "CHAMPION DU SYSTÈME SOLAIRE"); y += 20; }
    if (AG.bilan.ouvertes > 0) { snprintf(b, sizeof b, "%d ÉPREUVE%s DE PLUS", AG.bilan.ouvertes, AG.bilan.ouvertes > 1 ? "S" : ""); ui_text(ui_fit(FONT_MONO, b, 330, 16, 11), 233, y, UI_CENTER, ui_rgb(CYAN), 255, b); }
    if (AG.phase_t > 1 && fmodf(E.clock, 1.2f) < 0.8f) ui_text(ui_font(FONT_DISP, 20), 233, 440, UI_CENTER, ui_rgb(AMBRE), 255, "A : CONTINUER");
}
static void ecran_classement(void) {
    char b[48];
    const int cm = champ_manche(), ct = champ_total();
    snprintf(b, sizeof b, "COURSE %d/%d", cm + 1, ct);
    titre_ecran(cm + 1 >= ct ? "CLASSEMENT FINAL" : AG.ev_on ? "COUPE" : "CHAMPIONNAT", b);
    /* les écuries engagées dans le championnat : celles de la dernière course (toujours les mêmes : AG_ADV_MAX adversaires au plus) */
    int ord[AG_NAV_MAX], ne = 0;
    for (int k = 0; k < AG.nn && ne < AG_NAV_MAX; k++) ord[ne++] = AG.nav[k].ecurie;
    for (int i = 1; i < ne; i++) for (int j = i; j > 0 && AG.points[ord[j]] > AG.points[ord[j - 1]]; j--) { const int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
    ui_rect(73, 110, 320, 30 * (float)ne + 20, ui_rgb(FOND), 210);
    for (int r = 0; r < ne; r++) {
        const int e = ord[r];
        const float y = 138 + 30 * (float)r;
        const uint32_t c = e == AG.ecurie ? AMBRE : BLANC;
        embleme(e, 104, y - 7, 22);
        snprintf(b, sizeof b, "%d. %s", r + 1, AG.ec[e].nom);
        ui_text(ui_font(FONT_DISP, 18), 124, y, UI_LEFT, ui_rgb(c), 255, b);
        snprintf(b, sizeof b, "%d PTS", AG.points[e]);
        ui_text(ui_font(FONT_MONO, 16), 380, y, UI_RIGHT, ui_rgb(c), 255, b);
    }
    if (AG.phase_t > 1 && fmodf(E.clock, 1.2f) < 0.8f) ui_text(ui_font(FONT_DISP, 20), 233, 440, UI_CENTER, ui_rgb(AMBRE), 255, cm + 1 >= ct ? "A : TERMINER" : "A : COURSE SUIVANTE");
}
static void ecran_pause(void) {
    ui_voile_image(ui_rgb(0x000000), 140);
    ui_text(ui_font(FONT_DISP, 36), 233, 150, UI_CENTER, ui_rgb(BLANC), 255, "PAUSE");
    static const char *const L[4] = { "REPRENDRE", "RECOMMENCER", "RÉGLAGES", "QUITTER" };
    liste(L, 4, AG.pause_sel, 210, 40);
}
void ag_hud(void) {
    switch (AG.phase) {
    case AGP_TITRE: ecran_titre(); return;
    case AGP_MENU: ecran_menu(); return;
    case AGP_ECURIE: ecran_ecurie(); return;
    case AGP_PISTE: ecran_piste(); return;
    case AGP_REGLAGES: ecran_reglages(); return;
    case AGP_DEPART: ecran_depart(); break;
    case AGP_COURSE: ecran_course(); break;
    case AGP_ARRIVEE: ecran_resultats(); break;
    case AGP_CLASSEMENT: ecran_classement(); break;
    case AGP_CARRIERE: ecran_carriere(); return;
    case AGP_CALENDRIER: ecran_calendrier(); return;
    case AGP_GARAGE: ecran_garage(); return;
    case AGP_BILAN: ecran_bilan(); return;
    }
    if (AG.pause) ecran_pause();
}
