#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Point2D { int X, Y; };
struct RectangleStruct { int X, Y, Width, Height; };

struct SyringeRegisters
{
    DWORD origin, flags;
    DWORD edi, esi, ebp, esp, ebx, edx, ecx, eax;
};

struct SyringeHandshakeInfo
{
    int cbSize;
    int num_hooks;
    unsigned int checksum;
    DWORD exeFilesize;
    DWORD exeTimestamp;
    unsigned int exeCRC;
    int cchMessage;
    char* Message;
};

extern "C" DWORD __cdecl UnitMarks_Draw(const SyringeRegisters* regs);
extern "C" DWORD __cdecl UnitMarks_DrawExtras(const SyringeRegisters* regs);
extern "C" DWORD __cdecl UnitMarks_ReadHero(const SyringeRegisters* regs);
extern "C" HRESULT __cdecl SyringeHandshake(SyringeHandshakeInfo* info);

#ifdef UNIT_MARKS_FIXTURE
void* FixtureAddress(unsigned address);
#define ENGINE_ADDRESS(address) FixtureAddress(address)
#else
#define ENGINE_ADDRESS(address) reinterpret_cast<void*>(address)
#endif

namespace Game
{
constexpr unsigned InfantryVtable = 0x7EB058u;
constexpr unsigned InfantryTypeVtable = 0x7EB610u;
constexpr unsigned InfantryTypeOffset = 0x6C0u;

inline const void* ObjectType(const void* object, unsigned offset)
{
    return *reinterpret_cast<const void* const*>(static_cast<const char*>(object) + offset);
}

inline const char* TypeId(const void* type)
{
    return static_cast<const char*>(type) + 0x24u;
}

inline bool IsAlive(const void* object)
{
    const auto* bytes = static_cast<const unsigned char*>(object);
    return bytes[0x90u] && *reinterpret_cast<const int*>(bytes + 0x6Cu) > 0;
}

inline bool IsInfantryType(const void* type)
{
    return type && *static_cast<void* const*>(type) == ENGINE_ADDRESS(InfantryTypeVtable);
}
}

enum class Mark { None, Speech, Alert, Danger };

Mark VehicleMark(const void* unit, const void* displayedType, const void* actualType);
Mark BuildingMark(const void* displayedType, const void* actualType);
Mark InfantryMark(const void* type);
void LoadHeroRule(const void* type, void* ini);
