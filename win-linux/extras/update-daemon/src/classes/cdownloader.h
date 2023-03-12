#ifndef CDOWNLOADER_H
#define CDOWNLOADER_H

#include <string>
#include <functional>
#include <future>

typedef std::function<void(int)> FnVoidInt;

using std::wstring;

class CDownloader
{
public:
    CDownloader();
    ~CDownloader();

    void downloadFile(const wstring &url, const wstring &filePath);
    void start();
    void pause();
    void stop();
    wstring GetFilePath();

    /* callback */
    void onComplete(FnVoidInt callback);
    void onProgress(FnVoidInt callback);

private:
    FnVoidInt m_complete_callback = nullptr,
              m_progress_callback = nullptr;
    wstring   m_url,
              m_filePath;
    std::future<void> m_future;
    std::atomic_bool m_run,
                     m_was_stopped,
                     m_lock;
    class DownloadProgress;
    friend DownloadProgress;
};

#endif // CDOWNLOADER_H
