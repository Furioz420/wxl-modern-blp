// wxl-modern-blp extension-wide API and configuration helpers.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "common/ExtensionConfig.hpp"
#include "wxl/PluginApi.h"
#include "wxl/StorageApi.h"

#include <cstdint>
#include <cstdlib>

namespace wxl_modern_blp
{
    extern const WXL_Api* g_api;

    inline uint32_t ConfigU32(const char* name, uint32_t fallback,
                              uint32_t minValue, uint32_t maxValue)
    {
        char value[32]{};
        if (!wxl::ext::config::Raw(
                name, value, sizeof value,
                "Extensions\\wxl-modern-blp\\wxl-modern-blp.cfg"))
            return fallback;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end == value) return fallback;
        if (parsed < minValue) return minValue;
        if (parsed > maxValue) return maxValue;
        return static_cast<uint32_t>(parsed);
    }
}

#define WLOG_INFO(...)  ::wxl_modern_blp::g_api->Log(WXL_LOG_INFO,  "wxl-modern-blp", __VA_ARGS__)
#define WLOG_WARN(...)  ::wxl_modern_blp::g_api->Log(WXL_LOG_WARN,  "wxl-modern-blp", __VA_ARGS__)
#define WLOG_ERROR(...) ::wxl_modern_blp::g_api->Log(WXL_LOG_ERROR, "wxl-modern-blp", __VA_ARGS__)
