#include "UnitMarks.h"
#include <cstddef>

namespace
{
constexpr unsigned VehicleTypeOffset = 0x6C4u;
constexpr unsigned BuildingTypeOffset = 0x520u;
constexpr unsigned BuildingVtable = 0x7E3EBCu;
constexpr unsigned InLimboOffset = 0x81u;

struct SyHookDecl
{
    unsigned int addr;
    unsigned int size;
    const char* name;
    unsigned int pad;
};

static_assert(sizeof(SyHookDecl) == 16, "Syringe hook records must be 16 bytes");
static_assert(sizeof(SyringeHandshakeInfo) == 32, "Syringe handshake must be 32 bytes");
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
    /* InfantryTypeClass::LoadFromINI entry. */
    { 0x5240A0u, 6u, "UnitMarks_ReadHero", 0u },
};

using VisibleToFn = unsigned char (__attribute__((thiscall)) *)(void*, void*);
using GetDisguiseFn = void* (__attribute__((thiscall)) *)(void*, int);
using VisualFn = int (__attribute__((thiscall)) *)(void*, int, void*);
using LoadShapeFn = void* (__fastcall *)(const char*, bool);
using ResolveShapeFn = void* (__attribute__((thiscall)) *)(void*);
using CreatePaletteFn = void (__fastcall *)(const char*, void**, void**);
using FreeGameMemoryFn = void (__cdecl *)(void*);
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

void DrawMark(Mark mark, const Point2D* location, const RectangleStruct* bounds)
{
    if (mark == Mark::None || !location || !bounds) return;
    void* surface = *(void**)ENGINE_ADDRESS(0x887314u);
    if (!surface) return;

    const bool isSpeech = mark == Mark::Speech;
    void* shape = isSpeech ? *(void**)ENGINE_ADDRESS(0xAC1484u) :
        ((LoadShapeFn)ENGINE_ADDRESS(0x5B40B0u))(
            mark == Mark::Alert ? "ALERTMARK.SHP" : "DANGERMARK.SHP", false);
    if (!shape) return;
    shape = ((ResolveShapeFn)ENGINE_ADDRESS(0x69E580u))(shape);
    if (!shape) return;
    const short frameCount = ((const short*)shape)[3];
    if (frameCount <= 0 || (isSpeech && frameCount < 3)) return;
    void* palette = isSpeech ? *(void**)ENGINE_ADDRESS(0x87F6C4u) : MarkPalette();
    if (!palette) return;

    /* The transparent source canvas places the icon above the object. */
    const int frame = frameCount > 2 ? 2 : 0;
    ((DrawShapeFn)ENGINE_ADDRESS(0x4AED70u))(surface, palette, shape, frame, location, bounds,
        0x600, 0, 0, 0, 1000, 0, NULL, 0, 0, 0);
}
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
    DrawMark(mark, reinterpret_cast<const Point2D*>(stack + 0x30u), bounds);
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

    /* Some objects can reach DrawExtras with their sprite hidden. Keep the
     * mine/hero marker out of the cloak ripple and fully hidden states. */
    VisualFn visual = (VisualFn)vt[0x68u / 4u];
    if (!visual || visual(obj, 0, NULL) >= 4) return 0;
    DrawMark(mark, *reinterpret_cast<const Point2D* const*>(stack + 0x9Cu),
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
