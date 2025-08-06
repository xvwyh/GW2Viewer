export module GW2Viewer.Data.Encryption;
import std;
#include "Macros.h"

namespace GW2Viewer::Data::Encryption
{

export enum class Status
{
    Missing,
    Unencrypted,
    Encrypted,
    Decrypted,
};

std::unordered_map<Status, std::pair<char const*, char const*>> const statuses
{
    { Status::Missing,     { "F00", "<nosel><c=#F00>" ICON_FA_BAN "</c></nosel>" } },
    { Status::Unencrypted, { "FFF", "" } },
    { Status::Encrypted,   { "F80", "<nosel>" ICON_FA_KEY "</nosel>" } },
    { Status::Decrypted,   { "0F0", "<nosel><c=#4>" ICON_FA_KEY "</c> </nosel>"} },
};

export char const* GetStatusColor(Status status) { return statuses.at(status).first; }
export char const* GetStatusText(Status status) { return statuses.at(status).second; }

}

