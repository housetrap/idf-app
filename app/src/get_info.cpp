/**
 ******************************************************************************
 * @file        : get_info.cpp
 * @brief       : Get Innformation
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : Get Innformation
 ******************************************************************************
 */

#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include <memory>

#include "app.hpp"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char* kTag = "get info";

esp_err_t App::DoGetInfo(httpd_req_t* req) {
    App* ctx = (App*)req->user_ctx;

    std::shared_ptr<cJSON> response(cJSON_CreateObject(), cJSON_Delete);

    cJSON* app_node = cJSON_CreateObject();
    cJSON_AddItemToObject(response.get(), "app", app_node);
    const esp_app_desc_t* app_descr = esp_app_get_description();
    cJSON_AddStringToObject(app_node, "app-version", app_descr->version);
    cJSON_AddStringToObject(app_node, "app-name", app_descr->project_name);
    cJSON_AddStringToObject(app_node, "idf-version", app_descr->idf_ver);
    cJSON_AddStringToObject(app_node, "compile-time", app_descr->time);
    cJSON_AddStringToObject(app_node, "compile-date", app_descr->date);

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    cJSON_AddNumberToObject(response.get(), "time-of-day-sec", tv_now.tv_sec);

    int64_t uptime = esp_timer_get_time() / 1000;
    cJSON_AddNumberToObject(response.get(), "uptime-msec", uptime);

    uint8_t mac_address[6];
    if (esp_read_mac(mac_address, ESP_MAC_WIFI_STA) == ESP_OK) {
        char mac_str[18];
        snprintf(mac_str,
                 sizeof(mac_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_address[0],
                 mac_address[1],
                 mac_address[2],
                 mac_address[3],
                 mac_address[4],
                 mac_address[5]);
        cJSON_AddStringToObject(response.get(), "wifi-mac-address", mac_str);
    }

    cJSON_AddStringToObject(response.get(), "hostname", ctx->hostname_);

    UBaseType_t nOfTasks = uxTaskGetNumberOfTasks();
    TaskStatus_t* data = new TaskStatus_t[nOfTasks];
    UBaseType_t res = uxTaskGetSystemState(data, nOfTasks, nullptr);
    if (res == pdFALSE) {
        ESP_LOGE(kTag, "Failed to get task status");
        delete[] data;
    } else {
        cJSON* tasks = cJSON_CreateArray();
        cJSON_AddItemToObject(response.get(), "tasks", tasks);
        for (UBaseType_t i = 0; i < nOfTasks; i++) {
            cJSON* task = cJSON_CreateObject();

            const char* state_name;
            switch (data[i].eCurrentState) {
                case eRunning:
                    state_name = "Running";
                    break;
                case eReady:
                    state_name = "Ready";
                    break;
                case eBlocked:
                    state_name = "Blocked";
                    break;
                case eSuspended:
                    state_name = "Suspended";
                    break;
                case eDeleted:
                    state_name = "Deleted";
                    break;
                case eInvalid:
                    state_name = "Invalid";
                    break;
                default:
                    state_name = "Unknown";
                    break;
            }

            const char* core_name;
            if (data[i].xCoreID == tskNO_AFFINITY) {
                core_name = "No Affinity";
            } else if (data[i].xCoreID == 0) {
                core_name = "0 (Pro)";
            } else if (data[i].xCoreID == 1) {
                core_name = "1 (App)";
            } else {
                core_name = "Unknown";
            }

            cJSON_AddStringToObject(task, "name", data[i].pcTaskName);
            cJSON_AddNumberToObject(task, "priority", data[i].uxCurrentPriority);
            cJSON_AddStringToObject(task, "state", state_name);
            /* cJSON_AddNumberToObject(task, "run-time-counter", data[i].ulRunTimeCounter); */
            cJSON_AddStringToObject(task, "core-id", core_name);
            cJSON_AddNumberToObject(task, "stack-high-water-mark", data[i].usStackHighWaterMark);
            cJSON_AddItemToArray(tasks, task);
        }
        delete[] data;
    }

    cJSON* heaps = cJSON_CreateObject();
    cJSON_AddItemToObject(response.get(), "heap", heaps);

    cJSON* system_heap = cJSON_CreateObject();
    cJSON_AddItemToObject(heaps, "SYSTEM", system_heap);
    cJSON_AddNumberToObject(system_heap, "free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(system_heap, "free-internal", esp_get_free_internal_heap_size());
    cJSON_AddNumberToObject(system_heap, "minimum-free", esp_get_minimum_free_heap_size());

    struct {
        const char* name;
        uint32_t caps;
    } caps_heaps[] = {
#if defined CONFIG_SPIRAM && defined CONFIG_SPIRAM_USE_MALLOC
        {"SPIRAM", MALLOC_CAP_SPIRAM},
#endif
        {"DEFAULT", MALLOC_CAP_DEFAULT},
        {"INTERNAL", MALLOC_CAP_INTERNAL},
    };

    multi_heap_info_t info;
    for (auto& ch : caps_heaps) {
        heap_caps_get_info(&info, ch.caps);
        cJSON* c_heap = cJSON_CreateObject();
        cJSON_AddItemToObject(heaps, ch.name, c_heap);
        cJSON_AddNumberToObject(c_heap, "free", info.total_free_bytes);
        cJSON_AddNumberToObject(c_heap, "minimum-free", info.minimum_free_bytes);
        cJSON_AddNumberToObject(c_heap, "largest-free", info.largest_free_block);
    }

    switch (esp_reset_reason()) {
        case ESP_RST_UNKNOWN:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Unknown");
            break;
        case ESP_RST_POWERON:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Power On");
            break;
        case ESP_RST_EXT:
            cJSON_AddStringToObject(response.get(), "reset-reason", "External");
            break;
        case ESP_RST_SW:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Software");
            break;
        case ESP_RST_PANIC:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Panic");
            break;
        case ESP_RST_INT_WDT:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Interrupt Watchdog");
            break;
        case ESP_RST_TASK_WDT:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Task Watchdog");
            break;
        case ESP_RST_WDT:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Watchdog");
            break;
        case ESP_RST_DEEPSLEEP:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Deep Sleep");
            break;
        case ESP_RST_BROWNOUT:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Brownout");
            break;
        case ESP_RST_SDIO:
            cJSON_AddStringToObject(response.get(), "reset-reason", "SDIO");
            break;
        case ESP_RST_USB:
            cJSON_AddStringToObject(response.get(), "reset-reason", "USB Peripheral");
            break;
        case ESP_RST_JTAG:
            cJSON_AddStringToObject(response.get(), "reset-reason", "JTAG");
            break;
        case ESP_RST_EFUSE:
            cJSON_AddStringToObject(response.get(), "reset-reason", "EFUSE");
            break;
        case ESP_RST_PWR_GLITCH:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Power Glitch");
            break;
        case ESP_RST_CPU_LOCKUP:
            cJSON_AddStringToObject(response.get(), "reset-reason", "CPU Lockup");
            break;
        default:
            cJSON_AddStringToObject(response.get(), "reset-reason", "Unknown");
            break;
    }

    std::shared_ptr<char> str(cJSON_PrintUnformatted(response.get()), free);
    ctx->httpd_->ReplyJson(req, str.get());
    ESP_LOGD(kTag, "Info Sent");
    return ESP_OK;
}
