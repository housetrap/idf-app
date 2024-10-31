/**
 ******************************************************************************
 * @file        : nvs_config.cpp
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

#include "nvs_config.hpp"

#include <mbedtls/base64.h>
#include <string.h>

NvsHandle::NvsHandle() : handle_(0) {}
NvsHandle::~NvsHandle() { Close(); }

esp_err_t NvsHandle::Open(const char* name_space, nvs_open_mode_t mode) {
    return nvs_open(name_space, mode, &handle_);
}
void NvsHandle::Close() {
    if (handle_ != 0) {
        nvs_close(handle_);
    }
}

esp_err_t NvsHandle::FindKey(const char* key, nvs_type_t* out_type) {
    return nvs_find_key(handle_, key, out_type);
}

esp_err_t NvsHandle::GetInt(const char* key, nvs_type_t type, double* value) {
    switch (type) {
        case NVS_TYPE_U8: {
            uint8_t u8;
            esp_err_t err = nvs_get_u8(handle_, key, &u8);
            *value        = u8;
            return err;
        }
        case NVS_TYPE_I8: {
            int8_t i8;
            esp_err_t err = nvs_get_i8(handle_, key, &i8);
            *value        = i8;
            return err;
        }
        case NVS_TYPE_U16: {
            uint16_t u16;
            esp_err_t err = nvs_get_u16(handle_, key, &u16);
            *value        = u16;
            return err;
        }
        case NVS_TYPE_I16: {
            int16_t i16;
            esp_err_t err = nvs_get_i16(handle_, key, &i16);
            *value        = i16;
            return err;
        }
        case NVS_TYPE_U32: {
            uint32_t u32;
            esp_err_t err = nvs_get_u32(handle_, key, &u32);
            *value        = u32;
            return err;
        }
        case NVS_TYPE_I32: {
            int32_t i32;
            esp_err_t err = nvs_get_i32(handle_, key, &i32);
            *value        = i32;
            return err;
        }
        case NVS_TYPE_U64: {
            uint64_t u64;
            esp_err_t err = nvs_get_u64(handle_, key, &u64);
            *value        = u64;
            return err;
        }
        case NVS_TYPE_I64: {
            int64_t i64;
            esp_err_t err = nvs_get_i64(handle_, key, &i64);
            *value        = i64;
            return err;
        }
        default:
            return ESP_ERR_NVS_TYPE_MISMATCH;
    }
}

esp_err_t NvsHandle::GetString(const char* key, char* value, size_t* length) {
    return nvs_get_str(handle_, key, value, length);
}

esp_err_t NvsHandle::GetBlob(const char* key, void* value, size_t* length) {
    return nvs_get_blob(handle_, key, value, length);
}

esp_err_t NvsHandle::SetInt(const char* key, nvs_type_t type, double value) {
    switch (type) {
        case NVS_TYPE_U8:
            return nvs_set_u8(handle_, key, value);
        case NVS_TYPE_I8:
            return nvs_set_i8(handle_, key, value);
        case NVS_TYPE_U16:
            return nvs_set_u16(handle_, key, value);
        case NVS_TYPE_I16:
            return nvs_set_i16(handle_, key, value);
        case NVS_TYPE_U32:
            return nvs_set_u32(handle_, key, value);
        case NVS_TYPE_I32:
            return nvs_set_i32(handle_, key, value);
        case NVS_TYPE_U64:
            return nvs_set_u64(handle_, key, value);
        case NVS_TYPE_I64:
            return nvs_set_i64(handle_, key, value);
        default:
            return ESP_ERR_NVS_TYPE_MISMATCH;
    }
}
esp_err_t NvsHandle::SetString(const char* key, const char* value) {
    return nvs_set_str(handle_, key, value);
}
esp_err_t NvsHandle::SetBlob(const char* key, const void* value, size_t length) {
    return nvs_set_blob(handle_, key, value, length);
}

esp_err_t NvsHandle::Commit() { return nvs_commit(handle_); }
esp_err_t NvsHandle::EraseKey(const char* key) { return nvs_erase_key(handle_, key); }
esp_err_t NvsHandle::EraseAll() { return nvs_erase_all(handle_); }

// ----- Static Methods -----

esp_err_t NvsHandle::TypeName(nvs_type_t type, char* name, size_t size) {
    esp_err_t err = ESP_OK;
    switch (type) {
        case NVS_TYPE_U8:
            strncpy(name, "uint8", size);
            break;
        case NVS_TYPE_I8:
            strncpy(name, "int8", size);
            break;
        case NVS_TYPE_U16:
            strncpy(name, "uint16", size);
            break;
        case NVS_TYPE_I16:
            strncpy(name, "int16", size);
            break;
        case NVS_TYPE_U32:
            strncpy(name, "uint32", size);
            break;
        case NVS_TYPE_I32:
            strncpy(name, "int32", size);
            break;
        case NVS_TYPE_U64:
            strncpy(name, "uint64", size);
            break;
        case NVS_TYPE_I64:
            strncpy(name, "int64", size);
            break;
        case NVS_TYPE_STR:
            strncpy(name, "string", size);
            break;
        case NVS_TYPE_BLOB:
            strncpy(name, "blob", size);
            break;
        case NVS_TYPE_ANY:
            strncpy(name, "any", size);
            break;
        default:
            strncpy(name, "unknown", size);
            err = ESP_ERR_NVS_TYPE_MISMATCH;
            break;
    }
    return err;
}

esp_err_t NvsHandle::TypeOf(const char* type, nvs_type_t* nvs_type) {
    esp_err_t err = ESP_OK;
    if (strcmp(type, "uint8") == 0) {
        *nvs_type = NVS_TYPE_U8;
    } else if (strcmp(type, "int8") == 0) {
        *nvs_type = NVS_TYPE_I8;
    } else if (strcmp(type, "uint16") == 0) {
        *nvs_type = NVS_TYPE_U16;
    } else if (strcmp(type, "int16") == 0) {
        *nvs_type = NVS_TYPE_I16;
    } else if (strcmp(type, "uint32") == 0) {
        *nvs_type = NVS_TYPE_U32;
    } else if (strcmp(type, "int32") == 0) {
        *nvs_type = NVS_TYPE_I32;
    } else if (strcmp(type, "uint64") == 0) {
        *nvs_type = NVS_TYPE_U64;
    } else if (strcmp(type, "int64") == 0) {
        *nvs_type = NVS_TYPE_I64;
    } else if (strcmp(type, "string") == 0) {
        *nvs_type = NVS_TYPE_STR;
    } else if (strcmp(type, "blob") == 0) {
        *nvs_type = NVS_TYPE_BLOB;
    } else if (strcmp(type, "any") == 0) {
        *nvs_type = NVS_TYPE_ANY;
    } else {
        *nvs_type = NVS_TYPE_ANY;
        err       = ESP_ERR_NVS_TYPE_MISMATCH;
    }
    return err;
}

int NvsHandle::Base64Encode(char* dst, size_t dlen, size_t* olen, const char* src, size_t slen) {
    return mbedtls_base64_encode((unsigned char*)dst, dlen, olen, (const unsigned char*)src, slen);
}

int NvsHandle::Base64Decode(char* dst, size_t dlen, size_t* olen, const char* src, size_t slen) {
    return mbedtls_base64_decode((unsigned char*)dst, dlen, olen, (const unsigned char*)src, slen);
}