#pragma once
#include <windows.h>
#include <objbase.h>
#include <string>
#include <stdexcept>
#include <cstdio>

namespace mayasaba
{
    inline bool IsUuid(std::string const& value)
    {
        if (value.size() != 36) return false;
        for (size_t i = 0; i != value.size(); ++i)
            if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
            else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
        return value != "00000000-0000-0000-0000-000000000000";
    }
    inline std::string NewUuid()
    {
        GUID g{};
        if (FAILED(CoCreateGuid(&g))) throw std::runtime_error("Could not allocate operation identity");
        char value[37]{};
        sprintf_s(value, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
            g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
        return value;
    }
}
