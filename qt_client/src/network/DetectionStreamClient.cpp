#include "DetectionStreamClient.h"

#include <QTcpSocket>
#include <QDataStream>

DetectionStreamClient::DetectionStreamClient(QObject *parent)
    : QObject(parent)
{
    socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, this, &DetectionStreamClient::onReadyRead);
    connect(socket, &QTcpSocket::errorOccurred, this, &DetectionStreamClient::onSocketError);
    connect(socket, &QTcpSocket::connected, this, [this]() { emit connectionStateChanged(true); });
    connect(socket, &QTcpSocket::disconnected, this, [this]() { emit connectionStateChanged(false); });
}

void DetectionStreamClient::connectToServer(const QString &host, quint16 port)
{
    socket->connectToHost(host, port);
}

void DetectionStreamClient::onReadyRead()
{
    buffer.append(socket->readAll());

    while (true) {
        if (state == ReadState::Length) {
            if (buffer.size() < 4)
                return;
            QDataStream stream(buffer.left(4));
            stream.setByteOrder(QDataStream::BigEndian);
            stream >> expectedPayloadLen;
            buffer.remove(0, 4);
            state = ReadState::Payload;
        } else if (state == ReadState::Payload) {
            if (static_cast<quint32>(buffer.size()) < expectedPayloadLen)
                return;
            pendingPayload = buffer.left(expectedPayloadLen);
            buffer.remove(0, expectedPayloadLen);
            state = ReadState::AlarmFlag;
        } else if (state == ReadState::AlarmFlag) {
            if (buffer.size() < 1)
                return;
            const bool alarmActive = buffer.at(0) != 0;
            buffer.remove(0, 1);
            state = ReadState::Length;

            const QImage image = QImage::fromData(pendingPayload, "JPG");
            if (!image.isNull())
                emit frameReady(image, alarmActive);
        }
    }
}

void DetectionStreamClient::onSocketError()
{
    emit connectionStateChanged(false);
}
