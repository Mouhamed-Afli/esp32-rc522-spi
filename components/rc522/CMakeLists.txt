idf_component_register(
    SRCS
        "rc522.c"
        "rc522_pcd.c"
        "rc522_picc.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi
    PRIV_REQUIRES esp_rom freertos log
)
