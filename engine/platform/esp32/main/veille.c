/*
 * Veille de la montre (inspirée de power_management.c du projet tiny386). La tâche du jeu appelle veille_image à chaque
 * image, entrées lues :
 * - BOOT : appui court = menu (au relâchement, main.c), appui long (VEILLE_BOOT_MS) = veille ; PWR (1,75", lu sur le
 *   TCA9554) : appui court = veille ;
 * - sans doigt ni bouton pendant CONFIG_ECHOES_VEILLE_S secondes : veille (l'écran baisse CONFIG_ECHOES_VEILLE_ATTENUER_S
 *   secondes avant ; toucher le rallume) ;
 * - batterie sous CONFIG_ECHOES_BATTERIE_MIN % sans USB : sauvegarde, puis l'AXP2101 coupe tout.
 * Veille : l'image en cours s'achève, le son et l'écran se coupent d'abord (rien ne se voit de la suite), puis la
 * partie est sauvegardée (eng_sauvegarder : pas en combat, course, scène ni dialogue) ; sauvegardée, la montre passe à
 * l'app montre, qui démarre écran éteint et s'endort (app_veille : son cadran au réveil ; Echoes relancé reprend la
 * partie sans l'écran titre). Sans sauvegarde possible (combat, scène…), veille sur place : sur batterie, veille légère : RAM et PSRAM gardées,
 * réveil par BOOT (broche basse) ou PWR (1,75" C : GPIO3 haut), et toutes les VEILLE_TICK_MS pour lire PWR et l'USB ; branchée en
 * USB, l'écran seulement s'éteint (la copie par l'USB, sdx.c, reste possible : la veille légère couperait l'USB). Au
 * réveil, l'écran se rallume et le moteur passe en mode verrouillé (cadran de la montre, déverrouillage par un
 * glissement : eng_verrouiller).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "board.h"
#include "board_power.h"
#include "lcd_co5300.h"
#include "touch.h"
#include "eng.h"
#include "veille.h"
#include "apps.h"
#include "manette.h"

static const char *TAG = "veille";

#define VEILLE_BOOT_MS 1500       /* appui long sur BOOT */
#ifdef PIN_PWR
#define VEILLE_TICK_MS 2000       /* veille légère : PWR réveille par sa broche ; le tic ne sert qu'à l'USB et la batterie */
#else
#define VEILLE_TICK_MS 300        /* veille légère : réveil pour lire PWR (extenseur ou AXP2101), l'USB et la batterie */
#endif
#define VERROU_S 15               /* cadran verrouillé laissé sans action : retour en veille */
#define BATTERIE_PERIODE_US 10000000

static void (*finir_image)(void);
static void (*demander_lum)(int);  /* luminosité appliquée par main.c entre deux images (pas pendant l'envoi des bandes) */
static uint8_t *luminosite;        /* luminosité réglée (main.c), rendue au réveil */
static int64_t derniere, boot_t0, batt_t, pwr_muet;
#define PWR_MUET_US 500000        /* PWR ignoré une demi-seconde après le réveil */
static int boot_avant;
#if !CONFIG_ECHOES_BANC
static int attenue, pwr_avant;
#endif

void veille_init(void (*finir)(void), uint8_t *lum, void (*demander)(int)) {
    finir_image = finir; luminosite = lum; demander_lum = demander;
    derniere = batt_t = esp_timer_get_time();
    /* PWR qui a allumé la montre, encore enfoncé ou qui rebondit : pas une demande de veille */
    pwr_avant = alim_bouton_pwr();
    pwr_muet = derniere + 2 * PWR_MUET_US;
    /* En veille légère, l'ESP-IDF isole les broches (entrées flottantes) : l'amplificateur et les resets de l'écran et
       du tactile gardent leur niveau, sinon le son repart écran éteint */
    const int garder[] = { PIN_LCD_RST, PIN_TP_RST, PIN_LCD_CS,
#if CONFIG_ECHOES_AUDIO
                           CONFIG_ECHOES_PA_GPIO,
#endif
    };
    for (unsigned k = 0; k < sizeof garder / sizeof garder[0]; k++) gpio_sleep_sel_dis(garder[k]);
}

/* Batterie presque vide sans USB : sauvegarde, puis coupure (rien ne revient de alim_eteindre) */
static void batterie(int64_t now) {
    if (now - batt_t < BATTERIE_PERIODE_US || CONFIG_ECHOES_BATTERIE_MIN <= 0) return;
    batt_t = now;
    const int pct = alim_batterie_pct();
    eng_set_batterie(pct, alim_usb());
    if (pct < 0 || pct > CONFIG_ECHOES_BATTERIE_MIN || alim_usb()) return;
    ESP_LOGW(TAG, "batterie à %d %% : sauvegarde et extinction", pct);
    eng_sauvegarder();
    vTaskDelay(pdMS_TO_TICKS(200));
    alim_eteindre();
}

static int delai_s = CONFIG_ECHOES_VEILLE_S;   /* sans action pendant ce temps : veille (0 : jamais ; réglages.c) */
void veille_delai(int s) { delai_s = s; }
static volatile int demandee;
void veille_demander(void) { demandee = 1; }
static volatile int reveil_pc;   /* SDX REVEIL : réveil demandé depuis le PC (essais de la veille sans toucher la montre) */
void veille_reveiller(void) { reveil_pc = 1; }

#if !CONFIG_ECHOES_BANC
/* Ce qui réveille : 1 BOOT, 2 doigt (INT du tactile), 4 PWR, 8 le PC (SDX REVEIL) */
/* Le doigt ne réveille pas (demande de l'utilisateur : la montre au poignet se rallumait sans cesse) ; bit 2 gardé
   pour le journal */
static int reveil(void) { return (gpio_get_level(CONFIG_ECHOES_BOOT_GPIO) ? 0 : 1) | alim_bouton_pwr() << 2 | (reveil_pc ? 8 : 0); }

static void dormir(int cause) {
    finir_image();                         /* plus rien à l'écran ni dans la file SPI */
    const float vol = eng_volume();
    eng_set_volume(0);
#if CONFIG_ECHOES_AUDIO
    gpio_set_level(CONFIG_ECHOES_PA_GPIO, 0);
#endif
    lcd_set_brightness(0);                 /* écran éteint d'abord : la sauvegarde ne se voit pas */
    lcd_dormir();
    const int sauve = eng_sauvegarder();
    /* Partie sauvegardée : l'app montre prend la veille (écran éteint, puis son cadran au réveil) ; Echoes, relancé,
       reprendra cette partie */
    if (sauve) {
        ESP_LOGI(TAG, "veille : partie sauvegardée, retour à l'app montre");
        app_veille("reprise");             /* ne revient pas s'il y a une app montre */
    }
    /* Branchée sur un PC (trames USB reçues) : l'écran seul s'éteint, la console et la copie par l'USB restent ; sur
       un chargeur ou la batterie, veille légère (l'USB du PC ne survivrait pas à la veille légère) */
    const int pc = usb_serial_jtag_is_connected();
    if (cause & 2) ESP_LOGI(TAG, "PWR : touche de l'AXP2101 0x%x", alim_pwr_axp());
    ESP_LOGI(TAG, "veille par%s%s%s%s%s%s (VBUS de l'AXP2101 : %d)", cause & 1 ? " BOOT" : "", cause & 2 ? " PWR" : "", cause & 4 ? " le PC" : "", cause & 8 ? " inactivité" : "",
             sauve ? " (partie sauvegardée)" : "", pc ? ", PC branché : écran éteint seulement" : "", alim_usb());
    /* Boutons et doigt relâchés depuis 300 ms, sinon le réveil serait immédiat (PWR rebondit au relâchement : SYS_OUT
       de la 1,75" C n'est pas filtré) */
    reveil_pc = 0;
    for (int calme = 0; calme < 15; ) { calme = reveil() ? 0 : calme + 1; vTaskDelay(pdMS_TO_TICKS(20)); }
    const bool bt = manette_active();   /* manette : pas de veille légère avec le Bluetooth, relancée au réveil */
    manette_veille();   /* contrôleur gardé avec sa SRAM, repris au réveil */
    for (;;) {
        if (usb_serial_jtag_is_connected() || alim_usb()) vTaskDelay(pdMS_TO_TICKS(50));   /* PC ou câble USB : pas de veille légère (l'USB doit pouvoir s'établir) */
        else {
            gpio_wakeup_enable(CONFIG_ECHOES_BOOT_GPIO, GPIO_INTR_LOW_LEVEL);
#ifdef PIN_PWR
            gpio_wakeup_enable(PIN_PWR, GPIO_INTR_HIGH_LEVEL);   /* 1,75" C : PWR réveille sans attendre le tic suivant */
#endif
            esp_sleep_enable_gpio_wakeup();
            esp_sleep_enable_timer_wakeup((uint64_t)VEILLE_TICK_MS * 1000);
            esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);   /* PSRAM gardée */
            esp_light_sleep_start();
            gpio_wakeup_disable(CONFIG_ECHOES_BOOT_GPIO);
#ifdef PIN_PWR
            gpio_wakeup_disable(PIN_PWR);
#endif
        }
        int r = reveil();
        if (r & 4) { vTaskDelay(pdMS_TO_TICKS(30)); if (!alim_bouton_pwr()) r &= ~4; else ESP_LOGI(TAG, "PWR : touche de l'AXP2101 0x%x", alim_pwr_axp()); }   /* un vrai appui, pas un rebond */
        if (r) { ESP_LOGI(TAG, "réveil par%s%s%s%s", r & 1 ? " BOOT" : "", r & 2 ? " le tactile" : "", r & 4 ? " PWR" : "", r & 8 ? " le PC" : ""); reveil_pc = 0; break; }
        batterie(esp_timer_get_time());
    }
    lcd_reveiller();
    if (luminosite) lcd_set_brightness(*luminosite);
#if CONFIG_ECHOES_AUDIO
    gpio_set_level(CONFIG_ECHOES_PA_GPIO, 1);
#endif
    eng_set_volume(vol);
    if (bt) manette_demarrer();
    eng_verrouiller();
    /* Le doigt ou le bouton du réveil relâché : il ne devient pas une entrée du jeu */
    while (!gpio_get_level(CONFIG_ECHOES_BOOT_GPIO) || alim_bouton_pwr()) vTaskDelay(pdMS_TO_TICKS(20));
    derniere = esp_timer_get_time(); attenue = 0; boot_t0 = 0; pwr_avant = 0; boot_avant = 0;
    pwr_muet = derniere + PWR_MUET_US;   /* les rebonds du relâchement ne rendorment pas la montre */
    ESP_LOGI(TAG, "réveil");
}
#endif

int veille_image(int boot, int doigt) {
#if CONFIG_ECHOES_BANC
    (void)boot; (void)doigt;
    batterie(esp_timer_get_time());   /* banc : batterie lue et affichée, pas de veille */
    return 0;
#else
    const int64_t now = esp_timer_get_time();
    batterie(now);
    if (boot || doigt) {
        derniere = now;
        if (attenue && luminosite) { demander_lum(*luminosite); attenue = 0; }
    }
    /* BOOT tenu longtemps */
    if (boot && !boot_avant) boot_t0 = now;
    boot_avant = boot;
    int dort = boot && boot_t0 && now - boot_t0 >= (int64_t)VEILLE_BOOT_MS * 1000 ? 1 : 0;
    /* PWR : appui (front montant) */
    const int pwr = alim_bouton_pwr();
    if (pwr && !pwr_avant && now >= pwr_muet) dort |= 2;
    pwr_avant = pwr;
    if (demandee) { demandee = 0; dort |= 4; }
    if (delai_s > 0) {
        /* Verrouillée (cadran), elle se rendort après VERROU_S secondes sans action */
        const int lim = eng_verrouille() && VERROU_S < delai_s ? VERROU_S : delai_s;
        const int64_t inactif = now - derniere;
        if (inactif >= (int64_t)lim * 1000000) dort |= 8;
        else if (!attenue && CONFIG_ECHOES_VEILLE_ATTENUER_S > 0 && CONFIG_ECHOES_VEILLE_ATTENUER_S < lim && inactif >= (int64_t)(lim - CONFIG_ECHOES_VEILLE_ATTENUER_S) * 1000000) {
            demander_lum(24); attenue = 1;
        }
    }
    if (!dort) return 0;
    dormir(dort);
    return 1;
#endif
}

int veille_boot_long(void) { return boot_t0 && boot_avant && esp_timer_get_time() - boot_t0 >= 1000000; }
