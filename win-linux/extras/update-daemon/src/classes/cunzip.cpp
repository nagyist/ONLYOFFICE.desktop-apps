#include "cunzip.h"
#include "utils.h"
#include <shlobj_core.h>
#include <atlbase.h>
#include <comutil.h>


bool StringToFolder(CComPtr<IShellDispatch> &pISD, CComPtr<Folder> &folder, const wstring &path)
{
    CComVariant vPath(CComBSTR(path.c_str()));
    vPath.ChangeType(VT_BSTR);
    HRESULT hr = pISD->NameSpace(vPath, &folder);
    return FAILED(hr) ? false : true;
}

int extractRecursively(CComPtr<IShellDispatch> &pISD, const CComPtr<Folder> &pSrcFolder,
                         const wstring &destFolder, CComVariant &vOptions, std::atomic_bool &run)
{
    CComPtr<FolderItems> pItems;
    HRESULT hr = pSrcFolder->Items(&pItems);
    if (FAILED(hr))
        return UNZIP_ERROR;

    long itemCount = 0;
    hr = pItems->get_Count(&itemCount);
    if (FAILED(hr))
        return UNZIP_ERROR;

    for (int i = 0; i < itemCount; i++) {
        if (!run)
            return UNZIP_ABORT;

        CComPtr<FolderItem> pItem;
        hr = pItems->Item(CComVariant(i), &pItem);
        if (FAILED(hr))
            return UNZIP_ERROR;

        CComBSTR srcPath;
        hr = pItem->get_Path(&srcPath);
        if (FAILED(hr))
            return UNZIP_ERROR;

        CComVariant vSrcPath(srcPath);
        vSrcPath.ChangeType(VT_BSTR);

        VARIANT_BOOL isFolder = VARIANT_FALSE;
        hr = pItem->get_IsFolder(&isFolder);
        if (FAILED(hr))
            return UNZIP_ERROR;

        if (isFolder) {
            // Source path
            CComPtr<Folder> pSubFolder;
            hr = pISD->NameSpace(vSrcPath, &pSubFolder);
            if (FAILED(hr))
                return UNZIP_ERROR;

            // Dest path
            CComBSTR bstrName;
            hr = pItem->get_Name(&bstrName);
            if (FAILED(hr))
                return UNZIP_ERROR;

            wstring targetFolder(destFolder);
            targetFolder += L"\\";
            targetFolder += bstrName;
            if (!CreateDirectory(targetFolder.c_str(), NULL))
                return UNZIP_ERROR;

            int res = extractRecursively(pISD, pSubFolder, targetFolder, vOptions, run);
            if (res != UNZIP_OK)
                return res;

        } else {
            CComPtr<Folder> pDestFolder;
            if (!StringToFolder(pISD, pDestFolder, destFolder))
                return UNZIP_ERROR;
            hr = pDestFolder->CopyHere(vSrcPath, vOptions);
            if (FAILED(hr))
                return UNZIP_ERROR;
        }
    }
    return UNZIP_OK;
}

int unzipArchive(const wstring &zipFilePath, const wstring &folderPath, std::atomic_bool &run)
{
    if (!NS_File::fileExists(zipFilePath) || !NS_File::dirExists(folderPath))
        return UNZIP_ERROR;

    wstring file = NS_File::toNativeSeparators(zipFilePath);
    wstring path = NS_File::toNativeSeparators(folderPath);

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return UNZIP_ERROR;

    CComPtr<IShellDispatch> pShell;
    hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pShell));
    if (FAILED(hr)) {
        CoUninitialize();
        return UNZIP_ERROR;
    }

    CComPtr<Folder> pSrcFolder;
    if (!StringToFolder(pShell, pSrcFolder, file)) {
        CoUninitialize();
        return UNZIP_ERROR;
    }

    CComVariant vOptions(0);
    vOptions.vt = VT_I4;
    vOptions.lVal = 1024 | 512 | 16 | 4;
    int res = extractRecursively(pShell, pSrcFolder, path, vOptions, run);
    CoUninitialize();
    return res;
}

CUnzip::CUnzip()
{
    m_run = false;
}

CUnzip::~CUnzip()
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
}

void CUnzip::extractArchive(const wstring &zipFilePath, const wstring &folderPath)
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
    m_run = true;
    m_future = std::async(std::launch::async, [=]() {
        int res = unzipArchive(zipFilePath, folderPath, m_run);
        if (m_complete_callback)
            m_complete_callback(res);
    });
}

void CUnzip::stop()
{
    m_run = false;
}

void CUnzip::onComplete(FnVoidInt callback)
{
    m_complete_callback = callback;
}
