#ifndef CAPPLICATION_H
#define CAPPLICATION_H

#include <Windows.h>


class CApplication
{
public:
    CApplication();
    ~CApplication();

    int exec();
    void exit(int);

    /* callback */
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    DWORD  mainThreadId = 0;
};

#endif // CAPPLICATION_H
