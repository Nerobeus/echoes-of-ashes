/* Stockage USB : la carte SD vue par le PC comme une clé (TinyUSB, classe MSC). La montre redémarre dans ce mode
   (demande en mémoire RTC), sans monter la carte pour elle-même ; PWR ou l'éjection côté PC la ramènent à l'accueil. */
#ifndef USB_H
#define USB_H

#include <stdbool.h>

/* Redémarre en mode stockage USB ; false si le firmware n'a pas TinyUSB */
bool usb_stockage(void);
/* Au démarrage : true si le mode stockage a été demandé (il tourne alors jusqu'au redémarrage) */
bool usb_demande(void);
void usb_mode(void);
/* Rend le port USB à la console série (après le mode stockage, et à chaque démarrage) */
void usb_console(void);

#endif
