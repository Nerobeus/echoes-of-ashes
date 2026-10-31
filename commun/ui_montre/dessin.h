/* Dessin de l'app montre : image entière en PSRAM (466 × 466, RGB565 dans l'ordre d'octets de l'écran), primitives,
   texte UTF-8 anticrénelé (police.h), envoi des seules lignes changées à l'écran */
#ifndef DESSIN_H
#define DESSIN_H

#include <stdint.h>
#include "police.h"

#define RGB(r, g, b) ((uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3))
#define C_NOIR    RGB(0, 0, 0)
#define C_FOND    RGB(8, 10, 20)
#define C_BLANC   RGB(235, 240, 250)
#define C_GRIS    RGB(120, 128, 150)
#define C_SOMBRE  RGB(28, 32, 52)
#define C_CYAN    RGB(34, 228, 255)
#define C_MAGENTA RGB(255, 45, 155)
#define C_VERT    RGB(60, 230, 120)
#define C_ROUGE   RGB(255, 70, 70)
#define C_ORANGE  RGB(255, 170, 40)

enum { A_GAUCHE, A_CENTRE, A_DROITE };

int d_init(void);                                   /* 0 si la mémoire manque */
/* Avec les tampons de l'appelant (PC DOS : l'image VGA d'origine, figée pendant le menu, et ses tampons DMA de lignes
   lignes) : rien n'est réservé */
int d_init_ext(uint16_t *img, uint16_t *t0, uint16_t *t1, int lignes);
void d_effacer(uint16_t c);
/* Décalage horizontal de tout ce qui suit (page glissée sous le doigt) */
void d_decalage(int dx);
void d_rect(int x, int y, int w, int h, uint16_t c);
void d_rect_arrondi(int x, int y, int w, int h, int r, uint16_t c);
void d_anneau(int cx, int cy, int r0, int r1, uint16_t c);          /* disque si r0 = 0 */
/* Arc de l'anneau r0..r1 entre les fractions de tour a0 et a1 (0 en haut, sens des aiguilles) */
void d_arc(int cx, int cy, int r0, int r1, float a0, float a1, uint16_t c);
/* Texte (UTF-8) : y = haut de la ligne ; largeur rendue */
int d_texte(const police_t *p, int x, int y, const char *s, uint16_t c, int align);
int d_largeur(const police_t *p, const char *s);
/* Texte coupé en lignes dans la largeur w, n lignes au plus (… à la fin si ça déborde) ; hauteur utilisée */
int d_paragraphe(const police_t *p, int x, int y, int w, int n, const char *s, uint16_t c);
uint16_t d_mele(uint16_t a, uint16_t b, int t16);   /* t16 : 0 = a, 16 = b */
/* Image plein écran (466 × 466, ordre d'octets de l'écran) recopiée avec le décalage ; mélanges (t16 sur 16) par-dessus
   ce qui est déjà dessiné ; trait anticrénelé */
void d_image(const uint16_t *img);
void d_rect_mele(int x, int y, int w, int h, uint16_t c, int t16);
void d_arc_mele(int cx, int cy, int r0, int r1, float a0, float a1, uint16_t c, int t16);
void d_ligne(float x0, float y0, float x1, float y1, float ep, uint16_t c);
/* Polygone plein (n sommets x, y), bords horizontaux anticrénelés, mêlé à t16 sur 16 */
void d_poly(const float *p, int n, uint16_t c, int t16);
uint16_t *d_tampon(void);                          /* l'image (essais sur PC) */
/* Dessin dans une autre image (même taille) ; rend l'ancienne cible. Décalage courant (page glissée) */
uint16_t *d_cible(uint16_t *img);
int d_decalage_courant(void);
/* Envoie à l'écran les lignes changées depuis le dernier envoi (toutes : d_tout) */
void d_envoyer(void);
void d_tout(void);

#endif
