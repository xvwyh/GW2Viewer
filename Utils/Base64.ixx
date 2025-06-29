module;
#include <cpp-base64/base64.cpp>

export module GW2Viewer.Utils.Base64;

export namespace Utils::Base64
{

std::string Encode(std::string_view sv) { return base64_encode(sv); }
std::string Decode(std::string_view sv) { return base64_decode(sv); }

}
