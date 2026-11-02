/*
 * A TCP connection using Open Transport in non-blocking mode.
 */
#include "OpenTransportTcpConnection.hpp"

OpenTransportTcpConnection::OpenTransportTcpConnection()
    : _endpoint(kOTInvalidEndpointRef), _state(State::closed), _lastError(kOTNoError)
{}

OpenTransportTcpConnection::~OpenTransportTcpConnection()
{
    close();
}

bool OpenTransportTcpConnection::connect(InetHost host, InetPort port)
{
    close();
    _lastError = kOTNoError;

    OSStatus err;
    _endpoint = OTOpenEndpoint(OTCreateConfiguration(kTCPName), 0, nullptr, &err);
    if (err != kOTNoError) {
        _endpoint = kOTInvalidEndpointRef;
        fail(err);
        return false;
    }

    // Synchronous and non-blocking: calls return straight away with
    // kOTNoDataErr or kOTFlowErr rather than waiting.
    OTSetSynchronous(_endpoint);
    OTSetNonBlocking(_endpoint);

    err = OTBind(_endpoint, nullptr, nullptr);
    if (err != kOTNoError) {
        fail(err);
        return false;
    }

    InetAddress address;
    OTInitInetAddress(&address, port, host);
    TCall call;
    OTMemzero(&call, sizeof(call));
    call.addr.buf = reinterpret_cast<UInt8 *>(&address);
    call.addr.len = sizeof(address);

    // in non-blocking mode this returns kOTNoDataErr while the connection is
    // in progress, and T_CONNECT arrives once it's established
    err = OTConnect(_endpoint, &call, nullptr);
    if (err == kOTNoError) {
        _state = State::connected;
    } else if (err == kOTNoDataErr) {
        _state = State::connecting;
    } else if (err == kOTLookErr) {
        _state = State::connecting;
        handleLook();
    } else {
        fail(err);
        return false;
    }
    return _state != State::failed;
}

void OpenTransportTcpConnection::close()
{
    if (_endpoint != kOTInvalidEndpointRef) {
        OTCloseProvider(_endpoint);
        _endpoint = kOTInvalidEndpointRef;
    }
    _pending.clear();
    if (_state != State::failed) {
        _state = State::closed;
    }
}

void OpenTransportTcpConnection::fail(OSStatus error)
{
    _lastError = error;
    close();
    _state = State::failed;
}

void OpenTransportTcpConnection::handleLook()
{
    switch (OTLook(_endpoint)) {
        case T_CONNECT:
            if (OTRcvConnect(_endpoint, nullptr) == kOTNoError) {
                _state = State::connected;
            }
            break;
        case T_DISCONNECT:
            // refused while connecting, otherwise reset or timed out
            OTRcvDisconnect(_endpoint, nullptr);
            fail(_state == State::connecting ? kECONNREFUSEDErr : kECONNRESETErr);
            break;
        case T_ORDREL:
            // the server closed its end
            OTRcvOrderlyDisconnect(_endpoint);
            fail(kECONNRESETErr);
            break;
        default:
            break;
    }
}

void OpenTransportTcpConnection::poll()
{
    if (_endpoint == kOTInvalidEndpointRef) {
        return;
    }
    if (_state == State::connecting) {
        if (OTLook(_endpoint) != 0) {
            handleLook();
        }
        return;
    }
    if (_state == State::connected) {
        flush();
    }
}

bool OpenTransportTcpConnection::send(const uint8_t *data, size_t length)
{
    if (_state != State::connected) {
        return false;
    }
    _pending.insert(_pending.end(), data, data + length);
    flush();
    return _state == State::connected;
}

void OpenTransportTcpConnection::flush()
{
    while (!_pending.empty()) {
        OTResult sent = OTSnd(_endpoint, _pending.data(), _pending.size(), 0);
        if (sent > 0) {
            _pending.erase(_pending.begin(), _pending.begin() + sent);
        } else if (sent == kOTFlowErr) {
            // the send buffer is full; try again on the next poll
            return;
        } else if (sent == kOTLookErr) {
            handleLook();
            if (_state != State::connected) {
                return;
            }
        } else {
            fail(sent);
            return;
        }
    }
}

size_t OpenTransportTcpConnection::receive(uint8_t *buffer, size_t capacity)
{
    if (_state != State::connected || capacity == 0) {
        return 0;
    }
    OTFlags flags = 0;
    OTResult received = OTRcv(_endpoint, buffer, capacity, &flags);
    if (received > 0) {
        return received;
    }
    if (received == kOTLookErr) {
        handleLook();
    } else if (received != kOTNoDataErr) {
        fail(received);
    }
    return 0;
}
