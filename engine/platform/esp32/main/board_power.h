#ifndef BOARD_POWER_H
#define BOARD_POWER_H

#include "driver/i2c.h"

/* Bus I2C (AXP2101, tactile, codec) puis régulateurs de l'AXP2101 ; à appeler avant lcd_init */
void board_power_init(void);
i2c_port_t board_i2c(void);

/* Batterie par l'AXP2101 : tension (mV) et pourcentage approché ; -1 : pas de mesure */
int alim_batterie_mv(void);
int alim_batterie_pct(void);
int alim_usb(void);              /* 1 si l'USB alimente la carte (VBUS), 1 aussi sans réponse de l'AXP2101 */
void alim_eteindre(void);        /* coupure par l'AXP2101 (rallumage par PWR) */
int alim_pwr_axp(void);           /* appuis vus par l'AXP2101 (0x49 : relâché, enfoncé, long, court), effacés */
int alim_bouton_pwr(void);       /* 1 tant que PWR est appuyé (TCA9554 de la 1,75", GPIO3 de la 1,75" C) ; sinon 1 une fois par appui fini (AXP2101) */

#endif
