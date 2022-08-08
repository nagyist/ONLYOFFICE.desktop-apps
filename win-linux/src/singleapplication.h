#ifndef SINGLEAPPLICATION_H
#define SINGLEAPPLICATION_H

#include <QApplication>
#include <QFuture>


class SingleApplication : public QApplication
{
    Q_OBJECT
public:
    explicit SingleApplication(int &argc, char *argv[]);
    ~SingleApplication();

    bool sendMessage(const QString &message);
    bool isPrimary();

signals:
    void receivedMessage(QString message);

private:
    enum Type : uchar {
        DAEMON, CLIENT, UNDEF
    };
    QFuture<void> future;
    class SingleApplicationPrv;
    SingleApplicationPrv *pimpl;
};

#endif // SINGLEAPPLICATION_H
