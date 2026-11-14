/*
 * Interface du jeu (portage de l'émulateur et des maquettes « Interface ronde Echoes of Ashes »)
 *
 * - HUD de la world map : graduations qui tournent avec la caméra, boussole, PV/PM, zone, destination
 * - Bulle d'interaction au-dessus du marchand, dialogue sur les deux tiers bas avec portraits dans les coins
 * - Menu en anneau (6 entrées) et ses écrans : objets, techniques, équipement, statut, carte, système
 * - Combat (logique dans battle.c) : timeline sur l'arc du haut, ennemi, barres de l'équipe, éventail de 5 boutons,
 *   ciblage, bannières, nombres flottants ; fondu au noir au début et à la fin
 * - Gestes : toucher le sol (Sera s'y rend), maintenir (Sera suit le doigt), glisser le long du bord (caméra,
 *   ou rotation de l'anneau et défilement des listes), double toucher sur Sera (menu)
 * - Sauvegarde : deux emplacements alternés, numéro de séquence et somme de contrôle
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
#define PM_HEX 0x9B7BFF   /* PM : violet, distinct du cyan des thèmes (combat, STATUT) */
/* Plein écran (ENG_DYN : téléphone, portrait) : l'interface prend toute l'image, UW × UH ; les écrans pas encore refaits
   pour le portrait se dessinent dans un cadre de 466 × 466 centré (cadre(1) : dessin et zones tactiles décalés). Sur
   l'écran rond, UW = UH = 466 et cadre ne fait rien. */
#if ENG_DYN
#define PORTRAIT 1
#define UW ((float)ui_w)
#define UH ((float)ui_h)
#define BANDE_H 110.0f                  /* bande du bas : glisser pour tourner la caméra */
static void portrait_world(void);
static void portrait_menu(void);
static void portrait_dialog(void);
static void portrait_choice(void);
static void portrait_course(void);        /* course en portrait (02/11/2026) : interface, menu de course, pause */
static void portrait_course_menu(void);
static void portrait_course_pause(void);
static void cadre_bouton(float x, float y, float w, float h, int sel, uint16_t col);
static int wrap_chord(const ui_font_t *f, const char *text, float y0, float lh, char lines[6][96]);
#else
#define PORTRAIT 0
#define UW ((float)ENG_SCREEN)
#define UH ((float)ENG_SCREEN)
#endif
static void aura(const screen_pos_t *p, uint32_t rgb, float t);   /* halo d'un personnage transformé (ÉVEIL) */
static void reglages_lire(void);   /* réglages communs au démarrage (mode course et histoire, 02/11/2026) */
static void draw_intro_combat(void);   /* entrée en combat (BP_INTRO) */
static void draw_victoire(void);       /* tableau de fin de combat (BP_WIN) */
static void draw_defaite(void);        /* écran de défaite (BP_LOSE) : REPRENDRE, ÉCRAN TITRE */
/* Panneau du butin d'un coffre (hud_butin ; drawButin de l'émulateur) : titre, crédits, objets et leur nombre, début */
static struct { int on, credits, n, obj[6], nb[6]; char titre[48]; float t0; } BUT ENG_FROID;
static void draw_butin(void);
static void butin_fermer(void);
/* Pictogramme du coffre (bulle ; iconeCoffre de l'émulateur), 26 px : caisse, couvercle bombé, deux ferrures, fermoir */
static void icone_coffre(float x, float y, uint16_t c, int a) {
    const float caisse[8] = { x - 12, y - 1, x + 12, y - 1, x + 12, y + 10, x - 12, y + 10 };
    const float couv[10] = { x - 12, y - 1, x - 11, y - 7, x, y - 10, x + 11, y - 7, x + 12, y - 1 };
    ui_outline(caisse, 4, 2, c, a); ui_outline(couv, 5, 2, c, a);
    ui_line(x - 7, y - 9, x - 7, y + 10, 2, c, a); ui_line(x + 7, y - 9, x + 7, y + 10, 2, c, a);
    ui_rect(x - 3, y - 4, 6, 7, c, a);
}
/* Lignes des listes des sous-menus : 4 dans la corde du cercle ; plein écran, sur toute la largeur, autant que la
   hauteur en laisse au-dessus du pied (détail et boutons) */
#if ENG_DYN
#define ROW_Y0 136.0f
#define ROW_H 72.0f
#define ROW_STEP 82.0f
#define PIED_H 250.0f
#define ROWS ((int)((UH - PIED_H - ROW_Y0) / ROW_STEP))
#else
#define ROW_Y0 156.0f
#define ROW_H 46.0f
#define ROW_STEP 52.0f
#define ROWS 4
#endif
static void cadre(int on) { if (PORTRAIT) ui_translate(on ? (UW - ENG_SCREEN) / 2 : 0, on ? (UH - ENG_SCREEN) / 2 : 0); }
/* Voile sur toute l'image : le cercle de la montre, ou tout l'écran du téléphone, marges comprises, même dessiné dans le
   cadre centré (cadre(1) : le décalage est retiré) ; voiles des sorts, éclair, cut-ins, entrée et fin de combat, fondus,
   pause et menu de course, fond des sous-menus et des menus */
static void voile_ecran(uint16_t c, int a) {
#if ENG_DYN || ENG_RECT
    ui_voile_image(c, a);   /* marges de la barre d'état et de la barre de navigation comprises ; écran rectangulaire : toute l'image */
#else
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, c, a);
#endif
}

/* ---------- Teintes (maquette « Variantes de teintes ») ---------- */
static const struct { uint32_t c1, c2; const char *name; } THEMES[5] = {
    { 0x22E4FF, 0xFF2D9B, "NÉON" }, { 0xFFB020, 0x22E4FF, "ROUILLE" }, { 0x8DFF5A, 0xFF2D9B, "TOXIQUE" },
    { 0xA77BFF, 0xFFD23F, "ULTRAVIOLET" }, { 0xC4ECFF, 0xFF6A3D, "GLACE" } };

/* ---------- Données de jeu (équipe et objets de l'émulateur ; équipements : à revoir avec le scénario) ---------- */
/* Techniques : coût en PM, soin, multiplicateur de l'attaque, retard infligé à l'ennemi (tours), soin de base, effet
   propre (0 : celui de l'arme) */
member_t PARTY[NMEMB] = {
    { "SERA", 0xFF4FB0, 3621, 3621, 310, 310, 430, 12, { "Lame-fouet", "Combinaison de ferrailleuse", "Écho d'Iris" }, { { "TIR LASER", 40, 0, 1.9f, 0, 0, 0 }, { "PIRATAGE", 60, 0, 0.5f, 1.6f, 0, 0 } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "VARN", 0xB8BDCA, 4287, 4287, 210, 210, 540, 9, { "Lame de l'Ordre", "Manteau d'inquisiteur", "Sceau de braise" }, { { "LAME-FOUDRE", 30, 0, 2.1f, 0, 0, FX_SORT(MG_FOUDRE, 2) } }, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "BORS", 0xC4442F, 4980, 4980, 180, 180, 500, 7, { "Canon à rivets", "Plastron de tôle", "Gant de levage" }, { { "OBUS", 25, 0, 2.4f, 0, 0, 0 } }, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "LYSE", 0xE8C25A, 2963, 2963, 420, 420, 380, 11, { "Arc à plasma", "Cape de cendre", "Filtre respiratoire" }, { { "SOIN", 35, 1400, 0, 0, 0, 0 }, { "FLÈCHE-PLASMA", 30, 0, 1.8f, 0, 0, 0 } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* Recrues (01/11/2026 : les héros au choix que le joueur n'incarne pas ; couleur : celle de l'aura de leurs cut-ins).
       Audit du 01/11/2026 (tools/equilibrage, membres mis à la place d'un compagnon d'origine) : chacune vaut à peu près un
       des quatre d'origine ; Jade, Onyx et Ambre (frappe, vitesse) l'emportaient de beaucoup, l'Étudiant et Azur (MAGICIEN :
       PV − 15 %, sans arme de leur job dans les paliers) étaient deux fois plus fragiles, Saule (ARTILLEUR, VIT − 2) trop lente */
    { "JADE", 0xA45CFF, 3600, 3600, 260, 260, 440, 12, { "Gants de jongleuse", "Tenue du Marché", "Torches" }, { { "JET DE TORCHES", 30, 0, 2.0f, 0, 0, FX_SORT(MG_BRASIER, 2) }, { "NUMÉRO DE FEU", 45, 0, 0.5f, 1.5f, 0, 0 } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "SAULE", 0x34E88E, 4700, 4700, 220, 220, 500, 9, { "Canon de chantier", "Exo-armure", "Sac de graines" }, { { "MORTIER", 30, 0, 2.4f, 0, 0, FX_OBUS }, { "RONCES", 35, 0, 1.4f, 1.2f, 0, FX_SORT(MG_TERRE, 2) } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "ÉTUDIANT", 0xC9B27C, 3400, 3400, 460, 460, 400, 11, { "Carnet", "Robe de novice", "Plume" }, { { "SCEAU D'ARCHIVE", 35, 0, 1.9f, 0, 0, FX_SORT(MG_NEUTRE, 2) }, { "LECTURE", 40, 0, 0.4f, 1.6f, 0, 0 } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "ONYX", 0xFF2E4A, 3800, 3800, 220, 220, 460, 10, { "Arbalète de la porte", "Cuirasse de sergente", "Registre" }, { { "CARREAU PERÇANT", 30, 0, 2.1f, 0, 0, FX_FLECHE }, { "TIR DE BARRAGE", 40, 0, 0.6f, 1.4f, 0, FX_TIR } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "AZUR", 0x46E4FF, 3400, 3400, 440, 440, 410, 11, { "Gants de lignarde", "Combinaison isolante", "Bobine" }, { { "ORAGE RETENU", 40, 0, 2.2f, 0, 0, FX_SORT(MG_FOUDRE, 2) }, { "COURT-CIRCUIT", 35, 0, 0.6f, 1.5f, 0, FX_SORT(MG_FOUDRE, 1) } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* AMBRE (dixième membre) : l'armurière de la tour, INQUISITEUR (lame et armure lourde), qui gravait sur chaque lame bénie
       le défaut de la cuirasse des machines (tools/histoire/compagnons/ambre du dépôt) */
    { "AMBRE", 0xFF8A2A, 4300, 4300, 210, 210, 480, 9, { "Lame bénie", "Armure d'or et d'argent", "Marteau de forge" }, { { "DÉFAUT DE CUIRASSE", 30, 0, 2.2f, 0, 0, FX_TAILLE }, { "GERBE D'ÉTINCELLES", 40, 0, 1.2f, 1.3f, 0, FX_SORT(MG_BRASIER, 1) } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
/* Objets d'origine ; ceux de l'Atelier (section SCEN) les remplacent au chargement du scénario (script.c) */
static const item_t ITEMS_ORIGINE[] = {
    { "POTION", "Rend 1 500 PV à un allié", 3, 1500, 0, 0, 60, 0, 0, 0, NULL, 0, 0, 0 },
    { "ÉTHER", "Rend 60 PM à un allié", 2, 0, 60, 0, 150, 0, 0, 0, NULL, 0, 0, 0 },
    { "ANTIDOTE", "Soigne l'empoisonnement", 4, 0, 0, 3, 40, 0, 0, 0, NULL, 0, 0, 0 },
    { "PIÈCE DE HERSE", "Ferraille de machine, à revendre", 5, 0, 0, 2, 40, 0, 0, 0, NULL, 0, 0, 0 },
    { "CRISTAL DE MACHINE", "Froid et muet. L'Ordre l'achète", 0, 0, 0, 2, 200, 1, 0, 0, NULL, 0, 0, 0 },
    { "CRISTAL DE TRAQUEUSE", "Encore chaud. Il murmure", 0, 0, 0, 1, 0, 0, 0, 0, NULL, 0, 0, 0 },
};
item_t ITEMS[MAX_ITEMS] ENG_FROID;   /* en PSRAM sur l'ESP32 : rempli par items_init, puis par le scénario */
int n_items;
void items_init(void) {
    n_items = (int)(sizeof ITEMS_ORIGINE / sizeof ITEMS_ORIGINE[0]);
    memcpy(ITEMS, ITEMS_ORIGINE, sizeof ITEMS_ORIGINE);
}
int item_potion(void) { for (int i = 0; i < n_items; i++) if (ITEMS[i].type == 0 && ITEMS[i].hp > 0) return i; return -1; }
int item_loot(void) { for (int i = 0; i < n_items; i++) if (ITEMS[i].butin) return i; return -1; }
#define NMENU 10
static const char *MENU_NAME[NMENU] = { "OBJETS", "TECHNIQ.", "ÉQUIP.", "STATUT", "CARTE", "SYSTÈME", "QUÊTES", "GUIDAGE", "ÉQUIPE", "JOURNAL" };
static const char *MENU_ICON[NMENU] = { "obj", "tech", "equip", "stat", "map", "sys", "quest", "guide", "team", "jour" };
/* Angle de l'entrée k de la roue, depuis le haut, dans le sens des aiguilles : place fixe (draw_menu) */
static float menu_angle(int k) { return k / (float)NMENU * 2 * PI_F; }
/* Entrée de la roue la plus proche de l'angle a (atan2 depuis le haut) : celle que montre le doigt sur le bord */
static int menu_a(float a) { const int k = (int)lroundf(a / (2 * PI_F) * NMENU) % NMENU; return k < 0 ? k + NMENU : k; }
#define MENU_JOURNAL 9   /* entrée de la roue, en-tête du sous-menu (SUB_JOURNAL : les rangs de la roue ne vont pas au-delà de ÉQUIPE) */
/* SUB_EQUIPE : les compagnons au combat (menu ÉQUIPE, sous-menu 12 de l'émulateur) */
enum { SUB_ITEMS, SUB_TECH, SUB_EQUIP, SUB_STATUS, SUB_MAP, SUB_SYSTEM, SUB_QUESTS, SUB_GUIDE, SUB_EQUIPE, SUB_SHOP, SUB_TELE, SUB_GALERIE, SUB_CODE };
#define SUB_JOURNAL 21   /* JOURNAL (06/11/2026) : Échos entendus, bestiaire ; sous-menu 14 de l'émulateur */
#define MENU_SUB(k) ((k) == MENU_JOURNAL ? SUB_JOURNAL : (k))
#define SUB_PANNEAU 20   /* panneau de quêtes (panneaux.c), hors de la roue : sous-menu 20 de l'émulateur */
/* Écran d'un panneau de quêtes : le panneau, ses quêtes ouvrables (rang de l'ouverture, distance), l'accroche de la ligne
   choisie (lue une fois par ligne) */
static struct { int k, n, acc_l; int16_t o[PAN_LISTE]; float d[PAN_LISTE]; char acc[96]; } PN ENG_FROID;
/* Lignes du menu SYSTÈME après DIRECTION (13) : CONDUITE (01/11/2026), COMMANDES (01/11/2026), CODE, ÉVEIL (mode d'essai),
   QUITTER (05/11/2026 : l'écran titre, à la place de MONTRE / ACCUEIL), HEURE (05/11/2026) ; SYS_PLAT + k : réglage k de la
   plateforme montré en jeu. Numéros logiques : l'ordre affiché est celui de sys_ligne (SYS_ORDRE) */
enum { SYS_CONDUITE = 14, SYS_COMMANDES, SYS_CODE, SYS_EVEIL, SYS_QUITTER, SYS_HEURE, SYS_PLAT = 32 };
static void sys_revenir(int code);   /* SUB_CODE : code du contenu 18+ (édition unique) */   /* SUB_SHOP : boutique d'un intérieur, SUB_TELE : téléport (SYSTÈME), hors de la roue */

/* ---------- État ---------- */
static struct {
    float now;
    int menu, sel, sub, sub_sel, sub_scroll;
    int gcat;                    /* menu GUIDAGE : -1 les groupes, GR_FAM + g le groupe g (villes, lieux : leurs lieux ; famille : ses sortes), GR_CAT + k les bâtiments de la sorte du lieu k */
    int gfam;                    /* groupe de la sorte ouverte (retour) */
    int jniv, jvie;              /* JOURNAL : 0 l'accueil, 1 les vies, 2 les voix d'une vie (jvie), 3 le bestiaire */
    int tcat;                    /* téléport : -1 les sortes, sinon la sorte (0 mondes, 1 villes, 2 lieux) */
    int galcat, galgrand;        /* galerie : -1 les catégories, sinon la catégorie (GAL_*) ; image de la ligne en grand */
    char code[5];                /* code 18+ en cours de saisie (SUB_CODE) */
    int dialog, line, apres; float line_t0;   /* apres : le commerce s'ouvre à la fin de la réplique */
    int psheet;                  /* planche du portrait de Sera (sa tenue) : refait quand elle change */
    int bip_k, menu_sig;         /* sons : dernier bip du texte qui défile, état du menu à l'image précédente */
    /* doigt */
    int down, rim, hold, walking; float x0, y0, cx, cy, ang, acc, moved, t0;
    int pinch; float pinch_d0, pinch_z0;   /* deux doigts : écart et zoom au début du geste */
    int list; float vy, move_t, fling;     /* liste glissée au doigt : vitesse, dernier mouvement, élan après (px/s) */
    float shown_zoom;                      /* dernier zoom annoncé à l'écran */
    float last_tap_t; int last_tap_sera;
    float fx_x, fx_y, fx_t; int rim_on; float rim_a;
    int vol_on; float vol_t;               /* menu Système : cadran du son (tourner le bord), dernier pas */
    int vol_canal;                         /* canal réglé par le cadran : 0 le volume général, 1 + CANAL_* (mixage) */
    char toast[80]; float toast_t;
    /* annonces de quête en attente (la première s'affiche) : genre, titre, objectif, début (-1 : pas encore montrée) */
    struct { char kind[64], title[64], text[96]; float t0; } ann[4]; int nann;
    uint32_t prev_keys;
    int seq, bright;
    /* combat au clavier : bouton choisi de l'éventail (place 0..4), boutons présents à l'image précédente */
    int bsel, bkbd, bslot_cmd[5], bphase, bactor;
} H ENG_FROID;

/* Zones tactiles construites à l'image précédente */
enum { A_TALK, A_MENU_ITEM, A_OPEN, A_ROW, A_BACK, A_BATTLE, A_COURSE, A_RACE_CHG, A_RACE_GO, A_RACE_BACK, A_RACE_PAGE, A_FIRE, A_CHOICE, A_PAUSE, A_REPRENDRE, A_ABANDON, A_RECOMMENCER, A_PORTE,
       A_MENU_BTN, A_MENU_GO, A_VALIDER, A_CODE, A_COFFRE, A_BUTIN, A_DEFAITE, A_CARTE, A_EQUIPER, A_PAUSE_SEL, A_PAUSE_GO, A_PAUSE_REGL, A_SHOP_MEMBRE, A_PANNEAU, A_DORMIR };   /* DORMIR : la couche d'un abri de l'île (ile.c) */   /* SHOP_MEMBRE : fiche de la boutique, un autre membre */   /* PAUSE_REGL : RÉGLAGES de la pause de course */   /* PAUSE_SEL, PAUSE_GO : pause de course du plein écran (choisir une ligne, la toucher encore ; VALIDER) */   /* EQUIPER : fiche du menu ÉQUIP., 0 OUI, 1 NON (d'un toucher) */   /* CARTE : toucher sur la carte du menu, en véhicule (pilote automatique) */   /* COFFRE : fouiller le coffre à portée ; BUTIN : fermer son panneau ; DEFAITE : choix de l'écran de défaite (0 REPRENDRE, 1 ÉCRAN TITRE) */   /* VALIDER : la ligne choisie d'un sous-menu (plein écran) */   /* plein écran : bouton du menu (ouvrir, fermer), entrée du menu en grille (ouvrir d'un toucher) */
/* Choix du scénario en cours : réponses, réponse surlignée */
static struct { int on, n, sel, porte, commerce, vol, shop, tele, arene, dormir, succube; char opts[4][48], titre[40], texte[2][72]; } CHX ENG_FROID;   /* succube : se battre ou s'offrir à elle (île, ile.c) */   /* dormir : DORMIR jusqu'au matin dans un abri de l'île (ile.c) */   /* arene : choix de l'arène (arene_choix) */   /* shop : confirmation d'un achat ou d'une vente (shop_k) ; tele : téléport vers le point touché sur la carte du menu (carte_tele_aller) */   /* porte : choix d'une porte fermée (int_choix) ; commerce : ce que propose un commerçant ; vol : confirmation du vol d'un véhicule du trafic (vol_k) ; texte : lignes sous le titre (ce qui est en jeu) */
static int vol_k = -1;   /* véhicule du trafic dont le vol attend sa confirmation */
/* Commerce en cours (openCommerce de l'émulateur) : commerce (COMM), exemplaire qui le tient, action de chaque réponse
   (0 acheter, 1 vendre, 2 soins, 3 un verre, 4 partir), boutique ouverte (0 achat, 1 vente), nom du commerçant ; liste :
   ce qu'on y achète (shop_liste : objets du commerce, puis l'équipement de sa gamme, du plus haut palier au plus bas) */
/* fiche : équipement à acheter montré sur la fiche de comparaison du menu ÉQUIP. (-1 : la liste), m : rang dans la bande du
   membre comparé (03/11/2026) */
static struct { int comm, npc, act[4], mode; char nom[24]; uint8_t liste[MAX_ITEMS]; int nl, fiche, m; int forge, cible, fact[3]; } SHOP = { -1, -1, { 0 }, 0, "", { 0 }, 0, -1, 0, 0, -1, { 0 } };   /* forge : AMÉLIORER chez un ferrailleur (ferrailleur_ici) ; cible : l'objet qui reçoit un élément (SHOP.mode 4) ; fact : actions du choix de la forge */
static int shop_rows(void);
static int shop_item(int row);
static int shop_prix(int k);
static int shop_possible(int k);
static void shop_demander(int k);
static void shop_fiche(int k);
static void shop_fiche_choix(int non);
static void shop_membre(int pas);
typedef struct { float x, y, r, w, h; int rect, act, arg; } button_t;
static button_t BTN[24] ENG_FROID;
static int nbtn;
static void button(float x, float y, float r, int act, int arg) { if (nbtn < 24) BTN[nbtn++] = (button_t){ x + ui_tx(), y + ui_ty(), r, 0, 0, 0, act, arg }; }
static void button_rect(float x, float y, float w, float h, int act, int arg) { if (nbtn < 24) BTN[nbtn++] = (button_t){ x + ui_tx(), y + ui_ty(), 0, w, h, 1, act, arg }; }
static int button_at(float x, float y) {
    for (int i = 0; i < nbtn; i++) {
        const button_t *b = &BTN[i];
        if (b->rect ? (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) : hypotf(x - b->x, y - b->y) < b->r) return i;
    }
    return -1;
}

/* Portraits (tête du sprite de face, agrandie) et carte du secteur, calculés au chargement */
static uint16_t portrait_px[2][24 * 24] ENG_FROID; static uint8_t portrait_mask[2][24 * 24] ENG_FROID;
/* Dialogue en cours : interlocuteur de droite (right), puis les répliques (qui parle, texte) */
#define DLG_MAX 16
static struct { char right[24], who[DLG_MAX][24]; const char *text[DLG_MAX]; int n; } D ENG_FROID;
/* Icônes des objets du sac (section OBJI) : 16 × 16 chacune, à la suite */
static uint16_t *obji_px; static uint8_t *obji_mask; static int n_obji;
/* Images 32 × 32 des équipements (section OBJG) ; une ligne de liste les désigne par l'icône ICON_OBJG + k (k ≥ 1) */
#define ICON_OBJG 1000
static uint16_t *objg_px; static uint8_t *objg_mask; static int n_objg;
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void toast(const char *s) { snprintf(H.toast, sizeof H.toast, "%s", s); H.toast_t = H.now; }

/* ---------- Héroïne choisie : noms échangés à l'affichage (03/11/2026, échange des rôles) ----------
   Le joueur porte le nom de l'héroïne choisie et la recrue de son nom celui de Sera (heroine_recrue de game.c). Filtre
   des textes de ui.c, posé par hud_build (pas à l'ouverture, ni dans la galerie, où les noms sont ceux des personnages
   dessinés) : les mots entiers SERA et Sera deviennent le nom de l'héroïne (JADE, Jade), et inversement ; pas « sera »
   (le verbe), ni « Serait », « Seras », ni « Sera-t-il ». Élision : devant un nom qui commence par une voyelle (AMBRE,
   AZUR, ONYX), « de Sera » devient « d'Ambre », et « d'Ambre » « de Sera » (de même que, lorsque, puisque, jusque). Comme
   nomEchange de l'émulateur, à garder identiques */
static uint32_t cp_lu(const char *s, int *n) {   /* caractère UTF-8 en s, n : ses octets */
    const uint8_t *p = (const uint8_t *)s;
    const int k = p[0] >= 0xF0 ? 3 : p[0] >= 0xE0 ? 2 : p[0] >= 0xC0 ? 1 : 0;
    uint32_t c = k ? p[0] & (0x3F >> k) : p[0];
    *n = 1;
    for (int i = 1; i <= k && p[i]; i++) { c = c << 6 | (p[i] & 63); *n = i + 1; }
    return c;
}
/* Lettre d'un mot : ASCII, chiffre, lettre latine accentuée (U+00C0 à U+024F, sauf × et ÷) */
static int lettre_mot(uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7); }
static int lettre_avant(const char *s, int i) {
    if (i <= 0) return 0;
    int j = i - 1, n;
    while (j > 0 && ((uint8_t)s[j] & 0xC0) == 0x80) j--;
    return lettre_mot(cp_lu(s + j, &n));
}
/* Le nom en minuscules sauf sa première lettre (« JADE » : « Jade » ; ASCII et lettres accentuées de Latin-1) */
static void nom_titre(const char *a, char *t, int cap) {
    int o = 0, n;
    cp_lu(a, &n);
    for (int i = 0; a[i] && o + 1 < cap; ) {
        if (i < n) { t[o++] = a[i++]; continue; }
        const uint8_t c = (uint8_t)a[i];
        if (c >= 'A' && c <= 'Z') { t[o++] = (char)(c + 32); i++; }
        else if (c == 0xC3 && (uint8_t)a[i + 1] >= 0x80 && (uint8_t)a[i + 1] <= 0x9E && (uint8_t)a[i + 1] != 0x97) {
            if (o + 2 >= cap) break;
            t[o++] = (char)c; t[o++] = (char)(a[i + 1] + 0x20); i += 2;
        } else t[o++] = a[i++];
    }
    t[o] = 0;
}
/* Le mot commence par une voyelle (élision) : AEIOUY, et leurs lettres accentuées de Latin-1 */
static int voyelle(const char *w) {
    const uint8_t c = (uint8_t)w[0], d = (uint8_t)w[1] & 0xDF;
    if (c < 0x80) return c && strchr("AEIOUYaeiouy", c) != NULL;
    return c == 0xC3 && (d == 0x80 || d == 0x82 || d == 0x84 || (d >= 0x88 && d <= 0x8B) || d == 0x8E || d == 0x8F || d == 0x94 || d == 0x96 || d == 0x99 || d == 0x9B || d == 0x9C);
}
/* Le mot qui précède le nom (fin de buf, o octets) suit la voyelle du nouveau nom (vn) : « de » et un espace deviennent
   « d' » ; « d' » (ou d’) devient « de » et un espace (« DE » si maj : le nom en capitales) ; rend la longueur de buf */
static int elision(char *buf, int o, int cap, int vn, int maj) {
    static const char *const ELIDE[5] = { "de", "que", "lorsque", "puisque", "jusque" };
    for (int w = 0; w < 5; w++) {
        const int L = (int)strlen(ELIDE[w]), ap = !vn && o >= 3 && !memcmp(buf + o - 3, "\xE2\x80\x99", 3) ? 3 : !vn && o >= 1 && buf[o - 1] == '\'' ? 1 : 0;
        const int d = vn ? o - 1 - L : o - ap - (L - 1), n = vn ? L : L - 1;   /* début du mot, ses lettres à comparer */
        if (d < 0 || (vn ? buf[o - 1] != ' ' : !ap) || lettre_avant(buf, d)) continue;
        int k = 0;
        while (k < n && (buf[d + k] | 0x20) == ELIDE[w][k]) k++;
        if (k < n) continue;
        if (vn) { buf[o - 2] = '\''; return o - 1; }
        if (d + n + 2 >= cap) return o;
        buf[d + n] = maj ? 'E' : 'e'; buf[d + n + 1] = ' ';
        return d + n + 2;
    }
    return o;
}
/* Hauteur de la bulle « parler » d'un personnage dont le haut est à top (écran) : 50 px au-dessus de lui, jamais à moins de
   BULLE_HAUT du haut de l'écran (11/11/2026, retour de l'utilisateur : devant la Gardienne de jade, statue de taille 400 sur
   son piédestal, la bulle sortait de l'écran et l'histoire semblait bloquée) ; bulleY de l'émulateur, à garder identique */
#define BULLE_HAUT 72
static float bulle_y(float top) { return top - 50 > BULLE_HAUT ? top - 50 : BULLE_HAUT; }
/* Texte selon le genre du ou de la protagoniste (11/11/2026, Île de Nacre : protagonistes femmes ou hommes) : « {petite|petit} » prend la
   première forme, la seconde pour un protagoniste masculin (son genre dans la table des héroïnes de la section ILE) ; sans accolades,
   le texte tel quel. genreTexte de l'émulateur de l'île, à garder identique */
static int protagoniste_masculin(void) {
    for (int k = 0; k < E.w.ile_nher; k++) if ((int)E.w.ile_her[k].perso == E.heroine) return E.w.ile_her[k].genre == 1;
    return 0;
}
static int genre_texte(const char *s, char *buf, int cap) {   /* 1 : le texte de buf (formes résolues), 0 : s tel quel */
    if (!s || !strchr(s, '{')) return 0;
    const int m = protagoniste_masculin();
    int o = 0;
    for (const char *p = s; *p && o < cap - 1; ) {
        const char *fin = *p == '{' ? strchr(p, '}') : NULL, *bar = fin ? (const char *)memchr(p, '|', (size_t)(fin - p)) : NULL;
        if (bar) {   /* {féminin|masculin} */
            const char *a = m ? bar + 1 : p + 1, *z = m ? fin : bar;
            while (a < z && o < cap - 1) buf[o++] = *a++;
            p = fin + 1;
        } else buf[o++] = *p++;
    }
    buf[o] = 0;
    return 1;
}
static const char *nom_echange(const char *s0, char *buf, int cap) {
    char g[256];
    const int gen = genre_texte(s0, g, (int)sizeof g);   /* d'abord les formes selon le genre (dans g, recopié dans buf s'il faut le rendre) */
    const char *s = gen ? g : s0;
    if (!s0 || E.heroine < 0 || E.heroine >= E.w.npersos || !E.w.persos[E.heroine].nom || !E.w.persos[E.heroine].nom[0]) {   /* sans héroïne choisie */
        if (!gen) return s0;
        snprintf(buf, (size_t)cap, "%s", g); return buf;
    }
    const char *S = PARTY[0].name, *N = E.w.persos[E.heroine].nom;
    char st[24], nt[24];
    nom_titre(S, st, sizeof st); nom_titre(N, nt, sizeof nt);
    const char *de[4] = { S, st, N, nt }, *vers[4] = { N, nt, S, st };
    int o = 0, chg = 0;
    for (int i = 0; s[i]; ) {
        int k = -1, l = 0;
        const int debut = !lettre_avant(s, i);   /* début d'un mot */
        for (int q = 0; debut && q < 4 && k < 0; q++) {
            int n;
            l = (int)strlen(de[q]);
            if (!l || strncmp(s + i, de[q], (size_t)l)) continue;
            const char *e = s + i + l;   /* après le nom : ni une lettre, ni « -t- » */
            if (!lettre_mot(cp_lu(e, &n)) && !(e[0] == '-' && (e[1] == 't' || e[1] == 'T') && e[2] == '-')) k = q;
        }
        if (k >= 0) {
            const int lv = (int)strlen(vers[k]);
            if (voyelle(de[k]) != voyelle(vers[k])) o = elision(buf, o, cap, voyelle(vers[k]), !(k & 1));   /* (formes 0 et 2 : en capitales) */
            if (o + lv >= cap) break;
            memcpy(buf + o, vers[k], (size_t)lv); o += lv; i += l; chg = 1;
            continue;
        }
        int n = 1;
        while (s[i + n] && ((uint8_t)s[i + n] & 0xC0) == 0x80) n++;
        if (o + n >= cap) break;
        memcpy(buf + o, s + i, (size_t)n); o += n; i += n;
    }
    if (!chg) { if (!gen) return s0; snprintf(buf, (size_t)cap, "%s", g); return buf; }
    buf[o] = 0;
    return buf;
}
/* Texte tel qu'il s'affiche (formes selon le genre, noms échangés) : essais de rendu_test (« filtre:TEXTE ») */
const char *hud_texte_affiche(const char *s, char *buf, int cap) { return nom_echange(s, buf, cap); }
/* Couleur du membre m (son nom, ses lignes, la frise du combat) : avec une recrue montrée en Sera (membre_en_sera), le
   joueur prend celle de la recrue et la recrue celle de Sera, comme leurs auras (couleurs de leurs cut-ins) */
static uint32_t membre_couleur(int m) {
    const int r = heroine_recrue();
    return m == 0 && membre_en_sera(r) ? PARTY[r].color : membre_en_sera(m) ? PARTY[0].color : PARTY[m].color;
}

/* Tenues : le membre m en a-t-il ; texte de la ligne du menu ÉQUIP. ; pourquoi une tenue est mise d'office */
static const char *const TENUE_RAISON[4] = { "", "dans l'eau", "pendant la course", "par le scénario" };
static int has_tenues(int m) { for (int t = 0; t < E.w.ntenues; t++) if (tenue_mine(m, t)) return 1; return 0; }
static const char *tenue_label(int m) { const int t = tenue_of(m, 0); return t >= 0 ? E.w.tenues[t].nom : has_tenues(m) ? "LA SIENNE" : PARTY[m].eq[0]; }

/* Portrait k (0 : joueur, 1 : interlocuteur), 24 × 24 : tête du sprite de face, dans la planche propre sheet (carré
   du portrait, première image) ou, si sheet < 0, dans la planche commune ; couleurs de la table remap */
static void portrait_dans(uint16_t *ppx, uint8_t *pmask, int sheet, int remap);
static void make_portrait(int k, int sheet, int remap) { portrait_dans(portrait_px[k], portrait_mask[k], sheet, remap); }
/* Portraits des membres (menu ÉQUIPE) : celui de chacun dans sa tenue, refait quand sa planche change (eq_psh : planche
   + 2, 0 : pas encore fait) */
static uint16_t eq_ppx[NMEMB][24 * 24] ENG_FROID; static uint8_t eq_pmask[NMEMB][24 * 24] ENG_FROID; static int eq_psh[NMEMB];
static void portrait_membre(int m, float x, float y, float r, uint16_t col) {
    const int pp = m > 0 ? E.w.party_perso[m] : -1, sh = member_sheet(m);
    const int remap = m == 0 ? 0 : pp >= 0 ? perso_vue(pp, 0).remap : m < 4 ? m + 1 : 0;   /* (la recrue de l'héroïne choisie : celle de Sera) */
    if (eq_psh[m] != sh + 2) { portrait_dans(eq_ppx[m], eq_pmask[m], sh, sh >= 0 || m < 4 ? remap : 0); eq_psh[m] = sh + 2; }
    ui_circle(x, y, r, ui_rgb(0x080612), 255);
    ui_image(eq_ppx[m], eq_pmask[m], 24, 24, x - r, y - r + r / 9, r * 2 / 24, x, y, r - 1);
    ui_ring(x, y, r, 2, col, 255);
}
static void portrait_dans(uint16_t *ppx, uint8_t *pmask, int sheet, int remap) {
    const world_t *w = &E.w;
    const sheet_t *s = sheet >= 0 ? &w->sheets[sheet] : NULL;
    /* Carré du portrait dans la direction de sa première ligne (planche lue à la demande : lue tout de suite) */
    const int pd = s ? s->py / s->fh : 0, py = s ? s->py - pd * s->fh : 0;
    const uint8_t *blk = s ? sheet_block(sheet, pd, 2) : NULL;
    if (s && !blk) s = NULL;
    for (int y = 0; y < 24; y++) for (int x = 0; x < 24; x++) {
        int yy = s ? py + y * s->psize / 24 : 0;
        if (s && yy > s->fh - 1) yy = s->fh - 1;
        int id = s ? blk[yy * (s->fw * s->frames) + s->px + x * s->psize / 24]
                   : w->sheet[(y + 1) * w->sw + x + 4];          /* ligne 0 = face, colonne 0 ; tête : x 4..27, y 1..24 */
        const uint8_t *c = w->pal[w->remap[remap][id]];
        ppx[y * 24 + x] = ui_rgb(((uint32_t)c[0] << 16) | (c[1] << 8) | c[2]);
        pmask[y * 24 + x] = id != 0;
    }
}

/* ---------- Initialisation ---------- */
static int tvg_ok;   /* villes du téléport rapportées à celles du guidage (tele_ville, plus bas) : à refaire avec ce paquet */
static void sauve_reserver(void);   /* bloc de la sauvegarde et du brouillard (plus bas) */
static void echs_lire(void);
void hud_init(void) {
    echs_lire();
    memset(&H, 0, sizeof H);
    sauve_reserver();
    memset(eq_psh, 0, sizeof eq_psh);   /* portraits du menu ÉQUIPE : refaits avec ce paquet */
    tvg_ok = 0; vol_k = -1;
    H.sub = -1; H.bright = 3; H.last_tap_t = -10; H.shown_zoom = E.zoom;
    const world_t *w = &E.w;
    make_portrait(0, w->psheet, 0);
    H.psheet = w->psheet;
    /* Pas de portrait à droite avant le premier interlocuteur (le joueur qui parle d'abord n'en change pas : il
       resterait sinon celui de la planche commune), comme le BLANK de speakerFor dans l'émulateur */
    memset(portrait_mask[1], 0, sizeof portrait_mask[1]);
    /* Icônes des objets : palette commune (index 1 à n, 0 transparent), converties une fois en RGB565 */
    uint32_t osz;
    const uint8_t *o = pak_section("OBJI", &osz);
    n_obji = 0;
    if (o && osz >= 8) {
        uint32_t ni = rd32(o), nc = rd32(o + 4), pal = (nc * 3 + 3) & ~3u;
        if (nc <= 255 && ni <= 255 && 8 + pal + ni * 256 <= osz
            && (obji_px = E.cfg.alloc(ni * 256 * 2, 0)) && (obji_mask = E.cfg.alloc(ni * 256, 0))) {
            const uint8_t *px = o + 8 + pal;
            for (uint32_t i = 0; i < ni * 256; i++) {
                const uint8_t v = px[i], *c = o + 8 + (v ? v - 1 : 0) * 3;
                obji_mask[i] = v && v <= nc;
                obji_px[i] = obji_mask[i] ? ui_rgb(((uint32_t)c[0] << 16) | (c[1] << 8) | c[2]) : 0;
            }
            n_obji = (int)ni;
        }
    }
    /* Images des équipements : 32 couleurs par image (index 1 à 32, 0 transparent), converties en RGB565 */
    const uint8_t *g = pak_section("OBJG", &osz);
    n_objg = 0;
    if (g && osz >= 4) {
        const uint32_t ni = rd32(g);
        if (ni <= 255 && 4 + ni * 1124 <= osz && (objg_px = E.cfg.alloc(ni * 1024 * 2, 0)) && (objg_mask = E.cfg.alloc(ni * 1024, 0))) {
            for (uint32_t k = 0; k < ni; k++) {
                const uint8_t *r = g + 4 + k * 1124, *px = r + 100;
                for (int i = 0; i < 1024; i++) {
                    const uint8_t v = px[i], *c = r + 4 + (v ? v - 1 : 0) * 3;
                    objg_mask[k * 1024 + i] = v && v <= r[0];
                    objg_px[k * 1024 + i] = objg_mask[k * 1024 + i] ? ui_rgb(((uint32_t)c[0] << 16) | (c[1] << 8) | c[2]) : 0;
                }
            }
            n_objg = (int)ni;
        }
    }
    /* Carte : précalculée par l'export (couleur moyenne de la texture de chaque case, routes éclaircies, bâtiments
       sombres), car seuls les chunks proches de Sera sont en mémoire ; dessinée depuis le paquet (ui_image_rgb : plus
       de copie en RGB565 ni de masque, 192 Ko de PSRAM) */
    reglages_lire();   /* réglages communs (plus bas), sinon ceux de la partie la plus récente */
}

/* ---------- Sauvegarde : deux emplacements alternés, somme de contrôle CRC-32 ---------- */
static uint32_t crc32(const uint8_t *p, int n) {
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;
}
/* Bloc des recrues (membres 4 et suivants ; les quatre premiers gardent leur place dans la sauvegarde) : au combat, la
   bande et les imposés (bit i : PARTY[i] ; team de la sauvegarde : les huit premiers), puis PV, PM, tenue, job, jobs
   connus et objets portés de chaque recrue ; n places : 5 en ESVD (JADE à AZUR), SAVE_RECRUES en ESVE (AMBRE, et de la
   place pour la suite sans changer de format) */
#define SV_RECRUES(n) struct { uint16_t team, recrues, impose, r0; int32_t hp[n], mp[n]; int8_t tenue[n]; uint8_t job[n]; \
                               uint16_t jobs_known[n]; uint8_t eq[n][3]; }
#define SAVE_RECRUES 8
typedef SV_RECRUES(5) sv_recrues5_t;
typedef char sv_place_t[NMEMB - 4 <= SAVE_RECRUES ? 1 : -1];   /* ESVE : NMEMB - 4 recrues au plus */
/* Magie apprise (ESVH, 03/11/2026) : sorts appris pour de bon par chaque membre (bit k : sort k ; u32 bas, haut) et points
   d'apprentissage de chaque sort, quatre bits chacun (deux sorts par octet, le premier dans les bits bas ; 15 au plus,
   autant qu'un cristal en demande) : 400 octets */
typedef struct { uint32_t appris[NMEMB][2]; uint8_t pts[NMEMB][MAX_SORTS / 2]; } sv_magie_t;
typedef struct {
    uint32_t magic, seq;
    float x, z, dir, yaw;
    uint8_t theme, fog, dither, bright;
    int32_t hp[4], mp[4];
    uint8_t items[MAX_ITEMS], team;
    int32_t level, exp, credits;         /* niveau, expérience, bourse */
    uint16_t obj[2];                     /* objectif du scénario */
    uint8_t quests[SC_MAX_QUESTS];       /* état des quêtes */
    int16_t qtrack;                      /* quête suivie */
    uint16_t mus_force;                  /* musique imposée par le scénario */
    uint8_t flags[SC_MAX_FLAGS / 8];     /* scénario : drapeaux, événements « une seule fois » faits */
    uint8_t done[SC_MAX_EVENTS / 8];
    int8_t tenue[4];                     /* tenue portée par chaque membre (index TENU, -1 : la sienne) */
    uint8_t job[4];                      /* job de chaque membre, jobs connus, objets portés (EQ_AUCUN : aucun) */
    uint16_t jobs_known[4];
    uint8_t eq[4][3];
    int16_t guide;                       /* lieu du menu GUIDAGE suivi (-1 : aucun) */
    char heroine[16];                    /* héroïne choisie au départ (nom du personnage ; vide : Sera) */
    char lieu[24];                       /* ville la plus proche (écran des parties) */
    uint32_t temps, ordre;               /* temps de jeu (s) ; rang de la sauvegarde parmi toutes les parties (CONTINUER) */
    uint32_t carte;                      /* carte où est Sera (CART ; 0 : celle du monde) */
    uint8_t cof_ouv[COFFRES_BAT / 8], tres_ouv[TRESORS_MAX / 8];   /* coffres fouillés : des bâtiments (PORT), trésors du dehors (TRES) */
    SV_RECRUES(SAVE_RECRUES) rec;        /* les recrues (01/11/2026), plus haut */
    uint8_t tele_vu[TELE_MAX / 8];       /* lieux du téléport visités : bit k, lieu k de TELE (ESVF, 01/11/2026) */
    uint8_t conduite, mix[3];            /* SYSTÈME › CONDUITE (CONDUITE_*, bit 0 ; SV_ANCIENNE : sauvegarde d'avant ESVF, lue) et COMMANDES
                                            (bits 1-2 : COMMANDES_* + 1, 0 : d'avant, le réglage en cours reste) ; mix : SYSTÈME › SON
                                            (01/11/2026), niveau des canaux CANAL_* sur 5 bits chacun (0 à 20 pas de 5 %), bit 23 :
                                            présents (0 : sauvegarde d'avant, le mixage en cours reste ; mix_ecrire, mix_lire) */
    garage_sv_t garage;                  /* véhicule de Sera (garage.c ; ESVG, 01/11/2026) */
    sv_magie_t magie;                    /* sorts appris et points des cristaux du Chœur (ESVH, 03/11/2026) */
    uint8_t best_vu[16], best_bat[16];   /* bestiaire (ESVJ, 06/11/2026) */
    uint8_t eq_up[64];                   /* amélioration de l'équipement (ESVJ) */
    uint8_t veh_up[4], libre[12];        /* véhicule de Sera, réserve (ESVJ) */
    uint8_t eq_el[128];                  /* éléments donnés par la forge (E.eq_el ; ESVK, 08/11/2026) */
    uint8_t arene[16];                   /* arène de combat (arene.c) : [0] rangs gagnés (E.arene_gagnes, bit r), le reste à zéro (ESVI, 04/11/2026) */
    uint32_t crc;
} save_t;
/* ESVI (04/11/2026) : le fichier est save_t (somme de contrôle crc), puis les octets du brouillard de la carte du menu
   (brouillard_taille, carte.c : en-tête, bits des blocs vus de chaque carte, leur somme de contrôle) ; les deux dans le
   bloc SV_BLOC (hud_init), où vivent les bits du brouillard : écrits d'un coup, sans copie */
static uint8_t *SV_BLOC;
/* État du départ (06/11/2026, retour de jeu : QUITTER, puis NOUVELLE PARTIE, repartait de la partie quittée, niveau et compagnons
   compris) : une sauvegarde prise à la fin de eng_init, avant toute partie, qu'une nouvelle partie applique comme un chargement.
   Tout ce que la sauvegarde connaît repart ainsi de zéro, sans liste à tenir à jour (hud_depart_garder, partie_depart) */
static save_t *SV_DEPART;
/* hud_init : le bloc (save_t, puis le brouillard de toutes les cartes du paquet : 4,7 Ko pour celui du 04/11/2026), une fois ;
   rien de vu */
static void sauve_reserver(void) {
    if (!SV_BLOC) SV_BLOC = E.cfg.alloc(sizeof(save_t) + brouillard_taille(), 0);
    if (!SV_DEPART) SV_DEPART = E.cfg.alloc(sizeof(save_t), 0);
    if (!SV_BLOC || !SV_DEPART) { E.error = "mémoire insuffisante (sauvegarde)"; return; }
    memset(SV_BLOC, 0, sizeof(save_t));
    brouillard_init(SV_BLOC + sizeof(save_t));
}
#define SV_ANCIENNE 0xFF         /* conduite d'une sauvegarde d'avant ESVF : lieux visités reconstitués au chargement (tele_ancien) */
#define SAVE_MAGIC 0x4B565345u   /* 'ESVK' : ESVJ et les éléments donnés par la forge (08/11/2026) */
#define SAVE_MAGICJ 0x4A565345u  /* 'ESVJ' : ESVI et le bestiaire (06/11/2026), encore lue */
#define SAVE_MAGICI 0x49565345u  /* 'ESVI' (scénario, équipe, quêtes, objets de l'Atelier, niveau, crédits, musique, tenues, jobs, guidage, héroïne, lieu, temps, carte, coffres, recrues, lieux visités, conduite, véhicule de Sera, magie apprise, arène ; puis le brouillard) */
#define SAVE_MAGICH 0x48565345u  /* 'ESVH' : la même sans l'arène ni le brouillard, encore lue (brouillard reconstitué : brouillard_ancien) */
#define SAVE_MAGICG 0x47565345u  /* 'ESVG' : la même sans la magie apprise, encore lue */
#define SAVE_MAGICF 0x46565345u  /* 'ESVF' : la même sans le véhicule de Sera, encore lue */
#define SAVE_MAGICE 0x45565345u  /* 'ESVE' : la même sans les lieux visités du téléport ni la conduite, encore lue */
#define SAVE_MAGICD 0x44565345u  /* 'ESVD' : la même avec cinq places de recrues (JADE à AZUR, sans AMBRE), encore lue */
#define SAVE_MAGICC 0x43565345u  /* 'ESVC' : la même sans les recrues (l'équipe des quatre), encore lue */
#define SAVE_MAGICB 0x42565345u  /* 'ESVB' : la même sans les coffres, encore lue */
#define SAVE_MAGICA 0x41565345u  /* 'ESVA' : la même sans la carte, encore lue */
#define SAVE_MAGIC9 0x39565345u  /* 'ESV9' : la même sans l'héroïne, le lieu ni le temps, encore lue */
#define SAVE_MAGIC8 0x38565345u  /* 'ESV8' : sans le guidage non plus, encore lue */
#define SAVE_MAGIC7 0x37565345u  /* 'ESV7' : la même sans les jobs (somme de contrôle à leur place), encore lue */
#define SAVE_MAGIC6 0x36565345u  /* 'ESV6' : sans les tenues ni les jobs, encore lue */

/* Ville la plus proche de (x, z), à moins de 80 cases (sinon les Friches) */
static void lieu_de(float x, float z, char *t, int n) {
    const guide_t *g = E.w.guides;
    int v = -1; float best = 80;
    for (int i = 0; i < E.w.nguides; i++) {
        if (g[i].kind != GUIDE_VILLE) continue;
        const float d = hypotf(g[i].x - x, g[i].z - z);
        if (d < best) { best = d; v = i; }
    }
    snprintf(t, n, "%s", v >= 0 ? g[v].nom : "LES FRICHES");
}
static int read_slot(int slot, save_t *s);
/* Sauvegarde la plus récente de la partie p (deux emplacements alternés : p × 2 et p × 2 + 1) ; 0 : aucune valide */
static int partie_lire(int p, save_t *s) {
    static save_t b ENG_FROID;   /* les sauvegardes en PSRAM : la SRAM manque avec le Bluetooth */
    const int va = read_slot(p * 2, s), vb = read_slot(p * 2 + 1, &b);
    if (vb && (!va || b.seq > s->seq)) *s = b;
    return va || vb;
}

/* Mixage de SYSTÈME › SON dans la sauvegarde (mix de save_t) : niveau de chaque canal en pas de 5 % */
static void mix_ecrire(uint8_t m[3]) {
    uint32_t v = 1u << 23;
    for (int c = 0; c < CANAUX; c++) v |= (uint32_t)lroundf(audio_canal_niveau(c) * 20) << (5 * c);
    m[0] = (uint8_t)v; m[1] = (uint8_t)(v >> 8); m[2] = (uint8_t)(v >> 16);
}
static void mix_lire(const uint8_t m[3]) {
    const uint32_t v = m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16;
    if (!(v >> 23 & 1)) return;   /* sauvegarde d'avant le mixage : celui en cours reste */
    for (int c = 0; c < CANAUX; c++) { const uint32_t n = v >> (5 * c) & 31; audio_canal(c, (n > 20 ? 20 : n) / 20.0f); }
}

/* Sauvegarde de la partie en cours ; discret (sauvegarde automatique) : « SAUVEGARDE » en haut de l'écran au lieu du message.
   Le rang parmi toutes les parties (CONTINUER) est lu une fois, puis suivi (avant le 01/11/2026, les six emplacements
   étaient relus à chaque sauvegarde : six lectures sur la carte SD de plus pour une écriture) */
static struct { int att, carte, lieu, us, vu; float libre, depuis, vu_t, lieu_t; } AS;   /* sauvegarde automatique (plus bas) ; us : durée de la dernière écriture ; vu : « SAUVEGARDE » montré depuis vu_t */
/* L'état du jeu dans une sauvegarde (sans le numéro ni la somme de contrôle : save_ecrire ; hud_depart_garder) */
static void sv_remplir(save_t *sv) {
    memset(sv, 0, sizeof *sv);
    int_position(&sv->x, &sv->z, &sv->yaw); sv->dir = E.sera.dir;   /* dans un intérieur : devant sa porte, dans la rue */
    int renc = essai_partie(&sv->x, &sv->z); if (renc < 0) renc = E.renc_mode;   /* essai de fluidité en cours : l'état d'avant lui (game.c) */
    /* Vision (la ville d'avant la Cendre) : la partie reprend au point d'avant le voyage (world_carte ; partieObjet de l'émulateur) */
    const int vision = E.w.cartes[E.w.carte].vision, carte = vision ? E.vis_carte : E.w.carte;
    if (vision) { sv->x = E.vis_x; sv->z = E.vis_z; }
    sv->theme = (uint8_t)E.theme; sv->fog = (uint8_t)(E.fog_on | (E.volant + 1) << 1); sv->dither = (uint8_t)E.dither_on; sv->bright = (uint8_t)(H.bright | E.eveil_essai << 3 | renc << 4 | E.essai << 7);   /* fog, bits 1 à 3 : DIRECTION + 1 (0 : sauvegarde d'avant) ; bright, bit 3 : ÉVEIL, 4 à 6 : rencontres, 7 : mode d'essai */
    for (int i = 0; i < 4; i++) { sv->hp[i] = PARTY[i].hp; sv->mp[i] = PARTY[i].mp; }
    for (int i = 0; i < n_items; i++) sv->items[i] = (uint8_t)ITEMS[i].count;
    sv->level = E.level; sv->exp = E.exp; sv->credits = E.credits;
    sv->team = (uint8_t)E.team; sv->obj[0] = E.obj[0]; sv->obj[1] = E.obj[1];
    memcpy(sv->quests, E.quests, sizeof sv->quests); sv->qtrack = (int16_t)E.qtrack; sv->mus_force = E.mus_force;
    memcpy(sv->flags, E.flags, sizeof sv->flags); memcpy(sv->done, E.ev_done, sizeof sv->done);
    if (E.w.ile[0]) ile_enleve_ecrire(sv->libre);   /* île : l'habitant enlevé (ile.c ; ailleurs, la réserve reste à zéro) */
    memcpy(sv->tenue, E.tenue_eq, sizeof sv->tenue);
    memcpy(sv->job, E.job, sizeof sv->job); memcpy(sv->jobs_known, E.jobs_known, sizeof sv->jobs_known); memcpy(sv->eq, E.eq, sizeof sv->eq);
    sv->rec.team = (uint16_t)E.team; sv->rec.recrues = (uint16_t)E.recrues; sv->rec.impose = (uint16_t)E.impose;   /* les recrues (ESVE) */
    for (int i = 4; i < NMEMB; i++) {
        sv->rec.hp[i - 4] = PARTY[i].hp; sv->rec.mp[i - 4] = PARTY[i].mp; sv->rec.tenue[i - 4] = E.tenue_eq[i];
        sv->rec.job[i - 4] = E.job[i]; sv->rec.jobs_known[i - 4] = E.jobs_known[i]; memcpy(sv->rec.eq[i - 4], E.eq[i], 3);
    }
    sv->guide = (int16_t)E.guide;
    if (E.heroine >= 0) snprintf(sv->heroine, sizeof sv->heroine, "%s", E.w.persos[E.heroine].nom);
    if (carte) snprintf(sv->lieu, sizeof sv->lieu, "%.23s", E.w.cartes[carte].nom);   /* ailleurs que sur la planète : sa carte (lieuDe de l'émulateur : villes de la carte, aucune) */
    else lieu_de(sv->x, sv->z, sv->lieu, sizeof sv->lieu);
    sv->temps = (uint32_t)E.temps_jeu; sv->carte = (uint32_t)carte;
    memcpy(sv->cof_ouv, E.cof_ouv, sizeof sv->cof_ouv); memcpy(sv->tres_ouv, E.tres_ouv, sizeof sv->tres_ouv);
    memcpy(sv->tele_vu, E.tele_vu, sizeof sv->tele_vu); sv->conduite = (uint8_t)(E.conduite | (E.commandes + 1) << 1 | (E.tele_tout ? 8 : 0) | (E.zoom_libre ? 16 : 0));   /* lieux visités, conduite, commandes, téléport partout et zoom manuel (ESVF) */
    memcpy(sv->best_vu, E.best_vu, sizeof sv->best_vu); memcpy(sv->best_bat, E.best_bat, sizeof sv->best_bat);   /* bestiaire (ESVJ) */
    memcpy(sv->eq_up, E.eq_up, sizeof sv->eq_up); memcpy(sv->veh_up, E.veh_up, sizeof sv->veh_up);
    memcpy(sv->eq_el, E.eq_el, sizeof sv->eq_el);   /* éléments de la forge (ESVK) */
    memset(sv->arene, 0, sizeof sv->arene); sv->arene[0] = (uint8_t)E.arene_gagnes;   /* arène : rangs gagnés (ESVI) */
    mix_ecrire(sv->mix);
    garage_ecrire(&sv->garage);   /* véhicule de Sera (ESVG) */
    memcpy(sv->magie.appris, E.appris, sizeof sv->magie.appris);   /* magie apprise (ESVH) */
    for (int i = 0; i < NMEMB; i++) for (int k = 0; k < MAX_SORTS; k++) sv->magie.pts[i][k >> 1] |= (uint8_t)((E.apts[i][k] > 15 ? 15 : E.apts[i][k]) << (4 * (k & 1)));
}
static int save_ecrire(int discret) {
    if (!E.cfg.save_write) { if (!discret) toast("Pas de carte SD"); return 0; }
    save_t *const sv = (save_t *)SV_BLOC;   /* la sauvegarde, suivie du brouillard de la carte du menu (hud_init) ; hors de la pile */
    if (!sv) { if (!discret) toast("Échec de l'écriture"); return 0; }
    static uint32_t ordre = 0; static int ordre_lu = 0;
    if (!ordre_lu) { for (int p = 0; p < PARTIES; p++) if (partie_lire(p, sv) && sv->ordre > ordre) ordre = sv->ordre; ordre_lu = 1; }   /* (dans la tête du bloc, refaite ensuite) */
    sv_remplir(sv);
    sv->magic = SAVE_MAGIC; sv->seq = (uint32_t)++H.seq; sv->ordre = ordre + 1;
    sv->crc = crc32((const uint8_t *)sv, offsetof(save_t, crc));
    /* Brouillard (ESVI) : ses octets sont déjà à la suite, sa somme de contrôle à la fin */
    const uint32_t nb = brouillard_taille(), ntot = (uint32_t)sizeof *sv + nb;
    uint8_t *br = SV_BLOC + sizeof *sv;
    const uint32_t bc = crc32(br, (int)nb - 4);
    memcpy(br + nb - 4, &bc, 4);
    char t[48];
    const uint32_t t0 = E.cfg.clock_us ? E.cfg.clock_us() : 0;
    const int ok = E.cfg.save_write(E.partie * 2 + (int)(sv->seq & 1), SV_BLOC, (int)ntot) == (int)ntot;
    AS.us = E.cfg.clock_us ? (int)(E.cfg.clock_us() - t0) : 0;   /* durée de l'écriture (mesure : rendu_test, console de l'ESP32) */
    if (ok) { ordre = sv->ordre; AS.libre = 0; AS.depuis = 0; }   /* le temps de jeu libre repart de zéro, menu compris */
    if (ok && discret) { AS.vu_t = H.now; AS.vu = 1; return ok; }
    if (ok) snprintf(t, sizeof t, "Partie %d : sauvegarde n° %u", E.partie + 1, (unsigned)sv->seq);
    else snprintf(t, sizeof t, "Échec de l'écriture");
    toast(t);
    return ok;
}
static int save_game(void) { return save_ecrire(0); }

/* Plateforme : heure réelle, et sauvegarde quand la montre se met en veille ou que sa batterie se vide (pas pendant
   l'écran titre, un combat, une course, une scène ou un dialogue : la position ou l'état ne vaudraient rien) */
void eng_set_horloge(int secondes) { E.horloge = secondes >= 0 && secondes < 86400 ? secondes : -1; }
int eng_sauvegarder(void) {
    if (AG.on) return 0;   /* course anti-gravité : rien à sauvegarder */
    if (!E.cfg.save_write || accueil_on() || BT.on || CR.on || script_running() || H.dialog || carriere_on()) return 0;   /* (pas en mode course) */
    return save_game();
}

/* ---------- Sauvegarde automatique (sauveAuto de l'émulateur, à garder identiques) ----------
   Dans la partie en cours, même format et mêmes emplacements alternés que celle du menu, aux points clés : début d'une
   nouvelle partie (accueil.c : REPRENDRE a toujours un point), arrivée sur une autre carte (passage, voyage, téléport,
   saut), étape d'une quête principale qui change (script.c, set_quest), entrée dans une ville ou un lieu du guidage (à
   moins de son rayon d'arrivée, AUTO_LIEU_R au moins ; un autre que le dernier), toutes les AUTO_LIBRE s de jeu libre ;
   et juste avant un événement du scénario qui contient un combat (script.c, run : rien n'est encore fait ; le recharger
   le relance, Sera étant dans sa zone). Une demande attend que rien ne soit en cours (auto_permise : ni script, combat,
   dialogue, choix, menu, course, alarme, recherche ou arrestation de la police, mise en scène, ouverture) et AUTO_ECART s
   depuis la précédente ; « SAUVEGARDE » discret en haut de l'écran AUTO_VU s (draw_sauve_vu) */
#define AUTO_LIBRE 300.0f
#define AUTO_ECART 20.0f
#define AUTO_VU 1.8f
#define AUTO_LIEU_R 4.0f
static int auto_permise(void) {
    return E.cfg.save_write && !E.essai_mesure && !accueil_on() && !BT.on && !CR.on && !hud_course_menu_open() && !H.dialog && !CHX.on && !H.menu && H.sub < 0
        && !E.ui_block && !POL.niveau && !police_bloque() && !E.alarme.on && !mise_bloque() && !MISE.fuite_on && !carriere_on();   /* (ni le mode course) */
}
/* Lieu du guidage où est Sera (planète) : une ville ou un lieu nommé à moins de son rayon d'arrivée (AUTO_LIEU_R au moins) ;
   dans les rues d'une ville (E.zoom_lieu de ville_maj : son rayon d'arrivée n'est que le centre), la ville la plus proche à
   moins de AUTO_VILLE_R ; -1 : aucun */
#define AUTO_VILLE_R 120.0f
/* Lieu du guidage en (x, z) de la planète (urbain : dans une zone urbaine, la ville la plus proche s'il n'y a rien plus
   près) ; aussi pour les villes visitées du téléport (tele_point) */
static int lieu_point(float x, float z, int urbain) {
    int best = -1;
    float bd = AUTO_VILLE_R;
    for (int i = 0; i < E.w.nguides; i++) {
        const guide_t *g = &E.w.guides[i];
        if ((g->kind != GUIDE_VILLE && g->kind != GUIDE_LIEU) || g->carte) continue;   /* ceux de la planète */
        const float d = hypotf(g->x - x, g->z - z);
        if (d < fmaxf(g->r, AUTO_LIEU_R)) return i;
        if (urbain && g->kind == GUIDE_VILLE && d < bd) { bd = d; best = i; }
    }
    return best;
}
static int lieu_ici(void) { return E.w.carte ? -1 : lieu_point(E.sera.x, E.sera.z, E.zoom_lieu == 2); }
static void tele_maj(void);   /* lieux visités du téléport (plus bas) */
/* Sauvegarde automatique tout de suite si rien n'est en cours (avant_combat : un script vient de partir, rien n'en est fait,
   et pas d'écart minimal) ; 1 si elle est écrite */
int hud_sauve_auto(int avant_combat) {
    if (!auto_permise() || (!avant_combat && (script_running() || AS.depuis < AUTO_ECART))) return 0;
    AS.att = 0;
    return save_ecrire(1);
}
void hud_sauve_demande(int raison) { AS.att |= raison; }
/* Nouvelle partie ou partie chargée : rien en attente, la carte et le lieu d'ici sont déjà vus */
void hud_sauve_repartir(void) { AS.att = 0; AS.carte = E.w.carte; AS.lieu = -2; AS.lieu_t = 0.5f; AS.libre = AS.depuis = 0; }   /* lieu : pris sans demande au premier coup d'œil (ville_maj l'a vu) */
int hud_sauve_us(void) { return AS.us; }
void hud_sauve_maj(float dt) {
    AS.depuis += dt;
    if (AS.carte != E.w.carte) { AS.carte = E.w.carte; AS.att |= AUTO_CARTE; }
    if ((AS.lieu_t += dt) >= 0.5f) {   /* deux fois par seconde : un lieu nouveau (pas celui d'où l'on vient) */
        const int l = lieu_ici();
        AS.lieu_t = 0;
        tele_maj();   /* lieux visités du téléport */
        if (AS.lieu == -2) AS.lieu = l;   /* partie commencée ou chargée ici */
        else if (l >= 0 && l != AS.lieu) { AS.att |= AUTO_LIEU; AS.lieu = l; }
    }
    const int libre = auto_permise() && !script_running();
    if (libre && (AS.libre += dt) >= AUTO_LIBRE) AS.att |= AUTO_TEMPS;
    if (AS.att && libre && AS.depuis >= AUTO_ECART) hud_sauve_auto(0);
}
/* REPRENDRE après une défaite : le dernier point de sauvegarde de la partie en cours (celui du début au pis) ; sans aucun
   (pas de carte SD), au départ de la planète, comme avant le 01/11/2026. Depuis le 02/11/2026 (PV et PM gardés d'un combat
   à l'autre), l'équipe y reprend soignée, PV et PM pleins, contre le prix de l'hôpital (prix_soins(PRIX_REPRENDRE), ce que
   la bourse permet) : une sauvegarde faite l'équipe à bout ne mène pas à une suite de défaites (reprendre de l'émulateur) */
#define PRIX_REPRENDRE 80
static int load_partie(int p);
void hud_reprendre(void) {
    if (!load_partie(E.partie)) {
        if (E.w.carte) world_carte(0, E.w.start_x, E.w.start_z);
        E.sera.x = E.w.start_x; E.sera.z = E.w.start_z; E.has_dest = 0;
        toast("Pas de sauvegarde : retour au départ");
    } else {
        int prix = prix_soins(PRIX_REPRENDRE);
        if (prix > E.credits) prix = E.credits;
        E.credits -= prix;
        char t[64]; snprintf(t, sizeof t, "Équipe soignée · −%d CR", prix); toast(t);
    }
    for (int i = 0; i < NMEMB; i++) { PARTY[i].hp = PARTY[i].max; PARTY[i].mp = PARTY[i].maxmp; }
    hud_sauve_repartir();
    E.sera_y_ok = 0; E.stream_hold = 1; BT.fade_t = E.clock;
}
int eng_ecran_titre(void) { return AG.on ? AG.phase == AGP_TITRE : accueil_on(); }
int hud_camera_tourne(void) { return H.down && H.rim_on && !H.menu && H.sub < 0; }

/* Ancienne sauvegarde (n octets) : la même jusqu'à end, puis sa somme de contrôle (alignée sur 4) */
static int read_old(const save_t *s, int n, uint32_t magic, size_t end) {
    const size_t o = (end + 3) & ~(size_t)3;
    uint32_t c;
    if (n != (int)(o + 4) || s->magic != magic) return 0;
    memcpy(&c, (const uint8_t *)s + o, 4);
    return c == crc32((const uint8_t *)s, (int)o);
}
static int read_ancien(save_t *s, int n);
static int read_avant_f(save_t *s, int n);
/* ESVI : sans le bestiaire ; arène et somme de contrôle juste après la magie, le brouillard à la suite (reconstitué : brouillard_ancien) */
static int read_esvi(int slot, save_t *s) {
    if (!E.cfg.save_read) return 0;
    const size_t tete = offsetof(save_t, best_vu), o = (tete + sizeof s->arene + 3) & ~(size_t)3;
    const int n = E.cfg.save_read(slot, s, (int)(o + 4));
    uint32_t c;
    if (n != (int)(o + 4) || s->magic != SAVE_MAGICI) return 0;
    memcpy(&c, (const uint8_t *)s + o, 4);
    if (c != crc32((const uint8_t *)s, (int)o)) return 0;
    uint8_t a[sizeof s->arene];
    memcpy(a, (const uint8_t *)s + tete, sizeof a);
    memset(s->best_vu, 0, sizeof s->best_vu); memset(s->best_bat, 0, sizeof s->best_bat); memset(s->eq_up, 0, sizeof s->eq_up); memset(s->veh_up, 0, sizeof s->veh_up); memset(s->libre, 0, sizeof s->libre);
    memcpy(s->arene, a, sizeof a);
    return 1;
}
/* ESVJ : sans les éléments de la forge ; arène et somme de contrôle juste après la réserve (libre), le brouillard à la suite
   (sizeof eq_el plus tôt : brouillard_lire) */
static int read_esvj(int slot, save_t *s) {
    if (!E.cfg.save_read) return 0;
    const size_t tete = offsetof(save_t, eq_el), o = (tete + sizeof s->arene + 3) & ~(size_t)3;
    const int n = E.cfg.save_read(slot, s, (int)(o + 4));
    uint32_t c;
    if (n != (int)(o + 4) || s->magic != SAVE_MAGICJ) return 0;
    memcpy(&c, (const uint8_t *)s + o, 4);
    if (c != crc32((const uint8_t *)s, (int)o)) return 0;
    uint8_t a[sizeof s->arene];
    memcpy(a, (const uint8_t *)s + tete, sizeof a);
    memset(s->eq_el, 0, sizeof s->eq_el);
    memcpy(s->arene, a, sizeof a);
    return 1;
}
static int read_slot(int slot, save_t *s) {
    if (!E.cfg.save_read) return 0;
    const int n = E.cfg.save_read(slot, s, sizeof *s);
    if (n == (int)sizeof *s && s->magic == SAVE_MAGIC) return s->crc == crc32((const uint8_t *)s, offsetof(save_t, crc));
    if (read_esvj(slot, s)) return 1;   /* ESVJ (sans les éléments de la forge) */
    if (read_esvi(slot, s)) return 1;   /* ESVI (le fichier est plus long : le brouillard suit) */
    if (read_old(s, n, SAVE_MAGICH, offsetof(save_t, best_vu))) { memset(s->arene, 0, sizeof s->arene); return 1; }   /* ESVH : ni arène ni brouillard */
    if (read_old(s, n, SAVE_MAGICG, offsetof(save_t, magie))) { memset(&s->magie, 0, sizeof s->magie); memset(s->arene, 0, sizeof s->arene); return 1; }   /* ESVG : sans la magie apprise */
    if (read_old(s, n, SAVE_MAGICF, offsetof(save_t, garage))) { memset(&s->garage, 0, sizeof s->garage); memset(&s->magie, 0, sizeof s->magie); memset(s->arene, 0, sizeof s->arene); return 1; }   /* ESVF : sans véhicule */
    if (!read_avant_f(s, n)) return 0;
    memset(s->tele_vu, 0, sizeof s->tele_vu); s->conduite = SV_ANCIENNE;   /* d'avant ESVF : lieux visités reconstitués (load_partie) */
    memset(s->mix, 0, sizeof s->mix);   /* ni mixage : celui en cours reste */
    memset(&s->garage, 0, sizeof s->garage);
    memset(&s->magie, 0, sizeof s->magie);   /* d'avant ESVH : aucune magie apprise */
    memset(s->arene, 0, sizeof s->arene);
    return 1;
}
/* Sauvegarde d'avant ESVF : ESVE (sans les lieux visités ni la conduite), puis les plus anciennes */
static int read_avant_f(save_t *s, int n) {
    if (read_old(s, n, SAVE_MAGICE, offsetof(save_t, tele_vu))) return 1;
    int lues = 0;   /* recrues lues ; les autres comme au départ (job, objets portés), PV et PM au maximum (load_partie) */
    if (read_old(s, n, SAVE_MAGICD, offsetof(save_t, rec) + sizeof(sv_recrues5_t))) {   /* ESVD : cinq places, JADE à AZUR */
        static sv_recrues5_t r5;
        memcpy(&r5, &s->rec, sizeof r5);
        s->rec.team = r5.team; s->rec.recrues = r5.recrues; s->rec.impose = r5.impose;
        for (int k = 0; k < 5; k++) {
            s->rec.hp[k] = r5.hp[k]; s->rec.mp[k] = r5.mp[k]; s->rec.tenue[k] = r5.tenue[k];
            s->rec.job[k] = r5.job[k]; s->rec.jobs_known[k] = r5.jobs_known[k]; memcpy(s->rec.eq[k], r5.eq[k], 3);
        }
        lues = 5;
    } else if (read_ancien(s, n)) {   /* avant ESVD : l'équipe des quatre, la bande aussi */
        s->rec.team = (uint16_t)((s->team & 15) | 1); s->rec.recrues = s->rec.team; s->rec.impose = 0;
    } else return 0;
    for (int k = lues; k < SAVE_RECRUES; k++) {
        const int i = 4 + k < NMEMB ? 4 + k : 0;
        s->rec.hp[k] = s->rec.mp[k] = 0x7FFFFFFF; s->rec.tenue[k] = -1;
        s->rec.job[k] = i ? E.w.job0[i] : EQ_AUCUN; s->rec.jobs_known[k] = i ? E.w.jobs0[i] : 0;
        memset(s->rec.eq[k], EQ_AUCUN, 3); if (i) memcpy(s->rec.eq[k], E.w.eq0[i], 3);
    }
    return 1;
}
static int read_ancien(save_t *s, int n) {
    if (read_old(s, n, SAVE_MAGICC, offsetof(save_t, rec))) return 1;   /* ESVC : sans les recrues (sa somme de contrôle à leur place) */
    const int vb = read_old(s, n, SAVE_MAGICB, offsetof(save_t, cof_ouv));   /* ESVB : sans les coffres (sa somme de contrôle à leur place) */
    memset(s->cof_ouv, 0, sizeof s->cof_ouv); memset(s->tres_ouv, 0, sizeof s->tres_ouv);   /* avant ESVC : aucun coffre fouillé */
    if (vb) return 1;
    if (read_old(s, n, SAVE_MAGICA, offsetof(save_t, carte))) { s->carte = 0; return 1; }   /* ESVA : sur la carte 0 */
    /* ESV9 (sans l'héroïne), ESV8 (sans le guidage), ESV7 (sans les jobs) et ESV6 (sans les tenues) : ce qui manque prend
       les valeurs du départ ; partie 1 (emplacements 0 et 1) */
    if (n < 8) return 0;
    const int v9 = read_old(s, n, SAVE_MAGIC9, offsetof(save_t, heroine));
    if (!v9) {
        if (read_old(s, n, SAVE_MAGIC8, offsetof(save_t, guide))) { /* jobs lus */ }
        else if (read_old(s, n, SAVE_MAGIC7, offsetof(save_t, job))) { /* tenues lues */ }
        else if (read_old(s, n, SAVE_MAGIC6, offsetof(save_t, tenue))) memset(s->tenue, -1, sizeof s->tenue);
        else return 0;
        if (s->magic != SAVE_MAGIC8) { memcpy(s->job, E.w.job0, sizeof s->job); memcpy(s->jobs_known, E.w.jobs0, sizeof s->jobs_known); memcpy(s->eq, E.w.eq0, sizeof s->eq); }
        s->guide = -1;
    }
    s->heroine[0] = 0; s->temps = s->ordre = 0; s->carte = 0;
    lieu_de(s->x, s->z, s->lieu, sizeof s->lieu);
    return 1;
}

int hud_partie_info(int p, partie_info_t *o) {
    save_t *const s = (save_t *)SV_BLOC;   /* (la tête du bloc de la sauvegarde : le brouillard qui suit n'est pas touché) */
    memset(o, 0, sizeof *o);
    if (!s || !partie_lire(p, s)) return 0;
    o->seq = s->seq; o->ordre = s->ordre; o->niveau = s->level; o->temps = (float)s->temps;
    memcpy(o->heroine, s->heroine, sizeof o->heroine); o->heroine[15] = 0;
    memcpy(o->lieu, s->lieu, sizeof o->lieu); o->lieu[23] = 0;
    return 1;
}
/* Nouvelle partie dans la partie p : ses sauvegardes suivantes prennent la suite de ses numéros (la plus récente
   l'emporte) ; l'ancienne reste jusqu'à la première */
static void partie_depart(void);
void hud_nouvelle(int p) {
    partie_depart();   /* tout l'état du départ d'abord : on peut venir d'une partie quittée */
    save_t *const s = (save_t *)SV_BLOC;   /* (la tête du bloc de la sauvegarde) */
    E.partie = p; H.seq = s && partie_lire(p, s) ? (int)s->seq : 0;
    E.temps_jeu = 0;
    police_reset();   /* ni recherche ni poursuite */
    memset(E.cof_ouv, 0, sizeof E.cof_ouv); memset(E.tres_ouv, 0, sizeof E.tres_ouv); E.coffre = -1;   /* aucun coffre fouillé */
    panneau_guide_arreter();   /* ni guidage d'un panneau de quêtes */
    memset(E.tele_vu, 0, sizeof E.tele_vu);   /* aucun lieu visité (le départ le sera tout de suite : tele_maj) */
    brouillard_effacer();   /* carte du menu : rien de vu (le départ le sera tout de suite) */
    garage_reset();   /* pas de véhicule à elle */
    E.arene_gagnes = 0;   /* arène : seul FER est ouvert */
    memset(E.appris, 0, sizeof E.appris); memset(E.apts, 0, sizeof E.apts);   /* aucune magie apprise (cristaux du Chœur) */
    memset(E.eq_el, 0, sizeof E.eq_el);   /* rien de forgé */
    pilote_arreter(NULL);
    hud_sauve_repartir();   /* sauvegarde automatique : rien en attente */
}
/* Nouvelle partie commencée (accueil.c, jouer) : son premier point de sauvegarde, l'état du départ (REPRENDRE après une
   défaite y ramène tant qu'il n'y en a pas d'autre) */
void hud_sauve_debut(void) { if (E.cfg.save_write) save_ecrire(1); hud_sauve_repartir(); }

static void tele_ancien(void);   /* lieux visités d'une sauvegarde d'avant ESVF (plus bas, avec le téléport) */
static void brouillard_ancien(void);   /* brouillard d'une sauvegarde d'avant ESVI (plus bas, avec le téléport) */
static const char *tele_refus(void);   /* téléport refusé maintenant : pourquoi (plus bas, avec la carte du menu) */
/* Brouillard d'une sauvegarde ESVI de l'emplacement slot : le fichier entier relu dans le bloc (le brouillard y vit), sa
   somme de contrôle, et fait avec ce paquet (brouillard_accepter) ; 0 sinon (rien de vu). esvj : sauvegarde ESVJ, plus courte
   des éléments de la forge (lue décalée d'autant : son brouillard tombe à sa place dans le bloc) */
static int brouillard_lire(int slot, int esvj) {
    const uint32_t d = esvj ? (uint32_t)sizeof(((save_t *)0)->eq_el) : 0, nb = brouillard_taille(), ntot = (uint32_t)sizeof(save_t) - d + nb;
    if (!SV_BLOC || !E.cfg.save_read) return 0;
    const int n = E.cfg.save_read(slot, SV_BLOC + d, (int)ntot);
    const uint8_t *br = SV_BLOC + sizeof(save_t);
    uint32_t c = 0;
    if (n == (int)ntot) memcpy(&c, br + nb - 4, 4);
    if (n != (int)ntot || c != crc32(br, (int)nb - 4)) { brouillard_effacer(); return 0; }
    return brouillard_accepter(nb);
}
/* Réglages communs (plus bas : reglages_garder) */
#define REGL_OCT 16
static uint8_t regl_vu[REGL_OCT];   /* derniers écrits, lus ou appliqués : un réglage qui en diffère est à garder */
static int regl_ok;                 /* un enregistrement commun existe */
static void regl_octets(uint8_t *r);
static void regl_appliquer(const uint8_t *r, int lum);
/* Applique une sauvegarde : tout, sauf le brouillard et les réglages communs (load_partie, partie_depart) */
static void partie_appliquer(save_t *s, int p) {
    E.partie = p; H.seq = (int)s->seq;
    s->heroine[15] = 0; E.heroine = heroine_de(s->heroine);   /* avant les tenues : elles sont à elle */
    ile_equipe(0);   /* île : ses compagnes et leurs fiches, avant le niveau, les PV et l'équipe de la sauvegarde */
    ile_rodeurs_reset();   /* île : ni rôdeur ni nuit commencée */
    ile_offrande_reset();  /* ni séquence d'une succube */
    ile_enleve_lire(s->libre);   /* l'habitant enlevé de la partie (13/11/2026 : save_t.libre ; des zéros : personne) */
    E.temps_jeu = (float)s->temps;
    int_reset();   /* on reprend dans la rue, sans alarme */
    police_reset();   /* ni recherche ni poursuite (pas sauvegardées) */
    ride_lacher();   /* à pied, comme à la sauvegarde (en conduisant, Sera restait dans le véhicule) */
    if (s->carte < (uint32_t)E.w.ncartes) world_carte((int)s->carte, s->x, s->z);   /* sur sa carte */
    E.sera.x = s->x; E.sera.z = s->z; E.sera.dir = s->dir; E.yaw = s->yaw;
    garage_lire(&s->garage);   /* véhicule de Sera : à sa place, Sera au volant si elle y était (garage_maj) */
    memcpy(E.appris, s->magie.appris, sizeof E.appris);   /* magie apprise (ESVH ; avant : aucune) */
    for (int i = 0; i < NMEMB; i++) for (int k = 0; k < MAX_SORTS; k++) E.apts[i][k] = (uint8_t)(s->magie.pts[i][k >> 1] >> (4 * (k & 1)) & 15);
    E.theme = s->theme % 5; E.fog_on = s->fog & 1; E.volant = (s->fog >> 1 & 7) ? ((s->fog >> 1 & 7) - 1) % 5 : VOLANT_DEFAUT; E.dither_on = s->dither; H.bright = (s->bright & 7) % 5; E.renc_mode = (s->bright >> 4 & 7) % 5; E.essai = s->bright >> 7; E.eveil_essai = E.essai && (s->bright >> 3 & 1);
    for (int i = 0; i < n_items; i++) ITEMS[i].count = s->items[i];
    for (int i = 0; i < NMEMB; i++) {   /* les quatre premiers à leur place, les recrues dans leur bloc (ESVE) */
        const int j = i < 4 ? s->job[i] : s->rec.job[i - 4];
        const uint8_t *e = i < 4 ? s->eq[i] : s->rec.eq[i - 4];
        E.job[i] = j < E.w.njobs ? (uint8_t)j : EQ_AUCUN; E.jobs_known[i] = i < 4 ? s->jobs_known[i] : s->rec.jobs_known[i - 4];
        for (int k = 0; k < 3; k++) E.eq[i][k] = eq_slot(e[k]) == k ? e[k] : EQ_AUCUN;
    }
    party_set_level(s->level); E.exp = s->exp; E.credits = s->credits;
    /* Courbe du 02/11/2026 (exp_next) : une partie d'avant garde son niveau ; son expérience en cours, faite pour l'ancienne
       courbe (3 à 7 fois plus longue au début), est bornée juste sous le niveau suivant : une victoire au plus avant lui,
       jamais plusieurs niveaux d'un coup (loadPartie de l'émulateur) */
    if (E.exp < 0) E.exp = 0;
    if (E.level < NIV_MAX && E.exp >= exp_next(E.level)) E.exp = exp_next(E.level) - 1;
    for (int i = 0; i < NMEMB; i++) {   /* PV et PM gardés d'un combat à l'autre (02/11/2026) : ceux de la sauvegarde */
        const int32_t hp = i < 4 ? s->hp[i] : s->rec.hp[i - 4], mp = i < 4 ? s->mp[i] : s->rec.mp[i - 4];
        PARTY[i].hp = hp < PARTY[i].max ? hp : PARTY[i].max;
        if (PARTY[i].hp < 0) PARTY[i].hp = 0;   /* à terre à la sauvegarde : à terre au chargement */
        PARTY[i].mp = mp < PARTY[i].maxmp ? mp : PARTY[i].maxmp;
        if (PARTY[i].mp < 0) PARTY[i].mp = 0;
    }
    E.team = s->rec.team; E.recrues = s->rec.recrues; E.impose = s->rec.impose; equipe_valider();   /* au combat : de la bande, quatre au plus */
    E.obj[0] = s->obj[0]; E.obj[1] = s->obj[1];
    memcpy(E.quests, s->quests, sizeof E.quests); E.qtrack = s->qtrack; E.mus_force = s->mus_force; H.nann = 0;
    memcpy(E.flags, s->flags, sizeof E.flags); memcpy(E.ev_done, s->done, sizeof E.ev_done); script_after_load();
    for (int i = 0; i < NMEMB; i++) { const int t = i < 4 ? s->tenue[i] : s->rec.tenue[i - 4]; E.tenue_eq[i] = (int8_t)(tenue_mine(i, t) ? t : -1); }
    E.guide = s->guide >= 0 && s->guide < E.w.nguides ? s->guide : -1;
    memcpy(E.cof_ouv, s->cof_ouv, sizeof E.cof_ouv); memcpy(E.tres_ouv, s->tres_ouv, sizeof E.tres_ouv); E.coffre = -1;   /* coffres fouillés */
    panneau_guide_arreter();   /* le guidage d'un panneau de quêtes n'est pas sauvegardé */
    pilote_arreter(NULL);
    if (s->magic == SAVE_MAGIC || s->magic == SAVE_MAGICJ) { memcpy(E.best_vu, s->best_vu, sizeof E.best_vu); memcpy(E.best_bat, s->best_bat, sizeof E.best_bat); memcpy(E.eq_up, s->eq_up, sizeof E.eq_up); memcpy(E.veh_up, s->veh_up, sizeof E.veh_up); }   /* bestiaire, équipement amélioré */
    else { memset(E.best_vu, 0, sizeof E.best_vu); memset(E.best_bat, 0, sizeof E.best_bat); memset(E.eq_up, 0, sizeof E.eq_up); memset(E.veh_up, 0, sizeof E.veh_up); }   /* d'avant ESVJ : rien de vu, rien d'amélioré */
    if (s->magic == SAVE_MAGIC) memcpy(E.eq_el, s->eq_el, sizeof E.eq_el); else memset(E.eq_el, 0, sizeof E.eq_el);   /* éléments de la forge (ESVK) */
    E.arene_gagnes = s->arene[0] & ((1 << ARN_RANGS) - 1);   /* arène : rangs gagnés (à zéro avant ESVI) */
    if (s->conduite == SV_ANCIENNE) { E.conduite = CONDUITE_DEFAUT; tele_ancien(); }   /* d'avant ESVF : après les drapeaux et les quêtes */
    else {
        E.conduite = s->conduite & 1; memcpy(E.tele_vu, s->tele_vu, sizeof E.tele_vu);
        E.tele_tout = s->conduite >> 3 & 1;   /* code CODE_TELE */
        E.zoom_libre = s->conduite >> 4 & 1; E.zoom_lieu = 0;   /* code CODE_ZOOM */
        if (s->conduite >> 1 & 3) E.commandes = ((s->conduite >> 1 & 3) - 1) % 3;
    }
    mix_lire(s->mix);   /* SYSTÈME › SON */
}
static int load_partie(int p) {
    E.essai_mesure = 0;   /* une partie rechargée : de nouveau sauvegardée */
    save_t *s = (save_t *)SV_BLOC;   /* la tête du bloc de la sauvegarde (le brouillard la suit : relu à la fin) */
    if (!s || !partie_lire(p, s)) { toast("Aucune sauvegarde valide"); return 0; }
    uint8_t regl_avant[REGL_OCT];
    regl_octets(regl_avant);   /* réglages communs en cours : ils restent (plus bas) */
    partie_appliquer(s, p);
    /* Brouillard de la carte du menu (ESVI, à la suite) ; d'avant, ou fait avec un autre paquet : reconstitué au mieux,
       après les lieux visités et les quêtes (brouillard_ancien). En dernier : la relecture du fichier entier repasse sur la
       tête du bloc (s, les mêmes octets) */
    const unsigned seq = (unsigned)s->seq;
    if ((s->magic != SAVE_MAGIC && s->magic != SAVE_MAGICJ) || !brouillard_lire(p * 2 + (int)(seq & 1), s->magic == SAVE_MAGICJ)) brouillard_ancien();
    if (regl_ok) regl_appliquer(regl_avant, 0);   /* réglages communs (02/11/2026) : ceux en cours restent, pas ceux de la partie */
    else regl_octets(regl_vu);                    /* (sans eux : ceux de la partie, en vigueur, n'ont pas à être gardés) */
    E.has_dest = 0;
    if (E.cfg.set_brightness) E.cfg.set_brightness(64 + H.bright * 48);
    hud_sauve_repartir();   /* sauvegarde automatique : rien en attente, la carte et le lieu d'ici déjà vus */
    char t[48]; snprintf(t, sizeof t, "Partie %d : sauvegarde n° %u chargée", p + 1, seq);
    toast(t);
    return 1;
}
static void load_game(void) { load_partie(E.partie); }
/* Fin de eng_init, avant l'écran titre : l'état du départ de toute nouvelle partie */
void hud_depart_garder(void) { if (SV_DEPART) { sv_remplir(SV_DEPART); SV_DEPART->magic = SAVE_MAGIC; } }
static void partie_depart(void) {
    E.essai_mesure = 0;
    if (!SV_DEPART || !SV_DEPART->magic) return;
    uint8_t regl_avant[REGL_OCT];
    regl_octets(regl_avant);   /* réglages communs en cours : ils restent, comme au chargement */
    partie_appliquer(SV_DEPART, E.partie);
    if (regl_ok) regl_appliquer(regl_avant, 0);
    else regl_octets(regl_vu);
    script_repartir();   /* l'événement du début se jouera (un chargement l'exclut) */
    E.has_dest = 0; H.nann = 0;
}
int hud_charger(int p) { return load_partie(p); }

/* ---------- Actions ---------- */
static int items_count(void) { int n = 0; for (int i = 0; i < n_items; i++) n += ITEMS[i].count; return n; }
/* Objets du menu : ceux qu'on a ; leur nombre, l'index dans ITEMS de la ligne row (-1 : aucun) */
/* Objets du sac montrés : ceux qu'on a (un objet 18+ caché tant que le 18+ est verrouillé, item_vu) */
static int owned_count(void) { int n = 0; for (int i = 0; i < n_items; i++) n += ITEMS[i].count > 0 && item_vu(&ITEMS[i]); return n; }
static int owned_item(int row) { for (int i = 0; i < n_items; i++) if (ITEMS[i].count > 0 && item_vu(&ITEMS[i]) && row-- == 0) return i; return -1; }
/* Membres de l'équipe au combat : leur nombre, leurs techniques ; ceux de la bande (au combat et en réserve : menus ÉQUIP.,
   STATUT et ÉQUIPE) : leur nombre, l'index dans PARTY du k-ième */
static int team_count(void) { return equipe_nombre(); }
static int band_count(void) { int n = 0; for (int i = 0; i < NMEMB; i++) n += in_band(i); return n; }
static int band_at(int k) { for (int i = 0; i < NMEMB; i++) if (in_band(i) && k-- == 0) return i; return 0; }
/* Qui peut porter l'objet k, tenue du sac ou armure qui se voit (sa tenue : bikinis) : « Pour SERA, JADE » (membres de la
   bande), sinon « Pour AZUR, hors de la bande », sinon « Personne ne la porte » ; renvoie le nombre de membres de la bande
   qui le peuvent, -1 si k ne porte pas de tenue (03/11/2026, demande de l'utilisateur : une tenue achetée que Sera ne
   pouvait pas mettre ; porteurs de l'émulateur) */
static int porteurs(int k, char *out, int cap) {
    out[0] = 0;
    if (k < 0 || k >= n_items || !ITEMS[k].tenue) return -1;
    int nb = 0, nh = 0, l = snprintf(out, cap, "Pour ");
    for (int pass = 0; pass < 2 && !nb; pass++)   /* la bande d'abord ; personne : les autres membres */
        for (int m = 0; m < NMEMB && l < cap; m++) {
            if (in_band(m) != !pass || tenue_named(m, ITEMS[k].tenue) < 0) continue;
            l += snprintf(out + l, cap - l, "%s%s", nb + nh ? ", " : "", PARTY[m].name);
            if (pass) nh++; else nb++;
        }
    if (!nb && nh && l < cap) snprintf(out + l, cap - l, ", hors de la bande");
    else if (!nb) snprintf(out, cap, "Personne ne la porte");
    return nb;
}
/* Lignes du menu TECHNIQ. : pour chaque membre de l'équipe, ses techniques puis ses sorts (job, équipement) */
static int team_techs(void) { int n = 0, l[MAX_SORTS]; for (int i = 0; i < NMEMB; i++) if (in_team(i)) n += PARTY[i].ntech + sorts_of(i, l); return n; }
/* Icône d'un objet du sac dans une liste : l'image de l'équipement, sinon son icône 16 × 16 */
static int item_icon(int i) { const eqitem_t *r = eq_item(i); return r && r->img ? ICON_OBJG + r->img : ITEMS[i].icon; }
/* ---------- Menu ÉQUIP. (refait le 01/11/2026 ; equipeM… de l'émulateur, à garder identiques) ----------
   Quatre niveaux (EQM.niv) : EQN_MEMBRES, les membres de la bande (en réserve compris : l'équiper avant de le prendre ;
   sauté quand Sera est seule) ; EQN_PLACES, les emplacements du membre EQM.m (equip_places) ; EQN_LISTE, les équipements
   de l'emplacement EQM.sl (equip_liste) et l'écart de chacun avec ce qui est porté ; EQN_FICHE, la fiche de comparaison
   avec l'objet EQM.it (EQ_AUCUN : retirer) et « ÉQUIPER ? » (ligne 0 OUI, 1 NON). r[niveau] : la ligne choisie de chaque
   niveau, retrouvée au retour (RETOUR remonte d'un niveau) */
enum { ER_JOB = -1, ER_TENUE = 3 };
enum { EQN_MEMBRES, EQN_PLACES, EQN_LISTE, EQN_FICHE };
static struct { int niv, m, sl, it, r[3]; } EQM;
static const char *const SLOT_NAME[3] = { "ARME", "ARMURE", "ACCESS." };
static const char *const AFF_NAME[5] = { "—", "C", "B", "A", "S" };
/* Emplacements du membre m : ER_JOB (avec les jobs de l'Atelier), arme, armure, accessoire (EQ_*, avec les types
   d'équipement), ER_TENUE (s'il en a, ou sans jobs : la ligne d'origine) ; renvoie leur nombre */
static int equip_places(int m, int *out) {
    const int eq = E.w.neqtypes > 0;
    int ns = 0;
    if (eq && E.w.njobs) out[ns++] = ER_JOB;
    if (eq) { out[ns++] = EQ_ARME; out[ns++] = EQ_ARMURE; out[ns++] = EQ_ACCESS; }
    if (!eq || has_tenues(m)) out[ns++] = ER_TENUE;
    return ns;
}
/* Liste de l'emplacement (EQL) : l'objet porté et RETIRER (EQ_AUCUN) s'il y en a un, puis les équipements du sac de cet
   emplacement que le job du membre peut porter, du plus haut palier au plus bas (l'ordre du sac à palier égal), puis ceux
   qu'il ne peut pas porter (grisés) ; renvoie leur nombre */
static int EQL[MAX_ITEMS + 2] ENG_FROID;
static int equip_liste(void) {
    const int m = EQM.m, sl = EQM.sl;
    if (sl < 0 || sl >= 3) return 0;
    const int cur = E.eq[m][sl];
    int n = 0;
    if (cur != EQ_AUCUN) { EQL[n++] = cur; EQL[n++] = EQ_AUCUN; }
    for (int pass = 0; pass < 2; pass++) {
        const int n0 = n;
        for (int i = 0; i < n_items && n < MAX_ITEMS + 2; i++)
            if (i != cur && ITEMS[i].count > 0 && item_vu(&ITEMS[i]) && eq_slot(i) == sl && (eq_aff(m, i) > 0) == !pass) EQL[n++] = i;
        for (int a = n0 + 1; a < n; a++) {   /* tri par insertion : palier décroissant */
            const int v = EQL[a];
            int b = a - 1;
            while (b >= n0 && E.w.eq_pal[EQL[b]] < E.w.eq_pal[v]) { EQL[b + 1] = EQL[b]; b--; }
            EQL[b + 1] = v;
        }
    }
    return n;
}
/* Statistiques du membre m avec l'objet it (EQ_AUCUN : rien) à l'emplacement sl, dans l'ordre ES_* (ATQ, DÉF, PV max,
   PM max, VIT, MAG), et ses sorts (bit k : sort k du catalogue) : party_refresh et sorts_of sur l'équipement changé, puis
   tout remis. L'écart affiché est la vraie différence, affinité du job comprise (eqStats de l'émulateur) */
enum { ES_ATQ, ES_DEF, ES_PV, ES_PM, ES_VIT, ES_MAG, ES_N };
static const char *const ES_NOM[ES_N] = { "ATQ", "DÉF", "PV", "PM", "VIT", "MAG" };
#define EQ_MIEUX 0x3CE07A   /* écart : vert si mieux, rouge si moins bien */
#define EQ_MOINS 0xFF3B4E
static uint64_t eq_stats(int m, int sl, int it, int *st) {
    const member_t garde = PARTY[m];
    const uint8_t e = E.eq[m][sl];
    int l[MAX_SORTS];
    uint64_t b = 0;
    E.eq[m][sl] = (uint8_t)it; party_refresh(m);
    const member_t *p = &PARTY[m];
    st[ES_ATQ] = p->atk; st[ES_DEF] = p->def; st[ES_PV] = p->max; st[ES_PM] = p->maxmp; st[ES_VIT] = p->spd; st[ES_MAG] = p->mag;
    const int ns = sorts_of(m, l);
    for (int k = 0; k < ns; k++) b |= 1ull << l[k];
    E.eq[m][sl] = e; PARTY[m] = garde;
    return b;
}
static void equip_ouvrir(void) {   /* menu ÉQUIP. ouvert : les membres, ou les emplacements de Sera quand elle est seule */
    memset(&EQM, 0, sizeof EQM);
    EQM.niv = band_count() > 1 ? EQN_MEMBRES : EQN_PLACES; EQM.it = EQ_AUCUN;
}
static void equip_niveau(int niv, int sel) {   /* au niveau niv, sur la ligne sel */
    EQM.niv = niv; H.sub_sel = sel; H.sub_scroll = sel - ROWS + 1 > 0 ? sel - ROWS + 1 : 0; H.fling = 0;
}
static int equip_lignes(void) {
    int pl[5];
    switch (EQM.niv) {
    case EQN_MEMBRES: return band_count();
    case EQN_PLACES: return equip_places(EQM.m, pl);
    case EQN_LISTE: return equip_liste();
    default: return 2;   /* OUI, NON */
    }
}
/* OUI : l'objet de la fiche porté (l'ancien retourne au sac), retour aux emplacements ; NON : retour à la liste
   (equipeConfirmer de l'émulateur) */
static void equip_confirmer(int non) {
    char t[80];
    if (non) { equip_niveau(EQN_LISTE, EQM.r[EQN_LISTE]); son_jouer(SON_ANNULER, 0.4f, 1); return; }
    const int m = EQM.m, sl = EQM.sl, cur = E.eq[m][sl], nx = EQM.it;
    if (nx != EQ_AUCUN && (ITEMS[nx].count <= 0 || !eq_aff(m, nx))) { toast("Plus dans le sac"); equip_niveau(EQN_LISTE, 0); return; }
    if (cur != EQ_AUCUN) ITEMS[cur].count++;
    if (nx != EQ_AUCUN) ITEMS[nx].count--;
    E.eq[m][sl] = (uint8_t)nx; party_refresh(m);
    if (nx != EQ_AUCUN && ITEMS[nx].tenue) E.tenue_eq[m] = -1;   /* armure qui se voit : on la voit (la tenue choisie au menu s'efface) */
    if (nx != EQ_AUCUN) snprintf(t, sizeof t, "%s porte %s", PARTY[m].name, ITEMS[nx].name);
    else snprintf(t, sizeof t, "%s : %s au sac", PARTY[m].name, cur != EQ_AUCUN ? ITEMS[cur].name : SLOT_NAME[sl]);
    toast(t); son_jouer(SON_VALIDER, 0.45f, 1);
    equip_niveau(EQN_PLACES, EQM.r[EQN_PLACES]);
}
/* Quêtes du menu : la suivie, celles en cours, puis les terminées ; renvoie leur nombre (rows : leurs index, ou NULL) */
/* ---------- JOURNAL (06/11/2026) : les Échos entendus (ECHS : vies, événements) et le bestiaire ---------- */
static const uint8_t *ECHS; static int ECHS_N;
static const uint8_t *CHSS; static int CHSS_N;   /* grandes chasses : nom[24], indice[48], événement u16 (74 octets) */
static void echs_lire(void) { uint32_t sz; const uint8_t *p = pak_section("ECHS", &sz); ECHS = p && sz >= 2 && p[0] == 1 ? p + 2 : NULL; ECHS_N = ECHS ? p[1] : 0;
    p = pak_section("CHSS", &sz); CHSS = p && sz >= 2 && p[0] == 1 ? p + 2 : NULL; CHSS_N = CHSS ? p[1] : 0; }
#define CHSS_EV(k) (CHSS[74 * (k) + 72] | CHSS[74 * (k) + 73] << 8)
/* La ligne r du tableau des chasses : celles dont le rang est ouvert */
static int chss_ligne(int r) { for (int k = 0; k < CHSS_N; k++) if (script_ev_ouvert(CHSS_EV(k)) && r-- == 0) return k; return -1; }
static int chss_nb(void) { int n = 0; for (int k = 0; k < CHSS_N; k++) n += script_ev_ouvert(CHSS_EV(k)); return n; }
static int chss_faites(void) { int n = 0; for (int k = 0; k < CHSS_N; k++) n += script_ev_fait(CHSS_EV(k)); return n; }
/* La vie k : son nom (24 octets) ; n Échos ; leurs événements (u16, à la suite) */
static const uint8_t *echs_vie(int k) { const uint8_t *q = ECHS; for (int i = 0; i < k; i++) q += 26 + 2 * q[24]; return q; }
static int echs_entendus(int k) { const uint8_t *q = echs_vie(k); int n = 0; for (int i = 0; i < q[24]; i++) n += script_ev_fait(q[26 + 2 * i] | q[27 + 2 * i] << 8); return n; }
static int echs_total(void) { int n = 0; for (int k = 0; k < ECHS_N; k++) n += echs_vie(k)[24]; return n; }
static int echs_tous(void) { int n = 0; for (int k = 0; k < ECHS_N; k++) n += echs_entendus(k); return n; }
static int best_nb(void) { int n = 0; for (int k = 0; k < E.w.nenemies; k++) n += E.best_vu[k >> 3] >> (k & 7) & 1; return n; }
/* La ligne r du bestiaire : l'ennemi vu de ce rang (-1 : aucun) */
static int best_ligne(int r) { for (int k = 0; k < E.w.nenemies; k++) if ((E.best_vu[k >> 3] >> (k & 7) & 1) && r-- == 0) return k; return -1; }
static int jour_lignes(void) { return H.jniv == 0 ? 3 : H.jniv == 1 ? ECHS_N : H.jniv == 2 ? echs_entendus(H.jvie) : H.jniv == 4 ? chss_nb() : best_nb(); }
/* La k-ième voix entendue de la vie : son événement (-1 : aucune) */
static int jour_voix(int r) { const uint8_t *q = echs_vie(H.jvie); for (int i = 0; i < q[24]; i++) { const int ev = q[26 + 2 * i] | q[27 + 2 * i] << 8; if (script_ev_fait(ev) && r-- == 0) return ev; } return -1; }
static int quest_rows(int *rows) {
    int n = 0;
    for (int pass = 0; pass < 3; pass++)
        for (int q = 0; q < script_quest_count(); q++) {
            const int st = E.quests[q];
            if (!st || (pass == 0) != (q == E.qtrack) || (pass == 1 && (q == E.qtrack || st == 255)) || (pass == 2 && st != 255)) continue;
            if (rows) rows[n] = q;
            n++;
        }
    return n;
}
/* ---------- Guidage : villes ou bâtiments spéciaux (section GUID), du plus proche au plus loin (guideRows de l'émulateur) ----------
   Les lieux de la carte où est Sera (01/11/2026 : avant, ceux de la planète seulement) : ailleurs, ceux de la carte (lieux
   nommés, bâtiments nommés, puits, aire d'envol), et sur le système solaire les astres (GUIDE_ASTRES) ; le lieu suivi est
   celui de sa carte (guide_ici) : ailleurs il ne se montre pas, et reprend au retour */
#define GUIDE_RGB 0x5DFF8A       /* chevron et repère du guidage, à côté de ceux de la quête (teinte 2) */
#define GR_STOP (-3)
#define GR_PILOTE (-4)            /* arrêter le pilote automatique (pilote.c), en tête quand il conduit */
#define GR_FAM 0x8000             /* + groupe : villes (0), lieux (1), familles (2 + rang) */
#define GR_CAT 0x10000
/* Familles du catalogue des bâtiments, dans l'ordre de FAMILLES_GUIDE de l'émulateur (groupe 2 + rang) */
static const char *const FAM_GUIDE[13] = { "COMMERCES", "LOISIRS", "SERVICES", "ORDRE", "VÉHICULES", "INDUSTRIE", "SOUTERRAIN", "HABITAT",
                                           "ENCEINTE", "CAMPEMENTS", "AUTRES MONDES", "RUINES", "AUTRES" };
static const char *groupe_nom(int g) { return g == 0 ? "VILLES" : g == 1 ? "LIEUX" : g == GUIDE_ASTRES ? "ASTRES" : FAM_GUIDE[g - 2 < 13 ? g - 2 : 12]; }
static int GR[MAX_GUIDE + 2] ENG_FROID; static float GD[MAX_GUIDE + 2] ENG_FROID;   /* lignes du menu (codes GR_* ou lieu), distances */
static float guide_dist(int k) { return hypotf(E.w.guides[k].x - E.sera.x, E.w.guides[k].z - E.sera.z); }
/* Lieu suivi sur la carte où est Sera (guideIci de l'émulateur) */
static int guide_ici(void) { return E.guide >= 0 && E.guide < E.w.nguides && E.w.guides[E.guide].carte == E.w.carte; }
/* Ce que propose le menu sur cette carte : sous l'icône (court) ou en tête de la liste (guideAide de l'émulateur) */
static const char *guide_aide(int court) {
    return !E.w.carte ? (court ? "Ville ou bâtiment" : "Villes, lieux, bâtiments") : vol_permanent() ? (court ? "Astres" : "Astres du système") : "Lieux de la carte";
}
/* Même sorte : deux bâtiments du même groupe et du même nom */
static int same_cat(int a, int b) {
    const guide_t *g = E.w.guides;
    return g[a].groupe == g[b].groupe && !strcmp(g[a].nom, g[b].nom);
}
/* Nom d'un lieu : un bâtiment (de la planète) porte celui de la ville la plus proche (à moins de 60 cases), après sep */
static void guide_label(int k, const char *sep, const char *end, char *t, int n) {
    const guide_t *g = E.w.guides;
    int v = -1; float best = 60;
    if (g[k].kind == GUIDE_BAT && !g[k].carte)
        for (int i = 0; i < E.w.nguides; i++) {
            if (g[i].kind != GUIDE_VILLE) continue;
            const float d = hypotf(g[i].x - g[k].x, g[i].z - g[k].z);
            if (d < best) { best = d; v = i; }
        }
    if (v >= 0) snprintf(t, n, "%s%s%s%s", g[k].nom, sep, g[v].nom, end); else snprintf(t, n, "%s", g[k].nom);
}
static void dist_txt(float d, char *t, int n) {
    if (d < 1000) snprintf(t, n, "%d m", (int)lroundf(d));
    else { const int hm = (int)lroundf(d / 100); snprintf(t, n, "%d,%d km", hm / 10, hm % 10); }
}
/* Remplit GR et GD : au premier niveau, arrêter (si un lieu est suivi), puis les groupes (villes, lieux, familles :
   distance du plus proche) ; un groupe de villes ou de lieux montre ses lieux, une famille ses sortes (une ligne par nom),
   une sorte ses bâtiments. Groupes dans leur ordre, le reste du plus proche au plus loin. Renvoie le nombre de lignes
   (guideRows de l'émulateur) */
static int guide_rows(void) {
    const guide_t *g = E.w.guides;
    int n = 0, head = 0, groupes = H.gcat == -1;
    if (groupes) {
        if (pilote_actif()) { GR[n] = GR_PILOTE; GD[n] = -1; n++; }
        { float px_, pz_; if (guide_ici() || panneau_guide(&px_, &pz_)) { GR[n] = GR_STOP; GD[n] = -1; n++; } }   /* lieu suivi, ou guidage d'un panneau de quêtes */
        head = n;
        for (int k = 0; k < E.w.nguides; k++) {
            if (g[k].carte != E.w.carte) continue;   /* les lieux de la carte où est Sera */
            const float d = guide_dist(k);
            int f = -1;
            for (int i = head; i < n && f < 0; i++) if (GR[i] == GR_FAM + g[k].groupe) f = i;
            if (f < 0) { GR[n] = GR_FAM + g[k].groupe; GD[n] = d; n++; }
            else if (d < GD[f]) GD[f] = d;
        }
    } else if (H.gcat >= GR_FAM + 2 && H.gcat < GR_CAT && H.gcat != GR_FAM + GUIDE_ASTRES) {   /* une famille : ses sortes (les astres : leurs lieux, plus bas) */
        for (int k = 0; k < E.w.nguides; k++) {
            if (GR_FAM + g[k].groupe != H.gcat || g[k].carte != E.w.carte) continue;
            const float d = guide_dist(k);
            int f = -1;
            for (int i = 0; i < n && f < 0; i++) if (same_cat(GR[i] - GR_CAT, k)) f = i;
            if (f < 0) { GR[n] = GR_CAT + k; GD[n] = d; n++; }
            else if (d < GD[f]) GD[f] = d;
        }
    } else {   /* villes, lieux, ou les bâtiments d'une sorte */
        for (int k = 0; k < E.w.nguides; k++)
            if (g[k].carte == E.w.carte && (H.gcat >= GR_CAT ? same_cat(H.gcat - GR_CAT, k) : GR_FAM + g[k].groupe == H.gcat)) { GR[n] = k; GD[n] = guide_dist(k); n++; }
    }
    /* Tri par insertion : les groupes dans leur ordre, le reste du plus proche au plus loin */
#define GKEY(i) (groupes ? (float)GR[i] : GD[i])
    for (int i = head + 1; i < n; i++) {
        const int c = GR[i]; const float d = GD[i], key = groupes ? (float)c : d;
        int j = i - 1;
        while (j >= head && GKEY(j) > key) { GR[j + 1] = GR[j]; GD[j + 1] = GD[j]; j--; }
        GR[j + 1] = c; GD[j + 1] = d;
    }
#undef GKEY
    return n;
}
/* Arrivée : en deçà du rayon du lieu, sur sa carte, le guidage s'arrête */
static void guide_step(void) {
    if (E.guide < 0 || E.guide >= E.w.nguides) { E.guide = -1; return; }
    if (!guide_ici() || guide_dist(E.guide) >= E.w.guides[E.guide].r) return;
    char nm[48], t[80];
    guide_label(E.guide, " (", ")", nm, sizeof nm);
    snprintf(t, sizeof t, "Arrivée : %s", nm);
    toast(t);
    E.guide = -1;
}
/* Retour d'un écran du menu : des lieux d'une sorte à la liste des sortes, sinon à la roue */
/* Téléport (SYSTÈME › TÉLÉPORTER, section TELE de l'émulateur) : 36 octets par lieu : nom UTF-8 sur 24, sorte u8 (0 monde,
   1 ville, 2 lieu), carte u8, 0 u16, point x, z f32 (case praticable de sa carte) */
static const char *TELE_CAT[3] = { "MONDES", "VILLES", "LIEUX" };
static int TR[259] ENG_FROID;   /* lignes : -1 - sorte (liste des sortes), sinon rang du lieu dans TELE */
static const uint8_t *tele_sec(int *n) {
    uint32_t sz = 0;
    const uint8_t *s = pak_section("TELE", &sz);
    *n = s && sz >= 4 && 4 + (uint64_t)rd32(s) * 36 <= sz ? (int)rd32(s) : 0;
    if (*n > TELE_MAX) *n = TELE_MAX;
    return *n ? s + 4 : NULL;
}
/* Lieux visités (E.tele_vu ; seuls ceux-là sont proposés, 01/11/2026) : un MONDE, à l'arrivée sur sa carte ; une VILLE,
   quand Sera est à elle (lieu_point : dans son rayon d'arrivée, ou dans une zone urbaine dont elle est la ville la plus
   proche, à moins de AUTO_VILLE_R : le lieu de la sauvegarde automatique) ; un LIEU, à moins de TELE_VU_R cases de son
   point, sur sa carte. Deux fois par seconde (hud_sauve_maj), ni dans un intérieur ni en combat (teleMaj de l'émulateur) */
#define TELE_VU_R 8.0f
static int tele_vu(int k) { return k >= 0 && k < TELE_MAX && (E.tele_tout || (E.tele_vu[k >> 3] >> (k & 7) & 1)); }   /* code CODE_TELE : tous */
static void tele_voir(int k) { if (k >= 0 && k < TELE_MAX) E.tele_vu[k >> 3] |= (uint8_t)(1u << (k & 7)); }
/* Ville du guidage (GUID) d'une ville du téléport k : la plus proche de son point, à moins de 12 cases (-1 : aucune) */
static int16_t TVG[TELE_MAX] ENG_FROID;
static int tele_ville(int k) {
    if (!tvg_ok) {
        int nt;
        const uint8_t *p = tele_sec(&nt);
        for (int i = 0; i < TELE_MAX; i++) TVG[i] = -1;
        for (int i = 0; i < nt; i++) {
            float x, z, bd = 12;
            if (p[i * 36 + 24] != 1) continue;
            memcpy(&x, p + i * 36 + 28, 4); memcpy(&z, p + i * 36 + 32, 4);
            for (int g = 0; g < E.w.nguides; g++) {
                const float d = hypotf(E.w.guides[g].x - x, E.w.guides[g].z - z);
                if (E.w.guides[g].kind == GUIDE_VILLE && d < bd) { bd = d; TVG[i] = (int16_t)g; }
            }
        }
        tvg_ok = 1;
    }
    return k >= 0 && k < TELE_MAX ? TVG[k] : -1;
}
/* Lieux visités depuis (x, z) de la carte c */
static void tele_point(float x, float z, int c) {
    int nt;
    const uint8_t *p = tele_sec(&nt);
    const int ville = c == 0 ? lieu_point(x, z, zone_urbaine(x, z)) : -1;
    for (int k = 0; k < nt; k++) {
        const uint8_t *q = p + k * 36;
        float tx, tz;
        memcpy(&tx, q + 28, 4); memcpy(&tz, q + 32, 4);
        if (q[24] == 0 ? q[25] == c : q[24] == 1 ? ville >= 0 && tele_ville(k) == ville : q[25] == c && hypotf(tx - x, tz - z) < TELE_VU_R) tele_voir(k);
    }
}
/* ... et le brouillard de la carte du menu : les blocs autour d'elle (carte.c ; ni en mode course) */
static void tele_maj(void) {
    if (BT.on || E.in.on || accueil_on()) return;
    tele_point(E.sera.x, E.sera.z, E.w.carte);
    if (!carriere_on()) brouillard_voir(E.sera.x, E.sera.z, E.w.carte, BROU_R);
}
/* Sauvegarde d'avant ESVF : lieux visités reconstitués d'après le chemin de l'histoire jouée — la planète, les cartes dont
   le drapeau de visite (CART) est levé, puis ce qu'on voit depuis le départ de la planète, la position sauvegardée et la
   cible de chaque étape déjà passée des quêtes commencées (toutes celles d'une quête terminée) */
static void tele_ancien(void) {
    int nt;
    const uint8_t *p = tele_sec(&nt);
    memset(E.tele_vu, 0, sizeof E.tele_vu);
    for (int k = 0; k < nt; k++) {
        const int c = p[k * 36 + 25];
        if (p[k * 36 + 24] || c >= E.w.ncartes) continue;
        const int f = E.w.cartes[c].visite - 1;
        if (!c || (f >= 0 && f < SC_MAX_FLAGS && (E.flags[f >> 3] >> (f & 7) & 1))) tele_voir(k);
    }
    tele_point(E.w.cartes[0].sx, E.w.cartes[0].sz, 0);
    tele_point(E.sera.x, E.sera.z, E.w.carte);
    for (int q = 0; q < script_quest_count(); q++) {
        const int st = E.quests[q], fin = st == 255 ? script_quest_steps(q) : st - 1;   /* 255 : terminée */
        for (int k = 1; k <= fin; k++) { float x = 0, z = 0; int c = 0; if (script_step_cible(q, k, &x, &z, &c)) tele_point(x, z, c); }
    }
}
/* Brouillard de la carte du menu d'une sauvegarde d'avant ESVI (ou faite avec un autre paquet) : reconstitué d'après ce
   qu'on sait — autour de Sera, des lieux visités du téléport (une ville : jusqu'à son rayon d'arrivée) et des cibles des
   étapes passées des quêtes commencées, comme tele_ancien (brouillardAncien de l'émulateur) */
static void brouillard_ancien(void) {
    int nt;
    const uint8_t *p = tele_sec(&nt);
    brouillard_effacer();
    brouillard_voir(E.sera.x, E.sera.z, E.w.carte, BROU_R);
    for (int k = 0; k < nt; k++) {
        if (!(E.tele_vu[k >> 3] >> (k & 7) & 1)) continue;   /* (pas tele_vu : le code du téléport partout ne compte pas) */
        const uint8_t *q = p + k * 36;
        float x, z, r = BROU_R;
        memcpy(&x, q + 28, 4); memcpy(&z, q + 32, 4);
        if (q[24] == 1) { const int g = tele_ville(k); if (g >= 0 && E.w.guides[g].r > r) r = E.w.guides[g].r; }
        brouillard_voir(x, z, q[25], r);
    }
    for (int q = 0; q < script_quest_count(); q++) {
        const int st = E.quests[q], fin = st == 255 ? script_quest_steps(q) : st - 1;   /* 255 : terminée */
        for (int k = 1; k <= fin; k++) { float x = 0, z = 0; int c = 0; if (script_step_cible(q, k, &x, &z, &c)) brouillard_voir(x, z, c, BROU_R); }
    }
}
/* Système solaire (vol permanent) : la liste est celle du saut, les mondes où l'on se pose déjà visités (TR = SAUT0 + rang
   dans ASTR), dans l'ordre des astres (sautListe de l'émulateur) */
#define SAUT0 1000
static int tele_rows(void) {
    int nt, n = 0;
    if (vol_permanent()) {
        for (int i = 0; i < E.w.nastres && n < 256; i++)
            if (E.w.astres[i].carte == E.w.carte && E.w.astres[i].vers >= 0 && astre_visite(i)) TR[n++] = SAUT0 + i;
        return n;
    }
    const uint8_t *p = tele_sec(&nt);
    if (H.tcat < 0) {   /* les sortes qui ont un lieu visité */
        for (int c = 0; c < 3; c++) { int k = 0; for (int i = 0; i < nt; i++) k += p[i * 36 + 24] == c && tele_vu(i); if (k) TR[n++] = -1 - c; }
        return n;
    }
    for (int i = 0; i < nt && n < 256; i++) if (p[i * 36 + 24] == H.tcat && tele_vu(i)) TR[n++] = i;
    return n;
}
int hud_tele_vus(char *out, int n) {
    int nt, k[3] = { 0 }, v[3] = { 0 }, o;
    const uint8_t *p = tele_sec(&nt);
    for (int i = 0; i < nt; i++) if (p[i * 36 + 24] < 3) { k[p[i * 36 + 24]]++; v[p[i * 36 + 24]] += tele_vu(i); }
    o = snprintf(out, n, "mondes %d/%d, villes %d/%d, lieux %d/%d :", v[0], k[0], v[1], k[1], v[2], k[2]);
    for (int i = 0; i < nt && o > 0 && o < n - 30; i++) if (tele_vu(i)) o += snprintf(out + o, n - o, " %.24s ;", (const char *)p + i * 36);
    return v[0] + v[1] + v[2];
}
/* Galerie (SYSTÈME › GALERIE) : les images du jeu (IMAG) par catégorie ; l'image de la ligne choisie derrière la liste,
   en grand quand on la touche (un toucher ou RETOUR la referme). Transformations : une fois l'éveil appris */
enum { GAL_CUT, GAL_PLEIN, GAL_DOS, GAL_DEGATS, GAL_TRANSFO, GAL_SCENES, GAL_INTRO, GAL_N };
static const char *GAL_CAT[GAL_N] = { "CUT-INS", "PLEIN ÉCRAN", "FACE / DOS", "DÉGÂTS", "TRANSFORMATIONS", "SCÈNES", "INTRODUCTION" };
#define GAL_MAX 600
static int GL[GAL_MAX] ENG_FROID, GLP[GAL_MAX] ENG_FROID;   /* lignes : -1 - catégorie, ou image IMAG ; son personnage (PERS, 255 Sera, -1 aucun) */
static int eveil_connu(void) {
    for (int j = 0; j < E.w.njobs && j < 16; j++) if (!strcmp(E.w.jobs[j].nom, "ÉVEIL") && ((E.jobs_known[0] >> j) & 1)) return 1;
    return 0;
}
static int gal_de(int i) {   /* catégorie d'un jeu de cut-ins */
    const int sp = E.w.cutins[i].special;
    return sp ? GAL_TRANSFO : E.w.cutins[i].degats ? GAL_DEGATS : E.w.cutins[i].paire ? GAL_DOS : E.w.cutins[i].plein ? GAL_PLEIN : GAL_CUT;
}
static int gal_images(int cat, int *img, int *pers) {   /* images d'une catégorie, sans doublon (les variantes partagent) */
    int n = 0;
    #define GAL_AJOUT(k, p) do { const int k_ = (k); int d_ = 0; for (int q_ = 0; q_ < n; q_++) d_ |= img[q_] == k_; \
        if (k_ >= 0 && !d_ && n < GAL_MAX) { img[n] = k_; pers[n++] = (p); } } while (0)
    if (cat == GAL_SCENES) { for (int k = 0; k < E.w.nscenes; k++) if (!(E.w.scene18[k] && verrou18())) GAL_AJOUT(scene_image(k), -1); }   /* (celle de l'héroïne choisie) */
    else if (cat == GAL_INTRO) for (int k = 0; k < E.w.ouv_ncartes; k++) GAL_AJOUT(E.w.ouv_img[k], -1);
    else if (cat != GAL_TRANSFO || eveil_connu())
        for (int i = 0; i < E.w.ncutins; i++) if (gal_de(i) == cat && cut_vu(E.w.cutins[i].ed)) for (int j = 0; j < E.w.cutins[i].n; j++) GAL_AJOUT(E.w.cutins[i].first + j, E.w.cutins[i].pers);
    #undef GAL_AJOUT
    return n;
}
static int gal_rows(void) {
    static int tmp[GAL_MAX] ENG_FROID, tp[GAL_MAX] ENG_FROID;
    if (H.galcat >= 0) return gal_images(H.galcat, GL, GLP);
    int n = 0;
    for (int c = 0; c < GAL_N; c++) if (gal_images(c, tmp, tp)) GL[n++] = -1 - c;
    return n;
}

/* Code (SYSTÈME › CODE, édition unique ; rien n'y dit à quoi il sert) : un chiffre (0 à 9) ou 10 effacer ; quatre
   chiffres : vérifiés, le bon déverrouille le contenu 18+ (edition_code), ou le reverrouille s'il l'était, et revient au
   menu SYSTÈME (codeTouche de l'émulateur) */
static void code_touche(int k) {
    size_t n = strlen(H.code);
    if (k == 10) { if (n) H.code[n - 1] = 0; return; }
    if (n < 4) { H.code[n] = (char)('0' + k); H.code[n + 1] = 0; n++; }
    if (n < 4) return;
    if (!strcmp(H.code, CODE_HEROINE)) {   /* changer d'héroïne : la page « Qui es-tu ? » (accueil.c), menu refermé */
        H.code[0] = 0;
        if (accueil_heroine_changer()) { H.menu = 0; H.sub = -1; } else toast("Pas d'autre héroïne");
        return;
    }
    if (!strcmp(H.code, CODE_VOITURE)) {   /* véhicule offert : le menu des véhicules (refusé ici : retour au menu SYSTÈME) */
        H.code[0] = 0;
        if (!hud_vehicules(0, NULL, 0)) sys_revenir(SYS_CODE);   /* (la raison : toast) */
        return;
    }
    const int essai = !strcmp(H.code, CODE_ESSAI), tele = !strcmp(H.code, CODE_TELE), zoom = !strcmp(H.code, CODE_ZOOM), ok = essai || tele || zoom || (!strcmp(H.code, CODE18) && E.w.edition == 2);
    if (essai) { E.essai = !E.essai; if (!E.essai) E.eveil_essai = 0; }
    else if (tele) E.tele_tout = !E.tele_tout;   /* téléport partout (tele_vu) */
    else if (zoom) { E.zoom_libre = !E.zoom_libre; E.zoom_lieu = 0; }   /* zoom manuel ; coupé : le zoom du lieu revient (ville_maj) */
    else if (ok && E.deverrou18) edition_verrouiller(); else if (ok) edition_code(H.code);
    if (ok) { sys_revenir(SYS_CODE); toast("Code accepté"); }
    else toast("Code erroné");
    H.code[0] = 0;
}

/* Essais (rendu_test) : le code tapé chiffre par chiffre dans SYSTÈME › CODE, menu ouvert */
void hud_code_taper(const char *code) {
    H.menu = 1; H.sub = SUB_CODE; H.code[0] = 0;
    for (const char *c = code; *c >= '0' && *c <= '9'; c++) code_touche(*c - '0');
}
static void forge_retour(void);
static int up_possede(int k);
static void sub_back(void) { if (H.sub == SUB_SHOP) { if (SHOP.fiche >= 0) { SHOP.fiche = -1; return; } if (SHOP.mode == 4) { forge_retour(); return; } H.sub = -1; H.menu = 0; SHOP.comm = -1; return; }
    if (H.sub == SUB_JOURNAL && H.jniv) { H.jniv = H.jniv == 2 ? 1 : 0; H.sub_sel = 0; H.sub_scroll = 0; return; }
    if (H.sub == SUB_PANNEAU) { H.sub = -1; H.menu = 0; return; }   /* panneau de quêtes : retour au monde */
    if (H.sub == SUB_CODE) { sys_revenir(SYS_CODE); H.code[0] = 0; return; }
    if (H.sub == SUB_EQUIP && EQM.niv > (band_count() > 1 ? EQN_MEMBRES : EQN_PLACES)) { equip_niveau(EQM.niv - 1, EQM.r[EQM.niv - 1]); return; }   /* d'un niveau au précédent, sur sa ligne */
    if (H.sub == SUB_GALERIE) {   /* l'image en grand, puis les catégories, puis le menu SYSTÈME, sur sa ligne */
        if (H.galgrand) H.galgrand = 0;
        else if (H.galcat >= 0) { H.galcat = -1; H.sub_sel = 0; H.sub_scroll = 0; }
        else sys_revenir(11);
        return;
    }
    if (H.sub == SUB_TELE) {   /* des lieux aux sortes, puis au menu SYSTÈME, sur sa ligne */
        if (H.tcat >= 0) { H.tcat = -1; H.sub_sel = 0; H.sub_scroll = 0; }
        else sys_revenir(10);
        return;
    } if (H.sub == SUB_GUIDE && H.gcat >= GR_CAT) { H.gcat = GR_FAM + H.gfam; H.sub_sel = 0; H.sub_scroll = 0; }
    else if (H.sub == SUB_GUIDE && H.gcat != -1) { H.gcat = -1; H.sub_sel = 0; H.sub_scroll = 0; } else H.sub = -1; }

/* Ordre du menu SYSTÈME (05/11/2026, demande de l'utilisateur : « le son en premier, puis téléporter, puis montre, que l'on
   remplace par quitter pour retourner au menu d'accueil ») : ligne affichée -> ligne logique (SYS_*), puis ÉVEIL en mode
   d'essai et les réglages de la plateforme (eng_config_t.regls_jeu ; ESP32 : MANETTE) ; SYS_ORDRE de l'émulateur */
static const uint8_t SYS_ORDRE[] = { 8, 10, SYS_QUITTER, 0, 1, SYS_HEURE, 9, 7, 12, SYS_CONDUITE, SYS_COMMANDES, 13, 5, 2, 3, 4, 6, 11, SYS_CODE };
#define SYS_NORDRE ((int)sizeof SYS_ORDRE)
static int sys_n(void) { return SYS_NORDRE + (E.essai ? 1 : 0) + E.cfg.nregls_jeu; }
static int sys_ligne(int i) {
    if (i < 0) return -1;
    if (i < SYS_NORDRE) return SYS_ORDRE[i];
    i -= SYS_NORDRE;
    if (E.essai) { if (!i) return SYS_EVEIL; i--; }
    return i < E.cfg.nregls_jeu ? SYS_PLAT + i : -1;
}
static int sys_rang(int code) { for (int i = 0; i < sys_n(); i++) if (sys_ligne(i) == code) return i; return 0; }
/* Retour au menu SYSTÈME, sur la ligne de ce réglage (au sortir du code, du téléport, de la galerie) */
static void sys_revenir(int code) { const int r = sys_rang(code); H.sub = SUB_SYSTEM; H.sub_sel = r; H.sub_scroll = r - ROWS + 1 > 0 ? r - ROWS + 1 : 0; }
/* HEURE (05/11/2026, demande de l'utilisateur : « on doit pouvoir changer d'heure dans le menu ») : l'heure que montre le
   ciel (jour figé : midi, nuit figée : minuit) ; un toucher avance l'horloge du cycle à la prochaine heure multiple de 3 et
   repasse au cycle automatique (jour, nuit figés ou heure réelle cacheraient l'heure choisie) ; heureVue de l'émulateur */
static float heure_vue(void) { return E.cycle == 1 ? 0.5f : E.cycle == 2 ? 0.0f : E.heure; }

static int sub_rows(void) {
    switch (H.sub) {
    case SUB_ITEMS: return owned_count();
    case SUB_TECH: return team_techs();
    case SUB_EQUIP: return equip_lignes();   /* membres, emplacements, liste ou OUI / NON (EQM.niv) */
    case SUB_STATUS: return band_count();
    case SUB_EQUIPE: return band_count();   /* Sera, puis chaque recrue de la bande (au combat ou en réserve) */
    case SUB_SYSTEM: return sys_n();   /* lignes dans l'ordre de sys_ligne, puis les réglages de la plateforme */
    case SUB_CODE: return 0;
    case SUB_TELE: return tele_rows();
    case SUB_GALERIE: return gal_rows();
    case SUB_QUESTS: return quest_rows(NULL);
    case SUB_JOURNAL: return jour_lignes();
    case SUB_GUIDE: return guide_rows();
    case SUB_SHOP: return SHOP.fiche >= 0 ? 2 : shop_rows();   /* fiche : OUI, NON */
    case SUB_PANNEAU: return PN.n;   /* les quêtes ouvrables du panneau */
    default: return 0;
    }
}
/* Ligne row du menu TECHNIQ. : membre m, technique t (t >= 0) ou sort -1 - t du catalogue (t < 0) */
static void tech_at(int row, int *m, int *t) {
    int l[MAX_SORTS];
    for (int i = 0; i < NMEMB; i++) {
        if (!in_team(i)) continue;
        if (row < PARTY[i].ntech) { *m = i; *t = row; return; }
        row -= PARTY[i].ntech;
        const int ns = sorts_of(i, l);
        if (row < ns) { *m = i; *t = -1 - l[row]; return; }
        row -= ns;
    }
    *m = 0; *t = 0;
}
/* Membre de l'équipe le plus blessé (PV ou PM), debout (02/11/2026 : un membre à terre ne se soigne pas, on le relève) ;
   -1 : personne debout */
static int weakest(int mp) {
    int best = -1; float br = 2;
    for (int i = 0; i < NMEMB; i++) {
        if (!in_team(i) || PARTY[i].hp <= 0) continue;
        float r = mp ? (float)PARTY[i].mp / PARTY[i].maxmp : (float)PARTY[i].hp / PARTY[i].max;
        if (r < br) { br = r; best = i; }
    }
    return best;
}

static void open_sub(int k) { H.sub = k; H.sub_sel = 0; H.sub_scroll = 0; H.fling = 0; H.gcat = -1; H.jniv = 0; if (k == SUB_EQUIP) equip_ouvrir(); if (k == SUB_MAP) carte_ouvrir(); }

/* Liste d'un sous-menu : défile de d lignes (> 0 : vers la fin), la ligne choisie garde sa place ; renvoie le pas fait */
static int scroll_rows(int d) {
    const int n = sub_rows(), top = n > ROWS ? n - ROWS : 0;
    if (d > top - H.sub_scroll) d = top - H.sub_scroll;
    if (d < -H.sub_scroll) d = -H.sub_scroll;
    H.sub_scroll += d; H.sub_sel += d;
    if (H.sub_sel > n - 1) H.sub_sel = n - 1;
    if (H.sub_sel < 0) H.sub_sel = 0;
    return d;
}
/* Doigt (ou élan) de dy pixels vers le bas : une ligne tous les DRAG_ROW pixels, la liste suit le doigt */
#define DRAG_ROW (PORTRAIT ? ROW_STEP : 40.0f)
static void drag_rows(float dy) {
    H.acc -= dy;
    while (H.acc >= DRAG_ROW) { H.acc -= DRAG_ROW; if (!scroll_rows(1)) { H.acc = 0; H.fling = 0; } }
    while (H.acc <= -DRAG_ROW) { H.acc += DRAG_ROW; if (!scroll_rows(-1)) { H.acc = 0; H.fling = 0; } }
}

/* STATUT (03/11/2026) : vue du bas de la fiche, ce qu'il porte ou ses sorts (draw_statut) ; fixe : choisie au toucher (OK),
   sinon l'une puis l'autre ; vu : celle de la dernière image */
/* SYSTÈME › COMMANDES : noms et aide (COMMANDES_* d'internal.h) */
static const char *const COMMANDES_NOMS[3] = { "DOUBLES", "JOYSTICK", "POINTER" };
static const char *const COMMANDES_AIDE[3] = { "Commandes doubles : gaz à gauche, volant à droite", "Joystick : un doigt sur le cercle, vers le haut pour avancer",
                                               "Pointer : touchez le sol, le véhicule y va" };
static struct { int fixe, sorts, vu; } ST_VUE;
static void activate_row(int row) {
    char t[80];
    if (H.sub == SUB_STATUS) { ST_VUE.fixe = 1; ST_VUE.sorts = !ST_VUE.vu; son_jouer(SON_MENU, 0.35f, 1); return; }   /* l'autre vue du bas de la fiche */
    if (H.sub == SUB_GALERIE) {   /* une catégorie : ses images ; une image : en grand (un autre toucher la referme) */
        if (H.galgrand) { H.galgrand = 0; return; }
        if (row >= gal_rows()) return;
        if (GL[row] < 0) { H.galcat = -1 - GL[row]; H.sub_sel = 0; H.sub_scroll = 0; }
        else { H.sub_sel = row; H.galgrand = 1; }
        return;
    }
    if (H.sub == SUB_PANNEAU) {   /* panneau de quêtes : le guidage vers la personne ou le lieu qui donne la quête, ou une
                                     quête d'affichage qui part (panneaux.c : ouverture_choisir) ; le panneau se ferme */
        if (row >= PN.n) return;
        const int i = PN.o[row], q = ouverture_quete(i);
        H.sub = -1; H.menu = 0;
        if (!ouverture_choisir(i)) {
            float gx, gz;
            if (panneau_guide(&gx, &gz) || !E.w.carte) { snprintf(t, sizeof t, "Guidage : %s", panneau_guide_nom()[0] ? panneau_guide_nom() : script_quest_name(q)); toast(t); }
        }
        son_jouer(SON_VALIDER, 0.4f, 1);
        return;
    }
    if (H.sub == SUB_SHOP) {   /* boutique : un exemplaire acheté ou vendu, après confirmation (shop_demander, shop_fiche) */
        if (SHOP.fiche >= 0) { shop_fiche_choix(row); return; }   /* fiche : OUI (0) ou NON (1) */
        const int k = shop_item(row);
        if (k >= 0 && SHOP.mode == 2 && SHOP.forge) {   /* forge : AMÉLIORER ou ÉLÉMENT, vérifiés au choix */
            if (!up_possede(k)) { toast("Vous ne l'avez pas"); son_jouer(SON_ANNULER, 0.45f, 1); return; }
            shop_demander(k); return;
        }
        if (k < 0 || !shop_possible(k)) return;
        if (SHOP.mode == 0 && eq_slot(k) >= 0) shop_fiche(k); else shop_demander(k);
        return;
    }
    if (H.sub == SUB_ITEMS) {
        const int k = owned_item(row);
        if (k < 0) { toast("Plus aucun exemplaire"); return; }
        item_t *it = &ITEMS[k];
        if (it->releve) {   /* relève le premier membre de l'équipe à terre, avec RELEVE_PART de ses PV (soinsSac de l'émulateur) */
            int ko = -1;
            for (int i = 0; i < NMEMB && ko < 0; i++) if (in_team(i) && PARTY[i].hp <= 0) ko = i;
            if (ko < 0) { toast("Personne n'est à terre"); return; }
            PARTY[ko].hp = (int)ceilf(PARTY[ko].max * RELEVE_PART);
            it->count--;
            snprintf(t, sizeof t, "%s · %s se relève (+%d PV)", it->name, PARTY[ko].name, PARTY[ko].hp); toast(t);
            son_jouer(SON_SOIN, 0.5f, 1.1f);
            return;
        }
        if (it->type == OBJ_LANCER) { toast("À lancer en combat"); return; }
        if (!it->hp && !it->mp) { toast(it->type == 1 ? "Objet de quête" : it->type == 2 ? "Objet à revendre" : it->type == 4 ? "Tenue : menu ÉQUIP." : it->type == 5 ? "Équipement : menu ÉQUIP." : "Ne s'utilise pas ici"); return; }
        /* Toute l'équipe, ou le membre le plus blessé (en PV si l'objet en rend, sinon en PM) ; PV et PM à la fois ; ceux
           qui sont à terre ne se soignent pas (02/11/2026 : on les relève) */
        const int one = weakest(it->hp == 0);
        if (one < 0 && !it->team) { toast("Toute l'équipe est à terre"); return; }
        int hp = 0, mp = 0;
        for (int i = 0; i < NMEMB; i++) {
            if (it->team ? !in_team(i) || PARTY[i].hp <= 0 : i != one) continue;
            member_t *m = &PARTY[i];
            int a = it->hp < m->max - m->hp ? it->hp : m->max - m->hp, b = it->mp < m->maxmp - m->mp ? it->mp : m->maxmp - m->mp;
            if (a < 0) a = 0;
            if (b < 0) b = 0;
            m->hp += a; m->mp += b; hp += a; mp += b;
        }
        if (hp + mp <= 0) { toast(it->hp && !it->mp ? "Tous les PV sont au maximum" : it->mp && !it->hp ? "Tous les PM sont au maximum" : "PV et PM au maximum"); return; }
        it->count--;
        char g[40];
        if (hp && mp) snprintf(g, sizeof g, " +%d PV +%d PM", hp, mp);
        else snprintf(g, sizeof g, " +%d %s", hp ? hp : mp, hp ? "PV" : "PM");
        snprintf(t, sizeof t, "%s · %s%s", it->name, it->team ? "ÉQUIPE" : PARTY[one].name, g);
        toast(t);
    } else if (H.sub == SUB_TECH) {
        int mi, ti; tech_at(row, &mi, &ti);
        member_t *m = &PARTY[mi];
        /* Technique de soin, ou sort de soin (MAG × puissance ; soin 2 : toute l'équipe) */
        const sort_t *sp = ti < 0 ? &E.w.sorts[-1 - ti] : NULL;
        const char *nom = sp ? sp->nom : m->tech[ti].name;
        const int cout = sp ? sp->pm : m->tech[ti].mp, soin = sp ? (sp->soin ? (int)lroundf(m->mag * sp->mult) : 0) : m->tech[ti].heal;
        if (!soin) { toast(sp ? "Sort de combat" : "Technique de combat"); return; }
        if (m->hp <= 0) { snprintf(t, sizeof t, "%s est à terre", m->name); toast(t); return; }   /* à terre : ne soigne personne */
        if (m->mp < cout) { toast("PM insuffisants"); return; }
        if (sp && sp->soin == SOIN_RELEVE) {   /* relève (02/11/2026) : le premier membre de l'équipe à terre, MAG × puissance PV */
            int ko = -1;
            for (int i = 0; i < NMEMB && ko < 0; i++) if (in_team(i) && PARTY[i].hp <= 0) ko = i;
            if (ko < 0) { toast("Personne n'est à terre"); return; }
            PARTY[ko].hp = soin < PARTY[ko].max ? soin : PARTY[ko].max;
            m->mp -= cout;
            snprintf(t, sizeof t, "%s · %s se relève (+%d PV)", nom, PARTY[ko].name, PARTY[ko].hp); toast(t);
            son_jouer(SON_SOIN, 0.5f, 1.1f);
            return;
        }
        int gain = 0;
        const int cible = weakest(0);   /* un allié : le plus faible, debout (un membre à terre se relève, il ne se soigne pas) */
        for (int i = 0; i < NMEMB; i++) {
            if (!in_team(i) || PARTY[i].hp <= 0) continue;
            if (!(sp && sp->soin == 2) && i != cible) continue;
            member_t *o = &PARTY[i];
            const int g = soin < o->max - o->hp ? soin : o->max - o->hp;
            o->hp += g; gain += g;
        }
        if (gain <= 0) { toast("Tous les PV sont au maximum"); return; }
        m->mp -= cout;
        snprintf(t, sizeof t, "%s · %s +%d PV", nom, sp && sp->soin == 2 ? "ÉQUIPE" : PARTY[cible].name, gain);
        toast(t);
    } else if (H.sub == SUB_EQUIPE) {   /* le membre passe au combat ou en réserve (equipe_basculer) ; pas pendant une scène */
        if (row >= band_count()) return;
        const int mi = band_at(row);
        if (script_running()) { toast("Pas pendant une scène"); son_jouer(SON_ANNULER, 0.45f, 1); return; }
        const int r = equipe_basculer(mi);
        if (r == EQB_FAIT) snprintf(t, sizeof t, "%s %s", PARTY[mi].name, in_team(mi) ? "rejoint le combat" : "passe en réserve");
        else if (r == EQB_SERA) snprintf(t, sizeof t, "%s mène l'équipe", PARTY[0].name);
        else if (r == EQB_IMPOSE) snprintf(t, sizeof t, "%s : imposé par l'histoire", PARTY[mi].name);
        else snprintf(t, sizeof t, "Équipe complète : retirer d'abord un compagnon");
        toast(t); son_jouer(r == EQB_FAIT ? SON_VALIDER : SON_ANNULER, 0.45f, 1);
    } else if (H.sub == SUB_EQUIP) {
        /* Menu ÉQUIP. : un membre, ses emplacements ; un emplacement d'équipement, sa liste ; une ligne de la liste, sa fiche ;
           la fiche, OUI ou NON (JOB et TENUE : le suivant, comme avant) */
        if (row >= equip_lignes()) return;
        if (EQM.niv < EQN_FICHE) EQM.r[EQM.niv] = row;
        if (EQM.niv == EQN_FICHE) { equip_confirmer(row); return; }
        if (EQM.niv == EQN_MEMBRES) { EQM.m = band_at(row); equip_niveau(EQN_PLACES, 0); return; }
        if (EQM.niv == EQN_LISTE) {
            const int k = EQL[row];
            if (k != EQ_AUCUN && k == E.eq[EQM.m][EQM.sl]) { toast("Déjà porté"); return; }
            if (k != EQ_AUCUN && !eq_aff(EQM.m, k)) {
                if (ITEMS[k].tenue) snprintf(t, sizeof t, "%s n'a pas cette tenue", PARTY[EQM.m].name);
                else snprintf(t, sizeof t, "%s ne peut pas le porter (%s)", PARTY[EQM.m].name, E.job[EQM.m] < E.w.njobs ? E.w.jobs[E.job[EQM.m]].nom : "job");
                toast(t); son_jouer(SON_ANNULER, 0.45f, 1); return;
            }
            EQM.it = k; equip_niveau(EQN_FICHE, 0);   /* OUI choisi d'avance */
            return;
        }
        int pl[5];
        const int mi = EQM.m, sl = row < equip_places(mi, pl) ? pl[row] : ER_TENUE;
        if (sl >= 0 && sl < 3) {   /* arme, armure, accessoire : sa liste (rien à y mettre : on reste ici) */
            EQM.sl = sl;
            if (!equip_liste()) { snprintf(t, sizeof t, "Rien à porter (%s) pour %s", SLOT_NAME[sl], PARTY[mi].name); toast(t); son_jouer(SON_ANNULER, 0.45f, 1); return; }
            equip_niveau(EQN_LISTE, 0);
            return;
        }
        if (sl == ER_JOB) {   /* le job suivant parmi ceux qu'il connaît */
            int nk = 0, nx = -1;
            for (int j = 0; j < E.w.njobs; j++) if (E.jobs_known[mi] >> j & 1) nk++;
            if (nk < 2) { snprintf(t, sizeof t, "%s ne connaît qu'un job", PARTY[mi].name); toast(t); return; }
            for (int d = 1; d <= E.w.njobs && nx < 0; d++) {
                const int j = ((E.job[mi] < E.w.njobs ? E.job[mi] : -1) + d + E.w.njobs) % E.w.njobs;
                if (E.jobs_known[mi] >> j & 1) nx = j;
            }
            const int off = party_set_job(mi, nx);
            snprintf(t, sizeof t, "%s : %s%s%s", PARTY[mi].name, E.w.jobs[nx].nom, off >= 0 ? " · au sac : " : "", off >= 0 ? ITEMS[off].name : "");
            toast(t);
            return;
        }
        /* Tenue : la sienne, puis les tenues dont un objet du sac (type tenue) porte le nom */
        int ok[MAX_TENUES + 1], n = 0, cur = -1;
        ok[n++] = -1;
        for (int k = 0; k < E.w.ntenues; k++) {
            if (!tenue_mine(mi, k)) continue;
            for (int i = 0; i < n_items; i++)
                if (ITEMS[i].type == 4 && ITEMS[i].count > 0 && ITEMS[i].tenue && !strcmp(ITEMS[i].tenue, E.w.tenues[k].nom)) { ok[n++] = k; break; }
        }
        if (n < 2) { snprintf(t, sizeof t, has_tenues(mi) ? "Aucune tenue de %s dans le sac" : "Pas de tenue pour %s", PARTY[mi].name); toast(t); return; }
        for (int k = 0; k < n; k++) if (ok[k] == E.tenue_eq[mi]) cur = k;
        E.tenue_eq[mi] = (int8_t)ok[(cur + 1) % n];
        snprintf(t, sizeof t, "%s : %s", PARTY[mi].name, E.tenue_eq[mi] >= 0 ? E.w.tenues[E.tenue_eq[mi]].nom : "sa tenue");
        toast(t);
    } else if (H.sub == SUB_JOURNAL) {
        if (H.jniv == 0) { H.jniv = row == 2 ? 4 : row ? 3 : 1; H.sub_sel = 0; H.sub_scroll = 0; }
        else if (H.jniv == 1) { if (echs_entendus(row)) { H.jvie = row; H.jniv = 2; H.sub_sel = 0; H.sub_scroll = 0; } else toast("Aucune voix entendue"); }
        else if (H.jniv == 2) {
            static char voix[200];
            if (script_ev_texte(jour_voix(row), voix, sizeof voix)) hud_say("ÉCHO", voix, "ÉCHO");
        }
    } else if (H.sub == SUB_QUESTS) {   /* suivre la quête (guidage) */
        int rows[SC_MAX_QUESTS];
        if (row >= quest_rows(rows)) return;
        if (E.quests[rows[row]] == 255) { toast("Quête terminée"); return; }
        script_track(rows[row]);
        snprintf(t, sizeof t, "Quête suivie : %s", script_quest_name(rows[row]));
        toast(t);
    } else if (H.sub == SUB_GUIDE) {   /* une sorte : ses lieux ; un lieu : le suivre (le menu se ferme) */
        if (row >= guide_rows()) return;
        const int c = GR[row];
        if (c == GR_PILOTE) { pilote_arreter("Pilote automatique arrêté"); H.sub_sel = 0; H.sub_scroll = 0; }
        else if (c == GR_STOP) { E.guide = -1; panneau_guide_arreter(); toast("Guidage arrêté"); }   /* (et celui d'un panneau de quêtes) */
        else if (c >= GR_CAT) { H.gfam = E.w.guides[c - GR_CAT].groupe; H.gcat = c; H.sub_sel = 0; H.sub_scroll = 0; }
        else if (c >= GR_FAM) { H.gcat = c; H.sub_sel = 0; H.sub_scroll = 0; }
        else {   /* en véhicule (planète, hors course et hors recherche) : le pilote automatique y conduit, guidage compris */
            char nm[48];
            E.guide = c; panneau_guide_arreter(); guide_label(c, " (", ")", nm, sizeof nm);
            if (E.ride >= 0 && pilote_possible(0)) { char pn[48]; guide_label(c, " · ", "", pn, sizeof pn); pilote_lancer(E.w.guides[c].x, E.w.guides[c].z, c, pn); }
            else { snprintf(t, sizeof t, "Guidage : %s", nm); toast(t); }
            H.sub = -1; H.menu = 0;
        }
    } else if (H.sub == SUB_SYSTEM) {
        const int code = sys_ligne(row);
        switch (code) {
        case 0: save_game(); break;
        case 1: load_game(); break;
        case 2: E.theme = (E.theme + 1) % 5; break;
        case 3: E.fog_on = !E.fog_on; break;
        case 4: E.dither_on = !E.dither_on; break;
        case 5: H.bright = (H.bright + 1) % 5; if (E.cfg.set_brightness) E.cfg.set_brightness(64 + H.bright * 48); break;
        case 6: break;   /* zoom : affiché seulement, fixe selon le lieu (game.c, ville_maj ; 01/11/2026) */
        case 7: E.weather = E.meteo_choix = (E.weather + 1) % 3; break;   /* météo : automatique, clair, pluie */
        case 8: H.vol_on = 1; H.acc = 0; break;   /* son : cadran réglé en tournant le bord (vol_pas) */
        case 9: E.cycle = (E.cycle + 1) % (E.horloge >= 0 ? 4 : 3); break;   /* cycle jour / nuit : automatique, jour, nuit, heure réelle (montre) */
        case 10: H.sub = SUB_TELE; H.tcat = -1; H.sub_sel = 0; H.sub_scroll = 0; break;   /* téléport : ses sortes de lieux */
        case 11: H.sub = SUB_GALERIE; H.galcat = -1; H.galgrand = 0; H.sub_sel = 0; H.sub_scroll = 0; break;   /* galerie : ses catégories */
        case 12: E.renc_mode = (E.renc_mode + 1) % 5; break;   /* rencontres : normales, rares, très rares, aucune, fréquentes */
        case 13: E.volant = (E.volant + 1) % 5; break;   /* direction en course : très douce, douce, moyenne, vive, très vive */
        case SYS_CONDUITE:   /* conduite hors course : assistée (maintien de voie) ou libre */
            E.conduite = E.conduite == CONDUITE_ASSISTEE ? CONDUITE_LIBRE : CONDUITE_ASSISTEE; PIL.autorite = 0;
            toast(E.conduite == CONDUITE_ASSISTEE ? "Conduite assistée : la voiture suit la voie" : "Conduite libre");
            break;
        case SYS_COMMANDES:   /* conduite au doigt : deux pouces (gaz à gauche, volant à droite), joystick d'un doigt, ou point touché */
            E.commandes = (E.commandes + 1) % 3;
            toast(COMMANDES_AIDE[E.commandes]);
            break;
        case SYS_CODE: H.sub = SUB_CODE; H.code[0] = 0; break;   /* le pavé du code (18+ de l'édition unique, mode d'essai) */
        case SYS_EVEIL:   /* mode d'essai : transformée ou non */
            if (E.essai) { E.eveil_essai = !E.eveil_essai; toast(E.eveil_essai ? "Transformée" : "Transformation levée"); }
            break;
        case SYS_HEURE: {   /* l'heure ronde suivante, multiple de 3 ; cycle automatique */
            const int h = ((int)floorf(heure_vue() * 8 + 1e-3f) + 1) % 8;
            E.heure = h / 8.0f; E.cycle = 0;
            snprintf(t, sizeof t, "Il est %d h", h * 3); toast(t);
            break;
        }
        case SYS_QUITTER:   /* partie sauvegardée, puis l'écran titre (avant le 05/11/2026 : MONTRE, l'app montre, ou ACCUEIL) */
            if (!save_game()) return;
            H.sub = -1; H.menu = 0;
            accueil_titre();
            return;
        }
        if (code >= SYS_PLAT && code < SYS_PLAT + E.cfg.nregls_jeu) {   /* réglage de la plateforme : un cran */
            const eng_reglage_t *pr = &E.cfg.regls_jeu[code - SYS_PLAT];
            if (pr->changer) pr->changer(1);
        }
    } else if (H.sub == SUB_TELE) {   /* une sorte : ses lieux ; un lieu : y aller (hors d'un intérieur, sur sa carte) */
        if (row >= tele_rows()) return;
        const int r = TR[row];
        if (r < 0) { H.tcat = -1 - r; H.sub_sel = 0; H.sub_scroll = 0; return; }
        if (r >= SAUT0) {   /* saut : à l'orbite du monde, à bord (sauter de l'émulateur) */
            const astre_t *a = &E.w.astres[r - SAUT0];
            E.sera_y_ok = 0; E.sera.x = a->ax; E.sera.z = a->az; E.has_dest = 0; E.stream_hold = 1; BT.fade_t = E.clock;
            vol_embarquer(0);
            snprintf(t, sizeof t, "Saut : %s", a->nom);
            toast(t);
            H.sub = -1; H.menu = 0;
            return;
        }
        const char *refus = tele_refus();   /* en véhicule, recherchée (pas de fuite par le téléport), pendant une scène */
        if (refus) { toast(refus); return; }
        int nt;
        const uint8_t *q = tele_sec(&nt) + r * 36;
        char nom[25];
        memcpy(nom, q, 24); nom[24] = 0;
        const int k = q[25];
        float x, z;
        memcpy(&x, q + 28, 4); memcpy(&z, q + 32, 4);
        int_reset();
        if (k != E.w.carte && k < E.w.ncartes) world_carte(k, x, z);
        else { E.sera_y_ok = 0; E.sera.x = x; E.sera.z = z; E.has_dest = 0; E.stream_hold = 1; BT.fade_t = E.clock; }
        snprintf(t, sizeof t, "Téléportation : %s", nom);
        toast(t);
        H.sub = -1; H.menu = 0;
    }
}

/* Son : d pas de 5 % (0 à 100 %) du canal choisi (H.vol_canal : le volume général, ou un canal du mixage), et un son
   de ce canal au nouveau niveau (le bip pour le général et l'interface) */
#define VOL_PAS_RAD 0.13f   /* angle tourné sur le bord pour un pas : de coupé à 100 % en un peu moins d'un demi-tour */
#define VOL_CANAUX (1 + CANAUX)
static const char *const VOL_NOMS[VOL_CANAUX] = { "GÉNÉRAL", "MUSIQUE", "EFFETS", "MOTEURS", "INTERFACE" };
static float vol_niveau(int k) { return k ? audio_canal_niveau(k - 1) : eng_volume(); }
static void vol_build(void);
static int vol_toucher(float dx, float dy);
/* Canal k du cadran (0 : le volume général) : d pas de 5 % ; un son du canal au nouveau niveau */
static void vol_regler(int k, int d) {
    const int v0 = (int)lroundf(vol_niveau(k) * 20), v = v0 + d < 0 ? 0 : v0 + d > 20 ? 20 : v0 + d;
    if (v == v0) return;
    if (k) audio_canal(k - 1, v / 20.0f); else eng_set_volume(v / 20.0f);
    H.vol_t = H.now;
    const int c = k - 1;
    static float apercu_t = -1;   /* coup ou moteur (1,5 s) : un toutes les 0,3 s au plus quand le bord tourne vite */
    if (c != CANAL_EFFETS && c != CANAL_MOTEURS) son_jouer(SON_BIP, 0.6f, 0.8f + v * 0.02f);
    else if (H.now - apercu_t > 0.3f || H.now < apercu_t) { apercu_t = H.now; son_jouer(c == CANAL_EFFETS ? SON_COUP : SON_MOTEUR_VOITURE, 0.6f, 1); }
}
static void vol_pas(int d) { vol_regler(H.vol_canal, d); }   /* le canal du cadran */
/* Canal suivant (d = 1) ou précédent du cadran */
static void vol_canal(int d) {
    H.vol_canal = (H.vol_canal + d + VOL_CANAUX) % VOL_CANAUX; H.vol_t = H.now; H.acc = 0;
    son_jouer(SON_MENU, 0.5f, 1);
}

/* ---------- Réglages du mode course (02/11/2026, demande de l'utilisateur : « en mode course, on ne peut pas aller dans le
   menu de réglage système, donc on ne peut rien régler ») ----------
   Les lignes de SYSTÈME qui ont un sens en course, pour la page RÉGLAGES du mode course (accueil.c, AP_CREGL : depuis la
   page COURSE de l'écran titre, ou la pause d'une course) : les cinq canaux du son (pas de 10 %), DIRECTION, COMMANDES,
   LUMINOSITÉ, BROUILLARD, TRAMAGE, TEINTE, puis les réglages de la plateforme montrés en jeu (ESP32 : MANETTE, BOUTON OK).
   Chaque ligne se règle (moins, plus). Mêmes lignes, valeurs et aides que reglLignes de l'émulateur */
enum { RG_GENERAL, RG_MUSIQUE, RG_EFFETS, RG_MOTEURS, RG_INTERFACE, RG_DIRECTION, RG_COMMANDES, RG_LUMINOSITE, RG_BROUILLARD, RG_TRAMAGE, RG_TEINTE, RG_N };
static const char *const RG_NOMS[RG_N] = { "SON · GÉNÉRAL", "SON · MUSIQUE", "SON · EFFETS", "SON · MOTEURS", "SON · INTERFACE", "DIRECTION",
                                           "COMMANDES", "LUMINOSITÉ", "BROUILLARD", "TRAMAGE", "TEINTE" };
static const char *const RG_AIDES[RG_N] = { "Volume de tout le jeu", "Musique des courses et du monde", "Chocs, tirs, armes", "Moteurs des véhicules",
                                            "Menus et boutons", "Virage du véhicule en course", "Deux pouces, joystick, ou pointer", "Luminosité de l'écran",
                                            "Brume au loin", "Trame de la lumière et de la brume", "Couleurs de l'interface" };
static const char *const DIR_NOMS[5] = { "TRÈS DOUCE", "DOUCE", "MOYENNE", "VIVE", "TRÈS VIVE" };
static int clampi(int v, int a, int b);
/* Ordre des lignes (04/11/2026, demande de l'utilisateur : « dans les réglages de course je ne peux pas changer le mode de
   conduite, je n'ai accès qu'aux réglages son » ; quatre lignes visibles sur la montre) : la conduite d'abord, puis le son
   (REGL_ORDRE de l'émulateur) ; ligne i -> réglage RG_* */
static const uint8_t RG_ORDRE[RG_N] = { RG_DIRECTION, RG_COMMANDES, RG_GENERAL, RG_MUSIQUE, RG_EFFETS, RG_MOTEURS, RG_INTERFACE, RG_LUMINOSITE,
                                        RG_BROUILLARD, RG_TRAMAGE, RG_TEINTE };
static int rg_ligne(int i) { return i >= 0 && i < RG_N ? RG_ORDRE[i] : i; }
int hud_regl_n(void) { return RG_N + E.cfg.nregls_jeu; }
const char *hud_regl_nom(int i) { return i < RG_N ? RG_NOMS[rg_ligne(i)] : i < hud_regl_n() ? E.cfg.regls_jeu[i - RG_N].nom : ""; }
const char *hud_regl_aide(int i) { return i < RG_N ? RG_AIDES[rg_ligne(i)] : "Réglage de la montre"; }
void hud_regl_valeur(int i, char *t, int n) {
    i = rg_ligne(i);
    if (i <= RG_INTERFACE) { const int v = (int)lroundf(vol_niveau(i) * 100); if (v) snprintf(t, n, "%d %%", v); else snprintf(t, n, "COUPÉ"); }
    else if (i == RG_DIRECTION) snprintf(t, n, "%s", DIR_NOMS[E.volant % 5]);
    else if (i == RG_COMMANDES) snprintf(t, n, "%s", COMMANDES_NOMS[E.commandes % 3]);
    else if (i == RG_LUMINOSITE) snprintf(t, n, "%d %%", 20 + H.bright * 20);
    else if (i == RG_BROUILLARD) snprintf(t, n, "%s", E.fog_on ? "OUI" : "NON");
    else if (i == RG_TRAMAGE) snprintf(t, n, "%s", E.dither_on ? "OUI" : "NON");
    else if (i == RG_TEINTE) snprintf(t, n, "%s", THEMES[E.theme % 5].name);
    else if (i < hud_regl_n() && E.cfg.regls_jeu[i - RG_N].valeur) snprintf(t, n, "%s", E.cfg.regls_jeu[i - RG_N].valeur());
    else if (n) t[0] = 0;
}
/* Un cran (d : -1 moins, +1 plus) ; le son du canal, sinon le tic du menu */
void hud_regl_changer(int i, int d) {
    i = rg_ligne(i);
    if (i <= RG_INTERFACE) { vol_regler(i, 2 * d); return; }
    if (i == RG_DIRECTION) E.volant = clampi(E.volant + d, 0, 4);
    else if (i == RG_COMMANDES) E.commandes = (E.commandes + (d < 0 ? 2 : 1)) % 3;
    else if (i == RG_LUMINOSITE) { H.bright = clampi(H.bright + d, 0, 4); if (E.cfg.set_brightness) E.cfg.set_brightness(64 + H.bright * 48); }
    else if (i == RG_BROUILLARD) E.fog_on = !E.fog_on;
    else if (i == RG_TRAMAGE) E.dither_on = !E.dither_on;
    else if (i == RG_TEINTE) E.theme = (E.theme + d + 5) % 5;
    else if (i < hud_regl_n() && E.cfg.regls_jeu[i - RG_N].changer) E.cfg.regls_jeu[i - RG_N].changer(d);
    son_jouer(SON_MENU, 0.35f, d > 0 ? 1.1f : 0.9f);
}

/* Réglages communs (02/11/2026) : ceux de la page RÉGLAGES (sauf le volume général, gardé par la plateforme : montre.txt
   sur l'ESP32), gardés une fois pour toutes les parties, les carrières et la course libre, dans l'emplacement REGLAGES_SLOT
   après le déverrouillage du 18+ (world.c : ses 4 premiers octets, 'E18' et 0 ou 1, lus seuls par edition_lire comme
   avant) : 'R', 'G', version 1, teinte, brouillard | tramage << 1, luminosité (0 à 4), direction (0 à 4), commandes, mixage
   (3 octets, comme la sauvegarde : mix_ecrire), somme de contrôle ; 16 octets. Écrits en quittant la page RÉGLAGES ou le
   menu SYSTÈME, et quand le 18+ change, seulement s'ils ont changé. Lus au démarrage (hud_init) ; sans eux (avant le
   02/11/2026), ceux de la partie la plus récente. Quand ils existent, une partie chargée garde les réglages en cours (ceux
   qu'elle a gardés sont lus mais ne servent plus) ; une carrière ouverte aussi (sa direction, car.volant, de l'étape 1).
   Comme reglagesLire, reglagesGarder de l'émulateur (localStorage « echoes-reglages ») */
static uint8_t regl_somme(const uint8_t *r) { uint8_t c = 0x5A; for (int k = 4; k < REGL_OCT - 1; k++) c = (uint8_t)(c * 31 + r[k]); return c; }
static void regl_octets(uint8_t *r) {
    memset(r, 0, REGL_OCT);
    r[0] = 'E'; r[1] = '1'; r[2] = '8'; r[3] = (uint8_t)(E.deverrou18 ? 1 : 0);
    r[4] = 'R'; r[5] = 'G'; r[6] = 1; r[7] = (uint8_t)(E.theme % 5); r[8] = (uint8_t)((E.fog_on ? 1 : 0) | (E.dither_on ? 2 : 0));
    r[9] = (uint8_t)(H.bright % 5); r[10] = (uint8_t)(E.volant % 5); r[11] = (uint8_t)(E.commandes % 3);
    mix_ecrire(r + 12);
    r[REGL_OCT - 1] = regl_somme(r);
}
/* Applique les réglages des octets r (lumière : seulement si lum) */
static void regl_appliquer(const uint8_t *r, int lum) {
    E.theme = r[7] % 5; E.fog_on = r[8] & 1; E.dither_on = r[8] >> 1 & 1; H.bright = r[9] % 5; E.volant = r[10] % 5; E.commandes = r[11] % 3;
    mix_lire(r + 12);
    if (lum && E.cfg.set_brightness) E.cfg.set_brightness(64 + H.bright * 48);
}
int reglages_communs(void) { return regl_ok; }
void reglages_garder(void) {
    uint8_t r[REGL_OCT];
    regl_octets(r);
    if (!memcmp(r, regl_vu, REGL_OCT) || !E.cfg.save_write) return;
    if (E.cfg.save_write(REGLAGES_SLOT, r, REGL_OCT) == REGL_OCT) { memcpy(regl_vu, r, REGL_OCT); regl_ok = 1; }
}
/* Au démarrage : les réglages communs, sinon ceux de la partie la plus récente (CONTINUER) ; regl_vu : ceux en vigueur */
static void reglages_lire(void) {
    uint8_t r[REGL_OCT];
    if (E.cfg.save_read && E.cfg.save_read(REGLAGES_SLOT, r, REGL_OCT) == REGL_OCT && r[4] == 'R' && r[5] == 'G' && r[6] == 1 && r[REGL_OCT - 1] == regl_somme(r)) {
        regl_appliquer(r, 1); regl_ok = 1;
    } else {
        save_t *const s = (save_t *)SV_BLOC;   /* (la tête du bloc de la sauvegarde : le brouillard qui suit n'est pas touché) */
        uint32_t best = 0; int bp = -1;
        for (int p = 0; p < PARTIES && s; p++) if (partie_lire(p, s) && s->ordre >= best) { best = s->ordre; bp = p; }
        if (bp >= 0 && partie_lire(bp, s)) {
            E.theme = s->theme % 5; E.fog_on = s->fog & 1; E.volant = (s->fog >> 1 & 7) ? ((s->fog >> 1 & 7) - 1) % 5 : VOLANT_DEFAUT; E.dither_on = s->dither;
            H.bright = (s->bright & 7) % 5;
            if (s->conduite != SV_ANCIENNE && (s->conduite >> 1 & 3)) E.commandes = ((s->conduite >> 1 & 3) - 1) % 3;
            mix_lire(s->mix);   /* (d'avant le mixage : sans effet) */
        }
    }
    regl_octets(regl_vu);
}

static void commerce_ouvrir(void);
/* Réplique en cours telle qu'elle s'affiche : noms échangés (héroïne choisie, nom_echange) ; la frappe lettre par lettre,
   ses bips et le toucher qui la finit comptent ses caractères. Le dialogue la dessine filtre de ui.c coupé (il
   échangerait de nouveau les noms) */
static const char *dlg_texte(void) {
    static char b[600] ENG_FROID;
    return H.line < D.n && D.text[H.line] ? nom_echange(D.text[H.line], b, (int)sizeof b) : "";
}
static int parle_maj = 1;   /* nouvelle réplique : hud_parle_pos cherche celui qui la dit */
static void advance_dialog(void) {
    int len = 0; for (const char *s = dlg_texte(); *s; s++) if ((*s & 0xC0) != 0x80) len++;
    if ((H.now - H.line_t0) / 0.028f < len) { H.line_t0 = -1e9f; return; }
    parle_maj = 1;
    if (++H.line >= D.n) { H.dialog = 0; if (H.apres == 2) { H.apres = 0; ile_enleve_retrouve(); } else if (H.apres) { H.apres = 0; commerce_ouvrir(); } } else H.line_t0 = H.now;   /* (2 : l'habitant enlevé retrouvé, ile.c) */
}

/* Nom de l'exemplaire k : « PRÉNOM NOM » tiré des tables (NOMT) d'après sa graine pour un personnage générique ou à
   métier (genre dans ses drapeaux, PERS_GENRE), sinon celui de son personnage. Même calcul que nomPnj de l'émulateur et
   Noms.nom de l'Atelier (js/noms.js) : aucun prénom répété pour nP graines de suite, aucun nom complet pour nP × nS */
const char *pnj_nom(int k) {
    static char buf[48];
    if (k < 0 || k >= E.nnpc) return "";
    const npc_t *n = &E.npcs[k];
    const perso_t *pe = &E.w.persos[n->perso];
    const int g = PERS_GENRE(pe->flags);
    const world_t *w = &E.w;
    if (!g || !w->nomt || !w->nnom[g - 1] || !w->nnom[3]) return pe->nom;
    uint32_t tot = 0, base = 0;
    for (int i = 0; i < 4; i++) { tot += (uint32_t)w->nnom[i]; if (i < g - 1) base += (uint32_t)w->nnom[i]; }
    const uint32_t nP = (uint32_t)w->nnom[g - 1], nS = (uint32_t)w->nnom[3], r = n->graine % nP, q = n->graine / nP;
    const uint8_t *off = w->nomt + 8, *ch = off + tot * 2;
    const uint32_t chsz = w->nomt_sz - 8 - tot * 2, ia = base + (r * 1009 + 7) % nP, ib = tot - nS + (q * 613 + r * 211 + 3) % nS;
    const uint32_t a = off[ia * 2] | off[ia * 2 + 1] << 8, b = off[ib * 2] | off[ib * 2 + 1] << 8;
    if (a >= chsz || b >= chsz) return pe->nom;
    snprintf(buf, sizeof buf, "%.20s %.24s", (const char *)ch + a, (const char *)ch + b);
    return buf;
}

/* Dialogue avec l'interlocuteur (E.talk) : un exemplaire dit une réplique au hasard, ou toutes dans l'ordre
   (PERS_CONV) ; « NOM\tTEXTE » : réplique dite par NOM (le joueur) */
static void open_dialog(void) {
    if (E.talk < 0) return;
    const perso_t *pe = &E.w.persos[E.npcs[E.talk].perso];
    const char *nm = pnj_nom(E.talk);   /* le nom de l'exemplaire (tiré, ou celui du personnage) */
    snprintf(D.right, sizeof D.right, "%s", nm);
    const perso_vue_t pv = perso_vue(E.npcs[E.talk].perso, E.npcs[E.talk].var);   /* (la recrue de l'héroïne choisie : en Sera) */
    make_portrait(1, pv.sheet, pv.remap);   /* dans ses couleurs (palette PVAR) */
    D.n = 0;
    {   /* île : l'habitant enlevé, retrouvé (ile.c) : ses mots, puis la récompense et son retour */
        const char *l1, *l2; int rec;
        if (ile_enleve_lignes(E.talk, &l1, &l2, &rec)) {
            snprintf(D.who[0], sizeof D.who[0], "%s", nm); D.text[0] = l1; D.n = 1;
            if (l2) { snprintf(D.who[1], sizeof D.who[1], "%s", nm); D.text[1] = l2; D.n = 2; }
            H.apres = rec ? 2 : 0;
            H.dialog = 1; H.line = 0; H.line_t0 = H.now; E.has_dest = 0; parle_maj = 1;
            return;
        }
    }
    /* Sans répliques à lui : une générique, celle de son métier dans un intérieur, sinon celle des rues (REPL) */
    static char gen[256];
    const int metier = int_metier_pnj(E.talk);
    /* Réplique 18+ (premier octet 1, édition unique) : sautée tant que le 18+ est verrouillé, sa marque jamais montrée */
    #define LIGNE_VUE(l) (!((l)[0] == 1 && verrou18()))
    int vues = 0;
    const char *l = pe->lines;
    for (int i = 0; i < pe->nlines; i++, l += strlen(l) + 1) vues += LIGNE_VUE(l);
    if (!vues) {
        snprintf(D.who[0], sizeof D.who[0], "%s", nm);
        D.text[0] = repl_dire(E.talk, metier, gen, sizeof gen) ? gen : "…"; D.n = 1;   /* lot du métier ou de la zone (repliques.c) */
    }
    /* Un commerçant de l'intérieur : ce qu'il propose, une fois sa réplique dite */
    const int bc = E.in.on && E.in.bat < E.w.nbats ? E.w.bats[E.in.bat].comm : 0;
    const comm_t *cm = bc && bc <= E.w.ncomms ? &E.w.comms[bc - 1] : NULL;
    H.apres = 0;
    if (cm && metier && (cm->qui >> (metier - 1) & 1) && (cm->n || cm->eq || cm->rachete || cm->soins || cm->verre)) {
        H.apres = 1; SHOP.comm = bc - 1; SHOP.npc = E.talk; snprintf(SHOP.nom, sizeof SHOP.nom, "%s", nm);
    }
    int first = 0, count = vues ? pe->nlines : 0;
    l = pe->lines;
    if (count && !(pe->flags & PERS_CONV)) {
        /* Au hasard, parmi ses propres répliques (sans tabulation) s'il en a */
        int own = 0;
        for (int i = 0; i < count; i++, l += strlen(l) + 1) own += LIGNE_VUE(l) && !strchr(l, '\t');
        int pick = (int)(H.now * 1000) % (own ? own : vues);
        l = pe->lines;
        for (first = 0; first < count; first++, l += strlen(l) + 1) if (LIGNE_VUE(l) && (!own || !strchr(l, '\t')) && pick-- == 0) break;
        count = 1;
    }
    l = pe->lines;
    for (int i = 0; i < first; i++) l += strlen(l) + 1;
    for (int i = 0; i < count && D.n < DLG_MAX; i++, l += strlen(l) + 1) {
        if (!LIGNE_VUE(l)) continue;
        const char *s0 = l + (l[0] == 1), *tab = strchr(s0, '\t');
        if (tab) {
            int m = (int)(tab - s0); if (m > 23) m = 23;
            memcpy(D.who[D.n], s0, (size_t)m); D.who[D.n][m] = 0; D.text[D.n] = tab + 1;
        } else { snprintf(D.who[D.n], sizeof D.who[D.n], "%s", nm); D.text[D.n] = s0; }
        D.n++;
    }
    #undef LIGNE_VUE
    H.dialog = 1; H.line = 0; H.line_t0 = H.now; E.has_dest = 0; parle_maj = 1;
}

/* ---------- Commerce ---------- */
static int shop_ok_vente(int k) { const comm_t *c = &E.w.comms[SHOP.comm]; return ITEMS[k].count > 0 && item_vu(&ITEMS[k]) && ITEMS[k].prix > 0 && ITEMS[k].type >= 0 && ITEMS[k].type < 8 && (c->rachete >> ITEMS[k].type & 1); }
/* Ce qu'on achète ici (SHOP.liste ; boutique_liste de battle.c : équipement compris, selon la gamme du bâtiment) */
static void shop_liste(void) { SHOP.nl = boutique_liste(SHOP.comm, E.in.on ? E.in.bat : -1, SHOP.liste, MAX_ITEMS); }
/* Amélioration (SHOP.mode 2, 06/11/2026) : l'équipement du sac ou porté par un membre, pas encore au maximum. Ce que le joueur possède (au sac, ou porté par la bande) d'abord ; à la suite, grisé et
   sans amélioration possible, ce que portent les membres pas encore arrivés (07/11/2026, demande de l'utilisateur : on ne distinguait pas ce qu'on avait ; upPossede de l'émulateur) */
static const uint8_t EQ_UP_CRISTAUX[EQ_UP_MAX] = { 1, 2, 4 };
static const uint8_t EQ_UP_PRIX[EQ_UP_MAX] = { 25, 50, 100 };
static int up_ok(int k) {
    if (!eq_item(k) || (eq_up_niv(k) >= EQ_UP_MAX && !SHOP.forge)) return 0;   /* la forge : aussi au maximum (pour l'élément) */
    if (ITEMS[k].count > 0) return 1;
    for (int m = 0; m < NMEMB; m++) for (int sl = 0; sl < 3; sl++) if (E.eq[m][sl] == k) return 1;
    return 0;
}
static int up_possede(int k) {
    if (ITEMS[k].count > 0) return 1;
    for (int m = 0; m < NMEMB; m++) if (in_band(m)) for (int sl = 0; sl < 3; sl++) if (E.eq[m][sl] == k) return 1;
    return 0;
}
static int up_cristal(void) { return item_loot(); }
/* Membres de la bande qui portent l'objet k : leur nombre ; t : leurs noms (« SERA, VARN ») (portePar de l'émulateur) */
static int porte_par(int k, char *t, int cap) {
    int n = 0, o = 0;
    if (t && cap > 0) t[0] = 0;
    if (k < 0 || k == EQ_AUCUN || !eq_item(k)) return 0;
    for (int m = 0; m < NMEMB; m++) {
        if (!in_band(m) || (E.eq[m][0] != k && E.eq[m][1] != k && E.eq[m][2] != k)) continue;
        if (t && o < cap - 1) { const int w = snprintf(t + o, (size_t)(cap - o), "%s%s", n ? ", " : "", PARTY[m].name); o = w < 0 ? o : o + w > cap - 1 ? cap - 1 : o + w; }
        n++;
    }
    return n;
}
/* VENDRE : un équipement porté par la bande sans autre exemplaire au sac, montré sans pouvoir se vendre (08/11/2026, demande de
   l'utilisateur : empêcher de vendre l'équipement porté, et le dire) ; à la fin de la liste */
static int vente_porte(int k) {
    const comm_t *c = &E.w.comms[SHOP.comm];
    return ITEMS[k].count <= 0 && eq_item(k) && item_vu(&ITEMS[k]) && ITEMS[k].type >= 0 && ITEMS[k].type < 8 && (c->rachete >> ITEMS[k].type & 1) && porte_par(k, NULL, 0) > 0;
}
/* Forge d'un ferrailleur (08/11/2026, demande de l'utilisateur : donner des éléments aux armes et à l'équipement contre des
   objets rares, chez les ferrailleurs) : Guilde des ferrailleurs, étal du ferrailleur (nom de PORT). AMÉLIORER y devient FORGE :
   la même liste, sans s'arrêter au maximum, et chaque objet propose AMÉLIORER ou ÉLÉMENT ; ÉLÉMENT (SHOP.mode 4) : les noyaux
   élémentaires du sac, un noyau et ELEM_PRIX % du prix de l'objet (forgeIci, forgeRefus, forgePrix de l'émulateur) */
static int ferrailleur_ici(void) { return E.in.on && E.in.bat >= 0 && E.in.bat < E.w.nbats && strstr(E.w.bats[E.in.bat].nom, "FERRAILLEUR") != NULL; }
static int noyau_ok(int k) { return ITEMS[k].noyau && ITEMS[k].count > 0 && item_vu(&ITEMS[k]); }
static int forge_prix(int k) { return (ITEMS[k].prix * ELEM_PRIX + 50) / 100; }
/* Le noyau k sur l'objet SHOP.cible : NULL s'il le peut, sinon pourquoi pas (même élément ; arme d'un élément qu'aucun ennemi
   ne craint : TERRE, MÉTÉORE) */
static const char *forge_refus(int k) {
    const int el = ITEMS[k].noyau - 1;
    if (eq_el_de(SHOP.cible) == el) return "Déjà de cet élément";
    if (eq_slot(SHOP.cible) == EQ_ARME && !element_utile(el)) return "Aucun ennemi ne le craint";
    return NULL;
}
/* « ÉLÉMENT FEU » (une arme), « RÉSISTE FEU » (armure, accessoire) ; 0 : sans élément */
static int elem_etiquette(int k, char *t, int cap) {
    const int el = k != EQ_AUCUN ? eq_el_de(k) : -1;
    if (el < 0) { t[0] = 0; return 0; }
    snprintf(t, (size_t)cap, "%s %s", eq_slot(k) == EQ_ARME ? "ÉLÉMENT" : "RÉSISTE", ELEM_NOM[el]);
    return 1;
}
/* ÉLÉMENT : la liste des noyaux pour l'objet k ; retour à la forge sur la ligne de l'objet */
static void forge_element(int k) {
    SHOP.cible = k; SHOP.mode = 4; H.sub_sel = 0; H.sub_scroll = 0;
    son_jouer(SON_VALIDER, 0.3f, 1.1f);
}
static int shop_rows(void);
static int shop_item(int row);
static void forge_retour(void) {
    const int i = SHOP.cible;
    SHOP.mode = 2; H.sub_sel = 0; H.sub_scroll = 0;
    for (int r = 0, n = shop_rows(); r < n; r++) if (shop_item(r) == i) { H.sub_sel = r; break; }
}
static int shop_rows(void) {
    if (SHOP.comm < 0 || SHOP.comm >= E.w.ncomms) return 0;
    int n = 0;
    if (SHOP.mode == 2) { for (int k = 0; k < n_items; k++) n += up_ok(k); return n; }
    if (SHOP.mode == 4) { for (int k = 0; k < n_items; k++) n += noyau_ok(k); return n; }
    if (SHOP.mode == 3) { for (int k = 0; k < VK_N; k++) n += E.veh_up[k] < vk_max(k); return n; }
    if (SHOP.mode == 0) n = SHOP.nl;
    else for (int k = 0; k < n_items; k++) n += shop_ok_vente(k) + vente_porte(k);   /* puis l'équipement porté, sans autre exemplaire */
    return n;
}
static int shop_item(int row) {
    if (SHOP.mode == 2) { for (int p = 1; p >= 0; p--) for (int k = 0; k < n_items; k++) if (up_ok(k) && up_possede(k) == p && row-- == 0) return k; return -1; }
    if (SHOP.mode == 4) { for (int k = 0; k < n_items; k++) if (noyau_ok(k) && row-- == 0) return k; return -1; }
    if (SHOP.mode == 3) { for (int k = 0; k < VK_N; k++) if (E.veh_up[k] < vk_max(k) && row-- == 0) return k; return -1; }
    if (SHOP.mode == 0) return row >= 0 && row < SHOP.nl ? SHOP.liste[row] : -1;
    for (int k = 0; k < n_items; k++) if (shop_ok_vente(k) && row-- == 0) return k;
    for (int k = 0; k < n_items; k++) if (vente_porte(k) && row-- == 0) return k;
    return -1;
}
static int shop_prix(int k) {
    if (SHOP.mode == 4) return forge_prix(SHOP.cible);
    if (SHOP.mode == 2) return eq_up_niv(k) < EQ_UP_MAX ? (ITEMS[k].prix * EQ_UP_PRIX[eq_up_niv(k)] + 50) / 100 : 0;
    return SHOP.mode == 3 ? vk_prix(k) : SHOP.mode == 0 ? ITEMS[k].prix : ITEMS[k].prix / 2;
}
/* Achat ou vente après confirmation (03/11/2026, demande de l'utilisateur : une lampe torche vendue par erreur) : boîte de
   choix avec le prix, les crédits et le sac après ; un équipement à acheter se montre d'abord sur la fiche de comparaison
   du menu ÉQUIP. (draw_equip_fiche), face à ce que porte un membre de la bande (haut, bas ou toucher la colonne de gauche :
   un autre) ; shopDemander, shopFiche de l'émulateur */
static int shop_k = -1;   /* objet dont l'achat ou la vente attend sa confirmation (CHX.shop) */
static int shop_possible(int k) {
    if (SHOP.mode == 3) { if (E.credits >= vk_prix(k)) return 1; toast("Pas assez de crédits"); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
    if (SHOP.mode == 4) {
        const char *r = forge_refus(k);
        if (r) { toast(r); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
        if (E.credits < forge_prix(SHOP.cible)) { toast("Pas assez de crédits"); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
        return 1;
    }
    if (SHOP.mode == 2) {
        const int c = up_cristal();
        if (!up_possede(k)) { toast("Vous ne l'avez pas"); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
        if (eq_up_niv(k) >= EQ_UP_MAX) { toast("Déjà au maximum"); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
        if (E.credits < shop_prix(k)) { toast("Pas assez de crédits"); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
        if (c < 0 || ITEMS[c].count < EQ_UP_CRISTAUX[eq_up_niv(k)]) { toast("Pas assez de cristaux froids"); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
        return 1;
    }
    if (SHOP.mode == 1 && ITEMS[k].count <= 0) {   /* l'exemplaire porté ne se vend pas */
        char pn[48], t[80];
        if (porte_par(k, pn, sizeof pn)) { snprintf(t, sizeof t, "Porté par %s : à retirer d'abord", pn); toast(t); son_jouer(SON_ANNULER, 0.45f, 1); }
        return 0;
    }
    if (SHOP.mode) return ITEMS[k].count > 0;
    if (E.credits < shop_prix(k)) { toast("Pas assez de crédits"); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
    if (ITEMS[k].count >= 99) { toast("Le sac en est plein"); son_jouer(SON_ANNULER, 0.45f, 1); return 0; }
    return 1;
}
static void shop_transaction(int k) {
    char t[80];
    if (k < 0 || k >= n_items || !shop_possible(k)) return;
    const int px = shop_prix(k);
    if (SHOP.mode == 4) {   /* la forge : le noyau k donne son élément à l'objet SHOP.cible ; retour à la forge */
        const int i = SHOP.cible, el = ITEMS[k].noyau - 1;
        E.credits -= px; ITEMS[k].count--; eq_el_poser(i, el);
        snprintf(t, sizeof t, "%s · %s", ITEMS[i].name, ELEM_NOM[el]);
        toast(t); son_jouer(SON_VALIDER, 0.5f, 1.2f);
        forge_retour();
        return;
    }
    if (SHOP.mode == 3) {
        E.credits -= px; E.veh_up[k]++;
        if (vk_max(k) > 1) snprintf(t, sizeof t, "%s +%d posé", vk_nom(k), E.veh_up[k]); else snprintf(t, sizeof t, "%s posé", vk_nom(k));
        toast(t); son_jouer(SON_VALIDER, 0.5f, 1.2f);
        return;
    }
    if (SHOP.mode == 2) {
        const int c = up_cristal(), niv = eq_up_niv(k);
        E.credits -= px; ITEMS[c].count -= EQ_UP_CRISTAUX[niv]; eq_up_poser(k, niv + 1);
        for (int m = 0; m < NMEMB; m++) party_refresh(m);
        snprintf(t, sizeof t, "%s +%d", ITEMS[k].name, niv + 1);
        toast(t); son_jouer(SON_VALIDER, 0.5f, 1.2f);
        return;
    }
    if (SHOP.mode == 0) { E.credits -= px; ITEMS[k].count++; snprintf(t, sizeof t, "%s · −%d CR", ITEMS[k].name, px); }
    else { ITEMS[k].count--; E.credits += px; snprintf(t, sizeof t, "%s vendu · +%d CR", ITEMS[k].name, px); }
    toast(t); son_jouer(SON_VALIDER, 0.4f, SHOP.mode == 0 ? 1.05f : 0.9f);
}
static void shop_demander(int k) {
    char opts[3][48];
    if (SHOP.mode == 4) {   /* forge : le noyau k sur l'objet SHOP.cible */
        const int i = SHOP.cible, el = ITEMS[k].noyau - 1, av = eq_el_de(i);
        snprintf(opts[0], sizeof opts[0], "DONNER · %d CR", forge_prix(i));
        snprintf(opts[1], sizeof opts[1], "ANNULER");
        hud_choice(opts, 2);
        CHX.shop = 1; shop_k = k;
        snprintf(CHX.titre, sizeof CHX.titre, "%s : %s ?", ITEMS[i].name, ELEM_NOM[el]);
        if (av >= 0) snprintf(CHX.texte[0], sizeof CHX.texte[0], "%s %s jusqu'ici", eq_slot(i) == EQ_ARME ? "Frappe de" : "Résiste à", ELEM_NOM[av]);
        else snprintf(CHX.texte[0], sizeof CHX.texte[0], "%s %s", eq_slot(i) == EQ_ARME ? "Frappera de" : "Résistera à", ELEM_NOM[el]);
        snprintf(CHX.texte[1], sizeof CHX.texte[1], "1 %s (%d au sac)", ITEMS[k].name, ITEMS[k].count);
        return;
    }
    if (SHOP.mode == 2 && SHOP.forge) {   /* forge : AMÉLIORER (pas au maximum), ÉLÉMENT, ANNULER (SHOP.fact) */
        const int niv = eq_up_niv(k), c = up_cristal(), nc = c >= 0 ? ITEMS[c].count : 0;
        int n = 0, nn = 0;
        for (int i = 0; i < n_items; i++) if (noyau_ok(i)) nn += ITEMS[i].count;
        if (niv < EQ_UP_MAX) { snprintf(opts[n], sizeof opts[n], "AMÉLIORER · %d CR", shop_prix(k)); SHOP.fact[n++] = 0; }
        snprintf(opts[n], sizeof opts[n], "ÉLÉMENT"); SHOP.fact[n++] = 1;
        snprintf(opts[n], sizeof opts[n], "ANNULER"); SHOP.fact[n++] = 2;
        hud_choice(opts, n);
        CHX.shop = 1; shop_k = k;
        snprintf(CHX.titre, sizeof CHX.titre, "%s +%d", ITEMS[k].name, niv);
        if (!elem_etiquette(k, CHX.texte[0], sizeof CHX.texte[0])) snprintf(CHX.texte[0], sizeof CHX.texte[0], "Sans élément");
        snprintf(CHX.texte[1], sizeof CHX.texte[1], "%d cristaux froids · %d noyaux", nc, nn);
        return;
    }
    if (SHOP.mode == 3) {
        snprintf(opts[0], sizeof opts[0], "POSER · %d CR", vk_prix(k));
        snprintf(opts[1], sizeof opts[1], "ANNULER");
        hud_choice(opts, 2);
        CHX.shop = 1; shop_k = k;
        snprintf(CHX.titre, sizeof CHX.titre, "%s ?", vk_nom(k));
        snprintf(CHX.texte[0], sizeof CHX.texte[0], "Vous aurez ensuite %d CR", E.credits - vk_prix(k));
        snprintf(CHX.texte[1], sizeof CHX.texte[1], "Elle reste si vous changez de véhicule");
        return;
    }
    if (SHOP.mode == 2) {
        const int niv = eq_up_niv(k), c = up_cristal(), av = c >= 0 ? ITEMS[c].count : 0;
        snprintf(opts[0], sizeof opts[0], "AMÉLIORER · %d CR", shop_prix(k));
        snprintf(opts[1], sizeof opts[1], "ANNULER");
        hud_choice(opts, 2);
        CHX.shop = 1; shop_k = k;
        snprintf(CHX.titre, sizeof CHX.titre, "%s +%d ?", ITEMS[k].name, niv + 1);
        snprintf(CHX.texte[0], sizeof CHX.texte[0], "Bonus × %d %%", (int)lroundf(100 * (1 + EQ_UP_X * (niv + 1))));
        snprintf(CHX.texte[1], sizeof CHX.texte[1], "%d cristaux froids (%d au sac)", EQ_UP_CRISTAUX[niv], av);
        return;
    }
    const int px = shop_prix(k), apres = SHOP.mode ? E.credits + px : E.credits - px, sac = ITEMS[k].count + (SHOP.mode ? -1 : 1);
    snprintf(opts[0], sizeof opts[0], "%s · %d CR", SHOP.mode ? "VENDRE" : "ACHETER", px);
    snprintf(opts[1], sizeof opts[1], "ANNULER");
    hud_choice(opts, 2);
    CHX.shop = 1; shop_k = k;
    snprintf(CHX.titre, sizeof CHX.titre, "%s ?", ITEMS[k].name);
    snprintf(CHX.texte[0], sizeof CHX.texte[0], "Vous aurez ensuite %d CR", apres);
    char pn[48];
    if (SHOP.mode == 1 && porte_par(k, pn, sizeof pn)) snprintf(CHX.texte[1], sizeof CHX.texte[1], "Celui que porte %s reste", pn);   /* on vend un exemplaire du sac */
    else if (sac) snprintf(CHX.texte[1], sizeof CHX.texte[1], "Dans le sac ensuite : %d", sac);
    else snprintf(CHX.texte[1], sizeof CHX.texte[1], "Il n'en restera plus dans le sac");
}
static void shop_fiche(int k) {
    const int nb = band_count();
    SHOP.fiche = k; SHOP.m = 0;
    for (int r = 0; r < nb; r++) if (eq_aff(band_at(r), k) > 0) { SHOP.m = r; break; }   /* le premier qui peut le porter */
    H.sub_sel = 0;   /* OUI choisi d'avance */
}
static void shop_membre(int pas) { const int nb = band_count() > 0 ? band_count() : 1; SHOP.m = (SHOP.m + pas + nb) % nb; son_jouer(SON_VALIDER, 0.3f, 1.2f); }
static void shop_fiche_choix(int non) {
    const int k = SHOP.fiche;
    SHOP.fiche = -1;
    if (non) { son_jouer(SON_ANNULER, 0.4f, 1); return; }
    shop_transaction(k);
}
/* Ce que propose le commerçant : acheter, vendre, soins de l'équipe, un verre ; partir */
static void commerce_ouvrir(void) {
    if (SHOP.comm < 0 || SHOP.comm >= E.w.ncomms) return;
    const comm_t *c = &E.w.comms[SHOP.comm];
    char o[4][48]; int n = 0;
    shop_liste();
    if (arene_ici()) { snprintf(o[n], 48, "COMBATTRE"); SHOP.act[n++] = 5; }   /* le maître de l'arène (arene.c) */
    const int conc = concession_ici();   /* la concession : ses véhicules, et les pièces de celui de Sera (garage.c) */
    if (conc) { snprintf(o[n], 48, "VÉHICULES"); SHOP.act[n++] = 7; }
    if (conc && garage_perso() >= 0) { snprintf(o[n], 48, "PIÈCES"); SHOP.act[n++] = 8; }
    if (SHOP.nl && !conc) { snprintf(o[n], 48, "ACHETER"); SHOP.act[n++] = 0; }
    if (c->rachete && !conc) { snprintf(o[n], 48, "VENDRE"); SHOP.act[n++] = 1; }
    if (((c->qui >> METIER_MECANO & 1) || ferrailleur_ici()) && n < 3) { snprintf(o[n], 48, "%s", ferrailleur_ici() ? "FORGE" : "AMÉLIORER"); SHOP.act[n++] = 6; }   /* un mécanicien (équipement) ; un ferrailleur : la forge, aussi les éléments */
    if (c->soins && n < 3) { snprintf(o[n], 48, "SOINS · %d CR", prix_soins(c->soins)); SHOP.act[n++] = 2; }
    if (c->verre && n < 3) { snprintf(o[n], 48, "UN VERRE · %d CR", prix_soins(c->verre)); SHOP.act[n++] = 3; }
    snprintf(o[n], 48, "PARTIR"); SHOP.act[n++] = 4;
    hud_choice(o, n);
    CHX.commerce = 1; snprintf(CHX.titre, sizeof CHX.titre, "%s · %d CR", SHOP.nom, E.credits);
}
/* Soins (part 1 : PV et PM au maximum, toute la bande, réserve comprise) ou un verre (part 0,2 des PV de l'équipe au
   combat) ; prix au niveau de l'équipe (prix_soins de battle.c ; service de l'émulateur, à garder identique) */
static void commerce_service(int base, float part, const char *msg) {
    char t[64];
    const int prix = prix_soins(base);
    if (E.credits < prix) { snprintf(t, sizeof t, "Pas assez de crédits (%d CR)", E.credits); toast(t); son_jouer(SON_ANNULER, 0.45f, 1); return; }
    E.credits -= prix;
    for (int i = 0; i < NMEMB; i++) {
        if (!(part >= 1 ? in_band(i) : in_team(i) && PARTY[i].hp > 0)) continue;   /* soins : ceux qui sont à terre se relèvent ; un verre ne relève personne */
        member_t *m = &PARTY[i];
        m->hp += (int)ceilf(m->max * part); if (m->hp > m->max) m->hp = m->max;
        if (part >= 1) m->mp = m->maxmp;
    }
    son_jouer(SON_SOIN, 0.5f, 1.1f);
    snprintf(t, sizeof t, "%s · −%d CR", msg, prix); toast(t);
}
static void commerce_choix(int k) {
    const comm_t *c = &E.w.comms[SHOP.comm];
    switch (SHOP.act[k]) {
    case 0: case 1: SHOP.mode = SHOP.act[k]; SHOP.forge = 0; shop_liste(); H.menu = 1; open_sub(SUB_SHOP); E.has_dest = 0; break;
    case 6: SHOP.mode = 2; SHOP.forge = ferrailleur_ici(); shop_liste(); H.menu = 1; open_sub(SUB_SHOP); E.has_dest = 0; break;
    case 7: concession_ouvrir(); break;
    case 8: SHOP.mode = 3; SHOP.forge = 0; shop_liste(); H.menu = 1; open_sub(SUB_SHOP); E.has_dest = 0; break;
    case 2: commerce_service(c->soins, 1, "Équipe soignée"); break;
    case 3: commerce_service(c->verre, 0.2f, "À la vôtre"); break;
    case 5: arene_proposer(); break;   /* COMBATTRE : le choix du rang */
    default: break;
    }
}

static int talk_is_vehicle(void);
/* Conduite à deux mains (écran natif 466) : à gauche du milieu, le pouce gauche avance (vers le haut) ou recule (vers
   le bas) ; à droite, le pouce droit braque (vers la gauche ou la droite). Chaque doigt prend pour origine l'endroit
   où il se pose : plein gaz à DRV_GAZ px, plein braquage à DRV_VOL px. Au repos, les deux rails sont dessinés à leur
   place (GAZ_X, VOL_X). Un doigt posé sur un bouton (sortie, pause, tir) le touche au lieu de conduire ; posé sur un
   bouton d'altitude (vaisseau en vol, au milieu en bas), il le tient.
   Joystick (SYSTÈME › COMMANDES, d'un seul doigt : page web pour PC, souris ; celui du 26/10/2026) : cercle fixe sous le
   véhicule (JOY_X, JOY_Y, rayon JOY_R) ; le doigt se pose dessus (à moins de 1,6 rayon), puis peut en sortir : la poignée
   reste au bord. Sur les côtés il braque (zone morte de 15 %, même courbe que le volant) ; les gaz suivent l'écart de la
   poignée dans toutes les directions (zone morte de 15 %, plein à 80 % : braquer ne coupe pas l'accélération), vers le
   bas frein puis marche arrière. Boutons d'altitude à droite du joystick. */
#define DRV_GAZ 50.0f
#define DRV_VOL 60.0f
#if ENG_DYN
/* Plein écran (téléphone, 02/11/2026) : les commandes en bas, sous les pouces (le haut de l'écran, ce qu'on lit), en course
   comme hors course : gaz à gauche, volant à droite, boutons d'altitude entre les deux ; joystick en bas au milieu, ses
   boutons d'altitude à sa droite. Au-dessus des pouces : le tir (course), l'état de Sera et DESCENDRE (hors course) */
#define GAZ_X 100.0f
#define GAZ_Y (UH - 170)
#define VOL_X (UW - 160)
#define VOL_Y (UH - 120)
#define ALT_X2 ((GAZ_X + VOL_X) / 2)
#define ALT_UP_Y2 (UH - 250)
#define ALT_DN_Y2 (UH - 190)
#define JOY_X (UW / 2)
#define JOY_Y (UH - 110)
#define JOY_R 60.0f
#define ALT_JX (UW / 2 + 130)
#define ALT_JUP (UH - 140)
#define ALT_JDN (UH - 82)
#else
#define GAZ_X 125.0f
#define GAZ_Y 305.0f
#define VOL_X 345.0f
#define VOL_Y 320.0f
#define ALT_X2 233.0f
#define ALT_UP_Y2 336.0f
#define ALT_DN_Y2 394.0f
#define JOY_X 233.0f
#define JOY_Y 372.0f
#define ALT_JX 338.0f
#define ALT_JUP 325.0f
#define ALT_JDN 380.0f
#define JOY_R 50.0f
#endif
#define ALT_R 24.0f
/* Joystick (aussi en course avec POINTER : la course n'a pas de point à viser) ; POINTER hors course : le doigt vise le
   sol, le véhicule y va (pilote_doigt de pilote.c) */
static int joy_on(void) { return E.commandes == COMMANDES_JOYSTICK; }
static int pointer_on(void) { return E.commandes == COMMANDES_POINTER; }
/* POINTER en course (04/11/2026, demande de l'utilisateur : « une commande comme pour les personnages, en pointant
   n'importe où sur l'écran » ; avant, le joystick) : la direction du doigt vu du véhicule à l'écran ; écart avec l'avant
   (le haut de l'écran) : le volant, plein à POINTER_ANGLE ; au-dessus du véhicule les gaz, en dessous le frein puis la
   marche arrière, pleins à POINTER_PLEIN du rayon de l'écran ; rien à moins de POINTER_MORT px (driveInput de l'émulateur) */
#define POINTER_ANGLE 0.9f    /* rad (52°) */
#define POINTER_PLEIN 0.35f
#define POINTER_MORT 12.0f
#define ALT_X (joy_on() ? ALT_JX : ALT_X2)
#define ALT_UP_Y (joy_on() ? ALT_JUP : ALT_UP_Y2)
#define ALT_DN_Y (joy_on() ? ALT_JDN : ALT_DN_Y2)
static int alt_visible(void) { return ship_alt_ok(); }
void hud_joy_centre(float *x, float *y) { *x = JOY_X + ui_ox; *y = JOY_Y + ui_oy; }   /* dans l'image (les touchers y arrivent) */
/* Doigts qui conduisent (deux au plus), suivis d'une image à l'autre par leur position (leur rang change quand l'autre
   se lève) ; rôle : 0 gaz, 1 volant, 2 bouton d'altitude, 3 rien (bouton touché, ou doigt remplacé), 4 joystick */
typedef struct { int on, role; float x0, y0, x, y; } doigt_t;
static doigt_t DG[2];

/* Menu de course (courseMenu de l'émulateur), ouvert par le drapeau de la ligne ou par l'instruction course du scénario
   (ses réglages en sont les valeurs de départ). Trois pages, onglets en haut (ligne 0 : gauche / droite change de page) :
   PILOTE (le véhicule : aperçu qui tourne sur lui-même, vitesse, accélération, adhérence ; sa couleur : ses livrées, les
   véhicules de même planche), RIVAUX (nombre, niveau, véhicule de chacun), ÉPREUVE (tracé : les pistes de la même
   ligne, avec leur plan ; tours ; armes). Seuls les véhicules utilisables en course (VEH2). Départ : la course ;
   retour : annulée (place 255 pour le scénario) */
static const char *NIVEAUX[3] = { "FACILE", "NORMAL", "DIFFICILE" };
static const char *const RM_PAGES[3] = { "PILOTE", "RIVAUX", "ÉPREUVE" };
#define RM_NR (COURSE_N - 1)   /* adversaires au plus */
#define RM_TR 8                /* tracés au plus (PISTES_MAX de course.c) */
static const int RM_LIGNES[3] = { 3, 3 + RM_NR, 4 };
/* ord : véhicules de course groupés par planche (le modèle m : ord[mod0[m]] .. ord[mod0[m] + modn[m] - 1]) ; riv :
   véhicules des adversaires ; trace : index des tracés (course_piste) ; t2 : second doigt posé (tir) */
static struct {
    int on, page, row, tours, nia, niv, armes, t2, n, nmod, mod, coul, ntr, tr; float t0, chrono;
    int mode;   /* RM_COURSE ; menu des véhicules (hud_vehicules) : RM_DON (offert), RM_BOUTIQUE (à vendre) */
    uint8_t fige_trace, fige_riv, fige_armes, fige_tours, fige_niv;   /* réglages imposés par le scénario */
    uint16_t ord[MAX_PERSOS]; uint8_t mod0[MAX_PERSOS], modn[MAX_PERSOS]; int riv[RM_NR], trace[RM_TR];
} RM ENG_FROID;
enum { RM_COURSE, RM_DON, RM_BOUTIQUE };
/* Couleurs du modèle m : ses livrées (menu de course) ; menu des véhicules : chaque livrée, puis ses palettes (PVAR) */
static int rm_ncoul(int m) {
    if (!RM.mode) return RM.modn[m];
    int n = 0;
    for (int c = 0; c < RM.modn[m]; c++) n += 1 + E.w.persos[RM.ord[RM.mod0[m] + c]].nvar;
    return n;
}
/* Personnage de la couleur choisie, et sa palette (*var ; 0 : ses couleurs) */
static int rm_couleur(int *var) {
    *var = 0;
    if (!RM.mode) return RM.ord[RM.mod0[RM.mod] + RM.coul];
    int c = RM.coul;
    for (int q = 0; q < RM.modn[RM.mod]; q++) {
        const int p = RM.ord[RM.mod0[RM.mod] + q], nv = 1 + E.w.persos[p].nvar;
        if (c < nv) { *var = c; return p; }
        c -= nv;
    }
    return RM.ord[RM.mod0[RM.mod]];
}
static int rm_perso(void) { int v; return rm_couleur(&v); }
/* Adversaires proposés : un par modèle autre que celui du joueur (une de ses livrées, tirée à l'ouverture), puis les
   autres véhicules de course */
static void rm_rivaux(void) {
    const int moi = rm_perso(), tirage = (int)(H.now * 997);
    int n = 0;
    for (int m = 0; m < RM.nmod && n < RM_NR; m++) if (m != RM.mod) RM.riv[n++] = RM.ord[RM.mod0[m] + (tirage + m * 7) % RM.modn[m]];
    for (int i = 0; i < RM.n && n < RM_NR; i++) {
        int pris = RM.ord[i] == moi;
        for (int k = 0; k < n; k++) pris |= RM.riv[k] == RM.ord[i];
        if (!pris) RM.riv[n++] = RM.ord[i];
    }
    for (; n < RM_NR; n++) RM.riv[n] = RM.ord[n % RM.n];
}
/* Aperçus du menu (k : 0 le véhicule du joueur, 1 à RM_NR les adversaires) : direction dir de la planche (première
   image), réduite d'un facteur entier pour tenir dans size × size ; refaits quand ils changent. Toutes les directions
   d'un véhicule ont le même cadre : chacune calée sur son point d'ancrage (PIVO, sinon la cellule), rognée à l'union des
   pixels des 8 directions ainsi calées. Le véhicule tourne sur place, à taille constante (sans ce cadre commun, chaque
   direction rognée à ses pixels sautait d'une image à l'autre). */
#define RM_AP 96
static uint16_t *rm_px; static uint8_t *rm_mk; static int rm_cle[1 + RM_NR], rm_w[1 + RM_NR], rm_h[1 + RM_NR];
static struct { int perso, x0, y0, x1, y1, ox[8], oy[8]; } rm_cadre[1 + RM_NR];   /* perso + 1 ; cadre en coordonnées calées */
/* Pixels des aperçus dans le tampon commun (img_emprunter de world.c ; 01/11/2026 : réservés à la première ouverture du
   menu, ils ne trouvaient plus leur place sur la montre) ; repris par une image : aperçus refaits */
uint32_t hud_apercus_taille(void) { return (uint32_t)(1 + RM_NR) * RM_AP * RM_AP * 3; }
void hud_apercus_perdus(void) { rm_px = NULL; rm_mk = NULL; memset(rm_cle, 0, sizeof rm_cle); }
/* (var : palette PVAR du véhicule, 0 ses couleurs ; menu des véhicules) */
static int rm_apercu_v(int k, int perso, int var, int dir, int size) {
    if (!rm_px) {
        if (!(rm_px = (uint16_t *)img_emprunter(TC_APERCUS, hud_apercus_taille()))) return 0;
        rm_mk = (uint8_t *)(rm_px + (1 + RM_NR) * RM_AP * RM_AP);
    }
    if (rm_cle[k] == (perso * 256 + var) * 8 + dir + 1) return 1;
    const perso_t *pe = &E.w.persos[perso];
    if (pe->sheet < 0) return 0;
    const sheet_t *sh = &E.w.sheets[pe->sheet];
    const int sw = sh->fw * sh->frames, d = dir & 7;
    if (rm_cadre[k].perso != perso + 1) {   /* cadre commun des 8 directions */
        int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
        for (int q = 0; q < 8; q++) {
            const int ox = sh->has_pv ? (int)lroundf(sh->pv[q][0] / 2.0f) : 0, oy = sh->has_pv ? (int)lroundf(sh->pv[q][1] / 2.0f) : 0;
            rm_cadre[k].ox[q] = ox; rm_cadre[k].oy[q] = oy;
            const uint8_t *b = sheet_block(pe->sheet, q, 2);
            if (!b) continue;
            for (int y = 0; y < sh->fh; y++) for (int x = 0; x < sh->fw; x++)
                if (b[y * sw + x]) { if (x - ox < x0) x0 = x - ox; if (x - ox > x1) x1 = x - ox; if (y - oy < y0) y0 = y - oy; if (y - oy > y1) y1 = y - oy; }
        }
        if (x1 < x0) return 0;
        rm_cadre[k].perso = perso + 1; rm_cadre[k].x0 = x0; rm_cadre[k].y0 = y0; rm_cadre[k].x1 = x1; rm_cadre[k].y1 = y1;
    }
    const uint8_t *blk = sheet_block(pe->sheet, d, 2);
    if (!blk) return 0;
    const int x0 = rm_cadre[k].x0, y0 = rm_cadre[k].y0, ox = rm_cadre[k].ox[d], oy = rm_cadre[k].oy[d];
    const int bw = rm_cadre[k].x1 - x0 + 1, bh = rm_cadre[k].y1 - y0 + 1, big = bw > bh ? bw : bh, f = (big + size - 1) / size, w = (bw + f - 1) / f, h = (bh + f - 1) / f;
    uint16_t *px = rm_px + k * RM_AP * RM_AP; uint8_t *mk = rm_mk + k * RM_AP * RM_AP;
    const uint8_t *rmp = E.w.remap[perso_remap(perso, var)];
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const int cx = x0 + x * f + ox, cy = y0 + y * f + oy;
        const int id = cx >= 0 && cy >= 0 && cx < sh->fw && cy < sh->fh ? blk[cy * sw + cx] : 0;
        const uint8_t *c = E.w.pal[rmp[id]];
        px[y * w + x] = ui_rgb(((uint32_t)c[0] << 16) | (c[1] << 8) | c[2]); mk[y * w + x] = id != 0;
    }
    rm_cle[k] = (perso * 256 + var) * 8 + dir + 1; rm_w[k] = w; rm_h[k] = h;
    return 1;
}
static int rm_apercu(int k, int perso, int dir, int size) { return rm_apercu_v(k, perso, 0, dir, size); }
/* Aperçu d'un véhicule pour le garage de la carrière (accueil.c ; menu de course fermé : sa première place) */
int hud_apercu_vehicule(int perso, int dir, int size, const uint16_t **px, const uint8_t **mk, int *w, int *h) {
    return hud_apercu_vehicule_v(perso, 0, dir, size, px, mk, w, h);
}
/* (var : sa palette PVAR ; garage de la carrière, étape 2) */
int hud_apercu_vehicule_v(int perso, int var, int dir, int size, const uint16_t **px, const uint8_t **mk, int *w, int *h) {
    if (RM.on || perso < 0 || perso >= E.w.npersos || !rm_apercu_v(0, perso, var, dir, size < RM_AP ? size : RM_AP)) return 0;
    *px = rm_px; *mk = rm_mk; *w = rm_w[0]; *h = rm_h[0];
    return 1;
}
int hud_course_menu(int tours, int nia, int niv, int veh) {
    course_cancel();
    if (!course_choisir(E.sera.x, E.sera.z) || CR.on) { toast("Pas de piste de course ici"); return 0; }
    static uint8_t vu[MAX_PERSOS] ENG_FROID;   /* statique, en PSRAM : la pile du jeu fait 8 Ko sur l'ESP32 */
    memset(vu, 0, sizeof vu);
    RM.n = RM.nmod = 0;
    for (int p = 0; p < E.w.npersos && p < MAX_PERSOS; p++) {
        const perso_t *pe = &E.w.persos[p];
        if (vu[p] || !(pe->flags & PERS_VEHICULE) || pe->sheet < 0 || !E.w.veh_course[p] || !perso_present(p)) continue;
        RM.mod0[RM.nmod] = (uint8_t)RM.n; RM.modn[RM.nmod] = 0;
        for (int q = p; q < E.w.npersos && q < MAX_PERSOS; q++) {
            const perso_t *qe = &E.w.persos[q];
            if (!vu[q] && (qe->flags & PERS_VEHICULE) && qe->sheet == pe->sheet && E.w.veh_course[q] && perso_present(q)) { vu[q] = 1; RM.ord[RM.n++] = (uint8_t)q; RM.modn[RM.nmod]++; }
        }
        RM.nmod++;
    }
    if (!RM.n) { toast("Aucun véhicule de course"); return 0; }
    const int want = veh >= 0 ? veh : E.ride >= 0 ? E.npcs[E.ride].perso : -1;
    RM.mod = RM.coul = 0;
    for (int m = 0; m < RM.nmod; m++) for (int c = 0; c < RM.modn[m]; c++) if (RM.ord[RM.mod0[m] + c] == want) { RM.mod = m; RM.coul = c; }
    RM.ntr = course_traces(RM.trace); RM.tr = 0;
    for (int k = 0; k < RM.ntr; k++) if (RM.trace[k] == course_piste_active()) RM.tr = k;
    RM.tours = tours >= 1 && tours <= 9 ? tours : PISTE.tours;
    RM.nia = nia >= 0 && nia <= RM_NR ? nia : RM_NR;
    RM.niv = niv >= 0 && niv <= 2 ? niv : 1;
    RM.armes = 1; RM.page = 0; RM.row = 1; RM.t0 = H.now; RM.chrono = 0; RM.on = 1; H.menu = 0; H.sub = -1; E.has_dest = 0;
    RM.fige_trace = RM.fige_riv = RM.fige_armes = RM.fige_tours = RM.fige_niv = 0; RM.mode = RM_COURSE;
    memset(rm_cle, 0, sizeof rm_cle);
    rm_rivaux();
    son_jouer(SON_MENU, 0.35f, 1);
    return 1;
}
/* Menu des véhicules (01/11/2026 ; vehiculesOuvrir de l'émulateur, à garder identiques) : la page PILOTE du menu de course,
   seule (ni onglets, ni rivaux, ni épreuve) : aperçu qui tourne, nom, couleur (chaque livrée, puis ses palettes PVAR),
   vitesse et accélération par rapport au meilleur véhicule proposé, adhérence. Offert (code CODE_VOITURE) : PRENDRE ; à
   vendre (boutique) : prix (veh_prix) et crédits de l'équipe, ACHETER (« Crédits insuffisants » sinon). Le véhicule pris
   est à Sera (garage.c). Refusé (vm_refus) dans un intérieur (sauf une boutique : livré devant la porte), en combat, en
   course ou en mode course, pendant une scène, une recherche de la police ou une alarme, à la nage, et au volant d'un autre
   véhicule (vol permanent du système solaire compris) */
static const char *vm_refus(int boutique) {
    if (E.in.on && !boutique) return "Pas dans un bâtiment";
    if (BT.on) return "Pas pendant un combat";
    if (CR.on || RM.on || carriere_on()) return "Pas pendant une course";
    if (script_running() || mise_bloque() || MISE.fuite_on) return "Pas pendant une scène";
    if (POL.niveau || police_bloque()) return "Pas avec la police aux trousses";
    if (E.alarme.on) return "Pas pendant une alarme";
    if (E.ride >= 0) return "Descends d'abord du véhicule";
    if (E.nage) return "Pas dans l'eau";
    return NULL;
}
int hud_vehicules(int boutique, const int *liste, int nl) {
    const char *refus = vm_refus(boutique);
    if (refus) { toast(refus); return 0; }
    static uint8_t pr[MAX_PERSOS] ENG_FROID;   /* 1 : proposé, 2 : déjà rangé (statique, en PSRAM : la pile du jeu) */
    memset(pr, 0, sizeof pr);
    for (int p = 0; p < E.w.npersos && p < MAX_PERSOS; p++) pr[p] = (uint8_t)(!liste && veh_proposable(p));
    for (int i = 0; liste && i < nl; i++) if (liste[i] >= 0 && liste[i] < MAX_PERSOS && veh_proposable(liste[i])) pr[liste[i]] = 1;
    RM.n = RM.nmod = 0;
    for (int p = 0; p < E.w.npersos && p < MAX_PERSOS; p++) {   /* groupés par planche : le modèle et ses livrées */
        if (pr[p] != 1) continue;
        RM.mod0[RM.nmod] = (uint8_t)RM.n; RM.modn[RM.nmod] = 0;
        for (int q = p; q < E.w.npersos && q < MAX_PERSOS; q++)
            if (pr[q] == 1 && E.w.persos[q].sheet == E.w.persos[p].sheet) { pr[q] = 2; RM.ord[RM.n++] = (uint8_t)q; RM.modn[RM.nmod]++; }
        RM.nmod++;
    }
    if (!RM.n) { toast("Aucun véhicule à proposer"); return 0; }
    RM.mode = boutique ? RM_BOUTIQUE : RM_DON;
    RM.mod = RM.coul = 0;
    const int moi = garage_perso();   /* d'abord celui qu'elle a déjà, s'il est proposé */
    for (int m = 0; m < RM.nmod; m++) for (int c = 0, o = 0; c < RM.modn[m]; o += 1 + E.w.persos[RM.ord[RM.mod0[m] + c]].nvar, c++)
        if (RM.ord[RM.mod0[m] + c] == moi) { RM.mod = m; RM.coul = o; }
    RM.page = 0; RM.row = 1; RM.t0 = H.now; RM.chrono = 0; RM.on = 1; H.menu = 0; H.sub = -1; E.has_dest = 0;
    RM.fige_trace = RM.fige_riv = RM.fige_armes = RM.fige_tours = RM.fige_niv = 0;
    memset(rm_cle, 0, sizeof rm_cle);
    son_jouer(SON_MENU, 0.35f, 1);
    return 1;
}
int hud_vehicules_open(void) { return RM.on && RM.mode != RM_COURSE; }
#ifndef ESP_PLATFORM
void hud_course_menu_fermer_essai(void) { RM.on = 0; }   /* rendu_test (scenecams) : le menu de course d'une scène, fermé */
#endif
/* Essais (rendu_test) : menu des véhicules ouvert sur le modèle dont le nom contient nom, couleur c (0 : la première) */
int hud_vehicules_choisir(const char *nom, int c) {
    if (!hud_vehicules_open()) return 0;
    for (int m = 0; m < RM.nmod; m++)
        if (strstr(E.w.persos[RM.ord[RM.mod0[m]]].nom, nom)) { RM.mod = m; RM.coul = c >= 0 && c < rm_ncoul(m) ? c : 0; RM.row = 1; return 1; }
    return 0;
}
/* PRENDRE ou ACHETER : le véhicule de la couleur choisie, à Sera (garage_donner : elle au volant, ou livré devant la porte) */
static void vehicules_go(void) {
    int var;
    const int p = rm_couleur(&var), prix = RM.mode == RM_BOUTIQUE ? veh_prix(p) : 0;
    char t[72];
    if (prix > E.credits) { toast("Crédits insuffisants"); son_jouer(SON_ANNULER, 0.4f, 1); return; }
    RM.on = 0;
    const int r = garage_donner(p, var);
    if (!r) { toast("Pas de place pour le véhicule ici"); return; }
    E.credits -= prix;
    if (r == 2 && prix) snprintf(t, sizeof t, "%s · -%d CR · devant la porte", E.w.persos[p].nom, prix);
    else if (r == 2) snprintf(t, sizeof t, "%s t'attend devant la porte", E.w.persos[p].nom);
    else if (prix) snprintf(t, sizeof t, "%s · -%d CR", E.w.persos[p].nom, prix);
    else snprintf(t, sizeof t, "%s est à toi", E.w.persos[p].nom);
    toast(t);
    son_jouer(SON_VALIDER, 0.5f, 1);
}
int hud_course_impose(int tours, int niv, int veh, const char *trace, int armes, int chrono, const int *riv, int nriv) {
    float sx = E.sera.x, sz = E.sera.z;
    if (trace) for (int k = 0; k < course_npistes(); k++) {   /* la piste active : celle du tracé de ce nom */
        const piste_t *P = course_piste(k);
        if (!strcmp(P->nom, trace)) { sx = P->xd; sz = P->zd; break; }   /* (ligne gardée : l'axe n'est en mémoire que sur sa carte) */
    }
    const float ox = E.sera.x, oz = E.sera.z;
    E.sera.x = sx; E.sera.z = sz;
    const int ok = hud_course_menu(tours, nriv >= 0 ? nriv : -1, niv, veh);
    E.sera.x = ox; E.sera.z = oz;
    if (!ok) return 0;
    if (trace) for (int k = 0; k < RM.ntr; k++) if (!strcmp(course_piste(RM.trace[k])->nom, trace)) { RM.tr = k; course_trace(RM.trace[k]); RM.fige_trace = 1; }
    if (nriv >= 0) { for (int k = 0; k < nriv; k++) RM.riv[k] = riv[k]; RM.nia = nriv; RM.fige_riv = 1; }
    if (armes >= 0) { RM.armes = armes != 0; RM.fige_armes = 1; }
    RM.fige_tours = tours >= 1; RM.fige_niv = niv >= 0; RM.chrono = (float)chrono;
    return 1;
}
/* Ligne figée par le scénario (page, ligne) */
static int rm_fige(int page, int row) {
    if (page == 1) return row == 2 ? RM.fige_niv : row >= 1 && RM.fige_riv;
    if (page == 2) return row == 1 ? RM.fige_trace : row == 2 ? RM.fige_tours : row == 3 ? RM.fige_armes : 0;
    return 0;
}
int hud_course_menu_open(void) { return RM.on; }
int hud_pause(void) { return (!BT.on && (H.menu || H.sub >= 0)) || (CR.on && CR.pause); }
/* Pause de course (bouton en haut à droite, touche menu) : reprendre ou abandonner */
static void course_pause(int on) { if (CR.on && CR.phase != CP_RESULTATS) { CR.pause = on; CR.pause_sel = 0; son_jouer(SON_MENU, 0.4f, 1); } }
/* Choix de la pause (coursePauseChoix de l'émulateur) : REPRENDRE, RÉGLAGES (02/11/2026 : la page RÉGLAGES du mode course,
   accueil.c, puis retour à la pause), RECOMMENCER (mode course de l'écran titre), puis ABANDONNER (la course ne compte
   pas) ou, au tour rapide, TERMINER (son meilleur tour compte) */
enum { PA_REPRENDRE, PA_REGLAGES, PA_RECOMMENCER, PA_FIN };
static int pause_liste(int *a) {
    int n = 0;
    a[n++] = PA_REPRENDRE; a[n++] = PA_REGLAGES;
    if (carriere_mode()) a[n++] = PA_RECOMMENCER;
    a[n++] = PA_FIN;
    return n;
}
static int pause_n(void) { int a[4]; return pause_liste(a); }
static int pause_quoi(int i) { int a[4]; const int n = pause_liste(a); return i >= 0 && i < n ? a[i] : PA_REPRENDRE; }
static void pause_faire(int q) {
    if (q == PA_REPRENDRE) course_pause(0);
    else if (q == PA_REGLAGES) { son_jouer(SON_VALIDER, 0.45f, 1); accueil_regl_ouvrir(); }
    else if (q == PA_RECOMMENCER) carr_recommencer();
    else if (carriere_mode() && carr_sorte_course() == CS_TOUR) carr_terminer();
    else course_abandon();
}
static void pause_choix(int i) { pause_faire(pause_quoi(i)); }
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static void course_menu_change(int row, int d) {
    if (RM.mode && row == 0) return;   /* menu des véhicules : la page PILOTE seule */
    RM.row = row;
    if (row && rm_fige(RM.page, row)) return;
    if (row == 0) RM.page = (RM.page + d + 3) % 3;
    else if (RM.page == 0) {
        if (row == 1) { RM.mod = (RM.mod + d + RM.nmod) % RM.nmod; RM.coul = 0; RM.t0 = H.now; }
        else RM.coul = (RM.coul + d + rm_ncoul(RM.mod)) % rm_ncoul(RM.mod);
    } else if (RM.page == 1) {
        if (row == 1) RM.nia = clampi(RM.nia + d, 0, RM_NR);
        else if (row == 2) RM.niv = clampi(RM.niv + d, 0, 2);
        else {   /* adversaire row - 3 : le véhicule de course suivant ; le régler le met en course */
            const int k = row - 3;
            int i = 0;
            while (i < RM.n && RM.ord[i] != RM.riv[k]) i++;
            RM.riv[k] = RM.ord[((i < RM.n ? i : 0) + d + RM.n) % RM.n];
            if (RM.nia < k + 1) RM.nia = k + 1;
        }
    } else {
        if (row == 1 && RM.ntr) { RM.tr = (RM.tr + d + RM.ntr) % RM.ntr; course_trace(RM.trace[RM.tr]); }
        else if (row == 2) RM.tours = clampi(RM.tours + d, 1, 9);
        else if (row == 3) RM.armes = !RM.armes;
    }
    son_jouer(SON_MENU, 0.35f, 1);
}
static void course_menu_go(void) {
    if (RM.mode) { vehicules_go(); return; }   /* menu des véhicules : PRENDRE, ACHETER */
    RM.on = 0;
    if (RM.ntr) course_trace(RM.trace[RM.tr]);
    if (!course_start(rm_perso(), RM.tours, RM.riv, RM.nia, RM.niv, RM.armes)) toast("Départ impossible");
    else CR.chrono = RM.chrono;
}
static void course_menu_cancel(void) { RM.on = 0; if (!RM.mode) course_cancel(); }   /* (menu des véhicules : fermé, rien de pris) */

/* Vol d'un véhicule du trafic (01/11/2026) : d'abord une confirmation, avec ce qu'il en coûte (le niveau de recherche qu'il
   donne : police_niveau_vol) ; VOLER, ou ANNULER (RETOUR à la manette) qui ne fait rien. Le véhicule attend, arrêté devant
   Sera qui s'adresse à lui (voleDemander de l'émulateur) */
static void vol_demander(int k) {
    char opts[2][48];
    snprintf(opts[0], sizeof opts[0], "VOLER"); snprintf(opts[1], sizeof opts[1], "ANNULER");
    hud_choice(opts, 2);
    CHX.vol = 1; vol_k = k;
    snprintf(CHX.titre, sizeof CHX.titre, "VOLER CE VÉHICULE ?");
    const int niv = police_niveau_vol(E.traffic[k].perso);
    snprintf(CHX.texte[0], sizeof CHX.texte[0], "%s", niv ? "La police sera prévenue." : "Son conducteur sera éjecté.");
    if (niv) snprintf(CHX.texte[1], sizeof CHX.texte[1], "RECHERCHE : NIVEAU %d / 3", niv);
}
static void vol_confirme(void) {
    const int k = vol_k;
    vol_k = -1;
    if (k < 0 || k >= TRAFFIC_MAX || !E.traffic[k].on || E.ride >= 0) { toast("Le véhicule est reparti"); return; }
    if (vehicle_steal(k)) toast(vol_cri);   /* le cri du conducteur éjecté */
}
static void carte_tele_aller(void);   /* téléport depuis la carte du menu (plus bas) */
/* Couche d'un abri de l'île (ile.c, 12/11/2026) : dormir jusqu'au matin, après confirmation, ce qui est en jeu sous le titre ; DORMIR, ou
   ANNULER (RETOUR) qui ne fait rien (ileDormirDemander de l'émulateur de l'île) */
static void dormir_demander(void) {
    if (ile_dormir_k() < 0) return;
    char opts[2][48];
    snprintf(opts[0], sizeof opts[0], "DORMIR"); snprintf(opts[1], sizeof opts[1], "ANNULER");
    ile_dormir_retenir();
    hud_choice(opts, 2);
    CHX.dormir = 1;
    snprintf(CHX.titre, sizeof CHX.titre, "DORMIR JUSQU'AU MATIN ?");
    snprintf(CHX.texte[0], sizeof CHX.texte[0], "L'équipe se repose : PV et PM rendus");
    snprintf(CHX.texte[1], sizeof CHX.texte[1], "La partie est sauvegardée au réveil");
}
/* Une succube nommée au contact (île, ile.c, 13/11/2026, édition 18+) : TE BATTRE ou T'OFFRIR À ELLE ; RETOUR n'annule pas (elle est là)
   (succubeProposer de l'émulateur de l'île) */
void hud_succube_demander(const char *nom, const char *nom2, int r, int n) {   /* nom2 : sa partenaire (paire), sinon NULL */
    char opts[2][48];
    snprintf(opts[0], sizeof opts[0], "TE BATTRE"); snprintf(opts[1], sizeof opts[1], nom2 ? "T'OFFRIR À ELLES" : "T'OFFRIR À ELLE");
    hud_choice(opts, 2);
    CHX.succube = 1;
    if (nom2) snprintf(CHX.titre, sizeof CHX.titre, "SUCCUBES %s ET %s", nom, nom2); else snprintf(CHX.titre, sizeof CHX.titre, "%s", nom);
    snprintf(CHX.texte[0], sizeof CHX.texte[0], nom2 ? "Elles te tendent la main en souriant" : "Elle te tend la main en souriant");
    snprintf(CHX.texte[1], sizeof CHX.texte[1], "Rencontre %d sur %d", r, n);
}
/* Réponse k d'un choix : au scénario, ou à la porte fermée (forcer ou annuler), au vol (voler ou annuler) */
static void choice_pick(int k) {
    CHX.on = 0;
    if (CHX.porte) { CHX.porte = 0; int_choix(k); }
    else if (CHX.vol) { CHX.vol = 0; if (k == 0) vol_confirme(); else vol_k = -1; }   /* VOLER, ou ANNULER : rien */
    else if (CHX.shop) {   /* ACHETER / VENDRE, ou ANNULER ; à la forge : AMÉLIORER, ÉLÉMENT, ANNULER */
        const int sk = shop_k; CHX.shop = 0; shop_k = -1;
        if (SHOP.mode == 2 && SHOP.forge) { const int a = k >= 0 && k < 3 ? SHOP.fact[k] : 2; if (a == 0) shop_transaction(sk); else if (a == 1) forge_element(sk); }
        else if (k == 0) shop_transaction(sk);
    }
    else if (CHX.commerce) { CHX.commerce = 0; commerce_choix(k); }
    else if (CHX.arene) { CHX.arene = 0; arene_choix(k); }   /* arène (arene.c) : rang, combat suivant ou abandon */
    else if (CHX.tele) { CHX.tele = 0; if (k == 0) carte_tele_aller(); }   /* TÉLÉPORTER, ou ANNULER : rien (la carte reste ouverte) */
    else if (CHX.dormir) { CHX.dormir = 0; if (k == 0) ile_dormir_lancer(); }   /* DORMIR, ou ANNULER : rien */
    else if (CHX.succube) { CHX.succube = 0; ile_succube_choix(k); }   /* TE BATTRE, ou T'OFFRIR À ELLE (ile.c) */
    else script_choose(k + 1);
}
/* Toucher sur la carte du menu, en véhicule : le pilote automatique jusqu'à ce point ; nommé d'après le lieu du guidage le
   plus proche à moins de 40 cases (carteToucher de l'émulateur) */
/* Nom d'un point de la carte : « PRÈS DE » le lieu du guidage le plus proche à moins de 40 cases, s'il a été vu (brouillard) */
static void carte_nom_point(float x, float z, char *nm, int n) {
    float bd = 40;
    snprintf(nm, n, "POINT DE LA CARTE");
    for (int i = 0; i < E.w.nguides; i++) {
        const guide_t *g = &E.w.guides[i];
        const float d = hypotf(g->x - x, g->z - z);
        if ((g->kind == GUIDE_VILLE || g->kind == GUIDE_LIEU) && g->carte == E.w.carte && d < bd && brouillard_vu(g->x, g->z, E.w.carte)) { bd = d; snprintf(nm, n, "PRÈS DE %s", g->nom); }
    }
}
static void carte_pilote(void) {
    float x, z;   /* point touché sur la carte du menu (carte.c : zoomée, déplacée) */
    if (!carte_touche(&x, &z) || x < 0 || z < 0 || x >= E.w.n || z >= E.w.n) return;
    char nm[40];
    carte_nom_point(x, z, nm, sizeof nm);
    if (pilote_lancer(x, z, -1, nm)) { H.sub = -1; H.menu = 0; }
}
/* Téléport (SYSTÈME › TÉLÉPORTER, carte du menu) : pourquoi il est refusé maintenant, NULL s'il est permis (teleRefus de
   l'émulateur) : en véhicule, recherchée par la police (pas de fuite par le téléport), pendant une scène */
static const char *tele_refus(void) {
    if (E.ride >= 0 || CR.on) return "Descendre du véhicule d'abord";
    if (POL.niveau) return "Recherchée : semer d'abord la police";
    if (script_running()) return "Pas pendant une scène";
    return NULL;
}
/* Téléport depuis la carte du menu (04/11/2026, demande de l'utilisateur : « se téléporter à l'aide de la carte, mais
   uniquement dans les zones déjà visitées ») : un toucher à pied sur un endroit vu (brouillard_vu) propose d'y aller,
   TÉLÉPORTER ou ANNULER (CHX.tele, comme le vol d'un véhicule ; RETOUR annule) ; pas vu : « Zone inconnue » ; sinon les
   règles de TÉLÉPORTER (tele_refus) ; ni dans l'enceinte de Lucerne sans le passage (vue du dehors : les gardes la
   reconduiraient). Sur la carte où est Sera ; ni sur le système solaire (à bord du vaisseau) ni sur une carte de course
   (sans brouillard) (teleCarteDemander de l'émulateur) */
static float TP_X, TP_Z;   /* point du téléport attendu (CHX.tele) */
#define TELE_CARTE_R 6.0f  /* cases : place libre la plus proche du point touché */
static void carte_tele_demander(void) {
    float x, z;
    if (!carte_touche(&x, &z) || x < 0 || z < 0 || x >= E.w.n || z >= E.w.n || !brouillard_carte(E.w.carte)) return;
    const char *r = tele_refus();
    if (r) { toast(r); return; }
    if (!brouillard_vu(x, z, E.w.carte)) { toast("Zone inconnue : pas de téléport"); return; }
    if (enceinte_interdite(x, z) >= 0) { toast("Enceinte fermée : pas de téléport"); return; }   /* (vue du dehors : les gardes la reconduiraient) */
    char opts[2][48], nm[40], d[24];
    snprintf(opts[0], sizeof opts[0], "TÉLÉPORTER"); snprintf(opts[1], sizeof opts[1], "ANNULER");
    carte_nom_point(x, z, nm, sizeof nm);
    dist_txt(hypotf(x - (E.in.on ? E.in.rx : E.sera.x), z - (E.in.on ? E.in.rz : E.sera.z)), d, sizeof d);
    hud_choice(opts, 2);
    CHX.tele = 1; TP_X = x; TP_Z = z;
    snprintf(CHX.titre, sizeof CHX.titre, "TÉLÉPORTER ICI ?");
    snprintf(CHX.texte[0], sizeof CHX.texte[0], "%s", nm);
    snprintf(CHX.texte[1], sizeof CHX.texte[1], "À %s d'ici", d);
}
/* Téléport confirmé : la fenêtre des chunks de la destination est lue tout de suite (la carte du menu couvre l'écran, rien
   ne lit l'ancienne : carte_plein), Sera sur la place libre la plus proche (sera_placer, TELE_CARTE_R) ; rien de libre (au
   milieu d'un lac, dans un mur) : elle reste où elle était, devant la porte si elle était dedans (teleCarteAller) */
static void carte_tele_aller(void) {
    const char *r = tele_refus();
    if (r) { toast(r); return; }
    const float ax = E.in.on ? E.in.rx : E.sera.x, az = E.in.on ? E.in.rz : E.sera.z;
    int_reset();   /* hors de l'intérieur, comme TÉLÉPORTER */
    world_stream(TP_X, TP_Z, 1);
    sera_placer(TP_X, TP_Z, TELE_CARTE_R);
    if (!sera_libre(E.sera.x, E.sera.z)) { E.sera.x = ax; E.sera.z = az; E.has_dest = 0; E.stream_hold = 1; toast("Pas de place ici"); return; }
    char nm[40], t[64];
    carte_nom_point(TP_X, TP_Z, nm, sizeof nm);
    E.sera_y_ok = 0; E.has_dest = 0; E.stream_hold = 1; BT.fade_t = E.clock;
    snprintf(t, sizeof t, "Téléportation : %s", nm);
    toast(t);
    H.sub = -1; H.menu = 0;
}
/* Toucher simple sur la carte du menu (carte.c, CARTE_TOUCHE) : en véhicule, le pilote automatique ; à pied, le téléport */
static void carte_toucher(void) {
    if (E.ride >= 0) { if (pilote_possible(0)) carte_pilote(); }
    else carte_tele_demander();
}
void hud_carte_touche(void) { carte_toucher(); }
static void do_action(int act, int arg) {
    switch (act) {
    case A_TALK:   /* parler, monter dans un véhicule ou en descendre */
        if (E.ride >= 0 && fabsf(E.ride_v) > 0.3f) toast("Arrêtez-vous pour descendre");
        else if (E.ride >= 0) { if (!vehicle_unboard()) toast(ride_type() == VEH_VAISSEAU ? "Pas de place pour atterrir" : "Pas de place pour descendre"); }
        else if (E.talk <= TALK_TRAF(0)) vol_demander(TALK_TRAF(0) - E.talk);   /* vol d'un véhicule du trafic : confirmé d'abord */
        else if (talk_is_vehicle()) vehicle_board(E.talk);
        else if (!script_talk(E.talk)) open_dialog();   /* un événement « parler » remplace les répliques */
        break;
    case A_MENU_ITEM: if (H.sel == arg) open_sub(MENU_SUB(arg)); else H.sel = arg; break;
    case A_OPEN: open_sub(MENU_SUB(H.sel)); break;
    case A_ROW: if (H.sub_sel == arg) activate_row(arg); else H.sub_sel = arg; break;
    case A_BACK: sub_back(); break;
    case A_CODE: code_touche(arg); break;
    case A_BATTLE: battle_command(arg >> 3, arg & 7); break;   /* commande × 8 + technique */
    case A_CHOICE: if (CHX.sel == arg) { choice_pick(arg); son_jouer(SON_VALIDER, 0.4f, 1); } else CHX.sel = arg; break;   /* surligner, puis répondre */
    case A_PORTE: int_interagir(); break;   /* devant une porte : entrer ou forcer */
    case A_CARTE: carte_pilote(); break;    /* carte du menu, en véhicule : pilote automatique jusque-là */
    case A_EQUIPER: H.sub_sel = arg; if (H.sub == SUB_SHOP) shop_fiche_choix(arg); else equip_confirmer(arg); break;   /* fiche du menu ÉQUIP. ou de la boutique : OUI ou NON */
    case A_SHOP_MEMBRE: shop_membre(arg); break;
    case A_COFFRE: coffre_ouvrir(); break;   /* devant un coffre : le fouiller (coffres.c) */
    case A_PANNEAU: hud_panneau_ouvrir(E.panneau); break;   /* devant un panneau de quêtes : le lire (panneaux.c) */
    case A_DORMIR: dormir_demander(); break;   /* devant la couche d'un abri de l'île : dormir jusqu'au matin (ile.c) */
    case A_BUTIN: butin_fermer(); break;
    case A_DEFAITE: battle_defaite(arg); break;
    case A_MENU_BTN: H.menu = !H.menu; E.has_dest = 0; son_jouer(SON_MENU, 0.35f, 1); break;
    case A_MENU_GO: H.sel = arg; open_sub(MENU_SUB(arg)); break;
    case A_VALIDER: if (sub_rows() > 0) activate_row(H.sub_sel); break;
    case A_COURSE: hud_course_menu(0, -1, -1, -1); break;
    case A_RACE_CHG: course_menu_change(arg >> 1, arg & 1 ? 1 : -1); break;   /* ligne × 2 + (1 : suivant) */
    case A_RACE_GO: course_menu_go(); break;
    case A_RACE_BACK: course_menu_cancel(); break;
    case A_RACE_PAGE: RM.page = arg; RM.row = 0; son_jouer(SON_MENU, 0.35f, 1); break;
    case A_FIRE: course_tir(); break;
    case A_PAUSE: course_pause(1); break;
    case A_REPRENDRE: course_pause(0); break;
    case A_ABANDON: pause_faire(PA_FIN); break;
    case A_RECOMMENCER: pause_faire(PA_RECOMMENCER); break;
    case A_PAUSE_REGL: pause_faire(PA_REGLAGES); break;
    case A_PAUSE_SEL: if (CR.pause_sel == arg) pause_choix(arg); else { CR.pause_sel = arg; son_jouer(SON_MENU, 0.35f, 1); } break;   /* plein écran : choisir, puis toucher encore */
    case A_PAUSE_GO: pause_choix(CR.pause_sel); break;
    }
}

/* Début d'un combat : menus et dialogue fermés ; un doigt encore posé (marche) ne vaudra pas toucher en se levant */
/* Réplique du scénario : une ligne ; le portrait de droite suit l'interlocuteur (personnage de ce nom, un membre de
   l'équipe sans fiche dans son sprite de la planche commune, sinon aucun), comme speakerFor de l'émulateur */
void hud_say(const char *who, const char *text, const char *right) {
    snprintf(D.who[0], sizeof D.who[0], "%s", who);
    D.text[0] = text; D.n = 1;
    if (strcmp(D.right, right) != 0) {
        snprintf(D.right, sizeof D.right, "%s", right);
        int found = 0;
        for (int i = 0; i < E.w.npersos && !found; i++)
            if (E.w.persos[i].sheet >= 0 && !strcmp(E.w.persos[i].nom, right)) {
                const perso_vue_t pv = perso_vue(i, 0);   /* la recrue de l'héroïne choisie : en Sera */
                int sh = pv.sheet;
                for (int m = 1; m < NMEMB; m++) if (E.w.party_perso[m] == i) sh = member_sheet(m);   /* un membre : dans sa tenue */
                make_portrait(1, sh, pv.remap); found = 1;
            }
        for (int i = 1; i < 4 && !found; i++)   /* tables de couleurs 2 à 4 : Varn, Bors, Lyse */
            if (!strcmp(PARTY[i].name, right)) { make_portrait(1, -1, i + 1); found = 1; }
        if (!found) memset(portrait_mask[1], 0, sizeof portrait_mask[1]);
    }
    H.dialog = 1; H.line = 0; H.line_t0 = H.now; E.has_dest = 0; parle_maj = 1;
}
int hud_in_dialog(void) { return H.dialog; }
/* Celui qui dit la réplique, pour le cadrage du dialogue (eng_build_frame, parlePos de l'émulateur) : Sera, un acteur, l'interlocuteur, sinon l'exemplaire de ce nom le plus proche
   du point regardé, à moins de PARLE_R cases ; 0 : personne (voix, récit, membre de l'équipe sans acteur). Cherché une fois par réplique */
#define PARLE_R 20.0f
#define PARLE_SERA 9999
int hud_parle_pos(float *x, float *z) {
    static int qui = -1;
    if (!H.dialog || H.line >= D.n) return 0;
    if (parle_maj) {
        parle_maj = 0; qui = -1;
        const char *who = D.who[H.line];
        if (!strcmp(who, PARTY[0].name)) qui = PARLE_SERA;
        for (int k = 0; k < ACT_MAX && qui < 0; k++) if (MISE.act[k].on && !MISE.act[k].chasse && MISE.act[k].perso < E.w.npersos && !strcmp(E.w.persos[MISE.act[k].perso].nom, who)) qui = k;
        if (qui < 0 && E.talk >= 0 && E.talk < E.nnpc && (!strcmp(E.w.persos[E.npcs[E.talk].perso].nom, who) || !strcmp(pnj_nom(E.talk), who))) qui = ACT_MAX + E.talk;
        if (qui < 0) {
            const float rx = MISE.cam_on ? MISE.cam[0] : E.sera.x, rz = MISE.cam_on ? MISE.cam[1] : E.sera.z;
            float best = PARLE_R;
            for (int q = 0; q < E.nnpc; q++) {
                const npc_t *n = &E.npcs[q];
                const float d = hypotf(n->x - rx, n->z - rz);
                if (d < best && npc_present(n) && !strcmp(E.w.persos[n->perso].nom, who)) { best = d; qui = ACT_MAX + q; }
            }
        }
    }
    if (qui < 0) return 0;
    if (qui == PARLE_SERA) { *x = E.sera.x; *z = E.sera.z; return 1; }
    if (qui < ACT_MAX) { if (!MISE.act[qui].on) return 0; *x = MISE.act[qui].x; *z = MISE.act[qui].z; return 1; }
    if (qui - ACT_MAX >= E.nnpc) return 0;
    *x = E.npcs[qui - ACT_MAX].x; *z = E.npcs[qui - ACT_MAX].z;
    return 1;
}
/* Plusieurs répliques de who (chaînes gardées par l'appelant), portrait de droite : right (la police d'une effraction) */
void hud_lines(const char *who, const char *const *lines, int n, const char *right) {
    if (n < 1) return;
    hud_say(who, lines[0], right);
    snprintf(D.right, sizeof D.right, "%s", who);   /* who parle à droite, avec le portrait de right */
    for (int i = 1; i < n && i < DLG_MAX; i++) { snprintf(D.who[i], sizeof D.who[i], "%s", who); D.text[i] = lines[i]; }
    D.n = n < DLG_MAX ? n : DLG_MAX;
}
void hud_toast(const char *s) { toast(s); }
const char *hud_toast_txt(void) { return H.toast; }   /* banc d'essai : le dernier message */
const char *hud_texte_vu(const char *s, char *buf, int cap) { return nom_echange(s, buf, cap); }
const char *hud_replique_vue(char *qui, int cap) {
    if (!H.dialog || H.line >= D.n) return NULL;
    const char *q = nom_echange(D.who[H.line], qui, cap);
    if (q != qui) snprintf(qui, (size_t)cap, "%s", q);
    return dlg_texte();
}

void hud_annonce(const char *kind, const char *title, const char *text) {
    if (H.nann == 4) { memmove(&H.ann[0], &H.ann[1], sizeof H.ann[0] * 3); H.nann = 3; }   /* pleine : la plus ancienne part */
    snprintf(H.ann[H.nann].kind, sizeof H.ann[0].kind, "%s", kind);
    snprintf(H.ann[H.nann].title, sizeof H.ann[0].title, "%s", title);
    snprintf(H.ann[H.nann].text, sizeof H.ann[0].text, "%s", text);
    H.ann[H.nann++].t0 = -1;
}

/* Carton de titre du scénario : bandeau au milieu de l'écran, titre et sous-titre, fondu d'entrée et de sortie de 0,4 s
   (drawCarton de l'émulateur) */
static struct { char title[48], sub[64]; float t0; int on; } TT;
void hud_title(const char *title, const char *sub) {
    snprintf(TT.title, sizeof TT.title, "%s", title);
    snprintf(TT.sub, sizeof TT.sub, "%s", sub);
    TT.t0 = H.now; TT.on = 1;
}

void hud_battle_start(void) {
    H.menu = 0; H.sub = -1; H.dialog = 0; H.pinch = 0; H.rim_on = 0; H.bsel = 0;
    if (H.down) { H.walking = 1; H.hold = 0; H.rim = 0; }
}

/* ---------- Gestes ---------- */
static int near_npc(void) { return E.talk != -2 || (E.ride >= 0 && !vol_permanent()); }   /* vol permanent : pas de bulle pour descendre */
static int talk_is_vehicle(void) { return E.talk <= TALK_TRAF(0) || (E.talk >= 0 && (E.w.persos[E.npcs[E.talk].perso].flags & PERS_VEHICULE)); }

/* Destination au doigt : le point du sol sous lui (x, y : écran natif). Sera y court si le doigt est à plus de
   DOIGT_COURSE px d'elle à l'écran, pareil dans toutes les directions (la distance au sol, écrasée par la perspective
   vers le bas de l'écran, la faisait marcher dès qu'on visait vers la caméra) ; Sera hors de l'écran : plus de 5 m */
#define DOIGT_COURSE 110.0f
#define VISE_BLOQUE 1.5f
static void viser(float x, float y) {
    float gx, gz;
    if (!pick_ground((x + ui_ox) / ENG_SCALE, (y + ui_oy) / ENG_SCALE, &gx, &gz)) return;
    /* Arrêtée devant un obstacle (game.c, BLOQUE_S) : le doigt maintenu qui vise encore là ne la relance pas ; ailleurs ou
       un nouveau toucher, si (VISE_BLOQUE cases) */
    if (E.dest_bloque && hypotf(gx - E.dest_bx, gz - E.dest_bz) < VISE_BLOQUE / E.w.ech) return;
    E.dest_bloque = 0;
    if (!E.has_dest) { E.bloque_x = E.sera.x; E.bloque_z = E.sera.z; E.bloque_t = 0; }   /* nouvelle marche : mesure du blocage à zéro */
    E.dest_x = gx; E.dest_z = gz; E.has_dest = 1;
    E.dest_court = E.scr_sera.vis ? hypotf(x - E.scr_sera.cx, y - E.scr_sera.cy) > DOIGT_COURSE : hypotf(gx - E.sera.x, gz - E.sera.z) > 5.0f / E.w.ech;
}
static void tap(float x, float y) {
    H.fx_x = x; H.fx_y = y; H.fx_t = H.now;
    if (BT.on) {   /* combat : boutons de l'éventail, sinon l'ennemi ou continuer */
        int b = button_at(x, y);
        if (b >= 0) do_action(BTN[b].act, BTN[b].arg); else battle_tap(x, y);
        return;
    }
    if (script_image_seule()) { script_image_passer(); return; }   /* illustration montrée seule : le toucher l'abrège */
    if (ile_offrande_passer()) return;   /* île : la séquence d'une succube, l'animation suivante (ile.c) */
    if (BUT.on) { butin_fermer(); return; }   /* panneau du butin : le toucher le ferme */
    if (H.dialog) { advance_dialog(); return; }
    if (CHX.on) { const int cb = button_at(x, y); if (cb >= 0 && BTN[cb].act == A_CHOICE) do_action(A_CHOICE, BTN[cb].arg); return; }
    if (RM.on) { const int rb = button_at(x, y); if (rb >= 0) do_action(BTN[rb].act, BTN[rb].arg); return; }   /* menu de course */
    if (CR.on) {   /* en course, le doigt pilote ; il touche le bouton de tir ; les résultats se ferment */
        if (CR.phase == CP_RESULTATS) course_end();
        else { const int fb = button_at(x, y); if (fb >= 0) do_action(BTN[fb].act, BTN[fb].arg); }
        return;
    }
    int b = button_at(x, y);
    if (b >= 0) { do_action(BTN[b].act, BTN[b].arg); return; }
    float d = hypotf(x - CX, y - CY);
    if (H.sub >= 0) return;
    if (H.menu) { if (!PORTRAIT && d > 200) H.menu = 0; return; }   /* plein écran : le bouton du menu le ferme */
    int on_sera = E.scr_sera.vis && hypotf(x - E.scr_sera.cx, y - E.scr_sera.cy) < 46;
    if (on_sera) {
        if (H.now - H.last_tap_t < 0.35f && H.last_tap_sera) { H.menu = 1; E.has_dest = 0; H.last_tap_t = -10; }
        else { H.last_tap_t = H.now; H.last_tap_sera = 1; }
        return;
    }
    H.last_tap_sera = 0;
    if (E.ride >= 0) return;   /* en véhicule, le doigt conduit (hud_update) */
    viser(x, y);
}

static void pointer(const eng_input_t *in) {
    if (H.sub == SUB_MAP && !H.dialog && !CHX.on) {   /* carte du menu : ses gestes (carte.c) ; au retour, le lever n'est pas un toucher */
        const int r = carte_doigts(in, H.now);
        H.down = in->touch; H.walking = 1; H.hold = H.rim = H.list = 0; H.pinch = 0;
        if (r == CARTE_RETOUR) sub_back();
        else if (r == CARTE_TOUCHE) carte_toucher();   /* en véhicule : pilote automatique ; à pied : téléport */
        return;
    }
    float x = in->touch_x, y = in->touch_y;
    /* Pinch (sur la world map) : le zoom suit l'écart des deux doigts ; le geste ne vaut ni toucher ni marche, et dure
       jusqu'à ce que tous les doigts soient levés */
    /* En véhicule, deux doigts conduisent (drive_touch) : pas de pincement */
    if (in->touch && in->touch2 && !H.menu && !H.dialog && H.sub < 0 && !BT.on && E.ride < 0) {
        float d = hypotf(in->touch2_x - x, in->touch2_y - y);
        if (!H.pinch) { H.pinch = 1; H.pinch_d0 = d > 20 ? d : 20; H.pinch_z0 = E.zoom; H.down = 0; H.walking = 0; H.rim_on = 0; E.has_dest = 0; }
        /* plus de zoom au pincement : il est fixe selon le lieu (game.c, ville_maj ; 01/11/2026) ; le geste ne fait plus
           que retenir Sera, sauf avec le zoom manuel (code CODE_ZOOM, 02/11/2026) : le zoom suit l'écart des doigts */
        if (E.zoom_libre) eng_set_zoom(H.pinch_z0 * (d > 20 ? d : 20) / H.pinch_d0);
        return;
    }
    if (H.pinch) { if (!in->touch) H.pinch = 0; return; }
    if (in->touch && !H.down) {
        float d = hypotf(x - CX, y - CY);
        int on_sera = E.scr_sera.vis && hypotf(x - E.scr_sera.cx, y - E.scr_sera.cy) < 46;
        int on_btn = button_at(x, y) >= 0;
        H.down = 1; H.x0 = H.cx = x; H.y0 = H.cy = y; H.moved = 0; H.t0 = H.now; H.acc = 0; H.walking = 0;
        E.dest_bloque = 0;   /* nouveau toucher : Sera peut retenter le point où elle s'est arrêtée */
        H.rim = (d > 186 || (H.menu && H.sub < 0 && d > 100)) && !H.dialog && H.sub != SUB_MAP && !BT.on && !BUT.on;   /* en combat : seulement des touchers ; roue du menu : le bord et l'anneau des icônes */
#if ENG_DYN
        H.rim = y > UH - BANDE_H && !H.dialog && !H.menu && H.sub < 0 && !BT.on && !BUT.on && E.ride < 0;   /* bande du bas */
#endif
        H.list = !H.rim && H.sub >= 0 && H.sub != SUB_MAP && !BT.on; H.vy = 0; H.move_t = H.now; H.fling = 0;
        H.ang = atan2f(x - CX, -(y - CY));
        H.hold = !H.rim && !H.dialog && !H.menu && !on_sera && !on_btn && !BT.on && !BUT.on;   /* ni pendant le panneau du butin */
        if (H.vol_on) { H.rim = 1; H.list = 0; H.hold = 0; H.acc = 0; H.ang = atan2f(x - UW / 2, -(y - UH / 2)); }
    } else if (in->touch && H.down) {
        if (H.vol_on) {   /* cadran du son : tourner autour du centre, sens des aiguilles = plus fort */
            const float a = atan2f(x - UW / 2, -(y - UH / 2));
            float da = a - H.ang;
            da = atan2f(sinf(da), cosf(da)); H.ang = a; H.cx = x; H.cy = y; H.moved = fmaxf(H.moved, hypotf(x - H.x0, y - H.y0));
            if (hypotf(x - UW / 2, y - UH / 2) > 50) {   /* près du centre, l'angle saute : rien */
                H.acc += da; H.rim_on = 1; H.rim_a = a;
                while (H.acc > VOL_PAS_RAD) { vol_pas(1); H.acc -= VOL_PAS_RAD; }
                while (H.acc < -VOL_PAS_RAD) { vol_pas(-1); H.acc += VOL_PAS_RAD; }
            }
            return;
        }
        H.moved = fmaxf(H.moved, hypotf(x - H.x0, y - H.y0));
        /* Liste : elle suit le doigt dès qu'il a bougé (sinon c'est un toucher) ; vitesse lissée pour l'élan */
        if (H.list && H.moved > 14 && y != H.cy) {
            const float dt = H.now - H.move_t > 0.001f ? H.now - H.move_t : 0.001f;
            drag_rows(y - H.cy); H.vy = 0.6f * H.vy + 0.4f * (y - H.cy) / dt; H.move_t = H.now;
        }
#if ENG_DYN
        if (H.rim) { E.yaw += (x - H.cx) * 0.012f; H.rim_on = 1; }   /* glisser à droite : la caméra tourne vers la droite */
        H.cx = x; H.cy = y;
        if (0) {
#else
        H.cx = x; H.cy = y;
        if (H.rim) {
#endif
            float a = atan2f(x - CX, -(y - CY)), da = a - H.ang;
            da = atan2f(sinf(da), cosf(da)); H.ang = a; H.rim_on = 1; H.rim_a = a;
            if (H.menu && H.sub < 0) {   /* roue à places fixes : l'entrée sous le doigt (au-delà d'un toucher) */
                if (H.moved > 8) H.sel = menu_a(a);
            } else if (H.sub >= 0) {
                int n = sub_rows();
                H.acc += da;
                while (H.acc > 0.35f) { if (H.sub_sel < n - 1) H.sub_sel++; H.acc -= 0.35f; }
                while (H.acc < -0.35f) { if (H.sub_sel > 0) H.sub_sel--; H.acc += 0.35f; }
            } else if (E.ride < 0) E.yaw -= da;   /* en véhicule, la caméra reste derrière lui */
        }
    } else if (!in->touch && H.down) {
        H.down = 0; H.rim_on = 0;
        /* un toucher sur une flèche ou une barre change de canal, ailleurs il referme le cadran */
        if (H.vol_on) { if (H.moved <= 14 && !vol_toucher(H.cx - UW / 2, H.cy - UH / 2)) H.vol_on = 0; return; }
        if (H.list && H.moved > 14) { if (H.now - H.move_t < 0.1f && fabsf(H.vy) > 150) H.fling = H.vy; return; }
        if (H.walking) return;
        if (H.rim && H.moved > 8) return;
        if (H.moved > 14) return;
        tap(H.cx, H.cy);
    }
}

/* Combat au clavier : flèches entre les boutons de l'éventail, OK pour valider (ou viser l'ennemi, continuer), retour */
/* Écran de défaite (draw_defaite) : choix au clavier (0 REPRENDRE, 1 ÉCRAN TITRE), point de sauvegarde lu une fois (t0 :
   début de la défaite qu'il décrit) */
static struct { int sel, ok; float t0; partie_info_t info; } DF;
/* Choix de l'écran de défaite, dans l'ordre (DEF_* de battle_defaite) : RÉESSAYER LE COMBAT s'il se peut, REPRENDRE,
   ÉCRAN TITRE ; renvoie leur nombre */
static int def_choix(int *c) {
    int n = 0;
    if (battle_reessai_ok()) c[n++] = DEF_REESSAYER;
    c[n++] = DEF_REPRENDRE; c[n++] = DEF_TITRE;
    return n;
}
static void battle_keys(uint32_t pressed) {
    const int ph = BT.phase, menu = ph == BP_CMD || ph == BP_TECH || ph == BP_MAGIE || ph == BP_ITEM || ph == BP_TARGET || ph == BP_ALLIE;
    if (ph == BP_LOSE) {   /* défaite : bas (droite) au choix suivant, haut (gauche) au précédent, OK choisit */
        if (BT.police || BT.arene || BT.nuit) return;   /* contre la police : pas d'écran de défaite, l'arrestation suit ; arène : la sortie ; nuit : le réveil (battle.c) */
        int c[3];
        const int n = def_choix(c);
        if (pressed & (ENG_KEY_LEFT | ENG_KEY_RIGHT | ENG_KEY_UP | ENG_KEY_DOWN)) {
            DF.sel = (DF.sel + ((pressed & (ENG_KEY_DOWN | ENG_KEY_RIGHT)) ? 1 : n - 1)) % n; H.bkbd = 1; son_jouer(SON_MENU, 0.35f, 1);
        }
        if (pressed & ENG_KEY_OK) battle_defaite(c[DF.sel % n]);
        return;
    }
    if (menu && (pressed & (ENG_KEY_LEFT | ENG_KEY_RIGHT | ENG_KEY_UP | ENG_KEY_DOWN))) {
#if ENG_DYN
        const int step = (pressed & (ENG_KEY_RIGHT | ENG_KEY_DOWN)) ? 1 : 4;   /* grille : ordre de lecture */
#else
        const int step = (pressed & (ENG_KEY_LEFT | ENG_KEY_UP)) ? 1 : 4;   /* arc (FAN) : place 0 en bas, les suivantes remontent à gauche */
#endif
        H.bkbd = 1;
        for (int k = 0; k < 5; k++) { H.bsel = (H.bsel + step) % 5; if (H.bslot_cmd[H.bsel]) break; }
    }
    if (pressed & ENG_KEY_BACK) battle_command(BC_BACK, 0);
    if (pressed & ENG_KEY_OK) {
        if (ph == BP_TARGET) { const screen_pos_t *c = &E.scr_enemy[BT.cible > 0 ? BT.cible : 0]; if (c->vis) battle_tap(c->cx, c->cy); }
        else if (menu) { const int a = H.bslot_cmd[H.bsel]; if (a) battle_command((a - 1) >> 3, (a - 1) & 7); }
        else battle_tap(-1000, -1000);
    }
}

/* Conduite au doigt : en véhicule, hors menu, dialogue, choix, menu de course, pause et résultats de course */
static int conduite_doigts(void) {
    return E.ride >= 0 && !H.menu && !H.dialog && H.sub < 0 && !RM.on && !CHX.on && !BT.on && !(CR.on && (CR.pause || CR.phase == CP_RESULTATS));
}
static void drive_touch(const eng_input_t *in) {
    float px[2], py[2];
    int n = 0, pris[2] = { 0, 0 };
    if (in->touch) { px[n] = in->touch_x; py[n] = in->touch_y; n++; }
    if (in->touch2) { px[n] = in->touch2_x; py[n] = in->touch2_y; n++; }
    for (int k = 0; k < 2; k++) {   /* chaque doigt suivi prend le point libre le plus proche ; sans point : il est levé */
        doigt_t *d = &DG[k];
        if (!d->on) continue;
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < n; i++) if (!pris[i] && hypotf(px[i] - d->x, py[i] - d->y) < bd) { bd = hypotf(px[i] - d->x, py[i] - d->y); best = i; }
        if (best < 0) { d->on = 0; continue; }
        pris[best] = 1; d->x = px[best]; d->y = py[best];
    }
    for (int i = 0; i < n; i++) {   /* doigt qui se pose */
        if (pris[i]) continue;
        doigt_t *d = !DG[0].on ? &DG[0] : !DG[1].on ? &DG[1] : NULL;
        if (!d) break;
        d->on = 1; d->x0 = d->x = px[i]; d->y0 = d->y = py[i];
        const int b = button_at(px[i], py[i]);
        if (alt_visible() && (hypotf(px[i] - ALT_X, py[i] - ALT_UP_Y) < ALT_R + 8 || hypotf(px[i] - ALT_X, py[i] - ALT_DN_Y) < ALT_R + 8)) d->role = 2;
        else if (b >= 0) { d->role = 3; H.fx_x = px[i]; H.fx_y = py[i]; H.fx_t = H.now; do_action(BTN[b].act, BTN[b].arg); }
        else if (E.manette) d->role = 3;   /* manette : ni gaz ni volant au doigt */
        else {
            d->role = pointer_on() ? 5 : !joy_on() ? (px[i] < UW / 2 ? 0 : 1) : hypotf(px[i] - JOY_X, py[i] - JOY_Y) < JOY_R * 1.6f ? 4 : 3;
            doigt_t *o = d == &DG[0] ? &DG[1] : &DG[0];
            if (o->on && o->role == d->role) o->role = 3;   /* deux doigts du même côté (ou sur le joystick) : le dernier posé conduit */
        }
    }
    E.drive_th = E.drive_st = 0; E.drive_touch = 0; E.drive_alt = 0;
    int vise = 0;
    for (int k = 0; k < 2; k++) {
        const doigt_t *d = &DG[k];
        if (!d->on) continue;
        if (d->role == 5 && CR.on) {   /* POINTER en course : volant et gaz d'après la direction du doigt (POINTER_*) */
            const racer_t *j = &CR.r[CR.n - 1];
            float vx, vy;
            if (project(j->x, j->y, j->z, &vx, &vy)) {
                const float dx = d->x - vx, dy = vy - d->y, l = hypotf(dx, dy), g = fminf(1, l / (POINTER_PLEIN * fminf(UW, UH) / 2));
                if (l > POINTER_MORT) {
                    E.drive_st = fmaxf(-1, fminf(1, atan2f(dx, fabsf(dy)) / POINTER_ANGLE));
                    E.drive_th = dy >= -POINTER_MORT ? g : -g;
                    E.drive_touch = 1;
                }
            }
            continue;
        }
        if (d->role == 5) {   /* POINTER : le point du sol sous le doigt, que le véhicule rejoint (pilote.c), et l'allure :
                                 distance du doigt au véhicule à l'écran, en part du rayon de l'écran */
            float gx, gz, vx, vy;
            const float rayon = fminf(UW, UH) / 2;
            const float part = project(E.sera.x, ground_y(E.sera.x, E.sera.z), E.sera.z, &vx, &vy) ? hypotf(d->x - vx, d->y - vy) / rayon : 1;
            if (pick_ground((d->x + ui_ox) / ENG_SCALE, (d->y + ui_oy) / ENG_SCALE, &gx, &gz)) pilote_doigt(gx, gz, part);
            vise = 1;
            continue;
        }
        if (d->role == 0) {   /* gaz : zone morte de 12 %, puis en proportion ; vers le bas, frein puis marche arrière */
            const float g = fmaxf(-1, fminf(1, (d->y0 - d->y) / DRV_GAZ)), a = fabsf(g) < 0.12f ? 0 : (fabsf(g) - 0.12f) / 0.88f;
            E.drive_th = g < 0 ? -a : a; E.drive_touch = 1;
        } else if (d->role == 1) {   /* volant : zone morte de 10 %, puis une courbe douce (moitié droite, moitié carré) */
            const float v = fmaxf(-1, fminf(1, (d->x - d->x0) / DRV_VOL)), a = fabsf(v) < 0.1f ? 0 : (fabsf(v) - 0.1f) / 0.9f;
            E.drive_st = (v < 0 ? -1 : 1) * (0.45f * a + 0.55f * a * a); E.drive_touch = 1;
        } else if (d->role == 4) {   /* joystick : volant en travers, gaz selon l'écart de la poignée, frein vers le bas */
            float jx = (d->x - JOY_X) / JOY_R, jy = (JOY_Y - d->y) / JOY_R;
            const float l = fminf(1, hypotf(jx, jy)), lj = hypotf(jx, jy);
            if (lj > 1) { jx /= lj; jy /= lj; }
            const float a = fabsf(jx) < 0.15f ? 0 : (fabsf(jx) - 0.15f) / 0.85f, gaz = l < 0.15f ? 0 : fminf(1, (l - 0.15f) / 0.65f);
            E.drive_st = (jx < 0 ? -1 : 1) * (0.45f * a + 0.55f * a * a);
            E.drive_th = jy < -0.15f ? -gaz : gaz; E.drive_touch = 1;
        } else if (d->role == 2 && alt_visible()) {
            if (hypotf(d->x - ALT_X, d->y - ALT_UP_Y) < ALT_R + 8) E.drive_alt = 1;
            else if (hypotf(d->x - ALT_X, d->y - ALT_DN_Y) < ALT_R + 8) E.drive_alt = -1;
        }
    }
    pilote_doigt_tenu(vise);
    /* Geste du toucher simple (pointer) tenu tant qu'un doigt est posé : au retour à pied, son lever n'est pas un toucher */
    H.down = in->touch; H.walking = 1; H.hold = H.rim = H.list = 0; H.pinch = 0;
    if (in->touch) { H.cx = in->touch_x; H.cy = in->touch_y; }
}

void hud_update(float dt, const eng_input_t *in) {
    H.now += dt;
    if (accueil_on()) { accueil_update(dt, in); H.prev_keys = in->keys; return; }   /* écran titre, introduction, héroïne */
    if (accueil_regl_on()) { accueil_regl_update(dt, in); H.prev_keys = in->keys; E.ui_block = 1; return; }   /* RÉGLAGES de la pause de course (page d'accueil.c) */
    {   /* SYSTÈME refermé : ses réglages communs gardés s'ils ont changé (reglages_garder) */
        static int sub_avant = -1;
        if (sub_avant == SUB_SYSTEM && H.sub != SUB_SYSTEM) reglages_garder();
        sub_avant = H.sub;
    }
    { const int ps = member_sheet(0); if (ps != H.psheet) { H.psheet = ps; make_portrait(0, ps, 0); } }
    if (conduite_doigts()) drive_touch(in);
    else { DG[0].on = DG[1].on = 0; E.drive_touch = 0; E.drive_alt = 0; pilote_doigt_tenu(0); pointer(in); }
    carte_actif(H.sub == SUB_MAP); if (H.sub == SUB_MAP) carte_maj(dt);   /* carte du menu (carte.c) : fermée, elle rend le tampon */
    /* Élan d'une liste lâchée en mouvement : il s'amortit, ou s'arrête au bout de la liste */
    if (H.fling != 0 && !H.down) {
        if (H.sub < 0) H.fling = 0;
        else { drag_rows(H.fling * dt); H.fling *= expf(-4 * dt); if (fabsf(H.fling) < 60) H.fling = 0; }
    }
    if (E.ride < 0 && H.down && H.hold && !RM.on && (H.now - H.t0 > 0.15f || H.moved > 14)) {
        H.walking = 1;
        viser(H.cx, H.cy);
    }
    /* Sons de l'interface : un bip toutes les trois lettres du texte qui défile, un tic quand le menu change */
    if (H.dialog && H.line < D.n) {
        int len = 0; for (const char *s = dlg_texte(); *s; s++) if ((*s & 0xC0) != 0x80) len++;
        const int b = (int)((H.now - H.line_t0) / 0.084f);
        if (b >= 0 && b * 3 < len && b != H.bip_k) { H.bip_k = b; son_jouer(SON_BIP, 0.25f, 0.9f + 0.1f * (b % 3)); }
    } else H.bip_k = -1;
    const int sig = H.menu * 4096 + (H.sub + 1) * 512 + H.sel * 64 + H.sub_sel;
    if (sig != H.menu_sig && (H.menu || H.sub >= 0)) son_jouer(SON_MENU, 0.35f, 1);
    H.menu_sig = sig;
    uint32_t pressed = in->keys & ~H.prev_keys;
    H.prev_keys = in->keys;
    /* Zoom changé (pinch, menu, molette de la page web) : niveau affiché un instant ; pas le zoom qui suit le lieu */
    if (E.zoom_auto) H.shown_zoom = E.zoom;
    if (fabsf(E.zoom - H.shown_zoom) > 0.004f) {
        char t[40];
        H.shown_zoom = E.zoom;
        snprintf(t, sizeof t, "ZOOM %d %%%s", (int)lroundf(E.zoom * 100),
                 E.zoom <= E.w.zoom_min + 0.001f ? " · MINI" : E.zoom >= ZOOM_MAX - 0.001f ? " · MAXI" : "");
        toast(t);
    }
    if (BT.on) { battle_keys(pressed); E.ui_block = 1; return; }
    if (RM.on) {   /* menu de course : haut / bas une ligne, gauche / droite sa valeur, OK départ, retour annuler */
        if (pressed & ENG_KEY_UP) RM.row = clampi(RM.row - 1, RM.mode ? 1 : 0, RM_LIGNES[RM.page] - 1);   /* (menu des véhicules : pas d'onglets) */
        if (pressed & ENG_KEY_DOWN) RM.row = clampi(RM.row + 1, 0, RM_LIGNES[RM.page] - 1);
        if (pressed & ENG_KEY_LEFT) course_menu_change(RM.row, -1);
        if (pressed & ENG_KEY_RIGHT) course_menu_change(RM.row, 1);
        if (pressed & ENG_KEY_OK) course_menu_go();
        else if (pressed & ENG_KEY_BACK) course_menu_cancel();
        E.ui_block = RM.on;
        return;
    }
    if (CR.on && CR.pause) {   /* pause de course : haut / bas un choix, OK le valide, retour ou menu reprend */
        if (pressed & (ENG_KEY_UP | ENG_KEY_DOWN)) CR.pause_sel = (CR.pause_sel + ((pressed & ENG_KEY_UP) ? pause_n() - 1 : 1)) % pause_n();
        if (pressed & ENG_KEY_OK) pause_choix(CR.pause_sel);
        else if (pressed & (ENG_KEY_BACK | ENG_KEY_MENU)) course_pause(0);
        E.ui_block = 1;
        return;
    }
    if (H.sub == SUB_MAP && !H.dialog && !CHX.on) pressed = carte_touches(pressed, in, dt);   /* carte du menu : flèches, stick, OK, A / E */
    if (H.sub != SUB_SYSTEM) H.vol_on = 0;   /* menu refermé ou autre écran */
    if (H.vol_on) {   /* cadran du son : haut plus fort, bas moins fort, gauche / droite le canal, OK ou retour le referme */
        if (pressed & ENG_KEY_UP) vol_pas(1);
        if (pressed & ENG_KEY_DOWN) vol_pas(-1);
        if (pressed & ENG_KEY_RIGHT) vol_canal(1);
        if (pressed & ENG_KEY_LEFT) vol_canal(-1);
        if (pressed & (ENG_KEY_OK | ENG_KEY_BACK)) H.vol_on = 0;
        pressed &= ~(uint32_t)(ENG_KEY_RIGHT | ENG_KEY_UP | ENG_KEY_LEFT | ENG_KEY_DOWN | ENG_KEY_OK | ENG_KEY_BACK);
    }
    if (BUT.on) {   /* panneau du butin d'un coffre : OK ou retour le ferme */
        if (pressed & (ENG_KEY_OK | ENG_KEY_BACK)) butin_fermer();
        E.ui_block = 1;
        return;
    }
    if ((pressed & ENG_KEY_MENU) && CR.on) course_pause(1);
    if ((pressed & ENG_KEY_MENU) && !CR.on && !ile_offrande_en_cours()) { if (!H.dialog) { H.menu = !H.menu; H.sub = -1; E.has_dest = 0; } }
    if ((MISE.fuite_on || police_bloque()) && (H.menu || H.sub >= 0)) { H.menu = 0; H.sub = -1; }   /* pas de menu pendant une fuite ni une arrestation */
    if (pressed & ENG_KEY_BACK) {
        if (H.dialog) H.dialog = 0;
        else if (CHX.on) { if (CHX.porte || CHX.vol || CHX.shop || CHX.tele || CHX.dormir) { choice_pick(CHX.n - 1); son_jouer(SON_ANNULER, 0.4f, 1); } }   /* confirmation : RETOUR annule (la dernière réponse) ; un choix du scénario attend sa réponse */
        else if (H.sub >= 0) sub_back();
        else { H.menu = !H.menu; E.has_dest = 0; }
    }
    if (CHX.on) {   /* choix : haut / bas une réponse, OK la prend */
        if (pressed & ENG_KEY_DOWN) CHX.sel = (CHX.sel + 1) % CHX.n;
        if (pressed & ENG_KEY_UP) CHX.sel = (CHX.sel + CHX.n - 1) % CHX.n;
        if (pressed & ENG_KEY_OK) choice_pick(CHX.sel);
        E.ui_block = 1;
        return;
    }
    if (pressed & ENG_KEY_OK) {
        if (ile_offrande_passer()) { }   /* île : la séquence d'une succube, l'animation suivante (ile.c) */
        else if (H.dialog) advance_dialog();
        else if (H.sub >= 0) activate_row(H.sub_sel);
        else if (H.menu) open_sub(MENU_SUB(H.sel));
        else if (CR.on) { if (CR.phase == CP_RESULTATS) course_end(); else course_tir(); }
        else if (E.coffre >= 0 && E.ride < 0 && coffre_avant()) coffre_ouvrir();   /* un coffre, plus près que l'interlocuteur */
        else if (E.panneau >= 0 && E.ride < 0 && panneau_avant()) hud_panneau_ouvrir(E.panneau);   /* un panneau de quêtes, plus près que l'interlocuteur */
        else if (ile_dormir_k() >= 0 && E.ride < 0 && ile_dormir_avant()) dormir_demander();   /* la couche d'un abri de l'île, plus près que l'interlocuteur */
        else if (E.porte >= 0 && E.ride < 0 && (E.talk == -2 || E.talk <= TALK_TRAF(0))) int_interagir();   /* la porte avant un véhicule du trafic */
        else if (near_npc()) do_action(A_TALK, 0);
    }
    if (H.sub >= 0) {
        int n = sub_rows();
        if (H.sub == SUB_SHOP && SHOP.fiche >= 0) {   /* fiche de la boutique : haut, bas un autre membre ; gauche, droite OUI, NON */
            if (pressed & (ENG_KEY_UP | ENG_KEY_DOWN)) shop_membre(pressed & ENG_KEY_UP ? -1 : 1);
            if (pressed & (ENG_KEY_LEFT | ENG_KEY_RIGHT)) H.sub_sel = !H.sub_sel;
        } else {
            if ((pressed & ENG_KEY_DOWN) && H.sub_sel < n - 1) H.sub_sel++;
            if ((pressed & ENG_KEY_UP) && H.sub_sel > 0) H.sub_sel--;
            if (H.sub == SUB_EQUIP && EQM.niv == EQN_FICHE && (pressed & (ENG_KEY_LEFT | ENG_KEY_RIGHT))) H.sub_sel = !H.sub_sel;   /* OUI, NON côte à côte */
        }
    } else if (H.menu) {
        if (pressed & (ENG_KEY_RIGHT | ENG_KEY_DOWN)) H.sel = (H.sel + 1) % NMENU;
        if (pressed & (ENG_KEY_LEFT | ENG_KEY_UP)) H.sel = (H.sel + NMENU - 1) % NMENU;
    }
    E.ui_block = H.menu || H.dialog || RM.on || CHX.on;
}

void hud_choice(char opts[][48], int n) {
    CHX.n = n < 1 ? 1 : n > 4 ? 4 : n; CHX.sel = 0; CHX.on = 1; CHX.porte = 0; CHX.commerce = 0; CHX.vol = 0; CHX.shop = 0; CHX.tele = 0; CHX.dormir = 0; CHX.succube = 0; CHX.titre[0] = 0; E.has_dest = 0;
    CHX.texte[0][0] = CHX.texte[1][0] = 0;
    for (int k = 0; k < CHX.n; k++) snprintf(CHX.opts[k], sizeof CHX.opts[k], "%s", opts[k]);
}
/* Lignes sous le titre du choix en cours (ce qui est en jeu : interieur.c, la porte fermée) */
void hud_choice_texte(const char *l1, const char *l2) {
    snprintf(CHX.texte[0], sizeof CHX.texte[0], "%s", l1 ? l1 : "");
    snprintf(CHX.texte[1], sizeof CHX.texte[1], "%s", l2 ? l2 : "");
}
void hud_choice_porte(char opts[][48], int n, const char *titre) {
    hud_choice(opts, n);
    CHX.porte = 1; snprintf(CHX.titre, sizeof CHX.titre, "%s", titre);
}
void hud_choice_titre(char opts[][48], int n, const char *titre) {
    hud_choice(opts, n);
    snprintf(CHX.titre, sizeof CHX.titre, "%s", titre);
}
/* Choix de l'arène (arene.c) : la réponse va à arene_choix */
void hud_choice_arene(char opts[][48], int n, const char *titre, const char *l1, const char *l2) {
    hud_choice_titre(opts, n, titre);
    hud_choice_texte(l1, l2);
    CHX.arene = 1;
}
int hud_in_choice(void) { return CHX.on; }
/* Banc d'essai : la boîte de choix en cours (titre | lignes | réponses) ; 0 : aucune */
int hud_choix_texte(char *out, int n) {
    if (!CHX.on) { if (n) out[0] = 0; return 0; }
    snprintf(out, n, "%s | %s | %s | %s / %s (choisie : %d)", CHX.titre, CHX.texte[0], CHX.texte[1], CHX.opts[0], CHX.n > 1 ? CHX.opts[1] : "", CHX.sel);
    return 1;
}

/* ---------- Dessin ---------- */
static uint16_t C1, C2;

static void glow_outline(const float *q, int n, float w, uint16_t c) {
    ui_outline(q, n, w + 5, c, 60);
    ui_outline(q, n, w, c, 255);
}

static void hex_button(float x, float y, float w, float h, uint16_t col, int selected, const char *icon, int icon_size) {
    float q[12];
    ui_hex_path(x, y, w, h, q);
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, selected ? 0.24f : 0.10f), 242);
    if (selected) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 2, col, 255);
    if (icon) ui_icon(icon, x, y, icon_size, selected ? ui_rgb(WHITE) : col, 255);
}

/* Graduations du pourtour */
/* Graduations du pourtour : sinus et cosinus des 120 angles précalculés, la rotation de la caméra s'applique par
   addition d'angles (deux calculs trigonométriques par image au lieu de 240) */
static void rim_ticks(float rot, int alpha) {
    static float ks[120] ENG_FROID, kc[120] ENG_FROID;
    static int ready;
    if (!ready) { for (int k = 0; k < 120; k++) { ks[k] = sinf(k / 120.0f * 2 * PI_F); kc[k] = cosf(k / 120.0f * 2 * PI_F); } ready = 1; }
    float sr = sinf(rot), cr = cosf(rot);
    for (int k = 0; k < 120; k++) {
        float s = ks[k] * cr - kc[k] * sr, c = kc[k] * cr + ks[k] * sr, r0 = k % 10 == 0 ? 218 : 223;
        ui_line(CX + s * r0, CY - c * r0, CX + s * 229, CY - c * 229, 2, C1, alpha);
    }
}

static void dashed_ring(float r, float dash, float gap, float w, uint16_t c, int alpha) {
    float step = (dash + gap) / r, len = dash / r;
    for (float a = 0; a < 2 * PI_F; a += step) ui_arc(CX, CY, r, a, a + len, w, c, alpha);
}

/* Interface de course (drawCourse de l'émulateur) : tour, place et temps en haut, feux, compteur sur le bord en bas à
   droite, plan de la piste à gauche, sens inverse ; puis le classement */
static void chrono(float t, char *s, int n) { const int c = (int)(t * 100); snprintf(s, n, "%d:%02d.%02d", c / 6000, c / 100 % 60, c % 100); }
static const char *nom_course(const racer_t *r) { return r->joueur ? (E.w.sc_player[0] ? E.w.sc_player : "SERA") : E.w.persos[r->perso].nom; }
static void draw_course(void) {
#if ENG_DYN
    portrait_course(); return;
#endif
    int order[COURSE_N];
    const int place = course_rank(order);
    const racer_t *j = &CR.r[CR.n - 1];
    char t[48];
    if (CR.phase == CP_RESULTATS) {
        ui_rect(0, 118, ENG_SCREEN, 236, ui_rgb(0x05040A), 224);
        ui_text(ui_font(FONT_DISP, 30), CX + 1, 159, UI_CENTER, C1, 90, "ARRIVÉE");
        ui_text(ui_font(FONT_DISP, 30), CX, 158, UI_CENTER, ui_rgb(0xE8F6FF), 255, "ARRIVÉE");
        for (int i = 0; i < CR.n; i++) {
            const racer_t *r = &CR.r[order[i]];
            const float y = 194.0f + i * 28;
            const uint16_t c = r->joueur ? C2 : ui_rgb(0xE8F6FF);
            snprintf(t, sizeof t, "%d. %s", i + 1, nom_course(r));
            ui_text(ui_fit(FONT_DISP, t, 170, 20, 16), 96, y, UI_LEFT, c, 255, t);
            if (r->fin >= 0) chrono(r->fin, t, sizeof t); else snprintf(t, sizeof t, "--");
            ui_text(ui_font(FONT_MONO, 20), 370, y, UI_RIGHT, c, 255, t);
        }
        if (CR.chrono > 0) {
            const int ok = j->fin >= 0 && j->fin <= CR.chrono;
            ui_text(ui_font(FONT_MONO, 18), CX, 318, UI_CENTER, ui_rgb(ok ? 0x33FF88 : 0xFF3344), 255, ok ? "TEMPS BATTU" : "TEMPS DÉPASSÉ");
        }
        ui_text(ui_font(FONT_MONO, 18), CX, 340, UI_CENTER, ui_rgb(0x7F93A8), 255, "Toucher pour continuer");
        return;
    }
    /* Accélération : halo blanc sur le bord de l'écran (anneau dégradé de 197 à 233 px, de plus en plus pâle vers le
       centre : ui_halo, une passe) et traits de vitesse (16, tirés à nouveau 24 fois par seconde) ; comme l'émulateur */
    if (j->halo > 0) {
        const uint16_t blanc = ui_rgb(0xFFFFFF);
        ui_halo(CX, CX, 197, 233, blanc, (int)(j->halo * 215), 1);
        const int f = (int)floorf(CR.clock * 24);
        for (int i = 0; i < 16; i++) {
            const float a = i * PI_F / 8 + ((i * 7 + f * 3) % 5) * 0.06f, r0 = 182.0f + ((i * 5 + f * 7) % 6) * 6;
            ui_line(CX + cosf(a) * r0, CX + sinf(a) * r0, CX + cosf(a) * 228, CX + sinf(a) * 228, 2, blanc, (int)(j->halo * 150));
        }
    }
    const int tour = clampi(j->tour + 1, 1, CR.tours);
    int bc = 0;   /* épreuve de la carrière (course_bandeau) : essai, écart au fantôme, éliminés */
    if (!course_bandeau(0, t, sizeof t, &bc)) snprintf(t, sizeof t, "TOUR %d/%d", tour, CR.tours);
    ui_text(ui_font(FONT_MONO, 18), CX, 66, UI_CENTER, C1, 255, t);
    if (!course_bandeau(1, t, sizeof t, &bc)) snprintf(t, sizeof t, "%d%s / %d", place, place == 1 ? "er" : "e", CR.n);
    ui_text(ui_fit(FONT_DISP, t, 220, 30, 20), CX + 1, 101, UI_CENTER, C1, 90, t);
    ui_text(ui_fit(FONT_DISP, t, 220, 30, 20), CX, 100, UI_CENTER, ui_rgb(bc == 1 ? 0x33FF88 : bc == 2 ? 0xFF3344 : 0xE8F6FF), 255, t);
    if (!course_bandeau(2, t, sizeof t, &bc)) chrono(j->fin >= 0 ? j->fin : CR.t, t, sizeof t);
    ui_text(ui_font(FONT_MONO, 20), CX, 126, UI_CENTER, C2, 255, t);
    if (CR.chrono > 0) {   /* temps à battre (rouge une fois dépassé) */
        char ch[16], tb[32];
        chrono(CR.chrono, ch, sizeof ch);
        snprintf(tb, sizeof tb, "À BATTRE %s", ch);
        ui_text(ui_font(FONT_MONO, 16), CX, 146, UI_CENTER, ui_rgb((j->fin >= 0 ? j->fin : CR.t) > CR.chrono ? 0xFF3344 : 0x7F93A8), 255, tb);
    }
    /* Feux : trois rouges, puis verts au départ (une seconde) */
    if (CR.phase == CP_GRILLE || CR.t < 1) {
        const int vert = CR.phase != CP_GRILLE;
        const uint16_t c = ui_rgb(vert ? 0x33FF88 : 0xFF3344);
        for (int k = 0; k < 3; k++) {
            ui_circle(193.0f + 40 * k, 172, 16, ui_rgb(0x060E1A), 217);
            if (vert || k < CR.feux) { ui_circle(193.0f + 40 * k, 172, 14, c, 70); ui_circle(193.0f + 40 * k, 172, 11, c, 255); }
        }
        if (vert) ui_text(ui_font(FONT_DISP, 30), CX, 218, UI_CENTER, c, 255, "GO");
    }
    if (j->inv > 0.8f && CR.phase == CP_COURSE) ui_text(ui_font(FONT_DISP, 22), CX, 160, UI_CENTER, ui_rgb(0xFF3344), 255, "SENS INVERSE");
    if (j->boost > 0) ui_text(ui_font(FONT_DISP, 22), CX, 160, UI_CENTER, ui_rgb(0x22E4FF), 255, "BOOST");
    /* Arme en main : bouton de tir à droite, au-dessus du volant (ou OK), son nom au-dessus */
    /* Pause : bouton en haut à droite (ou touche menu) */
    if (CR.phase != CP_RESULTATS) {
        float qp[12];
        ui_hex_path(372, 116, 52, 46, qp);
        ui_poly(qp, 6, ui_rgb(0x060E1A), 230);
        ui_outline(qp, 6, 2, C1, 204);
        ui_icon("pause", 372, 116, 22, C1, 255);
        button(372, 116, 27, A_PAUSE, 0);
    }
    if (CR.armes && j->fin < 0) {
        static const char *NOM[5] = { "", "MISSILE", "MINE", "BOUCLIER", "TURBO" }, *ICO[5] = { "", "missile", "mine", "equip", "tech" };
        float q[12];
        ui_hex_path(372, 200, 64, 56, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 230);
        if (j->arme) {
            glow_outline(q, 6, 2, C2);
            ui_icon(ICO[j->arme], 372, 200, 26, C2, 255);
            ui_text(ui_font(FONT_MONO, 16), 372, 164, UI_CENTER, C2, 255, NOM[j->arme]);
            button(372, 200, 32, A_FIRE, 0);
        } else ui_outline(q, 6, 2, ui_rgb(0x3A4A5C), 255);
    }
    /* Compteur : arc sur le bord, en bas à droite */
    const float k = fminf(1, fabsf(j->v) / j->vmax);
    ui_arc(CX, CY, 205, 0.3f, 1.3f, 6, ui_rgb(0xFFFFFF), 38);
    ui_arc(CX, CY, 205, 0.3f, 0.3f + fmaxf(0.01f, k), 6, C1, 255);
    snprintf(t, sizeof t, "%d", (int)lroundf(fabsf(j->v) * 25));
    ui_text(ui_font(FONT_DISP, 26), 352, 342, UI_CENTER, C1, 255, t);
    ui_text(ui_font(FONT_MONO, 18), 352, 362, UI_CENTER, C1, 255, "KM/H");
    /* Plan : la piste (48 segments au plus) et les concurrents (le joueur en couleur 2) */
    const float sc = fminf(96 / fmaxf(1, PISTE.x1 - PISTE.x0), 64 / fmaxf(1, PISTE.z1 - PISTE.z0));
    const float mx = 112 + (PISTE.x0 + PISTE.x1) / 2 * sc, mz = 316 - (PISTE.z0 + PISTE.z1) / 2 * sc;   /* x en miroir, comme la carte (CARTE_X de carte.c) */
    const int step = PISTE.n > 48 ? PISTE.n / 48 : 1;
    for (int i = 0; i < PISTE.n; i += step) {
        const int e = i + step < PISTE.n ? i + step : 0;
        ui_line(mx - PISTE.X[i] * sc, mz + PISTE.Z[i] * sc, mx - PISTE.X[e] * sc, mz + PISTE.Z[e] * sc, 3, C1, 140);
    }
    for (int i = 0; i < CR.n; i++) if (!CR.r[i].joueur && !CR.r[i].elim) ui_circle(mx - CR.r[i].x * sc, mz + CR.r[i].z * sc, 3, ui_rgb(0xE8F6FF), 255);
    { const fantome_t *g = course_fantome(); if (g) ui_circle(mx - g->x * sc, mz + g->z * sc, 3, ui_rgb(0x60D8FF), 160); }   /* fantôme */
    ui_circle(mx - j->x * sc, mz + j->z * sc, 4.5f, C2, 255);
}

/* Pause de course (drawCoursePause de l'émulateur) : seuls ses deux boutons répondent */
static void draw_course_pause(void) {
#if ENG_DYN
    portrait_course_pause(); return;
#endif
    float q[12];
    nbtn = 0;
    voile_ecran(ui_rgb(0x05040A), 209);
    const int np = pause_n(), tr = carriere_mode() && carr_sorte_course() == CS_TOUR;   /* mode course : RECOMMENCER ; tour rapide : TERMINER */
    const int n4 = np >= 4;   /* (quatre choix depuis RÉGLAGES : resserrés, le titre plus haut) */
    ui_text(ui_font(FONT_DISP, 30), CX + 1, n4 ? 151 : 161, UI_CENTER, C1, 90, "PAUSE");
    ui_text(ui_font(FONT_DISP, 30), CX, n4 ? 150 : 160, UI_CENTER, ui_rgb(0xE8F6FF), 255, "PAUSE");
    for (int i = 0; i < np; i++) {
        const float y = n4 ? 190.0f + i * 54 : (np == 3 ? 212.0f : 236.0f) + i * (np == 3 ? 64 : 76), h = n4 ? 46 : np == 3 ? 52 : 58;
        const int q0 = pause_quoi(i);
        const uint16_t col = q0 == PA_FIN && !tr ? ui_rgb(0xFF3344) : C1;
        ui_hex_path(CX, y, 190, h, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 235);
        if (i == CR.pause_sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 2, col, 255);
        const char *lab = q0 == PA_REPRENDRE ? "REPRENDRE" : q0 == PA_REGLAGES ? "RÉGLAGES" : q0 == PA_RECOMMENCER ? "RECOMMENCER" : tr ? "TERMINER" : "ABANDONNER";
        ui_text(ui_fit(FONT_DISP, lab, 160, 22, 16), CX, y + 8, UI_CENTER, col, 255, lab);
        const int act = q0 == PA_REPRENDRE ? A_REPRENDRE : q0 == PA_REGLAGES ? A_PAUSE_REGL : q0 == PA_RECOMMENCER ? A_RECOMMENCER : A_ABANDON;
        if (n4) button_rect(CX - 95, y - 26, 190, 52, act, 0);   /* (rangées serrées : le rectangle de chacune) */
        else button(CX, y, np == 3 ? 52 : 60, act, 0);
    }
    ui_text(ui_font(FONT_MONO, 18), CX, n4 ? 400 : np == 3 ? 394 : 372, UI_CENTER, ui_rgb(0x7F93A8), 255, tr ? "Terminer : le meilleur tour compte" : "Abandon : la course ne compte pas");
}

/* Menu de course (drawCourseMenu de l'émulateur) : flèches d'une ligne (actif : une valeur à changer), ligne à valeur */
static void rm_fleches(int row, float y, float dx, float w, float h, int actif) {
    float q[12];
    for (int sg = -1; sg <= 1; sg += 2) {
        const float x = CX + sg * dx;
        ui_hex_path(x, y, w, h, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 235);
        ui_outline(q, 6, 2, C1, !actif ? 64 : RM.row == row ? 255 : 140);
        ui_icon(sg < 0 ? "back" : "next", x, y, (int)(h * 0.45f), C1, actif ? 255 : 90);
        button(x, y, w / 2 + 2, A_RACE_CHG, row * 2 + (sg > 0));
    }
}
static void rm_valeur(int row, const char *lab, const char *val, float y, float dx) {
    ui_text(ui_font(FONT_MONO, 16), CX, y - 8, UI_CENTER, ui_rgb(0x7F93A8), 255, lab);
    ui_text(ui_fit(FONT_DISP, val, 200, 24, 16), CX, y + 18, UI_CENTER, RM.row == row ? C1 : ui_rgb(0xE8F6FF), 255, val);
    rm_fleches(row, y + 9, dx, 44, 40, !rm_fige(RM.page, row));
}
static void draw_course_menu(void) {
#if ENG_DYN
    portrait_course_menu(); return;
#endif
    const uint16_t GRIS = ui_rgb(0x7F93A8), BLANC = ui_rgb(0xE8F6FF);
    char t[48];
    float q[12];
    voile_ecran(ui_rgb(0x05040A), 224);
    const char *titre = RM.mode == RM_DON ? "VÉHICULES" : RM.mode == RM_BOUTIQUE ? "CONCESSION" : "COURSE";
    ui_text(ui_font(FONT_DISP, 26), CX + 1, 65, UI_CENTER, C1, 90, titre);
    ui_text(ui_font(FONT_DISP, 26), CX, 64, UI_CENTER, BLANC, 255, titre);
    int var = 0;
    const int pc = rm_couleur(&var);   /* véhicule de la couleur choisie (page PILOTE) et sa palette */
    if (RM.mode == RM_DON) ui_text(ui_font(FONT_MONO, 18), CX, 98, UI_CENTER, C2, 255, "OFFERT");   /* à la place des onglets */
    else if (RM.mode) {   /* boutique : son prix (rouge s'il est trop cher), les crédits de l'équipe */
        const int prix = veh_prix(pc);
        snprintf(t, sizeof t, "PRIX %d CR", prix);
        ui_text(ui_font(FONT_MONO, 18), CX, 98, UI_CENTER, prix <= E.credits ? C2 : ui_rgb(0xFF3344), 255, t);
        snprintf(t, sizeof t, "CRÉDITS %d CR", E.credits);
        ui_text(ui_font(FONT_MONO, 14), CX, 119, UI_CENTER, GRIS, 255, t);
    }
    for (int i = 0; i < 3 && !RM.mode; i++) {   /* onglets (ligne 0) */
        const float x = CX + (i - 1) * 108.0f;
        const int on = i == RM.page;
        const uint16_t c = on ? (RM.row == 0 ? C2 : C1) : GRIS;
        ui_text(ui_font(FONT_MONO, 18), x, 98, UI_CENTER, c, 255, RM_PAGES[i]);
        if (on) ui_rect(x - 38, 105, 76, 2, c, 255);
        button(x, 92, 36, A_RACE_PAGE, i);
    }
    if (RM.page == 0) {
        /* Le véhicule qui tourne, son nom, sa couleur ; vitesse et accélération par rapport au meilleur véhicule de
           course, adhérence sur 160 % */
        const int p = pc, dir = (int)((H.now - RM.t0) / 0.35f) & 7, nc = rm_ncoul(RM.mod);
        if (rm_apercu_v(0, p, var, dir, RM_AP)) {
            const float sc = fminf(fminf(150.0f / rm_w[0], 100.0f / rm_h[0]), 5);
            ui_image(rm_px, rm_mk, rm_w[0], rm_h[0], CX - rm_w[0] * sc / 2, 184 - rm_h[0] * sc / 2, sc, CX, CY, 232);
        }
        rm_fleches(1, 184, 162, 48, 44, 1);
        ui_text(ui_fit(FONT_DISP, E.w.persos[p].nom, 260, 24, 16), CX, 262, UI_CENTER, RM.row == 1 ? C1 : BLANC, 255, E.w.persos[p].nom);
        snprintf(t, sizeof t, "COULEUR %d/%d", RM.coul + 1, nc);
        ui_text(ui_font(FONT_MONO, 18), CX, 296, UI_CENTER, RM.row == 2 ? C1 : GRIS, 255, t);
        rm_fleches(2, 290, 112, 38, 34, nc > 1);
        float vm = 0, am = 0, v, a;
        for (int i = 0; i < RM.n; i++) { veh_params(RM.ord[i], &v, &a); vm = fmaxf(vm, v); am = fmaxf(am, a); }
        veh_params(p, &v, &a);
        static const char *const LAB[3] = { "VITESSE", "ACCÉL.", "ADHÉRENCE" };
        const float k3[3] = { v / fmaxf(vm, 1e-3f), a / fmaxf(am, 1e-3f), E.w.veh_grip[p] / 1.6f };
        for (int i = 0; i < 3; i++) {
            const float y = 330 + i * 24.0f, k = fmaxf(0.03f, fminf(1, k3[i]));
            ui_text(ui_font(FONT_MONO, 16), 92, y + 5, UI_LEFT, GRIS, 255, LAB[i]);
            ui_rect(208, y - 4, 160, 8, ui_rgb(0xFFFFFF), 31);
            ui_rect(208, y - 4, 160 * k, 8, C2, 255);
        }
    } else if (RM.page == 1) {
        snprintf(t, sizeof t, "%d", RM.nia);
        rm_valeur(1, "ADVERSAIRES", t, 132, 120);
        rm_valeur(2, "NIVEAU", NIVEAUX[RM.niv], 182, 120);
        for (int k = 0; k < RM_NR; k++) {
            const float y = 240 + k * 38.0f;
            const int actif = k < RM.nia, p = RM.riv[k];
            if (rm_apercu(1 + k, p, 1, 40)) {
                const float sc = fminf(40.0f / rm_w[1 + k], 30.0f / rm_h[1 + k]);
                ui_image(rm_px + (1 + k) * RM_AP * RM_AP, rm_mk + (1 + k) * RM_AP * RM_AP, rm_w[1 + k], rm_h[1 + k], 124 - rm_w[1 + k] * sc / 2, y - rm_h[1 + k] * sc / 2, sc, CX, CY, 232);
            }
            if (!actif) ui_rect(100, y - 17, 48, 34, ui_rgb(0x05040A), 170);
            ui_text(ui_fit(FONT_DISP, E.w.persos[p].nom, 160, 20, 14), 152, y + 7, UI_LEFT, RM.row == 3 + k ? C1 : actif ? BLANC : ui_rgb(0x3A4A5C), 255, E.w.persos[p].nom);
            rm_fleches(3 + k, y, 166, 36, 32, actif && !RM.fige_riv);
        }
    } else {
        /* Plan des tracés : les autres en pâle, le choisi en couleur 1, la ligne de départ en couleur 2 */
        const piste_t *Pc = RM.ntr ? course_piste(RM.trace[RM.tr]) : &PISTE;
        float x0 = Pc->x0, x1 = Pc->x1, z0 = Pc->z0, z1 = Pc->z1;
        for (int k = 0; k < RM.ntr; k++) {
            const piste_t *P = course_piste(RM.trace[k]);
            x0 = fminf(x0, P->x0); x1 = fmaxf(x1, P->x1); z0 = fminf(z0, P->z0); z1 = fmaxf(z1, P->z1);
        }
        const float sc = fminf(180 / fmaxf(1, x1 - x0), 84 / fmaxf(1, z1 - z0)), mx = CX + (x0 + x1) / 2 * sc, mz = 168 - (z0 + z1) / 2 * sc;   /* x en miroir (CARTE_X) */
        for (int pass = 0; pass < 2; pass++) for (int k = 0; k < (RM.ntr ? RM.ntr : 1); k++) {
            const int sel = !RM.ntr || k == RM.tr;
            if (sel != pass) continue;
            const piste_t *P = RM.ntr ? course_piste(RM.trace[k]) : &PISTE;
            const int step = P->n > 64 ? P->n / 64 : 1;
            for (int i = 0; i < P->n; i += step) {
                const int e = i + step < P->n ? i + step : 0;
                ui_line(mx - P->X[i] * sc, mz + P->Z[i] * sc, mx - P->X[e] * sc, mz + P->Z[e] * sc, sel ? 3 : 2, sel ? C1 : GRIS, sel ? 255 : 77);
            }
        }
        ui_circle(mx - Pc->X[Pc->dep] * sc, mz + Pc->Z[Pc->dep] * sc, 5, C2, 255);
        snprintf(t, sizeof t, "TRACÉ · %s", course_surface_nom());
        rm_valeur(1, t, Pc->nom[0] ? Pc->nom : "PISTE", 236, 150);
        char tt[12];
        snprintf(tt, sizeof tt, "%d", RM.tours);
        char lab[40];
        if (RM.chrono > 0) { char ch[16]; chrono(RM.chrono, ch, sizeof ch); snprintf(lab, sizeof lab, "TOURS · À BATTRE %s", ch); }
        else snprintf(lab, sizeof lab, "TOURS");
        rm_valeur(2, lab, tt, 290, 120);
        rm_valeur(3, "ARMES", RM.armes ? "OUI" : "NON", 342, 120);
    }
    ui_hex_path(158, 414, 66, 50, q);
    ui_poly(q, 6, ui_rgb(0x060E1A), 235);
    ui_outline(q, 6, 2, GRIS, 255);
    ui_icon("back", 158, 414, 22, GRIS, 255);
    button(158, 414, 31, A_RACE_BACK, 0);
    ui_hex_path(286, 414, 140, 52, q);
    ui_poly(q, 6, ui_rgb(0x060E1A), 235);
    const int cher = RM.mode == RM_BOUTIQUE && veh_prix(pc) > E.credits;   /* trop cher : le bouton terne */
    if (cher) ui_outline(q, 6, 2, GRIS, 255); else glow_outline(q, 6, 2, C2);
    ui_text(ui_font(FONT_DISP, 22), 286, 422, UI_CENTER, cher ? GRIS : C2, 255, RM.mode == RM_DON ? "PRENDRE" : RM.mode ? "ACHETER" : "DÉPART");
    button(286, 414, 46, A_RACE_GO, 0);
}

/* Effets de course (drawCourseFx de l'émulateur), par-dessus la 3D, sous l'interface : fumée et poussière en disques qui
   grossissent (0,25 à 0,75 case, 26 px au plus) et s'effacent, étincelles en traits jaunes puis orange, de leur position
   un instant plus tôt à maintenant */
/* Sous le tablier d'un pont (vu d'au-dessus) : caché */
static int sous_pont(float x, float z, float y) { const float t = deck_above(x, z, y); return t == t; }
/* Gyrophares des véhicules d'urgence du trafic (traffic_t.urgence) : deux feux au-dessus du toit (62 % de la hauteur du
   sprite), qui s'allument tour à tour trois fois par seconde, avec un halo ; police rouge et bleu, secours et pompiers
   bleus ; à moins de 16 cases de Sera (gyrophares de l'émulateur) */
static void draw_gyrophares(void) {
    if (E.in.on || CR.on) return;
    const int ph = (int)(H.now * 6) & 1;
    for (int k = 0; k < TRAFFIC_MAX; k++) {
        const traffic_t *t = &E.traffic[k];
        if (!t->on || !t->urgence || hypotf(t->x - E.sera.x, t->z - E.sera.z) > 16.0f / E.w.ech) continue;
        const perso_t *pe = &E.w.persos[t->perso];
        const float h = 2.0f / E.w.ech * E.w.sprite_scale * (pe->taille > 0 ? pe->taille : 1), y = height_at(t->x, t->z);
        float sx, sy, sx2, sy2;
        if (!project(t->x, y + h * 0.62f, t->z, &sx, &sy) || !project(t->x, y, t->z, &sx2, &sy2)) continue;
        const float e = fmaxf(2, fabsf(sy2 - sy) * 0.09f), r = fmaxf(1.5f, e * 0.5f);
        const uint32_t ca = E.w.veh_service[t->perso] == 1 ? 0xFF2236 : 0x2A6BFF, cb = 0x2A6BFF;
        const float ax = sx - e, bx = sx + e;
        ui_circle(ph ? ax : bx, sy, r * 4, ui_rgb(ph ? ca : cb), 70);
        ui_circle(ax, sy, r, ui_rgb(ph ? ca : 0x30303A), 255);
        ui_circle(bx, sy, r, ui_rgb(ph ? 0x30303A : cb), 255);
    }
}
static void draw_course_fx(void) {
    /* Mines (disque sombre, cœur rouge qui clignote), missiles (point magenta), boucliers (anneau cyan autour du véhicule) */
    const int clign = (int)(H.now * 4) & 1;
    float px, py, px2, py2;
    for (int m = 0; m < 12 && CR.on; m++) {
        if (!CR.mines[m].on || sous_pont(CR.mines[m].x, CR.mines[m].z, CR.mines[m].y) || !project(CR.mines[m].x, CR.mines[m].y, CR.mines[m].z, &px, &py) || !project(CR.mines[m].x, CR.mines[m].y + 0.32f, CR.mines[m].z, &px2, &py2)) continue;
        const float r0 = fminf(14, fabsf(py - py2));
        ui_circle(px, py, r0, ui_rgb(0x20232C), 255);
        ui_circle(px, py, r0 * 0.4f, ui_rgb(clign && CR.mines[m].t > 0.6f ? 0xFF3344 : 0x5A1010), 255);
    }
    for (int m = 0; m < 8 && CR.on; m++) {
        if (!CR.mis[m].on || sous_pont(CR.mis[m].x, CR.mis[m].z, CR.mis[m].y) || !project(CR.mis[m].x, CR.mis[m].y, CR.mis[m].z, &px, &py) || !project(CR.mis[m].x, CR.mis[m].y + 0.18f, CR.mis[m].z, &px2, &py2)) continue;
        const float r0 = fmaxf(3, fminf(9, fabsf(py - py2)));
        ui_circle(px, py, r0, ui_rgb(0xFF2D9B), 255);
        ui_circle(px, py, r0 * 0.45f, ui_rgb(0xFFFFFF), 255);
    }
    for (int k = 0; k < CR.n && CR.on; k++) {
        const racer_t *r = &CR.r[k];
        if (r->bouclier <= 0 || sous_pont(r->x, r->z, r->y) || !project(r->x, r->y + 0.45f, r->z, &px, &py) || !project(r->x, r->y + 1.2f, r->z, &px2, &py2)) continue;
        const float rr = fabsf(py - py2);
        ui_ellipse_ring(px, py, fminf(60, rr), fminf(40, rr * 0.66f), 3, ui_rgb(0x22E4FF), (int)(165 + 75 * sinf(H.now * 11)));
    }
    for (int k = 0; k < COURSE_FX; k++) {
        const course_fx_t *f = &CR.fx[k];
        if (f->t >= f->life) continue;
        const float tb = deck_above(f->x, f->z, f->y - 0.3f);
        if (tb == tb) continue;   /* sous un pont : caché par son tablier */
        const float a = 1 - f->t / f->life;
        float sx, sy, sx2, sy2;
        if (!project(f->x, f->y, f->z, &sx, &sy)) continue;
        if (f->kind >= 3) {   /* éclat brun, feuille verte : point de 0,09 case (1,5 à 5 px) */
            if (!project(f->x, f->y + 0.09f, f->z, &sx2, &sy2)) continue;
            ui_circle(sx, sy, fmaxf(1.5f, fminf(5, fabsf(sy - sy2))), ui_rgb(f->kind == 3 ? 0x6B5D50 : 0x4F8A3A), (int)(255 * fminf(1, a * 2)));
        } else if (f->kind != 1) {
            const float rw = 0.25f + 0.5f * f->t / f->life;
            if (!project(f->x, f->y + rw, f->z, &sx2, &sy2)) continue;
            ui_circle(sx, sy, fminf(26, fabsf(sy - sy2)), ui_rgb(f->kind ? 0xA8906C : 0xC8C8D0), (int)(110 * a));
        } else {
            if (!project(f->x - f->vx * 0.04f, f->y - f->vy * 0.04f, f->z - f->vz * 0.04f, &sx2, &sy2)) continue;
            ui_line(sx2, sy2, sx, sy, 2, ui_rgb(a > 0.5f ? 0xFFF2A0 : 0xFF9A30), (int)(255 * fminf(1, a * 1.5f)));
        }
    }
}

/* Avalanche (game.c : AVAL, aval_avance ; internal.h, « Avalanche » ; drawAvalanche de l'émulateur, à garder identiques) :
   bouffées de poudre dans le monde, du plus loin au plus proche, sur la bande de AVAL_QUEUE du trajet derrière le front
   (de plus en plus large en descendant, plus hautes, plus grosses et plus claires en arrière, opaques au front) ; chacune en
   trois disques : son ombre en bas à droite, son corps, un reflet en haut à gauche (une masse de neige qui roule) ; blocs de
   neige et cailloux qui rebondissent devant ; à l'arrivée, la poudre s'étale, monte et retombe en AVAL_APRES s, et un voile
   blanc couvre l'image (il monte dans les 0,3 dernières secondes du front, s'efface en AVAL_VOILE s) */
static float aval_h(int k, int s) {
    uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)s * 2246822519u;
    h ^= h >> 15; h *= 2654435761u; h ^= h >> 13;
    return (h & 0xFFFF) / 65536.0f;
}
static void draw_avalanche(void) {
    if (!AVAL.on || E.in.on || BT.on) return;
    const float t = AVAL.t, s = aval_avance(t), ta = t > AVAL.d ? t - AVAL.d : 0, fa = 1 - ta / AVAL_APRES;
    const float dx = AVAL.bx - AVAL.ax, dz = AVAL.bz - AVAL.az, L = sqrtf(dx * dx + dz * dz);
    if (L < 0.01f || fa <= 0) return;
    const float ux = dx / L, uz = dz / L, px = -uz, pz = ux;
    static const uint32_t TON[5] = { 0xF4F5F6, 0xDADDE2, 0xB4BAC4, 0x8E95A0, 0x4A4642 };   /* neige claire, ombres bleutées, cendre ; caillou */
    typedef struct { float sx, sy, r, z; uint8_t c, a, bloc; } bouffee_t;
    bouffee_t b[AVAL_BOUFFEES + AVAL_BLOCS];
    int n = 0;
    for (int k = 0; k < AVAL_BOUFFEES; k++) {
        const float q = aval_h(k, 1), h2 = aval_h(k, 2), h3 = aval_h(k, 3), h4 = aval_h(k, 4);
        const float pos = s - q * AVAL_QUEUE;   /* place sur le trajet (0 en haut, 1 à l'arrivée) */
        if (pos < 0) continue;
        const float lat = (h2 - 0.5f) * AVAL.w * (0.45f + 0.55f * pos) * (1 + 0.7f * ta) + 0.25f * sinf(t * 1.7f + k);
        const float x = AVAL.ax + ux * pos * L + px * lat + ux * 0.8f * ta * (0.5f + h4), z = AVAL.az + uz * pos * L + pz * lat + uz * 0.8f * ta * (0.5f + h4);
        const float r = (0.3f + 0.7f * q + 0.3f * h4) * (1 + 0.5f * ta), y = height_at(x, z) + r * 0.6f + (0.15f + 1.1f * q + 0.4f * h3) * (0.4f + 0.6f * pos) + 0.5f * ta * h3;
        float sx, sy, sx2, sy2;
        if (!project(x, y, z, &sx, &sy) || !project(x, y + r, z, &sx2, &sy2)) continue;
        const float rp = fabsf(sy - sy2);
        if (rp < 1) continue;
        const float entree = pos < 0.06f ? pos / 0.06f : 1;   /* une bouffée naît en haut sans paraître d'un coup */
        b[n].sx = sx; b[n].sy = sy; b[n].r = rp < AVAL_R_MAX ? rp : AVAL_R_MAX;
        b[n].z = (x - E.cam.x) * E.cam.sy + (z - E.cam.z) * E.cam.cy;
        const int ci = (int)(h3 * 1.5f + q * 1.6f);
        b[n].c = (uint8_t)(ci > 2 ? 2 : ci); b[n].bloc = 0;
        b[n].a = (uint8_t)((255 - 150 * q) * fa * entree);
        n++;
    }
    for (int k = 0; k < AVAL_BLOCS && ta < 0.4f; k++) {   /* blocs et cailloux au front, qui rebondissent */
        const float h1 = aval_h(k, 5), h2 = aval_h(k, 6), h3 = aval_h(k, 7);
        const float pos = s - 0.07f * h1;
        if (pos < 0.02f) continue;
        const float lat = (h2 - 0.5f) * AVAL.w * (0.4f + 0.5f * pos);
        const float x = AVAL.ax + ux * pos * L + px * lat, z = AVAL.az + uz * pos * L + pz * lat, r = 0.1f + 0.16f * h3;
        const float y = height_at(x, z) + r + fabsf(sinf(t * (5 + 3 * h1) + 7 * h2)) * 0.5f;
        float sx, sy, sx2, sy2;
        if (!project(x, y, z, &sx, &sy) || !project(x, y + r, z, &sx2, &sy2)) continue;
        const float rp = fabsf(sy - sy2);
        b[n].sx = sx; b[n].sy = sy; b[n].r = rp < 1.5f ? 1.5f : rp > 9 ? 9 : rp;
        b[n].z = (x - E.cam.x) * E.cam.sy + (z - E.cam.z) * E.cam.cy;
        b[n].c = h3 < 0.35f ? 4 : 1; b[n].bloc = 1;
        b[n].a = (uint8_t)(255 * (1 - ta / 0.4f));
        n++;
    }
    /* Du plus loin au plus proche (tri par insertion : quelques dizaines) */
    for (int i = 1; i < n; i++) { const int j0 = i; for (int j = j0; j > 0 && b[j - 1].z < b[j].z; j--) { const bouffee_t e = b[j]; b[j] = b[j - 1]; b[j - 1] = e; } }
    for (int i = 0; i < n; i++) {
        const bouffee_t *o = &b[i];
        if (o->bloc) { ui_circle(o->sx, o->sy, o->r, ui_rgb(TON[o->c]), o->a); continue; }
        ui_circle(o->sx + 0.12f * o->r, o->sy + 0.22f * o->r, o->r, ui_rgb(TON[o->c + 1]), o->a);           /* ombre */
        ui_circle(o->sx, o->sy, 0.9f * o->r, ui_rgb(TON[o->c]), o->a);                                     /* corps */
        ui_circle(o->sx - 0.22f * o->r, o->sy - 0.28f * o->r, 0.5f * o->r, ui_rgb(0xFBFBFC), o->a * 7 / 8); /* reflet */
    }
    /* Voile blanc de l'arrivée */
    const float va = t < AVAL.d - 0.3f ? 0 : t < AVAL.d ? (t - (AVAL.d - 0.3f)) / 0.3f : ta < AVAL_VOILE ? 1 - ta / AVAL_VOILE : 0;
    if (va > 0) voile_ecran(ui_rgb(0xEEF1F4), (int)(215 * va));
}

/* Éclair (eclair_age de game.c, 01/11/2026) : seulement un voile de lumière blanche en deux coups (fort de 0 à 0,06 s,
   moins fort de 0,10 à 0,16 s, puis il s'éteint jusqu'à 0,5 s), sans trait de foudre (jugé bizarre par l'utilisateur),
   sous le reste de l'interface (drawEclair de l'émulateur) */
static void draw_eclair(void) {
    const float t = eclair_age();
    if (t < 0 || t > 0.5f || BT.on || E.in.on) return;
    const float a = t < 0.06f ? 0.55f : t < 0.10f ? 0.12f : t < 0.16f ? 0.40f : 0.40f * (0.5f - t) / 0.34f;
    voile_ecran(ui_rgb(0xE8EEFF), (int)(255 * a));
}

/* Scène en cours (01/11/2026, demande de l'utilisateur) : script du scénario (sauf une fuite, que le joueur mène), dialogue
   ou choix, caméra ou marche de scène, arrestation : ni guidage, ni bulles, ni jauges, ni boussole ; restent les effets du
   monde, les dialogues et les fondus (sceneUi de l'émulateur) */
static int hud_scene(void) {
    /* Une course n'est pas une scène (02/11/2026). Une course de l'histoire (instructions « course ») laisse le script en
       cours, qui attend son résultat : sans cette exception, ni son menu ni son interface n'étaient dessinés (ni leurs
       zones tactiles) ; d'abord sur le téléphone, puis aussi sur la montre et dans l'émulateur (sceneUi) */
    if (CR.on || RM.on) return 0;
    return !carriere_on() && ((script_running() && !MISE.fuite_on) || H.dialog || CHX.on || mise_bloque());   /* (mode course : l'histoire est arrêtée, son script ne cache rien) */
}
int scene_en_cours(void) { return hud_scene(); }   /* calme du trafic pendant une scène (traffic_update de game.c) */
/* Éclair de couleur d'une boîte de nuit (club_age de game.c) : voile magenta, cyan, violet ou blanc, fort puis qui
   s'éteint en 0,18 s, sous le reste de l'interface (drawClub de l'émulateur) */
static void draw_club(void) {
    static const uint32_t C[4] = { 0xFF2BD6, 0x2BE4FF, 0x9B4DFF, 0xF4F0FF };
    int c;
    const float t = club_age(&c);
    if (t < 0 || t > 0.18f || BT.on) return;
    voile_ecran(ui_rgb(C[c & 3]), (int)(255 * 0.42f * (1 - t / 0.18f)));
}
/* Marques des personnages (panneaux.c) : badge « ! » (il ouvre une quête) ou « ? » (l'étape en cours le vise) au-dessus
   de la tête des exemplaires présents à moins de MARQUE_R cases (dans un intérieur : tous les siens), les MARQUE_VUS plus
   proches ; l'interlocuteur dont la bulle « parler » est montrée : dans le coin de sa bulle (drawMarques de l'émulateur) */
#define MARQUE_R 22.0f
#define MARQUE_VUS 12
#define MARQUE_HAUT 1.05f   /* part de la hauteur du sprite où se pose le badge (son pied 16 px plus haut) */
static struct { int n; int16_t npc[MARQUE_VUS]; float x[MARQUE_VUS], y[MARQUE_VUS]; } MV;   /* badges dessinés à la dernière image (banc d'essai) */
int hud_marques_vues(char *out, int n) {
    int o = 0;
    o += snprintf(out, (size_t)n, "%d badge%s à l'écran", MV.n, MV.n > 1 ? "s" : "");
    for (int i = 0; i < MV.n && o < n - 64; i++) o += snprintf(out + o, (size_t)(n - o), "\n  %s (exemplaire %d) en %.0f %.0f", E.w.persos[E.npcs[MV.npc[i]].perso].nom, MV.npc[i], MV.x[i], MV.y[i]);
    return o;
}
static void draw_marques(void) {
    MV.n = 0;
    if (BT.on || CR.on || RM.on || H.menu || H.dialog) return;
    int ks[MARQUE_VUS], nk = 0;
    float ds[MARQUE_VUS];
    const int i0 = E.in.on ? E.in.npc0 : 0, i1 = E.in.on ? E.in.npc0 + E.in.npcn : E.nnpc;
    const float R = E.in.on ? 1e9f : MARQUE_R / E.w.ech;
    for (int i = i0; i < i1 && i < E.nnpc; i++) {
        const npc_t *pn = &E.npcs[i];
        if (!marque_de(pn->perso) || !npc_present(pn) || acteur_de(pn->perso) >= 0) continue;
        const float d = hypotf(pn->x - E.sera.x, pn->z - E.sera.z);
        if (d > R) continue;
        int j;
        if (nk < MARQUE_VUS) j = nk++;
        else if (d < ds[MARQUE_VUS - 1]) j = MARQUE_VUS - 1;
        else continue;
        while (j > 0 && ds[j - 1] > d) { ks[j] = ks[j - 1]; ds[j] = ds[j - 1]; j--; }
        ks[j] = i; ds[j] = d;
    }
    const int bulle = near_npc() && E.scr_npc.vis && !(E.ride >= 0 && (fabsf(E.ride_v) > 0.3f || E.ride_phase));
    /* la personne visée par la quête suivie ou le guidage d'un panneau, à moins de 6 cases : son repère de près (draw_cible)
       la montre déjà, pas de badge par-dessus */
    float ox = 0, oz = 0, gx = 0, gz = 0;
    const int ob = objective_pos(&ox, &oz), gp = panneau_guide(&gx, &gz);
    for (int a = 0; a < nk; a++) {
        const npc_t *pn = &E.npcs[ks[a]];
        const int m = marque_de(pn->perso);
        if (ds[a] < 6 && ((ob && hypotf(pn->x - ox, pn->z - oz) < 0.3f) || (gp && hypotf(pn->x - gx, pn->z - gz) < 0.3f))) continue;
        if (ks[a] == E.talk && bulle) { marque_dessin(E.scr_npc.cx + 34, bulle_y(E.scr_npc.top) - 30, m, 0.8f); MV.npc[MV.n] = (int16_t)ks[a]; MV.x[MV.n] = E.scr_npc.cx + 34; MV.y[MV.n++] = bulle_y(E.scr_npc.top) - 30; continue; }
        const perso_t *pe = &E.w.persos[pn->perso];
        const float h = 2.0f / E.w.ech * E.w.sprite_scale * (pe->taille > 0 ? pe->taille : 1);
        float sx, sy;
        if (!project(pn->x, ground_y(pn->x, pn->z) + h * MARQUE_HAUT, pn->z, &sx, &sy)) continue;
        sy -= 16;   /* le badge, au-dessus de la tête */
        if (PORTRAIT ? (sx < 0 || sx > UW || sy < 0 || sy > UH) : hypotf(sx - CX, sy - CY) > 200) continue;
        marque_dessin(sx, sy, m, 1);
        MV.npc[MV.n] = (int16_t)ks[a]; MV.x[MV.n] = sx; MV.y[MV.n++] = sy;
    }
}
#if ENG_RECT
/* Écran rectangulaire (RetroFW, 11/11/2026, demande de l'utilisateur : « revoir l'UI, notamment le cadran de montre, on ne
   l'a pas dans la version Android ») : plus de graduations du pourtour, de boussole ni de jauges sur le cercle ; comme le
   haut de portrait_world du téléphone, aux bords de toute l'image (x de -ui_ox à ui_img_w - ui_ox) : lieu et heure en
   haut à gauche, boussole en bande au milieu, quête suivie à droite ; Sera, son niveau et ses jauges en bas à gauche.
   Le reste de l'interface paysage suivra la maquette maquettes/interface_paysage.html, une fois validée */
static void rect_world(void) {
    const float x0 = (float)-ui_ox + 20, x1 = (float)(ui_img_w - ui_ox) - 20, yb = (float)(ui_img_h - ui_oy);
    const uint16_t ink = ui_rgb(INK);
    char t[96];
    /* Pas de bandeau semi-transparent (sur la LDK, un mélange par pixel : 80 000 pixels par image pour les deux fonds, une
       dizaine de ms) : textes ombrés */
    #define OMBRE(f, x, y, al, col, s) do { ui_text(f, (x) + 2, (y) + 2, al, ink, 200, s); ui_text(f, x, y, al, col, 255, s); } while (0)
    if (E.in.on && E.in.etage) snprintf(t, sizeof t, "ÉTAGE %d", E.in.etage);
    else snprintf(t, sizeof t, "%s", E.in.on ? "INTÉRIEUR" : E.w.carte ? E.w.cartes[E.w.carte].nom : E.w.ile[0] ? E.w.ile : "SECTEUR 6");
    OMBRE(ui_fit(FONT_DISP, t, 230, 28, 20), x0, 36, UI_LEFT, ui_rgb(WHITE), t);
    const unsigned hr = (unsigned)(E.horloge / 3600) % 24u, mn = (unsigned)(E.horloge / 60) % 60u;
    if (E.horloge >= 0) snprintf(t, sizeof t, "%.1f · %.1f · %02u:%02u", E.sera.x, E.sera.z, hr, mn);
    else snprintf(t, sizeof t, "%.1f · %.1f", E.sera.x, E.sera.z);
    OMBRE(ui_font(FONT_MONO, 20), x0, 64, UI_LEFT, ui_rgb(0xB8C6D6), t);
    /* Boussole en bande : les points cardinaux défilent avec la caméra, le nord en teinte 2 (portrait_world) */
    const float bx = CX;
    ui_line(bx - 96, 58, bx + 96, 58, 2, C1, 90);
    static const char *const CARD[4] = { "N", "E", "S", "O" };
    for (int k = 0; k < 4; k++) {
        float a = (k == 0 ? PI_F : k == 2 ? 0 : k == 1 ? 3 * PI_F / 2 : PI_F / 2) - E.yaw;   /* nord vers -z, sud vers +z, est vers -x (CARTE_X de carte.c) */
        a = atan2f(sinf(a), cosf(a));
        if (fabsf(a) > 1.3f) continue;
        ui_text(ui_font(FONT_DISP, k ? 22 : 26), bx + a / (PI_F / 2) * 76, 46, UI_CENTER, k ? ui_rgb(DIM) : C2, (int)(255 * (1 - fabsf(a) / 1.6f)), CARD[k]);
    }
    ui_line(bx, 52, bx, 64, 2, C1, 255);
    /* Quête suivie et distance de son objectif, à droite */
    if (E.qtrack >= 0 && E.qtrack < script_quest_count() && !H.dialog) {
        const int q = E.qtrack, st = E.quests[q] >= 1 && E.quests[q] < 255 ? E.quests[q] - 1 : 0;
        const char *txt = script_quest_text(q, st);
        snprintf(t, sizeof t, "%s", script_quest_name(q));
        OMBRE(ui_fit(FONT_DISP, t, 220, 22, 18), x1, 34, UI_RIGHT, C2, t);
        float ox, oz;
        const int dm = objective_pos(&ox, &oz) ? (int)hypotf(ox - E.sera.x, oz - E.sera.z) : -1;
        if (dm >= 0 && txt[0]) snprintf(t, sizeof t, "%s · %d m", txt, dm);
        else if (dm >= 0) snprintf(t, sizeof t, "%d m", dm);
        else snprintf(t, sizeof t, "%s", txt);
        OMBRE(ui_fit(FONT_MONO, t, 230, 20, 16), x1, 62, UI_RIGHT, ui_rgb(WHITE), t);
    }
    if (H.dialog || CR.on || RM.on) return;
    /* Sera (ou l'héroïne choisie) : portrait, nom, niveau, PV et PM, en bas à gauche */
    const float pr = 30, py = yb - 24 - pr, jw = 200;
    ui_circle(x0 + pr, py, pr, ui_rgb(0x080612), 255);
    ui_image(portrait_px[0], portrait_mask[0], 24, 24, x0, py - pr + pr / 9, pr * 2 / 24, x0 + pr, py, pr - 1);
    ui_ring(x0 + pr, py, pr, 3, C2, 255);
    const float tx = x0 + 2 * pr + 14;
    snprintf(t, sizeof t, "%s  NIV. %d", PARTY[0].name, E.level);
    OMBRE(ui_fit(FONT_DISP, t, jw + 40, 22, 18), tx, py - 12, UI_LEFT, ui_rgb(WHITE), t);
    const float fh = PARTY[0].max > 0 ? fminf(1, (float)PARTY[0].hp / PARTY[0].max) : 0, fm = PARTY[0].maxmp > 0 ? fminf(1, (float)PARTY[0].mp / PARTY[0].maxmp) : 0;
    ui_rect(tx, py + 2, jw, 12, ui_rgb(WHITE), 26); if (fh > 0) ui_rect(tx, py + 2, jw * fh, 12, C2, 255);
    ui_rect(tx, py + 20, jw, 8, ui_rgb(WHITE), 26); if (fm > 0) ui_rect(tx, py + 20, jw * fm, 8, C1, 255);
    const float tc = E.clock - E.cendre_coup;   /* coup de la pluie de cendre : la jauge des PV pulse en gris cendre */
    if (E.cendre_coup > 0 && tc >= 0 && tc < 0.6f && fh > 0) ui_rect(tx, py + 2, jw * fh, 12, ui_rgb(0xC8C4BC), (int)(200 * (1 - tc / 0.6f)));
    #undef OMBRE
}
#endif
static void draw_world_hud(void) {
    draw_eclair();
    draw_club();
    draw_course_fx();   /* hors course : éclats, feuilles et poussière des décors cassés */
    draw_avalanche();   /* avalanche du scénario : poudre, blocs, voile blanc de l'arrivée */
    draw_gyrophares();
    if (hud_scene()) {
        if (eveil_hors_combat() && E.scr_sera.vis && E.ride < 0) aura(&E.scr_sera, eveil_aura(0), E.clock * 4);
        return;
    }
#if ENG_DYN
    if (!CR.on && !RM.on) portrait_world();   /* en course et au menu de course : rien de l'exploration (portrait_course) */
    if (0) {
#elif ENG_RECT
    if (!CR.on && !RM.on) rect_world();   /* écran rectangulaire : pas de cadran de montre */
    if (0) {
#endif
    rim_ticks(E.yaw, 115);
    if (H.rim_on && !H.menu && H.sub < 0) {
        ui_arc(CX, CY, 224, H.rim_a - 0.35f - PI_F / 2, H.rim_a + 0.35f - PI_F / 2, 12, C1, 60);
        ui_arc(CX, CY, 224, H.rim_a - 0.35f - PI_F / 2, H.rim_a + 0.35f - PI_F / 2, 6, C1, 255);
    }
    /* Boussole : le nord vers -z, en haut de la carte (CARTE, cartes de l'Atelier ; il visait +z, le sud) */
    float na = PI_F - E.yaw, bx = CX + sinf(na) * 208, by = CY - cosf(na) * 208, s = sinf(na), c = cosf(na);
    float tri[6] = { bx + 8 * s, by - 8 * c, bx + 6 * c - 4 * s, by + 6 * s + 4 * c, bx - 6 * c - 4 * s, by - 6 * s + 4 * c };
    ui_poly(tri, 3, C2, 255);
    ui_text(ui_font(FONT_DISP, 20), bx - 24 * s, by + 24 * c + 7, UI_CENTER, C2, 255, "N");
    if (!H.dialog && !CR.on && !RM.on) {
        char t[48];
        /* PV et PM de Sera : jauges sans chiffres (les valeurs sont dans les menus et en combat), remplies du bas */
        const float fh = PARTY[0].max > 0 ? fminf(1, (float)PARTY[0].hp / PARTY[0].max) : 0, fm = PARTY[0].maxmp > 0 ? fminf(1, (float)PARTY[0].mp / PARTY[0].maxmp) : 0;
        ui_arc(CX, CY, 205, 2.06f, 2.96f, 4, C2, 60);
        ui_arc(CX, CY, 197, 2.13f, 2.85f, 3, C1, 60);
        if (fh > 0) ui_arc(CX, CY, 205, 2.06f, 2.06f + 0.90f * fh, 4, C2, 255);
        if (fm > 0) ui_arc(CX, CY, 197, 2.13f, 2.13f + 0.72f * fm, 3, C1, 255);
        /* Coup de la pluie de cendre (cendre_update) : la jauge des PV pulse en gris cendre 0,6 s */
        const float tc = E.clock - E.cendre_coup;
        if (E.cendre_coup > 0 && tc >= 0 && tc < 0.6f && fh > 0) ui_arc(CX, CY, 205, 2.06f, 2.06f + 0.90f * fh, 8, ui_rgb(0xC8C4BC), (int)(200 * (1 - tc / 0.6f)));
        /* Coordonnées sur l'arc du bas, en deçà des graduations du bord, décalées à droite des jauges (place pour
           « SEC-06 // 255.5 255.5 »), avec une ombre pour le sol clair */
        /* Heure réelle connue (montre : eng_set_horloge) : à la place des coordonnées */
        const unsigned hr = (unsigned)(E.horloge / 3600) % 24u, mn = (unsigned)(E.horloge / 60) % 60u;
        if (E.horloge >= 0) {
            if (E.in.on && E.in.etage) snprintf(t, sizeof t, "ÉTAGE %d // %02u:%02u", E.in.etage, hr, mn);
            else if (E.in.on) snprintf(t, sizeof t, "INTÉRIEUR // %02u:%02u", hr, mn);
            else if (!E.w.carte && E.w.ile[0]) snprintf(t, sizeof t, "%.24s // %02u:%02u", E.w.ile, hr, mn);   /* île : son nom entier (UTF-8) */
            else snprintf(t, sizeof t, "%.12s // %02u:%02u", E.w.carte ? E.w.cartes[E.w.carte].nom : "SEC-06", hr, mn);
        }
        else if (E.in.on && E.in.etage) snprintf(t, sizeof t, "ÉTAGE %d", E.in.etage); else if (E.in.on) snprintf(t, sizeof t, "INTÉRIEUR"); else if (E.w.carte) snprintf(t, sizeof t, "%.12s // %.1f %.1f", E.w.cartes[E.w.carte].nom, E.sera.x, E.sera.z); else snprintf(t, sizeof t, "%.24s // %.1f %.1f", E.w.ile[0] ? E.w.ile : "SEC-06", E.sera.x, E.sera.z);
        ui_text_arc(ui_font(FONT_MONO, 20), CX + 1, CY + 1, 214, PI_F / 2 - 0.12f, 1, ui_rgb(0x05080E), 170, t);
        ui_text_arc(ui_font(FONT_MONO, 20), CX, CY, 214, PI_F / 2 - 0.12f, 1, ui_rgb(0x7F93A8), 255, t);
    }
#if ENG_DYN || ENG_RECT
    }
#endif
    draw_marques();   /* « ! » et « ? » au-dessus des personnages (panneaux.c), sous les bulles */
    /* Destination */
    if (E.has_dest) {
        float sx, sy;
        if (project(E.dest_x, height_at(E.dest_x, E.dest_z), E.dest_z, &sx, &sy)) {
            float p = 1 + 0.15f * sinf(H.now * 1000 / 160);
            ui_ellipse_ring(sx, sy, 22 * p, 8 * p, 2, C1, 255);
            float d[8] = { sx, sy - 7, sx + 5, sy, sx, sy + 7, sx - 5, sy };
            ui_poly(d, 4, C1, 255);
        }
    }
    if (H.now - H.fx_t < 0.4f) { float k = (H.now - H.fx_t) / 0.4f; ui_ring(H.fx_x, H.fx_y, 10 + k * 30, 2, C1, (int)(255 * (1 - k))); }
    /* Bulle d'interaction */
    /* Rails de conduite (gaz à gauche, volant à droite : à leur place au repos, sous le doigt quand il est posé ; cachés
       quand la manette est connectée), ou joystick (SYSTÈME › COMMANDES), et boutons d'altitude (vaisseau) */
    if (conduite_doigts()) {
        if (joy_on() && !E.manette) {   /* joystick : socle, repères des quatre directions, poignée sous le doigt */
            const doigt_t *d = DG[0].on && DG[0].role == 4 ? &DG[0] : DG[1].on && DG[1].role == 4 ? &DG[1] : NULL;
            float jx = d ? (d->x - JOY_X) / JOY_R : 0, jy = d ? (d->y - JOY_Y) / JOY_R : 0;
            const float l = hypotf(jx, jy);
            if (l > 1) { jx /= l; jy /= l; }
            ui_circle(JOY_X, JOY_Y, JOY_R, ui_rgb(0x060E1A), d ? 150 : 110);
            ui_ring(JOY_X, JOY_Y, JOY_R, 2, C1, d ? 230 : 130);
            for (int k = 0; k < 4; k++) {
                const float an = k * PI_F / 2;
                ui_circle(JOY_X + sinf(an) * (JOY_R - 9), JOY_Y - cosf(an) * (JOY_R - 9), 2, C1, 150);
            }
            ui_circle(JOY_X + jx * JOY_R * 0.7f, JOY_Y + jy * JOY_R * 0.7f, 17, C1, d ? 210 : 110);
        }
        for (int r = 0; r < 2 && !E.manette && !joy_on() && !pointer_on(); r++) {
            const doigt_t *d = DG[0].on && DG[0].role == r ? &DG[0] : DG[1].on && DG[1].role == r ? &DG[1] : NULL;
            const float cx = d ? d->x0 : r ? VOL_X : GAZ_X, cy = d ? d->y0 : r ? VOL_Y : GAZ_Y, L = r ? DRV_VOL : DRV_GAZ, e = 20;
            /* hexagone allongé, pointes aux deux bouts : vertical pour le gaz, horizontal pour le volant */
            float q[12];
            for (int k = 0; k < 6; k++) {
                static const float A[6] = { 0, 1, 1, 0, -1, -1 }, B[6] = { -1, -1, 1, 1, 1, -1 }, P[6] = { 1, 0, 0, 1, 0, 0 };
                const float u = A[k] * e, v = B[k] * (L + e - (P[k] ? 0 : e));   /* u : en travers, v : le long du rail */
                q[k * 2] = cx + (r ? v : u); q[k * 2 + 1] = cy + (r ? u : v);
            }
            ui_poly(q, 6, ui_rgb(0x060E1A), d ? 150 : 100);
            ui_outline(q, 6, 2, C1, d ? 230 : 120);
            for (int k = -1; k <= 1; k += 2) {   /* flèches aux deux bouts */
                const float t = L - 4, w = 6;
                const float tr[6] = { cx + (r ? k * (t + w) : -w), cy + (r ? -w : k * (t + w)), cx + (r ? k * (t + w) : w), cy + (r ? w : k * (t + w)),
                                      cx + (r ? k * (t + 2 * w + 2) : 0), cy + (r ? 0 : k * (t + 2 * w + 2)) };
                ui_poly(tr, 3, C1, d ? 220 : 130);
            }
            const float o = d ? fmaxf(-L, fminf(L, r ? d->x - d->x0 : d->y - d->y0)) * 0.8f : 0;
            ui_circle(cx + (r ? o : 0), cy + (r ? 0 : o), 16, C1, d ? 210 : 100);
        }
        /* POINTER : le point visé au sol (pilote.c), anneau qui pulse et point au centre */
        float vx, vz, mx, my, mx2, my2;
        if (pointer_on() && pilote_doigt_cible(&vx, &vz)) {
            const float gy = ground_y(vx, vz);
            if (project(vx, gy, vz, &mx, &my) && project(vx, gy + 0.4f, vz, &mx2, &my2)) {
                const float rr = fminf(40, fmaxf(7, fabsf(my - my2)));
                ui_ellipse_ring(mx, my, rr, rr * 0.5f, 2, C1, (int)(170 + 60 * sinf(H.now * 8)));
                ui_circle(mx, my, 3, C1, 220);
            }
        }
        if (alt_visible()) {
            for (int k = 0; k < 2; k++) {
                const float ay = k ? ALT_DN_Y : ALT_UP_Y, sg = k ? 1 : -1;
                const int on = E.drive_alt == (k ? -1 : 1);
                ui_circle(ALT_X, ay, ALT_R, ui_rgb(0x060E1A), 170);
                ui_ring(ALT_X, ay, ALT_R, 2, C1, on ? 255 : 150);
                const float tr[6] = { ALT_X - 9, ay - sg * 5, ALT_X + 9, ay - sg * 5, ALT_X, ay + sg * 8 };
                ui_poly(tr, 3, C1, on ? 255 : 190);
            }
            char a[16]; snprintf(a, sizeof a, "ALT %.1f", E.ride_alt);
            ui_text(ui_fit(FONT_MONO, a, 90, 16, 14), ALT_X, ALT_DN_Y + ALT_R + 16, UI_CENTER, C1, 200, a);
        }
    }
    /* Sera transformée hors combat (mode d'essai, SYSTÈME › ÉVEIL) : son halo */
    if (eveil_hors_combat() && E.scr_sera.vis && E.ride < 0) aura(&E.scr_sera, eveil_aura(0), E.clock * 4);
    /* Bulle d'interaction (masquée en roulant, au décollage et à l'atterrissage : on descend à l'arrêt) */
    if (RM.on) draw_course_menu();   /* (plein écran : refaits pour le portrait, sans le cadre de 466 centré) */
    else if (CR.on) { draw_course(); if (CR.pause && accueil_regl_on()) { nbtn = 0; accueil_regl_build(); } else if (CR.pause) draw_course_pause(); }   /* (RÉGLAGES de la pause : la page d'accueil.c) */
    if (RM.on || CR.on) {}
    else if (near_npc() && E.scr_npc.vis && !H.dialog && !H.menu && !(E.ride >= 0 && (fabsf(E.ride_v) > 0.3f || E.ride_phase))) {
        float x = E.scr_npc.cx, y = bulle_y(E.scr_npc.top);
        if (E.scr_npc.top > y + 36) ui_line(x, y + 36, x, E.scr_npc.top, 1, C1, 153);
        float q[12]; ui_hex_path(x, y, 76, 66, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 235);
        glow_outline(q, 6, 2, C1);
        ui_icon(E.ride >= 0 ? "flee" : talk_is_vehicle() ? "drive" : "talk", x, y, 26, C1, 255);
        button(x, y, 38, A_TALK, 0);
        /* Nom du personnage ou du véhicule au-dessus de la bulle (comme celui d'un bâtiment sur celle de sa porte) ; plus
           haut quand la bulle d'une porte est à côté (son nom est à cette hauteur) */
        const int pk = E.ride >= 0 ? -1 : E.talk >= 0 ? E.npcs[E.talk].perso : E.talk <= TALK_TRAF(0) ? E.traffic[TALK_TRAF(0) - E.talk].perso : -1;
        const char *nom = E.ride < 0 && E.talk >= 0 ? pnj_nom(E.talk) : pk >= 0 && pk < E.w.npersos ? E.w.persos[pk].nom : NULL;   /* un exemplaire : son nom (tiré) */
        if (nom && nom[0]) {
            const int cote = E.porte >= 0 && E.scr_sera.vis && fabsf(E.scr_npc.cx - E.scr_sera.cx) < 84 && fabsf(E.scr_npc.top - E.scr_sera.top) < 70;
            ui_text(ui_fit(FONT_MONO, nom, 200, 18, 13), x, y - (cote ? 64 : 42), UI_CENTER, C1, 255, nom);
        }
        if (course_ok()) {   /* près de la ligne de départ : drapeau, lancer une course */
            const float fx = x + 84;
            ui_hex_path(fx, y, 76, 66, q);
            ui_poly(q, 6, ui_rgb(0x060E1A), 235);
            glow_outline(q, 6, 2, C2);
            ui_icon("flag", fx, y, 26, C2, 255);
            button(fx, y, 38, A_COURSE, 0);
        }
    }
    /* Devant une porte : entrer (porte ouverte) ou forcer (cadenas), au-dessus de Sera ; à côté de la bulle d'un
       personnage ou d'un véhicule proche s'il y en a une (elle ne la cache plus) */
    if (E.porte >= 0 && E.scr_sera.vis && !H.dialog && !H.menu && !CHX.on && !RM.on && !CR.on) {
        const int ok = int_porte_ouverte(E.porte);
        const uint16_t col = ok ? C1 : C2;
        const int pres = near_npc() && E.scr_npc.vis && fabsf(E.scr_npc.cx - E.scr_sera.cx) < 84 && fabsf(E.scr_npc.top - E.scr_sera.top) < 70;
        const float x = E.scr_sera.cx + (pres ? (E.scr_npc.cx > E.scr_sera.cx ? -84 : 84) : 0), y = E.scr_sera.top - 50;
        float q[12]; ui_hex_path(x, y, 76, 66, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 235);
        glow_outline(q, 6, 2, col);
        ui_icon(ok ? "door" : "lock", x, y, 26, col, 255);
        button(x, y, 38, A_PORTE, 0);
        const char *nom = int_porte_nom(E.porte);   /* le nom du bâtiment au-dessus de la bulle */
        if (nom[0]) ui_text(ui_fit(FONT_MONO, nom, 200, 18, 13), x, y - 42, UI_CENTER, col, 255, nom);
    }
    /* Devant un coffre (coffres.c) : le fouiller ; bulle au-dessus du coffre (à côté de celles d'un personnage ou d'une
       porte), son nom au-dessus (bulleCoffre de l'émulateur) */
    float kx, ky, kz, sx, sy;
    if (E.coffre >= 0 && E.ride < 0 && !BUT.on && !H.dialog && !H.menu && !CHX.on && !RM.on && !CR.on && coffre_position(&kx, &ky, &kz)) {
        if (!project(kx, ky, kz, &sx, &sy)) { sx = E.scr_sera.cx; sy = E.scr_sera.top; }
        float x = sx, y = sy - 46;
        if (near_npc() && E.scr_npc.vis && fabsf(E.scr_npc.cx - x) < 84 && fabsf(bulle_y(E.scr_npc.top) - y) < 70) x = E.scr_npc.cx + (x >= E.scr_npc.cx ? 84 : -84);
        if (E.porte >= 0 && E.scr_sera.vis && fabsf(E.scr_sera.cx - x) < 84 && fabsf(E.scr_sera.top - 50 - y) < 70) x = E.scr_sera.cx + (x >= E.scr_sera.cx ? 84 : -84);
        float q[12]; ui_hex_path(x, y, 76, 66, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 235);
        glow_outline(q, 6, 2, C2);
        icone_coffre(x, y, C2, 255);
        button(x, y, 38, A_COFFRE, 0);
        const char *nom = coffre_nom();
        ui_text(ui_fit(FONT_MONO, nom, 200, 18, 13), x, y - 42, UI_CENTER, C2, 255, nom);
    }
    /* Devant un panneau de quêtes (panneaux.c) : le lire ; bulle LIRE au-dessus du panneau (à côté de celles d'un personnage
       ou d'une porte), son nom au-dessus, un « ! » dans son coin s'il propose une quête (bullePanneau de l'émulateur) */
    if (E.panneau >= 0 && E.ride < 0 && !BUT.on && !H.dialog && !H.menu && !CHX.on && !RM.on && !CR.on && panneau_position(&kx, &ky, &kz)) {
        if (!project(kx, ky, kz, &sx, &sy)) { sx = E.scr_sera.cx; sy = E.scr_sera.top; }
        float x = sx, y = sy - 46;
        if (near_npc() && E.scr_npc.vis && fabsf(E.scr_npc.cx - x) < 84 && fabsf(bulle_y(E.scr_npc.top) - y) < 70) x = E.scr_npc.cx + (x >= E.scr_npc.cx ? 84 : -84);
        if (E.porte >= 0 && E.scr_sera.vis && fabsf(E.scr_sera.cx - x) < 84 && fabsf(E.scr_sera.top - 50 - y) < 70) x = E.scr_sera.cx + (x >= E.scr_sera.cx ? 84 : -84);
        const uint16_t or_ = ui_rgb(MQ_RGB);
        float q[12]; ui_hex_path(x, y, 76, 66, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 235);
        glow_outline(q, 6, 2, or_);
        ui_text(ui_font(FONT_DISP, 20), x, y + 7, UI_CENTER, or_, 255, "LIRE");
        button(x, y, 38, A_PANNEAU, 0);
        const char *nom = panneau_nom(E.panneau);
        ui_text(ui_fit(FONT_MONO, nom, 200, 18, 13), x, y - 42, UI_CENTER, or_, 255, nom);
        if (panneau_quetes(E.panneau) > 0) marque_dessin(x + 34, y - 28, MQ_OUVRE, 0.8f);
    }
    /* Devant la couche d'un abri de l'île (ile.c, 12/11/2026 : la natte, le hamac, le feu, le pied de la Gardienne) : DORMIR, le nom de
       l'abri au-dessus (bulleDormir de l'émulateur de l'île) */
    if (ile_dormir_k() >= 0 && E.ride < 0 && !BUT.on && !H.dialog && !H.menu && !CHX.on && !RM.on && !CR.on && ile_dormir_position(&kx, &ky, &kz)) {
        if (!project(kx, ky, kz, &sx, &sy)) { sx = E.scr_sera.cx; sy = E.scr_sera.top; }
        float x = sx, y = sy - 46;
        if (near_npc() && E.scr_npc.vis && fabsf(E.scr_npc.cx - x) < 84 && fabsf(bulle_y(E.scr_npc.top) - y) < 70) x = E.scr_npc.cx + (x >= E.scr_npc.cx ? 84 : -84);
        if (E.porte >= 0 && E.scr_sera.vis && fabsf(E.scr_sera.cx - x) < 84 && fabsf(E.scr_sera.top - 50 - y) < 70) x = E.scr_sera.cx + (x >= E.scr_sera.cx ? 84 : -84);
        const uint16_t ca = ui_rgb(ABRI_RGB);
        float q[12]; ui_hex_path(x, y, 76, 66, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 235);
        glow_outline(q, 6, 2, ca);
        ui_text(ui_fit(FONT_DISP, "DORMIR", 64, 20, 14), x, y + 7, UI_CENTER, ca, 255, "DORMIR");
        button(x, y, 38, A_DORMIR, 0);
        const char *nom = ile_dormir_nom();
        ui_text(ui_fit(FONT_MONO, nom, 200, 18, 13), x, y - 42, UI_CENTER, ca, 255, nom);
    }
}
/* Alarme d'une effraction : halo rouge au bord de l'écran rond, qui clignote en fondu (1,2 Hz), et compte à rebours
   (drawAlarme de l'émulateur) */
static void draw_alarme(void) {
    const float k = 0.5f + 0.5f * sinf(E.alarme.t * PI_F * 2.4f), top = 0.2f + 0.5f * k;
    ui_halo(CX, CY, 150, 233, ui_rgb(0xFF1428), (int)(255 * top), 0);   /* une passe (ui_halo), plus quinze anneaux */
    int sec = (int)ceilf(E.alarme.duree - E.alarme.t); if (sec < 0) sec = 0;
    char t[32]; snprintf(t, sizeof t, "ALARME · 0:%02d", sec);
    ui_text(ui_font(FONT_DISP, 22), CX, 146, UI_CENTER, ui_rgb(0xFF3048), (int)(255 * (0.7f + 0.3f * k)), t);
}
/* Recherche (police.c) : « RECHERCHÉE » en haut de l'écran et un chevron par niveau (trois places), rouges, qui clignotent
   rouge et bleu quand un poursuivant voit Sera ; arc du haut de l'écran (plein écran : barre sous les chevrons) : le temps
   qu'il reste à tenir hors de vue pour semer la police, plein tant qu'elle est vue. Véhicule volé conduit sans recherche :
   « VÉHICULE VOLÉ » (drawRecherche de l'émulateur) */
/* Pilote automatique (pilote.c) : bandeau « PILOTE AUTO » et distance restante par le réseau, en haut de l'écran, à la place
   de la recherche (le pilote se coupe quand la police cherche Sera ; drawPilote de l'émulateur) */
static void draw_pilote(void) {
    char t[48];
    if (!pilote_bandeau(t, sizeof t)) return;
    const float x = PORTRAIT ? UW / 2 : CX, y = PORTRAIT ? 236 : 66;
    const ui_font_t *f = ui_font(FONT_MONO, 16);
    ui_rect(x - 118, y - 18, 236, 25, ui_rgb(0x05080E), 150);
    ui_line(x - 118, y + 7, x + 118, y + 7, 2, ui_rgb(GUIDE_RGB), 200);
    ui_text(f, x + 1, y + 1, UI_CENTER, ui_rgb(0x05080E), 170, t);
    ui_text(f, x, y, UI_CENTER, ui_rgb(GUIDE_RGB), 245, t);
}
#define RECH_ROUGE 0xFF2236
#define RECH_BLEU 0x2A6BFF
static void draw_recherche(void) {
    const float x = PORTRAIT ? UW / 2 : CX, y = PORTRAIT ? 236 : 66;
    const uint16_t rouge = ui_rgb(RECH_ROUGE), bleu = ui_rgb(RECH_BLEU), ombre = ui_rgb(0x05080E);
    if (!POL.niveau) {
        ui_text(ui_font(FONT_MONO, 16), x + 1, y + 1, UI_CENTER, ombre, 170, "VÉHICULE VOLÉ");
        ui_text(ui_font(FONT_MONO, 16), x, y, UI_CENTER, ui_rgb(0xFF5A6A), 230, "VÉHICULE VOLÉ");
        return;
    }
    const int ph = (int)(H.now * 6) & 1;
    const char *t = POL.clan ? "RECHERCHÉE · MILICE" : "RECHERCHÉE";
    const ui_font_t *f = ui_font(FONT_DISP, 20);
    ui_text(f, x + 1, y + 1, UI_CENTER, ombre, 170, t);
    ui_text(f, x, y, UI_CENTER, rouge, 255, t);
    for (int k = 0; k < 3; k++) {   /* chevron pointe en haut : deux bras de 11 × 6 px */
        const float cx = x + (k - 1) * 28, cy = y + 20;
        const float g[8] = { cx - 11, cy + 4, cx, cy - 7, cx, cy - 1, cx - 11, cy + 10 }, d[8] = { cx, cy - 7, cx + 11, cy + 4, cx + 11, cy + 10, cx, cy - 1 };
        const int on = k < POL.niveau;
        const uint16_t c = on && POL.vue && ((ph ^ k) & 1) ? bleu : rouge;
        ui_poly(g, 4, c, on ? 255 : 55); ui_poly(d, 4, c, on ? 255 : 55);
    }
    const float r = POL.vue ? 1 : 1 - fminf(1, POL.semer / police_semer_s());
    const uint16_t cj = POL.vue && ph ? bleu : rouge;
    if (PORTRAIT) {
        ui_rect(x - 100, y + 36, 200, 6, rouge, 50);
        if (r > 0) ui_rect(x - 100 * r, y + 36, 200 * r, 6, cj, 230);
    } else {
        ui_arc(CX, CY, 222, -PI_F / 2 - 0.42f, -PI_F / 2 + 0.42f, 6, rouge, 50);
        if (r > 0.01f) ui_arc(CX, CY, 222, -PI_F / 2 - 0.42f * r, -PI_F / 2 + 0.42f * r, 6, cj, 230);
    }
}

static void menu_detail(int k, char *t, int n) {
    switch (k) {
    case 0: snprintf(t, n, "%d dans le sac", items_count()); break;
    case 1: snprintf(t, n, "%d techniques", team_techs()); break;
    case 2: snprintf(t, n, "%s", E.eq[0][EQ_ARME] != EQ_AUCUN && E.eq[0][EQ_ARME] < n_items ? ITEMS[E.eq[0][EQ_ARME]].name : "Lame-fouet, combi"); break;
    case 3: snprintf(t, n, "Équipe de %d", band_count()); break;
    case 4: snprintf(t, n, "Secteur 6"); break;
    case 6: { int a = 0; for (int q = 0; q < script_quest_count(); q++) a += E.quests[q] >= 1 && E.quests[q] < 255; snprintf(t, n, "%d en cours", a); break; }
    case 7: snprintf(t, n, "%s", guide_ici() ? E.w.guides[E.guide].nom : guide_aide(1)); break;
    case 8: { int r = 0; for (int i = 0; i < NMEMB; i++) r += in_band(i) && !in_team(i);
        if (r) snprintf(t, n, "%d au combat · %d réserve", team_count(), r); else snprintf(t, n, "Au combat : %d / %d", team_count(), EQUIPE_MAX); break; }
    default: snprintf(t, n, "Teinte, sauvegarde"); break;
    }
}

static void backdrop(void) {
#if ENG_DYN
    voile_ecran(ui_rgb(INK), 222); return;   /* toute l'image (les sous-menus sont décalés en x) */
#endif
    voile_ecran(ui_rgb(INK), 222);   /* (écran rond : le cercle ; rectangulaire : toute l'image) */
    ui_circle(CX, CY, 150, ui_rgb(0x0D1428), 110);
    if (ENG_RECT) return;   /* écran rectangulaire : pas de pourtour de cadran */
    dashed_ring(226, 1.5f, 10.33f, 5, C1, 102);
    ui_ring(CX, CY, 215, 1, C1, 46);
}

static void draw_menu(void) {
#if ENG_DYN
    portrait_menu(); return;
#endif
    backdrop();
    dashed_ring(160, 4, 6, 1, C1, 56);
    /* Icônes à place fixe (13/11/2026, demande de l'utilisateur : l'anneau qui tournait pour amener l'entrée choisie en haut
       désorientait) : l'entrée k à l'angle k / NMENU du tour depuis le haut, dans le sens des aiguilles ; seule la sélection
       se déplace, avec son repère sur le pourtour (menu_angle ; drawMenu de l'émulateur) */
    const float as = menu_angle(H.sel);
    if (!ENG_RECT) ui_arc(CX, CY, 226, as - PI_F / 2 - 0.13f, as - PI_F / 2 + 0.13f, 5, C2, 255);   /* (repère sur le pourtour du cadran) */
    for (int k = 0; k < NMENU; k++) {
        float a = menu_angle(k), x = CX + sinf(a) * 160, y = CY - cosf(a) * 160;
        int sel = k == H.sel;
        uint16_t col = sel ? C2 : C1;
        ui_line(CX + sinf(a) * 80, CY - cosf(a) * 80, CX + sinf(a) * 122, CY - cosf(a) * 122, sel ? 2 : 1, col, sel ? 255 : 77);
        hex_button(x, y, 84, 73, col, sel, MENU_ICON[k], 34);
        button(x, y, 40, A_MENU_ITEM, k);
    }
    ui_circle(CX, CY, 70, ui_mix(ui_rgb(0x071420), C1, 0.12f), 235);
    ui_ring(CX, CY, 78, 1, C1, 128);
    dashed_ring(70, 2, 5, 3, C1, 90);
    char t[40];
    snprintf(t, sizeof t, "%02d / %02d", H.sel + 1, NMENU);
    ui_text(ui_font(FONT_MONO, 18), CX, 202, UI_CENTER, C2, 255, t);
    ui_text(ui_fit(FONT_DISP, MENU_NAME[H.sel], 132, 30, 22), CX, 240, UI_CENTER, ui_rgb(WHITE), 255, MENU_NAME[H.sel]);
    menu_detail(H.sel, t, sizeof t);
    ui_text(ui_fit(FONT_MONO, t, 128, 20, 18), CX, 266, UI_CENTER, ui_rgb(DIM), 255, t);
    ui_text(ui_font(FONT_MONO, 18), CX, 292, UI_CENTER, C1, 255, "▶ OUVRIR");
    button(CX, CY, 70, A_OPEN, 0);
}

/* Barre segmentée (comme segBar de l'émulateur) */
static void seg_bar(float x, float y, float w, float h, float ratio, uint16_t col) {
    int n = (int)(w / 6), fill = (int)ceilf(n * ratio);
    for (int k = 0; k < n; k++) ui_rect(x + k * 6, y, 4, h, k < fill ? col : ui_rgb(WHITE), k < fill ? 255 : 31);
}

/* Ligne de liste dans la corde du cercle (plein écran : toute la largeur, moins 24 de chaque côté) */
static float row_half(float y) {
    if (PORTRAIT) return (UW - 48) / 2;
    float d = y - CY; return sqrtf(233 * 233 - d * d) - 40;   /* hors des graduations */
}

/* Icône d'objet (item_icon) en 32 × 32, coin en (x, y) ; cr : rayon de la découpe autour du centre de l'écran */
static void objet_icone(int icon, float x, float y, float cr) {
    if (icon > 0 && icon <= n_obji) ui_image(obji_px + (icon - 1) * 256, obji_mask + (icon - 1) * 256, 16, 16, x, y, 2, CX, CY, cr);
    if (icon > ICON_OBJG && icon <= ICON_OBJG + n_objg) ui_image(objg_px + (icon - ICON_OBJG - 1) * 1024, objg_mask + (icon - ICON_OBJG - 1) * 1024, 32, 32, x, y, 1, CX, CY, cr);
}
/* icon : icône d'objet k (≥ 1) dessinée en 32 × 32 à gauche ; 0 : sa place reste vide ; -1 : pas de colonne */
/* Couleur de la valeur de la ligne suivante (-1 : celle de la ligne), remise à -1 après elle */
static int row_vcol = -1;
/* Ligne éteinte (08/11/2026, demande de l'utilisateur : dans AMÉLIORER, ce qu'on n'a pas ne se distinguait toujours pas,
   seul le nom était atténué) : cadre, fond, icône voilée et valeur en gris, et en gris aussi une fois choisie ; posée
   avant list_row_icon, comme row_vcol (listRow de l'émulateur, argument eteinte) */
static int row_eteinte;
/* Seconde ligne sous le nom de la prochaine ligne de liste (08/11/2026 : « PORTÉ · SERA », l'équipement porté dans VENDRE, AMÉLIORER,
   FORGE ; le nom monte et rétrécit) ; une fois (listRow de l'émulateur, sous) */
static const char *row_sous;
#define ETEINT 0x5A6474
#define ETEINT_TEXTE 0x6E7888
/* Niveau conseillé face au niveau de l'équipe : vert si elle l'a, orange s'il lui manque 1 à 3 niveaux, rouge au-delà
   (nivCouleur de l'émulateur) */
static uint16_t niveau_couleur(int niv) {
    return ui_rgb(E.level >= niv ? 0x5DFF8A : E.level >= niv - 3 ? 0xFFB347 : 0xFF3B4E);
}
static void list_row_icon(int i, int slot, const char *label, const char *value, uint16_t label_col, float bar, int icon) {
    float y = ROW_Y0 + slot * ROW_STEP, hw = row_half(y + ROW_H), x0 = CX - hw, w0 = 2 * hw;
    float ic = icon >= 0 ? 36 : 0, x = x0 + ic, w = w0 - ic;
    int sel = i == H.sub_sel;
    const int et = row_eteinte; row_eteinte = 0;
    uint16_t col = et ? ui_rgb(ETEINT) : sel ? C2 : C1;
    float q[12]; ui_chamfer_path(x0, y, w0, ROW_H, 10, q);
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.24f : 0.10f), 238);
    if (sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 1, col, 128);
    const float yi = y + (ROW_H - 32) / 2, ym = y + ROW_H / 2, cr = PORTRAIT ? 4000 : 233;   /* icône, milieu de la ligne, découpe */
    objet_icone(icon, x0 + 12, yi, cr);
    if (et && icon >= 0) ui_rect(x0 + 12, yi, 32, 32, ui_rgb(INK), 150);   /* icône voilée */
    float vw = value ? ui_text_width(ui_font(FONT_MONO, 20), value) + 16 : 0;
    const char *sous = row_sous; row_sous = NULL;
    if (sous) {   /* deux lignes : le nom, puis la seconde (porté par…) */
        ui_text(ui_fit(FONT_DISP, label, w - vw - 32, PORTRAIT ? 24 : 20, 14), x + 16, y + ROW_H * 0.52f, UI_LEFT, label_col, 255, label);
        ui_text(ui_fit(FONT_MONO, sous, w - vw - 32, PORTRAIT ? 18 : 15, 11), x + 16, y + ROW_H * 0.86f, UI_LEFT, et ? ui_rgb(ETEINT_TEXTE) : C2, 255, sous);
    }
    else ui_text(ui_fit(FONT_DISP, label, w - vw - 32, 22, 16), x + 16, ym + (bar >= 0 ? 4 : 8), UI_LEFT, label_col, 255, label);
    if (value) ui_text(ui_font(FONT_MONO, 20), x + w - 14, ym + 7, UI_RIGHT, row_vcol >= 0 ? (uint16_t)row_vcol : col, 255, value);
    row_vcol = -1;
    if (bar >= 0) seg_bar(x + 16, ym + 11, w * 0.55f, 4, bar, bar < 0.25f ? ui_rgb(0xFF3B4E) : col);
    button_rect(x0, y, w0, ROW_H, A_ROW, i);
}
static void list_row(int i, int slot, const char *label, const char *value, uint16_t label_col, float bar) {
    list_row_icon(i, slot, label, value, label_col, bar, -1);
}

#if ENG_DYN
/* En-tête du plein écran : ‹ et le titre en haut à gauche (toucher : retour), le sous-titre dessous ; repère de l'image
   (les sous-menus sont décalés de ui_tx en x) */
static void entete(const char *icon, const char *titre, const char *sub) {
    const float L = 24 - ui_tx();
    backdrop();
    ui_icon("back", L + 12, 44, 26, C1, 255);   /* « ‹ » n'est pas dans la police du paquet */
    ui_icon(icon, L + 44, 44, 26, C2, 255);
    ui_text(ui_font(FONT_DISP, 30), L + 70, 54, UI_LEFT, ui_rgb(WHITE), 255, titre);
    button_rect(L - 12, 8, 300, 76, A_BACK, 0);
    const int msg = H.now - H.toast_t < 1.8f && H.toast[0];
    const char *line = msg ? H.toast : sub;
    if (line) ui_text(ui_fit(FONT_MONO, line, UW - 48, 20, 16), L, 100, UI_LEFT, msg ? C2 : ui_rgb(DIM), 255, line);
}
#endif
static void sub_header(int k, const char *sub) {
#if ENG_DYN
    entete(MENU_ICON[k], MENU_NAME[k], sub); return;
#endif
    backdrop();
    ui_icon(MENU_ICON[k], CX, 62, 34, C2, 255);
    ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, MENU_NAME[k]);
    /* Le message de confirmation (sauvegarde, objet utilisé) remplace un instant le sous-titre */
    int msg = H.now - H.toast_t < 1.8f && H.toast[0];
    const char *line = msg ? H.toast : sub;
    if (line) ui_text(ui_fit(FONT_MONO, line, 300, 20, 16), CX, 138, UI_CENTER, msg ? C2 : ui_rgb(DIM), 255, line);
}

static void sub_footer(const char *detail) {
#if ENG_DYN
    /* Pied : le détail de la ligne choisie (deux lignes au plus), RETOUR et VALIDER (la ligne choisie) */
    const float L = 24 - ui_tx(), top = UH - PIED_H + 20;
    ui_rect(-ui_tx(), top - 20, UW, PIED_H, ui_rgb(0x0E0A1E), 245);
    ui_line(-ui_tx(), top - 20, UW - ui_tx(), top - 20, 2, C1, 255);
    if (detail) {
        char l[6][96];
        const ui_font_t *f = ui_font(FONT_MONO, 20);
        const int n = wrap_chord(f, detail, 0, 0, l);
        for (int i = 0; i < n && i < 3; i++) ui_text(f, L, top + 26 + i * 26, UI_LEFT, ui_rgb(DIM), 255, l[i]);
    }
    const float bw = (UW - 48 - 14) / 2, by = UH - 40 - 84;
    cadre_bouton(L, by, bw, 84, 0, ui_rgb(DIM));
    ui_icon("back", L + 34, by + 42, 26, ui_rgb(DIM), 255);
    ui_text(ui_font(FONT_DISP, 22), L + bw / 2 + 14, by + 50, UI_CENTER, ui_rgb(WHITE), 255, "RETOUR");
    button_rect(L, by, bw, 84, A_BACK, 0);
    if (sub_rows() > 0 && H.sub != SUB_STATUS) {
        cadre_bouton(L + bw + 14, by, bw, 84, 1, C2);
        ui_text(ui_font(FONT_DISP, 22), L + bw + 14 + bw / 2, by + 50, UI_CENTER, ui_rgb(WHITE), 255, "VALIDER");
        button_rect(L + bw + 14, by, bw, 84, A_VALIDER, 0);
    }
    return;
#endif
    if (detail) ui_text(ui_fit(FONT_MONO, detail, 300, 20, 16), CX, 384, UI_CENTER, ui_rgb(DIM), 255, detail);
    hex_button(CX, 428, 72, 54, ui_rgb(DIM), 0, "back", 26);
    button(CX, 428, 34, A_BACK, 0);
}

/* STATUT : la fiche d'un membre (‹ › ou le bord : le suivant) ; niveau et expérience de l'équipe (ce qu'il manque pour le
   niveau suivant), PV et PM, ATQ, MAG, DÉF, VIT, job et ce qu'il porte ; drawStatut de l'émulateur */
static void stat_barre(float l, float r, float y, const char *nom, const char *val, float ratio, uint16_t col) {
    ui_text(ui_font(FONT_DISP, 18), l, y, UI_LEFT, ui_rgb(WHITE), 255, nom);
    ui_text(ui_font(FONT_MONO, 18), r, y, UI_RIGHT, col, 255, val);
    seg_bar(l, y + 8, r - l, 6, ratio < 0 ? 0 : ratio > 1 ? 1 : ratio, col);
}
/* Sorts du membre mi pour STATUT, ligne k dans t : 0 ceux qu'il connaît (job à son niveau, équipement, appris), 1 ceux que
   les cristaux du Chœur lui ont appris, 2 le cristal porté et ses points, sinon le prochain sort de son job et son niveau ;
   0 : rien à écrire (drawStatut de l'émulateur) */
/* Cristal du Chœur i pour le membre mi (menu ÉQUIP.) : « Apprend GIVRE : 3/8 », ou « GIVRE appris » ; 0 : pas un cristal */
static int cristal_texte(int mi, int i, char *t, int cap) {
    const eqitem_t *q = eq_item(i);
    int premier = -1;
    if (!q || !q->pts) return 0;
    for (int k = 0; k < E.w.nsorts && k < 64; k++) {
        if (!(q->sorts >> k & 1)) continue;
        if (!sort_appris(mi, k)) { snprintf(t, (size_t)cap, "Apprend %s : %d/%d", E.w.sorts[k].nom, E.apts[mi][k], q->pts); return 1; }
        if (premier < 0) premier = k;
    }
    if (premier < 0) return 0;
    snprintf(t, (size_t)cap, "%s appris", E.w.sorts[premier].nom);
    return 1;
}
static int statut_sorts(int mi, int k, char *t, int cap) {
    int l[MAX_SORTS], o, c = 0;
    t[0] = 0;
    if (k == 0) {
        const int n = sorts_of(mi, l);
        o = snprintf(t, (size_t)cap, "SORTS ·");
        for (; c < n && c < 3 && o < cap - 20; c++) o += snprintf(t + o, (size_t)(cap - o), "%s %s", c ? "," : "", E.w.sorts[l[c]].nom);
        if (!n) snprintf(t, (size_t)cap, "SORTS · aucun");
        else if (c < n && o < cap - 8) snprintf(t + o, (size_t)(cap - o), " +%d", n - c);
        return 1;
    }
    if (k == 1) {
        o = snprintf(t, (size_t)cap, "APPRIS ·");
        for (int s = 0; s < E.w.nsorts && o < cap - 20; s++) if (sort_appris(mi, s)) o += snprintf(t + o, (size_t)(cap - o), "%s %s", c++ ? "," : "", E.w.sorts[s].nom);
        if (!c) snprintf(t, (size_t)cap, "APPRIS · aucun cristal encore");
        return 1;
    }
    int s, p, mx, best = -1, bn = 1000;
    if (cristal_de(mi, &s, &p, &mx)) { snprintf(t, (size_t)cap, "CRISTAL · %s %d/%d", E.w.sorts[s].nom, p, mx); return 1; }
    const int j = E.job[mi] < E.w.njobs ? E.job[mi] : -1;
    for (int x = 0; j >= 0 && x < E.w.nsorts; x++) { const int nv = sort_niv_job(j, x); if ((E.w.jobs[j].sorts >> x & 1) && nv > E.level && nv < bn) { bn = nv; best = x; } }
    if (best < 0) return 0;
    snprintf(t, (size_t)cap, "NIV. %d · %s", bn, E.w.sorts[best].nom);
    return 1;
}
static void draw_statut(int n) {
    char t[96], v[48];
    const int mi = band_at(H.sub_sel);
    const member_t *m = &PARTY[mi];
#if ENG_DYN
    snprintf(t, sizeof t, "%d / %d · %d crédits", H.sub_sel + 1, n, E.credits);
    entete(MENU_ICON[SUB_STATUS], MENU_NAME[SUB_STATUS], t);
    const float L = 24 - ui_tx(), R = UW - 24 - ui_tx(), cx = (L + R) / 2, y0 = 70;
#else
    backdrop();
    ui_icon(MENU_ICON[SUB_STATUS], CX, 44, 26, C2, 255);
    const int msg = H.now - H.toast_t < 1.8f && H.toast[0];
    snprintf(t, sizeof t, "STATUT · %d / %d · %d CR", H.sub_sel + 1, n, E.credits);
    ui_text(ui_fit(FONT_MONO, msg ? H.toast : t, 260, 16, 14), CX, 76, UI_CENTER, msg ? C2 : ui_rgb(DIM), 255, msg ? H.toast : t);
    const float L = CX - 150, R = CX + 150, cx = CX, y0 = 0;
#endif
    /* Le membre, ‹ › de part et d'autre (A_ROW sur le voisin : il devient la ligne choisie) */
    ui_text(ui_fit(FONT_DISP, m->name, R - L - 90, 30, 20), cx, y0 + 112, UI_CENTER, ui_rgb(membre_couleur(mi)), 255, m->name);
    if (n > 1)
        for (int s = -1; s <= 1; s += 2) {
            const float ax = s < 0 ? L + 8 : R - 8, ay = y0 + 102;
            const float tri[6] = { ax + s * 9, ay, ax - s * 7, ay - 11, ax - s * 7, ay + 11 };
            ui_poly(tri, 3, C1, 255);
            button(ax, ay, 30, A_ROW, (H.sub_sel + s + n) % n);
        }
    const int job = E.w.neqtypes > 0 && E.job[mi] < E.w.njobs;
    if (job) snprintf(t, sizeof t, "NIV. %d · %s", E.level, E.w.jobs[E.job[mi]].nom);
    else snprintf(t, sizeof t, "NIVEAU %d", E.level);
    ui_text(ui_fit(FONT_MONO, t, R - L, 18, 14), cx, y0 + 138, UI_CENTER, C1, 255, t);
    /* Expérience de l'équipe : celle du niveau, ce qu'il manque */
    const int nx = exp_next(E.level), fin = E.level >= NIV_MAX;
    snprintf(v, sizeof v, fin ? "—" : "%d / %d", E.exp, nx);
    stat_barre(L, R, y0 + 166, "EXP", v, fin ? 1 : (float)E.exp / nx, C2);
    if (fin) snprintf(t, sizeof t, "Niveau maximum");
    else snprintf(t, sizeof t, "Encore %d EXP pour le niveau %d", nx - E.exp, E.level + 1);
    ui_text(ui_fit(FONT_MONO, t, R - L, 16, 13), cx, y0 + 196, UI_CENTER, ui_rgb(DIM), 255, t);
    const float pv = (float)m->hp / (m->max > 0 ? m->max : 1);
    if (m->hp <= 0) snprintf(v, sizeof v, "À TERRE · 0 / %d", m->max);   /* 02/11/2026 : à terre jusqu'aux soins (drawStatut) */
    else snprintf(v, sizeof v, "%d / %d", m->hp, m->max);
    stat_barre(L, R, y0 + 226, "PV", v, pv, pv < 0.25f ? ui_rgb(0xFF3B4E) : ui_rgb(0x3CE07A));
    snprintf(v, sizeof v, "%d / %d", m->mp, m->maxmp);
    stat_barre(L, R, y0 + 260, "PM", v, m->maxmp > 0 ? (float)m->mp / m->maxmp : 0, ui_rgb(PM_HEX));
    /* Les quatre statistiques (avec le job et l'équipement) */
    static const char *const NOM[4] = { "ATQ", "MAG", "DÉF", "VIT" };
    const int val[4] = { m->atk, m->mag ? m->mag : m->atk, m->def, m->spd };
    const float pas = (R - L) / 4;
    for (int k = 0; k < 4; k++) {
        const float x = L + pas * (k + 0.5f);
        ui_text(ui_font(FONT_MONO, 14), x, y0 + 298, UI_CENTER, ui_rgb(DIM), 255, NOM[k]);
        snprintf(v, sizeof v, "%d", val[k]);
        ui_text(ui_fit(FONT_DISP, v, pas - 6, 26, 16), x, y0 + 326, UI_CENTER, ui_rgb(WHITE), 255, v);
        if (k) ui_line(L + pas * k, y0 + 284, L + pas * k, y0 + 330, 1, C1, 70);
    }
    /* Ce qu'il porte, ou ses sorts (03/11/2026) : sur l'écran rond, l'un puis l'autre toutes les 5 s quand il connaît un sort ou
       porte un cristal du Chœur ; toucher le bas de la fiche (OK) montre l'autre et la garde ; plein écran : les deux */
    int ls[MAX_SORTS], cs, cp, cm;
    const int a_sorts = E.w.nsorts > 0 && (sorts_of(mi, ls) > 0 || cristal_de(mi, &cs, &cp, &cm));
    ST_VUE.vu = !PORTRAIT && a_sorts && (ST_VUE.fixe ? ST_VUE.sorts : ((int)(H.now / 5) & 1));
    if (a_sorts && !PORTRAIT) button_rect(L, y0 + 340, R - L, 52, A_ROW, H.sub_sel);
    for (int k = 0; k < 3 && (PORTRAIT || ST_VUE.vu); k++)
        if (statut_sorts(mi, k, t, sizeof t)) ui_text(ui_fit(FONT_MONO, t, PORTRAIT ? R - L : 280 - k * 30, 16, 12), cx, y0 + 354 + k * 19 + (PORTRAIT ? 70 : 0), UI_CENTER, k == 2 ? C2 : k == 0 ? C1 : ui_rgb(DIM), 255, t);
    /* Ce qu'il porte : arme, armure, accessoire (sans jobs : son équipement écrit, puis sa tenue) */
    for (int k = 0; k < 3 && !ST_VUE.vu; k++) {
        const int it = E.eq[mi][k];
        const char *nm = E.w.neqtypes > 0 ? (it != EQ_AUCUN ? ITEMS[it].name : "—") : k < 2 ? m->eq[k] : tenue_label(mi);
        snprintf(t, sizeof t, "%s · %s", E.w.neqtypes > 0 || k < 2 ? SLOT_NAME[k] : "TENUE", nm ? nm : "—");
        ui_text(ui_fit(FONT_MONO, t, PORTRAIT ? R - L : 280 - k * 30, 16, 12), cx, y0 + 354 + k * 19, UI_CENTER, k == 0 ? C1 : ui_rgb(DIM), 255, t);
    }
    sub_footer(NULL);
}

/* ÉQUIPE : Sera puis chaque recrue de la bande, une ligne chacune (drawEquipe de l'émulateur, à garder identiques) :
   portrait dans sa tenue, nom à sa couleur, job et niveau commun, à droite sa place (CHEF, COMBAT, RÉSERVE, IMPOSÉ :
   « equipe » du scénario) ; toucher la ligne choisie : au combat ou en réserve (activate_row, equipe_basculer) */
static void equipe_ligne(int i, int slot, int mi) {
    char t[48];
    const float y = ROW_Y0 + slot * ROW_STEP, hw = row_half(y + ROW_H), x0 = CX - hw, w0 = 2 * hw, r = (ROW_H - 10) / 2;
    const int sel = i == H.sub_sel, au = in_team(mi), imp = (E.impose >> mi) & 1;
    const uint16_t col = sel ? C2 : C1, mc = ui_rgb(membre_couleur(mi));
    float q[12]; ui_chamfer_path(x0, y, w0, ROW_H, 10, q);
    ui_poly(q, 6, ui_mix(ui_rgb(INK), au ? mc : col, sel ? 0.24f : au ? 0.14f : 0.06f), 238);
    if (sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 1, col, au ? 160 : 90);
    portrait_membre(mi, x0 + 12 + r, y + ROW_H / 2, r, au ? mc : ui_rgb(DIM));
    const char *tag = mi == 0 ? "CHEF" : imp ? "IMPOSÉ" : au ? "COMBAT" : "RÉSERVE";
    const ui_font_t *ft = ui_font(FONT_MONO, 18);
    const float tw = ui_text_width(ft, tag), xt = x0 + 22 + 2 * r, wt = w0 - (xt - x0) - tw - 28;
    ui_text(ui_fit(FONT_DISP, PARTY[mi].name, wt, 22, 14), xt, y + ROW_H * 0.52f, UI_LEFT, au ? mc : ui_rgb(WHITE), 255, PARTY[mi].name);
    const int ko = PARTY[mi].hp <= 0;   /* à terre (02/11/2026) : écrit en rouge sous son nom (drawEquipe) */
    snprintf(t, sizeof t, "%s · NIV. %d", ko ? "À TERRE" : E.job[mi] < E.w.njobs ? E.w.jobs[E.job[mi]].nom : "—", E.level);
    ui_text(ui_fit(FONT_MONO, t, wt, 15, 11), xt, y + ROW_H * 0.86f, UI_LEFT, ko ? ui_rgb(0xFF3B4E) : ui_rgb(DIM), 255, t);
    ui_text(ft, x0 + w0 - 14, y + ROW_H / 2 + 7, UI_RIGHT, mi == 0 || au ? C2 : ui_rgb(DIM), au || mi == 0 ? 255 : 170, tag);
    button_rect(x0, y, w0, ROW_H, A_ROW, i);
}
static void draw_equipe(int n) {
    char t[96];
    snprintf(t, sizeof t, "Au combat : %d / %d · NIV. %d", team_count(), EQUIPE_MAX, E.level);
    sub_header(SUB_EQUIPE, t);
    for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) equipe_ligne(H.sub_scroll + s, s, band_at(H.sub_scroll + s));
    const int mi = band_at(H.sub_sel);
    const member_t *m = &PARTY[mi];
    if (n < 2) snprintf(t, sizeof t, "Personne d'autre dans l'équipe");
    else if (mi == 0) snprintf(t, sizeof t, "Toucher un compagnon : combat ou réserve");
    else snprintf(t, sizeof t, "PV %d / %d · PM %d / %d · ATQ %d", m->hp, m->max, m->mp, m->maxmp, m->atk);   /* PV et PM gardés d'un combat à l'autre */
    sub_footer(t);
}

/* ---------- Menu ÉQUIP. : dessin (drawEquip, equipLigneObjet, drawEquipFiche de l'émulateur, à garder identiques) ---------- */
/* Image d'un objet (item_icon) en 32k × 32k, coin en (x, y) ; cr : rayon de la découpe autour du centre de l'écran */
static void eq_icone(int icon, float x, float y, float k, float cr) {
    if (icon > 0 && icon <= n_obji) ui_image(obji_px + (icon - 1) * 256, obji_mask + (icon - 1) * 256, 16, 16, x, y, 2 * k, CX, CY, cr);
    if (icon > ICON_OBJG && icon <= ICON_OBJG + n_objg) ui_image(objg_px + (icon - ICON_OBJG - 1) * 1024, objg_mask + (icon - ICON_OBJG - 1) * 1024, 32, 32, x, y, k, CX, CY, cr);
}
/* Case vide (RETIRER, emplacement libre) : carré de côté 2r, barré */
static void case_vide(float x, float y, float r, uint16_t c) {
    const float q[8] = { x - r, y - r, x + r, y - r, x + r, y + r, x - r, y + r };
    ui_outline(q, 4, 2, c, 200);
    ui_line(x - r, y + r, x + r, y - r, 2, c, 200);
}
/* Ajoute s à d (longueur n, capacité cap) s'il tient ; renvoie la nouvelle longueur */
static int eq_ajoute(char *d, int n, int cap, const char *s) {
    const int l = (int)strlen(s);
    if (n + l >= cap) return n;
    memcpy(d + n, s, (size_t)l + 1);
    return n + l;
}
/* Écart des statistiques (a : avant, b : après) et des sorts (sa, sb) en morceaux à partir de x sur la ligne y, coupés à
   xmax (« … ») : « ATQ +30 », « +FOUDRE I », vert si mieux, rouge si moins bien ; neutre : tout en couleur 1 (ce que
   l'objet porté apporte). Renvoie le nombre de morceaux (0 : rien ne change) */
static int eq_ecarts(const int *a, const int *b, uint64_t sa, uint64_t sb, float x, float y, float xmax, int neutre) {
    const ui_font_t *f = ui_font(FONT_MONO, PORTRAIT ? 20 : 18);
    const float esp = ui_text_width(f, " ") * 1.5f;
    char t[40];
    int n = 0;
    for (int k = 0; k < ES_N + E.w.nsorts && k < ES_N + 64; k++) {
        int d;
        if (k < ES_N) { d = b[k] - a[k]; if (d) snprintf(t, sizeof t, "%s %+d", ES_NOM[k], d); }
        else { const int s = k - ES_N; d = (int)(sb >> s & 1) - (int)(sa >> s & 1); if (d) snprintf(t, sizeof t, "%c%.20s", d > 0 ? '+' : '-', E.w.sorts[s].nom); }
        if (!d) continue;
        const float w = ui_text_width(f, t);
        if (x + w > xmax) { ui_text(f, x, y, UI_LEFT, ui_rgb(DIM), 255, "…"); return n + 1; }
        ui_text(f, x, y, UI_LEFT, neutre ? C1 : ui_rgb(d > 0 ? EQ_MIEUX : EQ_MOINS), 255, t);
        x += w + esp; n++;
    }
    return n;
}
/* Ligne i (place slot) de la liste : l'objet k (EQ_AUCUN : RETIRER), son image, son nom (grisé si le job du membre ne
   peut pas le porter), sur la seconde ligne l'écart avec ce qui est porté (l'objet porté : ce qu'il apporte), à droite
   PORTÉ ou le nombre au sac */
static void equip_ligne_objet(int i, int slot, int k) {
    const int m = EQM.m, sl = EQM.sl, cur = E.eq[m][sl], porte = k != EQ_AUCUN && k == cur, ok = k == EQ_AUCUN || eq_aff(m, k) > 0;
    const float y = ROW_Y0 + slot * ROW_STEP, hw = row_half(y + ROW_H), x0 = CX - hw, w0 = 2 * hw;
    const int sel = i == H.sub_sel;
    const uint16_t col = sel ? C2 : C1;
    float q[12]; ui_chamfer_path(x0, y, w0, ROW_H, 10, q);
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.24f : porte ? 0.16f : 0.10f), 238);
    if (sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 1, col, porte ? 200 : 128);
    if (k != EQ_AUCUN) eq_icone(item_icon(k), x0 + 12, y + (ROW_H - 32) / 2, 1, PORTRAIT ? 4000 : 233);
    else case_vide(x0 + 28, y + ROW_H / 2, 11, ui_rgb(DIM));
    char tag[16] = "";
    if (porte) snprintf(tag, sizeof tag, "PORTÉ");
    else if (k != EQ_AUCUN && ITEMS[k].count > 1) snprintf(tag, sizeof tag, "×%d", ITEMS[k].count);
    const ui_font_t *ft = ui_font(FONT_MONO, PORTRAIT ? 20 : 18);
    const float tw = tag[0] ? ui_text_width(ft, tag) + 12 : 0, xt = x0 + 52, y1 = y + ROW_H * 0.52f, y2 = y + ROW_H * 0.86f;
    const char *nom = k == EQ_AUCUN ? "RETIRER" : ITEMS[k].name;
    ui_text(ui_fit(FONT_DISP, nom, x0 + w0 - 14 - tw - xt, PORTRAIT ? 26 : 22, 14), xt, y1, UI_LEFT, ok ? ui_rgb(WHITE) : ui_rgb(DIM), ok ? 255 : 170, nom);
    if (tag[0]) ui_text(ft, x0 + w0 - 14, y1, UI_RIGHT, porte ? C2 : ui_rgb(DIM), 255, tag);
    if (!ok) ui_text(ft, xt, y2, UI_LEFT, ui_rgb(DIM), 170, "Pas pour son job");
    else {
        int a[ES_N], b[ES_N];
        const uint64_t sa = eq_stats(m, sl, porte ? EQ_AUCUN : cur, a), sb = eq_stats(m, sl, k, b);
        if (!eq_ecarts(a, b, sa, sb, xt, y2, x0 + w0 - 14, porte)) ui_text(ft, xt, y2, UI_LEFT, ui_rgb(DIM), 255, "Aucun changement");
    }
    button_rect(x0, y, w0, ROW_H, A_ROW, i);
}
/* Nom d'un objet de la fiche (RIEN sans objet) ; sa coupure en deux lignes (à l'espace le plus près du milieu) s'il ne tient
   pas sur une ligne de largeur w, -1 sinon */
#define EQ_ECART_MILIEU(i, l) ((i) < (l) / 2 ? (l) / 2 - (i) : (i) - (l) / 2)
static const char *eq_nom(int it) { return it != EQ_AUCUN ? ITEMS[it].name : "RIEN"; }
static int eq_coupe(int it, float w) {
    const char *nom = eq_nom(it);
    const int len = (int)strlen(nom);
    int cut = -1;
    for (int i = 0; i < len; i++) if (nom[i] == ' ' && (cut < 0 || EQ_ECART_MILIEU(i, len) < EQ_ECART_MILIEU(cut, len))) cut = i;
    return cut > 0 && ui_text_width(ui_fit(FONT_DISP, nom, w, PORTRAIT ? 26 : 20, 16), nom) > w ? cut : -1;
}
/* Élément d'un équipement du sac (MG_*, celui de la forge ou le sien), -1 : aucun */
static int eq_element(int it) { return it != EQ_AUCUN ? eq_el_de(it) : -1; }
/* Colonne de la fiche : titre, image (case vide sans objet), nom (sur deux lignes s'il ne tient pas ; deux : la place de
   deux lignes, prise si l'une des colonnes en a besoin), affinité du job, puis l'élément d'une arme (elem : une ligne
   de plus, prise si l'une des colonnes en a un ; 08/11/2026) ; renvoie le bas de la colonne */
static float eq_colonne(float cx, float y, float w, float k, int it, const char *titre, uint16_t tc, int deux, int elem) {
    const ui_font_t *fm = ui_font(FONT_MONO, PORTRAIT ? 20 : 18);
    const float s = 32 * k, lh = PORTRAIT ? 28 : 20, yn = y + 10 + s + (PORTRAIT ? 34 : 24) + (deux ? lh / 2 : 0);
    float ya = yn + (PORTRAIT ? 32 : 22) + (deux ? lh / 2 : 0);
    ui_text(fm, cx, y, UI_CENTER, tc, 255, titre);
    if (it != EQ_AUCUN) eq_icone(item_icon(it), cx - s / 2, y + 10, k, PORTRAIT ? 4000 : 233);
    else case_vide(cx, y + 10 + s / 2, s / 2 - 5, ui_rgb(DIM));
    const char *nom = eq_nom(it);
    const uint16_t nc = it != EQ_AUCUN ? ui_rgb(WHITE) : ui_rgb(DIM);
    const int cut = eq_coupe(it, w);
    if (cut > 0) {   /* deux lignes */
        char l1[64];
        snprintf(l1, sizeof l1, "%.*s", cut < 63 ? cut : 63, nom);
        ui_text(ui_fit(FONT_DISP, l1, w, PORTRAIT ? 26 : 20, 16), cx, yn - lh / 2, UI_CENTER, nc, 255, l1);
        ui_text(ui_fit(FONT_DISP, nom + cut + 1, w, PORTRAIT ? 26 : 20, 16), cx, yn + lh / 2, UI_CENTER, nc, 255, nom + cut + 1);
    } else ui_text(ui_fit(FONT_DISP, nom, w, PORTRAIT ? 26 : 20, 16), cx, yn, UI_CENTER, nc, 255, nom);
    if (it != EQ_AUCUN) {
        char t[32];
        snprintf(t, sizeof t, "AFFINITÉ %s", AFF_NAME[eq_aff(EQM.m, it)]);
        ui_text(fm, cx, ya, UI_CENTER, ui_rgb(DIM), 255, t);
    }
    if (elem) {   /* l'élément, à sa couleur : « ÉLÉMENT FEU » (une arme), « RÉSISTE FEU » (armure, accessoire) */
        const int el = eq_element(it);
        ya += PORTRAIT ? 30 : 20;
        if (el >= 0) {
            char t[32];
            elem_etiquette(it, t, sizeof t);
            ui_text(ui_fit(FONT_MONO, t, w, PORTRAIT ? 20 : 18, 12), cx, ya, UI_CENTER, ui_rgb(fx_couleur(FX_SORT(el, 1))), 255, t);
        }
    }
    return ya;
}
/* Fiche de comparaison (EQN_FICHE) : l'objet porté à gauche, le nouveau à droite (RIEN pour retirer), les statistiques qui
   changent (avant, après, écart en vert ou en rouge), les sorts gagnés et perdus, puis « ÉQUIPER ? » (« RETIRER ? ») et
   OUI, NON (A_EQUIPER, d'un toucher ; au clavier ou à la manette : haut, bas, gauche, droite, puis OK) */
static void draw_equip_fiche(void) {
    char t[96];
    const int m = EQM.m, sl = EQM.sl, cur = E.eq[m][sl], nx = EQM.it;
    const int shop = H.sub == SUB_SHOP;   /* fiche d'un équipement à acheter (shop_fiche) */
    if (shop) snprintf(t, sizeof t, "< %s · %s >", PARTY[m].name, SLOT_NAME[sl]);   /* (« ‹ » n'est pas dans la police) */
    else snprintf(t, sizeof t, "%s · %s", PARTY[m].name, SLOT_NAME[sl]);
#if ENG_DYN
    entete(MENU_ICON[SUB_EQUIP], "COMPARER", t);
    const float L = 24 - ui_tx(), R = UW - 24 - ui_tx(), k = 2, y0 = 150, ybas = UH - PIED_H - 10;   /* ybas : au-dessus du pied */
#else
    backdrop();
    ui_icon(MENU_ICON[SUB_EQUIP], CX, 42, 26, C2, 255);
    const int msg = H.now - H.toast_t < 1.8f && H.toast[0];
    ui_text(ui_fit(FONT_MONO, msg ? H.toast : t, 260, 18, 16), CX, 76, UI_CENTER, msg ? C2 : ui_rgb(DIM), 255, msg ? H.toast : t);
    const float L = CX - 150, R = CX + 150, k = 1, y0 = 102, ybas = 342;   /* ybas : au-dessus de la question */
#endif
    const float cx = (L + R) / 2, pw = R - L, cw = pw / 2 - 12;
    const int deux = eq_coupe(cur, cw) > 0 || eq_coupe(nx, cw) > 0;   /* un nom sur deux lignes : les deux colonnes alignées */
    const int elem = eq_element(cur) >= 0 || eq_element(nx) >= 0;   /* un objet à élément : sa ligne dans les deux colonnes */
    const float ya = eq_colonne(cx - pw / 4, y0, cw, k, cur, "PORTÉ", ui_rgb(DIM), deux, elem);
    const float yb = eq_colonne(cx + pw / 4, y0, cw, k, nx, nx == EQ_AUCUN ? "APRÈS" : "NOUVEAU", C2, deux, elem);
    { const float fy = y0 + 10 + 16 * k, tri[6] = { cx + 8 * k, fy, cx - 6 * k, fy - 9 * k, cx - 6 * k, fy + 9 * k }; ui_poly(tri, 3, C2, 255); }
    /* Statistiques qui changent, puis les sorts gagnés et perdus ; le pas se resserre s'il y en a beaucoup */
    int a[ES_N], b[ES_N], nl = 0;
    const uint64_t sa = eq_stats(m, sl, cur, a), sb = eq_stats(m, sl, nx, b), gain = sb & ~sa, perte = sa & ~sb;
    for (int s = 0; s < ES_N; s++) nl += a[s] != b[s];
    nl += (gain != 0) + (perte != 0);
    float y = (ya > yb ? ya : yb) + (PORTRAIT ? 46 : 28), pas = PORTRAIT ? 38 : 22;
    if (nl > 1 && y + (nl - 1) * pas > ybas) pas = (ybas - y) / (nl - 1);
    const ui_font_t *fn = ui_font(FONT_DISP, PORTRAIT ? 22 : 18), *fv = ui_font(FONT_MONO, PORTRAIT ? 20 : 18);
    if (!nl) ui_text(fv, cx, y, UI_CENTER, ui_rgb(DIM), 255, "Aucun changement");
    for (int s = 0; s < ES_N; s++) {
        if (a[s] == b[s]) continue;
        const int d = b[s] - a[s];
        const float tw = pw < 360 ? pw : 360, tr[6] = { cx + tw * 0.08f, y - 6, cx + tw * 0.04f, y - 11, cx + tw * 0.04f, y - 1 };   /* tableau : 360 px au plus */
        ui_text(fn, cx - tw * 0.42f, y, UI_LEFT, ui_rgb(WHITE), 255, ES_NOM[s]);
        snprintf(t, sizeof t, "%d", a[s]); ui_text(fv, cx - tw * 0.01f, y, UI_RIGHT, ui_rgb(DIM), 255, t);
        ui_poly(tr, 3, ui_rgb(DIM), 255);
        snprintf(t, sizeof t, "%d", b[s]); ui_text(fv, cx + tw * 0.28f, y, UI_RIGHT, ui_rgb(WHITE), 255, t);
        snprintf(t, sizeof t, "%+d", d); ui_text(fv, cx + tw * 0.45f, y, UI_RIGHT, ui_rgb(d > 0 ? EQ_MIEUX : EQ_MOINS), 255, t);
        y += pas;
    }
    for (int g = 0; g < 2; g++) {   /* « + FOUDRE I, GIVRE I » gagnés, « - BRASIER I » perdus (ce qui ne tient pas : « … ») */
        const uint64_t bits = g ? perte : gain;
        if (!bits) continue;
        char so[96];
        int n = eq_ajoute(so, 0, sizeof so, g ? "SORTS - " : "SORTS + "), vus = 0;
        for (int s = 0; s < E.w.nsorts && s < 64; s++) {
            if (!(bits >> s & 1)) continue;
            char essai[96];
            memcpy(essai, so, (size_t)n + 1);
            int ne = vus ? eq_ajoute(essai, n, sizeof essai, ", ") : n;
            ne = eq_ajoute(essai, ne, sizeof essai, E.w.sorts[s].nom);
            if (ui_text_width(fv, essai) > pw - 20) { n = eq_ajoute(so, n, sizeof so, " …"); break; }
            memcpy(so, essai, (size_t)ne + 1); n = ne; vus++;
        }
        ui_text(fv, cx, y, UI_CENTER, ui_rgb(g ? EQ_MOINS : EQ_MIEUX), 255, so);
        y += pas;
    }
    char qa[40];
    if (shop) snprintf(qa, sizeof qa, "ACHETER · %d CR ?", shop_prix(nx));
    const char *qn = shop ? qa : nx == EQ_AUCUN ? "RETIRER ?" : "ÉQUIPER ?";
    if (shop) button_rect(cx - pw / 2, y0, pw / 2, ya - y0, A_SHOP_MEMBRE, 1);   /* la colonne de gauche : un autre membre */
#if ENG_DYN
    /* Pied : la question, NON à gauche (comme RETOUR), OUI à droite (comme VALIDER) */
    const float top = UH - PIED_H + 20, bw = (UW - 48 - 14) / 2, by = UH - 40 - 84;
    ui_rect(-ui_tx(), top - 20, UW, PIED_H, ui_rgb(0x0E0A1E), 245);
    ui_line(-ui_tx(), top - 20, UW - ui_tx(), top - 20, 2, C1, 255);
    ui_text(ui_font(FONT_DISP, 26), cx, top + 42, UI_CENTER, ui_rgb(WHITE), 255, qn);
    for (int r = 0; r < 2; r++) {
        const float bx = r ? L : L + bw + 14;
        cadre_bouton(bx, by, bw, 84, H.sub_sel == r, H.sub_sel == r ? C2 : ui_rgb(DIM));
        ui_text(ui_font(FONT_DISP, 26), bx + bw / 2, by + 52, UI_CENTER, ui_rgb(WHITE), 255, r ? "NON" : "OUI");
        button_rect(bx, by, bw, 84, A_EQUIPER, r);
    }
#else
    ui_text(ui_font(FONT_DISP, 22), CX, 364, UI_CENTER, ui_rgb(WHITE), 255, qn);
    for (int r = 0; r < 2; r++) {   /* OUI à gauche, NON à droite */
        const float bx = CX + (r ? 58 : -58), by = 400;
        const int sel = H.sub_sel == r;
        const uint16_t c = sel ? C2 : C1;
        float q[12]; ui_hex_path(bx, by, 100, 44, q);
        ui_poly(q, 6, ui_mix(ui_rgb(INK), c, sel ? 0.28f : 0.10f), 242);
        if (sel) glow_outline(q, 6, 2, c); else ui_outline(q, 6, 2, c, 200);
        ui_text(ui_font(FONT_DISP, 22), bx, by + 8, UI_CENTER, ui_rgb(WHITE), 255, r ? "NON" : "OUI");
        button_rect(bx - 50, by - 22, 100, 44, A_EQUIPER, r);
    }
#endif
}
/* Menu ÉQUIP. : les membres (comme le menu ÉQUIPE), les emplacements du membre (ce qu'il porte à droite), la liste d'un
   emplacement, ou la fiche */
static void draw_equip(int n) {
    char t[96];
    const int jobs = E.w.neqtypes > 0, mi = EQM.m;
    const member_t *m = &PARTY[mi];
    if (EQM.niv == EQN_FICHE) { draw_equip_fiche(); return; }
    if (EQM.niv == EQN_MEMBRES) {
        sub_header(SUB_EQUIP, "Choisir un membre");
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) equipe_ligne(H.sub_scroll + s, s, band_at(H.sub_scroll + s));
        const member_t *ms = &PARTY[band_at(H.sub_sel)];
        snprintf(t, sizeof t, "ATQ %d · MAG %d · DÉF %d · VIT %d", ms->atk, ms->mag, ms->def, ms->spd);
        sub_footer(t);
        return;
    }
    if (EQM.niv == EQN_LISTE) {
        const int sl = EQM.sl, k = n ? EQL[H.sub_sel < n ? H.sub_sel : 0] : EQ_AUCUN;
        snprintf(t, sizeof t, "%s · %s", m->name, SLOT_NAME[sl]);
        sub_header(SUB_EQUIP, t);
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) equip_ligne_objet(H.sub_scroll + s, s, EQL[H.sub_scroll + s]);
        if (!n) snprintf(t, sizeof t, "Rien à porter dans le sac");
        else if (k == EQ_AUCUN) snprintf(t, sizeof t, "L'emplacement reste vide");
        else if (!eq_aff(mi, k)) snprintf(t, sizeof t, "Le job %s ne le porte pas", E.job[mi] < E.w.njobs ? E.w.jobs[E.job[mi]].nom : "—");
        else if (cristal_texte(mi, k, t, sizeof t)) { /* cristal du Chœur : ce qu'il lui apprend, et où il en est */ }
        else { char rs[96]; eq_resume(k, rs, sizeof rs); snprintf(t, sizeof t, "Affinité %s · %.70s", AFF_NAME[eq_aff(mi, k)], rs); }
        sub_footer(t);
        return;
    }
    /* Les emplacements : à gauche leur nom (et l'image de ce qui y est), ce qu'il porte à droite */
    int pl[5];
    equip_places(mi, pl);
    if (jobs && E.job[mi] < E.w.njobs) snprintf(t, sizeof t, "%s · %s", m->name, E.w.jobs[E.job[mi]].nom);
    else snprintf(t, sizeof t, "%s", m->name);
    sub_header(SUB_EQUIP, t);
    for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) {
        const int r = H.sub_scroll + s, rs = pl[r], it = rs >= 0 && rs < 3 ? E.eq[mi][rs] : EQ_AUCUN, ic = rs >= 0 && rs < 3 ? (it != EQ_AUCUN ? item_icon(it) : 0) : -1;
        const char *lb = rs == ER_JOB ? "JOB" : rs == ER_TENUE ? "TENUE" : SLOT_NAME[rs];
        const char *tx = rs == ER_JOB ? (E.job[mi] < E.w.njobs ? E.w.jobs[E.job[mi]].nom : "—") : rs == ER_TENUE ? tenue_label(mi) : it != EQ_AUCUN ? ITEMS[it].name : "—";
        list_row_icon(r, s, lb, NULL, ui_rgb(membre_couleur(mi)), -1, ic);
        const float y = ROW_Y0 + s * ROW_STEP, hw = row_half(y + ROW_H), x = CX - hw + (ic >= 0 ? 36 : 0) + 16;
        const float lw = ui_text_width(ui_font(FONT_DISP, 22), lb), vw = CX + hw - 14 - (x + lw + 18);
        ui_text(ui_fit(FONT_DISP, tx, vw, 20, 16), CX + hw - 14, y + ROW_H / 2 + 7, UI_RIGHT, r == H.sub_sel ? C2 : C1, 255, tx);
    }
    const int sl = n ? pl[H.sub_sel < n ? H.sub_sel : 0] : ER_TENUE;
    const int tt = tenue_of(mi, 0), eq = tenue_mine(mi, E.tenue_eq[mi]) ? E.tenue_eq[mi] : -1;
    char st[64];
    snprintf(st, sizeof st, "ATQ %d · MAG %d · DÉF %d · VIT %d", m->atk, m->mag, m->def, m->spd);
    const eqitem_t *ri = sl >= 0 && sl < 3 ? eq_item(E.eq[mi][sl]) : NULL;   /* objet porté qui donne des sorts : leurs noms */
    char so[64] = "";   /* plus de 32 caractères : le premier et le nombre des autres */
    int nso = 0, k1 = -1, nc = 0;
    for (int k = 0; ri && k < E.w.nsorts; k++)
        if (ri->sorts >> k & 1) { if (k1 < 0) k1 = k; const size_t u = strlen(so); snprintf(so + u, sizeof so - u, "%s%s", nso++ ? ", " : "Sorts : ", E.w.sorts[k].nom); }
    for (const char *c = so; *c; c++) nc += (*c & 0xC0) != 0x80;
    if (nc > 32) snprintf(so, sizeof so, "Sorts : %s +%d", E.w.sorts[k1].nom, nso - 1);
    if (sl == ER_TENUE && tt >= 0 && tt != eq) snprintf(t, sizeof t, "Imposée %s", TENUE_RAISON[E.w.tenues[tt].ctx & 3]);
    else if (sl == ER_TENUE && !jobs) snprintf(t, sizeof t, "%s · %s", m->eq[0], m->eq[1]);
    else if (sl >= 0 && sl < 3 && cristal_texte(mi, E.eq[mi][sl], t, sizeof t)) { /* cristal du Chœur porté : ses points */ }
    else if (so[0]) snprintf(t, sizeof t, "%s", so);
    else if (sl >= 0 && sl < 3 && E.eq[mi][sl] != EQ_AUCUN) {   /* ce qui est porté : affinité et bonus de l'objet */
        char rs[96]; eq_resume(E.eq[mi][sl], rs, sizeof rs);
        snprintf(t, sizeof t, "Affinité %s · %.70s", AFF_NAME[eq_aff(mi, E.eq[mi][sl])], rs);
    }
    else snprintf(t, sizeof t, "%s", st);
    sub_footer(t);
}
/* Banc d'essai (rendu_test quetes) : menu QUÊTES ouvert */
void hud_journal_ouvrir(int niv) { H.menu = 1; H.sel = MENU_JOURNAL; open_sub(SUB_JOURNAL); H.jniv = niv; }   /* essais */
void hud_quetes_ouvrir(void) { H.menu = 1; H.sel = SUB_QUESTS; open_sub(SUB_QUESTS); }
/* Panneau de quêtes k (bulle LIRE, panneaux.c) : ses quêtes ouvrables, l'écran SUB_PANNEAU (panneauOuvrir de l'émulateur) */
void hud_panneau_ouvrir(int k) {
    if (k < 0 || k >= panneau_n()) return;
    PN.k = k; PN.n = panneau_liste(k, PN.o, PN.d, PAN_LISTE); PN.acc_l = -1;
    H.menu = 1; open_sub(SUB_PANNEAU); E.has_dest = 0;
    son_jouer(SON_MENU, 0.35f, 1);
}
/* Banc d'essai (rendu_test panchoix:r) : la ligne r de l'écran du panneau ouvert, choisie */
void hud_panneau_choisir(int row) { if (H.sub == SUB_PANNEAU) { H.sub_sel = row; activate_row(row); } }
/* Banc d'essai (rendu_test panneau:k) : l'écran ouvert, ses lignes (quête, niveau, distance, accroche) */
int hud_panneau_texte(char *out, int n) {
    int o = 0;
    char acc[96], dt[16];
#define PN_AJ(...) do { if (o < n - 1) { const int w_ = snprintf(out + o, (size_t)(n - o), __VA_ARGS__); o = w_ < 0 ? o : o + w_ > n - 1 ? n - 1 : o + w_; } } while (0)
    PN_AJ("panneau %d « %s » : %d quête%s%s", PN.k, panneau_nom(PN.k), PN.n, PN.n > 1 ? "s" : "", H.sub == SUB_PANNEAU ? ", écran ouvert" : "");
    for (int r = 0; r < PN.n; r++) {
        const int q = ouverture_quete(PN.o[r]), ac = ouverture_accroche(PN.o[r]);
        acc[0] = 0; if (ac != 65535) script_string(ac, acc, sizeof acc);
        if (PN.d[r] < 1e8f) dist_txt(PN.d[r], dt, sizeof dt); else snprintf(dt, sizeof dt, "loin");
        PN_AJ("\n  %d. %s (niv %d, %s) : %s", r, script_quest_name(q), script_quest_niveau(q), dt, acc);
    }
#undef PN_AJ
    return o;
}
void hud_carte_ouvrir(void) { H.menu = 1; H.sel = SUB_MAP; open_sub(SUB_MAP); }
/* Banc d'essai : menu SYSTÈME ouvert (ligne logique code choisie ; < 0 : la première) ; toucher : sa ligne activée.
   Renvoie le nom de la ligne affichée en premier (ordre de sys_ligne) */
const char *hud_systeme_essai(int code, int toucher) {
    H.menu = 1; H.sel = SUB_SYSTEM; open_sub(SUB_SYSTEM);
    if (code >= 0) { sys_revenir(code); if (toucher) activate_row(H.sub_sel); }
    static char t[160];
    t[0] = 0;
    for (int i = 0; i < 6 && i < sys_n(); i++) {
        const int c = sys_ligne(i);
        static const char *N[SYS_HEURE + 1] = { "SAUVEGARDER", "CHARGER", "TEINTE", "BROUILLARD", "TRAMAGE", "LUMINOSITÉ", "ZOOM", "MÉTÉO", "SON", "CYCLE", "TÉLÉPORTER", "GALERIE",
                                                "RENCONTRES", "DIRECTION", "CONDUITE", "COMMANDES", "CODE", "ÉVEIL", "QUITTER", "HEURE" };
        snprintf(t + strlen(t), sizeof t - strlen(t), "%s%s", i ? ", " : "", c <= SYS_HEURE ? N[c] : "(plateforme)");
    }
    return t;
}
/* Banc d'essai (rendu_test guidemenu[:g]) : menu GUIDAGE ouvert sur ses groupes (g < 0) ou sur le groupe g */
void hud_guide_ouvrir(int g) { H.menu = 1; H.sel = SUB_GUIDE; open_sub(SUB_GUIDE); if (g >= 0) H.gcat = GR_FAM + g; }
/* Banc d'essai (rendu_test guidage) : lieux du menu GUIDAGE de la carte où est Sera, groupe par groupe (nom, distance) ;
   renvoie leur nombre */
int hud_guide_liste(char *out, int n) {
    int o = 0, k = 0;
    if (n > 0) out[0] = 0;
    for (int gr = 0; gr <= GUIDE_ASTRES; gr++) {
        int dans = 0;
        for (int i = 0; i < E.w.nguides && o < n - 100; i++) {
            const guide_t *g = &E.w.guides[i];
            if (g->carte != E.w.carte || g->groupe != gr) continue;
            char nm[48], d[16];
            guide_label(i, " · ", "", nm, sizeof nm); dist_txt(guide_dist(i), d, sizeof d);
            if (!dans) o += snprintf(out + o, n - o, "%s%s : ", o ? "\n  " : "  ", groupe_nom(gr));
            o += snprintf(out + o, n - o, "%s%s (%s)", dans ? ", " : "", nm, d);
            dans = 1; k++;
        }
    }
    return k;
}
/* Banc d'essai (rendu_test equip:m:s) : menu ÉQUIP. ouvert sur le membre m (-1 : la liste des membres), à l'emplacement sl
   (EQ_* : sa liste ; -1 : ses emplacements) ; état du menu en texte (niveau, membre, lignes de la liste et leurs écarts) */
/* Banc d'essai : la boutique du commerce de l'intérieur où est Sera, en achat (mode 0) ou en vente (1) ; renvoie son nombre
   de lignes (-1 : pas de commerce ici). hud_shop_texte : son état (lignes, fiche, membre comparé, confirmation) */
int hud_shop_ouvrir(int mode) {
    if (!E.in.on || E.in.bat < 0 || E.in.bat >= E.w.nbats || !E.w.bats[E.in.bat].comm) return -1;
    SHOP.comm = E.w.bats[E.in.bat].comm - 1; SHOP.npc = -1; SHOP.mode = mode; SHOP.fiche = -1; SHOP.forge = mode == 2 && ferrailleur_ici();
    snprintf(SHOP.nom, sizeof SHOP.nom, "%s", E.w.bats[E.in.bat].nom);
    shop_liste(); H.menu = 1; open_sub(SUB_SHOP);
    return shop_rows();
}
int hud_shop_texte(char *out, int n) {
    int o = 0;
#define SH_AJ(...) do { if (o < n - 1) { const int w_ = snprintf(out + o, (size_t)(n - o), __VA_ARGS__); o = w_ < 0 ? o : o + w_ > n - 1 ? n - 1 : o + w_; } } while (0)
    SH_AJ("boutique %s (%s), %d crédits, ligne %d", SHOP.nom, SHOP.mode == 4 ? "élément" : SHOP.mode == 2 ? (SHOP.forge ? "forge" : "améliorer") : SHOP.mode ? "vente" : "achat", E.credits, H.sub_sel);
    if (SHOP.mode == 4) SH_AJ(" ; objet %s, élément %d", ITEMS[SHOP.cible].name, eq_el_de(SHOP.cible));
    for (int r = 0; r < shop_rows() && r < 12; r++) { const int k = shop_item(r); SH_AJ("\n  %d. %s ×%d · %d CR%s", r, ITEMS[k].name, ITEMS[k].count, shop_prix(k), SHOP.mode == 2 && !up_possede(k) ? " (GRISÉ)" : eq_slot(k) >= 0 ? " (équipement)" : ""); { char pn[48]; if (porte_par(k, pn, sizeof pn)) SH_AJ(" (PORTÉ par %s)", pn); } }
    if (SHOP.fiche >= 0) SH_AJ("\n  fiche : %s comparé sur %s, réponse %s", ITEMS[SHOP.fiche].name, PARTY[band_at(SHOP.m)].name, H.sub_sel ? "NON" : "OUI");
    if (CHX.on) SH_AJ("\n  confirmation : %s | %s | %s | [%s] [%s] [%s]%s", CHX.titre, CHX.texte[0], CHX.texte[1], CHX.opts[0], CHX.opts[1], CHX.n > 2 ? CHX.opts[2] : "", CHX.shop ? " (boutique)" : "");
#undef SH_AJ
    return o;
}
/* Banc d'essai (rendu_test statut:m:v) : STATUT du membre m, vue v (0 ce qu'il porte, 1 ses sorts, -1 l'une puis l'autre) */
void hud_statut_ouvrir(int m, int v) {
    H.menu = 1; H.sel = SUB_STATUS; open_sub(SUB_STATUS);
    for (int k = 0; k < band_count(); k++) if (band_at(k) == m) H.sub_sel = k;
    ST_VUE.fixe = v >= 0; ST_VUE.sorts = v > 0;
}
void hud_equip_ouvrir(int m, int sl) {
    H.menu = 1; H.sel = 2; open_sub(SUB_EQUIP);
    if (m < 0 || m >= NMEMB) return;
    for (int k = 0; k < band_count(); k++) if (band_at(k) == m) EQM.r[EQN_MEMBRES] = k;
    EQM.m = m; equip_niveau(EQN_PLACES, 0);
    if (sl < 0 || sl >= 3) return;
    int pl[5];
    for (int k = 0, np = equip_places(m, pl); k < np; k++) if (pl[k] == sl) EQM.r[EQN_PLACES] = k;   /* retour : sur sa ligne */
    EQM.sl = sl; equip_niveau(EQN_LISTE, 0);
}
int hud_equip_texte(char *out, int n) {
    int o = 0;
#define EQ_AJ(...) do { if (o < n - 1) { const int w_ = snprintf(out + o, (size_t)(n - o), __VA_ARGS__); o = w_ < 0 ? o : o + w_ > n - 1 ? n - 1 : o + w_; } } while (0)
    EQ_AJ("ÉQUIP. niveau %d, %s, ligne %d", EQM.niv, PARTY[EQM.m].name, H.sub_sel);
    if (EQM.niv >= EQN_LISTE) {
        const int nl = equip_liste(), cur = E.eq[EQM.m][EQM.sl];
        EQ_AJ(" · %s :", SLOT_NAME[EQM.sl]);
        for (int r = 0; r < nl; r++) {
            const int k = EQL[r];
            int a[ES_N], b[ES_N];
            const uint64_t sa = eq_stats(EQM.m, EQM.sl, k == cur ? EQ_AUCUN : cur, a), sb = eq_stats(EQM.m, EQM.sl, k, b);
            EQ_AJ("\n  %s%s%s", k == EQ_AUCUN ? "RETIRER" : ITEMS[k].name, k != EQ_AUCUN && k == cur ? " (porté : ce qu'il apporte)" : "", k != EQ_AUCUN && !eq_aff(EQM.m, k) ? " (pas pour son job)" : "");
            if (k != EQ_AUCUN) EQ_AJ(" [P%d, affinité %s]", E.w.eq_pal[k], AFF_NAME[eq_aff(EQM.m, k)]);
            for (int s = 0; s < ES_N; s++) if (a[s] != b[s]) EQ_AJ(" %s %d>%d", ES_NOM[s], a[s], b[s]);
            for (int s = 0; s < E.w.nsorts && s < 64; s++) if ((sa ^ sb) >> s & 1) EQ_AJ(" %c%s", (sb >> s & 1) ? '+' : '-', E.w.sorts[s].nom);
        }
    }
    if (EQM.niv == EQN_FICHE) EQ_AJ("\n  fiche : %s, réponse %s", EQM.it == EQ_AUCUN ? "RETIRER" : ITEMS[EQM.it].name, H.sub_sel ? "NON" : "OUI");
#undef EQ_AJ
    return o;
}

/* Pied de l'écran d'un panneau de quêtes : la distance et l'accroche de la ligne choisie, sur deux lignes dans la corde
   du cercle (plein écran : le pied des sous-menus, trois lignes) ; le bouton retour */
#if !ENG_DYN
static int wrap_chord(const ui_font_t *f, const char *text, float y0, float lh, char lines[6][96]);
#endif
static void pan_pied(const char *t) {
#if ENG_DYN
    sub_footer(t); return;
#endif
    char l[6][96];
    const ui_font_t *f = ui_font(FONT_DISP, 16);   /* Oxanium 16, la plus petite police du paquet */
    const int nl = wrap_chord(f, t, 360, 18, l);
    for (int i = 0; i < nl && i < 2; i++) ui_text(f, CX, 374 + i * 18, UI_CENTER, ui_rgb(DIM), 255, l[i]);
    hex_button(CX, 428, 72, 54, ui_rgb(DIM), 0, "back", 26);
    button(CX, 428, 34, A_BACK, 0);
}
static void draw_sub(void) {
    char t[96], v[32];
    int n = sub_rows();
    if (H.sub_sel >= n) H.sub_sel = n ? n - 1 : 0;
    if (H.sub_sel < H.sub_scroll) H.sub_scroll = H.sub_sel;
    if (H.sub_sel >= H.sub_scroll + ROWS) H.sub_scroll = H.sub_sel - ROWS + 1;
    switch (H.sub) {
    case SUB_SHOP: {
        if (SHOP.fiche >= 0) {   /* équipement à acheter : la fiche de comparaison avec le membre comparé */
            EQM.m = band_at(SHOP.m); EQM.sl = eq_slot(SHOP.fiche); EQM.it = SHOP.fiche;
            draw_equip_fiche();
            break;
        }
        snprintf(t, sizeof t, "%s · %d crédits", SHOP.mode == 4 ? ITEMS[SHOP.cible].name : SHOP.nom, E.credits);
        const char *titre_shop = SHOP.mode == 4 ? "ÉLÉMENT" : SHOP.mode == 3 ? "PIÈCES" : SHOP.mode == 2 ? (SHOP.forge ? "FORGE" : "AMÉLIORER") : SHOP.mode ? "VENDRE" : "ACHETER";
#if ENG_DYN
        entete("obj", titre_shop, t);
#else
        backdrop();
        ui_icon("obj", CX, 62, 34, C2, 255);
        ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, titre_shop);
        const int msg = H.now - H.toast_t < 1.8f && H.toast[0];
        ui_text(ui_fit(FONT_MONO, msg ? H.toast : t, 300, 20, 16), CX, 138, UI_CENTER, msg ? C2 : ui_rgb(DIM), 255, msg ? H.toast : t);
#endif
        if (SHOP.mode == 3) {   /* les pièces du véhicule de Sera (garage.c) */
            for (int s2 = 0; s2 < ROWS && H.sub_scroll + s2 < n; s2++) {
                const int k = shop_item(H.sub_scroll + s2);
                char lb[64];
                if (vk_max(k) > 1) snprintf(lb, sizeof lb, "%s +%d", vk_nom(k), E.veh_up[k] + 1); else snprintf(lb, sizeof lb, "%s", vk_nom(k));
                snprintf(v, sizeof v, "%d CR", vk_prix(k));
                list_row(H.sub_scroll + s2, s2, lb, v, ui_rgb(WHITE), -1);
            }
            sub_footer(n ? vk_desc(shop_item(H.sub_sel)) : "Tout est posé");
            break;
        }
        for (int s2 = 0; s2 < ROWS && H.sub_scroll + s2 < n; s2++) {
            const int k = shop_item(H.sub_scroll + s2);
            char lb[64];
            if (SHOP.mode == 2) snprintf(lb, sizeof lb, "%s +%d", ITEMS[k].name, eq_up_niv(k));
            else if (ITEMS[k].count) snprintf(lb, sizeof lb, "%s ×%d", ITEMS[k].name, ITEMS[k].count); else snprintf(lb, sizeof lb, "%s", ITEMS[k].name);
            snprintf(v, sizeof v, "%s%d CR", SHOP.mode == 1 ? "+" : "", shop_prix(k));
            if (SHOP.mode == 2 && SHOP.forge) { const int el = eq_el_de(k); snprintf(v, sizeof v, "%s", el >= 0 ? ELEM_NOM[el] : "—"); }   /* forge : l'élément de l'objet */
            if (SHOP.mode == 4) snprintf(v, sizeof v, "%s", ELEM_NOM[ITEMS[k].noyau - 1]);   /* noyau : son élément */
            char pt[96], pn[48], sous[64];
            const int porte = (SHOP.mode == 1 || SHOP.mode == 2) && porte_par(k, pn, sizeof pn);   /* équipement porté par la bande : « PORTÉ · SERA » sous son nom */
            const int eteinte = (SHOP.mode == 2 && !up_possede(k)) || (SHOP.mode == 4 && forge_refus(k)) || (SHOP.mode == 1 && ITEMS[k].count <= 0);   /* amélioration d'un objet qu'on n'a pas : ligne éteinte, sans prix ; noyau qui ne va pas sur l'objet ; exemplaire porté, rien au sac */
            if (eteinte) { if (SHOP.mode == 2 || SHOP.mode == 1) snprintf(v, sizeof v, "—"); row_eteinte = 1; }
            if (porte) { snprintf(sous, sizeof sous, "PORTÉ · %s", pn); row_sous = sous; }
            list_row_icon(H.sub_scroll + s2, s2, lb, v, ui_rgb(eteinte ? ETEINT_TEXTE : SHOP.mode != 2 && SHOP.mode != 4 && porteurs(k, pt, sizeof pt) == 0 ? DIM : WHITE), -1, item_icon(k));   /* tenue que personne de la bande ne porte : atténuée */
        }
        /* équipement : ses bonus (eq_resume) à la place de sa description ; tenue : qui la porte d'abord (porteurs) */
        const int ks = n ? shop_item(H.sub_sel) : -1;
        char rs[96] = "", pt[96], ft[200];
        if (ks >= 0 && SHOP.mode != 4) eq_resume(ks, rs, sizeof rs);
        char et[40];
        if (SHOP.mode == 4) {   /* forge, ÉLÉMENT : ce que deviendra l'objet, ou pourquoi ce noyau ne va pas */
            const char *rf = ks >= 0 ? forge_refus(ks) : NULL;
            if (ks < 0) snprintf(ft, sizeof ft, "Aucun noyau dans le sac");
            else if (rf) snprintf(ft, sizeof ft, "%s", rf);
            else snprintf(ft, sizeof ft, "%s %s · %d CR", eq_slot(SHOP.cible) == EQ_ARME ? "ÉLÉMENT" : "RÉSISTE", ELEM_NOM[ITEMS[ks].noyau - 1], forge_prix(SHOP.cible));
        }
        else if (ks >= 0 && SHOP.mode == 2 && !up_possede(ks)) snprintf(ft, sizeof ft, "Vous ne l'avez pas · %s", rs[0] ? rs : ITEMS[ks].desc);
        else if (ks >= 0 && SHOP.mode == 1 && ITEMS[ks].count <= 0 && porte_par(ks, et, sizeof et)) snprintf(ft, sizeof ft, "Porté par %s : à retirer d'abord", et);
        else if (ks >= 0 && SHOP.mode == 2 && SHOP.forge) snprintf(ft, sizeof ft, "%s · %s", elem_etiquette(ks, et, sizeof et) ? et : "SANS ÉLÉMENT", rs[0] ? rs : ITEMS[ks].desc);
        else if (ks >= 0 && porteurs(ks, pt, sizeof pt) >= 0) snprintf(ft, sizeof ft, "%s · %s", pt, rs[0] ? rs : ITEMS[ks].desc);
        else snprintf(ft, sizeof ft, "%s", ks >= 0 ? (rs[0] ? rs : ITEMS[ks].desc) : SHOP.mode == 2 ? "Rien à améliorer" : SHOP.mode ? "Rien à vendre ici" : "Plus rien à vendre");
        sub_footer(ft);
        break;
    }
    case SUB_ITEMS:
        snprintf(t, sizeof t, "%d dans le sac · %d crédits", items_count(), E.credits);
        sub_header(SUB_ITEMS, t);
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) {
            const item_t *it = &ITEMS[owned_item(H.sub_scroll + s)];
            snprintf(v, sizeof v, "× %d", it->count);
            list_row_icon(H.sub_scroll + s, s, it->name, v, ui_rgb(WHITE), -1, item_icon(owned_item(H.sub_scroll + s)));
        }
        {   /* tenue : qui la porte (porteurs), puis sa description */
            char pt[96], ft[200];
            const int ko = n ? owned_item(H.sub_sel) : -1;
            if (ko >= 0 && porteurs(ko, pt, sizeof pt) >= 0) snprintf(ft, sizeof ft, "%s · %s", pt, ITEMS[ko].desc);
            else snprintf(ft, sizeof ft, "%s", ko >= 0 ? ITEMS[ko].desc : "Le sac est vide");
            sub_footer(ft);
        }
        break;
    case SUB_TECH: {
        sub_header(SUB_TECH, "Hors combat : seuls les soins");
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) {
            int i = H.sub_scroll + s, mi, ti; tech_at(i, &mi, &ti);
            const sort_t *sp = ti < 0 ? &E.w.sorts[-1 - ti] : NULL;
            snprintf(t, sizeof t, "%s · %s", PARTY[mi].name, sp ? sp->nom : PARTY[mi].tech[ti].name);
            snprintf(v, sizeof v, "%d PM", sp ? sp->pm : PARTY[mi].tech[ti].mp);
            list_row(i, s, t, v, sp && fx_couleur(sp->fx) ? ui_rgb(fx_couleur(sp->fx)) : ui_rgb(membre_couleur(mi)), -1);
        }
        int mi, ti; tech_at(H.sub_sel, &mi, &ti);
        snprintf(t, sizeof t, "%s : %d / %d PM · MAG %d", PARTY[mi].name, PARTY[mi].mp, PARTY[mi].maxmp, PARTY[mi].mag);
        sub_footer(t);
        break;
    }
    case SUB_EQUIP: draw_equip(n); break;   /* membres, emplacements, liste, fiche (EQM.niv) */
    case SUB_STATUS: draw_statut(n); break;
    case SUB_EQUIPE: draw_equipe(n); break;
    case SUB_MAP: {   /* carte du menu (carte.c : plan zoomable, ses gestes et ses boutons) ; repères : objectif, guidage, pilote */
        carte_reperes_t rp = { 0 };
        rp.pilote_ok = E.ride >= 0 && pilote_possible(0);   /* en véhicule : un toucher sur la carte y envoie le pilote automatique */
        rp.obj = objective_pos(&rp.ox, &rp.oz);
        if (guide_ici()) { rp.guide = 1; rp.gx = E.w.guides[E.guide].x; rp.gz = E.w.guides[E.guide].z; }   /* lieu du guidage (de cette carte) */
        else if (panneau_guide(&rp.gx, &rp.gz)) rp.guide = 1;   /* ou celui d'un panneau de quêtes */
        if (pilote_actif() && !E.w.carte) { rp.pil = 1; rp.px = PIL.x; rp.pz = PIL.z; }   /* destination du pilote automatique */
        rp.obj_princ = E.qtrack < 0 || script_quest_type(E.qtrack) == 0;   /* repère de la quête suivie : cyan (principale) ou or */
        rp.tele_ok = E.ride < 0 && !CHX.on && brouillard_carte(E.w.carte) && !tele_refus();   /* à pied : un toucher propose le téléport */
        if (H.now - H.toast_t < 1.8f && H.toast[0]) rp.aide = H.toast;   /* refus du téléport, du pilote… sous le titre */
        carte_dessiner(C1, C2, &rp);
        break;
    }
    case SUB_SYSTEM: {
        if (H.vol_on) { vol_build(); break; }
        sub_header(SUB_SYSTEM, PORTRAIT ? "Glisser : défiler" : "Glisser sur le bord : défiler");
        const char *names[SYS_HEURE + 1] = { "SAUVEGARDER", "CHARGER", "TEINTE", "BROUILLARD", "TRAMAGE", "LUMINOSITÉ", "ZOOM", "MÉTÉO", "SON", "CYCLE", "TÉLÉPORTER", "GALERIE",
                                  "RENCONTRES", "DIRECTION", "CONDUITE", "COMMANDES", "CODE", "ÉVEIL", "QUITTER", "HEURE" };
        if (vol_permanent()) names[10] = "SAUT";   /* système solaire : le saut vers les mondes visités */
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) {
            const int i = H.sub_scroll + s, c = sys_ligne(i);
            const char *val = NULL;
            switch (c) {
            case 2: val = THEMES[E.theme].name; break;
            case 3: val = E.fog_on ? "OUI" : "NON"; break;
            case 4: val = E.dither_on ? "OUI" : "NON"; break;
            case 5: snprintf(v, sizeof v, "%d %%", 20 + H.bright * 20); val = v; break;
            case 6: snprintf(v, sizeof v, "%d %%", (int)lroundf(E.zoom * 100)); val = v; break;
            case 7: { static const char *W[3] = { "AUTO", "CLAIR", "PLUIE" }; val = W[E.weather % 3]; break; }
            case SYS_EVEIL: if (E.essai) val = E.eveil_essai ? "OUI" : "NON"; break;
            case SYS_CONDUITE: val = E.conduite == CONDUITE_ASSISTEE ? "ASSISTÉE" : "LIBRE"; break;
            case SYS_COMMANDES: val = COMMANDES_NOMS[E.commandes % 3]; break;
            case 13: { static const char *DI[5] = { "TRÈS DOUCE", "DOUCE", "MOYENNE", "VIVE", "TRÈS VIVE" }; val = DI[E.volant % 5]; break; }
            case 12: { static const char *RN[5] = { "NORMALES", "RARES", "TRÈS RARES", "AUCUNE", "FRÉQUENTES" }; val = RN[E.renc_mode % 5]; break; }
            case 9: { static const char *CYC[4] = { "AUTO", "JOUR", "NUIT", "RÉEL" }; val = CYC[E.cycle % 4]; break; }
            case 8: if (eng_volume() < 0.05f) val = "COUPÉ"; else { snprintf(v, sizeof v, "%d %%", (int)lroundf(eng_volume() * 100)); val = v; } break;
            case SYS_HEURE: { const int m = (int)(heure_vue() * 1440 + 0.5f) % 1440; snprintf(v, sizeof v, "%d H %02d", m / 60, m % 60); val = v; break; }
            }
            const eng_reglage_t *pr = c >= SYS_PLAT ? &E.cfg.regls_jeu[c - SYS_PLAT] : NULL;
            if (pr && pr->valeur) val = pr->valeur();
            list_row(i, s, pr ? pr->nom : c >= 0 && c <= SYS_HEURE ? names[c] : "", val, ui_rgb(WHITE), -1);
        }
        if (H.seq) snprintf(t, sizeof t, "Dernière sauvegarde : n° %d", H.seq);
        else snprintf(t, sizeof t, "Carte SD · deux emplacements alternés");
        sub_footer(t);
        break;
    }
    case SUB_PANNEAU: {   /* panneau de quêtes : ses quêtes ouvrables (nom, niveau conseillé) ; sous le titre, la distance de
                             la ligne choisie, en pied son accroche ; la toucher encore : le guidage vers qui la donne
                             (drawPanneau de l'émulateur) */
        const char *nom = panneau_nom(PN.k);
        char f[160], dt[16];
        if (!n) snprintf(t, sizeof t, "Rien de nouveau");
        else if (PN.d[H.sub_sel] < 1e8f) { dist_txt(PN.d[H.sub_sel], dt, sizeof dt); snprintf(t, sizeof t, "À %s d'ici", dt); }
        else snprintf(t, sizeof t, "Sur une autre carte");
#if ENG_DYN
        entete("quest", nom, t);
#else
        backdrop();
        ui_icon("quest", CX, 62, 34, ui_rgb(MQ_RGB), 255);
        ui_text(ui_fit(FONT_DISP, nom, 300, 30, 18), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, nom);
        { const int msg = H.now - H.toast_t < 1.8f && H.toast[0]; ui_text(ui_fit(FONT_MONO, msg ? H.toast : t, 300, 20, 16), CX, 138, UI_CENTER, msg ? C2 : ui_rgb(DIM), 255, msg ? H.toast : t); }
#endif
        for (int s2 = 0; s2 < ROWS && H.sub_scroll + s2 < n; s2++) {
            const int i = H.sub_scroll + s2, q = ouverture_quete(PN.o[i]), niv = script_quest_niveau(q);
            if (niv > 0) { snprintf(v, sizeof v, "NIV %d", niv); row_vcol = niveau_couleur(niv); }
            list_row(i, s2, script_quest_name(q), niv > 0 ? v : NULL, ui_rgb(WHITE), -1);
        }
        if (!n) snprintf(f, sizeof f, "Aucune quête à prendre ici pour l'instant.");
        else {
            if (PN.acc_l != H.sub_sel) {   /* l'accroche de la ligne choisie, lue une fois (carte SD) */
                const int ac = ouverture_accroche(PN.o[H.sub_sel]);
                PN.acc_l = H.sub_sel; PN.acc[0] = 0;
                if (ac != 65535) script_string(ac, PN.acc, sizeof PN.acc);
            }
            snprintf(f, sizeof f, "%s", PN.acc);
        }
        pan_pied(f);
        break;
    }
    case SUB_JOURNAL: {
        const char *const *ELEM = ELEM_NOM;
        if (H.jniv == 0) {
            sub_header(MENU_JOURNAL, "Ce que le Lien et la chasse ont gardé");
            snprintf(v, sizeof v, "%d/%d", echs_tous(), echs_total()); list_row(0, 0, "ÉCHOS", v, ui_rgb(WHITE), -1);
            snprintf(v, sizeof v, "%d/%d", best_nb(), E.w.nenemies); list_row(1, 1, "BESTIAIRE", v, ui_rgb(WHITE), -1);
            snprintf(v, sizeof v, "%d/%d", chss_faites(), CHSS_N); list_row(2, 2, "CHASSES", v, ui_rgb(WHITE), -1);
            sub_footer(H.sub_sel == 2 ? "Le tableau de Kess" : H.sub_sel ? "Les machines que vous avez croisées" : "Les voix entendues, vie par vie");
        } else if (H.jniv == 1) {
            sub_header(MENU_JOURNAL, "Les vies que le Lien a trouvées");
            for (int sl = 0; sl < ROWS && H.sub_scroll + sl < ECHS_N; sl++) {
                const int i = H.sub_scroll + sl, en = echs_entendus(i); const uint8_t *q = echs_vie(i);
                char nm[25]; memcpy(nm, q, 24); nm[24] = 0;
                snprintf(v, sizeof v, "%d/%d", en, q[24]);
                list_row(i, sl, en ? nm : "? ? ?", v, en == q[24] ? C2 : en ? ui_rgb(WHITE) : ui_rgb(0x5A6070), -1);
            }
            sub_footer("Toucher une vie pour réécouter ses voix");
        } else if (H.jniv == 2) {
            const uint8_t *q = echs_vie(H.jvie); char nm[25]; memcpy(nm, q, 24); nm[24] = 0;
            sub_header(MENU_JOURNAL, nm);
            for (int sl = 0; sl < ROWS && H.sub_scroll + sl < n; sl++) {
                const int i = H.sub_scroll + sl; char lb[32];
                snprintf(lb, sizeof lb, "VOIX %d", i + 1);
                list_row(i, sl, lb, "", ui_rgb(WHITE), -1);
            }
            sub_footer("Toucher pour réécouter");
        } else if (H.jniv == 4) {
            sub_header(MENU_JOURNAL, "Le tableau de Kess");
            for (int sl = 0; sl < ROWS && H.sub_scroll + sl < n; sl++) {
                const int i = H.sub_scroll + sl, k = chss_ligne(i);
                if (k < 0) break;
                char nm[25]; memcpy(nm, CHSS + 74 * k, 24); nm[24] = 0;
                const int fait = script_ev_fait(CHSS_EV(k));
                list_row(i, sl, nm, fait ? "ABATTUE" : "", fait ? ui_rgb(0x5A6070) : ui_rgb(WHITE), -1);
            }
            const int k = chss_ligne(H.sub_sel);
            if (k < 0) sub_footer("La carte de chasse se demande à Kess");
            else { char ind[49]; memcpy(ind, CHSS + 74 * k + 24, 48); ind[48] = 0; sub_footer(ind); }
        } else {
            sub_header(MENU_JOURNAL, "Bestiaire");
            for (int sl = 0; sl < ROWS && H.sub_scroll + sl < n; sl++) {
                const int i = H.sub_scroll + sl, k = best_ligne(i);
                if (k < 0) break;
                const int bat = E.best_bat[k >> 3] >> (k & 7) & 1;
                snprintf(v, sizeof v, "NIV %d", E.w.enemies[k].lvl);
                list_row(i, sl, E.w.persos[E.w.enemies[k].perso].nom, v, bat ? ui_rgb(WHITE) : ui_rgb(0x8A93A8), -1);
            }
            const int k = best_ligne(H.sub_sel);
            if (k < 0) sub_footer("Aucune machine pour l'instant");
            else if (!(E.best_bat[k >> 3] >> (k & 7) & 1)) sub_footer("Pas encore vaincue");
            else {   /* ses faiblesses : seulement une fois vaincue */
                char f[96]; int l = 0;
                const enemy_t *en = &E.w.enemies[k];
                l += snprintf(f + l, sizeof f - l, "Faible :");
                int nf = 0; for (int e = 0; e < MAGIE_N; e++) if (en->faible >> e & 1) { l += snprintf(f + l, sizeof f - l, "%s%s", nf++ ? ", " : " ", ELEM[e]); }
                if (!nf) l += snprintf(f + l, sizeof f - l, " aucun");
                int nr = 0; for (int e = 0; e < MAGIE_N; e++) if (e != MG_SOIN && (en->resiste >> e & 1)) { l += snprintf(f + l, sizeof f - l, "%s%s", nr++ ? ", " : " · Résiste : ", ELEM[e]); }
                sub_footer(f);
            }
        }
        break;
    }
    case SUB_QUESTS: {   /* la suivie (▶), celles en cours (étape / nombre d'étapes), les terminées ; toucher : la suivre */
        int rows[SC_MAX_QUESTS], act = 0;
        quest_rows(rows);
        for (int i = 0; i < n; i++) act += E.quests[rows[i]] != 255;
        snprintf(t, sizeof t, "%d en cours · %d terminée%s", act, n - act, n - act > 1 ? "s" : "");
        sub_header(SUB_QUESTS, t);
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) {
            const int i = H.sub_scroll + s, q = rows[i], st = E.quests[q], done = st == 255;
            char nm[48];
            snprintf(nm, sizeof nm, "%s%s", q == E.qtrack ? "▶ " : "", script_quest_name(q));
            /* niveau conseillé (couleur selon celui de l'équipe) ; l'étape passe dans le pied */
            const int niv = done ? 0 : script_quest_niveau(q);
            if (done) snprintf(v, sizeof v, "FINIE");
            else if (niv > 0) { snprintf(v, sizeof v, "NIV %d", niv); row_vcol = niveau_couleur(niv); }
            else snprintf(v, sizeof v, "%d/%d", st, script_quest_steps(q));
            list_row(i, s, nm, v, done ? ui_rgb(0x5A6070) : script_quest_type(q) == 0 ? C2 : ui_rgb(WHITE), -1);
        }
        if (!n) sub_footer("Aucune quête pour l'instant");
        else if (E.quests[rows[H.sub_sel]] == 255) sub_footer("Quête terminée");
        else {
            const int q = rows[H.sub_sel], st = E.quests[q];
            const char *o = script_quest_text(q, st);
            char f[160];
            snprintf(f, sizeof f, "%d/%d · %s", st, script_quest_steps(q), o[0] ? o : "Pas d'objectif");
            sub_footer(f);
        }
        break;
    }
    case SUB_CODE: {   /* code du contenu 18+ : quatre cases (un point par chiffre saisi), pavé de 1 à 9, effacer, 0 */
        sub_header(SUB_SYSTEM, "Entrer le code");
        const int n2 = (int)strlen(H.code);
        for (int i = 0; i < 4; i++) {
            const float x = CX + (i - 1.5f) * 44, y = 154;
            float q[12]; ui_chamfer_path(x - 17, y, 34, 40, 6, q);
            ui_poly(q, 6, ui_rgb(0x060A14), 237);
            ui_outline(q, 6, 2, i < n2 ? C2 : C1, 255);
            if (i < n2) ui_circle(x, y + 20, 6, C2, 255);
        }
        static const char *TOUCHES[11] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", NULL, "0" };
        for (int i = 0; i < 11; i++) {
            const float x = CX + (i % 3 - 1) * 64, y = 226 + (i / 3) * 46;
            hex_button(x, y, 52, 40, C1, 0, TOUCHES[i] ? NULL : "back", 20);
            if (TOUCHES[i]) ui_text(ui_font(FONT_DISP, 24), x, y + 9, UI_CENTER, ui_rgb(WHITE), 255, TOUCHES[i]);
            button(x, y, 22, A_CODE, i == 9 ? 10 : i == 10 ? 0 : i + 1);
        }
        sub_footer(NULL);
        break;
    }
    case SUB_GALERIE: {   /* les catégories (leur nombre d'images), puis les images ; celle de la ligne choisie derrière */
        const int img = H.galcat >= 0 && n ? GL[H.sub_sel] : -1;
        int iw, ih;
        const uint16_t *pal;
        const uint8_t *ix = img >= 0 ? img_ix(img, &iw, &ih, &pal, 1) : NULL;
        const float s = ix ? (UW - 2) / iw : 1, iy = (PORTRAIT ? UH * 0.42f : CY) - (ix ? ih * s / 2 : 0);
        if (H.galgrand) {   /* en grand : l'image seule, son rang */
            voile_ecran(ui_rgb(0), 255);
            if (ix) ui_image_pal(ix, pal, iw, ih, (UW - iw * s) / 2, iy, s, UW / 2, UH / 2, PORTRAIT ? UW + UH : 233);
            snprintf(t, sizeof t, "%d / %d · toucher : fermer", H.sub_sel + 1, n);
            ui_text(ui_font(FONT_MONO, 18), UW / 2, PORTRAIT ? UH - 40 : 444, UI_CENTER, ui_rgb(DIM), 200, t);
            break;
        }
        if (H.galcat < 0) snprintf(t, sizeof t, "GALERIE");
        else snprintf(t, sizeof t, "GALERIE · %s", GAL_CAT[H.galcat]);
        sub_header(SUB_SYSTEM, t);
        if (ix) { ui_image_pal(ix, pal, iw, ih, (UW - iw * s) / 2, iy, s, UW / 2, UH / 2, PORTRAIT ? UW + UH : 233); voile_ecran(ui_rgb(INK), 150); }
        static int tmp[GAL_MAX] ENG_FROID, tp[GAL_MAX] ENG_FROID;
        for (int sl = 0; sl < ROWS && H.sub_scroll + sl < n; sl++) {
            const int i = H.sub_scroll + sl;
            if (GL[i] < 0) {
                snprintf(v, sizeof v, "%d", gal_images(-1 - GL[i], tmp, tp));
                list_row(i, sl, GAL_CAT[-1 - GL[i]], v, ui_rgb(WHITE), -1);
                continue;
            }
            const int p = GLP[i];
            snprintf(t, sizeof t, "%s", p == 255 ? "SERA" : p >= 0 && p < E.w.npersos ? E.w.persos[p].nom : GAL_CAT[H.galcat]);
            snprintf(v, sizeof v, "%d", i + 1);
            const ui_filtre_t f0 = ui_filtre(NULL);   /* le nom du personnage dessiné, pas celui de son rôle (héroïne choisie) */
            list_row(i, sl, t, v, ui_rgb(WHITE), -1);
            ui_filtre(f0);
        }
        sub_footer(H.galcat < 0 ? "Les images du jeu" : "Toucher : voir en grand");
        break;
    }
    case SUB_TELE: {   /* les sortes de lieux, puis les lieux de la sorte choisie (leur carte, ou la distance sur celle-ci) */
        const int vol = vol_permanent();   /* système solaire : le saut vers les mondes visités, leur distance */
        if (vol) snprintf(t, sizeof t, "SAUT · MONDES VISITÉS");
        else if (H.tcat < 0) snprintf(t, sizeof t, "TÉLÉPORTER");
        else snprintf(t, sizeof t, "TÉLÉPORTER · %s", TELE_CAT[H.tcat % 3]);
        sub_header(SUB_SYSTEM, t);
        int nt;
        const uint8_t *p = tele_sec(&nt);
        for (int sl = 0; sl < ROWS && H.sub_scroll + sl < n; sl++) {
            const int i = H.sub_scroll + sl, r = TR[i];
            char nm[25];
            if (r >= SAUT0) {
                const astre_t *a = &E.w.astres[r - SAUT0];
                dist_txt(hypotf(a->ax - E.sera.x, a->az - E.sera.z), v, sizeof v);
                list_row(i, sl, a->nom, v, ui_rgb(WHITE), -1);
                continue;
            }
            if (r < 0) {   /* une sorte : ses lieux visités, sur leur nombre */
                int k = 0, kv = 0; for (int j = 0; j < nt; j++) if (p[j * 36 + 24] == -1 - r) { k++; kv += tele_vu(j); }
                snprintf(v, sizeof v, "%d / %d", kv, k);
                list_row(i, sl, TELE_CAT[-1 - r], v, ui_rgb(WHITE), -1);
                continue;
            }
            const uint8_t *q = p + r * 36;
            float x, z;
            memcpy(nm, q, 24); nm[24] = 0; memcpy(&x, q + 28, 4); memcpy(&z, q + 32, 4);
            if (q[25] != E.w.carte && q[25] < E.w.ncartes) snprintf(v, sizeof v, "%.10s", E.w.cartes[q[25]].nom);
            else dist_txt(hypotf(x - E.sera.x, z - E.sera.z), v, sizeof v);
            list_row(i, sl, nm, v, ui_rgb(WHITE), -1);
        }
        sub_footer(!n ? (vol ? "Aucun monde visité" : "Aucun lieu visité") : vol ? "Toucher : sauter jusque-là" : H.tcat < 0 ? "Les lieux déjà visités" : "Toucher : y aller");
        break;
    }
    case SUB_GUIDE: {   /* les sortes (villes, puis bâtiments, la plus proche d'abord), puis les lieux de la sorte choisie */
        char nm[48];
        if (H.gcat == -1 && guide_ici()) { guide_label(E.guide, " (", ")", nm, sizeof nm); snprintf(t, sizeof t, "Vers : %s", nm); }
        else snprintf(t, sizeof t, "%s", H.gcat == -1 ? guide_aide(0) : H.gcat >= GR_CAT ? E.w.guides[H.gcat - GR_CAT].nom : groupe_nom(H.gcat - GR_FAM));
        sub_header(SUB_GUIDE, t);
        for (int sl = 0; sl < ROWS && H.sub_scroll + sl < n; sl++) {
            const int i = H.sub_scroll + sl, c = GR[i];
            uint16_t col = ui_rgb(WHITE);
            if (c == GR_PILOTE) { snprintf(nm, sizeof nm, "ARRÊTER LE PILOTE"); col = ui_rgb(GUIDE_RGB); }
            else if (c == GR_STOP) { snprintf(nm, sizeof nm, "ARRÊTER LE GUIDAGE"); col = ui_rgb(DIM); }
            else if (c >= GR_CAT) snprintf(nm, sizeof nm, "%s", E.w.guides[c - GR_CAT].nom);
            else if (c >= GR_FAM) snprintf(nm, sizeof nm, "%s", groupe_nom(c - GR_FAM));
            else { guide_label(c, " · ", "", nm, sizeof nm); if (c == E.guide) col = ui_rgb(GUIDE_RGB); }
            if (GD[i] >= 0) dist_txt(GD[i], v, sizeof v);
            list_row(i, sl, nm, GD[i] >= 0 ? v : NULL, col, -1);
        }
        const int c = n ? GR[H.sub_sel] : 0;
        sub_footer(!n ? "Aucun lieu connu" : c == GR_PILOTE ? "Le véhicule s'arrête" : c == GR_STOP ? "Le chevron vert disparaît" : c >= GR_FAM ? "Toucher : voir la liste"
                   : E.ride >= 0 && pilote_possible(0) ? "Toucher : pilote automatique" : "Toucher : guider jusque-là");
        break;
    }
    }
    /* Barre de défilement à droite des lignes, quand la liste en a plus que l'écran n'en montre */
    if (H.vol_on) return;   /* cadran du son : pas de liste */
    if (PORTRAIT && H.sub != SUB_MAP && n > ROWS) {   /* plein écran : barre verticale au bord droit */
        const float y0 = ROW_Y0, hh = ROWS * ROW_STEP - (ROW_STEP - ROW_H), len = fmaxf(30, hh * ROWS / n);
        const float p = y0 + (hh - len) * H.sub_scroll / (n - ROWS), x = UW - 10 - ui_tx();
        ui_rect(x, y0, 4, hh, C1, 70);
        ui_rect(x - 1, p, 6, len, C2, 255);
    } else if (H.sub != SUB_MAP && n > ROWS) {
        const float a0 = -0.42f, a1 = 0.62f, len = fmaxf(0.1f, (a1 - a0) * ROWS / n);
        const float p = a0 + (a1 - a0 - len) * H.sub_scroll / (n - ROWS);
        ui_arc(CX, CY, 203, a0, a1, 3, C1, 70);
        ui_arc(CX, CY, 203, p, p + len, 5, C2, 255);
    }
}

/* Dialogue : panneau sur les deux tiers bas, texte de 28 px sur des lignes qui suivent la corde du cercle */
#define DLG_TOP 172.0f
int hud_cover_y(void) { return H.dialog && !BT.on ? (int)DLG_TOP : 0; }
static float chord_w(float y) {
    if (PORTRAIT) return UW - 64;   /* plein écran : toute la largeur */
    float d = y - CY; float v = 233 * 233 - d * d; return v > 0 ? 2 * sqrtf(v) - 64 : 0;
}

/* Coupe le texte en lignes dont la largeur suit la corde du cercle (une ligne : au plus 95 octets).
   Chaque ligne est délimitée dans le texte d'origine (début, longueur), sans copie intermédiaire. */
static int wrap_chord(const ui_font_t *f, const char *text, float y0, float lh, char lines[6][96]) {
    int n = 0;
    const char *start = text, *p = text, *last_fit = NULL;
    char buf[96];
    while (n < 6) {
        const char *e = ui_fin_mot(p);
        int len = (int)(e - start);
        if (len > 95) len = 95;
        memcpy(buf, start, (size_t)len); buf[len] = 0;
        int fits = ui_text_width(f, buf) <= chord_w(y0 + n * lh + 8) && (int)(e - start) <= 95;
        if (!fits && last_fit) {
            /* La ligne s'arrête au dernier mot qui tenait */
            int l = (int)(last_fit - start);
            memcpy(lines[n], start, (size_t)l); lines[n][l] = 0; n++;
            start = p = last_fit + 1; last_fit = NULL;
            continue;
        }
        if (!*e) { memcpy(lines[n], buf, (size_t)len + 1); n++; break; }
        last_fit = e; p = e + 1;
    }
    return n;
}

/* Panneau du bas (dialogue, choix) et son filet : de DLG_TOP au bas de l'écran rond ; écran rectangulaire (ENG_RECT) : sur
   toute la largeur et jusqu'au bas de l'image */
static void panneau_bas(int a) {
#if ENG_RECT
    ui_rect((float)-ui_ox, DLG_TOP, (float)ui_img_w, (float)(ui_img_h - ui_oy) - DLG_TOP, ui_rgb(0x080612), a);
    ui_line((float)-ui_ox, DLG_TOP, (float)(ui_img_w - ui_ox), DLG_TOP, 2, C2, 255);
#else
    ui_rect(0, DLG_TOP, ENG_SCREEN, ENG_SCREEN - DLG_TOP, ui_rgb(0x080612), a);
    ui_line(0, DLG_TOP, ENG_SCREEN, DLG_TOP, 2, C2, 255);
#endif
}
/* Choix : panneau du dialogue, réponses en rangées (la surlignée en teinte 2) ; toucher une fois surligne, deux fois
   répond (drawChoice de l'émulateur) */
static void draw_choice(void) {
#if ENG_DYN
    portrait_choice(); return;
#endif
    panneau_bas(245);
    char ti[48]; snprintf(ti, sizeof ti, "%s", CHX.titre[0] ? CHX.titre : "CHOISIR");   /* sans ▸ : absent des polices du paquet */
    ui_text(ui_font(FONT_MONO, 18), CX, DLG_TOP + 34, UI_CENTER, C2, 255, ti);
    /* Confirmation (vol, effraction) : ce qui est en jeu sous le titre, les réponses un peu plus bas */
    const int nt = (CHX.texte[0][0] != 0) + (CHX.texte[1][0] != 0);
    for (int k = 0, l = 0; k < 2; k++) if (CHX.texte[k][0]) {
        const ui_font_t *f = ui_fit(k ? FONT_MONO : FONT_BODY, CHX.texte[k], 330, k ? 16 : 20, 14);
        ui_text(f, CX, DLG_TOP + 62 + 24 * l++, UI_CENTER, k ? C1 : ui_rgb(WHITE), 235, CHX.texte[k]);
    }
    /* Réponses sous la dernière ligne de texte (08/11/2026 : trois réponses et deux lignes, la forge, se chevauchaient) ;
       serrées s'il le faut (drawChoice de l'émulateur) */
    float y0 = 260 - (CHX.n - 2) * 22 + (nt ? 18 : 0), pas = 52, h = 44;
    const float ymin = nt ? DLG_TOP + 62 + 24 * (nt - 1) + 10 : 0;
    if (y0 < ymin) { y0 = ymin; if (CHX.n >= 3) { pas = 48; h = 40; } }
    const float w = 340, x = CX - w / 2;
    for (int k = 0; k < CHX.n; k++) {
        const float y = y0 + k * pas;
        const int sel = k == CHX.sel;
        const float q[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
        ui_rect(x, y, w, h, sel ? ui_rgb(0x28061E) : ui_rgb(0x060E1A), 242);
        ui_outline(q, 4, sel ? 2 : 1, sel ? C2 : C1, 255);
        ui_text(ui_fit(FONT_DISP, CHX.opts[k], w - 24, 22, 16), CX, y + h - 14, UI_CENTER, ui_rgb(WHITE), 255, CHX.opts[k]);
        button_rect(x, y, w, h, A_CHOICE, k);
    }
}

static void draw_dialog(void) {
#if ENG_DYN
    portrait_dialog(); return;
#endif
    const char *who = D.who[H.line], *text = dlg_texte();   /* (le nom de who : par le filtre de ui.c) */
    /* Caractères affichés ; toucher pendant le défilement met line_t0 à -1e9 : borné, sinon l'entier déborde (négatif) et
       la première ligne reste vide */
    const float shown_f = (H.now - H.line_t0) / 0.028f;
    int shown = shown_f > 100000 ? 100000 : (int)shown_f;
    for (int k = 0; k < 2; k++) {
        float x = k ? 354 : 112, y = 112, r = 56;
        int talking = (k == 0) == (strcmp(who, D.right) != 0);
        int a = talking ? 255 : 102;
        ui_circle(x, y, r, ui_rgb(0x080612), 255);   /* opaque : remplissage direct, sans mélange par pixel */
        ui_image(portrait_px[k], portrait_mask[k], 24, 24, x - r, y - r + 6, r * 2 / 24, x, y, r - 1);
        if (!talking) ui_circle(x, y, r, ui_rgb(0x080612), 150);
        if (talking) { ui_ring(x, y, r, 8, C2, 60); ui_ring(x, y, r, 3, C2, 255); } else ui_ring(x, y, r, 2, C1, a);
    }
    panneau_bas(255);   /* opaque : 50 ms de mélange en moins par image */
    int right = !strcmp(who, D.right);
    ui_text(ui_font(FONT_DISP, 26), right ? 420 : 46, DLG_TOP + 36, right ? UI_RIGHT : UI_LEFT, C2, 255, who);
    const ui_filtre_t f0 = ui_filtre(NULL);   /* texte déjà filtré (dlg_texte) */
    const ui_font_t *f = ui_font(FONT_BODY, 28);
    float lh = 38, top = DLG_TOP + 82;
    char lines[6][96];
    int n = wrap_chord(f, text, top, lh, lines);
    if (n > 4) { f = ui_font(FONT_BODY, 24); lh = 32; top = DLG_TOP + 78; n = wrap_chord(f, text, top, lh, lines); }
    int left = shown, total = 0;
    for (int k = 0; k < n; k++) {
        /* Lettre par lettre ; chaque ligne garde sa place (calée sur la ligne entière) */
        float full = ui_text_width(f, lines[k]);
        char part[96]; int i = 0, cps = 0;
        while (lines[k][i] && cps < left) { i++; while ((lines[k][i] & 0xC0) == 0x80) i++; cps++; }
        memcpy(part, lines[k], i); part[i] = 0;
        int len = 0; for (const char *s = lines[k]; *s; s++) if ((*s & 0xC0) != 0x80) len++;
        left -= len + 1; total += len + 1;
        ui_text(f, CX - full / 2, top + k * lh, UI_LEFT, ui_rgb(0xE8F6FF), 255, part);
    }
    ui_filtre(f0);
    if (shown >= total - 1) {
        int a = (int)(255 * (0.6f + 0.4f * sinf(H.now * 1000 / 200)));
        ui_text(ui_font(FONT_MONO, 20), CX, 442, UI_CENTER, C1, a, "▼ TOUCHER");
    }
}


#if ENG_DYN
/* ---------- Plein écran (téléphone, portrait) : écrans refaits pour l'image entière (UW × UH) ----------
   Maquette : engine/maquettes/interface_portrait.html. En haut ce qu'on lit (lieu, boussole, quête) ; en bas, sous le
   pouce, ce qu'on touche (bouton d'action, bande de rotation de la caméra, entrées du menu, réponses). */
#define DLG_H 420.0f                    /* panneau du dialogue */
static void cadre_bouton(float x, float y, float w, float h, int sel, uint16_t col) {
    float q[12]; ui_chamfer_path(x, y, w, h, 12, q);
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.28f : 0.10f), 235);
    if (sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 2, col, 200);
}
static void jauge(float x, float y, float w, float h, float r, uint16_t col) {
    ui_rect(x, y, w, h, ui_rgb(WHITE), 26);
    if (r > 0) ui_rect(x, y, w * (r > 1 ? 1 : r), h, col, 255);
}
static void portrait_rond(int k, float x, float y, float r, uint16_t col) {
    ui_circle(x, y, r, ui_rgb(0x080612), 255);
    ui_image(portrait_px[k], portrait_mask[k], 24, 24, x - r, y - r + r / 9, r * 2 / 24, x, y, r - 1);
    ui_ring(x, y, r, 3, col, 255);
}
static void bouton_menu(void) {
    const float x = UW - 96, y = 18;
    cadre_bouton(x, y, 76, 76, H.menu, H.menu ? C2 : C1);
    for (int k = 0; k < 3; k++) ui_rect(x + 20, y + 25 + k * 12, 36, 4, ui_rgb(WHITE), 255);
    button_rect(x, y, 76, 76, A_MENU_BTN, 0);
}
/* Exploration : barre du haut, quête suivie, état de Sera, bouton d'action, bande de rotation */
static void portrait_world(void) {
    const uint16_t ink = ui_rgb(INK);
    char t[96];
    ui_rect(0, 0, UW, 76, ink, 150); ui_rect(0, 76, UW, 36, ink, 60);
    if (E.in.on && E.in.etage) snprintf(t, sizeof t, "ÉTAGE %d", E.in.etage); else snprintf(t, sizeof t, "%s", E.in.on ? "INTÉRIEUR" : E.w.ile[0] ? E.w.ile : "SECTEUR 6");
    ui_text(ui_font(FONT_DISP, 26), 24, 46, UI_LEFT, ui_rgb(WHITE), 255, t);
    snprintf(t, sizeof t, "%.1f · %.1f", E.sera.x, E.sera.z);
    ui_text(ui_font(FONT_MONO, 18), 24, 76, UI_LEFT, ui_rgb(0x7F93A8), 255, t);
    /* Boussole en bande : les points cardinaux défilent avec la caméra, le nord en teinte 2 */
    const float bx = UW - 200;
    ui_line(bx - 88, 72, bx + 88, 72, 2, C1, 90);
    static const char *const CARD[4] = { "N", "E", "S", "O" };
    for (int k = 0; k < 4; k++) {
        float a = (k == 0 ? PI_F : k == 2 ? 0 : k == 1 ? 3 * PI_F / 2 : PI_F / 2) - E.yaw;   /* nord vers -z, sud vers +z, est vers -x (comme la carte : CARTE_X de carte.c) */
        a = atan2f(sinf(a), cosf(a));
        if (fabsf(a) > 1.3f) continue;
        ui_text(ui_font(FONT_DISP, k ? 20 : 24), bx + a / (PI_F / 2) * 70, 60, UI_CENTER, k ? ui_rgb(DIM) : C2, (int)(255 * (1 - fabsf(a) / 1.6f)), CARD[k]);
    }
    ui_line(bx, 66, bx, 78, 2, C1, 255);
    bouton_menu();
    /* Quête suivie et distance de son objectif */
    if (E.qtrack >= 0 && E.qtrack < script_quest_count() && !H.dialog) {
        const int q = E.qtrack, st = E.quests[q] >= 1 && E.quests[q] < 255 ? E.quests[q] - 1 : 0;
        const char *txt = script_quest_text(q, st);
        float ox, oz;
        if (objective_pos(&ox, &oz)) snprintf(t, sizeof t, "%s · %d m", txt[0] ? txt : script_quest_name(q), (int)hypotf(ox - E.sera.x, oz - E.sera.z));
        else snprintf(t, sizeof t, "%s", txt[0] ? txt : script_quest_name(q));
        const ui_font_t *f = ui_fit(FONT_DISP, t, UW - 90, 20, 16);
        const float w = ui_text_width(f, t) + 44;
        ui_rect(24, 120, w, 40, ui_rgb(0x080612), 180);
        ui_rect(24, 120, 4, 40, C2, 255);
        ui_text(f, 40, 147, UI_LEFT, ui_rgb(WHITE), 255, t);
    }
    if (H.dialog) return;
    /* État de Sera (ou de l'héroïne choisie : son portrait, le nom du joueur). En véhicule (02/11/2026), plus petit et
       au-dessus des commandes de conduite, qui prennent le bas de l'écran (sous les pouces) */
    const int veh = E.ride >= 0;
    const float sy = veh ? UH - 350 : UH - BANDE_H - 140, pr = veh ? 26 : 32, jw = veh ? 170 : 250;
    portrait_rond(0, 24 + pr, sy + pr, pr, C2);
    snprintf(t, sizeof t, "%s", PARTY[0].name);
    ui_text(ui_font(FONT_DISP, veh ? 20 : 22), 36 + 2 * pr, sy + pr - 6, UI_LEFT, ui_rgb(WHITE), 255, t);
    snprintf(t, sizeof t, "NIV. %d", E.level);
    ui_text(ui_font(FONT_MONO, 16), 36 + 2 * pr, sy + pr + 18, UI_LEFT, ui_rgb(DIM), 255, t);
    const float jy = sy + 2 * pr + 12;
    jauge(24, jy, jw, veh ? 10 : 12, PARTY[0].max > 0 ? (float)PARTY[0].hp / PARTY[0].max : 0, C2);
    jauge(24, jy + (veh ? 16 : 18), jw, veh ? 7 : 8, PARTY[0].maxmp > 0 ? (float)PARTY[0].mp / PARTY[0].maxmp : 0, C1);
    {   /* coup de la pluie de cendre : la jauge des PV pulse en gris cendre (comme l'arc de l'écran rond) */
        const float tc = E.clock - E.cendre_coup;
        if (E.cendre_coup > 0 && tc >= 0 && tc < 0.6f && PARTY[0].max > 0) ui_rect(24, jy, jw * fminf(1, (float)PARTY[0].hp / PARTY[0].max), veh ? 10 : 12, ui_rgb(0xC8C4BC), (int)(200 * (1 - tc / 0.6f)));
    }
    /* Bouton d'action : ce que propose la bulle au-dessus de la cible, sous le pouce */
    const char *act = NULL; int a = 0; uint16_t col = C1;
    if (!CR.on && !RM.on && !CHX.on && !BUT.on && E.coffre >= 0 && E.ride < 0 && coffre_avant()) {   /* un coffre (coffres.c), plus près que l'interlocuteur */
        act = "FOUILLER"; a = A_COFFRE; col = C2;
    } else if (!CR.on && !RM.on && !CHX.on && !BUT.on && E.panneau >= 0 && E.ride < 0 && panneau_avant()) {   /* un panneau de quêtes (panneaux.c) */
        act = "LIRE"; a = A_PANNEAU; col = ui_rgb(MQ_RGB);
    } else if (!CR.on && !RM.on && !CHX.on && !BUT.on && ile_dormir_k() >= 0 && E.ride < 0 && ile_dormir_avant()) {   /* la couche d'un abri de l'île (ile.c) */
        act = "DORMIR"; a = A_DORMIR; col = ui_rgb(ABRI_RGB);
    } else if (!CR.on && !RM.on && near_npc() && E.scr_npc.vis && !(E.ride >= 0 && (fabsf(E.ride_v) > 0.3f || E.ride_phase))) {
        act = E.ride >= 0 ? "DESCENDRE" : talk_is_vehicle() ? "CONDUIRE" : "PARLER"; a = A_TALK;
    } else if (!CR.on && !RM.on && !CHX.on && E.porte >= 0 && E.scr_sera.vis) {
        const int ok = int_porte_ouverte(E.porte);
        act = ok ? "ENTRER" : "FORCER"; a = A_PORTE; col = ok ? C1 : C2;
    }
    if (act) {
        const float bw = 170, bh = veh ? 84 : 96, x = UW - 24 - bw, y = sy + (veh ? 2 : 8);
        cadre_bouton(x, y, bw, bh, 1, col);
        ui_text(ui_fit(FONT_DISP, act, bw - 20, 26, 18), x + bw / 2, y + bh / 2 + 9, UI_CENTER, ui_rgb(WHITE), 255, act);
        button_rect(x, y, bw, bh, a, 0);
    }
    /* Bande de rotation : graduations qui défilent avec la caméra (pas en véhicule : la caméra le suit) */
    if (E.ride < 0 && !CR.on && !RM.on) {
        ui_rect(0, UH - BANDE_H, UW, BANDE_H, ink, 110);
        const float cy = UH - BANDE_H / 2, pas = 24, off = fmodf(E.yaw * 160, pas);
        for (int i = -12; i <= 12; i++) {
            const float x = UW / 2 + i * pas + off;
            if (x < 20 || x > UW - 20) continue;
            ui_rect(x - 1, cy - 13, 2, 26, C1, i * i < 169 ? 150 - (i < 0 ? -i : i) * 9 : 30);
        }
        const float f[8] = { UW / 2 - 34, cy - 21, UW / 2 + 34, cy - 21, UW / 2 + 34, cy + 21, UW / 2 - 34, cy + 21 };
        ui_outline(f, 4, 2, H.rim_on ? C2 : C1, 255);
    }
}
/* Menu : l'équipe en haut, les 8 entrées en grille de 2 × 4 en bas */
static void portrait_menu(void) {
    char t[64];
    voile_ecran(ui_rgb(INK), 222);
    ui_text(ui_font(FONT_DISP, 30), 24, 54, UI_LEFT, ui_rgb(WHITE), 255, "MENU");
    const int s = (int)E.temps_jeu;
    snprintf(t, sizeof t, "%d CR · %d:%02d", E.credits, s / 3600, s / 60 % 60);
    ui_text(ui_font(FONT_MONO, 18), UW - 112, 54, UI_RIGHT, ui_rgb(DIM), 255, t);
    bouton_menu();
    float y = 112;
    for (int m = 0; m < NMEMB; m++) {
        if (!in_team(m)) continue;
        const uint16_t mc = ui_rgb(membre_couleur(m));
        cadre_bouton(24, y, UW - 48, 96, m == 0, m == 0 ? C2 : C1);
        if (m == 0) portrait_rond(0, 72, y + 48, 32, mc);
        else { ui_circle(72, y + 48, 32, ui_mix(ui_rgb(INK), mc, 0.6f), 255); ui_ring(72, y + 48, 32, 3, mc, 255); }
        ui_text(ui_font(FONT_DISP, 22), 120, y + 36, UI_LEFT, ui_rgb(WHITE), 255, PARTY[m].name);
        snprintf(t, sizeof t, "NIV. %d", E.level);
        ui_text(ui_font(FONT_MONO, 16), UW - 44, y + 36, UI_RIGHT, ui_rgb(DIM), 255, t);
        jauge(120, y + 52, UW - 168, 10, PARTY[m].max > 0 ? (float)PARTY[m].hp / PARTY[m].max : 0, C2);
        jauge(120, y + 68, UW - 168, 7, PARTY[m].maxmp > 0 ? (float)PARTY[m].mp / PARTY[m].maxmp : 0, C1);
        y += 108;
    }
    static const int ORDRE[NMENU] = { 0, 1, 2, 3, 8, 4, 6, 7, 5 };   /* ÉQUIPE après STATUT, SYSTÈME en dernier */
    const int nl = (NMENU + 1) / 2;   /* lignes de la grille (cinq depuis ÉQUIPE) */
    const float gap = 14, gw = (UW - 48 - gap) / 2, gh = nl > 4 ? 84 : 96, y0 = UH - 40 - nl * gh - (nl - 1) * gap;
    for (int i = 0; i < NMENU; i++) {
        const int k = ORDRE[i];
        const float x = 24 + (i & 1) * (gw + gap), yy = y0 + (i >> 1) * (gh + gap);
        const int sel = k == H.sel;
        cadre_bouton(x, yy, gw, gh, sel, sel ? C2 : C1);
        ui_icon(MENU_ICON[k], x + 34, yy + gh / 2, 26, sel ? C2 : C1, 255);
        ui_text(ui_fit(FONT_DISP, MENU_NAME[k], gw - 74, 22, 16), x + 62, yy + gh / 2 + 8, UI_LEFT, ui_rgb(WHITE), 255, MENU_NAME[k]);
        button_rect(x, yy, gw, gh, A_MENU_GO, k);
    }
    menu_detail(H.sel, t, sizeof t);
    ui_text(ui_fit(FONT_MONO, t, UW - 48, 18, 14), UW / 2, y0 - 20, UI_CENTER, ui_rgb(DIM), 255, t);
}
/* Dialogue : panneau sur le tiers bas, toute la largeur ; portraits de 64 px de rayon au-dessus */
static void portrait_dialog(void) {
    const char *who = D.who[H.line], *text = dlg_texte();   /* (le nom de who : par le filtre de ui.c) */
    const float shown_f = (H.now - H.line_t0) / 0.028f, top0 = UH - DLG_H;
    int shown = shown_f > 100000 ? 100000 : (int)shown_f;
    for (int k = 0; k < 2; k++) {
        const float x = k ? UW - 88 : 88, y = top0 - 80, r = 64;
        const int talking = (k == 0) == (strcmp(who, D.right) != 0);
        if (!portrait_mask[k][24 * 12 + 12] && !talking) continue;   /* pas de portrait à droite avant le premier interlocuteur */
        portrait_rond(k, x, y, r, talking ? C2 : C1);
        if (!talking) ui_circle(x, y, r, ui_rgb(0x080612), 150);
    }
    ui_rect(0, top0, UW, DLG_H, ui_rgb(0x080612), 245);
    ui_line(0, top0, UW, top0, 2, C2, 255);
    const int right = !strcmp(who, D.right);
    const ui_font_t *fn = ui_font(FONT_DISP, 24);
    const float nw = ui_text_width(fn, who) + 32, nx = right ? UW - 24 - nw : 24;
    ui_rect(nx, top0 - 18, nw, 36, C1, 255);
    ui_text(fn, nx + nw / 2, top0 + 8, UI_CENTER, ui_rgb(INK), 255, who);
    const ui_filtre_t f0 = ui_filtre(NULL);   /* texte déjà filtré (dlg_texte) */
    const ui_font_t *f = ui_font(FONT_BODY, 28);
    float lh = 38, top = top0 + 70;
    char lines[6][96];
    int n = wrap_chord(f, text, top, lh, lines);
    if (n > 6) { f = ui_font(FONT_BODY, 24); lh = 32; n = wrap_chord(f, text, top, lh, lines); }
    int left = shown, total = 0;
    for (int k = 0; k < n; k++) {
        char part[96]; int i = 0, cps = 0;
        while (lines[k][i] && cps < left) { i++; while ((lines[k][i] & 0xC0) == 0x80) i++; cps++; }
        memcpy(part, lines[k], i); part[i] = 0;
        int len = 0; for (const char *s = lines[k]; *s; s++) if ((*s & 0xC0) != 0x80) len++;
        left -= len + 1; total += len + 1;
        ui_text(f, 32, top + k * lh, UI_LEFT, ui_rgb(0xE8F6FF), 255, part);
    }
    ui_filtre(f0);
    ui_text(ui_font(FONT_MONO, 16), 32, UH - 44, UI_LEFT, ui_rgb(DIM), 255, "TOUCHER : SUITE · MAINTENIR : PASSER");
    if (shown >= total - 1) {
        const int a = (int)(255 * (0.6f + 0.4f * sinf(H.now * 1000 / 200)));
        ui_text(ui_font(FONT_MONO, 26), UW - 40, UH - 44, UI_CENTER, C1, a, "▼");
    }
}
/* Choix : réponses empilées sur toute la largeur, dans le panneau du dialogue */
static void portrait_choice(void) {
    const float top0 = UH - DLG_H, w = UW - 64, x = 32, hb = 72, gap = 12;
    ui_rect(0, top0, UW, DLG_H, ui_rgb(0x080612), 245);
    ui_line(0, top0, UW, top0, 2, C2, 255);
    char ti[48]; snprintf(ti, sizeof ti, "%s", CHX.titre[0] ? CHX.titre : "CHOISIR");   /* sans ▸ : absent des polices du paquet */
    ui_text(ui_font(FONT_MONO, 18), 32, top0 + 40, UI_LEFT, C2, 255, ti);
    for (int k = 0, l = 0; k < 2; k++) if (CHX.texte[k][0])   /* confirmation : ce qui est en jeu */
        ui_text(ui_fit(k ? FONT_MONO : FONT_BODY, CHX.texte[k], w, k ? 18 : 22, 14), 32, top0 + 80 + 30 * l++, UI_LEFT, k ? C1 : ui_rgb(WHITE), 235, CHX.texte[k]);
    const float y0 = UH - 40 - CHX.n * hb - (CHX.n - 1) * gap;
    for (int k = 0; k < CHX.n; k++) {
        const float y = y0 + k * (hb + gap);
        const int sel = k == CHX.sel;
        cadre_bouton(x, y, w, hb, sel, sel ? C2 : C1);
        ui_text(ui_fit(FONT_DISP, CHX.opts[k], w - 32, 22, 16), UW / 2, y + hb / 2 + 8, UI_CENTER, ui_rgb(WHITE), 255, CHX.opts[k]);
        button_rect(x, y, w, hb, A_CHOICE, k);
    }
}

/* ---------- Course en portrait (02/11/2026, demande de l'utilisateur : « l'UI en course n'a pas été corrigée sur la
   version android » ; avant, celle de la montre dans le cadre de 466 centré, sous la barre du haut et l'état de Sera) ----------
   En haut, ce qu'on lit : un bandeau avec la place (contre-la-montre : l'écart au fantôme ; tour rapide : le meilleur
   tour), le tour, le temps (et le temps à battre, ou le meilleur tour), le bouton de pause à la place de celui du menu ;
   le plan de la piste dessous, à droite. Au milieu : les feux du départ et les annonces (tour, dernier tour, record,
   élimination) dans un bandeau sur toute la largeur, BOOST et SENS INVERSE au-dessus du véhicule. En bas, sous les
   pouces : les commandes de conduite (draw_world_hud : GAZ_X…, JOY_X…), la vitesse entre elles, le tir au-dessus du
   volant. Rien de l'exploration (barre du haut, état de Sera, quête suivie, guidage). Accélération : halo sur les bords de
   toute l'image (ui_halo_bord) et traits de vitesse vers le milieu. Arrivée : le classement sur toute la largeur. Menu de
   course, pause : le cadre des sous-menus (en-tête et retour en haut, lignes sur toute la largeur, actions en bas) */
#define CRS_BANDE 112.0f   /* hauteur du bandeau du haut */
#define CRS_PIED 148.0f    /* pied des menus de course : RETOUR et l'action */
static float cy3d(void) { return ui_img_h / 2.0f - ui_oy; }   /* milieu de la 3D (le véhicule du joueur) dans le canevas */
/* Texte sur la 3D : une ombre sombre décalée, puis le texte */
static void texte_ombre(const ui_font_t *f, float x, float y, int align, uint16_t c, int a, const char *s) {
    ui_text(f, x + 2, y + 2, align, ui_rgb(INK), a * 3 / 4, s);
    ui_text(f, x, y, align, c, a, s);
}
/* Bandeau sur toute la largeur, centré sur y (feux, annonces) : fond sombre, filets de couleur c */
static void crs_bandeau(float y, float h, uint16_t c, int a) {
    ui_rect(0, y - h / 2, UW, h, ui_rgb(INK), a * 200 / 255);
    ui_line(0, y - h / 2, UW, y - h / 2, 2, c, a);
    ui_line(0, y + h / 2, UW, y + h / 2, 2, c, a);
}
/* Vitesse : cadran de 270° (comme celui du son), chiffres et KM/H au milieu */
static void crs_compteur(float x, float y, const racer_t *j) {
    char t[16];
    const float k = j->vmax > 0 ? fminf(1, fabsf(j->v) / j->vmax) : 0, a0 = 0.75f * PI_F, sp = 1.5f * PI_F;
    ui_circle(x, y, 52, ui_rgb(INK), 120);
    ui_arc(x, y, 58, a0, a0 + sp, 7, ui_rgb(WHITE), 40);
    ui_arc(x, y, 58, a0, a0 + sp * fmaxf(0.01f, k), 7, C1, 255);
    snprintf(t, sizeof t, "%d", (int)lroundf(fabsf(j->v) * 25));
    ui_text(ui_font(FONT_DISP, 30), x, y + 10, UI_CENTER, ui_rgb(WHITE), 255, t);
    ui_text(ui_font(FONT_MONO, 18), x, y + 36, UI_CENTER, C1, 255, "KM/H");
}
/* Plan de la piste dans le rectangle (x, y, w, h) : le tracé, les adversaires, le fantôme, le joueur en teinte 2 */
static void crs_plan(float x, float y, float w, float h) {
    const racer_t *j = &CR.r[CR.n - 1];
    ui_rect(x, y, w, h, ui_rgb(INK), 130);
    const float m = 12, sc = fminf((w - 2 * m) / fmaxf(1, PISTE.x1 - PISTE.x0), (h - 2 * m) / fmaxf(1, PISTE.z1 - PISTE.z0));
    const float mx = x + w / 2 + (PISTE.x0 + PISTE.x1) / 2 * sc, mz = y + h / 2 - (PISTE.z0 + PISTE.z1) / 2 * sc;   /* x en miroir (CARTE_X) */
    const int step = PISTE.n > 64 ? PISTE.n / 64 : 1;
    for (int i = 0; i < PISTE.n; i += step) {
        const int e = i + step < PISTE.n ? i + step : 0;
        ui_line(mx - PISTE.X[i] * sc, mz + PISTE.Z[i] * sc, mx - PISTE.X[e] * sc, mz + PISTE.Z[e] * sc, 3, C1, 150);
    }
    for (int i = 0; i < CR.n; i++) if (!CR.r[i].joueur && !CR.r[i].elim) ui_circle(mx - CR.r[i].x * sc, mz + CR.r[i].z * sc, 4, ui_rgb(0xE8F6FF), 255);
    { const fantome_t *g = course_fantome(); if (g) ui_circle(mx - g->x * sc, mz + g->z * sc, 4, ui_rgb(0x60D8FF), 170); }
    ui_circle(mx - j->x * sc, mz + j->z * sc, 6, C2, 255);
}
/* Arrivée : le classement sur toute la largeur, le joueur surligné ; CONTINUER en bas (tout toucher continue : tap) */
static void crs_arrivee(void) {
    int order[COURSE_N];
    course_rank(order);
    const racer_t *j = &CR.r[CR.n - 1];
    char t[48];
    const float pas = 58, top = fmaxf(CRS_BANDE, cy3d() - 300), h = 104 + CR.n * pas + (CR.chrono > 0 ? 44 : 0);
    voile_ecran(ui_rgb(INK), 110);
    crs_bandeau(top + h / 2, h, C2, 255);
    texte_ombre(ui_font(FONT_DISP, 40), UW / 2, top + 60, UI_CENTER, ui_rgb(0xE8F6FF), 255, "ARRIVÉE");
    for (int i = 0; i < CR.n; i++) {
        const racer_t *r = &CR.r[order[i]];
        const float y = top + 92 + i * pas;
        const uint16_t c = r->joueur ? C2 : ui_rgb(0xE8F6FF);
        if (r->joueur) { float q[12]; ui_chamfer_path(16, y, UW - 32, pas - 8, 10, q); ui_poly(q, 6, ui_mix(ui_rgb(INK), C2, 0.22f), 235); ui_outline(q, 6, 2, C2, 255); }
        snprintf(t, sizeof t, "%d. %s", i + 1, nom_course(r));
        ui_text(ui_fit(FONT_DISP, t, UW - 220, 26, 18), 32, y + pas / 2 + 5, UI_LEFT, c, 255, t);
        if (r->fin >= 0) chrono(r->fin, t, sizeof t); else snprintf(t, sizeof t, "--");
        ui_text(ui_font(FONT_DISP, 26), UW - 32, y + pas / 2 + 5, UI_RIGHT, c, 255, t);
    }
    if (CR.chrono > 0) {
        const int ok = j->fin >= 0 && j->fin <= CR.chrono;
        ui_text(ui_font(FONT_DISP, 26), UW / 2, top + 92 + CR.n * pas + 30, UI_CENTER, ui_rgb(ok ? 0x33FF88 : 0xFF3344), 255, ok ? "TEMPS BATTU" : "TEMPS DÉPASSÉ");
    }
    const float by = UH - 40 - 84;
    cadre_bouton(24, by, UW - 48, 84, 1, C2);
    ui_text(ui_font(FONT_DISP, 26), UW / 2, by + 52, UI_CENTER, ui_rgb(WHITE), 255, "CONTINUER");
}
static void portrait_course(void) {
    const racer_t *j = &CR.r[CR.n - 1];
    char t[48];
    if (CR.pause) return;   /* pause : son écran seul (portrait_course_pause), rien de la course dessous */
    if (CR.phase == CP_RESULTATS) { crs_arrivee(); return; }
    const float c3 = cy3d();
    /* Accélération : halo blanc sur les bords de l'image, traits de vitesse (16, tirés à nouveau 24 fois par seconde) du
       bord du canevas vers le milieu de la 3D */
    if (j->halo > 0) {
        const uint16_t blanc = ui_rgb(0xFFFFFF);
        ui_halo_bord(0.11f * fminf((float)ui_img_w, (float)ui_img_h), blanc, (int)(j->halo * 215));
        const int f = (int)floorf(CR.clock * 24);
        for (int i = 0; i < 16; i++) {
            const float a = i * PI_F / 8 + ((i * 7 + f * 3) % 5) * 0.06f, ca = cosf(a), sa = sinf(a);
            const float bx = fabsf(ca) > 1e-3f ? UW / 2 / fabsf(ca) : 1e9f, by = fabsf(sa) > 1e-3f ? (sa > 0 ? UH - c3 : c3) / fabsf(sa) : 1e9f;
            const float bord = fminf(bx, by), r0 = bord * (0.78f + ((i * 5 + f * 7) % 6) * 0.025f);   /* du bord du canevas vers le milieu */
            ui_line(UW / 2 + ca * r0, c3 + sa * r0, UW / 2 + ca * bord, c3 + sa * bord, 3, blanc, (int)(j->halo * 150));
        }
    }
    /* Bandeau du haut : place (ou écart au fantôme, meilleur tour), tour ; temps (et à battre, ou meilleur tour) ; pause */
    const uint16_t ink = ui_rgb(INK), GRIS = ui_rgb(DIM);
    ui_rect(0, 0, UW, CRS_BANDE, ink, 170); ui_rect(0, CRS_BANDE, UW, 24, ink, 70);
    int place, order[COURSE_N], bc = 0;
    place = course_rank(order);
    const int tour = clampi(j->tour + 1, 1, CR.tours), solo_tour = CR.sorte == CS_TOUR, clm = CR.sorte == CS_CLM;
    ui_text(ui_font(FONT_MONO, 18), 24, 26, UI_LEFT, GRIS, 255, solo_tour ? "MEILLEUR TOUR" : clm ? "FANTÔME" : "PLACE");
    if (!course_bandeau(1, t, sizeof t, &bc)) snprintf(t, sizeof t, "%d%s / %d", place, place == 1 ? "er" : "e", CR.n);
    const ui_font_t *fp = ui_fit(FONT_DISP, t, UW / 2 - 40, 40, 22);
    ui_text(fp, 25, 69, UI_LEFT, C1, 90, t);
    ui_text(fp, 24, 68, UI_LEFT, ui_rgb(bc == 1 ? 0x33FF88 : bc == 2 ? 0xFF3344 : 0xE8F6FF), 255, t);
    if (!course_bandeau(0, t, sizeof t, &bc)) snprintf(t, sizeof t, "TOUR %d/%d", tour, CR.tours);
    ui_text(ui_font(FONT_MONO, 20), 24, 98, UI_LEFT, C1, 255, t);
    const float xr = UW - 116;   /* colonne du temps, à gauche du bouton de pause */
    ui_text(ui_font(FONT_MONO, 18), xr, 26, UI_RIGHT, GRIS, 255, solo_tour ? "TOUR EN COURS" : "TEMPS");
    if (!course_bandeau(2, t, sizeof t, &bc)) chrono(j->fin >= 0 ? j->fin : CR.t, t, sizeof t);
    ui_text(ui_font(FONT_DISP, 30), xr, 64, UI_RIGHT, C2, 255, t);
    if (CR.chrono > 0) {   /* temps à battre (rouge une fois dépassé) */
        char ch[16], tb[32];
        chrono(CR.chrono, ch, sizeof ch);
        snprintf(tb, sizeof tb, "À BATTRE %s", ch);
        ui_text(ui_font(FONT_MONO, 18), xr, 96, UI_RIGHT, ui_rgb((j->fin >= 0 ? j->fin : CR.t) > CR.chrono ? 0xFF3344 : 0x7F93A8), 255, tb);
    } else if (!solo_tour && j->meilleur > 0) {   /* meilleur tour (le tour rapide l'a en grand) */
        char ch[16], tb[32];
        chrono(j->meilleur, ch, sizeof ch);
        snprintf(tb, sizeof tb, "MEILLEUR %s", ch);
        ui_text(ui_font(FONT_MONO, 18), xr, 96, UI_RIGHT, GRIS, 255, tb);
    }
    {   /* pause, à la place du bouton menu */
        const float x = UW - 96, y = 18;
        cadre_bouton(x, y, 76, 76, 0, C1);
        ui_icon("pause", x + 38, y + 38, 30, C1, 255);
        button_rect(x, y, 76, 76, A_PAUSE, 0);
    }
    crs_plan(UW - 24 - 150, CRS_BANDE + 10, 150, 108);
    /* Feux : trois rouges, puis verts au départ (une seconde), dans un bandeau sur toute la largeur ; GO dessous */
    const float ly = c3 - 220;
    if (CR.phase == CP_GRILLE || CR.t < 1) {
        const int vert = CR.phase != CP_GRILLE;
        const uint16_t c = ui_rgb(vert ? 0x33FF88 : 0xFF3344);
        crs_bandeau(ly, 84, vert ? c : C2, 255);
        for (int k = 0; k < 3; k++) {
            const float x = UW / 2 + (k - 1) * 76.0f;
            ui_circle(x, ly, 27, ui_rgb(0x060E1A), 230);
            if (vert || k < CR.feux) { ui_circle(x, ly, 25, c, 80); ui_circle(x, ly, 20, c, 255); }
            else ui_ring(x, ly, 20, 2, ui_rgb(0x3A4A5C), 255);
        }
        if (vert) texte_ombre(ui_font(FONT_DISP, 40), UW / 2, ly + 100, UI_CENTER, c, 255, "GO");
    }
    /* Annonces de la course (hud_toast de course.c : tour, dernier tour, record, éliminations) : bandeau sur toute la
       largeur, en fondu ; rouge pour une élimination */
    const float tt = H.now - H.toast_t;
    if (tt < 1.8f && H.toast[0] && !(CR.phase == CP_GRILLE || CR.t < 1)) {
        const float a = fminf(1, fminf(tt / 0.15f, (1.8f - tt) / 0.3f));
        const int al = (int)(255 * (a < 0 ? 0 : a)), elim = strstr(H.toast, "ÉLIMIN") != NULL;
        crs_bandeau(ly, 60, elim ? ui_rgb(0xFF3344) : C2, al);
        ui_text(ui_fit(FONT_DISP, H.toast, UW - 48, 30, 18), UW / 2, ly + 11, UI_CENTER, elim ? ui_rgb(0xFF5A6A) : ui_rgb(WHITE), al, H.toast);
    }
    if (j->inv > 0.8f && CR.phase == CP_COURSE) texte_ombre(ui_font(FONT_DISP, 26), UW / 2, c3 - 130, UI_CENTER, ui_rgb(0xFF3344), 255, "SENS INVERSE");
    if (j->boost > 0) texte_ombre(ui_font(FONT_DISP, 26), UW / 2, c3 - 130, UI_CENTER, ui_rgb(0x22E4FF), 255, "BOOST");
    /* Vitesse : entre les pouces (doubles commandes), à gauche du joystick */
    crs_compteur(joy_on() ? 100 : (GAZ_X + 20 + VOL_X - DRV_VOL - 20) / 2, UH - 150, j);
    /* Arme en main (tir, turbo) : au-dessus du volant, sous le pouce droit ; son nom au-dessus */
    if (CR.armes && j->fin < 0) {
        static const char *const NOM[5] = { "", "MISSILE", "MINE", "BOUCLIER", "TURBO" }, *const ICO[5] = { "", "missile", "mine", "equip", "tech" };
        const float bw = 124, bh = 84, x = VOL_X - bw / 2, y = UH - 330;
        float q[12]; ui_chamfer_path(x, y, bw, bh, 12, q);
        if (j->arme) {
            ui_poly(q, 6, ui_mix(ui_rgb(INK), C2, 0.28f), 235);
            glow_outline(q, 6, 2, C2);
            ui_icon(ICO[j->arme], x + bw / 2, y + 32, 30, ui_rgb(WHITE), 255);
            ui_text(ui_font(FONT_MONO, 18), x + bw / 2, y + 70, UI_CENTER, C2, 255, NOM[j->arme]);
            button_rect(x, y, bw, bh, A_FIRE, 0);
        } else {
            ui_poly(q, 6, ui_rgb(INK), 150);
            ui_outline(q, 6, 2, ui_rgb(0x3A4A5C), 255);
            ui_text(ui_font(FONT_MONO, 18), x + bw / 2, y + bh / 2 + 6, UI_CENTER, ui_rgb(0x3A4A5C), 255, "ARME");
        }
    }
}
/* En-tête des menus de course (comme entete des sous-menus) : ‹, pictogramme et titre en haut à gauche (toucher : act),
   le sous-titre dessous, ou un instant le message du jeu (toast) */
static void crs_entete(const char *icon, const char *titre, const char *sous, uint16_t cs, int act) {
    const float L = 24;
    ui_icon("back", L + 12, 44, 26, C1, 255);
    ui_icon(icon, L + 44, 44, 26, C2, 255);
    ui_text(ui_font(FONT_DISP, 30), L + 70, 54, UI_LEFT, ui_rgb(WHITE), 255, titre);
    button_rect(L - 12, 8, 300, 76, act, 0);
    const int msg = H.now - H.toast_t < 1.8f && H.toast[0];
    const char *line = msg ? H.toast : sous;
    if (line && line[0]) ui_text(ui_fit(FONT_MONO, line, UW - 48, 20, 16), L, 100, UI_LEFT, msg ? C2 : cs, 255, line);
}
/* Pied des menus de course : RETOUR (a0) et l'action (a1, son libellé ; terne si dim), sur un panneau en bas */
static void crs_pied(int a0, int a1, const char *lab, int dim) {
    ui_rect(0, UH - CRS_PIED, UW, CRS_PIED, ui_rgb(0x0E0A1E), 245);
    ui_line(0, UH - CRS_PIED, UW, UH - CRS_PIED, 2, C1, 255);
    const float bw = (UW - 48 - 14) / 2, by = UH - 40 - 84;
    cadre_bouton(24, by, bw, 84, 0, ui_rgb(DIM));
    ui_icon("back", 24 + 34, by + 42, 26, ui_rgb(DIM), 255);
    ui_text(ui_font(FONT_DISP, 22), 24 + bw / 2 + 14, by + 50, UI_CENTER, ui_rgb(WHITE), 255, "RETOUR");
    button_rect(24, by, bw, 84, a0, 0);
    cadre_bouton(24 + bw + 14, by, bw, 84, !dim, dim ? ui_rgb(DIM) : C2);
    ui_text(ui_fit(FONT_DISP, lab, bw - 24, 22, 16), 24 + bw + 14 + bw / 2, by + 50, UI_CENTER, dim ? ui_rgb(DIM) : ui_rgb(WHITE), 255, lab);
    button_rect(24 + bw + 14, by, bw, 84, a1, 0);
}
/* Pause (portrait) : en-tête (toucher : reprendre), une ligne par choix (la toucher la choisit, la toucher encore la
   valide), le détail du choix et RETOUR (reprendre), VALIDER en bas */
static void portrait_course_pause(void) {
    char t[64];
    nbtn = 0;   /* seuls ses boutons répondent */
    voile_ecran(ui_rgb(INK), 222);
    const racer_t *j = &CR.r[CR.n - 1];
    snprintf(t, sizeof t, "%s · TOUR %d/%d", PISTE.nom[0] ? PISTE.nom : "PISTE", clampi(j->tour + 1, 1, CR.tours), CR.tours);
    crs_entete("pause", "PAUSE", t, ui_rgb(DIM), A_REPRENDRE);
    const int np = pause_n(), tr = carriere_mode() && carr_sorte_course() == CS_TOUR;
    if (CR.pause_sel >= np) CR.pause_sel = 0;
    for (int i = 0; i < np; i++) {
        const float y = ROW_Y0 + i * ROW_STEP, x = 24, w = UW - 48;
        const int q0 = pause_quoi(i), der = q0 == PA_FIN, sel = i == CR.pause_sel;
        const uint16_t col = der && !tr ? ui_rgb(0xFF3344) : sel ? C2 : C1;
        float q[12]; ui_chamfer_path(x, y, w, ROW_H, 10, q);
        ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.24f : 0.10f), 238);
        if (sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 1, col, 160);
        const char *lab = q0 == PA_REPRENDRE ? "REPRENDRE" : q0 == PA_REGLAGES ? "RÉGLAGES" : q0 == PA_RECOMMENCER ? "RECOMMENCER" : tr ? "TERMINER" : "ABANDONNER";
        ui_text(ui_font(FONT_DISP, 22), x + 20, y + ROW_H / 2 + 8, UI_LEFT, der && !tr ? ui_rgb(0xFF5A6A) : ui_rgb(WHITE), 255, lab);
        button_rect(x, y, w, ROW_H, A_PAUSE_SEL, i);
    }
    const int qs = pause_quoi(CR.pause_sel), der = qs == PA_FIN;
    const char *det = qs == PA_REPRENDRE ? "Retour à la course" : qs == PA_REGLAGES ? "Son, direction, commandes, écran" : qs == PA_RECOMMENCER ? "La course repart de la grille"
                    : tr ? "Terminer : le meilleur tour compte" : "Abandon : la course ne compte pas";
    ui_rect(0, UH - PIED_H, UW, PIED_H, ui_rgb(0x0E0A1E), 245);
    ui_line(0, UH - PIED_H, UW, UH - PIED_H, 2, C1, 255);
    ui_text(ui_fit(FONT_MONO, det, UW - 48, 20, 16), 24, UH - PIED_H + 46, UI_LEFT, ui_rgb(DIM), 255, det);
    const float bw = (UW - 48 - 14) / 2, by = UH - 40 - 84;
    cadre_bouton(24, by, bw, 84, 0, ui_rgb(DIM));
    ui_icon("back", 24 + 34, by + 42, 26, ui_rgb(DIM), 255);
    ui_text(ui_font(FONT_DISP, 22), 24 + bw / 2 + 14, by + 50, UI_CENTER, ui_rgb(WHITE), 255, "RETOUR");
    button_rect(24, by, bw, 84, A_REPRENDRE, 0);
    cadre_bouton(24 + bw + 14, by, bw, 84, 1, der && !tr ? ui_rgb(0xFF3344) : C2);
    ui_text(ui_font(FONT_DISP, 22), 24 + bw + 14 + bw / 2, by + 50, UI_CENTER, ui_rgb(WHITE), 255, "VALIDER");
    button_rect(24 + bw + 14, by, bw, 84, A_PAUSE_GO, 0);
}
/* Ligne réglable du menu de course : toute la largeur, flèches aux deux bouts (moitié gauche : moins, moitié droite : plus),
   le libellé en petit au-dessus de la valeur (lab NULL : la valeur seule ; val NULL : rien, la ligne est remplie après) */
static void rm_ligne(int row, float y, const char *lab, const char *val, int actif) {
    const float x = 24, w = UW - 48, h = ROW_H;
    const int sel = RM.row == row;
    const uint16_t col = sel ? C2 : C1;
    float q[12]; ui_chamfer_path(x, y, w, h, 10, q);
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.24f : 0.10f), 238);
    if (sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 1, col, 128);
    for (int sg = -1; sg <= 1; sg += 2) {
        const float ax = sg < 0 ? x + 26 : x + w - 26, ay = y + h / 2;
        const float tri[6] = { ax + sg * 9, ay, ax - sg * 7, ay - 12, ax - sg * 7, ay + 12 };
        ui_poly(tri, 3, C1, actif ? 255 : 60);
        button_rect(sg < 0 ? x : x + w / 2, y, w / 2, h, A_RACE_CHG, row * 2 + (sg > 0));
    }
    if (!val) return;
    if (lab) {
        ui_text(ui_fit(FONT_MONO, lab, w - 100, 18, 16), UW / 2, y + 25, UI_CENTER, ui_rgb(DIM), 255, lab);
        ui_text(ui_fit(FONT_DISP, val, w - 100, 26, 16), UW / 2, y + 57, UI_CENTER, sel ? C1 : ui_rgb(0xE8F6FF), 255, val);
    } else ui_text(ui_fit(FONT_DISP, val, w - 100, 26, 16), UW / 2, y + h / 2 + 9, UI_CENTER, sel ? C1 : ui_rgb(0xE8F6FF), 255, val);
}
/* Menu de course (portrait) : en-tête (toucher : annuler), onglets PILOTE, RIVAUX, ÉPREUVE (sauf le menu des véhicules),
   la page, puis RETOUR et DÉPART (PRENDRE, ACHETER) en bas */
static void portrait_course_menu(void) {
    const uint16_t GRIS = ui_rgb(DIM), BLANC = ui_rgb(0xE8F6FF);
    char t[64];
    voile_ecran(ui_rgb(INK), 224);
    int var = 0;
    const int pc = rm_couleur(&var);   /* véhicule de la couleur choisie (page PILOTE) et sa palette */
    const int prix = RM.mode == RM_BOUTIQUE ? veh_prix(pc) : 0, cher = RM.mode == RM_BOUTIQUE && prix > E.credits;
    if (RM.mode == RM_DON) crs_entete("drive", "VÉHICULES", "OFFERT", C2, A_RACE_BACK);
    else if (RM.mode) {   /* boutique : son prix (rouge s'il est trop cher), les crédits de l'équipe */
        snprintf(t, sizeof t, "PRIX %d CR · CRÉDITS %d CR", prix, E.credits);
        crs_entete("drive", "CONCESSION", t, cher ? ui_rgb(0xFF3344) : C2, A_RACE_BACK);
    } else {
        const piste_t *P = RM.ntr ? course_piste(RM.trace[RM.tr]) : &PISTE;
        snprintf(t, sizeof t, "%s · %d TOUR%s", P->nom[0] ? P->nom : "PISTE", RM.tours, RM.tours > 1 ? "S" : "");
        crs_entete("flag", "COURSE", t, GRIS, A_RACE_BACK);
        const float tw = (UW - 48) / 3;
        for (int i = 0; i < 3; i++) {   /* onglets (ligne 0) */
            const float x = 24 + i * tw;
            const int on = i == RM.page;
            const uint16_t c = on ? (RM.row == 0 ? C2 : C1) : GRIS;
            ui_text(ui_font(FONT_MONO, 20), x + tw / 2, 152, UI_CENTER, c, 255, RM_PAGES[i]);
            if (on) ui_rect(x + 12, 162, tw - 24, 3, c, 255);
            button_rect(x, 120, tw, 56, A_RACE_PAGE, i);
        }
    }
    const float T = RM.mode ? 128 : 190, F = UH - CRS_PIED;   /* haut et bas de la page */
    if (RM.page == 0) {
        /* De bas en haut : vitesse, accélération, adhérence ; la couleur ; le nom ; le véhicule qui tourne, entre les flèches
           du modèle */
        const int p = pc, dir = (int)((H.now - RM.t0) / 0.35f) & 7, nc = rm_ncoul(RM.mod);
        float vm = 0, am = 0, v, a;
        for (int i = 0; i < RM.n; i++) { veh_params(RM.ord[i], &v, &a); vm = fmaxf(vm, v); am = fmaxf(am, a); }
        veh_params(p, &v, &a);
        static const char *const LAB[3] = { "VITESSE", "ACCÉL.", "ADHÉRENCE" };
        const float k3[3] = { v / fmaxf(vm, 1e-3f), a / fmaxf(am, 1e-3f), E.w.veh_grip[p] / 1.6f };
        const float ys = F - 24 - 3 * 36;
        for (int i = 0; i < 3; i++) {
            const float y = ys + i * 36.0f, k = fmaxf(0.03f, fminf(1, k3[i]));
            ui_text(ui_font(FONT_MONO, 18), 24, y + 6, UI_LEFT, GRIS, 255, LAB[i]);
            ui_rect(180, y - 5, UW - 204, 10, ui_rgb(0xFFFFFF), 31);
            ui_rect(180, y - 5, (UW - 204) * k, 10, C2, 255);
        }
        const float yc = ys - 30 - ROW_H;
        snprintf(t, sizeof t, "%d / %d", RM.coul + 1, nc);
        rm_ligne(2, yc, "COULEUR", t, nc > 1);
        const float yn = yc - 22;
        ui_text(ui_fit(FONT_DISP, E.w.persos[p].nom, UW - 48, 30, 18), UW / 2, yn, UI_CENTER, RM.row == 1 ? C1 : BLANC, 255, E.w.persos[p].nom);
        const float z0 = T + 8, z1 = yn - 44, zm = (z0 + z1) / 2, zh = z1 - z0;
        if (zh > 40 && rm_apercu_v(0, p, var, dir, RM_AP)) {
            const float sc = fminf(fminf((UW - 180) / rm_w[0], zh / rm_h[0]), 5);
            ui_image(rm_px, rm_mk, rm_w[0], rm_h[0], UW / 2 - rm_w[0] * sc / 2, zm - rm_h[0] * sc / 2, sc, UW / 2, zm, UW + UH);
        }
        for (int sg = -1; sg <= 1; sg += 2) {   /* modèle précédent, suivant (ligne 1) */
            const float bw = 60, bh = fminf(150, zh), x = sg < 0 ? 24 : UW - 24 - bw, y = zm - bh / 2;
            cadre_bouton(x, y, bw, bh, RM.row == 1, RM.row == 1 ? C2 : C1);
            const float ax = x + bw / 2, tri[6] = { ax + sg * 9, zm, ax - sg * 7, zm - 13, ax - sg * 7, zm + 13 };
            ui_poly(tri, 3, C1, 255);
            button_rect(x, y, bw, bh, A_RACE_CHG, 2 + (sg > 0));
        }
    } else if (RM.page == 1) {
        snprintf(t, sizeof t, "%d", RM.nia);
        rm_ligne(1, T, "ADVERSAIRES", t, !rm_fige(1, 1));
        rm_ligne(2, T + ROW_STEP, "NIVEAU", NIVEAUX[RM.niv], !rm_fige(1, 2));
        for (int k = 0; k < RM_NR; k++) {
            const float y = T + (2 + k) * ROW_STEP;
            const int actif = k < RM.nia, p = RM.riv[k];
            rm_ligne(3 + k, y, NULL, NULL, actif && !RM.fige_riv);
            if (rm_apercu(1 + k, p, 1, 48)) {
                const float sc = fminf(64.0f / rm_w[1 + k], 48.0f / rm_h[1 + k]), cx = 24 + 90, cy = y + ROW_H / 2;
                ui_image(rm_px + (1 + k) * RM_AP * RM_AP, rm_mk + (1 + k) * RM_AP * RM_AP, rm_w[1 + k], rm_h[1 + k], cx - rm_w[1 + k] * sc / 2, cy - rm_h[1 + k] * sc / 2, sc, cx, cy, UW + UH);
                if (!actif) ui_rect(cx - 36, cy - 28, 72, 56, ui_rgb(INK), 170);
            }
            ui_text(ui_fit(FONT_DISP, E.w.persos[p].nom, UW - 240, 22, 16), 24 + 140, y + ROW_H / 2 + 8, UI_LEFT, RM.row == 3 + k ? C1 : actif ? BLANC : ui_rgb(0x3A4A5C), 255, E.w.persos[p].nom);
        }
    } else {
        /* Plan des tracés : les autres en pâle, le choisi en teinte 1, la ligne de départ en teinte 2 ; puis tracé, tours, armes */
        const piste_t *Pc = RM.ntr ? course_piste(RM.trace[RM.tr]) : &PISTE;
        const float ph = fminf(260, F - T - 3 * ROW_STEP - 24);
        float x0 = Pc->x0, x1 = Pc->x1, z0 = Pc->z0, z1 = Pc->z1;
        for (int k = 0; k < RM.ntr; k++) {
            const piste_t *P = course_piste(RM.trace[k]);
            x0 = fminf(x0, P->x0); x1 = fmaxf(x1, P->x1); z0 = fminf(z0, P->z0); z1 = fmaxf(z1, P->z1);
        }
        if (ph > 40) {
            ui_rect(24, T, UW - 48, ph, ui_rgb(INK), 140);
            const float sc = fminf((UW - 72) / fmaxf(1, x1 - x0), (ph - 24) / fmaxf(1, z1 - z0)), mx = UW / 2 + (x0 + x1) / 2 * sc, mz = T + ph / 2 - (z0 + z1) / 2 * sc;   /* x en miroir (CARTE_X) */
            for (int pass = 0; pass < 2; pass++) for (int k = 0; k < (RM.ntr ? RM.ntr : 1); k++) {
                const int sel = !RM.ntr || k == RM.tr;
                if (sel != pass) continue;
                const piste_t *P = RM.ntr ? course_piste(RM.trace[k]) : &PISTE;
                const int step = P->n > 96 ? P->n / 96 : 1;
                for (int i = 0; i < P->n; i += step) {
                    const int e = i + step < P->n ? i + step : 0;
                    ui_line(mx - P->X[i] * sc, mz + P->Z[i] * sc, mx - P->X[e] * sc, mz + P->Z[e] * sc, sel ? 4 : 2, sel ? C1 : GRIS, sel ? 255 : 77);
                }
            }
            ui_circle(mx - Pc->X[Pc->dep] * sc, mz + Pc->Z[Pc->dep] * sc, 7, C2, 255);
        }
        const float y0 = T + (ph > 40 ? ph + 16 : 0);
        snprintf(t, sizeof t, "TRACÉ · %s", course_surface_nom());
        rm_ligne(1, y0, t, Pc->nom[0] ? Pc->nom : "PISTE", !rm_fige(2, 1));
        char tt[12], lab[40];
        snprintf(tt, sizeof tt, "%d", RM.tours);
        if (RM.chrono > 0) { char ch[16]; chrono(RM.chrono, ch, sizeof ch); snprintf(lab, sizeof lab, "TOURS · À BATTRE %s", ch); }
        else snprintf(lab, sizeof lab, "TOURS");
        rm_ligne(2, y0 + ROW_STEP, lab, tt, !rm_fige(2, 2));
        rm_ligne(3, y0 + 2 * ROW_STEP, "ARMES", RM.armes ? "OUI" : "NON", !rm_fige(2, 3));
    }
    crs_pied(A_RACE_BACK, A_RACE_GO, RM.mode == RM_DON ? "PRENDRE" : RM.mode ? "ACHETER" : "DÉPART", cher);
}
#endif

/* ---------- Combat (drawBattleHUD de l'émulateur) ---------- */
#define RED_HEX 0xFF3B4E
static uint16_t bt_col(uint32_t c) { return c == BT_C1 ? C1 : c == BT_C2 ? C2 : ui_rgb(c); }
static uint32_t unit_color(int u) { return u >= BT_ENEMY ? RED_HEX : membre_couleur(u); }
/* Lettre d'une unité dans la timeline : l'initiale d'un membre ; un ennemi seul « ! », dans un groupe son numéro (1 à 4,
   celui de sa barre sous lui : des chiffres, pas des lettres, que l'on confondrait avec les initiales de l'équipe) */
/* initiale d'un membre : une capitale accentuée (ÉTUDIANT, deux octets en UTF-8) prend sa lettre sans accent ; celle de
   son nom affiché (héroïne choisie : nom_echange) */
static char unit_lettre(int u) {
    if (u >= BT_ENEMY) return BT.nen > 1 ? (char)('1' + (u - BT_ENEMY)) : '!';
    char b[24];
    const unsigned char *n = (const unsigned char *)nom_echange(PARTY[u].name, b, (int)sizeof b);
    return n[0] == 0xC3 && n[1] >= 0x80 && n[1] < 0xA0 ? "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"[n[1] - 0x80] : (char)n[0];
}

/* Texte entouré d'un trait sombre (strokeText de l'émulateur, trait de 5 px) : huit copies décalées, puis le texte */
static void text_outlined(const ui_font_t *f, float x, float y, uint16_t c, int a, const char *s) {
    static const int8_t O[8][2] = { { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 }, { -2, -2 }, { 2, -2 }, { -2, 2 }, { 2, 2 } };
    for (int k = 0; k < 8; k++) ui_text(f, x + O[k][0], y + O[k][1], UI_CENTER, ui_rgb(INK), a, s);
    ui_text(f, x, y, UI_CENTER, c, a, s);
}

/* Bouton de l'éventail (hexBtn) : hexagone de 76 × 66 ; icône et libellé, ou libellé coupé après un tiret ou à une
   espace et sous-titre (coût, nombre) ; dim : action impossible (contour et texte à 38 %). Commande cmd × 8 + arg ;
   au clavier ou à la manette, le bouton choisi est rempli de sa couleur, cerclé de blanc dans un halo qui pulse, texte
   et icône en blanc (un simple halo ne se voyait pas sur la montre) ; à la manette, dès l'ouverture de l'éventail. */
static int fan_choisi(int slot) { return (H.bkbd || E.manette) && H.bsel == slot; }
static void fan_halo(const float *q, uint16_t col) {
    ui_outline(q, 6, 12, col, (int)(70 + 50 * sinf(E.clock * 7)));
    ui_outline(q, 6, 3, ui_rgb(WHITE), 255);
}
/* Éventail de commandes sur l'arc en bas à gauche (l'équipe est en bas à droite, l'ennemi en haut à gauche) */
static const float FAN[5][2] = { { 227, 403 }, { 153, 383 }, { 95, 333 }, { 66, 262 }, { 70, 186 } };
#if ENG_DYN
/* Plein écran : place du bouton de la place slot de l'éventail. Commandes : ATTAQUE, TECHNIQ., OBJETS, DÉFENSE dans la
   grille, FUITE en petit ; techniques et sorts : places 0, 1, 3, 4 dans la grille, RETOUR (place 2) en petit */
#define BT_GRILLE_Y (UH - 40 - 2 * 96 - 14)
static void fan_place(int slot, float *x, float *y, float *w, float *h) {
    const int g = BT.phase == BP_CMD ? (slot < 4 ? slot : -1) : slot == 2 ? -1 : slot < 2 ? slot : slot - 1;
    const float gap = 14, gw = (UW - 48 - gap) / 2, gh = 96;
    if (g < 0) { *w = 170; *h = 56; *x = UW - 24 - *w; *y = BT_GRILLE_Y - 14 - *h; return; }
    *w = gw; *h = gh; *x = 24 + (g & 1) * (gw + gap); *y = BT_GRILLE_Y + (g >> 1) * (gh + gap);
}
#endif
static void fan_button(int slot, const char *label, const char *icon, uint16_t col, int cmd, int arg, const char *sub, int dim) {
#if ENG_DYN
    {
        float x, y, w, h;
        fan_place(slot, &x, &y, &w, &h);
        const int a = dim ? 97 : 255, grand = h > 60, ch = fan_choisi(slot);
        float q[12]; ui_chamfer_path(x, y, w, h, 12, q);
        ui_poly(q, 6, ui_mix(ui_rgb(INK), col, ch ? 0.45f : 0.12f), 235);
        if (ch) { fan_halo(q, col); col = ui_rgb(WHITE); } else ui_outline(q, 6, 2, col, a);
        if (icon && grand) ui_icon(icon, x + 34, y + h / 2, 26, col, a);
        const float tx = icon && grand ? x + 62 : x + w / 2;
        const int al = icon && grand ? UI_LEFT : UI_CENTER;
        ui_text(ui_fit(FONT_DISP, label, w - (tx - x) - 12, 22, 14), al == UI_LEFT ? tx : x + w / 2, y + h / 2 + (sub && grand ? -2 : 8), al, col, a, label);
        if (sub && grand) ui_text(ui_font(FONT_MONO, 16), al == UI_LEFT ? tx : x + w / 2, y + h / 2 + 22, al, ui_rgb(DIM), a, sub);
        button_rect(x, y, w, h, A_BATTLE, cmd * 8 + arg);
        H.bslot_cmd[slot] = cmd * 8 + arg + 1;
        return;
    }
#endif
    const float x = FAN[slot][0], y = FAN[slot][1];
    const int a = dim ? 97 : 255, ch = fan_choisi(slot);
    float q[12];
    ui_hex_path(x, y, 76, 66, q);
    ui_poly(q, 6, ch ? ui_mix(ui_rgb(0x060E1A), col, 0.45f) : ui_rgb(0x060E1A), 240);
    if (ch) { fan_halo(q, col); col = ui_rgb(WHITE); } else ui_outline(q, 6, 2, col, a);
    if (icon) {
        ui_icon(icon, x, y - 9, 26, col, a);
        ui_text(ui_fit(FONT_DISP, label, 64, 16, 12), x, y + 22, UI_CENTER, col, a, label);
    } else {
        char part[3][24];
        int n = 0, len = 0;
        for (const char *c = label; *c && n < 3; c++) {
            if (*c == ' ') { if (len) { part[n][len] = 0; n++; len = 0; } continue; }
            if (len < 23) part[n][len++] = *c;
            if (*c == '-') { part[n][len] = 0; n++; len = 0; }
        }
        if (len && n < 3) { part[n][len] = 0; n++; }
        const ui_font_t *f = NULL;
        for (int k = 0; k < n; k++) {
            const ui_font_t *g = ui_fit(FONT_DISP, part[k], 64, 16, 12);
            if (!f || (g && g->size < f->size)) f = g;
        }
        for (int k = 0; k < n; k++) ui_text(f, x, y - 2 + k * 17 - (n - 1) * 8.5f - (sub ? 6 : 0), UI_CENTER, col, a, part[k]);
        if (sub) ui_text(ui_font(FONT_DISP, 15), x, y + 24, UI_CENTER, ui_rgb(DIM), a, sub);
    }
    button(x, y, 36, A_BATTLE, cmd * 8 + arg);
    H.bslot_cmd[slot] = cmd * 8 + arg + 1;
}

/* Sous-titre de bannière coupé en deux lignes au plus de max_w pixels (aux espaces) */
static int wrap2(const ui_font_t *f, const char *text, float max_w, char out[2][72]) {
    int n = 0;
    const char *p = text;
    while (*p && n < 2) {
        const char *best = NULL, *e = p;
        for (;;) {
            const char *w = ui_fin_mot(e);
            char buf[72];
            int l = (int)(w - p); if (l > 71) l = 71;
            memcpy(buf, p, (size_t)l); buf[l] = 0;
            if (best && ui_text_width(f, buf) > max_w) break;
            best = w;
            if (!*w) break;
            e = w + 1;
        }
        int l = (int)(best - p); if (l > 71) l = 71;
        memcpy(out[n], p, (size_t)l); out[n][l] = 0; n++;
        p = *best ? best + 1 : best;
    }
    return n;
}

/* ---------- Effets des armes (fxDraw de l'émulateur) ----------
   Dessinés en 2D par-dessus le combat, de l'attaquant vers la cible, t secondes après le début de l'action ; tirage
   fixe par action (fx_h, même suite que l'émulateur) ; alpha de 0 à 255. */
static float fx_h(int s, int i) {
    uint32_t x = (uint32_t)(i + 1) * 0x9E3779B1u ^ (uint32_t)s;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    return (float)(x & 0xFFFF) / 65536.0f;
}
static int fx_a(float a) { return a <= 0 ? 0 : a >= 255 ? 255 : (int)a; }
static uint16_t fx_mix(uint16_t c, float t) { return ui_mix(c, ui_rgb(WHITE), t); }
static void fx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, float a) { if (a > 0) ui_line(x0, y0, x1, y1, w, c, fx_a(a)); }
static void fx_circle(float x, float y, float r, uint16_t c, float a) { if (r > 0 && a > 0) ui_circle(x, y, r, c, fx_a(a)); }
static void fx_ring(float x, float y, float r, float w, uint16_t c, float a) { if (r > 0 && a > 0) ui_ring(x, y, r, w, c, fx_a(a)); }
/* Trait lumineux : halo large, trait, cœur clair */
static void fx_glow(float x0, float y0, float x1, float y1, float w, uint16_t c, float a) {
    fx_line(x0, y0, x1, y1, w * 3, c, a * 0.3f); fx_line(x0, y0, x1, y1, w, c, a);
    fx_line(x0, y0, x1, y1, w * 0.4f > 1 ? w * 0.4f : 1, fx_mix(c, 0.8f), a);
}
/* Éclat au point touché, e de 0 à 1 (0,3 s) : anneau qui s'ouvre, flash, étincelles */
static void fx_impact(float x, float y, float e, uint16_t c, float k, int s) {
    if (e < 0 || e > 1) return;
    const float a = 255 * (1 - e);
    fx_ring(x, y, (8 + 52 * e) * k, 4 * (1 - e) + 1, c, a);
    fx_circle(x, y, 16 * k * (1 - e), fx_mix(c, 0.7f), a);
    for (int i = 0; i < 8; i++) {
        const float g = fx_h(s, i) * 6.2832f, r0 = (10 + 30 * e) * k, r1 = r0 + (14 + 16 * fx_h(s, i + 8)) * k * (1 - e);
        fx_line(x + cosf(g) * r0, y + sinf(g) * r0, x + cosf(g) * r1, y + sinf(g) * r1, 2.5f, fx_mix(c, 0.5f), a);
    }
}
/* Flèche dont la pointe est en (x, y), direction (cx, cy) ; renvoie le bout de la hampe */
static void fx_arrow(float x, float y, float cx, float cy, uint16_t c, float a, float *bx, float *by) {
    const float nx = -cy, ny = cx;
    *bx = x - cx * 26; *by = y - cy * 26;
    fx_line(*bx, *by, x, y, 2.5f, fx_mix(c, 0.3f), a);
    const float tri[6] = { x + cx * 6, y + cy * 6, x - cx * 4 + nx * 5, y - cy * 4 + ny * 5, x - cx * 4 - nx * 5, y - cy * 4 - ny * 5 };
    if (a > 0) ui_poly(tri, 3, fx_mix(c, 0.6f), fx_a(a));
    for (int sg = -1; sg <= 1; sg += 2) fx_line(*bx, *by, *bx - cx * 6 + nx * 5 * sg, *by - cy * 6 + ny * 5 * sg, 2, c, a);
}
/* ---------- Effets puissants : voile qui assombrit l'écran, flashs, halos, rayons, éclairs, particules ---------- */
/* Enveloppe : 0 → 1 de t0 à t1, 1 jusqu'à t2, → 0 à t3 */
static float fx_env(float t, float t0, float t1, float t2, float t3) {
    return t <= t0 || t >= t3 ? 0 : t < t1 ? (t - t0) / (t1 - t0) : t <= t2 ? 1 : 1 - (t - t2) / (t3 - t2);
}
static float fx_01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
/* Couleur vers le noir (0 : noir, 1 : elle-même) */
static uint16_t fx_dark(uint16_t c, float t) { return ui_mix(ui_rgb(0), c, t); }
/* Voile sur tout l'écran : assombrissement, teinte, flash (× fx_vk : force des voiles du sort en cours) */
static float fx_vk = 1;
static void fx_veil(uint16_t c, float a) { a *= fx_vk; if (fx_a(a)) voile_ecran(c, fx_a(a)); }
static void fx_arc(float x, float y, float r, float a0, float a1, float w, uint16_t c, float a) { if (r > 0 && a > 0) ui_arc(x, y, r, a0, a1, w, c, fx_a(a)); }
/* Particule : hexagone de rayon r */
static void fx_dot(float x, float y, float r, uint16_t c, float a) {
    if (r <= 0 || !fx_a(a)) return;
    float q[12];
    for (int i = 0; i < 6; i++) { q[i * 2] = x + cosf(i * PI_F / 3) * r; q[i * 2 + 1] = y + sinf(i * PI_F / 3) * r; }
    ui_poly(q, 6, c, fx_a(a));
}
/* Halo de lumière de rayon r : disques de plus en plus clairs et opaques vers le centre */
static void fx_bloom(float x, float y, float r, uint16_t c, float a) {
    fx_circle(x, y, r, c, a * 0.18f); fx_circle(x, y, r * 0.66f, c, a * 0.28f);
    fx_circle(x, y, r * 0.4f, fx_mix(c, 0.5f), a * 0.5f); fx_circle(x, y, r * 0.2f, fx_mix(c, 0.9f), a);
}
/* n rayons de lumière autour de (x, y), de r0 à r0 + len (60 à 100 % de len), demi-largeur w au pied, tournés de rot */
static void fx_rays(float x, float y, int n, float r0, float len, float w, float rot, uint16_t c, float a, int s) {
    if (!fx_a(a)) return;
    for (int i = 0; i < n; i++) {
        const float g = rot + i * 2 * PI_F / n + (fx_h(s, i) - 0.5f) * 0.3f, l = len * (0.6f + 0.4f * fx_h(s, i + 30)), cg = cosf(g), sg = sinf(g);
        const float q[6] = { x + cg * r0 - sg * w, y + sg * r0 + cg * w, x + cg * r0 + sg * w, y + sg * r0 - cg * w, x + cg * (r0 + l), y + sg * (r0 + l) };
        ui_poly(q, 3, c, fx_a(a));
    }
}
/* Langue de flamme : pied en (x, y), pointe dans la direction (ux, uy) (unitaire) à h, demi-largeur w ; elle ondule
   (ph : phase) ; trois couches : bord de la couleur, milieu plus clair, cœur presque blanc */
static void fx_flame(float x, float y, float ux, float uy, float w, float h, float ph, uint16_t c, float a) {
    static const float V[7] = { 0, 0.08f, 0.2f, 0.38f, 0.58f, 0.8f, 1 }, HW[7] = { 0.55f, 0.9f, 1, 0.85f, 0.6f, 0.3f, 0 };
    if (h <= 0 || w <= 0 || !fx_a(a)) return;
    const float px = -uy, py = ux;
    for (int L = 0; L < 3; L++) {
        const float sh = L == 0 ? 1 : L == 1 ? 0.75f : 0.5f, sw = L == 0 ? 1 : L == 1 ? 0.62f : 0.32f;
        float q[26];   /* côté gauche du pied à la pointe (0 à 6), côté droit de la pointe au pied (6 à 12) */
        for (int i = 0; i < 7; i++) {
            const float v = V[i], hh = h * sh * v, o = sinf(ph + v * 3.2f) * w * 0.45f * v + sinf(ph * 1.7f) * w * 0.25f * v * v;
            const float mx = x + ux * hh + px * o, my = y + uy * hh + py * o, hw = w * sw * HW[i];
            q[i * 2] = mx - px * hw; q[i * 2 + 1] = my - py * hw;
            q[(12 - i) * 2] = mx + px * hw; q[(12 - i) * 2 + 1] = my + py * hw;
        }
        ui_poly(q, 13, L == 0 ? c : fx_mix(c, L == 1 ? 0.45f : 0.85f), fx_a(a * (L == 0 ? 0.85f : 1)));
    }
}
/* Éclair de (x0, y0) à (x1, y1) : ligne brisée de 8 segments (écart j au plus, nul aux bouts) et une branche de biais */
static void fx_bolt(float x0, float y0, float x1, float y1, float j, float w, uint16_t c, float a, int s) {
    const float dx = x1 - x0, dy = y1 - y0, L = fmaxf(1, hypotf(dx, dy)), nx = -dy / L, ny = dx / L;
    float lx = x0, ly = y0;
    for (int i = 1; i <= 8; i++) {
        const float u = i / 8.0f, o = (fx_h(s, i) - 0.5f) * 2 * j * sinf(PI_F * u), x = x0 + dx * u + nx * o, y = y0 + dy * u + ny * o;
        fx_glow(lx, ly, x, y, w, c, a);
        if (i == 4) {
            const float g = atan2f(dy, dx) + (fx_h(s, 20) < 0.5f ? -0.7f : 0.7f), bl = L * 0.3f;
            float bx = x, by = y;
            for (int m = 1; m <= 3; m++) {
                const float v = m / 3.0f, bo = (fx_h(s, 20 + m) - 0.5f) * j, ex = x + cosf(g) * bl * v - sinf(g) * bo, ey = y + sinf(g) * bl * v + cosf(g) * bo;
                fx_glow(bx, by, ex, ey, w * 0.5f, c, a * 0.8f); bx = ex; by = ey;
            }
        }
        lx = x; ly = y;
    }
}
/* ---------- Sorts (effets puissants) : 12 éléments × 3 niveaux (FX_SORT) ----------
   Chaque élément est dessiné à son niveau 2 ; les niveaux 1 et 3 changent le temps (MG_TS : plus vif, plus ample), la
   taille (MG_KS), la force des voiles (MG_VS) et le nombre d'éléments (NB : éclairs, flammes, éclats, météores…) */
#define NB(a, b, c) (n == 1 ? (a) : n == 2 ? (b) : (c))
static void fx_magie(int fx, float t, float ax, float ay, float tx, float ty, uint16_t c, float k, int s, int onde, float fy) {
    const int m = FX_ELEM(fx), n = FX_NIV(fx);
    const uint16_t white = ui_rgb(WHITE);
    t /= MG_TS(n); k *= MG_KS(n); fx_vk = MG_VS(n);
    switch (m) {
    case MG_FOUDRE: {   /* ciel assombri, éclairs autour de l'ennemi puis un sur lui (flash à chacun), onde au sol */
        if (onde) fx_veil(fx_dark(c, 0.2f), 150 * fx_env(t, 0, 0.3f, 1.0f, 1.4f));
        const int nb = NB(1, 3, 5);
        for (int j = 0; j < nb; j++) {
            const int big = j == nb - 1;
            const float t0 = big ? 0.75f : 0.35f + j * 0.4f / (nb - 1), q = (t - t0) / (big ? 0.2f : 0.14f);
            if (q < 0 || q > 1) continue;
            const float x = tx + (big ? 0 : (fx_h(s, j) - 0.5f) * 90 * k), y = ty + (big ? 0 : 14 * k);
            const float a = 255 * (1 - q) * (0.6f + 0.4f * fx_h(s + (int)floorf(t * 60), j));
            fx_bolt(x + (fx_h(s, j + 5) - 0.5f) * 120, -20, x, y, 28 * k, (big ? 7 : 4) * k, c, a, s + j * 13 + (int)floorf(t * 30));
            fx_bloom(x, y, (big ? 80 : 45) * k, c, a);
            if (onde) fx_veil(fx_mix(c, 0.7f), (big ? 170 : 110) * (1 - q));
        }
        const float e = (t - 0.75f) / 0.6f;
        if (e >= 0 && e <= 1) {
            if (fx_a(255 * (1 - e))) ui_ellipse_ring(tx, fy, (30 + 130 * e) * k, (10 + 40 * e) * k, 4 * (1 - e) + 1, c, fx_a(255 * (1 - e)));
            for (int i = 0; i < NB(6, 10, 16); i++) {   /* étincelles qui retombent */
                const float g = PI_F + fx_h(s, i + 40) * PI_F, v = (60 + 90 * fx_h(s, i + 60)) * k;
                fx_dot(tx + cosf(g) * v * e, fy + sinf(g) * v * e + 160 * e * e, 2.5f * k, fx_mix(c, 0.7f), 255 * (1 - e));
            }
        }
        fx_impact(tx, ty, (t - 0.75f) / 0.35f, c, 1.6f * k, s);
        break;
    }
    case MG_BRASIER: {   /* boule de feu chargée au-dessus du lanceur, lancée en cloche avec sa traînée ; explosion, puis colonne
                            de flammes qui enveloppe l'ennemi, lueur au sol, braises */
        if (onde) fx_veil(fx_dark(c, 0.25f), 130 * fx_env(t, 0, 0.35f, 1.35f, 1.65f));
        const float ox = ax, oy = ay - 30 * k;
        if (t < 0.45f) {
            const float e0 = fx_01(t / 0.4f);
            for (int i = 0; i < 12; i++) {   /* braises aspirées */
                const float ph = fmodf(t * 1.8f + fx_h(s, i), 1), g = fx_h(s, i + 12) * 6.2832f, r = (80 - 70 * ph) * k;
                fx_dot(ox + cosf(g) * r, oy + sinf(g) * r, 2.5f * k, fx_mix(c, 0.5f), 255 * ph);
            }
            fx_bloom(ox, oy, (14 + 30 * e0) * k, c, 255 * e0);
            const int nc = NB(3, 5, 7);
            for (int i = 0; i < nc; i++) {   /* flammes de la boule */
                const float g = (i - (nc - 1) / 2.0f) * 0.35f;
                fx_flame(ox + sinf(g) * 10 * k * e0, oy, sinf(g), -cosf(g), 7 * k * e0, (22 + 12 * fx_h(s, i + 24)) * k * e0, t * 14 + i * 1.3f, c, 255 * e0);
            }
            fx_circle(ox, oy, 9 * k * e0, fx_mix(c, 0.8f), 255 * e0);
        }
        const float u = (t - 0.45f) / 0.25f;
        if (u >= 0 && u <= 1) {   /* vol : flammes tirées vers l'arrière */
            const float x = ox + (tx - ox) * u, y = oy + (ty - oy) * u - 90 * u * (1 - u), vx = tx - ox, vy = ty - oy - 90 * (1 - 2 * u), vl = fmaxf(1, hypotf(vx, vy));
            for (int j = 0; j < 3; j++) {
                const float g = (j - 1) * 0.3f, bx = -vx / vl, by = -vy / vl, dxj = bx * cosf(g) - by * sinf(g), dyj = bx * sinf(g) + by * cosf(g);
                fx_flame(x, y, dxj, dyj, (j == 1 ? 13 : 9) * k, (j == 1 ? 60 : 40) * k, t * 20 + j * 2, c, 255);
            }
            fx_bloom(x, y, 30 * k, c, 255);
            fx_circle(x, y, 9 * k, fx_mix(c, 0.85f), 255);
        }
        const float e = (t - 0.7f) / 0.8f;
        if (e >= 0 && e <= 1) {
            fx_rays(tx, ty, 10, 20 * k, (80 + 50 * e) * k, 5 * k, t * 0.8f, fx_mix(c, 0.5f), 200 * (1 - e * 2), s);
            fx_bloom(tx, ty, (60 + 60 * e) * k, c, 255 * (1 - e));
            fx_ring(tx, ty, (20 + 150 * e) * k, 8 * (1 - e) + 1, c, 255 * (1 - e * 1.5f));
        }
        const float f = fx_env(t, 0.7f, 0.8f, 1.35f, 1.65f);   /* colonne de flammes */
        if (f > 0) {
            float el[32];   /* lueur au sol */
            for (int i = 0; i < 16; i++) { el[i * 2] = tx + cosf(i * PI_F / 8) * 60 * k; el[i * 2 + 1] = fy + sinf(i * PI_F / 8) * 16 * k; }
            ui_poly(el, 16, fx_mix(c, 0.3f), fx_a(150 * f));
            const int nf = NB(5, 9, 13);
            const float sp = 45 * k * (0.6f + 0.4f * nf / 9);
            for (int r = 0; r < 2; r++)   /* rang du fond (pairs), puis rang de devant */
                for (int i = r; i < nf; i += 2) {
                    const float d = (i - (nf - 1) / 2.0f) / ((nf - 1) / 2.0f), g = -d * 0.25f;
                    const float h = (90 + 75 * fx_h(s, i + 120)) * k * f * (1 - 0.35f * fabsf(d)) * (0.85f + 0.15f * sinf(t * 23 + i * 1.7f));
                    fx_flame(tx + d * sp * (0.8f + 0.2f * fx_h(s, i + 135)), fy - (r ? 0 : 6 * k), sinf(g), -cosf(g), 18 * k, h, t * 12 + i * 2.1f, c, 210 * fx_01(f * 1.5f));   /* un peu translucides */
                }
            for (int i = 0; i < NB(10, 16, 24); i++) {   /* braises qui montent */
                const float ph = fmodf(t * 1.2f + fx_h(s, i + 150), 1);
                fx_dot(tx + (fx_h(s, i + 180) - 0.5f) * 90 * k + sinf(t * 6 + i) * 6 * k, fy - ph * 160 * k, 2 * k, (i & 1) ? fx_mix(c, 0.6f) : c, 255 * f * (1 - ph));
            }
        }
        if (e >= 0 && e <= 1 && onde) fx_veil(fx_mix(c, 0.6f), 200 * (1 - e * 4));
        break;
    }
    case MG_GIVRE: {   /* éclats de glace qui poussent au pied de l'ennemi, cristal autour de lui, gel (flash), puis il vole en éclats */
        if (onde) fx_veil(fx_dark(c, 0.3f), 130 * fx_env(t, 0, 0.3f, 1.0f, 1.4f));
        const float g = fx_01((t - 0.15f) / 0.45f);
        const int ns = NB(4, 7, 11);
        if (t < 0.8f && g > 0)
            for (int i = 0; i < ns; i++) {
                const float th = -PI_F / 2 + (i - (ns - 1) / 2.0f) * 2.94f / ns + (fx_h(s, i) - 0.5f) * 0.2f, l = (45 + 50 * fx_h(s, i + 12)) * k * g, w = 8 * k;
                const float bx = tx + (fx_h(s, i + 24) - 0.5f) * 50 * k, by = fy - 4 * k, cg = cosf(th), sg = sinf(th);
                const float q[8] = { bx, by, bx + cg * l * 0.35f - sg * w, by + sg * l * 0.35f + cg * w, bx + cg * l, by + sg * l,
                                     bx + cg * l * 0.35f + sg * w, by + sg * l * 0.35f - cg * w };
                ui_poly(q, 4, fx_mix(c, 0.3f), 220);
                fx_line(bx, by, bx + cg * l, by + sg * l, 1.5f, fx_mix(c, 0.9f), 255);
            }
        for (int i = 0; i < NB(5, 8, 12) && t < 0.8f; i++) {   /* givre qui scintille */
            const float ph = fmodf(t * 2.5f + fx_h(s, i + 36), 1);
            fx_dot(tx + (fx_h(s, i + 48) - 0.5f) * 110 * k, ty + (fx_h(s, i + 60) - 0.5f) * 90 * k - 20 * ph * k, 2 * k, fx_mix(c, 0.8f), 255 * sinf(PI_F * ph) * fx_01(t / 0.2f));
        }
        const float gp = fx_01((t - 0.5f) / 0.1f);   /* l'ennemi pris dans un cristal de glace */
        if (gp > 0 && t < 0.8f) {
            const float h = 50 * k, w = 30 * k, cy2 = ty + 5 * k;
            const float pr[12] = { tx, cy2 - h, tx + w, cy2 - h * 0.55f, tx + w, cy2 + h * 0.55f, tx, cy2 + h * 0.8f, tx - w, cy2 + h * 0.55f, tx - w, cy2 - h * 0.55f };
            ui_poly(pr, 6, fx_mix(c, 0.4f), fx_a(120 * gp));
            ui_outline(pr, 6, 2, fx_mix(c, 0.9f), fx_a(220 * gp));
            fx_line(tx, cy2 - h, tx, cy2 + h * 0.8f, 1.5f, fx_mix(c, 0.9f), 120 * gp);
            fx_line(tx - w, cy2 - h * 0.55f, tx + w * 0.4f, cy2 + h * 0.2f, 1.5f, white, 160 * gp);
        }
        const float q = (t - 0.6f) / 0.2f;
        if (q >= 0 && q <= 1) { fx_bloom(tx, ty, 70 * k, c, 255 * (1 - q)); if (onde) fx_veil(fx_mix(c, 0.8f), 140 * (1 - q)); }
        const float e = (t - 0.8f) / 0.6f;
        if (e >= 0 && e <= 1) {
            fx_rays(tx, ty, NB(7, 10, 14), 16 * k, 70 * k, 3 * k, 0.3f, white, 255 * (1 - e * 2), s);
            for (int i = 0; i < NB(7, 12, 18); i++) {   /* éclats projetés, qui retombent en tournant */
                const float a0 = fx_h(s, i + 72) * 6.2832f, v = (70 + 110 * fx_h(s, i + 90)) * k, x = tx + cosf(a0) * v * e, y = ty + sinf(a0) * v * e + 220 * e * e;
                const float r = (5 + 5 * fx_h(s, i + 108)) * k * (1 - e * 0.5f), rot = e * 9 + i;
                const float tri[6] = { x + cosf(rot) * r, y + sinf(rot) * r, x + cosf(rot + 2.3f) * r * 0.6f, y + sinf(rot + 2.3f) * r * 0.6f,
                                       x + cosf(rot + 3.9f) * r * 0.6f, y + sinf(rot + 3.9f) * r * 0.6f };
                if (fx_a(255 * (1 - e))) ui_poly(tri, 3, (i & 1) ? c : fx_mix(c, 0.7f), fx_a(255 * (1 - e)));
            }
        }
        fx_impact(tx, ty, (t - 0.8f) / 0.35f, c, 1.4f * k, s);
        break;
    }
    case MG_SACRE: {   /* obscurité, faisceaux du ciel qui convergent, sphère de lumière qui emplit l'écran, rayons, croix */
        if (onde) fx_veil(ui_rgb(0), 190 * fx_env(t, 0, 0.35f, 1.15f, 1.6f));
        const float gc = fx_01((t - 0.15f) / 0.6f);
        if (t >= 0.15f && t < 0.8f) {
            const float a = 255 * fx_01((t - 0.15f) / 0.15f);
            const int nb = NB(3, 5, 7);
            for (int i = 0; i < nb; i++) {
                const float x = tx + (i - (nb - 1) / 2.0f) * 65 * k * (1 - gc), w = 14 * k;
                if (fx_a(a * 0.35f)) ui_rect(x - w / 2, -10, w, ty + 10, c, fx_a(a * 0.35f));
                if (fx_a(a * 0.8f)) ui_rect(x - w / 6, -10, w / 3, ty + 10, fx_mix(c, 0.8f), fx_a(a * 0.8f));
            }
            for (int i = 0; i < NB(6, 10, 16); i++) {   /* poussière de lumière qui descend */
                const float ph = fmodf(t * 1.6f + fx_h(s, i), 1);
                fx_dot(tx + (fx_h(s, i + 16) - 0.5f) * 200 * k * (1 - gc), ty - 220 * (1 - ph), 2 * k, fx_mix(c, 0.7f), a * ph);
            }
        }
        const float e = (t - 0.75f) / 0.85f;
        if (e >= 0 && e <= 1) {
            fx_rays(tx, ty, NB(10, 18, 26), 30 * k, (150 + 100 * e) * k, 6 * k, t * 0.6f, fx_mix(c, 0.7f), 220 * (1 - e), s);
            fx_rays(tx, ty, NB(7, 12, 18), 20 * k, (110 + 80 * e) * k, 3 * k, -t * 0.9f, fx_mix(c, 0.9f), 200 * (1 - e), s + 5);
            fx_bloom(tx, ty, (40 + 260 * e) * k, fx_mix(c, 0.3f), 255 * (1 - e));
            fx_ring(tx, ty, (30 + 220 * e) * k, 6 * (1 - e) + 1, fx_mix(c, 0.5f), 255 * (1 - e));
            if (e < 0.6f) {
                const float l = 160 * k, a = 255 * (1 - e / 0.6f);
                fx_glow(tx, ty - l, tx, ty + l * 0.6f, 6 * k, c, a); fx_glow(tx - l * 0.6f, ty - l * 0.2f, tx + l * 0.6f, ty - l * 0.2f, 6 * k, c, a);
            }
            if (onde) fx_veil(white, 230 * (1 - e * 3));
        }
        break;
    }
    case MG_METEORE: {   /* ciel rougi, météores en biais autour de l'ennemi, puis le plus gros sur lui */
        if (onde) fx_veil(fx_dark(c, 0.25f), 150 * fx_env(t, 0, 0.3f, 1.25f, 1.65f));
        static const float OX[4] = { -50, 45, -75, 70 }, OY[4] = { 20, 25, -5, 0 };
        const int nb = NB(1, 3, 5);
        for (int j = 0; j < nb; j++) {
            const int big = j == nb - 1;
            const float lt = big ? 0.9f : 0.6f + j * 0.3f / (nb - 1);
            const float px2 = tx + (big ? 0 : OX[j] * k), py2 = ty + (big ? 0 : OY[j] * k), sx = px2 + 200, sy = py2 - 330, u = (t - lt + 0.35f) / 0.35f;
            if (u >= 0 && u < 1) {
                const float hx = sx + (px2 - sx) * u, hy = sy + (py2 - sy) * u, L2 = hypotf(px2 - sx, py2 - sy), vx = (px2 - sx) / L2, vy = (py2 - sy) / L2;
                fx_glow(hx - vx * 90 * k, hy - vy * 90 * k, hx, hy, (big ? 12 : 8) * k, c, 220);
                for (int i = 0; i < 4; i++) {
                    const float b = (i + 1) * 22 * k;
                    fx_dot(hx - vx * b + (fx_h(s, j * 8 + i) - 0.5f) * 14, hy - vy * b + (fx_h(s, j * 8 + i + 4) - 0.5f) * 14, 3 * k, fx_mix(c, 0.5f), 200 - i * 40);
                }
                fx_bloom(hx, hy, (big ? 30 : 22) * k, c, 255);
            }
            const float e = (t - lt) / 0.5f;
            if (e >= 0 && e <= 1) {
                const float mm = big ? 1.5f : 1;
                if (big) fx_rays(px2, py2, NB(8, 12, 16), 20 * k, (80 + 60 * e) * k, 5 * k, 0.2f, fx_mix(c, 0.6f), 220 * (1 - e), s);
                fx_bloom(px2, py2, (40 + 70 * e) * k * mm, c, 255 * (1 - e));
                fx_ring(px2, py2, (14 + 90 * e) * k * mm, 6 * (1 - e) + 1, c, 255 * (1 - e));
                for (int i = 0; i < 8; i++) {   /* débris */
                    const float g = PI_F + fx_h(s, j * 16 + i + 40) * PI_F, v = (60 + 100 * fx_h(s, j * 16 + i + 48)) * k * mm, w = e * 0.6f;
                    fx_dot(px2 + cosf(g) * v * w, py2 + sinf(g) * v * w + 260 * w * w, 3 * k * (1 - e) + 1, (i & 1) ? c : fx_mix(c, 0.6f), 255 * (1 - e));
                }
                if (onde) fx_veil(fx_mix(c, 0.6f), (big ? 180 : 110) * (1 - e * 4));
            }
        }
        break;
    }
    case MG_NEANT: {   /* obscurité, trou noir qui aspire la lumière en spirale, implosion puis déflagration */
        if (onde) fx_veil(fx_dark(c, 0.15f), 200 * fx_env(t, 0, 0.45f, 1.1f, 1.55f));
        const float g = fx_01((t - 0.15f) / 0.6f) * (t < 0.9f ? 1 : fx_01(1 - (t - 0.9f) / 0.06f));
        if (g > 0) {
            for (int i = 0; i < NB(8, 14, 22); i++) {
                const float ph = fmodf(t * 1.5f + fx_h(s, i), 1), r = (200 - 180 * ph) * k, an = fx_h(s, i + 24) * 6.2832f + ph * 3;
                fx_dot(tx + cosf(an) * r, ty + sinf(an) * r * 0.8f, 2.5f * k, fx_mix(c, 0.6f), 255 * ph * g);
            }
            const float ph = fmodf(t * 2.5f, 1);   /* anneaux qui se resserrent */
            fx_ring(tx, ty, (160 - 140 * ph) * k, 2, c, 160 * g * ph);
            for (int i = 0; i < NB(2, 3, 4); i++) {
                const float a0 = t * (4 + i) + i * 2.1f;
                fx_arc(tx, ty, (40 + 14 * i) * k * g, a0, a0 + 2.2f, (5 - i) * k, i ? c : fx_mix(c, 0.5f), 230 * g);
            }
            fx_circle(tx, ty, 32 * k * g, ui_rgb(0x050008), 255);
            fx_ring(tx, ty, 32 * k * g, 3, fx_mix(c, 0.6f), 255 * g);
        }
        const float e = (t - 0.9f) / 0.6f;
        if (e >= 0 && e <= 1) {
            fx_rays(tx, ty, NB(8, 12, 16), 10 * k, (120 + 80 * e) * k, 5 * k, -0.4f, c, 220 * (1 - e), s);
            fx_bloom(tx, ty, (30 + 200 * e) * k, c, 255 * (1 - e));
            fx_ring(tx, ty, (20 + 200 * e) * k, 8 * (1 - e) + 1, fx_mix(c, 0.5f), 255 * (1 - e));
            if (onde) fx_veil(fx_mix(c, 0.7f), 200 * (1 - e * 3));
        }
        break;
    }
    case MG_POISON: {   /* air vicié, nuée toxique qui tourne autour de l'ennemi, bulles qui montent et crèvent, giclée, gouttes */
        if (onde) fx_veil(fx_dark(c, 0.3f), 140 * fx_env(t, 0, 0.3f, 1.1f, 1.5f));
        const float g = fx_01((t - 0.15f) / 0.4f), f = fx_env(t, 0.15f, 0.55f, 1.05f, 1.45f);
        const uint16_t violet = ui_mix(c, ui_rgb(0x6A2A8A), 0.6f);
        for (int i = 0; i < NB(4, 6, 9); i++) {   /* nuée */
            const float a0 = fx_h(s, i) * 6.2832f + t * 0.9f * ((i & 1) ? 1 : -1), r = (22 + 22 * fx_h(s, i + 10)) * k * (0.6f + 0.4f * g);
            fx_circle(tx + cosf(a0) * r, ty + sinf(a0) * r * 0.6f + 10 * k, (20 + 14 * fx_h(s, i + 20)) * k * g, (i & 1) ? c : violet, 90 * f);
        }
        for (int i = 0; i < NB(8, 14, 22); i++) {   /* bulles */
            const float ph = fmodf(t * 0.9f + fx_h(s, i + 30), 1), x = tx + (fx_h(s, i + 54) - 0.5f) * 100 * k + sinf(t * 5 + i) * 6 * k, y = fy - ph * 140 * k;
            const float r = (3 + 5 * fx_h(s, i + 78)) * k * (0.5f + ph), a = 230 * f * (ph < 0.85f ? 1 : (1 - ph) / 0.15f);
            fx_ring(x, y, r, 1.5f, fx_mix(c, 0.3f), a); fx_dot(x - r * 0.35f, y - r * 0.35f, r * 0.3f, fx_mix(c, 0.9f), a);
        }
        const float e = (t - 0.75f) / 0.45f;
        if (e >= 0 && e <= 1) {   /* giclée : les bulles crèvent toutes ensemble */
            fx_bloom(tx, ty, (40 + 50 * e) * k, c, 220 * (1 - e));
            for (int i = 0; i < NB(6, 10, 14); i++) {
                const float a0 = fx_h(s, i + 102) * 6.2832f, d = (20 + 50 * fx_h(s, i + 116)) * k * (0.3f + e);
                fx_ring(tx + cosf(a0) * d, ty + sinf(a0) * d * 0.7f, (4 + 14 * e) * k, 2 * (1 - e) + 1, (i & 1) ? c : violet, 255 * (1 - e));
            }
            if (onde) fx_veil(fx_mix(c, 0.5f), 120 * (1 - e * 3));
        }
        const float e2 = (t - 0.8f) / 0.6f;
        if (e2 >= 0 && e2 <= 1)
            for (int i = 0; i < NB(4, 6, 9); i++) {   /* gouttes qui coulent */
                const float v = fx_01(e2 * 1.4f - fx_h(s, i + 130) * 0.4f), x = tx + (fx_h(s, i + 140) - 0.5f) * 50 * k, y = ty - 25 * k + v * v * (fy - ty + 25 * k);
                fx_flame(x, y, 0, -1, 3.5f * k, 9 * k, 0, (i & 1) ? c : violet, 255 * (1 - e2));
            }
        fx_impact(tx, ty, (t - 0.75f) / 0.35f, c, 1.2f * k, s);
        break;
    }
    case MG_TERRE: {   /* le sol se fend autour de l'ennemi, puis des pics de roche jaillissent, débris et poussière ; ils s'effritent */
        if (onde) fx_veil(fx_dark(c, 0.25f), 140 * fx_env(t, 0, 0.3f, 1.15f, 1.5f));
        const uint16_t clair = fx_mix(c, 0.25f), ombre = fx_dark(c, 0.55f);
        const int nf = NB(4, 6, 9);
        const float gf = fx_01((t - 0.12f) / 0.4f), af = 255 * fx_env(t, 0.12f, 0.2f, 1.1f, 1.45f);
        for (int i = 0; i < nf; i++) {   /* fissures : lignes brisées couchées sur le sol */
            const float a0 = i * 6.2832f / nf + fx_h(s, i) * 0.5f, L = (60 + 50 * fx_h(s, i + 10)) * k * gf;
            float x0 = tx, y0 = fy;
            for (int j = 1; j <= 4; j++) {
                const float v = j / 4.0f, o = (fx_h(s, i * 4 + j + 20) - 0.5f) * 0.5f, x1 = tx + cosf(a0 + o) * L * v, y1 = fy + sinf(a0 + o) * L * v * 0.3f;
                fx_line(x0, y0, x1, y1, 3 * (1 - v) + 1, ui_rgb(0x1A0E06), af); fx_line(x0, y0, x1, y1, 1, fx_mix(c, 0.6f), af * 0.6f);
                x0 = x1; y0 = y1;
            }
        }
        const int np = NB(3, 5, 8);
        const float gp = fx_01((t - 0.55f) / 0.1f), dp = fx_01(1 - (t - 1.1f) / 0.35f);
        if (gp > 0 && dp > 0)
            for (int r = 0; r < 2; r++)   /* pics de roche : rang du fond, puis de devant */
                for (int i = r; i < np; i += 2) {
                    const float d = (i - (np - 1) / 2.0f) / ((np - 1) / 2.0f), x = tx + d * 50 * k, y = fy - (r ? -4 : 6) * k;
                    const float h = (55 + 45 * fx_h(s, i + 60)) * k * gp * dp * (1 - 0.3f * fabsf(d)), w = (11 + 6 * fx_h(s, i + 70)) * k, lean = d * 14 * k;
                    const float L1[6] = { x - w, y, x + lean, y - h, x, y }, L2[6] = { x, y, x + lean, y - h, x + w, y };
                    ui_poly(L1, 3, clair, 255); ui_poly(L2, 3, ombre, 255);
                    fx_line(x + lean, y - h, x - w, y, 1.5f, fx_mix(c, 0.6f), 200);
                }
        const float e = (t - 0.55f) / 0.8f;
        if (e >= 0 && e <= 1) {
            for (int i = 0; i < NB(6, 10, 15); i++) {   /* débris projetés */
                const float g = PI_F + fx_h(s, i + 80) * PI_F, v = (80 + 120 * fx_h(s, i + 96)) * k, w2 = e * 0.7f;
                const float x = tx + cosf(g) * v * w2, y = fy - 10 * k + sinf(g) * v * w2 + 300 * w2 * w2, r = (3 + 4 * fx_h(s, i + 112)) * k, rot = e * 7 + i;
                const float qd[8] = { x + cosf(rot) * r, y + sinf(rot) * r, x + cosf(rot + 1.6f) * r, y + sinf(rot + 1.6f) * r,
                                      x + cosf(rot + 3.3f) * r, y + sinf(rot + 3.3f) * r, x + cosf(rot + 4.8f) * r, y + sinf(rot + 4.8f) * r };
                if (fx_a(255 * (1 - e))) ui_poly(qd, 4, (i & 1) ? clair : ombre, fx_a(255 * (1 - e)));
            }
            for (int i = 0; i < 6; i++) {   /* poussière */
                const float a0 = i * 1.05f + fx_h(s, i + 130), d = (30 + 70 * e) * k;
                fx_circle(tx + cosf(a0) * d, fy - 8 * k + sinf(a0) * d * 0.25f, (18 + 22 * e) * k, ui_rgb(0x9A8A78), 110 * (1 - e));
            }
            if (onde) fx_veil(fx_mix(c, 0.5f), 120 * (1 - e * 5));
        }
        fx_impact(tx, ty, (t - 0.6f) / 0.35f, c, 1.2f * k, s);
        break;
    }
    case MG_EAU: {   /* gouttes qui se rassemblent en sphère au-dessus de l'ennemi, elle tombe, geyser qui jaillit du sol, gerbes, écume */
        if (onde) fx_veil(fx_dark(c, 0.3f), 130 * fx_env(t, 0, 0.3f, 1.15f, 1.5f));
        const float sx = tx, sy = ty - 70 * k;
        if (t < 0.55f) {
            const float g = fx_01((t - 0.05f) / 0.4f);
            for (int i = 0; i < NB(8, 12, 18); i++) {   /* gouttes attirées */
                const float ph = fx_01(g * 1.3f - fx_h(s, i) * 0.3f), a0 = fx_h(s, i + 20) * 6.2832f, r = (110 - 100 * ph) * k;
                fx_dot(sx + cosf(a0) * r, sy + sinf(a0) * r, 3 * k, fx_mix(c, 0.5f), 255 * ph);
            }
            const float d = fx_01((t - 0.45f) / 0.1f), R = 22 * k * g * (1 + 0.08f * sinf(t * 30)) * (1 - d * 0.4f), y = sy + d * (ty - sy);
            fx_circle(sx, y, R, c, 200 * g); fx_ring(sx, y, R, 2, fx_mix(c, 0.7f), 255 * g);
            fx_dot(sx - R * 0.35f, y - R * 0.4f, R * 0.22f, white, 230 * g);
        }
        const float f = fx_env(t, 0.55f, 0.65f, 1.05f, 1.4f);
        if (f > 0) {   /* geyser */
            float el[32];
            for (int i = 0; i < 16; i++) { el[i * 2] = tx + cosf(i * PI_F / 8) * 55 * k; el[i * 2 + 1] = fy + sinf(i * PI_F / 8) * 15 * k; }
            ui_poly(el, 16, fx_mix(c, 0.4f), fx_a(160 * f));
            const int nj = NB(3, 5, 7);
            for (int r = 0; r < 2; r++)
                for (int i = r; i < nj; i += 2) {
                    const float d = (i - (nj - 1) / 2.0f) / ((nj - 1) / 2.0f), g = -d * 0.18f;
                    const float h = (110 + 50 * fx_h(s, i + 40)) * k * f * (1 - 0.4f * fabsf(d)) * (0.9f + 0.1f * sinf(t * 17 + i));
                    fx_flame(tx + d * 30 * k, fy - (r ? 0 : 5 * k), sinf(g), -cosf(g), 15 * k, h, t * 6 + i * 1.7f, c, 200 * fx_01(f * 1.5f));
                }
            for (int i = 0; i < NB(10, 16, 24); i++) {   /* gerbes : gouttes qui retombent */
                const float ph = fmodf(t * 1.4f + fx_h(s, i + 50), 1), a0 = -PI_F / 2 + (fx_h(s, i + 76) - 0.5f) * 2.2f, v = (90 + 80 * fx_h(s, i + 102)) * k;
                fx_dot(tx + cosf(a0) * v * ph, fy - 100 * k * f + sinf(a0) * v * ph + 200 * ph * ph * k, 2.5f * k, (i & 1) ? fx_mix(c, 0.6f) : white, 255 * f * (1 - ph));
            }
        }
        const float e = (t - 0.6f) / 0.6f;
        if (e >= 0 && e <= 1) {   /* écume */
            if (fx_a(255 * (1 - e))) ui_ellipse_ring(tx, fy, (30 + 120 * e) * k, (8 + 34 * e) * k, 5 * (1 - e) + 1, fx_mix(c, 0.8f), fx_a(255 * (1 - e)));
            if (onde) fx_veil(fx_mix(c, 0.6f), 140 * (1 - e * 4));
        }
        fx_impact(tx, ty, (t - 0.62f) / 0.35f, c, 1.3f * k, s);
        break;
    }
    case MG_VENT: {   /* rafales qui traversent l'écran, tornade qui s'élève autour de l'ennemi, feuilles emportées, entailles */
        if (onde) fx_veil(fx_dark(c, 0.25f), 110 * fx_env(t, 0, 0.3f, 1.15f, 1.5f));
        for (int i = 0; i < NB(4, 7, 10); i++) {   /* rafales */
            const float q = (t - 0.05f - fx_h(s, i) * 0.5f) / 0.3f;
            if (q < 0 || q > 1) continue;
            const float y = ty + (fx_h(s, i + 12) - 0.5f) * 220, x = -60 + q * 600, l = 80 + 80 * fx_h(s, i + 24);
            fx_glow(x - l, y + 10, x, y, 2, fx_mix(c, 0.6f), 200 * sinf(PI_F * q));
        }
        const float g = fx_01((t - 0.15f) / 0.35f), f = fx_env(t, 0.15f, 0.5f, 1.1f, 1.4f), HT = 150 * k * g;
        if (f > 0) {
            const int nr = NB(5, 7, 10);
            for (int j = 0; j < nr; j++) {   /* tornade : anneaux de plus en plus larges vers le haut, particules qui tournent */
                const float v = (float)j / (nr - 1), y = fy - v * HT, rx = (10 + 50 * v) * k, ry = rx * 0.3f, x = tx + sinf(t * 5 + v * 3) * 8 * k * v;
                if (fx_a(110 * f)) ui_ellipse_ring(x, y, rx, ry, 2 + 2 * v, fx_mix(c, 0.4f), fx_a(110 * f));
                for (int q = 0; q < 3; q++) {
                    const float a0 = t * 9 + j * 0.7f + q * 2.094f, z = sinf(a0);
                    fx_dot(x + cosf(a0) * rx, y + z * ry, (z > 0 ? 3 : 2) * k, z > 0 ? white : fx_mix(c, 0.5f), 255 * f * (z > 0 ? 1 : 0.5f));
                }
            }
            for (int i = 0; i < NB(5, 8, 12); i++) {   /* feuilles et débris emportés */
                const float ph = fmodf(t * 0.8f + fx_h(s, i + 36), 1), a0 = t * 7 + i * 1.3f, rx = (15 + 45 * ph) * k;
                const float x = tx + cosf(a0) * rx, y = fy - ph * HT, r = 3.5f * k, rot = t * 11 + i;
                const float lf[8] = { x + cosf(rot) * r * 1.6f, y + sinf(rot) * r * 1.6f, x + cosf(rot + 1.57f) * r * 0.6f, y + sinf(rot + 1.57f) * r * 0.6f,
                                      x - cosf(rot) * r * 1.6f, y - sinf(rot) * r * 1.6f, x - cosf(rot + 1.57f) * r * 0.6f, y - sinf(rot + 1.57f) * r * 0.6f };
                if (fx_a(230 * f)) ui_poly(lf, 4, (i & 1) ? ui_rgb(0x7DBF4A) : ui_rgb(0xC8B060), fx_a(230 * f));
            }
        }
        for (int j = 0; j < NB(2, 3, 5); j++) {   /* entailles du vent */
            const float p = (t - 0.7f - 0.06f * j) / 0.2f;
            if (p < 0 || p > 1) continue;
            const float a0 = fx_h(s, j + 50) * PI_F, L = 60 * k, ca = cosf(a0), sa = sinf(a0), u = fminf(1, p * 3);
            fx_glow(tx - ca * L, ty - sa * L, tx - ca * L + ca * 2 * L * u, ty - sa * L + sa * 2 * L * u, 3 * k * (1 - p) + 1, fx_mix(c, 0.5f), 255 * (1 - p));
        }
        const float e = (t - 0.7f) / 0.4f;
        if (e >= 0 && e <= 1 && onde) fx_veil(fx_mix(c, 0.7f), 110 * (1 - e * 4));
        fx_impact(tx, ty, (t - 0.72f) / 0.35f, c, 1.2f * k, s);
        break;
    }
    case MG_NEUTRE: {   /* énergie pure : anneaux qui se resserrent, sceaux géométriques qui tournent, point de lumière, dôme qui s'étend */
        if (onde) fx_veil(fx_dark(c, 0.12f), 170 * fx_env(t, 0, 0.35f, 1.2f, 1.6f));
        const float g = fx_01((t - 0.1f) / 0.5f);
        if (t < 0.8f) {
            const int na = NB(2, 3, 4);
            for (int i = 0; i < na; i++) {   /* anneaux qui se resserrent */
                const float ph = fmodf(t * 1.8f + (float)i / na, 1);
                fx_ring(tx, ty, (150 - 130 * ph) * k, 2, c, 200 * ph * g);
            }
            for (int j = 0; j < NB(1, 2, 3); j++) {   /* sceaux : deux triangles en étoile, qui tournent en sens contraires */
                const float R = (55 - 12 * j) * k * g, rot = t * ((j & 1) ? -2.5f : 2) + j;
                for (int tr = 0; tr < 2; tr++) {
                    float q[6];
                    for (int v = 0; v < 3; v++) { const float a0 = rot + tr * PI_F / 3 + v * 2.094f; q[v * 2] = tx + cosf(a0) * R; q[v * 2 + 1] = ty + sinf(a0) * R; }
                    if (fx_a(230 * g)) ui_outline(q, 3, 2, j ? fx_mix(c, 0.5f) : c, fx_a(230 * g));
                }
                fx_ring(tx, ty, R, 1.5f, c, 160 * g);
            }
            fx_bloom(tx, ty, 18 * k * g, c, 255 * g);
        }
        const float q0 = (t - 0.7f) / 0.1f;
        if (q0 >= 0 && q0 <= 1) fx_circle(tx, ty, 6 * k * (1 - q0) + 2, white, 255);   /* tout se ramasse en un point */
        const float e = (t - 0.8f) / 0.8f;
        if (e >= 0 && e <= 1) {   /* dôme d'énergie */
            const float R = (30 + 230 * fx_01(e * 1.6f)) * k;
            fx_circle(tx, ty, R, c, 70 * (1 - e)); fx_circle(tx, ty, R * 0.8f, fx_mix(c, 0.4f), 60 * (1 - e));
            fx_ring(tx, ty, R, 6 * (1 - e) + 2, white, 255 * (1 - e)); fx_ring(tx, ty, R * 0.92f, 3, c, 200 * (1 - e));
            fx_ring(tx, ty, R * 0.6f, 2, fx_mix(c, 0.5f), 150 * (1 - e));
            fx_rays(tx, ty, NB(8, 12, 16), 10 * k, R * 0.9f, 2 * k, 0.1f, white, 180 * (1 - e), s);
            if (onde) fx_veil(white, 220 * (1 - e * 3));
        }
        break;
    }
    case MG_SOIN: {   /* lumière douce sur l'allié : anneau au sol, colonne de lumière, croix et étincelles qui montent, halo */
        if (onde) fx_veil(fx_dark(c, 0.35f), 70 * fx_env(t, 0, 0.3f, 1.0f, 1.35f));
        const float f = fx_env(t, 0.1f, 0.35f, 0.95f, 1.3f);
        if (f > 0) {
            float el[32];
            for (int i = 0; i < 16; i++) { el[i * 2] = tx + cosf(i * PI_F / 8) * 34 * k; el[i * 2 + 1] = fy + sinf(i * PI_F / 8) * 11 * k; }
            ui_poly(el, 16, fx_mix(c, 0.4f), fx_a(90 * f));
            if (fx_a(220 * f)) ui_ellipse_ring(tx, fy, (34 + 6 * sinf(t * 6)) * k, 11 * k, 3, c, fx_a(220 * f));
            const float w = 26 * k, h = fy - ty + 130 * k;   /* colonne de lumière */
            if (fx_a(70 * f)) ui_rect(tx - w / 2, ty - 130 * k, w, h, c, fx_a(70 * f));
            if (fx_a(120 * f)) ui_rect(tx - w / 6, ty - 130 * k, w / 3, h, fx_mix(c, 0.8f), fx_a(120 * f));
            for (int i = 0; i < NB(8, 12, 18); i++) {   /* croix et étincelles qui montent */
                const float ph = fmodf(t * 0.9f + fx_h(s, i), 1), x = tx + (fx_h(s, i + 20) - 0.5f) * 60 * k + sinf(t * 4 + i) * 5 * k, y = fy - ph * 150 * k;
                const float a = 255 * f * sinf(PI_F * ph), r = 5 * k;
                if (i % 3 == 0) { fx_line(x - r, y, x + r, y, 2.5f * k, white, a); fx_line(x, y - r, x, y + r, 2.5f * k, white, a); fx_dot(x, y, 2 * k, c, a); }
                else fx_dot(x, y, (1.5f + 1.5f * fx_h(s, i + 40)) * k, (i & 1) ? fx_mix(c, 0.7f) : c, a);
            }
        }
        const float e = (t - 0.55f) / 0.5f;   /* le soin : halo, anneau qui s'ouvre */
        if (e >= 0 && e <= 1) {
            fx_bloom(tx, ty, (40 + 40 * e) * k, c, 230 * (1 - e));
            fx_ring(tx, ty, (20 + 70 * e) * k, 3 * (1 - e) + 1, fx_mix(c, 0.6f), 255 * (1 - e));
            if (onde) fx_veil(fx_mix(c, 0.7f), 60 * (1 - e * 4));
        }
        break;
    }
    }
    fx_vk = 1;
}
#undef NB
/* Effet fx du point (ax, ay) vers (tx, ty) (fy : pied de la cible) ; balayage : onde vers le centre visé (cx, cy),
   dessinée une seule fois (onde), l'éclat sur chaque cible ; effets puissants : voiles d'écran dessinés une seule fois
   (onde) ; sorts : fx_magie */
static void fx_draw(int fx, float t, float ax, float ay, float tx, float ty, uint16_t c, float k, int s, float cx, float cy, int onde, float fy) {
    if (fx >= FX_MAGIE) { fx_magie(fx, t, ax, ay, tx, ty, c, k, s, onde, fy); return; }
    const float dx = tx - ax, dy = ty - ay, L = fmaxf(1, hypotf(dx, dy)), ux = dx / L, uy = dy / L, px = -uy, py = ux, P = PI_F;
    const uint16_t white = ui_rgb(WHITE);
    switch (fx) {
    case FX_IMPACT: {   /* un trait en travers, puis l'éclat */
        const float e = (t - 0.33f) / 0.3f;
        if (e >= 0 && e <= 1) fx_glow(tx - 26 * k * (1 - e), ty - 26 * k * (1 - e) * 0.6f, tx + 26 * k, ty + 26 * k * 0.6f, 4 * (1 - e) + 1, c, 255 * (1 - e));
        fx_impact(tx, ty, e, c, k, s);
        break;
    }
    case FX_TAILLE: {   /* croissants qui se tracent puis s'effacent */
        static const float G[4] = { 0.75f, 2.4f, 0.1f, 1.57f };
        const int n = k > 1 ? 4 : 3;
        for (int j = 0; j < n; j++) {
            const float p = (t - 0.28f - 0.07f * j) / 0.2f;
            if (p < 0 || p > 1) continue;
            const float R = 58 * k, nrm = G[j] + P / 2, ccx = tx - cosf(nrm) * R * 0.75f, ccy = ty - sinf(nrm) * R * 0.75f;
            const float um = fminf(1, p * 2.5f), a = 255 * (p < 0.4f ? 1 : 1 - (p - 0.4f) / 0.6f);
            for (int pass = 0; pass < 2; pass++) {
                const float sc = pass ? 1 : 2.2f;
                float xy[52];   /* bord extérieur, puis bord intérieur parcouru à l'envers */
                for (int i = 0; i <= 12; i++) {
                    const float u = um * i / 12, th = nrm - 0.75f + 1.5f * u, w = 7 * k * sinf(P * u) * sc;
                    xy[i * 2] = ccx + cosf(th) * (R + w); xy[i * 2 + 1] = ccy + sinf(th) * (R + w);
                    xy[(25 - i) * 2] = ccx + cosf(th) * (R - w); xy[(25 - i) * 2 + 1] = ccy + sinf(th) * (R - w);
                }
                const int aa = fx_a(pass ? a : a * 0.35f);
                if (aa) ui_poly(xy, 26, pass ? fx_mix(c, 0.85f) : c, aa);
            }
        }
        fx_impact(tx, ty, (t - 0.36f) / 0.3f, c, k, s);
        break;
    }
    case FX_FOUET: {   /* lanière qui ondule jusqu'à l'ennemi, claque, revient */
        const float e = t < 0.18f ? 0 : t < 0.33f ? (t - 0.18f) / 0.15f : t < 0.5f ? 1 - (t - 0.33f) / 0.17f * 0.9f : 0;
        if (e > 0) {
            float x0 = ax, y0 = ay;
            for (int i = 1; i <= 14; i++) {
                const float u = i / 14.0f, amp = 22 * k * (1 - u * 0.6f) * (t < 0.33f ? 1 : 0.4f), wv = sinf(u * 9.4f - t * 40) * amp * sinf(P * u);
                const float x = ax + dx * u * e + px * wv, y = ay + dy * u * e + py * wv;
                fx_line(x0, y0, x, y, 4.5f - 3 * u, c, 255); x0 = x; y0 = y;
            }
        }
        const float e2 = (t - 0.36f) / 0.25f;
        if (e2 >= 0 && e2 <= 1)
            for (int i = 0; i < 6; i++) {
                const float g = i * P / 3 + fx_h(s, i), r = 30 * k * (0.3f + e2);
                fx_line(tx + cosf(g) * r * 0.3f, ty + sinf(g) * r * 0.3f, tx + cosf(g) * r, ty + sinf(g) * r, 3, fx_mix(c, 0.7f), 255 * (1 - e2));
            }
        fx_impact(tx, ty, (t - 0.36f) / 0.3f, c, 0.8f * k, s);
        break;
    }
    case FX_TIR: {   /* rafale de balles traçantes */
        const int n = k > 1 ? 5 : 3;
        for (int j = 0; j < n; j++) {
            const float q = t - 0.25f - 0.08f * j, mx = ax + ux * 14, my = ay + uy * 14;
            if (q >= 0 && q < 0.06f) {
                fx_circle(mx, my, 9 * k * (1 - q / 0.06f), fx_mix(c, 0.8f), 255);
                for (int sg = -1; sg <= 1; sg += 2) {
                    const float g = 0.5f * sg;
                    fx_line(mx, my, mx + (ux * cosf(g) - uy * sinf(g)) * 16, my + (uy * cosf(g) + ux * sinf(g)) * 16, 2, c, 255 * (1 - q / 0.06f));
                }
            }
            const float f = q / 0.07f;
            if (f >= 0 && f <= 1) { const float hx = ax + dx * f, hy = ay + dy * f; fx_glow(hx - ux * 40, hy - uy * 40, hx, hy, 3, c, 255); }
            fx_impact(tx + (fx_h(s, j) - 0.5f) * 30, ty + (fx_h(s, j + 9) - 0.5f) * 30, (q - 0.07f) / 0.25f, c, 0.6f * k, s + j);
        }
        break;
    }
    case FX_OBUS: {   /* départ, projectile en cloche avec traînée, explosion et débris */
        const float q = (t - 0.2f) / 0.1f, mx = ax + ux * 16, my = ay + uy * 16;
        if (q >= 0 && q <= 1) { fx_circle(mx, my, 16 * k * (1 - q), fx_mix(c, 0.8f), 255); fx_ring(mx, my, 10 + 30 * q, 3, c, 255 * (1 - q)); }
        const float u = (t - 0.2f) / 0.3f;
        if (u >= 0 && u <= 1)
            for (int i = 3; i >= 0; i--) {
                const float v = u - i * 0.05f;
                if (v < 0) continue;
                fx_circle(ax + dx * v, ay + dy * v - 360 * v * (1 - v), (6 - i) * k, i ? c : fx_mix(c, 0.8f), 255 * (1 - i * 0.25f));
            }
        const float e = (t - 0.5f) / 0.45f;
        if (e >= 0 && e <= 1) {
            fx_circle(tx, ty, 46 * k * (1 - e), fx_mix(c, 0.6f * (1 - e)), 255 * (1 - e));
            fx_ring(tx, ty, (14 + 86 * e) * k, 7 * (1 - e) + 1, c, 255 * (1 - e));
            for (int i = 0; i < 12; i++) {
                const float g = fx_h(s, i) * 6.2832f, v = (80 + 140 * fx_h(s, i + 20)) * k, w = e * 0.45f;
                fx_circle(tx + cosf(g) * v * w, ty + sinf(g) * v * w + 260 * w * w, 3 * (1 - e) + 1, (i & 1) ? c : fx_mix(c, 0.6f), 255 * (1 - e));
            }
        }
        break;
    }
    case FX_FLECHE: {   /* vol en cloche, puis plantée avec un reflet */
        const float u = (t - 0.25f) / 0.17f;
        float bx, by;
        if (u >= 0 && u <= 1) {
            const float vx = dx, vy = dy - 112 * (1 - 2 * u), vl = fmaxf(1, hypotf(vx, vy)), cx2 = vx / vl, cy2 = vy / vl;
            fx_arrow(ax + dx * u, ay + dy * u - 112 * u * (1 - u), cx2, cy2, c, 255, &bx, &by);
            fx_line(bx - cx2 * 30, by - cy2 * 30, bx - cx2 * 8, by - cy2 * 8, 1, c, 120);
        }
        const float e = (t - 0.42f) / 0.35f;
        if (e >= 0 && e <= 1) {
            const float vy = dy + 112, vl = fmaxf(1, hypotf(dx, vy)), g = 22 * k * sinf(P * e);
            fx_arrow(tx, ty, dx / vl, vy / vl, c, 255 * (1 - e), &bx, &by);
            fx_line(tx - g, ty, tx + g, ty, 2, fx_mix(c, 0.9f), 255 * (1 - e)); fx_line(tx, ty - g, tx, ty + g, 2, fx_mix(c, 0.9f), 255 * (1 - e));
        }
        fx_impact(tx, ty, (t - 0.42f) / 0.3f, c, 0.7f * k, s);
        break;
    }
    case FX_ARCANE: {   /* cercle de runes qui tourne, particules aspirées, colonne de lumière */
        const float e0 = fminf(1, t / 0.4f);
        if (t < 0.75f) {
            const float a = t < 0.45f ? 255 * e0 : 255 * fmaxf(0, 1 - (t - 0.45f) / 0.3f), rot = t * 3, R = 41 * k * e0;
            fx_ring(tx, ty, 48 * k * e0, 2, c, a); fx_ring(tx, ty, 34 * k * e0, 1.5f, c, a * 0.7f);
            float rx[6], ry[6];
            for (int i = 0; i < 6; i++) { const float g = rot + i * P / 3; rx[i] = tx + cosf(g) * R; ry[i] = ty + sinf(g) * R; }
            for (int i = 0; i < 6; i++) {
                fx_line(rx[i], ry[i], rx[(i + 2) % 6], ry[(i + 2) % 6], 1.5f, c, a * 0.6f);
                const float d[8] = { rx[i], ry[i] - 5, rx[i] + 4, ry[i], rx[i], ry[i] + 5, rx[i] - 4, ry[i] };
                if (fx_a(a)) ui_poly(d, 4, fx_mix(c, 0.5f), fx_a(a));
            }
            if (t < 0.45f) {
                for (int i = 0; i < 10; i++) {
                    const float ph = fmodf(t * 2.2f + fx_h(s, i), 1), g = fx_h(s, i + 10) * 6.2832f, r = (70 - 60 * ph) * k;
                    fx_circle(tx + cosf(g) * r, ty + sinf(g) * r, 2.5f, fx_mix(c, 0.6f), 255 * ph);
                }
                fx_circle(ax, ay - 10, 6 + 4 * sinf(t * 30), c, 120); fx_circle(ax, ay - 10, 3, fx_mix(c, 0.9f), 255);
            }
        }
        const float e = (t - 0.45f) / 0.35f;
        if (e >= 0 && e <= 1) {
            const float w = 32 * k * (1 - e);
            if (fx_a(150 * (1 - e))) ui_rect(tx - w / 2, ty - 150, w, 170, c, fx_a(150 * (1 - e)));
            if (fx_a(255 * (1 - e))) ui_rect(tx - w / 6, ty - 150, w / 3, 170, fx_mix(c, 0.9f), fx_a(255 * (1 - e)));
            fx_ring(tx, ty, (20 + 70 * e) * k, 3, c, 255 * (1 - e));
        }
        break;
    }
    case FX_FRAPPE: {   /* coups étoilés successifs */
        const int n = k > 1 ? 4 : 3;
        for (int j = 0; j < n; j++) {
            const float e = (t - 0.3f - 0.08f * j) / 0.22f;
            if (e < 0 || e > 1) continue;
            const float x = tx + (fx_h(s, j) - 0.5f) * 36, y = ty + (fx_h(s, j + 5) - 0.5f) * 36, g0 = fx_h(s, j + 11);
            float xy[32];
            for (int i = 0; i < 16; i++) {
                const float r = (i & 1) ? 11 * k : 30 * k * (1 - 0.5f * e), g = i * P / 8 + g0;
                xy[i * 2] = x + cosf(g) * r; xy[i * 2 + 1] = y + sinf(g) * r;
            }
            if (fx_a(255 * (1 - e))) ui_poly(xy, 16, fx_mix(c, 0.5f), fx_a(255 * (1 - e)));
            fx_circle(x, y, 9 * k * (1 - e), white, 255 * (1 - e));
            fx_ring(x, y, (12 + 40 * e) * k, 2, c, 255 * (1 - e));
        }
        break;
    }
    case FX_LASER: {   /* charge, rayon qui pulse, étincelles au point touché */
        const float q = (t - 0.15f) / 0.2f, mx = ax + ux * 12, my = ay + uy * 12;
        if (q >= 0 && q <= 1) { fx_circle(mx, my, 4 + 10 * q * k, c, 160); fx_circle(mx, my, 2 + 5 * q, fx_mix(c, 0.9f), 255); }
        const float e = (t - 0.35f) / 0.3f;
        if (e >= 0 && e <= 1) {
            const float w = 9 * k * (1 - e * e) * (0.8f + 0.2f * sinf(t * 90));
            const int s2 = s + (int)floorf(t * 30);
            fx_glow(mx, my, tx, ty, w, c, 255); fx_circle(tx, ty, w * 1.4f, fx_mix(c, 0.8f), 255);
            for (int i = 0; i < 5; i++) {
                const float g = fx_h(s2, i) * 6.2832f, r = (12 + 22 * fx_h(s2, i + 5)) * k;
                fx_line(tx, ty, tx + cosf(g) * r, ty + sinf(g) * r, 2, fx_mix(c, 0.5f), 255);
            }
        }
        break;
    }
    case FX_BALAYAGE: {   /* onde de feu en arc vers le centre visé, éclat sur la cible */
        const float e = (t - 0.2f) / 0.4f, Dx = cx - ax, Dy = cy - ay, dist = fmaxf(1, hypotf(Dx, Dy)), th = atan2f(Dy, Dx);
        if (onde && e >= 0 && e <= 1) {
            const float r = dist * e * 1.15f, a = 255 * (e < 0.7f ? 1 : 1 - (e - 0.7f) / 0.3f), w = 16 * k * (1 - e * 0.5f);
            if (r > 0 && fx_a(a)) {
                ui_arc(ax, ay, r, th - 0.55f, th + 0.55f, w, c, fx_a(a * 0.6f));
                ui_arc(ax, ay, r, th - 0.5f, th + 0.5f, w * 0.35f, fx_mix(c, 0.7f), fx_a(a));
            }
            for (int i = 0; i < 10; i++) {
                const float g = th + (fx_h(s, i) - 0.5f) * 1.1f, rr = r + (fx_h(s, i + 10) - 0.5f) * 20, up = fmodf(t * 4 + fx_h(s, i + 20), 1) * 14;
                fx_circle(ax + cosf(g) * rr, ay + sinf(g) * rr - up, 4 * k * (1 - e * 0.5f), (i & 1) ? c : fx_mix(c, 0.5f), a);
            }
        }
        fx_impact(tx, ty, (t - 0.45f) / 0.3f, c, 0.8f * k, s);
        break;
    }
    }
}
/* Énergie par-dessus le cut-in d'une transformation ou d'une attaque ultime (BT.cut.special) : rayons qui tournent depuis
   le centre, étincelles qui montent, onde de choc, éclairs blancs ; bande de l'image de y à y + bh, opacité v */
static void cutin_energie(float t, float y, float bh, float v) {
    const uint16_t c = ui_rgb(0xFFD54A), w = ui_rgb(WHITE);
    const float cx = UW / 2, cy = y + bh / 2;
    for (int i = 0; i < 16; i++) {
        const float a = i * 0.3927f + t * 0.6f, l = 240 + 120 * sinf(t * 7 + i);
        ui_line(cx + cosf(a) * 70, cy + sinf(a) * 70, cx + cosf(a) * l, cy + sinf(a) * l, 3, (i & 1) ? c : w, (int)(90 * v));
    }
    for (int i = 0; i < 28; i++) {
        const float ph = fmodf(t * (0.6f + (i * 37 % 10) * 0.07f) + i * 0.137f, 1.0f);
        ui_circle(UW * ((i * 53 % 97) / 97.0f), y + bh - ph * bh, 2 + (i % 3), (i % 4) ? c : w, (int)(220 * v * (1 - ph)));
    }
    const float r = fmodf(t, 0.9f) / 0.9f;
    ui_ring(cx, cy, 30 + r * 260, 4, w, (int)(200 * v * (1 - r)));
    if (fmodf(t, 0.45f) < 0.05f) ui_rect(0, y, UW, bh, w, (int)(90 * v));
}

/* Halo d'un personnage transformé (ÉVEIL), de la couleur de son aura (eveil_aura, celle des cut-ins) : lueur autour du
   corps, flammes de lumière qui montent sur ses bords, étincelles, anneau au sol (aura de l'émulateur) */
static void aura(const screen_pos_t *p, uint32_t rgb, float t) {
    const uint16_t c = ui_rgb(rgb), w = ui_rgb(WHITE);
    const float h = p->foot - p->top, cy = p->top + h * 0.52f, rx = h * 0.3f;
    for (int k = 0; k < 3; k++) ui_circle(p->cx, cy, h * (0.36f + 0.1f * k) + 2 * sinf(t * 1.5f + k), c, 34 - 9 * k);   /* lueur */
    for (int i = 0; i < 14; i++) {   /* flammes : traits qui montent le long de la silhouette */
        const float a = i * 0.4488f, ph = fmodf(t * 0.45f + i * 0.31f, 1.0f), sx = sinf(a) * rx * (1 - ph * 0.4f);
        const float y0 = p->foot - h * (0.08f + 0.85f * ph * (0.6f + 0.4f * fabsf(cosf(a)))), l = h * (0.12f + 0.08f * sinf(t * 3 + i));
        ui_line(p->cx + sx, y0, p->cx + sx * 0.85f, y0 - l, 3, (i % 3) ? c : w, (int)(200 * (1 - ph)));
    }
    for (int i = 0; i < 10; i++) {   /* étincelles */
        const float ph = fmodf(t * 0.3f + i * 0.173f, 1.0f), x = p->cx + sinf(i * 2.4f + t * 0.4f) * rx * 1.4f;
        ui_circle(x, p->foot - ph * h * 1.15f, 1.5f + (i % 3), (i & 1) ? c : w, (int)(230 * (1 - ph)));
    }
    ui_ellipse_ring(p->cx, p->foot, rx * 1.3f + 2 * sinf(t * 2), rx * 0.38f, 3, c, 200);
    ui_ellipse_ring(p->cx, p->foot, rx * 1.05f, rx * 0.3f, 1.5f, w, 160);
}
/* Halo des membres transformés (ÉVEIL) autour de leur sprite, en combat */
static void draw_auras(void) {
    for (int u = 0; u < NMEMB; u++)
        if (BT.transfo[u] && in_team(u) && E.scr_party[u].vis) aura(&E.scr_party[u], eveil_aura(u), E.clock * 4 + u);
}

/* Cut-in de la technique (BP_CUTIN) : voile sombre sur le combat, bandeau de l'image (à la définition de l'écran) qui
   entre par la droite, glisse lentement et sort à gauche, liserés de la couleur du thème, lignes de vitesse au-dessus et
   au-dessous, nom de la technique en bas à droite ; plein écran (BT.cut.plein : image carrée, CUT_S_PLEIN) : nom en bas,
   au milieu ; paire face / dos (BT.cut.paire, CUT_S_PAIRE) : le dos à mi-temps, après un éclair blanc ; dégâts
   (BT.cut.reac, CUT_S_DEGATS) : sans nom */
static void draw_cutin(void) {
    if (BT.phase != BP_CUTIN) return;
    const float t = E.clock - BT.cut.t0, d = CUT_DUREE();
    const float in = fminf(1, t / CUT_IN), out = fmaxf(0, fminf(1, (t - (d - CUT_OUT)) / CUT_OUT)), v = in * (1 - out);
    voile_ecran(ui_rgb(0x05040A), (int)(160 * v));
    int iw, ih;
    const uint16_t *pal;
    /* paire face / dos : la face, puis le dos à mi-temps (éclair blanc) */
    const int dos = BT.cut.paire && t >= d * 0.5f;
    if (BT.cut.paire && !img_paire(BT.cut.img, 1)) return;
    const uint8_t *ix = img_ix(BT.cut.img + dos, &iw, &ih, &pal, 1);
    if (!ix) return;
    const float s = (UW - 2) / iw, bw = iw * s, bh = ih * s, y = (PORTRAIT ? UH * 0.42f : CY) - bh / 2;
    const float e = 1 - (1 - in) * (1 - in), x = (UW - bw) / 2 + (1 - e) * UW - out * out * UW + (0.5f - t) * 16;
    for (int i = 0; i < 12; i++) {   /* lignes de vitesse vers la gauche */
        const float ly = i < 6 ? y - 12 - i * 16 : y + bh + 12 + (i - 6) * 16, len = 50 + (i * 37) % 90;
        const float lx = UW - fmodf(t * (900 + i * 45) + i * 97, UW + len);
        ui_line(lx, ly, lx + len, ly, 2, ui_rgb(WHITE), (int)(150 * v));
    }
    ui_image_pal(ix, pal, iw, ih, x, y, s, UW / 2, UH / 2, PORTRAIT ? UW + UH : 233);
    ui_rect(0, y - 4, UW, 4, C1, (int)(255 * v));
    ui_rect(0, y + bh, UW, 4, C1, (int)(255 * v));
    /* nom de la technique en bas à droite du bandeau (la bannière cacherait le visage) */
    const char *nom = BT.cut.act.name ? BT.cut.act.name : "";
    if (dos && t < d * 0.5f + 0.15f) ui_rect(0, y, UW, bh, ui_rgb(WHITE), (int)(220 * (1 - (t - d * 0.5f) / 0.15f)));
    if (BT.cut.special) cutin_energie(t, y, bh, v);
    if (BT.cut.plein) text_outlined(ui_fit(FONT_DISP, nom, 300, 38, 24), UW / 2, (PORTRAIT ? UH * 0.42f : CY) + 150, ui_rgb(WHITE), (int)(255 * v), nom);
    else text_outlined(ui_fit(FONT_DISP, nom, 280, 34, 22), UW / 2 + 70, y + bh - 22, ui_rgb(WHITE), (int)(255 * v), nom);
}

/* Départ d'un effet lancé par un membre de l'équipe, vu de dos : là où la droite de son centre vers la cible sort de sa
   silhouette (boîte de demi-largeur FX_DOS_L × sa hauteur, du haut de la tête aux pieds). Avant le 02/11/2026, le départ
   du tir (éclair, balles traçantes), de l'obus ou du sort était dessiné sur son dos (fxDepart de l'émulateur) */
#define FX_DOS_L 0.25f
static void fx_depart(const screen_pos_t *p, float tx, float ty, float *ax, float *ay) {
    const float dx = tx - p->cx, dy = ty - p->cy, L = fmaxf(1, hypotf(dx, dy)), ux = dx / L, uy = dy / L;
    float s = L;
    if (fabsf(ux) > 1e-3f) s = fminf(s, FX_DOS_L * (p->foot - p->top) / fabsf(ux));
    if (uy < -1e-3f) s = fminf(s, (p->cy - p->top) / -uy); else if (uy > 1e-3f) s = fminf(s, (p->foot - p->cy) / uy);
    *ax = p->cx + ux * s; *ay = p->cy + uy * s;
}
/* Effet de l'action en cours : de l'attaquant (membre : le bord de sa silhouette, fx_depart ; ennemi : le point visé)
   vers la cible ou chacune */
static void draw_battle_fx(void) {
    if (BT.phase != BP_ANIM || BT.an.fx < 0) return;
    const float t = E.clock - BT.an.t0;
    if (t < 0) return;   /* élan : pas encore au contact */
    const int en = BT.an.who >= BT_ENEMY;
    const screen_pos_t *from = en ? &E.scr_enemy[(BT.an.who - BT_ENEMY) % BT_EMAX] : &E.scr_party[BT.an.who];
    if (!from->vis) return;
    const screen_pos_t *T[4];
    int n = 0;
    for (int i = 0; i < BT.an.ntarget && n < 4; i++) {   /* ennemi, soin : l'équipe ; sinon les ennemis touchés */
        const int u = BT.an.targets[i];
        const screen_pos_t *p = u >= BT_ENEMY ? &E.scr_enemy[(u - BT_ENEMY) % BT_EMAX] : &E.scr_party[u];
        if (p->vis) T[n++] = p;
    }
    if (!n) return;
    float cx = 0, cy = 0, fy = 0;
    for (int i = 0; i < n; i++) { cx += T[i]->cx / n; cy += T[i]->cy / n; fy += T[i]->foot / n; }
    /* Sort d'attaque sur plusieurs cibles : dessiné une fois, au milieu d'elles (un soin : sur chacune) */
    float ax = from->cx, ay = from->cy;
    if (BT.an.fx >= FX_MAGIE && FX_ELEM(BT.an.fx) != MG_SOIN && n > 1) {
        if (!en) fx_depart(from, cx, cy, &ax, &ay);
        fx_draw(BT.an.fx, t, ax, ay, cx, cy, bt_col(BT.an.col), BT.an.k, BT.an.seed, cx, cy, 1, fy);
        return;
    }
    for (int i = 0; i < n; i++) {
        if (!en) fx_depart(from, T[i]->cx, T[i]->cy, &ax, &ay);
        fx_draw(BT.an.fx, t, ax, ay, T[i]->cx, T[i]->cy, bt_col(BT.an.col), BT.an.k, BT.an.seed + i * 7, cx, cy, i == 0, T[i]->foot);
    }
}

static void draw_battle_hud(void) {
    const float now = E.clock;
    char t[64];
    /* Ennemi montré (nom, niveau, barre) : celui qui agit pendant son tour, sinon l'ennemi visé */
    const int em = (BT.phase == BP_ENEMY || BT.phase == BP_ANIM) && BT.actor >= BT_ENEMY ? (BT.actor - BT_ENEMY) % BT_EMAX : BT.cible > 0 ? BT.cible : 0;
    const bt_en_t *ev = &BT.en[em];
    const int debout = battle_vivants() > 0;
    /* Groupe : sous chaque ennemi debout, sa barre de PV (comme les barres de l'équipe) et son numéro à droite ; l'ennemi visé
       (pendant le choix d'une action) : anneau rouge qui pulse sous lui, flèche à hauteur de sa tête (comme le membre qui
       choisit) */
    if ((BT.nen > 1 || BT.phase == BP_ALLIE) && BT.visible && BT.phase != BP_INTRO)
        for (int k = 0; k < BT.nen; k++) {
            const screen_pos_t *es = &E.scr_enemy[k];
            if (!es->vis || BT.en[k].hp <= 0) continue;
            const int vise = BT.phase == BP_ALLIE ? BT.allie == BT_ENEMY + k : k == BT.cible && bt_choix();
            const char l[2] = { (char)('1' + k), 0 };
            const float by = es->foot + 9;
            if (BT.nen > 1) {
                ui_text(ui_font(FONT_DISP, 16), es->cx + 27, by + 5, UI_LEFT, k == em ? ui_rgb(WHITE) : ui_rgb(0xFF8A96), 255, l);
                seg_bar(es->cx - 21, by - 2, 44, 4, (float)BT.en[k].hp / BT.en[k].max, ui_rgb(RED_HEX));
            }
            if (vise) {
                const float p = sinf(now * 6);
                static const float FL[14] = { 0, 0, 11, -12, 4, -12, 4, -24, -4, -24, -4, -12, -11, -12 };   /* pointe en bas */
                float q[14];
                const float hy = fmaxf(30, es->cy - (es->foot - es->top) * 0.3f) + 3 * p;   /* haut de la cellule : souvent vide au-dessus de la tête */
                for (int i = 0; i < 7; i++) { q[2 * i] = es->cx + FL[2 * i]; q[2 * i + 1] = hy + FL[2 * i + 1]; }
                ui_poly(q, 7, ui_rgb(RED_HEX), 255); ui_outline(q, 7, 2, ui_rgb(INK), 255);
            }
        }
#if ENG_DYN
    /* Plein écran : l'ennemi montré et sa jauge en haut sur toute la largeur, les 7 prochains tours en frise dessous */
    if (BT.visible && BT.phase != BP_INTRO) {   /* entrée : le cut-in seul */
        ui_rect(0, 0, UW, 150, ui_rgb(INK), 140);
        if (BT.nen > 1) snprintf(t, sizeof t, "%d · %s", em + 1, ev->name); else snprintf(t, sizeof t, "%s", ev->name);
        ui_text(ui_fit(FONT_DISP, t, UW - 160, 24, 16), 24, 44, UI_LEFT, ui_rgb(0xE8F6FF), 255, t);
        snprintf(t, sizeof t, "NIV. %d", BT.lvl);
        ui_text(ui_font(FONT_MONO, 18), UW - 24, 44, UI_RIGHT, ui_rgb(DIM), 255, t);
        jauge(24, 58, UW - 48, 10, ev->max > 0 ? (float)ev->hp / ev->max : 0, ui_rgb(RED_HEX));
    }
    if (debout && BT.phase != BP_INTRO && BT.phase != BP_LOSE) {
        int tl[7];
        const int n = battle_timeline(7, tl);
        for (int k = 0; k < n; k++) {
            const int big = k == 0, u = tl[k];
            const float x = big ? 48 : 108 + (k - 1) * 46, y = 110;
            const uint16_t uc = ui_rgb(unit_color(u));
            float q[12];
            ui_hex_path(x, y, big ? 46 : 34, big ? 40 : 30, q);
            ui_poly(q, 6, uc, 255);
            if (big) { ui_outline(q, 6, 8, uc, 70); ui_outline(q, 6, 2, ui_rgb(WHITE), 255); }
            const char l[2] = { unit_lettre(u), 0 };
            ui_text(ui_font(FONT_DISP, big ? 22 : 17), x, y + (big ? 8 : 6), UI_CENTER, ui_rgb(INK), 255, l);
        }
    }
    if (0) {
#else
    if (!ENG_RECT) rim_ticks(0, 102);   /* (écran rectangulaire : pas de graduations du cadran) */
    {
#endif
    /* Timeline CTB sur l'arc en haut à droite : les 7 prochains tours, le premier en grand */
    if (debout && BT.phase != BP_INTRO && BT.phase != BP_LOSE) {
        ui_arc(CX, CY, 205, -100 * PI_F / 180, 10 * PI_F / 180, 1.5f, C1, 89);
        int tl[7];
        const int n = battle_timeline(7, tl);
        for (int k = 0; k < n; k++) {
            const float a = (-95 + k * (100.0f / 6)) * PI_F / 180, x = CX + cosf(a) * 205, y = CY + sinf(a) * 205;
            const int big = k == 0, u = tl[k];
            const uint16_t uc = ui_rgb(unit_color(u));
            float q[12];
            ui_hex_path(x, y, big ? 46 : 34, big ? 40 : 30, q);
            ui_poly(q, 6, uc, 255);
            if (big) { ui_outline(q, 6, 8, uc, 70); ui_outline(q, 6, 2, ui_rgb(WHITE), 255); }
            const char l[2] = { unit_lettre(u), 0 };
            ui_text(ui_font(FONT_DISP, big ? 22 : 17), x, y + (big ? 8 : 6), UI_CENTER, ui_rgb(INK), 255, l);
            if (big) ui_text(ui_font(FONT_DISP, 18), x + 4, y + 40, UI_CENTER, uc, 255, "NOW");
        }
    }
    /* Ennemi montré : nom, niveau, barre de PV à sa droite, à hauteur de sa tête (il est en haut à gauche, la place est
       libre jusqu'à la timeline) ; groupe : sa lettre devant son nom, en haut au-dessus des ennemis (leurs lettres et
       barres sont sur eux) ; pas pendant le cut-in d'entrée */
    if (BT.visible && BT.phase != BP_INTRO) {
        const screen_pos_t *es = &E.scr_enemy[em];
        float lx = (es->vis ? es->cx : 130) + 50, ly = (es->vis ? es->top : 110) + 18;
        lx = lx < 150 ? 150 : lx > 230 ? 230 : lx; ly = ly < 125 ? 125 : ly > 200 ? 200 : ly;   /* sous la timeline */
        if (BT.nen > 1) { lx = 96; ly = 96; }
        if (BT.nen > 1) snprintf(t, sizeof t, "%d · %s · NIV. %d", em + 1, ev->name, BT.lvl);
        else snprintf(t, sizeof t, "%s · NIV. %d", ev->name, BT.lvl);
        ui_text(ui_fit(FONT_DISP, t, 160, 20, 14), lx, ly, UI_LEFT, ui_rgb(0xE8F6FF), 255, t);
        seg_bar(lx, ly + 8, 150, 7, ev->max > 0 ? (float)ev->hp / ev->max : 0, ui_rgb(RED_HEX));
    }
    }
    /* Équipe : anneau sous celui qui choisit, barres de PV et de PM (bleue) sous chacun, KO */
    const int choosing = bt_choix();
    for (int i = 0; i < NMEMB; i++) {
        const screen_pos_t *cs = &E.scr_party[i];
        if (!cs->vis) continue;
        const member_t *m = &PARTY[i];
        const int act = BT.phase == BP_ALLIE ? BT.allie == i : BT.actor == i && choosing;
        if (act) {   /* celui qui choisit (ou le camarade proposé pour l'objet) : anneau qui pulse sous lui (render.c), flèche qui flotte au-dessus de sa tête */
            const float p = sinf(now * 6);
            static const float FL[14] = { 0, 0, 13, -14, 5, -14, 5, -28, -5, -28, -5, -14, -13, -14 };   /* pointe en bas */
            float q[14];
            const float hy = cs->top - 8 + 4 * p;
            for (int k = 0; k < 7; k++) { q[2 * k] = cs->cx + FL[2 * k]; q[2 * k + 1] = hy + FL[2 * k + 1]; }
            ui_poly(q, 7, C2, 255); ui_outline(q, 7, 2, ui_rgb(INK), 255);
        }
        const float r = (float)m->hp / m->max;
        seg_bar(cs->cx - 21, cs->foot + 10, 44, 4, r, m->hp <= 0 ? ui_rgb(0x555555) : r < 0.25f ? ui_rgb(RED_HEX) : act ? C2 : C1);
        if (m->maxmp > 0) seg_bar(cs->cx - 21, cs->foot + 16, 44, 3, (float)m->mp / m->maxmp, ui_rgb(PM_HEX));
        if (m->hp <= 0) ui_text(ui_font(FONT_DISP, 18), cs->cx, cs->foot + 36, UI_CENTER, ui_rgb(RED_HEX), 255, "KO");
    }
    draw_auras();
    draw_battle_fx();
#if ENG_DYN
    {
        int nm = 0;
        for (int i = 0; i < NMEMB; i++) nm += in_team(i);
        float y = BT_GRILLE_Y - 84 - nm * 36;
        for (int i = 0; i < NMEMB; i++) {
            if (!in_team(i)) continue;
            const member_t *m = &PARTY[i];
            const int act = BT.actor == i && choosing;
            const float r = m->max > 0 ? (float)m->hp / m->max : 0;
            snprintf(t, sizeof t, "%s", m->name);   /* le membre qui choisit : en couleur (Oxanium n'a pas ▸) */
            ui_text(ui_fit(FONT_DISP, t, 110, 20, 14), 24, y + 20, UI_LEFT, act ? C2 : ui_rgb(WHITE), 255, t);
            jauge(140, y + 6, UW - 300, 9, r, m->hp <= 0 ? ui_rgb(0x555555) : r < 0.25f ? ui_rgb(RED_HEX) : C2);
            if (m->maxmp > 0) jauge(140, y + 19, UW - 300, 5, (float)m->mp / m->maxmp, ui_rgb(PM_HEX));
            snprintf(t, sizeof t, "%d", m->hp);
            ui_text(ui_font(FONT_MONO, 16), UW - 24, y + 15, UI_RIGHT, ui_rgb(DIM), 255, t);
            snprintf(t, sizeof t, "%d PM", m->mp);
            ui_text(ui_font(FONT_MONO, 14), UW - 24, y + 31, UI_RIGHT, ui_rgb(PM_HEX), 255, t);
            y += 36;
        }
    }
    if (0) {
#else
    if (choosing && BT.actor >= 0 && BT.actor < BT_ENEMY) {
#endif
        const member_t *a = &PARTY[BT.actor];
        snprintf(t, sizeof t, "%s · PV %d · PM %d", a->name, a->hp, a->mp);
        const screen_pos_t *cs = &E.scr_party[BT.actor];   /* sous ses barres (au-dessus de sa tête, il passait sur le nom de l'ennemi) */
        const float x = cs->vis ? fminf(fmaxf(cs->cx, 190), 300) : 260, y = cs->vis ? fminf(cs->foot + 40, 400) : 380;
        text_outlined(ui_fit(FONT_DISP, t, 190, 20, 14), x, y, C2, 255, t);
    }
    /* Éventail de commandes */
    if (BT.phase != H.bphase || BT.actor != H.bactor) { H.bphase = BT.phase; H.bactor = BT.actor; H.bsel = -1; }
    memset(H.bslot_cmd, 0, sizeof H.bslot_cmd);
    const uint16_t green = ui_rgb(0x7DFF9A), dim = ui_rgb(DIM);
    if (BT.phase == BP_CMD) {
        fan_button(0, "ATTAQUE", "atk", C1, BC_ATTACK, 0, NULL, 0);
        fan_button(1, "TECHNIQ.", "tech", C1, BC_TECHS, 0, NULL, 0);
        fan_button(2, "OBJETS", "item", C1, BC_ITEMS, 0, NULL, 0);
        fan_button(3, "DÉFENSE", "equip", C1, BC_DEFEND, 0, NULL, 0);
        fan_button(4, "FUITE", "flee", C1, BC_FLEE, 0, NULL, 0);
    } else if (BT.phase == BP_TECH && BT.actor >= 0 && BT.actor < BT_ENEMY) {
        static const int SLOTS[2] = { 0, 1 };   /* places 0, 1, 3, 4 de l'émulateur : deux techniques au plus */
        const member_t *a = &PARTY[BT.actor];
        for (int k = 0; k < a->ntech && k < 2; k++) {
            const tech_t *tc = &a->tech[k];
            char sub[16];
            snprintf(sub, sizeof sub, "%d PM", tc->mp);
            fan_button(SLOTS[k], tc->name, NULL, tc->heal ? green : C1, BC_TECH, k, sub, a->mp < tc->mp);
        }
        /* MAGIE en 4 quand le membre connaît des sorts (job ou équipement) */
        int l[MAX_SORTS];
        const int ns = sorts_of(BT.actor, l);
        if (ns) { char sub[16]; snprintf(sub, sizeof sub, "%d SORT%s", ns, ns > 1 ? "S" : ""); fan_button(4, "MAGIE", NULL, ui_rgb(0xB07DFF), BC_MAGIES, 0, sub, 0); }
        /* transformation ultime en 3 : ÉVEIL, puis ULTIME une fois transformée */
        if (eveil_dispo(BT.actor)) {
            const int tr = BT.transfo[BT.actor];
            char sub[16];
            snprintf(sub, sizeof sub, "%d PM", ULTIME_PM);
            fan_button(3, tr ? "ULTIME" : "ÉVEIL", NULL, ui_rgb(0xFFD54A), tr ? BC_ULTIME : BC_EVEIL, 0, tr ? sub : "TRANSFO", tr && a->mp < ULTIME_PM);
        }
        fan_button(2, "RETOUR", "back", dim, BC_BACK, 0, NULL, 0);
    } else if (BT.phase == BP_MAGIE && BT.actor >= 0 && BT.actor < BT_ENEMY) {
        /* Sorts du membre, couleur de leur élément : quatre à la fois (places 0, 1, 3, 4), au-delà trois et SUITE en 4 */
        static const int SLOTS[4] = { 0, 1, 3, 4 };
        const member_t *a = &PARTY[BT.actor];
        int l[MAX_SORTS], first, np;
        const int n = sorts_of(BT.actor, l), c = sorts_page(n, &first, &np);
        for (int k = 0; k < c; k++) {
            const sort_t *sp = &E.w.sorts[l[first + k]];
            char sub[16];
            snprintf(sub, sizeof sub, "%d PM", sp->pm);
            fan_button(SLOTS[k], sp->nom, NULL, fx_couleur(sp->fx) ? ui_rgb(fx_couleur(sp->fx)) : C1, BC_SORT, k, sub, a->mp < sp->pm);
        }
        if (n > 4) { char sub[16]; snprintf(sub, sizeof sub, "%d / %d", first / 3 + 1, np); fan_button(4, "SUITE", "next", C1, BC_SUITE, 0, sub, 0); }
        fan_button(2, "RETOUR", "back", dim, BC_BACK, 0, NULL, 0);
    } else if (BT.phase == BP_ITEM) {
        /* Les soins du sac : quatre à la fois (places 0, 1, 3, 4), au-delà trois et SUITE en 4 ; RETOUR en 2 */
        static const int SLOTS[4] = { 0, 1, 3, 4 };
        uint8_t lo[MAX_ITEMS]; int first, np;
        const int n = battle_objets(lo), c = sorts_page(n, &first, &np);
        for (int k = 0; k < c; k++) {
            char sub[16];
            snprintf(sub, sizeof sub, "× %d", ITEMS[lo[first + k]].count);
            fan_button(SLOTS[k], ITEMS[lo[first + k]].name, NULL, ITEMS[lo[first + k]].type == OBJ_LANCER ? ui_rgb(0xFFB020) : green, BC_POTION, k, sub, 0);
        }
        if (!n) fan_button(1, "RIEN À DONNER", NULL, dim, BC_BACK, 0, NULL, 1);
        if (n > 4) { char sub[16]; snprintf(sub, sizeof sub, "%d / %d", first / 3 + 1, np); fan_button(4, "SUITE", "next", C1, BC_SUITE, 0, sub, 0); }
        fan_button(2, "RETOUR", "back", dim, BC_BACK, 0, NULL, 0);
    } else if (BT.phase == BP_ALLIE && BT.allie >= 0 && BT.obj < n_items) {
        /* À qui ? (objet, ou action en attente : obj -1, cible_choisir de battle.c) Son nom et son état ; SUIVANT, ou toucher son sprite (battle_tap) */
        char sub[24];
        if (BT.allie >= BT_ENEMY) {
            const bt_en_t *ce = &BT.en[(BT.allie - BT_ENEMY) % BT_EMAX];
            snprintf(sub, sizeof sub, "%d PV", ce->hp);
            fan_button(1, ce->name, NULL, ui_rgb(RED_HEX), BC_ALLIE_OK, 0, sub, 0);
        } else {
            const member_t *cq = &PARTY[BT.allie];
            if (BT.obj < 0 ? BT.pend_sorte == CIBLE_RELEVE : ITEMS[BT.obj].releve) snprintf(sub, sizeof sub, "À TERRE");   /* relève (08/11/2026) */
            else if (BT.obj < 0 || ITEMS[BT.obj].hp > 0) snprintf(sub, sizeof sub, "%d PV", cq->hp); else snprintf(sub, sizeof sub, "%d PM", cq->mp);
            fan_button(1, cq->name, NULL, green, BC_ALLIE_OK, 0, sub, 0);
        }
        fan_button(0, "SUIVANT", "next", C1, BC_ALLIE_SUIV, 0, NULL, 0);
        fan_button(2, "RETOUR", "back", dim, BC_BACK, 0, NULL, 0);
    } else if (BT.phase == BP_TARGET && E.scr_enemy[em].vis) {
        /* Viseur : cercle en tirets qui tourne (plus épais, avec un halo, une fois verrouillé), coins, panneau de la cible */
        const float x = E.scr_enemy[em].cx, y = E.scr_enemy[em].cy, R = 78, step = 28 / R, len = 18 / R;
        const float a0 = fmodf(now * 1000 / 25, 28) / R;
        for (float a = a0 - step; a < 2 * PI_F + a0 - step; a += step) {
            if (BT.locked) ui_arc(x, y, R, a, a + len, 10, C1, 60);
            ui_arc(x, y, R, a, a + len, BT.locked ? 3 : 2, C1, 255);
        }
        for (int c = 0; c < 4; c++) {
            const float sx = (c & 1) ? 1.0f : -1.0f, sy = (c & 2) ? 1.0f : -1.0f;
            ui_line(x + sx * 64, y + sy * 50, x + sx * 64, y + sy * 64, 2.5f, C1, 255);
            ui_line(x + sx * 64, y + sy * 64, x + sx * 50, y + sy * 64, 2.5f, C1, 255);
        }
        float q[12];
        /* Panneau de la cible : au centre, décalé vers la gauche (l'équipe est à droite) */
        const float px = PORTRAIT ? 24 : 40, py = PORTRAIT ? 170 : 222, pw = PORTRAIT ? UW - 48 : 220, pc = px + pw / 2;   /* plein écran : sous la frise */
        ui_chamfer_path(px, py, pw, 76, 12, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 240);
        ui_outline(q, 6, 1, C1, 255);
        snprintf(t, sizeof t, "CIBLE : %s", ev->name);
        ui_text(ui_fit(FONT_DISP, t, pw - 20, 22, 16), pc, py + 30, UI_CENTER, ui_rgb(0xE8F6FF), 255, t);
        if (BT.locked) snprintf(t, sizeof t, "▶ ENCORE : %s", BT.pending.name ? BT.pending.name : "");
        else snprintf(t, sizeof t, "TOUCHER POUR VERROUILLER");
        ui_text(ui_fit(FONT_MONO, t, pw - 20, 20, 14), pc, py + 60, UI_CENTER, BT.locked ? C2 : C1, 255, t);
        fan_button(2, "RETOUR", "back", dim, BC_BACK, 0, NULL, 0);
    }
    /* Clavier : à chaque changement de phase ou de membre, le premier bouton présent */
    /* (« À qui ? » : la cible proposée, OK la confirme ; 11/11/2026) */
    if (H.bsel < 0 || !H.bslot_cmd[H.bsel]) { H.bsel = BT.phase == BP_ALLIE && H.bslot_cmd[1] ? 1 : 0; while (H.bsel < 4 && !H.bslot_cmd[H.bsel]) H.bsel++; }
    draw_cutin();
    draw_intro_combat();
    draw_victoire();
    draw_defaite();
    /* Bannière : apparaît en 0,15 s ; tenue jusqu'au toucher, ou 1,3 s */
    if (BT.ban.on && (BT.ban.hold || now - BT.ban.t0 < 1.3f)) {
        const float k = fminf(1, (now - BT.ban.t0) / 0.15f);
        const ui_font_t *fs = ui_font(FONT_MONO, 18);
        char sub[2][72];
        const int ns = BT.ban.sub[0] ? wrap2(fs, BT.ban.sub, 330, sub) : 0;
        const uint16_t bc = bt_col(BT.ban.color);
        float q[12];
        const float bx = PORTRAIT ? 24 : 43, by = PORTRAIT ? 260 : 156, bw = PORTRAIT ? UW - 48 : 380, bcx = bx + bw / 2;
        ui_chamfer_path(bx, by, bw, 64 + ns * 24, 12, q);
        ui_poly(q, 6, ui_rgb(0x05040A), (int)(230 * k));
        ui_outline(q, 6, 2, bc, (int)(255 * k));
        const ui_font_t *ft = ui_fit(FONT_DISP, BT.ban.text, bw - 30, 32, 22);
        for (int d = 0; d < 4; d++) ui_text(ft, bcx + ((d & 1) ? 2 : -2), by + 44 + ((d & 2) ? 2 : -2), UI_CENTER, bc, (int)(80 * k), BT.ban.text);
        ui_text(ft, bcx, by + 44, UI_CENTER, ui_rgb(WHITE), (int)(255 * k), BT.ban.text);
        for (int i = 0; i < ns; i++) ui_text(fs, bcx, by + 74 + i * 24, UI_CENTER, dim, (int)(255 * k), sub[i]);
    }
    /* Nombres flottants : montent de 40 px par seconde, s'effacent après 0,9 s */
    for (int i = 0; i < BT_POPUPS; i++) {
        if (!BT.pop[i].on) continue;
        const float age = now - BT.pop[i].t0;
        if (age > 1.3f) continue;
        const int u = BT.pop[i].unit;
        const screen_pos_t *pos = u >= BT_ENEMY ? &E.scr_enemy[(u - BT_ENEMY) % BT_EMAX] : &E.scr_party[u];
        if (!pos->vis) continue;
        const float y = (u >= BT_ENEMY ? pos->cy - 40 : pos->top) - age * 40;
        const int a = (int)(255 * (age > 0.9f ? 1 - (age - 0.9f) / 0.4f : 1));
        text_outlined(ui_font(FONT_DISP, 28), pos->cx + BT.pop[i].dx, y, bt_col(BT.pop[i].color), a, BT.pop[i].text);
    }
}

/* ---------- Entrée en combat et fin de combat ---------- */
/* Image frame de l'animation sheet (planche propre), direction row, dans l'interface : pieds en (cx, pied), cellule de
   hauteur h, table de couleurs remap (palette pal, remplie ici : elle sert jusqu'au dessin de l'image), opacité a */
static void ui_planche(int sheet, int row, int frame, int remap, uint16_t *pal, float cx, float pied, float h, int a) {
    const uint8_t *blk = sheet >= 0 ? sheet_block(sheet, row, 1) : NULL;
    if (!blk) return;
    const sheet_t *sh = &E.w.sheets[sheet];
    const uint8_t *rm = E.w.remap[remap];
    for (int i = 0; i < 256; i++) { const uint8_t *c = E.w.pal[rm ? rm[i] : i]; pal[i] = ui_rgb((uint32_t)c[0] << 16 | c[1] << 8 | c[2]); }
    const float s = h * sh->norm / sh->fh;   /* silhouette normalisée (SNRM), comme dans le monde */
    ui_sprite(blk + (frame % sh->frames) * sh->fw, sh->fw * sh->frames, pal, sh->fw, sh->fh, cx - sh->fw * s / 2, pied - (sh->fh - sh->foot[row & 7]) * s, s, a,
              UW / 2, UH / 2, PORTRAIT ? UW + UH : 233);
}
static uint16_t SPR_PAL[4 + BT_EMAX][256] ENG_FROID;   /* palettes des sprites de l'entrée en combat (équipe, puis chaque ennemi) */
/* Bande en biais (parallélogramme) de centre (x, y), largeur w, hauteur h, pente k (descend vers la droite si k > 0) */
static void bande(float x, float y, float w, float h, float k, uint16_t c, int a, int bord) {
    const float q[8] = { x - w / 2, y - h / 2 - k * w / 2, x + w / 2, y - h / 2 + k * w / 2, x + w / 2, y + h / 2 + k * w / 2, x - w / 2, y + h / 2 - k * w / 2 };
    ui_poly(q, 4, c, a);
    if (bord) { ui_line(q[0], q[1], q[2], q[3], 2, c, bord); ui_line(q[6], q[7], q[4], q[5], 2, c, bord); }
}
/* Entrée en combat (BP_INTRO, INTRO_S ; un toucher l'abrège) : voile sombre et éclair blanc, l'équipe dans une bande en
   biais qui entre par la gauche (de trois quarts, vers la droite), l'ennemi plus grand dans une bande rouge qui entre par
   la droite, lignes de vitesse, VS au milieu, ALERTE en haut, nom et niveau de l'ennemi en bas ; fondu à la fin
   (drawIntroCombat de l'émulateur) */
static void draw_intro_combat(void) {
    if (!BT.on || BT.phase != BP_INTRO) return;
    const float t = E.clock - BT.t0, in = fminf(1, t / 0.4f), e = 1 - (1 - in) * (1 - in);
    const float v = 1 - fmaxf(0, fminf(1, (t - (INTRO_S - 0.3f)) / 0.3f));
    const float cx = UW / 2, cy = PORTRAIT ? UH * 0.45f : CY, R = PORTRAIT ? UW / 2 : 233;
    const uint16_t rouge = ui_rgb(0xFF3B4E), w = ui_rgb(WHITE);
    const int a = (int)(255 * v);
    voile_ecran(ui_rgb(INK), (int)(228 * v));
    /* Bandes : l'équipe en haut, l'ennemi en bas */
    const float ya = cy - 0.3f * R, yb = cy + 0.26f * R, dx1 = -(1 - e) * 2 * R, dx2 = (1 - e) * 2 * R;
    bande(cx + dx1, ya, 2.4f * R, 0.4f * R, -0.18f, C1, (int)(60 * v), (int)(200 * v));
    bande(cx + dx2, yb, 2.4f * R, 0.46f * R, -0.18f, rouge, (int)(60 * v), (int)(200 * v));
    for (int i = 0; i < 10; i++) {   /* lignes de vitesse : vers la droite en haut, vers la gauche en bas */
        const int bas = i >= 5;
        const float len = 40 + (i * 37) % 70, ly = (bas ? yb : ya) + ((i % 5) - 2) * 0.07f * R;
        const float p = fmodf(t * (700 + i * 60) + i * 131, 2 * R + len), lx = bas ? cx + R - p : cx - R - len + p;
        ui_line(lx, ly, lx + len, ly, 2, bas ? rouge : w, (int)(110 * v));
    }
    /* L'équipe : de trois quarts vers la droite, côte à côte */
    int n = 0, M[EQUIPE_MAX];
    for (int i = 0; i < NMEMB && n < EQUIPE_MAX; i++) if (in_team(i) && member_sheet(i) >= 0) M[n++] = i;
    const float hh = (PORTRAIT ? 0.5f : 0.44f) * R, pas = n > 1 ? fminf(0.24f * R, 0.62f * R / (n - 1)) : 0;
    for (int k = 0; k < n; k++) {
        const int i = M[k], own = member_sheet(i), sh = anim_sheet(own, ANIM_IDLE);
        /* xs : sa place dans la bande ; pieds sur la pente de la bande (elle monte vers la droite) */
        const float xs = cx - 0.42f * R - (n - 1) * pas / 2 + k * pas, x = xs + dx1 * (1 + 0.3f * k), pied = ya + 0.17f * R - 0.18f * (xs - cx);
        ui_planche(sh, 1, (int)(E.clock / 0.22f), i ? perso_vue(E.w.party_perso[i], 0).remap : 0, SPR_PAL[k], x, pied, hh, a);   /* palette de sa place */
    }
    /* Les ennemis : le chef plus grand (un boss encore plus), de trois quarts vers la gauche ; un groupe : les autres plus
       petits, derrière lui, de part et d'autre (INTRO_GRP, en part du rayon), pieds sur la pente de la bande */
    static const float INTRO_GRP[BT_EMAX] = { 0, -0.34f, 0.3f, -0.62f };
    for (int k = BT.nen - 1; k >= 0; k--) {
        const bt_en_t *ek = &BT.en[k];
        const perso_t *pe = &E.w.persos[ek->perso];
        const float ex = cx + (0.36f + INTRO_GRP[k]) * R + dx2 * (1 + 0.25f * k), pied = yb + 0.18f * R - 0.18f * INTRO_GRP[k] * R - (k ? 0.1f * R : 0);
        ui_planche(anim_sheet(pe->sheet, ANIM_IDLE), 7, (int)(E.clock / 0.22f) + k, perso_remap(ek->perso, ek->var), SPR_PAL[4 + k],
                   ex, pied, (k ? 0.56f : BT.boss ? 0.86f : 0.74f) * R, a);
    }
    /* VS, ALERTE, nom de l'ennemi (du groupe) */
    if (t > 0.25f) {
        const float k = fminf(1, (t - 0.25f) / 0.2f), sz = 1 + 0.6f * (1 - k);
        text_outlined(ui_font(FONT_DISP, (int)(44 * sz)), cx - 0.02f * R, cy + 0.06f * R, w, (int)(a * k), "VS");
    }
    if (fmodf(t, 0.5f) < 0.36f) ui_text(ui_font(FONT_DISP, 26), cx, cy - 0.7f * R, UI_CENTER, rouge, a, BT.boss ? "ALERTE · BOSS" : BT.police ? (POL.clan ? "ALERTE · MILICE" : "ALERTE · POLICE") : BT.arene ? "ARÈNE" : BT.nen > 1 ? "ALERTE · GROUPE" : "ALERTE");
    char nm[96], gn[72];
    bt_groupe_nom(gn, sizeof gn);
    if (BT.nen > 1) {   /* groupe : les noms (plus longs) sur une ligne, le niveau dessous */
        ui_text(ui_fit(FONT_DISP, gn, 1.2f * R, 22, 13), cx + 0.04f * R, cy + 0.66f * R, UI_CENTER, w, a, gn);
        snprintf(nm, sizeof nm, "NIV. %d", BT.lvl);
        ui_text(ui_font(FONT_DISP, 18), cx + 0.04f * R, cy + 0.79f * R, UI_CENTER, w, a, nm);
    } else {
        snprintf(nm, sizeof nm, "%s · NIV. %d", gn, BT.lvl);
        ui_text(ui_fit(FONT_DISP, nm, 1.1f * R, 24, 16), cx + 0.08f * R, cy + 0.69f * R, UI_CENTER, w, a, nm);
    }
    if (t < 0.12f) voile_ecran(w, (int)(200 * (1 - t / 0.12f)));   /* éclair */
}
/* Fin de combat (BP_WIN) : tableau du résultat. VICTOIRE et l'ennemi vaincu ; expérience (la barre du niveau de l'équipe
   se remplit, niveau supérieur annoncé) ; crédits gagnés et bourse ; butin, chaque objet avec son icône ; les panneaux
   entrent l'un après l'autre ; un toucher continue (drawVictoire de l'émulateur) */
static void draw_victoire(void) {
    if (!BT.on || BT.phase != BP_WIN) return;
    const float t = E.clock - BT.t0, cx = UW / 2, oy = PORTRAIT ? (UH - ENG_SCREEN) * 0.4f : 0, pw = PORTRAIT ? UW - 48 : 320, l = cx - pw / 2, r = cx + pw / 2;
    const uint16_t w = ui_rgb(WHITE), dim = ui_rgb(DIM);
    char tx[96];
    voile_ecran(ui_rgb(INK), (int)(170 * fminf(1, t / 0.3f)));
    text_outlined(ui_font(FONT_DISP, 34), cx, oy + 92, C2, (int)(255 * fminf(1, t / 0.2f)), "VICTOIRE");
    char gn[48];   /* tx (96) : le nom, « vaincus » et le niveau sans troncature (-Wformat-truncation d'ESP-IDF) */
    bt_groupe_nom(gn, sizeof gn);
    snprintf(tx, sizeof tx, "%s vaincu%s · NIV. %d", gn, BT.nen > 1 ? "s" : "", BT.lvl);
    ui_text(ui_fit(FONT_MONO, tx, pw, 16, 13), cx, oy + 118, UI_CENTER, dim, 255, tx);
    float q[12];
    /* Panneau k : entre par la droite 0,15 s après le précédent */
    #define PANNEAU(k, y, h) const float e##k = fminf(1, fmaxf(0, (t - 0.15f * (k) - 0.1f) / 0.25f)), py##k = oy + (y);         const int a##k = (int)(255 * e##k); const float ox##k = (1 - e##k) * 60;         ui_chamfer_path(l + ox##k, py##k, pw, h, 10, q); ui_poly(q, 6, ui_rgb(0x060E1A), (int)(230 * e##k)); ui_outline(q, 6, 1, C1, a##k)
    /* Expérience : le niveau de l'équipe après la victoire, la barre qui se remplit de l'ancienne à la nouvelle valeur */
    const float dm = BT.rw_napp ? 22 : 0;   /* une ligne de plus pour la magie apprise (03/11/2026) : les panneaux suivants descendent */
    PANNEAU(0, 136, 66 + dm);
    int L = E.level, x = E.exp + BT.rw_xp;
    while (L < NIV_MAX && x >= exp_next(L)) { x -= exp_next(L); L++; }
    const float p0 = E.level >= NIV_MAX ? 1 : (float)E.exp / exp_next(E.level), p1 = (L - E.level) + (L >= NIV_MAX ? 1 : (float)x / exp_next(L));
    const float pc = p0 + (p1 - p0) * fminf(1, fmaxf(0, (t - 0.4f) / 1.0f)), up = floorf(pc);
    const int Lc = E.level + (int)up > NIV_MAX ? NIV_MAX : E.level + (int)up;
    snprintf(tx, sizeof tx, "NIV. %d", Lc);
    ui_text(ui_font(FONT_DISP, 20), l + ox0 + 14, py0 + 26, UI_LEFT, w, a0, tx);
    snprintf(tx, sizeof tx, "+%d EXP", BT.rw_xp);
    ui_text(ui_font(FONT_MONO, 20), r + ox0 - 14, py0 + 26, UI_RIGHT, C2, a0, tx);
    seg_bar(l + ox0 + 14, py0 + 36, pw - 28, 6, Lc >= NIV_MAX ? 1 : pc - up, C2);
    if (up >= 1) { snprintf(tx, sizeof tx, "NIVEAU SUPÉRIEUR ! PV et ATQ en hausse"); ui_text(ui_fit(FONT_MONO, tx, pw - 28, 15, 12), cx + ox0, py0 + 58, UI_CENTER, (fmodf(t, 0.6f) < 0.4f) ? C2 : w, a0, tx); }
    else if (L < NIV_MAX) { snprintf(tx, sizeof tx, "Encore %d EXP pour le niveau %d", exp_next(L) - x, L + 1); ui_text(ui_fit(FONT_MONO, tx, pw - 28, 15, 12), cx + ox0, py0 + 58, UI_CENTER, dim, a0, tx); }
    /* Magie apprise (apprentissage de battle.c) : « VARN apprend MÉGA SACRÉ » (job, niveau gagné ; cristal du Chœur), sinon le
       cristal porté « SERA · GIVRE 3/8 » ; plusieurs : l'une après l'autre, 1,6 s chacune (drawVictoire de l'émulateur) */
    if (BT.rw_napp) {
        const int k = t < 1.2f ? 0 : (int)((t - 1.2f) / 1.6f) % BT.rw_napp, mi = BT.rw_app[k].m, s = BT.rw_app[k].s;
        const int appris = !BT.rw_app[k].max || BT.rw_app[k].pts >= BT.rw_app[k].max;
        if (appris) snprintf(tx, sizeof tx, "%s apprend %s", PARTY[mi].name, E.w.sorts[s].nom);
        else snprintf(tx, sizeof tx, "%s · %s %d/%d", PARTY[mi].name, E.w.sorts[s].nom, BT.rw_app[k].pts, BT.rw_app[k].max);
        ui_text(ui_fit(FONT_MONO, tx, pw - 28, 15, 12), cx + ox0, py0 + 80, UI_CENTER, appris ? C2 : dim, a0, tx);
    }
    /* Crédits : gagnés, puis la bourse */
    PANNEAU(1, 210 + dm, 40);
    snprintf(tx, sizeof tx, "+%d CR", BT.rw_cr);
    ui_text(ui_font(FONT_DISP, 18), l + ox1 + 14, py1 + 27, UI_LEFT, w, a1, "CRÉDITS");
    ui_text(ui_font(FONT_MONO, 20), r + ox1 - 14, py1 + 27, UI_RIGHT, C2, a1, tx);
    snprintf(tx, sizeof tx, "bourse %d", E.credits + BT.rw_cr);
    ui_text(ui_font(FONT_MONO, 14), cx + ox1 + 10, py1 + 26, UI_CENTER, dim, a1, tx);
    /* Butin : les objets laissés, avec leur icône et leur nombre (groupe : les cœurs de toutes les machines, deux
       équipements au plus) */
    int drop[5], ndrop[5], nd = 0;
    if (BT.rw_loot >= 0) { drop[nd] = BT.rw_loot; ndrop[nd++] = BT.rw_nloot > 0 ? BT.rw_nloot : 1; }
    if (BT.rw_eq >= 0) { drop[nd] = BT.rw_eq; ndrop[nd++] = 1; }   /* équipement de son palier (drop_equip de battle.c) */
    if (BT.rw_eq2 >= 0) { if (nd && drop[nd - 1] == BT.rw_eq2) ndrop[nd - 1]++; else { drop[nd] = BT.rw_eq2; ndrop[nd++] = 1; } }
    if (BT.rw_soin >= 0) { drop[nd] = BT.rw_soin; ndrop[nd++] = 1; }   /* soin lâché (DROP_SOIN de battle.c) */
    if (BT.rw_noyau >= 0) { drop[nd] = BT.rw_noyau; ndrop[nd++] = 1; }   /* noyau élémentaire (noyau_butin de battle.c) */
    const float pas_b = nd > 3 ? (dm > 0 ? 24 : 27) : nd > 2 ? (dm > 0 ? 30 : 34) : 40;   /* trois lignes : plus serrées, au-dessus de « Toucher pour continuer » */
    PANNEAU(2, 258 + dm, 34 + (nd ? nd : 1) * pas_b);
    ui_text(ui_font(FONT_DISP, 18), l + ox2 + 14, py2 + 24, UI_LEFT, w, a2, "BUTIN");
    for (int k = 0; k < nd; k++) {
        const float yy = py2 + 34 + k * pas_b;
        if (e2 > 0) objet_icone(item_icon(drop[k]), l + ox2 + 18, yy, PORTRAIT ? UW + UH : 233);
        ui_text(ui_fit(FONT_DISP, ITEMS[drop[k]].name, pw - 120, 20, 14), l + ox2 + 60, yy + 23, UI_LEFT, w, a2, ITEMS[drop[k]].name);
        snprintf(tx, sizeof tx, "× %d", ndrop[k]);
        ui_text(ui_font(FONT_MONO, 20), r + ox2 - 14, yy + 23, UI_RIGHT, C2, a2, tx);
    }
    if (!nd) ui_text(ui_font(FONT_MONO, 16), cx + ox2, py2 + 58, UI_CENTER, dim, a2, "Aucun butin");
    #undef PANNEAU
    if (t > 0.8f && fmodf(t, 1.0f) < 0.7f) ui_text(ui_font(FONT_MONO, 16), cx, oy + (PORTRAIT ? 440 + dm : 412 + (dm > 0 ? 8 : 0)), UI_CENTER, C1, 255, "Toucher pour continuer");
}

/* Coffre fouillé (coffres.c) : panneau du butin, comme le tableau de victoire. Le titre (COFFRE et le bâtiment, ou le nom
   du trésor), les crédits trouvés et la bourse, puis chaque objet avec son icône et son nombre ; les panneaux entrent l'un
   après l'autre ; un toucher, OK ou retour le ferme (drawButin de l'émulateur) */
void hud_butin(const char *titre, int credits, const int *obj, const int *nb, int n) {
    BUT.on = 1; BUT.t0 = H.now; BUT.credits = credits; BUT.n = n < 0 ? 0 : n > 6 ? 6 : n;
    for (int i = 0; i < BUT.n; i++) { BUT.obj[i] = obj[i]; BUT.nb[i] = nb[i]; }
    snprintf(BUT.titre, sizeof BUT.titre, "%s", titre);
    H.menu = 0; H.sub = -1; H.down = 0; H.walking = 0; E.has_dest = 0;
}
int hud_butin_on(void) { return BUT.on; }
static void butin_fermer(void) { if (BUT.on && H.now - BUT.t0 > 0.4f) { BUT.on = 0; son_jouer(SON_MENU, 0.35f, 1); } }   /* pas avant que le panneau soit entré */
static void draw_butin(void) {
    const float t = H.now - BUT.t0, cx = UW / 2, oy = PORTRAIT ? (UH - ENG_SCREEN) * 0.4f : 0, pw = PORTRAIT ? UW - 48 : 320, l = cx - pw / 2, r = cx + pw / 2;
    const uint16_t w = ui_rgb(WHITE), dim = ui_rgb(DIM);
    char tx[64];
    voile_ecran(ui_rgb(INK), (int)(170 * fminf(1, t / 0.3f)));
    text_outlined(ui_font(FONT_DISP, 30), cx, oy + 104, C2, (int)(255 * fminf(1, t / 0.2f)), "TRÉSOR");
    ui_text(ui_fit(FONT_MONO, BUT.titre, pw, 16, 12), cx, oy + 130, UI_CENTER, dim, 255, BUT.titre);
    float q[12];
    #define PANNEAU(k, y, h) const float e##k = fminf(1, fmaxf(0, (t - 0.15f * (k) - 0.1f) / 0.25f)), py##k = oy + (y);         const int a##k = (int)(255 * e##k); const float ox##k = (1 - e##k) * 60;         ui_chamfer_path(l + ox##k, py##k, pw, h, 10, q); ui_poly(q, 6, ui_rgb(0x060E1A), (int)(230 * e##k)); ui_outline(q, 6, 1, C1, a##k)
    PANNEAU(0, 146, 40);   /* crédits trouvés, puis la bourse */
    snprintf(tx, sizeof tx, "+%d CR", BUT.credits);
    ui_text(ui_font(FONT_DISP, 18), l + ox0 + 14, py0 + 27, UI_LEFT, w, a0, "CRÉDITS");
    ui_text(ui_font(FONT_MONO, 20), r + ox0 - 14, py0 + 27, UI_RIGHT, C2, a0, tx);
    snprintf(tx, sizeof tx, "bourse %d", E.credits);
    ui_text(ui_font(FONT_MONO, 14), cx + ox0 + 10, py0 + 26, UI_CENTER, dim, a0, tx);
    const float pas_b = BUT.n > 3 ? 30 : 40;   /* plus de trois objets : lignes plus serrées */
    const float hb = 34 + (BUT.n ? BUT.n : 1) * pas_b;
    PANNEAU(1, 194, hb);   /* les objets, avec leur icône */
    ui_text(ui_font(FONT_DISP, 18), l + ox1 + 14, py1 + 24, UI_LEFT, w, a1, "BUTIN");
    for (int k = 0; k < BUT.n; k++) {
        const float yy = py1 + 34 + k * pas_b;
        if (e1 > 0) objet_icone(item_icon(BUT.obj[k]), l + ox1 + 18, yy, PORTRAIT ? UW + UH : 233);
        ui_text(ui_fit(FONT_DISP, ITEMS[BUT.obj[k]].name, pw - 120, 20, 14), l + ox1 + 60, yy + (pas_b > 30 ? 23 : 19), UI_LEFT, w, a1, ITEMS[BUT.obj[k]].name);
        snprintf(tx, sizeof tx, "× %d", BUT.nb[k]);
        ui_text(ui_font(FONT_MONO, 20), r + ox1 - 14, yy + (pas_b > 30 ? 23 : 19), UI_RIGHT, C2, a1, tx);
    }
    if (!BUT.n) ui_text(ui_font(FONT_MONO, 16), cx + ox1, py1 + 58, UI_CENTER, dim, a1, "Rien d'autre");
    #undef PANNEAU
    if (t > 0.6f && fmodf(t, 1.0f) < 0.7f) ui_text(ui_font(FONT_MONO, 16), cx, fminf(oy + 194 + hb + 26, oy + (PORTRAIT ? 440 : 420)), UI_CENTER, C1, 255, "Toucher pour continuer");
}

/* Défaite (BP_LOSE, depuis le 01/11/2026 ; drawDefaite de l'émulateur) : voile qui se fonce, DÉFAITE en rouge, le point de
   sauvegarde où l'on reprendra (numéro, lieu, temps de jeu perdu ; aucun : le début de la partie), puis les choix (def_choix) :
   RÉESSAYER LE COMBAT (copie du début du combat, en mémoire), REPRENDRE (ce point) et ÉCRAN TITRE ; un toucher ou OK choisit
   (battle_defaite de battle.c), le clavier passe de l'un à l'autre. Avant : une bannière, et Sera revenait au départ de la
   planète en gardant tout */
static void draw_defaite(void) {
    if (!BT.on || BT.phase != BP_LOSE || BT.police || BT.arene || BT.nuit) return;   /* contre la police : la bannière, puis l'arrestation ; arène : ÉLIMINÉE ; nuit : À TERRE */
    const float t = E.clock - BT.t0, cx = UW / 2, oy = PORTRAIT ? (UH - ENG_SCREEN) * 0.4f : 0, pw = PORTRAIT ? UW - 48 : 300, l = cx - pw / 2;
    const uint16_t w = ui_rgb(WHITE), dim = ui_rgb(DIM), rouge = ui_rgb(RED_HEX);
    char tx[96];
    if (DF.t0 != BT.t0) { DF.t0 = BT.t0; DF.sel = 0; DF.ok = hud_partie_info(E.partie, &DF.info); }   /* une lecture par défaite */
    const float e = fminf(1, t / 0.4f);
    voile_ecran(ui_rgb(INK), (int)(200 * e));
    text_outlined(ui_font(FONT_DISP, 34), cx, oy + 96, rouge, (int)(255 * fminf(1, t / 0.25f)), "DÉFAITE");
    ui_text(ui_font(FONT_MONO, 15), cx, oy + 122, UI_CENTER, dim, (int)(255 * e), "L'équipe est à terre");
    if (t < 0.35f) return;   /* les choix après le voile */
    const int a = (int)(255 * fminf(1, (t - 0.35f) / 0.25f));
    float q[12];
    /* Le point de sauvegarde */
    ui_chamfer_path(l, oy + 142, pw, 66, 10, q); ui_poly(q, 6, ui_rgb(0x060E1A), a * 230 / 255); ui_outline(q, 6, 1, C1, a);
    ui_text(ui_font(FONT_DISP, 16), cx, oy + 166, UI_CENTER, w, a, DF.ok ? "DERNIER POINT DE SAUVEGARDE" : "AUCUN POINT DE SAUVEGARDE");
    if (DF.ok) {
        const int perdu = E.temps_jeu > DF.info.temps ? (int)(E.temps_jeu - DF.info.temps) : 0;
        if (perdu >= 60) snprintf(tx, sizeof tx, "n° %u · %s · il y a %d min", (unsigned)DF.info.seq, DF.info.lieu, perdu / 60);
        else snprintf(tx, sizeof tx, "n° %u · %s · il y a %d s", (unsigned)DF.info.seq, DF.info.lieu, perdu);
    } else snprintf(tx, sizeof tx, "Reprendre au départ de la planète");
    ui_text(ui_fit(FONT_MONO, tx, pw - 24, 15, 12), cx, oy + 192, UI_CENTER, dim, a, tx);
    /* RÉESSAYER LE COMBAT (s'il se peut), REPRENDRE, ÉCRAN TITRE : le choisi (clavier, manette) plein, les autres au trait ;
       le premier en couleur du thème */
    static const char *const NOM[3] = { [DEF_REPRENDRE] = "REPRENDRE", [DEF_TITRE] = "ÉCRAN TITRE", [DEF_REESSAYER] = "RÉESSAYER LE COMBAT" };
    int c[3];
    const int n = def_choix(c);
    for (int k = 0; k < n; k++) {
        const float by = oy + 222 + k * 56, bw = pw - 40, bx = cx - bw / 2;
        const int sel = k == DF.sel % n;
        ui_chamfer_path(bx, by, bw, 46, 12, q);
        ui_poly(q, 6, sel ? C1 : ui_rgb(0x060E1A), sel ? a * 60 / 255 : a * 230 / 255);
        ui_outline(q, 6, sel ? 2.5f : 1, k ? dim : C1, a);
        ui_text(ui_fit(FONT_DISP, NOM[c[k]], bw - 24, 22, 15), cx, by + 31, UI_CENTER, k ? ui_rgb(0xE8F6FF) : C2, a, NOM[c[k]]);
        button_rect(bx, by, bw, 46, A_DEFAITE, c[k]);
    }
}

static void hud_world_layers(void);
static void draw_sauve_vu(void);

/* Cible à plus de 6 cases : flèche sur le bord de l'écran rond (hampe et pointe vers le dehors, contour sombre ; FLECHE),
   dans sa direction vue de la caméra (avant : haut de l'écran, droite : (cos yaw, -sin yaw)), qui pulse ; plus près : repère au-dessus de la cible (losange et pointe vers le
   bas qui flottent). k : déphasage (guidage). Renvoie 1 et l'angle du canevas de la flèche (0 = droite, sens horaire) quand
   elle est dessinée (sa distance s'écrit à côté : cible_distances) (drawCible de l'émulateur) */
#define CIBLE_R 204.0f     /* rayon de la flèche du bord sur l'écran rond (hampe à R - 18, pointe à R + 23) */
#define CIBLE_VUE 190.0f   /* repère de près : dans ce rayon autour du centre de l'écran, sinon la flèche du bord */
#define CIBLE_TETE 2.0f    /* × échelle des sprites / ech : le haut d'un sprite de taille 1 ; 2,2 cases au-dessus du sol avant le 01/11/2026, le repère sortait du cadre */
/* Cercle des flèches du bord : l'écran rond ; plein écran (téléphone, depuis le 02/11/2026), le cercle centré sur le milieu
   de la 3D (là où est Sera), à la largeur de l'écran, sa pointe sous la quête suivie (y 160 du canevas) ; avant : celui
   de la montre, dans le coin haut gauche du canevas */
static void cible_cadre(float *cx, float *cy, float *r) {
#if ENG_DYN
    *cx = UW / 2; *cy = ui_img_h / 2.0f - ui_oy; *r = fminf(UW / 2 - 35, *cy - 200);
#elif ENG_RECT
    *cx = CX; *cy = CY; *r = 140;   /* écran rectangulaire : autour de Sera, entre la barre du haut et son cadre en bas (rect_world) */
#else
    *cx = CX; *cy = CY; *r = CIBLE_R;
#endif
}
static int draw_cible(float ox, float oz, uint16_t col, int k, float *ang) {
    const float dx = ox - E.sera.x, dz = oz - E.sera.z, d = hypotf(dx, dz);
    float cx0, cy0, R;
    cible_cadre(&cx0, &cy0, &R);
    /* De près, le repère au-dessus de la cible ; hors du champ (derrière la caméra, au bord de l'écran), la flèche du bord
       la montre encore (01/11/2026 : rien ne s'affichait) */
    float x = 0, y = 0;
    /* point visé : le haut d'un sprite de taille 1 (la cible est souvent une personne : 2 × échelle des sprites / ech, la
       hauteur que lui donne add_character de render.c) ; le repère pointe juste au-dessus */
    const int vu = d < 6 && project(ox, height_at(ox, oz) + CIBLE_TETE * E.w.sprite_scale / E.w.ech, oz, &x, &y) && hypotf(x - cx0, y - cy0) < R - (CIBLE_R - CIBLE_VUE);
    if (!vu) {
        if (d <= 0.8f) return 0;
        const float a = atan2f(dx * cosf(E.yaw) - dz * sinf(E.yaw), dx * sinf(E.yaw) + dz * cosf(E.yaw)), s = sinf(a), c = -cosf(a);
        /* (distance au centre, écart de côté) de chaque sommet : pointe, pointe large, hampe */
        static const float FLECHE[14] = { 23, 0, 4, 18, 4, 6, -18, 6, -18, -6, 4, -6, 4, -18 };
        float q[14];
        for (int i = 0; i < 7; i++) { const float t = R + FLECHE[2 * i], e = FLECHE[2 * i + 1]; q[2 * i] = cx0 + s * t + c * e; q[2 * i + 1] = cy0 + c * t - s * e; }
        const int al = (int)(255 * (0.7f + 0.3f * sinf(H.now * 1000 / 260 + k * 1.6f)));
        ui_poly(q, 7, col, al);
        ui_outline(q, 7, 2, ui_rgb(INK), al);
        *ang = atan2f(c, s);
        return 1;
    }
    if (d <= 0.8f) return 0;
    y += sinf(H.now * 1000 / 300 + k) * 4 - 26;   /* pointe (y + 19) au-dessus de la tête */
    const int al = (int)(255 * (0.85f + 0.15f * sinf(H.now * 1000 / 200)));
    const float lo[8] = { x, y - 32, x + 11, y - 19, x, y - 6, x - 11, y - 19 }, tri[6] = { x - 14, y + 2, x + 14, y + 2, x, y + 19 };
    ui_poly(lo, 4, col, al);
    ui_poly(tri, 3, col, al);
    return 0;
}
/* Distance de chaque flèche du bord (02/11/2026, demande de l'utilisateur : « à côté des indicateurs, en arc de cercle sur
   le bord, pour ne pas trop charger la vue ») : « 120 m », « 1,2 km » (dist_txt, comme le menu GUIDAGE et le pilote), en
   Oxanium 16 (la plus petite police du paquet) de la couleur de la flèche, avec une ombre, le long du cercle des flèches,
   sans pulser ; le texte commence DIST_ECART px après l'axe de la flèche dans le sens horaire (lettres vers le dehors en
   haut de l'écran, vers le centre en bas : lu de gauche à droite), dans la bande [R - 3, R + 9] de la pointe ; deux
   flèches trop proches pour le texte de la première (sens horaire) : il passe de l'autre côté d'elle. Écran rond : un
   texte qui toucherait les jauges PV / PM ou les coordonnées du bas (DIST_ZONE0 à DIST_ZONE1) passe dans la bande
   intérieure [R - 24, R - 12]. Rien quand la cible a son repère de près (pas de flèche) ; rien pendant une scène (comme
   les flèches) ; tout sur la pile (cibleDistances de l'émulateur) */
#define DIST_ECART 25.0f    /* de l'axe de la flèche au début du texte (demi-largeur de la pointe 18, contour, marge) */
#define DIST_H 12.0f        /* hauteur des chiffres d'Oxanium 16 : largeur de la bande du texte */
#define DIST_ZONE0 0.85f    /* angles du canevas des jauges PV / PM et des coordonnées du bas (écran rond) */
#define DIST_ZONE1 3.05f
/* Le signal du Lien (06/11/2026) : un arc qui bat au bord de l'écran, vers l'Écho le plus proche ; plus vite à mesure qu'on approche ;
   terne s'il ne parle pas maintenant (la nuit, la pluie). Tout près et muet : on le dit une fois */
static void draw_echo_signal(void) {
    float x, z, d; int pret, nuit;
    static int dit;
    if (!script_echo_proche(&x, &z, &d, &pret, &nuit)) { dit = 0; return; }
    if (d < 2.5f && !pret && !dit) { dit = 1; toast(nuit ? "Le Lien frémit. Il faudra revenir la nuit." : "Le Lien frémit. Il faudra revenir sous la pluie."); }
    if (d >= 6) dit = 0;
    if (d < 1.2f) return;
    const float dx = x - E.sera.x, dz = z - E.sera.z, a = atan2f(dx * cosf(E.yaw) - dz * sinf(E.yaw), dx * sinf(E.yaw) + dz * cosf(E.yaw));
    float cx0, cy0, R; cible_cadre(&cx0, &cy0, &R);
    const float vite = 0.8f + 2.6f * (1 - d / ECHO_R), ph = H.now * vite - floorf(H.now * vite);
    const int al = (int)((pret ? 210 : 90) * (1 - ph));
    const float an = atan2f(-cosf(a), sinf(a)), l = 0.16f + 0.3f * (1 - d / ECHO_R);
    ui_arc(cx0, cy0, R + 18 + ph * 8, an - l, an + l, 5, pret ? ui_rgb(0x7FE8FF) : ui_rgb(0x5A7A96), al);
}
/* Indicateur de menace (08/11/2026, demande de l'utilisateur : « comme l'indicateur d'écho », pour fuir ou anticiper une rencontre) : un
   arc rouge qui bat au bord de l'écran vers chaque groupe d'ennemis à moins de MENACE_R cases (patrouilles de battle.c), plus vite et plus
   large à mesure qu'il approche ; vif quand il chasse Sera, moins quand il rôde, terne au repos après une fuite (drawMenace de
   l'émulateur, à garder identiques) */
#define MENACE_R 20.0f
#define MENACE_RGB 0xFF3B4A
static void draw_menace(void) {
    float cx0 = 0, cy0 = 0, R = 0;
    int cadre = 0;
    for (int s = 0; s < PATR_MAX; s++) {
        const patr_t *p = &PATR[s];
        if (!p->on) continue;
        const float dx = p->x[0] - E.sera.x, dz = p->z[0] - E.sera.z, d = hypotf(dx, dz);
        if (d >= MENACE_R || d < 1.2f) continue;
        if (!cadre) { cible_cadre(&cx0, &cy0, &R); cadre = 1; }
        const float a = atan2f(dx * cosf(E.yaw) - dz * sinf(E.yaw), dx * sinf(E.yaw) + dz * cosf(E.yaw)), k = 1 - d / MENACE_R;
        const float vite = 1.0f + 3.0f * k, ph = H.now * vite - floorf(H.now * vite);
        const float an = atan2f(-cosf(a), sinf(a)), l = 0.14f + 0.3f * k;
        const int al = (int)((p->etat == 1 ? 230 : p->etat == 2 ? 70 : 150) * (1 - ph));
        ui_arc(cx0, cy0, R + 18 + ph * 8, an - l, an + l, 5, ui_rgb(MENACE_RGB), al);
    }
}
static void cible_distances(const float *an, const float *d, const uint16_t *co, int n) {
    float cx0, cy0, R;
    cible_cadre(&cx0, &cy0, &R);
    const ui_font_t *f = ui_font(FONT_DISP, 16);
    char t[2][16];
    float w[2], cote[2] = { 1, 1 };
    for (int i = 0; i < n; i++) { dist_txt(d[i], t[i], sizeof t[i]); w[i] = ui_text_width(f, t[i]); }
    if (n == 2) {   /* écart de la flèche 0 à la flèche 1 (sens horaire > 0) ; a : la première des deux dans le sens horaire */
        const float e = atan2f(sinf(an[1] - an[0]), cosf(an[1] - an[0]));
        const int a = e >= 0 ? 0 : 1;
        if (fabsf(e) * (R + 3) < 2 * DIST_ECART + w[a]) cote[a] = -1;
    }
    for (int i = 0; i < n; i++) {
        const float rm = R - 3 + DIST_H / 2;   /* milieu de la bande : écarts le long du bord */
        float tc = an[i] + cote[i] * (DIST_ECART + w[i] / 2) / rm;
        int bas = sinf(tc) > 0;
        float rb = bas ? R - 3 + DIST_H : R - 3;   /* ligne de base : lettres vers le centre (bas) ou vers le dehors */
        if (!PORTRAIT) {   /* les jauges et les coordonnées du bas : bande intérieure */
            const float m = atan2f(sinf(tc), cosf(tc)), h = w[i] / 2 / rm;
            if (m + h > DIST_ZONE0 && m - h < DIST_ZONE1) { rb = R - 12; tc = an[i] + cote[i] * (DIST_ECART + w[i] / 2) / (rb - DIST_H / 2); bas = 1; }
        }
        ui_text_arc(f, cx0 + 1, cy0 + 1, rb, tc, bas, ui_rgb(0x05080E), 170, t[i]);
        ui_text_arc(f, cx0, cy0, rb, tc, bas, co[i], 235, t[i]);
    }
}
/* Vol permanent (système solaire) : chaque astre (ASTR) à plus de ASTRE_LOIN cases, une petite flèche sur le bord de
   l'écran dans sa direction (sous celle de l'objectif) et son nom vers le centre ; les mondes où l'on se pose en teinte 1,
   les autres en gris ; pas les petits astres sans atterrissage ; nom décalé de ASTRE_LIGNE px vers le milieu de l'écran
   (en hauteur) tant que son cadre chevauche celui d'un nom déjà posé (avant le 31/10/2026 : d'après l'écart des
   directions seul, TERRE et SOLEIL se recouvraient au bord de l'écran ; drawAstres de l'émulateur) */
#define ASTRE_LOIN 14
#define ASTRE_LIGNE 14
static void draw_astres(void) {
    if (!vol_permanent()) return;
    const ui_font_t *f = ui_font(FONT_MONO, 13);
    float vus[32][3];   /* noms posés : milieu, ligne de base, demi-largeur */
    int nv = 0;
    float cx0, cy0, R;   /* cercle des flèches du bord (cible_cadre), 8 px en deçà de celles de l'objectif */
    cible_cadre(&cx0, &cy0, &R);
    R -= 8;
    for (int i = 0; i < E.w.nastres; i++) {
        const astre_t *a = &E.w.astres[i];
        const float dx = a->x - E.sera.x, dz = a->z - E.sera.z;
        if (a->carte != E.w.carte || hypotf(dx, dz) < ASTRE_LOIN || (a->vers < 0 && a->r > 0 && a->r < 1.5f)) continue;
        const float an = atan2f(dx * cosf(E.yaw) - dz * sinf(E.yaw), dx * sinf(E.yaw) + dz * cosf(E.yaw)), s = sinf(an), c = -cosf(an);
        const float lx = cx0 + s * (R - 16), hw = ui_text_width(f, a->nom) / 2 + 3;
        float ly = cy0 + c * (R - 16) + 5;
        for (int essai = 0; essai < 8; essai++) {
            int chevauche = 0;
            for (int b = 0; b < nv && !chevauche; b++) chevauche = fabsf(lx - vus[b][0]) < hw + vus[b][2] && fabsf(ly - vus[b][1]) < ASTRE_LIGNE;
            if (!chevauche) break;
            ly += c <= 0 ? ASTRE_LIGNE : -ASTRE_LIGNE;
        }
        if (nv < 32) { vus[nv][0] = lx; vus[nv][1] = ly; vus[nv][2] = hw; nv++; }
        const uint16_t col = a->vers >= 0 ? C1 : ui_rgb(0x9AA4B4);
        const float q[6] = { cx0 + s * (R + 12), cy0 + c * (R + 12), cx0 + s * R + c * 8, cy0 + c * R - s * 8, cx0 + s * R - c * 8, cy0 + c * R + s * 8 };
        ui_poly(q, 3, col, 220);
        ui_outline(q, 3, 2, ui_rgb(INK), 220);
        ui_text(f, lx, ly, UI_CENTER, col, 230, a->nom);
    }
}
/* Objectif de la quête (teinte 2), lieu du menu GUIDAGE (vert) s'il n'est pas au même endroit ; puis la distance de
   chaque flèche du bord dessinée (par-dessus les deux flèches) */
static void draw_objective(void) {
    float ox, oz, an[2], d[2];
    uint16_t co[2];
    int n = 0;
    const int ob = objective_pos(&ox, &oz);
    draw_echo_signal();
    draw_menace();
    if (ob && draw_cible(ox, oz, C2, 0, &an[n])) { d[n] = hypotf(ox - E.sera.x, oz - E.sera.z); co[n++] = C2; }
    if (guide_ici()) {   /* le lieu suivi, sur sa carte (guideIci de l'émulateur) */
        const guide_t *g = &E.w.guides[E.guide];
        if ((!ob || hypotf(ox - g->x, oz - g->z) >= 1) && draw_cible(g->x, g->z, ui_rgb(GUIDE_RGB), 1, &an[n])) { d[n] = guide_dist(E.guide); co[n++] = ui_rgb(GUIDE_RGB); }
    } else {   /* ou la personne (le lieu) d'une quête choisie sur un panneau de quêtes (panneaux.c) */
        float gx, gz;
        if (panneau_guide(&gx, &gz) && (!ob || hypotf(ox - gx, oz - gz) >= 1) && draw_cible(gx, gz, ui_rgb(GUIDE_RGB), 1, &an[n])) { d[n] = hypotf(gx - E.sera.x, gz - E.sera.z); co[n++] = ui_rgb(GUIDE_RGB); }
        /* le lieu suivi est sur une autre carte : le passage à prendre */
        else if (E.guide >= 0 && E.guide < E.w.nguides && passage_vers(E.w.guides[E.guide].carte, &gx, &gz) && (!ob || hypotf(ox - gx, oz - gz) >= 1) && draw_cible(gx, gz, ui_rgb(GUIDE_RGB), 1, &an[n])) { d[n] = hypotf(gx - E.sera.x, gz - E.sera.z); co[n++] = ui_rgb(GUIDE_RGB); }
    }
    cible_distances(an, d, co, n);
}

/* Annonce de quête : bandeau en haut de l'écran, ANNONCE_S secondes à partir du moment où rien ne la cache (dialogue,
   menu, carton), l'une après l'autre (drawAnnonce de l'émulateur) */
#define ANNONCE_S 3.5f
static void draw_annonce(void) {
    if (H.ann[0].t0 < 0) H.ann[0].t0 = H.now;
    const float t = H.now - H.ann[0].t0;
    if (t > ANNONCE_S) { memmove(&H.ann[0], &H.ann[1], sizeof H.ann[0] * 3); H.nann--; return; }
    float a = fminf(t, ANNONCE_S - t) / 0.3f;
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    const int al = (int)(255 * a), h = H.ann[0].text[0] ? 92 : 68;
    ui_rect(63, 96, 340, (float)h, ui_rgb(INK), (int)(230 * a));
    ui_line(63, 96, 403, 96, 2, C2, al); ui_line(63, 96.0f + h, 403, 96.0f + h, 2, C2, al);
    ui_text(ui_fit(FONT_MONO, H.ann[0].kind, 310, 18, 18), CX, 120, UI_CENTER, C2, al, H.ann[0].kind);
    ui_text(ui_fit(FONT_DISP, H.ann[0].title, 316, 22, 16), CX, 148, UI_CENTER, ui_rgb(WHITE), al, H.ann[0].title);
    if (H.ann[0].text[0]) ui_text(ui_fit(FONT_MONO, H.ann[0].text, 316, 18, 18), CX, 174, UI_CENTER, C1, al, H.ann[0].text);
}

/* Carton de titre (hud_title), par-dessus la 3D et le HUD de la world map */
static void draw_title(void) {
    const float t = H.now - TT.t0;
    if (t > HUD_TITLE_S) { TT.on = 0; return; }
    float a = fminf(t, HUD_TITLE_S - t) / 0.4f;
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    const int al = (int)(255 * a);
    ui_rect(0, 170, ENG_SCREEN, 126, ui_rgb(INK), (int)(217 * a));
    ui_line(40, 170, 426, 170, 2, C2, al);
    ui_line(40, 296, 426, 296, 2, C2, al);
    ui_text(ui_fit(FONT_DISP, TT.title, 380, 30, 20), CX, TT.sub[0] ? 230 : 246, UI_CENTER, ui_rgb(WHITE), al, TT.title);
    if (TT.sub[0]) ui_text(ui_fit(FONT_MONO, TT.sub, 360, 20, 18), CX, 268, UI_CENTER, C1, al, TT.sub);
}

/* ---------- Montre verrouillée (réveil de la veille) ----------
   Cadran dans le style de l'interface ronde, sur fond noir (rien de la 3D : B->noir de render.c) : graduations des
   minutes au bord, arc des secondes, heure en chiffres à sept segments néon (polygones : nets à toute taille, sans
   police de plus dans le paquet), date, batterie, et l'invite à glisser. Le cadran suit le doigt et s'efface ; un
   glissement de VERROU_GLISSER pixels (dans n'importe quel sens) déverrouille. Le monde reste arrêté (eng_update). */
#define VERROU_GLISSER 140.0f
static struct { int on, doigt, touche; float sx, sy, dx, dy, t; } V;
static struct { int jour, mois, jsem, batt, charge; } MT = { 0, 0, -1, -1, 0 };
void eng_set_date(int jour, int mois, int jsem) { MT.jour = jour; MT.mois = mois; MT.jsem = jsem; }
/* Date pour le défi du jour du mode course (carriere.c) : mois × 32 + jour, -1 inconnue */
int eng_date_jour(void) { return MT.mois >= 1 && MT.mois <= 12 && MT.jour >= 1 && MT.jour <= 31 ? MT.mois * 32 + MT.jour : -1; }
void eng_set_batterie(int pct, int charge) { MT.batt = pct; MT.charge = charge; }
void eng_verrouiller(void) { if (AG.on) return; V.on = 1; V.doigt = 0; V.touche = 0; V.dx = V.dy = 0; V.t = 0; }
int eng_verrouille(void) { return V.on; }
int hud_verrou_on(void) { return V.on; }

int hud_verrou(float dt, const eng_input_t *in) {
    if (!V.on) return 0;
    V.t += dt;
    if (in->touch) {
        if (!V.doigt) { V.doigt = 1; V.sx = in->touch_x; V.sy = in->touch_y; }
        V.dx = in->touch_x - V.sx; V.dy = in->touch_y - V.sy;
    } else if (V.doigt) {
        V.doigt = 0;
        if (hypotf(V.dx, V.dy) >= VERROU_GLISSER) { V.on = 0; son_jouer(SON_VALIDER, 0.5f, 1); }
        V.dx = V.dy = 0;
    }
    /* Manette ou clavier : OK ou RETOUR déverrouille, au relâchement (la touche tenue ne devient pas une action du jeu) */
    if (in->keys & (ENG_KEY_OK | ENG_KEY_BACK)) V.touche = 1;
    else if (V.touche) { V.touche = 0; V.on = 0; son_jouer(SON_VALIDER, 0.5f, 1); }
    return 1;   /* l'image du déverrouillage aussi : le doigt levé ne devient pas un toucher du jeu */
}

/* Chiffres à sept segments (bits a à g : haut, haut droit, bas droit, bas, bas gauche, haut gauche, milieu) */
static const uint8_t SEG7[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
static void segment7(float x0, float y0, float x1, float y1, float t, uint16_t c, int alpha) {
    const float h = t / 2;
    float p[12];
    if (y0 == y1) { const float q[12] = { x0, y0, x0 + h, y0 - h, x1 - h, y0 - h, x1, y0, x1 - h, y0 + h, x0 + h, y0 + h }; memcpy(p, q, sizeof p); }
    else { const float q[12] = { x0, y0, x0 + h, y0 + h, x0 + h, y1 - h, x0, y1, x0 - h, y1 - h, x0 - h, y0 + h }; memcpy(p, q, sizeof p); }
    ui_poly(p, 6, c, alpha);
}
static void chiffre7(float x, float y, float w, float h, float t, int v, uint16_t c, float a);
/* Cadran du son (menu Système, ligne SON) : arc de 270° le long du bord, de bas à gauche à bas à droite par le haut,
   graduations tous les 10 %, niveau en chiffres sept segments comme le cadran de la montre. Mixage (01/11/2026) : le
   canal réglé entre ‹ › au-dessus des chiffres, et les cinq canaux en barres dessous (vol_toucher) */
#define VOL_TITRE_Y (-92)   /* ligne du canal (par rapport au centre) */
#define VOL_BARRE_Y 14      /* haut des barres */
#define VOL_BARRE_H 44
#define VOL_BARRE_W 30
#define VOL_BARRE_PAS 46    /* d'une barre à la suivante */
/* Toucher (dx, dy depuis le centre) sur le cadran : ‹ › ou le nom du canal, une barre ; 0 ailleurs (le cadran se ferme) */
static int vol_toucher(float dx, float dy) {
    if (dy > VOL_TITRE_Y - 26 && dy < VOL_TITRE_Y + 26 && fabsf(dx) < 140) { vol_canal(dx < -40 ? -1 : 1); return 1; }
    const float x0 = -(VOL_CANAUX * VOL_BARRE_W + (VOL_CANAUX - 1) * (VOL_BARRE_PAS - VOL_BARRE_W)) / 2.0f;
    if (dy > VOL_BARRE_Y - 8 && dy < VOL_BARRE_Y + VOL_BARRE_H + 26 && dx > x0 - 8 && dx < -x0 + 8) {
        int k = (int)((dx - x0 + (VOL_BARRE_PAS - VOL_BARRE_W) / 2.0f) / VOL_BARRE_PAS);
        k = k < 0 ? 0 : k >= VOL_CANAUX ? VOL_CANAUX - 1 : k;
        if (k != H.vol_canal) vol_canal(k - H.vol_canal);
        return 1;
    }
    return 0;
}
static void vol_build(void) {
    const float cx = UW / 2 - ui_tx(), cy = UH / 2 - ui_ty(), r = 200, a0 = 0.75f * PI_F, span = 1.5f * PI_F;
    const int v = (int)lroundf(vol_niveau(H.vol_canal) * 20);
    const float a1 = a0 + span * v / 20, pouls = H.now - H.vol_t < 0.25f ? 1 - (H.now - H.vol_t) / 0.25f : 0;
    const uint16_t BL = ui_rgb(0xE8F6FF);
    ui_circle(cx, cy, 228, ui_rgb(0x05070C), 242);   /* le menu et le monde s'effacent derrière le cadran (les barres restent lisibles) */
    ui_arc(cx, cy, r, a0, a0 + span, 16, C1, 45);
    for (int k = 0; k <= 10; k++) {
        const float an = a0 + span * k / 10, r0 = k % 5 ? 214 : 210;
        ui_line(cx + cosf(an) * r0, cy + sinf(an) * r0, cx + cosf(an) * 224, cy + sinf(an) * 224, k % 5 ? 2 : 3, k * 2 <= v ? C2 : C1, k * 2 <= v ? 230 : 90);
    }
    if (v > 0) {
        ui_arc(cx, cy, r, a0, a1, 26, C2, (int)(50 + 60 * pouls));
        ui_arc(cx, cy, r, a0, a1, 14, C2, 255);
    }
    ui_circle(cx + cosf(a1) * r, cy + sinf(a1) * r, 13 + 4 * pouls, BL, 255);   /* curseur au bout de l'arc */
    ui_circle(cx + cosf(a1) * r, cy + sinf(a1) * r, 7, C2, 255);
    {   /* haut-parleur, ondes selon le niveau (une par tiers), barré si coupé */
        const float hx = cx - 8, hy = cy - 132;
        const float hp[12] = { hx - 14, hy - 6, hx - 6, hy - 6, hx + 4, hy - 15, hx + 4, hy + 15, hx - 6, hy + 6, hx - 14, hy + 6 };
        ui_poly(hp, 6, C1, 230);
        for (int k = 0; k < 3; k++) ui_arc(hx + 4, hy, 10 + k * 7, -0.8f, 0.8f, 3, C1, v > k * 7 ? 230 : 45);
        if (v == 0) ui_line(hx - 16, hy + 16, hx + 26, hy - 16, 3, C2, 255);
    }
    /* Canal réglé, entre ‹ › (toucher : le précédent, le suivant) */
    ui_text(ui_font(FONT_DISP, 24), cx, cy + VOL_TITRE_Y + 9, UI_CENTER, BL, 255, VOL_NOMS[H.vol_canal]);
    for (int s = -1; s <= 1; s += 2) {   /* triangles (« ‹ » n'est pas dans la police du paquet), comme STATUT */
        const float ax = cx + s * 112, ay = cy + VOL_TITRE_Y;
        const float tri[6] = { ax + s * 9, ay, ax - s * 7, ay - 11, ax - s * 7, ay + 11 };
        ui_poly(tri, 3, C2, 230);
    }
    /* Niveau : trois chiffres, ou « coupé » */
    if (v == 0) ui_text(ui_font(FONT_DISP, 30), cx, cy - 20, UI_CENTER, C2, 255, "COUPÉ");
    else {
        const int pc = v * 5, nd = pc >= 100 ? 3 : pc >= 10 ? 2 : 1;
        const float w = 32, h = 58, t = 8, ec = 8, y = cy - 64;
        float x = cx - (nd * w + (nd - 1) * ec + 22) / 2;
        for (int k = nd - 1; k >= 0; k--) {
            int d = pc; for (int j = 0; j < k; j++) d /= 10;
            chiffre7(x, y, w, h, t, d % 10, BL, 1); x += w + ec;
        }
        ui_text(ui_font(FONT_MONO, 20), x + 6, y + h, UI_LEFT, C1, 230, "%");
    }
    {   /* le mixage : une barre par canal, remplie selon son niveau, celle réglée en couleur vive */
        static const char *const COURT[VOL_CANAUX] = { "GÉN", "MUS", "EFF", "MOT", "INT" };
        const float x0 = cx - (VOL_CANAUX * VOL_BARRE_W + (VOL_CANAUX - 1) * (VOL_BARRE_PAS - VOL_BARRE_W)) / 2.0f, y0 = cy + VOL_BARRE_Y;
        for (int k = 0; k < VOL_CANAUX; k++) {
            const float x = x0 + k * VOL_BARRE_PAS, f = vol_niveau(k) * VOL_BARRE_H;
            const int sel = k == H.vol_canal;
            ui_rect(x, y0, VOL_BARRE_W, VOL_BARRE_H, C1, sel ? 70 : 35);
            if (f > 0.5f) ui_rect(x, y0 + VOL_BARRE_H - f, VOL_BARRE_W, f, sel ? C2 : C1, sel ? 255 : 150);
            ui_text(ui_font(FONT_MONO, 13), x + VOL_BARRE_W / 2, y0 + VOL_BARRE_H + 17, UI_CENTER, sel ? C2 : C1, sel ? 255 : 170, COURT[k]);
        }
    }
    ui_text(ui_font(FONT_MONO, 16), cx, cy + 106, UI_CENTER, C1, 190, "TOURNER LE BORD");
    ui_text(ui_font(FONT_MONO, 14), cx, cy + 128, UI_CENTER, ui_rgb(0x7F93A8), 190, "Barre ou flèche : canal");
    ui_text(ui_font(FONT_MONO, 14), cx, cy + 148, UI_CENTER, ui_rgb(0x7F93A8), 190, "Ailleurs : terminer");
}

/* Chiffre v (-1 : tous les segments éteints) dans le rectangle (x, y, w, h) ; éteints à peine visibles, allumés avec un
   halo */
static void chiffre7(float x, float y, float w, float h, float t, int v, uint16_t c, float a) {
    const float g = 3, l = x + t / 2, r = x + w - t / 2, hh = y + t / 2, m = y + h / 2, b = y + h - t / 2;
    const float S[7][4] = { { l + g, hh, r - g, hh }, { r, hh + g, r, m - g }, { r, m + g, r, b - g }, { l + g, b, r - g, b },
                            { l, m + g, l, b - g }, { l, hh + g, l, m - g }, { l + g, m, r - g, m } };
    for (int k = 0; k < 7; k++) {
        if (v >= 0 && (SEG7[v] >> k & 1)) {
            segment7(S[k][0], S[k][1], S[k][2], S[k][3], t + 8, c, (int)(50 * a));
            segment7(S[k][0], S[k][1], S[k][2], S[k][3], t, c, (int)(255 * a));
        } else segment7(S[k][0], S[k][1], S[k][2], S[k][3], t, c, (int)(18 * a));
    }
}

/* Anneau des secondes (le même que l'app montre, montre/main/theme.c) : 60 segments, la seconde en cours dans la
   seconde teinte avec un halo, une traînée de la teinte qui s'estompe sur les secondes passées, le rail des secondes à
   venir à peine visible */
static void verrou_segment(float k, float r0, float r1, float gap, uint16_t c, int alpha) {
    const float a0 = (k + gap / 2) * (2 * PI_F / 60), a1 = (k + 1 - gap / 2) * (2 * PI_F / 60);
    const float q[8] = { CX + sinf(a0) * r0, CY - cosf(a0) * r0, CX + sinf(a0) * r1, CY - cosf(a0) * r1,
                         CX + sinf(a1) * r1, CY - cosf(a1) * r1, CX + sinf(a1) * r0, CY - cosf(a1) * r0 };
    ui_poly(q, 4, c, alpha);
}
static void verrou_secondes(int s, float a) {
    for (int k = 0; k < 60; k++) {
        if (s < 0 || k > s) verrou_segment(k, 223, 231, 0.3f, C1, (int)(32 * a));
        else if (k < s) { const int age = s - k; verrou_segment(k, 223, 231, 0.3f, C1, (int)(16 * (age < 24 ? 15 - age * 10 / 24 : 5) * a)); }
    }
    if (s >= 0) {
        verrou_segment(s - 0.15f, 219, 233, 0, C2, (int)(80 * a));   /* halo */
        verrou_segment(s, 223, 231, 0.3f, C2, (int)(255 * a));
    }
}

static void verrou_build(void) {
    const float a = 1 - fminf(1, hypotf(V.dx, V.dy) / VERROU_GLISSER) * 0.85f;   /* s'efface sous le doigt */
    const uint16_t BL = ui_rgb(0xE8F6FF);
    const int connue = E.horloge >= 0, s = connue ? E.horloge : 0;
    ui_translate(V.dx * 0.5f, V.dy * 0.5f);
    /* Graduations des minutes, et arc des secondes depuis le haut */
    for (int k = 0; k < 60; k++) {
        const float an = k * (2 * PI_F / 60) - PI_F / 2, r0 = k % 5 ? 213 : 203, r1 = 220;
        ui_line(CX + cosf(an) * r0, CY + sinf(an) * r0, CX + cosf(an) * r1, CY + sinf(an) * r1, k % 5 ? 2 : 3,
                k % 5 ? C1 : BL, (int)((k % 5 ? 80 : 210) * a));
    }
    verrou_secondes(connue ? s % 60 : -1, a);
    ui_text(ui_font(FONT_MONO, 18), CX, 120, UI_CENTER, C1, (int)(170 * a), "ECHOES // SEC-06");
    /* HH:MM ; deux-points qui battent la seconde */
    const float w = 60, h = 112, t = 12, y = 158, ec = 12, dp = 34;
    float x = CX - (4 * w + 2 * ec + dp) / 2;
    const int hr = s / 3600 % 24, mn = s / 60 % 60;
    chiffre7(x, y, w, h, t, connue ? hr / 10 : -1, C1, a); x += w + ec;
    chiffre7(x, y, w, h, t, connue ? hr % 10 : -1, C1, a); x += w;
    const int bat = !connue || s % 2 == 0;
    for (int k = 0; k < 2; k++) {
        const float cy = y + h * (k ? 0.7f : 0.3f), cx = x + dp / 2;
        ui_rect(cx - 6, cy - 6, 12, 12, C1, (int)((bat ? 255 : 60) * a));
    }
    x += dp;
    chiffre7(x, y, w, h, t, connue ? mn / 10 : -1, C1, a); x += w + ec;
    chiffre7(x, y, w, h, t, connue ? mn % 10 : -1, C1, a);
    /* Date, batterie */
    static const char *JS[7] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
    static const char *MS[12] = { "JAN", "FÉV", "MARS", "AVR", "MAI", "JUIN", "JUIL", "AOÛT", "SEPT", "OCT", "NOV", "DÉC" };
    char d[40];
    if (!connue) snprintf(d, sizeof d, "HEURE À RÉGLER");
    else if (MT.jsem >= 0 && MT.jsem < 7 && MT.mois >= 1 && MT.mois <= 12) snprintf(d, sizeof d, "%s %d %s", JS[MT.jsem], MT.jour, MS[MT.mois - 1]);
    else d[0] = 0;
    if (d[0]) ui_text(ui_font(FONT_DISP, 26), CX, 316, UI_CENTER, BL, (int)(235 * a), d);
    if (MT.batt >= 0) {
        const float bx = CX - 40, by = 336;
        const uint16_t cb = MT.batt <= 15 && !MT.charge ? C2 : C1;
        const float cadre[8] = { bx, by, bx + 28, by, bx + 28, by + 14, bx, by + 14 };
        ui_outline(cadre, 4, 2, cb, (int)(220 * a));
        ui_rect(bx + 28, by + 4, 3, 6, cb, (int)(220 * a));
        ui_rect(bx + 3, by + 3, 22 * MT.batt / 100.0f, 8, cb, (int)(220 * a));
        snprintf(d, sizeof d, "%d %%%s", MT.batt, MT.charge ? " +" : "");
        ui_text(ui_font(FONT_MONO, 18), bx + 40, by + 13, UI_LEFT, cb, (int)(220 * a), d);
    }
    /* Invite : trois chevrons qui montent, puis le texte */
    for (int k = 0; k < 3; k++) {
        const float cy = 378 - k * 10, f = sinf(V.t * 3.2f - k * 0.9f), al = (0.25f + 0.75f * (f > 0 ? f : 0)) * a;
        const float ch[6] = { CX - 12, cy + 5, CX, cy - 5, CX + 12, cy + 5 };
        ui_line(ch[0], ch[1], ch[2], ch[3], 2.5f, C1, (int)(230 * al));
        ui_line(ch[2], ch[3], ch[4], ch[5], 2.5f, C1, (int)(230 * al));
    }
    ui_text(ui_font(FONT_MONO, 18), CX, 402, UI_CENTER, C1, (int)(200 * a), "GLISSER POUR JOUER");   /* dans les graduations du bas */
    ui_translate(0, 0);
}

void hud_build(void) {
    C1 = ui_rgb(THEMES[E.theme].c1); C2 = ui_rgb(THEMES[E.theme].c2);
    nbtn = 0;
    ui_translate(0, 0);
    ui_filtre(NULL);
    img_trame();
    if (V.on) { verrou_build(); return; }   /* montre verrouillée : son cadran seul */
    if (accueil_on()) { accueil_build(); return; }
    ui_filtre(nom_echange);   /* héroïne choisie : noms échangés dans tout ce qui s'écrit ; formes selon le genre (genre_texte) */
    if (BT.on) draw_battle_hud();   /* combat : ni menu, ni dialogue, ni bulle d'interaction */
    else hud_world_layers();
    /* Fondu au noir de 0,45 s au début et à la fin d'un combat */
    const float k = (E.clock - BT.fade_t) / 0.45f;
    if (BT.fade_t > 0 && k < 1) voile_ecran(ui_rgb(0), (int)(255 * (1 - (k < 0 ? 0 : k))));
    ui_filtre(NULL);
}

/* Hors combat : HUD de la world map, puis dialogue, écran du menu, menu ou message */
/* Illustration de la scène (instruction « image », E.scene) : plein écran, sous la bulle du dialogue */
static void draw_image_plein(int k) {   /* une image du jeu en plein écran (illustration d'une scène, cut-in d'une succube de l'île) */
    int iw, ih;
    const uint16_t *pal;
    const uint8_t *ix = img_ix(k, &iw, &ih, &pal, 1);
    if (!ix) return;
    const float s = (UW - 2) / iw, y = (PORTRAIT ? UH * 0.42f : CY) - ih * s / 2;
    ui_image_pal(ix, pal, iw, ih, (UW - iw * s) / 2, y, s, UW / 2, UH / 2, PORTRAIT ? UW + UH : 233);
}
static void draw_scene(void) { draw_image_plein(E.scene - 1); }

#ifdef ENG_COMPTE
#define HT0() uint32_t ht_ = E.cfg.clock_us ? E.cfg.clock_us() : 0
#define HT(k) do { extern unsigned long eng_hud_us[8]; const uint32_t t_ = E.cfg.clock_us ? E.cfg.clock_us() : 0; eng_hud_us[k] += t_ - ht_; ht_ = t_; } while (0)
#else
#define HT0() ((void)0)
#define HT(k) ((void)0)
#endif
static void hud_world_layers(void) {
    HT0();
    if (E.alarme.on && !H.menu && H.sub < 0) { cadre(1); draw_alarme(); cadre(0); }   /* effraction : sous l'interface */
    const int scene = hud_scene();   /* ni guidage, ni bulles, ni jauges pendant une scène */
    if (PIL.on && !POL.niveau && !H.menu && H.sub < 0 && !H.dialog && !CR.on && !scene) draw_pilote();   /* pilote.c (avant « VÉHICULE VOLÉ ») */
    else if ((POL.niveau || (E.ride >= 0 && E.npcs[E.ride].vole)) && !POL.arr && !H.menu && H.sub < 0 && !H.dialog && !CR.on && !scene) draw_recherche();   /* police.c */
    HT(0);
    if (!H.menu && H.sub < 0) draw_world_hud();   /* sous le menu et ses écrans, seule la 3D reste visible */
    HT(1);
    if (!E.in.on) guide_step();
    HT(2);
    const int course_portrait = PORTRAIT && (CR.on || RM.on);   /* plein écran : la course a son interface, sans guidage ni toast */
    if (!H.menu && H.sub < 0 && !H.dialog && !TT.on && !E.in.on && !scene && !course_portrait) { draw_astres(); HT(3); draw_objective(); }
    HT(4);
    /* Illustration de la scène : sous le carton de titre, les annonces, le fondu et la bulle du dialogue */
    if (E.scene && script_running() && !H.menu) draw_scene();
    { const int oc = ile_offrande_cutin(); if (oc >= 0 && !H.menu) draw_image_plein(oc); }   /* île : le cut-in d'une animation d'offrande (ile.c) */
    if (TT.on) { cadre(1); draw_title(); cadre(0); }
    else if (H.nann && !H.menu && H.sub < 0 && !H.dialog && !course_portrait) {   /* (plein écran : après la course, elle cacherait son bandeau) */
        if (PORTRAIT) ui_translate((UW - ENG_SCREEN) / 2, 80);   /* sous la barre du haut */
        draw_annonce(); ui_translate(0, 0);
    }
    /* Mise en scène : fondu au noir (instruction « fondu »), chrono de la fuite */
    if (MISE.fondu > 0.004f) voile_ecran(ui_rgb(0), (int)(255 * (MISE.fondu > 1 ? 1 : MISE.fondu)));
    if (MISE.fuite_on) {
        char t[24];
        snprintf(t, sizeof t, "FUITE  %.1f", MISE.fuite_t > 0 ? MISE.fuite_t : 0);
        ui_text(ui_font(FONT_DISP, 26), PORTRAIT ? UW / 2 : CX, PORTRAIT ? 150 : 78, UI_CENTER, MISE.fuite_t < 10 ? C2 : C1, 255, t);
    }
    if (H.dialog) draw_dialog();
    else if (CHX.on) {   /* (téléport de la carte du menu : la carte dessous, comme l'émulateur) */
        if (H.sub == SUB_MAP) { if (PORTRAIT) ui_translate((UW - ENG_SCREEN) / 2, 0); draw_sub(); ui_translate(0, 0); }
        draw_choice();
    }
    else if (BUT.on) draw_butin();   /* panneau du butin d'un coffre */
    else if (H.sub >= 0) {
        if (PORTRAIT) ui_translate((UW - ENG_SCREEN) / 2, 0);   /* plein écran : CX au milieu de l'écran */
        draw_sub(); ui_translate(0, 0);
    } else if (H.menu) {
        draw_menu();
        if (H.now - H.toast_t < 1.6f && H.toast[0]) ui_text(ui_fit(FONT_MONO, H.toast, PORTRAIT ? UW - 48 : 190, 20, 16), PORTRAIT ? UW / 2 : CX, PORTRAIT ? UH - 520 : 336, UI_CENTER, C2, 255, H.toast);
    } else if (H.now - H.toast_t < 1.8f && H.toast[0] && !course_portrait) ui_text(ui_fit(FONT_MONO, H.toast, PORTRAIT ? UW - 48 : 300, 20, 16), PORTRAIT ? UW / 2 : CX, PORTRAIT ? 190 : 160, UI_CENTER, C2, 255, H.toast);
    draw_sauve_vu();
}
/* « SAUVEGARDE » d'une sauvegarde automatique : discret, en haut de l'écran, AUTO_VU s (entrée et sortie en fondu), un arc
   qui tourne à sa gauche (drawSauveVu de l'émulateur) */
static void draw_sauve_vu(void) {
    const float t = H.now - AS.vu_t;
    if (!AS.vu || t > AUTO_VU) return;
    const int a = (int)(210 * (t < 0.2f ? t / 0.2f : t > AUTO_VU - 0.4f ? (AUTO_VU - t) / 0.4f : 1));
    const float x = PORTRAIT ? UW / 2 : CX, y = PORTRAIT ? 96 : 44, g = t * 7;
    ui_text(ui_font(FONT_MONO, 14), x + 9, y + 5, UI_CENTER, C1, a, "SAUVEGARDE");
    ui_arc(x - 58, y, 6, g, g + 4.4f, 2, C1, a);
}

uint32_t hud_theme_c1(void) { return THEMES[E.theme].c1; }
uint32_t hud_theme_c2(void) { return THEMES[E.theme].c2; }
