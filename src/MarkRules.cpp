#include "UnitMarks.h"

namespace
{
using ReadBoolFn = unsigned char (__attribute__((thiscall)) *)(void*, const char*, const char*, bool);
using ReadStringFn = int (__attribute__((thiscall)) *)(void*, const char*, const char*, const char*, char*, int);

constexpr unsigned PassengerCountOffset = 0x114u;
constexpr unsigned FirstPassengerOffset = 0x118u;
constexpr unsigned NextObjectOffset = 0x30u;
constexpr unsigned InfantryIndexOffset = 0xDF8u;
constexpr unsigned HeroFlag = 1u;
constexpr unsigned FirstInsigniaFlag = 2u;

struct HeroRule
{
    char id[25];
    unsigned flags;
    unsigned generation;
};

HeroRule heroRules[4096];
unsigned heroGeneration = 1;

template<unsigned N>
bool Matches(const char* id, const char* const (&ids)[N])
{
    for (const char* candidate : ids)
        if (lstrcmpiA(id, candidate) == 0) return true;
    return false;
}

int InfantryIndex(const void* type)
{
    return *reinterpret_cast<const int*>(static_cast<const char*>(type) + InfantryIndexOffset);
}

bool ValidHeroIndex(int index)
{
    return index >= 0 && index < static_cast<int>(sizeof(heroRules) / sizeof(heroRules[0]));
}

const HeroRule* CachedHeroRule(int index, const char* id)
{
    if (!ValidHeroIndex(index)) return nullptr;
    const HeroRule& entry = heroRules[index];
    if (entry.generation != heroGeneration || lstrcmpiA(entry.id, id) != 0)
        return nullptr;
    return &entry;
}

bool IsInfantryHero(const void* type)
{
    const char* id = Game::TypeId(type);
    if (lstrcmpiA(id, "ARMR") == 0) return true;
    if (const HeroRule* rule = CachedHeroRule(InfantryIndex(type), id))
        return rule->flags != 0;

    // Base 3.3.6 roster for types restored without an INI read.
    static const char* const baseHeroes[] = {
        "TANY", "SIEG", "MORALES", "AIMORA", "VOLKOV", "CHITZ", "YUNRU", "ASSN",
        "UNDER", "LIBRA", "SIBFIN", "SICALI", "EUREKA", "URAGAN", "CYCOM",
        "REZNOV", "KRUKOV", "CBRIS", "RHAD", "STLN", "ARND", "CLNT",
    };
    return Matches(id, baseHeroes);
}

bool HasHeroPassenger(const void* vehicle)
{
    const auto* bytes = static_cast<const char*>(vehicle);
    const int count = *reinterpret_cast<const int*>(bytes + PassengerCountOffset);
    if (count <= 0 || count > 64) return false;

    const auto* passenger = *reinterpret_cast<const char* const*>(bytes + FirstPassengerOffset);
    for (int i = 0; i < count && passenger; ++i,
         passenger = *reinterpret_cast<const char* const*>(passenger + NextObjectOffset))
    {
        if (*reinterpret_cast<void* const*>(passenger) != ENGINE_ADDRESS(Game::InfantryVtable))
            continue;
        // Carried infantry normally has InLimbo set.
        if (!Game::IsAlive(passenger)) continue;
        const void* type = Game::ObjectType(passenger, Game::InfantryTypeOffset);
        if (!Game::IsInfantryType(type)) continue;
        if (lstrcmpiA(Game::TypeId(type), "TANY") == 0 || IsInfantryHero(type)) return true;
    }
    return false;
}
}

bool HasSpeechMark(const void* type, bool building)
{
    static const char* const vehicles[] = {
        "ARCH", "ARCH2", "ARCHW", "MGTK", "PHNT",
        "SUB", "SUB2", "DLPH", "SQD", "SLED", "NAUT",
        "XPCV", "YMIN", "YMON",
    };
    static const char* const buildings[] = {
        "NATRAP", "YAVNMM", "GACRYO", "YATXCH", "YACXST", "YAPPXT",
    };
    const char* id = Game::TypeId(type);
    return building ? Matches(id, buildings) : Matches(id, vehicles);
}

Mark VehicleMark(const void* unit, const void* displayedType, const void* actualType)
{
    static const char* const heroCarriers[] = { "FV", "AMC", "TENGU" };
    static const char* const alert[] = { "DRIL", "FDRON", "DTRUCK", "BGGY" };
    static const char* const danger[] = {
        "SCAV", "WORMQ", "AKULA", "BSUB", "MWF", "CNTR", "BASIL", "ZEP",
    };

    const char* id = Game::TypeId(displayedType);
    if (displayedType == actualType && Matches(id, heroCarriers) && HasHeroPassenger(unit))
        return Mark::Danger;
    if (Matches(id, alert)) return Mark::Alert;
    if (Matches(id, danger)) return Mark::Danger;
    return Mark::None;
}

Mark BuildingMark(const void* type)
{
    static const char* const techCenters[] = {
        "GATECH", "NATECHR", "NATECHC", "NATEK", "YATECH", "FACYBR", "FACLDP", "FANANO",
    };
    static const char* const superweapons[] = {
        "GACSPH", "GAWEAT", "NAIRON", "NAMISL", "NAEMPS", "NAICUR",
        "YAGNTC", "YAPPET", "FABLST", "FAELEV", "FAHARB",
    };

    // Epsilon and Foehn add-ons are drawn as part of their host building.
    // FABTRC is the trench itself. FABLST is the superweapon that activates it.
    const char* id = Game::TypeId(type);
    if (lstrcmpiA(id, "FAMMIN") == 0 || Matches(id, techCenters) || Matches(id, superweapons))
        return Mark::Alert;
    if (lstrcmpiA(id, "NAFIST") == 0) return Mark::Danger;
    return Mark::None;
}

Mark InfantryMark(const void* type)
{
    const auto* data = static_cast<const unsigned char*>(type);
    // Both flags identify working agents. Infiltrate alone also matches decoys.
    const bool infiltrate = data[0xEBEu] != 0;
    const bool agent = data[0xEC4u] != 0;
    if (infiltrate && agent) return Mark::Danger;
    return IsInfantryHero(type) ? Mark::Alert : Mark::None;
}

void LoadHeroRule(const void* type, void* ini)
{
    const int index = InfantryIndex(type);
    if (!ValidHeroIndex(index)) return;

    const bool baseRules = ini == *static_cast<void**>(ENGINE_ADDRESS(0x887048u));
    // The first base type starts a new rules load and invalidates prior map overrides.
    if (baseRules && index == 0) ++heroGeneration;
    const char* id = Game::TypeId(type);
    const HeroRule* previous = baseRules ? nullptr : CachedHeroRule(index, id);
    unsigned flags = previous ? previous->flags : 0;

    const auto readBool = reinterpret_cast<ReadBoolFn>(ENGINE_ADDRESS(0x5295F0u));
    const auto readString = reinterpret_cast<ReadStringFn>(ENGINE_ADDRESS(0x528A10u));
    if (readBool(ini, id, "Hero", (flags & HeroFlag) != 0))
        flags |= HeroFlag;
    else
        flags &= ~HeroFlag;

    static const char* const keys[] = { "Insignia.Rookie", "Insignia.Veteran", "Insignia.Elite" };
    static const char* const marks[] = {
        "herostcr", "herostbr", "herostar", "herostcr.shp", "herostbr.shp", "herostar.shp",
    };
    unsigned bit = FirstInsigniaFlag;
    for (const char* key : keys)
    {
        char value[48] = "*";
        readString(ini, id, key, "*", value, sizeof(value));
        // An unspecified map override inherits the previous value.
        if (value[0] != '*' || value[1] != '\0')
        {
            if (Matches(value, marks)) flags |= bit;
            else flags &= ~bit;
        }
        bit <<= 1;
    }

    HeroRule& entry = heroRules[index];
    lstrcpynA(entry.id, id, sizeof(entry.id));
    entry.flags = flags;
    entry.generation = heroGeneration;
}
