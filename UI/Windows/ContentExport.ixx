export module GW2Viewer.UI.Windows.ContentExport;
import GW2Viewer.Common;
import GW2Viewer.Common.JSON;
import GW2Viewer.Common.Time;
import GW2Viewer.Common.Token32;
import GW2Viewer.Common.Token64;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Manager;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Exception;
import GW2Viewer.Utils.Encoding;
import std;
#include "Macros.h"

export namespace GW2Viewer::UI::Windows
{

    struct ContentExport : Window
    {
        std::mutex Lock;
        std::vector<Data::Content::ContentObject const*> Results;
        uint32 InlineObjectMaxDepth = 0;
		//bool IncludeUnnamedFields = false;

        void ExportContentData(std::span<const Data::Content::ContentObject* const> contentList)
        {
            std::scoped_lock _(Lock);
            Results = std::vector<const Data::Content::ContentObject*>(contentList.begin(), contentList.end());
            Show();
        }


        std::string Title() override { return "Export Content Object Data"; }

        void Draw() override
        {
            I::AlignTextToFramePadding(); I::Text("Resolve content object reference depth: "); I::SameLine();
            I::AlignTextToFramePadding(); I::Text(ICON_FA_PLUS_MINUS); I::SameLine();
            I::SetNextItemWidth(30);
            I::DragInt("##ResolveContentObjectReferenceDepth", (int*)&InlineObjectMaxDepth, 0.05f, 0, 5);

            //I::Checkbox("Include unnamed fields ", &IncludeUnnamedFields);


            std::scoped_lock _(Lock);
            if (I::Button(std::format("Export {} content objects", Results.size()).c_str()))
            {
                nlohmann::ordered_json exportJsonArray = nlohmann::ordered_json::array();
                for (auto const& object : Results) {
                    nlohmann::ordered_json json = Data::Content::ExportSymbolData(*object, { .InlineObjectMaxDepth = InlineObjectMaxDepth });

                    if (!json.empty())
                        exportJsonArray.push_back(json);
                }

                //I::SetClipboardText(exportJsonArray.dump(2).c_str());

				auto exportString = exportJsonArray.dump(2);
                std::filesystem::path path = std::format(R"(Export\Game Content\json\{:%F_%H-%M-%S}Z_{}.json)", Time::Now(), G::Game.Build);
                create_directories(path.parent_path());
                auto originalPath = path;
                int attempt = 1;
                while (exists(path))
                    path.replace_filename(originalPath.stem().string() + std::format(" ({})", attempt++) + originalPath.extension().string());
                G::UI.ExportData({ (byte const*)exportString.data(), exportString.size() }, path);
            }
        }
    };
}

export namespace GW2Viewer::G::Windows { UI::Windows::ContentExport ContentExport; }