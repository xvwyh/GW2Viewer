export module GW2Viewer.UI.Windows.ContentExport;
import GW2Viewer.Common;
import GW2Viewer.Common.JSON;
import GW2Viewer.Common.Time;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Encoding;
import std;
#include "Macros.h"

export namespace GW2Viewer::UI::Windows
{

struct ContentExport : Window
{
    Utils::Async::Scheduler Async;
    std::mutex Lock;
    std::vector<Data::Content::ContentObject const*> Objects;
    Data::Content::ExportOptions Options;
    int Indent = 2;
    int IndentChar = ' ';
    std::optional<std::filesystem::path> Path;
    std::string Preview;

    void Export(std::span<Data::Content::ContentObject const* const> objects)
    {
        std::scoped_lock _(Lock);
        Objects.assign_range(objects);
        Path.reset();
        Preview.clear();
        Show();
    }

    std::string Title() override { return "Export Content Object Data"; }

    void Draw() override
    {
        I::AlignTextToFramePadding(); I::Text("Indentation:"); I::SameLine();
        I::SetNextItemWidth(60);
        I::DragInt("##Indent", &Indent, 0.05f, -1, 8, Indent >= 0 ? "%d" : "compact");
        if (I::IsItemHovered())
            I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (Indent > 0)
        {
            I::SameLine();
            I::RadioButton(std::format("space{}##IndentSpace", Indent != 1 ? "s" : "").c_str(), &IndentChar, ' ');
            I::SameLine();
            I::RadioButton(std::format("tab{}##IndentSpace", Indent != 1 ? "s" : "").c_str(), &IndentChar, '\t');
        }

        I::Checkbox("Ignore unnamed fields", &Options.IgnoreUnnamedFields);

        I::AlignTextToFramePadding(); I::Text("Resolve content object reference depth:"); I::SameLine();
        I::SetNextItemWidth(30);
        I::DragInt("##ContentPointerMaxInlineDepth", (int*)&Options.ContentPointerMaxInlineDepth, 0.05f, 0, 5);
        if (I::IsItemHovered())
            I::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        using enum Data::Content::ExportOptions::ContentPointerFormats;
        I::AlignTextToFramePadding(); I::Text("Format unresolved content pointers as:");
        I::SameLine(); I::RadioButton("GUID only", (int*)&Options.ContentPointerFormat, (int)GUID);
        I::SameLine(); I::RadioButton("Verbose", (int*)&Options.ContentPointerFormat, (int)Verbose);
        I::SameLine(); I::RadioButton("Joined with ", (int*)&Options.ContentPointerFormat, (int)Joined);
        I::SameLine(0, 0); I::SetNextItemWidth(30); I::InputText("##JoinedSeparator", &Options.ContentPointerFormatJoinedSeparator);
        if (scoped::Font(13))
        {
            static ordered_json const json
            {
                { "Content::GUID", "01234567-89AB-CDEF-0123-456789ABCDEF" },
                { "Content::Type", "TypeName" },
                { "Content::DataID", 12345 }
            };
            std::string preview;
            switch (Options.ContentPointerFormat)
            {
                case GUID:    preview = json["Content::GUID"].dump(); break;
                case Verbose: preview = json.dump(Indent, IndentChar); break;
                case Joined:  preview = ordered_json(std::format("{1}{0}{2}{0}{3}", Options.ContentPointerFormatJoinedSeparator, (std::string_view)json["Content::GUID"], (std::string_view)json["Content::Type"], (uint32)json["Content::DataID"])).dump(); break;
            }
            I::TextUnformatted(std::format("<code>{}</code>", preview).c_str());
        }

        I::Separator();

        if (scoped::Disabled(Async.Current()))
        if (I::Button(std::format("Export {} Object{}", Objects.size(), Objects.size() != 1 ? "s" : "").c_str()))
        {
            Async.Run([this, Objects = Objects, Options = Options, Indent = Indent, IndentChar = IndentChar](Utils::Async::Context context)
            {
                context->SetTotal(Objects.size());
                ordered_json result;
                for (auto const object : Objects)
                {
                    CHECK_ASYNC;
                    if (auto json = Data::Content::ExportSymbolData(*object, Options); !json.empty())
                        result.emplace_back(std::move(json));
                    context->Increment();
                }

                CHECK_ASYNC;
                context->SetIndeterminate();
                std::filesystem::path path = std::format(R"(Export\Game Content\json\{}\{:%F_%H-%M-%S}Z.json)", G::Game.Build, Time::Now());
                create_directories(path.parent_path());
                auto originalPath = path;
                int attempt = 1;
                while (exists(path))
                    path.replace_filename(originalPath.stem().string() + std::format(" ({})", attempt++) + originalPath.extension().string());
                std::ofstream(path) << std::setw(Indent) << std::setfill((char)IndentChar) << result << std::endl;

                std::scoped_lock _(Lock);
                Path = path;
                Preview.resize(10 * 1024);
                Preview.resize(std::ifstream(path).read(Preview.data(), Preview.size()).gcount());

                context->Finish();
            });
        }

        I::SameLine();
        std::scoped_lock _(Lock);
        if (auto context = Async.Current())
        {
            if (I::Button("Stop"))
                Async.Run([](Utils::Async::Context context) { context->Finish(); });

            I::SameLine();
            I::SetNextItemWidth(-FLT_MIN);
            if (scoped::Disabled(true))
                I::InputTextReadOnly("##Description", context.IsIndeterminate() ? "Writing file..." : std::format("{} / {}", context.Current, context.Total));
            Controls::AsyncProgressBar(Async);
        }
        else if (Path)
        {
            if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
            if (scoped::Table("##Table", 2, ImGuiTableFlags_NoSavedSettings, { -FLT_MIN, 0 }))
            {
                I::TableSetupColumn("Center");
                I::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthFixed);

                I::TableNextColumn();
                I::SetNextItemWidth(-I::GetStyle().ItemSpacing.x);
                I::InputTextReadOnly("##Path", Utils::Encoding::ToUTF8(absolute(*Path).wstring()), ImGuiInputTextFlags_ElideLeft);

                I::TableNextColumn();
                if (I::Button("Open File"))
                    I::OpenURL(Path->wstring().c_str());
                I::SameLine();
                if (I::Button(ICON_FA_COPY " Contents"))
                {
                    std::string buffer(file_size(*Path), '\0');
                    std::ifstream(*Path).read(buffer.data(), buffer.size());
                    I::SetClipboardText(buffer.c_str());
                }
            }
        }

        if (!Preview.empty())
        {
            I::Separator();
            I::TextUnformatted("Preview (first 10 KB):");
            if (scoped::Font(G::UI.Fonts.Monospace, 13))
                I::InputTextMultiline("##Preview", &Preview, { -FLT_MIN, -FLT_MIN }, ImGuiInputTextFlags_ReadOnly);
        }
    }
};

}

export namespace GW2Viewer::G::Windows { UI::Windows::ContentExport ContentExport; }
