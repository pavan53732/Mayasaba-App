#include "ProjectRoot.h"
#include <windows.h>
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
    ProjectRoot ValidateProjectRoot(std::wstring const& selected)
    {
        if (selected.empty()) throw std::runtime_error("Folder selection was empty");
        if (selected.rfind(L"\\\\",0)==0) throw std::runtime_error("Remote/UNC folders are not supported");
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
        std::wostringstream id;
        id<<std::hex<<identity.VolumeSerialNumber<<L":";
        for(unsigned char byte: identity.FileId.Identifier)
            id<<std::setw(2)<<std::setfill(L'0')<<static_cast<unsigned>(byte);
        std::wstring name=std::filesystem::path(canonical).filename().wstring();
        if(name.empty())name=canonical;
        return {canonical,name,id.str()};
    }
}
