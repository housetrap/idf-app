/**
 ******************************************************************************
 * @file        : httpd.cpp
 * @brief       : HTTP Server
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : HTTP Server
 ******************************************************************************
 */

#include "httpd.hpp"

#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char* kTag = "httpd";

Httpd* Httpd::instance_             = nullptr;
SemaphoreHandle_t Httpd::semaphore_ = xSemaphoreCreateMutex();

Httpd* Httpd::GetInstance() {
    if (instance_ == nullptr) {
        xSemaphoreTake(semaphore_, portMAX_DELAY);
        if (instance_ == nullptr) {
            instance_ = new Httpd();
        }
        xSemaphoreGive(semaphore_);
    }
    return instance_;
}

void Httpd::AddRoute(const char* uri,
                     httpd_method_t method,
                     esp_err_t (*handler)(httpd_req_t* r),
                     void* user_ctx) {
    httpd_uri_t route = {};
    route.uri         = uri;
    route.method      = method;
    route.handler     = handler;
    route.user_ctx    = user_ctx;
    routes_.push_back(route);
}

void Httpd::Start(size_t stack_size, int max_uri_handlers) {
    if (server_ != nullptr) {
        ESP_LOGW(kTag, "Server already started");
        return;
    }
    httpd_handle_t server   = nullptr;
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.stack_size       = stack_size;
    config.lru_purge_enable = true;
    config.max_uri_handlers = max_uri_handlers;
    ESP_LOGI(kTag, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(kTag, "Registering URI handlers");
        for (auto& route : routes_) {
            ESP_LOGI(kTag, "- %s", route.uri);
            httpd_register_uri_handler(server, &route);
        }
    }
}

void Httpd::Stop() {
    if (server_ == nullptr) {
        ESP_LOGW(kTag, "Server already stopped");
        return;
    }
    ESP_LOGI(kTag, "Stopping server");
    httpd_stop(*server_);
    server_ = nullptr;
}

void Httpd::SendError(httpd_req_t* req, httpd_err_code_t status_code, const char* message) {
    ESP_LOGW(kTag, "Sending error: %d - %s", status_code, message);
    httpd_resp_send_err(req, status_code, message);
}