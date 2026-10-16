#include "ProjectRoot.h"
#include <windows.h>
#include <winternl.h>
#include <vector>
#include <memory>
#include <filesystem>
#include <iomanip>
#include <sstream>
namespace mayasaba
{
    namespace
    {
        struct UniqueHandle
        {
            HANDLE value{INVALID_HANDLE_VALUE};
            ~UniqueHandle() { if(value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
        };
    }
    namespace
    {
        struct PathLocks
        {
            std::vector<std::unique_ptr<UniqueHandle>> handles;
            explicit PathLocks(std::wstring const& selected)
            {
                std::filesystem::path path(selected);
                if (!path.is_absolute() || selected.rfind(L"\\\\", 0) == 0)
                    throw std::runtime_error("Choose an absolute local fixed-volume directory");
                auto current = path.root_path();
                for (auto const& part : path.relative_path())
                {
                    if (part == L"." || part == L"..") throw std::runtime_error("Relative path components are not admitted");
                    current /= part;
                    auto h = std::make_unique<UniqueHandle>();
                    h->value = CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                    BY_HANDLE_FILE_INFORMATION info{};
                    if (h->value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h->value, &info) ||
                        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                        throw std::runtime_error("Directory path is unavailable or traverses a reparse point");
                    handles.push_back(std::move(h));
                }
            }
        };
    }
    ProjectRoot ValidateProjectRoot(std::wstring const& selected)
    {
        if (selected.empty()) throw std::runtime_error("Folder selection was empty");
        if (selected.rfind(L"\\\\",0)==0) throw std::runtime_error("Remote/UNC folders are not supported");
        auto locks = std::make_shared<PathLocks>(selected);
        DWORD attrs=GetFileAttributesW(selected.c_str());
        if (attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("Selected directory does not exist or is inaccessible");
        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("Selected root is a reparse point; choose a direct local folder");
        wchar_t volumePath[MAX_PATH]{};
        if(!GetVolumePathNameW(selected.c_str(),volumePath,MAX_PATH) ||
           GetDriveTypeW(volumePath)!=DRIVE_FIXED)
            throw std::runtime_error("The project root must be on a local fixed volume");
        UniqueHandle h{CreateFileW(selected.c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
        if(h.value==INVALID_HANDLE_VALUE)
            throw std::runtime_error("Could not open the selected directory to verify its identity");
        FILE_ID_INFO identity{};
        if(!GetFileInformationByHandleEx(h.value,FileIdInfo,&identity,sizeof(identity)))
            throw std::runtime_error("The directory has no verifiable stable Windows file identity");
        DWORD chars=GetFinalPathNameByHandleW(h.value,nullptr,0,FILE_NAME_NORMALIZED);
        if(!chars)throw std::runtime_error("Could not canonicalize the directory path");
        std::wstring canonical(chars+1,L'\0');
        DWORD actual=GetFinalPathNameByHandleW(h.value,canonical.data(),static_cast<DWORD>(canonical.size()),FILE_NAME_NORMALIZED);
        if(!actual||actual>=canonical.size())throw std::runtime_error("Canonical path observation failed");
        canonical.resize(actual);
        if(canonical.rfind(L"\\\\?\\",0)==0) canonical.erase(0,4);

        // Ancestor reparse traversal check: ensure no ancestor is a junction/symlink/reparse point
        std::filesystem::path canonPath(canonical);
        for (auto curr = canonPath.parent_path(); curr != curr.root_path() && !curr.empty(); curr = curr.parent_path())
        {
            DWORD ancestorAttrs = GetFileAttributesW(curr.c_str());
            if (ancestorAttrs == INVALID_FILE_ATTRIBUTES)
            {
                throw std::runtime_error("Ancestor directory is inaccessible");
            }
            if (ancestorAttrs & FILE_ATTRIBUTE_REPARSE_POINT)
            {
                throw std::runtime_error("Ancestor directory contains a reparse point or junction: unsafe project path");
            }
        }

        std::wostringstream id;
        id<<std::hex<<identity.VolumeSerialNumber<<L":";
        for(unsigned char byte: identity.FileId.Identifier)
            id<<std::setw(2)<<std::setfill(L'0')<<static_cast<unsigned>(byte);
        std::wstring name=std::filesystem::path(canonical).filename().wstring();
        if(name.empty())name=canonical;
        return {canonical,name,id.str(),locks};
    }

    std::string ValidateSingleChildFolderName(std::wstring const& newFolderName)
    {
        if (newFolderName.size() > 255) return "Folder name exceeds 255 characters";
        if (newFolderName.empty())
            return "Folder name cannot be empty";
        if (newFolderName == L"." || newFolderName == L"..")
            return "Folder name cannot be '.' or '..'";
        if (newFolderName.find(L'/') != std::wstring::npos ||
            newFolderName.find(L'\\') != std::wstring::npos ||
            newFolderName.find(L':') != std::wstring::npos)
        {
            return "Folder name must be a single child name without path separators or colons";
        }
        for (wchar_t c : newFolderName)
        {
            if (c < 32 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
                c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*')
            {
                return "Folder name contains invalid characters (< > : \" / \\ | ? * or control characters)";
            }
        }
        if (newFolderName.back() == L'.' || newFolderName.back() == L' ')
        {
            return "Folder name cannot end with a period or space";
        }
        std::wstring base = newFolderName;
        auto dotPos = base.find(L'.');
        if (dotPos != std::wstring::npos) base = base.substr(0, dotPos);
        std::wstring upperBase = base;
        for (auto& ch : upperBase) ch = static_cast<wchar_t>(towupper(ch));
        static const wchar_t* const reserved[] = {
            L"CON", L"PRN", L"AUX", L"NUL",
            L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
            L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
        };
        for (auto* res : reserved)
        {
            if (upperBase == res)
                return "Folder name is a reserved Windows device name";
        }
        return "";
    }

    ProjectRoot CreateProjectFolder(std::wstring const& parentPath, std::wstring const& newFolderName)
    {
        auto err = ValidateSingleChildFolderName(newFolderName);
        if (!err.empty())
        {
            throw std::runtime_error(err);
        }

        auto locks = std::make_shared<PathLocks>(parentPath);
        ValidateProjectRoot(parentPath); // fixed-volume and original ancestor checks before any effect
        // Open parent directory with FILE_SHARE_READ | FILE_SHARE_WRITE (WITHOUT FILE_SHARE_DELETE)
        // to prevent parent directory deletion, renaming, or replacement races while creating child.
        UniqueHandle parentLock{CreateFileW(parentPath.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (parentLock.value == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Could not lock parent directory to verify identity and prevent replacement races");
        }

        BY_HANDLE_FILE_INFORMATION parentFileInfo{};
        if (!GetFileInformationByHandle(parentLock.value, &parentFileInfo) || !(parentFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            throw std::runtime_error("Parent path is not a valid directory");
        }
        if (parentFileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            throw std::runtime_error("Parent directory is a reparse point; child creation refused");
        }

        FILE_ID_INFO parentIdBefore{};
        if (!GetFileInformationByHandleEx(parentLock.value, FileIdInfo, &parentIdBefore, sizeof(parentIdBefore)))
        {
            throw std::runtime_error("Parent directory has no verifiable stable Windows file identity");
        }

        DWORD parentChars = GetFinalPathNameByHandleW(parentLock.value, nullptr, 0, FILE_NAME_NORMALIZED);
        if (!parentChars) throw std::runtime_error("Could not canonicalize parent directory path");
        std::wstring parentCanonical(parentChars + 1, L'\0');
        DWORD actualParent = GetFinalPathNameByHandleW(parentLock.value, parentCanonical.data(), static_cast<DWORD>(parentCanonical.size()), FILE_NAME_NORMALIZED);
        if (!actualParent || actualParent >= parentCanonical.size()) throw std::runtime_error("Parent canonical path observation failed");
        parentCanonical.resize(actualParent);
        if (parentCanonical.rfind(L"\\\\?\\", 0) == 0) parentCanonical.erase(0, 4);

        // Ancestor reparse traversal check on parent
        std::filesystem::path parentCanonPath(parentCanonical);
        for (auto curr = parentCanonPath.parent_path(); curr != curr.root_path() && !curr.empty(); curr = curr.parent_path())
        {
            DWORD ancestorAttrs = GetFileAttributesW(curr.c_str());
            if (ancestorAttrs == INVALID_FILE_ATTRIBUTES || (ancestorAttrs & FILE_ATTRIBUTE_REPARSE_POINT))
            {
                throw std::runtime_error("Parent ancestor contains a reparse point or junction: unsafe creation target");
            }
        }

        std::filesystem::path target = parentCanonPath / newFolderName;
        std::wstring targetStr = target.wstring();

        DWORD attrs = GetFileAttributesW(targetStr.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES)
        {
            throw std::runtime_error("A file or directory already exists at target path");
        }

        // Create and acquire the child in one native call relative to the held
        // parent. FILE_CREATE refuses collisions; no path reopen race exists.
        auto ntCreate = reinterpret_cast<decltype(&NtCreateFile)>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
        if (!ntCreate) throw std::runtime_error("Atomic directory creation API unavailable");
        UNICODE_STRING childName{};
        childName.Buffer = const_cast<PWSTR>(newFolderName.c_str());
        childName.Length = static_cast<USHORT>(newFolderName.size() * sizeof(wchar_t));
        childName.MaximumLength = childName.Length;
        OBJECT_ATTRIBUTES attributes{};
        attributes.Length = sizeof(attributes);
        attributes.RootDirectory = parentLock.value;
        attributes.ObjectName = &childName;
        attributes.Attributes = 0x40; // OBJ_CASE_INSENSITIVE
        IO_STATUS_BLOCK io{};
        UniqueHandle childLock;
        NTSTATUS status = ntCreate(&childLock.value, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            &attributes, &io, nullptr, FILE_ATTRIBUTE_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE, 2 /* FILE_CREATE */,
            0x1 | 0x20 /* FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT */, nullptr, 0);
        if (status < 0)
            throw std::runtime_error("Atomic directory creation refused (NTSTATUS " + std::to_string(status) + ")");

        BY_HANDLE_FILE_INFORMATION childFileInfo{};
        if (!GetFileInformationByHandle(childLock.value, &childFileInfo) || !(childFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            throw std::runtime_error("Created target is not a valid directory");
        }
        if (childFileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            throw std::runtime_error("Created child is a reparse point; unsafe project folder");
        }

        FILE_ID_INFO childId{};
        if (!GetFileInformationByHandleEx(childLock.value, FileIdInfo, &childId, sizeof(childId)))
        {
            throw std::runtime_error("Created child directory has no verifiable stable Windows file identity");
        }

        DWORD childChars = GetFinalPathNameByHandleW(childLock.value, nullptr, 0, FILE_NAME_NORMALIZED);
        if (!childChars) throw std::runtime_error("Could not canonicalize created child path");
        std::wstring childCanonical(childChars + 1, L'\0');
        DWORD actualChild = GetFinalPathNameByHandleW(childLock.value, childCanonical.data(), static_cast<DWORD>(childCanonical.size()), FILE_NAME_NORMALIZED);
        if (!actualChild || actualChild >= childCanonical.size()) throw std::runtime_error("Child canonical path observation failed");
        childCanonical.resize(actualChild);
        if (childCanonical.rfind(L"\\\\?\\", 0) == 0) childCanonical.erase(0, 4);

        // Verify containment strictly under verified parent
        std::filesystem::path childCanonPath(childCanonical);
        if (childCanonPath.parent_path() != parentCanonPath)
        {
            throw std::runtime_error("Created directory parent mismatch: parent replacement or escape detected");
        }

        // Re-verify parent identity on held parent handle to guarantee no replacement race occurred
        FILE_ID_INFO parentIdAfter{};
        if (!GetFileInformationByHandleEx(parentLock.value, FileIdInfo, &parentIdAfter, sizeof(parentIdAfter)) ||
            std::memcmp(&parentIdBefore, &parentIdAfter, sizeof(FILE_ID_INFO)) != 0)
        {
            throw std::runtime_error("Parent directory identity changed during creation: replacement race detected");
        }

        std::wostringstream id;
        id << std::hex << childId.VolumeSerialNumber << L":";
        for (unsigned char byte : childId.FileId.Identifier)
            id << std::setw(2) << std::setfill(L'0') << static_cast<unsigned>(byte);
        std::wstring name = childCanonPath.filename().wstring();
        if (name.empty()) name = childCanonical;
        // Preserve parent and child locks until Project service commits its receipt.
        struct HeldCreation { std::shared_ptr<PathLocks> ancestors; HANDLE child;
            ~HeldCreation() { CloseHandle(child); } };
        auto held = std::make_shared<HeldCreation>();
        held->ancestors = locks;
        held->child = childLock.value;
        childLock.value = INVALID_HANDLE_VALUE;
        return {childCanonical, name, id.str(), held};
    }

    ProjectRoot LocateProject(std::wstring const& expectedIdentity, std::wstring const& candidatePath)
    {
        auto root = ValidateProjectRoot(candidatePath);
        if (root.stableIdentity != expectedIdentity)
        {
            throw std::runtime_error("Selected folder identity does not match the original project. The chosen folder is a different root.");
        }
        return root;
    }
}
