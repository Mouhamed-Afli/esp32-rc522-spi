#include <string.h>

#include "esp_log.h"

#include "rc522_internal.h"

static const char *TAG = "rc522_picc";

enum {
    PICC_WUPA = 0x52,
    PICC_HLTA = 0x50,
    PICC_SEL_CL1 = 0x93,
    PICC_SEL_CL2 = 0x95,
    PICC_SEL_CL3 = 0x97,
    PICC_CT = 0x88,
    PICC_SAK_CASCADE = 0x04,
};

static esp_err_t rc522_picc_request(rc522_device_t *device)
{
    const uint8_t command = PICC_WUPA;
    uint8_t response[2] = {0};
    size_t response_length = 0;

    esp_err_t err = rc522_pcd_transceive(
        device, &command, 1, 7, response, sizeof(response), &response_length);
    if (err == ESP_ERR_TIMEOUT) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (response_length != sizeof(response)) {
        ESP_LOGW(TAG, "WUPA returned %u bits of data, expected 16", (unsigned)(response_length * 8));
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t rc522_picc_halt(rc522_device_t *device)
{
    const uint8_t command[] = {PICC_HLTA, 0x00};
    uint8_t frame[sizeof(command) + 2];
    uint8_t response[1];
    size_t response_length = 0;

    memcpy(frame, command, sizeof(command));
    esp_err_t err = rc522_pcd_calculate_crc(device, command, sizeof(command), &frame[2]);
    if (err != ESP_OK) {
        return err;
    }
    err = rc522_pcd_transceive(device, frame, sizeof(frame), 0, response, sizeof(response), &response_length);
    if (err == ESP_ERR_TIMEOUT) {
        return ESP_OK;
    }
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "Unexpected response to HALT command (%u bytes)", (unsigned)response_length);
    }
    return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
}

esp_err_t rc522_picc_read_uid(rc522_device_t *device, rc522_uid_t *uid)
{
    if (uid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uid->length = 0;
    esp_err_t err = rc522_picc_request(device);
    if (err != ESP_OK) {
        return err;
    }

    static const uint8_t select_commands[] = {PICC_SEL_CL1, PICC_SEL_CL2, PICC_SEL_CL3};
    bool cascade_required = false;
    for (size_t cascade_level = 0; cascade_level < 3; cascade_level++) {
        const uint8_t anticollision_command[] = {select_commands[cascade_level], 0x20};
        uint8_t anticollision_response[5] = {0};
        size_t response_length = 0;

        err = rc522_pcd_transceive(
            device,
            anticollision_command,
            sizeof(anticollision_command),
            0,
            anticollision_response,
            sizeof(anticollision_response),
            &response_length);
        if (err != ESP_OK) {
            return err;
        }
        if (response_length != sizeof(anticollision_response) ||
            (uint8_t)(anticollision_response[0] ^ anticollision_response[1] ^
                      anticollision_response[2] ^ anticollision_response[3]) !=
                anticollision_response[4]) {
            uint8_t expected_bcc = 0;
            if (response_length == sizeof(anticollision_response)) {
                expected_bcc = (uint8_t)(anticollision_response[0] ^ anticollision_response[1] ^
                                         anticollision_response[2] ^ anticollision_response[3]);
            }
            ESP_LOGW(
                TAG,
                "Cascade level %u anticollision invalid (length=%u, UID/BCC=%02X %02X %02X %02X %02X, expected BCC=%02X)",
                (unsigned)(cascade_level + 1),
                (unsigned)response_length,
                anticollision_response[0],
                anticollision_response[1],
                anticollision_response[2],
                anticollision_response[3],
                anticollision_response[4],
                expected_bcc);
            return ESP_ERR_INVALID_RESPONSE;
        }

        const bool has_cascade_tag = anticollision_response[0] == PICC_CT;
        if (has_cascade_tag) {
            if (uid->length + 3 > sizeof(uid->bytes)) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&uid->bytes[uid->length], &anticollision_response[1], 3);
            uid->length += 3;
        } else {
            if (uid->length + 4 > sizeof(uid->bytes)) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&uid->bytes[uid->length], anticollision_response, 4);
            uid->length += 4;
        }

        uint8_t select_frame[9] = {
            select_commands[cascade_level],
            0x70,
            anticollision_response[0],
            anticollision_response[1],
            anticollision_response[2],
            anticollision_response[3],
            anticollision_response[4],
        };
        err = rc522_pcd_calculate_crc(device, select_frame, 7, &select_frame[7]);
        if (err != ESP_OK) {
            return err;
        }

        uint8_t select_response[3];
        err = rc522_pcd_transceive(
            device, select_frame, sizeof(select_frame), 0, select_response, sizeof(select_response), &response_length);
        if (err != ESP_OK) {
            return err;
        }
        if (response_length != sizeof(select_response)) {
            ESP_LOGW(
                TAG,
                "Cascade level %u SAK response length was %u, expected 3",
                (unsigned)(cascade_level + 1),
                (unsigned)response_length);
            return ESP_ERR_INVALID_RESPONSE;
        }
        uint8_t expected_crc[2];
        err = rc522_pcd_calculate_crc(device, select_response, 1, expected_crc);
        if (err != ESP_OK) {
            return err;
        }
        if (select_response[1] != expected_crc[0] || select_response[2] != expected_crc[1]) {
            ESP_LOGW(
                TAG,
                "Cascade level %u SAK CRC mismatch (received %02X%02X, expected %02X%02X)",
                (unsigned)(cascade_level + 1),
                select_response[2],
                select_response[1],
                expected_crc[1],
                expected_crc[0]);
            return ESP_ERR_INVALID_RESPONSE;
        }

        cascade_required = (select_response[0] & PICC_SAK_CASCADE) != 0;
        if (cascade_required != has_cascade_tag) {
            ESP_LOGW(
                TAG,
                "Cascade level %u inconsistency (SAK=0x%02X, cascade tag=%u)",
                (unsigned)(cascade_level + 1),
                select_response[0],
                has_cascade_tag);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (!cascade_required) {
            if (uid->length != 4 && uid->length != 7 && uid->length != 10) {
                ESP_LOGW(TAG, "Selected UID has unsupported length %u", (unsigned)uid->length);
                return ESP_ERR_INVALID_RESPONSE;
            }
            return rc522_picc_halt(device);
        }
    }

    return ESP_ERR_INVALID_RESPONSE;
}
