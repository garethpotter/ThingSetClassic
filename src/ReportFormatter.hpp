/*
 * Decodes ThingSet reports into text without needing to know the sending
 * device's data model.
 */
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "OpenTransportSubscriptionTransport.hpp"

enum struct ReportStatus
{
    ok,
    /// @brief Not a plain report, e.g. an enhanced report.
    unsupportedType,
    /// @brief The subset ID couldn't be read.
    malformed,
    /// @brief Decoding stopped part way through; earlier entries were delivered.
    truncated,
};

/// @brief Receives one top-level entry of a report. Exactly one of id and name is set.
using ReportEntryCallback = std::function<void(std::optional<uint32_t> id, const std::optional<std::string> &name,
                                               const std::string &value)>;

/// @brief Decodes a report, rendering each value as text such as `24.5`, `"ok"`, `[1,2]` or `{0x1:null}`.
/// @param sender The report's sender.
/// @param decoder A decoder positioned at the start of the report body.
/// @param subsetId Receives the report's subset ID.
/// @param entry Invoked for each entry in the report.
ReportStatus decodeReport(const ReportSender &sender, ThingSet::ThingSetBinaryDecoder &decoder, uint32_t &subsetId,
                          const ReportEntryCallback &entry);

/// @brief Renders the next value from a decoder as text, as decodeReport() does.
/// @return False if the value couldn't be decoded.
bool formatValue(std::string &out, ThingSet::ThingSetBinaryDecoder &decoder);

/// @brief Formats an address as dotted decimal, e.g. `10.0.0.5`.
std::string formatAddress(InetHost host);

/// @brief Formats a key as hex (`0x300`) or, for a named key, the name.
std::string formatKey(std::optional<uint32_t> id, const std::optional<std::string> &name);

/// @brief Formats a number as hex, e.g. `0x1E`.
std::string formatHex(uint32_t value);
