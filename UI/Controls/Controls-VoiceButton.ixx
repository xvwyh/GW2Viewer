module;
#include "UI/ImGui/ImGui.h"

export module GW2Viewer.UI.Controls:VoiceButton;
import GW2Viewer.Data.Content;
import GW2Viewer.Data.Encryption;
import GW2Viewer.Data.Encryption.Asset;
import GW2Viewer.Data.Game;
import GW2Viewer.UI.Manager;
import GW2Viewer.User.Config;
import GW2Viewer.Utils.Format;

export namespace UI::Controls
{

struct VoiceButtonOptions
{
    bool Selectable = false;
    bool MenuItem = false;
    uint32 VariantIndex = 0;
};
void VoiceButton(uint32 voiceID, VoiceButtonOptions const& options = { })
{
    scoped::WithID(voiceID);
    auto const status = G::Game.Voice.GetStatus(voiceID, G::Config.Language, G::Game.Encryption.GetAssetKey(Data::Encryption::AssetType::Voice, voiceID));
    static constexpr char const* VARIANT_NAMES[] { "Asura Male", "Asura Female", "Charr Male", "Charr Female", "Human Male", "Human Female", "Norn Male", "Norn Female", "Sylvari Male", "Sylvari Female" };
    std::string const text = std::vformat(options.MenuItem ? "<c=#{}>{} {} ({})</c>##Play" : "<c=#{}>{} {}</c>##Play", std::make_format_args(
        GetStatusColor(status),
        status == Data::Encryption::Status::Missing || status == Data::Encryption::Status::Encrypted ? GetStatusText(status) : ICON_FA_PLAY,
        voiceID,
        VARIANT_NAMES[options.VariantIndex]));
    if ([&]
    {
        if (options.MenuItem)
            return I::MenuItem(text.c_str());
            if (options.Selectable)
                return I::Selectable(text.c_str());
                return I::Button(text.c_str());

    }())
        G::UI.PlayVoice(voiceID);
}
void TextVoiceButton(uint32 textID, VoiceButtonOptions const& options = { })
{
    scoped::WithID(textID);
    if (auto const variants = G::Game.Text.GetVariants(textID))
    {
        auto const status = std::ranges::fold_left(*variants, Data::Encryption::Status::Missing, [](Data::Encryption::Status value, uint32 variant)
        {
            switch (G::Game.Voice.GetStatus(variant, G::Config.Language, G::Game.Encryption.GetAssetKey(Data::Encryption::AssetType::Voice, variant)))
            {
                using enum Data::Encryption::Status;
                case Missing: break;
                case Encrypted: return Encrypted;
                case Decrypted: if (value != Encrypted) return Decrypted; break;
                case Unencrypted: if (value != Encrypted && value != Decrypted) return Unencrypted; break;
            }
            return value;
        });

        std::string const text = std::format("<c=#{}>{} Variant " ICON_FA_CHEVRON_DOWN "</c>##PlayVariant",
            GetStatusColor(status),
            status == Data::Encryption::Status::Missing || status == Data::Encryption::Status::Encrypted ? GetStatusText(status) : ICON_FA_PLAY);
        if (options.MenuItem)
            I::MenuItem(text.c_str());
        else if (options.Selectable)
            I::Selectable(text.c_str());
        else
            I::Button(text.c_str());

        if (scoped::PopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft))
        {
            for (auto const& [index, variant] : *variants | std::views::enumerate)
            {
                VoiceButtonOptions menuItemOptions = options;
                menuItemOptions.MenuItem = true;
                menuItemOptions.VariantIndex = index;
                VoiceButton(variant, menuItemOptions);
            }
        }
    }
    else if (auto const voice = G::Game.Text.GetVoice(textID))
        VoiceButton(voice, options);
}

}
