module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Windows.Parse;
import GW2Viewer.Common.GUID;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Data.Content.Mangling;
import GW2Viewer.Data.Pack;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.Utils.ConstString;
import GW2Viewer.Utils.Scan;
import GW2Viewer.Utils.Visitor;

export namespace GW2Viewer::UI::Windows
{

struct Parse : Window
{
    std::string Title() override { return "Parse"; }
    void Draw() override
    {
        //static constexpr auto success = [](auto const& result) { return result && result.empty(); };

        static auto parse = Utils::Visitor::Overloaded
        {
            [](std::string_view in, std::string& out)
            {
                out = in;
                return true;
            },
            [](std::string_view in, GUID& out)
            {
                return (bool)Utils::Scan::Into(in, "{}", out);
            },
            [](std::string_view in, Token32& out)
            {
                out = in;
                return true;
            },
            [](std::string_view in, Token64& out)
            {
                out = in;
                return true;
            },
            [](std::string_view in, Data::Pack::FileReference& out)
            {
                if (uint32 fileID; Utils::Scan::Into(in, "{}", fileID))
                {
                    auto* chars = (uint16*)&out;
                    chars[0] = 0xFF + fileID % 0xFF00;
                    chars[1] = 0x100 + fileID / 0xFF00;
                    chars[2] = 0;
                    return true;
                }
                return false;
            },
            [](std::string_view in, std::array<byte, 16>& out)
            {
                return Utils::Scan::Into(in, "{:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x}", out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], out[8], out[9], out[10], out[11], out[12], out[13], out[14], out[15]);
            },
            [](std::string_view in, std::array<uint32, 4>& out)
            {
                return Utils::Scan::Into(in, "{} {} {} {}", out[0], out[1], out[2], out[3]) || Utils::Scan::Into(in, "0x{:x} 0x{:x} 0x{:x} 0x{:x}", out[0], out[1], out[2], out[3]);
            },
            [](std::string_view in, std::array<uint64, 2>& out)
            {
                return Utils::Scan::Into(in, "{} {}", out[0], out[1]) || Utils::Scan::Into(in, "0x{:x} 0x{:x}", out[0], out[1]);
            },
            []<std::integral T>(std::string_view in, T& out)
            {
                /* TODO
                if (in.contains(' '))
                {
                    auto type = [&]<typename BufferT>(BufferT)
                    {
                        std::vector<BufferT> tokens;
                        BufferT buffer;
                        auto range = std::ranges::subrange(in);
                        out = { };
                        size_t i = 0;
                        while (auto result = Utils::Scan::Into(range, "{:02x}", buffer))
                        {
                            out |= (T)buffer << i++ * 8 * sizeof(BufferT);
                            range = result.Rest();
                            //if (result.empty() || std::ranges::all_of(range, isspace))
                            //    return true;
                        }
                        return false;
                    };
                    if (type(byte())) return true;
                    if (type(uint16())) return true;
                }
                else*/ if (Utils::Scan::Into(in, "0x{:x}", out))
                    return true;
                else if (Utils::Scan::Into(in, "{}", out))
                    return true;

                return false;
            },
            [](std::string_view in, auto& out) { return false; }
        };
        static auto print = Utils::Visitor::Overloaded
        {
            []<std::integral T>(T const& in) { return std::format("{}", in); },
            [](GUID const& in) { return std::format("{}", in); },
            [](Token32 const& in) { return std::format("{}", in.GetString().data()); },
            [](Token64 const& in) { return std::format("{}", in.GetString().data()); },
            [](Data::Pack::FileReference const& in) { return std::format("{}", in.GetFileID()); },
            [](std::array<byte, 16> const& in) { return std::format("{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}", in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7], in[8], in[9], in[10], in[11], in[12], in[13], in[14], in[15]); },
            [](std::array<uint32, 4> const& in) { return std::format("0x{:X} 0x{:X} 0x{:X} 0x{:X}", in[0], in[1], in[2], in[3]); },
            [](std::array<uint64, 2> const& in) { return std::format("0x{:X} 0x{:X}", in[0], in[1]); },
            [](std::string_view const& in) { return in; },
            [](std::wstring_view const& in) { return Utils::Encoding::ToUTF8(in); },
        };
        static auto conversion = []<ConstString Label, typename L, typename R>(void(*convertLR)(L const& in, R& out), void(*convertRL)(R const& in, L& out) = nullptr)
        {
            static std::string inputL, inputR;
            static L l { };
            static R r { };

            I::TableNextRow();
            I::TableNextColumn();
            I::AlignTextToFramePadding();
            I::TextUnformatted(Label.str);

            if (scoped::WithID(Label.str))
            {
                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                if (I::InputText("##L", &inputL) && parse(inputL, l))
                {
                    convertLR(l, r);
                    inputR = print(r);
                }

                I::TableNextColumn();
                I::SetNextItemWidth(-FLT_MIN);
                if (I::InputText("##R", &inputR, convertRL ? 0 : ImGuiInputTextFlags_ReadOnly) && parse(inputR, r) && convertRL)
                {
                    convertRL(r, l);
                    inputL = print(l);
                }
            }
        };
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::Table("Conversions", 3))
        {
            I::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
            I::TableSetupColumn("Left");
            I::TableSetupColumn("Right");

            conversion.operator()<"uint64", uint64, uint64>([](uint64 const& in, uint64& out) { out = in; }, [](uint64  const&in, uint64& out) { out = in; });
            conversion.operator()<"Token32", uint32, Token32>([](uint32 const& in, Token32& out) { out = *(Token32*)&in; }, [](Token32 const& in, uint32& out) { out = *(uint32*)&in; });
            conversion.operator()<"Token64", uint64, Token64>([](uint64 const& in, Token64& out) { out = *(Token64*)&in; }, [](Token64 const& in, uint64& out) { out = *(uint64*)&in; });
            conversion.operator()<"FileReference", uint64, Data::Pack::FileReference>([](uint64 const& in, Data::Pack::FileReference& out) { out = *(Data::Pack::FileReference*)&in; }, [](Data::Pack::FileReference const& in, uint64& out) { out = 0; memcpy(&out, &in, sizeof(in)); });
            conversion.operator()<"Mangle Content Name", std::string, std::wstring>([](std::string const& in, std::wstring& out) { out = Data::Content::MangleFullName(Utils::Encoding::FromUTF8(in)); });
            conversion.operator()<"hex[16]<->GUID", std::array<byte, 16>, GUID>([](std::array<byte, 16> const& in, GUID& out) { out = *(GUID const*)in.data(); }, [](GUID const& in, std::array<byte, 16>& out) { out = *(std::array<byte, 16> const*)&in; });
            conversion.operator()<"uint32[4]<->GUID", std::array<uint32, 4>, GUID>([](std::array<uint32, 4> const& in, GUID& out) { out = *(GUID const*)in.data(); }, [](GUID const& in, std::array<uint32, 4>& out) { out = *(std::array<uint32, 4> const*)&in; });
            conversion.operator()<"uint64[2]<->GUID", std::array<uint64, 2>, GUID>([](std::array<uint64, 2> const& in, GUID& out) { out = *(GUID const*)in.data(); }, [](GUID const& in, std::array<uint64, 2>& out) { out = *(std::array<uint64, 2> const*)&in; });
        }
    }
};

}

export namespace GW2Viewer::G::Windows { UI::Windows::Parse Parse; }
