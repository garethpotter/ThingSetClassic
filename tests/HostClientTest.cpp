/*
 * Host-side test of DeviceBrowser against ThingSet++'s own server, connected
 * through the Open Transport stubs. Responses are delivered a few bytes at a
 * time, as TCP may do.
 */
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "thingset++/ThingSetGroup.hpp"
#include "thingset++/ThingSetRecordMember.hpp"
#include "thingset++/ThingSetServer.hpp"

#include "DeviceBrowser.hpp"

using namespace ThingSet;

namespace {

class TestServer : public _ThingSetServer
{
public:
    TestServer() : _ThingSetServer(nullptr)
    {}

    bool listen() override
    {
        return true;
    }

    using _ThingSetServer::handleBinaryRequest;
};

ThingSetGroup<0x100, 0, "Battery"> battery;
ThingSetReadOnlyProperty<float> voltage{ 0x101, 0x100, "Voltage", 24.5f };
ThingSetReadWriteProperty<bool> enabled{ 0x102, 0x100, "Enabled", true };
ThingSetGroup<0x110, 0x100, "Cells"> cells;
ThingSetReadOnlyProperty<uint16_t> cellCount{ 0x111, 0x110, "Count", 16 };
ThingSetReadWriteProperty<uint32_t> interval{ 0x10, 0, "Interval", 1000 };

struct CellRecord
{
    ThingSetReadOnlyRecordMember<0x211, 0x210, "Soc", float> soc;
};

struct ModuleRecord
{
    ThingSetReadOnlyRecordMember<0x201, 0x200, "Voltage", float> voltage;
    ThingSetReadOnlyRecordMember<0x202, 0x200, "Error", uint32_t> error;
    ThingSetReadOnlyRecordMember<0x210, 0x200, "Cells", std::array<CellRecord, 2>> cells;
};

ThingSetReadOnlyProperty<std::array<ModuleRecord, 2>> modules{ 0x200, 0, "Modules" };

void setUpModules()
{
    modules[0].voltage = 24.0f;
    modules[0].error = 0u;
    modules[0].cells[0].soc = 0.5f;
    modules[0].cells[1].soc = 0.75f;
    modules[1].voltage = 24.2f;
    modules[1].error = 3u;
    modules[1].cells[0].soc = 0.25f;
    modules[1].cells[1].soc = 1.0f;
}

constexpr InetHost deviceHost = 0x0A000009; // 10.0.0.9
constexpr uint32_t notFound = 0xFFFFFFFF;

TestServer server;
size_t serverBufferSize;
UInt32 ticks;
int failures;

void expect(const std::string &actual, const std::string &expected, const char *what)
{
    if (actual != expected) {
        printf("FAIL %s\n  expected: %s\n  actual:   %s\n", what, expected.c_str(), actual.c_str());
        failures++;
    } else {
        printf("ok   %s\n", what);
    }
}

void runUntilIdle(DeviceBrowser &browser)
{
    for (int i = 0; i < 2000; i++) {
        browser.poll(ticks);
        ticks += 3;
        if (!browser.busy() && browser.state() != DeviceBrowser::State::connecting) {
            return;
        }
    }
}

uint32_t findChild(const DeviceBrowser &browser, uint32_t parent, const std::string &name)
{
    for (uint32_t id : browser.node(parent)->children) {
        if (browser.node(id)->name == name) {
            return id;
        }
    }
    return notFound;
}

std::string childNames(const DeviceBrowser &browser, uint32_t parent)
{
    std::string names;
    for (uint32_t id : browser.node(parent)->children) {
        names += names.empty() ? "" : ",";
        names += browser.node(id)->name;
    }
    return names;
}

std::string valueOf(const DeviceBrowser &browser, uint32_t id)
{
    const DeviceBrowser::Node *node = browser.node(id);
    return node ? node->value : "<missing>";
}

void reset(size_t bufferSize, size_t chunkSize)
{
    serverBufferSize = bufferSize;
    stubTcpChunkSize = chunkSize;
    stubTcpRefuse = false;
    stubTcpServer = [](const uint8_t *request, size_t length) {
        std::vector<uint8_t> copy(request, request + length);
        std::vector<uint8_t> response(serverBufferSize);
        int responseLength = server.handleBinaryRequest(copy.data(), length, response.data(), response.size());
        // set TRACE=1 to see the requests and the server's responses
        if (getenv("TRACE")) {
            printf("TRACE req");
            for (size_t i = 0; i < length; i++) printf(" %02x", request[i]);
            printf(" -> %d bytes, status %02x\n", responseLength, responseLength > 0 ? response[0] : 0);
        }
        response.resize(responseLength > 0 ? responseLength : 0);
        return response;
    };
}

void testBrowse()
{
    reset(1024, 5);
    voltage = 24.5f;
    DeviceBrowser browser(deviceHost, "test");
    browser.start();
    runUntilIdle(browser);

    expect(browser.statusText().substr(0, 21), "Connected to 10.0.0.9", "connects");
    const DeviceBrowser::Node *root = browser.node(DeviceBrowser::rootId);
    expect(root->childrenLoaded ? "loaded" : "not loaded", "loaded", "root loaded on connect");
    expect(findChild(browser, DeviceBrowser::rootId, "_Metadata") == notFound ? "hidden" : "shown", "hidden",
           "internal items hidden");

    uint32_t batteryId = findChild(browser, DeviceBrowser::rootId, "Battery");
    uint32_t intervalId = findChild(browser, DeviceBrowser::rootId, "Interval");
    expect(batteryId == 0x100 ? "found" : "missing", "found", "group found by name");
    expect(browser.node(batteryId)->type, "group", "group type from metadata");
    expect(browser.node(batteryId)->childrenLoaded ? "loaded" : "not loaded", "not loaded", "groups load lazily");
    expect(browser.node(intervalId)->type, "u32", "property type from metadata");
    expect(std::to_string(browser.node(intervalId)->access), std::to_string(0x77), "access from metadata");
    expect(valueOf(browser, intervalId), "1000", "root value read");
    expect(valueOf(browser, batteryId), "", "group has no value of its own");

    browser.setExpanded(batteryId, true);
    runUntilIdle(browser);
    expect(childNames(browser, batteryId), "Voltage,Enabled,Cells", "group children in order");
    expect(browser.node(0x101)->path, "Battery/Voltage", "child path");
    expect(valueOf(browser, 0x101), "24.5", "child float value");
    expect(valueOf(browser, 0x102), "true", "child bool value");

    browser.setExpanded(0x110, true);
    runUntilIdle(browser);
    expect(valueOf(browser, 0x111), "16", "nested group value");

    std::vector<uint32_t> changed;
    browser.onValueChanged = [&](uint32_t id) { changed.push_back(id); };
    voltage = 25.25f;
    ticks += 600;
    runUntilIdle(browser);
    expect(changed.empty() ? "unchanged" : "changed", "unchanged", "no refresh until asked");
    browser.refresh();
    runUntilIdle(browser);
    expect(valueOf(browser, 0x101), "25.25", "refresh picks up new value");
    expect(changed.size() == 1 && changed[0] == 0x101 ? "only voltage" : std::to_string(changed.size()) + " changes",
           "only voltage", "change notification");

    browser.setExpanded(batteryId, false);
    expect(browser.node(batteryId)->expanded ? "expanded" : "collapsed", "collapsed", "collapse");
}

void testRecords()
{
    // the root GET carries whole record arrays, so no extra requests are needed
    reset(1024, 5);
    setUpModules();
    DeviceBrowser browser(deviceHost, "test");
    browser.start();
    runUntilIdle(browser);

    expect(browser.node(0x200)->type, "record[]", "records: type");
    expect(valueOf(browser, 0x200), "2 records", "records: count");

    browser.setExpanded(0x200, true);
    runUntilIdle(browser);
    expect(childNames(browser, 0x200), "[0],[1]", "records: elements");
    uint32_t second = browser.node(0x200)->children[1];
    expect(browser.node(second)->path, "Modules/1", "records: element path");

    browser.setExpanded(second, true);
    runUntilIdle(browser);
    // ThingSet++ doesn't name record members, so they're shown by ID
    expect(childNames(browser, second), "0x201,0x202,0x210", "records: members");
    const std::vector<uint32_t> &members = browser.node(second)->children;
    expect(valueOf(browser, members[0]), "24.2", "records: member value");
    expect(valueOf(browser, members[1]), "3", "records: second member value");
    expect(browser.node(members[2])->type, "record[]", "records: nested array type");
    expect(valueOf(browser, members[2]), "2 records", "records: nested array count");

    uint32_t cells = members[2];
    browser.setExpanded(cells, true);
    uint32_t firstCell = browser.node(cells)->children[0];
    browser.setExpanded(firstCell, true);
    runUntilIdle(browser);
    expect(valueOf(browser, browser.node(firstCell)->children[0]), "0.25", "records: nested member value");

    uint32_t voltageMember = members[0];
    modules[1].voltage = 25.5f;
    browser.refresh();
    runUntilIdle(browser);
    expect(valueOf(browser, voltageMember), "25.5", "records: refresh keeps nodes and updates value");
}

void testRecordsByPath()
{
    // with a small response buffer the array is read item by item, which gives
    // only its length, so elements are read by path
    reset(48, 7);
    setUpModules();
    DeviceBrowser browser(deviceHost, "test");
    browser.start();
    runUntilIdle(browser);
    expect(valueOf(browser, 0x200), "2 records", "records by path: count");

    browser.setExpanded(0x200, true);
    runUntilIdle(browser);
    uint32_t second = browser.node(0x200)->children[1];
    browser.setExpanded(second, true);
    runUntilIdle(browser);
    expect(childNames(browser, second), "0x201,0x202,0x210", "records by path: members");
    expect(valueOf(browser, browser.node(second)->children[0]), "24.2", "records by path: member value");

    modules[1].voltage = 26.0f;
    browser.refresh();
    runUntilIdle(browser);
    expect(valueOf(browser, browser.node(second)->children[0]), "26", "records by path: refresh");
}

void testSmallServerBuffer()
{
    // too small for a whole group or a batch of metadata, so the browser
    // falls back to names by path and values one at a time
    reset(48, 7);
    voltage = 24.5f;
    DeviceBrowser browser(deviceHost, "test");
    browser.start();
    runUntilIdle(browser);
    browser.setExpanded(0x100, true);
    runUntilIdle(browser);

    expect(browser.state() == DeviceBrowser::State::connected ? "connected" : browser.statusText(), "connected",
           "small buffer: still connected");
    expect(childNames(browser, 0x100), "Voltage,Enabled,Cells", "small buffer: names");
    expect(valueOf(browser, 0x101), "24.5", "small buffer: value");
}

void testRefused()
{
    reset(1024, 1500);
    stubTcpRefuse = true;
    DeviceBrowser browser(deviceHost, "test");
    browser.start();
    runUntilIdle(browser);
    expect(browser.statusText(), "Connection refused by 10.0.0.9", "refused");
}

void testNoResponse()
{
    reset(1024, 1500);
    stubTcpServer = [](const uint8_t *, size_t) { return std::vector<uint8_t>(); };
    DeviceBrowser browser(deviceHost, "test");
    browser.start();
    runUntilIdle(browser);
    expect(browser.statusText(), "No response from 10.0.0.9", "timeout");
}

} // namespace

int main()
{
    setUpModules();
    testBrowse();
    testSmallServerBuffer();
    testRecords();
    testRecordsByPath();
    testRefused();
    testNoResponse();
    printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
