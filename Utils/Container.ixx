export module GW2Viewer.Utils.Container;
import std;

template<typename T> constexpr bool IsSTLSet = false;
template<typename... Args> constexpr bool IsSTLSet<std::set<Args...>> = true;
template<typename... Args> constexpr bool IsSTLSet<std::unordered_set<Args...>> = true;

template<typename T> concept KeyContainer = requires { typename std::decay_t<T>::key_type; };
template<typename T> concept ValueContainer = requires { typename std::decay_t<T>::value_type; };
template<typename T> concept MapContainer = requires { typename std::decay_t<T>::mapped_type; };
template<typename T> concept SetContainer = IsSTLSet<T>;
template<typename T> concept KeyValueContainer = KeyContainer<T> && ValueContainer<T> && !SetContainer<T>;
template<typename T> concept ValueOnlyContainer = !KeyContainer<T> && ValueContainer<T> || SetContainer<T>;

static_assert(!KeyContainer<std::array<int, 1>>);
static_assert( ValueContainer<std::array<int, 1>>);
static_assert(!MapContainer<std::array<int, 1>>);
static_assert(!SetContainer<std::array<int, 1>>);
static_assert(!KeyValueContainer<std::array<int, 1>>);
static_assert( ValueOnlyContainer<std::array<int, 1>>);

static_assert(!KeyContainer<std::vector<int>>);
static_assert( ValueContainer<std::vector<int>>);
static_assert(!MapContainer<std::vector<int>>);
static_assert(!SetContainer<std::vector<int>>);
static_assert(!KeyValueContainer<std::vector<int>>);
static_assert( ValueOnlyContainer<std::vector<int>>);

static_assert(!KeyContainer<std::list<int>>);
static_assert( ValueContainer<std::list<int>>);
static_assert(!MapContainer<std::list<int>>);
static_assert(!SetContainer<std::list<int>>);
static_assert(!KeyValueContainer<std::list<int>>);
static_assert( ValueOnlyContainer<std::list<int>>);

static_assert( KeyContainer<std::set<int>>);
static_assert( ValueContainer<std::set<int>>);
static_assert(!MapContainer<std::set<int>>);
static_assert( SetContainer<std::set<int>>);
static_assert(!KeyValueContainer<std::set<int>>);
static_assert( ValueOnlyContainer<std::set<int>>);

static_assert( KeyContainer<std::map<int, int>>);
static_assert( ValueContainer<std::map<int, int>>);
static_assert( MapContainer<std::map<int, int>>);
static_assert(!SetContainer<std::map<int, int>>);
static_assert( KeyValueContainer<std::map<int, int>>);
static_assert(!ValueOnlyContainer<std::map<int, int>>);

export namespace GW2Viewer::Utils::Container
{

using ::KeyContainer;
using ::ValueContainer;
using ::MapContainer;
using ::SetContainer;
using ::KeyValueContainer;
using ::ValueOnlyContainer;

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
