# Pilotes communs avec Echoes (carte 1,75" C validée) : écran, tactile, alimentation, carte SD, heure, applications ;
# interface de la montre commune avec le menu du PC DOS (commun/ui_montre : dessin, polices, thème, pages)
set(echoes ../../engine/platform/esp32/main)
set(ui ../../commun/ui_montre)
idf_component_register(
    SRCS montre.c ble.c usb.c
         ${ui}/dessin.c ${ui}/police.c ${ui}/theme.c ${ui}/pages.c
         ${echoes}/lcd_co5300.c ${echoes}/touch.c ${echoes}/board_power.c ${echoes}/sd.c ${echoes}/horloge.c ${echoes}/apps.c
    INCLUDE_DIRS . ${ui} ${echoes}
    REQUIRES driver esp_partition esp_timer fatfs sdmmc vfs hal nvs_flash bt app_update esp_pm)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
