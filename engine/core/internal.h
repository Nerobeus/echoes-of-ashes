/* État partagé du cœur (chargement, jeu, rendu) */
#ifndef ENG_INTERNAL_H
#define ENG_INTERNAL_H

#include <math.h>
#include "eng.h"
#include "pak.h"

#define PI_F 3.14159265f

/* Boucles internes du rastériseur en IRAM sur l'ESP32 (jamais d'attente du cache de la flash). noinline : une fonction
   fondue dans son appelant (en flash) perdait sa section, et le rastériseur tournait depuis la flash */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define ENG_HOT IRAM_ATTR __attribute__((noinline))
/* Tableaux froids (intérieurs, menus, sac) en PSRAM : la SRAM interne reste aux tampons rapides du rendu (eng_alloc) */
#define ENG_FROID EXT_RAM_BSS_ATTR
#else
#define ENG_HOT
#define ENG_FROID
#endif

/* Chunk de CS × CS cases décompressé dans une place de la fenêtre (coordonnées monde, comme l'ancien paquet entier) */
#define CS 16
#if ENG_DYN
#define CWIN 9                   /* plein écran (téléphone) : distance de vue doublée, donc plus de chunks autour de Sera */
#else
#define CWIN 5                   /* fenêtre de CWIN × CWIN chunks autour de Sera */
#endif
#define CV (CS + 1)              /* sommets par côté */
#define TILE_LOD 0xC000          /* tuile ≥ TILE_LOD : texture réduite d'un bloc, (zone × 64 + bloc) dans w->lodbuf */
#define LOD_BUFS 10              /* zones de textures réduites : les 3 × 3 chunks autour de Sera, plus une de marge */
#define LOD_PENDING 0xFFFE       /* bloc simplifiable dont la texture réduite n'est pas encore fabriquée */
typedef struct {
    int cx, cz;                  /* chunk chargé dans cette place, -1 si aucun */
    float hv[CV * CV], lv[CV * CV];          /* hauteur et lumière aux sommets */
    float road_h[CS * CS];                   /* hauteur de chaussée (NaN sans route) */
    float water_h[CS * CS];                  /* surface de l'eau : lac, mer, ou eau voisine d'une berge (-1e9 sans eau) */
    uint16_t tile[CS * CS], lod[(CS / 2) * (CS / 2)];
    uint8_t flags[CS * CS], water[CS * CS], hidden[CS * CS];
    uint16_t road_first[CS * CS], plant_first[CS * CS];
    uint8_t road_count[CS * CS], plant_cnt[CS * CS];
    int slot;                    /* numéro de la place dans la fenêtre */
    int lodbuf;                  /* zone de textures réduites (w->lodbuf) tenue par ce chunk, -1 si aucune */
    /* polygones (routes case par case, garde-corps, blocs du décor, bâtiments), dans la réserve mem */
    uint8_t *mem; uint32_t mlen;   /* sa place dans le bloc de la fenêtre (fen_mem), mlen octets ; NULL : aucune */
    const uint8_t *poly_base; const uint32_t *poly_off; uint32_t npoly;
    uint32_t rail_first, rail_count, box_first, box_count;
    const pak_building_t *bld; uint32_t nbld;
    const float *solids; uint32_t nsolid;    /* x0, z0, x1, z1 */
    const pak_plant_t *plants; uint32_t nplant;
} chunk_t;

#define ZOOM_MAX 5.0f             /* zoom le plus fort (caméra 5 fois plus près qu'à l'origine) */
#define MAX_EAUX 64               /* sortes d'eau (EAUC) : 0 les rivières, puis une par terrain d'eau */

/* Planche propre (SPRS) : 8 directions × frames images de fw × fh, carré du portrait, taille à l'écran */
#define MAX_SHEETS 1024          /* entrées de SPRS (prim_t.sheet : index + 1 sur deux octets ; PERS et TENU : SPIX) ; tableau alloué au chargement */
/* Animations d'un personnage (planches de même cellule) ; anim[a] : index de la planche de l'animation a (-1 : absente) */
enum { ANIM_WALK, ANIM_IDLE, ANIM_RUN, ANIM_ATTACK, ANIM_HURT, ANIM_KO, ANIM_CAST, ANIM_DEFEND, ANIM_VICTORY, ANIM_DANSE, ANIM_DEPART, ANIM_N };
/* foot : vide sous chaque direction (pixels sous la rangée la plus basse avec un pixel opaque, toutes images) */
/* pv : ancrage par direction (PIVO, demi-pixels de cellule) si has_pv : le point posé sur la position au sol */
/* tissu : masque du tissu d'une tenue (TISS), 1 bit par pixel de la planche (NULL : aucun) : seuls ces pixels prennent
   les couleurs d'une variante de la tenue (TENV) */
/* ok : planche utilisable ; data : ses pixels en mémoire (NULL : lue à la demande sur la carte SD, à foff dans le
   fichier, par direction : sl[d] place du cache, -1 si absente ; footok : bits des directions dont foot est calculé) */
typedef struct { const uint8_t *data, *tissu; int ok, fw, fh, frames, px, py, psize; float taille; int16_t anim[ANIM_N]; uint8_t foot[8]; int16_t pv[8][2]; int has_pv;
                 uint32_t foff; int16_t sl[8]; uint8_t footok; } sheet_t;
/* Tenue (TENU) : planche de rechange du joueur ou d'un membre de l'équipe (porteur : TENUE_JOUEUR, sinon index PERS),
   mise d'office (TENUE_*), étanche (on nage dans les eaux noires), drapeau du scénario (TENUE_DRAPEAU ; 65535 aucun),
   nom (objets de type tenue, instruction « tenue ») */
#define MAX_TENUES 192
#define MAX_TENV 118             /* variantes de couleur des tenues (TENV) et des personnages (PTIV) : tables après celles des personnages */
#define MAX_PVAR 1536            /* palettes des personnages (PVAR) : variantes de couleur tirées par exemplaire, après les tables TENV */
#define TENUE_JOUEUR 255
/* TENUE_SAGE : planche de remplacement d'un personnage 18+ (édition unique, tant que le 18+ est verrouillé) : jamais
   portée, pas dans w.tenues (perso_t.sage) */
enum { TENUE_PORTEE, TENUE_NAGE, TENUE_COURSE, TENUE_DRAPEAU, TENUE_SAGE };
/* vremap : variante de couleur (TENV) : table de w.remap appliquée aux pixels du tissu de la planche (0 : aucune) ;
   adulte : tenue 18+ (édition unique : ni portée ni montrée tant que c'est verrouillé, tenue_vue) */
typedef struct { int owner, sheet, ctx, etanche, flag, vremap, adulte; char nom[20]; } tenue_t;
/* Jobs et équipement (JOBS, js/equipement.js de l'Atelier) : types d'équipement (emplacement EQ_ARME, EQ_ARMURE,
   EQ_ACCESS), jobs (modificateurs de PV, PM, ATQ en %, VIT en points ; affinité par type : 0 ne peut pas le porter,
   1 à 4 C, B, A, S), bonus des objets d'équipement (cat : type, 255 : pas un équipement ; img : image k de OBJG, 0 aucune) */
#define MAX_EQTYPES 32
#define MAX_JOBS 16
#define EQ_AUCUN 255
enum { EQ_ARME, EQ_ARMURE, EQ_ACCESS };
/* Type d'équipement : emplacement (EQ_*), effet en combat d'une arme (FX_*) et sa couleur RGB */
typedef struct { int slot, effet; uint32_t couleur; char nom[16]; } eqtype_t;
/* sorts : bit k, sort k du catalogue (JOBS version 2) ; mag : MAG % du job, MAG d'un objet */
typedef struct { char nom[16]; int pv, pm, atq, vit, mag; uint8_t aff[MAX_EQTYPES]; uint64_t sorts; } job_t;
typedef struct { uint8_t cat, img; int8_t vit; int16_t atq, def, pv, pm, mag; uint64_t sorts; } eqitem_t;   /* sorts donnés à qui le porte */
/* Sort (JOBS version 2) : nom, effet (FX_SORT), coût en PM, puissance (× MAG), soin (0 attaque, 1 un allié, 2 l'équipe) */
#define MAX_SORTS 64
typedef struct { char nom[16]; int fx, pm, soin; float mult; } sort_t;
/* Personnage de la bibliothèque (PERS) : planche (-1 : aucune), drapeaux, répliques (nlines chaînes à la suite) */
#define MAX_PERSOS 255          /* personnages de la bibliothèque (PERS) ; un octet les désigne (pnj, trafic ; 255 : aucun, ou le joueur) */
#define PERS_FLANE 1             /* flâne autour de sa place */
#define PERS_CONV 2              /* toutes les répliques dans l'ordre (sinon une au hasard) */
#define PERS_REMAP 4             /* table de couleurs propre (variante de couleur) */
#define PERS_VEHICULE 8          /* véhicule : on y monte (type dans les bits 4 à 6 des drapeaux) */
#define PERS_TRAFIC 128          /* véhicule du trafic (apparaît au hasard sur les routes) */
#define PERS_ADULTE 64           /* hors véhicule : planche de base 18+ (édition unique : sa planche de remplacement, ou masqué, tant que le 18+ est verrouillé) */
/* Hors véhicule, bits 4 et 5 des drapeaux : genre du nom tiré par exemplaire (NOMT ; 0 : le nom du modèle, 1 féminin,
   2 masculin, 3 neutre) */
#define PERS_GENRE(f) (((f) & PERS_VEHICULE) ? 0 : ((f) >> 4) & 3)
/* Véhicule du trafic : d'une case de route (ci, cj) à la suivante (ni, nj), avancement k (0..1), position, cap */
#define TRAFFIC_MAX 8
/* Véhicule du trafic : sur un axe de route (lane : axe road, segment seg parcouru dans le sens sgn, k de 0 à 1), sinon de
   case de rue en case de rue (ci, cj vers ni, nj) ; stuck : temps bloqué ; v : cases/s */
typedef struct { int on, perso, ci, cj, ni, nj, walking, lane, road, seg, sgn; float k, x, z, dir, anim, stuck, v; uint8_t frame, var, urgence;
                 uint8_t poursuite, mode, wl; int16_t wi, wj, wq, ws; int8_t wg; float wk, wx, wz, demi; } traffic_t;   /* urgence : roule vite (secours, pompiers, police parfois) */
/* poursuite : voiture de police qui poursuit Sera (police.c ; game.c, pour_*) ; mode : POUR_* ; ralliement du réseau
   (POUR_RALLIE) : point (wx, wz), rue (wl = 1 : case wi, wj) ou axe (wl = 0 : axe wq, segment ws, abscisse wk, sens wg) ;
   demi : temps avant le prochain demi-tour permis */
typedef struct { const uint8_t *p; int n; float hw; } route_t;   /* axe de route : n points (x, z) f32 en p, demi-largeur hw */
/* Lieu du menu GUIDAGE : point visé, rayon d'arrivée, sorte (GUIDE_VILLE, GUIDE_BAT), nom (d'un bâtiment : celui de son type) */
#define MAX_GUIDE 1024
#define OUV_CARTES 24            /* cartons de l'introduction (OUVR) */
#define OUV_HEROINES 15          /* héroïnes au choix, en plus de Sera */
#define CUT_PERS 320             /* jeux de cut-ins (CUTI) : par personnage, tenue (et variante de couleur), sorte et édition ; 146 sans les variantes le 30/10/2026 */
#define SCENES_MAX 128           /* illustrations des scènes du scénario (SCNI, instruction « image ») */
#define VOLANT_DEFAUT 2          /* SYSTÈME › DIRECTION au départ : MOYENNE (COURSE_VOLANT de course.c) */
#define PARTIES 3                /* parties sauvegardées : deux emplacements alternés chacune (partie × 2 + séquence & 1) */
#define GUIDE_VILLE 0
#define GUIDE_BAT 1
#define GUIDE_LIEU 2             /* circuit, astroport, puits (ceux du téléport) */
/* groupe : 0 villes, 1 lieux, 2 + rang de la famille du catalogue (FAM_GUIDE de hud.c) */
typedef struct { float x, z, r; int kind, groupe; char nom[24]; } guide_t;
/* Intérieurs (interieur.c) : bâtiment qu'on visite (PORT : accès, entrée de INTR ; force : serrure déjà forcée), porte
   (point devant elle dans la rue, normale vers la rue, index dans le plan : 0 la principale) */
enum { ACC_LIBRE, ACC_HORAIRES, ACC_FERME, ACC_SCELLE };
enum { AUT_AUCUNE, AUT_ORDRE, AUT_CLAN };
typedef struct { float cx, cz; uint8_t mode, serrure, niveau, alarme, autorite, force, comm, eclaire, noir, carte; uint16_t intr; char nom[24]; } int_bat_t;   /* comm : commerce (COMM) + 1, 0 : aucun ; nom : celui du type ; eclaire : intérieur allumé même fermé ; noir : toujours éteint (cinéma) */
/* Commerce d'un intérieur (COMM) : métiers qui le tiennent (bit k : METIERS[k]), prix des soins et d'un verre (0 :
   aucun), types d'objets rachetés (bit k), objets vendus (rangs dans ITEMS) */
#define COMM_MAX 32
#define COMM_VEND 48
typedef struct { uint16_t qui, soins, verre; uint8_t rachete, n; uint8_t vend[COMM_VEND]; } comm_t;
/* Répliques génériques (REPL) : une liste par métier (METIERS), puis ville, village, campement */
#define REPL_MAX 16
#define REPL_VILLE 11
typedef struct { float x, z, nx, nz, w; uint16_t bat; uint8_t di; } int_porte_t;   /* w : largeur de l'ouverture */
#define TALK_TRAF(k) (-100 - (k))   /* interlocuteur : véhicule k du trafic (à voler) */
#define STOLEN_MAX 8                /* places d'exemplaires pour les véhicules volés et leurs conducteurs éjectés */
#define INT_PNJ_MAX 8               /* places d'exemplaires pour les personnages d'un intérieur (pendant la visite) */
/* Ombre d'un personnage : décalage (% de la cellule ; dy vers le haut), largeur et hauteur (% de l'ombre d'origine) */
typedef struct { int8_t dx, dy; uint8_t l, h, off; } ombre_t;
#define OMBRE_JOUEUR MAX_PERSOS
enum { VEH_VOITURE = 1, VEH_CAMION, VEH_MECH, VEH_BATEAU, VEH_VAISSEAU };
#define VEH_TYPE(flags) (((flags) >> 4) & 7)
#define SHIP_ALT 2.2f            /* altitude du vaisseau (unités du monde, avant l'échelle ech) */
/* Vol permanent (carte du système solaire, CART) : vitesse et accélération du vaisseau × VOL_VITESSE (orbites resserrées :
   environ 2 minutes de la Terre à Mars à bord de la navette) ; il n'entre pas à moins de VOL_MARGE de la sphère d'un astre */
#define VOL_VITESSE 0.45f
#define VOL_MARGE 0.4f
#define VOL_BRUME 0.7f           /* brouillard 1 / VOL_BRUME fois plus profond (les astres se voient venir) */
#define VOL_PITCH 0.32f          /* pente de la caméra en vol permanent (on regarde devant soi) */
/* remap : index dans world_t.remap (6 + i pour une variante, sinon 0 : identité) ; taille à l'écran */
/* cond, neg : présence (PVIS) : là seulement si le drapeau cond du scénario est levé (baissé si neg) ; 65535 : toujours */
/* vremap : variante de couleur sur le tissu de sa planche (PTIV : danseuse d'une tenue de scène recolorée ; 0 : aucune) */
/* var0, nvar : palettes (PVAR) : tables E.w.remap[var0 .. var0 + nvar - 1], variantes 1 à nvar de ses exemplaires */
/* Édition unique (edition_appliquer) : adulte (PERS_ADULTE), sage : planche de remplacement (TENUE_SAGE, -1 : aucune) et
   ses couleurs (PSAG, 0 : celles de la planche), masque : 18+ verrouillé sans planche de remplacement (perso_present) ;
   base : planche et couleurs de la planche de base, gardées le temps du verrou */
typedef struct { int sheet, flags, nlines, remap, cond, neg, vremap, var0, nvar; float taille; const char *nom, *lines;
                 uint8_t adulte, masque; int16_t sage, sage_remap; struct { int16_t ok, sheet, remap, vremap, var0, nvar; } base; } perso_t;
/* Exemplaire (PNJS) : place hx, hz, position, cible de flânerie tx, tz, attente avant de repartir ; depart : starter des
   circuits (son animation de départ pendant le compte à rebours et l'arrivée d'une course) */
#define MAX_NPCS 2048
#define DEPART_IPS 10            /* images par seconde de l'animation de départ du starter */
#define NPC_DRAW 14              /* exemplaires dessinés au plus (les plus proches) */
/* graine : celle du nom tiré (rang dans PNJS ; intérieur : 2048 + (bâtiment visité × 4 + étage) × 8 + place) */
/* Patrouilles : meneur (il se promène toujours) ; chef : indice + 1 dans E.npcs du meneur d'un suiveur (0 : aucun), rang :
   sa place dans la file */
typedef struct { float x, z, hx, hz, dir, anim, tx, tz, wait; uint8_t perso, frame, walking, depart; uint16_t graine; uint8_t var, meneur, rang; uint16_t chef; uint8_t fuit, abri, vole; } npc_t;   /* vole : véhicule volé, signalé tant que Sera le conduit (niveau de recherche qu'il donne : police.c) */   /* fuit : conducteur éjecté qui s'enfuit vers (tx, tz) */   /* var : palette (0 : ses couleurs, k : PVAR k) */   /* abri : passant à l'abri de la pluie de cendre (abri_maj de game.c), ni dessiné ni rencontré */

/* Ennemi de la bibliothèque (ENNE) : personnage (index dans PERS), niveau, poids du tirage, statistiques, terrains où il
   apparaît (bit k : terrain k de ZONE) */
#define MAX_ENEMIES 32
/* Événement du scénario (histoire.scn de la carte SD, sinon section SCEN ; script.c) : déclencheur (0 lancement, 1 zone,
   2 parler, 3 épave), personnage (index PERS, 254 le marchand), drapeaux (1 une seule fois, 2 condition inversée),
   condition (drapeau, 65535 aucune), zone, bytecode (décalage et longueur : lu à la demande) */
#define SC_MAX_EVENTS 1024
#define SC_MAX_FLAGS 1024
#define SC_MAX_QUESTS 256
/* quest, step : l'événement ne part qu'à cette étape de cette quête (65535 : hors quête) */
typedef struct { int trigger, perso, flags, cond, quest, step, carte; float x, z, r; uint32_t code_off; int len; uint16_t sujet; } sc_event_t;   /* sujet : intitulé du choix des sujets (chaîne, 65535 aucune) */
/* Cible de chasse (CIBL, js/scenario.js) : l'ennemi d'un événement de zone, au centre de la zone, dessiné tant que sa quête
   est à l'étape de l'événement (render.c : ciblesVues de l'émulateur) */
typedef struct { uint8_t perso, carte, step, vaincue; uint16_t quest; float x, z; } cible_t;   /* vaincue : son combat gagné, plus dessinée */
#define CIBLE_MAX 64
#define PAL_VUE 12.0f        /* combat du scénario, acteur : palette de l'exemplaire posé du même personnage à moins de tant de cases */
#define CIBLE_DOUBLE 6.0f    /* pas dessinée si un exemplaire posé de son personnage est à moins de tant de cases */
#define CIBLE_DRAW 4         /* les plus proches seulement */
#define CIBLE_IMAGE 0.22f    /* s par image de l'animation d'attente */   /* carte : celle d'une zone */
/* Quête : nom (gardé en mémoire), type (0 principale, 1 secondaire), étapes (8 octets chacune, lues à la demande) ;
   c_st : étape dont le niveau et la cible sont gardés dans c_niv, c_x, c_z (-1 : aucune) */
typedef struct { char nom[40]; int type, nsteps; uint32_t steps_off; int c_st, c_niv, c_carte; uint16_t c_x, c_z; } sc_quest_t;
/* coeur : vaincu, il laisse un CŒUR DE MACHINE dans le sac (drapeau 1 de ENNE) */
/* sorts : ses sorts d'attaque (bit k : sort k du catalogue, section ENSO), magie : part de ses tours où il en lance (%) ;
   src : son rang dans ENNE */
typedef struct { int perso, lvl, poids, hp, atk, spd, coeur, magie, src; uint32_t zones; uint64_t sorts; } enemy_t;
typedef struct { float x, z, r; uint16_t piste; } mus_zone_t;   /* zone de musique : centre, rayon (cases), piste */
typedef struct { int perso; uint16_t piste; } mus_enn_t;        /* piste de combat d'un ennemi (les boss) */
#define REMAP_WHITE (6 + MAX_PERSOS)    /* table de couleurs blanche : ennemi touché (flash) */
#define REMAP_TENV (7 + MAX_PERSOS)     /* tables des variantes de tenue (TENV) : REMAP_TENV + k */
#define REMAP_PVAR (REMAP_TENV + MAX_TENV)   /* palettes des personnages (PVAR) : REMAP_PVAR + k */

/* Carte (CART) : nom, côté, ciel (0 celui du monde, 1 souterrain, 2 espace, 3 autre planète), niveau de l'eau, brouillard, départ
   (arrivée par défaut), arène des combats, données dans CRTD (index des chunks, zone des rencontres, carte du menu) ; vol :
   vol permanent (système solaire : Sera toujours à bord du vaisseau de l'équipe, à l'altitude vol_alt, cases) ; visite :
   drapeau du scénario + 1 levé à l'arrivée (0 : aucun) */
#define MAX_CARTES 16
typedef struct { char nom[24]; int n, ciel, vol; float wl, vol_alt; uint16_t visite; uint8_t fog[3]; float sx, sz, ax, az; uint32_t chnk_off, chnk_len, zone_off, zone_len, map_off, map_len; const uint8_t *cmem; } carte_t;   /* cmem : tailles de ses chunks (CMEM) */
/* Astre (ASTR) de la carte carte (système solaire) : centre, rayon de sa sphère (0 : repère seul), point d'orbite (arrivée
   de son monde, saut), carte où l'on se pose (vers, -1 : aucune), drapeau de visite (65535 : toujours ; le saut n'y mène
   qu'une fois levé) */
typedef struct { char nom[24]; int carte, vers; uint16_t visite; float x, z, r, ax, az; } astre_t;
/* Passage (PASS) : zone (x, z, r) de la carte carte vers la carte vers, arrivée (ax, az), sorte (0 passage, 1 puits,
   2 égout, 3 ascenseur, 4 faille, 5 astroport), condition (drapeau du scénario, 65535 aucune) */
typedef struct { int carte, vers, sorte, cond; float x, z, r, ax, az; } passage_t;

/* Monde (paquet) */
typedef struct {
    int n;                       /* côté de la grille */
    float wl;                    /* niveau de l'eau */
    uint32_t ground_off, g0, decor_slots;
    const uint8_t *tex; uint32_t tex_len;
    uint32_t big_off;            /* textures 64 × 64 des grands décors (tuiles ≥ TILE_BIG) */
    const uint8_t *deco; uint32_t deco_n;   /* grands décors : entrées de 12 octets de la table DECO (pak.h) */
    int tex_copied;              /* textures copiées en mémoire rapide (sinon lues dans le paquet) */
    /* Lecture à la demande (paquet léger de la flash, monde.pak sur la carte SD : sections SPRX, TEXS ; pak.h) : textures
       du sol (gt_n, à gt_off dans le fichier) dans un cache de gt_nsl places de 1 Ko ; planches par direction dans des
       places de taille ST_CLASSES ; sframe : image en préparation (une place servie à l'image f ou f - 1 reste) */
    int stream;
    uint32_t gt_off; int gt_n, gt_nsl; int16_t *gt_slot; int32_t *gt_who; uint32_t *gt_used; uint8_t *gt_mem;
    uint8_t *st_mem; int32_t *st_who; uint32_t *st_used; int st_nsl;   /* st_who : planche × 8 + direction, -1 libre */
    uint32_t sframe; int s_tex_budget, s_spr_budget, s_sync;
    /* Cartes (CART ; la 0 est celle de HEAD, CHNK, ZONE, MAP, gardée dans c0) : carte en cours, son ciel ; c_buf : index
       des chunks, zone et carte du menu de la carte en cours (k ≥ 1, lus dans CRTD) ; passages (PASS) */
    carte_t cartes[MAX_CARTES]; int ncartes, carte, ciel;
    struct { const uint8_t *dir, *zone, *map, *cmem; uint32_t cdat_off; int nc, zone_n, map_n; } c0;
    uint8_t *c_buf; uint32_t c_cap;
    passage_t *pass; int npass;
    astre_t *astres; int nastres;   /* astres du système solaire (ASTR) */
    float start_x, start_z, npc_x, npc_z;   /* npc : place donnée au marchand par l'atelier (Odric est un personnage) */
    float arena_x, arena_z, epos_x, epos_z, wreck_x, wreck_z;   /* combat : équipe, ennemi ; épave (Sentinelle) */
    uint8_t fog_rgb[3];
    float sun[3];
    float ech;                   /* échelle du monde par rapport aux personnages (1,3 : cases 30 % plus grandes) */
    /* palette : couleurs modifiables (cycle de l'eau) et drapeau pleine lumière */
    uint8_t pal[256][3], full[256], water[8];
    int pal_count;
    struct { int corps; float prof; } eaux[MAX_EAUX]; int neaux;   /* sortes d'eau (EAUC) : couleur du corps, profondeur opaque */
    /* sprites */
    int fw, fh, sw;
    const uint8_t *sheet;
    const uint8_t *remap[7 + MAX_PERSOS + MAX_TENV + MAX_PVAR];   /* Sera, marchand, Varn, Bors, Lyse, KO, une par personnage (PERS), blanche, variantes de tenue (TENV) */
    /* Planches propres (SPRS), dont celle du joueur (psheet, -1 : planche commune), et personnages (PERS) */
    sheet_t *sheets; int nsheets, psheet;
    tenue_t tenues[MAX_TENUES]; int ntenues;
    eqtype_t eqtypes[MAX_EQTYPES]; int neqtypes;
    job_t jobs[MAX_JOBS]; int njobs;
    sort_t sorts[MAX_SORTS]; int nsorts;   /* catalogue des sorts (JOBS version 2) */
    eqitem_t *eqit;                              /* MAX_ITEMS entrées (NULL sans JOBS) */
    uint8_t job0[4], eq0[4][3]; uint16_t jobs0[4];   /* au départ : job, objets portés, jobs connus (bits) */
    perso_t persos[MAX_PERSOS]; int npersos;
    cible_t cibles[CIBLE_MAX]; int ncibles;   /* cibles de chasse (CIBL) */
    const uint8_t *lum; int nlum;             /* lueur des feux (LUMI) : nlum entrées de 12 octets, toutes cartes (render.c : lum_band) */
    const route_t *routes; int nroutes;   /* axes des routes (ROUT) */
    const guide_t *guides; int nguides;   /* lieux du menu GUIDAGE (GUID) */
    /* Ouverture (OUVR) : écran titre (titre, sous-titre, musique, lieu du fond), cartons de l'introduction (lieu x, z en
       cases, x < 0 : noir ; heure, < 0 : celle du départ ; texte), question et héroïnes au choix (index PERS), après Sera.
       Chaînes dans le paquet ; sans OUVR, titre d'office, ni cartons ni choix. */
    const char *ouv_titre, *ouv_sous, *ouv_musique, *ouv_question;
    float ouv_fond[3];
    int ouv_ncartes; struct { float x, z, heure; const char *texte; } ouv_cartes[OUV_CARTES];
    int ouv_nher; int ouv_her[OUV_HEROINES];
    uint32_t mip0, nmips; const uint8_t *mips;   /* textures réduites des bâtiments (MIPS) : tuiles des niveaux 1 et 2 de mip0 + k */
    int_bat_t *bats; int nbats;           /* bâtiments qu'on visite et leurs portes (PORT) */
    comm_t comms[COMM_MAX]; int ncomms;   /* commerces des intérieurs (COMM) */
    const uint8_t *nomt; uint32_t nomt_sz; int nnom[4];   /* noms des PNJ (NOMT) : décalages u16 puis chaînes ; prénoms f, m, neutres, noms de famille */
    const uint8_t *repl[REPL_MAX]; uint16_t nrepl[REPL_MAX]; int nrepls;   /* répliques génériques (REPL) : début de chaque liste (longueur u8, texte), nombre */
    int_porte_t *portes; int nportes;
    /* Intérieurs (INTR ou interieurs.bin) : répertoire (décalage, tailles compressée et brute), tampons du chargement :
       pris dans le bloc de la fenêtre de chunks le temps d'une visite (int_fenetre ; intr_need octets) */
    int intr_n; uint8_t *intr_dir, *intr_raw, *intr_comp, *intr_mem; uint32_t intr_cap, intr_raw_max, intr_comp_max, intr_need;
    float veh_v[MAX_PERSOS], veh_a[MAX_PERSOS];
    float veh_grip[MAX_PERSOS];                   /* adhérence en course (VEH2) : part de celle de la piste, 1 normale */
    uint8_t veh_course[MAX_PERSOS];               /* utilisable en course (VEH2 ; sans elle, tous) */
    uint8_t veh_service[MAX_PERSOS];              /* service (VEH2) : 0 civil, 1 police, 2 secours, 3 pompiers, 4 armée, 5 voirie, 6 transport */
    int8_t veh_roule[MAX_PERSOS];                 /* véhicule : 1 roule, 0 plane, -1 selon son type (ROUL) */
    uint8_t son_moteur[MAX_PERSOS];               /* son du moteur (SONM, SON_*), 255 : celui de son type */
    float veh_alt[MAX_PERSOS];                    /* vaisseau : altitude maxi en cases (VALT), 0 : 4 cases */
    ombre_t ombre[MAX_PERSOS + 1];                /* ombre de chaque personnage (OMBR), puis du joueur (OMBRE_JOUEUR) */   /* véhicules : vitesse maxi (cases/s), accélération (cases/s²) ; 0 : type */
    float zoom, zoom_min;        /* zoom de départ de la caméra, plus petit zoom permis (le plus grand : ZOOM_MAX) */
    int traf_max;                /* véhicules du trafic autour de Sera (TRAF) */
    float sprite_scale;          /* échelle globale des sprites (personnages, véhicules, décors) par rapport au monde */
    int edition;                 /* édition du paquet (EDIT) : 0 18+, 1 enfant, 2 unique (le 18+ sous code : verrou18) */
    int rain_freq, rain_int;    /* météo (METE) : part du temps sous la pluie et intensité, en % */
    float jour_s, jour_debut;    /* cycle jour / nuit (CYCL) : durée d'une journée (s), heure de départ (0..1) */
    /* Rencontres : distance moyenne entre deux (RENC, unités du monde × ech, 0 : aucune), ennemis (ENNE), terrain par
       bloc de 2 × 2 cases (ZONE, zone_n × zone_n, 255 : inconnu ; NULL : pas de rencontre) */
    float renc;
    enemy_t enemies[MAX_ENEMIES]; int nenemies;
    /* Scénario : table des événements en mémoire ; chaînes et code lus à la demande sur la carte SD (eng_config_t.scen_read)
       ou dans la section SCEN (sc_mem) ; sc_idx : index des chaînes ; sc_code : code de l'événement en cours */
    sc_event_t *sc_ev; int sc_nev, sc_nstr; uint32_t sc_idx, sc_size; const uint8_t *sc_mem; uint8_t *sc_code;
    sc_quest_t *sc_q; int sc_nq;  /* quêtes */
    char sc_player[24];          /* chaîne 0 : nom du joueur (ses répliques à gauche) */
    /* Plan de musique du scénario (section SCEN) : pistes (chaînes : noms de musique.bin, 65535 aucune) par défaut, des
       combats, de la victoire, de la défaite, des courses ; zones (cercles) et pistes propres à des ennemis ; mus_plan :
       0 sans plan (music.c prend alors les noms combat, victoire…) */
    int mus_plan; uint16_t mus_defaut, mus_combat, mus_victoire, mus_defaite, mus_course;
    mus_zone_t *mus_z; int mus_nz;
    mus_enn_t *mus_e; int mus_ne;
    uint16_t mus_carte[16];              /* piste de chaque autre carte (chaîne ; 65535 : celle par défaut) */
    int party_perso[4];          /* personnage (PERS) qui donne sa planche à chaque membre de l'équipe, -1 : planche commune */
    const uint8_t *zone; int zone_n;
    /* chunks : répertoire (décalage dans CDAT, taille compressée, taille brute), fenêtre autour de Sera */
    int nc;                      /* chunks par côté */
    const uint8_t *chunk_dir;
    uint32_t cdat_off, raw_max, comp_max, mem_max;
    chunk_t *slot[CWIN * CWIN];  /* place du chunk (cx, cz) : (cz mod CWIN) × CWIN + cx mod CWIN */
    /* Mémoire des places : un seul bloc, que l'intérieur visité reprend (fen_int : il l'occupe, les places sont vides) */
    uint8_t *fen_mem; uint32_t fen_cap; int fen_int;
    /* Taille en mémoire de chaque chunk de la carte courante (CMEM : u16 en unités de 16 octets, ligne par ligne) ; NULL :
       paquet plus ancien, chacun prend mem_max (places égales, comme avant) */
    const uint8_t *cmem;
    uint8_t *zbuf, *rbuf;        /* chunk compressé, puis décompressé */
    /* Textures réduites des blocs de 2 × 2 cases (64 × 32 × 32 par zone) : seuls les chunks voisins de celui de Sera
       (et le sien) en tiennent une ; tout ce qui est visible (15 cases au plus) est dans ces 3 × 3 chunks */
    uint8_t *lodbuf[LOD_BUFS]; int8_t lodbuf_slot[LOD_BUFS];
    /* carte du menu : map_n × map_n couleurs RGB */
    int map_n; const uint8_t *map_rgb;
    /* Images indexées (IMAG ; IMGX du paquet léger : pixels sur la carte SD) : table de 12 octets par image, données à
       img_base + leur décalage ; img_max : plus grande (w × h) */
    const uint8_t *img_tab; int nimg; uint32_t img_base, img_max;
    uint32_t img_cap;   /* tampon des images : la plus grande, ou les deux images de la plus grande paire face / dos */
    /* Cut-ins des techniques (CUTI) : images first à first + n - 1 du personnage pers (index PERS, 255 : Sera) dans la
       tenue tenue (index TENU, 255 : la sienne) ; plein : images plein écran (coup de grâce, grand sort), sinon bandeaux ;
       paire : bandeaux par deux, la face puis le dos (édition 18+) ; degats : bandeaux de l'héroïne touchée, 1 sous la
       moitié de ses PV, 2 sous le quart (édition 18+) */
    struct { uint8_t pers, tenue, n, plein, paire, degats, special, ed; uint16_t first; } cutins[CUT_PERS]; int ncutins;   /* special : 1 transformation, 2 attaque ultime ; ed : 1 18+, 2 version sage d'un jeu 18+ (cut_vu) */
    int16_t ouv_img[OUV_CARTES];   /* image de chaque carton de l'introduction (OUVI), -1 : aucune */
    int16_t scene_img[SCENES_MAX]; int nscenes;   /* illustration de chaque scène du scénario (SCNI), -1 : aucune */
    uint8_t scene18[SCENES_MAX];   /* scène 18+ (bit 15 de SCNI) : sans image tant que le 18+ est verrouillé */
    const uint8_t *nsur; int nnsur;   /* noms sûrs des bâtiments et des lieux du guidage (NSUR, 28 octets chacun) */
} world_t;

/* Caméra */
typedef struct {
    float x, y, z, cy, sy, cp, sp, yaw, pitch;
} camera_t;

/* Personnage */
typedef struct {
    float x, z, dir, anim;
    int frame;
    int state;                   /* 0 attente, 1 marche, 2 course (animation de sa planche) */
} actor_t;

/* Position d'un personnage à l'écran natif (centre du sprite, haut, pieds) */
typedef struct { int vis; float cx, cy, top, foot; } screen_pos_t;

/* Intérieur chargé (interieur.c) : plan de w × d cases, grille de marche gw × gh au pas res (décalée de 2 : 0 mur ou
   meuble, 1 libre, 2 seuil d'une sortie), hauteur h, échelle humaine f. Polygones au format pak_poly_t (décalés de IO ;
   les nsol premiers : sols et plaques ; drapeau INT_WALL : peut s'estomper devant Sera), objets (murs, linteaux, portes,
   meubles : leurs polygones et leur rectangle au sol ; mesh : meuble low poly, triangles triés), arbre des pièces
   (kind 0 feuille : ses objets ; 1, 2 : coupe en x, en z à c, objets posés sur la coupe, côtés lo et hi), sorties
   (côté 0 z0, 1 x1, 2 z1, 3 x0 ; porte : index de la porte du plan). rx, rz, ryaw : retour dans la rue. Étages : un
   intérieur par niveau, ceux d'un bâtiment à la suite dans INTR (cur : celui chargé, etage : son rang) ; escaliers :
   zone de la grille (3 : il monte, 4 : la trémie, on descend), arrivée sur le niveau vers (+1 au-dessus, -1 au-dessous) */
#define IO (-4096.0f)
#define INT_WALL 4
#define INT_EAU 8                   /* surface de l'eau d'un bassin (sorte d'eau : drapeaux >> 4) */
#define INT_EAU_Y (-0.06f)          /* hauteur de la surface des bassins, fois l'échelle humaine (eauInt de l'émulateur) */
typedef struct { float x0, z0, x1, z1; int p0, np, mesh; } int_obj_t;
typedef struct { int kind; float c; int lo, hi, first, count; } int_node_t;
typedef struct { float x0, z0, x1, z1; int side, porte; } int_sortie_t;
typedef struct { float x, z, dir; int perso, danse, nage, metier; } int_pnj_t;   /* personnage à métier : place du plan, personnage (PERS), danseuse (sa danse), nageur (dans un bassin), métier + 1 (0 : inconnu) */
typedef struct { float x0, z0, x1, z1, x, z, dir; int vers; } int_esc_t;
#define INT_ESC_MAX 4
typedef struct {
    int on, bat, sombre;         /* sombre : éteint (fermé à clé, ou à horaires la nuit, sans bats[].eclaire) */
    float w, d, res, h, f;
    int gw, gh, npoly, nsol, nobj, nnode, nsort;
    int_pnj_t pnj[INT_PNJ_MAX]; int npnj;
    int_esc_t esc[INT_ESC_MAX]; int nesc, etage, cur, zone;
    int npc0, npcn;              /* ses exemplaires dans E.npcs, le temps de la visite (npc0 : premier, npcn : nombre) */
    uint32_t *poly_off; uint8_t *poly_base;
    int_obj_t *obj; int_node_t *node; int_sortie_t *sort; uint8_t *grid, *fade;
    float rx, rz, ryaw;
} interieur_t;
static inline const pak_poly_t *int_poly(const interieur_t *I, int i) { return (const pak_poly_t *)(I->poly_base + I->poly_off[i]); }
/* Alarme d'une effraction : compte à rebours (t sur duree), bâtiment (x, z), amende, milice d'un village (clan) */
typedef struct { int on, amende, clan; float t, duree, x, z; } alarme_t;

typedef struct {
    eng_config_t cfg;
    const uint8_t *pak; size_t pak_len;
    world_t w;
    camera_t cam;
    actor_t sera;
    npc_t *npcs; int nnpc;       /* exemplaires des personnages : ils flânent, donc en mémoire */
    int nnpc_base, npc_cap;      /* exemplaires du paquet ; au-delà, jusqu'à npc_cap : véhicules volés */
    int talk;                    /* interlocuteur à portée : -2 personne, TALK_TRAF(k) trafic, sinon exemplaire */
    int ride;                    /* véhicule conduit (exemplaire), -1 à pied : Sera n'est pas dessinée, il suit sa position */
    float ride_v;                /* vitesse du véhicule conduit (cases par seconde ; négative : marche arrière) */
    float frotte;                /* le véhicule conduit frotte contre un obstacle (0 à 1, son de l'image suivante) */
    float drive_th, drive_st;    /* conduite au doigt (hud.c) : accélérer (1) / reculer (-1), braquer (-1 gauche, 1 droite) */
    int drive_touch;             /* un doigt conduit (drive_th, drive_st valent) */
    int manette;                 /* manette connectée (eng_input_t.manette) : pas de rails de conduite au doigt */
    int drive_alt;               /* boutons d'altitude tenus : 1 monter, -1 descendre */
    float ride_alt;              /* vaisseau : altitude au-dessus du sol (cases) */
    int ride_phase;              /* vaisseau : 0 en vol ou posé, 1 décollage, 2 atterrissage (puis Sera descend) */
    float ride_coule;            /* véhicule qui coule (lac, mer) : secondes depuis qu'il a perdu pied, 0 sinon ; ride_alt :
                                    sa hauteur au-dessus du fond */
    float rive_x, rive_z;        /* dernière place hors de l'eau du véhicule conduit (Sera y revient s'il coule là où elle
                                    ne peut pas nager) */
    traffic_t traffic[TRAFFIC_MAX]; float traf_spawn_t;
    float yaw, pitch, cam_dist, zoom;
    float time_s, pal_t;
    int fog_on, dither_on, theme;
    float ville_v;   /* Sera en ville, 0 à 1 (game.c, ville_maj) : le brouillard y commence plus loin (render.c) */
    /* Météo : weather 0 automatique (phases tirées selon rain_freq), 1 toujours clair, 2 toujours la pluie ; rain : intensité
       actuelle 0..1 (fondu de RAIN_FADE s vers la cible), raining et phase_left : phase automatique en cours */
    int weather, raining; float rain, phase_left;
    int meteo_choix;             /* réglage du joueur (SYSTÈME › MÉTÉO) : « meteo auto » d'un script y revient */
    /* Pluie de cendre (cendre_update de game.c) : temps passé dessous sans protection depuis le dernier coup (s), heure
       (E.clock) du dernier coup (la jauge des PV pulse) */
    float cendre_t, cendre_coup;
    int essai, eveil_essai;      /* code CODE_ESSAI : mode d'essai ; SYSTÈME › ÉVEIL : Sera transformée partout (eveil_hors_combat) */
    int renc_mode;               /* SYSTÈME › RENCONTRES : 0 normales, 1 rares, 2 très rares, 3 aucune, 4 fréquentes (RENC_FOIS de game.c) */
    int volant;                  /* SYSTÈME › DIRECTION : virage du joueur en course, 0 très douce à 4 très vive (COURSE_VOLANT de course.c) */
    int cycle; float heure;      /* cycle jour / nuit : 0 automatique, 1 toujours le jour, 2 toujours la nuit, 3 l'heure réelle ; heure 0..1 (0,5 midi) */
    int horloge;                 /* heure réelle (eng_set_horloge) : secondes depuis minuit, -1 inconnue */
    int ui_block;                /* menu ou dialogue ouvert : Sera ne bouge pas */
    int has_dest; float dest_x, dest_z;   /* destination touchée */
    int dest_court;              /* destination touchée loin de Sera à l'écran (hud.c, DOIGT_COURSE) : elle y court */
    /* Destination hors d'atteinte (game.c, BLOQUE_S) : point de départ de la fenêtre de mesure et son temps ; dest_bloque :
       Sera s'est arrêtée devant l'obstacle, le doigt qui vise encore ce point (dest_bx, dest_bz) ne la relance pas */
    float bloque_x, bloque_z, bloque_t; int dest_bloque; float dest_bx, dest_bz;
    int guide;                   /* lieu suivi du menu GUIDAGE (index de w.guides, -1 : aucun) */
    interieur_t in;              /* intérieur où est Sera (in.on) */
    int porte;                   /* porte devant Sera dans la rue (index de w.portes, -1 : aucune) */
    alarme_t alarme;
    screen_pos_t scr_sera, scr_npc;   /* scr_npc : l'interlocuteur (bulle) */
    screen_pos_t scr_party[4], scr_enemy;  /* combat : membres de l'équipe, point visé de l'ennemi (cx, cy) */
    float clock;                 /* temps de jeu en secondes (eng_update) : combat, bannières, fondus */
    /* Fenêtre de chunks à recentrer (début ou fin de combat) : l'image préparée entre-temps n'a pas de 3D, et les chunks
       se chargent à la mise à jour suivante, quand plus aucune bande ne lit les textures réduites de l'ancienne fenêtre */
    int stream_hold;
    int pass_arme, nnpc_c0;      /* passage repris une fois Sera sortie de toute zone ; exemplaires de la carte 0 */
    npc_t *npc_tous;             /* tous les exemplaires (PNJS) : la planète, sa réserve, puis un bloc par autre carte… */
    int npc_off[16], npc_cnt[16], npc_cap0;   /* … début et nombre du bloc de chaque carte ; capacité de la planète */
    float sera_y; int sera_y_ok;   /* hauteur de Sera suivie d'une image à l'autre (sous un pont, la route du dessous) */
    uint8_t flags[SC_MAX_FLAGS / 8];     /* drapeaux du scénario */
    uint8_t ev_done[SC_MAX_EVENTS / 8];  /* événements « une seule fois » déjà faits */
    int team;                            /* membres de l'équipe (bit i : PARTY[i]) ; instruction « equipe » du scénario */
    int8_t tenue_eq[4];                  /* tenue portée par chaque membre (index TENU, -1 : la sienne) : menu ÉQUIP., « tenue » */
    uint8_t job[4];                      /* job de chaque membre (index JOBS, EQ_AUCUN : aucun) : menu ÉQUIP., « job » */
    uint16_t jobs_known[4];              /* jobs connus (bit j) */
    uint8_t eq[4][3];                    /* objets portés (arme, armure, accessoire : index ITEMS, EQ_AUCUN) ; hors du sac */
    int nage;                            /* Sera à la nage : 0 non, 1 eau claire, 2 eau noire (tenue étanche) */
    int level, exp, credits;             /* niveau et expérience de l'équipe, bourse */
    uint16_t mus_force;                  /* instruction « musique » : chaîne de la piste ; 65535 auto, 65534 silence */
    uint16_t obj[2];                     /* objectif (instruction « objectif ») : x, z en 1/8 de case ; 65535 aucun */
    int obj_carte;                       /* carte où l'objectif a été posé */
    uint8_t quests[SC_MAX_QUESTS];       /* état des quêtes : 0 pas commencée, 1 à 253 étape, 255 terminée */
    int qtrack;                          /* quête suivie (guidage, menu) ; -1 : aucune */
    int heroine;                         /* héroïne choisie au départ (index PERS : sa planche, ses tenues) ; -1 : Sera */
    int deverrou18;                      /* édition unique : le code 18+ a été donné (emplacement REGLAGES_SLOT de la sauvegarde) */
    int scene;                           /* instruction « image » : illustration montrée (image IMAG + 1 ; 0 : aucune) */
    int pose_anim;                       /* instruction « pose » : animation (ANIM_*) que joue Sera sur place… */
    float pose_t;                        /* … pendant ce temps encore (s) ; 0 : aucune */
    int partie;                          /* partie en cours (0 à PARTIES - 1) : où va la sauvegarde */
    float temps_jeu;                     /* temps de jeu de la partie (s), gardé dans la sauvegarde */
    uint32_t stream_loads, stream_us;     /* chunks chargés et temps passé depuis la dernière image préparée */
    const char *error;
} eng_t;

extern eng_t E;

/* Effet de la manette (lumière et vibration de la Poké Ball Plus sur la montre) : ENG_EFFET_* ; rien sans elle */
static inline void effet_manette(int effet) { if (E.cfg.effet_manette) E.cfg.effet_manette(effet); }

/* Édition unique (EDIT = 2) : contenu 18+ caché tant que le code CODE18 n'a pas été donné (SYSTÈME › CODE) */
#define CODE18 "1512"
#define CODE_ESSAI "1221"     /* mode d'essai : SYSTÈME › ÉVEIL (transformation à tout moment) ; le même code le coupe */
#define REGLAGES_SLOT 6          /* emplacement de la sauvegarde qui garde le déverrouillage (après les parties, 0 à 5) */
static inline int verrou18(void) { return E.w.edition == 2 && !E.deverrou18; }
/* Jeu de cut-ins (ed : 1 18+, 2 version sage d'un jeu 18+) montré dans l'état du verrou */
static inline int cut_vu(int ed) { return ed == 1 ? !verrou18() : ed == 2 ? verrou18() : 1; }
static inline int tenue_vue(int t) { return t < 0 || t >= E.w.ntenues || !E.w.tenues[t].adulte || !verrou18(); }
void edition_appliquer(void);           /* planches, masques et noms d'après le verrou (world.c) ; au chargement et au code */
void edition_lire(void);                /* déverrouillage gardé (REGLAGES_SLOT), avant edition_appliquer */
int edition_code(const char *code);     /* le code : déverrouille et le garde (1), sinon 0 */
void edition_verrouiller(void);         /* verrouille de nouveau, gardé */
/* Personnage p de la bibliothèque présent (PVIS : drapeau du scénario) ; l'héroïne choisie n'a plus d'exemplaire ; masqué
   tant que le 18+ est verrouillé (édition unique) */
static inline int perso_present(int p) {
    const perso_t *pe = &E.w.persos[p];
    if (p == E.heroine || pe->masque) return 0;
    return pe->cond >= SC_MAX_FLAGS || (((E.flags[pe->cond >> 3] >> (pe->cond & 7)) & 1) != pe->neg);
}
/* PARTY[i] est-il dans l'équipe (Sera, i = 0, toujours) */
static inline int in_team(int i) { return (E.team >> i) & 1; }
/* Exemplaire présent : son personnage l'est (perso_present) et il n'est pas à l'abri de la pluie de cendre (abri_maj) */
static inline int npc_present(const npc_t *n) { return !n->abri && perso_present(n->perso); }
/* Pluie de cendre (game.c) : son intensité là où est la scène (la pluie E.rain dehors sur la planète, combat compris ; 0
   dans un intérieur, sous terre, dans l'espace, sur un autre monde) */
float cendre_dehors(void);

/* Chunk (cx, cz) s'il est dans la fenêtre, sinon NULL */
static inline const chunk_t *chunk_at(int cx, int cz) {
    if ((unsigned)cx >= (unsigned)E.w.nc || (unsigned)cz >= (unsigned)E.w.nc) return NULL;
    const chunk_t *c = E.w.slot[(cz % CWIN) * CWIN + cx % CWIN];
    return c->cx == cx && c->cz == cz ? c : NULL;
}
static inline const chunk_t *chunk_of_cell(int i, int j) { return i < 0 || j < 0 ? NULL : chunk_at(i / CS, j / CS); }
static inline int cell_local(int i, int j) { return (j % CS) * CS + i % CS; }
static inline const pak_poly_t *chunk_poly(const chunk_t *c, uint32_t i) { return (const pak_poly_t *)(c->poly_base + c->poly_off[i]); }

/* Charge les chunks de la fenêtre centrée sur (x, z) qui manquent : tous si sync, sinon ceux qui touchent le chunk de
   (x, z) plus un seul des plus éloignés (les autres aux images suivantes) */
void world_stream(float x, float z, int sync);
/* Places de la fenêtre vidées (rechargées par le world_stream suivant) */
void world_vider_fenetre(void);
/* Passe sur la carte k (CART) : Sera en (x, z), fenêtre de chunks vidée (rechargée à la mise à jour suivante) ;
   exemplaires, trafic, portes et événements de zone ne sont que sur la carte 0. 0 si tout va bien */
int world_carte(int k, float x, float z);
/* Lecture à la demande (world.c ; sans SPRX ni TEXS, tout est en mémoire) : stream_frame au début de la préparation
   d'une image ; ground_tex : texture du sol k pour le rendu, sans lecture (celle de remplacement si elle n'est pas
   arrivée) ; ground_need : la demander pour l'image en préparation (lue dans la limite de l'image, tout de suite si
   now ; 1 si elle est là) ; sheet_block : pixels de la direction d de la planche s (fh lignes de fw × frames), load
   0 : sans lecture (rendu), 1 : dans la limite de l'image, 2 : tout de suite ; NULL si absents */
void stream_frame(void);
const uint8_t *ground_tex(int k);
int ground_need(int k, int now);
const uint8_t *sheet_block(int s, int d, int load);

float height_at(float x, float z);
/* Véhicules (game.c) : type du véhicule conduit (0 à pied), monter dans l'exemplaire k, descendre (0 : pas de place) */
int ride_type(void);
int vehicle_board(int k);
/* Sera lâche le véhicule conduit sans chercher où se poser : il reste là où il était (changement de carte, partie chargée) */
void ride_lacher(void);
void course_rendre(int pret, int avant);   /* fin de course : le véhicule amené repart, Sera reprend le sien ou descend */
int vehicle_unboard(void);
int vehicle_steal(int k);
extern const char *vol_cri;   /* cri du conducteur éjecté au dernier vol (toast) */
int vehicle_spawn(int perso, float x, float z, float dir);   /* nouvel exemplaire du véhicule perso (-1 : pas de place) */
int ship_alt_ok(void);   /* boutons d'altitude utilisables (vaisseau en vol, pas en décollage ni atterrissage) */
/* Vol permanent (carte du système solaire) : la carte en cours l'est ; Sera mise à bord du vaisseau de l'équipe là où elle
   est (garder : sa direction et la caméra restent) ; astre i visité (le saut y mène) */
int vol_permanent(void);
void vol_embarquer(int garder);
int astre_visite(int i);
int veh_rolls(int p);    /* le véhicule p roule (sinon il plane, et glisse sur l'eau) */
float eau_prof(float x, float z);   /* profondeur de l'eau en (x, z) (cases), 0 hors de l'eau et sur un pont */
int veh_noye(int p, float x, float z);   /* le véhicule p qui roule est noyé en (x, z) : lac ou mer plus haut que son gué */
/* Vitesse maxi (cases/s) et accélération (cases/s²) du véhicule p : celles de l'atelier (VEHI), sinon celles de son type */
void veh_params(int p, float *vmax, float *acc);
float ground_y(float x, float z);
float deck_above(float x, float z, float y);   /* tablier d'un pont au-dessus (NAN : aucun, ou caméra dessous) */
/* Hauteur au sommet (i, j) ; 0 si son chunk n'est pas chargé */
static inline float hv(int i, int j) {
    int n = E.w.n;
    if (i < 0) i = 0; else if (i > n) i = n;
    if (j < 0) j = 0; else if (j > n) j = n;
    int cx = i / CS, cz = j / CS;
    if (cx >= E.w.nc) cx = E.w.nc - 1;
    if (cz >= E.w.nc) cz = E.w.nc - 1;
    const chunk_t *c = chunk_at(cx, cz);
    return c ? c->hv[(j - cz * CS) * CV + i - cx * CS] : 0;
}

/* Paquet */
const uint8_t *pak_section(const char *tag, uint32_t *size);
/* Image k de la banque (IMAG) : ses index (un octet par pixel) et sa palette RGB565 (*pal, pour ui_image_pal), lus au
   besoin dans le tampon commun (une image à la fois) ; dessin = 1 si elle est dessinée à cette image. NULL : pas d'image
   k, ou une autre image du tampon est encore à l'écran (redemander à l'image suivante). img_trame : au début de chaque
   image de l'interface. */
const uint8_t *img_ix(int k, int *w, int *h, const uint16_t **pal, int dessin);
int img_paire(int k, int dessin);   /* images k et k + 1 (paire face / dos) ensemble dans le tampon ; 0 : pas encore */
void img_trame(void);
/* Image d'un cut-in du personnage pers dans la tenue (index TENU, -1 : la sienne ; sans jeu pour elle : ceux de sa tenue
   de base), plein écran si plein le demande et qu'il y en a (*est_plein), sinon un bandeau ou une paire face / dos
   (*est_paire : l'image et la suivante) ; degats (1, 2) : un bandeau de dégâts de ce degré ; autre que avant si possible ;
   -1 : aucun */
/* Genre d'une image de cut-in (octet 5 de sa ligne de IMAG, d'après sa pose : tools/cutin, js/cutins.js de l'Atelier) et
   genre voulu par l'action (cut_genre : effet de l'arme, de la technique ou du sort) ; cutin_tirer prend l'image la
   plus proche : même genre, puis même famille (CG_PARENT : armes à distance, lames, magie), puis une pose neutre */
enum { CG_NEUTRE, CG_LAME, CG_TIR, CG_LASER, CG_CORPS, CG_MAGIE, CG_FOUDRE, CG_FEU, CG_GLACE, CG_SACRE, CG_TENEBRES, CG_SOIN,
       CG_FOUET, CG_CANON, CG_ARC, CG_N };
int cut_genre(int fx, int soin);
int cut_genre_nom(const char *nom);   /* d'après le nom d'une technique sans effet propre ; CG_NEUTRE sans mot-clé */
int cutin_tirer(int pers, int tenue, int avant, int plein, int degats, int special, int genre, int *est_plein, int *est_paire);
/* Transformation ultime (quête « Éveil », niveau NIV_EVEIL) : le membre m connaît le job ÉVEIL et l'équipe a le niveau */
int eveil_dispo(int m);
/* Couleur de l'aura du membre m transformé, celle de ses cut-ins (AURA de tools/cutin/generer.py, d'après son nom) ; Sera
   transformée hors combat (mode d'essai, SYSTÈME › ÉVEIL) */
uint32_t eveil_aura(int m);
static inline int eveil_hors_combat(void) { return E.essai && E.eveil_essai; }

/* Rendu */
void render_init(void);
void render_palette_changed(const uint8_t *indices, int count);
/* Ambiance de l'heure h (0..1) : ciel, brouillard, lumière ; la table des couleurs n'est refaite que si elle a changé */
void render_ambiance(float h);
/* Point de l'écran interne -> sol (rayon depuis la caméra), 0 si rien n'est touché */
int pick_ground(float px, float py, float *x, float *z);   /* px, py : pixels de l'image 3D */
extern int ui_ox, ui_oy;   /* origine de l'interface dans l'image (ui.c) : 0 sur l'écran rond */
/* Point du monde -> écran natif, 0 s'il est derrière la caméra */
int project(float x, float y, float z, float *sx, float *sy);
/* Disposition du combat (caméra fixe de lacet BAT_YAW, pente BAT_PITCH, à BAT_DIST du centre ; distances ÷ ech, à la
   taille des sprites) : centre entre l'arène et l'ennemi, ennemi BAT_EAV plus loin et BAT_EG à gauche (en haut à gauche
   de l'écran, BAT_ETAILLE fois sa taille), équipe côte à côte en bas à droite (membre i : BAT_PAS plus à droite, en
   quinconce léger), avant son élan ; l'élan au contact s'arrête à BAT_CONTACT de l'ennemi (comme battleCenter, battleMember et BCAM de l'émulateur) */
#define BAT_YAW 0.1f
#define BAT_PITCH 0.64f
#define BAT_DIST 4.3f
#define BAT_HAUT 1.3f
#define BAT_EAV 1.5f
#define BAT_EG 0.6f
#define BAT_ETAILLE 1.0f
#define BAT_PAS 0.45f
#define BAT_CONTACT 0.7f
static inline void battle_center(float *x, float *z) { *x = E.w.arena_x; *z = E.w.arena_z + 2.5f; }
static inline void battle_member(int i, float *x, float *z) {
    float cx, cz; battle_center(&cx, &cz);
    const float fx = sinf(BAT_YAW), fz = cosf(BAT_YAW), rx = cosf(BAT_YAW), rz = -sinf(BAT_YAW), b = -0.2f - (i & 1) * 0.12f, r = 0.15f + i * BAT_PAS;
    *x = cx + (fx * b + rx * r) / E.w.ech; *z = cz + (fz * b + rz * r) / E.w.ech;
}
/* Planche de l'animation a du personnage dont la planche de base est base (base si elle n'existe pas) */
static inline int anim_sheet(int base, int a) { return base < 0 || E.w.sheets[base].anim[a] < 0 ? base : E.w.sheets[base].anim[a]; }
static inline int has_anim(int base, int a) { return base >= 0 && E.w.sheets[base].anim[a] >= 0; }
/* Tenues (game.c). tenue_of : celle du membre m (index TENU, -1 : la sienne) ; mise d'office d'abord (nage, course,
   drapeau), sinon celle qu'il porte ; sec : sans tenir compte de la nage. member_sheet : sa planche (-1 : planche commune).
   tenue_mine : la tenue t est-elle au membre m ; tenue_named : sa tenue de ce nom (-1 : aucune ; NULL : la sienne) */
int tenue_of(int m, int sec);
int member_sheet(int m);
int member_tissu(int m);   /* table de la variante de couleur de la tenue du membre m (0 : aucune) */
int tenue_mine(int m, int t);
int tenue_named(int m, const char *nom);
float swim_alt(void);    /* Sera à la nage : surface de l'eau au-dessus du sol sous elle */
int tenue_nage(int perso);   /* tenue de nage du personnage perso (PERS), -1 : aucune */
/* Intérieur visité de nouveau lisible (relu s'il a rendu le bloc de la fenêtre : combat pendant la visite) ; 0 : illisible,
   Sera est remise dans la rue */
int int_pret(void);
int int_case(float x, float z);   /* case de la grille de marche de l'intérieur sous (x, z) (0 hors de la grille ; 5 : eau d'un bassin) */

/* ---------- Équipe, sac et combat (battle.c ; équipe et objets définis dans hud.c) ---------- */
/* delay : tours de retard infligés ; bheal : soin au niveau 20 (party_set_level) */
typedef struct { const char *name; int mp, heal; float mult, delay; int bheal, fx; } tech_t;   /* fx : effet propre (FX_*), 0 : celui de l'arme */
/* bhp, bmp, batk, bspd : PV, PM, attaque, vitesse au niveau 20 (les valeurs écrites dans PARTY, reprises par
   party_set_level) ; lhp, lmp, latk : celles du niveau ; max, maxmp, atk, def, spd, mag : avec le job et l'équipement
   (mag : puissance des sorts, l'ATQ du niveau × (100 + MAG % du job) / 100, plus le MAG de l'équipement) */
typedef struct {
    const char *name; uint32_t color; int hp, max, mp, maxmp, atk, spd;
    const char *eq[3]; tech_t tech[2]; int ntech;
    int bhp, bmp, batk, bspd;
    float lhp, lmp, latk; int def, mag;
} member_t;
/* Objet du sac : ceux de l'Atelier (js/objets.js, section SCEN), sinon ceux d'origine (hud.c). type 0 soin, 1 objet de
   quête, 2 à revendre, 3 divers ; butin : une machine vaincue en laisse un ; prix d'achat (revente : la moitié) ; team :
   soin de toute l'équipe ; icon : icône icon - 1 de la section OBJI (0 : aucune) */
/* type 4 : tenue (tenue : son nom ; se porte depuis le menu ÉQUIP.) */
typedef struct { const char *name, *desc; int count, hp, mp, type, prix, butin, team, icon; const char *tenue; int adulte; } item_t;   /* adulte : objet d'une tenue 18+ (item_vu) */
static inline int item_vu(const item_t *it) { return !it->adulte || !verrou18(); }
#define MAX_ITEMS 255
extern member_t PARTY[4];
extern item_t ITEMS[MAX_ITEMS];
extern int n_items;
void items_init(void);   /* objets d'origine (hud.c), avant le scénario */
int item_potion(void);   /* le premier soin de PV (bouton du combat), -1 : aucun */
int item_loot(void);     /* le premier butin des machines, -1 : aucun */
/* Niveau de l'équipe (battle.c), NIV_MAX au plus : toute l'équipe partage l'expérience ; statistiques × (L + 6) / 26 de
   celles du niveau 20. party_gain_exp renvoie le nombre de niveaux pris ; exp_next : expérience du niveau L au suivant
   (environ 15 + 0,1 L combats à niveau égal) */
#define NIV_MAX 100
void party_set_level(int L);
/* Jobs et équipement (battle.c) : bonus d'un objet d'équipement (NULL : pas un équipement), son emplacement (-1),
   l'affinité du job du membre m pour lui (0 : ne peut pas le porter, 1 à 4), statistiques recalculées ; job : change
   de job (ce qu'il ne peut plus porter retourne au sac ; renvoie le premier objet retiré, -1 aucun) ; equip_reset :
   job, jobs connus et objets portés du départ */
const eqitem_t *eq_item(int i);
int eq_slot(int i);
int eq_aff(int m, int i);
void party_refresh(int m);
int party_set_job(int m, int j);
void equip_reset(void);
int party_gain_exp(int n);
int exp_next(int L);

/* Unités du combat : 0..3 l'équipe, BT_ENEMY l'ennemi */
#define BT_UNITS 5
#define BT_ENEMY 4
/* Effets des armes (type d'équipement, section JOBS) ; l'ennemi : FX_LASER (rafale), FX_BALAYAGE (toute l'équipe) ;
   à partir de FX_MAGIE, les sorts (magies, armes magiques : écran assombri, flashs, secousses) : 12 éléments × 3
   niveaux, FX_SORT(élément, niveau 1 à 3) ; au niveau 1 plus vif et plus petit, au niveau 3 plus ample (temps × MG_TS,
   taille × MG_KS, voiles × MG_VS, secousses × MG_AS, et plus d'éléments à l'écran) */
enum { FX_IMPACT, FX_TAILLE, FX_FOUET, FX_TIR, FX_OBUS, FX_FLECHE, FX_ARCANE, FX_FRAPPE, FX_LASER, FX_BALAYAGE, FX_MAGIE };
enum { MG_FOUDRE, MG_BRASIER, MG_GIVRE, MG_SACRE, MG_METEORE, MG_NEANT, MG_POISON, MG_TERRE, MG_EAU, MG_VENT, MG_NEUTRE, MG_SOIN, MAGIE_N };
#define FX_N (FX_MAGIE + 3 * MAGIE_N)
#define FX_SORT(m, n) (FX_MAGIE + (m) * 3 + (n) - 1)
#define FX_ELEM(fx) (((fx) - FX_MAGIE) / 3)
#define FX_NIV(fx) (((fx) - FX_MAGIE) % 3 + 1)
#define MG_TS(n) ((n) == 1 ? 0.8f : (n) == 2 ? 1.0f : 1.2f)
#define MG_KS(n) ((n) == 1 ? 0.75f : (n) == 2 ? 1.0f : 1.3f)
#define MG_VS(n) ((n) == 1 ? 0.55f : (n) == 2 ? 1.0f : 1.15f)
#define MG_AS(n) ((n) == 1 ? 0.5f : (n) == 2 ? 1.0f : 1.4f)
uint32_t fx_couleur(int fx);   /* couleur d'un sort (0 : effet d'arme), quand une technique le porte (battle.c) */
enum { BP_INTRO, BP_CMD, BP_TECH, BP_ITEM, BP_TARGET, BP_ENEMY, BP_ANIM, BP_DYING, BP_WIN, BP_LOSE, BP_MAGIE, BP_CUTIN, BP_FUI };
/* Cut-in d'une technique (BP_CUTIN, avant BP_ANIM) : durée, entrée et sortie du bandeau (s) */
#define CUT_S 1.2f
#define CUT_S_PLEIN 1.6f         /* cut-in plein écran (coup de grâce, sort de niveau III) */
#define CUT_S_PAIRE 2.0f         /* paire face / dos : le dos à mi-temps */
#define CUT_S_DEGATS 1.4f        /* héroïne touchée sous la moitié, puis le quart de ses PV (après l'attaque ennemie) */
#define CUT_S_TRANSFO 3.0f       /* transformation (ÉVEIL) et attaque ultime : cut-ins longs, énergie par-dessus */
#define CUT_S_ULTIME 2.8f
#define INTRO_S 2.2f             /* cut-in d'entrée en combat (BP_INTRO) : l'équipe face à l'ennemi, puis le premier tour */
#define NIV_EVEIL 80             /* niveau de la transformation ultime */
#define ULTIME_PM 80             /* PM de l'attaque ultime */
#define CUT_IN 0.18f
#define CUT_OUT 0.2f
/* Couleurs des bannières et des nombres : RGB, ou la teinte 1 ou 2 du thème */
#define BT_C1 0x01000000u
#define BT_C2 0x02000000u
/* Action : attaque, technique, sort ou objet (heal : soin ; item : objet, pas d'élan au corps à corps ; fx : effet
   propre de la technique ou du sort, 0 : celui de l'arme ; sort : magie, dégâts × MAG ; soin : 2 toute l'équipe) */
typedef struct { const char *name; int mp, heal, item; float mult, delay; int fx, sort, soin; } bt_act_t;
#define BT_POPUPS 12
typedef struct {
    int on;                      /* combat en cours */
    int phase; float t0;         /* phase (BP_*) et son début (E.clock) */
    int actor;                   /* unité dont c'est le tour */
    float ct[BT_UNITS];          /* timeline CTB : la plus petite valeur joue, puis avance de 100 / VIT × rang */
    float off[4], anim[4]; int frame[4], defending[4];   /* élan vers l'ennemi, marche, défense */
    /* ennemi : personnage de la bibliothèque */
    int perso, ek, var; char name[24]; int lvl, hp, max, atk, spd; float flash; int visible;   /* ek : rang dans E.w.enemies ; var : sa palette (PVAR), tirée au début */
    bt_act_t pending; int locked;    /* action en attente de sa cible ; cible verrouillée (deuxième toucher : action) */
    /* action en cours ; fx : effet (FX_*, -1 aucun), col : sa couleur (RGB ou BT_C1), k : taille (technique : 1,4),
       seed : tirage fixe des particules, cue : prochain son de l'effet, scue : prochaine secousse, melee : élan, cast :
       animation de technique */
    /* an : action en cours ; melee : élan et frappe au contact, cast : technique (sort, piratage, objet), tir : à distance
       (ni élan ni frappe, recul au coup ; battle.c, lancer) */
    struct { int who, hit, melee, cast, tir, all, ntarget, targets[4]; bt_act_t act; float t0; int fx, seed, cue, scue; uint32_t col; float k; } an;
    struct { char text[40], sub[112]; uint32_t color; float t0; int hold, on; } ban;         /* bannière */
    int boss;                    /* combat du scénario : récompense × BOSS_RW, pas de fuite */
    int fuites;                  /* essais de fuite ratés dans ce combat (chacun rend la suivante plus sûre) */
    int rw_xp, rw_cr, rw_loot;   /* récompense de la victoire (gagnée en revenant au monde) ; rw_loot : objet, -1 aucun */
    struct { char text[24]; uint32_t color; float t0; int unit, dx, on; } pop[BT_POPUPS];   /* nombres flottants */
    /* cut-in : image tirée au choix de la technique tech (lue d'avance), la précédente, début ; action qui suit */
    struct { int img, last, plein, paire, reac, special; const char *tech; float t0; bt_act_t act; } cut;   /* special : 1 ÉVEIL, 2 ULTIME */
    float avance[4];             /* pas en avant du membre qui choisit son action (0 à 1, PAS_ACTIF) */
    int transfo[4];              /* membres transformés (ÉVEIL) : ATQ × 1,5, dégâts reçus × 0,7, halo, ULTIME */
    /* cut-in de dégâts : degres_vus (bit 0 : la moitié, bit 1 : le quart, par membre), image tirée au coup ennemi
       (reac_img, -1 aucune), montrée à la fin de son attaque */
    uint8_t degres_vus[4]; int reac_img;
    int mpage;                   /* page du menu MAGIE */
    float shake, shake_amp, fade_t;   /* secousse restante (s), sa force (× l'écart de 4 pixels), début du fondu au noir */
    float hurt_t[BT_UNITS];      /* dernier coup reçu (E.clock), animation « touché » */
    float ret_x, ret_z;          /* position de Sera avant le combat */
} battle_t;
extern battle_t BT;
/* Un membre choisit son action (éventail, techniques, sorts, objets, cible) */
static inline int bt_choix(void) { return BT.phase == BP_CMD || BT.phase == BP_TECH || BT.phase == BP_MAGIE || BT.phase == BP_ITEM || BT.phase == BP_TARGET; }
#define PAS_ACTIF 0.6f                   /* le membre qui choisit avance d'autant (mètres) vers l'ennemi */
#define CUT_DUREE() (BT.cut.special == 1 ? CUT_S_TRANSFO : BT.cut.special == 2 ? CUT_S_ULTIME : BT.cut.reac ? CUT_S_DEGATS : \
                     BT.cut.plein ? CUT_S_PLEIN : BT.cut.paire ? CUT_S_PAIRE : CUT_S)
/* Lance le combat contre l'ennemi k de ENNE au niveau lvl (0 : celui de sa fiche ; PV et ATQ de la fiche × (lvl + 6) /
   (niveau de la fiche + 6), comme l'équipe) ; boss : combat du scénario, expérience et crédits × BOSS_RW */
#define BOSS_RW 4
void battle_start(int k, int lvl, int boss);
void battle_update(float dt);
/* Commandes de l'éventail (BC_*, arg : technique), toucher hors des boutons (ennemi en ciblage, continuer), retour */
enum { BC_ATTACK, BC_TECHS, BC_ITEMS, BC_DEFEND, BC_FLEE, BC_TECH, BC_POTION, BC_BACK, BC_MAGIES, BC_SORT, BC_SUITE, BC_EVEIL, BC_ULTIME };
/* Sorts du membre m (index dans E.w.sorts, dans l'ordre du catalogue) : ceux de son job et ceux de son équipement
   (accessoire, arme magique), même sans le job ; renvoie leur nombre. Menu MAGIE : quatre à la fois, au-delà trois
   par page et SUITE (sorts_page : premier sort et nombre de la page BT.mpage) */
int sorts_of(int m, int *out);
int sorts_page(int n, int *first, int *pages);
void battle_command(int cmd, int arg);
void battle_tap(float x, float y);
/* Unités des n prochains tours (timeline CTB), renvoie leur nombre */
int battle_timeline(int n, int *out);
/* Rencontres (game.c) : ennemi tiré pour le terrain sous Sera (-1 : aucun) */
int encounter_pick(void);
/* Niveau d'une rencontre ici (0 : celui de la fiche) */
int encounter_level(void);

/* ---------- Course (course.c) ---------- */
/* Pistes (PIS2) : n points de l'axe en boucle fermée (X, Y, Z), abscisse cumulée S (n + 1 valeurs, L = S[n]), points sur
   un pont (PB : murs au bord de la chaussée, sans bandes), demi-largeur de la chaussée, bandes de gravier (les murs
   au-delà), tours, point de la ligne de départ, surface (course.c : 0 asphalte, 1 laser, 2 glace, 3 gravier, 4 terre),
   emprise (plan de l'interface), nom du tracé (PNOM). PISTE : la piste active, la plus proche de Sera quand s'ouvre le
   menu de course (course_choisir), ou le tracé choisi (course_trace) */
typedef struct {
    int n, tours, dep, surface; float hw, bande, L, x0, x1, z0, z1; const float *X, *Y, *Z, *S; const uint8_t *PB;
    const uint8_t *plaques; int nplaques;   /* plaques : 4 octets chacune (point u16, écart i8 en 1/10 de case, sorte u8 : 0 accélération, 1 armes) */
    char nom[24];
} piste_t;
extern piste_t PISTE;
int course_choisir(float x, float z);   /* 0 s'il n'y a aucune piste */
int int_trop_grand(int k);              /* intérieur k (INTR) plus grand que le bloc de la fenêtre de chunks */
extern int course_allege;               /* allègement du rendu en course (render.c ; eng_essai_course le coupe pour comparer) */
int course_npistes(void);
const char *course_surface_nom(void);   /* surface de la piste active (ASPHALTE, LASER...) */
const piste_t *course_piste(int k);
int course_traces(int *out);            /* tracés de la ligne de la piste active (départs à moins de 8 cases) ; out : index */
void course_trace(int k);               /* la piste k devient la piste active */
int course_piste_active(void);          /* index de la piste active (-1 : aucune) */
float course_alt_joueur(void);          /* hauteur de la piste sous le joueur par rapport au sol (en course) */
#define COURSE_N 4   /* concurrents : trois adversaires, puis le joueur (quatre avant le 31/10/2026 : images par seconde) */
enum { CP_GRILLE, CP_COURSE, CP_ARRIVEE, CP_RESULTATS };
/* Concurrent : position, cap, vitesse (vx, vz ; v le long du cap), segment et abscisse sur la piste, progression,
   tour terminé (-1 sur la grille), temps d'arrivée (-1 en course), sens inverse (s), vitesse maxi, accélération,
   trajectoire (écart à l'axe en part de la demi-largeur), élastique ; rotation (w, rad/s), tête-à-queue (spin, s
   restantes), dérapage (slip : angle entre le cap et la route suivie), vitesse, écart à l'axe (lat), direction du
   sprite (vis) et de la caméra (cam), prochaine bouffée de fumée */
typedef struct {
    int perso, joueur, seg, tour, gravier;
    float x, y, z, dir, vx, vz, v, s, prog, fin, inv, vmax, acc, off, rb, anim, pente;   /* y : hauteur de la piste */
    float w, spin, slip, spd, lat, vis, cam, fumee, grip;   /* grip : adhérence du véhicule (VEH2) */
    int arme, plaque; float boost, bouclier, arme_t;   /* arme en main (COURSE_ARME_*), dernière plaque, accélération et bouclier restants (s) */
    float halo;                                        /* halo blanc du bord de l'écran pendant une accélération (0..1) */
    uint8_t frame, walking;
} racer_t;
/* Effet de course : 0 fumée, 1 étincelle, 2 poussière du gravier, 3 éclat de rocher, 4 feuille (décors cassés, aussi
   hors course) ; position, vitesse, âge et durée (s) */
#define COURSE_FX 60
typedef struct { float x, y, z, vx, vy, vz, t, life, sol; uint8_t kind; } course_fx_t;   /* sol : une étincelle s'y éteint */
typedef struct {
    int on, phase, feux, n, tours; float clock, t, t_fin;
    float chrono;                        /* temps à battre (s, 0 aucun : instruction course du scénario) ; raté : place 9 */
    racer_t r[COURSE_N];                 /* n concurrents, le joueur en dernier */
    course_fx_t fx[COURSE_FX]; int fxk;  /* effets (life 0 : place libre), dernière place prise */
    int armes;                           /* plaques d'armes actives */
    int pause, pause_sel;                /* course en pause (menu : reprendre, abandonner) */
    int pret, avant;                     /* exemplaire amené pour la course (-1 : celui que Sera conduisait), véhicule d'avant (-1 : à pied) */
    struct { float x, y, z, vx, vz, v, t; int owner, cible, on; } mis[8];   /* missiles */
    struct { float x, y, z, t; int owner, on; } mines[12];
} course_t;
extern course_t CR;
void course_load(const uint8_t *p, uint32_t size);
void course_noms(const uint8_t *p, uint32_t size);   /* PNOM : noms des tracés, après course_load */
int course_ok(void);      /* drapeau : en véhicule, arrêté près de la ligne */
int course_start(int perso, int tours, const int *rivaux, int nia, int niv, int armes);   /* véhicule (PERS), tours, véhicules des nia adversaires, niveau 0..2, armes 0/1 */
enum { COURSE_ARME_AUCUNE, COURSE_ARME_MISSILE, COURSE_ARME_MINE, COURSE_ARME_BOUCLIER, COURSE_ARME_TURBO };
void course_tir(void);   /* le joueur tire son arme */
void course_effet(int kind, float x, float y, float z, float vx, float vy, float vz, float life, float sol);
void course_fx_maj(float dt);   /* les effets vivent leur vie (hors course : ceux des décors cassés) */
/* Décor cassé par un véhicule à la position (x, z) : 1, son âge t (s), le sens de sa chute (dx, dz) */
int decor_casse(float x, float z, float *t, float *dx, float *dz);
void course_update(float th, float st, float dt);
void course_end(void);
int course_place(void);   /* place de la dernière course (255 : annulée ou aucune) */
void course_cancel(void); /* menu de course fermé sans départ */
int course_rank(int *order);   /* ordre des concurrents (COURSE_N), renvoie la place du joueur */

/* Scénario (script.c) */
void script_load(const uint8_t *p, uint32_t len);
void script_update(float dt);
int script_talk(int target);
int script_running(void);
int script_image_seule(void); void script_image_passer(void);   /* illustration nouvelle montrée seule (script.c) */
void script_after_load(void);
void script_abort(void);
void script_choose(int k);
void script_string(int i, char *buf, int cap);   /* chaîne i du scénario (vide si elle manque) */
/* Mise en scène du scénario (instructions camera, acteur, marcher, regarder, fondu, secousse, fuite ; game.c, comme
   mise de l'émulateur) : acteurs (exemplaires temporaires d'un personnage de la bibliothèque, un par personnage ; leur
   personnage n'est plus dessiné ailleurs tant qu'ils sont là), caméra de script (cam : x, z, lacet, zoom ; de a vers b en
   cam_d s), marche de Sera, fondu au noir (0 à 1), secousse, fuite (cercle à atteindre avant la fin du temps) */
#define ACT_MAX 8
#define MARCHE_MS 1.6f      /* marche d'une scène (m/s) ; vitesse 1 */
#define FUITE_RECUL 12.0f   /* les poursuivants partent à tant de cases derrière Sera */
#define FUITE_VITESSE 4.6f  /* m/s : un peu moins que Sera à la course (5,2) */
#define FUITE_PRISE 0.6f    /* cases : rattrapée */
typedef struct {
    uint8_t on, perso, garder, walking, court, chasse, carte;
    uint8_t var;            /* palette : celle de l'exemplaire posé qu'il remplace (PAL_VUE), sinon la base */
    uint16_t frame;
    float x, z, dir, tx, tz, v, anim;
} acteur_t;
typedef struct {
    acteur_t act[ACT_MAX];
    int cam_on, cam_retour;
    float cam_t, cam_d, cam_a[4], cam_b[4], cam[4], yaw0, zoom0;
    int sera_on;
    float sera_tx, sera_tz, sera_v;
    float fondu, fondu_a, fondu_b, fondu_t, fondu_d;
    float sec_t, sec_f;
    int fuite_on, fuite_ok;
    float fuite_x, fuite_z, fuite_r, fuite_t;
} mise_t;
extern mise_t MISE;
void mise_update(float dt);
int mise_bloque(void);                   /* caméra de script ou marche de Sera : le joueur ne la dirige pas */
void mise_fin(int tout);                 /* fin de l'événement (tout : aussi les acteurs gardés, partie chargée) */
int acteur_de(int perso);                /* place de l'acteur de ce personnage, -1 : aucun */
int acteur_poser(int perso, float x, float z, float dir);
int mise_marche(int qui);                /* qui (place, 255 : Sera, -2 : tous) marche encore */
void mise_camera(int retour, float x, float z, float lacet, float zoom, float d);   /* lacet < -9 : celui du moment, zoom 0 : idem */
void mise_fuite(float x, float z, float r, float t, int perso, int n);

/* ---------- Police (police.c ; section « Police » de l'émulateur, à garder identiques) : recherche, poursuite, arrestation ----------
   Niveau de recherche 0 à 3 (planète seulement, hors course ; ni sauvegardé, ni gardé d'une carte à l'autre), motif
   (POL_VOL, POL_EFFRACTION), milice d'un village (clan : à pied seulement) ; Sera vue cette image (vue), dernier point
   connu (lx, lz, vu_t : son heure, t : horloge de la police), temps tenu hors de vue (semer), prochain renfort (spawn_t),
   contact d'une voiture contre son véhicule presque arrêté (prise_t) ; agents à pied (ag) ; arrestation (arr : phase
   ARR_*, arr_t : temps de la phase, arr_tt : depuis le début ; aa : les deux agents qui l'encadrent ; conf : véhicule
   confisqué + 1 ; amende, paye ; l : répliques) */
#define POL_AGENTS 4
#define POL_LOIN0 15.0f           /* cases : les voitures de police apparaissent entre POL_LOIN0 et POL_LOIN1 de Sera, hors de vue */
#define POL_LOIN1 25.0f
#define POL_VUE_R (ENG_DYN ? 38.0f : 19.0f)   /* cases : rayon de vue du rendu (VIEW_R de render.c) */
enum { POL_VOL = 1, POL_EFFRACTION = 2 };
enum { ARR_AUCUNE, ARR_HALTE, ARR_ENCADREE, ARR_DIALOGUE, ARR_FONDU, ARR_RETOUR };
enum { AG_CHASSE, AG_PLACE, AG_DEPART };              /* agent : poursuit Sera, va à sa place (arrestation), s'en va */
enum { POUR_RESEAU, POUR_DROIT, POUR_RALLIE, POUR_GARE };   /* voiture : sur le réseau, tout droit vers sa cible, vers le réseau, garée */
/* Agent à pied : personnage (PERS), palette, allure (0 attente, 1 marche, 2 course), état (AG_*), but (tx, tz), temps
   passé au dernier point connu sans la voir (perdu) */
typedef struct { uint8_t on, perso, var, allure, etat; uint16_t frame; float x, z, dir, anim, tx, tz, perdu; } agent_t;
typedef struct {
    int niveau, motif, clan, vue, arr, conf, amende, amende_eff, paye;
    float t, semer, vu_t, lx, lz, spawn_t, prise_t, arr_t, arr_tt;
    int8_t aa[2];
    agent_t ag[POL_AGENTS];
    char l[4][64];
} police_t;
extern police_t POL;
void police_update(float dt);           /* à chaque image (eng_update) */
void police_reset(void);                /* plus de recherche (autre carte, partie chargée ou nouvelle) */
int police_bloque(void);                /* arrestation en cours : le joueur ne dirige pas Sera */
int police_cible(float *x, float *z);   /* cible des poursuivants : 1 Sera (vue à l'instant), 0 son dernier point connu */
int police_hors_vue(float x, float z);  /* (x, z) hors de l'image : derrière la caméra, ou plus loin que le rayon de vue */
float police_semer_s(void);             /* temps à tenir hors de vue au niveau en cours (jauge de hud.c) */
void police_vol(int npc);               /* Sera vient de voler le véhicule npc (exemplaire) */
void police_effraction(int amende, int clan);         /* alarme d'une effraction semée */
void police_arreter(int motif, int amende, int clan); /* scène de l'arrestation (motif, amende : ceux d'une alarme non semée) */
void police_descendre(int k);           /* la voiture de police k du trafic s'est garée près de Sera à pied : ses agents descendent */
/* game.c : voiture de police poursuivante ajoutée au trafic (0 : pas de place), véhicule ajouté k retiré du monde
   (confisqué), Sera sortie du véhicule conduit, case praticable à pied (sans les personnages) */
int traffic_poursuivant(int perso);
void vehicule_retirer(int k);
void vehicule_sortir(void);
int sol_pieton(float x, float z);

/* Musique (music.c) : chaque image, piste voulue et lecture sur la carte SD ; mixage dans la tâche audio */
void music_update(float dt);
void music_mix(int16_t *out, int n, int rate, float gain);
void music_reset_names(void);   /* nouveau scénario : noms de pistes à relire */
const char *music_now(void);    /* piste en cours (« - » : aucune) */   /* réponse k (1 à 4) au choix en cours (instruction « choix ») */   /* combat du scénario perdu : script interrompu, l'événement pourra repartir */
/* Quêtes (menu QUÊTES, guidage) : nombre, nom, type, nombre d'étapes, objectif de l'étape st (lu à la demande, gardé
   jusqu'à la demande suivante d'une autre étape), quête suivie */
int script_quest_count(void);
const char *script_quest_name(int q);
int script_quest_type(int q);
int script_quest_steps(int q);
const char *script_quest_text(int q, int st);
void script_track(int q);
/* Niveau de l'histoire en (x, z) : celui de l'étape en cours des quêtes principales (une terminée : sa dernière étape),
   relevé par une quête en cours dont l'objectif est à moins de NIV_RAYON cases ; 0 : le scénario n'en donne aucun */
#define NIV_RAYON 24
int script_niveau(float x, float z);
/* Niveau des combats de l'étape st de la quête q (0 : aucun) */
int script_step_niveau(int q, int st);
/* Guidage : l'objectif posé par le script, sinon la cible de l'étape de la quête suivie ; 1 et sa position s'il y en a */
int objective_pos(float *x, float *z);

/* Interface */
void hud_init(void);
/* Réplique du scénario (right : interlocuteur du portrait de droite), dialogue ouvert, message court */
void hud_say(const char *who, const char *text, const char *right);
int hud_in_dialog(void);
/* Ligne native à partir de laquelle l'interface de l'image couvre tout l'écran (panneau opaque du dialogue), 0 : aucune */
int hud_cover_y(void);
void hud_toast(const char *s);
/* Menu de course (véhicule, tours, adversaires, niveau ; -1 : valeur par défaut) ; 0 sans piste ni véhicule */
int hud_course_menu(int tours, int nia, int niv, int veh);
/* Course imposée (instruction course du scénario, RACE2) : tracé (nom, NULL au choix), armes (-1 au choix), temps à
   battre (s, 0 aucun), rivaux (nriv < 0 : au choix) ; ces réglages, et tours et niveau donnés, sont figés au menu */
int hud_course_impose(int tours, int niv, int veh, const char *trace, int armes, int chrono, const int *riv, int nriv);
int hud_course_menu_open(void);
int hud_pause(void);     /* le jeu s'arrête : menu ouvert, ou course en pause */
void course_abandon(void);   /* course abandonnée (menu de pause) : place 255, comme un menu annulé */
/* Carton de titre (instruction « titre » ; sub peut être vide), affiché HUD_TITLE_S secondes */
#define HUD_TITLE_S 3.2f
void hud_title(const char *title, const char *sub);
/* Choix du scénario : n réponses (2 à 4) ; la réponse prise part dans script_choose */
void hud_choice(char opts[][48], int n);
void hud_choice_porte(char opts[][48], int n, const char *titre);
void hud_choice_titre(char opts[][48], int n, const char *titre);   /* choix du scénario avec un titre (sujets d'un personnage) */   /* choix d'une porte fermée (réponse : int_choix) */
void hud_lines(const char *who, const char *const *lines, int n, const char *right);   /* répliques (chaînes gardées) */

/* ---------- Intérieurs (interieur.c) ---------- */
void int_load(void);
void int_update(float dt);
void int_interagir(void);
void int_choix(int k);
int int_libre(float x, float z);
int int_porte_ouverte(int porte);
int int_entrer_porte(int k, int force);
void int_position(float *x, float *z, float *yaw);
void int_reset(void);
void sera_placer(float x0, float z0, float rmax);   /* sur la place libre la plus proche (game.c) */
int int_metier_pnj(int npc);          /* métier + 1 d'un exemplaire de l'intérieur (0 : aucun, hors intérieur) */
const char *pnj_nom(int npc);         /* nom de l'exemplaire : tiré (NOMT) ou celui de son personnage */
int pvar_choix(int perso, unsigned graine);   /* palette d'un exemplaire d'après sa graine (0 : ses couleurs) */
int perso_remap(int perso, int var);  /* table de couleurs du personnage, ou de sa palette var (PVAR) */
const char *int_porte_nom(int porte); /* nom du bâtiment de la porte ("" : aucun) */
int repl_ligne(int liste, char *out, int cap);   /* une réplique générique au hasard de la liste, dans out ; 0 : aucune */
int lz4_decode(const uint8_t *src, int slen, uint8_t *dst, int dcap);
int hud_in_choice(void);
/* Annonce de quête (« NOUVELLE QUÊTE », titre, objectif), affichée quand rien ne la cache, l'une après l'autre */
void hud_annonce(const char *kind, const char *title, const char *text);
void hud_battle_start(void);   /* ferme menus et dialogue, oublie le geste en cours */
void hud_update(float dt, const eng_input_t *in);
void hud_build(void);
/* Parties sauvegardées (hud.c) : résumé de la partie p (0 : aucune sauvegarde valide), chargement, nouvelle partie */
typedef struct { uint32_t seq, ordre; int niveau; float temps; char heroine[16], lieu[24]; } partie_info_t;
int hud_partie_info(int p, partie_info_t *o);
int hud_charger(int p);
void hud_nouvelle(int p);

/* ---------- Ouverture (accueil.c) : écran titre, parties, introduction, choix de l'héroïne ---------- */
void accueil_debut(void);              /* après le chargement du monde : écran titre (monde arrêté, Sera cachée) */
void accueil_fin(void);                /* nouvelle partie tout de suite (banc d'essai) : Sera au départ */
int accueil_on(void);
/* Montre verrouillée (eng_verrouiller) : cadran dessiné par hud_build, monde arrêté ; hud_verrou : à chaque image, le
   glissement qui déverrouille ; 1 tant que la montre est verrouillée */
int hud_verrou_on(void);
int hud_verrou(float dt, const eng_input_t *in);
float accueil_heure(void);             /* heure montrée (lumière, néons) */
const char *accueil_musique(void);     /* piste de l'écran titre ("" : silence) */
void accueil_update(float dt, const eng_input_t *in);
void accueil_build(void);
int heroine_de(const char *nom);       /* index PERS de l'héroïne de ce nom (-1 : Sera, ou inconnue)  */
uint32_t hud_theme_c1(void);
uint32_t hud_theme_c2(void);

#endif
