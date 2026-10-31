/* Écran AMOLED CO5300 en QSPI, envoi par bandes (repris du pilote du projet tiny386), utilisable depuis deux cœurs */
#ifndef LCD_CO5300_H
#define LCD_CO5300_H

#include <stdint.h>

#define LCD_BUFFERS 4          /* tampons de bande distincts (numérotés 0..3) */

void lcd_init(void);
void lcd_set_brightness(uint8_t level);   /* commande synchrone : seulement quand plus rien n'est en cours d'envoi */

/* Envoie le rectangle colonnes [x0, x1), lignes [y, y + h) (tous pairs) depuis buf : (x1 - x0) × h pixels
   contigus en mémoire DMA. Asynchrone : buf (tampon numéro tag) ne doit pas être réécrit avant lcd_buffer_wait(tag). */
void lcd_band_send(int x0, int x1, int y, int h, const uint16_t *buf, int tag);

/* Attend que le tampon tag ne soit plus en cours d'envoi */
void lcd_buffer_wait(int tag);

/* Attend la fin de tous les envois */
void lcd_wait_all(void);

/* Veille de l'écran : DISPOFF puis SLPIN (plus rien en cours d'envoi) ; réveil : SLPOUT, 120 ms, DISPON */
void lcd_dormir(void);
void lcd_reveiller(void);

#endif
