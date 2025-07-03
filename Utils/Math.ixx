export module GW2Viewer.Utils.Math;
import std;

export namespace GW2Viewer::Utils::Math
{

template<typename T>
auto Remap(T const& in, T const& inMin, T const& inMax, T const& outMin, T const& outMax)
{
    return outMin + (outMax - outMin) * (in - inMin) / (inMax - inMin);
}

template<typename T>
auto ExpDecay(T const& current, T const& target, float decay, float deltaTime)
{
    return target + (current - target) * std::exp(-decay * deltaTime);
}

}
