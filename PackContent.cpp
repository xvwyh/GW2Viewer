#include "PackContent.h"

#include "Archive.h"
#include "Content.h"
#include "ContentConstants.h"
#include "PackFile.h"
#include "ProgressBarContext.h"

#define NATIVE

STATIC(g_firstContentFileID) = 1282830;
STATIC(g_numContentFiles) = 32;

#ifdef NATIVE
#pragma pack(push, 1)
namespace pf
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
    WString64 name;
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
    Array<PackContentTypeInfo> typeInfos;
    Array<PackContentNamespace> namespaces;
    Array<FileName> fileRefs;
    Array<PackContentIndexEntry> indexEntries;
    Array<PackContentLocalOffsetFixup> localOffsets;
    Array<PackContentExternalOffsetFixup> externalOffsets;
    Array<PackContentFileIndexFixup> fileIndices;
    Array<PackContentStringIndexFixup> stringIndices;
    Array<PackContentTrackedReference> trackedReferences;
    Array<WString64> strings;
    Array<byte> content;
};
}
#pragma pack(pop)
pf::PackContent const* g_rootContentFile;
#else
#include "PackFileLayoutTraversal.h"

std::optional<pf::Layout::Traversal::QueryChunk> g_rootContentFile;
#endif

struct LoadedContentFile
{
    std::unique_ptr<pf::PackFile> File;
    std::set<size_t> EntryBoundaries;
    std::unique_ptr<byte[]> UsedContentByteMap;
};
std::vector<LoadedContentFile> g_loadedContentFiles;
std::map<byte const*, std::set<byte const*>> g_contentReferences;

auto GetNamespace(size_t index) { return *std::next(g_contentNamespaces.begin(), index); }

std::set<byte const*> g_contentDataPointers;
void AddReference(byte const* const& target)
{
    if (!g_contentDataPointers.contains(target))
        return;

    auto current = g_contentDataPointers.lower_bound((byte const*)&target);
    if (current == g_contentDataPointers.end())
        current = --g_contentDataPointers.end();
    if (*current > (byte const*)&target)
        --current;

    g_contentReferences[*current].emplace(target);
}

enum class PostProcessStage
{
    GatherContentPointers,
    ProcessFixupsAndCreateObjects,
    ProcessTrackedReferences,
};

void PostProcessContentFile(LoadedContentFile& loaded, PostProcessStage stage)
{
    #ifdef NATIVE
    using namespace pf;
    auto& file = *loaded.File;
    assert(file.Header.HeaderSize == sizeof(file.Header));
    auto& chunk = file.GetFirstChunk();
    assert(chunk.Header.HeaderSize == sizeof(chunk.Header));
    auto& content = (PackContent&)chunk.Data;
    assert(!(content.flags & Content::CONTENT_FLAG_ENCRYPTED)); // TODO: RC4 encrypted

    if (!g_rootContentFile)
        g_rootContentFile = &content;
    #else
    auto const content = loaded.File->QueryChunk(fcc::Main);
    if (!g_rootContentFile)
        g_rootContentFile.emplace(content);
    
    assert(!((uint32)content["flags"] & Content::CONTENT_FLAG_ENCRYPTED)); // TODO: RC4 encrypted

    auto const data = content["content[]"];
    #endif

    switch (stage)
    {
        case PostProcessStage::GatherContentPointers:
        {
            #ifdef NATIVE
            for (auto const& entry : content.indexEntries)
                g_contentDataPointers.emplace(&content.content[entry.offset]);
            #else
            for (auto const& entry : content["indexEntries"])
                g_contentDataPointers.emplace(data[entry["offset"]]);
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
                (byte*&)content.content[relocOffset] = &(*(PackContent*)&g_loadedContentFiles.at(targetFileIndex).File->GetFirstChunk().Data).content[(size_t&)content.content[relocOffset]];
                memset(&usedBytesMap[relocOffset], 0xEE, sizeof(void*));
                AddReference((byte*&)content.content[relocOffset]);
            }
            #else
            for (auto const& fixup : content["externalOffsets"])
            {
                uint32 const relocOffset = fixup["relocOffset"];
                uint32 const targetFileIndex = fixup["targetFileIndex"];
                *(byte**)data[relocOffset] = g_loadedContentFiles.at(targetFileIndex).File->QueryChunk(fcc::Main)["content"][*(uintptr_t*)data[relocOffset]];
                memset(&usedBytesMap[relocOffset], 0xEE, sizeof(void*));
                AddReference(*(byte**)data[relocOffset]);
            }
            #endif

            #ifdef NATIVE
            for (auto const& [relocOffset] : content.fileIndices)
            {
                (byte*&)content.content[relocOffset] = (byte*)g_rootContentFile->fileRefs[(size_t&)content.content[relocOffset]].GetFileID();
                memset(&usedBytesMap[relocOffset], 0xFF, sizeof(void*));
            }
            #else
            for (auto const& fixup : content["fileIndices"])
            {
                uint32 const relocOffset = fixup["relocOffset"];
                *(byte**)data[relocOffset] = (byte*)(uint32)(*g_rootContentFile)["fileRefs"][*(uintptr_t*)data[relocOffset]];
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
                g_contentTypeInfos.reserve(content.typeInfos.size());
                for (auto const& [index, typeInfo] : content.typeInfos | std::views::enumerate)
            #else
            if (auto const typeInfos = content["typeInfos[]"])
            {
                g_contentTypeInfos.reserve(typeInfos.GetArraySize());
                for (auto const& typeInfo : typeInfos)
            #endif
                {
                    g_contentTypeInfos.emplace_back(new ContentTypeInfo
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
                    });
                }
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
                g_contentNamespaces.reserve(content.namespaces.size());
                for (auto const& ns : content.namespaces)
                #else
                g_contentNamespaces.reserve(namespaces.GetArraySize());
                for (auto const& ns : namespaces)
                #endif
                {
                    #ifdef NATIVE
                    auto object = new ContentNamespace { .Index = (uint32)g_contentNamespaces.size(), .Domain = ns.domain, .Name = ns.name.data() };
                    #else
                    auto object = new ContentNamespace { .Index = (uint32)g_contentNamespaces.size(), .Domain = ns["domain"], .Name = ns["name"] };
                    #endif
                    g_contentNamespaces.emplace_back(object);
                    g_contentNamespacesByName[object->Name].emplace_back(object);
                }

                // Organize namespaces into a tree
                #ifdef NATIVE
                for (auto const& [index, ns] : content.namespaces | std::views::enumerate)
                #else
                for (auto const& ns : namespaces)
                #endif
                {
                    #ifdef NATIVE
                    auto* current = GetNamespace(index);
                    auto const parentIndex = ns.parentIndex;
                    #else
                    auto* current = GetNamespace(ns.GetArrayIndex());
                    int32 const parentIndex = ns["parentIndex"];
                    #endif
                    if (parentIndex >= 0)
                    {
                        auto* parent = GetNamespace(parentIndex);
                        current->Parent = parent;
                        parent->Namespaces.emplace_back(current);
                    }
                    else
                    {
                        assert(!g_contentRoot);
                        g_contentRoot = current;
                    }
                }
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
                    //if (g_contentTypeInfos.at(entry.type)->NameOffset >= 0)
                    loaded.EntryBoundaries.emplace(entry.offset);
                #else
                loaded.EntryBoundaries.emplace(data.GetArraySize());
                for (auto const& entry : indexEntries)
                    //if (g_contentTypeInfos.at(entry["type"])->NameOffset >= 0)
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
                    auto* type = g_contentTypeInfos.at(entry.type).get();
                    auto* ns = GetNamespace(entry.namespaceIndex);
                    auto* root = entry.rootIndex >= 0 ? GetContentObjectByDataPointer(&content.content[content.indexEntries[entry.rootIndex].offset]) : nullptr;
                    #else
                    uint32 const offset = entry["offset"];
                    auto* type = g_contentTypeInfos.at(entry["type"]).get();
                    auto* ns = GetNamespace(entry["namespaceIndex"]);
                    int32 const rootIndex = entry["rootIndex"];
                    auto* root = rootIndex >= 0 ? GetContentObjectByDataPointer(data[indexEntries[rootIndex]["offset"]]) : nullptr;
                    #endif

                    ContentObject* object = new ContentObject
                    {
                        .Index = (uint32)g_contentObjects.size(),
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
                    g_contentObjects.emplace_back(object);
                    #ifdef NATIVE
                    assert(g_contentObjectsByDataPointer.emplace(&content.content[entry.offset], object).second);
                    #else
                    assert(g_contentObjectsByDataPointer.emplace(data[offset], object).second);
                    #endif
                    if (auto const guid = object->GetGUID(); guid && !g_contentObjectsByGUID.emplace(*guid, object).second)
                        std::terminate();
                    type->Objects.emplace_back(object);
                    if (root)
                    {
                        root->Entries.emplace_back(object);
                        root->AddReference(*object, ContentObject::Reference::Types::Root);
                        g_rootedContentObjects.emplace_back(object);
                    }
                    else
                    {
                        ns->Entries.emplace_back(object);
                        g_unrootedContentObjects.emplace_back(object);
                    }
                    if (auto const names = object->GetName(); names && names->FullName && *names->FullName && **names->FullName)
                    {
                        std::wstring_view name = *names->FullName;
                        if (auto const pos = name.find_last_of(L'.'); pos != std::wstring_view::npos)
                            name.remove_prefix(pos + 1);
                        g_contentObjectsByName[name].emplace_back(object);
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
                auto const* target = &(*(PackContent*)&g_loadedContentFiles[targetFileIndex].File->GetFirstChunk().Data).content[targetOffset];
                GetContentObjectByDataPointer(source)->AddReference(*g_contentObjectsByDataPointer.at(target), ContentObject::Reference::Types::Tracked);
            }
            #else
            for (auto const& trackedReference : content["trackedReferences"])
            {
                byte const* source = data[trackedReference["sourceOffset"]];
                byte const* target = g_loadedContentFiles[trackedReference["targetFileIndex"]].File->QueryChunk(fcc::Main)["content"][trackedReference["targetOffset"]];
                GetContentObjectByDataPointer(source)->AddReference(*g_contentObjectsByDataPointer.at(target), ContentObject::Reference::Types::Tracked);
            }
            #endif
            break;
        }
    }
}

void LoadContentFiles(Archive& archive, ProgressBarContext& progress)
{
    g_loadedContentFiles.resize(g_numContentFiles);
    uint32 fileID = g_firstContentFileID;
    progress.Start("Loading content files", g_loadedContentFiles.size());
    for (auto& loaded : g_loadedContentFiles)
    {
        loaded.File = archive.GetPackFile(fileID++);
        ++progress;
    }
}
void ProcessContentFiles(ProgressBarContext& progress)
{
    std::array<std::tuple<PostProcessStage, char const*>, 3> STAGES
    { {
        { PostProcessStage::GatherContentPointers, "Marking content pointers" },
        { PostProcessStage::ProcessFixupsAndCreateObjects, "Processing content files" },
        { PostProcessStage::ProcessTrackedReferences, "Processing tracked references" },
    } };
    for (auto const& [stage, description] : STAGES)
    {
        progress.Start(description, g_loadedContentFiles.size());
        for (auto& loaded : g_loadedContentFiles)
        {
            PostProcessContentFile(loaded, stage);
            ++progress;
        }
    }

    g_contentLoaded = true;

    progress.Start("Processing all references", g_contentReferences.size());
    for (auto const& [source, targets] : g_contentReferences)
    {
        auto* sourceObject = GetContentObjectByDataPointer(source);
        for (auto const& target : targets)
            sourceObject->AddReference(*GetContentObjectByDataPointer(target), ContentObject::Reference::Types::All);
        ++progress;
    }
}
