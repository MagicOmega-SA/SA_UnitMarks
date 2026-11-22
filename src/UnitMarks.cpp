#include "UnitMarks.h"
#include <cstddef>

namespace
{
constexpr unsigned VehicleTypeOffset = 0x6C4u;
constexpr unsigned BuildingTypeOffset = 0x520u;
constexpr unsigned BuildingVtable = 0x7E3EBCu;
constexpr unsigned InLimboOffset = 0x81u;
constexpr unsigned UnitArrayAddress = 0x8B4108u;
constexpr unsigned BuildingArrayAddress = 0xA8EB40u;

struct ObjectArray
{
    const void* vtable;
    const void* const* items;
    int capacity;
    unsigned flags;
    int count;
};

struct SyHookDecl
{
    unsigned int addr;
    unsigned int size;
    const char* name;
    unsigned int pad;
};

static_assert(sizeof(SyHookDecl) == 16, "Syringe hook records must be 16 bytes");
static_assert(sizeof(SyringeHandshakeInfo) == 32, "Syringe handshake must be 32 bytes");
static_assert(offsetof(ObjectArray, items) == 4 && offsetof(ObjectArray, count) == 16,
              "Game object arrays must match DynamicVectorClass");
static_assert(sizeof(SyringeRegisters) == 0x28 && offsetof(SyringeRegisters, esi) == 0x0C &&
              offsetof(SyringeRegisters, ebp) == 0x10 && offsetof(SyringeRegisters, esp) == 0x14 &&
              offsetof(SyringeRegisters, ebx) == 0x18 && offsetof(SyringeRegisters, ecx) == 0x20,
              "Syringe register layout must match the 32-bit loader");

static SyHookDecl g_hooks[] __attribute__((section(".syhks00"), used, aligned(16))) =
{
    /* UnitClass::DrawIt, after the vehicle sprite. */
    { 0x73D408u, 5u, "UnitMarks_Draw", 0u },
    /* TechnoClass::DrawExtras, just before the native talk bubble. */
    { 0x6F5E8Du, 6u, "UnitMarks_DrawExtras", 0u },
    /* After the tactical and shroud passes, before the composite surface is presented. */
    { 0x4F4562u, 6u, "UnitMarks_DrawSpeechOverlay", 0u },
    /* InfantryTypeClass::LoadFromINI entry. */
    { 0x5240A0u, 6u, "UnitMarks_ReadHero", 0u },
    /* Scenario startup, CellClass::GetCoords and RadarEventClass::Create. */
    { 0x683E94u, 5u, "UnitMarks_ResetImpactMarkers", 0u },
    { 0x486840u, 5u, "UnitMarks_ObserveSuperweaponLaunch", 0u },
    { 0x65FA70u, 6u, "UnitMarks_ObserveSuperweaponImpact", 0u },
};

using VisibleToFn = unsigned char (__attribute__((thiscall)) *)(void*, void*);
using GetDisguiseFn = void* (__attribute__((thiscall)) *)(void*, int);
using VisualFn = int (__attribute__((thiscall)) *)(void*, int, void*);
using LoadShapeFn = void* (__fastcall *)(const char*, bool);
using ResolveShapeFn = void* (__attribute__((thiscall)) *)(void*);
using ShapeFrameBoundsFn = RectangleStruct* (__attribute__((thiscall)) *)(void*, RectangleStruct*, int);
using CreatePaletteFn = void (__fastcall *)(const char*, void**, void**);
using FreeGameMemoryFn = void (__cdecl *)(void*);
using GetCoordsFn = WorldPoint* (__attribute__((thiscall)) *)(const void*, WorldPoint*);
using ProjectFn = bool (__attribute__((thiscall)) *)(void*, const WorldPoint*, Point2D*);
using SurfaceSizeFn = int (__attribute__((thiscall)) *)(void*);
using DrawShapeFn = void (__fastcall *)(void*, void*, void*, int,
    const Point2D*, const RectangleStruct*, int, int, int, int, int, int,
    void*, int, int, int);

/* Match the displayed type chosen by UnitClass::DrawIt at 0x73D2CA. */
const void* DisplayedType(void* unit, unsigned typeOffset)
{
    void** vt = *(void***)unit;
    void* house = *(void**)ENGINE_ADDRESS(0xA83D4Cu);
    if (!vt || !house) return NULL;
    VisibleToFn visibleTo = (VisibleToFn)vt[0x440u / 4u];
    GetDisguiseFn getDisguise = (GetDisguiseFn)vt[0xCCu / 4u];
    if (!visibleTo || !getDisguise) return NULL;
    if (!visibleTo(unit, house)) return getDisguise(unit, 1);
    return Game::ObjectType(unit, typeOffset);
}

/* CreateFromFile adds the conversion to the engine's live ConvertClass list.
 * Recheck it after renderer changes and free only our separately allocated PAL. */
void* MarkPalette()
{
    static void* convert = NULL;
    static void* colors = NULL;
    static int index = -1;

    void** entries = *(void***)ENGINE_ADDRESS(0x89ECFCu);
    int count = *(int*)ENGINE_ADDRESS(0x89ED08u);
    if (convert && entries && count > 0 && count <= 16384)
    {
        if (index >= 0 && index < count && entries[index] == convert) return convert;
        for (int i = 0; i < count; ++i)
        {
            if (entries[i] != convert) continue;
            index = i;
            return convert;
        }
    }
    if (!*(void**)ENGINE_ADDRESS(0x887310u)) return NULL;
    if (colors) ((FreeGameMemoryFn)ENGINE_ADDRESS(0x7C8B3Du))(colors);
    colors = convert = NULL;
    index = -1;
    ((CreatePaletteFn)ENGINE_ADDRESS(0x72ADE0u))("MARKPAL.PAL", &colors, &convert);
    return convert;
}

void DrawMarkerSprite(void* surface, void* palette, void* shape, int frame,
                      const Point2D* location, const RectangleStruct* bounds)
{
    // Center the sprite on its canvas, with no depth test against the world.
    ((DrawShapeFn)ENGINE_ADDRESS(0x4AED70u))(surface, palette, shape, frame, location, bounds,
        0x600, 0, 0, 0, 1000, 0, NULL, 0, 0, 0);
}

void DrawMark(Mark mark, void* surface, const Point2D* location, const RectangleStruct* bounds,
              bool centerOnCell = false)
{
    if (mark == Mark::None || !surface || !location || !bounds) return;

    void* shape = ((LoadShapeFn)ENGINE_ADDRESS(0x5B40B0u))(
        mark == Mark::Alert ? "ALERTMARK.SHP" : "DANGERMARK.SHP", false);
    if (!shape) return;
    shape = ((ResolveShapeFn)ENGINE_ADDRESS(0x69E580u))(shape);
    if (!shape) return;
    const auto* header = static_cast<const short*>(shape);
    const short frameCount = header[3];
    if (frameCount <= 0) return;
    void* palette = MarkPalette();
    if (!palette) return;

    const int frame = frameCount > 2 ? 2 : 0;
    Point2D anchor = *location;
    if (centerOnCell)
    {
        RectangleStruct pixels{};
        const auto frameBounds = reinterpret_cast<ShapeFrameBoundsFn>(ENGINE_ADDRESS(0x69E7E0u));
        frameBounds(shape, &pixels, frame);
        // The unit icon sits high within its canvas. Center its visible pixels on the impact cell.
        anchor.X += header[1] / 2 - pixels.X - pixels.Width / 2;
        anchor.Y += header[2] / 2 - pixels.Y - pixels.Height / 2;
        location = &anchor;
    }
    DrawMarkerSprite(surface, palette, shape, frame, location, bounds);
}

bool ProjectMarker(void* tactical, const WorldPoint& world, const RectangleStruct& view, Point2D& pixel)
{
    const auto project = reinterpret_cast<ProjectFn>(ENGINE_ADDRESS(0x6D2140u));
    if (!project(tactical, &world, &pixel)) return false;
    pixel.X -= view.X;
    pixel.Y -= view.Y;
    return true;
}

void DrawSpeechMarkers(const ObjectArray& objects, bool building, void* tactical, void* surface,
                       void* palette, void* shape, const RectangleStruct& view)
{
    if (!objects.items || objects.count <= 0 || objects.count > objects.capacity) return;
    const RectangleStruct bounds{0, 0, view.Width, view.Height};
    const unsigned typeOffset = building ? BuildingTypeOffset : VehicleTypeOffset;
    for (int i = 0; i < objects.count; ++i)
    {
        const auto* object = static_cast<const unsigned char*>(objects.items[i]);
        // InLimbo also covers objects that are not currently placed on the map.
        if (!object || object[InLimboOffset] || !Game::IsAlive(object)) continue;
        const void* type = Game::ObjectType(object, typeOffset);
        if (!type || !HasSpeechMark(type, building)) continue;

        auto* vtable = *reinterpret_cast<void* const* const*>(object);
        const auto getCoords = reinterpret_cast<GetCoordsFn>(vtable[0x48u / 4u]);
        WorldPoint world{};
        Point2D pixel{};
        getCoords(object, &world);
        if (!ProjectMarker(tactical, world, view, pixel)) continue;
        DrawMarkerSprite(surface, palette, shape, 2, &pixel, &bounds);
    }
}
}

extern "C" __declspec(dllexport) DWORD __cdecl UnitMarks_DrawSpeechOverlay(const SyringeRegisters*)
{
    if (!*static_cast<const unsigned char*>(ENGINE_ADDRESS(0xA8ED5Cu))) return 0;
    void* tactical = *static_cast<void**>(ENGINE_ADDRESS(0x887324u));
    void* surface = *static_cast<void**>(ENGINE_ADDRESS(0x88731Cu));
    if (!tactical || !surface) return 0;

    auto view = *static_cast<const RectangleStruct*>(ENGINE_ADDRESS(0x886FA0u));
    auto* vtable = *static_cast<void***>(surface);
    const int width = reinterpret_cast<SurfaceSizeFn>(vtable[0x7Cu / 4u])(surface);
    const int height = reinterpret_cast<SurfaceSizeFn>(vtable[0x80u / 4u])(surface);
    if (view.Width > width) view.Width = width;
    if (view.Height > height) view.Height = height;
    if (view.Width <= 0 || view.Height <= 0) return 0;

    void* palette = *static_cast<void**>(ENGINE_ADDRESS(0x87F6C4u));
    void* shape = *static_cast<void**>(ENGINE_ADDRESS(0xAC1484u));
    if (shape && palette)
    {
        shape = ((ResolveShapeFn)ENGINE_ADDRESS(0x69E580u))(shape);
        if (shape && static_cast<const short*>(shape)[3] >= 3)
        {
            DrawSpeechMarkers(*static_cast<const ObjectArray*>(ENGINE_ADDRESS(UnitArrayAddress)),
                              false, tactical, surface, palette, shape, view);
            DrawSpeechMarkers(*static_cast<const ObjectArray*>(ENGINE_ADDRESS(BuildingArrayAddress)),
                              true, tactical, surface, palette, shape, view);
        }
    }

    WorldPoint impacts[MaxImpactMarkers];
    const unsigned count = CollectImpactMarkers(impacts);
    const RectangleStruct bounds{0, 0, view.Width, view.Height};
    for (unsigned i = 0; i < count; ++i)
    {
        Point2D pixel{};
        if (ProjectMarker(tactical, impacts[i], view, pixel))
            DrawMark(Mark::Danger, surface, &pixel, &bounds, true);
    }
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl UnitMarks_Draw(const SyringeRegisters* regs)
{
    if (!regs) return 0;
    auto* unit = reinterpret_cast<void*>(regs->esi);
    auto* stack = reinterpret_cast<const char*>(regs->esp);
    auto* bounds = reinterpret_cast<const RectangleStruct*>(regs->ebx);
    if (!unit || !stack || !bounds) return 0;
    const void* displayedType = DisplayedType(unit, VehicleTypeOffset);
    if (!displayedType) return 0;

    const void* actualType = Game::ObjectType(unit, VehicleTypeOffset);
    const Mark mark = VehicleMark(unit, displayedType, actualType);
    if (mark != Mark::None && actualType && HasSpeechMark(actualType, false)) return 0;
    DrawMark(mark, *static_cast<void**>(ENGINE_ADDRESS(0x887314u)),
             reinterpret_cast<const Point2D*>(stack + 0x30u), bounds);
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl UnitMarks_DrawExtras(const SyringeRegisters* regs)
{
    if (!regs) return 0;
    auto* obj = reinterpret_cast<void*>(regs->ebp);
    auto* stack = reinterpret_cast<const char*>(regs->esp);
    if (!obj || !stack) return 0;
    void** vt = *(void***)obj;
    const bool infantry = vt == ENGINE_ADDRESS(Game::InfantryVtable);
    if (!infantry && vt != ENGINE_ADDRESS(BuildingVtable)) return 0;
    const unsigned char* bytes = (const unsigned char*)obj;
    if (bytes[InLimboOffset] || !Game::IsAlive(obj)) return 0;

    const void* type = DisplayedType(obj, infantry ? Game::InfantryTypeOffset : BuildingTypeOffset);
    if (!type) return 0;
    if (infantry && !Game::IsInfantryType(type)) return 0;
    const Mark mark = infantry ? InfantryMark(type) : BuildingMark(type);
    if (mark == Mark::None) return 0;
    if (!infantry)
    {
        const void* actualType = Game::ObjectType(obj, BuildingTypeOffset);
        if (actualType && HasSpeechMark(actualType, true)) return 0;
    }

    /* Some objects can reach DrawExtras with their sprite hidden. Keep the
     * red markers out of the cloak ripple and fully hidden states. */
    VisualFn visual = (VisualFn)vt[0x68u / 4u];
    if (!visual || visual(obj, 0, NULL) >= 4) return 0;
    DrawMark(mark, *static_cast<void**>(ENGINE_ADDRESS(0x887314u)),
             *reinterpret_cast<const Point2D* const*>(stack + 0x9Cu),
             *reinterpret_cast<const RectangleStruct* const*>(stack + 0xA0u));
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl UnitMarks_ReadHero(const SyringeRegisters* regs)
{
    if (!regs || !regs->ecx || !regs->esp) return 0;
    auto* type = reinterpret_cast<const void*>(regs->ecx);
    auto* stack = reinterpret_cast<void* const*>(regs->esp);
    void* ini = stack[1];
    if (ini) LoadHeroRule(type, ini);
    return 0;
}

extern "C" __declspec(dllexport) HRESULT __cdecl SyringeHandshake(SyringeHandshakeInfo* info)
{
    /* Fixed addresses are for this exact YR 1.001 executable. */
    if (!info || info->cbSize < (int)sizeof(SyringeHandshakeInfo) ||
        info->exeFilesize != 4816864u || info->exeTimestamp != 0x3BDF544Eu ||
        info->exeCRC != 0x1B499086u)
        return S_FALSE;
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
