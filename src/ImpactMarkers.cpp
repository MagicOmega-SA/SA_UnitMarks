#include "UnitMarks.h"

namespace
{
constexpr unsigned AresLaunchReturn = 0x33033u;
constexpr unsigned AresNukeRadarReturn = 0x369EDu;
constexpr unsigned AresLightningRadarReturn = 0x36D30u;
constexpr unsigned AresDominatorRadarReturn = 0x378F8u;
constexpr unsigned CurrentFrame = 0xA8ED84u;
constexpr DWORD ExpirationFrames = 900;
constexpr DWORD SuperweaponRadarEvent = 13;
constexpr unsigned GridSize = 512;
constexpr unsigned BulletTargetOffset = 0x10Cu;
constexpr unsigned BulletWarheadOffset = 0x128u;
constexpr unsigned BulletTargetCoordsOffset = 0x140u;
constexpr unsigned CellVtable = 0x7E4EECu;
constexpr unsigned CellMapCoordsOffset = 0x24u;

enum class Weapon { None, Nuke, Tempest, Lightning, Domination, BoidBlitz };

struct ImpactMarker
{
    Weapon weapon;
    DWORD cell;
    DWORD frame;
    WorldPoint location;
};

ImpactMarker markers[MaxImpactMarkers];
const unsigned char* aresImage;

const unsigned char* FindAresImage()
{
#ifdef UNIT_MARKS_FIXTURE
    const auto* image = static_cast<const unsigned char*>(ENGINE_ADDRESS(0x10000000u));
#else
    const auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleA("Ares.dll"));
#endif
    if (!image) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
        return nullptr;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->FileHeader.TimeDateStamp != 0x5FC37EF6u || pe->OptionalHeader.SizeOfImage != 0xDA000u)
        return nullptr;
    // Verify the tail of the accepted-launch coordinate lookup in the bundled Ares build.
    const auto* call = image + AresLaunchReturn - 3;
    return call[0] == 0xFF && call[1] == 0x50 && call[2] == 0x48 ? image : nullptr;
}

Weapon WeaponFromExtension(DWORD extension)
{
    if (!extension) return Weapon::None;
    const void* type = *reinterpret_cast<const void* const*>(extension);
    if (!type) return Weapon::None;
    const char* id = Game::TypeId(type);
    if (lstrcmpiA(id, "NukeSpecial") == 0) return Weapon::Nuke;
    if (lstrcmpiA(id, "GreatTempestSpecial") == 0) return Weapon::Tempest;
    if (lstrcmpiA(id, "LightningStormSpecial") == 0) return Weapon::Lightning;
    if (lstrcmpiA(id, "PsychicDominatorSpecial") == 0) return Weapon::Domination;
    if (lstrcmpiA(id, "BoidBlitzSpecial") == 0) return Weapon::BoidBlitz;
    return Weapon::None;
}

void RemoveExpired(DWORD frame)
{
    for (auto& marker : markers)
        if (marker.weapon != Weapon::None && frame - marker.frame >= ExpirationFrames)
            marker.weapon = Weapon::None;
}

void AddMarker(Weapon weapon, DWORD cell, const void* square)
{
    const unsigned x = cell & 0xFFFFu;
    const unsigned y = cell >> 16;
    if (!square || x >= GridSize || y >= GridSize) return;

    const DWORD now = *static_cast<const DWORD*>(ENGINE_ADDRESS(CurrentFrame));
    RemoveExpired(now);
    ImpactMarker* slot = markers;
    for (auto& marker : markers)
    {
        if (marker.weapon == Weapon::None)
        {
            slot = &marker;
            break;
        }
        if (now - marker.frame > now - slot->frame) slot = &marker;
    }

    // CellClass::GetCoords gives the center and terrain height without checking shroud.
    using GetCellCoords = WorldPoint* (__attribute__((thiscall)) *)(const void*, WorldPoint*);
    const auto getCoords = reinterpret_cast<GetCellCoords>(ENGINE_ADDRESS(0x486840u));
    WorldPoint location{};
    getCoords(square, &location);
    const auto* bytes = static_cast<const unsigned char*>(square);
    if (*reinterpret_cast<const unsigned*>(bytes + 0x140u) & 0x100u) location.Z += 416;
    *slot = {weapon, cell, now, location};
}

bool RemoveMarker(Weapon weapon, DWORD cell)
{
    const DWORD now = *static_cast<const DWORD*>(ENGINE_ADDRESS(CurrentFrame));
    RemoveExpired(now);
    ImpactMarker* first = nullptr;
    for (auto& marker : markers)
        if (marker.weapon == weapon && marker.cell == cell && (!first || now - marker.frame > now - first->frame))
            first = &marker;
    if (!first) return false;
    first->weapon = Weapon::None;
    return true;
}

DWORD CellFromWorld(const WorldPoint& point)
{
    constexpr int CellSize = 256;
    constexpr int MapLimit = GridSize * CellSize;
    if (point.X < 0 || point.Y < 0 || point.X >= MapLimit || point.Y >= MapLimit) return 0xFFFFFFFFu;
    return static_cast<DWORD>(point.X / CellSize) | static_cast<DWORD>(point.Y / CellSize) << 16;
}
}

extern "C" __declspec(dllexport) DWORD __cdecl UnitMarks_ResetImpactMarkers(const SyringeRegisters*)
{
    for (auto& marker : markers) marker.weapon = Weapon::None;
    aresImage = FindAresImage();
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl UnitMarks_ObserveSuperweaponLaunch(const SyringeRegisters* regs)
{
    if (!aresImage || !regs || !regs->esp) return 0;
    const auto* stack = reinterpret_cast<const void* const*>(regs->esp);
    if (stack[0] != aresImage + AresLaunchReturn) return 0;

    // At this call, EBX is SWTypeExt and EDI is the packed target cell.
    const Weapon weapon = WeaponFromExtension(regs->ebx);
    if (weapon != Weapon::None) AddMarker(weapon, regs->edi, reinterpret_cast<const void*>(regs->ecx));
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl UnitMarks_ObserveSuperweaponImpact(const SyringeRegisters* regs)
{
    if (!aresImage || !regs || !regs->esp || regs->ecx != SuperweaponRadarEvent) return 0;
    const auto* stack = reinterpret_cast<const DWORD*>(regs->esp);
    const DWORD returnOffset = stack[0] - reinterpret_cast<DWORD>(aresImage);
    if (returnOffset != AresNukeRadarReturn && returnOffset != AresLightningRadarReturn &&
        returnOffset != AresDominatorRadarReturn) return 0;

    const Weapon weapon = WeaponFromExtension(returnOffset == AresDominatorRadarReturn ? regs->edi : regs->ebx);
    if (weapon != Weapon::None) RemoveMarker(weapon, stack[1]);
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl UnitMarks_ObserveBoidBlitzDetonation(const SyringeRegisters* regs)
{
    if (!aresImage || !regs || !regs->ecx || !regs->esp) return 0;
    const auto* bullet = reinterpret_cast<const char*>(regs->ecx);
    const void* warhead = Game::ObjectType(bullet, BulletWarheadOffset);
    if (!warhead || lstrcmpiA(Game::TypeId(warhead), "BoidBlitzWH") != 0) return 0;

    // Boid Blitz disables radar events. Use the shell's assigned cell even if it lands short.
    const auto* target = static_cast<const char*>(Game::ObjectType(bullet, BulletTargetOffset));
    const DWORD cell = target && *reinterpret_cast<const DWORD*>(target) == CellVtable
        ? *reinterpret_cast<const DWORD*>(target + CellMapCoordsOffset)
        : CellFromWorld(*reinterpret_cast<const WorldPoint*>(bullet + BulletTargetCoordsOffset));
    if (!RemoveMarker(Weapon::BoidBlitz, cell))
    {
        const auto* stack = reinterpret_cast<const WorldPoint* const*>(regs->esp);
        if (stack[1]) RemoveMarker(Weapon::BoidBlitz, CellFromWorld(*stack[1]));
    }
    return 0;
}

unsigned CollectImpactMarkers(WorldPoint (&positions)[MaxImpactMarkers])
{
    RemoveExpired(*static_cast<const DWORD*>(ENGINE_ADDRESS(CurrentFrame)));
    unsigned count = 0;
    for (const auto& marker : markers)
        if (marker.weapon != Weapon::None) positions[count++] = marker.location;
    return count;
}
