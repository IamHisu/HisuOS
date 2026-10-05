idf_component_register(
    SRCS "src/friend_http.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES hisu_wifi hisu_settings hisu_ai hisu_ui esp_http_server json
    EMBED_TXTFILES
        "../../web/assets/index.html" "../../web/assets/style.css"
        "../../web/assets/app.js" "../../web/assets/home.html"
        "../../web/assets/home.js" "../../web/assets/gif-resize.js"
)
