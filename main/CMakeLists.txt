idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES storage appstate relays timekeeper controller netmgr webserver ota
)
