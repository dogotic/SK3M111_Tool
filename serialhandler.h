#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <QStringList>

class SerialHandler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY availablePortsChanged)
    Q_PROPERTY(QString receivedData READ receivedData NOTIFY receivedDataChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY receivedDataChanged)
    Q_PROPERTY(int rangeGate READ rangeGate NOTIFY receivedDataChanged)    // gate# (normal mode), -1 otherwise
    Q_PROPERTY(double distanceM READ distanceM NOTIFY receivedDataChanged) // metres, -1 if no target
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit SerialHandler(QObject *parent = nullptr);
    ~SerialHandler();

    QStringList availablePorts() const { return m_availablePorts; }
    QString     receivedData()   const { return m_receivedData; }
    bool        isActive()       const { return m_isActive; }
    int         rangeGate()      const { return m_rangeGate; }
    double      distanceM()      const { return m_distanceM; }
    bool        connected()      const { return m_connected; }
    QString     statusMessage()  const { return m_statusMessage; }

    Q_INVOKABLE void connectToPort(const QString &portName);
    Q_INVOKABLE void disconnectFromPort();
    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void sendConfig(int maxRangeGate, int absenceReportDelay);
    Q_INVOKABLE void readFirmwareVersion();
    Q_INVOKABLE void setMode(int mode); // 0=Normal, 1=Report, 2=Debug

signals:
    void availablePortsChanged();
    void receivedDataChanged();
    void connectedChanged();
    void statusMessageChanged();

private slots:
    void onDataReady();

private:
    void setStatus(const QString &msg);
    void closePort();
    void sendFrame(const QByteArray &payload); // wraps FD FC FB FA ... 04 03 02 01
    void processBuffer();
    void parseAsciiLine(const QByteArray &line);
    void parseCommandResponse(const QByteArray &frame);
    void parseReportFrame(const QByteArray &frame);

    int              m_fd        = -1;
    QSocketNotifier *m_notifier  = nullptr;
    QStringList      m_availablePorts;
    QString          m_receivedData;
    QString          m_statusMessage;
    QByteArray       m_buffer;
    bool             m_isActive  = false;
    bool             m_connected = false;
    int              m_rangeGate = -1;
    double           m_distanceM = -1.0;
};
