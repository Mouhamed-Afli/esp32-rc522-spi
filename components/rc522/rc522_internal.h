#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "rc522.h"

typedef struct rc522_device {
    spi_device_handle_t spi_device;
    gpio_num_t reset_gpio;
    uint32_t command_timeout_ms;
    SemaphoreHandle_t mutex;
    bool initialized;
} rc522_device_t;

esp_err_t rc522_pcd_reset(rc522_device_t *device);
esp_err_t rc522_pcd_initialize(rc522_device_t *device);
esp_err_t rc522_pcd_get_version(rc522_device_t *device, uint8_t *version);
esp_err_t rc522_pcd_transceive(
    rc522_device_t *device,
    const uint8_t *tx_data,
    size_t tx_length,
    uint8_t tx_last_bits,
    uint8_t *rx_data,
    size_t rx_capacity,
    size_t *rx_length);
esp_err_t rc522_pcd_calculate_crc(
    rc522_device_t *device,
    const uint8_t *data,
    size_t length,
    uint8_t crc[2]);

esp_err_t rc522_picc_read_uid(rc522_device_t *device, rc522_uid_t *uid);
