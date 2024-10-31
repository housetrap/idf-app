/**
 ******************************************************************************
 * @file        : firmware_updater.hpp
 * @brief       : Firmware Updater
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : Firmware Updater
 ******************************************************************************
 */

#pragma once

#include <esp_err.h>
#include <esp_event.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Updater {
   public:
    static Updater* GetInstance();
    esp_err_t Update(const char* url);
    bool PendingVerification();
    void Commit() { esp_ota_mark_app_valid_cancel_rollback(); }
    void Rollback() { esp_ota_mark_app_invalid_rollback_and_reboot(); }

   private:
    static Updater* instance_;
    static SemaphoreHandle_t semaphore_;

    Updater() {};
    Updater(Updater const&)        = delete;
    void operator=(Updater const&) = delete;

    void EventHandler(esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void EventHandlerForwarder(void* arg,
                                      esp_event_base_t event_base,
                                      int32_t event_id,
                                      void* event_data) {
        Updater* instance = static_cast<Updater*>(arg);
        instance->EventHandler(event_base, event_id, event_data);
    }
};
