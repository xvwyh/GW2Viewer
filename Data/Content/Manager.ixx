export module GW2Viewer.Data.Content.Manager;
import GW2Viewer.Common;
import GW2Viewer.Common.FourCC;
import GW2Viewer.Common.GUID;
import GW2Viewer.Content;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Pack;
import GW2Viewer.Data.Pack.PackFile;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Async.ProgressBarContext;
import GW2Viewer.Utils.Container;
import std;
import <cassert>;
import <cstring>;

#define NATIVE

#ifdef NATIVE
#pragma pack(push, 1)
namespace GW2Viewer::Data::Content
{

struct PackContentTypeInfo
{
    int32 guidOffset;
    int32 uidOffset;
    int32 dataIdOffset;
    int32 nameOffset;
    byte trackReferences;
};
struct PackContentNamespace
{
    Pack::WString64 name;
    int32 domain;
    int32 parentIndex;
};
struct PackContentIndexEntry
{
    uint32 type;
    uint32 offset;
    uint32 namespaceIndex;
    int32 rootIndex;
};
struct PackContentLocalOffsetFixup
{
    uint32 relocOffset;
};
struct PackContentExternalOffsetFixup
{
    uint32 relocOffset;
    uint32 targetFileIndex;
};
struct PackContentFileIndexFixup
{
    uint32 relocOffset;
};
struct PackContentStringIndexFixup
{
    uint32 relocOffset;
};
struct PackContentTrackedReference
{
    uint32 sourceOffset;
    uint32 targetFileIndex;
    uint32 targetOffset;
};
struct PackContent
{
    uint32 flags; // &1 - CONTENT_FLAG_ENCRYPTED, &2 - name mangled
    Pack::Array<PackContentTypeInfo> typeInfos;
    Pack::Array<PackContentNamespace> namespaces;
    Pack::Array<Pack::FileName> fileRefs;
    Pack::Array<PackContentIndexEntry> indexEntries;
    Pack::Array<PackContentLocalOffsetFixup> localOffsets;
    Pack::Array<PackContentExternalOffsetFixup> externalOffsets;
    Pack::Array<PackContentFileIndexFixup> fileIndices;
    Pack::Array<PackContentStringIndexFixup> stringIndices;
    Pack::Array<PackContentTrackedReference> trackedReferences;
    Pack::Array<Pack::WString64> strings;
    Pack::Array<byte> content;
};
}
#pragma pack(pop)
#endif

export namespace GW2Viewer::Data::Content
{

class Manager
{
public:
    void Load(Utils::Async::ProgressBarContext& progress);
    void Process(Utils::Async::ProgressBarContext& progress)
    {
        std::array<std::tuple<PostProcessStage, char const*>, 3> STAGES
        { {
            { PostProcessStage::GatherContentPointers, "Marking content pointers" },
            { PostProcessStage::ProcessFixupsAndCreateObjects, "Processing content files" },
            { PostProcessStage::ProcessTrackedReferences, "Processing tracked references" },
        } };
        for (auto const& [stage, description] : STAGES)
        {
            progress.Start(description, m_loadedContentFiles.size());
            for (auto& loaded : m_loadedContentFiles)
            {
                PostProcessContentFile(loaded, stage);
                ++progress;
            }
        }
        assert(GetNamespaceRoot());

        m_loadedObjects = true;

        progress.Start("Processing all references", m_references.size());
        for (auto const& [source, targets] : m_references)
        {
            auto* sourceObject = GetByDataPointerMutable(source);
            for (auto const& target : targets)
                sourceObject->AddReference(*GetByDataPointerMutable(target), ContentObject::Reference::Types::All);
            ++progress;
        }

        m_loaded = true;
    }
    [[nodiscard]] bool IsLoaded() const { return m_loaded; }
    [[nodiscard]] auto GetFileIDs() const { return std::views::iota(m_firstContentFileID) | std::views::take(m_numContentFiles); }

    [[nodiscard]] bool AreTypesLoaded() const { return m_loadedTypes; }
    [[nodiscard]] uint32 GetNumTypes() const { return m_typeInfos.size(); }
    [[nodiscard]] std::span<ContentTypeInfo const* const> GetTypes() const { return m_typeInfoPointers; }
    [[nodiscard]] ContentTypeInfo const* GetType(uint32 index) const { return m_typeInfos.at(index).get(); }
    [[nodiscard]] ContentTypeInfo const* GetType(GW2Viewer::Content::EContentTypes type) const
    {
        if (auto const itr = std::ranges::find(G::Config.TypeInfo, type, [](auto const& pair) { return pair.second.ContentType; }); itr != G::Config.TypeInfo.end())
            if (itr->first < m_typeInfos.size())
                return m_typeInfos.at(itr->first).get();

        return nullptr;
    }
    [[nodiscard]] ContentTypeInfo const* GetType(std::string_view name) const
    {
        if (auto const itr = std::ranges::find(G::Config.TypeInfo, name, [](auto const& pair) -> auto& { return pair.second.Name; }); itr != G::Config.TypeInfo.end())
            if (itr->first < m_typeInfos.size())
                return m_typeInfos.at(itr->first).get();

        return nullptr;
    }

    [[nodiscard]] bool AreNamespacesLoaded() const { return m_loadedNamespaces; }
    [[nodiscard]] std::span<ContentNamespace const* const> GetNamespaces() const { return m_namespaces; }
    [[nodiscard]] ContentNamespace const* GetNamespaceRoot() const { return m_root.get(); }
    [[nodiscard]] ContentNamespace const* GetNamespace(uint32 index) const { return GetNamespaceMutable(index); }

    [[nodiscard]] bool AreObjectsLoaded() const { return m_loadedObjects; }
    [[nodiscard]] std::span<ContentObject const* const> GetObjects() const { return m_objects; }
    [[nodiscard]] std::span<ContentObject const* const> GetRootedObjects() const { return m_rootedObjects; }
    [[nodiscard]] std::span<ContentObject const* const> GetUnrootedObjects() const { return m_unrootedObjects; }
    [[nodiscard]] ContentObject const* GetByIndex(uint32 index) const { return m_objects.at(index); }
    [[nodiscard]] ContentObject const* GetByGUID(GUID const& guid) const { if (auto const object = Utils::Container::Find(m_objectsByGUID, guid)) return *object; return nullptr; }
    [[nodiscard]] ContentObject const* GetByDataPointer(byte const* ptr) const { return GetByDataPointerMutable(ptr); }
    [[nodiscard]] ContentObject const* GetByDataID(GW2Viewer::Content::EContentTypes type, uint32 dataID) const
    {
        if (auto const typeInfo = GetType(type))
        {
            if (typeInfo->DataIDOffset < 0)
                std::terminate();

            for (auto const object : typeInfo->Objects)
                if (*object->GetDataID() == dataID)
                    return object;
        }

        return nullptr;
    }

    [[nodiscard]] auto GetByName(std::wstring_view name) const { return Utils::Container::Find(m_objectsByName, name); }
    [[nodiscard]] auto GetNamespacesByName(std::wstring_view name) const { return Utils::Container::Find(m_namespacesByName, name); }

private:
    bool m_loaded = false;
    uint32 m_firstContentFileID = 1282830;
    uint32 m_numContentFiles = 32;

    bool m_loadedTypes = false;
    std::vector<std::unique_ptr<ContentTypeInfo>> m_typeInfos;
    std::vector<ContentTypeInfo const*> m_typeInfoPointers;

    bool m_loadedNamespaces = false;
    std::unique_ptr<ContentNamespace> m_root;
    std::vector<ContentNamespace*> m_namespaces;

    bool m_loadedObjects = false;
    std::vector<ContentObject*> m_objects;
    std::vector<ContentObject*> m_rootedObjects;
    std::vector<ContentObject*> m_unrootedObjects;
    std::unordered_map<byte const*, ContentObject*> m_objectsByDataPointer;
    std::unordered_map<GUID, ContentObject*> m_objectsByGUID;

    std::unordered_map<std::wstring_view, std::vector<ContentObject*>> m_objectsByName;
    std::unordered_map<std::wstring_view, std::vector<ContentNamespace*>> m_namespacesByName;

    [[nodiscard]] ContentNamespace* GetNamespaceMutable(uint32 index) const { return m_namespaces.at(index); }
    [[nodiscard]] ContentObject* GetByDataPointerMutable(byte const* ptr) const { if (auto const object = Utils::Container::Find(m_objectsByDataPointer, ptr)) return *object; return nullptr; }

#ifdef NATIVE
    PackContent const* m_rootContentFile = nullptr;
#else
    std::optional<Pack::Layout::Traversal::QueryChunk> m_rootContentFile;
#endif
    struct LoadedContentFile
    {
        std::unique_ptr<Pack::PackFile> File;
        std::set<size_t> EntryBoundaries;
        std::unique_ptr<byte[]> UsedContentByteMap;
    };
    std::vector<LoadedContentFile> m_loadedContentFiles;
    enum class PostProcessStage
    {
        GatherContentPointers,
        ProcessFixupsAndCreateObjects,
        ProcessTrackedReferences,
    };
    void PostProcessContentFile(LoadedContentFile& loaded, PostProcessStage stage)
    {
        #ifdef NATIVE
        auto& file = *loaded.File;
        assert(file.Header.HeaderSize == sizeof(file.Header));
        auto& chunk = file.GetFirstChunk();
        assert(chunk.Header.HeaderSize == sizeof(chunk.Header));
        auto& content = (PackContent&)chunk.Data;
        assert(!(content.flags & GW2Viewer::Content::CONTENT_FLAG_ENCRYPTED)); // TODO: RC4 encrypted

        if (!m_rootContentFile)
            m_rootContentFile = &content;
        #else
        auto const content = loaded.File->QueryChunk(fcc::Main);
        if (!m_rootContentFile)
            m_rootContentFile.emplace(content);

        assert(!((uint32)content["flags"] & GW2Viewer::Content::CONTENT_FLAG_ENCRYPTED)); // TODO: RC4 encrypted

        auto const data = content["content[]"];
        #endif

        switch (stage)
        {
            case PostProcessStage::GatherContentPointers:
            {
                #ifdef NATIVE
                for (auto const& entry : content.indexEntries)
                    m_contentDataPointers.emplace(&content.content[entry.offset]);
                #else
                for (auto const& entry : content["indexEntries"])
                    m_contentDataPointers.emplace(data[entry["offset"]]);
                #endif
                break;
            }
            case PostProcessStage::ProcessFixupsAndCreateObjects:
            {
                #ifdef NATIVE
                loaded.UsedContentByteMap = std::make_unique<byte[]>(content.content.size());
                byte* usedBytesMap = loaded.UsedContentByteMap.get();
                memset(usedBytesMap, 0, content.content.size());
                #else
                loaded.UsedContentByteMap = std::make_unique<byte[]>(data.GetArraySize());
                byte* usedBytesMap = loaded.UsedContentByteMap.get();
                memset(usedBytesMap, 0, data.GetArraySize());
                #endif

                #ifdef NATIVE
                for (auto const& [relocOffset] : content.localOffsets)
                {
                    (byte*&)content.content[relocOffset] += (size_t)content.content.data();
                    memset(&usedBytesMap[relocOffset], 0xAA, sizeof(void*));
                    AddReference((byte*&)content.content[relocOffset]);
                }
                #else
                for (auto const& fixup : content["localOffsets"])
                {
                    uint32 const relocOffset = fixup["relocOffset"];
                    *(byte**)data[relocOffset] += (uintptr_t)data.GetPointer();
                    memset(&usedBytesMap[relocOffset], 0xAA, sizeof(void*));
                    AddReference(*(byte**)data[relocOffset]);
                }
                #endif

                #ifdef NATIVE
                for (auto const& [relocOffset, targetFileIndex] : content.externalOffsets)
                {
                    (byte*&)content.content[relocOffset] = &(*(PackContent*)&m_loadedContentFiles.at(targetFileIndex).File->GetFirstChunk().Data).content[(size_t&)content.content[relocOffset]];
                    memset(&usedBytesMap[relocOffset], 0xEE, sizeof(void*));
                    AddReference((byte*&)content.content[relocOffset]);
                }
                #else
                for (auto const& fixup : content["externalOffsets"])
                {
                    uint32 const relocOffset = fixup["relocOffset"];
                    uint32 const targetFileIndex = fixup["targetFileIndex"];
                    *(byte**)data[relocOffset] = m_loadedContentFiles.at(targetFileIndex).File->QueryChunk(fcc::Main)["content"][*(uintptr_t*)data[relocOffset]];
                    memset(&usedBytesMap[relocOffset], 0xEE, sizeof(void*));
                    AddReference(*(byte**)data[relocOffset]);
                }
                #endif

                #ifdef NATIVE
                for (auto const& [relocOffset] : content.fileIndices)
                {
                    (byte*&)content.content[relocOffset] = (byte*)m_rootContentFile->fileRefs[(size_t&)content.content[relocOffset]].GetFileID();
                    memset(&usedBytesMap[relocOffset], 0xFF, sizeof(void*));
                }
                #else
                for (auto const& fixup : content["fileIndices"])
                {
                    uint32 const relocOffset = fixup["relocOffset"];
                    *(byte**)data[relocOffset] = (byte*)(uint32)(*m_rootContentFile)["fileRefs"][*(uintptr_t*)data[relocOffset]];
                    memset(&usedBytesMap[relocOffset], 0xFF, sizeof(void*));
                }
                #endif

                #ifdef NATIVE
                for (auto const& [relocOffset] : content.stringIndices)
                {
                    (byte*&)content.content[relocOffset] = (byte*)content.strings[(size_t&)content.content[relocOffset]].data();
                    memset(&usedBytesMap[relocOffset], 0xBB, sizeof(void*));
                }
                #else
                for (auto const strings = content["strings"]; auto const& fixup : content["stringIndices"])
                {
                    uint32 const relocOffset = fixup["relocOffset"];
                    *(byte**)data[relocOffset] = (byte*)((std::wstring_view)strings[*(uintptr_t*)data[relocOffset]]).data();
                    memset(&usedBytesMap[relocOffset], 0xBB, sizeof(void*));
                }
                #endif

                // Read type infos (root content file only)
                #ifdef NATIVE
                if (!content.typeInfos.empty())
                {
                    m_typeInfos.reserve(content.typeInfos.size());
                    for (auto const& [index, typeInfo] : content.typeInfos | std::views::enumerate)
                #else
                if (auto const typeInfos = content["typeInfos[]"])
                {
                    m_typeInfos.reserve(typeInfos.GetArraySize());
                    for (auto const& typeInfo : typeInfos)
                #endif
                    {
                        (void)m_typeInfos.emplace_back(new ContentTypeInfo
                        {
                            #ifdef NATIVE
                            .Index = (uint32)index,
                            .GUIDOffset = typeInfo.guidOffset,
                            .UIDOffset = typeInfo.uidOffset,
                            .DataIDOffset = typeInfo.dataIdOffset,
                            .NameOffset = typeInfo.nameOffset,
                            .TrackReferences = (bool)typeInfo.trackReferences,
                            #else
                            .Index = typeInfo.GetArrayIndex(),
                            .GUIDOffset = typeInfo["guidOffset"],
                            .UIDOffset = typeInfo["uidOffset"],
                            .DataIDOffset = typeInfo["dataIdOffset"],
                            .NameOffset = typeInfo["nameOffset"],
                            .TrackReferences = (bool)typeInfo["trackReferences"],
                            #endif
                        })->GetTypeInfo(); // Ensure that it's initialized in config
                    }
                    m_typeInfoPointers.assign_range(m_typeInfos | std::views::transform([](auto const& ptr) { return ptr.get(); }));

                    m_loadedTypes = true;
                }

                // Read namespaces (root content file only)
                #ifdef NATIVE
                if (!content.namespaces.empty())
                #else
                if (auto const namespaces = content["namespaces[]"])
                #endif
                {
                    // Create runtime objects for namespaces
                    #ifdef NATIVE
                    m_namespaces.reserve(content.namespaces.size());
                    for (auto const& ns : content.namespaces)
                    #else
                    m_namespaces.reserve(namespaces.GetArraySize());
                    for (auto const& ns : namespaces)
                    #endif
                    {
                        #ifdef NATIVE
                        auto object = new ContentNamespace { .Index = (uint32)m_namespaces.size(), .Domain = ns.domain, .Name = ns.name.data() };
                        #else
                        auto object = new ContentNamespace { .Index = (uint32)m_namespaces.size(), .Domain = ns["domain"], .Name = ns["name"] };
                        #endif
                        m_namespaces.emplace_back(object);
                        m_namespacesByName[object->Name].emplace_back(object);
                    }

                    // Organize namespaces into a tree
                    #ifdef NATIVE
                    for (auto const& [index, ns] : content.namespaces | std::views::enumerate)
                    #else
                    for (auto const& ns : namespaces)
                    #endif
                    {
                        #ifdef NATIVE
                        auto* current = GetNamespaceMutable(index);
                        auto const parentIndex = ns.parentIndex;
                        #else
                        auto* current = GetNamespaceMutable(ns.GetArrayIndex());
                        int32 const parentIndex = ns["parentIndex"];
                        #endif
                        if (parentIndex >= 0)
                        {
                            auto* parent = GetNamespaceMutable(parentIndex);
                            current->Parent = parent;
                            parent->Namespaces.emplace_back(current);
                        }
                        else
                        {
                            assert(!m_root);
                            m_root.reset(current);
                        }
                    }

                    m_loadedNamespaces = true;
                }

                // Read entries
                #ifdef NATIVE
                if (!content.indexEntries.empty())
                #else
                if (auto const indexEntries = content["indexEntries"])
                #endif
                {
                    // Determine the boundaries of each entry
                    #ifdef NATIVE
                    loaded.EntryBoundaries.emplace(content.content.size());
                    for (auto const& entry : content.indexEntries)
                        //if (m_typeInfos.at(entry.type)->NameOffset >= 0)
                        loaded.EntryBoundaries.emplace(entry.offset);
                    #else
                    loaded.EntryBoundaries.emplace(data.GetArraySize());
                    for (auto const& entry : indexEntries)
                        //if (m_typeInfos.at(entry["type"])->NameOffset >= 0)
                            loaded.EntryBoundaries.emplace(entry["offset"]);
                    #endif
                    // Read entries and add them to namespace tree
                    #ifdef NATIVE
                    for (auto const& entry : content.indexEntries)
                    #else
                    for (auto const& entry : indexEntries)
                    #endif
                    {
                        // Calculation moved to runtime to speed up loading
                        #ifdef NATIVE
                        //auto itr = std::ranges::upper_bound(loaded.EntryBoundaries, entry.offset);
                        #else
                        //auto itr = std::ranges::upper_bound(loaded.EntryBoundaries, (size_t)entry["offset"]);
                        #endif
                        //assert(itr != loaded.EntryBoundaries.end());

                        #ifdef NATIVE
                        auto const offset = entry.offset;
                        auto* type = m_typeInfos.at(entry.type).get();
                        auto* ns = GetNamespaceMutable(entry.namespaceIndex);
                        auto* root = entry.rootIndex >= 0 ? GetByDataPointerMutable(&content.content[content.indexEntries[entry.rootIndex].offset]) : nullptr;
                        #else
                        uint32 const offset = entry["offset"];
                        auto* type = m_typeInfos.at(entry["type"]).get();
                        auto* ns = GetNamespaceMutable(entry["namespaceIndex"]);
                        int32 const rootIndex = entry["rootIndex"];
                        auto* root = rootIndex >= 0 ? GetByDataPointerMutable(data[indexEntries[rootIndex]["offset"]]) : nullptr;
                        #endif

                        ContentObject* object = new ContentObject
                        {
                            .Index = (uint32)m_objects.size(),
                            .Type = type,
                            .Namespace = ns,
                            .Root = root,
                            #ifdef NATIVE
                            .Data = { &content.content[entry.offset], ContentObject::UNINITIALIZED_SIZE /*&content.content[*itr]*/ },
                            #else
                            .Data = { (byte const*)data[offset], ContentObject::UNINITIALIZED_SIZE /*(byte const*)data[*itr]*/ },
                            #endif
                            .ContentFileEntryOffset = offset,
                            .ContentFileEntryBoundaries = &loaded.EntryBoundaries,
                            .ByteMap = &usedBytesMap[offset],
                        };
                        m_objects.emplace_back(object);
                        #ifdef NATIVE
                        assert(m_objectsByDataPointer.emplace(&content.content[entry.offset], object).second);
                        #else
                        assert(m_objectsByDataPointer.emplace(data[offset], object).second);
                        #endif
                        if (auto const guid = object->GetGUID(); guid && !m_objectsByGUID.emplace(*guid, object).second)
                            std::terminate();
                        type->Objects.emplace_back(object);
                        if (root)
                        {
                            root->Entries.emplace_back(object);
                            root->AddReference(*object, ContentObject::Reference::Types::Root);
                            m_rootedObjects.emplace_back(object);
                        }
                        else
                        {
                            ns->Entries.emplace_back(object);
                            m_unrootedObjects.emplace_back(object);
                        }
                        if (auto const names = object->GetName(); names && names->FullName && *names->FullName && **names->FullName)
                        {
                            std::wstring_view name = *names->FullName;
                            if (auto const pos = name.find_last_of(L'.'); pos != std::wstring_view::npos)
                                name.remove_prefix(pos + 1);
                            m_objectsByName[name].emplace_back(object);
                        }
                    }
                }
                break;
            }
            case PostProcessStage::ProcessTrackedReferences:
            {
                #ifdef NATIVE
                for (auto const& [sourceOffset, targetFileIndex, targetOffset] : content.trackedReferences)
                {
                    auto const* source = &content.content[sourceOffset];
                    auto const* target = &(*(PackContent*)&m_loadedContentFiles[targetFileIndex].File->GetFirstChunk().Data).content[targetOffset];
                #else
                for (auto const& trackedReference : content["trackedReferences"])
                {
                    byte const* source = data[trackedReference["sourceOffset"]];
                    byte const* target = m_loadedContentFiles[trackedReference["targetFileIndex"]].File->QueryChunk(fcc::Main)["content"][trackedReference["targetOffset"]];
                #endif
                    GetByDataPointerMutable(source)->AddReference(*GetByDataPointerMutable(target), ContentObject::Reference::Types::Tracked);
                }
                break;
            }
        }
    }

    std::set<byte const*> m_contentDataPointers;
    std::map<byte const*, std::set<byte const*>> m_references;
    void AddReference(byte const* const& target)
    {
        if (!m_contentDataPointers.contains(target))
            return;

        auto current = m_contentDataPointers.lower_bound((byte const*)&target);
        if (current == m_contentDataPointers.end())
            current = --m_contentDataPointers.end();
        if (*current > (byte const*)&target)
            --current;

        m_references[*current].emplace(target);
    }
};

}
