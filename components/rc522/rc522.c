#include <stdlib.h>

#include "esp_check.h"

#include "rc522_internal.h"

esp_err_t rc522_create(const rc522_config_t *config, rc522_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL || config->cs_gpio == GPIO_NUM_NC ||
        config->clock_speed_hz <= 0 || config->command_timeout_ms == 0 ||
        config->command_timeout_ms > UINT32_MAX / 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;

    rc522_device_t *device = calloc(1, sizeof(*device));
    if (device == NULL) {
        return ESP_ERR_NO_MEM;
    }
    device->reset_gpio = config->reset_gpio;
    device->command_timeout_ms = config->command_timeout_ms;
    device->mutex = xSemaphoreCreateMutex();
    if (device->mutex == NULL) {
        free(device);
        return ESP_ERR_NO_MEM;
    }

    if (device->reset_gpio != GPIO_NUM_NC) {
        esp_err_t err = gpio_set_direction(device->reset_gpio, GPIO_MODE_OUTPUT);
        if (err != ESP_OK) {
            vSemaphoreDelete(device->mutex);
            free(device);
            return err;
        }
    }

    spi_device_interface_config_t spi_config = {
        .mode = 0,
        .clock_speed_hz = config->clock_speed_hz,
        .spics_io_num = config->cs_gpio,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_add_device(config->spi_host, &spi_config, &device->spi_device);
    if (err != ESP_OK) {
        vSemaphoreDelete(device->mutex);
        free(device);
        return err;
    }

    *out_handle = device;
    return ESP_OK;
}

esp_err_t rc522_init(rc522_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = rc522_pcd_initialize(handle);
    if (err == ESP_OK) {
        handle->initialized = true;
    }
    xSemaphoreGive(handle->mutex);
    return err;
}

esp_err_t rc522_get_version(rc522_handle_t handle, uint8_t *version)
{
    if (handle == NULL || version == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = rc522_pcd_get_version(handle, version);
    xSemaphoreGive(handle->mutex);
    return err;
}

esp_err_t rc522_read_uid(rc522_handle_t handle, rc522_uid_t *uid)
{
    if (handle == NULL || uid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = handle->initialized
                        ? rc522_picc_read_uid(handle, uid)
                        : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(handle->mutex);
    return err;
}

esp_err_t rc522_delete(rc522_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = spi_bus_remove_device(handle->spi_device);
    if (err != ESP_OK) {
        return err;
    }
    vSemaphoreDelete(handle->mutex);
    free(handle);
    return ESP_OK;
}
