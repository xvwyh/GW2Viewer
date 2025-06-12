#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <numeric>
#include <span>

struct RC4
{
    uint8_t x { };
    uint8_t y { };
    std::array<uint8_t, 256> m { };

    RC4(std::span<uint8_t const> const key)
    {
        std::ranges::iota(m, 0);

        uint8_t j = 0;
        for (uint32_t i = 0; i < m.size(); ++i)
        {
            j = (uint8_t)(j + m[i] + key[i % key.size()]);
            std::swap(m[i], m[j]);
        }
    }
    void Crypt(std::span<uint8_t> data)
    {
        for (uint8_t& byte : data)
        {
            y += m[++x];
            std::swap(m[x], m[y]);
            byte ^= m[(uint8_t)(m[x] + m[y])];
        }
    }

    static auto MakeKey(uint8_t const* keyData, uint32_t keyBytes)
    {
        std::array<uint8_t, 20> keyA { };
        for (uint32_t i = 0; i < 20; ++i)
            keyA[i] = keyData[i % keyBytes];
        for (uint32_t i = 20; i < keyBytes; ++i)
            keyA[i % 20] ^= keyData[i];

        auto& key = *reinterpret_cast<std::array<uint32_t, 5>*>(&keyA);

        uint32_t a = std::rotl(key[0] - 0x604B674D, 5) + key[1] + 0x66B0CD0D;
        uint32_t const b = (~((key[0] - 0x604B674D) & 0x22222222) & 0x7BF36AE2) + key[2] + std::rotl(a, 5) - 0xCC2A969;
        uint32_t const c = std::rotl(key[0] - 0x604B674D, 30);
        uint32_t const d = (a & (c ^ 0x59D148C0) ^ 0x59D148C0) + key[3] + std::rotl(b, 5) - 0x298A1B85;
        a = std::rotl(a, 30);
        key[0] += key[4] + (c ^ (b & (a ^ c))) + std::rotl(d, 5) - 0x4BAC3DA7;
        key[1] += d;
        key[2] += std::rotl(b, 30);
        key[3] += a;
        key[4] += c;
        return keyA;
    }
    static auto MakeKey(uint64_t key)
    {
        return MakeKey((uint8_t const*)&key, sizeof(key));
    }
};
