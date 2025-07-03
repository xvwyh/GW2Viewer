export module GW2Viewer.Data.Encryption.Manager;
import GW2Viewer.Common;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Encryption.Asset;
import GW2Viewer.Data.Encryption.Text;
import std;

export namespace GW2Viewer::Data::Encryption
{

class Manager
{
public:
    [[nodiscard]] auto& Mutex() { return m_lock; }

    TextKeyInfo* AddTextKeyInfo(uint32 stringID, TextKeyInfo info)
    {
        //std::unique_lock _(m_lock);
        return m_textKeysByOrder.emplace_back(&(m_textKeys[stringID] = std::move(info)));
    }
    [[nodiscard]] TextKeyInfo* GetTextKeyInfo(uint32 stringID)
    {
        std::shared_lock _(m_lock);
        if (auto const itr = m_textKeys.find(stringID); itr != m_textKeys.end())
            return &itr->second;
        return nullptr;
    }
    [[nodiscard]] TextKeyInfo const* GetTextKeyInfo(uint32 stringID) const
    {
        std::shared_lock _(m_lock);
        if (auto const itr = m_textKeys.find(stringID); itr != m_textKeys.end())
            return &itr->second;
        return nullptr;
    }
    [[nodiscard]] std::optional<uint64> GetTextKey(uint32 stringID) const
    {
        if (auto const info = GetTextKeyInfo(stringID); info && info->Key)
            return info->Key;
        return { };
    }

    void AddAssetKey(AssetType assetType, uint32 assetID, uint64 key)
    {
        //std::unique_lock _(m_lock);
        m_assetKeys[{ assetType, assetID }] = key;
    }
    std::optional<uint64> GetAssetKey(AssetType assetType, uint32 assetID) const
    {
        std::shared_lock _(m_lock);
        if (auto const itr = m_assetKeys.find({ assetType, assetID }); itr != m_assetKeys.end())
            return itr->second;
        return { };
    }

private:
    mutable std::shared_mutex m_lock;
    std::unordered_map<uint32, TextKeyInfo> m_textKeys;
    std::vector<TextKeyInfo*> m_textKeysByOrder;
    std::map<std::pair<AssetType, uint32>, uint64> m_assetKeys;
};

}
