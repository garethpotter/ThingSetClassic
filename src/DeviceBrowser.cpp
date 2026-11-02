/*
 * Browses a ThingSet device over TCP.
 */
#include "DeviceBrowser.hpp"

#include <algorithm>
#include <cstdlib>

#include "thingset++/ThingSetBinaryDecoder.hpp"
#include "thingset++/ThingSetBinaryEncoder.hpp"
#include "thingset++/ThingSetStatus.hpp"

#include "ReportFormatter.hpp"

using namespace ThingSet;

// Overlay endpoint for metadata, and the keys of its entries (see ThingSet++'s
// ThingSetServer.cpp and ThingSet.Net's ThingSetSchema.cs).
static constexpr uint32_t metadataId = 0x19;
static constexpr uint32_t metadataNameId = 0x1a;
static constexpr uint32_t metadataTypeId = 0x1b;
static constexpr uint32_t metadataAccessId = 0x1c;

// IDs per metadata request, keeping responses small enough for small devices.
static constexpr size_t metadataBatchSize = 12;

static constexpr uint8_t cborNull = 0xF6;
static constexpr size_t requestBufferSize = 512;
static constexpr size_t maxResponseSize = 16384;

static constexpr UInt32 responseTimeoutTicks = 5 * 60;

static constexpr uint8_t anyRead = 0x07;

// IDs for elements and members start here, clear of the device's 16-bit IDs.
static constexpr uint32_t firstLocalId = 0x80000000;

struct DeviceBrowser::ValueTree
{
    enum struct Kind
    {
        scalar,
        list,
        map,
    };

    Kind kind = Kind::scalar;
    /// @brief A scalar rendered as text.
    std::string text;
    /// @brief The key of this value within a map.
    std::optional<uint32_t> id;
    std::optional<std::string> name;
    std::vector<ValueTree> items;

    bool isRecordList() const
    {
        return kind == Kind::list
               && std::all_of(items.begin(), items.end(), [](const ValueTree &item) { return item.kind == Kind::map; });
    }
};

static std::string recordsText(size_t count)
{
    return std::to_string(count) + (count == 1 ? " record" : " records");
}

DeviceBrowser::DeviceBrowser(InetHost host, const std::string &label, InetPort port)
    : _host(host), _port(port), _state(State::connecting), _nextNodeId(firstLocalId), _structureChanged(false),
      _awaitingResponse(false), _current{}, _sentTicks(0)
{
    Node root{};
    root.id = rootId;
    root.parentId = rootId;
    root.name = label;
    root.type = "group";
    root.access = anyRead;
    root.expanded = true;
    _nodes[rootId] = root;
}

void DeviceBrowser::start()
{
    _state = State::connecting;
    if (!_connection.connect(_host, _port)) {
        fail("Could not connect (error " + std::to_string((long)_connection.lastError()) + ")");
    }
}

const DeviceBrowser::Node *DeviceBrowser::node(uint32_t id) const
{
    auto found = _nodes.find(id);
    return found != _nodes.end() ? &found->second : nullptr;
}

std::string DeviceBrowser::statusText() const
{
    std::string address = formatAddress(_host);
    switch (_state) {
        case State::connecting:
            return "Connecting to " + address + "...";
        case State::failed:
            return _failure;
        case State::connected:
            break;
    }

    size_t items = std::count_if(_nodes.begin(), _nodes.end(), [](const auto &entry) {
        return entry.second.kind == Node::Kind::item && entry.first != rootId;
    });
    std::string text = !_loading.empty() ? "Loading from " + address + "..."
                                         : "Connected to " + address + ": " + std::to_string(items) + " items";
    if (!_notice.empty()) {
        text += ". " + _notice;
    }
    return text;
}

void DeviceBrowser::fail(const std::string &reason)
{
    _state = State::failed;
    _failure = reason;
    _connection.close();
    _queue.clear();
    _awaitingResponse = false;
}

void DeviceBrowser::poll(UInt32 nowTicks)
{
    if (_state == State::failed) {
        return;
    }

    _connection.poll();

    switch (_connection.state()) {
        case OpenTransportTcpConnection::State::connecting:
            return;
        case OpenTransportTcpConnection::State::failed:
        case OpenTransportTcpConnection::State::closed: {
            OSStatus error = _connection.lastError();
            if (_state == State::connecting) {
                fail(error == kECONNREFUSEDErr ? "Connection refused by " + formatAddress(_host)
                                               : "Could not connect (error " + std::to_string((long)error) + ")");
            } else {
                fail("Disconnected from " + formatAddress(_host));
            }
            return;
        }
        case OpenTransportTcpConnection::State::connected:
            break;
    }

    if (_state == State::connecting) {
        _state = State::connected;
        _loading[rootId] = Loading{};
        enqueue(Purpose::childIds, rootId);
    }

    if (_awaitingResponse) {
        receive();
        if (_state == State::failed) {
            return;
        }
        if (_awaitingResponse && nowTicks - _sentTicks > responseTimeoutTicks) {
            fail("No response from " + formatAddress(_host));
            return;
        }
    }

    if (!_awaitingResponse) {
        sendNext(nowTicks);
    }
}

void DeviceBrowser::enqueue(Purpose purpose, uint32_t node, std::vector<uint32_t> ids, size_t offset)
{
    // don't pile up repeated value reads if the device is slow to answer
    if (purpose == Purpose::groupValues || purpose == Purpose::value || purpose == Purpose::element) {
        for (const Request &queued : _queue) {
            if (queued.purpose == purpose && queued.node == node) {
                return;
            }
        }
    }
    _queue.push_back(Request{ purpose, node, std::move(ids), offset });
}

void DeviceBrowser::sendNext(UInt32 nowTicks)
{
    if (_queue.empty()) {
        return;
    }
    _current = _queue.front();
    _queue.pop_front();

    uint8_t buffer[requestBufferSize];
    DefaultFixedDepthThingSetBinaryEncoder encoder(buffer + 1, sizeof(buffer) - 1, 2);
    bool encoded;
    switch (_current.purpose) {
        case Purpose::childIds:
            buffer[0] = (uint8_t)ThingSetBinaryRequestType::fetch;
            encoded = encoder.encode(_current.node) && encoder.encodeNull();
            break;
        case Purpose::childNames:
            buffer[0] = (uint8_t)ThingSetBinaryRequestType::fetch;
            encoded = encoder.encode(_nodes[_current.node].path) && encoder.encodeNull();
            break;
        case Purpose::metadata:
            buffer[0] = (uint8_t)ThingSetBinaryRequestType::fetch;
            encoded = encoder.encode(metadataId) && encoder.encodeListStart(_current.ids.size());
            for (uint32_t id : _current.ids) {
                encoded = encoded && encoder.encode(id);
            }
            encoded = encoded && encoder.encodeListEnd(_current.ids.size());
            break;
        case Purpose::element:
            buffer[0] = (uint8_t)ThingSetBinaryRequestType::get;
            encoded = encoder.encode(_nodes[_current.node].path);
            break;
        case Purpose::groupValues:
        case Purpose::value:
        default:
            buffer[0] = (uint8_t)ThingSetBinaryRequestType::get;
            encoded = encoder.encode(_current.node);
            break;
    }

    if (!encoded) {
        _notice = "Request too large to send";
        return;
    }

    // anything left over from an earlier response would corrupt this one
    _response.clear();
    if (!_connection.send(buffer, 1 + encoder.getEncodedLength())) {
        fail("Disconnected from " + formatAddress(_host));
        return;
    }
    _awaitingResponse = true;
    _sentTicks = nowTicks;
}

void DeviceBrowser::receive()
{
    uint8_t chunk[512];
    size_t received;
    while ((received = _connection.receive(chunk, sizeof(chunk))) > 0) {
        _response.insert(_response.end(), chunk, chunk + received);
        if (_response.size() > maxResponseSize) {
            fail("Response from " + formatAddress(_host) + " too large");
            return;
        }
    }

    if (_connection.state() != OpenTransportTcpConnection::State::connected) {
        fail("Disconnected from " + formatAddress(_host));
        return;
    }

    if (responseComplete()) {
        _awaitingResponse = false;
        handleResponse();
        notifyStructureChanged();
    }
}

bool DeviceBrowser::responseComplete() const
{
    // TCP doesn't preserve message boundaries and ThingSet adds no framing,
    // so a response is complete once it holds a whole CBOR value.
    if (_response.empty()) {
        return false;
    }
    if (_response[0] >= (uint8_t)ThingSetStatusCode::badRequest) {
        // errors carry just the status code
        return true;
    }
    if (_response.size() < 3) {
        return false;
    }
    DefaultFixedDepthThingSetBinaryDecoder decoder(&_response[2], _response.size() - 2);
    return decoder.skip();
}

void DeviceBrowser::handleResponse()
{
    uint8_t status = _response[0];
    if (status >= (uint8_t)ThingSetStatusCode::badRequest) {
        handleError(status);
        return;
    }
    if (_response[1] != cborNull) {
        _notice = "Unexpected response from device";
        return;
    }

    DefaultFixedDepthThingSetBinaryDecoder decoder(&_response[2], _response.size() - 2);
    switch (_current.purpose) {
        case Purpose::childIds:
            handleChildIds(decoder);
            break;
        case Purpose::childNames:
            handleChildNames(decoder);
            break;
        case Purpose::metadata:
            handleMetadata(decoder);
            break;
        case Purpose::groupValues:
            handleGroupValues(decoder);
            break;
        case Purpose::value:
            handleValue(decoder);
            break;
        case Purpose::element:
            handleElement(decoder);
            break;
    }
}

void DeviceBrowser::handleError(uint8_t status)
{
    std::string code = formatHex(status);
    switch (_current.purpose) {
        case Purpose::childIds:
            // can't list this group; show it as empty. Many devices can't
            // list record members, which isn't worth mentioning.
            if (!_nodes[_current.node].isRecord()) {
                _notice = "Could not list " + _nodes[_current.node].name + " (" + code + ")";
            }
            finishLoading(_current.node);
            break;
        case Purpose::metadata: {
            auto found = _loading.find(_current.node);
            if (found == _loading.end()) {
                break;
            }
            Loading &loading = found->second;
            if (_current.ids.size() > 1) {
                // perhaps too large for the device's response buffer: ask about each item alone
                for (size_t i = 0; i < _current.ids.size(); i++) {
                    enqueue(Purpose::metadata, _current.node, { _current.ids[i] }, _current.offset + i);
                }
                loading.metadataOutstanding += _current.ids.size() - 1;
            } else if (--loading.metadataOutstanding == 0) {
                // this item has no metadata; metadataDone() gets its name another way
                metadataDone(_current.node);
            }
            break;
        }
        case Purpose::childNames:
            _notice = "Could not get names in " + _nodes[_current.node].name + " (" + code + ")";
            finishLoading(_current.node);
            break;
        case Purpose::groupValues:
            if (status == (uint8_t)ThingSetStatusCode::requestTooLarge) {
                // too much for the device's response buffer; read values one at a time
                _nodes[_current.node].perItemValues = true;
                requestValues(_current.node);
            } else {
                _notice = "Could not read " + _nodes[_current.node].name + " (" + code + ")";
            }
            break;
        case Purpose::value:
        case Purpose::element:
            if (_nodes.count(_current.node)) {
                setValue(_current.node, "<" + code + ">");
            }
            break;
    }
}

void DeviceBrowser::handleChildIds(ThingSetBinaryDecoder &decoder)
{
    uint32_t group = _current.node;
    Loading &loading = _loading[group];
    loading.children.clear();
    bool ok = decoder.decodeList([&](size_t) {
        uint32_t id;
        if (!decoder.decode(&id)) {
            return false;
        }
        loading.children.push_back(ChildInfo{ id, "", "", anyRead });
        return true;
    });
    if (!ok) {
        _notice = "Could not list " + _nodes[group].name;
    }

    if (loading.children.empty()) {
        finishLoading(group);
        return;
    }

    loading.metadataOutstanding = 0;
    for (size_t i = 0; i < loading.children.size(); i += metadataBatchSize) {
        std::vector<uint32_t> ids;
        for (size_t j = i; j < std::min(i + metadataBatchSize, loading.children.size()); j++) {
            ids.push_back(loading.children[j].id);
        }
        enqueue(Purpose::metadata, group, ids, i);
        loading.metadataOutstanding++;
    }
}

void DeviceBrowser::handleMetadata(ThingSetBinaryDecoder &decoder)
{
    uint32_t group = _current.node;
    auto found = _loading.find(group);
    if (found == _loading.end()) {
        return;
    }
    Loading &loading = found->second;

    size_t index = _current.offset;
    decoder.decodeList([&](size_t) {
        ChildInfo *child = index < loading.children.size() ? &loading.children[index] : nullptr;
        index++;
        return decoder.decodeMap([&](std::optional<uint32_t> id, std::optional<std::string> name) {
            // keys may be numeric (ThingSet++) or text
            bool isName = name ? *name == "name" : id == metadataNameId;
            bool isType = name ? *name == "type" : id == metadataTypeId;
            bool isAccess = name ? *name == "access" : id == metadataAccessId;
            if (child && isName) {
                return decoder.decode(&child->name);
            }
            if (child && isType) {
                return decoder.decode(&child->type);
            }
            if (child && isAccess) {
                return decoder.decode(&child->access);
            }
            return decoder.skip();
        });
    });

    if (loading.metadataOutstanding > 0) {
        loading.metadataOutstanding--;
    }
    if (loading.metadataOutstanding == 0) {
        metadataDone(group);
    }
}

void DeviceBrowser::metadataDone(uint32_t group)
{
    // some servers leave names out of the metadata; ask for them by path
    const Loading &loading = _loading[group];
    bool missingNames = std::any_of(loading.children.begin(), loading.children.end(),
                                    [](const ChildInfo &child) { return child.name.empty(); });
    if (missingNames) {
        enqueue(Purpose::childNames, group);
    } else {
        finishLoading(group);
    }
}

void DeviceBrowser::handleChildNames(ThingSetBinaryDecoder &decoder)
{
    uint32_t group = _current.node;
    Loading &loading = _loading[group];
    decoder.decodeList([&](size_t index) {
        std::string name;
        if (!decoder.decode(&name)) {
            return false;
        }
        if (index < loading.children.size() && loading.children[index].name.empty()) {
            loading.children[index].name = name;
        }
        return true;
    });
    finishLoading(group);
}

void DeviceBrowser::finishLoading(uint32_t group)
{
    Node &parent = _nodes[group];
    auto found = _loading.find(group);

    if (parent.isRecord()) {
        // a record's children come from its value; just note its members' names and types
        if (found != _loading.end()) {
            for (const ChildInfo &info : found->second.children) {
                if (!info.name.empty()) {
                    _memberInfo[info.id] = info;
                }
            }
            _loading.erase(found);
        }
        for (auto &entry : _nodes) {
            Node &node = entry.second;
            auto info = _memberInfo.find(node.memberId);
            if (node.kind == Node::Kind::member && info != _memberInfo.end() && node.name != info->second.name) {
                node.name = info->second.name;
                if (!info->second.type.empty()) {
                    node.type = info->second.type;
                }
            }
        }
        parent.childrenLoaded = true;
        _structureChanged = true;
        return;
    }

    parent.children.clear();
    if (found != _loading.end()) {
        for (const ChildInfo &info : found->second.children) {
            // like ThingSet.Net, hide internal items such as _Metadata
            if (info.name.empty() || info.name[0] == '_') {
                continue;
            }
            Node child{};
            child.id = info.id;
            child.parentId = group;
            child.name = info.name;
            child.path = parent.path.empty() ? info.name : parent.path + "/" + info.name;
            child.type = info.type;
            child.access = info.access;
            // the same item might be reachable twice; keep the first
            if (_nodes.emplace(info.id, child).second) {
                parent.children.push_back(info.id);
            }
        }
        _loading.erase(found);
    }

    parent.childrenLoaded = true;
    _structureChanged = true;
    if (parent.expanded) {
        requestValues(group);
    }
}

void DeviceBrowser::requestValues(uint32_t group)
{
    const Node &parent = _nodes[group];
    bool anyValues = false;
    for (uint32_t id : parent.children) {
        const Node &child = _nodes[id];
        if (child.hasValue() && (child.access & anyRead)) {
            anyValues = true;
            if (parent.perItemValues) {
                enqueue(Purpose::value, id);
            }
        }
    }
    if (anyValues && !parent.perItemValues) {
        enqueue(Purpose::groupValues, group);
    }
}

void DeviceBrowser::refresh()
{
    if (_state == State::connected) {
        refreshExpanded(rootId);
    }
}

void DeviceBrowser::refreshExpanded(uint32_t group)
{
    const Node &parent = _nodes[group];
    if (!parent.expanded || !parent.childrenLoaded) {
        return;
    }
    requestValues(group);
    for (uint32_t id : parent.children) {
        const Node &child = _nodes[id];
        if (child.isGroup()) {
            refreshExpanded(id);
        } else if (child.isRecordArray() && child.expanded && child.elementsByPath) {
            requestElements(id);
        }
    }
}

void DeviceBrowser::handleGroupValues(ThingSetBinaryDecoder &decoder)
{
    applyGroupValues(_current.node, decoder);
}

bool DeviceBrowser::applyGroupValues(uint32_t group, ThingSetBinaryDecoder &decoder)
{
    return decoder.decodeMap([&](std::optional<uint32_t> id, std::optional<std::string> name) {
        uint32_t target = 0;
        bool known = false;
        if (name) {
            for (uint32_t child : _nodes[group].children) {
                if (_nodes[child].name == *name) {
                    target = child;
                    known = true;
                }
            }
        } else if (id) {
            target = *id;
            known = _nodes.count(target) != 0;
        }
        if (!known) {
            return decoder.skip();
        }

        // ThingSet++ includes child groups as nested maps; use them if the group has been loaded
        const Node &node = _nodes[target];
        if (node.isGroup()) {
            if (node.childrenLoaded && decoder.peekType() == ThingSetEncodedNodeType::map) {
                return applyGroupValues(target, decoder);
            }
            return decoder.skip();
        }

        applyValue(target, decoder);
        return true;
    });
}

void DeviceBrowser::handleValue(ThingSetBinaryDecoder &decoder)
{
    applyValue(_current.node, decoder);
}

void DeviceBrowser::handleElement(ThingSetBinaryDecoder &decoder)
{
    // the element may have gone if the array shrank meanwhile
    ValueTree tree;
    if (_nodes.count(_current.node) && decodeTree(decoder, tree)) {
        applyRecordValue(_current.node, tree);
    }
}

void DeviceBrowser::applyValue(uint32_t id, ThingSetBinaryDecoder &decoder)
{
    if (_nodes[id].isRecord()) {
        ValueTree tree;
        if (decodeTree(decoder, tree)) {
            applyRecordValue(id, tree);
        }
        return;
    }
    std::string value;
    if (formatValue(value, decoder)) {
        setValue(id, value);
    }
}

void DeviceBrowser::applyRecordValue(uint32_t id, const ValueTree &tree)
{
    Node &node = _nodes[id];
    if (node.isRecordArray() && tree.kind == ValueTree::Kind::scalar) {
        // GET on a record array itself gives just its length; elements are then read by path
        size_t count = strtoul(tree.text.c_str(), nullptr, 10);
        node.elementsByPath = node.kind == Node::Kind::item;
        setValue(id, recordsText(count));
        syncElements(id, count, nullptr);
        if (node.expanded && node.elementsByPath) {
            requestElements(id);
        }
    } else if (tree.isRecordList()) {
        node.elementsByPath = false;
        setValue(id, recordsText(tree.items.size()));
        syncElements(id, tree.items.size(), &tree.items);
    } else if (tree.kind == ValueTree::Kind::map) {
        setValue(id, formatTree(tree));
        syncMembers(id, tree);
    } else {
        setValue(id, formatTree(tree));
    }
}

void DeviceBrowser::syncElements(uint32_t id, size_t count, const std::vector<ValueTree> *records)
{
    std::vector<uint32_t> &children = _nodes[id].children;
    while (children.size() > count) {
        removeSubtree(children.back());
        children.pop_back();
        _structureChanged = true;
    }
    while (children.size() < count) {
        // addNode doesn't move existing nodes, so children stays valid
        uint32_t element = addNode(id, Node::Kind::element, "[" + std::to_string(children.size()) + "]", "record");
        _nodes[element].path = _nodes[id].path + "/" + std::to_string(children.size());
        children.push_back(element);
    }
    if (records) {
        for (size_t i = 0; i < count; i++) {
            applyRecordValue(children[i], (*records)[i]);
        }
    }
}

void DeviceBrowser::syncMembers(uint32_t id, const ValueTree &record)
{
    for (const ValueTree &item : record.items) {
        uint32_t member = 0;
        for (uint32_t child : _nodes[id].children) {
            const Node &node = _nodes[child];
            if (item.name ? node.name == *item.name : node.memberId == item.id.value_or(0)) {
                member = child;
                break;
            }
        }

        if (member == 0) {
            // ThingSet++ doesn't say what members are called, so fall back to their IDs
            auto info = item.id ? _memberInfo.find(*item.id) : _memberInfo.end();
            bool known = info != _memberInfo.end();
            std::string name = item.name ? *item.name : known ? info->second.name : formatHex(item.id.value_or(0));
            std::string type = known && !info->second.type.empty() ? info->second.type
                               : item.isRecordList()                ? "record[]"
                               : item.kind == ValueTree::Kind::map  ? "record"
                                                                    : "";
            member = addNode(id, Node::Kind::member, name, type);
            _nodes[member].memberId = item.id.value_or(0);
            _nodes[member].path = _nodes[id].path + "/" + name;
            _nodes[id].children.push_back(member);
        }

        if (_nodes[member].isRecord()) {
            applyRecordValue(member, item);
        } else {
            setValue(member, formatTree(item));
        }
    }
}

void DeviceBrowser::requestElements(uint32_t id)
{
    for (uint32_t element : _nodes[id].children) {
        enqueue(Purpose::element, element);
    }
}

uint32_t DeviceBrowser::addNode(uint32_t parentId, Node::Kind kind, const std::string &name, const std::string &type)
{
    Node node{};
    node.id = _nextNodeId++;
    node.parentId = parentId;
    node.kind = kind;
    node.name = name;
    node.type = type;
    node.access = _nodes[parentId].access;
    node.childrenLoaded = true;
    _nodes[node.id] = node;
    _structureChanged = true;
    return node.id;
}

void DeviceBrowser::removeSubtree(uint32_t id)
{
    for (uint32_t child : _nodes[id].children) {
        removeSubtree(child);
    }
    _nodes.erase(id);
}

void DeviceBrowser::notifyStructureChanged()
{
    if (_structureChanged) {
        _structureChanged = false;
        if (onStructureChanged) {
            onStructureChanged();
        }
    }
}

bool DeviceBrowser::decodeTree(ThingSetBinaryDecoder &decoder, ValueTree &tree)
{
    switch (decoder.peekType()) {
        case ThingSetEncodedNodeType::list:
            tree.kind = ValueTree::Kind::list;
            return decoder.decodeList([&](size_t) {
                tree.items.emplace_back();
                return decodeTree(decoder, tree.items.back());
            });
        case ThingSetEncodedNodeType::map:
            tree.kind = ValueTree::Kind::map;
            return decoder.decodeMap([&](std::optional<uint32_t> id, std::optional<std::string> name) {
                tree.items.emplace_back();
                ValueTree &item = tree.items.back();
                // ThingSet++ doesn't clear the other optional between keys; see appendMap()
                if (name) {
                    item.name = name;
                } else {
                    item.id = id;
                }
                return decodeTree(decoder, item);
            });
        default:
            tree.kind = ValueTree::Kind::scalar;
            return formatValue(tree.text, decoder);
    }
}

std::string DeviceBrowser::formatTree(const ValueTree &tree)
{
    switch (tree.kind) {
        case ValueTree::Kind::list: {
            std::string text = "[";
            for (size_t i = 0; i < tree.items.size(); i++) {
                text += (i ? "," : "") + formatTree(tree.items[i]);
            }
            return text + "]";
        }
        case ValueTree::Kind::map: {
            std::string text = "{";
            for (size_t i = 0; i < tree.items.size(); i++) {
                const ValueTree &item = tree.items[i];
                text += (i ? "," : "") + formatKey(item.id, item.name) + ":" + formatTree(item);
            }
            return text + "}";
        }
        default:
            return tree.text;
    }
}

void DeviceBrowser::setValue(uint32_t id, const std::string &value)
{
    Node &node = _nodes[id];
    if (node.value != value) {
        node.value = value;
        if (onValueChanged) {
            onValueChanged(id);
        }
    }
}

void DeviceBrowser::setExpanded(uint32_t id, bool expanded)
{
    auto found = _nodes.find(id);
    if (found == _nodes.end() || !found->second.isExpandable() || found->second.expanded == expanded) {
        return;
    }
    Node &node = found->second;
    node.expanded = expanded;
    if (expanded && node.isRecord()) {
        // see whether the device will name the record's members (ThingSet++ won't)
        if (node.kind == Node::Kind::item && !node.childrenLoaded && !_loading.count(id)) {
            _loading[id] = Loading{};
            enqueue(Purpose::childIds, id);
        }
        if (node.elementsByPath) {
            requestElements(id);
        } else if (node.kind == Node::Kind::item && node.value.empty()) {
            enqueue(Purpose::value, id);
        }
    } else if (expanded) {
        if (!node.childrenLoaded) {
            if (!_loading.count(id)) {
                _loading[id] = Loading{};
                enqueue(Purpose::childIds, id);
            }
        } else {
            requestValues(id);
        }
    }
    if (onStructureChanged) {
        onStructureChanged();
    }
}
