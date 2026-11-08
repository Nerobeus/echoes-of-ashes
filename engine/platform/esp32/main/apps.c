/*
 * Applications de la montre (table commune de partitions.csv) : « montre » (l'accueil), « echoes » (ce firmware),
 * « pcdos » (tiny386) et « gameboy » (gnuboy, gameboy/ du dépôt). Le chargeur de démarrage lance celle que otadata désigne ; app_lancer la change puis redémarre.
 * L'heure est gardée en mémoire RTC (horloge_garder). Lire ou écrire la flash coupe le cache : jamais depuis une tâche
 * dont la pile est en PSRAM ; tout se fait donc dans une petite tâche à pile interne, quelle que soit la tâche appelante.
 * Drapeaux passés d'une application à l'autre (NVS, espace « apps », lus une fois) : « veille » (l'app montre démarre
 * écran éteint, en veille, après la mise en veille d'Echoes, du PC DOS ou du Game Boy), « reprise » (Echoes reprend la dernière
 * partie sans l'écran titre).
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "horloge.h"
#include "apps.h"

static const char *TAG = "apps";

/* lancer : 0 vérifier, 1 lancer ; drapeaux : posés avant de lancer (lancer), ou lus et effacés (lire) */
typedef struct { const char *nom; int lancer, r; SemaphoreHandle_t fait; const char *poser[2]; const char *lire; } demande_t;

static void drapeaux(demande_t *d) {
    if (!d->poser[0] && !d->lire) return;
    nvs_flash_init();   /* sans effet s'il l'est déjà */
    nvs_handle_t h;
    if (nvs_open("apps", NVS_READWRITE, &h) != ESP_OK) return;
    for (int k = 0; k < 2; k++) if (d->poser[k]) nvs_set_u8(h, d->poser[k], 1);
    if (d->lire) {
        uint8_t v = 0;
        d->r = nvs_get_u8(h, d->lire, &v) == ESP_OK && v;
        if (d->r) nvs_erase_key(h, d->lire);
    }
    nvs_commit(h);
    nvs_close(h);
}

static void tache(void *p) {
    demande_t *d = p;
    if (d->lire) { drapeaux(d); xSemaphoreGive(d->fait); vTaskDelete(NULL); return; }
    const esp_partition_t *pt = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, d->nom);
    esp_app_desc_t desc;
    d->r = pt && esp_ota_get_partition_description(pt, &desc) == ESP_OK;
    if (d->r && d->lancer) {
        drapeaux(d);
        const esp_err_t e = esp_ota_set_boot_partition(pt);
        if (e == ESP_OK) {
            ESP_LOGI(TAG, "redémarrage dans %s", d->nom);
            vTaskDelay(pdMS_TO_TICKS(50));
            horloge_garder();
            esp_restart();
        }
        ESP_LOGE(TAG, "%s : %s", d->nom, esp_err_to_name(e));
        d->r = 0;
    }
    xSemaphoreGive(d->fait);
    vTaskDelete(NULL);
}
/* Attente par un sémaphore à soi (pas une notification de la tâche appelante : l'écran du Game Boy ou le tactile de l'app
   montre en reçoivent sans cesse, et l'attente finissait avant la réponse, sur une demande encore lue par la petite
   tâche dans la pile de l'appelante) */
static int demander(demande_t *d) {
    StaticSemaphore_t s;
    d->fait = xSemaphoreCreateBinaryStatic(&s);
    /* La pile de la petite tâche d'avant (app_presente juste avant app_lancer) n'est rendue que par la tâche inactive :
       nouvel essai toutes les 50 ms, 1 s au plus (04/11/2026 : mémoire interne presque pleine après un eng_init en échec,
       la seconde tâche n'était pas créée, le retour à la montre échouait, « pas d'application », et l'écran restait noir) */
    int t = 0;
    while (xTaskCreatePinnedToCore(tache, "apps", 4096, d, 6, NULL, 0) != pdPASS) {
        if (++t >= 20) { ESP_LOGE(TAG, "%s : petite tâche impossible à créer (mémoire interne)", d->nom ? d->nom : "drapeaux"); return 0; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    xSemaphoreTake(d->fait, portMAX_DELAY);
    return d->r;
}

int app_presente(const char *nom) { demande_t d = { .nom = nom }; return demander(&d); }
int app_lancer(const char *nom) {
    demande_t d = { .nom = nom, .lancer = 1 };
    if (!demander(&d)) { ESP_LOGE(TAG, "%s : pas d'application dans cette partition", nom); return 0; }
    return 1;
}
int app_veille(const char *reprise) {
    demande_t d = { .nom = "montre", .lancer = 1, .poser = { "veille", reprise } };
    if (!demander(&d)) { ESP_LOGE(TAG, "veille : pas d'app montre"); return 0; }
    return 1;
}
int app_drapeau(const char *cle) { demande_t d = { .lire = cle }; return demander(&d); }
