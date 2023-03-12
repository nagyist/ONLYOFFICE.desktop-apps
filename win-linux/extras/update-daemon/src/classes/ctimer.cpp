#include "ctimer.h"
#include <chrono>
#include <signal.h>

std::atomic_bool CTimer::m_run{false};

CTimer::CTimer()
{
    signal(SIGTERM, &CTimer::handle_signal);
    signal(SIGABRT, &CTimer::handle_signal);
    signal(SIGBREAK, &CTimer::handle_signal);
    signal(SIGINT, &CTimer::handle_signal);
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

void CTimer::handle_signal(int signal)
{
    switch (signal) {
    case SIGTERM:
    case SIGABRT:
    case SIGBREAK:
    case SIGINT:
        m_run = false;
        break;
    }
}
