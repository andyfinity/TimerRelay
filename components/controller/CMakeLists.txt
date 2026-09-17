idf_component_register(
    SRCS "controller.c"
    INCLUDE_DIRS "include"
    REQUIRES appstate relays schedule timekeeper
)
