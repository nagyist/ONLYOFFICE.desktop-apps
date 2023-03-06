#include "csocket.h"
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <future>

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#ifndef UNICODE
# define UNICODE 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winsock2.h>
#include <sys/types.h>
#include <io.h>
#define AF_TYPE AF_INET
#define INADDR "127.0.0.1"
#define RETRIES_DELAY_MS 4000
#define BUFFSIZE 1024

typedef struct sockaddr_in SockAddr;


class CSocket::CSocketPrv
{
public:
    CSocketPrv();
    ~CSocketPrv();

    static void handle_signal(int signal);
    bool createSocket(u_short port);
    bool connectToSocket(u_short port);
    void startReadMessages();
    void closeSocket(SOCKET &socket);
    void postError(const char*);

    SOCKET sender_fd = -1,
           receiver_fd = -1;

    FnVoidData m_received_callback = nullptr;
    FnVoidCharPtr m_error_callback = nullptr;
    std::future<void> m_future;
    int m_sender_port;
    static bool run;
};

bool CSocket::CSocketPrv::run = true;

CSocket::CSocketPrv::CSocketPrv()
{}

CSocket::CSocketPrv::~CSocketPrv()
{}

bool CSocket::CSocketPrv::createSocket(u_short port)
{
    WSADATA wsaData = {0};
    int iResult = 0;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        postError("Create socket: WSAStartup failed!");
        return false;
    }
    SOCKET tmpd = INVALID_SOCKET;
    if ((tmpd = socket(AF_TYPE, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        postError("Create socket: socket not valid!");
        return false;
    }

    int len = 0;
    SockAddr addr;
    memset(&addr, 0, sizeof(SockAddr));
    addr.sin_family = AF_TYPE;
    addr.sin_addr.s_addr = inet_addr(INADDR);
    addr.sin_port = htons(port);
    len = sizeof(addr);

    // bind the name to the descriptor
    int ret = ::bind(tmpd, (struct sockaddr*)&addr, len);
    if (ret == 0) {
        receiver_fd = tmpd;
        return true;
    }
    closesocket(tmpd);
    postError("Could not create socket!");
    return false;
}

bool CSocket::CSocketPrv::connectToSocket(u_short port)
{
    WSADATA wsaData = {0};
    int iResult = 0;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        postError("Connect to socket: WSAStartup failed!");
        return false;
    }
    SOCKET tmpd = INVALID_SOCKET;
    if ((tmpd = socket(AF_TYPE, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        postError("Connect to socket: socket not valid!");
        return false;
    }

    int len = 0;
    SockAddr addr;
    memset(&addr, 0, sizeof(SockAddr));
    addr.sin_family = AF_TYPE;
    addr.sin_addr.s_addr = inet_addr(INADDR);
    addr.sin_port = htons(port);
    len = sizeof(addr);

    // bind the name to the descriptor
    int ret = ::bind(tmpd, (struct sockaddr*)&addr, len);
    if (ret != 0) {
        if (WSAGetLastError() == WSAEADDRINUSE) {
            ret = ::connect(tmpd, (struct sockaddr*)&addr, sizeof(SockAddr));
            if (ret == 0) {
                sender_fd = tmpd;
                return true;
            }
        }
    }
    closesocket(tmpd);
    postError("Could not connect to socket!");
    return false;
}

void CSocket::CSocketPrv::handle_signal(int signal)
{
    switch (signal) {
    case SIGTERM:
    case SIGABRT:
    case SIGBREAK:
    case SIGINT:
        run = false;
        break;
    }
}

void CSocket::CSocketPrv::startReadMessages()
{
    signal(SIGTERM, &CSocketPrv::handle_signal);
    signal(SIGABRT, &CSocketPrv::handle_signal);
    signal(SIGBREAK, &CSocketPrv::handle_signal);
    signal(SIGINT, &CSocketPrv::handle_signal);

    while (run) {
        char rcvBuf[BUFFSIZE] = {0};
        int ret_data = recv(receiver_fd, rcvBuf, BUFFSIZE, 0); // Receive the string data
        if (ret_data != BUFFSIZE) {
            if (ret_data < 0) {
                // FAILURE
                postError("Start read messages: error while accessing socket!");
            } else {
                // Connection closed.
            }
        } else {
            // SUCCESS
            if (m_received_callback)
                m_received_callback(rcvBuf, strlen(rcvBuf));
        }
    }
    // Dropped out of daemon loop.
}

void CSocket::CSocketPrv::closeSocket(SOCKET &socket)
{
    if (socket >= 0) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        socket = -1;
    }
}

void CSocket::CSocketPrv::postError(const char *error)
{
    if (m_error_callback)
        m_error_callback(error);
}

CSocket::CSocket(int sender_port, int receiver_port) :
    pimpl(new CSocketPrv)
{
    pimpl->m_sender_port = sender_port;
    pimpl->m_future = std::async(std::launch::async, [=]() {
        bool socket_created = false;
        while (pimpl->run && !(socket_created = pimpl->createSocket(receiver_port))) {
            pimpl->postError("Unable to create socket, retrying after 4 seconds.");
            Sleep(RETRIES_DELAY_MS);
        }
        if (socket_created)
            pimpl->startReadMessages();
    });
}

CSocket::~CSocket()
{
    pimpl->run = false;
    pimpl->closeSocket(pimpl->sender_fd);
    pimpl->closeSocket(pimpl->receiver_fd);
    if (pimpl->m_future.valid())
        pimpl->m_future.wait();
    WSACleanup();
    delete pimpl;
}

bool CSocket::sendMessage(void *data, size_t size)
{
    if (!data || size > BUFFSIZE - 1)
        return false;

    if (!pimpl->connectToSocket(pimpl->m_sender_port))
        return false;

    char client_arg[BUFFSIZE] = {0};
    memcpy(client_arg, data, size);
    int ret_data = send(pimpl->sender_fd, client_arg, BUFFSIZE, 0); // Send the string
    if (ret_data != BUFFSIZE) {
        if (ret_data < 0) {
            pimpl->postError("Send message error: could not send device address to daemon!");
        } else {
            pimpl->postError("Send message error: could not send device address to daemon completely!");
        }
        pimpl->closeSocket(pimpl->sender_fd);
        return false;
    }
    pimpl->closeSocket(pimpl->sender_fd);
    return true;
}

void CSocket::onMessageReceived(FnVoidData callback)
{
    pimpl->m_received_callback = callback;
}

void CSocket::onError(FnVoidCharPtr callback)
{
    pimpl->m_error_callback = callback;
}
