#include "serialhandler.h"

#include <QDir>
#include <QFileInfo>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

static constexpr double GATE_DIST_M = 0.70; // 70 cm per gate (per wiki)

SerialHandler::SerialHandler(QObject *parent)
    : QObject(parent)
{
    refreshPorts();
    setStatus("Disconnected");
}

SerialHandler::~SerialHandler()
{
    closePort();
}

void SerialHandler::refreshPorts()
{
    m_availablePorts.clear();

    QDir dev("/dev");
    QStringList entries;
    for (const QString &pattern : {"ttyUSB*", "ttyACM*", "ttyAMA*", "ttyS*"})
        entries += dev.entryList({pattern}, QDir::System);
    entries.sort();

    for (const QString &name : entries) {
        if (name.startsWith("ttyS") &&
            !QFileInfo(QString("/sys/class/tty/%1/device").arg(name)).exists())
            continue;
        m_availablePorts.append(name);
    }

    emit availablePortsChanged();
}

void SerialHandler::connectToPort(const QString &portName)
{
    closePort();

    const QByteArray path = ("/dev/" + portName).toLocal8Bit();
    m_fd = ::open(path.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        setStatus(QString("Cannot open %1: %2").arg(portName, strerror(errno)));
        return;
    }

    struct termios tty {};
    if (tcgetattr(m_fd, &tty) != 0) {
        setStatus(QString("tcgetattr: %1").arg(strerror(errno)));
        closePort();
        return;
    }

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tty.c_cflag |=  CS8 | CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT |
                     PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~(OPOST | ONLCR);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        setStatus(QString("tcsetattr: %1").arg(strerror(errno)));
        closePort();
        return;
    }

    tcflush(m_fd, TCIOFLUSH);

    m_buffer.clear();
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &SerialHandler::onDataReady);

    m_connected = true;
    emit connectedChanged();
    setStatus(QString("Connected to %1 @ 115200 8N1").arg(portName));
}

void SerialHandler::disconnectFromPort()
{
    closePort();
    m_receivedData.clear();
    m_rangeGate = -1;
    m_distanceM = -1.0;
    emit receivedDataChanged();
    setStatus("Disconnected");
}

void SerialHandler::closePort()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
}

// Wraps payload in the standard binary frame: FD FC FB FA [len16 LE] [payload] 04 03 02 01
void SerialHandler::sendFrame(const QByteArray &payload)
{
    if (m_fd < 0) return;

    QByteArray frame;
    frame.reserve(4 + 2 + payload.size() + 4);
    frame.append("\xFD\xFC\xFB\xFA", 4);
    frame.append(char(payload.size() & 0xFF));
    frame.append(char((payload.size() >> 8) & 0xFF));
    frame.append(payload);
    frame.append("\x04\x03\x02\x01", 4);

    ::write(m_fd, frame.constData(), frame.size());
}

void SerialHandler::sendConfig(int maxRangeGate, int absenceReportDelay)
{
    if (m_fd < 0) { setStatus("Not connected"); return; }

    // Command 0x0007: Write Parameters
    // Params: 0x0001 = max range gate, 0x0004 = absence delay, 0x0010 = sensitivity (fixed)
    QByteArray payload;
    payload.append("\x07\x00", 2);                          // command 0x0007
    payload.append("\x01\x00", 2);                          // param ID: max range gate
    payload.append(char(maxRangeGate)); payload.append("\x00\x00\x00", 3); // uint32 LE
    payload.append("\x04\x00", 2);                          // param ID: absence delay
    payload.append(char(absenceReportDelay)); payload.append("\x00\x00\x00", 3); // uint32 LE
    payload.append("\x10\x00", 2);                          // param ID: sensitivity
    payload.append("\x4B\xEA\x00\x00", 4);                  // default sensitivity value

    sendFrame(payload);
    setStatus(QString("Config sent: gate %1 (%.2f m max), absence %2 s")
              .arg(maxRangeGate).arg(maxRangeGate * GATE_DIST_M).arg(absenceReportDelay));
}

void SerialHandler::readFirmwareVersion()
{
    if (m_fd < 0) { setStatus("Not connected"); return; }
    // Command 0x0000: Read Firmware Version
    sendFrame(QByteArray("\x00\x00", 2));
}

void SerialHandler::setMode(int mode)
{
    if (m_fd < 0) { setStatus("Not connected"); return; }

    // Command 0x0012: Set Mode
    // Normal = 0x64, Report = 0x04, Debug = 0x00
    uint8_t modeVal = 0;
    const char *modeName = "Unknown";
    switch (mode) {
    case 0: modeVal = 0x64; modeName = "Normal"; break;
    case 1: modeVal = 0x04; modeName = "Report"; break;
    case 2: modeVal = 0x00; modeName = "Debug";  break;
    default: return;
    }

    QByteArray payload;
    payload.append("\x12\x00", 2);           // command 0x0012
    payload.append("\x00\x00", 2);           // sub-param
    payload.append(char(modeVal));
    payload.append("\x00\x00\x00", 3);       // uint32 LE mode value

    sendFrame(payload);
    setStatus(QString("Mode → %1").arg(modeName));
}

void SerialHandler::onDataReady()
{
    char buf[256];
    ssize_t n;

    while ((n = ::read(m_fd, buf, sizeof(buf))) > 0)
        m_buffer.append(buf, static_cast<int>(n));

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        setStatus(QString("Read error: %1").arg(strerror(errno)));
        closePort();
        return;
    }

    processBuffer();
}

void SerialHandler::processBuffer()
{
    while (!m_buffer.isEmpty()) {

        // ── Report mode frame: F4 F3 F2 F1 ... F8 F7 F6 F5 ──────────────
        if (m_buffer.startsWith(QByteArray("\xF4\xF3\xF2\xF1", 4))) {
            if (m_buffer.size() < 6) break;
            const int dataLen  = (uint8_t)m_buffer[4] | ((uint8_t)m_buffer[5] << 8);
            const int frameLen = 4 + 2 + dataLen + 4;
            if (m_buffer.size() < frameLen) break;
            parseReportFrame(m_buffer.left(frameLen));
            m_buffer = m_buffer.mid(frameLen);
        }

        // ── Command response frame: FD FC FB FA ... 04 03 02 01 ──────────
        else if (m_buffer.startsWith(QByteArray("\xFD\xFC\xFB\xFA", 4))) {
            if (m_buffer.size() < 6) break;
            const int dataLen  = (uint8_t)m_buffer[4] | ((uint8_t)m_buffer[5] << 8);
            const int frameLen = 4 + 2 + dataLen + 4;
            if (m_buffer.size() < frameLen) break;
            parseCommandResponse(m_buffer.left(frameLen));
            m_buffer = m_buffer.mid(frameLen);
        }

        // ── Normal mode ASCII line ────────────────────────────────────────
        else if (m_buffer.contains('\n')) {
            const int idx = m_buffer.indexOf('\n');
            parseAsciiLine(m_buffer.left(idx).trimmed());
            m_buffer = m_buffer.mid(idx + 1);
        }

        else break; // wait for more data
    }

    // Safety: discard if buffer grows too large (de-sync)
    if (m_buffer.size() > 1024)
        m_buffer.clear();
}

void SerialHandler::parseAsciiLine(const QByteArray &raw)
{
    if (raw.isEmpty()) return;

    const QString data = QString::fromUtf8(raw);
    m_isActive = data.startsWith("RANGE ", Qt::CaseInsensitive);

    if (m_isActive) {
        bool ok = false;
        m_rangeGate = data.mid(6).trimmed().toInt(&ok);
        m_distanceM = ok ? m_rangeGate * GATE_DIST_M : -1.0;
        if (!ok) m_rangeGate = -1;
    } else {
        m_rangeGate = -1;
        m_distanceM = -1.0;
    }

    m_receivedData = data;
    emit receivedDataChanged();
}

void SerialHandler::parseCommandResponse(const QByteArray &frame)
{
    // Layout: [4 hdr][2 len][1 cmd][1 marker=0x01][2 status][data...][4 tail]
    if (frame.size() < 10) return;

    const uint8_t  cmd    = (uint8_t)frame[6];
    const uint8_t  marker = (uint8_t)frame[7];
    const uint16_t status = (uint8_t)frame[8] | ((uint8_t)frame[9] << 8);

    if (marker != 0x01) return; // not a command response

    if (status != 0) {
        setStatus(QString("Command 0x%1 failed (0x%2)")
                  .arg(cmd, 2, 16, QChar('0'))
                  .arg(status, 4, 16, QChar('0')));
        return;
    }

    switch (cmd) {
    case 0x00: { // firmware version: [2 str_len][str...]
        if (frame.size() < 12) return;
        const int strLen = (uint8_t)frame[10] | ((uint8_t)frame[11] << 8);
        if (frame.size() < 12 + strLen) return;
        setStatus(QString("Firmware: %1").arg(
            QString::fromLatin1(frame.constData() + 12, strLen)));
        break;
    }
    default:
        setStatus(QString("Command 0x%1 OK").arg(cmd, 2, 16, QChar('0')));
        break;
    }
}

void SerialHandler::parseReportFrame(const QByteArray &frame)
{
    // Layout: [4 hdr][2 len][1 detection][2 dist_cm][32 gate_energy × 16][4 tail]
    if (frame.size() < 10) return;

    const uint8_t  detection = (uint8_t)frame[6];
    const uint16_t distCm    = (uint8_t)frame[7] | ((uint8_t)frame[8] << 8);

    m_isActive = (detection == 0x01);

    if (m_isActive) {
        m_rangeGate    = -1;                  // gate# not meaningful in report mode
        m_distanceM    = distCm / 100.0;
        m_receivedData = QString("%1 cm").arg(distCm);
    } else {
        m_rangeGate    = -1;
        m_distanceM    = -1.0;
        m_receivedData = "OFF";
    }

    emit receivedDataChanged();
}

void SerialHandler::setStatus(const QString &msg)
{
    if (m_statusMessage == msg) return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}
