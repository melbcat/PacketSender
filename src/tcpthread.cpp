/*
 * This file is part of Packet Sender
 *
 * Licensed GPL v2
 * http://PacketSender.com/
 *
 * Copyright Dan Nagle
 *
 */

#include "tcpthread.h"
#include "globals.h"
#include "packet.h"
#include <QDataStream>
#include <QDebug>
#include <QSettings>
#include <QDesktopServices>
#include <QDir>

TCPThread::TCPThread(int socketDescriptor, QObject *parent)
    : QThread(parent), socketDescriptor(socketDescriptor)
{

    init();
    sendFlag = false;
    sendPacket.clear();


}

TCPThread::TCPThread(Packet sendPacket, QObject *parent)
    : QThread(parent), sendPacket(sendPacket)
{
    sendFlag = true;


}

void TCPThread::sendAnother(Packet sendPacket)
{

    QDEBUG() << "Send another packet to " << sendPacket.port;
    this->sendPacket = sendPacket;

}

void TCPThread::init() {

}


void TCPThread::wasdisconnected() {

    QDEBUG();
}

void TCPThread::writeResponse(QTcpSocket *sock, Packet tcpPacket) {

    QSettings settings(SETTINGSFILE, QSettings::IniFormat);
    bool sendResponse = settings.value("sendReponse", false).toBool();
    QString responseData = (settings.value("responseHex","")).toString();


    if(sendResponse)
    {
        Packet tcpPacketreply;
        tcpPacketreply.timestamp = QDateTime::currentDateTime();
        tcpPacketreply.name = "Reply to " + tcpPacket.timestamp.toString(DATETIMEFORMAT);
        tcpPacketreply.tcpOrUdp = "TCP";
        tcpPacketreply.fromIP = "You (Response)";
        tcpPacketreply.toIP = sock->peerAddress().toString();
        tcpPacketreply.port = sock->peerPort();
        tcpPacketreply.fromPort = sock->localPort();
        QByteArray data = Packet::HEXtoByteArray(responseData);
        tcpPacketreply.hexString = Packet::byteArrayToHex(data);
        sock->write(data);
        sock->waitForBytesWritten(2000);
        QDEBUG() << "packetSent " << tcpPacketreply.name << tcpPacketreply.hexString;
        emit packetSent(tcpPacketreply);

    }

}

void TCPThread::run()
{

    //TODO this should use exec() signals/slots event loop!!!
    QDEBUG();

    if(sendFlag) {
        QDEBUG() << "We are threaded sending!";
        clientConnection = new QTcpSocket(this);

        QHostAddress address;
        address.setAddress(sendPacket.toIP);

        sendPacket.fromIP = "You";
        sendPacket.timestamp = QDateTime::currentDateTime();
        sendPacket.name = sendPacket.timestamp.toString(DATETIMEFORMAT);
        bool portpass = false;

        //TODO should remember last successful port so can just +1 it
        for(int localport=5000; localport<60000 && !portpass; localport++) {
            portpass = clientConnection->bind(localport);
            sendPacket.fromPort = localport;
        }


        clientConnection->connectToHost(address,  sendPacket.port);
        clientConnection->waitForConnected(5000);


        if(sendPacket.delayAfterConnect > 0) {
            QDEBUG() << "sleeping " << sendPacket.delayAfterConnect;
            QObject().thread()->usleep(1000*sendPacket.delayAfterConnect);
        }

        QDEBUGVAR(clientConnection->localPort());

        if(clientConnection->state() == QAbstractSocket::ConnectedState)
        {
            sendPacket.port = clientConnection->peerPort();
            sendPacket.fromPort = clientConnection->localPort();

            while(clientConnection->state() == QAbstractSocket::ConnectedState) {

                if(sendPacket.hexString.isEmpty() && sendPacket.persistent) {
                    QDEBUG() << "Loop and wait.";
                    QObject().thread()->usleep(1000*100); //sleep 100 ms
                    continue;
                }

                if(clientConnection->state() != QAbstractSocket::ConnectedState && sendPacket.persistent) {
                    QDEBUG() << "Connection broken.";
                    break;
                }

                if(sendPacket.receiveBeforeSend) {
                    QDEBUG() << "Wait for data before sending...";
                    clientConnection->waitForReadyRead(500);

                    Packet tcpRCVPacket;
                    tcpRCVPacket.hexString = Packet::byteArrayToHex(clientConnection->readAll());
                    if(!tcpRCVPacket.hexString.trimmed().isEmpty()) {
                        QDEBUG() << "Received: " << tcpRCVPacket.hexString;
                        tcpRCVPacket.timestamp = QDateTime::currentDateTime();
                        tcpRCVPacket.name = QDateTime::currentDateTime().toString(DATETIMEFORMAT);
                        tcpRCVPacket.tcpOrUdp = "TCP";
                        tcpRCVPacket.fromIP = clientConnection->peerAddress().toString();
                        tcpRCVPacket.toIP = "You";
                        tcpRCVPacket.port = sendPacket.fromPort;
                        tcpRCVPacket.fromPort =    clientConnection->peerPort();
                        emit packetSent(tcpRCVPacket);

                    } else {
                        QDEBUG() << "No pre-emptive receive data";
                    }

                } // end receive before send


                //sendPacket.fromPort = clientConnection->localPort();
                clientConnection->write(sendPacket.getByteArray());

                Packet tcpPacket;
                tcpPacket.timestamp = QDateTime::currentDateTime();
                tcpPacket.name = QDateTime::currentDateTime().toString(DATETIMEFORMAT);
                tcpPacket.tcpOrUdp = "TCP";
                tcpPacket.fromIP = clientConnection->peerAddress().toString();
                tcpPacket.toIP = "You";
                tcpPacket.port = sendPacket.fromPort;
                tcpPacket.fromPort =    clientConnection->peerPort();

                clientConnection->waitForReadyRead(2000);
                tcpPacket.hexString = Packet::byteArrayToHex(clientConnection->readAll());


                if(!sendPacket.persistent) {
                    clientConnection->disconnectFromHost();
                }

                QDEBUG() << "packetSent " << tcpPacket.name;

                if(sendPacket.receiveBeforeSend) {
                    if(!tcpPacket.hexString.isEmpty()) {
                        emit packetSent(tcpPacket);
                    }
                } else {
                    emit packetSent(tcpPacket);
                }



                sendPacket.response = clientConnection->readAll();
                emit packetSent(sendPacket);


                if(!sendPacket.persistent) {
                    break;
                } else {
                    sendPacket.clear();
                    sendPacket.persistent = true;
                    QDEBUG() << "Persistent connection. Loop and wait.";
                    continue;
                }
            } // end while connected

            QDEBUG() << "Not connected.";

        } else {


            //qintptr sock = clientConnection->socketDescriptor();

            //sendPacket.fromPort = clientConnection->localPort();
            QDEBUG() << "Could not connect";
            sendPacket.errorString = "Could not connect";
            emit packetSent(sendPacket);

        }

        QDEBUG() << "packetSent " << sendPacket.name;
        if(clientConnection->state() == QAbstractSocket::ConnectedState) {
            clientConnection->disconnectFromHost();
            clientConnection->waitForDisconnected(1000);

        }
        clientConnection->close();
        clientConnection->deleteLater();

        return;
    }


    QTcpSocket sock;
    sock.setSocketDescriptor(socketDescriptor);

    connect(&sock, SIGNAL(disconnected()),
            this, SLOT(wasdisconnected()));

    //connect(&sock, SIGNAL(readyRead())

    Packet tcpPacket;
    QByteArray data;

    data.clear();
    tcpPacket.timestamp = QDateTime::currentDateTime();
    tcpPacket.name = tcpPacket.timestamp.toString(DATETIMEFORMAT);
    tcpPacket.tcpOrUdp = "TCP";
    tcpPacket.fromIP = sock.peerAddress().toString();
    tcpPacket.toIP = "You";
    tcpPacket.port = sock.localPort();
    tcpPacket.fromPort = sock.peerPort();

    sock.waitForReadyRead(5000); //initial packet
    data = sock.readAll();
    tcpPacket.hexString = Packet::byteArrayToHex(data);
    emit packetSent(tcpPacket);
    writeResponse(&sock, tcpPacket);
/*

    QDateTime twentyseconds = QDateTime::currentDateTime().addSecs(30);

    while ( sock.bytesAvailable() < 1 && twentyseconds > QDateTime::currentDateTime()) {
        sock.waitForReadyRead();
        data = sock.readAll();
        tcpPacket.hexString = Packet::byteArrayToHex(data);
        tcpPacket.timestamp = QDateTime::currentDateTime();
        tcpPacket.name = tcpPacket.timestamp.toString(DATETIMEFORMAT);
        emit packetSent(tcpPacket);

        writeResponse(&sock, tcpPacket);
    }
*/
    sock.disconnectFromHost();
    sock.close();
}
