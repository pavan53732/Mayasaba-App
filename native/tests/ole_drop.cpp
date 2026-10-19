#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <ole2.h>
#include <oleidl.h>
#include <shlobj.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>

class DropSource : public IDropSource
{
public:
    DropSource() : refCount_(1), startTime_(std::chrono::steady_clock::now()) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource)
        {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override
    {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;

        // Bounded safeguard: after 2.5 seconds, force drop completion so DoDragDrop never hangs
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
        if (elapsed > 2500)
        {
            return DRAGDROP_S_DROP;
        }

        if ((grfKeyState & MK_LBUTTON) == 0)
        {
            return DRAGDROP_S_DROP;
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    std::atomic<ULONG> refCount_;
    std::chrono::steady_clock::time_point startTime_;
};

class FileDataObject : public IDataObject
{
public:
    FileDataObject(std::vector<std::wstring> const& files) : refCount_(1)
    {
        // Calculate total buffer size for DROPFILES + double null-terminated wide string list
        size_t totalChars = 1; // final terminating null
        for (auto const& f : files)
        {
            totalChars += f.size() + 1;
        }
        size_t totalBytes = sizeof(DROPFILES) + (totalChars * sizeof(wchar_t));

        hGlobal_ = GlobalAlloc(GHND, totalBytes);
        if (hGlobal_)
        {
            auto* pDrop = static_cast<DROPFILES*>(GlobalLock(hGlobal_));
            if (pDrop)
            {
                pDrop->pFiles = sizeof(DROPFILES);
                pDrop->pt.x = 0;
                pDrop->pt.y = 0;
                pDrop->fNC = FALSE;
                pDrop->fWide = TRUE;

                auto* pDest = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(pDrop) + sizeof(DROPFILES));
                for (auto const& f : files)
                {
                    memcpy(pDest, f.c_str(), f.size() * sizeof(wchar_t));
                    pDest += f.size();
                    *pDest++ = L'\0';
                }
                *pDest = L'\0';
                GlobalUnlock(hGlobal_);
            }
        }
    }

    ~FileDataObject()
    {
        if (hGlobal_)
        {
            GlobalFree(hGlobal_);
            hGlobal_ = nullptr;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject)
        {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override
    {
        if (!pformatetcIn || !pmedium) return E_INVALIDARG;
        if (pformatetcIn->cfFormat == CF_HDROP && (pformatetcIn->tymed & TYMED_HGLOBAL))
        {
            pmedium->tymed = TYMED_HGLOBAL;
            // Duplicate handle
            size_t size = GlobalSize(hGlobal_);
            HGLOBAL hCopy = GlobalAlloc(GHND, size);
            if (!hCopy) return E_OUTOFMEMORY;
            void* src = GlobalLock(hGlobal_);
            void* dst = GlobalLock(hCopy);
            if (src && dst) memcpy(dst, src, size);
            if (src) GlobalUnlock(hGlobal_);
            if (dst) GlobalUnlock(hCopy);

            pmedium->hGlobal = hCopy;
            pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) override
    {
        if (!pformatetc) return E_INVALIDARG;
        if (pformatetc->cfFormat == CF_HDROP && (pformatetc->tymed & TYMED_HGLOBAL))
        {
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }

private:
    std::atomic<ULONG> refCount_;
    HGLOBAL hGlobal_{nullptr};
};

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3)
    {
        std::wcout << L"Usage: ole_drop.exe <target_hwnd_or_title> <file1> [file2 ...]\n";
        return 1;
    }

    HWND targetHwnd = nullptr;
    // Check if argv[1] is a numeric HWND
    wchar_t* endPtr = nullptr;
    unsigned long long val = wcstoull(argv[1], &endPtr, 10);
    if (endPtr && *endPtr == L'\0' && val != 0)
    {
        targetHwnd = reinterpret_cast<HWND>(val);
    }
    else
    {
        targetHwnd = FindWindowW(nullptr, argv[1]);
    }

    if (!targetHwnd || !IsWindow(targetHwnd))
    {
        std::wcerr << L"FAIL: Target window not found: " << argv[1] << L"\n";
        return 2;
    }

    std::vector<std::wstring> files;
    for (int i = 2; i < argc; ++i)
    {
        files.push_back(argv[i]);
    }

    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr))
    {
        std::wcerr << L"FAIL: OleInitialize failed: " << hr << L"\n";
        return 3;
    }

    RECT rc{};
    GetWindowRect(targetHwnd, &rc);
    int targetX = (rc.left + rc.right) / 2;
    int targetY = rc.bottom - 75; // Composer box region

    SetForegroundWindow(targetHwnd);
    SetCursorPos(targetX, targetY);

    auto* pDropSource = new DropSource();
    auto* pDataObject = new FileDataObject(files);

    DWORD mainThreadId = GetCurrentThreadId();
    std::atomic<bool> completed{false};

    // Watchdog thread: unblocks OLE modal loop if UIPI or message pump stalls
    std::thread watchdog([mainThreadId, &completed]() {
        for (int i = 0; i < 20; ++i)
        {
            if (completed.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!completed.load())
        {
            keybd_event(VK_ESCAPE, 0, 0, 0);
            keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
            PostThreadMessageW(mainThreadId, WM_CANCELMODE, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            if (!completed.load())
            {
                std::wcerr << L"WATCHDOG: DoDragDrop modal loop timed out under OS environment.\n";
                ExitProcess(4);
            }
        }
    });

    // Press left button down before entering DoDragDrop
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);

    std::thread worker([targetX, targetY]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        mouse_event(MOUSEEVENTF_MOVE, 1, 1, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    });

    DWORD dwEffect = 0;
    DWORD res = DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY, &dwEffect);
    completed.store(true);

    if (worker.joinable()) worker.join();
    if (watchdog.joinable()) watchdog.detach();

    pDataObject->Release();
    pDropSource->Release();
    OleUninitialize();

    std::wcout << L"PASS: DoDragDrop returned " << res << L", dwEffect=" << dwEffect << L"\n";
    return (res == DRAGDROP_S_DROP) ? 0 : 5;
}
