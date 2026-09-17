idf_component_register(
    SRCS "relays.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio appstate
)
