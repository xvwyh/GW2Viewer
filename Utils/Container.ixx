export module GW2Viewer.Utils.Container;
import std;

template<typename T> constexpr bool IsSTLSet = false;
template<typename... Args> constexpr bool IsSTLSet<std::set<Args...>> = true;
template<typename... Args> constexpr bool IsSTLSet<std::unordered_set<Args...>> = true;

template<typename T> concept KeyContainer = requires { typename std::decay_t<T>::key_type; };
template<typename T> concept ValueContainer = requires { typename std::decay_t<T>::value_type; };
template<typename T> concept SetContainer = IsSTLSet<T>;
template<typename T> concept KeyValueContainer = KeyContainer<T> && ValueContainer<T>;
template<typename T> concept ValueOnlyContainer = !KeyContainer<T> && ValueContainer<T> || SetContainer<T>;

static_assert(ValueOnlyContainer<std::array<int, 1>>);
static_assert(ValueOnlyContainer<std::vector<int>>);
static_assert(ValueOnlyContainer<std::list<int>>);
static_assert(ValueOnlyContainer<std::set<int>>);
static_assert(KeyValueContainer<std::map<int, int>>);

export namespace GW2Viewer::Utils::Container
{

template<KeyValueContainer Map, typename T>
auto Find(Map&& map, T&& key)
{
    auto const itr = map.find(key);
    return itr != map.end() ? &itr->second : nullptr;
}

template<ValueOnlyContainer Container, typename T>
auto Find(Container&& container, T&& value)
{
    auto const itr = container.find(value);
    return itr != container.end() ? &*itr : nullptr;
}

template<typename Set, typename T>
bool TogglePresence(Set& set, T const& element, bool present)
{
    if (present)
        set.emplace(element);
    else
        set.erase(element);
    return present;
}

}
