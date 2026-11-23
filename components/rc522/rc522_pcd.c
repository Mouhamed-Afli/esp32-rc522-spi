#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_check.h"

#include "rc522_internal.h"

static const char *TAG = "rc522_pcd";

enum {
    REG_COMMAND = 0x01,
    REG_COM_IRQ = 0x04,
    REG_DIV_IRQ = 0x05,
    REG_ERROR = 0x06,
    REG_FIFO_DATA = 0x09,
    REG_FIFO_LEVEL = 0x0A,
    REG_BIT_FRAMING = 0x0D,
    REG_TX_CONTROL = 0x14,
    REG_TX_ASK = 0x15,
    REG_MODE = 0x11,
    REG_TX_MODE = 0x12,
    REG_RX_MODE = 0x13,
    REG_MOD_WIDTH = 0x24,
    REG_T_MODE = 0x2A,
    REG_T_PRESCALER = 0x2B,
    REG_T_RELOAD_H = 0x2C,
    REG_T_RELOAD_L = 0x2D,
    REG_CRC_RESULT_H = 0x21,
    REG_CRC_RESULT_L = 0x22,
    REG_VERSION = 0x37,
};

enum {
    PCD_IDLE = 0x00,
    PCD_CALC_CRC = 0x03,
    PCD_TRANSCEIVE = 0x0C,
    PCD_SOFT_RESET = 0x0F,
};

static esp_err_t rc522_write_register(
    rc522_device_t *device,
    uint8_t reg,
    const uint8_t *data,
    size_t length)
{
    uint8_t tx_data[2];
    spi_transaction_t transaction = {0};

    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (length == 0 || length > 64) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx_data[0] = (uint8_t)((reg << 1) & 0x7E);
    transaction.tx_buffer = tx_data;
    transaction.length = sizeof(tx_data) * 8;
    for (size_t i = 0; i < length; i++) {
        tx_data[1] = data[i];
        esp_err_t err = spi_device_polling_transmit(device->spi_device, &transaction);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t rc522_read_register(
    rc522_device_t *device,
    uint8_t reg,
    uint8_t *data,
    size_t length)
{
    uint8_t tx_data[2] = {0};
    uint8_t rx_data[2] = {0};
    spi_transaction_t transaction = {0};

    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (length == 0 || length > 64) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx_data[0] = (uint8_t)(((reg << 1) & 0x7E) | 0x80);
    transaction.length = sizeof(tx_data) * 8;
    transaction.tx_buffer = tx_data;
    transaction.rx_buffer = rx_data;
    for (size_t i = 0; i < length; i++) {
        rx_data[0] = 0;
        rx_data[1] = 0;
        esp_err_t err = spi_device_polling_transmit(device->spi_device, &transaction);
        if (err != ESP_OK) {
            return err;
        }
        data[i] = rx_data[1];
    }
    return ESP_OK;
}

static esp_err_t rc522_write_reg(rc522_device_t *device, uint8_t reg, uint8_t value)
{
    return rc522_write_register(device, reg, &value, 1);
}

static esp_err_t rc522_read_reg(rc522_device_t *device, uint8_t reg, uint8_t *value)
{
    return rc522_read_register(device, reg, value, 1);
}

static esp_err_t rc522_set_bits(rc522_device_t *device, uint8_t reg, uint8_t mask)
{
    uint8_t value;
    esp_err_t err = rc522_read_reg(device, reg, &value);
    if (err != ESP_OK) {
        return err;
    }
    return rc522_write_reg(device, reg, (uint8_t)(value | mask));
}

esp_err_t rc522_pcd_reset(rc522_device_t *device)
{
    if (device->reset_gpio != GPIO_NUM_NC) {
        esp_err_t err = gpio_set_level(device->reset_gpio, 0);
        if (err != ESP_OK) {
            return err;
        }
        esp_rom_delay_us(1000);
        err = gpio_set_level(device->reset_gpio, 1);
        if (err != ESP_OK) {
            return err;
        }
        esp_rom_delay_us(50000);
        return ESP_OK;
    }

    esp_err_t err = rc522_write_reg(device, REG_COMMAND, PCD_SOFT_RESET);
    if (err != ESP_OK) {
        return err;
    }
    esp_rom_delay_us(50000);
    return ESP_OK;
}

esp_err_t rc522_pcd_initialize(rc522_device_t *device)
{
    esp_err_t err = rc522_pcd_reset(device);
    if (err != ESP_OK) {
        return err;
    }

    err = rc522_write_reg(device, REG_T_MODE, 0x80);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_T_PRESCALER, 0xA9);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_T_RELOAD_H, 0x03);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_T_RELOAD_L, 0xE8);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_TX_ASK, 0x40);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_MODE, 0x3D);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_TX_MODE, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_RX_MODE, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_MOD_WIDTH, 0x26);
    if (err != ESP_OK) {
        return err;
    }
    return rc522_set_bits(device, REG_TX_CONTROL, 0x03);
}

esp_err_t rc522_pcd_get_version(rc522_device_t *device, uint8_t *version)
{
    return rc522_read_reg(device, REG_VERSION, version);
}

esp_err_t rc522_pcd_transceive(
    rc522_device_t *device,
    const uint8_t *tx_data,
    size_t tx_length,
    uint8_t tx_last_bits,
    uint8_t *rx_data,
    size_t rx_capacity,
    size_t *rx_length)
{
    uint8_t irq = 0;
    uint8_t error_register = 0;
    uint8_t fifo_length = 0;

    if (tx_data == NULL || tx_length == 0 || rx_data == NULL || rx_length == NULL ||
        tx_last_bits > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    *rx_length = 0;

    esp_err_t err = rc522_write_reg(device, REG_COMMAND, PCD_IDLE);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_COM_IRQ, 0x7F);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_FIFO_LEVEL, 0x80);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_register(device, REG_FIFO_DATA, tx_data, tx_length);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_COMMAND, PCD_TRANSCEIVE);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_BIT_FRAMING, (uint8_t)(0x80 | tx_last_bits));
    if (err != ESP_OK) {
        return err;
    }

    uint32_t elapsed_us = 0;
    const uint32_t timeout_us = device->command_timeout_ms * 1000;
    while (elapsed_us < timeout_us) {
        err = rc522_read_reg(device, REG_COM_IRQ, &irq);
        if (err != ESP_OK) {
            return err;
        }
        if ((irq & 0x30) != 0 || (irq & 0x01) != 0) {
            break;
        }
        esp_rom_delay_us(50);
        elapsed_us += 50;
    }

    err = rc522_write_reg(device, REG_BIT_FRAMING, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    if ((irq & 0x01) != 0 || elapsed_us >= timeout_us) {
        return ESP_ERR_TIMEOUT;
    }

    err = rc522_read_reg(device, REG_ERROR, &error_register);
    if (err != ESP_OK) {
        return err;
    }
    if ((error_register & 0x1B) != 0) {
        ESP_LOGW(
            TAG,
            "Transceive error: ErrorReg=0x%02X (protocol=%u, parity=%u, collision=%u, overflow=%u)",
            error_register,
            (error_register >> 0) & 1,
            (error_register >> 1) & 1,
            (error_register >> 3) & 1,
            (error_register >> 4) & 1);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = rc522_read_reg(device, REG_FIFO_LEVEL, &fifo_length);
    if (err != ESP_OK) {
        return err;
    }
    if (fifo_length > rx_capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (fifo_length > 0) {
        err = rc522_read_register(device, REG_FIFO_DATA, rx_data, fifo_length);
        if (err != ESP_OK) {
            return err;
        }
    }
    *rx_length = fifo_length;
    return ESP_OK;
}

esp_err_t rc522_pcd_calculate_crc(
    rc522_device_t *device,
    const uint8_t *data,
    size_t length,
    uint8_t crc[2])
{
    if (data == NULL || length == 0 || crc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = rc522_write_reg(device, REG_COMMAND, PCD_IDLE);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_DIV_IRQ, 0x04);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_FIFO_LEVEL, 0x80);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_register(device, REG_FIFO_DATA, data, length);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_write_reg(device, REG_COMMAND, PCD_CALC_CRC);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t irq = 0;
    uint32_t elapsed_us = 0;
    const uint32_t timeout_us = device->command_timeout_ms * 1000;
    while (elapsed_us < timeout_us) {
        err = rc522_read_reg(device, REG_DIV_IRQ, &irq);
        if (err != ESP_OK) {
            return err;
        }
        if ((irq & 0x04) != 0) {
            break;
        }
        esp_rom_delay_us(50);
        elapsed_us += 50;
    }
    if ((irq & 0x04) == 0) {
        return ESP_ERR_TIMEOUT;
    }

    err = rc522_read_reg(device, REG_CRC_RESULT_L, &crc[0]);
    if (err != ESP_OK) {
        return err;
    }
    return rc522_read_reg(device, REG_CRC_RESULT_H, &crc[1]);
}
