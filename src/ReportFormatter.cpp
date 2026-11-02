/*
 * Decodes ThingSet reports into text.
 */
#include "ReportFormatter.hpp"

#include <cstdio>

#include "thingset++/ThingSetStatus.hpp"

using namespace ThingSet;

static void appendUnsigned(std::string &out, uint64_t value)
{
    // Formatted by hand: 64-bit printf support isn't guaranteed in the 68k libc.
    char digits[21];
    int i = sizeof(digits);
    digits[--i] = '\0';
    do {
        digits[--i] = '0' + (value % 10);
        value /= 10;
    } while (value != 0);
    out += &digits[i];
}

static void appendSigned(std::string &out, int64_t value)
{
    if (value < 0) {
        out += '-';
        appendUnsigned(out, -(uint64_t)value);
    } else {
        appendUnsigned(out, value);
    }
}

static void appendHex(std::string &out, uint32_t value)
{
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "0x%lX", (unsigned long)value);
    out += buffer;
}

static void appendDouble(std::string &out, double value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.6g", value);
    out += buffer;
}

static bool appendValue(std::string &out, ThingSetBinaryDecoder &decoder);

static bool appendMap(std::string &out, ThingSetBinaryDecoder &decoder)
{
    bool first = true;
    out += '{';
    bool ok = decoder.decodeMap([&](std::optional<uint32_t> id, std::optional<std::string> name) {
        if (!first) {
            out += ',';
        }
        first = false;
        // ThingSet++ doesn't clear the other optional between keys, so prefer
        // the name: binary reports key by ID, so a name means a text key.
        if (name) {
            out += '"';
            out += *name;
            out += '"';
        } else if (id) {
            appendHex(out, *id);
        }
        out += ':';
        return appendValue(out, decoder);
    });
    out += '}';
    return ok;
}

static bool appendList(std::string &out, ThingSetBinaryDecoder &decoder)
{
    out += '[';
    bool ok = decoder.decodeList([&](size_t index) {
        if (index != 0) {
            out += ',';
        }
        return appendValue(out, decoder);
    });
    out += ']';
    return ok;
}

static bool appendString(std::string &out, ThingSetBinaryDecoder &decoder)
{
    std::string text;
    if (decoder.decode(&text)) {
        out += '"';
        out += text;
        out += '"';
        return true;
    }

    uint8_t bytes[64];
    size_t size;
    if (decoder.decodeBytes(bytes, sizeof(bytes), size)) {
        char hex[3];
        out += "h'";
        for (size_t i = 0; i < size; i++) {
            snprintf(hex, sizeof(hex), "%02X", bytes[i]);
            out += hex;
        }
        out += '\'';
        return true;
    }

    // Byte string too long to show; skip over it.
    out += "h'...'";
    return decoder.skip();
}

static bool appendPrimitive(std::string &out, ThingSetBinaryDecoder &decoder)
{
    // zcbor leaves the stream untouched when a decode fails, so try each
    // encoding in turn.
    int64_t integer;
    if (decoder.decode(&integer)) {
        appendSigned(out, integer);
        return true;
    }

    uint64_t unsignedInteger;
    if (decoder.decode(&unsignedInteger)) {
        appendUnsigned(out, unsignedInteger);
        return true;
    }

    bool boolean;
    if (decoder.decode(&boolean)) {
        out += boolean ? "true" : "false";
        return true;
    }

    float single;
    if (decoder.decode(&single)) {
        appendDouble(out, single);
        return true;
    }

    double dbl;
    if (decoder.decode(&dbl)) {
        appendDouble(out, dbl);
        return true;
    }

    if (decoder.decodeNull()) {
        out += "null";
        return true;
    }

    out += '?';
    return decoder.skip();
}

static bool appendValue(std::string &out, ThingSetBinaryDecoder &decoder)
{
    switch (decoder.peekType()) {
        case ThingSetEncodedNodeType::map:
            return appendMap(out, decoder);
        case ThingSetEncodedNodeType::list:
            return appendList(out, decoder);
        case ThingSetEncodedNodeType::string:
            return appendString(out, decoder);
        case ThingSetEncodedNodeType::primitive:
            return appendPrimitive(out, decoder);
        default:
            // e.g. CBOR tags, which ThingSet doesn't use in reports
            out += '?';
            return decoder.skip();
    }
}

bool formatValue(std::string &out, ThingSetBinaryDecoder &decoder)
{
    return appendValue(out, decoder);
}

std::string formatAddress(InetHost host)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", (unsigned)((host >> 24) & 0xFF), (unsigned)((host >> 16) & 0xFF),
             (unsigned)((host >> 8) & 0xFF), (unsigned)(host & 0xFF));
    return buffer;
}

std::string formatKey(std::optional<uint32_t> id, const std::optional<std::string> &name)
{
    return name ? *name : formatHex(id.value_or(0));
}

std::string formatHex(uint32_t value)
{
    std::string text;
    appendHex(text, value);
    return text;
}

ReportStatus decodeReport(const ReportSender &sender, ThingSetBinaryDecoder &decoder, uint32_t &subsetId,
                          const ReportEntryCallback &entry)
{
    if (sender.reportType != (uint8_t)ThingSetBinaryRequestType::report) {
        // Enhanced reports prefix the body with the sender's EUI-64, which
        // ThingSet++'s UDP decoder doesn't yet allow for, so don't decode them.
        return ReportStatus::unsupportedType;
    }

    if (!decoder.decode(&subsetId)) {
        return ReportStatus::malformed;
    }

    std::string value;
    bool ok = decoder.decodeMap([&](std::optional<uint32_t> id, std::optional<std::string> name) {
        value.clear();
        if (!appendValue(value, decoder)) {
            return false;
        }
        // ThingSet++ doesn't clear the other optional between keys; see appendMap()
        if (name) {
            entry(std::nullopt, name, value);
        } else {
            entry(id, std::nullopt, value);
        }
        return true;
    });
    return ok ? ReportStatus::ok : ReportStatus::truncated;
}
