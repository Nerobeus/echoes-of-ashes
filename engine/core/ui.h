/*
 * Couche 2D de l'interface, en coordonnées de l'écran natif 466 × 466
 *
 * Chaque image, l'interface remplit une liste d'opérations (polygones, glyphes, icônes, images) ; chaque bande
 * dessine ensuite celles qui la touchent, par-dessus la 3D, dans l'ordre. Polygones remplis en pair-impair
 * (un anneau = contour extérieur puis intérieur), traits et arcs convertis en polygones, texte et icônes
 * en cartes d'opacité 4 bits exportées par l'émulateur.
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>

enum { UI_LEFT, UI_CENTER, UI_RIGHT };
enum { FONT_DISP = 0, FONT_BODY = 1, FONT_MONO = 2 };   /* Oxanium 600, Oxanium 400, Share Tech Mono */

typedef struct {
    const uint8_t *data;               /* opacités 4 bits, deux pixels par octet */
    uint8_t w, h;
    int8_t x0, y0;                     /* coin haut-gauche depuis le stylo (glyphe) ou le centre (icône) */
    uint16_t cp, adv16;                /* glyphe : code, avance × 16 */
} ui_mask_t;

typedef struct {
    int style, size, n;
    ui_mask_t *g;
    ui_mask_t *ascii[128];
} ui_font_t;

int ui_init(void);                     /* lit les sections FONT et ICON du paquet */
void ui_begin(int swap565);
int ui_op_count(void);
int ui_pts_count(void);
void ui_present(void);                 /* la liste remplie devient celle que dessinent les bandes */
extern int ui_ox, ui_oy;   /* origine du canevas de l'interface dans l'image (0, 0 : le canevas couvre l'image) */
extern int ui_w, ui_h;     /* taille du canevas : 466 × 466, ou toute l'image en plein écran */
extern int ui_img_w, ui_img_h;   /* taille de toute l'image (le canevas s'arrête aux marges du téléphone) */
void ui_voile_image(uint16_t c, int alpha);   /* rectangle sur toute l'image, marges comprises, sans ui_translate */
/* Décalage des opérations enregistrées ensuite (et des zones tactiles de hud.c) : cadre de 466 × 466 d'un écran pas
   encore refait pour le plein écran ; (0, 0) sinon */
void ui_translate(float x, float y);
float ui_tx(void);
float ui_ty(void);
void ui_render_band(int y0, int h, uint16_t *dst, int x0, int x1);   /* dst : (x1 - x0) × h pixels */
int ui_band_spans(int y0, int h, int *xs, int max);   /* colonnes de l'image où l'interface dessine sur ces lignes : intervalles [xs[2k], xs[2k + 1]) */
/* Écran rectangulaire (render.c, eng_render_band_rect) : opérations dessinées par ui_render_band et ui_band_spans, de i0 à
   i1 exclu (i1 < 0 : toutes) ; voiles de toute l'image (ui_voile_image) appliqués en 320 × 240 entre deux plages */
void ui_plage_ops(int i0, int i1);
int ui_nops(void);
int ui_voile_suivant(int i);
void ui_voile_bande(int i, uint16_t *dst, int n);

/* Couleurs RGB565 natives (sans inversion d'octets) */
uint16_t ui_native(uint16_t c);
static inline uint16_t ui_rgb(uint32_t hex) {
    return (uint16_t)(((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex & 0xFF) >> 3));
}
uint16_t ui_mix(uint16_t a, uint16_t b, float t);   /* a + (b - a) × t */

void ui_poly(const float *xy, int n, uint16_t c, int alpha);
void ui_rect(float x, float y, float w, float h, uint16_t c, int alpha);
void ui_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int alpha);
void ui_outline(const float *xy, int n, float w, uint16_t c, int alpha);   /* contour fermé */
void ui_circle(float cx, float cy, float r, uint16_t c, int alpha);
void ui_ring(float cx, float cy, float r, float w, uint16_t c, int alpha);
/* Anneau dégradé de r0 (transparent) à r1 (alpha), en une passe : halos du bord de l'écran ; g2 : plus vite près du bord */
void ui_halo(float cx, float cy, float r0, float r1, uint16_t c, int alpha, int g2);
/* Plein écran (ENG_DYN) : halo des bords de toute l'image, marges comprises, épaisseur e (accélération en course) */
void ui_halo_bord(float e, uint16_t c, int alpha);
void ui_arc(float cx, float cy, float r, float a0, float a1, float w, uint16_t c, int alpha);   /* angles du canevas (0 = droite, sens horaire) */
void ui_ellipse_ring(float cx, float cy, float rx, float ry, float w, uint16_t c, int alpha);
/* Hexagone pointes à gauche et à droite, comme hexPath de l'émulateur */
void ui_hex_path(float cx, float cy, float w, float h, float *xy12);
void ui_chamfer_path(float x, float y, float w, float h, float c, float *xy12);

const ui_font_t *ui_font(int style, int size);   /* plus grande taille disponible ≤ size */
float ui_text_width(const ui_font_t *f, const char *s);
/* Fin du mot qui commence en p, pour couper un texte en lignes : la ponctuation haute séparée par une espace (« ? », « ! »,
   « : », « ; », « » ») reste avec le mot d'avant (11/11/2026 : « Qui en descend » puis « ? » seul sur sa ligne) */
const char *ui_fin_mot(const char *p);
/* Taille la plus grande (≤ size, ≥ min) pour que le texte tienne dans max_w */
const ui_font_t *ui_fit(int style, const char *s, float max_w, int size, int min);
float ui_text(const ui_font_t *f, float x, float y, int align, uint16_t c, int alpha, const char *s);
/* Texte centré sur l'angle a (angles du canevas) du cercle (cx, cy, r), ligne de base sur le cercle : en haut
   (bottom = 0) les lettres vont vers l'extérieur et le texte tourne en sens horaire ; en bas (bottom = 1) elles vont
   vers le centre et le texte se lit de gauche à droite */
void ui_text_arc(const ui_font_t *f, float cx, float cy, float r, float a, int bottom, uint16_t c, int alpha, const char *s);
/* Filtre des textes (hud.c : noms échangés de l'héroïne choisie, nom_echange) : rend s, ou le texte filtré écrit dans
   buf (cap octets). ui_text, ui_text_width, ui_fit et ui_text_arc l'appliquent ; NULL : aucun. Rend le précédent */
typedef const char *(*ui_filtre_t)(const char *s, char *buf, int cap);
ui_filtre_t ui_filtre(ui_filtre_t f);
void ui_icon(const char *name, float cx, float cy, int size, uint16_t c, int alpha);
/* Carte d'opacité donnée (pictogramme de la carte du menu), comme une icône : coin à (x0, y0) du centre */
void ui_masque(const ui_mask_t *m, float cx, float cy, uint16_t c, int alpha);
/* Image RGB565 (masque : 0 = transparent ; NULL : opaque), agrandie de s, découpée au disque (ccx, ccy, cr) */
void ui_image(const uint16_t *px, const uint8_t *mask, int w, int h, float x, float y, float s, float ccx, float ccy, float cr);
/* Même chose, opaque, depuis des couleurs RGB (3 octets par pixel) lues telles quelles : pas de copie en mémoire */
void ui_image_rgb(const uint8_t *rgb, int w, int h, float x, float y, float s, float ccx, float ccy, float cr);
/* Image opaque indexée : un octet par pixel, couleur pal[index] (RGB565 de ui_rgb) */
void ui_image_pal(const uint8_t *ix, const uint16_t *pal, int w, int h, float x, float y, float s, float ccx, float ccy, float cr);
/* Cellule d'une planche de sprites : index (0 transparent) d'une cellule w × h dans des lignes de stride octets, palette
   pal, opacité a ; ix doit rester lisible jusqu'au dessin de l'image suivante (planche demandée par sheet_block) */
void ui_sprite(const uint8_t *ix, int stride, const uint16_t *pal, int w, int h, float x, float y, float s, int a, float ccx, float ccy, float cr);
/* L'image que l'appel précédent vient d'enregistrer se dessine retournée gauche-droite (carte du menu) */
void ui_miroir(void);

#endif
