/*
 * Cartes : la Waveshare ESP32-S3-Touch-AMOLED-1.75 (montre cible, écran rond 466 × 466) et la 2,06" de test (broches du
 * projet tiny386), menuconfig > Echoes of Ashes > Carte. Même pilote d'écran CO5300 par bandes.
 * 1,75" : brochage de HARDWARE_REFERENCE.md du dépôt Waveshare de la carte (GPIO13 y est l'entrée TE de l'écran, sans
 * alimentation à piloter ; le bouton PWR passe par l'AXP2101, son état se lit sur le TCA9554).
 */
#ifndef BOARD_H
#define BOARD_H

#include "sdkconfig.h"

#define LCD_W        466
#define LCD_H        466
#define LCD_X_OFFSET 6       /* première colonne physique du CO5300 pour la dalle de 466 */
#define LCD_Y_OFFSET 0
#define LCD_BAND     34      /* lignes natives par bande : 17 lignes internes doublées, 466 × 34 × 2 = 31 688 octets (une transaction SPI : 32 Ko au plus) */

#define PIN_LCD_CS   12
#define PIN_LCD_D0   4
#define PIN_LCD_D1   5
#define PIN_LCD_D2   6
#define PIN_LCD_D3   7
#define PIN_I2C_SDA  15      /* bus partagé : AXP2101, tactile, ES8311, PCF85063 (et TCA9554, QMI8658 sur la 1,75") */
#define PIN_I2C_SCL  14

#if CONFIG_ECHOES_CARTE_206
#define PIN_LCD_SCLK 11
#define PIN_LCD_RST  8
#define PIN_LCD_EN   13      /* alimentation de l'écran (aussi TE) */
#define PIN_TP_RST   9
#define PIN_TP_INT   38
#define TOUCH_FT3168 1
#define CARTE_NOM    "2,06\" de test"
#elif CONFIG_ECHOES_CARTE_175C
/* BSP Waveshare esp32_s3_touch_amoled_1_75c (pas le brochage de la 1,75" sans C : 39, 40 et 42 y sont libres, sur le
   connecteur du dos) */
#define PIN_LCD_SCLK 38
#define PIN_LCD_RST  1
#define PIN_LCD_EN   -1
#define PIN_TP_RST   2
#define PIN_TP_INT   11
#define TOUCH_CST9217 1
#define TOUCH_MIROIR 1       /* dalle tactile tournée d'un demi-tour (mirror_x et mirror_y du BSP) */
#define LCD_PAGE20   1       /* réglages de la page 0x20 du CO5300 envoyés avant la sortie de veille (BSP Waveshare) */
#define CARTE_TCA9554 0      /* relevé I2C du 29/10 : 18 ES8311, 34 AXP2101, 40 ES7210, 5a CST9217, 6b QMI8658 ; ni TCA9554
                                ni PCF85063 */
#define PIN_PWR      3       /* SYS_OUT du schéma : PWRON par un BSS138, haut tant que PWR est appuyé (tirage de 10 kΩ) */
#define CARTE_NOM    "1,75\" C"
#else
#define PIN_LCD_SCLK 38
#define PIN_LCD_RST  39
#define PIN_LCD_EN   -1      /* GPIO13 : entrée TE de l'écran, pas d'alimentation à piloter */
#define PIN_TP_RST   40
#define PIN_TP_INT   11      /* actif bas : aussi le réveil de la veille (doigt sur l'écran) */
#define TOUCH_CST9217 1
#define CARTE_TCA9554 1      /* extenseur 0x20 : P3 interruption du PCF85063, P4 bouton PWR (haut : appuyé), P5 AXP2101 */
#define CARTE_NOM    "1,75\""
#endif

#endif
