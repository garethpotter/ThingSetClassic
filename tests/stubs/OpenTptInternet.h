/*
 * Minimal stand-in for Open Transport's TCP/IP definitions.
 */
#pragma once

#include "OpenTransport.h"

typedef uint32_t InetHost;
typedef uint16_t InetPort;

#define kOTAnyInetAddress 0

struct InetAddress
{
    uint16_t fAddressType;
    InetPort fPort;
    InetHost fHost;
    UInt8 fUnused[8];
};

inline void OTInitInetAddress(InetAddress *address, InetPort port, InetHost host)
{
    memset(address, 0, sizeof(*address));
    address->fPort = port;
    address->fHost = host;
}
