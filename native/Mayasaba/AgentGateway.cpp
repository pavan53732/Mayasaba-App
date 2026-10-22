#include "AgentGateway.h"

#include "Sha256Hex.h"

#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace mayasaba::gateway
{
    namespace
    {
        struct UniqueHandle
        {
            HANDLE value{ nullptr };
            UniqueHandle() = default;
            explicit UniqueHandle(HANDLE h) noexcept : value(h) {}
            ~UniqueHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
            UniqueHandle(UniqueHandle const&) = delete;
            UniqueHandle& operator=(UniqueHandle const&) = delete;
            explicit operator bool() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
        };

        std::wstring UtcNow()
        {
            SYSTEMTIME now{};
            GetSystemTime(&now);
            wchar_t buffer[32]{};
            swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
            return buffer;
        }

        std::wstring FileTimeUtc(FILETIME const& value)
        {
            SYSTEMTIME utc{};
            if (!FileTimeToSystemTime(&value, &utc)) return {};
            wchar_t buffer[32]{};
            swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
                utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
            return buffer;
        }

        std::string Narrow(std::wstring const& value)
        {
            if (value.empty()) return {};
            int const needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                nullptr, 0, nullptr, nullptr);
            if (needed <= 0) return {};
            std::string out(static_cast<std::size_t>(needed), '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                out.data(), needed, nullptr, nullptr);
            return out;
        }

        std::wstring Widen(std::string const& value)
        {
            if (value.empty()) return {};
            int const needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (needed <= 0) return {};
            std::wstring out(static_cast<std::size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed);
            return out;
        }

        std::wstring Trim(std::wstring value)
        {
            auto const notSpace = [](wchar_t c) { return c != L' ' && c != L'\t' && c != L'\r' && c != L'\n'; };
            auto const first = std::find_if(value.begin(), value.end(), notSpace);
            auto const last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
            if (first >= last) return {};
            return std::wstring(first, last);
        }

        std::wstring FormatIdentity(FILE_ID_INFO const& identity)
        {
            std::wostringstream id;
            id << std::hex << identity.VolumeSerialNumber << L":";
            for (unsigned char byte : identity.FileId.Identifier)
                id << std::setw(2) << std::setfill(L'0') << static_cast<unsigned>(byte);
            return id.str();
        }

        // Hash a file's bytes. Returns an empty string on any failure rather than
        // a partial digest: an identity with a wrong digest is worse than one
        // that says it could not hash.
        std::string Sha256OfFile(std::wstring const& path, std::uint64_t& sizeOut)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return {};
            std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            sizeOut = bytes.size();
            return mayasaba::Sha256Hex(bytes).value_or(std::string{});
        }

        // Authenticode observation. Absent signature is `verified == false` with
        // an empty subject, which is a recorded observation and not a failure:
        // D-11 asks for "signer observation when available".
        void ObserveSigner(std::wstring const& path, ExecutableIdentity& identity)
        {
            WINTRUST_FILE_INFO fileInfo{};
            fileInfo.cbStruct = sizeof(fileInfo);
            fileInfo.pcwszFilePath = path.c_str();

            WINTRUST_DATA data{};
            data.cbStruct = sizeof(data);
            data.dwUIChoice = WTD_UI_NONE;
            data.fdwRevocationChecks = WTD_REVOKE_NONE;   // offline-safe; no network
            data.dwUnionChoice = WTD_CHOICE_FILE;
            data.pFile = &fileInfo;
            data.dwStateAction = WTD_STATEACTION_VERIFY;
            data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;

            GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            LONG const status = WinVerifyTrust(nullptr, &policy, &data);

            if (status == ERROR_SUCCESS)
            {
                identity.signerVerified = true;
                // The signer certificate is reached through the provider data,
                // not through CRYPT_PROVIDER_SGNR directly: that struct carries a
                // certificate *chain*, and the leaf is the first element of the
                // first chain.
                auto* provider = static_cast<CRYPT_PROVIDER_DATA*>(
                    WTHelperProvDataFromStateData(data.hWVTStateData));
                if (provider)
                {
                    if (CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0))
                    {
                        if (CRYPT_PROVIDER_CERT* signerCertificate = WTHelperGetProvCertFromChain(signer, 0))
                        {
                            if (signerCertificate->pCert)
                            {
                                wchar_t name[512]{};
                                if (CertGetNameStringW(signerCertificate->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                        0, nullptr, name, static_cast<DWORD>(std::size(name))) > 1)
                                    identity.signerSubject = Narrow(name);
                            }
                        }
                    }
                }
            }

            data.dwStateAction = WTD_STATEACTION_CLOSE;
            WinVerifyTrust(nullptr, &policy, &data);
        }

        // The directory a `.cmd`/`.exe` shim lives in, used to give a candidate a
        // readable provenance label.
        std::wstring DirectoryOf(std::wstring const& path)
        {
            auto const slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
        }

        // File extensions a CLI installation can legitimately present. A `.cmd` or
        // `.bat` shim is included because two of the three CLIs here are installed
        // that way; excluding them would report an installed CLI as MISSING, which
        // UI-UX §6.1 forbids ("Do not label an installed but incompatible CLI as
        // missing" — and here it is not even incompatible).
        bool IsLaunchableExtension(std::wstring const& path)
        {
            auto const dot = path.find_last_of(L'.');
            if (dot == std::wstring::npos) return false;
            std::wstring ext = path.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
            return ext == L".exe" || ext == L".cmd" || ext == L".bat" || ext == L".com";
        }

        // Resolve one PATH entry plus a CLI name into a concrete file.
        //
        // A name that already carries an extension is tried as written. A bare
        // name is tried against PATHEXT order, which is the same order the shell
        // would use. This is resolution, not selection: when two files match in
        // different directories, both are returned and D-11's tie-break applies.
        std::vector<std::wstring> ResolveInDirectory(std::wstring const& directory, std::wstring const& name)
        {
            std::vector<std::wstring> found;
            if (directory.empty()) return found;

            std::wstring const base = directory.back() == L'\\' || directory.back() == L'/'
                ? directory + name : directory + L"\\" + name;

            if (name.find(L'.') != std::wstring::npos)
            {
                DWORD const attributes = GetFileAttributesW(base.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    found.push_back(base);
                return found;
            }

            for (wchar_t const* extension : { L".com", L".exe", L".bat", L".cmd" })
            {
                std::wstring candidate = base + extension;
                DWORD const attributes = GetFileAttributesW(candidate.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    found.push_back(candidate);
            }
            return found;
        }

        // Build the identity record for one resolved path. Every observation that
        // can fail is reported as absent rather than guessed.
        ExecutableIdentity Identify(Cli cli, std::wstring const& path, std::string source)
        {
            ExecutableIdentity identity;
            identity.cli = cli;
            identity.discoverySource = std::move(source);
            identity.observedUtc = UtcNow();

            UniqueHandle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));

            if (file)
            {
                FILE_ID_INFO fileId{};
                if (GetFileInformationByHandleEx(file.value, FileIdInfo, &fileId, sizeof(fileId)))
                    identity.stableIdentity = FormatIdentity(fileId);

                // Canonical path from the handle, so a relative entry, a `..`
                // segment or a symlinked directory cannot leave two spellings of
                // one file looking like two installations.
                DWORD const needed = GetFinalPathNameByHandleW(file.value, nullptr, 0, FILE_NAME_NORMALIZED);
                if (needed > 0)
                {
                    std::wstring canonical(needed + 1, L'\0');
                    DWORD const actual = GetFinalPathNameByHandleW(file.value, canonical.data(),
                        static_cast<DWORD>(canonical.size()), FILE_NAME_NORMALIZED);
                    if (actual > 0 && actual < canonical.size())
                    {
                        canonical.resize(actual);
                        if (canonical.rfind(L"\\\\?\\", 0) == 0) canonical.erase(0, 4);
                        identity.canonicalPath = canonical;
                    }
                }

                BY_HANDLE_FILE_INFORMATION info{};
                if (GetFileInformationByHandle(file.value, &info))
                    identity.lastWriteUtc = FileTimeUtc(info.ftLastWriteTime);
            }

            if (identity.canonicalPath.empty()) identity.canonicalPath = path;
            identity.sha256 = Sha256OfFile(identity.canonicalPath, identity.sizeBytes);
            ObserveSigner(identity.canonicalPath, identity);
            return identity;
        }

        // The process count inside the job, observed through the job's own
        // accounting rather than by walking the system. This is what makes the
        // "owned tree" claim checkable: a `.cmd` shim starts cmd.exe and then a
        // runtime, and a kernel that reported only its direct child would be
        // describing a tree of one.
        std::size_t JobProcessCount(HANDLE job)
        {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if (job && QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                    &accounting, sizeof(accounting), nullptr))
                return static_cast<std::size_t>(accounting.ActiveProcesses);
            return 0;
        }
    }

    wchar_t const* CliDisplayName(Cli cli) noexcept
    {
        switch (cli)
        {
        case Cli::Hermes: return L"Hermes Agent CLI";
        case Cli::KiloCode: return L"Kilo Code CLI";
        case Cli::ClaudeCode: return L"Claude Code CLI";
        }
        return L"Unknown CLI";
    }

    char const* CliKey(Cli cli) noexcept
    {
        switch (cli)
        {
        case Cli::Hermes: return "hermes";
        case Cli::KiloCode: return "kilo-code";
        case Cli::ClaudeCode: return "claude-code";
        }
        return "unknown";
    }

    wchar_t const* ReadinessText(Readiness readiness) noexcept
    {
        switch (readiness)
        {
        case Readiness::Checking: return L"CHECKING";
        case Readiness::Ready: return L"READY";
        case Readiness::Missing: return L"MISSING";
        case Readiness::Unsupported: return L"UNSUPPORTED";
        case Readiness::ProbeFailed: return L"PROBE_FAILED";
        }
        return L"CHECKING";
    }

    char const* ReadinessDotToken(Readiness readiness) noexcept
    {
        switch (readiness)
        {
        case Readiness::Ready: return "MayasabaMintBrush";
        case Readiness::Missing:
        case Readiness::Unsupported:
        case Readiness::ProbeFailed: return "MayasabaDangerBrush";
        case Readiness::Checking: return "MayasabaAmberBrush";
        }
        return "MayasabaAmberBrush";
    }

    bool NeedsProblemCard(Readiness readiness) noexcept
    {
        return readiness == Readiness::Missing
            || readiness == Readiness::Unsupported
            || readiness == Readiness::ProbeFailed;
    }

    DiscoveryResult Discover(Cli cli, std::wstring const& userSelectedPath)
    {
        DiscoveryResult result;

        if (!userSelectedPath.empty())
        {
            DWORD const attributes = GetFileAttributesW(userSelectedPath.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                result.diagnostic = L"The selected executable does not exist: " + userSelectedPath;
                return result;
            }
            if (!IsLaunchableExtension(userSelectedPath))
            {
                result.diagnostic = L"The selected file is not a launchable executable: " + userSelectedPath;
                return result;
            }
            Candidate candidate;
            candidate.identity = Identify(cli, userSelectedPath, "user-selection");
            candidate.userSelected = true;
            result.candidates.push_back(std::move(candidate));
            return result;
        }

        // The CLI name each installation publishes on PATH. These are the three
        // documented entry points, not a guess at file names.
        wchar_t const* commandName = nullptr;
        switch (cli)
        {
        case Cli::Hermes: commandName = L"hermes"; break;
        case Cli::KiloCode: commandName = L"kilo"; break;
        case Cli::ClaudeCode: commandName = L"claude"; break;
        }

        DWORD const required = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        std::wstring pathValue;
        if (required > 0)
        {
            pathValue.resize(required);
            DWORD const written = GetEnvironmentVariableW(L"PATH", pathValue.data(), required);
            if (written > 0 && written < required) pathValue.resize(written);
            else pathValue.clear();
        }

        // Deduplicate directories by their canonical path. The same directory
        // twice on PATH must not make one installation look like two candidates:
        // that would turn D-11's tie-break into a coin flip on a machine with a
        // repeated PATH entry.
        std::vector<std::wstring> seen;
        std::size_t start = 0;
        while (start <= pathValue.size())
        {
            auto const separator = pathValue.find(L';', start);
            std::wstring entry = pathValue.substr(start,
                separator == std::wstring::npos ? std::wstring::npos : separator - start);
            if (!entry.empty())
            {
                wchar_t canonical[MAX_PATH * 2]{};
                DWORD const length = GetFullPathNameW(entry.c_str(),
                    static_cast<DWORD>(std::size(canonical)), canonical, nullptr);
                std::wstring resolved = (length > 0 && length < std::size(canonical))
                    ? std::wstring(canonical, length) : entry;

                std::wstring folded = resolved;
                std::transform(folded.begin(), folded.end(), folded.begin(), towlower);
                if (std::find(seen.begin(), seen.end(), folded) == seen.end())
                {
                    seen.push_back(folded);
                    result.searchedDirectories.push_back(resolved);

                    DWORD const attributes = GetFileAttributesW(resolved.c_str());
                    if (attributes == INVALID_FILE_ATTRIBUTES)
                    {
                        // A PATH entry that does not resolve is ordinary and is
                        // not an error; it is only worth naming when the whole
                        // search comes up empty.
                        continue;
                    }
                    for (auto const& found : ResolveInDirectory(resolved, commandName))
                    {
                        // Two PATH entries can resolve to one file through a
                        // junction. Deduplicate on the observed stable identity,
                        // so the candidate count describes installations and not
                        // spellings.
                        Candidate candidate;
                        candidate.identity = Identify(cli, found, "path");
                        bool duplicate = false;
                        for (auto const& existing : result.candidates)
                        {
                            if (!candidate.identity.stableIdentity.empty() &&
                                existing.identity.stableIdentity == candidate.identity.stableIdentity)
                            {
                                duplicate = true;
                                break;
                            }
                        }
                        if (!duplicate) result.candidates.push_back(std::move(candidate));
                    }
                }
            }
            if (separator == std::wstring::npos) break;
            start = separator + 1;
        }

        if (result.candidates.empty())
            result.diagnostic = L"No " + std::wstring(commandName) + L" executable was found in any PATH directory.";
        return result;
    }

    ProbeResult Probe(ExecutableIdentity const& identity, ProbeSpec const& spec)
    {
        ProbeResult result;

        if (identity.canonicalPath.empty())
        {
            result.readiness = Readiness::ProbeFailed;
            result.reason = L"The executable path is empty, so no probe could be launched.";
            return result;
        }

        // The launch goes through the kernel, so a `.cmd` shim is started the way
        // the shell would start it while still being owned, bounded and
        // cancellable. CreateProcess cannot execute a `.cmd` directly, so the
        // command processor is the image and the shim is its first argument —
        // which is exactly what cmd.exe does for an interactive invocation.
        kernel::Spec launch;
        std::wstring const extension = [&]
        {
            auto const dot = identity.canonicalPath.find_last_of(L'.');
            if (dot == std::wstring::npos) return std::wstring{};
            std::wstring ext = identity.canonicalPath.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
            return ext;
        }();

        if (extension == L".cmd" || extension == L".bat")
        {
            wchar_t systemDirectory[MAX_PATH]{};
            UINT const length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
            if (length == 0 || length >= std::size(systemDirectory))
            {
                result.readiness = Readiness::ProbeFailed;
                result.reason = L"The command processor could not be located, so the launcher script could not be run.";
                return result;
            }
            launch.applicationPath = std::wstring(systemDirectory, length) + L"\\cmd.exe";
            // `/d` skips AutoRun, `/s` preserves the quoting of the shim path, and
            // `/c` runs and exits. AutoRun is skipped deliberately: it is a
            // machine-level configuration that would otherwise run inside our
            // bounded probe.
            launch.arguments = L"/d /s /c \"\"" + identity.canonicalPath + L"\" " + spec.arguments + L"\"";
        }
        else
        {
            launch.applicationPath = identity.canonicalPath;
            launch.arguments = spec.arguments;
        }
        launch.workingDirectory = DirectoryOf(identity.canonicalPath);

        kernel::Limits limits;
        limits.wallClock = spec.wallClock;
        limits.maxCaptureBytes = spec.maxCaptureBytes;

        auto process = kernel::Launch(launch, limits);
        auto const outcome = process.wait();
        result.launchError = outcome.error;
        result.exitCode = outcome.exitCode;
        result.exited = outcome.exited;
        result.timedOut = outcome.timedOut;
        result.elapsed = outcome.elapsed;
        result.observedTreeProcesses = outcome.peakTreeProcesses;
        result.diagnostic = outcome.diagnostic;

        // First non-empty line, trimmed and bounded. This is shown in the
        // diagnostics sheet as the observation behind the state, never parsed
        // into a version comparison.
        std::wstring const output = Widen(outcome.standardOutput);
        std::size_t lineStart = 0;
        while (lineStart <= output.size())
        {
            auto const newline = output.find(L'\n', lineStart);
            std::wstring line = Trim(output.substr(lineStart,
                newline == std::wstring::npos ? std::wstring::npos : newline - lineStart));
            if (!line.empty())
            {
                if (line.size() > 200) line.resize(200);
                result.observedVersionLine = line;
                break;
            }
            if (newline == std::wstring::npos) break;
            lineStart = newline + 1;
        }

        if (outcome.error == kernel::Error::TimedOut)
        {
            result.readiness = Readiness::ProbeFailed;
            result.reason = L"The readiness probe did not finish within " +
                std::to_wstring(spec.wallClock.count()) +
                L" ms, so the process tree was terminated and no readiness is claimed.";
            return result;
        }

        if (outcome.error != kernel::Error::None)
        {
            // A launch that could not even start is a concrete probe failure, not
            // a missing installation: the file was found and identified above.
            result.readiness = Readiness::ProbeFailed;
            result.reason = L"The readiness probe could not run: " +
                Widen(kernel::ErrorText(outcome.error)) + L".";
            if (!outcome.diagnostic.empty()) result.reason += L" " + outcome.diagnostic;
            return result;
        }

        if (!outcome.exited)
        {
            result.readiness = Readiness::ProbeFailed;
            result.reason = L"The readiness probe ended without an observed exit, so no readiness is claimed.";
            return result;
        }

        if (outcome.exitCode != 0)
        {
            result.readiness = Readiness::ProbeFailed;
            std::wstring reason = L"The readiness probe exited with code " + std::to_wstring(outcome.exitCode) +
                L" instead of answering the version question.";
            std::wstring const errors = Trim(Widen(outcome.standardError));
            if (!errors.empty())
            {
                std::wstring first = errors.substr(0, std::min<std::size_t>(errors.size(), 300));
                reason += L" Reported: " + first;
            }
            result.reason = reason;
            return result;
        }

        if (result.observedVersionLine.empty())
        {
            // Exit zero with no output is not evidence that the installation
            // works: it is a launch that said nothing, and calling that READY
            // would be exactly the "never green without the corresponding
            // observed evidence" failure UI-UX §6.2 forbids.
            result.readiness = Readiness::ProbeFailed;
            result.reason = L"The readiness probe exited successfully but printed nothing, "
                L"so no version answer was observed.";
            return result;
        }

        result.readiness = Readiness::Ready;
        result.reason = L"The installed executable answered the version probe.";
        return result;
    }

    Detection Select(SelectionInput const& input)
    {
        Detection detection;
        detection.cli = input.cli;
        detection.candidates = input.candidates;
        detection.observedUtc = UtcNow();

        // A previously pinned executable is compared to what is on disk now. The
        // comparison is by stable identity AND digest, because either one alone
        // can miss a replacement: an in-place rewrite keeps the file id, and a
        // same-size different build keeps nothing but its name.
        if (input.pinnedIdentity)
        {
            ExecutableIdentity const& pin = *input.pinnedIdentity;
            auto const match = std::find_if(input.candidates.begin(), input.candidates.end(),
                [&](Candidate const& candidate)
                {
                    return !pin.stableIdentity.empty()
                        && candidate.identity.stableIdentity == pin.stableIdentity;
                });

            if (match == input.candidates.end())
            {
                // The pinned installation is gone. D-11: readiness becomes
                // CHECKING and a fresh probe is required; it must never silently
                // switch to a different installation, even when one was found.
                detection.readiness = Readiness::Checking;
                detection.reason = L"The previously selected executable is no longer present at " +
                    pin.canonicalPath + L". A fresh selection and probe are required.";
                return detection;
            }

            if (!pin.sha256.empty() && !match->identity.sha256.empty() &&
                pin.sha256 != match->identity.sha256)
            {
                detection.readiness = Readiness::Checking;
                detection.reason = L"The executable at " + pin.canonicalPath +
                    L" changed since it was last observed. A fresh probe is required before any readiness is claimed.";
                return detection;
            }

            detection.pinned = match->identity;
        }
        else
        {
            if (input.candidates.empty())
            {
                detection.readiness = Readiness::Missing;
                detection.reason = input.diagnostic.empty()
                    ? std::wstring(L"No installation was found.") : input.diagnostic;
                return detection;
            }

            // D-11: without a user selection, exactly one verified candidate may
            // be pinned automatically. Two or more require the user's choice, and
            // no ordering rule may break the tie.
            if (input.candidates.size() > 1)
            {
                bool const allSelected = std::all_of(input.candidates.begin(), input.candidates.end(),
                    [](Candidate const& candidate) { return candidate.userSelected; });
                if (!allSelected)
                {
                    detection.requiresSelection = true;
                    detection.readiness = Readiness::Checking;
                    detection.reason = L"More than one installation was found. Choose the executable "
                        L"Mayasaba should use; no candidate is preferred by discovery order, path or version.";
                    return detection;
                }
            }
            detection.pinned = input.candidates.front().identity;
        }

        if (!input.probeRan)
        {
            detection.readiness = Readiness::Checking;
            detection.reason = L"A readiness probe has not completed yet.";
            return detection;
        }

        detection.probe = input.probe;
        detection.readiness = input.probe.readiness;
        detection.reason = input.probe.reason;
        return detection;
    }

    Detection Detect(Cli cli, std::wstring const& userSelectedPath, ProbeSpec const& spec)
    {
        SelectionInput input;
        input.cli = cli;

        auto discovery = Discover(cli, userSelectedPath);
        input.candidates = std::move(discovery.candidates);
        input.diagnostic = std::move(discovery.diagnostic);

        // Selection first, without a probe, so the D-11 refusal is decided before
        // any process is launched. A tie that requires the user's choice must not
        // spend two launches to reach that conclusion.
        Detection preliminary = Select(input);
        if (preliminary.readiness == Readiness::Missing ||
            preliminary.requiresSelection ||
            !preliminary.pinned)
        {
            return preliminary;
        }

        input.pinnedIdentity = preliminary.pinned;
        input.probe = Probe(*preliminary.pinned, spec);
        input.probeRan = true;
        return Select(input);
    }
}
