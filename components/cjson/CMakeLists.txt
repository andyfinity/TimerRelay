# Vendored cJSON (from espressif/cjson 1.7.19, MIT). Kept in-tree so the build
# is fully self-contained and does not require the component manager / network.
idf_component_register(
    SRCS "cJSON.c"
    INCLUDE_DIRS "."
)
