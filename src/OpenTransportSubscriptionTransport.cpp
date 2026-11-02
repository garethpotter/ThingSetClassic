/*
 * ThingSet subscription transport for classic Mac OS Open Transport.
 */
#include "OpenTransportSubscriptionTransport.hpp"

using namespace ThingSet::Ip;

// Upper bound on datagrams handled per poll() so that a busy network can't
// starve the event loop.
static constexpr int maxDatagramsPerPoll = 16;

OpenTransportSubscriptionTransport::OpenTransportSubscriptionTransport(InetPort port)
    : _endpoint(kOTInvalidEndpointRef), _port(port), _lastError(kOTNoError), _droppedCount(0)
{}

OpenTransportSubscriptionTransport::~OpenTransportSubscriptionTransport()
{
    close();
}

bool OpenTransportSubscriptionTransport::subscribe(
    std::function<void(const ReportSender &, ThingSet::ThingSetBinaryDecoder &)> callback)
{
    close();

    OSStatus err;
    _endpoint = OTOpenEndpoint(OTCreateConfiguration(kUDPName), 0, nullptr, &err);
    if (err != kOTNoError) {
        _endpoint = kOTInvalidEndpointRef;
        _lastError = err;
        return false;
    }

    // Synchronous and non-blocking: OTRcvUData returns kOTNoDataErr straight
    // away when nothing is waiting, which suits polling from the event loop.
    OTSetSynchronous(_endpoint);
    OTSetNonBlocking(_endpoint);

    // Broadcasts are delivered to an endpoint bound to the wildcard address.
    InetAddress address;
    OTInitInetAddress(&address, _port, kOTAnyInetAddress);

    TBind request;
    OTMemzero(&request, sizeof(request));
    request.addr.buf = reinterpret_cast<UInt8 *>(&address);
    request.addr.len = sizeof(address);

    err = OTBind(_endpoint, &request, nullptr);
    if (err != kOTNoError) {
        OTCloseProvider(_endpoint);
        _endpoint = kOTInvalidEndpointRef;
        _lastError = err;
        return false;
    }

    _callback = callback;
    _lastError = kOTNoError;
    return true;
}

void OpenTransportSubscriptionTransport::close()
{
    if (_endpoint != kOTInvalidEndpointRef) {
        OTUnbind(_endpoint);
        OTCloseProvider(_endpoint);
        _endpoint = kOTInvalidEndpointRef;
    }
    _decodersBySender.clear();
    _reportTypesBySender.clear();
}

void OpenTransportSubscriptionTransport::poll()
{
    if (_endpoint == kOTInvalidEndpointRef) {
        return;
    }

    for (int i = 0; i < maxDatagramsPerPoll; i++) {
        Frame frame;
        InetAddress from;

        TUnitData unitData;
        OTMemzero(&unitData, sizeof(unitData));
        unitData.addr.buf = reinterpret_cast<UInt8 *>(&from);
        unitData.addr.maxlen = sizeof(from);
        unitData.udata.buf = frame.buffer;
        unitData.udata.maxlen = sizeof(frame.buffer);

        OTFlags flags = 0;
        OTResult result = OTRcvUData(_endpoint, &unitData, &flags);

        if (result == kOTNoDataErr) {
            return;
        }

        if (result == kOTLookErr) {
            // A previous send failed or similar; clear the event and carry on.
            if (OTLook(_endpoint) == T_UDERR) {
                OTRcvUDErr(_endpoint, nullptr);
            }
            continue;
        }

        if (result != kOTNoError) {
            _lastError = result;
            return;
        }

        if (flags & T_MORE) {
            // Larger than any ThingSet streaming frame, so not one of ours.
            discardRemainder();
            _droppedCount++;
            continue;
        }

        frame.length = unitData.udata.len;
        handleFrame(frame, from);
    }
}

void OpenTransportSubscriptionTransport::discardRemainder()
{
    UInt8 scratch[128];
    OTFlags flags;
    do {
        TUnitData unitData;
        OTMemzero(&unitData, sizeof(unitData));
        unitData.udata.buf = scratch;
        unitData.udata.maxlen = sizeof(scratch);
        flags = 0;
        if (OTRcvUData(_endpoint, &unitData, &flags) != kOTNoError) {
            return;
        }
    } while (flags & T_MORE);
}

void OpenTransportSubscriptionTransport::handleFrame(Frame &frame, const InetAddress &from)
{
    if (frame.length <= THINGSET_STREAMING_HEADER_SIZE) {
        _droppedCount++;
        return;
    }

    // The report type byte only appears in the first frame of a report, and
    // ThingSet++'s decoder skips over it, so note it here for the formatter.
    MessageType messageType = getMessageType(frame);
    if (messageType == MessageType::first || messageType == MessageType::single) {
        _reportTypesBySender[from.fHost] = frame.buffer[THINGSET_STREAMING_HEADER_SIZE];
    }

    ReportSender sender;
    sender.host = from.fHost;
    sender.port = from.fPort;
    auto found = _reportTypesBySender.find(from.fHost);
    sender.reportType = found != _reportTypesBySender.end() ? found->second : 0;

    SubscriptionListener::handle(frame, sender, from.fHost, _decodersBySender, _callback);
}
