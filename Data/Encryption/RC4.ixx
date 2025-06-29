export module GW2Viewer.Data.Encryption.RC4;
import GW2Viewer.Common;
import std;

export namespace Data::Encryption
{

struct RC4
{
    byte x { };
    byte y { };
    std::array<byte, 256> m { };

    RC4(std::span<byte const> const key)
    {
        std::ranges::iota(m, 0);

        byte j = 0;
        for (uint32 i = 0; i < m.size(); ++i)
        {
            j = (byte)(j + m[i] + key[i % key.size()]);
            std::swap(m[i], m[j]);
        }
    }
    void Crypt(std::span<byte> data)
    {
        for (byte& byte : data)
        {
            y += m[++x];
            std::swap(m[x], m[y]);
            byte ^= m[(::byte)(m[x] + m[y])];
        }
    }

    static auto MakeKey(byte const* keyData, uint32 keyBytes)
    {
        std::array<byte, 20> keyA { };
        for (uint32 i = 0; i < 20; ++i)
            keyA[i] = keyData[i % keyBytes];
        for (uint32 i = 20; i < keyBytes; ++i)
            keyA[i % 20] ^= keyData[i];

        auto& key = *reinterpret_cast<std::array<uint32, 5>*>(&keyA);

        uint32 a = std::rotl(key[0] - 0x604B674D, 5) + key[1] + 0x66B0CD0D;
        uint32 const b = (~((key[0] - 0x604B674D) & 0x22222222) & 0x7BF36AE2) + key[2] + std::rotl(a, 5) - 0xCC2A969;
        uint32 const c = std::rotl(key[0] - 0x604B674D, 30);
        uint32 const d = (a & (c ^ 0x59D148C0) ^ 0x59D148C0) + key[3] + std::rotl(b, 5) - 0x298A1B85;
        a = std::rotl(a, 30);
        key[0] += key[4] + (c ^ (b & (a ^ c))) + std::rotl(d, 5) - 0x4BAC3DA7;
        key[1] += d;
        key[2] += std::rotl(b, 30);
        key[3] += a;
        key[4] += c;
        return keyA;
    }
    static auto MakeKey(uint64 key)
    {
        return MakeKey((byte const*)&key, sizeof(key));
    }
};

}
