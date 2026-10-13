#pragma once
#include <string>
#include <stdexcept>
namespace mayasaba
{
    struct ProjectRoot
    {
        std::wstring canonicalPath;
        std::wstring displayName;
        std::wstring stableIdentity;
    };
    // Opens only local fixed-volume, ordinary directory roots. No scan, write or CLI effect.
    // Throws with specific reason; selected root is a *candidate* until validated.
    ProjectRoot ValidateProjectRoot(std::wstring const& selectedPath);
}
