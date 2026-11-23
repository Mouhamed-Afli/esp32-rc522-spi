#include <stdio.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc522.h"

static const char *TAG = "rc522_example";

void app_main(void)
{
    const spi_host_device_t spi_host = SPI2_HOST;
    const spi_bus_config_t bus_config = {
        .mosi_io_num = 23,
        .miso_io_num = 19,
        .sclk_io_num = 18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(spi_host, &bus_config, SPI_DMA_CH_AUTO));

    rc522_config_t reader_config = RC522_DEFAULT_CONFIG(spi_host, GPIO_NUM_5);
    // Set reset_gpio to GPIO_NUM_22 when the module's RST pin is wired there.
    reader_config.reset_gpio = GPIO_NUM_22;

    rc522_handle_t reader = NULL;
    ESP_ERROR_CHECK(rc522_create(&reader_config, &reader));
    ESP_ERROR_CHECK(rc522_init(reader));

    uint8_t version = 0;
    ESP_ERROR_CHECK(rc522_get_version(reader, &version));
    ESP_LOGI(TAG, "MFRC522 version: 0x%02X", version);

    while (true) {
        rc522_uid_t uid = {0};
        esp_err_t err = rc522_read_uid(reader, &uid);
        if (err == ESP_OK) {
            char uid_string[RC522_UID_MAX_LENGTH * 3 + 1] = {0};
            size_t offset = 0;
            for (size_t i = 0; i < uid.length; i++) {
                int written = snprintf(
                    &uid_string[offset],
                    sizeof(uid_string) - offset,
                    i == 0 ? "%02X" : ":%02X",
                    uid.bytes[i]);
                if (written > 0) {
                    offset += (size_t)written;
                }
            }
            ESP_LOGI(TAG, "Card UID: %s", uid_string);
        } else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Card polling failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
