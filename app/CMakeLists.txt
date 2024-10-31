idf_component_register(
    SRCS 
        "src/app.cpp"
        "src/firmware_updater.cpp"
        "src/get_info.cpp"
        "src/httpd.cpp"
        "src/mqtt.cpp"
        "src/nvs_config_web_services.cpp"
        "src/nvs_config.cpp"
        "src/provisioner.cpp"

    INCLUDE_DIRS "include"
    REQUIRES
        "app_update"
        "esp_app_format"
        "esp_http_server"
        "esp_https_ota"
        "esp_timer"
        "json"
        "mbedtls"
        "mdns"
        "mqtt"
        "nvs_flash"
        "wifi_provisioning"
)
