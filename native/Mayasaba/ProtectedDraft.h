#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <stdexcept>
#include <memory>
#pragma comment(lib, "crypt32.lib")

namespace mayasaba
{
    inline std::string ProtectDraft(std::string const& text)
    {
        if(text.empty())return {};
        if(text.size()>262144)throw std::runtime_error("Protected draft exceeds its byte bound");
        DATA_BLOB in{static_cast<DWORD>(text.size()),reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))},out{};
        if(!CryptProtectData(&in,L"Mayasaba draft",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))
            throw std::runtime_error("Windows user protection failed; draft was not saved");
        std::unique_ptr<void,decltype(&LocalFree)> memory(out.pbData,LocalFree);
        constexpr char hex[]="0123456789abcdef";
        std::string result="dpapi1:"; result.reserve(7+out.cbData*2);
        for(DWORD i=0;i<out.cbData;++i){result+=hex[out.pbData[i]>>4];result+=hex[out.pbData[i]&15];}
        return result;
    }
    inline std::string ReadDraft(std::string const& stored)
    {
        if(stored.empty())return {};
        if(stored.rfind("dpapi1:",0)!=0)return stored; // read-only legacy migration input
        if((stored.size()-7)%2)throw std::runtime_error("Protected draft encoding is invalid");
        auto nibble=[](char c)->unsigned {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("Protected draft encoding is invalid");};
        std::string bytes;bytes.reserve((stored.size()-7)/2);
        for(size_t i=7;i<stored.size();i+=2)bytes+=static_cast<char>((nibble(stored[i])<<4)|nibble(stored[i+1]));
        DATA_BLOB in{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())},out{};
        if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))
            throw std::runtime_error("Draft cannot be read by this Windows user; original bytes preserved");
        std::unique_ptr<void,decltype(&LocalFree)> memory(out.pbData,LocalFree);
        return {reinterpret_cast<char*>(out.pbData),out.cbData};
    }
}
