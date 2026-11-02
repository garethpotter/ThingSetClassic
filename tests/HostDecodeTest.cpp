/*
 * Host-side test of report reception and formatting: feeds datagrams through
 * OpenTransportSubscriptionTransport::poll() via the Open Transport stubs.
 */
#include <cstdio>
#include <string>
#include <vector>

#include "OpenTransportSubscriptionTransport.hpp"
#include "ReportFormatter.hpp"

static constexpr uint32_t hostA = 0x0A000005; // 10.0.0.5
static constexpr uint32_t hostB = 0xC0A80117; // 192.168.1.23

static int failures;

static void expect(const std::string &actual, const std::string &expected, const char *what)
{
    if (actual != expected) {
        printf("FAIL %s\n  expected: %s\n  actual:   %s\n", what, expected.c_str(), actual.c_str());
        failures++;
    } else {
        printf("ok   %s\n", what);
    }
}

// Renders a report as one line, e.g. `10.0.0.5:9002 subset 0x1 {0x300:24.5}`.
static std::string describeReport(const ReportSender &sender, ThingSet::ThingSetBinaryDecoder &decoder)
{
    std::string line = formatAddress(sender.host) + ":" + std::to_string(sender.port);
    uint32_t subsetId = 0;
    std::string entries;
    ReportStatus status = decodeReport(sender, decoder, subsetId,
                                       [&](std::optional<uint32_t> id, const std::optional<std::string> &name,
                                           const std::string &value) {
                                           entries += entries.empty() ? "" : ",";
                                           entries += formatKey(id, name) + ":" + value;
                                       });
    switch (status) {
        case ReportStatus::unsupportedType:
            return line + " report type " + formatHex(sender.reportType) + " (not decoded)";
        case ReportStatus::malformed:
            return line + " malformed report";
        case ReportStatus::truncated:
            entries += " (truncated)";
            break;
        case ReportStatus::ok:
            break;
    }
    return line + " subset " + formatHex(subsetId) + " {" + entries + "}";
}

static void send(uint32_t host, uint8_t header, const std::vector<uint8_t> &body)
{
    StubDatagram datagram{ host, 9002, { header, 0x00 } };
    datagram.data.insert(datagram.data.end(), body.begin(), body.end());
    stubDatagrams.push_back(datagram);
}

static void appendUint16Key(std::vector<uint8_t> &out, uint16_t id)
{
    out.insert(out.end(), { 0x19, (uint8_t)(id >> 8), (uint8_t)id });
}

int main()
{
    std::vector<std::string> lines;
    OpenTransportSubscriptionTransport transport;
    if (!transport.subscribe([&](const ReportSender &sender, ThingSet::ThingSetBinaryDecoder &decoder) {
            lines.push_back(describeReport(sender, decoder));
        })) {
        printf("FAIL subscribe\n");
        return 1;
    }

    // single-frame report with assorted value types
    std::vector<uint8_t> single = { 0x1F, 0x01, 0xA6 };
    appendUint16Key(single, 0x300);
    single.insert(single.end(), { 0xFA, 0x41, 0xC4, 0x00, 0x00 }); // 24.5f
    appendUint16Key(single, 0x301);
    single.push_back(0xF5); // true
    appendUint16Key(single, 0x302);
    single.push_back(0x24); // -5
    appendUint16Key(single, 0x303);
    single.insert(single.end(), { 0x62, 'o', 'k' });
    appendUint16Key(single, 0x304);
    single.insert(single.end(), { 0x82, 0x01, 0x02 });
    appendUint16Key(single, 0x305);
    single.insert(single.end(), { 0xA1, 0x01, 0xF6 }); // {1: null}
    send(hostA, 0x30, single);
    transport.poll();
    expect(lines.empty() ? "" : lines.back(),
           "10.0.0.5:9002 subset 0x1 {0x300:24.5,0x301:true,0x302:-5,0x303:\"ok\",0x304:[1,2],0x305:{0x1:null}}",
           "single-frame report");

    // two-frame report, interleaved with a single-frame report from another sender
    std::vector<uint8_t> large = { 0x1F, 0x00, 0xB8, 100 };
    std::string expected = "10.0.0.5:9002 subset 0x0 {";
    for (uint32_t i = 0; i < 100; i++) {
        appendUint16Key(large, 0x1000 + i);
        uint32_t value = i * 1000;
        if (value < 24) {
            large.push_back(value);
        } else if (value < 0x10000) {
            large.insert(large.end(), { 0x19, (uint8_t)(value >> 8), (uint8_t)value });
        } else {
            large.insert(large.end(),
                         { 0x1A, (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value });
        }
        char entry[32];
        snprintf(entry, sizeof(entry), "%s0x%X:%u", i ? "," : "", 0x1000 + i, value);
        expected += entry;
    }
    expected += '}';
    const size_t frameData = THINGSET_STREAMING_MSG_SIZE - THINGSET_STREAMING_HEADER_SIZE;
    send(hostA, 0x00, std::vector<uint8_t>(large.begin(), large.begin() + frameData));
    send(hostB, 0x30, { 0x1F, 0x02, 0xA1, 0x19, 0x07, 0x00, 0xF4 });
    send(hostA, 0x21, std::vector<uint8_t>(large.begin() + frameData, large.end()));
    lines.clear();
    transport.poll();
    expect(lines.size() == 2 ? lines[0] : "", "192.168.1.23:9002 subset 0x2 {0x700:false}", "interleaved sender");
    expect(lines.size() == 2 ? lines[1] : "", expected, "two-frame report");

    // enhanced reports are identified but not decoded
    send(hostB, 0x30, { 0x1E, 0x48, 1, 2, 3, 4, 5, 6, 7, 8, 0x00, 0xA0 });
    lines.clear();
    transport.poll();
    expect(lines.empty() ? "" : lines.back(), "192.168.1.23:9002 report type 0x1E (not decoded)", "enhanced report");

    // oversized datagrams are dropped without disturbing later reports
    stubDatagrams.push_back({ hostA, 9002, std::vector<uint8_t>(1500, 0x30) });
    send(hostA, 0x30, { 0x1F, 0x01, 0xA1, 0x19, 0x03, 0x00, 0x01 });
    lines.clear();
    transport.poll();
    expect(lines.empty() ? "" : lines.back(), "10.0.0.5:9002 subset 0x1 {0x300:1}", "report after oversized datagram");
    expect(std::to_string(transport.droppedCount()), "1", "oversized datagram counted");

    printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
