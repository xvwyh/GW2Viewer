export module GW2Viewer.Data.Content:ContentTypeInfo;
import GW2Viewer.Common;
import std;

export namespace GW2Viewer::Data::Content
{
struct ContentObject;

struct ContentTypeInfo
{
    uint32 Index;
    int32 GUIDOffset;
    int32 UIDOffset;
    int32 DataIDOffset;
    int32 NameOffset;
    bool TrackReferences;
    std::list<ContentObject*> Objects;

    [[nodiscard]] std::wstring GetDisplayName() const;
};

}
