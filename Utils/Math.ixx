export module GW2Viewer.Utils.Math;
import GW2Viewer.Common.Time;
import std;

export namespace GW2Viewer::Utils::Math
{

template<typename T>
auto Remap(T const& in, T const& inMin, T const& inMax, T const& outMin, T const& outMax)
{
    return outMin + (outMax - outMin) * (in - inMin) / (inMax - inMin);
}

template<typename T>
auto ExpDecay(T const& current, T const& target, float decay)
{
    return target + (current - target) * std::exp(-decay * Time::DeltaSecs);
}

template<typename T>
auto ExpDecayChase(T& current, T const& target, float decay, float threshold)
{
    if (current == target)
        return current;
    if (current < target && current > target - threshold || current > target && current < target + threshold)
        return current = target;
    return current = ExpDecay(current, target, decay);
}

}
