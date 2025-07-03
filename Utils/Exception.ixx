export module GW2Viewer.Utils.Exception;
import std;
import <eh.h>;

export namespace GW2Viewer::Utils::Exception
{

class SEHandler
{
    _se_translator_function const old;

    SEHandler(_se_translator_function handler) noexcept : old(_set_se_translator(handler)) { }

public:
    ~SEHandler() noexcept { _set_se_translator(old); }

    [[nodiscard]] static SEHandler Create();
};

}
