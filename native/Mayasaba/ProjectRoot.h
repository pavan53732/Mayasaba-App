#pragma once
#include <string>
#include <stdexcept>
#include <memory>
namespace mayasaba
{
    struct ProjectRoot
    {
        std::wstring canonicalPath;
        std::wstring displayName;
        std::wstring stableIdentity;
        // Keeps every observed path component locked through the owner commit.
        std::shared_ptr<void> observation;
    };
    // Opens only local fixed-volume, ordinary directory roots. No scan, write or CLI effect.
    // Throws with specific reason; selected root is a *candidate* until validated.
    ProjectRoot ValidateProjectRoot(std::wstring const& selectedPath);

    // Validates a single child folder name without checking the filesystem.
    // Returns empty string if valid, or a descriptive English error message if invalid.
    std::string ValidateSingleChildFolderName(std::wstring const& newFolderName);

    // Creates exactly one empty folder under an existing validated parent.
    // Rejects path traversal, reserved names, separators, collisions, reparse points.
    ProjectRoot CreateProjectFolder(std::wstring const& parentPath, std::wstring const& newFolderName);

    // Verifies that candidatePath matches expectedIdentity. Throws if missing, invalid, or identity mismatch.
    ProjectRoot LocateProject(std::wstring const& expectedIdentity, std::wstring const& candidatePath);
}
