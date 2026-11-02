/*
 * Minimal stand-in for Open Transport so the networking code can be tested on
 * the host. UDP datagrams are taken from stubDatagrams; TCP requests are
 * passed to stubTcpServer and its responses delivered stubTcpChunkSize bytes
 * at a time.
 */
#pragma once

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

typedef uint8_t UInt8;
typedef uint32_t UInt32;
typedef int32_t OSStatus;
typedef int32_t OTResult;
typedef uint32_t OTFlags;
typedef void *EndpointRef;
typedef const char *OTConfigurationRef;
typedef struct TEndpointInfo TEndpointInfo;

#define kOTInvalidEndpointRef ((EndpointRef)0)
#define kOTNoError 0
#define kOTNoDataErr (-3162)
#define kOTLookErr (-3158)
#define kOTFlowErr (-3161)
#define kECONNRESETErr (-3253)
#define kECONNREFUSEDErr (-3260)
#define T_MORE 0x0001
#define T_CONNECT 0x0004
#define T_DISCONNECT 0x0010
#define T_UDERR 0x0040
#define T_ORDREL 0x0080
#define kUDPName "udp"
#define kTCPName "tcp"

struct TNetbuf
{
    UInt32 maxlen;
    UInt32 len;
    UInt8 *buf;
};

struct TBind
{
    TNetbuf addr;
    UInt32 qlen;
};

struct TUnitData
{
    TNetbuf addr;
    TNetbuf opt;
    TNetbuf udata;
};

struct TCall
{
    TNetbuf addr;
    TNetbuf opt;
    TNetbuf udata;
    int32_t sequence;
};

struct StubDatagram
{
    uint32_t host;
    uint16_t port;
    std::vector<uint8_t> data;
};

extern std::deque<StubDatagram> stubDatagrams;

// TCP: the server's reply to each request, whether connecting is refused,
// how many bytes each OTRcv returns, and the pending OTLook event.
extern std::function<std::vector<uint8_t>(const uint8_t *request, size_t length)> stubTcpServer;
extern bool stubTcpRefuse;
extern size_t stubTcpChunkSize;
extern std::deque<uint8_t> stubTcpIncoming;
extern OTResult stubTcpLook;

static const EndpointRef stubUdpEndpoint = (EndpointRef)1;
static const EndpointRef stubTcpEndpoint = (EndpointRef)2;

inline void OTMemzero(void *p, size_t n)
{
    memset(p, 0, n);
}

inline OTConfigurationRef OTCreateConfiguration(const char *name)
{
    return name;
}

inline EndpointRef OTOpenEndpoint(OTConfigurationRef config, UInt32, TEndpointInfo *, OSStatus *err)
{
    *err = kOTNoError;
    return strcmp(config, kTCPName) == 0 ? stubTcpEndpoint : stubUdpEndpoint;
}

inline OSStatus OTSetSynchronous(EndpointRef)
{
    return kOTNoError;
}

inline OSStatus OTSetNonBlocking(EndpointRef)
{
    return kOTNoError;
}

inline OSStatus OTBind(EndpointRef, TBind *, TBind *)
{
    return kOTNoError;
}

inline OSStatus OTUnbind(EndpointRef)
{
    return kOTNoError;
}

inline OSStatus OTCloseProvider(EndpointRef)
{
    return kOTNoError;
}

inline OTResult OTLook(EndpointRef endpoint)
{
    return endpoint == stubTcpEndpoint ? stubTcpLook : 0;
}

inline OSStatus OTConnect(EndpointRef, TCall *, TCall *)
{
    stubTcpIncoming.clear();
    stubTcpLook = stubTcpRefuse ? T_DISCONNECT : T_CONNECT;
    return kOTNoDataErr;
}

inline OSStatus OTRcvConnect(EndpointRef, TCall *)
{
    stubTcpLook = 0;
    return kOTNoError;
}

inline OSStatus OTRcvDisconnect(EndpointRef, void *)
{
    stubTcpLook = 0;
    return kOTNoError;
}

inline OSStatus OTRcvOrderlyDisconnect(EndpointRef)
{
    stubTcpLook = 0;
    return kOTNoError;
}

inline OTResult OTSnd(EndpointRef, void *buffer, size_t length, OTFlags)
{
    std::vector<uint8_t> response = stubTcpServer(static_cast<const uint8_t *>(buffer), length);
    stubTcpIncoming.insert(stubTcpIncoming.end(), response.begin(), response.end());
    return length;
}

inline OTResult OTRcv(EndpointRef, void *buffer, size_t capacity, OTFlags *)
{
    if (stubTcpLook != 0) {
        return kOTLookErr;
    }
    size_t length = std::min({ capacity, stubTcpChunkSize, stubTcpIncoming.size() });
    if (length == 0) {
        return kOTNoDataErr;
    }
    std::copy(stubTcpIncoming.begin(), stubTcpIncoming.begin() + length, static_cast<uint8_t *>(buffer));
    stubTcpIncoming.erase(stubTcpIncoming.begin(), stubTcpIncoming.begin() + length);
    return length;
}

inline OSStatus OTRcvUDErr(EndpointRef, void *)
{
    return kOTNoError;
}

OTResult OTRcvUData(EndpointRef, TUnitData *unitData, OTFlags *flags);
