idf_component_register(
    SRCS "timekeeper.c" "tzdata.c"
    INCLUDE_DIRS "include"
    REQUIRES appstate esp_netif lwip
)
