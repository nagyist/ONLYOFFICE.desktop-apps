/*
 * (c) Copyright Ascensio System SIA 2010-2019
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation. In accordance with
 * Section 7(a) of the GNU AGPL its Section 15 shall be amended to the effect
 * that Ascensio System SIA expressly excludes the warranty of non-infringement
 * of any third-party rights.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR  PURPOSE. For
 * details, see the GNU AGPL at: http://www.gnu.org/licenses/agpl-3.0.html
 *
 * You can contact Ascensio System SIA at 20A-12 Ernesta Birznieka-Upisha
 * street, Riga, Latvia, EU, LV-1050.
 *
 * The  interactive user interfaces in modified source and object code versions
 * of the Program must display Appropriate Legal Notices, as required under
 * Section 5 of the GNU AGPL version 3.
 *
 * Pursuant to Section 7(b) of the License you must retain the original Product
 * logo when distributing the program. Pursuant to Section 7(e) we decline to
 * grant you any rights under trademark law for use of our trademarks.
 *
 * All the Product's GUI elements, including illustrations and icon sets, as
 * well as technical writing content are licensed under the terms of the
 * Creative Commons Attribution-ShareAlike 4.0 International. See the License
 * terms at http://creativecommons.org/licenses/by-sa/4.0/legalcode
 *
 */

#include "singleapplication.h"
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <QtConcurrent/QtConcurrent>

#ifdef _WIN32
# ifndef UNICODE
#  define UNICODE 1
# endif
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# pragma comment(lib, "Ws2_32.lib")
# include <windows.h>
# include <winsock2.h>
# include <sys/types.h>
# include <io.h>
# define close_socket(a) closesocket(a)
# define AF_TYPE AF_INET
# define INADDR "127.0.0.1"
  typedef struct sockaddr_in SockAddr;
#else
# include <unistd.h>
# include <sys/socket.h>
# include <sys/un.h>
# include <arpa/inet.h>
# define close_socket(a) close(a)
# define AF_TYPE AF_UNIX
# define PATHNAME "/tmp/socket_desktopeditors_0fdc58ba"
  typedef struct sockaddr_un SockAddr;
  typedef int SOCKET;
#endif

#define SEND_DELAY_MS 50
#define RETRIES_DELAY 50
#define RETRIES_COUNT 10
#define BUFFSIZE 1024


class SingleApplication::SingleApplicationPrv : public QObject
{
    Q_OBJECT
public:
    SingleApplicationPrv(SingleApplication *owner);
    ~SingleApplicationPrv();

    enum Type : uchar {
        DAEMON, CLIENT, UNDEF
    };
    uchar singleton_connect();
    static void handle_signal(int signal);
    void startPrimary();
    void readMessage();

    SingleApplication *owner;
    SOCKET socket_fd;
    static bool run;
    uchar instType;

private slots:
    void sendSignal(const QString&);
};

bool SingleApplication::SingleApplicationPrv::run = true;

SingleApplication::SingleApplicationPrv::SingleApplicationPrv(SingleApplication *owner) :
    QObject(nullptr),
    owner(owner),
    socket_fd(-1),
    instType(Type::UNDEF)
{}

SingleApplication::SingleApplicationPrv::~SingleApplicationPrv()
{}

uchar SingleApplication::SingleApplicationPrv::singleton_connect()
{
    SockAddr addr;
    memset(&addr, 0, sizeof(SockAddr));
    int len = 0;
#ifdef _WIN32
    WSADATA wsaData = {0};
    int iResult = 0;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed: %d\n", iResult);
        return Type::UNDEF;
    }
    SOCKET tmpd = INVALID_SOCKET;
    if ((tmpd = socket(AF_TYPE, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
#else
    SOCKET tmpd = -1;
    if ((tmpd = socket(AF_TYPE, SOCK_DGRAM, 0)) < 0) {
#endif
        printf("Could not create socket\n");
        fflush(stdout);
        return Type::UNDEF;
    }
    // fill socket address structure
#ifdef _WIN32
    addr.sin_family = AF_TYPE;
    addr.sin_addr.s_addr = inet_addr(INADDR);
    addr.sin_port = htons(12010);
    len = sizeof(addr);
#else
    addr.sun_family = AF_TYPE;
    strcpy(addr.sun_path, PATHNAME);
    len = offsetof(SockAddr, sun_path) + (int)strlen(PATHNAME);
#endif

    int ret;
    unsigned int retries = RETRIES_COUNT;
    do {
        // bind the name to the descriptor
        ret = bind(tmpd, (struct sockaddr*)&addr, len);
        if (ret == 0) {
            socket_fd = tmpd;
            return Type::DAEMON;

        } else {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEADDRINUSE) {
#else
            if (errno == EADDRINUSE) {
#endif
                ret = ::connect(tmpd, (struct sockaddr*)&addr, sizeof(SockAddr));
                if (ret != 0) {                    
#ifdef _WIN32
                    if (WSAGetLastError() == WSAECONNREFUSED) {
#else
                    if (errno == ECONNREFUSED) {
#endif
                        printf("Could not connect to socket - assuming daemon died\n");
                        fflush(stdout);
#ifdef _WIN32
                        //unlink(PATHNAME);
#else
                        unlink(PATHNAME);
#endif
                        QThread::currentThread()->msleep(RETRIES_DELAY);
                        continue;
                    }
                    //printf("Could not connect to socket\n");
                    //fflush(stdout);
                    QThread::currentThread()->msleep(RETRIES_DELAY);
                    continue;
                }
                //printf("Daemon is already running\n");
                //fflush(stdout);
                socket_fd = tmpd;
                return Type::CLIENT;
            }
            //printf("Could not bind to socket\n");
            //fflush(stdout);
            QThread::currentThread()->msleep(RETRIES_DELAY);
            continue;
        }
        QThread::currentThread()->msleep(RETRIES_DELAY);
    } while (retries-- > 0);

    //printf("Could neither connect to an existing daemon nor become one\n");
    //fflush(stdout);
    close_socket(tmpd);
    return Type::UNDEF;
}

void SingleApplication::SingleApplicationPrv::handle_signal(int signal)
{
    switch (signal) {
#ifdef _WIN32
    case SIGTERM:
    case SIGABRT:
    case SIGBREAK:
#else
    case SIGHUP:
#endif
        run = false;
    break;
    case SIGINT:
        run = false;
    break;
    }
}

void SingleApplication::SingleApplicationPrv::startPrimary()
{
#ifdef _WIN32
    signal(SIGINT, &SingleApplicationPrv::handle_signal);
    signal(SIGTERM, &SingleApplicationPrv::handle_signal);
    signal(SIGABRT, &SingleApplicationPrv::handle_signal);
    readMessage();
#else
    struct sigaction sa;
    sa.sa_handler = &SingleApplicationPrv::handle_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0 ||
            sigaction(SIGQUIT, &sa, NULL) != 0 ||
            sigaction(SIGTERM, &sa, NULL) != 0)
    {
        printf("Could not set up signal handlers!\n");
        fflush(stdout);
        //::exit(EXIT_FAILURE);
    } else {
        readMessage();
    }
#endif
}

void SingleApplication::SingleApplicationPrv::readMessage()
{
    while (run) {
        char rcvBuf[BUFFSIZE] = {0};
        int ret_data = recv(socket_fd, rcvBuf, BUFFSIZE, 0); // Receive the string data
        if (ret_data != BUFFSIZE) {
#ifdef _WIN32
            if (WSAGetLastError() != WSATRY_AGAIN && WSAGetLastError() != WSAEWOULDBLOCK) {
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
#endif
                //printf("Error while accessing socket\n");
                //fflush(stdout);
                // FAILURE
            }
            //printf("No further client_args in socket\n");
            //fflush(stdout);

        } else {
            QMetaObject::invokeMethod(this, "sendSignal", Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromLocal8Bit(rcvBuf, strlen(rcvBuf))));
            QCoreApplication::processEvents();
            printf("Received client arg: %s\n", rcvBuf);
            fflush(stdout);
            // SUCCESS
        }
    }
    //printf("Dropped out of daemon loop\n");
    //fflush(stdout);
}

void SingleApplication::SingleApplicationPrv::sendSignal(const QString& msg)
{
    emit owner->receivedMessage(msg);
}


SingleApplication::SingleApplication(int &argc, char *argv[]) :
    QApplication(argc, argv),
    pimpl(new SingleApplicationPrv(this))
{
    pimpl->instType = pimpl->singleton_connect();
    switch (pimpl->instType) {
    case SingleApplicationPrv::Type::DAEMON:
        future = QtConcurrent::run([=]() {
            pimpl->startPrimary();
        });
        break;
    case SingleApplicationPrv::Type::CLIENT:
        break;
    default:
        //printf("Identification error!\n");
        break;
    }
}

SingleApplication::~SingleApplication()
{
    pimpl->run = false;
    if (pimpl->socket_fd >= 0) {
#ifdef _WIN32
        shutdown(pimpl->socket_fd, SD_BOTH);
        close_socket(pimpl->socket_fd);
        WSACleanup();
#else
        shutdown(pimpl->socket_fd, SHUT_RDWR);
        if (pimpl->instType == SingleApplicationPrv::Type::DAEMON) {
            if (unlink(PATHNAME) < 0)
                printf("Could not remove FIFO.\n");
        } else
            close_socket(pimpl->socket_fd);
#endif
    }

    if (future.isRunning())
        future.waitForFinished();

    delete pimpl;
}

bool SingleApplication::sendMessage(const QString &msg)
{
    if (msg.isEmpty() || pimpl->instType != SingleApplicationPrv::Type::CLIENT)
        return false;
    if (msg.length() > BUFFSIZE - 1)
        return false;
    QThread::msleep(SEND_DELAY_MS);
    char client_arg[BUFFSIZE] = {0};
    memcpy(client_arg, msg.toLocal8Bit().data(), msg.length());
#ifdef _WIN32
    int ret_data = send(pimpl->socket_fd, client_arg, BUFFSIZE, 0); // Send the string
#else
    int ret_data =(int)send(pimpl->socket_fd, client_arg, BUFFSIZE, MSG_CONFIRM); // Send the string
#endif
    if (ret_data != BUFFSIZE) {
        if (ret_data < 0)
            printf("Could not send device address to daemon!\n");
        else
            printf("Could not send device address to daemon completely!\n");
        return false;
    }
    printf("Sended data to daemon: %s\n", msg.toLocal8Bit().data());
    return true;
}

bool SingleApplication::isPrimary()
{
    return pimpl->instType == SingleApplicationPrv::Type::DAEMON;
}

#include "singleapplication.moc"
