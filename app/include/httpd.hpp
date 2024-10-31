/**
 ******************************************************************************
 * @file        : mqtt.hpp
 * @brief       : MQTT Client
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : MQTT Client
 ******************************************************************************
 */

#pragma once

#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

class Httpd {
   public:
    static Httpd* GetInstance();
    void AddRoute(const char* uri,
                  httpd_method_t method,
                  esp_err_t (*handler)(httpd_req_t* r),
                  void* user_ctx);

    void Start(size_t stack_size, int max_uri_handlers);
    void Stop();
    void ClearRoutes() { routes_.clear(); }

    int Receive(httpd_req_t* req, char* buffer, size_t buffer_size) {
        return httpd_req_recv(req, buffer, buffer_size);
    }

    void Reply(httpd_req_t* req, const char* data) {
        httpd_resp_send(req, data, HTTPD_RESP_USE_STRLEN);
    }
    void ReplyJson(httpd_req_t* req, const char* data) {
        httpd_resp_set_type(req, HTTPD_TYPE_JSON);
        httpd_resp_send(req, data, HTTPD_RESP_USE_STRLEN);
    }

    void SendError(httpd_req_t* req, httpd_err_code_t status_code, const char* message);

   private:
    static Httpd* instance_;
    static SemaphoreHandle_t semaphore_;

    Httpd() {};
    Httpd(Httpd const&)          = delete;
    void operator=(Httpd const&) = delete;

    httpd_handle_t* server_ = nullptr;
    std::vector<httpd_uri_t> routes_;
};