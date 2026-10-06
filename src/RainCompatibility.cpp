// Rain-texture compatibility for Wrath's 2x-modulate weather pass.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"
#include "engine/assets/shared/textures/blp/BlpTranscode.hpp"
#include "wxl/StorageApi.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace wxl_modern_blp::rain
{
    namespace
    {
    namespace blp = wxl::modern::assets::textures::blp;

    constexpr uint32_t kMagicBlp2 = 0x32504C42; // 'BLP2'
    constexpr uint32_t kHeaderSize = 0x94;
    constexpr uint32_t kPaletteSize = 256 * 4;
    constexpr uint32_t kEncoding = 0x08;
    constexpr uint32_t kAlphaDepth = 0x09;
    constexpr uint32_t kAlphaType = 0x0A;
    constexpr uint32_t kWidth = 0x0C;
    constexpr uint32_t kHeight = 0x10;
    constexpr uint32_t kMipOffsets = 0x14;
    constexpr uint32_t kMipSizes = 0x54;

    std::atomic<uint32_t> g_seen{0};
    bool g_enabled = true;

    uint32_t ReadU32(const uint8_t* p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
               (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    uint64_t Fnv1a64(const uint8_t* bytes, uint32_t size)
    {
        uint64_t hash = 14695981039346656037ull;
        for (uint32_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool EqualFoldedPath(const char* name, std::string_view expected)
    {
        if (!name) return false;
        size_t i = 0;
        for (; i < expected.size(); ++i)
        {
            const unsigned char raw = static_cast<unsigned char>(name[i]);
            if (!raw) return false;
            const char folded = name[i] == '\\'
                ? '/'
                : static_cast<char>(std::tolower(raw));
            if (folded != expected[i]) return false;
        }
        return name[i] == '\0';
    }

    int RainTextureIndex(const char* name)
    {
        if (EqualFoldedPath(name, "textures/weather/raindrop01.blp")) return 0;
        if (EqualFoldedPath(name, "textures/weather/raindropsplash01.blp")) return 1;
        return -1;
    }

    bool IsBlp2(const uint8_t* raw, uint32_t rawLen)
    {
        return raw && rawLen >= kHeaderSize && ReadU32(raw) == kMagicBlp2 &&
               ReadU32(raw + 4) == 1;
    }

    uint8_t ModulateChannel(uint8_t value)
    {
        // Wrath blend state 5 computes 2*src*dst. 128 is therefore neutral;
        // keeping every source channel at or above it prevents black quads.
        return static_cast<uint8_t>(128u + (uint32_t(value) >> 1));
    }

    bool BiasPalette(std::vector<uint8_t>& bytes)
    {
        if (!IsBlp2(bytes.data(), static_cast<uint32_t>(bytes.size())) ||
            bytes[kEncoding] != 1 || bytes.size() < kHeaderSize + kPaletteSize)
            return false;

        for (uint32_t i = 0; i < 256; ++i)
        {
            uint8_t* color = bytes.data() + kHeaderSize + i * 4;
            color[0] = ModulateChannel(color[0]); // B
            color[1] = ModulateChannel(color[1]); // G
            color[2] = ModulateChannel(color[2]); // R
        }
        return true;
    }

    bool BiasRawBgra(std::vector<uint8_t>& bytes)
    {
        if (!IsBlp2(bytes.data(), static_cast<uint32_t>(bytes.size())) ||
            bytes[kEncoding] != 3)
            return false;

        const uint32_t width = ReadU32(bytes.data() + kWidth);
        const uint32_t height = ReadU32(bytes.data() + kHeight);
        if (!width || !height || width > 4096 || height > 4096) return false;

        bool changed = false;
        for (uint32_t mip = 0; mip < 16; ++mip)
        {
            const uint32_t offset = ReadU32(bytes.data() + kMipOffsets + mip * 4);
            const uint32_t size = ReadU32(bytes.data() + kMipSizes + mip * 4);
            if (!offset || !size) continue;
            if (offset > bytes.size() || size > bytes.size() - offset) return false;

            const uint32_t mipWidth = (width >> mip) ? (width >> mip) : 1;
            const uint32_t mipHeight = (height >> mip) ? (height >> mip) : 1;
            const uint64_t pixels = uint64_t(mipWidth) * mipHeight;
            if (!pixels || size % pixels != 0) return false;
            const uint32_t bytesPerPixel = static_cast<uint32_t>(size / pixels);
            if (bytesPerPixel != 3 && bytesPerPixel != 4) return false;

            uint8_t* pixel = bytes.data() + offset;
            for (uint64_t i = 0; i < pixels; ++i, pixel += bytesPerPixel)
            {
                pixel[0] = ModulateChannel(pixel[0]);
                pixel[1] = ModulateChannel(pixel[1]);
                pixel[2] = ModulateChannel(pixel[2]);
            }
            changed = true;
        }
        return changed;
    }

    bool BuildModulateCompatible(const uint8_t* raw, uint32_t rawLen,
                                 std::vector<uint8_t>& out)
    {
        if (!IsBlp2(raw, rawLen)) return false;

        const uint8_t encoding = raw[kEncoding];
        if (encoding == 1)
        {
            out.assign(raw, raw + rawLen);
            return BiasPalette(out);
        }

        if (encoding == 2)
        {
            if (!blp::TextureComponentToPaletted(
                    std::span<const uint8_t>(raw, rawLen), out, 4096))
                return false;
            return BiasPalette(out);
        }

        if (encoding == 3)
        {
            std::vector<uint8_t> biased(raw, raw + rawLen);
            if (!BiasRawBgra(biased)) return false;
            return blp::TranscodeBlp(
                std::span<const uint8_t>(biased.data(), biased.size()), out);
        }

        return false;
    }

    }

    int __cdecl Transform(const char* name, const uint8_t* raw,
                          uint32_t rawLen, const WXL_ByteSink* sink)
    {
        const int index = RainTextureIndex(name);
        if (index < 0 || !raw || !rawLen || !sink || !sink->Write) return 0;

        const uint32_t bit = 1u << static_cast<uint32_t>(index);
        const bool first =
            (g_seen.fetch_or(bit, std::memory_order_relaxed) & bit) == 0;
        const uint64_t sourceHash = Fnv1a64(raw, rawLen);

        if (first)
        {
            if (IsBlp2(raw, rawLen))
            {
                g_api->Log(
                    WXL_LOG_INFO, "wxl-modern-blp",
                    "rain asset path=%s bytes=%u fnv=%08X%08X blp=2 encoding=%u alphaDepth=%u alphaType=%u size=%ux%u mip0=%u+%u enabled=%u",
                    name, rawLen, static_cast<uint32_t>(sourceHash >> 32),
                    static_cast<uint32_t>(sourceHash), raw[kEncoding],
                    raw[kAlphaDepth], raw[kAlphaType], ReadU32(raw + kWidth),
                    ReadU32(raw + kHeight), ReadU32(raw + kMipOffsets),
                    ReadU32(raw + kMipSizes),
                    g_enabled ? 1u : 0u);
            }
            else
            {
                g_api->Log(
                    WXL_LOG_WARN, "wxl-modern-blp",
                    "rain asset path=%s bytes=%u fnv=%08X%08X is not supported BLP2; passing through",
                    name, rawLen, static_cast<uint32_t>(sourceHash >> 32),
                    static_cast<uint32_t>(sourceHash));
            }
        }

        if (!g_enabled) return 0;

        try
        {
            std::vector<uint8_t> normalized;
            if (!BuildModulateCompatible(raw, rawLen, normalized))
            {
                if (first)
                    g_api->Log(
                        WXL_LOG_WARN, "wxl-modern-blp",
                        "rain compatibility declined invalid/unsupported asset path=%s",
                        name);
                return 0;
            }

            sink->Write(sink->ctx, normalized.data(),
                        static_cast<uint32_t>(normalized.size()));
            if (first)
            {
                const uint64_t outputHash = Fnv1a64(
                    normalized.data(), static_cast<uint32_t>(normalized.size()));
                g_api->Log(
                    WXL_LOG_INFO, "wxl-modern-blp",
                    "rain compatibility applied path=%s bytes=%u->%u fnv=%08X%08X outputEncoding=%u",
                    name, rawLen, static_cast<uint32_t>(normalized.size()),
                    static_cast<uint32_t>(outputHash >> 32),
                    static_cast<uint32_t>(outputHash), normalized[kEncoding]);
            }
            return 1;
        }
        catch (...)
        {
            if (first)
                g_api->Log(WXL_LOG_WARN, "wxl-modern-blp",
                           "rain compatibility failed path=%s; passing through",
                           name);
            return 0;
        }
    }

    void Initialize()
    {
        g_enabled = wxl_modern_blp::ConfigU32(
                        "WXL_BLP_RAIN_MODULATE", 1, 0, 1) != 0;
        WLOG_INFO("rain 2x-modulate compatibility %s",
                  g_enabled ? "enabled" : "disabled");
    }
}
