# RC522 Driver Development Steps and Architecture

This document records the design followed for the RC522 SPI component and gives
a practical sequence for extending it. The goal is a reusable ESP-IDF component
that can be copied into other projects, is easy to read, and can grow without
mixing application code with chip-protocol details.

## 1. Design goals and boundaries

The component is designed to:

- Work as an ESP-IDF component on ESP-IDF 6.0 or newer.
- Use ESP-IDF's SPI master driver rather than implementing SPI at GPIO level.
- Let the application own and initialize the SPI bus.
- Keep the public API independent of MFRC522 register details.
- Separate the MFRC522 reader (PCD) protocol from the contactless card (PICC)
  protocol.
- Return ESP-IDF error codes so applications can decide how to recover.
- Support one reader per handle and serialize operations on that handle.

The current implementation covers reader initialization, version readout, and
single-card ISO/IEC 14443 Type A selection for 4-, 7-, and 10-byte UIDs. It
does not yet implement MIFARE authentication, block operations, or selection
among multiple cards.

## 2. Component architecture

```text
Application
    |
    | rc522_create / rc522_init / rc522_read_uid / rc522_delete
    v
Public API and per-device state              rc522.c
    |
    +--------------------------+
    |                          |
    v                          v
MFRC522 PCD layer          Type A PICC layer
rc522_pcd.c                rc522_picc.c
register access            REQA/WUPA, anticollision,
SPI transfers              select, cascade UID, halt
reset, CRC, transceive
    |
    v
ESP-IDF SPI master + optional GPIO reset
```

### Public API: `components/rc522/include/rc522.h`

This is the interface consuming applications include. It defines:

- An opaque `rc522_handle_t`, hiding the internal device structure.
- `rc522_config_t` and `RC522_DEFAULT_CONFIG` for bus, CS/reset pins, SPI
  frequency, and command timeout.
- `rc522_uid_t` for the UID bytes and their length.
- Lifecycle and operation functions: `rc522_create`, `rc522_init`,
  `rc522_get_version`, `rc522_read_uid`, and `rc522_delete`.

Keep register addresses and protocol state out of this header. Add a public
function only when it represents a useful operation for an application.

### Device state and internal interface: `rc522_internal.h`

The private device object holds the ESP-IDF SPI device handle, reset pin,
timeout, initialization state, and mutex. Internal PCD/PICC function
declarations live here and are not part of the component's public API.

Each created handle owns its SPI device registration and mutex. The application
owns the SPI bus and must not deinitialize it until after deleting all RC522
handles attached to it.

### Public API implementation: `rc522.c`

This layer validates configuration and arguments, allocates/frees device
resources, serializes public operations with the device mutex, and delegates
chip and card work to lower layers. It should remain the boundary between
application-facing operations and implementation details.

### MFRC522 PCD layer: `rc522_pcd.c`

PCD means proximity coupling device: the MFRC522 reader chip. This file owns
MFRC522-specific register reads/writes and implements:

- SPI register framing, with FIFO bytes accessed as repeated single-register
  transactions rather than assuming a multi-byte SPI burst behaves as repeated
  accesses to `FIFODataReg`.
- Hardware or software reset.
- Reader register initialization and antenna enable.
- Version register access.
- FIFO-based transceive with timeout and error checks.
- CRC calculation using the chip.

New PCD features should build on these helpers instead of duplicating SPI
framing or register access in the PICC layer.

### Type A PICC layer: `rc522_picc.c`

PICC means proximity integrated circuit card: the contactless card. This file
builds ISO/IEC 14443 Type A exchanges on top of PCD transceive and CRC
operations. It performs card wake-up, UID anticollision/select across cascade
levels, UID validation, and card halt.

Keep card commands and UID/cascade rules here. Other card families or MIFARE
commands should be implemented as appropriately scoped protocol modules rather
than added to low-level SPI code.

### ESP-IDF component and example application

`components/rc522/CMakeLists.txt` lists component sources and ESP-IDF
dependencies. `main/rc522_spi_driver.c` is a usage example: it configures the
SPI bus, creates and initializes the reader, reads its version, and polls for
UIDs. Example wiring and consumer setup are in `README.md`.

## 3. Development sequence

Use the following sequence when adding or changing driver functionality:

1. **Define the supported behavior.** Identify the card/chip operation, valid
   inputs, expected response, timeout behavior, and error cases. Check the
   MFRC522 datasheet and the relevant ISO/IEC protocol requirements.
2. **Choose the correct layer.** Public application operation belongs in the
   API; MFRC522 registers, FIFO, CRC, and transceive belong in PCD; card
   commands and UID/card protocol belong in PICC.
3. **Add the smallest internal operation.** Reuse register, CRC, and transceive
   helpers. Keep buffers bounded, validate response lengths and checksums, and
   return a meaningful `esp_err_t` on failure.
4. **Expose only useful application behavior.** Add or update a public type or
   function in `include/rc522.h`; document ownership, valid state, returned
   errors, and concurrency expectations.
5. **Wire lifecycle and concurrency.** Route public operations through the
   per-device mutex. Ensure partial initialization failures release resources,
   and that deletion does not silently discard an ESP-IDF error.
6. **Update the example and documentation.** Keep the sample representative
   but small, update `README.md` for user setup, and update this document when
   architecture or development conventions change.
7. **Build and validate.** Build the ESP-IDF project for its configured target.
   Exercise success and failure paths where practical, and test with physical
   RC522 hardware before claiming hardware support. A successful build alone
   does not verify wiring or RF behavior.

## 4. Error and ownership conventions

- `ESP_OK` indicates a successful operation.
- `ESP_ERR_NOT_FOUND` from `rc522_read_uid` means no card answered that poll.
- Other errors report invalid arguments/state, timeout, invalid response, SPI
  failures, or resource allocation failures.
- The caller initializes and owns the SPI bus; the component adds/removes only
  its own SPI device.
- Delete the reader before deinitializing its SPI bus.
- Do not delete a handle while another task is using it. The mutex serializes
  normal operations, but does not make concurrent deletion safe.
- Do not share a single handle across independently managed lifetimes.

## 5. Extension roadmap

Potential next additions, each implemented and validated as a separate feature:

1. Add unit-testable protocol helpers or a mockable transport seam for
   validating register framing and state transitions without hardware.
2. Add collision-aware anticollision and explicitly define multi-card behavior.
3. Add MIFARE Classic authentication and block access, including key handling
   and clear documentation of card security limitations.
4. Add card removal/reselection behavior and optional higher-level event or
   polling helpers only if applications need them.
5. Test across supported ESP32 targets and ESP-IDF versions before documenting
   broader compatibility.

## 6. Current validation status

The project has been built with ESP-IDF 6.1 for the ESP32 target. This confirms
the current component and example compile together in that configuration; it
does not replace testing against a physical RC522 module and cards.
