/*
 * State and out-of-line functions for the Open Transport stand-in.
 */
#include "OpenTransport.h"
#include "OpenTptInternet.h"

std::deque<StubDatagram> stubDatagrams;

OTResult OTRcvUData(EndpointRef, TUnitData *unitData, OTFlags *flags)
{
    if (stubDatagrams.empty()) {
        return kOTNoDataErr;
    }
    StubDatagram &datagram = stubDatagrams.front();
    InetAddress *from = reinterpret_cast<InetAddress *>(unitData->addr.buf);
    if (from) {
        OTInitInetAddress(from, datagram.port, datagram.host);
    }
    size_t length = datagram.data.size() < unitData->udata.maxlen ? datagram.data.size() : unitData->udata.maxlen;
    memcpy(unitData->udata.buf, datagram.data.data(), length);
    unitData->udata.len = length;
    if (length < datagram.data.size()) {
        datagram.data.erase(datagram.data.begin(), datagram.data.begin() + length);
        *flags = T_MORE;
    } else {
        stubDatagrams.pop_front();
        *flags = 0;
    }
    return kOTNoError;
}

std::function<std::vector<uint8_t>(const uint8_t *request, size_t length)> stubTcpServer;
bool stubTcpRefuse;
size_t stubTcpChunkSize = 1500;
std::deque<uint8_t> stubTcpIncoming;
OTResult stubTcpLook;
