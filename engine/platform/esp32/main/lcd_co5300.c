/*
 * Écran AMOLED CO5300 en QSPI : pilote par bandes, repris du projet tiny386 (lcd_co5300.c), adapté à l'écran
 * rond 466 × 466 (définition et taille des bandes, voir board.h).
 *
 * - SPI direct (sans esp_lcd) : commande = opcode 0x02 + adresse (cmd << 8) ; pixels = opcode 0x32 en QIO
 * - Coordonnées toujours paires (sinon le CO5300 laisse des lignes noires)
 * - Chaque bande : CASET + RASET + RAMWR mis en file (DMA), réserves de transactions persistantes, sans
 *   allocation pendant le rendu. La fenêtre de colonnes de la bande est rognée au cercle : on n'envoie que la
 *   plus grande corde de la bande, pas les coins hors de l'écran rond.
 * - Les deux cœurs envoient des bandes : un verrou protège la file SPI, et chaque envoi porte le numéro de son tampon
 *   (champ user de la transaction RAMWR) ; un cœur n'attend que la libération de son propre tampon.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "board.h"
#include "lcd_co5300.h"

#define OP_CMD   0x02
#define OP_COLOR 0x32
#define SLOTS    8           /* bandes en vol au plus */
#ifdef LCD_PAGE20
#define LCD_ATTENTE_MS 600   /* 1,75" C : attentes du BSP Waveshare, avant et après la sortie de veille */
#else
#define LCD_ATTENTE_MS 120
#endif

static const char *TAG = "lcd";
static spi_device_handle_t dev;
static spi_transaction_ext_t caset_pool[SLOTS], raset_pool[SLOTS], ramwr_pool[SLOTS];
static uint8_t caset_data[SLOTS][4] __attribute__((aligned(4))), raset_data[SLOTS][4] __attribute__((aligned(4)));
#define TX_PER_BAND 3
static int slot, pending;    /* transactions en file */
static SemaphoreHandle_t lock;
static volatile uint8_t busy[LCD_BUFFERS];   /* tampon en cours d'envoi */

static void cmd_sync(uint8_t cmd, const uint8_t *data, int len) {
    spi_transaction_ext_t t = {
        .base = { .flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR, .cmd = OP_CMD, .addr = cmd << 8,
                  .length = len * 8, .tx_buffer = len ? data : NULL },
        .command_bits = 8, .address_bits = 24,
    };
    spi_device_polling_transmit(dev, (spi_transaction_t *)&t);
}

/* Écran noir (la mémoire de l'écran démarre verte) */
static void ecran_noir(void) {
    uint16_t *buf = heap_caps_calloc(LCD_W * LCD_BAND, 2, MALLOC_CAP_DMA);
    if (!buf) return;
    for (int y = 0; y < LCD_H; y += LCD_BAND) { lcd_band_send(0, LCD_W, y, LCD_H - y < LCD_BAND ? LCD_H - y : LCD_BAND, buf, 0); lcd_wait_all(); }
    free(buf);
}

void lcd_init(void) {
#if PIN_LCD_EN >= 0   /* 2,06" : alimentation de l'écran ; 1,75" : rien à piloter */
    gpio_config_t en = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_LCD_EN };
    gpio_config(&en);
    gpio_set_level(PIN_LCD_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
    gpio_config_t rst = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_LCD_RST };
    gpio_config(&rst);
    gpio_set_level(PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK, .data0_io_num = PIN_LCD_D0, .data1_io_num = PIN_LCD_D1,
        .data2_io_num = PIN_LCD_D2, .data3_io_num = PIN_LCD_D3,
        .max_transfer_sz = LCD_W * LCD_BAND * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dc = {
        .command_bits = 8, .address_bits = 24, .mode = 0,
        .clock_speed_hz = CONFIG_ECHOES_LCD_MHZ * 1000 * 1000,
        .spics_io_num = PIN_LCD_CS, .queue_size = SLOTS * TX_PER_BAND + 2,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dc, &dev));
    lock = xSemaphoreCreateMutex();

#ifdef LCD_PAGE20
    /* 1,75" C : séquence du BSP Waveshare (esp32_s3_touch_amoled_1_75c.c, lcd_init_cmds) : page 0x20, tous les réglages
       avant la sortie de veille, ses longues attentes (LCD_ATTENTE_MS), puis l'image avant l'affichage (fiche technique du
       CO5300). Celle de tiny386 (sortie de veille d'abord, 120 ms, réglages, affichage) laissait parfois l'écran noir :
       PWR plusieurs fois avant l'image, Game Boy noir au lancement (31/10/2026). Le CO5300 de la carte ne se relit pas
       (SIO0 rend 0, et le mode 3 fils bloque les envois QIO) : pas de contrôle de l'allumage possible */
    cmd_sync(0xFE, (const uint8_t[]){ 0x20 }, 1);
    cmd_sync(0x19, (const uint8_t[]){ 0x10 }, 1);
    cmd_sync(0x1C, (const uint8_t[]){ 0xA0 }, 1);
    cmd_sync(0xFE, (const uint8_t[]){ 0x00 }, 1);
    cmd_sync(0xC4, (const uint8_t[]){ 0x80 }, 1);
    cmd_sync(0x36, (const uint8_t[]){ 0x00 }, 1);
    cmd_sync(0x3A, (const uint8_t[]){ 0x55 }, 1);
    cmd_sync(0x53, (const uint8_t[]){ 0x20 }, 1);
    cmd_sync(0x51, (const uint8_t[]){ 0x00 }, 1);
    cmd_sync(0x63, (const uint8_t[]){ 0xFF }, 1);
    vTaskDelay(pdMS_TO_TICKS(LCD_ATTENTE_MS));
    cmd_sync(0x11, NULL, 0); vTaskDelay(pdMS_TO_TICKS(LCD_ATTENTE_MS));
    ecran_noir();
    cmd_sync(0x29, NULL, 0); vTaskDelay(pdMS_TO_TICKS(20));
    lcd_set_brightness(0xD0);
#else
    /* Séquence d'initialisation du projet tiny386 (sortie de veille, mode QSPI, RGB565) */
    cmd_sync(0x11, NULL, 0); vTaskDelay(pdMS_TO_TICKS(120));
    cmd_sync(0xFE, (const uint8_t[]){ 0x00 }, 1);
    cmd_sync(0xC4, (const uint8_t[]){ 0x80 }, 1);
    cmd_sync(0x36, (const uint8_t[]){ 0x00 }, 1);
    cmd_sync(0x3A, (const uint8_t[]){ 0x55 }, 1);
    cmd_sync(0x53, (const uint8_t[]){ 0x20 }, 1);
    cmd_sync(0x63, (const uint8_t[]){ 0xFF }, 1);
    cmd_sync(0x29, NULL, 0); vTaskDelay(pdMS_TO_TICKS(20));
    lcd_set_brightness(0xD0);
    ecran_noir();
#endif
    ESP_LOGI(TAG, "CO5300 prêt : %d x %d, QSPI %d MHz", LCD_W, LCD_H, CONFIG_ECHOES_LCD_MHZ);
}

void lcd_set_brightness(uint8_t level) { cmd_sync(0x51, &level, 1); }

/* Veille (comme lcd_sleep / lcd_wake du projet tiny386) : l'AMOLED n'a pas de rétroéclairage, DISPOFF et SLPIN coupent
   l'affichage ; sa mémoire est gardée, mais l'image suivante est redessinée de toute façon */
void lcd_dormir(void) {
    lcd_wait_all();
    cmd_sync(0x28, NULL, 0); vTaskDelay(pdMS_TO_TICKS(20));
    cmd_sync(0x10, NULL, 0); vTaskDelay(pdMS_TO_TICKS(120));
}
void lcd_reveiller(void) {
    cmd_sync(0x11, NULL, 0); vTaskDelay(pdMS_TO_TICKS(LCD_ATTENTE_MS));
    cmd_sync(0x29, NULL, 0); vTaskDelay(pdMS_TO_TICKS(20));
}

/* Récupère une transaction terminée (verrou tenu) et libère son tampon s'il s'agit d'un RAMWR */
static void reap_one(void) {
    spi_transaction_t *done;
    if (spi_device_get_trans_result(dev, &done, pdMS_TO_TICKS(200)) != ESP_OK) {
        ESP_LOGW(TAG, "envoi bloqué, file remise à zéro");
        pending = 0;
        memset((void *)busy, 0, sizeof busy);
        return;
    }
    pending--;
    if (done->user) busy[(intptr_t)done->user - 1] = 0;
}

void lcd_wait_all(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    while (pending > 0) reap_one();
    xSemaphoreGive(lock);
}

void lcd_buffer_wait(int tag) {
    while (busy[tag]) {
        xSemaphoreTake(lock, portMAX_DELAY);
        if (busy[tag] && pending > 0) reap_one();
        xSemaphoreGive(lock);
    }
}

void lcd_band_send(int x0, int x1, int y, int h, const uint16_t *buf, int tag) {
    xSemaphoreTake(lock, portMAX_DELAY);
    int cx0 = x0 + LCD_X_OFFSET, cx1 = x1 + LCD_X_OFFSET;
    uint8_t *c = caset_data[slot];
    c[0] = cx0 >> 8; c[1] = cx0 & 255; c[2] = (cx1 - 1) >> 8; c[3] = (cx1 - 1) & 255;
    y += LCD_Y_OFFSET;
    int y1 = y + h;
    uint8_t *d = raset_data[slot];
    d[0] = y >> 8; d[1] = y & 255; d[2] = (y1 - 1) >> 8; d[3] = (y1 - 1) & 255;

    spi_transaction_ext_t *k = &caset_pool[slot], *r = &raset_pool[slot], *w = &ramwr_pool[slot];
    memset(k, 0, sizeof *k); memset(r, 0, sizeof *r); memset(w, 0, sizeof *w);
    k->base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    k->base.cmd = OP_CMD; k->base.addr = 0x2A << 8; k->base.length = 32; k->base.tx_buffer = c;
    k->command_bits = 8; k->address_bits = 24;
    r->base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    r->base.cmd = OP_CMD; r->base.addr = 0x2B << 8; r->base.length = 32; r->base.tx_buffer = d;
    r->command_bits = 8; r->address_bits = 24;
    w->base.flags = SPI_TRANS_MODE_QIO;
    w->base.cmd = OP_COLOR; w->base.addr = 0x2C << 8; w->base.length = (x1 - x0) * h * 16; w->base.tx_buffer = buf;
    w->base.user = (void *)(intptr_t)(tag + 1);
    w->command_bits = 8; w->address_bits = 24;
    busy[tag] = 1;
    spi_device_queue_trans(dev, (spi_transaction_t *)k, portMAX_DELAY);
    spi_device_queue_trans(dev, (spi_transaction_t *)r, portMAX_DELAY);
    spi_device_queue_trans(dev, (spi_transaction_t *)w, portMAX_DELAY);
    pending += TX_PER_BAND;
    slot = (slot + 1) % SLOTS;
    xSemaphoreGive(lock);
}
