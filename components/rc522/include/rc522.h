#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC522_UID_MAX_LENGTH 10

typedef struct rc522_device *rc522_handle_t;

typedef struct {
    uint8_t bytes[RC522_UID_MAX_LENGTH];
    size_t length;
} rc522_uid_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t cs_gpio;
    gpio_num_t reset_gpio;
    int clock_speed_hz;
    uint32_t command_timeout_ms;
} rc522_config_t;

#define RC522_DEFAULT_CONFIG(host, cs) \
    {                                   \
        .spi_host = (host),             \
        .cs_gpio = (cs),                \
        .reset_gpio = GPIO_NUM_NC,      \
        .clock_speed_hz = 4000000,      \
        .command_timeout_ms = 25,       \
    }

/**
 * @brief Create an RC522 device and attach it to an initialized SPI bus.
 *
 * The caller owns the SPI bus and must keep it initialized until the device is
 * deleted. The component owns the SPI device handle and configures the optional
 * reset GPIO.
 */
esp_err_t rc522_create(const rc522_config_t *config, rc522_handle_t *out_handle);

/**
 * @brief Reset and initialize the MFRC522, then enable its RF antenna.
 */
esp_err_t rc522_init(rc522_handle_t handle);

/**
 * @brief Read the silicon version register.
 *
 * This can be used to confirm basic SPI communication before card polling.
 */
esp_err_t rc522_get_version(rc522_handle_t handle, uint8_t *version);

/**
 * @brief Poll for and select one ISO/IEC 14443 Type A card, returning its UID.
 *
 * Supports single-card 4, 7, and 10-byte UIDs. Returns ESP_ERR_NOT_FOUND when
 * no card responds during polling.
 */
esp_err_t rc522_read_uid(rc522_handle_t handle, rc522_uid_t *uid);

/**
 * @brief Detach the SPI device and release component-owned resources.
 *
 * Do not call concurrently with any other operation on this handle.
 */
esp_err_t rc522_delete(rc522_handle_t handle);

#ifdef __cplusplus
}
#endif
