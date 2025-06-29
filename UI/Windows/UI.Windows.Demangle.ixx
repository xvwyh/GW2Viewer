module;
#include "UI/ImGui/ImGui.h"
#include "Utils/Utils.Async.h"

export module GW2Viewer.UI.Windows.Demangle;
import GW2Viewer.Common.Hash;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Content.Mangling;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Controls;
import GW2Viewer.UI.Windows.Window;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Async;
import GW2Viewer.Utils.Visitor;
import std;
import <experimental/generator>;

export namespace UI::Windows
{

struct Demangle : Window
{
    Utils::Async::Scheduler Async;
    std::mutex Lock;
    std::unordered_multimap<Data::Content::ContentNamespace*, std::wstring> NamespaceResults;
    std::unordered_multimap<Data::Content::ContentObject*, std::wstring> ContentResults;
    std::wstring ResultsPrefix;
    bool BruteforceObjects = false;
    bool BruteforceNamespaces = true;
    bool BruteforceRecursively = false;
    std::deque<Data::Content::ContentNamespace*> BruteforceRecursiveQueue;

    std::mutex UILock;
    std::wstring BruteforceUIPrefix;
    Data::Content::ContentNamespace* BruteforceUIRecursiveBase = nullptr;
    bool BruteforceUIStart = false;

    void MatchNamespace(std::wstring_view fullName, std::wstring_view mangledName, Hasher& hasher)
    {
        if (auto const namespaces = G::Game.Content.GetNamespacesByName(mangledName))
        {
            auto const pos = fullName.find_last_of(L'.');
            auto const name = fullName.substr(pos + 1);
            auto const parentName = fullName.substr(0, pos);
            Data::Content::Mangle(parentName, (wchar_t*)mangledName.data(), 6, hasher);
            std::scoped_lock _(Lock);
            for (auto ns : *namespaces)
                if (ns->Parent && ns->Parent->Name == mangledName.substr(0, 5))
                    if (auto range = NamespaceResults.equal_range(ns); !std::ranges::contains(range.first, range.second, name, &decltype(NamespaceResults)::value_type::second))
                        NamespaceResults.emplace(ns, name);
            Show();
        }
    }
    void MatchObject(std::wstring_view name, std::wstring_view mangledName)
    {
        if (auto const objects = G::Game.Content.GetByName(mangledName))
        {
            std::scoped_lock _(Lock);
            for (auto object : *objects)
                if (auto range = ContentResults.equal_range(object); !std::ranges::contains(range.first, range.second, name, &decltype(ContentResults)::value_type::second))
                    ContentResults.emplace(object, name);
            Show();
        }
    }
    void Match(std::wstring_view name, std::wstring_view mangledName)
    {
        auto const namePos = name.find_last_of(L'.');
        if (auto const pos = mangledName.find(L'.'); pos != std::wstring_view::npos)
        {
            Hasher hasher;
            MatchNamespace(name.substr(0, namePos), mangledName.substr(0, pos), hasher);
            mangledName.remove_prefix(pos + 1);
        }
        MatchObject(name.substr(namePos + 1), mangledName);
    }
    void Match(std::wstring_view name)
    {
        Match(name, Data::Content::MangleFullName(name));
    }
    void MatchRecursively(std::wstring_view name)
    {
        ResultsPrefix.clear();
        while (true)
        {
            Match(name, Data::Content::MangleFullName(name));
            auto const pos = name.find_last_of(L'.');
            if (pos == std::wstring_view::npos)
                break;
            name = name.substr(0, pos);
        }
    }
    void Bruteforce(std::wstring_view prefix, Data::Content::ContentNamespace* recursiveBase, uint32 words)
    {
        Async.Run([this, nonRecursivePrefix = std::wstring(prefix), recursiveBase, words, objects = BruteforceObjects, namespaces = BruteforceNamespaces, recursively = BruteforceRecursively](Utils::Async::Context context)
        {
            context->SetIndeterminate();
            {
                std::scoped_lock _(Lock);
                BruteforceRecursiveQueue.clear();
            }

            std::set<std::wstring> uniqueDictionary;
            //dictionary.reserve(std::ranges::count(G::Config.BruteforceDictionary, L'\n') + 1);
            for (auto const& word : G::Config.BruteforceDictionary | std::views::split(L'\n'))
                uniqueDictionary.emplace(std::from_range, word);
            uniqueDictionary.erase(L"");

            std::vector<std::wstring> dictionary { std::from_range, uniqueDictionary };

            /* Decent optimization, but ultimately meaningless considering that the bulk of the time is spent elsewhere
            static auto nameHashInUse = []
            {
                std::wstring paddedName(8, L'=');
                auto result = std::make_unique<std::bitset<1 << 30>>();
                for (auto const& name : g_contentNamespacesByName | std::views::keys)
                {
                    name.copy(paddedName.data(), name.size());
                    result->set(std::byteswap(Data::Content::DemangleToNumber(paddedName)) >> 2);
                }
                return result;
            }();
            */

            auto process = [this, &dictionary, objects, namespaces](Utils::Async::Context context, std::span<wchar_t> name, uint32 depth, std::span<wchar_t> mangledBuffer, Hasher& hasher, auto& generate_combinations) -> void
            {
                if (!depth)
                {
                    std::wstring_view nameView { name.data(), name.size() };
                    if (namespaces)
                    {
                        //if (nameHashInUse->test(std::byteswap((uint32)Data::Content::MangleToNumber(nameView)) >> 2))
                        {
                            Data::Content::Mangle(nameView, mangledBuffer.data(), 6, hasher);
                            MatchNamespace(nameView, { mangledBuffer.data(), 5 }, hasher);
                        }
                    }
                    if (objects)
                    {
                        nameView.remove_prefix(nameView.find_last_of(L'.') + 1);
                        //if (nameHashInUse->test(std::byteswap((uint32)Data::Content::MangleToNumber(nameView)) >> 2))
                        {
                            Data::Content::Mangle(nameView, mangledBuffer.data(), 6, hasher);
                            MatchObject(nameView, { mangledBuffer.data(), 5 });
                        }
                    }
                    return;
                }

                for (const auto& word : dictionary)
                {
                    CHECK_ASYNC;
                    auto const begin = name.begin();
                    auto end = name.end();
                    auto wordEnd = end + word.copy(&*end, 4096);
                    // PrefixWord
                    generate_combinations(context, { begin, wordEnd }, depth - 1, mangledBuffer, hasher, generate_combinations);
                    // PrefixWords
                    *wordEnd++ = L's';
                    generate_combinations(context, { begin, wordEnd }, depth - 1, mangledBuffer, hasher, generate_combinations);
                    // Prefix Word
                    *end++ = L' ';
                    wordEnd = end + word.copy(&*end, 4096);
                    generate_combinations(context, { begin, wordEnd }, depth - 1, mangledBuffer, hasher, generate_combinations);
                    // Prefix Words
                    *wordEnd++ = L's';
                    generate_combinations(context, { begin, wordEnd }, depth - 1, mangledBuffer, hasher, generate_combinations);
                }
            };

            auto generatePrefix = [this, nonRecursivePrefix, objects, namespaces, recursively](Data::Content::ContentNamespace const& ns, bool skipRecursiveQueue, auto& generatePrefix) -> std::experimental::generator<std::wstring>
            {
                if (!recursively)
                {
                    co_yield nonRecursivePrefix;
                    co_return;
                }

                while (!skipRecursiveQueue)
                {
                    Data::Content::ContentNamespace* current = nullptr;
                    {
                        std::scoped_lock _(Lock);
                        if (!BruteforceRecursiveQueue.empty())
                        {
                            current = BruteforceRecursiveQueue.front();
                            BruteforceRecursiveQueue.pop_front();
                        }
                    }

                    if (!current)
                        break;

                    for (auto const& result : generatePrefix(*current, true, generatePrefix))
                        co_yield result;
                }

                std::wstring current;
                {
                    std::scoped_lock _(Lock);
                    if (objects && std::ranges::any_of(ns.Entries, [](auto const& object) { return !object->HasCustomName(); }) ||
                        namespaces && std::ranges::any_of(ns.Namespaces, [](auto const& ns) { return !ns->HasCustomName(); }))
                        current = std::format(L"{}.", ns.GetFullDisplayName());
                }
                if (!current.empty())
                    co_yield current;

                for (auto const& child : ns.Namespaces)
                {
                    bool named;
                    {
                        std::scoped_lock _(Lock);
                        named = child->HasCustomName();
                    }
                    if (named)
                        for (auto const& result : generatePrefix(*child, false, generatePrefix))
                            co_yield result;
                }
            };

            context->SetTotal(dictionary.size());
            for (auto const& prefix : generatePrefix(recursiveBase ? *recursiveBase : *G::Game.Content.GetNamespaceRoot(), false, generatePrefix))
            {
                CHECK_ASYNC;
                context->Clear();
                {
                    std::scoped_lock _(UILock);
                    ResultsPrefix = prefix; // TODO: Store alongside results
                    BruteforceUIPrefix = prefix;
                }
                std::for_each(std::execution::par_unseq, dictionary.begin(), dictionary.end(), [context, &process, prefix, words](std::wstring const& word) mutable
                {
                    CHECK_ASYNC;
                    std::wstring mangledBuffer(Data::Content::MANGLE_FULL_NAME_BUFFER_SIZE, L'\0');
                    std::wstring nameBuffer(4096, L'\0');
                    auto const begin = nameBuffer.begin();
                    auto const end = begin + prefix.copy(nameBuffer.data(), nameBuffer.size());
                    Hasher hasher;
                    for (uint32 depth = 0; depth < words; ++depth)
                    {
                        CHECK_ASYNC;
                        auto wordEnd = end + word.copy(&*end, 4096);
                        process(context, { begin, wordEnd }, depth, mangledBuffer, hasher, process);
                        *wordEnd++ = L's';
                        process(context, { begin, wordEnd }, depth, mangledBuffer, hasher, process);
                    }
                    CHECK_ASYNC;
                    context->InterlockedIncrement();
                });
                CHECK_ASYNC;
            }
            context->Finish();
        });
    }
    void OpenBruteforceUI(std::wstring_view prefix, Data::Content::ContentNamespace* recursiveBase, bool start = false, std::optional<bool> objects = { }, std::optional<bool> namespaces = { })
    {
        BruteforceUIPrefix = prefix;
        BruteforceUIRecursiveBase = recursiveBase;
        BruteforceUIStart = start;
        if (objects)
            BruteforceObjects = *objects;
        if (namespaces)
            BruteforceNamespaces = *namespaces;
        Show();
    }

    std::string Title() override { return "Demangle"; }
    void Draw() override
    {
        std::scoped_lock __(UILock);
        if (scoped::TabBar("Tabs"))
        {
            if (scoped::TabItem("Match Name List"))
            {
                static std::string names;
                I::SetNextItemWidth(-FLT_MIN);
                if (I::InputTextMultiline("##Names", &names, { -FLT_MIN, 100 }))
                    for (auto const& name : std::views::split(names, '\n'))
                        MatchRecursively(Utils::Encoding::FromUTF8(std::string(std::from_range, name)));
            }
            if (scoped::TabItem("Bruteforce", nullptr, !BruteforceUIPrefix.empty() ? ImGuiTabItemFlags_SetSelected : 0))
            {
                static std::string dictionary = Utils::Encoding::ToUTF8(G::Config.BruteforceDictionary);
                I::SetNextItemWidth(-FLT_MIN);
                if (I::InputTextMultiline("##Dictionary", &dictionary, { -FLT_MIN, 100 }))
                    G::Config.BruteforceDictionary = Utils::Encoding::FromUTF8(dictionary);

                static std::string prefix;
                if (!BruteforceUIPrefix.empty())
                {
                    prefix = Utils::Encoding::ToUTF8(BruteforceUIPrefix);
                    BruteforceUIPrefix.clear();
                }
                I::SetNextItemWidth(-200);
                I::InputText("##Prefix", &prefix);
                if (auto context = Async.Current())
                {
                    I::SetCursorScreenPos(I::GetCurrentContext()->LastItemData.Rect.Min);
                    if (scoped::WithColorVar(ImGuiCol_FrameBg, 0))
                    if (scoped::WithColorVar(ImGuiCol_Border, 0))
                    if (scoped::WithColorVar(ImGuiCol_BorderShadow, 0))
                    if (scoped::WithColorVar(ImGuiCol_Text, 0))
                    if (scoped::WithColorVar(ImGuiCol_PlotHistogram, 0x20FFFFFF))
                        if (context.IsIndeterminate())
                            I::IndeterminateProgressBar(I::GetCurrentContext()->LastItemData.Rect.GetSize());
                        else
                            I::ProgressBar(context.Progress(), I::GetCurrentContext()->LastItemData.Rect.GetSize());
                }
                I::SameLine();

                static int words = 2;
                I::SetNextItemWidth(70);
                I::DragInt("##Words", &words, 0.02f, 1, 100, "Words: %u");
                I::SameLine();

                I::CheckboxButton(ICON_FA_FILE, BruteforceObjects, "Bruteforce Content Object Names", I::GetFrameHeight());
                I::SameLine(0, 0);
                I::CheckboxButton(ICON_FA_FOLDER, BruteforceNamespaces, "Bruteforce Content Namespace Names", I::GetFrameHeight());
                I::SameLine();

                I::CheckboxButton(ICON_FA_FOLDER_TREE, BruteforceRecursively, "Bruteforce Recursively", I::GetFrameHeight());
                I::SameLine();

                if (Async.Current() && !BruteforceUIStart)
                {
                    if (I::Button("Stop", { I::GetContentRegionAvail().x, 0 }))
                        Async.Run([](Utils::Async::Context context) { context->Finish(); });
                }
                else if (I::Button("Start", { I::GetContentRegionAvail().x, 0 }) || BruteforceUIStart)
                {
                    bool const old = BruteforceRecursively;
                    if (BruteforceUIRecursiveBase)
                        BruteforceRecursively = true;
                    Bruteforce(Utils::Encoding::FromUTF8(prefix), BruteforceUIRecursiveBase, words);
                    if (BruteforceUIRecursiveBase)
                        BruteforceRecursively = old;
                }
                BruteforceUIStart = false;
                BruteforceUIRecursiveBase = nullptr;
            }
        }

        std::scoped_lock ___(Lock);
        bool apply = false;
        if (I::Button("Apply"))
            apply = true;
        I::SameLine();
        if (I::Button("Clear"))
        {
            NamespaceResults.clear();
            ContentResults.clear();
        }
        I::SameLine();
        static bool onlyUnnamed;
        I::Checkbox("Only Unnamed", &onlyUnnamed);
        if (scoped::WithStyleVar(ImGuiStyleVar_CellPadding, ImVec2()))
        if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
        if (scoped::Table("Results", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, { -FLT_MIN, -FLT_MIN }))
        {
            I::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            I::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150);
            I::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            I::TableSetupScrollFreeze(0, 1);
            I::TableHeadersRow();

            Hasher hasher;
            static std::wstring mangledPrefix(Data::Content::MANGLE_FULL_NAME_BUFFER_SIZE, L'\0');
            Data::Content::MangleFullName(ResultsPrefix, mangledPrefix.data(), mangledPrefix.size(), hasher);

            static auto filter = Utils::Visitor::Overloaded
            {
                [this](Data::Content::ContentNamespace const* ns, std::wstring const& name)
                {
                    if (auto const itr = G::Config.ContentNamespaceNames.find(ns->GetFullName()); itr != G::Config.ContentNamespaceNames.end() && (onlyUnnamed || itr->second == name))
                        return true;
                    return false;
                },
                [this](Data::Content::ContentObject const* object, std::wstring const& name)
                {
                    if (auto const itr = G::Config.ContentObjectNames.find(*object->GetGUID()); itr != G::Config.ContentObjectNames.end() && (onlyUnnamed || itr->second == name))
                        return true;
                    if (!ResultsPrefix.empty())
                        if (!object->GetFullName().starts_with({ mangledPrefix.data(), 5 }))
                            return true;
                    return false;
                }
            };
            std::erase_if(NamespaceResults, [](auto const& pair) { return filter(pair.first, pair.second); });
            std::erase_if(ContentResults, [](auto const& pair) { return filter(pair.first, pair.second); });
            std::optional<decltype(NamespaceResults)::value_type> eraseNamespace;
            std::optional<decltype(ContentResults)::value_type> eraseObject;
            ImGuiListClipper clipper;
            clipper.Begin(NamespaceResults.size() + ContentResults.size(), I::GetFrameHeight());
            while (clipper.Step())
            {
                int drawn = 0;
                for (auto const& [ns, name] : NamespaceResults | std::views::drop(clipper.DisplayStart) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart))
                {
                    if (filter(ns, name))
                        continue;

                    scoped::WithID(&ns);
                    I::TableNextRow();

                    I::TableNextColumn();
                    if (I::Button("<c=#0F0>" ICON_FA_CHECK "</c>") || apply)
                    {
                        G::Config.ContentNamespaceNames[ns->GetFullName()] = name;
                        BruteforceRecursiveQueue.emplace_back(ns);
                    }
                    I::SameLine(0, 0);
                    if (I::Button("<c=#F00>" ICON_FA_XMARK "</c>"))
                        eraseNamespace.emplace(ns, name);

                    I::TableNextColumn();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::InputTextReadOnly("##Name", Utils::Encoding::ToUTF8(name));

                    I::TableNextColumn();
                    I::Text(ICON_FA_FOLDER_CLOSED " %s", Utils::Encoding::ToUTF8(ns->GetFullDisplayName()).c_str());
                    ++drawn;
                }
                for (auto const& [object, name] : ContentResults | std::views::drop(std::max(0, clipper.DisplayStart - (int)NamespaceResults.size())) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart - drawn))
                {
                    if (filter(object, name))
                        continue;

                    scoped::WithID(&object);
                    I::TableNextRow();

                    I::TableNextColumn();
                    if (I::Button("<c=#0F0>" ICON_FA_CHECK "</c>") || apply)
                        G::Config.ContentObjectNames[*object->GetGUID()] = name;
                    I::SameLine(0, 0);
                    if (I::Button("<c=#F00>" ICON_FA_XMARK "</c>"))
                        eraseObject.emplace(object, name);

                    I::TableNextColumn();
                    I::SetNextItemWidth(-FLT_MIN);
                    I::InputTextReadOnly("##Name", Utils::Encoding::ToUTF8(name));

                    I::TableNextColumn();
                    Controls::ContentButton(object, object);
                }
            }
            if (eraseNamespace)
                NamespaceResults.erase(std::ranges::find(NamespaceResults, *eraseNamespace));
            if (eraseObject)
                ContentResults.erase(std::ranges::find(ContentResults, *eraseObject));
        }
    }
};

}

export namespace G::Windows { UI::Windows::Demangle Demangle; }
