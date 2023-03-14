#ifndef CUNZIP_H
#define CUNZIP_H

#include <future>
#include <functional>
#include <string>

#define UNZIP_OK 0
#define UNZIP_ERROR 1
#define UNZIP_ABORT 2

typedef std::function<void(int)> FnVoidInt;

using std::wstring;


class CUnzip
{
public:
    CUnzip();
    ~CUnzip();

    void extractArchive(const wstring &zipFilePath, const wstring &folderPath);
    void stop();

    /* callback */
    void onComplete(FnVoidInt callback);

private:
    FnVoidInt m_complete_callback = nullptr;
    std::atomic_bool m_run;
    std::future<void> m_future;
};

#endif // CUNZIP_H
