// Modern BLP compatibility and memory-budget extension for the 3.3.5 client.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"

#include "engine/assets/shared/textures/blp/BlpTranscode.hpp"
#include "wxl/ModernBlpApi.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <span>
#include <vector>

namespace wxl_modern_blp::rain
{
    void Initialize();
    int Transform(const char* name, const uint8_t* raw, uint32_t rawLen,
                  const WXL_ByteSink* sink);
}

namespace
{
    namespace blp = wxl::modern::assets::textures::blp;

    std::atomic<uint32_t> g_transcoded{0};
    std::atomic<uint32_t> g_componentPalettized{0};
    std::atomic<uint32_t> g_capped{0};
    std::atomic<uint64_t> g_bytesSaved{0};
    std::atomic<uint32_t> g_logged{0};

    bool IsInterfaceTexture(const char* name)
    {
        if (!name) return false;
        constexpr std::string_view prefix = "interface/";
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            const unsigned char raw = static_cast<unsigned char>(name[i]);
            if (!raw) return false;
            const char folded = name[i] == '\\'
                ? '/'
                : static_cast<char>(std::tolower(raw));
            if (folded != prefix[i]) return false;
        }
        return true;
    }

    bool HasFoldedPathPrefix(const char* name, std::string_view prefix)
    {
        if (!name) return false;
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            const unsigned char raw = static_cast<unsigned char>(name[i]);
            if (!raw) return false;
            const char folded = name[i] == '\\'
                ? '/'
                : static_cast<char>(std::tolower(raw));
            if (folded != prefix[i]) return false;
        }
        return true;
    }

    bool IsCharacterComponentTexture(const char* name)
    {
        // These textures are composited into a character skin by a separate
        // legacy path. Unlike ordinary world/M2 textures, that compositor
        // requires a paletted BLP and renders modern BGRA/DXT inputs green.
        return HasFoldedPathPrefix(name, "item/texturecomponents/");
    }

    bool IsLoadingScreenTexture(const char* name)
    {
        // Loading-screen art is a single, short-lived full-screen texture. It
        // benefits visibly from retaining its authored top mip and does not
        // contribute to the persistent world-texture working set that the
        // lower world cap is intended to control.
        return HasFoldedPathPrefix(name, "interface/glues/loadingscreens/") ||
               HasFoldedPathPrefix(name, "interface/loadingscreens/");
    }

    int __cdecl Transform(const char* name, const uint8_t* raw,
                          uint32_t rawLen, const WXL_ByteSink* sink)
    {
        if (!raw || rawLen == 0 || !sink || !sink->Write) return 0;

        // Weather textures need a neutral-RGB transform before generic BLP
        // normalization because Wrath renders them with a 2x-modulate pass.
        if (wxl_modern_blp::rain::Transform(name, raw, rawLen, sink))
            return 1;

        try
        {
            // Interface atlases need the extra resolution for readable text
            // and thin frame art. World, creature, spell and item textures are
            // sampled through mip filtering and are by far the larger working
            // set in dense zones, so give them a separate 32-bit-client budget.
            const bool interfaceTexture = IsInterfaceTexture(name);
            const bool loadingScreenTexture =
                IsLoadingScreenTexture(name);
            const uint32_t maxEdge = loadingScreenTexture
                ? wxl_modern_blp::ConfigU32(
                    "WXL_BLP_LOADING_SCREEN_MAX_EDGE", 2048, 1024, 4096)
                : interfaceTexture
                    ? wxl_modern_blp::ConfigU32(
                        "WXL_BLP_INTERFACE_MAX_EDGE", 1024, 512, 4096)
                    : wxl_modern_blp::ConfigU32(
                        "WXL_BLP_WORLD_MAX_EDGE", 512, 256, 4096);

            std::span<const uint8_t> current(raw, rawLen);
            std::vector<uint8_t> capped;
            std::vector<uint8_t> transcoded;
            bool changed = false;
            bool didCap = false;
            bool didTranscode = false;

            // Drop existing oversized top mips before doing any re-encode. This
            // is byte-only and avoids spending CPU/memory on levels the client
            // will not retain. It deliberately declines short mip chains.
            if (blp::CapBlpMips(current, capped, maxEdge))
            {
                current = std::span<const uint8_t>(capped.data(), capped.size());
                changed = didCap = true;
            }

            if (IsCharacterComponentTexture(name))
            {
                const uint32_t componentMaxEdge =
                    wxl_modern_blp::ConfigU32(
                        "WXL_BLP_COMPONENT_MAX_EDGE", 512, 256, 1024);
                std::vector<uint8_t> paletted;

                // DXT character components can be palettized directly. Raw
                // BGRA components need the same two-stage conversion used by
                // the retail equipment alias provider.
                bool converted = blp::TextureComponentToPaletted(
                    current, paletted, componentMaxEdge);
                if (!converted && blp::TranscodeBlp(current, transcoded))
                {
                    converted = blp::TextureComponentToPaletted(
                        std::span<const uint8_t>(transcoded.data(),
                                                 transcoded.size()),
                        paletted, componentMaxEdge);
                    didTranscode = converted;
                }

                if (converted)
                {
                    current = std::span<const uint8_t>(paletted.data(),
                                                       paletted.size());
                    // The sink copies synchronously before this local buffer
                    // is destroyed.
                    sink->Write(sink->ctx, current.data(),
                                static_cast<uint32_t>(current.size()));
                    g_componentPalettized.fetch_add(
                        1, std::memory_order_relaxed);
                    if (didCap)
                        g_capped.fetch_add(1, std::memory_order_relaxed);
                    if (didTranscode)
                        g_transcoded.fetch_add(1, std::memory_order_relaxed);
                    if (current.size() < rawLen)
                        g_bytesSaved.fetch_add(
                            rawLen - current.size(),
                            std::memory_order_relaxed);
                    const uint32_t logIndex =
                        g_logged.fetch_add(1, std::memory_order_relaxed);
                    if (logIndex < 48)
                        WLOG_INFO(
                            "character component palettized path=%s bytes=%u->%u transcode=%u maxEdge=%u",
                            name ? name : "", rawLen,
                            static_cast<unsigned>(current.size()),
                            didTranscode ? 1u : 0u, componentMaxEdge);
                    return 1;
                }
            }

            // Retail Interface and model packages commonly ship BLP2 encoding
            // 3 as raw BGRA. A 2048x2048 atlas then occupies 16 MB; DXT5 keeps
            // the same dimensions and alpha while retaining only 4 MB.
            if (blp::TranscodeBlp(current, transcoded))
            {
                current = std::span<const uint8_t>(transcoded.data(),
                                                   transcoded.size());
                changed = didTranscode = true;
            }

            if (!changed) return 0;

            sink->Write(sink->ctx, current.data(),
                        static_cast<uint32_t>(current.size()));

            if (didCap) g_capped.fetch_add(1, std::memory_order_relaxed);
            if (didTranscode)
                g_transcoded.fetch_add(1, std::memory_order_relaxed);
            if (current.size() < rawLen)
                g_bytesSaved.fetch_add(rawLen - current.size(),
                                       std::memory_order_relaxed);

            const uint32_t logIndex =
                g_logged.fetch_add(1, std::memory_order_relaxed);
            if (logIndex < 48)
                WLOG_INFO(
                    "texture normalized path=%s bytes=%u->%u transcode=%u cap=%u maxEdge=%u",
                    name ? name : "", rawLen,
                    static_cast<unsigned>(current.size()),
                    didTranscode ? 1u : 0u, didCap ? 1u : 0u, maxEdge);
            else if (logIndex == 48)
                WLOG_INFO(
                    "texture normalization summary transcode=%u components=%u cap=%u saved=%.1f MB (per-file logs suppressed)",
                    g_transcoded.load(std::memory_order_relaxed),
                    g_componentPalettized.load(std::memory_order_relaxed),
                    g_capped.load(std::memory_order_relaxed),
                    static_cast<double>(
                        g_bytesSaved.load(std::memory_order_relaxed)) /
                        (1024.0 * 1024.0));
            return 1;
        }
        catch (...)
        {
            // A compatibility transform must never turn memory pressure into
            // an exception crossing the extension ABI. The native handle is
            // rewound and used unchanged when we decline.
            WLOG_WARN("texture normalization failed path=%s bytes=%u",
                      name ? name : "", rawLen);
            return 0;
        }
    }

    const WXL_ModernBlpApi g_modernBlpApi = {
        sizeof(WXL_ModernBlpApi), WXL_MODERN_BLP_API_VERSION, &Transform,
    };
}

const WXL_PluginInfo* __cdecl WXL_Query(void)
{
    static const WXL_PluginInfo info = {
        sizeof(WXL_PluginInfo), WXL_API_VERSION, "wxl-modern-blp", 1,
        WXL_CLIENT_BUILD,
    };
    return &info;
}

int __cdecl WXL_Load(const WXL_Api* api)
{
    if (!api || api->apiVersion != WXL_API_VERSION) return 0;
    wxl_modern_blp::g_api = api;
    wxl_modern_blp::rain::Initialize();

    const auto* storage = static_cast<const WXL_StorageApi*>(
        api->GetInterface("wxl.storage", WXL_STORAGE_API_VERSION));
    if (!storage || !storage->RegisterClientTransform)
    {
        WLOG_ERROR("wxl.storage v1 is unavailable");
        return 0;
    }

    storage->RegisterClientTransform(".blp", &Transform);
    api->PublishInterface(
        "wxl.modern-blp", WXL_MODERN_BLP_API_VERSION,
        const_cast<WXL_ModernBlpApi*>(&g_modernBlpApi));

    WLOG_INFO(
        "modern BLP normalizer active (encoding-3 BGRA -> DXT5, loading edge=%u interface edge=%u world edge=%u)",
        wxl_modern_blp::ConfigU32(
            "WXL_BLP_LOADING_SCREEN_MAX_EDGE", 2048, 1024, 4096),
        wxl_modern_blp::ConfigU32(
            "WXL_BLP_INTERFACE_MAX_EDGE", 1024, 512, 4096),
        wxl_modern_blp::ConfigU32(
            "WXL_BLP_WORLD_MAX_EDGE", 512, 256, 4096));
    return 1;
}
