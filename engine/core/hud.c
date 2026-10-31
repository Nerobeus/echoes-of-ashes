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
static void cadre_bouton(float x, float y, float w, float h, int sel, uint16_t col);
static int wrap_chord(const ui_font_t *f, const char *text, float y0, float lh, char lines[6][96]);
#else
#define PORTRAIT 0
#define UW ((float)ENG_SCREEN)
#define UH ((float)ENG_SCREEN)
#endif
static void aura(const screen_pos_t *p, uint32_t rgb, float t);   /* halo d'un personnage transformé (ÉVEIL) */
static void draw_intro_combat(void);   /* entrée en combat (BP_INTRO) */
static void draw_victoire(void);       /* tableau de fin de combat (BP_WIN) */
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

/* ---------- Teintes (maquette « Variantes de teintes ») ---------- */
static const struct { uint32_t c1, c2; const char *name; } THEMES[5] = {
    { 0x22E4FF, 0xFF2D9B, "NÉON" }, { 0xFFB020, 0x22E4FF, "ROUILLE" }, { 0x8DFF5A, 0xFF2D9B, "TOXIQUE" },
    { 0xA77BFF, 0xFFD23F, "ULTRAVIOLET" }, { 0xC4ECFF, 0xFF6A3D, "GLACE" } };

/* ---------- Données de jeu (équipe et objets de l'émulateur ; équipements : à revoir avec le scénario) ---------- */
/* Techniques : coût en PM, soin, multiplicateur de l'attaque, retard infligé à l'ennemi (tours), soin de base, effet
   propre (0 : celui de l'arme) */
member_t PARTY[4] = {
    { "SERA", 0xFF4FB0, 3621, 3621, 310, 310, 430, 12, { "Lame-fouet", "Combinaison de ferrailleuse", "Écho d'Iris" }, { { "TIR LASER", 40, 0, 1.9f, 0, 0, 0 }, { "PIRATAGE", 60, 0, 0.5f, 1.6f, 0, 0 } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "VARN", 0xB8BDCA, 4287, 4287, 210, 210, 540, 9, { "Lame de l'Ordre", "Manteau d'inquisiteur", "Sceau de braise" }, { { "LAME-FOUDRE", 30, 0, 2.1f, 0, 0, FX_SORT(MG_FOUDRE, 2) } }, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "BORS", 0xC4442F, 4980, 4980, 180, 180, 500, 7, { "Canon à rivets", "Plastron de tôle", "Gant de levage" }, { { "OBUS", 25, 0, 2.4f, 0, 0, 0 } }, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "LYSE", 0xE8C25A, 2963, 2963, 420, 420, 380, 11, { "Arc à plasma", "Cape de cendre", "Filtre respiratoire" }, { { "SOIN", 35, 1400, 0, 0, 0, 0 }, { "FLÈCHE-PLASMA", 30, 0, 1.8f, 0, 0, 0 } }, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
/* Objets d'origine ; ceux de l'Atelier (section SCEN) les remplacent au chargement du scénario (script.c) */
static const item_t ITEMS_ORIGINE[] = {
    { "POTION", "Rend 1 500 PV à un allié", 3, 1500, 0, 0, 60, 0, 0, 0, NULL, 0 },
    { "ÉTHER", "Rend 60 PM à un allié", 2, 0, 60, 0, 150, 0, 0, 0, NULL, 0 },
    { "ANTIDOTE", "Soigne l'empoisonnement", 4, 0, 0, 3, 40, 0, 0, 0, NULL, 0 },
    { "PIÈCE DE HERSE", "Ferraille de machine, à revendre", 5, 0, 0, 2, 40, 0, 0, 0, NULL, 0 },
    { "CRISTAL DE MACHINE", "Froid et muet. L'Ordre l'achète", 0, 0, 0, 2, 200, 1, 0, 0, NULL, 0 },
    { "CRISTAL DE TRAQUEUSE", "Encore chaud. Il murmure", 0, 0, 0, 1, 0, 0, 0, 0, NULL, 0 },
};
item_t ITEMS[MAX_ITEMS] ENG_FROID;   /* en PSRAM sur l'ESP32 : rempli par items_init, puis par le scénario */
int n_items;
void items_init(void) {
    n_items = (int)(sizeof ITEMS_ORIGINE / sizeof ITEMS_ORIGINE[0]);
    memcpy(ITEMS, ITEMS_ORIGINE, sizeof ITEMS_ORIGINE);
}
int item_potion(void) { for (int i = 0; i < n_items; i++) if (ITEMS[i].type == 0 && ITEMS[i].hp > 0) return i; return -1; }
int item_loot(void) { for (int i = 0; i < n_items; i++) if (ITEMS[i].butin) return i; return -1; }
#define NMENU 8
static const char *MENU_NAME[NMENU] = { "OBJETS", "TECHNIQ.", "ÉQUIP.", "STATUT", "CARTE", "SYSTÈME", "QUÊTES", "GUIDAGE" };
static const char *MENU_ICON[NMENU] = { "obj", "tech", "equip", "stat", "map", "sys", "quest", "guide" };
enum { SUB_ITEMS, SUB_TECH, SUB_EQUIP, SUB_STATUS, SUB_MAP, SUB_SYSTEM, SUB_QUESTS, SUB_GUIDE, SUB_SHOP, SUB_TELE, SUB_GALERIE, SUB_CODE };   /* SUB_CODE : code du contenu 18+ (édition unique) */   /* SUB_SHOP : boutique d'un intérieur, SUB_TELE : téléport (SYSTÈME), hors de la roue */

/* ---------- État ---------- */
static struct {
    float now;
    int menu, sel, sub, sub_sel, sub_scroll;
    int gcat;                    /* menu GUIDAGE : -1 les groupes, GR_FAM + g le groupe g (villes, lieux : leurs lieux ; famille : ses sortes), GR_CAT + k les bâtiments de la sorte du lieu k */
    int gfam;                    /* groupe de la sorte ouverte (retour) */
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
    char toast[80]; float toast_t;
    /* annonces de quête en attente (la première s'affiche) : genre, titre, objectif, début (-1 : pas encore montrée) */
    struct { char kind[64], title[64], text[96]; float t0; } ann[4]; int nann;
    uint32_t prev_keys;
    int seq, bright;
    /* combat au clavier : bouton choisi de l'éventail (place 0..4), boutons présents à l'image précédente */
    int bsel, bkbd, bslot_cmd[5], bphase, bactor;
} H;

/* Zones tactiles construites à l'image précédente */
enum { A_TALK, A_MENU_ITEM, A_OPEN, A_ROW, A_BACK, A_BATTLE, A_COURSE, A_RACE_CHG, A_RACE_GO, A_RACE_BACK, A_RACE_PAGE, A_FIRE, A_CHOICE, A_PAUSE, A_REPRENDRE, A_ABANDON, A_PORTE,
       A_MENU_BTN, A_MENU_GO, A_VALIDER, A_CODE };   /* VALIDER : la ligne choisie d'un sous-menu (plein écran) */   /* plein écran : bouton du menu (ouvrir, fermer), entrée du menu en grille (ouvrir d'un toucher) */
/* Choix du scénario en cours : réponses, réponse surlignée */
static struct { int on, n, sel, porte, commerce; char opts[4][48], titre[40]; } CHX;   /* porte : choix d'une porte fermée (int_choix) ; commerce : ce que propose un commerçant */
/* Commerce en cours (openCommerce de l'émulateur) : commerce (COMM), exemplaire qui le tient, action de chaque réponse
   (0 acheter, 1 vendre, 2 soins, 3 un verre, 4 partir), boutique ouverte (0 achat, 1 vente), nom du commerçant */
static struct { int comm, npc, act[4], mode; char nom[24]; } SHOP = { -1, -1, { 0 }, 0, "" };
static int shop_rows(void);
static int shop_item(int row);
static int shop_prix(int k);
typedef struct { float x, y, r, w, h; int rect, act, arg; } button_t;
static button_t BTN[24];
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
static uint16_t portrait_px[2][24 * 24]; static uint8_t portrait_mask[2][24 * 24];
/* Dialogue en cours : interlocuteur de droite (right), puis les répliques (qui parle, texte) */
#define DLG_MAX 16
static struct { char right[24], who[DLG_MAX][24]; const char *text[DLG_MAX]; int n; } D;
/* Icônes des objets du sac (section OBJI) : 16 × 16 chacune, à la suite */
static uint16_t *obji_px; static uint8_t *obji_mask; static int n_obji;
/* Images 32 × 32 des équipements (section OBJG) ; une ligne de liste les désigne par l'icône ICON_OBJG + k (k ≥ 1) */
#define ICON_OBJG 1000
static uint16_t *objg_px; static uint8_t *objg_mask; static int n_objg;
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void toast(const char *s) { snprintf(H.toast, sizeof H.toast, "%s", s); H.toast_t = H.now; }

/* Tenues : le membre m en a-t-il ; texte de la ligne du menu ÉQUIP. ; pourquoi une tenue est mise d'office */
static const char *const TENUE_RAISON[4] = { "", "dans l'eau", "pendant la course", "par le scénario" };
static int has_tenues(int m) { for (int t = 0; t < E.w.ntenues; t++) if (tenue_mine(m, t)) return 1; return 0; }
static const char *tenue_label(int m) { const int t = tenue_of(m, 0); return t >= 0 ? E.w.tenues[t].nom : has_tenues(m) ? "LA SIENNE" : PARTY[m].eq[0]; }

/* Portrait k (0 : joueur, 1 : interlocuteur), 24 × 24 : tête du sprite de face, dans la planche propre sheet (carré
   du portrait, première image) ou, si sheet < 0, dans la planche commune ; couleurs de la table remap */
static void make_portrait(int k, int sheet, int remap) {
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
        portrait_px[k][y * 24 + x] = ui_rgb(((uint32_t)c[0] << 16) | (c[1] << 8) | c[2]);
        portrait_mask[k][y * 24 + x] = id != 0;
    }
}

/* ---------- Initialisation ---------- */
void hud_init(void) {
    memset(&H, 0, sizeof H);
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
}

/* ---------- Sauvegarde : deux emplacements alternés, somme de contrôle CRC-32 ---------- */
static uint32_t crc32(const uint8_t *p, int n) {
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;
}
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
    uint32_t crc;
} save_t;
#define SAVE_MAGIC 0x42565345u   /* 'ESVB' (scénario, équipe, quêtes, objets de l'Atelier, niveau, crédits, musique, tenues, jobs, guidage, héroïne, lieu, temps, carte) */
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
    static save_t b;
    const int va = read_slot(p * 2, s), vb = read_slot(p * 2 + 1, &b);
    if (vb && (!va || b.seq > s->seq)) *s = b;
    return va || vb;
}

static int save_game(void) {
    if (!E.cfg.save_write) { toast("Pas de carte SD"); return 0; }
    static save_t s, o;   /* hors de la pile (tâche du jeu de l'ESP32) */
    uint32_t ordre = 0;
    for (int p = 0; p < PARTIES; p++) if (partie_lire(p, &o) && o.ordre > ordre) ordre = o.ordre;
    memset(&s, 0, sizeof s);
    s.magic = SAVE_MAGIC; s.seq = (uint32_t)++H.seq; s.ordre = ordre + 1;
    int_position(&s.x, &s.z, &s.yaw); s.dir = E.sera.dir;   /* dans un intérieur : devant sa porte, dans la rue */
    s.theme = (uint8_t)E.theme; s.fog = (uint8_t)(E.fog_on | (E.volant + 1) << 1); s.dither = (uint8_t)E.dither_on; s.bright = (uint8_t)(H.bright | E.eveil_essai << 3 | E.renc_mode << 4 | E.essai << 7);   /* fog, bits 1 à 3 : DIRECTION + 1 (0 : sauvegarde d'avant) ; bright, bit 3 : ÉVEIL, 4 à 6 : rencontres, 7 : mode d'essai */
    for (int i = 0; i < 4; i++) { s.hp[i] = PARTY[i].hp; s.mp[i] = PARTY[i].mp; }
    for (int i = 0; i < n_items; i++) s.items[i] = (uint8_t)ITEMS[i].count;
    s.level = E.level; s.exp = E.exp; s.credits = E.credits;
    s.team = (uint8_t)E.team; s.obj[0] = E.obj[0]; s.obj[1] = E.obj[1];
    memcpy(s.quests, E.quests, sizeof s.quests); s.qtrack = (int16_t)E.qtrack; s.mus_force = E.mus_force;
    memcpy(s.flags, E.flags, sizeof s.flags); memcpy(s.done, E.ev_done, sizeof s.done);
    memcpy(s.tenue, E.tenue_eq, sizeof s.tenue);
    memcpy(s.job, E.job, sizeof s.job); memcpy(s.jobs_known, E.jobs_known, sizeof s.jobs_known); memcpy(s.eq, E.eq, sizeof s.eq);
    s.guide = (int16_t)E.guide;
    if (E.heroine >= 0) snprintf(s.heroine, sizeof s.heroine, "%s", E.w.persos[E.heroine].nom);
    lieu_de(s.x, s.z, s.lieu, sizeof s.lieu);
    s.temps = (uint32_t)E.temps_jeu; s.carte = (uint32_t)E.w.carte;
    s.crc = crc32((const uint8_t *)&s, offsetof(save_t, crc));
    char t[48];
    const int ok = E.cfg.save_write(E.partie * 2 + (int)(s.seq & 1), &s, sizeof s) == (int)sizeof s;
    if (ok) snprintf(t, sizeof t, "Partie %d : sauvegarde n° %u", E.partie + 1, (unsigned)s.seq);
    else snprintf(t, sizeof t, "Échec de l'écriture");
    toast(t);
    return ok;
}

/* Plateforme : heure réelle, et sauvegarde quand la montre se met en veille ou que sa batterie se vide (pas pendant
   l'écran titre, un combat, une course, une scène ou un dialogue : la position ou l'état ne vaudraient rien) */
void eng_set_horloge(int secondes) { E.horloge = secondes >= 0 && secondes < 86400 ? secondes : -1; }
int eng_sauvegarder(void) {
    if (!E.cfg.save_write || accueil_on() || BT.on || CR.on || script_running() || H.dialog) return 0;
    return save_game();
}
int eng_ecran_titre(void) { return accueil_on(); }

/* Ancienne sauvegarde (n octets) : la même jusqu'à end, puis sa somme de contrôle (alignée sur 4) */
static int read_old(const save_t *s, int n, uint32_t magic, size_t end) {
    const size_t o = (end + 3) & ~(size_t)3;
    uint32_t c;
    if (n != (int)(o + 4) || s->magic != magic) return 0;
    memcpy(&c, (const uint8_t *)s + o, 4);
    return c == crc32((const uint8_t *)s, (int)o);
}
static int read_slot(int slot, save_t *s) {
    if (!E.cfg.save_read) return 0;
    const int n = E.cfg.save_read(slot, s, sizeof *s);
    if (n == (int)sizeof *s && s->magic == SAVE_MAGIC) return s->crc == crc32((const uint8_t *)s, offsetof(save_t, crc));
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
    static save_t s;
    memset(o, 0, sizeof *o);
    if (!partie_lire(p, &s)) return 0;
    o->seq = s.seq; o->ordre = s.ordre; o->niveau = s.level; o->temps = (float)s.temps;
    memcpy(o->heroine, s.heroine, sizeof o->heroine); o->heroine[15] = 0;
    memcpy(o->lieu, s.lieu, sizeof o->lieu); o->lieu[23] = 0;
    return 1;
}
/* Nouvelle partie dans la partie p : ses sauvegardes suivantes prennent la suite de ses numéros (la plus récente
   l'emporte) ; l'ancienne reste jusqu'à la première */
void hud_nouvelle(int p) {
    static save_t s;
    E.partie = p; H.seq = partie_lire(p, &s) ? (int)s.seq : 0;
    E.temps_jeu = 0;
    police_reset();   /* ni recherche ni poursuite */
}

static int load_partie(int p) {
    static save_t sv;
    save_t *s = &sv;
    if (!partie_lire(p, s)) { toast("Aucune sauvegarde valide"); return 0; }
    E.partie = p; H.seq = (int)s->seq;
    s->heroine[15] = 0; E.heroine = heroine_de(s->heroine);   /* avant les tenues : elles sont à elle */
    E.temps_jeu = (float)s->temps;
    int_reset();   /* on reprend dans la rue, sans alarme */
    police_reset();   /* ni recherche ni poursuite (pas sauvegardées) */
    ride_lacher();   /* à pied, comme à la sauvegarde (en conduisant, Sera restait dans le véhicule) */
    if (s->carte < (uint32_t)E.w.ncartes) world_carte((int)s->carte, s->x, s->z);   /* sur sa carte */
    E.sera.x = s->x; E.sera.z = s->z; E.sera.dir = s->dir; E.yaw = s->yaw;
    E.theme = s->theme % 5; E.fog_on = s->fog & 1; E.volant = (s->fog >> 1 & 7) ? ((s->fog >> 1 & 7) - 1) % 5 : VOLANT_DEFAUT; E.dither_on = s->dither; H.bright = (s->bright & 7) % 5; E.renc_mode = (s->bright >> 4 & 7) % 5; E.essai = s->bright >> 7; E.eveil_essai = E.essai && (s->bright >> 3 & 1);
    for (int i = 0; i < n_items; i++) ITEMS[i].count = s->items[i];
    for (int i = 0; i < 4; i++) {
        E.job[i] = s->job[i] < E.w.njobs ? s->job[i] : EQ_AUCUN; E.jobs_known[i] = s->jobs_known[i];
        for (int k = 0; k < 3; k++) E.eq[i][k] = eq_slot(s->eq[i][k]) == k ? s->eq[i][k] : EQ_AUCUN;
    }
    party_set_level(s->level); E.exp = s->exp; E.credits = s->credits;
    for (int i = 0; i < 4; i++) {
        PARTY[i].hp = s->hp[i] < PARTY[i].max ? s->hp[i] : PARTY[i].max;
        PARTY[i].mp = s->mp[i] < PARTY[i].maxmp ? s->mp[i] : PARTY[i].maxmp;
    }
    E.team = (s->team & 15) | 1; E.obj[0] = s->obj[0]; E.obj[1] = s->obj[1];
    memcpy(E.quests, s->quests, sizeof E.quests); E.qtrack = s->qtrack; E.mus_force = s->mus_force; H.nann = 0;
    memcpy(E.flags, s->flags, sizeof E.flags); memcpy(E.ev_done, s->done, sizeof E.ev_done); script_after_load();
    for (int i = 0; i < 4; i++) E.tenue_eq[i] = tenue_mine(i, s->tenue[i]) ? s->tenue[i] : -1;
    E.guide = s->guide >= 0 && s->guide < E.w.nguides ? s->guide : -1;
    E.has_dest = 0;
    if (E.cfg.set_brightness) E.cfg.set_brightness(64 + H.bright * 48);
    char t[48]; snprintf(t, sizeof t, "Partie %d : sauvegarde n° %u chargée", p + 1, (unsigned)s->seq);
    toast(t);
    return 1;
}
static void load_game(void) { load_partie(E.partie); }
int hud_charger(int p) { return load_partie(p); }

/* ---------- Actions ---------- */
static int items_count(void) { int n = 0; for (int i = 0; i < n_items; i++) n += ITEMS[i].count; return n; }
/* Objets du menu : ceux qu'on a ; leur nombre, l'index dans ITEMS de la ligne row (-1 : aucun) */
/* Objets du sac montrés : ceux qu'on a (un objet 18+ caché tant que le 18+ est verrouillé, item_vu) */
static int owned_count(void) { int n = 0; for (int i = 0; i < n_items; i++) n += ITEMS[i].count > 0 && item_vu(&ITEMS[i]); return n; }
static int owned_item(int row) { for (int i = 0; i < n_items; i++) if (ITEMS[i].count > 0 && item_vu(&ITEMS[i]) && row-- == 0) return i; return -1; }
/* Membres de l'équipe : leur nombre, l'index dans PARTY du k-ième, leurs techniques */
static int team_count(void) { int n = 0; for (int i = 0; i < 4; i++) n += in_team(i); return n; }
static int team_at(int k) { for (int i = 0; i < 4; i++) if (in_team(i) && k-- == 0) return i; return 0; }
/* Lignes du menu TECHNIQ. : pour chaque membre de l'équipe, ses techniques puis ses sorts (job, équipement) */
static int team_techs(void) { int n = 0, l[MAX_SORTS]; for (int i = 0; i < 4; i++) if (in_team(i)) n += PARTY[i].ntech + sorts_of(i, l); return n; }
/* Icône d'un objet du sac dans une liste : l'image de l'équipement, sinon son icône 16 × 16 */
static int item_icon(int i) { const eqitem_t *r = eq_item(i); return r && r->img ? ICON_OBJG + r->img : ITEMS[i].icon; }
/* Lignes du menu ÉQUIP. : pour chaque membre de l'équipe, ER_JOB (avec les jobs de l'Atelier), arme, armure,
   accessoire (EQ_*, avec les types d'équipement), ER_TENUE (s'il en a, ou sans jobs : la ligne d'origine).
   Renvoie leur nombre ; mi, sl : membre et emplacement de la ligne row (si non NULL) */
enum { ER_JOB = -1, ER_TENUE = 3 };
static const char *const SLOT_NAME[3] = { "ARME", "ARMURE", "ACCESS." };
static const char *const AFF_NAME[5] = { "—", "C", "B", "A", "S" };
static int equip_rows(int row, int *mi, int *sl) {
    const int eq = E.w.neqtypes > 0;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (!in_team(i)) continue;
        int slots[5], ns = 0;
        if (eq && E.w.njobs) slots[ns++] = ER_JOB;
        if (eq) { slots[ns++] = EQ_ARME; slots[ns++] = EQ_ARMURE; slots[ns++] = EQ_ACCESS; }
        if (!eq || has_tenues(i)) slots[ns++] = ER_TENUE;
        for (int k = 0; k < ns; k++, n++) if (n == row && mi && sl) { *mi = i; *sl = slots[k]; }
    }
    return n;
}
/* Quêtes du menu : la suivie, celles en cours, puis les terminées ; renvoie leur nombre (rows : leurs index, ou NULL) */
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
/* ---------- Guidage : villes ou bâtiments spéciaux (section GUID), du plus proche au plus loin (guideRows de l'émulateur) ---------- */
#define GUIDE_RGB 0x5DFF8A       /* chevron et repère du guidage, à côté de ceux de la quête (teinte 2) */
#define GR_STOP (-3)
#define GR_FAM 0x8000             /* + groupe : villes (0), lieux (1), familles (2 + rang) */
#define GR_CAT 0x10000
/* Familles du catalogue des bâtiments, dans l'ordre de FAMILLES_GUIDE de l'émulateur (groupe 2 + rang) */
static const char *const FAM_GUIDE[13] = { "COMMERCES", "LOISIRS", "SERVICES", "ORDRE", "VÉHICULES", "INDUSTRIE", "SOUTERRAIN", "HABITAT",
                                           "ENCEINTE", "CAMPEMENTS", "AUTRES MONDES", "RUINES", "AUTRES" };
static const char *groupe_nom(int g) { return g == 0 ? "VILLES" : g == 1 ? "LIEUX" : FAM_GUIDE[g - 2 < 13 ? g - 2 : 12]; }
static int GR[MAX_GUIDE + 2] ENG_FROID; static float GD[MAX_GUIDE + 2] ENG_FROID;   /* lignes du menu (codes GR_* ou lieu), distances */
static float guide_dist(int k) { return hypotf(E.w.guides[k].x - E.sera.x, E.w.guides[k].z - E.sera.z); }
/* Même sorte : deux bâtiments du même groupe et du même nom */
static int same_cat(int a, int b) {
    const guide_t *g = E.w.guides;
    return g[a].groupe == g[b].groupe && !strcmp(g[a].nom, g[b].nom);
}
/* Nom d'un lieu : un bâtiment porte celui de la ville la plus proche (à moins de 60 cases), après sep */
static void guide_label(int k, const char *sep, const char *end, char *t, int n) {
    const guide_t *g = E.w.guides;
    int v = -1; float best = 60;
    if (g[k].kind == GUIDE_BAT)
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
        if (E.guide >= 0) { GR[n] = GR_STOP; GD[n] = -1; n++; }
        head = n;
        for (int k = 0; k < E.w.nguides; k++) {
            const float d = guide_dist(k);
            int f = -1;
            for (int i = head; i < n && f < 0; i++) if (GR[i] == GR_FAM + g[k].groupe) f = i;
            if (f < 0) { GR[n] = GR_FAM + g[k].groupe; GD[n] = d; n++; }
            else if (d < GD[f]) GD[f] = d;
        }
    } else if (H.gcat >= GR_FAM + 2 && H.gcat < GR_CAT) {   /* une famille : ses sortes */
        for (int k = 0; k < E.w.nguides; k++) {
            if (GR_FAM + g[k].groupe != H.gcat) continue;
            const float d = guide_dist(k);
            int f = -1;
            for (int i = 0; i < n && f < 0; i++) if (same_cat(GR[i] - GR_CAT, k)) f = i;
            if (f < 0) { GR[n] = GR_CAT + k; GD[n] = d; n++; }
            else if (d < GD[f]) GD[f] = d;
        }
    } else {   /* villes, lieux, ou les bâtiments d'une sorte */
        for (int k = 0; k < E.w.nguides; k++)
            if (H.gcat >= GR_CAT ? same_cat(H.gcat - GR_CAT, k) : GR_FAM + g[k].groupe == H.gcat) { GR[n] = k; GD[n] = guide_dist(k); n++; }
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
/* Arrivée : en deçà du rayon du lieu, le guidage s'arrête */
static void guide_step(void) {
    if (E.guide < 0 || E.guide >= E.w.nguides) { E.guide = -1; return; }
    if (guide_dist(E.guide) >= E.w.guides[E.guide].r) return;
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
static int TR[259];   /* lignes : -1 - sorte (liste des sortes), sinon rang du lieu dans TELE */
static const uint8_t *tele_sec(int *n) {
    uint32_t sz = 0;
    const uint8_t *s = pak_section("TELE", &sz);
    *n = s && sz >= 4 && 4 + (uint64_t)rd32(s) * 36 <= sz ? (int)rd32(s) : 0;
    return *n ? s + 4 : NULL;
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
    if (H.tcat < 0) {
        for (int c = 0; c < 3; c++) { int k = 0; for (int i = 0; i < nt; i++) k += p[i * 36 + 24] == c; if (k) TR[n++] = -1 - c; }
        return n;
    }
    for (int i = 0; i < nt && n < 256; i++) if (p[i * 36 + 24] == H.tcat) TR[n++] = i;
    return n;
}
/* Galerie (SYSTÈME › GALERIE) : les images du jeu (IMAG) par catégorie ; l'image de la ligne choisie derrière la liste,
   en grand quand on la touche (un toucher ou RETOUR la referme). Transformations : une fois l'éveil appris */
enum { GAL_CUT, GAL_PLEIN, GAL_DOS, GAL_DEGATS, GAL_TRANSFO, GAL_SCENES, GAL_INTRO, GAL_N };
static const char *GAL_CAT[GAL_N] = { "CUT-INS", "PLEIN ÉCRAN", "FACE / DOS", "DÉGÂTS", "TRANSFORMATIONS", "SCÈNES", "INTRODUCTION" };
#define GAL_MAX 600
static int GL[GAL_MAX], GLP[GAL_MAX];   /* lignes : -1 - catégorie, ou image IMAG ; son personnage (PERS, 255 Sera, -1 aucun) */
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
    if (cat == GAL_SCENES) { for (int k = 0; k < E.w.nscenes; k++) if (!(E.w.scene18[k] && verrou18())) GAL_AJOUT(E.w.scene_img[k], -1); }
    else if (cat == GAL_INTRO) for (int k = 0; k < E.w.ouv_ncartes; k++) GAL_AJOUT(E.w.ouv_img[k], -1);
    else if (cat != GAL_TRANSFO || eveil_connu())
        for (int i = 0; i < E.w.ncutins; i++) if (gal_de(i) == cat && cut_vu(E.w.cutins[i].ed)) for (int j = 0; j < E.w.cutins[i].n; j++) GAL_AJOUT(E.w.cutins[i].first + j, E.w.cutins[i].pers);
    #undef GAL_AJOUT
    return n;
}
static int gal_rows(void) {
    static int tmp[GAL_MAX], tp[GAL_MAX];
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
    const int essai = !strcmp(H.code, CODE_ESSAI), ok = essai || (!strcmp(H.code, CODE18) && E.w.edition == 2);
    if (essai) { E.essai = !E.essai; if (!E.essai) E.eveil_essai = 0; }
    else if (ok && E.deverrou18) edition_verrouiller(); else if (ok) edition_code(H.code);
    if (ok) { H.sub = SUB_SYSTEM; H.sub_sel = 14; H.sub_scroll = 14 - ROWS + 1 > 0 ? 14 - ROWS + 1 : 0; toast("Code accepté"); }
    else toast("Code erroné");
    H.code[0] = 0;
}

static void sub_back(void) { if (H.sub == SUB_SHOP) { H.sub = -1; H.menu = 0; SHOP.comm = -1; return; }
    if (H.sub == SUB_CODE) { H.sub = SUB_SYSTEM; H.sub_sel = 14; H.sub_scroll = 14 - ROWS + 1 > 0 ? 14 - ROWS + 1 : 0; H.code[0] = 0; return; }
    if (H.sub == SUB_GALERIE) {   /* l'image en grand, puis les catégories, puis le menu SYSTÈME, sur sa ligne */
        if (H.galgrand) H.galgrand = 0;
        else if (H.galcat >= 0) { H.galcat = -1; H.sub_sel = 0; H.sub_scroll = 0; }
        else { H.sub = SUB_SYSTEM; H.sub_sel = 11; H.sub_scroll = 11 - ROWS + 1 > 0 ? 11 - ROWS + 1 : 0; }
        return;
    }
    if (H.sub == SUB_TELE) {   /* des lieux aux sortes, puis au menu SYSTÈME, sur sa ligne */
        if (H.tcat >= 0) { H.tcat = -1; H.sub_sel = 0; H.sub_scroll = 0; }
        else { H.sub = SUB_SYSTEM; H.sub_sel = 10; H.sub_scroll = 10 - ROWS + 1 > 0 ? 10 - ROWS + 1 : 0; }
        return;
    } if (H.sub == SUB_GUIDE && H.gcat >= GR_CAT) { H.gcat = GR_FAM + H.gfam; H.sub_sel = 0; H.sub_scroll = 0; }
    else if (H.sub == SUB_GUIDE && H.gcat != -1) { H.gcat = -1; H.sub_sel = 0; H.sub_scroll = 0; } else H.sub = -1; }

/* Ligne ACCUEIL du menu SYSTÈME (montre : menu des applications) : la dernière, après CODE s'il y est */
static int systeme_base(void) { return 15 + E.essai; }   /* CODE toujours (14) ; ÉVEIL en mode d'essai (15) */
static int accueil_ligne(void) { return E.cfg.montre || E.cfg.napps > 0 || E.cfg.nregls > 0 ? systeme_base() : -1; }
/* Réglages de la plateforme montrés en jeu (eng_config_t.regls_jeu ; ESP32 : MANETTE), après MONTRE ou ACCUEIL */
static int plateforme_ligne(void) { return systeme_base() + (accueil_ligne() >= 0); }

static int sub_rows(void) {
    switch (H.sub) {
    case SUB_ITEMS: return owned_count();
    case SUB_TECH: return team_techs();
    case SUB_EQUIP: return equip_rows(-1, NULL, NULL);
    case SUB_STATUS: return team_count();
    case SUB_SYSTEM: return plateforme_ligne() + E.cfg.nregls_jeu;   /* montre : ACCUEIL, puis les réglages de la plateforme */
    case SUB_CODE: return 0;
    case SUB_TELE: return tele_rows();
    case SUB_GALERIE: return gal_rows();
    case SUB_QUESTS: return quest_rows(NULL);
    case SUB_GUIDE: return guide_rows();
    case SUB_SHOP: return shop_rows();
    default: return 0;
    }
}
/* Ligne row du menu TECHNIQ. : membre m, technique t (t >= 0) ou sort -1 - t du catalogue (t < 0) */
static void tech_at(int row, int *m, int *t) {
    int l[MAX_SORTS];
    for (int i = 0; i < 4; i++) {
        if (!in_team(i)) continue;
        if (row < PARTY[i].ntech) { *m = i; *t = row; return; }
        row -= PARTY[i].ntech;
        const int ns = sorts_of(i, l);
        if (row < ns) { *m = i; *t = -1 - l[row]; return; }
        row -= ns;
    }
    *m = 0; *t = 0;
}
/* Membre de l'équipe le plus blessé (PV ou PM) */
static int weakest(int mp) {
    int best = 0; float br = 2;
    for (int i = 0; i < 4; i++) {
        if (!in_team(i)) continue;
        float r = mp ? (float)PARTY[i].mp / PARTY[i].maxmp : (float)PARTY[i].hp / PARTY[i].max;
        if (r < br) { br = r; best = i; }
    }
    return best;
}

static void open_sub(int k) { H.sub = k; H.sub_sel = 0; H.sub_scroll = 0; H.fling = 0; H.gcat = -1; }

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

static void activate_row(int row) {
    char t[80];
    if (H.sub == SUB_GALERIE) {   /* une catégorie : ses images ; une image : en grand (un autre toucher la referme) */
        if (H.galgrand) { H.galgrand = 0; return; }
        if (row >= gal_rows()) return;
        if (GL[row] < 0) { H.galcat = -1 - GL[row]; H.sub_sel = 0; H.sub_scroll = 0; }
        else { H.sub_sel = row; H.galgrand = 1; }
        return;
    }
    if (H.sub == SUB_SHOP) {   /* boutique : un exemplaire acheté ou vendu */
        const int k = shop_item(row);
        if (k < 0) return;
        const int px = shop_prix(k);
        if (SHOP.mode == 0) {
            if (E.credits < px) { toast("Pas assez de crédits"); son_jouer(SON_ANNULER, 0.45f, 1); return; }
            if (ITEMS[k].count >= 99) { toast("Le sac en est plein"); son_jouer(SON_ANNULER, 0.45f, 1); return; }
            E.credits -= px; ITEMS[k].count++; snprintf(t, sizeof t, "%s · −%d CR", ITEMS[k].name, px);
        } else { ITEMS[k].count--; E.credits += px; snprintf(t, sizeof t, "%s vendu · +%d CR", ITEMS[k].name, px); }
        toast(t); son_jouer(SON_VALIDER, 0.4f, SHOP.mode == 0 ? 1.05f : 0.9f);
        return;
    }
    if (H.sub == SUB_ITEMS) {
        const int k = owned_item(row);
        if (k < 0) { toast("Plus aucun exemplaire"); return; }
        item_t *it = &ITEMS[k];
        if (!it->hp && !it->mp) { toast(it->type == 1 ? "Objet de quête" : it->type == 2 ? "Objet à revendre" : it->type == 4 ? "Tenue : menu ÉQUIP." : it->type == 5 ? "Équipement : menu ÉQUIP." : "Ne s'utilise pas ici"); return; }
        /* Toute l'équipe, ou le membre le plus blessé (en PV si l'objet en rend, sinon en PM) ; PV et PM à la fois */
        const int one = weakest(it->hp == 0);
        int hp = 0, mp = 0;
        for (int i = 0; i < 4; i++) {
            if (it->team ? !in_team(i) : i != one) continue;
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
        if (m->mp < cout) { toast("PM insuffisants"); return; }
        int gain = 0;
        for (int i = 0; i < 4; i++) {
            if (!in_team(i)) continue;
            if (sp && sp->soin == 2) { if (PARTY[i].hp <= 0) continue; } else if (i != weakest(0)) continue;   /* un allié : le plus faible */
            member_t *o = &PARTY[i];
            const int g = soin < o->max - o->hp ? soin : o->max - o->hp;
            o->hp += g; gain += g;
        }
        if (gain <= 0) { toast("Tous les PV sont au maximum"); return; }
        m->mp -= cout;
        snprintf(t, sizeof t, "%s · %s +%d PV", nom, sp && sp->soin == 2 ? "ÉQUIPE" : PARTY[weakest(0)].name, gain);
        toast(t);
    } else if (H.sub == SUB_EQUIP) {
        int mi = 0, sl = ER_TENUE;
        if (row >= equip_rows(row, &mi, &sl)) return;
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
        if (sl != ER_TENUE) {   /* rien, puis les objets du sac que son job peut porter (l'ancien retourne au sac) */
            const int cur = E.eq[mi][sl];
            int nx = EQ_AUCUN, any = 0;
            for (int d = 1; d <= n_items; d++) {
                const int i = ((cur == EQ_AUCUN ? -1 : cur) + d) % (n_items + 1);   /* n_items : aucun */
                if (i == n_items) { if (cur != EQ_AUCUN) { nx = EQ_AUCUN; any = 1; break; } continue; }
                if (eq_slot(i) == sl && eq_aff(mi, i) && (ITEMS[i].count > 0 || i == cur) && i != cur) { nx = i; any = 1; break; }
            }
            if (!any) { snprintf(t, sizeof t, "Rien à porter (%s) pour %s", SLOT_NAME[sl], PARTY[mi].name); toast(t); return; }
            if (cur != EQ_AUCUN) ITEMS[cur].count++;
            if (nx != EQ_AUCUN) ITEMS[nx].count--;
            E.eq[mi][sl] = (uint8_t)nx; party_refresh(mi);
            if (nx == EQ_AUCUN) snprintf(t, sizeof t, "%s : %s retirée", PARTY[mi].name, SLOT_NAME[sl]);
            else snprintf(t, sizeof t, "%s : %s (%s)", PARTY[mi].name, ITEMS[nx].name, AFF_NAME[eq_aff(mi, nx)]);
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
        if (c == GR_STOP) { E.guide = -1; toast("Guidage arrêté"); }
        else if (c >= GR_CAT) { H.gfam = E.w.guides[c - GR_CAT].groupe; H.gcat = c; H.sub_sel = 0; H.sub_scroll = 0; }
        else if (c >= GR_FAM) { H.gcat = c; H.sub_sel = 0; H.sub_scroll = 0; }
        else {
            char nm[48];
            E.guide = c; guide_label(c, " (", ")", nm, sizeof nm);
            snprintf(t, sizeof t, "Guidage : %s", nm);
            toast(t);
            H.sub = -1; H.menu = 0;
        }
    } else if (H.sub == SUB_SYSTEM) {
        switch (row) {
        case 0: save_game(); break;
        case 1: load_game(); break;
        case 2: E.theme = (E.theme + 1) % 5; break;
        case 3: E.fog_on = !E.fog_on; break;
        case 4: E.dither_on = !E.dither_on; break;
        case 5: H.bright = (H.bright + 1) % 5; if (E.cfg.set_brightness) E.cfg.set_brightness(64 + H.bright * 48); break;
        case 6: {   /* zoom : niveau suivant */
            /* Niveaux : le zoom mini, puis ceux de la liste au-dessus de lui ; après le dernier, retour au mini */
            static const float Z[9] = { 0.8f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, ZOOM_MAX };
            float next = E.w.zoom_min;
            for (int i = 0; i < 9; i++) if (Z[i] > E.w.zoom_min + 0.01f && Z[i] > E.zoom + 0.01f) { next = Z[i]; break; }
            eng_set_zoom(next);
            break;
        }
        case 7: E.weather = E.meteo_choix = (E.weather + 1) % 3; break;   /* météo : automatique, clair, pluie */
        case 8: H.vol_on = 1; H.acc = 0; break;   /* son : cadran réglé en tournant le bord (vol_pas) */
        case 9: E.cycle = (E.cycle + 1) % (E.horloge >= 0 ? 4 : 3); break;   /* cycle jour / nuit : automatique, jour, nuit, heure réelle (montre) */
        case 10: H.sub = SUB_TELE; H.tcat = -1; H.sub_sel = 0; H.sub_scroll = 0; break;   /* téléport : ses sortes de lieux */
        case 11: H.sub = SUB_GALERIE; H.galcat = -1; H.galgrand = 0; H.sub_sel = 0; H.sub_scroll = 0; break;   /* galerie : ses catégories */
        case 12: E.renc_mode = (E.renc_mode + 1) % 5; break;   /* rencontres : normales, rares, très rares, aucune, fréquentes */
        case 13: E.volant = (E.volant + 1) % 5; break;   /* direction en course : très douce, douce, moyenne, vive, très vive */
        case 14: H.sub = SUB_CODE; H.code[0] = 0; break;   /* le pavé du code (18+ de l'édition unique, mode d'essai) */
        case 15:   /* mode d'essai : transformée ou non */
            if (E.essai) { E.eveil_essai = !E.eveil_essai; toast(E.eveil_essai ? "Transformée" : "Transformation levée"); }
            break;
        }
        if (row >= plateforme_ligne() && row < plateforme_ligne() + E.cfg.nregls_jeu) {   /* réglage de la plateforme : un cran */
            const eng_reglage_t *pr = &E.cfg.regls_jeu[row - plateforme_ligne()];
            if (pr->changer) pr->changer(1);
        }
        if (row == accueil_ligne()) {   /* montre : partie sauvegardée, puis l'accueil de la montre (ou le menu des applications) */
            if (!save_game()) return;
            H.sub = -1; H.menu = 0;
            if (E.cfg.montre) E.cfg.montre(); else accueil_debut();
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
        if (E.ride >= 0 || CR.on) { toast("Descendre du véhicule d'abord"); return; }
        if (POL.niveau) { toast("Recherchée : semer d'abord la police"); return; }   /* pas de fuite par le téléport */
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

/* Son : d pas de 5 % (0 à 100 %), bip au nouveau niveau */
#define VOL_PAS_RAD 0.13f   /* angle tourné sur le bord pour un pas : de coupé à 100 % en un peu moins d'un demi-tour */
static void vol_build(void);
static void vol_pas(int d) {
    const int v0 = (int)lroundf(eng_volume() * 20), v = v0 + d < 0 ? 0 : v0 + d > 20 ? 20 : v0 + d;
    if (v == v0) return;
    eng_set_volume(v / 20.0f); H.vol_t = H.now;
    son_jouer(SON_BIP, 0.6f, 0.8f + v * 0.02f);
}

static void commerce_ouvrir(void);
static void advance_dialog(void) {
    int len = 0; for (const char *s = D.text[H.line]; *s; s++) if ((*s & 0xC0) != 0x80) len++;
    if ((H.now - H.line_t0) / 0.028f < len) { H.line_t0 = -1e9f; return; }
    if (++H.line >= D.n) { H.dialog = 0; if (H.apres) { H.apres = 0; commerce_ouvrir(); } } else H.line_t0 = H.now;
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
    make_portrait(1, pe->sheet, perso_remap(E.npcs[E.talk].perso, E.npcs[E.talk].var));   /* dans ses couleurs (palette PVAR) */
    D.n = 0;
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
        D.text[0] = repl_ligne(metier ? metier - 1 : REPL_VILLE, gen, sizeof gen) ? gen : "…"; D.n = 1;
    }
    /* Un commerçant de l'intérieur : ce qu'il propose, une fois sa réplique dite */
    const int bc = E.in.on && E.in.bat < E.w.nbats ? E.w.bats[E.in.bat].comm : 0;
    const comm_t *cm = bc && bc <= E.w.ncomms ? &E.w.comms[bc - 1] : NULL;
    H.apres = 0;
    if (cm && metier && (cm->qui >> (metier - 1) & 1) && (cm->n || cm->rachete || cm->soins || cm->verre)) {
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
    H.dialog = 1; H.line = 0; H.line_t0 = H.now; E.has_dest = 0;
}

/* ---------- Commerce ---------- */
static int shop_ok_vente(int k) { const comm_t *c = &E.w.comms[SHOP.comm]; return ITEMS[k].count > 0 && item_vu(&ITEMS[k]) && ITEMS[k].prix > 0 && ITEMS[k].type >= 0 && ITEMS[k].type < 8 && (c->rachete >> ITEMS[k].type & 1); }
static int shop_rows(void) {
    if (SHOP.comm < 0 || SHOP.comm >= E.w.ncomms) return 0;
    const comm_t *c = &E.w.comms[SHOP.comm];
    int n = 0;
    if (SHOP.mode == 0) { for (int i = 0; i < c->n; i++) n += c->vend[i] < n_items && item_vu(&ITEMS[c->vend[i]]); }
    else for (int k = 0; k < n_items; k++) n += shop_ok_vente(k);
    return n;
}
static int shop_item(int row) {
    const comm_t *c = &E.w.comms[SHOP.comm];
    if (SHOP.mode == 0) { for (int i = 0; i < c->n; i++) if (c->vend[i] < n_items && item_vu(&ITEMS[c->vend[i]]) && row-- == 0) return c->vend[i]; }
    else for (int k = 0; k < n_items; k++) if (shop_ok_vente(k) && row-- == 0) return k;
    return -1;
}
static int shop_prix(int k) { return SHOP.mode == 0 ? ITEMS[k].prix : ITEMS[k].prix / 2; }
/* Ce que propose le commerçant : acheter, vendre, soins de l'équipe, un verre ; partir */
static void commerce_ouvrir(void) {
    if (SHOP.comm < 0 || SHOP.comm >= E.w.ncomms) return;
    const comm_t *c = &E.w.comms[SHOP.comm];
    char o[4][48]; int n = 0;
    if (c->n) { snprintf(o[n], 48, "ACHETER"); SHOP.act[n++] = 0; }
    if (c->rachete) { snprintf(o[n], 48, "VENDRE"); SHOP.act[n++] = 1; }
    if (c->soins && n < 3) { snprintf(o[n], 48, "SOINS · %d CR", c->soins); SHOP.act[n++] = 2; }
    if (c->verre && n < 3) { snprintf(o[n], 48, "UN VERRE · %d CR", c->verre); SHOP.act[n++] = 3; }
    snprintf(o[n], 48, "PARTIR"); SHOP.act[n++] = 4;
    hud_choice(o, n);
    CHX.commerce = 1; snprintf(CHX.titre, sizeof CHX.titre, "%s · %d CR", SHOP.nom, E.credits);
}
/* Soins (part 1 : PV et PM au maximum) ou un verre (part 0,2 des PV) pour toute l'équipe */
static void commerce_service(int prix, float part, const char *msg) {
    char t[64];
    if (E.credits < prix) { snprintf(t, sizeof t, "Pas assez de crédits (%d CR)", E.credits); toast(t); son_jouer(SON_ANNULER, 0.45f, 1); return; }
    E.credits -= prix;
    for (int i = 0; i < 4; i++) {
        if (!in_team(i)) continue;
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
    case 0: case 1: SHOP.mode = SHOP.act[k]; H.menu = 1; open_sub(SUB_SHOP); E.has_dest = 0; break;
    case 2: commerce_service(c->soins, 1, "Équipe soignée"); break;
    case 3: commerce_service(c->verre, 0.2f, "À la vôtre"); break;
    default: break;
    }
}

static int talk_is_vehicle(void);
/* Conduite à deux mains (écran natif 466) : à gauche du milieu, le pouce gauche avance (vers le haut) ou recule (vers
   le bas) ; à droite, le pouce droit braque (vers la gauche ou la droite). Chaque doigt prend pour origine l'endroit
   où il se pose : plein gaz à DRV_GAZ px, plein braquage à DRV_VOL px. Au repos, les deux rails sont dessinés à leur
   place (GAZ_X, VOL_X). Un doigt posé sur un bouton (sortie, pause, tir) le touche au lieu de conduire ; posé sur un
   bouton d'altitude (vaisseau en vol, au milieu en bas), il le tient. */
#define DRV_GAZ 50.0f
#define DRV_VOL 60.0f
#if ENG_DYN
#define GAZ_X 110.0f
#define GAZ_Y (UH - 300)
#define VOL_X (UW - 150)
#define VOL_Y (UH - 280)
#define ALT_X (UW / 2)
#define ALT_UP_Y (UH - 380)
#define ALT_DN_Y (UH - 320)
#else
#define GAZ_X 125.0f
#define GAZ_Y 305.0f
#define VOL_X 345.0f
#define VOL_Y 320.0f
#define ALT_X 233.0f
#define ALT_UP_Y 336.0f
#define ALT_DN_Y 394.0f
#endif
#define ALT_R 24.0f
static int alt_visible(void) { return ship_alt_ok(); }
/* Doigts qui conduisent (deux au plus), suivis d'une image à l'autre par leur position (leur rang change quand l'autre
   se lève) ; rôle : 0 gaz, 1 volant, 2 bouton d'altitude, 3 rien (bouton touché, ou doigt remplacé) */
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
    uint8_t fige_trace, fige_riv, fige_armes, fige_tours, fige_niv;   /* réglages imposés par le scénario */
    uint8_t ord[MAX_PERSOS], mod0[MAX_PERSOS], modn[MAX_PERSOS]; int riv[RM_NR], trace[RM_TR];
} RM;
static int rm_perso(void) { return RM.ord[RM.mod0[RM.mod] + RM.coul]; }
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
static int rm_apercu(int k, int perso, int dir, int size) {
    if (!rm_px) {
        rm_px = E.cfg.alloc ? E.cfg.alloc((size_t)(1 + RM_NR) * RM_AP * RM_AP * 3, 0) : NULL;
        if (!rm_px) return 0;
        rm_mk = (uint8_t *)(rm_px + (1 + RM_NR) * RM_AP * RM_AP);
    }
    if (rm_cle[k] == perso * 8 + dir + 1) return 1;
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
    const uint8_t *rmp = E.w.remap[pe->remap];
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const int cx = x0 + x * f + ox, cy = y0 + y * f + oy;
        const int id = cx >= 0 && cy >= 0 && cx < sh->fw && cy < sh->fh ? blk[cy * sw + cx] : 0;
        const uint8_t *c = E.w.pal[rmp[id]];
        px[y * w + x] = ui_rgb(((uint32_t)c[0] << 16) | (c[1] << 8) | c[2]); mk[y * w + x] = id != 0;
    }
    rm_cle[k] = perso * 8 + dir + 1; rm_w[k] = w; rm_h[k] = h;
    return 1;
}
int hud_course_menu(int tours, int nia, int niv, int veh) {
    course_cancel();
    if (!course_choisir(E.sera.x, E.sera.z) || CR.on) { toast("Pas de piste de course ici"); return 0; }
    uint8_t vu[MAX_PERSOS] = { 0 };
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
    RM.fige_trace = RM.fige_riv = RM.fige_armes = RM.fige_tours = RM.fige_niv = 0;
    memset(rm_cle, 0, sizeof rm_cle);
    rm_rivaux();
    son_jouer(SON_MENU, 0.35f, 1);
    return 1;
}
int hud_course_impose(int tours, int niv, int veh, const char *trace, int armes, int chrono, const int *riv, int nriv) {
    float sx = E.sera.x, sz = E.sera.z;
    if (trace) for (int k = 0; k < course_npistes(); k++) {   /* la piste active : celle du tracé de ce nom */
        const piste_t *P = course_piste(k);
        if (!strcmp(P->nom, trace)) { sx = P->X[P->dep]; sz = P->Z[P->dep]; break; }
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
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static void course_menu_change(int row, int d) {
    RM.row = row;
    if (row && rm_fige(RM.page, row)) return;
    if (row == 0) RM.page = (RM.page + d + 3) % 3;
    else if (RM.page == 0) {
        if (row == 1) { RM.mod = (RM.mod + d + RM.nmod) % RM.nmod; RM.coul = 0; RM.t0 = H.now; }
        else RM.coul = (RM.coul + d + RM.modn[RM.mod]) % RM.modn[RM.mod];
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
    RM.on = 0;
    if (RM.ntr) course_trace(RM.trace[RM.tr]);
    if (!course_start(rm_perso(), RM.tours, RM.riv, RM.nia, RM.niv, RM.armes)) toast("Départ impossible");
    else CR.chrono = RM.chrono;
}
static void course_menu_cancel(void) { RM.on = 0; course_cancel(); }

/* Réponse k d'un choix : au scénario, ou à la porte fermée (forcer ou partir) */
static void choice_pick(int k) {
    CHX.on = 0;
    if (CHX.porte) { CHX.porte = 0; int_choix(k); }
    else if (CHX.commerce) { CHX.commerce = 0; commerce_choix(k); }
    else script_choose(k + 1);
}
static void do_action(int act, int arg) {
    switch (act) {
    case A_TALK:   /* parler, monter dans un véhicule ou en descendre */
        if (E.ride >= 0 && fabsf(E.ride_v) > 0.3f) toast("Arrêtez-vous pour descendre");
        else if (E.ride >= 0) { if (!vehicle_unboard()) toast(ride_type() == VEH_VAISSEAU ? "Pas de place pour atterrir" : "Pas de place pour descendre"); }
        else if (E.talk <= TALK_TRAF(0)) { if (vehicle_steal(TALK_TRAF(0) - E.talk)) toast(vol_cri); }   /* le cri du conducteur éjecté */
        else if (talk_is_vehicle()) vehicle_board(E.talk);
        else if (!script_talk(E.talk)) open_dialog();   /* un événement « parler » remplace les répliques */
        break;
    case A_MENU_ITEM: if (H.sel == arg) open_sub(arg); else H.sel = arg; break;
    case A_OPEN: open_sub(H.sel); break;
    case A_ROW: if (H.sub_sel == arg) activate_row(arg); else H.sub_sel = arg; break;
    case A_BACK: sub_back(); break;
    case A_CODE: code_touche(arg); break;
    case A_BATTLE: battle_command(arg >> 3, arg & 7); break;   /* commande × 8 + technique */
    case A_CHOICE: if (CHX.sel == arg) { choice_pick(arg); son_jouer(SON_VALIDER, 0.4f, 1); } else CHX.sel = arg; break;   /* surligner, puis répondre */
    case A_PORTE: int_interagir(); break;   /* devant une porte : entrer ou forcer */
    case A_MENU_BTN: H.menu = !H.menu; E.has_dest = 0; son_jouer(SON_MENU, 0.35f, 1); break;
    case A_MENU_GO: H.sel = arg; open_sub(arg); break;
    case A_VALIDER: if (sub_rows() > 0) activate_row(H.sub_sel); break;
    case A_COURSE: hud_course_menu(0, -1, -1, -1); break;
    case A_RACE_CHG: course_menu_change(arg >> 1, arg & 1 ? 1 : -1); break;   /* ligne × 2 + (1 : suivant) */
    case A_RACE_GO: course_menu_go(); break;
    case A_RACE_BACK: course_menu_cancel(); break;
    case A_RACE_PAGE: RM.page = arg; RM.row = 0; son_jouer(SON_MENU, 0.35f, 1); break;
    case A_FIRE: course_tir(); break;
    case A_PAUSE: course_pause(1); break;
    case A_REPRENDRE: course_pause(0); break;
    case A_ABANDON: course_abandon(); break;
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
                int sh = E.w.persos[i].sheet;
                for (int m = 1; m < 4; m++) if (E.w.party_perso[m] == i) sh = member_sheet(m);   /* un membre : dans sa tenue */
                make_portrait(1, sh, E.w.persos[i].remap); found = 1;
            }
        for (int i = 1; i < 4 && !found; i++)   /* tables de couleurs 2 à 4 : Varn, Bors, Lyse */
            if (!strcmp(PARTY[i].name, right)) { make_portrait(1, -1, i + 1); found = 1; }
        if (!found) memset(portrait_mask[1], 0, sizeof portrait_mask[1]);
    }
    H.dialog = 1; H.line = 0; H.line_t0 = H.now; E.has_dest = 0;
}
int hud_in_dialog(void) { return H.dialog; }
/* Plusieurs répliques de who (chaînes gardées par l'appelant), portrait de droite : right (la police d'une effraction) */
void hud_lines(const char *who, const char *const *lines, int n, const char *right) {
    if (n < 1) return;
    hud_say(who, lines[0], right);
    snprintf(D.right, sizeof D.right, "%s", who);   /* who parle à droite, avec le portrait de right */
    for (int i = 1; i < n && i < DLG_MAX; i++) { snprintf(D.who[i], sizeof D.who[i], "%s", who); D.text[i] = lines[i]; }
    D.n = n < DLG_MAX ? n : DLG_MAX;
}
void hud_toast(const char *s) { toast(s); }

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
    float x = in->touch_x, y = in->touch_y;
    /* Pinch (sur la world map) : le zoom suit l'écart des deux doigts ; le geste ne vaut ni toucher ni marche, et dure
       jusqu'à ce que tous les doigts soient levés */
    /* En véhicule, deux doigts conduisent (drive_touch) : pas de pincement */
    if (in->touch && in->touch2 && !H.menu && !H.dialog && H.sub < 0 && !BT.on && E.ride < 0) {
        float d = hypotf(in->touch2_x - x, in->touch2_y - y);
        if (!H.pinch) { H.pinch = 1; H.pinch_d0 = d > 20 ? d : 20; H.pinch_z0 = E.zoom; H.down = 0; H.walking = 0; H.rim_on = 0; E.has_dest = 0; }
        else eng_set_zoom(H.pinch_z0 * d / H.pinch_d0);
        return;
    }
    if (H.pinch) { if (!in->touch) H.pinch = 0; return; }
    if (in->touch && !H.down) {
        float d = hypotf(x - CX, y - CY);
        int on_sera = E.scr_sera.vis && hypotf(x - E.scr_sera.cx, y - E.scr_sera.cy) < 46;
        int on_btn = button_at(x, y) >= 0;
        H.down = 1; H.x0 = H.cx = x; H.y0 = H.cy = y; H.moved = 0; H.t0 = H.now; H.acc = 0; H.walking = 0;
        E.dest_bloque = 0;   /* nouveau toucher : Sera peut retenter le point où elle s'est arrêtée */
        H.rim = d > 186 && !H.dialog && H.sub != SUB_MAP && !BT.on;   /* en combat : seulement des touchers */
#if ENG_DYN
        H.rim = y > UH - BANDE_H && !H.dialog && !H.menu && H.sub < 0 && !BT.on && E.ride < 0;   /* bande du bas */
#endif
        H.list = !H.rim && H.sub >= 0 && H.sub != SUB_MAP && !BT.on; H.vy = 0; H.move_t = H.now; H.fling = 0;
        H.ang = atan2f(x - CX, -(y - CY));
        H.hold = !H.rim && !H.dialog && !H.menu && !on_sera && !on_btn && !BT.on;
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
            if (H.menu && H.sub < 0) {
                H.acc += da;
                while (H.acc > 0.5f) { H.sel = (H.sel + NMENU - 1) % NMENU; H.acc -= 2 * PI_F / NMENU; }
                while (H.acc < -0.5f) { H.sel = (H.sel + 1) % NMENU; H.acc += 2 * PI_F / NMENU; }
            } else if (H.sub >= 0) {
                int n = sub_rows();
                H.acc += da;
                while (H.acc > 0.35f) { if (H.sub_sel < n - 1) H.sub_sel++; H.acc -= 0.35f; }
                while (H.acc < -0.35f) { if (H.sub_sel > 0) H.sub_sel--; H.acc += 0.35f; }
            } else if (E.ride < 0) E.yaw -= da;   /* en véhicule, la caméra reste derrière lui */
        }
    } else if (!in->touch && H.down) {
        H.down = 0; H.rim_on = 0;
        if (H.vol_on) { if (H.moved <= 14) H.vol_on = 0; return; }   /* un toucher referme le cadran */
        if (H.list && H.moved > 14) { if (H.now - H.move_t < 0.1f && fabsf(H.vy) > 150) H.fling = H.vy; return; }
        if (H.walking) return;
        if (H.rim && H.moved > 8) return;
        if (H.moved > 14) return;
        tap(H.cx, H.cy);
    }
}

/* Combat au clavier : flèches entre les boutons de l'éventail, OK pour valider (ou viser l'ennemi, continuer), retour */
static void battle_keys(uint32_t pressed) {
    const int ph = BT.phase, menu = ph == BP_CMD || ph == BP_TECH || ph == BP_MAGIE || ph == BP_ITEM || ph == BP_TARGET;
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
        if (ph == BP_TARGET) { if (E.scr_enemy.vis) battle_tap(E.scr_enemy.cx, E.scr_enemy.cy); }
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
            d->role = px[i] < UW / 2 ? 0 : 1;
            doigt_t *o = d == &DG[0] ? &DG[1] : &DG[0];
            if (o->on && o->role == d->role) o->role = 3;   /* deux doigts du même côté : le dernier posé conduit */
        }
    }
    E.drive_th = E.drive_st = 0; E.drive_touch = 0; E.drive_alt = 0;
    for (int k = 0; k < 2; k++) {
        const doigt_t *d = &DG[k];
        if (!d->on) continue;
        if (d->role == 0) {   /* gaz : zone morte de 12 %, puis en proportion ; vers le bas, frein puis marche arrière */
            const float g = fmaxf(-1, fminf(1, (d->y0 - d->y) / DRV_GAZ)), a = fabsf(g) < 0.12f ? 0 : (fabsf(g) - 0.12f) / 0.88f;
            E.drive_th = g < 0 ? -a : a; E.drive_touch = 1;
        } else if (d->role == 1) {   /* volant : zone morte de 10 %, puis une courbe douce (moitié droite, moitié carré) */
            const float v = fmaxf(-1, fminf(1, (d->x - d->x0) / DRV_VOL)), a = fabsf(v) < 0.1f ? 0 : (fabsf(v) - 0.1f) / 0.9f;
            E.drive_st = (v < 0 ? -1 : 1) * (0.45f * a + 0.55f * a * a); E.drive_touch = 1;
        } else if (d->role == 2 && alt_visible()) {
            if (hypotf(d->x - ALT_X, d->y - ALT_UP_Y) < ALT_R + 8) E.drive_alt = 1;
            else if (hypotf(d->x - ALT_X, d->y - ALT_DN_Y) < ALT_R + 8) E.drive_alt = -1;
        }
    }
    /* Geste du toucher simple (pointer) tenu tant qu'un doigt est posé : au retour à pied, son lever n'est pas un toucher */
    H.down = in->touch; H.walking = 1; H.hold = H.rim = H.list = 0; H.pinch = 0;
    if (in->touch) { H.cx = in->touch_x; H.cy = in->touch_y; }
}

void hud_update(float dt, const eng_input_t *in) {
    H.now += dt;
    if (accueil_on()) { accueil_update(dt, in); H.prev_keys = in->keys; return; }   /* écran titre, introduction, héroïne */
    { const int ps = member_sheet(0); if (ps != H.psheet) { H.psheet = ps; make_portrait(0, ps, 0); } }
    if (conduite_doigts()) drive_touch(in);
    else { DG[0].on = DG[1].on = 0; E.drive_touch = 0; E.drive_alt = 0; pointer(in); }
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
        int len = 0; for (const char *s = D.text[H.line]; *s; s++) if ((*s & 0xC0) != 0x80) len++;
        const int b = (int)((H.now - H.line_t0) / 0.084f);
        if (b >= 0 && b * 3 < len && b != H.bip_k) { H.bip_k = b; son_jouer(SON_BIP, 0.25f, 0.9f + 0.1f * (b % 3)); }
    } else H.bip_k = -1;
    const int sig = H.menu * 4096 + (H.sub + 1) * 512 + H.sel * 64 + H.sub_sel;
    if (sig != H.menu_sig && (H.menu || H.sub >= 0)) son_jouer(SON_MENU, 0.35f, 1);
    H.menu_sig = sig;
    uint32_t pressed = in->keys & ~H.prev_keys;
    H.prev_keys = in->keys;
    /* Zoom changé (pinch, menu, molette de la page web) : niveau affiché un instant */
    if (fabsf(E.zoom - H.shown_zoom) > 0.004f) {
        char t[40];
        H.shown_zoom = E.zoom;
        snprintf(t, sizeof t, "ZOOM %d %%%s", (int)lroundf(E.zoom * 100),
                 E.zoom <= E.w.zoom_min + 0.001f ? " · MINI" : E.zoom >= ZOOM_MAX - 0.001f ? " · MAXI" : "");
        toast(t);
    }
    if (BT.on) { battle_keys(pressed); E.ui_block = 1; return; }
    if (RM.on) {   /* menu de course : haut / bas une ligne, gauche / droite sa valeur, OK départ, retour annuler */
        if (pressed & ENG_KEY_UP) RM.row = clampi(RM.row - 1, 0, RM_LIGNES[RM.page] - 1);
        if (pressed & ENG_KEY_DOWN) RM.row = clampi(RM.row + 1, 0, RM_LIGNES[RM.page] - 1);
        if (pressed & ENG_KEY_LEFT) course_menu_change(RM.row, -1);
        if (pressed & ENG_KEY_RIGHT) course_menu_change(RM.row, 1);
        if (pressed & ENG_KEY_OK) course_menu_go();
        else if (pressed & ENG_KEY_BACK) course_menu_cancel();
        E.ui_block = RM.on;
        return;
    }
    if (CR.on && CR.pause) {   /* pause de course : haut / bas un choix, OK le valide, retour ou menu reprend */
        if (pressed & (ENG_KEY_UP | ENG_KEY_DOWN)) CR.pause_sel = !CR.pause_sel;
        if (pressed & ENG_KEY_OK) { if (CR.pause_sel) course_abandon(); else course_pause(0); }
        else if (pressed & (ENG_KEY_BACK | ENG_KEY_MENU)) course_pause(0);
        E.ui_block = 1;
        return;
    }
    if (H.sub != SUB_SYSTEM) H.vol_on = 0;   /* menu refermé ou autre écran */
    if (H.vol_on) {   /* cadran du son : droite / haut plus fort, gauche / bas moins fort, OK ou retour le referme */
        if (pressed & (ENG_KEY_RIGHT | ENG_KEY_UP)) vol_pas(1);
        if (pressed & (ENG_KEY_LEFT | ENG_KEY_DOWN)) vol_pas(-1);
        if (pressed & (ENG_KEY_OK | ENG_KEY_BACK)) H.vol_on = 0;
        pressed &= ~(uint32_t)(ENG_KEY_RIGHT | ENG_KEY_UP | ENG_KEY_LEFT | ENG_KEY_DOWN | ENG_KEY_OK | ENG_KEY_BACK);
    }
    if ((pressed & ENG_KEY_MENU) && CR.on) course_pause(1);
    if ((pressed & ENG_KEY_MENU) && !CR.on) { if (!H.dialog) { H.menu = !H.menu; H.sub = -1; E.has_dest = 0; } }
    if ((MISE.fuite_on || police_bloque()) && (H.menu || H.sub >= 0)) { H.menu = 0; H.sub = -1; }   /* pas de menu pendant une fuite ni une arrestation */
    if (pressed & ENG_KEY_BACK) {
        if (H.dialog) H.dialog = 0;
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
        if (H.dialog) advance_dialog();
        else if (H.sub >= 0) activate_row(H.sub_sel);
        else if (H.menu) open_sub(H.sel);
        else if (CR.on) { if (CR.phase == CP_RESULTATS) course_end(); else course_tir(); }
        else if (E.porte >= 0 && E.ride < 0 && (E.talk == -2 || E.talk <= TALK_TRAF(0))) int_interagir();   /* la porte avant un véhicule du trafic */
        else if (near_npc()) do_action(A_TALK, 0);
    }
    if (H.sub >= 0) {
        int n = sub_rows();
        if ((pressed & ENG_KEY_DOWN) && H.sub_sel < n - 1) H.sub_sel++;
        if ((pressed & ENG_KEY_UP) && H.sub_sel > 0) H.sub_sel--;
    } else if (H.menu) {
        if (pressed & (ENG_KEY_RIGHT | ENG_KEY_DOWN)) H.sel = (H.sel + 1) % NMENU;
        if (pressed & (ENG_KEY_LEFT | ENG_KEY_UP)) H.sel = (H.sel + NMENU - 1) % NMENU;
    }
    E.ui_block = H.menu || H.dialog || RM.on || CHX.on;
}

void hud_choice(char opts[][48], int n) {
    CHX.n = n < 1 ? 1 : n > 4 ? 4 : n; CHX.sel = 0; CHX.on = 1; CHX.porte = 0; CHX.commerce = 0; CHX.titre[0] = 0; E.has_dest = 0;
    for (int k = 0; k < CHX.n; k++) snprintf(CHX.opts[k], sizeof CHX.opts[k], "%s", opts[k]);
}
void hud_choice_porte(char opts[][48], int n, const char *titre) {
    hud_choice(opts, n);
    CHX.porte = 1; snprintf(CHX.titre, sizeof CHX.titre, "%s", titre);
}
void hud_choice_titre(char opts[][48], int n, const char *titre) {
    hud_choice(opts, n);
    snprintf(CHX.titre, sizeof CHX.titre, "%s", titre);
}
int hud_in_choice(void) { return CHX.on; }

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
    static float ks[120], kc[120];
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
    snprintf(t, sizeof t, "TOUR %d/%d", tour, CR.tours);
    ui_text(ui_font(FONT_MONO, 18), CX, 66, UI_CENTER, C1, 255, t);
    snprintf(t, sizeof t, "%d%s / %d", place, place == 1 ? "er" : "e", CR.n);
    ui_text(ui_font(FONT_DISP, 30), CX + 1, 101, UI_CENTER, C1, 90, t);
    ui_text(ui_font(FONT_DISP, 30), CX, 100, UI_CENTER, ui_rgb(0xE8F6FF), 255, t);
    chrono(j->fin >= 0 ? j->fin : CR.t, t, sizeof t);
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
    const float mx = 112 - (PISTE.x0 + PISTE.x1) / 2 * sc, mz = 316 - (PISTE.z0 + PISTE.z1) / 2 * sc;
    const int step = PISTE.n > 48 ? PISTE.n / 48 : 1;
    for (int i = 0; i < PISTE.n; i += step) {
        const int e = i + step < PISTE.n ? i + step : 0;
        ui_line(mx + PISTE.X[i] * sc, mz + PISTE.Z[i] * sc, mx + PISTE.X[e] * sc, mz + PISTE.Z[e] * sc, 3, C1, 140);
    }
    for (int i = 0; i < CR.n; i++) if (!CR.r[i].joueur) ui_circle(mx + CR.r[i].x * sc, mz + CR.r[i].z * sc, 3, ui_rgb(0xE8F6FF), 255);
    ui_circle(mx + j->x * sc, mz + j->z * sc, 4.5f, C2, 255);
}

/* Pause de course (drawCoursePause de l'émulateur) : seuls ses deux boutons répondent */
static void draw_course_pause(void) {
    float q[12];
    nbtn = 0;
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(0x05040A), 209);
    ui_text(ui_font(FONT_DISP, 30), CX + 1, 161, UI_CENTER, C1, 90, "PAUSE");
    ui_text(ui_font(FONT_DISP, 30), CX, 160, UI_CENTER, ui_rgb(0xE8F6FF), 255, "PAUSE");
    for (int i = 0; i < 2; i++) {
        const float y = 236.0f + i * 76;
        const uint16_t col = i ? ui_rgb(0xFF3344) : C1;
        ui_hex_path(CX, y, 190, 58, q);
        ui_poly(q, 6, ui_rgb(0x060E1A), 235);
        if (i == CR.pause_sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 2, col, 255);
        ui_text(ui_font(FONT_DISP, 22), CX, y + 8, UI_CENTER, col, 255, i ? "ABANDONNER" : "REPRENDRE");
        button(CX, y, 60, i ? A_ABANDON : A_REPRENDRE, 0);
    }
    ui_text(ui_font(FONT_MONO, 18), CX, 372, UI_CENTER, ui_rgb(0x7F93A8), 255, "Abandon : la course ne compte pas");
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
    const uint16_t GRIS = ui_rgb(0x7F93A8), BLANC = ui_rgb(0xE8F6FF);
    char t[48];
    float q[12];
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(0x05040A), 224);
    ui_text(ui_font(FONT_DISP, 26), CX + 1, 65, UI_CENTER, C1, 90, "COURSE");
    ui_text(ui_font(FONT_DISP, 26), CX, 64, UI_CENTER, BLANC, 255, "COURSE");
    for (int i = 0; i < 3; i++) {   /* onglets (ligne 0) */
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
        const int p = rm_perso(), dir = (int)((H.now - RM.t0) / 0.35f) & 7;
        if (rm_apercu(0, p, dir, RM_AP)) {
            const float sc = fminf(fminf(150.0f / rm_w[0], 100.0f / rm_h[0]), 5);
            ui_image(rm_px, rm_mk, rm_w[0], rm_h[0], CX - rm_w[0] * sc / 2, 184 - rm_h[0] * sc / 2, sc, CX, CY, 232);
        }
        rm_fleches(1, 184, 162, 48, 44, 1);
        ui_text(ui_fit(FONT_DISP, E.w.persos[p].nom, 260, 24, 16), CX, 262, UI_CENTER, RM.row == 1 ? C1 : BLANC, 255, E.w.persos[p].nom);
        snprintf(t, sizeof t, "COULEUR %d/%d", RM.coul + 1, RM.modn[RM.mod]);
        ui_text(ui_font(FONT_MONO, 18), CX, 296, UI_CENTER, RM.row == 2 ? C1 : GRIS, 255, t);
        rm_fleches(2, 290, 112, 38, 34, RM.modn[RM.mod] > 1);
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
        const float sc = fminf(180 / fmaxf(1, x1 - x0), 84 / fmaxf(1, z1 - z0)), mx = CX - (x0 + x1) / 2 * sc, mz = 168 - (z0 + z1) / 2 * sc;
        for (int pass = 0; pass < 2; pass++) for (int k = 0; k < (RM.ntr ? RM.ntr : 1); k++) {
            const int sel = !RM.ntr || k == RM.tr;
            if (sel != pass) continue;
            const piste_t *P = RM.ntr ? course_piste(RM.trace[k]) : &PISTE;
            const int step = P->n > 64 ? P->n / 64 : 1;
            for (int i = 0; i < P->n; i += step) {
                const int e = i + step < P->n ? i + step : 0;
                ui_line(mx + P->X[i] * sc, mz + P->Z[i] * sc, mx + P->X[e] * sc, mz + P->Z[e] * sc, sel ? 3 : 2, sel ? C1 : GRIS, sel ? 255 : 77);
            }
        }
        ui_circle(mx + Pc->X[Pc->dep] * sc, mz + Pc->Z[Pc->dep] * sc, 5, C2, 255);
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
    glow_outline(q, 6, 2, C2);
    ui_text(ui_font(FONT_DISP, 22), 286, 422, UI_CENTER, C2, 255, "DÉPART");
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

static void draw_world_hud(void) {
    draw_course_fx();   /* hors course : éclats, feuilles et poussière des décors cassés */
    draw_gyrophares();
#if ENG_DYN
    portrait_world();
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
        char t[32];
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
            else snprintf(t, sizeof t, "%.12s // %02u:%02u", E.w.carte ? E.w.cartes[E.w.carte].nom : "SEC-06", hr, mn);
        }
        else if (E.in.on && E.in.etage) snprintf(t, sizeof t, "ÉTAGE %d", E.in.etage); else if (E.in.on) snprintf(t, sizeof t, "INTÉRIEUR"); else if (E.w.carte) snprintf(t, sizeof t, "%.12s // %.1f %.1f", E.w.cartes[E.w.carte].nom, E.sera.x, E.sera.z); else snprintf(t, sizeof t, "SEC-06 // %.1f %.1f", E.sera.x, E.sera.z);
        ui_text_arc(ui_font(FONT_MONO, 20), CX + 1, CY + 1, 214, PI_F / 2 - 0.12f, 1, ui_rgb(0x05080E), 170, t);
        ui_text_arc(ui_font(FONT_MONO, 20), CX, CY, 214, PI_F / 2 - 0.12f, 1, ui_rgb(0x7F93A8), 255, t);
    }
#if ENG_DYN
    }
#endif
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
       quand la manette est connectée) et boutons d'altitude (vaisseau) */
    if (conduite_doigts()) {
        for (int r = 0; r < 2 && !E.manette; r++) {
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
    cadre(1);
    if (RM.on) draw_course_menu();
    else if (CR.on) { draw_course(); if (CR.pause) draw_course_pause(); }
    cadre(0);
    if (RM.on || CR.on) {}
    else if (near_npc() && E.scr_npc.vis && !H.dialog && !H.menu && !(E.ride >= 0 && (fabsf(E.ride_v) > 0.3f || E.ride_phase))) {
        float x = E.scr_npc.cx, y = E.scr_npc.top - 50;
        ui_line(x, y + 36, x, E.scr_npc.top, 1, C1, 153);
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
    case 3: snprintf(t, n, "Équipe de %d", team_count()); break;
    case 4: snprintf(t, n, "Secteur 6"); break;
    case 6: { int a = 0; for (int q = 0; q < script_quest_count(); q++) a += E.quests[q] >= 1 && E.quests[q] < 255; snprintf(t, n, "%d en cours", a); break; }
    case 7: if (E.guide >= 0) snprintf(t, n, "%s", E.w.guides[E.guide].nom); else snprintf(t, n, "Ville ou bâtiment"); break;
    default: snprintf(t, n, "Teinte, sauvegarde"); break;
    }
}

static void backdrop(void) {
#if ENG_DYN
    ui_rect(-ui_tx(), -ui_ty(), UW, UH, ui_rgb(INK), 222); return;   /* toute l'image (les sous-menus sont décalés en x) */
#endif
    ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, ui_rgb(INK), 222);
    ui_circle(CX, CY, 150, ui_rgb(0x0D1428), 110);
    dashed_ring(226, 1.5f, 10.33f, 5, C1, 102);
    ui_ring(CX, CY, 215, 1, C1, 46);
}

static void draw_menu(void) {
#if ENG_DYN
    portrait_menu(); return;
#endif
    backdrop();
    dashed_ring(160, 4, 6, 1, C1, 56);
    ui_arc(CX, CY, 226, -PI_F / 2 - 0.13f, -PI_F / 2 + 0.13f, 5, C2, 255);
    for (int k = 0; k < NMENU; k++) {
        float a = (k - H.sel) / (float)NMENU * 2 * PI_F, x = CX + sinf(a) * 160, y = CY - cosf(a) * 160;
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
    snprintf(t, sizeof t, "0%d / 0%d", H.sel + 1, NMENU);
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
static void list_row_icon(int i, int slot, const char *label, const char *value, uint16_t label_col, float bar, int icon) {
    float y = ROW_Y0 + slot * ROW_STEP, hw = row_half(y + ROW_H), x0 = CX - hw, w0 = 2 * hw;
    float ic = icon >= 0 ? 36 : 0, x = x0 + ic, w = w0 - ic;
    int sel = i == H.sub_sel;
    uint16_t col = sel ? C2 : C1;
    float q[12]; ui_chamfer_path(x0, y, w0, ROW_H, 10, q);
    ui_poly(q, 6, ui_mix(ui_rgb(INK), col, sel ? 0.24f : 0.10f), 238);
    if (sel) glow_outline(q, 6, 2, col); else ui_outline(q, 6, 1, col, 128);
    const float yi = y + (ROW_H - 32) / 2, ym = y + ROW_H / 2, cr = PORTRAIT ? 4000 : 233;   /* icône, milieu de la ligne, découpe */
    objet_icone(icon, x0 + 12, yi, cr);
    float vw = value ? ui_text_width(ui_font(FONT_MONO, 20), value) + 16 : 0;
    ui_text(ui_fit(FONT_DISP, label, w - vw - 32, 22, 16), x + 16, ym + (bar >= 0 ? 4 : 8), UI_LEFT, label_col, 255, label);
    if (value) ui_text(ui_font(FONT_MONO, 20), x + w - 14, ym + 7, UI_RIGHT, col, 255, value);
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
static void draw_statut(int n) {
    char t[96], v[48];
    const int mi = team_at(H.sub_sel);
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
    ui_text(ui_fit(FONT_DISP, m->name, R - L - 90, 30, 20), cx, y0 + 112, UI_CENTER, ui_rgb(m->color), 255, m->name);
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
    snprintf(v, sizeof v, "%d / %d", m->hp, m->max);
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
    /* Ce qu'il porte : arme, armure, accessoire (sans jobs : son équipement écrit, puis sa tenue) */
    for (int k = 0; k < 3; k++) {
        const int it = E.eq[mi][k];
        const char *nm = E.w.neqtypes > 0 ? (it != EQ_AUCUN ? ITEMS[it].name : "—") : k < 2 ? m->eq[k] : tenue_label(mi);
        snprintf(t, sizeof t, "%s · %s", E.w.neqtypes > 0 || k < 2 ? SLOT_NAME[k] : "TENUE", nm ? nm : "—");
        ui_text(ui_fit(FONT_MONO, t, PORTRAIT ? R - L : 280 - k * 30, 16, 12), cx, y0 + 354 + k * 19, UI_CENTER, k == 0 ? C1 : ui_rgb(DIM), 255, t);
    }
    sub_footer(NULL);
}

static void draw_sub(void) {
    char t[96], v[32];
    int n = sub_rows();
    if (H.sub_sel >= n) H.sub_sel = n ? n - 1 : 0;
    if (H.sub_sel < H.sub_scroll) H.sub_scroll = H.sub_sel;
    if (H.sub_sel >= H.sub_scroll + ROWS) H.sub_scroll = H.sub_sel - ROWS + 1;
    switch (H.sub) {
    case SUB_SHOP: {
        snprintf(t, sizeof t, "%s · %d crédits", SHOP.nom, E.credits);
#if ENG_DYN
        entete("obj", SHOP.mode ? "VENDRE" : "ACHETER", t);
#else
        backdrop();
        ui_icon("obj", CX, 62, 34, C2, 255);
        ui_text(ui_font(FONT_DISP, 30), CX, 112, UI_CENTER, ui_rgb(WHITE), 255, SHOP.mode ? "VENDRE" : "ACHETER");
        const int msg = H.now - H.toast_t < 1.8f && H.toast[0];
        ui_text(ui_fit(FONT_MONO, msg ? H.toast : t, 300, 20, 16), CX, 138, UI_CENTER, msg ? C2 : ui_rgb(DIM), 255, msg ? H.toast : t);
#endif
        for (int s2 = 0; s2 < ROWS && H.sub_scroll + s2 < n; s2++) {
            const int k = shop_item(H.sub_scroll + s2);
            char lb[64];
            if (ITEMS[k].count) snprintf(lb, sizeof lb, "%s ×%d", ITEMS[k].name, ITEMS[k].count); else snprintf(lb, sizeof lb, "%s", ITEMS[k].name);
            snprintf(v, sizeof v, "%s%d CR", SHOP.mode ? "+" : "", shop_prix(k));
            list_row_icon(H.sub_scroll + s2, s2, lb, v, ui_rgb(WHITE), -1, item_icon(k));
        }
        sub_footer(n ? ITEMS[shop_item(H.sub_sel)].desc : SHOP.mode ? "Rien à vendre ici" : "Plus rien à vendre");
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
        sub_footer(n ? ITEMS[owned_item(H.sub_sel)].desc : "Le sac est vide");
        break;
    case SUB_TECH: {
        sub_header(SUB_TECH, "Hors combat : seuls les soins");
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) {
            int i = H.sub_scroll + s, mi, ti; tech_at(i, &mi, &ti);
            const sort_t *sp = ti < 0 ? &E.w.sorts[-1 - ti] : NULL;
            snprintf(t, sizeof t, "%s · %s", PARTY[mi].name, sp ? sp->nom : PARTY[mi].tech[ti].name);
            snprintf(v, sizeof v, "%d PM", sp ? sp->pm : PARTY[mi].tech[ti].mp);
            list_row(i, s, t, v, sp && fx_couleur(sp->fx) ? ui_rgb(fx_couleur(sp->fx)) : ui_rgb(PARTY[mi].color), -1);
        }
        int mi, ti; tech_at(H.sub_sel, &mi, &ti);
        snprintf(t, sizeof t, "%s : %d / %d PM · MAG %d", PARTY[mi].name, PARTY[mi].mp, PARTY[mi].maxmp, PARTY[mi].mag);
        sub_footer(t);
        break;
    }
    case SUB_EQUIP: {
        const int jobs = E.w.neqtypes > 0;
        sub_header(SUB_EQUIP, jobs ? "Équipement · toucher pour changer" : "Tenue · toucher pour changer");
        /* Une ligne : membre et emplacement à gauche (le membre seul sans jobs), ce qu'il porte à droite */
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) {
            int r = H.sub_scroll + s, ri = 0, rs = ER_TENUE;
            equip_rows(r, &ri, &rs);
            const member_t *m = &PARTY[ri];
            const int it = rs >= 0 && rs < 3 ? E.eq[ri][rs] : EQ_AUCUN;
            const char *tx = rs == ER_JOB ? (E.job[ri] < E.w.njobs ? E.w.jobs[E.job[ri]].nom : "—") : rs == ER_TENUE ? tenue_label(ri) : it != EQ_AUCUN ? ITEMS[it].name : "—";
            if (jobs) snprintf(t, sizeof t, "%s · %s", m->name, rs == ER_JOB ? "JOB" : rs == ER_TENUE ? "TENUE" : SLOT_NAME[rs]);
            else snprintf(t, sizeof t, "%s", m->name);
            list_row_icon(r, s, t, NULL, ui_rgb(m->color), -1, rs >= 0 && rs < 3 ? (it != EQ_AUCUN ? item_icon(it) : 0) : -1);
            float y = ROW_Y0 + s * ROW_STEP, hw = row_half(y + ROW_H);
            ui_text(ui_fit(FONT_MONO, tx, hw - 10, 18, 12), CX + hw - 14, y + ROW_H / 2 + 7, UI_RIGHT, r == H.sub_sel ? C2 : C1, 255, tx);
        }
        int mi = 0, sl = ER_TENUE;
        equip_rows(H.sub_sel, &mi, &sl);
        const int tt = tenue_of(mi, 0), eq = tenue_mine(mi, E.tenue_eq[mi]) ? E.tenue_eq[mi] : -1;
        char st[64];
        snprintf(st, sizeof st, "ATQ %d · MAG %d · DÉF %d · VIT %d", PARTY[mi].atk, PARTY[mi].mag, PARTY[mi].def, PARTY[mi].spd);
        const eqitem_t *ri = sl >= 0 && sl < 3 ? eq_item(E.eq[mi][sl]) : NULL;   /* objet porté qui donne des sorts : leurs noms */
        char so[64] = "";   /* plus de 32 caractères : le premier et le nombre des autres */
        int nso = 0, k1 = -1, nc = 0;
        for (int k = 0; ri && k < E.w.nsorts; k++)
            if (ri->sorts >> k & 1) { if (k1 < 0) k1 = k; const size_t u = strlen(so); snprintf(so + u, sizeof so - u, "%s%s", nso++ ? ", " : "Sorts : ", E.w.sorts[k].nom); }
        for (const char *c = so; *c; c++) nc += (*c & 0xC0) != 0x80;
        if (nc > 32) snprintf(so, sizeof so, "Sorts : %s +%d", E.w.sorts[k1].nom, nso - 1);
        if (sl == ER_TENUE && tt >= 0 && tt != eq) snprintf(t, sizeof t, "Imposée %s", TENUE_RAISON[E.w.tenues[tt].ctx & 3]);
        else if (sl == ER_TENUE && !jobs) snprintf(t, sizeof t, "%s · %s", PARTY[mi].eq[0], PARTY[mi].eq[1]);
        else if (so[0]) snprintf(t, sizeof t, "%s", so);
        else if (sl >= 0 && sl < 3 && E.eq[mi][sl] != EQ_AUCUN) snprintf(t, sizeof t, "Affinité %s · %s", AFF_NAME[eq_aff(mi, E.eq[mi][sl])], st);
        else snprintf(t, sizeof t, "%s", st);
        sub_footer(t);
        break;
    }
    case SUB_STATUS: draw_statut(n); break;
    case SUB_MAP: {
        sub_header(SUB_MAP, "Les Friches");
        int nn = E.w.n;
        const float my = PORTRAIT ? 160 + (UW - 60) / 2 : 268;   /* centre de la carte */
        float r = PORTRAIT ? (UW - 60) / 2 : 118, s = 2 * r / nn, ox = CX - r, oy = my - r;
        ui_circle(CX, my, r + 6, ui_rgb(INK), 255);
        if (E.w.map_rgb && E.w.map_n > 0) ui_image_rgb(E.w.map_rgb, E.w.map_n, E.w.map_n, ox, oy, 2 * r / E.w.map_n, CX, my, r);
        ui_ring(CX, my, r + 3, 2, C1, 200);
        /* Objectif, puis Sera (flèche dans sa direction de marche) */
        float obx, obz;   /* objectif du scénario : anneau et point */
        if (objective_pos(&obx, &obz)) { ui_ring(ox + obx * s, oy + obz * s, 7, 2, C1, 255); ui_circle(ox + obx * s, oy + obz * s, 2.5f, C1, 255); }
        if (E.guide >= 0 && !E.w.carte) {   /* lieu du guidage (carte 0) : anneau et point verts */
            const guide_t *g = &E.w.guides[E.guide];
            ui_ring(ox + g->x * s, oy + g->z * s, 7, 2, ui_rgb(GUIDE_RGB), 255); ui_circle(ox + g->x * s, oy + g->z * s, 2.5f, ui_rgb(GUIDE_RGB), 255);
        }
        float px = ox + E.sera.x * s, py = oy + E.sera.z * s, ds = sinf(E.sera.dir), dc = cosf(E.sera.dir);
        float tri[6] = { px + ds * 10, py + dc * 10, px - dc * 6 - ds * 5, py + ds * 6 - dc * 5, px + dc * 6 - ds * 5, py - ds * 6 - dc * 5 };
        ui_poly(tri, 3, C2, 255);
        ui_ring(px, py, 12, 2, C2, 120);
        if (PORTRAIT) break;   /* retour : l'en-tête (‹) */
        hex_button(CX, 428, 72, 54, ui_rgb(DIM), 0, "back", 26);
        button(CX, 428, 34, A_BACK, 0);
        break;
    }
    case SUB_SYSTEM: {
        if (H.vol_on) { vol_build(); break; }
        sub_header(SUB_SYSTEM, PORTRAIT ? "Glisser : défiler" : "Glisser sur le bord : défiler");
        const char *names[16] = { "SAUVEGARDER", "CHARGER", "TEINTE", "BROUILLARD", "TRAMAGE", "LUMINOSITÉ", "ZOOM", "MÉTÉO", "SON", "CYCLE", "TÉLÉPORTER", "GALERIE",
                                  "RENCONTRES", "DIRECTION", "CODE", "ÉVEIL" };
        if (vol_permanent()) names[10] = "SAUT";   /* système solaire : le saut vers les mondes visités */
        for (int s = 0; s < ROWS && H.sub_scroll + s < n; s++) {
            int i = H.sub_scroll + s;
            const char *val = NULL;
            switch (i) {
            case 2: val = THEMES[E.theme].name; break;
            case 3: val = E.fog_on ? "OUI" : "NON"; break;
            case 4: val = E.dither_on ? "OUI" : "NON"; break;
            case 5: snprintf(v, sizeof v, "%d %%", 20 + H.bright * 20); val = v; break;
            case 6: snprintf(v, sizeof v, "%d %%", (int)lroundf(E.zoom * 100)); val = v; break;
            case 7: { static const char *W[3] = { "AUTO", "CLAIR", "PLUIE" }; val = W[E.weather % 3]; break; }
            case 15: if (E.essai) val = E.eveil_essai ? "OUI" : "NON"; break;
            case 13: { static const char *DI[5] = { "TRÈS DOUCE", "DOUCE", "MOYENNE", "VIVE", "TRÈS VIVE" }; val = DI[E.volant % 5]; break; }
            case 12: { static const char *RN[5] = { "NORMALES", "RARES", "TRÈS RARES", "AUCUNE", "FRÉQUENTES" }; val = RN[E.renc_mode % 5]; break; }
            case 9: { static const char *CYC[4] = { "AUTO", "JOUR", "NUIT", "RÉEL" }; val = CYC[E.cycle % 4]; break; }
            case 8: if (eng_volume() < 0.05f) val = "COUPÉ"; else { snprintf(v, sizeof v, "%d %%", (int)lroundf(eng_volume() * 100)); val = v; } break;
            }
            const eng_reglage_t *pr = i >= plateforme_ligne() ? &E.cfg.regls_jeu[i - plateforme_ligne()] : NULL;
            if (pr && pr->valeur) val = pr->valeur();
            list_row(i, s, pr ? pr->nom : i == accueil_ligne() ? (E.cfg.montre ? "MONTRE" : "ACCUEIL") : names[i], val, ui_rgb(WHITE), -1);
        }
        if (H.seq) snprintf(t, sizeof t, "Dernière sauvegarde : n° %d", H.seq);
        else snprintf(t, sizeof t, "Carte SD · deux emplacements alternés");
        sub_footer(t);
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
            if (done) snprintf(v, sizeof v, "FINIE"); else snprintf(v, sizeof v, "%d/%d", st, script_quest_steps(q));
            list_row(i, s, nm, v, done ? ui_rgb(0x5A6070) : script_quest_type(q) == 0 ? C2 : ui_rgb(WHITE), -1);
        }
        if (!n) sub_footer("Aucune quête pour l'instant");
        else if (E.quests[rows[H.sub_sel]] == 255) sub_footer("Quête terminée");
        else { const char *o = script_quest_text(rows[H.sub_sel], E.quests[rows[H.sub_sel]]); sub_footer(o[0] ? o : "Pas d'objectif"); }
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
            ui_rect(-ui_tx(), -ui_ty(), UW, UH, ui_rgb(0), 255);
            if (ix) ui_image_pal(ix, pal, iw, ih, (UW - iw * s) / 2, iy, s, UW / 2, UH / 2, PORTRAIT ? UW + UH : 233);
            snprintf(t, sizeof t, "%d / %d · toucher : fermer", H.sub_sel + 1, n);
            ui_text(ui_font(FONT_MONO, 18), UW / 2, PORTRAIT ? UH - 40 : 444, UI_CENTER, ui_rgb(DIM), 200, t);
            break;
        }
        if (H.galcat < 0) snprintf(t, sizeof t, "GALERIE");
        else snprintf(t, sizeof t, "GALERIE · %s", GAL_CAT[H.galcat]);
        sub_header(SUB_SYSTEM, t);
        if (ix) { ui_image_pal(ix, pal, iw, ih, (UW - iw * s) / 2, iy, s, UW / 2, UH / 2, PORTRAIT ? UW + UH : 233); ui_rect(-ui_tx(), -ui_ty(), UW, UH, ui_rgb(INK), 150); }
        static int tmp[GAL_MAX], tp[GAL_MAX];
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
            list_row(i, sl, t, v, ui_rgb(WHITE), -1);
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
            if (r < 0) {
                int k = 0; for (int j = 0; j < nt; j++) k += p[j * 36 + 24] == -1 - r;
                snprintf(v, sizeof v, "%d", k);
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
        sub_footer(!n ? (vol ? "Aucun monde visité" : "Aucun lieu") : vol ? "Toucher : sauter jusque-là" : H.tcat < 0 ? "Toucher : voir la liste" : "Toucher : y aller");
        break;
    }
    case SUB_GUIDE: {   /* les sortes (villes, puis bâtiments, la plus proche d'abord), puis les lieux de la sorte choisie */
        char nm[48];
        if (H.gcat == -1 && E.guide >= 0) { guide_label(E.guide, " (", ")", nm, sizeof nm); snprintf(t, sizeof t, "Vers : %s", nm); }
        else snprintf(t, sizeof t, "%s", H.gcat == -1 ? "Villes, lieux, bâtiments" : H.gcat >= GR_CAT ? E.w.guides[H.gcat - GR_CAT].nom : groupe_nom(H.gcat - GR_FAM));
        sub_header(SUB_GUIDE, t);
        for (int sl = 0; sl < ROWS && H.sub_scroll + sl < n; sl++) {
            const int i = H.sub_scroll + sl, c = GR[i];
            uint16_t col = ui_rgb(WHITE);
            if (c == GR_STOP) { snprintf(nm, sizeof nm, "ARRÊTER LE GUIDAGE"); col = ui_rgb(DIM); }
            else if (c >= GR_CAT) snprintf(nm, sizeof nm, "%s", E.w.guides[c - GR_CAT].nom);
            else if (c >= GR_FAM) snprintf(nm, sizeof nm, "%s", groupe_nom(c - GR_FAM));
            else { guide_label(c, " · ", "", nm, sizeof nm); if (c == E.guide) col = ui_rgb(GUIDE_RGB); }
            if (GD[i] >= 0) dist_txt(GD[i], v, sizeof v);
            list_row(i, sl, nm, GD[i] >= 0 ? v : NULL, col, -1);
        }
        const int c = n ? GR[H.sub_sel] : 0;
        sub_footer(!n ? "Aucun lieu connu" : c == GR_STOP ? "Le chevron vert disparaît" : c >= GR_FAM ? "Toucher : voir la liste" : "Toucher : guider jusque-là");
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
        const char *e = p; while (*e && *e != ' ') e++;
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

/* Choix : panneau du dialogue, réponses en rangées (la surlignée en teinte 2) ; toucher une fois surligne, deux fois
   répond (drawChoice de l'émulateur) */
static void draw_choice(void) {
#if ENG_DYN
    portrait_choice(); return;
#endif
    ui_rect(0, DLG_TOP, ENG_SCREEN, ENG_SCREEN - DLG_TOP, ui_rgb(0x080612), 245);
    ui_line(0, DLG_TOP, ENG_SCREEN, DLG_TOP, 2, C2, 255);
    char ti[48]; snprintf(ti, sizeof ti, "%s", CHX.titre[0] ? CHX.titre : "CHOISIR");   /* sans ▸ : absent des polices du paquet */
    ui_text(ui_font(FONT_MONO, 18), CX, DLG_TOP + 34, UI_CENTER, C2, 255, ti);
    const float y0 = 260 - (CHX.n - 2) * 22, w = 340, x = CX - w / 2;
    for (int k = 0; k < CHX.n; k++) {
        const float y = y0 + k * 52;
        const int sel = k == CHX.sel;
        const float q[8] = { x, y, x + w, y, x + w, y + 44, x, y + 44 };
        ui_rect(x, y, w, 44, sel ? ui_rgb(0x28061E) : ui_rgb(0x060E1A), 242);
        ui_outline(q, 4, sel ? 2 : 1, sel ? C2 : C1, 255);
        ui_text(ui_fit(FONT_DISP, CHX.opts[k], w - 24, 22, 16), CX, y + 30, UI_CENTER, ui_rgb(WHITE), 255, CHX.opts[k]);
        button_rect(x, y, w, 44, A_CHOICE, k);
    }
}

static void draw_dialog(void) {
#if ENG_DYN
    portrait_dialog(); return;
#endif
    const char *who = D.who[H.line], *text = D.text[H.line];
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
    ui_rect(0, DLG_TOP, ENG_SCREEN, ENG_SCREEN - DLG_TOP, ui_rgb(0x080612), 255);   /* opaque : 50 ms de mélange en moins par image */
    ui_line(0, DLG_TOP, ENG_SCREEN, DLG_TOP, 2, C2, 255);
    int right = !strcmp(who, D.right);
    ui_text(ui_font(FONT_DISP, 26), right ? 420 : 46, DLG_TOP + 36, right ? UI_RIGHT : UI_LEFT, C2, 255, who);
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
    if (E.in.on && E.in.etage) snprintf(t, sizeof t, "ÉTAGE %d", E.in.etage); else snprintf(t, sizeof t, "%s", E.in.on ? "INTÉRIEUR" : "SECTEUR 6");
    ui_text(ui_font(FONT_DISP, 26), 24, 46, UI_LEFT, ui_rgb(WHITE), 255, t);
    snprintf(t, sizeof t, "%.1f · %.1f", E.sera.x, E.sera.z);
    ui_text(ui_font(FONT_MONO, 18), 24, 76, UI_LEFT, ui_rgb(0x7F93A8), 255, t);
    /* Boussole en bande : les points cardinaux défilent avec la caméra, le nord en teinte 2 */
    const float bx = UW - 200;
    ui_line(bx - 88, 72, bx + 88, 72, 2, C1, 90);
    static const char *const CARD[4] = { "N", "E", "S", "O" };
    for (int k = 0; k < 4; k++) {
        float a = (k == 0 ? PI_F : k == 2 ? 0 : k * PI_F / 2) - E.yaw;   /* nord vers -z, sud vers +z (comme la carte) */
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
    /* État de Sera (ou de l'héroïne choisie : son portrait, le nom du joueur) */
    const float sy = UH - BANDE_H - 140;
    portrait_rond(0, 56, sy + 32, 32, C2);
    snprintf(t, sizeof t, "%s", PARTY[0].name);
    ui_text(ui_font(FONT_DISP, 22), 100, sy + 26, UI_LEFT, ui_rgb(WHITE), 255, t);
    snprintf(t, sizeof t, "NIV. %d", E.level);
    ui_text(ui_font(FONT_MONO, 16), 100, sy + 50, UI_LEFT, ui_rgb(DIM), 255, t);
    jauge(24, sy + 76, 250, 12, PARTY[0].max > 0 ? (float)PARTY[0].hp / PARTY[0].max : 0, C2);
    jauge(24, sy + 94, 250, 8, PARTY[0].maxmp > 0 ? (float)PARTY[0].mp / PARTY[0].maxmp : 0, C1);
    {   /* coup de la pluie de cendre : la jauge des PV pulse en gris cendre (comme l'arc de l'écran rond) */
        const float tc = E.clock - E.cendre_coup;
        if (E.cendre_coup > 0 && tc >= 0 && tc < 0.6f && PARTY[0].max > 0) ui_rect(24, sy + 76, 250 * fminf(1, (float)PARTY[0].hp / PARTY[0].max), 12, ui_rgb(0xC8C4BC), (int)(200 * (1 - tc / 0.6f)));
    }
    /* Bouton d'action : ce que propose la bulle au-dessus de la cible, sous le pouce */
    const char *act = NULL; int a = 0; uint16_t col = C1;
    if (!CR.on && !RM.on && near_npc() && E.scr_npc.vis && !(E.ride >= 0 && (fabsf(E.ride_v) > 0.3f || E.ride_phase))) {
        act = E.ride >= 0 ? "DESCENDRE" : talk_is_vehicle() ? "CONDUIRE" : "PARLER"; a = A_TALK;
    } else if (!CR.on && !RM.on && !CHX.on && E.porte >= 0 && E.scr_sera.vis) {
        const int ok = int_porte_ouverte(E.porte);
        act = ok ? "ENTRER" : "FORCER"; a = A_PORTE; col = ok ? C1 : C2;
    }
    if (act) {
        const float bw = 170, bh = 96, x = UW - 24 - bw, y = sy + 8;
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
    ui_rect(0, 0, UW, UH, ui_rgb(INK), 222);
    ui_text(ui_font(FONT_DISP, 30), 24, 54, UI_LEFT, ui_rgb(WHITE), 255, "MENU");
    const int s = (int)E.temps_jeu;
    snprintf(t, sizeof t, "%d CR · %d:%02d", E.credits, s / 3600, s / 60 % 60);
    ui_text(ui_font(FONT_MONO, 18), UW - 112, 54, UI_RIGHT, ui_rgb(DIM), 255, t);
    bouton_menu();
    float y = 112;
    for (int m = 0; m < 4; m++) {
        if (!in_team(m)) continue;
        const uint16_t mc = ui_rgb(PARTY[m].color);
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
    static const int ORDRE[NMENU] = { 0, 1, 2, 3, 4, 6, 7, 5 };   /* SYSTÈME en dernier */
    const float gap = 14, gw = (UW - 48 - gap) / 2, gh = 96, y0 = UH - 40 - 4 * gh - 3 * gap;
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
    const char *who = D.who[H.line], *text = D.text[H.line];
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
    const float y0 = UH - 40 - CHX.n * hb - (CHX.n - 1) * gap;
    for (int k = 0; k < CHX.n; k++) {
        const float y = y0 + k * (hb + gap);
        const int sel = k == CHX.sel;
        cadre_bouton(x, y, w, hb, sel, sel ? C2 : C1);
        ui_text(ui_fit(FONT_DISP, CHX.opts[k], w - 32, 22, 16), UW / 2, y + hb / 2 + 8, UI_CENTER, ui_rgb(WHITE), 255, CHX.opts[k]);
        button_rect(x, y, w, hb, A_CHOICE, k);
    }
}
#endif

/* ---------- Combat (drawBattleHUD de l'émulateur) ---------- */
#define RED_HEX 0xFF3B4E
static uint16_t bt_col(uint32_t c) { return c == BT_C1 ? C1 : c == BT_C2 ? C2 : ui_rgb(c); }
static uint32_t unit_color(int u) { return u == BT_ENEMY ? RED_HEX : PARTY[u].color; }

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
            const char *w = e;
            while (*w && *w != ' ') w++;
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
static void fx_veil(uint16_t c, float a) { a *= fx_vk; if (fx_a(a)) ui_rect(0, 0, ENG_SCREEN, ENG_SCREEN, c, fx_a(a)); }
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
    for (int u = 0; u < 4; u++)
        if (BT.transfo[u] && E.scr_party[u].vis) aura(&E.scr_party[u], eveil_aura(u), E.clock * 4 + u);
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
    ui_rect(0, 0, UW, UH, ui_rgb(0x05040A), (int)(160 * v));
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

/* Effet de l'action en cours : de l'attaquant (membre : son centre ; ennemi : le point visé) vers la cible ou chacune */
static void draw_battle_fx(void) {
    if (BT.phase != BP_ANIM || BT.an.fx < 0) return;
    const float t = E.clock - BT.an.t0;
    const int en = BT.an.who == BT_ENEMY;
    const screen_pos_t *from = en ? &E.scr_enemy : &E.scr_party[BT.an.who];
    if (!from->vis) return;
    const screen_pos_t *T[4];
    int n = 0;
    if (en || BT.an.act.heal) { for (int i = 0; i < BT.an.ntarget; i++) if (E.scr_party[BT.an.targets[i]].vis) T[n++] = &E.scr_party[BT.an.targets[i]]; }   /* ennemi, soin : l'équipe */
    else if (E.scr_enemy.vis) T[n++] = &E.scr_enemy;
    if (!n) return;
    float cx = 0, cy = 0, fy = 0;
    for (int i = 0; i < n; i++) { cx += T[i]->cx / n; cy += T[i]->cy / n; fy += T[i]->foot / n; }
    /* Sort d'attaque sur plusieurs cibles : dessiné une fois, au milieu d'elles (un soin : sur chacune) */
    if (BT.an.fx >= FX_MAGIE && FX_ELEM(BT.an.fx) != MG_SOIN && n > 1) { fx_draw(BT.an.fx, t, from->cx, from->cy, cx, cy, bt_col(BT.an.col), BT.an.k, BT.an.seed, cx, cy, 1, fy); return; }
    for (int i = 0; i < n; i++)
        fx_draw(BT.an.fx, t, from->cx, from->cy, T[i]->cx, T[i]->cy, bt_col(BT.an.col), BT.an.k, BT.an.seed + i * 7, cx, cy, i == 0, T[i]->foot);
}

static void draw_battle_hud(void) {
    const float now = E.clock;
    char t[64];
#if ENG_DYN
    /* Plein écran : l'ennemi et sa jauge en haut sur toute la largeur, les 7 prochains tours en frise dessous */
    if (BT.visible && BT.phase != BP_INTRO) {   /* entrée : le cut-in seul */
        ui_rect(0, 0, UW, 150, ui_rgb(INK), 140);
        ui_text(ui_fit(FONT_DISP, BT.name, UW - 160, 24, 16), 24, 44, UI_LEFT, ui_rgb(0xE8F6FF), 255, BT.name);
        snprintf(t, sizeof t, "NIV. %d", BT.lvl);
        ui_text(ui_font(FONT_MONO, 18), UW - 24, 44, UI_RIGHT, ui_rgb(DIM), 255, t);
        jauge(24, 58, UW - 48, 10, BT.max > 0 ? (float)BT.hp / BT.max : 0, ui_rgb(RED_HEX));
    }
    if (BT.hp > 0 && BT.phase != BP_INTRO) {
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
            const char l[2] = { u == BT_ENEMY ? '!' : PARTY[u].name[0], 0 };
            ui_text(ui_font(FONT_DISP, big ? 22 : 17), x, y + (big ? 8 : 6), UI_CENTER, ui_rgb(INK), 255, l);
        }
    }
    if (0) {
#else
    rim_ticks(0, 102);
    {
#endif
    /* Timeline CTB sur l'arc en haut à droite : les 7 prochains tours, le premier en grand */
    if (BT.hp > 0 && BT.phase != BP_INTRO) {
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
            const char l[2] = { u == BT_ENEMY ? '!' : PARTY[u].name[0], 0 };
            ui_text(ui_font(FONT_DISP, big ? 22 : 17), x, y + (big ? 8 : 6), UI_CENTER, ui_rgb(INK), 255, l);
            if (big) ui_text(ui_font(FONT_DISP, 18), x + 4, y + 40, UI_CENTER, uc, 255, "NOW");
        }
    }
    /* Ennemi : nom, niveau, barre de PV à sa droite, à hauteur de sa tête (il est en haut à gauche, la place est libre
       jusqu'à la timeline) ; pas pendant le cut-in d'entrée */
    if (BT.visible && BT.phase != BP_INTRO) {
        float lx = (E.scr_enemy.vis ? E.scr_enemy.cx : 130) + 50, ly = (E.scr_enemy.vis ? E.scr_enemy.top : 110) + 18;
        lx = lx < 150 ? 150 : lx > 230 ? 230 : lx; ly = ly < 125 ? 125 : ly > 200 ? 200 : ly;   /* sous la timeline */
        snprintf(t, sizeof t, "%s · NIV. %d", BT.name, BT.lvl);
        ui_text(ui_fit(FONT_DISP, t, 160, 20, 14), lx, ly, UI_LEFT, ui_rgb(0xE8F6FF), 255, t);
        seg_bar(lx, ly + 8, 150, 7, (float)BT.hp / BT.max, ui_rgb(RED_HEX));
    }
    }
    /* Équipe : anneau sous celui qui choisit, barres de PV et de PM (bleue) sous chacun, KO */
    const int choosing = bt_choix();
    for (int i = 0; i < 4; i++) {
        const screen_pos_t *cs = &E.scr_party[i];
        if (!cs->vis) continue;
        const member_t *m = &PARTY[i];
        const int act = BT.actor == i && choosing;
        if (act) {   /* celui qui choisit : anneau qui pulse sous lui, flèche qui flotte au-dessus de sa tête */
            const float p = sinf(now * 6), rx = 24 + 3 * p;
            ui_ellipse_ring(cs->cx, cs->foot, rx, rx * 0.32f, 8, C2, 70); ui_ellipse_ring(cs->cx, cs->foot, rx, rx * 0.32f, 2.5f, C2, 255);
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
        for (int i = 0; i < 4; i++) nm += in_team(i);
        float y = BT_GRILLE_Y - 84 - nm * 36;
        for (int i = 0; i < 4; i++) {
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
        char sub[16];
        const int pi = item_potion(), pc = pi >= 0 ? ITEMS[pi].count : 0;
        snprintf(sub, sizeof sub, "× %d", pc);
        fan_button(1, pi >= 0 ? ITEMS[pi].name : "POTION", NULL, green, BC_POTION, 0, sub, pc <= 0);
        fan_button(2, "RETOUR", "back", dim, BC_BACK, 0, NULL, 0);
    } else if (BT.phase == BP_TARGET && E.scr_enemy.vis) {
        /* Viseur : cercle en tirets qui tourne (plus épais, avec un halo, une fois verrouillé), coins, panneau de la cible */
        const float x = E.scr_enemy.cx, y = E.scr_enemy.cy, R = 78, step = 28 / R, len = 18 / R;
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
        snprintf(t, sizeof t, "CIBLE : %s", BT.name);
        ui_text(ui_fit(FONT_DISP, t, pw - 20, 22, 16), pc, py + 30, UI_CENTER, ui_rgb(0xE8F6FF), 255, t);
        if (BT.locked) snprintf(t, sizeof t, "▶ ENCORE : %s", BT.pending.name ? BT.pending.name : "");
        else snprintf(t, sizeof t, "TOUCHER POUR VERROUILLER");
        ui_text(ui_fit(FONT_MONO, t, pw - 20, 20, 14), pc, py + 60, UI_CENTER, BT.locked ? C2 : C1, 255, t);
        fan_button(2, "RETOUR", "back", dim, BC_BACK, 0, NULL, 0);
    }
    /* Clavier : à chaque changement de phase ou de membre, le premier bouton présent */
    if (H.bsel < 0 || !H.bslot_cmd[H.bsel]) { H.bsel = 0; while (H.bsel < 4 && !H.bslot_cmd[H.bsel]) H.bsel++; }
    draw_cutin();
    draw_intro_combat();
    draw_victoire();
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
        const screen_pos_t *pos = u == BT_ENEMY ? &E.scr_enemy : &E.scr_party[u];
        if (!pos->vis) continue;
        const float y = (u == BT_ENEMY ? pos->cy - 40 : pos->top) - age * 40;
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
    const float s = h / sh->fh;
    ui_sprite(blk + (frame % sh->frames) * sh->fw, sh->fw * sh->frames, pal, sh->fw, sh->fh, cx - sh->fw * s / 2, pied - (sh->fh - sh->foot[row & 7]) * s, s, a,
              UW / 2, UH / 2, PORTRAIT ? UW + UH : 233);
}
static uint16_t SPR_PAL[5][256];   /* palettes des sprites de l'entrée en combat (équipe, ennemi) */
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
    ui_rect(0, 0, UW, UH, ui_rgb(INK), (int)(228 * v));
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
    int n = 0, M[4];
    for (int i = 0; i < 4; i++) if (in_team(i) && member_sheet(i) >= 0) M[n++] = i;
    const float hh = (PORTRAIT ? 0.5f : 0.44f) * R, pas = n > 1 ? fminf(0.24f * R, 0.62f * R / (n - 1)) : 0;
    for (int k = 0; k < n; k++) {
        const int i = M[k], own = member_sheet(i), sh = anim_sheet(own, ANIM_IDLE);
        /* xs : sa place dans la bande ; pieds sur la pente de la bande (elle monte vers la droite) */
        const float xs = cx - 0.42f * R - (n - 1) * pas / 2 + k * pas, x = xs + dx1 * (1 + 0.3f * k), pied = ya + 0.17f * R - 0.18f * (xs - cx);
        ui_planche(sh, 1, (int)(E.clock / 0.22f), i ? E.w.persos[E.w.party_perso[i]].remap : 0, SPR_PAL[i], x, pied, hh, a);
    }
    /* L'ennemi : plus grand (un boss encore plus), de trois quarts vers la gauche */
    const perso_t *pe = &E.w.persos[BT.perso];
    ui_planche(anim_sheet(pe->sheet, ANIM_IDLE), 7, (int)(E.clock / 0.22f), perso_remap(BT.perso, BT.var), SPR_PAL[4],
               cx + 0.36f * R + dx2, yb + 0.18f * R, (BT.boss ? 0.86f : 0.74f) * R, a);
    /* VS, ALERTE, nom de l'ennemi */
    if (t > 0.25f) {
        const float k = fminf(1, (t - 0.25f) / 0.2f), sz = 1 + 0.6f * (1 - k);
        text_outlined(ui_font(FONT_DISP, (int)(44 * sz)), cx - 0.02f * R, cy + 0.06f * R, w, (int)(a * k), "VS");
    }
    if (fmodf(t, 0.5f) < 0.36f) ui_text(ui_font(FONT_DISP, 26), cx, cy - 0.7f * R, UI_CENTER, rouge, a, BT.boss ? "ALERTE · BOSS" : "ALERTE");
    char nm[56];
    snprintf(nm, sizeof nm, "%s · NIV. %d", BT.name, BT.lvl);
    ui_text(ui_fit(FONT_DISP, nm, 1.1f * R, 24, 16), cx + 0.08f * R, cy + 0.69f * R, UI_CENTER, w, a, nm);
    if (t < 0.12f) ui_rect(0, 0, UW, UH, w, (int)(200 * (1 - t / 0.12f)));   /* éclair */
}
/* Fin de combat (BP_WIN) : tableau du résultat. VICTOIRE et l'ennemi vaincu ; expérience (la barre du niveau de l'équipe
   se remplit, niveau supérieur annoncé) ; crédits gagnés et bourse ; butin, chaque objet avec son icône ; les panneaux
   entrent l'un après l'autre ; un toucher continue (drawVictoire de l'émulateur) */
static void draw_victoire(void) {
    if (!BT.on || BT.phase != BP_WIN) return;
    const float t = E.clock - BT.t0, cx = UW / 2, oy = PORTRAIT ? (UH - ENG_SCREEN) * 0.4f : 0, pw = PORTRAIT ? UW - 48 : 320, l = cx - pw / 2, r = cx + pw / 2;
    const uint16_t w = ui_rgb(WHITE), dim = ui_rgb(DIM);
    char tx[96];
    ui_rect(0, 0, UW, UH, ui_rgb(INK), (int)(170 * fminf(1, t / 0.3f)));
    text_outlined(ui_font(FONT_DISP, 34), cx, oy + 92, C2, (int)(255 * fminf(1, t / 0.2f)), "VICTOIRE");
    snprintf(tx, sizeof tx, "%s vaincu · NIV. %d", BT.name, BT.lvl);
    ui_text(ui_fit(FONT_MONO, tx, pw, 16, 13), cx, oy + 118, UI_CENTER, dim, 255, tx);
    float q[12];
    /* Panneau k : entre par la droite 0,15 s après le précédent */
    #define PANNEAU(k, y, h) const float e##k = fminf(1, fmaxf(0, (t - 0.15f * (k) - 0.1f) / 0.25f)), py##k = oy + (y);         const int a##k = (int)(255 * e##k); const float ox##k = (1 - e##k) * 60;         ui_chamfer_path(l + ox##k, py##k, pw, h, 10, q); ui_poly(q, 6, ui_rgb(0x060E1A), (int)(230 * e##k)); ui_outline(q, 6, 1, C1, a##k)
    /* Expérience : le niveau de l'équipe après la victoire, la barre qui se remplit de l'ancienne à la nouvelle valeur */
    PANNEAU(0, 136, 66);
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
    if (up >= 1) { snprintf(tx, sizeof tx, "NIVEAU SUPÉRIEUR ! PV, PM et ATQ en hausse"); ui_text(ui_fit(FONT_MONO, tx, pw - 28, 15, 12), cx + ox0, py0 + 58, UI_CENTER, (fmodf(t, 0.6f) < 0.4f) ? C2 : w, a0, tx); }
    else if (L < NIV_MAX) { snprintf(tx, sizeof tx, "Encore %d EXP pour le niveau %d", exp_next(L) - x, L + 1); ui_text(ui_fit(FONT_MONO, tx, pw - 28, 15, 12), cx + ox0, py0 + 58, UI_CENTER, dim, a0, tx); }
    /* Crédits : gagnés, puis la bourse */
    PANNEAU(1, 210, 40);
    snprintf(tx, sizeof tx, "+%d CR", BT.rw_cr);
    ui_text(ui_font(FONT_DISP, 18), l + ox1 + 14, py1 + 27, UI_LEFT, w, a1, "CRÉDITS");
    ui_text(ui_font(FONT_MONO, 20), r + ox1 - 14, py1 + 27, UI_RIGHT, C2, a1, tx);
    snprintf(tx, sizeof tx, "bourse %d", E.credits + BT.rw_cr);
    ui_text(ui_font(FONT_MONO, 14), cx + ox1 + 10, py1 + 26, UI_CENTER, dim, a1, tx);
    /* Butin : les objets laissés, avec leur icône */
    int drop[4], nd = 0;
    if (BT.rw_loot >= 0) drop[nd++] = BT.rw_loot;
    PANNEAU(2, 258, 34 + (nd ? nd : 1) * 40);
    ui_text(ui_font(FONT_DISP, 18), l + ox2 + 14, py2 + 24, UI_LEFT, w, a2, "BUTIN");
    for (int k = 0; k < nd; k++) {
        const float yy = py2 + 34 + k * 40;
        if (e2 > 0) objet_icone(item_icon(drop[k]), l + ox2 + 18, yy, PORTRAIT ? UW + UH : 233);
        ui_text(ui_fit(FONT_DISP, ITEMS[drop[k]].name, pw - 120, 20, 14), l + ox2 + 60, yy + 23, UI_LEFT, w, a2, ITEMS[drop[k]].name);
        ui_text(ui_font(FONT_MONO, 20), r + ox2 - 14, yy + 23, UI_RIGHT, C2, a2, "× 1");
    }
    if (!nd) ui_text(ui_font(FONT_MONO, 16), cx + ox2, py2 + 58, UI_CENTER, dim, a2, "Aucun butin");
    #undef PANNEAU
    if (t > 0.8f && fmodf(t, 1.0f) < 0.7f) ui_text(ui_font(FONT_MONO, 16), cx, oy + (PORTRAIT ? 440 : 412), UI_CENTER, C1, 255, "Toucher pour continuer");
}

static void hud_world_layers(void);

/* Cible à plus de 6 cases : flèche sur le bord de l'écran rond (hampe et pointe vers le dehors, contour sombre ; FLECHE),
   dans sa direction vue de la caméra (avant : haut de l'écran, droite : (cos yaw, -sin yaw)), qui pulse ; plus près : repère au-dessus de la cible (losange et pointe vers le
   bas qui flottent). k : déphasage (guidage) (drawCible de l'émulateur) */
static void draw_cible(float ox, float oz, uint16_t col, int k) {
    const float dx = ox - E.sera.x, dz = oz - E.sera.z, d = hypotf(dx, dz);
    if (d >= 6) {
        const float a = atan2f(dx * cosf(E.yaw) - dz * sinf(E.yaw), dx * sinf(E.yaw) + dz * cosf(E.yaw)), s = sinf(a), c = -cosf(a), R = 204;
        /* (distance au centre, écart de côté) de chaque sommet : pointe, pointe large, hampe */
        static const float FLECHE[14] = { 23, 0, 4, 18, 4, 6, -18, 6, -18, -6, 4, -6, 4, -18 };
        float q[14];
        for (int i = 0; i < 7; i++) { const float t = R + FLECHE[2 * i], e = FLECHE[2 * i + 1]; q[2 * i] = CX + s * t + c * e; q[2 * i + 1] = CY + c * t - s * e; }
        const int al = (int)(255 * (0.7f + 0.3f * sinf(H.now * 1000 / 260 + k * 1.6f)));
        ui_poly(q, 7, col, al);
        ui_outline(q, 7, 2, ui_rgb(INK), al);
        return;
    }
    float x, y;
    if (d <= 0.8f || !project(ox, height_at(ox, oz) + 2.2f, oz, &x, &y)) return;
    y += sinf(H.now * 1000 / 300 + k) * 6;
    const int al = (int)(255 * (0.85f + 0.15f * sinf(H.now * 1000 / 200)));
    const float lo[8] = { x, y - 32, x + 11, y - 19, x, y - 6, x - 11, y - 19 }, tri[6] = { x - 14, y + 2, x + 14, y + 2, x, y + 19 };
    ui_poly(lo, 4, col, al);
    ui_poly(tri, 3, col, al);
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
    for (int i = 0; i < E.w.nastres; i++) {
        const astre_t *a = &E.w.astres[i];
        const float dx = a->x - E.sera.x, dz = a->z - E.sera.z;
        if (a->carte != E.w.carte || hypotf(dx, dz) < ASTRE_LOIN || (a->vers < 0 && a->r > 0 && a->r < 1.5f)) continue;
        const float an = atan2f(dx * cosf(E.yaw) - dz * sinf(E.yaw), dx * sinf(E.yaw) + dz * cosf(E.yaw)), s = sinf(an), c = -cosf(an), R = 196;
        const float lx = CX + s * (R - 16), hw = ui_text_width(f, a->nom) / 2 + 3;
        float ly = CY + c * (R - 16) + 5;
        for (int essai = 0; essai < 8; essai++) {
            int chevauche = 0;
            for (int b = 0; b < nv && !chevauche; b++) chevauche = fabsf(lx - vus[b][0]) < hw + vus[b][2] && fabsf(ly - vus[b][1]) < ASTRE_LIGNE;
            if (!chevauche) break;
            ly += c <= 0 ? ASTRE_LIGNE : -ASTRE_LIGNE;
        }
        if (nv < 32) { vus[nv][0] = lx; vus[nv][1] = ly; vus[nv][2] = hw; nv++; }
        const uint16_t col = a->vers >= 0 ? C1 : ui_rgb(0x9AA4B4);
        const float q[6] = { CX + s * (R + 12), CY + c * (R + 12), CX + s * R + c * 8, CY + c * R - s * 8, CX + s * R - c * 8, CY + c * R + s * 8 };
        ui_poly(q, 3, col, 220);
        ui_outline(q, 3, 2, ui_rgb(INK), 220);
        ui_text(f, lx, ly, UI_CENTER, col, 230, a->nom);
    }
}
/* Objectif de la quête (teinte 2), lieu du menu GUIDAGE (vert) s'il n'est pas au même endroit */
static void draw_objective(void) {
    float ox, oz;
    const int ob = objective_pos(&ox, &oz);
    if (ob) draw_cible(ox, oz, C2, 0);
    if (E.guide >= 0 && !E.w.carte) {
        const guide_t *g = &E.w.guides[E.guide];
        if (!ob || hypotf(ox - g->x, oz - g->z) >= 1) draw_cible(g->x, g->z, ui_rgb(GUIDE_RGB), 1);
    }
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
void eng_set_batterie(int pct, int charge) { MT.batt = pct; MT.charge = charge; }
void eng_verrouiller(void) { V.on = 1; V.doigt = 0; V.touche = 0; V.dx = V.dy = 0; V.t = 0; }
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
   graduations tous les 10 %, niveau en chiffres sept segments comme le cadran de la montre */
static void vol_build(void) {
    const float cx = UW / 2 - ui_tx(), cy = UH / 2 - ui_ty(), r = 200, a0 = 0.75f * PI_F, span = 1.5f * PI_F;
    const int v = (int)lroundf(eng_volume() * 20);
    const float a1 = a0 + span * v / 20, pouls = H.now - H.vol_t < 0.25f ? 1 - (H.now - H.vol_t) / 0.25f : 0;
    const uint16_t BL = ui_rgb(0xE8F6FF);
    ui_circle(cx, cy, 228, ui_rgb(0x05070C), 220);   /* le menu s'efface derrière le cadran */
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
        const float hx = cx - 8, hy = cy - 92;
        const float hp[12] = { hx - 14, hy - 6, hx - 6, hy - 6, hx + 4, hy - 15, hx + 4, hy + 15, hx - 6, hy + 6, hx - 14, hy + 6 };
        ui_poly(hp, 6, C1, 230);
        for (int k = 0; k < 3; k++) ui_arc(hx + 4, hy, 10 + k * 7, -0.8f, 0.8f, 3, C1, v > k * 7 ? 230 : 45);
        if (v == 0) ui_line(hx - 16, hy + 16, hx + 26, hy - 16, 3, C2, 255);
    }
    ui_text(ui_font(FONT_MONO, 18), cx, cy - 56, UI_CENTER, C1, 200, "SON");
    /* Niveau : trois chiffres, ou « coupé » */
    if (v == 0) ui_text(ui_font(FONT_DISP, 30), cx, cy + 14, UI_CENTER, C2, 255, "COUPÉ");
    else {
        const int pc = v * 5, nd = pc >= 100 ? 3 : pc >= 10 ? 2 : 1;
        const float w = 38, h = 70, t = 9, ec = 9, y = cy - 36;
        float x = cx - (nd * w + (nd - 1) * ec + 22) / 2;
        for (int k = nd - 1; k >= 0; k--) {
            int d = pc; for (int j = 0; j < k; j++) d /= 10;
            chiffre7(x, y, w, h, t, d % 10, BL, 1); x += w + ec;
        }
        ui_text(ui_font(FONT_MONO, 20), x + 6, y + h, UI_LEFT, C1, 230, "%");
    }
    ui_text(ui_font(FONT_MONO, 18), cx, cy + 92, UI_CENTER, C1, 190, "TOURNER LE BORD");
    ui_text(ui_font(FONT_MONO, 16), cx, cy + 116, UI_CENTER, ui_rgb(0x7F93A8), 190, "Toucher : terminer");
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
    img_trame();
    if (V.on) { verrou_build(); return; }   /* montre verrouillée : son cadran seul */
    if (accueil_on()) { accueil_build(); return; }
    if (BT.on) draw_battle_hud();   /* combat : ni menu, ni dialogue, ni bulle d'interaction */
    else hud_world_layers();
    /* Fondu au noir de 0,45 s au début et à la fin d'un combat */
    const float k = (E.clock - BT.fade_t) / 0.45f;
    if (BT.fade_t > 0 && k < 1) ui_rect(0, 0, UW, UH, ui_rgb(0), (int)(255 * (1 - (k < 0 ? 0 : k))));
}

/* Hors combat : HUD de la world map, puis dialogue, écran du menu, menu ou message */
/* Illustration de la scène (instruction « image », E.scene) : plein écran, sous la bulle du dialogue */
static void draw_scene(void) {
    int iw, ih;
    const uint16_t *pal;
    const uint8_t *ix = img_ix(E.scene - 1, &iw, &ih, &pal, 1);
    if (!ix) return;
    const float s = (UW - 2) / iw, y = (PORTRAIT ? UH * 0.42f : CY) - ih * s / 2;
    ui_image_pal(ix, pal, iw, ih, (UW - iw * s) / 2, y, s, UW / 2, UH / 2, PORTRAIT ? UW + UH : 233);
}

static void hud_world_layers(void) {
    if (E.alarme.on && !H.menu && H.sub < 0) { cadre(1); draw_alarme(); cadre(0); }   /* effraction : sous l'interface */
    if ((POL.niveau || (E.ride >= 0 && E.npcs[E.ride].vole)) && !POL.arr && !H.menu && H.sub < 0 && !H.dialog && !CR.on) draw_recherche();   /* police.c */
    if (!H.menu && H.sub < 0) draw_world_hud();   /* sous le menu et ses écrans, seule la 3D reste visible */
    if (!E.in.on) guide_step();
    if (!H.menu && H.sub < 0 && !H.dialog && !TT.on && !E.in.on) { draw_astres(); draw_objective(); }
    /* Illustration de la scène : sous le carton de titre, les annonces, le fondu et la bulle du dialogue */
    if (E.scene && script_running() && !H.menu) draw_scene();
    if (TT.on) { cadre(1); draw_title(); cadre(0); }
    else if (H.nann && !H.menu && H.sub < 0 && !H.dialog) {
        if (PORTRAIT) ui_translate((UW - ENG_SCREEN) / 2, 80);   /* sous la barre du haut */
        draw_annonce(); ui_translate(0, 0);
    }
    /* Mise en scène : fondu au noir (instruction « fondu »), chrono de la fuite */
    if (MISE.fondu > 0.004f) ui_rect(0, 0, UW, UH, ui_rgb(0), (int)(255 * (MISE.fondu > 1 ? 1 : MISE.fondu)));
    if (MISE.fuite_on) {
        char t[24];
        snprintf(t, sizeof t, "FUITE  %.1f", MISE.fuite_t > 0 ? MISE.fuite_t : 0);
        ui_text(ui_font(FONT_DISP, 26), PORTRAIT ? UW / 2 : CX, PORTRAIT ? 150 : 78, UI_CENTER, MISE.fuite_t < 10 ? C2 : C1, 255, t);
    }
    if (H.dialog) draw_dialog();
    else if (CHX.on) draw_choice();
    else if (H.sub >= 0) {
        if (PORTRAIT) ui_translate((UW - ENG_SCREEN) / 2, 0);   /* plein écran : CX au milieu de l'écran */
        draw_sub(); ui_translate(0, 0);
    } else if (H.menu) {
        draw_menu();
        if (H.now - H.toast_t < 1.6f && H.toast[0]) ui_text(ui_fit(FONT_MONO, H.toast, PORTRAIT ? UW - 48 : 190, 20, 16), PORTRAIT ? UW / 2 : CX, PORTRAIT ? UH - 520 : 336, UI_CENTER, C2, 255, H.toast);
    } else if (H.now - H.toast_t < 1.8f && H.toast[0]) ui_text(ui_fit(FONT_MONO, H.toast, PORTRAIT ? UW - 48 : 300, 20, 16), PORTRAIT ? UW / 2 : CX, PORTRAIT ? 190 : 160, UI_CENTER, C2, 255, H.toast);
}

uint32_t hud_theme_c1(void) { return THEMES[E.theme].c1; }
uint32_t hud_theme_c2(void) { return THEMES[E.theme].c2; }
