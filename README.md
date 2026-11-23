# RC522 SPI component for ESP-IDF

This project contains a reusable ESP-IDF component for the MFRC522 RFID reader.
The component is separated into a PCD layer (MFRC522 registers, SPI, CRC, and
transceive operations) and a PICC layer (ISO/IEC 14443 Type A card selection
and UID handling).

## Features

- SPI mode 0 communication, attached to an SPI bus initialized by the application
- Optional hardware reset pin or MFRC522 software reset
- Reader initialization and silicon version readout
- Single-card polling and selection for 4-, 7-, and 10-byte Type A UIDs
- Per-device locking for task-safe calls to public operations

This initial component does not implement MIFARE authentication or block
read/write operations. It also does not perform multi-card anticollision.

## Wiring example

The sample in `main/rc522_spi_driver.c` uses these classic ESP32 GPIOs:

| RC522 | ESP32 |
|---|---:|
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| SDA / SS | 5 |
| RST | 22 |
| GND | GND |
| 3.3V | 3.3V |

Use 3.3 V logic and power. Change the example pins and SPI host to match your
board. The RC522 `SDA` pin is the SPI chip-select pin in SPI mode. If the
module's RST pin is not connected to an ESP32 GPIO, configure
`reset_gpio = GPIO_NUM_NC` to use the MFRC522 software reset and ensure the
module's RST input is held high; do not leave it floating.

## Use in another ESP-IDF project

Copy `components/rc522` into the project's `components` directory, add `rc522`
to the consuming component's `REQUIRES`, initialize the bus, then create and
initialize a reader:

```c
#include "driver/spi_master.h"
#include "rc522.h"

const spi_host_device_t host = SPI2_HOST;
const spi_bus_config_t bus = {
    .mosi_io_num = 23,
    .miso_io_num = 19,
    .sclk_io_num = 18,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
};
ESP_ERROR_CHECK(spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO));

rc522_config_t config = RC522_DEFAULT_CONFIG(host, GPIO_NUM_5);
// Set config.reset_gpio = GPIO_NUM_22 if the module's RST pin is wired there.
// Otherwise leave GPIO_NUM_NC for software reset and hold the module RST pin high.

rc522_handle_t reader = NULL;
ESP_ERROR_CHECK(rc522_create(&config, &reader));
ESP_ERROR_CHECK(rc522_init(reader));
```

Poll `rc522_read_uid(reader, &uid)`. `ESP_ERR_NOT_FOUND` means no card answered
that poll; other non-`ESP_OK` values indicate an SPI, timeout, or protocol
error. After use, call `rc522_delete(reader)` before deinitializing the SPI bus.
The application owns the SPI bus and must keep it initialized for the reader's
lifetime. Do not delete a reader concurrently with other calls on its handle.

The component declares ESP-IDF 6.0 or newer in its Component Manager manifest.

For the design rationale, module responsibilities, development workflow, and
extension guidance, see [devsteps.md](devsteps.md).
