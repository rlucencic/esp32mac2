#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "emu/sdcard.h"
#include "rom_store.h"

#define ROM_PARTITION_TYPE  0x40
#define ROM_FOLDER          SDCARD_MOUNT_POINT "/roms"

static const char *TAG = "rom";

static const char *const partitions[] = { "rom_lc", "rom_ii" };
static const uint32_t sizes[] = { 512 * 1024, 256 * 1024 };

static bool from_partition(mac_model_t model, uint8_t *buf, uint32_t *size)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                           ROM_PARTITION_TYPE, partitions[model]);
    if (part == NULL || part->size < sizes[model]) {
        return false;
    }
    /* Through a mapping, not esp_partition_read(): that turns the caches
     * off while it reads, which stalls the LCD's refill (picture artefacts)
     */
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, 0, sizes[model], ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) {
        return false;
    }
    memcpy(buf, map, sizes[model]);
    esp_partition_munmap(handle);
    mac_model_t found;
    if (mac_rom_identify(buf, sizes[model], &found) == NULL || found != model) {
        return false;
    }
    *size = sizes[model];
    return true;
}

static bool from_sd(mac_model_t model, uint8_t *buf, uint32_t *size)
{
    DIR *dir = opendir(ROM_FOLDER);
    if (dir == NULL) {
        return false;
    }
    bool ok = false;
    struct dirent *e;
    while (!ok && (e = readdir(dir)) != NULL) {
        char path[300];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", ROM_FOLDER, e->d_name);
        if (e->d_name[0] == '.' || stat(path, &st) != 0 || st.st_size != sizes[model]) {
            continue;
        }
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            continue;
        }
        mac_model_t found;
        ok = fread(buf, 1, sizes[model], f) == sizes[model] &&
             mac_rom_identify(buf, sizes[model], &found) != NULL && found == model;
        fclose(f);
        if (ok) {
            ESP_LOGI(TAG, "ROM from %s", path);
        }
    }
    closedir(dir);
    if (ok) {
        *size = sizes[model];
    }
    return ok;
}

bool rom_store_load(mac_model_t model, uint8_t *buf, uint32_t *size, const char **name)
{
    if (model > MAC_MODEL_II) {
        return false;
    }
    if (!from_partition(model, buf, size) && !from_sd(model, buf, size)) {
        return false;
    }
    *name = mac_rom_identify(buf, *size, NULL);
    return true;
}

/* ---- The 8*24 card's declaration ROM: its format block names it ---- */

static bool is_card_rom(const uint8_t *buf)
{
    static const uint8_t tail[12] = { 0xD1, 0x62, 0x96, 0x64, 0x03, 0x01, 0x5A, 0x93, 0x2B, 0xC7, 0x00, 0x78 };
    return memcmp(buf + MAC_CARD_ROM_SIZE - sizeof(tail), tail, sizeof(tail)) == 0;
}

bool rom_store_load_card(uint8_t *buf)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                           ROM_PARTITION_TYPE, "rom_card");
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (part != NULL && part->size >= MAC_CARD_ROM_SIZE &&
        esp_partition_mmap(part, 0, MAC_CARD_ROM_SIZE, ESP_PARTITION_MMAP_DATA, &map, &handle) == ESP_OK) {
        memcpy(buf, map, MAC_CARD_ROM_SIZE);
        esp_partition_munmap(handle);
        if (is_card_rom(buf)) {
            return true;
        }
    }
    DIR *dir = opendir(ROM_FOLDER);
    if (dir == NULL) {
        return false;
    }
    bool ok = false;
    struct dirent *e;
    while (!ok && (e = readdir(dir)) != NULL) {
        char path[300];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", ROM_FOLDER, e->d_name);
        if (e->d_name[0] == '.' || stat(path, &st) != 0 || st.st_size != MAC_CARD_ROM_SIZE) {
            continue;
        }
        FILE *f = fopen(path, "rb");
        if (f != NULL) {
            ok = fread(buf, 1, MAC_CARD_ROM_SIZE, f) == MAC_CARD_ROM_SIZE && is_card_rom(buf);
            fclose(f);
            if (ok) {
                ESP_LOGI(TAG, "video card ROM from %s", path);
            }
        }
    }
    closedir(dir);
    return ok;
}
