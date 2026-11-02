/*
 * A TCP connection using Open Transport in non-blocking mode, driven by
 * polling from the event loop so the rest of the Mac keeps running.
 */
#pragma once

#include <OpenTransport.h>
#include <OpenTptInternet.h>

#include <cstdint>
#include <vector>

class OpenTransportTcpConnection
{
public:
    enum struct State
    {
        closed,
        connecting,
        connected,
        /// @brief The connection failed or was dropped; see lastError().
        failed,
    };

    OpenTransportTcpConnection();
    ~OpenTransportTcpConnection();

    /// @brief Starts connecting. Progress is made by poll().
    bool connect(InetHost host, InetPort port);

    /// @brief Completes connecting, sends anything queued and notices disconnection.
    void poll();

    /// @brief Queues data to send.
    bool send(const uint8_t *data, size_t length);

    /// @brief Reads whatever has arrived, without waiting.
    /// @return The number of bytes read, possibly 0.
    size_t receive(uint8_t *buffer, size_t capacity);

    void close();

    State state() const
    {
        return _state;
    }

    /// @brief The error that caused the connection to fail, if any.
    OSStatus lastError() const
    {
        return _lastError;
    }

private:
    void fail(OSStatus error);
    void handleLook();
    void flush();

    EndpointRef _endpoint;
    State _state;
    OSStatus _lastError;
    std::vector<uint8_t> _pending;
};
