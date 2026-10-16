// seed_store — isolated UI-verification store seeding utility.
//
// This utility exists only so the native UI can be exercised end to end without
// the separately hosted Windows folder picker. It binds a project and optionally
// records contributions through the SAME production code path the application
// uses (mayasaba::Storage and mayasaba::ProjectService), writing only to an
// explicitly supplied store file.
//
// It hard-refuses any path inside the production store directory
// (%LOCALAPPDATA%\Mayasaba), so it can never seed, migrate or overwrite the
// user's real Mayasaba database. It launches no CLI, scans nothing and creates
// no directory.

#include "../Mayasaba/Storage.h"
#include "../Mayasaba/ProjectService.h"
#include "../Mayasaba/NativeIds.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
    // The test project is plain C++ with no C++/WinRT dependency, so the few
    // conversions needed here are done with the Win32 helpers directly.
    std::string ToUtf8(std::wstring const& value)
    {
        int const n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(n, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
        return result;
    }

    std::filesystem::path ProductionDirectory()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD const length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            throw std::runtime_error("LOCALAPPDATA is unavailable; refusing to guess a store location");
        }
        return std::filesystem::path{std::wstring{buffer, length}} / L"Mayasaba";
    }

    // The guard is deliberately conservative: anything inside the production
    // store directory is refused, whatever it is named.
    void RefuseProductionPath(std::filesystem::path const& store)
    {
        auto const production = ProductionDirectory();
        auto const absolute = std::filesystem::absolute(store);
        auto const prefix = std::filesystem::absolute(production);
        auto const rel = std::filesystem::relative(absolute, prefix);
        if (!rel.empty() && rel.native().rfind(L"..", 0) != 0)
        {
            throw std::runtime_error("Refused: the supplied store path is inside the production store directory " +
                ToUtf8(prefix.wstring()));
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc < 3)
        {
            std::wcerr << L"usage: seed_store <store.db> <project-folder> [contribution-count] [--stage-pending <text>]\n";
            return 2;
        }
        std::filesystem::path const storePath{argv[1]};
        std::wstring const projectFolder{argv[2]};

        // Positional count, then an optional fault-seeding flag. The flag exists so the
        // unresolved-submission path can be exercised without waiting for a real failure.
        int contributionCount = 0;
        std::string pendingText;
        for (int index = 3; index < argc; ++index)
        {
            std::wstring const argument{argv[index]};
            if (argument == L"--stage-pending")
            {
                if (index + 1 >= argc)
                {
                    std::wcerr << L"--stage-pending requires the pending submission text\n";
                    return 2;
                }
                pendingText = ToUtf8(argv[++index]);
            }
            else
            {
                contributionCount = std::stoi(argument);
            }
        }

        RefuseProductionPath(storePath);
        if (!std::filesystem::exists(projectFolder))
        {
            std::wcerr << L"refused: the project folder does not exist\n";
            return 2;
        }

        mayasaba::Storage store{storePath};
        store.ReconcileCrashOperations();

        mayasaba::ProjectService service{store};
        auto const projectId = service.Bind(mayasaba::NewUuid(), projectFolder);

        for (int index = 0; index < contributionCount; ++index)
        {
            auto const submissionId = mayasaba::NewUuid();
            std::string const body =
                "Seeded contribution " + std::to_string(index + 1) +
                ". This text exists only to exercise the native timeline renderer, its "
                "reading position and its bounded update batches. It is a stored user "
                "contribution, not an agent reply, and no reply is claimed for it.";
            // A contribution may only be committed against a staged submission, so the
            // durable intent is recorded first. Calling SaveContribution directly
            // returns Conflict and would silently produce an empty history.
            auto const staged = store.StageSubmission(projectId, submissionId, body);
            auto const disposition = store.SaveContribution(projectId, staged, body);
            if (disposition != mayasaba::SaveDisposition::Saved &&
                disposition != mayasaba::SaveDisposition::Duplicate)
            {
                throw std::runtime_error("Seeded contribution was refused with disposition " +
                    std::to_string(static_cast<int>(disposition)));
            }
        }

        // An unresolved submission: the durable pending row the controller must
        // reconcile before any new contribution may be accepted for this project.
        if (!pendingText.empty())
        {
            store.StageSubmission(projectId, mayasaba::NewUuid(), pendingText);
        }

        auto const record = store.Project(projectId);
        std::cout << "seeded project " << (record ? record->id : std::string{"<none>"}) << "\n";
        std::cout << "canonical path " << (record ? record->canonicalPath : std::string{"<none>"}) << "\n";
        std::cout << "contributions " << contributionCount << "\n";
        std::cout << "pending submission " << (pendingText.empty() ? "none" : pendingText) << "\n";
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
