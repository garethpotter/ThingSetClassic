/*
 * ThingSet subscription transport for classic Mac OS Open Transport.
 *
 * Unlike ThingSet++'s socket transports this does not spawn a listener thread:
 * classic Mac OS is cooperatively multitasked, so the application calls poll()
 * from its event loop and reports are delivered synchronously from there.
 */
#pragma once

#include <OpenTransport.h>
#include <OpenTptInternet.h>

#include <map>

#include "thingset++/ThingSetSubscriptionTransport.hpp"
#include "thingset++/ip/StreamingUdpThingSetBinaryDecoder.hpp"

/// @brief Identifies the sender of a report and what kind of report it sent.
struct ReportSender
{
    InetHost host;
    InetPort port;
    /// @brief First byte of the reassembled report (0x1F report, 0x1E enhanced report).
    uint8_t reportType;
};

class OpenTransportSubscriptionTransport final : public ThingSet::ThingSetMultiFrameSubscriptionTransport<ReportSender>
{
public:
    static constexpr InetPort defaultPort = 9002;

    explicit OpenTransportSubscriptionTransport(InetPort port = defaultPort);
    ~OpenTransportSubscriptionTransport();

    /// @brief Opens a UDP endpoint bound to the report port. Open Transport
    /// must already have been initialised with InitOpenTransport().
    bool subscribe(std::function<void(const ReportSender &, ThingSet::ThingSetBinaryDecoder &)> callback) override;

    /// @brief Reads any pending datagrams and invokes the callback for each
    /// completed report. Call this regularly from the event loop.
    void poll();

    /// @brief Unbinds and closes the endpoint.
    void close();

    bool isOpen() const
    {
        return _endpoint != kOTInvalidEndpointRef;
    }

    InetPort port() const
    {
        return _port;
    }

    /// @brief The most recent Open Transport error, or kOTNoError.
    OSStatus lastError() const
    {
        return _lastError;
    }

    /// @brief Count of datagrams dropped for being malformed or too large.
    unsigned long droppedCount() const
    {
        return _droppedCount;
    }

private:
    void handleFrame(ThingSet::Ip::Frame &frame, const InetAddress &from);
    void discardRemainder();

    EndpointRef _endpoint;
    InetPort _port;
    OSStatus _lastError;
    unsigned long _droppedCount;
    std::function<void(const ReportSender &, ThingSet::ThingSetBinaryDecoder &)> _callback;
    std::map<InetHost, ThingSet::Ip::StreamingUdpThingSetBinaryDecoder> _decodersBySender;
    std::map<InetHost, uint8_t> _reportTypesBySender;
};
