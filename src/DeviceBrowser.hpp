/*
 * Browses a ThingSet device over TCP: discovers its schema a group at a time
 * and reads the values of expanded groups when asked.
 *
 * Requests are encoded and responses decoded with ThingSet++, but sent and
 * received asynchronously here rather than with ThingSetClient, whose
 * blocking reads would freeze a cooperatively multitasked Mac.
 */
#pragma once

#include <OpenTptInternet.h>

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "OpenTransportTcpConnection.hpp"

namespace ThingSet {
class ThingSetBinaryDecoder;
}

class DeviceBrowser
{
public:
    static constexpr InetPort defaultPort = 9001;
    static constexpr uint32_t rootId = 0;

    struct Node
    {
        enum struct Kind : uint8_t
        {
            /// @brief An item on the device: a group, property or function.
            item,
            /// @brief One record in a record array, e.g. `Modules/1`.
            element,
            /// @brief A member of a record.
            member,
        };

        /// @brief The device's ID for items; elements and members get IDs of their own.
        uint32_t id;
        uint32_t parentId;
        std::string name;
        /// @brief Slash-separated path from the root, e.g. `Battery/Voltage` or `Modules/1`.
        std::string path;
        Kind kind;
        /// @brief For members, the record member's ID on the device.
        uint32_t memberId;
        /// @brief ThingSet type, e.g. `f32`, `group`, `record[]` or `(i32)->()`; empty if unknown.
        std::string type;
        uint8_t access;
        /// @brief The value rendered as text; empty until first read.
        std::string value;
        std::vector<uint32_t> children;
        bool childrenLoaded;
        bool expanded;
        /// @brief The group's values are too large to GET at once, so are read one by one.
        bool perItemValues;
        /// @brief The device gave only this record array's length, so its elements are read by path.
        bool elementsByPath;

        bool isGroup() const
        {
            return type == "group";
        }

        /// @brief Whether this is a record or an array of records.
        bool isRecord() const
        {
            return type.compare(0, 6, "record") == 0;
        }

        bool isRecordArray() const
        {
            return type == "record[]";
        }

        bool isExpandable() const
        {
            return isGroup() || isRecord();
        }

        bool isFunction() const
        {
            return !type.empty() && type[0] == '(';
        }

        bool hasValue() const
        {
            return !isGroup() && !isFunction();
        }
    };

    enum struct State
    {
        connecting,
        connected,
        failed,
    };

    /// @param label How to describe the device, e.g. the host name typed by the user.
    DeviceBrowser(InetHost host, const std::string &label, InetPort port = defaultPort);

    /// @brief Starts connecting; the root group is loaded once connected.
    void start();

    /// @brief Sends and receives requests. Call regularly from the event loop.
    /// @param nowTicks The current time in ticks (60ths of a second).
    void poll(UInt32 nowTicks);

    /// @brief Re-reads the values of everything expanded. Values are otherwise
    /// read only when a group is first loaded or expanded: redrawing a tree
    /// that updates itself is too much for slower Macs.
    void refresh();

    /// @brief Expands or collapses a group or record, loading its children the first time.
    void setExpanded(uint32_t id, bool expanded);

    /// @brief Gets a node, or null if there isn't one with that ID.
    const Node *node(uint32_t id) const;

    InetHost host() const
    {
        return _host;
    }

    State state() const
    {
        return _state;
    }

    /// @brief Whether a request is outstanding or queued.
    bool busy() const
    {
        return _awaitingResponse || !_queue.empty();
    }

    /// @brief Describes what the browser is doing, for a status bar.
    std::string statusText() const;

    /// @brief Invoked when nodes are added, or groups expanded or collapsed.
    std::function<void()> onStructureChanged;
    /// @brief Invoked when a node's value changes.
    std::function<void(uint32_t id)> onValueChanged;

private:
    enum struct Purpose
    {
        childIds,
        childNames,
        metadata,
        groupValues,
        value,
        element,
    };

    struct Request
    {
        Purpose purpose;
        /// @brief The group being loaded, or the node whose value is wanted.
        uint32_t node;
        /// @brief For metadata: the IDs asked about and where they start in the group's child list.
        std::vector<uint32_t> ids;
        size_t offset;
    };

    /// @brief A decoded value, kept structured so records can be shown as children.
    struct ValueTree;

    struct ChildInfo
    {
        uint32_t id;
        std::string name;
        std::string type;
        uint8_t access;
    };

    /// @brief A group whose children are being discovered.
    struct Loading
    {
        std::vector<ChildInfo> children;
        size_t metadataOutstanding;
    };

    void enqueue(Purpose purpose, uint32_t node, std::vector<uint32_t> ids = {}, size_t offset = 0);
    void sendNext(UInt32 nowTicks);
    void receive();
    bool responseComplete() const;
    void handleResponse();
    void handleError(uint8_t status);
    void handleChildIds(ThingSet::ThingSetBinaryDecoder &decoder);
    void handleChildNames(ThingSet::ThingSetBinaryDecoder &decoder);
    void handleMetadata(ThingSet::ThingSetBinaryDecoder &decoder);
    void handleGroupValues(ThingSet::ThingSetBinaryDecoder &decoder);
    bool applyGroupValues(uint32_t group, ThingSet::ThingSetBinaryDecoder &decoder);
    void handleValue(ThingSet::ThingSetBinaryDecoder &decoder);
    void handleElement(ThingSet::ThingSetBinaryDecoder &decoder);
    void applyValue(uint32_t id, ThingSet::ThingSetBinaryDecoder &decoder);
    void applyRecordValue(uint32_t id, const ValueTree &tree);
    void syncElements(uint32_t id, size_t count, const std::vector<ValueTree> *records);
    void syncMembers(uint32_t id, const ValueTree &record);
    void requestElements(uint32_t id);
    uint32_t addNode(uint32_t parentId, Node::Kind kind, const std::string &name, const std::string &type);
    void removeSubtree(uint32_t id);
    void notifyStructureChanged();
    static bool decodeTree(ThingSet::ThingSetBinaryDecoder &decoder, ValueTree &tree);
    static std::string formatTree(const ValueTree &tree);
    void metadataDone(uint32_t group);
    void finishLoading(uint32_t group);
    void requestValues(uint32_t group);
    void refreshExpanded(uint32_t group);
    void setValue(uint32_t id, const std::string &value);
    void fail(const std::string &reason);

    InetHost _host;
    InetPort _port;
    State _state;
    std::string _failure;
    std::string _notice;
    OpenTransportTcpConnection _connection;
    std::map<uint32_t, Node> _nodes;
    std::map<uint32_t, Loading> _loading;
    /// @brief Names and types of record members, for devices that list them.
    std::map<uint32_t, ChildInfo> _memberInfo;
    uint32_t _nextNodeId;
    bool _structureChanged;
    std::deque<Request> _queue;
    bool _awaitingResponse;
    Request _current;
    UInt32 _sentTicks;
    std::vector<uint8_t> _response;
};
