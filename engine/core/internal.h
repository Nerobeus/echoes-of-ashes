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
#define LOD_BUFS 9               /* zones de textures réduites : les 3 × 3 chunks autour de Sera (lod_release libère d'abord celles qui en sortent) */
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
/* Zoom fixe selon le lieu (01/11/2026, demande de l'utilisateur) : 200 % hors des villes, 300 % en ville (game.c,
   ville_maj ; plus de pincement ni de réglage du menu) ; la distance d'affichage suit (render.c, dist_k) */
#define ZOOM_DEHORS 3.0f    /* 300 % partout depuis le 07/11/2026 (demande de l'utilisateur ; 200 % avant) */
#define ZOOM_VILLE 3.0f
#define DLG_CHERCHE 0.5f      /* s : la caméra va chercher celui qui parle hors de l'image (mise_update) */
#define CAM_AIMANT 2.5f       /* cases : une caméra de scène vise le personnage à qui l'on parle s'il est si près du point écrit (cam_aimant de script.c) */
#define CAM_ACTEUR 3.0f       /* cases : un acteur si près du point écrit : la scène est mise en place, rien à corriger */
#define SCENE_ZOOM 1.7f       /* zoom écrit dans une scène (« camera … zoom 1,2 ») × ceci (07/11/2026, revue des caméras) : les zooms des scènes datent d'avant les zooms fixes (2 dehors, 3 en ville) : 3 fois plus loin qu'en jeu, derrière les immeubles, personnages minuscules dans la bande que le dialogue laisse */
#define ZOOM_INTERIEUR 3.5f   /* dans un intérieur (02/11/2026, demande de l'utilisateur : 300 % de la rue y était trop loin) */
#define MAX_EAUX 64               /* sortes d'eau (EAUC) : 0 les rivières, puis une par terrain d'eau */

/* Planche propre (SPRS) : 8 directions × frames images de fw × fh, carré du portrait, taille à l'écran */
#define MAX_SHEETS 1024          /* entrées de SPRS (prim_t.sheet : index + 1 sur deux octets ; PERS et TENU : SPIX) ; tableau alloué au chargement */
/* Animations d'un personnage (planches de même cellule) ; anim[a] : index de la planche de l'animation a (-1 : absente).
   ANIM_NAGE (04/11/2026) : la nage d'une tenue de bain qui a aussi sa marche et sa course de terre (bikini porté comme
   armure : la marche était la nage) ; sans elle, dans l'eau, la marche comme avant */
enum { ANIM_WALK, ANIM_IDLE, ANIM_RUN, ANIM_ATTACK, ANIM_HURT, ANIM_KO, ANIM_CAST, ANIM_DEFEND, ANIM_VICTORY, ANIM_DANSE, ANIM_DEPART, ANIM_NAGE, ANIM_TIR, ANIM_N };   /* ANIM_TIR (07/11/2026) : tir au fusil en combat, direction du combat */
/* foot : vide sous chaque direction (pixels sous la rangée la plus basse avec un pixel opaque, toutes images) */
/* pv : ancrage par direction (PIVO, demi-pixels de cellule) si has_pv : le point posé sur la position au sol */
/* tissu : masque du tissu d'une tenue (TIS2 : table de TISSU_TABLE octets, 8 par case, puis les bits ; NULL : aucun) :
   seuls ces pixels prennent les couleurs d'une variante de la tenue (TENV) */
#define TISSU_TABLE(images) ((uint32_t)(images) * 8 * 8)
/* ok : planche utilisable ; data : ses pixels en mémoire (NULL : lue à la demande sur la carte SD, à foff dans le
   fichier, par direction : sl[d] place du cache, -1 si absente ; footok : bits des directions dont foot est calculé) */
/* norm : facteur de normalisation de la silhouette (SNRM, 01/11/2026) : la planche est dessinée × norm pour que sa
   silhouette debout fasse celle de Sera (une cellule plus haute que son personnage le rapetissait) ; 1 sans SNRM */
typedef struct { const uint8_t *data, *tissu; int ok, fw, fh, frames, px, py, psize; float taille, norm; int16_t anim[ANIM_N]; uint8_t foot[8]; int16_t pv[8][2]; int has_pv;
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
/* Membres de l'équipe (PARTY, ordre de MEMBERS du compilateur, js/scenario.js) : SERA, VARN, BORS, LYSE, puis les recrues
   (héros au choix que le joueur n'incarne pas, depuis le 01/11/2026) JADE, SAULE, ÉTUDIANT, ONYX, AZUR, AMBRE. Au combat :
   Sera et EQUIPE_MAX - 1 compagnons au plus, choisis au menu ÉQUIPE parmi les recrues (E.team, E.recrues) */
#define NMEMB 10
#define EQUIPE_MAX 4
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
/* niv : niveau d'équipe où le job enseigne chaque sort (JOBS version 3, une table de ns octets lue sur place dans le paquet ;
   NULL : celui du sort, sorts[k].niv) */
typedef struct { char nom[16]; int pv, pm, atq, vit, mag; uint8_t aff[MAX_EQTYPES]; uint64_t sorts; const uint8_t *niv; } job_t;
typedef struct { uint8_t cat, img; int8_t vit; uint8_t pts; int16_t atq, def, pv, pm, mag; uint8_t el; uint64_t sorts; } eqitem_t;   /* sorts donnés à qui le porte ; pts : cristal du Chœur, points d'apprentissage de ses sorts (JOBS version 3, 0 : aucun) ; el : élément d'une arme + 1 (MG_* ; 0 : aucun ; octet 11 de son entrée de JOBS, 08/11/2026) */
/* Sort (JOBS version 2) : nom, effet (FX_SORT), coût en PM, puissance (× MAG), soin (0 attaque, 1 un allié, 2 l'équipe) */
#define MAX_SORTS 64
typedef struct { char nom[16]; int fx, pm, soin, niv; float mult; } sort_t;   /* soin : 0 attaque, 1 un allié, 2 l'équipe, 3 relève un allié à terre (SOIN_RELEVE) ; niv : niveau d'équipe où on l'apprend (0 : tout de suite) */
#define SOIN_RELEVE 3   /* 02/11/2026 : CHANT DE RAPPEL de « Jobs et équipement », MAG × puissance PV au premier membre de l'équipe à terre */
/* Personnage de la bibliothèque (PERS) : planche (-1 : aucune), drapeaux, répliques (nlines chaînes à la suite) */
/* Personnages de la bibliothèque (PERS). Un octet désigne ceux du monde (pnj, trafic, tenues, scénario ; 255 : aucun, ou le
   joueur) : ce sont les premiers, avant le rang 255. Au-delà (03/11/2026 : le paquet en avait 274, le moteur perdait les
   derniers), les personnages des tenues à métier (danseuses…), que seules les places des intérieurs désignent (INTR : 9
   bits) */
#define MAX_PERSOS 384
#define PERS_FLANE 1             /* flâne autour de sa place */
#define PERS_CONV 2              /* toutes les répliques dans l'ordre (sinon une au hasard) */
#define PERS_REMAP 4             /* table de couleurs propre (variante de couleur) */
#define PERS_VEHICULE 8          /* véhicule : on y monte (type dans les bits 4 à 6 des drapeaux) */
#define PERS_TRAFIC 128          /* véhicule du trafic (apparaît au hasard sur les routes) */
#define PERS_ADULTE 64           /* hors véhicule : planche de base 18+ (édition unique : sa planche de remplacement, ou masqué, tant que le 18+ est verrouillé) */
/* Hors véhicule, bits 4 et 5 des drapeaux : genre du nom tiré par exemplaire (NOMT ; 0 : le nom du modèle, 1 féminin,
   2 masculin, 3 neutre) */
#define PERS_GENRE(f) (((f) & PERS_VEHICULE) ? 0 : ((f) >> 4) & 3)
/* Hors véhicule, bit 7 des drapeaux (celui de PERS_TRAFIC chez un véhicule) : objet inanimé (épave, carcasse ; case
   « Objet inanimé » de l'onglet Personnages, p.objet) : ses exemplaires ne se tournent jamais vers Sera, ne flânent pas
   et gardent la direction de leur place (on peut encore les examiner) ; même règle dans l'émulateur (pe.objet) */
#define PERS_OBJET 128
#define PERS_EST_OBJET(f) (!((f) & PERS_VEHICULE) && ((f) & PERS_OBJET))
/* Véhicule du trafic : d'une case de route (ci, cj) à la suivante (ni, nj), avancement k (0..1), position, cap */
#define TRAFFIC_MAX 8
/* Véhicule du trafic : sur un axe de route (lane : axe road, segment seg parcouru dans le sens sgn, k de 0 à 1), sinon de
   case de rue en case de rue (ci, cj vers ni, nj) ; stuck : temps bloqué ; v : cases/s */
typedef struct { int on, perso, ci, cj, ni, nj, walking, lane, road, seg, sgn; float k, x, z, dir, anim, stuck, v; uint8_t frame, var, urgence;
                 uint8_t poursuite, mode, wl; int16_t wi, wj, wq, ws; int8_t wg; float wk, wx, wz, demi;
                 float sx, sz, ex, ez, serre; uint8_t etroit; } traffic_t;   /* urgence : roule vite (secours, pompiers, police parfois) */
/* Rues (01/11/2026, circulation unique) : de point de voie en point de voie, de (sx, sz) à (ex, ez) pendant que k va de 0
   à 1 (voie de droite de la rue, game.c : voie_rue) ; etroit : la rue sous lui trop étroite pour se croiser ; serre (0 à
   1) : rangé sur le bas-côté pour laisser passer un véhicule qui vient en face (chemin, rue étroite). Un véhicule qui
   passe d'un réseau à l'autre (bout d'un axe et rue) suit la liaison (mode POUR_RALLIE, mêmes champs que le ralliement
   d'un poursuivant) */
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
#define GUIDE_LIEU 2             /* circuit, astroport, puits (ceux du téléport) ; ailleurs, les lieux de la carte et les astres */
#define GUIDE_ASTRES 15          /* groupe des astres (carte du système solaire ; GUIDE_ASTRES de l'émulateur) */
/* groupe : 0 villes, 1 lieux, 2 + rang de la famille du catalogue (FAM_GUIDE de hud.c), GUIDE_ASTRES ; carte : la sienne
   (CART ; 0 la planète) : le menu ne propose que ceux de la carte où est Sera (01/11/2026). 40 octets, comme dans GUID */
typedef struct { float x, z, r; uint8_t kind, groupe, carte, r0; char nom[24]; } guide_t;
/* Intérieurs (interieur.c) : bâtiment qu'on visite (PORT : accès, entrée de INTR ; force : serrure déjà forcée), porte
   (point devant elle dans la rue, normale vers la rue, index dans le plan : 0 la principale) */
enum { ACC_LIBRE, ACC_HORAIRES, ACC_FERME, ACC_SCELLE };
enum { AUT_AUCUNE, AUT_ORDRE, AUT_CLAN };
typedef struct { float cx, cz; uint8_t mode, serrure, niveau, alarme, autorite, force, comm, eclaire, noir, club, carte, gmin, gmax; uint16_t intr; char nom[24]; } int_bat_t;   /* comm : commerce (COMM) + 1, 0 : aucun ; nom : celui du type ; eclaire : intérieur allumé même fermé ; noir : toujours éteint (cinéma) ; club : boîte de nuit ; gmin, gmax : gamme de l'équipement vendu (EQPL), paliers ; 0 : aucun */
/* Commerce d'un intérieur (COMM) : métiers qui le tiennent (bit k : METIERS[k]), prix des soins et d'un verre (0 :
   aucun), types d'objets rachetés (bit k), objets vendus (rangs dans ITEMS) ; eq : types d'équipement vendus (EQPL,
   bit k : type k de JOBS), la liste se fait dans le jeu (hud.c : shop_liste) */
#define COMM_MAX 32
#define COMM_VEND 48
typedef struct { uint16_t qui, soins, verre; uint8_t rachete, n; uint8_t vend[COMM_VEND]; uint32_t eq; } comm_t;
/* Paliers de l'équipement (EQPL ; atelier : js/paliers.js) : niveau de chaque palier ; par objet du sac, son palier et ses
   drapeaux (EQ_VENDU : en boutique ; EQ_BUTIN : lâché par les ennemis de son palier). Butin : chance de base, ennemi rare
   (sorts, ou poids de rencontre au plus RARE_POIDS) et combat de l'histoire (dropEquip de l'émulateur) */
#define PAL_MAX 16
#define EQ_RECENTS 3   /* une boutique vend les 3 derniers paliers ouverts de sa gamme */
#define EQ_VENDU 1
#define EQ_BUTIN 2
#define EQ_ELITE 4   /* lâché par les seuls élites et boss de son palier, et trouvé dans les coffres (cristaux du Chœur, 03/11/2026) */
/* Cristaux du Chœur : points d'apprentissage d'une victoire, debout, le cristal porté (rencontre, ennemi rare ou élite,
   combat de l'histoire) ; magie annoncée au tableau de victoire (rw_app) : RW_APP_MAX lignes au plus */
#define CRISTAL_PT_RENC 1
#define CRISTAL_PT_RARE 2
#define CRISTAL_PT_BOSS 3
#define RW_APP_MAX 8
#define DROP_BASE 0.06f
#define DROP_RARE 0.15f
#define DROP_BOSS 0.35f
/* Soin lâché (04/11/2026, demande de l'utilisateur : « que les ennemis lâchent des potions, surtout au début ») : à
   DROP_SOIN des victoires, un soin de PV du sac (ni de PM, ni qui relève), le plus cher dont le prix ne dépasse pas
   DROP_SOIN_VAL fois les crédits du combat, sinon le moins cher : la POTION au début, SUPER vers le niveau 20, MAXI vers
   40, MÉGA vers 60 (soin_butin de battle.c, soinButin de l'émulateur, combat.js de tools/equilibrage) */
#define DROP_SOIN 0.30f
#define DROP_SOIN_VAL 1.2f
#define RARE_POIDS 2
/* Répliques génériques (REPL) : une liste par métier (METIERS), puis ville, village, campement */
#define REPL_MAX 16
#define REPL_VILLE 11
typedef struct { float x, z, nx, nz, w; uint16_t bat; uint8_t di; } int_porte_t;   /* w : largeur de l'ouverture */
#define TALK_TRAF(k) (-100 - (k))   /* interlocuteur : véhicule k du trafic (à voler) */
#define STOLEN_MAX 8                /* places d'exemplaires pour les véhicules volés et leurs conducteurs éjectés */
#define INT_PNJ_MAX 20              /* places d'exemplaires pour les personnages d'un intérieur (pendant la visite ; 8 avant le
                                       03/11/2026 : la foule d'un club, Interieurs.PNJ_MAX_TYPE de l'Atelier) */
#define CARTE_RESERVE 2             /* places après le bloc d'exemplaires d'une autre carte : le véhicule amené à une course (carrière) */
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
#define DEFI_MAX 16               /* bandeaux défilants des enseignes (section DEFI) au plus */
#define VOL_PITCH 0.32f          /* pente de la caméra en vol permanent (on regarde devant soi) */
/* remap : index dans world_t.remap (6 + i pour une variante, sinon 0 : identité) ; taille à l'écran */
/* cond, neg : présence (PVIS) : là seulement si le drapeau cond du scénario est levé (baissé si neg) ; 65535 : toujours */
/* vremap : variante de couleur sur le tissu de sa planche (PTIV : danseuse d'une tenue de scène recolorée ; 0 : aucune) */
/* var0, nvar : palettes (PVAR) : tables E.w.remap[var0 .. var0 + nvar - 1], variantes 1 à nvar de ses exemplaires */
/* Édition unique (edition_appliquer) : adulte (PERS_ADULTE), sage : planche de remplacement (TENUE_SAGE, -1 : aucune) et
   ses couleurs (PSAG, 0 : celles de la planche), masque : 18+ verrouillé sans planche de remplacement (perso_present) ;
   base : planche et couleurs de la planche de base, gardées le temps du verrou ; membre : membre de l'équipe de ce nom
   (1 à NMEMB - 1, world.c ; -1 aucun) : recruté, il n'a plus d'exemplaire dans le monde (perso_present) */
typedef struct { int sheet, flags, nlines, remap, cond, neg, vremap, var0, nvar; float taille; const char *nom, *lines;
                 uint8_t adulte, masque; int16_t sage, sage_remap; struct { int16_t ok, sheet, remap, vremap, var0, nvar; } base; int8_t membre; } perso_t;
/* Exemplaire (PNJS) : place hx, hz, position, cible de flânerie tx, tz, attente avant de repartir ; depart : starter des
   circuits (son animation de départ pendant le compte à rebours et l'arrivée d'une course) */
#define MAX_NPCS 2048
#define DEPART_IPS 10            /* images par seconde de l'animation de départ du starter */
#define NPC_DRAW 14              /* exemplaires dessinés au plus (les plus proches) */
/* graine : celle du nom tiré (rang dans PNJS ; intérieur : 2048 + (bâtiment visité × 4 + étage) × 8 + place) */
/* Patrouilles : meneur (il se promène toujours) ; chef : indice + 1 dans E.npcs du meneur d'un suiveur (0 : aucun), rang :
   sa place dans la file */
typedef struct { float x, z, hx, hz, dir, anim, tx, tz, wait; uint16_t perso; uint8_t frame, walking, depart; uint16_t graine; uint8_t var, meneur, rang, mien; uint16_t chef; uint8_t fuit, abri, vole, place_ok; } npc_t;   /* mien : le véhicule de Sera (garage.c ; 2 : l'ancien, à retirer ; à la place d'un octet de bourrage) */   /* place_ok : place vérifiée hors de la chaussée (game.c, hors_chaussee) */   /* vole : véhicule volé, signalé tant que Sera le conduit (niveau de recherche qu'il donne : police.c) */   /* fuit : conducteur éjecté qui s'enfuit vers (tx, tz) */   /* var : palette (0 : ses couleurs, k : PVAR k) */   /* abri : passant à l'abri de la pluie de cendre (abri_maj de game.c), ni dessiné ni rencontré */

/* Ennemi de la bibliothèque (ENNE) : personnage (index dans PERS), niveau, poids du tirage, statistiques, terrains où il
   apparaît (bit k : terrain k de ZONE) */
#define MAX_ENEMIES 128          /* 32 avant le 03/11/2026 : le paquet en a 79, les 47 derniers (Glaneur blanc, Reine des
                                    cendres, boss de l'espace…) n'étaient pas lus, et leurs combats n'avaient pas lieu */
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
typedef struct { uint16_t perso; uint8_t carte, step, vaincue; uint16_t quest; float x, z; } cible_t;   /* vaincue : son combat gagné, plus dessinée */
#define CIBLE_MAX 128
int script_cible_active(const cible_t *c);   /* sa quête à l'étape, ou (grande chasse : quest = 0x8000 + événement) son événement à faire */
int script_ev_ouvert(int k);   /* son drapeau d'ouverture est levé */
#define PAL_VUE 12.0f        /* combat du scénario, acteur : palette de l'exemplaire posé du même personnage à moins de tant de cases */
#define CIBLE_DOUBLE 6.0f    /* pas dessinée si un exemplaire posé de son personnage est à moins de tant de cases */
#define CIBLE_DRAW 4         /* les plus proches seulement */
#define CIBLE_IMAGE 0.22f    /* s par image de l'animation d'attente */   /* carte : celle d'une zone */
/* Quête : nom (gardé en mémoire), type (0 principale, 1 secondaire), étapes (8 octets chacune, lues à la demande) ;
   c_st : étape dont le niveau et la cible sont gardés dans c_niv, c_x, c_z (-1 : aucune) */
typedef struct { char nom[40]; int type, nsteps; uint32_t steps_off; int c_st, c_niv, c_carte; uint16_t c_x, c_z; } sc_quest_t;
/* coeur : vaincu, il laisse un CŒUR DE MACHINE dans le sac (drapeau 1 de ENNE) */
/* sorts : ses sorts d'attaque (bit k : sort k du catalogue, section ENSO), magie : part de ses tours où il en lance (%) ;
   src : son rang dans ENNE ; groupe : taille de groupe au plus en rencontre (drapeaux de ENNE, 0 : selon la règle) ;
   atq : ses attaques hors des sorts (section ENAT, dans le paquet : natq attaques de ENAT_ATQ octets, lues sur place ;
   natq 0 : la règle d'avant, rafale laser et balayage thermique) */
#define ENAT_MAX 4       /* attaques par ennemi au plus */
#define ENAT_ATQ 32      /* octets d'une attaque dans ENAT : nom 20, effet, cible, poids, drapeaux, puissance u16, couleur */
typedef struct { int perso, lvl, poids, hp, atk, spd, coeur, magie, src, groupe; uint32_t zones; uint64_t sorts; const uint8_t *atq; int natq; uint16_t faible, resiste; } enemy_t;
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
typedef struct { char nom[24]; int n, ciel, vol, course; float wl, vol_alt;   /* course : carte de course de la carrière (CART, drapeau 2) */ uint16_t visite; uint8_t fog[3]; float sx, sz, ax, az; uint32_t chnk_off, chnk_len, zone_off, zone_len, map_off, map_len; const uint8_t *cmem; } carte_t;   /* cmem : tailles de ses chunks (CMEM) */
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
    /* Bandeaux défilants des enseignes (DEFI, 05/11/2026) : leur grand décor, couleurs, message en colonnes, et leur
       texture en mémoire (defi_buf, réservée au chargement), réécrite par defi_maj ; defi_k0..defi_k1 : plage de leurs
       grands décors (draw_poly ne cherche que là) */
    struct { uint16_t k, len; uint8_t lit, dim; int last; const uint8_t *cols; } defi[DEFI_MAX];
    int defi_n, defi_w, defi_h, defi_v; uint32_t defi_k0, defi_k1; uint8_t *defi_buf;
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
    int psheet0, psage;   /* base 18+ du joueur (édition unique) : sa planche à lui, et celle tant que le 18+ est verrouillé (TENUE_SAGE, -1 : aucune) */
    tenue_t tenues[MAX_TENUES]; int ntenues;
    eqtype_t eqtypes[MAX_EQTYPES]; int neqtypes;
    job_t jobs[MAX_JOBS]; int njobs;
    sort_t sorts[MAX_SORTS]; int nsorts;   /* catalogue des sorts (JOBS version 2) */
    eqitem_t *eqit;                              /* MAX_ITEMS entrées (NULL sans JOBS) */
    uint8_t job0[NMEMB], eq0[NMEMB][3]; uint16_t jobs0[NMEMB];   /* au départ : job, objets portés, jobs connus (bits) */
    perso_t persos[MAX_PERSOS]; int npersos;
    cible_t cibles[CIBLE_MAX]; int ncibles;   /* cibles de chasse (CIBL) */
    const uint8_t *lum; int nlum;             /* lueur des feux (LUMI) : nlum entrées de 12 octets, toutes cartes (render.c : lum_band) */
    const route_t *routes; int nroutes;   /* axes des routes (ROUT) */
    const guide_t *guides; int nguides;   /* lieux du menu GUIDAGE (GUID) */
    const uint8_t *sures; int nsures;     /* zones sûres (SURS, 16 octets chacune : carte u8, 0 × 3, x, z, r f32) */
    /* Ouverture (OUVR) : écran titre (titre, sous-titre, musique, lieu du fond), cartons de l'introduction (lieu x, z en
       cases, x < 0 : noir ; heure, < 0 : celle du départ ; texte), question et héroïnes au choix (index PERS), après Sera.
       Chaînes dans le paquet ; sans OUVR, titre d'office, ni cartons ni choix. ouv_her18 : héroïne proposée seulement
       quand le 18+ est déverrouillé (OUV8 ; AMBRE ; accueil.c, her_vue) */
    const char *ouv_titre, *ouv_sous, *ouv_musique, *ouv_question;
    float ouv_fond[3];
    int ouv_ncartes; struct { float x, z, heure; const char *texte; } ouv_cartes[OUV_CARTES];
    int ouv_nher; int ouv_her[OUV_HEROINES]; uint8_t ouv_her18[OUV_HEROINES];
    uint32_t mip0, nmips; const uint8_t *mips;   /* textures réduites des bâtiments (MIPS) : tuiles des niveaux 1 et 2 de mip0 + k */
    int_bat_t *bats; int nbats;           /* bâtiments qu'on visite et leurs portes (PORT) */
    comm_t comms[COMM_MAX]; int ncomms;   /* commerces des intérieurs (COMM) */
    uint8_t pal_niv[PAL_MAX]; int npal, pal_avance;   /* paliers de l'équipement (EQPL) : niveau de chacun (1 à npal), marge des boutiques */
    uint8_t eq_pal[256], eq_fl[256];      /* palier (0 : aucun) et drapeaux (EQ_VENDU, EQ_BUTIN) de chaque objet du sac */
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
    int party_perso[NMEMB];      /* personnage (PERS) qui donne sa planche à chaque membre de l'équipe, -1 : planche commune */
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
    const uint8_t *scene_var; int nscene_var;   /* variantes par héroïne choisie (fin de SCNI, 6 octets chacune : scene_image) */
    const uint8_t *nsur; int nnsur;   /* noms sûrs des bâtiments et des lieux du guidage (NSUR, 28 octets chacun) */
    const uint8_t *tres; int ntres;   /* trésors posés dehors (TRES, coffres.c : TRES_OCT octets chacun ; atelier : js/tresors.js) */
    const uint8_t *panq; int npanq;   /* panneaux de quêtes (PANQ, panneaux.c : PANQ_OCT octets chacun ; atelier : js/panneaux.js) */
    const uint8_t *ence; int nence;   /* enceintes des villes (ENCE, police.c : enceinte_*), Lucerne ; NULL : aucune */
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
typedef struct { float x0, z0, x1, z1; int p0, np, mesh, mur; } int_obj_t;   /* mur : rang + 1 du pan de mur qui le porte (affiche au mur : son fondu ; 0 aucun) */
typedef struct { int kind; float c; int lo, hi, first, count; } int_node_t;
typedef struct { float x0, z0, x1, z1; int side, porte; } int_sortie_t;
typedef struct { float x, z, dir; int perso, danse, nage, metier, barre, service; } int_pnj_t;   /* personnage à métier : place du plan, personnage (PERS), danseuse (sa danse ; client qui danse sur la piste), nageur (dans un bassin), métier + 1 (0 : inconnu), barre : scène de pole dance (la danseuse tourne autour, POLE_TOUR), service : serveuse (du comptoir aux clients, int_service) */
#define POLE_TOUR 1.4f   /* pole dance : vitesse de rotation de la danseuse autour de la barre (rad/s : un tour en 4,5 s) */
/* Danse de foule (04/11/2026, demande de l'utilisateur : « les personnes sont immobiles ») : qui danse sans animation de danse
   à lui (les clients de la piste) sautille au rythme de DANSEUR_BPM de DANSEUR_SAUT (fois l'échelle humaine) dans son
   attente, se tourne de 45° tous les deux temps (draw_int_npc) et, client au sol, fait quelques pas autour de sa place (sa
   marche, un pas par temps), à DANSEUR_ERRE au plus, à DANSEUR_V, entre deux pauses de DANSEUR_PAUSE0 à 1 s (int_danseurs ;
   drawPnj et interieurDanseurs de l'émulateur) ; pas de marche sur place : une place de 32 Ko par direction dans le cache
   des planches de la montre */
#define DANSEUR_BPM 124.0f
#define DANSEUR_SAUT 0.07f
#define DANSEUR_ERRE 0.6f
#define DANSEUR_V 0.35f
#define DANSEUR_PAUSE0 3.0f
#define DANSEUR_PAUSE1 8.0f
typedef struct { float x0, z0, x1, z1, x, z, dir; int vers; } int_esc_t;
#define INT_ESC_MAX 4
/* Sol surélevé (bloc « hauteurs » de INTR, 03/11/2026) : rectangle du plan, hauteur au début et à la fin de la montée (axe
   0 : plat, y0 ; 1 : monte vers +x, 2 vers +z, 3 vers -x, 4 vers -z) ; le dernier qui contient le point l'emporte (int_sol_y) */
typedef struct { float x0, z0, x1, z1, y0, y1; int axe; } int_haut_t;
#define INT_MARCHE_ESC 0.5f         /* allure de Sera sur une pente (escalier) : part de sa marche */
#define COFFRES_BAT 2048            /* coffres des bâtiments visités : un bit par bâtiment de PORT (coffres.c) */
#define TRESORS_MAX 64              /* trésors posés dehors (TRES) */
#define PANQ_MAX 48                 /* panneaux de quêtes (PANQ) */
#define PANQ_OCT 36                 /* un panneau : x, z f32 ; carte u8, drapeaux u8, 0 u16 ; nom 24 octets UTF-8 */
#define PAN_LISTE 24                /* quêtes montrées au plus sur un panneau */
/* Marque d'un personnage (panneaux.c) : « ! » il ouvre une quête, « ? » l'étape en cours le vise ; secondaire, principale */
enum { MQ_AUCUNE, MQ_OUVRE, MQ_SUITE, MQ_OUVRE_PRINC, MQ_SUITE_PRINC };
#define MQ_RGB 0xFFC83C             /* or : quête secondaire */
#define MQ_RGB_PRINC 0x4FE8FF       /* cyan : quête principale */
#define COFFRE_INT 1000             /* E.coffre : le coffre de l'intérieur visité */
#define TELE_MAX 256                /* lieux du téléport (TELE) : visités ou non (E.tele_vu) */
enum { CONDUITE_LIBRE, CONDUITE_ASSISTEE };   /* SYSTÈME › CONDUITE (pilote.c) */
#define CONDUITE_DEFAUT CONDUITE_ASSISTEE
enum { COMMANDES_DOUBLES, COMMANDES_JOYSTICK, COMMANDES_POINTER };   /* SYSTÈME › COMMANDES : conduite au doigt (drive_touch de hud.c) ; POINTER : le véhicule va au point touché (pilote.c), joystick en course */
typedef struct {
    int on, bat, sombre;         /* sombre : éteint (fermé à clé, ou à horaires la nuit, sans bats[].eclaire) */
    int club;                    /* boîte de nuit (bit 5 de l'accès dans PORT) : lumière sombre, éclairs de couleur, musique techno */
    float w, d, res, h, f;
    int gw, gh, npoly, nsol, nobj, nnode, nsort;
    int_pnj_t pnj[INT_PNJ_MAX]; int npnj;
    int_esc_t esc[INT_ESC_MAX]; int nesc, etage, cur, zone;
    int ncel; float cel[4];      /* cellule (pièce cellule ou cellules) de ce niveau : bloc après la grille (paquet du 01/11/2026), x0, z0, x1, z1 du plan */
    int ncof, cof_sorte; float cof[2];   /* coffre à butin de ce niveau (coffres.c) : bloc après les cellules (paquet du 01/11/2026), point d'action x, z du plan, sorte (0 coffre, 1 caisse, 2 armoire) */
    int npc0, npcn;              /* ses exemplaires dans E.npcs, le temps de la visite (npc0 : premier, npcn : nombre) */
    int_haut_t *haut; int nhaut; /* sols surélevés de ce niveau : escaliers, gradins, estrades (bloc après le coffre) */
    float sy; int sy_ok;         /* hauteur de Sera, lissée (ground_y à sa place ; sy_ok : 0 = à reprendre du sol) */
    uint32_t *poly_off; uint8_t *poly_base;
    int_obj_t *obj; int_node_t *node; int_sortie_t *sort; uint8_t *grid, *fade;
    float rx, rz, ryaw;
} interieur_t;
static inline const pak_poly_t *int_poly(const interieur_t *I, int i) { return (const pak_poly_t *)(I->poly_base + I->poly_off[i]); }
int int_affiche_pan(const interieur_t *I, const int_obj_t *o);   /* pan de mur qui porte une affiche (render.c), -1 : aucun */
/* Alarme d'une effraction : compte à rebours (t sur duree), bâtiment (x, z), amende, milice d'un village (clan) */
typedef struct { int on, amende, clan; float t, duree, x, z; } alarme_t;
#define BT_EMAX 4   /* ennemis d'un combat au plus (groupe : battle.c) */

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
    float mech_pas;              /* chemin du mech conduit depuis son dernier pas (cases, ride_drive) */
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
    int zoom_lieu, zoom_auto; float zoom_cible;   /* zoom selon le lieu (game.c, ville_maj) : 1 dehors, 2 en ville ; zoom en route vers zoom_cible */
    /* Météo : weather 0 automatique (phases tirées selon rain_freq), 1 toujours clair, 2 toujours la pluie ; rain : intensité
       actuelle 0..1 (fondu de RAIN_FADE s vers la cible), raining et phase_left : phase automatique en cours */
    int weather, raining; float rain, phase_left;
    int meteo_choix;             /* réglage du joueur (SYSTÈME › MÉTÉO) : « meteo auto » d'un script y revient */
    /* Pluie de cendre (cendre_update de game.c) : temps passé dessous sans protection depuis le dernier coup (s), heure
       (E.clock) du dernier coup (la jauge des PV pulse) */
    float cendre_t, cendre_coup;
    int essai, eveil_essai;      /* code CODE_ESSAI : mode d'essai ; SYSTÈME › ÉVEIL : Sera transformée partout (eveil_hors_combat) */
    int tele_tout;               /* code CODE_TELE : téléport vers tous les lieux, visités ou non (bit 3 de la conduite de ESVF) */
    int zoom_libre;              /* code CODE_ZOOM : zoom manuel (pincement, molette), plus de zoom fixe selon le lieu (bit 4 de la conduite de ESVF) */
    int essai_mesure;            /* essai de fluidité fait (eng_essai_monde) : plus de sauvegarde automatique jusqu'au prochain chargement */
    int renc_mode;               /* SYSTÈME › RENCONTRES : 0 normales, 1 rares, 2 très rares, 3 aucune, 4 fréquentes (RENC_FOIS de game.c) */
    int arene_gagnes;            /* arène de combat (arene.c) : rangs dont la série a été gagnée (bit r), gardés dans la sauvegarde */
    int volant;                  /* SYSTÈME › DIRECTION : virage du joueur en course, 0 très douce à 4 très vive (COURSE_VOLANT de course.c) */
    int conduite;                /* SYSTÈME › CONDUITE (hors course) : CONDUITE_LIBRE, CONDUITE_ASSISTEE (pilote.c) */
    int commandes;               /* SYSTÈME › COMMANDES : COMMANDES_DOUBLES (deux pouces), COMMANDES_JOYSTICK (un doigt) ou COMMANDES_POINTER (point touché) ; au départ, eng_config_t.joystick */
    int cycle; float heure;     /* cycle jour / nuit : 0 automatique, 1 toujours le jour, 2 toujours la nuit, 3 l'heure réelle ; heure 0..1 (0,5 midi) */
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
    int coffre;                  /* coffre à portée (coffres.c) : -1 aucun, COFFRE_INT celui de l'intérieur, k ≥ 0 le trésor k (TRES) */
    int panneau;                 /* panneau de quêtes à portée (panneaux.c) : -1 aucun, k le panneau k (PANQ) */
    uint8_t cof_ouv[COFFRES_BAT / 8], tres_ouv[TRESORS_MAX / 8];   /* coffres fouillés : bit du bâtiment visité (PORT), du trésor (sauvegarde ESVC) */
    uint8_t tele_vu[TELE_MAX / 8];       /* lieux du téléport visités : bit k, lieu k de TELE (hud.c, tele_maj ; sauvegarde ESVF) */
    alarme_t alarme;
    screen_pos_t scr_sera, scr_npc;   /* scr_npc : l'interlocuteur (bulle) */
    screen_pos_t scr_party[NMEMB], scr_enemy[BT_EMAX];  /* combat : membres de l'équipe, point visé de chaque ennemi (cx, cy) */
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
    uint8_t best_vu[16], best_bat[16];   /* bestiaire (06/11/2026) : ennemis vus, vaincus (rang dans E.w.enemies) ; sauvegarde ESVJ */
    uint8_t eq_up[64];                   /* amélioration de chaque objet (2 bits ; eq_up_niv) ; sauvegarde ESVJ */
    uint8_t veh_up[4];                   /* pièces du véhicule de Sera (VK_*) ; sauvegarde ESVJ */
    uint8_t eq_el[128];                  /* élément donné à chaque objet par la forge d'un ferrailleur (4 bits : élément + 1, 0 : le sien ; eq_el_de) ; sauvegarde ESVK */
    /* Équipe (bit i : PARTY[i], Sera toujours) : team, au combat (EQUIPE_MAX au plus) ; recrues, la bande (team comprise :
       « rejoindre », menu ÉQUIPE) ; impose : compagnons imposés par « equipe » du scénario, que le menu ne retire pas */
    int team, recrues, impose;
    int8_t tenue_eq[NMEMB];              /* tenue portée par chaque membre (index TENU, -1 : la sienne) : menu ÉQUIP., « tenue » */
    uint8_t job[NMEMB];                  /* job de chaque membre (index JOBS, EQ_AUCUN : aucun) : menu ÉQUIP., « job » */
    uint16_t jobs_known[NMEMB];          /* jobs connus (bit j) */
    uint8_t eq[NMEMB][3];                /* objets portés (arme, armure, accessoire : index ITEMS, EQ_AUCUN) ; hors du sac */
    uint32_t appris[NMEMB][2];           /* sorts appris pour de bon (cristaux du Chœur : bit k, sort k ; u32 bas, haut ; sauvegarde ESVH) */
    uint8_t apts[NMEMB][MAX_SORTS];      /* points d'apprentissage de chaque sort (cristal porté au combat : apprentissage de battle.c) */
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
#define CODE_TELE "1985"      /* téléport partout : tous les lieux du téléport, visités ou non ; le même code le coupe (01/11/2026) */
#define CODE_VOITURE "6669"   /* véhicule offert : le menu des véhicules (hud_vehicules), à chaque fois (01/11/2026) */
#define CODE_ZOOM "1111"      /* zoom manuel : pincement à deux doigts et molette, sans zoom fixe selon le lieu ; le même code le coupe (02/11/2026) */
#define CODE_HEROINE "9999"   /* changer d'héroïne à tout moment : la page « Qui es-tu ? » en pleine partie (accueil_heroine_changer, 04/11/2026) */
#define REGLAGES_SLOT 6          /* emplacement de la sauvegarde qui garde le déverrouillage (après les parties, 0 à 5) */
static inline int verrou18(void) { return E.w.edition == 2 && !E.deverrou18; }
/* Jeu de cut-ins (ed : 1 18+, 2 version sage d'un jeu 18+) montré dans l'état du verrou */
static inline int cut_vu(int ed) { return ed == 1 ? !verrou18() : ed == 2 ? verrou18() : 1; }
static inline int tenue_vue(int t) { return t < 0 || t >= E.w.ntenues || !E.w.tenues[t].adulte || !verrou18(); }
void edition_appliquer(void);           /* planches, masques et noms d'après le verrou (world.c) ; au chargement et au code */
void edition_lire(void);                /* déverrouillage gardé (REGLAGES_SLOT), avant edition_appliquer */
int edition_code(const char *code);     /* le code : déverrouille et le garde (1), sinon 0 */
void edition_verrouiller(void);         /* verrouille de nouveau, gardé */
/* Personnage p de la bibliothèque présent (PVIS : drapeau du scénario) ; ni un membre recruté (E.recrues : JADE dans la
   bande n'est plus dans les rues ni au club), ni l'héroïne choisie quand elle n'est pas une recrue (sinon ses exemplaires
   restent, dessinés en Sera : perso_vue) ; masqué tant que le 18+ est verrouillé (édition unique) */
static inline int perso_present(int p) {
    const perso_t *pe = &E.w.persos[p];
    if ((p == E.heroine && pe->membre <= 0) || pe->masque || (pe->membre > 0 && (E.recrues >> pe->membre & 1))) return 0;
    return pe->cond >= SC_MAX_FLAGS || (((E.flags[pe->cond >> 3] >> (pe->cond & 7)) & 1) != pe->neg);
}
/* PARTY[i] est-il au combat (E.team ; Sera, i = 0, toujours) ; dans la bande (E.recrues : au combat ou en réserve) */
static inline int in_team(int i) { return (E.team >> i) & 1; }
static inline int in_band(int i) { return (E.recrues >> i) & 1; }
/* Place du membre i dans l'équipe au combat (0 Sera, puis les compagnons dans l'ordre de PARTY) : battle_member */
static inline int team_place(int i) { int n = 0; for (int k = 0; k < i && k < NMEMB; k++) n += in_team(k); return n; }
/* Exemplaire présent : son personnage l'est (perso_present) et il n'est pas à l'abri de la pluie de cendre (abri_maj) */
static inline int npc_present(const npc_t *n) { return !n->abri && perso_present(n->perso); }
/* Pluie de cendre (game.c) : son intensité là où est la scène (la pluie E.rain dehors sur la planète, combat compris ; 0
   dans un intérieur, sous terre, dans l'espace, sur un autre monde, et sous la neige) */
float cendre_dehors(void);
/* Neige (game.c, 01/11/2026) : Sera dehors sur la planète, dans la classe « neige » de la grille des zones : la pluie y
   tombe en flocons (render.c), sans cendre ni éclairs */
int neige_ici(void);
/* Éclair en cours (game.c) : secondes depuis son début (-1 : aucun), pour le voile de lumière de hud.c (draw_eclair) */
float eclair_age(void);
float club_age(int *couleur);   /* éclair de couleur d'une boîte de nuit : âge (-1 : aucun), couleur 0 à 3 (game.c) */

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
void traffic_rue_poser(traffic_t *t, int i, int j, int ni, int nj);   /* essais : véhicule du trafic posé dans la rue, de (i, j) vers (ni, nj) */
extern const char *vol_cri;   /* cri du conducteur éjecté au dernier vol (toast) */
int vehicle_spawn(int perso, float x, float z, float dir);   /* nouvel exemplaire du véhicule perso (-1 : pas de place) */
int ship_alt_ok(void);   /* boutons d'altitude utilisables (vaisseau en vol, pas en décollage ni atterrissage) */
/* Vol permanent (carte du système solaire) : la carte en cours l'est ; Sera mise à bord du vaisseau de l'équipe là où elle
   est (garder : sa direction et la caméra restent) ; astre i visité (le saut y mène) */
int vol_permanent(void);
void vol_embarquer(int garder);
int astre_visite(int i);
int veh_rolls(int p);    /* le véhicule p roule (sinon il plane, et glisse sur l'eau) */
/* Le véhicule de Sera et ses pièces (06/11/2026 ; garage.c, à la concession de Lucerne) : une trousse montée sur le véhicule qu'elle possède,
   gardée quand elle en change. VK_MOTEUR a trois niveaux, les autres un seul */
enum { VK_GUE, VK_CHENILLES, VK_FLOTTEURS, VK_MOTEUR, VK_N };
#define VK_GUE_X 2.5f        /* gué renforcé : sa hauteur de gué × tant */
#define VK_CHENILLES_K 0.9f  /* chenilles : part de sa vitesse hors des routes (0,55 sans) */
#define VK_FLOT_K 0.5f       /* flotteurs : part de sa vitesse au plus lent, sur l'eau profonde (il ne coule plus) */
#define VK_MOTEUR_X 0.08f    /* moteur : vitesse et accélération en plus, par niveau */
int vk_monte(int p, int k);  /* niveau de la pièce k sur le véhicule p (0 : pas le véhicule de Sera) */
int vk_max(int k);
int vk_prix(int k);          /* prix du niveau suivant */
const char *vk_nom(int k), *vk_desc(int k);
int veh_flotte(int p);       /* le véhicule p roule mais flotte (flotteurs) : à la surface, comme celui qui plane */
int concession_ici(void);    /* Sera dans la concession (bâtiment « Concession » de PORT) */
void concession_ouvrir(void);
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
/* Illustration de la scène k du scénario (SCNI ; instruction « image ») : la variante de l'héroïne choisie (clé
   « NOM@HÉROÏNE » de l'Atelier) s'il y en a une, sinon l'image de la scène ; -1 : aucune (world.c) */
int scene_image(int k);
/* Tampon commun (celui des images, réservé par eng_init) prêté à qui le demande, NULL s'il est trop petit ; celui qui
   l'avait est prévenu (pilote arrêté, aperçus refaits) ; rendu par img_rendre, repris par toute image demandée */
enum { TC_PILOTE = 1, TC_APERCUS, TC_PLATEFORME, TC_CARTE, TC_FANTOME };   /* TC_FANTOME : fantôme du contre-la-montre (course.c) */
/* TC_CARTE (carte.c) : la fin du tampon, img_carte_place() octets, après la place des champs du pilote (les deux tiennent
   ensemble) ; une image, les aperçus ou la plateforme la reprennent (carte_perdue) */
uint32_t img_carte_place(void);
/* Carte du menu (carte.c ; sous-menu CARTE de hud.c) : plans de la section PLAN, zoom et déplacement au doigt */
/* objectif (obj_princ : de la quête principale), guidage, pilote : leurs points ; tele_ok : un toucher propose le téléport ;
   aide : message sous le titre à la place de l'aide (toast de hud.c) */
typedef struct { int obj, guide, pil, pilote_ok, obj_princ, tele_ok; float ox, oz, gx, gz, px, pz; const char *aide; } carte_reperes_t;
enum { CARTE_RETOUR = 1, CARTE_TOUCHE };   /* carte_doigts : bouton retour ; toucher (en véhicule : pilote automatique, à pied : téléport ; carte_touche) */
/* Brouillard de guerre (carte.c, 04/11/2026) : un bit par bloc de BROU_G × BROU_G cases et par carte, levé autour de Sera
   (hud.c, tele_maj) ; ni sur une carte de vol permanent ni sur une carte de course. Ses octets (en-tête, bits, somme de
   contrôle) sont la fin du bloc de la sauvegarde (hud.c, ESVI), écrits tels quels à sa suite */
#define BROU_G 8                        /* côté des blocs (cases) */
#define BROU_R 14.0f                    /* rayon de la découverte autour de Sera (cases, jusqu'au centre d'un bloc) */
uint32_t brouillard_taille(void);       /* octets de toutes les cartes du paquet, en-tête et somme de contrôle compris */
void brouillard_init(uint8_t *mem);     /* hud_init : sa place (brouillard_taille octets), rien de vu */
void brouillard_effacer(void);          /* nouvelle partie : rien de vu */
void brouillard_voir(float x, float z, int carte, float r);   /* blocs dont le centre est à moins de r cases de (x, z) */
int brouillard_vu(float x, float z, int carte);   /* bloc de (x, z) vu (1 sur une carte sans brouillard) */
int brouillard_carte(int carte);        /* la carte a un brouillard */
int brouillard_accepter(uint32_t n);    /* sauvegarde lue en place (n octets) : même paquet (en-tête) ? sinon tout effacé, 0 */
void brouillard_etat(char *out, int n); /* banc d'essai : blocs vus par carte */
void carte_init(void);               /* eng_init : PLAN, ses cartes et ses pictogrammes */
uint32_t carte_taille(void);         /* place à garder pour elle dans le tampon commun */
void carte_perdue(void);             /* world.c : le tampon a été repris */
void carte_ouvrir(void);
void carte_actif(int ouverte);       /* à chaque image : fermée, elle rend le tampon */
int carte_plein(void);               /* ouverte : pas de 3D derrière (render.c) */
int carte_calme(void);               /* immobile depuis une seconde (eng_ecran_calme) */
void carte_maj(float dt);
int carte_doigts(const eng_input_t *in, float now);
uint32_t carte_touches(uint32_t pressed, const eng_input_t *in, float dt);   /* renvoie les touches qu'elle laisse */
void carte_molette(float f);         /* molette, lunette (eng_set_zoom) : zoom × f */
void carte_vue(float x, float z, float s);
int carte_point(float sx, float sy, float *x, float *z);
int carte_touche(float *x, float *z);   /* point du dernier toucher (CARTE_TOUCHE) */
void carte_touche_essai(float x, float z);   /* banc d'essai : un toucher en ce point du plan */
void hud_carte_touche(void);         /* banc d'essai : le toucher de la carte traité (pilote ou téléport, hud.c) */
void carte_dessiner(uint16_t c1, uint16_t c2, const carte_reperes_t *rp);
void carte_etat(char *out, int n);   /* banc d'essai */
void hud_carte_ouvrir(void);         /* banc d'essai : menu CARTE ouvert */
const char *hud_systeme_essai(int code, int toucher);   /* banc d'essai : menu SYSTÈME (ligne logique, activée) */
uint8_t *img_emprunter(int qui, uint32_t n);
void img_rendre(int qui);
void music_reserver(void);           /* music.c : anneau de la musique, réservé par eng_init avant les images */
uint32_t pilote_taille(void);        /* pilote.c : octets des champs (dans le tampon commun) */
uint32_t hud_apercus_taille(void);   /* hud.c : octets des aperçus du menu de course (dans le tampon commun) */
void hud_apercus_perdus(void);       /* hud.c : tampon repris, aperçus à refaire */
uint32_t course_fantome_taille(void); /* course.c : octets du relevé et du fantôme rejoué (dans le tampon commun) */
void course_fantome_perdu(void);      /* course.c : tampon repris, fantôme perdu */
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
int member_sheet_base(int m);   /* sa planche sans tenue */
int member_sheet_mouv(int m);   /* en mouvement : une tenue de pose (ni marche ni course) prend celles de la planche de base */
float joueur_taille(void);   /* taille du joueur : Sera, ou le personnage de l'héroïne choisie (game.c) */
int passage_vers(int carte, float *x, float *z);   /* cible sur une autre carte : le passage à prendre d'ici (script.c) */
int zone_sure(float x, float z);   /* zone sûre : ni rencontre ni ennemi qui chasse (battle.c) */
int enceinte_dedans(float x, float z);   /* dans le mur d'une enceinte de la planète, passage ou non (police.c) */
int member_tissu(int m);   /* table de la variante de couleur de la tenue du membre m (0 : aucune) */
int tenue_base(int t);     /* tenue de base d'une variante de couleur (ses cut-ins), t sinon */
int tenue_mine(int m, int t);
int tenue_named(int m, const char *nom);
float swim_alt(void);    /* Sera à la nage : surface de l'eau au-dessus du sol sous elle */
int tenue_nage(int perso);   /* tenue de nage du personnage perso (PERS), -1 : aucune */
/* Héroïne choisie au départ (03/11/2026, échange des rôles décidé par l'utilisateur) : le joueur porte son nom, et le
   membre de son nom (sa recrue de l'histoire : JADE…) prend la place de Sera à l'écran, son nom, sa planche et ses
   tenues, son portrait, ses cut-ins (255), son aura ; ses exemplaires et ses acteurs se dessinent en Sera. Le bytecode et
   les données gardent SERA et l'héroïne ; seul l'affichage change (game.c ; noms : nom_echange de hud.c, filtre des
   textes de ui.c). heroine_recrue : ce membre (-1 : aucun) ; membre_en_sera : le membre m est cette recrue et se montre
   en Sera (planche de Sera présente) ; perso_en_sera : le personnage p est le sien ; perso_vue : ce qui dessine p
   (planche de base, table de couleurs de sa palette var, réglage d'ombre, table de la variante du tissu, taille) */
int heroine_recrue(void);
int membre_en_sera(int m);
int perso_en_sera(int p);
typedef struct { int sheet, remap, ombre, tissu; float taille; } perso_vue_t;
perso_vue_t perso_vue(int p, int var);
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
typedef struct { const char *name, *desc; int count, hp, mp, type, prix, butin, team, icon; const char *tenue; int adulte, releve, noyau; } item_t;   /* adulte : objet d'une tenue 18+ (item_vu) ; releve : relève un membre à terre (02/11/2026) ; noyau : noyau élémentaire, élément + 1 (drapeau 32 de la table des objets, PM = l'élément ; 08/11/2026) */
#define RELEVE_PART 0.25f   /* un objet qui relève : le membre à terre revient avec ce quart de ses PV max (menu OBJETS ; soinsSac de l'émulateur) */
static inline int item_vu(const item_t *it) { return !it->adulte || !verrou18(); }
#define MAX_ITEMS 255
extern member_t PARTY[NMEMB];
extern item_t ITEMS[MAX_ITEMS];
extern int n_items;
void items_init(void);   /* objets d'origine (hud.c), avant le scénario */
int item_potion(void);   /* le premier soin de PV (bouton du combat), -1 : aucun */
int item_loot(void);     /* le premier butin des machines, -1 : aucun */
/* Niveau de l'équipe (battle.c), NIV_MAX au plus : toute l'équipe partage l'expérience ; PV et soins des techniques
   × (L + 6) / 26 de ceux du niveau 20 (PM : ceux du niveau 20 à tous les niveaux, depuis le 02/11/2026). party_gain_exp
   renvoie le nombre de niveaux pris ; exp_next : expérience du niveau L au suivant (2 + 18 (L − 1) / 58 combats à niveau
   égal). Gains d'ATQ et de DÉF (06/11/2026, demande de l'utilisateur : « je suis niveau 17 et un ennemi de niveau 12 nous
   fait beaucoup de dégâts ») : ATQ du niveau (et MAG, qui en part) = ATQ du niveau 20 × (7 + GAIN_ATQ (L − 1)) / 26 ; DÉF +
   GAIN_DEF par niveau au-dessus du premier (avant : 0, seul l'équipement en donnait) ; niveau 1 inchangé ; party_set_level,
   party_refresh, setLevel et refreshStats de l'émulateur, member de combat.js */
#define NIV_MAX 100
/* Faiblesses (06/11/2026, section ENFA, js/attaques.js de l'atelier : par archétype) : une attaque de cet élément (bits MG_*, sorts et
   techniques à effet de sort) × FAIBLE_X, × RESISTE_X ; apply_hit de battle.c, applyHit de l'émulateur. Bestiaire : E.best_vu, E.best_bat.
   08/11/2026 (demande de l'utilisateur : « le système des faiblesses ne donne pas assez de bonus ») : × 2 et × 0,5 (1,4 et 0,6 avant,
   moins qu'un critique) ; et une arme peut avoir un élément (eqitem_t.el, js/paliers.js : ELEMENTS_ARMES) : l'attaque et les techniques
   sans effet de sort de celui qui la porte en prennent l'élément (arme_element). Le simulateur de l'équilibrage ne compte pas les
   faiblesses : les combats restent étalonnés sans elles */
/* Amélioration de l'équipement (06/11/2026, demande de l'utilisateur : la ferraille sert à quelque chose) : chez un mécanicien (commerce dont
   le métier 3 tient le poste), chaque objet jusqu'à +EQ_UP_MAX, un même exemplaire pour tous ceux du sac : bonus × (1 + EQ_UP_X × n) ;
   prix : EQ_UP_PRIX[n] pour cent du prix de l'objet, et EQ_UP_CRISTAUX[n] cristaux froids (CRISTAL DE MACHINE, l'objet de butin) */
#define EQ_UP_X 0.15f
#define EQ_UP_MAX 3
#define METIER_MECANO 3
int eq_up_niv(int i);
void eq_up_poser(int i, int n);
/* Éléments de l'équipement (08/11/2026, demande de l'utilisateur) : une arme frappe de son élément (arme_element), une armure
   ou un accessoire y résiste (membre_resiste : dégâts × RESISTE_X des attaques et sorts ennemis de cet élément). Élément
   d'origine : octet 11 de l'objet dans JOBS (eqitem_t.el, js/paliers.js : ELEMENTS_EQUIPEMENT) ; la forge d'un ferrailleur
   (Guilde des ferrailleurs, étal du ferrailleur : ferrailleur_ici de hud.c) en donne un autre contre un noyau élémentaire de
   cet élément et ELEM_PRIX % du prix de l'objet (E.eq_el, sauvegarde ESVK). Noyaux : objets du sac au drapeau 32 (item_t.noyau),
   lâchés par les ennemis qui attaquent de leur élément (sorts, attaques élémentaires de ENAT) : NOYAU_BOSS d'un combat de
   l'histoire, NOYAU_RARE d'un ennemi rare ou d'une élite, sinon NOYAU_RENC (noyau_butin de battle.c, noyauButin de l'émulateur) */
#define ELEM_PRIX 30
#define NOYAU_RENC 0.04f
#define NOYAU_RARE 0.12f
#define NOYAU_BOSS 0.25f
int eq_el_de(int i);                /* élément de l'objet i (MG_*, -1 : aucun) : celui de la forge, sinon le sien */
void eq_el_poser(int i, int el);    /* la forge donne l'élément el à l'objet i */
int membre_resiste(int m, int el);  /* le membre m porte une armure ou un accessoire de cet élément */
int noyau_de(int el);               /* objet du sac, noyau de cet élément (-1 : aucun) */
int element_utile(int el);          /* un ennemi du paquet y est faible (une arme de cet élément sert) */
#define FAIBLE_X 2.0f
/* Objets lancés et réactions (07/11/2026, demande de l'utilisateur : une potion fait du mal à un zombie ; objet_utiliser de battle.c, objetUtiliser de l'émulateur) : objet de type
   OBJ_LANCER : PV = ses dégâts, PM = son effet (FX_SORT, dont l'élément compte pour FAIBLE_X), « toute l'équipe » = tout le groupe ennemi ; sans dégâts, un retard de FUMI_RETARD tour. Un soin
   donné à un ennemi : faible à SOIN, il lui fait du mal (ses PV de soin, au plus REACT_PART de ses PV, REACT_BOSS d'un boss) ; l'eau claire aussi à qui est faible à l'EAU ; une machine (résiste à
   SOIN, sans que le bestiaire l'écrive) : sans effet ; sinon, il le soigne pour de bon */
#define OBJ_LANCER 6
#define FUMI_RETARD 0.7f
#define REACT_PART 0.3f
#define REACT_BOSS 0.12f
#define RESISTE_X 0.5f
#define GAIN_ATQ 1.5f
#define GAIN_DEF 3
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
int credits_combat(int lvl);                       /* crédits d'un combat ordinaire au niveau lvl (< 1 : celui de l'équipe) */
int prix_soins(int base);                          /* prix d'un soin (clinique, hôpital, verre, REPRENDRE) au niveau de l'équipe */
/* Équipe (battle.c) : membres au combat ; instruction du scénario (mode 0 equipe, 1 rejoindre, 2 quitter, 3 equipe libre ;
   renvoie qui a rejoint ou quitté la bande) ; équipe d'une sauvegarde remise d'aplomb ; menu ÉQUIPE : le membre m passe
   au combat ou en réserve (EQB_FAIT, ou ce qui l'en empêche) ; noms des membres d'un masque (« JADE, ONYX ») */
enum { EQB_FAIT, EQB_SERA, EQB_IMPOSE, EQB_PLEIN, EQB_HORS };
int equipe_nombre(void);
int equipe_scenario(int mode, int mask);
void equipe_valider(void);
int equipe_basculer(int m);
void equipe_noms(int mask, char *buf, int cap);
/* Paliers de l'équipement (EQPL, battle.c) : palier d'un niveau (le dernier dont le niveau ne le dépasse pas, 0 sans
   paliers) ; niveau jusqu'où vendent les boutiques (max(équipe, histoire) + marge) ; résumé des bonus d'un équipement
   (« ATQ +45 · VIT +1 »), vide sinon */
int palier_de(int lvl);
int eq_limite_vente(void);
void eq_resume(int i, char *buf, int cap);
/* Ce qu'on achète dans le commerce comm du bâtiment bat (objets du commerce, puis l'équipement de sa gamme) : nombre
   d'objets écrits dans out ; équipement lâché à la fin du combat en cours (-1 : rien) */
int boutique_liste(int comm, int bat, uint8_t *out, int cap);
int drop_equip(void);
int equip_hasard(int palier, int fl);              /* un équipement au hasard de ce palier (ou d'en dessous) aux drapeaux fl d'EQPL ; -1 aucun */
extern int bt_atq_force;   /* essais : attaque k du répertoire (ENAT) au prochain tour d'un ennemi, -1 : le tirage (battle.c) */

/* Unités du combat : 0 à NMEMB - 1 les membres (ceux de l'équipe au combat jouent, in_team), BT_ENEMY + k l'ennemi k du
   groupe (BT.en[k], k < BT.nen) */
#define BT_UNITS (NMEMB + BT_EMAX)
#define BT_ENEMY NMEMB
/* Effets des armes (type d'équipement, section JOBS) ; l'ennemi : ceux de ses attaques (ENAT), sans elles FX_LASER
   (rafale), FX_BALAYAGE (toute l'équipe) ;
   à partir de FX_MAGIE, les sorts (magies, armes magiques : écran assombri, flashs, secousses) : 12 éléments × 3
   niveaux, FX_SORT(élément, niveau 1 à 3) ; au niveau 1 plus vif et plus petit, au niveau 3 plus ample (temps × MG_TS,
   taille × MG_KS, voiles × MG_VS, secousses × MG_AS, et plus d'éléments à l'écran) */
enum { FX_IMPACT, FX_TAILLE, FX_FOUET, FX_TIR, FX_OBUS, FX_FLECHE, FX_ARCANE, FX_FRAPPE, FX_LASER, FX_BALAYAGE, FX_MAGIE };
enum { MG_FOUDRE, MG_BRASIER, MG_GIVRE, MG_SACRE, MG_METEORE, MG_NEANT, MG_POISON, MG_TERRE, MG_EAU, MG_VENT, MG_NEUTRE, MG_SOIN, MAGIE_N };
/* Noms des éléments à l'écran (bestiaire, fiche d'une arme : ELEMENTS_FR de l'émulateur) ; élément de l'arme du membre k
   (MG_*, -1 : aucun ; battle.c) */
extern const char *const ELEM_NOM[MAGIE_N];
int arme_element(int k);
#define FX_N (FX_MAGIE + 3 * MAGIE_N)
#define FX_SORT(m, n) (FX_MAGIE + (m) * 3 + (n) - 1)
#define FX_ELEM(fx) (((fx) - FX_MAGIE) / 3)
#define FX_NIV(fx) (((fx) - FX_MAGIE) % 3 + 1)
#define MG_TS(n) ((n) == 1 ? 0.8f : (n) == 2 ? 1.0f : 1.2f)
#define MG_KS(n) ((n) == 1 ? 0.75f : (n) == 2 ? 1.0f : 1.3f)
#define MG_VS(n) ((n) == 1 ? 0.55f : (n) == 2 ? 1.0f : 1.15f)
#define MG_AS(n) ((n) == 1 ? 0.5f : (n) == 2 ? 1.0f : 1.4f)
uint32_t fx_couleur(int fx);   /* couleur d'un sort (0 : effet d'arme), quand une technique le porte (battle.c) */
enum { BP_INTRO, BP_CMD, BP_TECH, BP_ITEM, BP_TARGET, BP_ENEMY, BP_ANIM, BP_DYING, BP_WIN, BP_LOSE, BP_MAGIE, BP_CUTIN, BP_FUI, BP_ALLIE };   /* BP_ALLIE : à quel camarade donner l'objet choisi */
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
/* cible1 : le membre soigné + 1 (objet donné à un camarade ; 0 : le plus blessé) ; rend_pm : PM rendus (objet) */
typedef struct { const char *name; int mp, heal, item; float mult, delay; int fx, sort, soin, cible1, rend_pm, degats, reaction; } bt_act_t;   /* cible1 : l'unité + 1 (0 : selon l'action) ; degats : objet lancé ; reaction : soin donné à un ennemi (2 : l'eau claire) */
#define BT_POPUPS 12
/* Ennemi d'un combat (groupe : BT.en) : personnage de la bibliothèque, rang dans E.w.enemies (ek), palette (PVAR), PV,
   ATQ, VIT ; flash : blanc quand il est touché ; mort_t : début de sa chute (E.clock, à la fin de l'action qui l'a
   abattu ; 0 debout ou pas encore tombé), caché ENNEMI_CHUTE s après ; part : sa part de la récompense (1 seul ; groupe :
   GROUPE_XP selon ses PV) ; sbire : escorte d'un boss */
typedef struct { int perso, ek, var, hp, max, atk, spd, sbire; float flash, mort_t, part; char name[24]; uint16_t faible, resiste; } bt_en_t;
#define ENNEMI_CHUTE 1.2f
#define GROUPE_PARTS 0           /* battle_start_groupe : groupe à parts égales (rencontres, patrouilles, « combat A, B ») */
#define GROUPE_SBIRES 1          /* le premier est un boss, les autres ses sbires (« combat BOSS avec S x2 ») */
typedef struct {
    int on;                      /* combat en cours */
    int phase; float t0;         /* phase (BP_*) et son début (E.clock) */
    int actor;                   /* unité dont c'est le tour */
    float ct[BT_UNITS];          /* timeline CTB : la plus petite valeur joue, puis avance de 100 / VIT × rang */
    float off[NMEMB], anim[NMEMB]; int frame[NMEMB], defending[NMEMB];   /* élan vers l'ennemi, marche, défense (par membre) */
    /* ennemis : le groupe (1 à BT_EMAX, le premier est le chef : boss, ennemi tiré d'une rencontre), niveau du combat,
       dessinés (0 après la victoire), ennemi visé par l'équipe (toucher un ennemi le vise ; abattu : le plus faible),
       boss et sbires, patrouille qui a engagé le combat (PATR, -1 aucune) */
    bt_en_t en[BT_EMAX]; int nen, lvl, visible, cible, sbires, patr;
    bt_act_t pending; int locked;    /* action en attente de sa cible ; cible verrouillée (deuxième toucher : action) */
    /* action en cours ; fx : effet (FX_*, -1 aucun), col : sa couleur (RGB ou BT_C1), k : taille (technique : 1,4),
       seed : tirage fixe des particules, cue : prochain son de l'effet, scue : prochaine secousse, melee : élan, cast :
       animation de technique */
    /* an : action en cours ; melee : élan et frappe au contact, cast : technique (sort, piratage, objet), tir : à distance
       (ni élan ni frappe, recul au coup ; battle.c, lancer) */
    struct { int who, hit, melee, cast, tir, all, ntarget, targets[4]; bt_act_t act; float t0; int fx, seed, cue, scue; uint32_t col; float k; float elan, retour, off0; } an;
    struct { char text[40], sub[112]; uint32_t color; float t0; int hold, on; } ban;         /* bannière */
    int boss;                    /* combat du scénario : récompense × BOSS_RW, pas de fuite */
    int police;                  /* combat contre les forces de l'ordre (police.c : police_combat) : pas de fuite ; perdu : ni
                                    écran de défaite ni RÉESSAYER, l'arrestation (police_apres_combat) */
    int arene;                   /* combat de l'arène (arene.c) : rang + 1, 0 aucun ; gains × ARN_RW, pas de fuite ; perdu : ni
                                    écran de défaite ni RÉESSAYER (arene_apres_combat) */
    int fuites;                  /* essais de fuite ratés dans ce combat (chacun rend la suivante plus sûre) */
    int rw_xp, rw_cr, rw_loot;   /* récompense de la victoire (gagnée en revenant au monde) ; rw_loot : objet, -1 aucun */
    int rw_nloot;                /* nombre de rw_loot (un cœur par machine du groupe, selon sa part) */
    int rw_eq;                   /* équipement lâché (objet du sac, EQPL : drop_equip), -1 aucun */
    int rw_eq2;                  /* un second équipement lâché par un autre ennemi du groupe, -1 aucun */
    int rw_soin;                 /* soin lâché (DROP_SOIN : soin_butin), -1 aucun */
    int rw_noyau;                /* noyau élémentaire lâché (NOYAU_* : noyau_butin), -1 aucun */
    /* magie apprise par la victoire (tableau de draw_victoire ; apprentissage) : membre, sort, points après le combat, points
       à réunir (0 : sort que son job lui apprend au niveau gagné) */
    struct { uint8_t m, s, pts, max; } rw_app[RW_APP_MAX]; int rw_napp;
    struct { char text[24]; uint32_t color; float t0; int unit, dx, on; } pop[BT_POPUPS];   /* nombres flottants */
    /* cut-in : image tirée au choix de la technique tech (lue d'avance), la précédente, début ; action qui suit */
    struct { int img, last, plein, paire, reac, special; const char *tech; float t0; bt_act_t act; } cut;   /* special : 1 ÉVEIL, 2 ULTIME */
    float avance[NMEMB];         /* pas en avant du membre qui choisit son action (0 à 1, PAS_ACTIF) */
    int transfo[NMEMB];          /* membres transformés (ÉVEIL) : ATQ × 1,5, dégâts reçus × 0,7, halo, ULTIME */
    /* cut-in de dégâts : degres_vus (bit 0 : la moitié, bit 1 : le quart, par membre), image tirée au coup ennemi
       (reac_img, -1 aucune), montrée à la fin de son attaque */
    uint8_t degres_vus[NMEMB]; int reac_img;
    int mpage;                   /* page du menu MAGIE, ou du menu OBJETS */
    int obj, allie;              /* BP_ALLIE : l'objet choisi, le camarade proposé */
    float shake, shake_amp, fade_t;   /* secousse restante (s), sa force (× l'écart de 4 pixels), début du fondu au noir */
    float hurt_t[BT_UNITS];      /* dernier coup reçu (E.clock), animation « touché » */
    float ret_x, ret_z;          /* position de Sera avant le combat */
} battle_t;
extern battle_t BT;
/* Un membre choisit son action (éventail, techniques, sorts, objets, cible) */
static inline int bt_choix(void) { return BT.phase == BP_CMD || BT.phase == BP_TECH || BT.phase == BP_MAGIE || BT.phase == BP_ITEM || BT.phase == BP_TARGET || BT.phase == BP_ALLIE; }
/* Élan au corps à corps (07/11/2026, retour de jeu : « elle flotte » : 0,2 s de course, puis elle glissait figée dans sa dernière pose d'attaque, à l'aller comme au retour). Maintenant : course
   jusqu'au contact, avant l'action (BT.an.t0 : l'instant du contact, d'où partent la frappe, le coup, les effets et les sons, comme avant tout élan), attaque, demi-tour et course jusqu'à sa place.
   Durées : distance / ELAN_V en mètres par seconde, bornées ; la distance est courte (un peu plus d'un mètre) : à la vraie vitesse de course (5,2), on ne verrait pas une foulée */
#define ELAN_V 3.2f
#define ELAN_MIN 0.32f
#define ELAN_MAX 0.6f
#define ELAN_CYCLE 0.54f                 /* s par cycle de l'animation de course (à pied : 8 × 0,09) */
#define ELAN_FRAPPE 0.65f                /* s entre le contact et le départ du retour */
#define PAS_ACTIF 0.6f                   /* le membre qui choisit avance d'autant (mètres) vers l'ennemi */
/* Ce pas, marché (07/11/2026, demande de l'utilisateur : il glissait, « comme l'élan, avec une marche rapide ») : à PAS_V m/s, l'image de la marche
   d'après la distance parcourue (PAS_FOULEE m par cycle ; en revenant, la marche à l'envers, face à l'ennemi) ; il reste en avant pendant son
   action (cut-in, technique, tir, sort, objet), puis revient. battle_anim de render.c, battleAnim de l'émulateur */
#define PAS_V 1.7f
#define PAS_FOULEE 0.9f
#define CUT_DUREE() (BT.cut.special == 1 ? CUT_S_TRANSFO : BT.cut.special == 2 ? CUT_S_ULTIME : BT.cut.reac ? CUT_S_DEGATS : \
                     BT.cut.plein ? CUT_S_PLEIN : BT.cut.paire ? CUT_S_PAIRE : CUT_S)
/* Lance le combat contre l'ennemi k de ENNE au niveau lvl (0 : celui de sa fiche ; PV et ATQ de la fiche × (lvl + 6) /
   (niveau de la fiche + 6), comme l'équipe) ; boss : combat du scénario, expérience et crédits × BOSS_RW (12 depuis le 03/11/2026,
   demande de l'utilisateur : les combats de l'histoire donnent l'essentiel de l'expérience, les crédits suivent ; 4 avant ;
   BOSS_RW de l'émulateur et de tools/equilibrage/combat.js) */
#define BOSS_RW 12
/* Expérience selon sa source (06/11/2026, demande de l'utilisateur : avec les gains d'ATQ et de DÉF, moins d'expérience pour coller
   à l'histoire ; le niveau de qui fait toutes les annexes montait à 91 pour un dernier boss de niveau 56) : combat d'une annexe
   (boss = 2) × BOSS_RW_ANNEXE au lieu de BOSS_RW, ses crédits restent × BOSS_RW ; rencontres, patrouilles, police, arène (boss = 0)
   × RENC_XP ; reward de battle_update, BOSS_RW_ANNEXE et RENC_XP de l'émulateur et de progression.js, à garder identiques */
#define BOSS_RW_ANNEXE 4
#define RENC_XP 0.45f
void battle_start(int k, int lvl, int boss);
/* Combat contre un groupe : ks[0..n-1] ennemis de ENNE (BT_EMAX au plus ; le premier est le chef, au niveau de sa fiche si
   lvl est 0), mode GROUPE_PARTS ou GROUPE_SBIRES (règle du partage : battle.c), vars : leurs palettes (NULL : tirées, ou
   celles de ce qu'on voit pour un combat du scénario) */
void battle_start_groupe(const int *ks, int n, int lvl, int boss, int mode, const int *vars);
int battle_vivants(void);                          /* ennemis encore debout */
void battle_enemy_pos(int k, float *x, float *z);  /* place de l'ennemi k du groupe dans l'arène (BAT_GRP de battle.c) */
void battle_vise(int i, float *x, float *z);       /* point vers lequel le membre i avance et se tourne (l'ennemi visé) */
int battle_vise_en(int i);                         /* l'ennemi visé par le membre i (BT.en) : celui que touche son action, sinon BT.cible */
int battle_enemy_at(float x, float y);             /* ennemi debout sous le doigt (écran natif), -1 : aucun */
int battle_enemy_vu(int k);                        /* l'ennemi k est dessiné (debout, ou pendant sa chute) */
void bt_groupe_nom(char *buf, int cap);            /* « GLANEUR », « GLANEUR × 3 », « PILLARDE, GLANEUR » */
void battle_update(float dt);
/* Commandes de l'éventail (BC_*, arg : technique), toucher hors des boutons (ennemi en ciblage, continuer), retour */
enum { BC_ATTACK, BC_TECHS, BC_ITEMS, BC_DEFEND, BC_FLEE, BC_TECH, BC_POTION, BC_BACK, BC_MAGIES, BC_SORT, BC_SUITE, BC_EVEIL, BC_ULTIME,
       BC_CIBLE, BC_ALLIE_OK, BC_ALLIE_SUIV };   /* BC_CIBLE : viser l'ennemi arg du groupe (-1 : le suivant) ; BC_POTION : l'objet arg de la page ; BC_ALLIE_* : à ce camarade, au suivant */
/* Sorts du membre m (index dans E.w.sorts, dans l'ordre du catalogue) : ceux de son job et ceux de son équipement
   (accessoire, arme magique), même sans le job ; renvoie leur nombre. Menu MAGIE : quatre à la fois, au-delà trois
   par page et SUITE (sorts_page : premier sort et nombre de la page BT.mpage) */
int sorts_of(int m, int *out);
/* Magie (03/11/2026 ; battle.c, à garder identique à la section « Magie » de l'émulateur) : sorts du membre m au niveau L
   (masque : job, au niveau où il les enseigne ; équipement et appris, au niveau du sort) ; cristal porté qui lui apprend encore
   un sort (1 : *s le sort, *pts ses points, *max ceux à réunir) ; nouveaux sorts entre deux niveaux, annoncés hors combat
   (hud_annonce : « experience » du scénario) ; points d'une victoire par cristal porté (CRISTAL_PT_*) */
uint64_t sorts_masque(int m, int L);
int sort_appris(int m, int s);
int cristal_de(int m, int *s, int *pts, int *max);
int sort_niv_job(int j, int s);
void sorts_annoncer(int L0);
int sorts_page(int n, int *first, int *pages);
int battle_objets(uint8_t *l);   /* objets qu'on peut donner en combat (soins du sac, PV ou PM ; pas ce qui relève) : leur nombre, leurs rangs dans l */
void battle_command(int cmd, int arg);
void battle_tap(float x, float y);
/* Écran de défaite : RÉESSAYER LE COMBAT (copie du début du combat, en mémoire), REPRENDRE (dernier point de sauvegarde),
   ÉCRAN TITRE ; battle_reessai_ok : le combat perdu peut être réessayé */
enum { DEF_REPRENDRE, DEF_TITRE, DEF_REESSAYER };
void battle_defaite(int choix);
int battle_reessai_ok(void);
/* Unités des n prochains tours (timeline CTB), renvoie leur nombre */
int battle_timeline(int n, int *out);
/* Rencontres (game.c) : ennemi tiré pour le terrain sous Sera (-1 : aucun), ou sous (x, z) */
int encounter_pick(void);
int encounter_pick_at(float x, float z);
int encounter_zone(float x, float z);   /* terrain des rencontres sous (x, z) (rang de Continent.LAND), -1 : aucun */
/* Niveau d'une rencontre ici (0 : celui de la fiche) */
int encounter_level(void);
/* Groupe d'une rencontre (game.c) : le chef k (out[0]) et ses compagnons tirés pour le terrain sous (x, z), n ennemis en
   tout (n_max au plus) ; renvoie leur nombre. encounter_taille : taille tirée selon le niveau de l'histoire et l'équipe */
int encounter_groupe(int k, float x, float z, int n, int *out);
int encounter_taille(int k, int histoire, int membres);
int sol_marchable(float x, float z);   /* case praticable à pied (sans personnage) : game.c */
/* Ennemis visibles (battle.c ; patrouilles de l'émulateur) : les rencontres à pied, groupes dessinés comme les cibles de chasse (render.c) qui rôdent, chassent Sera
   à vue et engagent le combat au contact */
#define PATR_MAX 3
#define PATR_REPOS 8.0f   /* après une fuite : secondes sans chasser */
typedef struct {
    int on, n, etat;             /* etat : 0 rôde, 1 chasse, 2 repos (après une fuite) */
    int ek[BT_EMAX], var[BT_EMAX], lvl;
    float x[BT_EMAX], z[BT_EMAX], dir[BT_EMAX];   /* le chef (0) puis les suiveurs, en file derrière lui */
    float hx, hz, tx, tz, wait, anim, repos;
    int marche, frame;
} patr_t;
extern patr_t PATR[PATR_MAX];
void patrouille_maj(float dt);
int patrouille_poser(int force);   /* (force : sans effet, gardé pour les essais) */       /* une patrouille autour de Sera (force : hors des zones dangereuses aussi) ; -1 : aucune */
#define PATR_REESSAI 2.0f   /* cases : rencontre sans place devant Sera, retentée après ce chemin (encounter_step, patrouille_poser) */

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
    int carte;               /* carte de la piste (0 la planète ; cartes de course de la carrière) */
    float xd, zd;            /* ligne de départ (X, Z… : seulement pour les pistes de la carte où est Sera, course_carte) */
    const uint8_t *SF;       /* surface de chaque point (circuits dessinés de la carrière), NULL : celle de la piste */
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
    float t_tour, meilleur;                            /* dernier passage de la ligne, meilleur tour (s ; -1 : aucun) */
    uint8_t frame, walking;
    uint8_t var;                                       /* palette (PVAR ; 0 : ses couleurs) */
    int elim;                                          /* élimination : rang de sortie (1 : le premier sorti ; 0 : en course) */
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
    int sorte, nelim;                    /* épreuve (CS_*, carrière) ; éliminés */
} course_t;
/* Épreuves (carrière) : course, contre-la-montre (seul, contre son fantôme), tour rapide (seul, le meilleur tour, essais
   jusqu'à la pause), élimination (le dernier à passer la ligne sort à chaque tour), duel (contre un rival nommé) */
enum { CS_COURSE, CS_CLM, CS_TOUR, CS_ELIM, CS_DUEL, CS_N };
void course_sorte(int s);                /* épreuve de la course suivante (prise une fois par course_start) */
/* Fantôme du contre-la-montre (course.c) : enregistré pendant la course, rejoué depuis la carte SD (carrière) ; position
   pour le rendu */
typedef struct { int on, perso, var; float x, y, z, vis; uint8_t frame, walking; } fantome_t;
const fantome_t *course_fantome(void);
int course_fantome_lire(int slot);       /* départ : le fantôme de cet emplacement (save_read) ; 0 : aucun */
int course_fantome_ecrire(int slot, int perso, int var, int classe, int circuit);   /* arrivée : le parcours du joueur ; 1 écrit */
int course_fantome_ok(void);             /* un fantôme est rejoué */
float course_fantome_temps(void);        /* temps du fantôme rejoué (s ; 0 : aucun) */
/* Texte du haut de l'interface de course selon l'épreuve (k 0 : la ligne du tour, 1 : celle de la place) ; 0 : le texte
   habituel ; *coul : 0 normal, 1 en avance (vert), 2 en retard (rouge) */
int course_bandeau(int k, char *t, int n, int *coul);
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
void course_classe(float v, float v_rivaux);   /* classe de la course suivante (carrière) : vitesse de tous, des adversaires */
void course_carte(int c);                      /* axes des pistes de la carte c dans le tampon commun (world_carte) */
void course_joueur_ia(int on);                 /* essais : le joueur piloté comme un adversaire (tour de référence) */
/* Résultat d'une course (course_end, pour la carrière) : place (255 abandon), temps, meilleur tour (s, -1 aucun), ordre
   d'arrivée (rangs dans la grille, le joueur en dernier), abandon, concurrents */
typedef struct { int place; float temps, meilleur; int ordre[COURSE_N]; int abandon, n; } course_res_t;

/* ---------- Mode course de l'écran titre : la carrière (carriere.c ; pages AP_C* de accueil.c) ---------- */
#define CARRIERES 3              /* carrières sauvegardées : emplacements CARR_SLOT0 + carrière × 2 + séquence & 1 */
#define CARR_SLOT0 8
#define CARR_CLASSES 16          /* 4 avant les catégories (CARR version 3, 03/11/2026) : celles d'OUVERTE, puis deux par catégorie */
#define CARR_CIRCUITS 16
#define CARR_COUPES 32           /* 16 avant les catégories */
#define CARR_VEH 64
#define CARR_EPR 48              /* épreuves (CARR version 2 ; 32 avant les catégories) */
#define CARR_CATS 8              /* catégories de compétition (CARR version 3) : OUVERTE (la carrière d'avant), CITADINES… */
#define CARR_FANT0 40            /* fantômes du contre-la-montre : carr_fant_slot (classes d'OUVERTE : CARR_FANT0 + circuit × 4 + classe) */
/* Classe de vitesse : nom, vitesse de tous (part de celle de leur véhicule), des adversaires (cases/s), gains, catégorie */
typedef struct { char nom[16]; float v, v_rivaux, gains; int cat; } carr_classe_t;
/* Circuit : nom (celui de sa piste), piste (PIS2), carte, tours, heure (1 = un jour), pluie (0 à 1), tour de référence par classe */
typedef struct { char nom[24]; int piste, carte, tours; float heure, pluie, ref[CARR_CLASSES]; } carr_circuit_t;
typedef struct { char nom[24]; int classe, niveau, armes, circuits[3]; } carr_coupe_t;
/* Véhicule de la carrière : personnage, donné au départ (bit 0 : de sa catégorie, bit 1 : d'OUVERTE), récompense (gagné
   en duel, pas à vendre), prix ; sa vitesse, son accélération et son adhérence en carrière (0 : les siennes, VEH2) ;
   catégorie (CARR version 3 ; 0 : OUVERTE, dont le garage a tous les véhicules) */
typedef struct { int perso, depart, recompense; uint32_t prix; float v, acc, grip; int cat; } carr_veh_t;
/* Épreuve (hors des coupes) : nom, sorte (CS_*), circuit, classe, tours, rival (PERS, -1 : aucun ; duel), niveau par défaut, armes */
typedef struct { char nom[24]; int sorte, circuit, classe, tours, rival, niveau, armes; } carr_epr_t;
/* Catégorie de compétition (CARR version 3) : nom, aide, ouverture (ouvre_cat -1 : dès le départ ; sinon quand toutes les
   coupes de la classe ouvre_classe ont le bronze), toutes (OUVERTE : le garage de tous les véhicules) */
typedef struct { char nom[16], aide[24]; int ouvre_cat, ouvre_classe, toutes; } carr_cat_t;
/* Données (CARR) : barèmes, classes, circuits, coupes, véhicules ; version 2 : épreuves, améliorations du garage,
   difficultés, défi du jour ; version 3 : catégories (une seule, OUVERTE, pour les versions 1 et 2) */
typedef struct {
    int ok, version, nclasses, ncircuits, ncoupes, nveh, nepr, ncats;
    int points[4], medailles[3], gains[4], prime[3]; float temps[3];
    carr_classe_t classes[CARR_CLASSES]; carr_circuit_t circuits[CARR_CIRCUITS]; carr_coupe_t coupes[CARR_COUPES]; carr_veh_t veh[CARR_VEH];
    carr_epr_t epr[CARR_EPR];
    carr_cat_t cats[CARR_CATS];
    int amelio_prix[3]; float amelio_pas[3];          /* prix des niveaux 1 à 3 ; part gagnée par niveau (vitesse, accélération, adhérence) */
    float diff_gain[3], diff_temps[3], clm_depart, defi_gain;   /* par difficulté : crédits ×, temps des médailles × ; contre-la-montre : s de départ ; défi : crédits × */
    int livree_vict;                                  /* victoires avec un véhicule par livrée débloquée */
} carr_data_t;
/* Sauvegarde d'une carrière ('ECR3' depuis les catégories, 03/11/2026, CRC-32 comme les parties ; 'ECR2' de 680 octets et
   'ECR1' de 292 octets encore lus, mis à la forme d'ECR3 : leur progression devient celle de la catégorie OUVERTE, aux
   mêmes rangs de coupes, de circuits, de classes et d'épreuves) */
typedef struct {
    uint32_t magic, seq, credits; float temps;
    uint8_t volant, coupe, course, coupe_niv;          /* direction + 1, coupe en cours (255 aucune), course suivante (0 à 2), sa difficulté */
    uint8_t places[3], cat;                            /* place à chaque course de la coupe en cours ; catégorie de la page ouverte */
    uint8_t pts[4];                                    /* points de la coupe en cours : le joueur, puis les trois adversaires */
    uint8_t classe[CARR_CATS];                         /* classe ouverte la plus haute de chaque catégorie (rang de CARRD.classes ; 255 : sa première) */
    uint8_t vehicule[CARR_CATS];                       /* véhicule choisi dans chaque catégorie (rang de CARRD.veh ; 255 aucun) */
    uint8_t garage[CARR_VEH / 8];                      /* véhicules à soi (bit i : CARRD.veh[i]) */
    uint8_t coupe_med[CARR_COUPES], coupe_pts[CARR_COUPES];   /* meilleure médaille (0 aucune, 1 bronze, 2 argent, 3 or), meilleurs points */
    struct { uint16_t tour[CARR_CLASSES]; uint8_t med[CARR_CLASSES]; } circuits[CARR_CIRCUITS];   /* meilleur tour (1/100 s), médaille de temps */
    struct { uint16_t rec; uint8_t med, niv; } epr[CARR_EPR];   /* record (1/100 s : temps, meilleur tour ; place), meilleure médaille, sa difficulté */
    uint8_t amelio[CARR_VEH];                          /* rang de CARRD.veh : niveaux d'amélioration (bits 0-1 vitesse, 2-3 accélération, 4-5 adhérence) */
    uint8_t livrees[CARR_VEH];                         /* palettes débloquées (bit k : palette PVAR k) */
    uint8_t couleur[CARR_VEH];                         /* palette choisie */
    uint8_t victoires[CARR_VEH];                       /* victoires avec ce véhicule (livrées) */
    uint16_t defi_jour; uint8_t defi_med, duels;       /* défi du jour réussi (jour : mois × 32 + jour), sa médaille ; duels gagnés (bits) */
    uint32_t crc;
} carr_save_t;
extern carr_data_t CARRD;
/* Résultat de la dernière course (page des résultats) ; fin d'une coupe (sa page) */
typedef struct { int ok, abandon, place, points, credits, med_t, riv_pts[COURSE_N - 1], sorte, med, record, bonus, recomp; float temps, meilleur, ref; } carr_res_t;
typedef struct { int coupe, med, prime, pts[4]; char ouvre[48]; } carr_fin_t;
/* Défi du jour : épreuve tirée d'après la date (jour : mois × 32 + jour ; -1 : date inconnue) */
typedef struct { int jour, sorte, circuit, classe, niveau, cat; } carr_defi_t;
/* Pages du mode course dans l'ouverture (accueil.c) */
enum { AP_CMENU = 100, AP_CCARR, AP_CCOUPE, AP_CRES, AP_CFIN, AP_CGARAGE, AP_CLIBRE, AP_CCOUPES, AP_CEPRS, AP_CEPR, AP_CDEFI, AP_CVEH, AP_CRECOMP,
       AP_CREGL, AP_CCAT };   /* AP_CREGL : RÉGLAGES (02/11/2026 ; depuis la page COURSE ou la pause d'une course) ; AP_CCAT : une catégorie (03/11/2026) */
/* Réglages de la page RÉGLAGES (hud.c) : nombre de lignes, nom, valeur, aide, un cran ; réglages communs gardés
   (REGLAGES_SLOT, après le 18+) : les écrire s'ils ont changé, ils existent */
int hud_regl_n(void);
const char *hud_regl_nom(int i);
void hud_regl_valeur(int i, char *t, int n);
const char *hud_regl_aide(int i);
void hud_regl_changer(int i, int d);
void reglages_garder(void);
int reglages_communs(void);
/* Page RÉGLAGES ouverte depuis la pause d'une course (accueil.c, sans l'ouverture) : ouverte, l'ouvrir, ses entrées, son dessin */
int accueil_regl_on(void);
void accueil_regl_ouvrir(void);
void accueil_regl_update(float dt, const eng_input_t *in);
void accueil_regl_build(void);
int accueil_carriere_page(void);        /* une page du mode course est ouverte */
int hud_apercu_vehicule(int perso, int dir, int size, const uint16_t **px, const uint8_t **mk, int *w, int *h);   /* garage (hud.c) */
int hud_apercu_vehicule_v(int perso, int var, int dir, int size, const uint16_t **px, const uint8_t **mk, int *w, int *h);   /* (avec sa palette) */
void hud_joy_centre(float *x, float *y);   /* centre du joystick de conduite dans l'image (essais : rendu_test, joy:) */
void accueil_carriere(int page);        /* l'ouverture reprend la main sur cette page (fin d'une course) */
void accueil_course_partie(void);       /* une course de la carrière part : l'ouverture s'efface */
void carriere_load(const uint8_t *p, uint32_t size);   /* CARR (world.c, après PIS2) */
int carr_disponible(void);
/* Catégories (03/11/2026) : ouverte dans une carrière, sa classe la plus haute (rang de CARRD.classes), son véhicule choisi
   (rang de CARRD.veh, -1 aucun), véhicule i de la catégorie k (OUVERTE les a tous), catégorie de la page ouverte (la
   choisir donne son véhicule de départ s'il n'y en a aucun), ses coupes et ses épreuves (rangs), nombre de catégories
   ouvertes, emplacement du fantôme d'un circuit dans une classe, personnage du véhicule choisi */
int carr_cat_ouverte_de(const carr_save_t *c, int k);
int carr_cat_ouverte(int k);
int carr_cat_classe(const carr_save_t *c, int k);
int carr_cat_veh(int k);
int carr_veh_dans(int i, int k);
int carr_cat(void);
void carr_cat_choisir(int k);
int carr_cat_coupes(int k, int *out);
int carr_cat_eprs(int k, int *out);
int carr_cats_ouvertes(const carr_save_t *c);
int carr_fant_slot(int circuit, int classe);
int carr_veh_perso(int k);
carr_save_t *carr_cur(void);

int carr_k(void);
const carr_res_t *carr_res(void);
const carr_fin_t *carr_fin(void);
int carr_course_circuit(void);
const int *carr_course_rivaux(void);
int carr_ecrire(void);
void carr_infos_lire(void);
const carr_save_t *carr_info(int k);
int carr_possede(int p);
void carr_ouvrir(int k);
int carr_medailles(const carr_save_t *c);
int carr_ouverte_de(const carr_save_t *c, int k);
int carr_ouverte(int k);
int carr_garage_liste(int *out);
void carr_rivaux(int base, int joueur, int *out);
int carr_circuits_libres(int *out);
int carr_vehicules_libres(int *out);
void carr_coupe_commencer(int c);
int carr_garage_choisir(int i);
int carr_lancer_coupe(void);
int carr_lancer_libre(int ci, int perso, int niv, int tours);
int carr_relancer(void);
int carr_essai_ref(int ci, int classe);   /* essais : tour de référence du circuit ci dans la classe */
/* Étape 2 : épreuves, défi du jour, garage (améliorations, livrées), récompense d'un duel, recommencer en pause */
int carr_epr_ouverte(int k);
int carr_lancer_epr(int k, int niv);
const carr_defi_t *carr_defi(void);      /* défi du jour de la carrière en cours (jour -1 : date inconnue) */
int carr_lancer_defi(void);
int carr_lancer_libre_s(int ci, int perso, int niv, int tours, int sorte);
int carr_veh_rang(int perso);            /* rang dans CARRD.veh (-1 : pas un véhicule de la carrière) */
int carr_amelio(int i, int stat);        /* niveau (0 à 3) de l'amélioration stat (0 vitesse, 1 accélération, 2 adhérence) du véhicule i */
int carr_amelio_acheter(int i, int stat);   /* 1 acheté, 0 trop cher ou au plus haut */
int carr_couleurs(int i, int *out);      /* palettes débloquées du véhicule i (0 : ses couleurs) */
void carr_couleur(int i, int var);
float carr_veh_stat(int i, int stat);    /* vitesse, accélération, adhérence en carrière, améliorations comprises */
int carr_recompense(int vehicule);       /* duel gagné : 1 son véhicule, 0 les livrées du sien */
const char *carr_sorte_nom(int s);
int carr_recommencer(void);              /* pause : la course en cours repart (rien ne compte) */
int carr_terminer(void);                 /* pause du tour rapide : terminer garde le meilleur tour */
int carr_sorte_course(void);             /* épreuve de la course en cours (CS_*) */
int eng_date_jour(void);                 /* hud.c : date de la montre (mois × 32 + jour), -1 inconnue */
int carr_valider(void);
void carr_quitter(void);
void carr_page_menu(void);
int carriere_on(void);           /* mode course actif (pages ou course de la carrière ou libre) : ni histoire ni sauvegarde de partie */
int carriere_mode(void);         /* 0 aucun, 1 course de carrière, 2 course libre */
void carriere_temps(float dt);   /* temps de jeu de la carrière (pages, courses) */
void carriere_course_finie(const course_res_t *r);   /* course_end : résultats, records, page des résultats */

/* Scénario (script.c) */
void script_load(const uint8_t *p, uint32_t len);
void script_update(float dt);
int script_talk(int target);
int script_running(void);
int script_lire(uint32_t off, void *dst, uint32_t len);   /* octets du scénario (carte SD ou section SCEN) ; 0 : lus */
int script_ev_ok(int k);                /* l'événement k peut-il partir (une fois, condition, 18+, étape de sa quête) */
int script_panneau(int k);              /* lance l'événement k (quête d'affichage choisie sur un panneau) ; 1 : parti */
int script_ev_combat(int k);   /* l'événement k contient un combat (BATTLE, BATTLE2) : essais */
int script_image_seule(void); void script_image_passer(void);   /* illustration nouvelle montrée seule (script.c) */
void script_after_load(void);
/* Échos à chercher (06/11/2026, js/echos.js de l'atelier : des événements de zone marqués echo, section ECHS pour le JOURNAL) :
   l'Écho pas encore entendu le plus proche sur cette carte, à moins de ECHO_R cases (0 : aucun) ; pret : il parle maintenant (la
   nuit, la pluie) ; nuit : il attend la nuit ; texte de sa voix ; entendu */
#define ECHO_R 32.0f
int script_echo_proche(float *x, float *z, float *d, int *pret, int *nuit);
int script_ev_texte(int k, char *txt, int n);
int script_ev_fait(int k);
void script_repartir(void);   /* nouvelle partie : la machine et la mise en scène à zéro, l'événement du début à jouer */
void hud_depart_garder(void);   /* fin de eng_init : l'état du départ de toute nouvelle partie */
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
    int8_t cote;            /* marche : côté par lequel il contourne l'obstacle */
    uint8_t place_att;      /* posé là où le monde n'était pas chargé : sa place libre reste à trouver */
    float marche_t, marche_lim;   /* marche : temps écoulé, temps au-delà duquel il s'arrête où il est */
} acteur_t;
typedef struct {
    acteur_t act[ACT_MAX];
    int cam_on, cam_retour;
    float cam_t, cam_d, cam_a[5], cam_b[5], cam[5], yaw0, zoom0;   /* cam : x, z, lacet, zoom, hauteur visée au-dessus du sol */
    int coupe, cam_auto;     /* coupe : 1 vers le noir, 2 retour (grand déplacement : CAM_COUPE) ; cam_auto : plan demandé par le dialogue (eng_dlg_hors) */
    float coupe_f;           /* fondu d'avant la coupe */
    int sera_on;
    float sera_tx, sera_tz, sera_v, sera_mt, sera_lim;
    int sera_cote;
    float fondu, fondu_a, fondu_b, fondu_t, fondu_d;
    float sec_t, sec_f;
    int fuite_on, fuite_ok;
    float fuite_x, fuite_z, fuite_r, fuite_t;
} mise_t;
extern mise_t MISE;
/* Scènes (07/11/2026, audit de toutes les scènes, demande de l'utilisateur ; engine/README.md, « Scènes : caméra, distances, regards, obstacles ») */
/* Caméra plus souple et poses (07/11/2026, demande de l'utilisateur : « il faut que les mouvements de caméra soient plus souples et il
   faut marquer des poses sur ce que l'on montre, sinon on ne comprend rien ») : vitesses moyennes plus basses (8 cases/s et 70°/s avant),
   un mouvement dure au moins CAM_MIN_D, part et s'arrête en douceur (courbe du cinquième degré, plus de vitesse à l'arrivée) ; arrivé,
   le plan est tenu CAM_POSE avant que la scène continue (OP_CAMERA) */
#define CAM_V 3.5f          /* cases par seconde en moyenne (le double au milieu du trajet) */
#define CAM_A 0.6f          /* radians par seconde en moyenne (environ 35°/s) */
#define CAM_MIN_D 0.9f      /* s : un mouvement de caméra n'est jamais plus court */
#define CAM_POSE 0.8f       /* s : le plan tenu à l'arrivée, avant la suite de la scène */
#define CAM_COUPE 18.0f     /* cases : au-delà, un fondu au noir plutôt qu'un travelling (30 avant : à cette allure, 5 s de trajet) */
#define CAM_COUPE_A 2.1f    /* radians (120°) : au-delà aussi */
#define COUPE_T 0.3f        /* s pour aller au noir, autant pour en revenir */
#define FIN_CAM 1.2f        /* s du retour vers Sera à la fin d'une scène */
#define DLG_LOIN 3.0f       /* cases : plus loin, Sera va vers celui qui parle */
#define DLG_PROCHE 1.8f     /* cases : elle s'arrête à cette distance */
#define DLG_COURT 4.0f      /* cases : au-delà, elle y court */
#define DLG_MILIEU 8.0f     /* cases : plus près, la caméra du dialogue vise le milieu des deux */
#define SCENE_R 0.45f       /* cases : deux personnages ne se croisent pas plus près */
#define SCENE_POSE_R 0.85f  /* cases : ni ne se posent plus près (à l'écran, leurs sprites se recouvriraient) */
#define DLG_SERA_LOIN 8.0f  /* cases : Sera qui parle ne va vers l'autre que s'il est à moins de ça (plus loin, elle parle toute seule) */
#define CAM_COUPE_AUTO 12.0f   /* cases : un plan demandé par le dialogue, plus loin que ça, passe par le noir */
#define CAM_COUPE_RETOUR 16.0f /* cases : le retour vers Sera aussi */
int point_dans_bat(float x, float y, float z);   /* dans la boîte d'un bâtiment ; 0 aussi si le monde n'est pas chargé */
int scene_approche(const char *who);    /* réplique de who : Sera va vers lui s'il est loin ; 1 : elle y va, le script attend (OP_SAY) */
void scene_cam_sortir(void);            /* une caméra qui suit Sera et serait dans un bâtiment en sort (début de scène, réplique) */
void scene_regards(const char *who);    /* réplique de who : chacun se tourne vers qui il faut, la caméra sort d'un bâtiment */
void mise_cam_stop(void);               /* caméra de scène coupée net (script interrompu) */
void mise_marcher_sera(float x, float z, float v);
void mise_marcher_acteur(int k, float x, float z, float v);
void mise_update(float dt);
int mise_bloque(void);                   /* caméra de script ou marche de Sera : le joueur ne la dirige pas */
float cam_pas(float yaw);                /* lacet au multiple de 45° le plus proche (8 directions des planches) */
#define CAM_PAS_VIF 8.0f                 /* pas magnétiques : vitesse du glissement de la caméra lâchée, par seconde */
int hud_camera_tourne(void);             /* hud.c : le doigt fait tourner la caméra (bord de l'écran) */
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
#define POL_LOIN1 28.0f           /* (25 avant le 01/11/2026 ; un anneau de 18 à 28 n'avait aucune rue au secteur 6) */
#define POL_VUE_R (ENG_DYN ? 38.0f : 19.0f)   /* cases : rayon de vue du rendu (VIEW_R de render.c) */
enum { POL_VOL = 1, POL_EFFRACTION = 2 };
/* Phases de l'arrestation, puis de la prison : cellule (carton du temps), garde qui vient ouvrir, ses répliques, sortie */
enum { ARR_AUCUNE, ARR_HALTE, ARR_ENCADREE, ARR_DIALOGUE, ARR_FONDU, ARR_CELLULE, ARR_GARDE, ARR_PAROLES, ARR_SORTIE, ARR_RETOUR };
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
    int prison, nuits, garde; float cel[6];   /* prison (01/11/2026) : bâtiment (bats, -1 : aucun), nuits passées, garde (E.npcs, -1), cellule dans le monde (rectangle, entrée du niveau) */
    char lp[5][128];                          /* répliques du garde (une boîte : 120 caractères au plus, plus les accents en UTF-8) */
    float repit;                              /* combat gagné : secondes sans renfort ni prise (police_apres_combat) */
    float alerte;                             /* vol : secondes avant que la police sache où est le véhicule (POL_ALERTE_S) */
    int vaincue;                              /* l'arrestation suit un combat perdu contre les agents */
    int dehors;                               /* prison quittée hors de l'enceinte (pas de passage) : devant la porte de la ville */
    /* Interception à l'enceinte (enceinte_maj) : phase ENC_*, son temps, enceinte (ENCE), véhicule volé confisqué + 1 */
    int enc, enc_k, enc_conf; float enc_t;
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
void police_scenario(int n);                          /* instruction « police N » : recherchée au moins au niveau n */
void police_arreter(int motif, int amende, int clan); /* scène de l'arrestation (motif, amende : ceux d'une alarme non semée) */
void police_descendre(int k);           /* la voiture de police k du trafic s'est garée près de Sera à pied : ses agents descendent */
/* game.c : voiture de police poursuivante ajoutée au trafic (0 : pas de place), véhicule ajouté k retiré du monde
   (confisqué), Sera sortie du véhicule conduit, case praticable à pied (sans les personnages) */
int traffic_poursuivant(int perso);
void vehicule_retirer(int k);
void vehicule_sortir(void);
int sol_pieton(float x, float z);
/* interieur.c : prison la plus proche de (x, z) (bâtiment visitable de la planète qui a des cellules, de l'autorité clan ou
   de l'Ordre d'abord ; -1 : aucune) ; Sera mise dans sa cellule (cel : son rectangle dans le monde, puis l'entrée du
   niveau ; 0 : intérieur illisible) ; un exemplaire de plus dans l'intérieur (-1 : pas de place) ; sortie par la porte
   principale */
int int_prison(float x, float z, int clan);
int int_cellule(int bat, float *cel);
int int_pnj_ajouter(int perso, float x, float z, float dir);
void int_liberer(void);
int police_niveau_vol(int perso);       /* niveau de recherche qu'apporterait le vol d'un véhicule perso du trafic (0 : aucun) */
/* Combat contre les forces de l'ordre (01/11/2026) : fin du combat (battle_end) : gagné, Sera s'échappe ; perdu, l'arrestation
   puis la prison. Enceintes (ENCE, lues par police_load) : enceinte interdite où est (x, z) (pas de passage ; -1 : aucune) */
void police_apres_combat(int won);
void police_load(void);
int enceinte_interdite(float x, float z);

/* ---------- Conduite assistée et pilote automatique (pilote.c ; section « Pilote » de l'émulateur, à garder identiques) ---------- */
typedef struct {
    int on;                      /* pilote automatique : 0 coupé, 1 en route, 2 arrivé (il freine, puis se coupe) */
    float x, z, r;               /* destination, rayon d'arrivée (cases) */
    int guide;                   /* lieu du GUIDAGE visé (-1 : point de la carte du menu) */
    char nom[32];
    float reste;                 /* distance restante par le réseau (cases) */
    float bloque_t, recul_t; int reculs;   /* blocage : temps presque à l'arrêt, marche arrière en cours (s), essais */
    float ax, az;                /* point visé (essais) */
    int assist;                  /* conduite assistée à cette image : 0 rien, 1 axe de route (ROUT), 2 rue */
    float autorite;              /* part de l'aide au maintien de voie (0 à 1 ; le joueur braque : 0) */
    float st_prec;               /* braquage de l'image d'avant (marche arrière d'un dégagement : à l'inverse) */
    int us_fin, us_fin_max;      /* durée du dernier champ fin et la plus longue (µs ; mesure) */
} pilote_t;
extern pilote_t PIL;
void pilote_init(void);                 /* fin de eng_init : mémoire des champs (sans elle, pas de pilote automatique) */
/* ride_drive (game.c) : commandes du joueur th, st corrigées (conduite assistée) ou remplacées (pilote automatique) ; top :
   vitesse maxi ici (cases/s), vmax, acc : celles du véhicule */
void pilote_conduire(float *th, float *st, float top, float vmax, float acc, float dt);
/* Pilote automatique vers (x, z) (guide : lieu du GUIDAGE, -1 : point de la carte ; nom : celui des messages) : 1 s'il
   part ; sinon 0 et la raison en message */
int pilote_lancer(float x, float z, int guide, const char *nom);
int pilote_possible(int dire);          /* le véhicule conduit peut le prendre ici (dire : la raison en message sinon) */
void pilote_arreter(const char *msg);   /* coupé (msg : message, NULL aucun) */
static inline int pilote_actif(void) { return PIL.on != 0; }
int pilote_bandeau(char *t, int n);     /* bandeau « PILOTE AUTO · 1,2 km » ; 0 : coupé */
/* Conduite au point touché (SYSTÈME › COMMANDES : POINTER) : point du sol visé par le doigt (hud.c, à chaque image tant
   qu'il est posé ; il coupe le pilote automatique) et part de l'allure (distance du doigt au véhicule à l'écran), doigt
   posé ou levé, point visé (marque de l'interface) : 1 s'il y en a un */
void pilote_doigt(float x, float z, float part);
void pilote_doigt_tenu(int tenu);
int pilote_doigt_cible(float *x, float *z);
/* Banc d'essai : grille grossière (côté, cases par case, classes sur 2 bits, distances) et fenêtre fine (origine, distances) */
int pilote_champs(int *gn, int *gs, const uint8_t **cls, const uint16_t **gd, int *fi0, int *fj0, int *fn, const uint16_t **fd);
int zone_urbaine(float x, float z);     /* game.c : (x, z) sur une zone urbaine de la planète (urbain, industriel, rue) */

/* ---------- Coffres de trésor (coffres.c ; section « Coffres » de l'émulateur, à garder identiques) ---------- */
void coffres_load(void);                /* section TRES du paquet (trésors posés dehors) */
void coffre_update(void);               /* à chaque image : coffre à portée (E.coffre) */
void coffre_ouvrir(void);               /* le coffre à portée : son butin, gardé fouillé */
int coffre_position(float *x, float *y, float *z);   /* point au-dessus du coffre à portée (bulle) ; 0 : aucun */
int coffre_palier(int bat);             /* palier du butin d'un coffre du bâtiment bat (PORT) */
const char *coffre_nom(void);           /* nom au-dessus de la bulle : la cachette (COFFRE, CAISSE, ARMOIRE) ou le trésor */
int coffre_avant(void);                 /* le coffre passe devant l'interlocuteur (OK) : personne, ou plus loin que le coffre */

/* ---------- Marques des personnages et panneaux de quêtes (panneaux.c ; section « Panneaux de quêtes » de l'émulateur,
   à garder identiques) ---------- */
void ouvertures_load(uint32_t off);     /* script.c : table des ouvertures du scénario (en-tête SCN5 de 60 octets ; 0 : aucune) */
void panneaux_load(void);               /* section PANQ, une marque par personnage, région de chaque ouverture */
int marque_de(int perso);               /* MQ_* du personnage, refaite quand l'état du scénario change */
void marque_dessin(float x, float y, int m, float k);   /* badge « ! » ou « ? » centré en (x, y), taille k */
int marques_points(float *x, float *z, uint8_t *m, int n);   /* exemplaires marqués de la carte (carte du menu) */
void marques_refaire(void);             /* banc d'essai : à refaire au prochain appel */
uint32_t marques_us(void);              /* banc d'essai : durée du dernier calcul (µs) */
int ouvertures_n(void);
void panneau_maj(void);                 /* chaque image : panneau à portée (E.panneau), guidage d'un panneau */
int panneau_n(void);
const char *panneau_nom(int k);
int panneau_lieu(int k, float *x, float *z, int *carte);
int panneau_quetes(int k);              /* quêtes ouvrables du panneau k */
int panneau_position(float *x, float *y, float *z);   /* point au-dessus du panneau à portée (bulle LIRE) */
int panneau_avant(void);                /* le panneau passe devant l'interlocuteur (OK) */
int panneau_liste(int k, int16_t *ov, float *dist, int n);   /* quêtes ouvrables du panneau k : ouvertures, distances */
int ouverture_quete(int i);
int ouverture_accroche(int i);          /* chaîne du scénario, 65535 : aucune */
int ouverture_choisir(int i);           /* guidage vers le donneur, ou l'événement d'affichage (1 : parti) */
int panneau_guide(float *x, float *z);  /* cible du guidage d'un panneau sur cette carte ; 0 : aucune */
const char *panneau_guide_nom(void);
void panneau_guide_arreter(void);
void hud_panneau_ouvrir(int k);         /* hud.c : l'écran du panneau k (SUB_PANNEAU) */
int hud_panneau_texte(char *out, int n);   /* hud.c, banc d'essai : l'écran du panneau, ses lignes */
void hud_panneau_choisir(int row);      /* hud.c, banc d'essai : la ligne row de l'écran du panneau, choisie */
int hud_marques_vues(char *out, int n); /* hud.c, banc d'essai : les badges dessinés à la dernière image */
#define MARQUES_CARTE 48                /* marques dessinées au plus sur la carte du menu */
/* hud.c : panneau du butin (titre, crédits, objets du sac et leur nombre) */
void hud_butin(const char *titre, int credits, const int *obj, const int *nb, int n);
/* Choix de l'arène (arene.c) : options, titre, deux lignes dessous ; la réponse va à arene_choix */
void hud_choice_arene(char opts[][48], int n, const char *titre, const char *l1, const char *l2);
/* ---------- Arène de combat (arene.c ; section « Arène » de l'émulateur, à garder identiques) ---------- */
#define ARN_RANGS 5
#define ARN_COMBATS 3       /* combats d'une série */
#define ARN_RW 1.5f         /* expérience et crédits de chaque combat de l'arène, fois ceux d'une rencontre de ce niveau */
#define ARN_PRIME 3         /* série gagnée : crédits de tant de combats du rang (le double la première fois) */
int arene_ici(void);                    /* Sera dans l'arène (bâtiment « Arène » de PORT) */
void arene_proposer(void);              /* COMBATTRE (commerce du maître de l'arène) : le choix du rang */
void arene_choix(int k);                /* réponse k au choix de l'arène (rang, puis combat suivant ou abandon) */
void arene_apres_combat(int won);       /* fin d'un combat de l'arène (battle_end) : la suite de la série */
const char *arene_rang_nom(int r);      /* FER, BRONZE, ARGENT, OR, CHAMPION */
int hud_butin_on(void);

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
int script_quest_niveau(int q);   /* niveau conseillé : celui de l'étape en cours (0 : aucun) */
const char *script_quest_text(int q, int st);
void script_track(int q);
/* Niveau de l'histoire en (x, z) : celui de l'étape en cours des quêtes principales (une terminée : sa dernière étape),
   relevé par une quête en cours dont l'objectif est à moins de NIV_RAYON cases ; 0 : le scénario n'en donne aucun */
#define NIV_RAYON 24
int script_niveau(float x, float z);
/* Niveau de l'histoire seul (quêtes principales, sans la quête dont l'objectif est proche) */
int script_niveau_histoire(void);
/* Niveau des combats de l'étape st de la quête q (0 : aucun) */
int script_step_niveau(int q, int st);
/* Cible de l'étape st de la quête q (cases, carte ; 0 : aucune) */
int script_step_cible(int q, int st, float *x, float *z, int *carte);
/* Guidage : l'objectif posé par le script, sinon la cible de l'étape de la quête suivie ; 1 et sa position s'il y en a */
int objective_pos(float *x, float *z);
/* Carte du menu (04/11/2026) : cibles des étapes en cours des quêtes ouvertes sur la carte où est Sera, hors de la quête
   suivie (son repère : objective_pos) ; genre : OBJ_PRINC (quête principale), OBJ_PERSO (sur une personne, que sa marque
   « ? » montre déjà) ; n au plus (objectifsCarte de l'émulateur) */
#define OBJ_PRINC 1
#define OBJ_PERSO 2
#define OBJ_CARTE 32                    /* objectifs sur la carte du menu au plus */
int script_objectifs(float *x, float *z, uint8_t *genre, int n);

/* Interface */
void hud_init(void);
/* Réplique du scénario (right : interlocuteur du portrait de droite), dialogue ouvert, message court */
void hud_say(const char *who, const char *text, const char *right);
int hud_in_dialog(void);
/* Ligne native à partir de laquelle l'interface de l'image couvre tout l'écran (panneau opaque du dialogue), 0 : aucune */
int hud_cover_y(void);
int hud_parle_pos(float *x, float *z);   /* celui qui dit la réplique : sa place (cadrage du dialogue) */
extern int eng_dlg_hors;                 /* posé par render.c : celui qui parle (pas Sera) est trop loin pour que l'image glisse jusqu'à lui ; mise_update y envoie la caméra */
extern float eng_dlg_x, eng_dlg_z;
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
void hud_choice_texte(const char *l1, const char *l2);   /* deux lignes sous le titre du choix en cours (ce qui est en jeu) */
int hud_choix_texte(char *out, int n);   /* banc d'essai : la boîte de choix en cours en texte (0 : aucune) */
int hud_tele_vus(char *out, int n);      /* banc d'essai : lieux du téléport visités par sorte, et leurs noms */
void hud_quetes_ouvrir(void);          /* banc d'essai : menu QUÊTES ouvert */
void hud_guide_ouvrir(int g);            /* banc d'essai : menu GUIDAGE ouvert (g < 0 : ses groupes, sinon le groupe g) */
int hud_guide_liste(char *out, int n);   /* banc d'essai : lieux du menu GUIDAGE de la carte où est Sera, par groupe ; leur nombre */
int hud_shop_ouvrir(int mode);           /* banc d'essai : boutique du commerce de l'intérieur (0 achat, 1 vente) ; hud_shop_texte : son état */
int hud_shop_texte(char *out, int n);
void hud_equip_ouvrir(int m, int sl);    /* banc d'essai : menu ÉQUIP. du membre m (-1 : les membres), emplacement sl (-1 : ses emplacements) */
void hud_statut_ouvrir(int m, int v);    /* banc d'essai : STATUT du membre m, vue v (0 ce qu'il porte, 1 ses sorts, -1 l'une puis l'autre) */
int hud_equip_texte(char *out, int n);   /* banc d'essai : état du menu ÉQUIP. (liste de l'emplacement et écarts, fiche) */
const char *hud_toast_txt(void);         /* banc d'essai : le dernier message */
/* Banc d'essai : s tel qu'il s'affiche (noms échangés de l'héroïne choisie, nom_echange) ; la réplique en cours telle
   qu'elle s'affiche (qui parle, texte ; NULL : aucun dialogue) */
const char *hud_texte_vu(const char *s, char *buf, int cap);
const char *hud_replique_vue(char *qui, int cap);
void hud_choice_titre(char opts[][48], int n, const char *titre);   /* choix du scénario avec un titre (sujets d'un personnage) */   /* choix d'une porte fermée (réponse : int_choix) */
void hud_lines(const char *who, const char *const *lines, int n, const char *right);   /* répliques (chaînes gardées) */

/* ---------- Intérieurs (interieur.c) ---------- */
void int_load(void);
void int_update(float dt);
void int_interagir(void);
void int_choix(int k);
int int_libre(float x, float z);
float int_sol_y(float x, float z);    /* hauteur du sol de l'intérieur en (x, z) : sols surélevés, sinon 0 */
int int_pente(float x, float z);      /* (x, z) sur une pente (escalier) */
int int_point_sortie(const int_porte_t *d, float *ox, float *oz);   /* point libre devant la porte d, à la sortie (0 : aucun) */
int int_porte_ouverte(int porte);
int int_entrer_porte(int k, int force);
void int_position(float *x, float *z, float *yaw);
void int_reset(void);
void sera_placer(float x0, float z0, float rmax);   /* sur la place libre la plus proche (game.c) */
int sera_libre(float x, float z);                   /* place libre pour son corps dehors, marges comprises (game.c) */
int int_metier_pnj(int npc);          /* métier + 1 d'un exemplaire de l'intérieur (0 : aucun, hors intérieur) */
const char *pnj_nom(int npc);         /* nom de l'exemplaire : tiré (NOMT) ou celui de son personnage */
int pvar_choix(int perso, unsigned graine);   /* palette d'un exemplaire d'après sa graine (0 : ses couleurs) */
int perso_remap(int perso, int var);  /* table de couleurs du personnage, ou de sa palette var (PVAR) */
const char *int_porte_nom(int porte); /* nom du bâtiment de la porte ("" : aucun) */
int repl_ligne(int liste, char *out, int cap);   /* une réplique générique au hasard de la liste, dans out ; 0 : aucune */
/* Répliques par zone (repliques.c, sections REPZ et REPT, 03/11/2026) */
void repl_init(void);                 /* eng_init : index, grille, tampon du plus gros lot (sans REPZ : REPL) */
void repl_maj(void);                  /* chaque image : dehors, le lot de la zone de Sera lu quand elle en change */
int repl_dire(int npc, int metier, char *out, int cap);   /* réplique de l'exemplaire (metier : + 1 dans un intérieur, 0) ; 0 : aucune */
int repl_phase(void);                 /* phase de l'histoire des répliques : 0 prologue à 6 après la Source */
int repl_zone(void);                  /* lot de la zone où est Sera (-1 : pas de REPZ) */
const char *repl_lot_nom(int k);      /* essais (rendu_test) : nom du lot k, nombre de ses répliques, état */
int repl_lot_lignes(int k);
int repl_phase_drapeau(int k);
void repl_etat(int *lots, int *lot, uint32_t *tampon, uint32_t *lus, uint32_t *lectures);
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
/* Sauvegarde automatique (hud.c) : demandes (raisons AUTO_*), écrite dès que rien n'est en cours ; avant un combat du
   scénario, tout de suite (script.c) ; point du début d'une nouvelle partie ; nouvelle partie ou partie chargée ; durée de
   la dernière écriture (µs) ; REPRENDRE après une défaite (dernier point de la partie en cours) */
enum { AUTO_CARTE = 1, AUTO_ETAPE = 2, AUTO_LIEU = 4, AUTO_TEMPS = 8 };
void hud_sauve_demande(int raison);
int hud_sauve_auto(int avant_combat);
void hud_sauve_debut(void);
void hud_sauve_repartir(void);
void hud_sauve_maj(float dt);
int hud_sauve_us(void);
void hud_reprendre(void);
void hud_nouvelle(int p);
void hud_journal_ouvrir(int niv);
/* Menu des véhicules (hud.c, 01/11/2026 ; vehiculesOuvrir de l'émulateur) : la page PILOTE du menu de course pour choisir un
   véhicule et sa couleur ; boutique 0 : offert (code CODE_VOITURE), 1 : à vendre (veh_prix, crédits de l'équipe). liste :
   les véhicules proposés (index PERS, n), NULL : tous ceux que Sera peut conduire. Point d'entrée des boutiques de
   véhicules. 0 : refusé ici (toast), ou aucun véhicule */
int hud_vehicules(int boutique, const int *liste, int n);
int hud_vehicules_open(void);
int hud_vehicules_choisir(const char *nom, int c);   /* essais : modèle dont le nom contient nom, couleur c */
void hud_code_taper(const char *code);               /* essais : le code tapé dans SYSTÈME › CODE */

/* ---------- Véhicule de Sera (garage.c, 01/11/2026 ; section « Garage » de l'émulateur, à garder identiques) ----------
   Offert (code CODE_VOITURE) ou acheté (hud_vehicules) : à elle, ni volé ni recherché ; un seul, le nouveau remplace
   l'ancien. Il reste là où elle le laisse, sur sa carte, et revient à sa place quand elle y retourne (le bloc d'exemplaires
   d'une autre carte se vide quand elle la quitte) ; gardé dans la sauvegarde (ESVG) : au chargement, à sa place, Sera au
   volant si elle y était. Acheté dans un intérieur : livré devant la porte, à la sortie */
typedef struct { uint8_t on, perso, var, carte, abord, r0, r1, r2; float x, z, dir; } garage_sv_t;   /* on : bit 0 ; bit 1 : à placer près de (x, z) */
int garage_donner(int perso, int var);   /* 1 Sera au volant, 2 livré devant la porte (intérieur), 0 pas de place */
void garage_maj(void);                   /* chaque image (game.c) : l'ancien retiré, le sien suivi, ou reposé à sa place */
void garage_reset(void);                 /* nouvelle partie : plus de véhicule */
void garage_ecrire(garage_sv_t *s);      /* sauvegarde */
void garage_lire(const garage_sv_t *s);  /* chargement (après world_carte) */
int garage_perso(void);                  /* son personnage (-1 : aucun) */
int veh_prix(int p);                     /* prix du véhicule p (crédits) : celui de la carrière (CARR), sinon d'après ses performances */
int veh_proposable(int p);               /* le véhicule p peut être offert ou vendu : une planche, présent, pas un bateau */

/* ---------- Ouverture (accueil.c) : écran titre, parties, introduction, choix de l'héroïne ---------- */
void accueil_debut(void);              /* après le chargement du monde : écran titre (monde arrêté, Sera cachée) */
void accueil_titre(void);              /* SYSTÈME › QUITTER : l'écran titre, même là où accueil_debut ouvre les applications */
void accueil_fin(void);                /* nouvelle partie tout de suite (banc d'essai) : Sera au départ */
int accueil_on(void);
int accueil_heroine_changer(void);   /* code CODE_HEROINE : la page des héroïnes en pleine partie ; 0 : pas d'autre héroïne */
float accueil_voile(void);   /* voile du monde derrière la page de l'écran titre (0 à 1 ; 1 : pas de 3D) */
int accueil_calme(void);     /* page de menu immobile : cadence réduite (eng_ecran_calme) */
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
