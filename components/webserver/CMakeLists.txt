idf_component_register(
    SRCS "webserver.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server cjson appstate timekeeper netmgr controller schedule
    EMBED_FILES
        "www/index.html"
        "www/style.css"
        "www/app.js"
)
