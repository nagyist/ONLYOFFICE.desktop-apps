#include "ctimer.h"
#include <chrono>


CTimer::CTimer()
{
    m_run = false;
}

CTimer::~CTimer()
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
}

void CTimer::stop()
{
    m_run = false;
}

void CTimer::start(unsigned int timeout, FnVoidVoid callback)
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
    m_run = true;
    m_future = std::async(std::launch::async, [=]() {
        while (m_run) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
            if (callback)
                callback();
        }
    });
}
