/**
 ******************************************************************************
 * @file        : nvs_config.hpp
 * @brief       : NVS Configuration
 * @author      : Jacques Supcik <jacques@supcik.net>
 * @date        : 28 October 2024
 ******************************************************************************
 * @copyright   : Copyright (c) 2024 HouseTrap Group
 * @attention   : SPDX-License-Identifier: MIT
 ******************************************************************************
 * @details     : NVS Configuration
 ******************************************************************************
 */

#pragma once

#include <nvs.h>

class NvsHandle {
   public:
    NvsHandle();
    ~NvsHandle();

    static esp_err_t TypeOf(const char* type, nvs_type_t* nvs_type);
    static esp_err_t TypeName(nvs_type_t type, char* name, size_t size);

    static int Base64Encode(char* dst, size_t dlen, size_t* olen, const char* src, size_t slen);
    static int Base64Decode(char* dst, size_t dlen, size_t* olen, const char* src, size_t slen);

    esp_err_t Open(const char* name_space, nvs_open_mode_t mode);
    void Close();

    esp_err_t FindKey(const char* key, nvs_type_t* out_type);

    esp_err_t GetInt(const char* key, nvs_type_t type, double* value);
    esp_err_t GetString(const char* key, char* value, size_t* length);
    esp_err_t GetBlob(const char* key, void* value, size_t* length);

    esp_err_t SetInt(const char* key, nvs_type_t type, double value);
    esp_err_t SetString(const char* key, const char* value);
    esp_err_t SetBlob(const char* key, const void* value, size_t length);

    esp_err_t Commit();

    esp_err_t EraseKey(const char* key);
    esp_err_t EraseAll();

   private:
    nvs_handle_t handle_;
};

class NvsConfig {
   public:
};
