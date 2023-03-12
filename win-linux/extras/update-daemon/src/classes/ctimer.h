#ifndef CTIMER_H
#define CTIMER_H

#include <future>
#include <functional>

typedef std::function<void(void)> FnVoidVoid;


class CTimer
{
public:
    CTimer();
    ~CTimer();

    void stop();

    /* callback */
    void start(unsigned int timeout, FnVoidVoid callback);

private:
    std::atomic_bool m_run;
    std::future<void> m_future;
};

#endif // CTIMER_H
