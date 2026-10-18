#include "esp_log.h"
#include "nvs.h"
#include "settings.h"

#define NAMESPACE   "mac2"

static const char *TAG = "settings";

settings_t settings;

static bool valid_ram(uint16_t mb)
{
    return mb == 2 || mb == 4 || mb == 6 || mb == 10;
}

void settings_load(void)
{
    settings.ram_mb = 4;
    settings.monitor = MAC_MONITOR_13;

    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t u8;
    uint16_t u16;
    if (nvs_get_u16(h, "ram_mb", &u16) == ESP_OK && valid_ram(u16)) {
        settings.ram_mb = u16;
    }
    if (nvs_get_u8(h, "monitor", &u8) == ESP_OK && u8 <= MAC_MONITOR_12) {
        settings.monitor = u8;
    }
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "can't open NVS");
        return;
    }
    nvs_set_u16(h, "ram_mb", settings.ram_mb);
    nvs_set_u8(h, "monitor", settings.monitor);
    nvs_commit(h);
    nvs_close(h);
}

bool settings_load_pram(uint8_t *pram)
{
    nvs_handle_t h;
    size_t size = MAC_PRAM_SIZE;
    bool ok = false;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_blob(h, "pram", pram, &size) == ESP_OK && size == MAC_PRAM_SIZE;
        nvs_close(h);
    }
    return ok;
}

void settings_save_pram(const uint8_t *pram)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "pram", pram, MAC_PRAM_SIZE);
        nvs_commit(h);
        nvs_close(h);
    }
}
