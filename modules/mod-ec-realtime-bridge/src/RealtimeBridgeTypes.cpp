/*
 * mod-ec-realtime-bridge - Type implementations
 * Copyright (C) 2025 EC
 */

#include "RealtimeBridgeTypes.h"
#include <sstream>
#include <cctype>

namespace ECRealtimeBridge
{

// ============================================================================
// JSON BUILDING
// ============================================================================

std::string BridgeEvent::ToJson() const
{
    std::ostringstream ss;
    ss << "{\"ns\":\"" << NamespaceToString(ns) << "\","
       << "\"type\":\"" << EscapeJsonString(type) << "\","
       << "\"ts\":" << timestamp << ","
       << "\"data\":" << (jsonPayload.empty() ? "{}" : jsonPayload)
       << "}";
    return ss.str();
}

// ============================================================================
// JSON UTILITIES
// ============================================================================

std::string EscapeJsonString(const std::string& input)
{
    std::string output;
    output.reserve(input.size() + 16);

    for (char c : input)
    {
        switch (c)
        {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    output += buf;
                }
                else
                {
                    output += c;
                }
                break;
        }
    }

    return output;
}

std::string ExtractJsonString(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\":\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos)
        return "";

    size_t valueStart = keyPos + searchKey.length();
    size_t valueEnd = valueStart;

    // Find end of string value, handling escapes
    bool escaped = false;
    while (valueEnd < json.length())
    {
        char c = json[valueEnd];
        if (escaped)
        {
            escaped = false;
        }
        else if (c == '\\')
        {
            escaped = true;
        }
        else if (c == '"')
        {
            break;
        }
        valueEnd++;
    }

    return json.substr(valueStart, valueEnd - valueStart);
}

int64_t ExtractJsonInt(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\":";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos)
        return 0;

    size_t valueStart = keyPos + searchKey.length();

    // Skip whitespace
    while (valueStart < json.length() && std::isspace(json[valueStart]))
        valueStart++;

    // Handle quoted numbers
    if (valueStart < json.length() && json[valueStart] == '"')
        valueStart++;

    // Parse number
    bool negative = false;
    if (valueStart < json.length() && json[valueStart] == '-')
    {
        negative = true;
        valueStart++;
    }

    int64_t result = 0;
    while (valueStart < json.length() && std::isdigit(json[valueStart]))
    {
        result = result * 10 + (json[valueStart] - '0');
        valueStart++;
    }

    return negative ? -result : result;
}

double ExtractJsonDouble(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\":";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos)
        return 0.0;

    size_t valueStart = keyPos + searchKey.length();

    // Skip whitespace
    while (valueStart < json.length() && std::isspace(json[valueStart]))
        valueStart++;

    // Find end of number
    size_t valueEnd = valueStart;
    while (valueEnd < json.length() &&
           (std::isdigit(json[valueEnd]) || json[valueEnd] == '.' ||
            json[valueEnd] == '-' || json[valueEnd] == '+' ||
            json[valueEnd] == 'e' || json[valueEnd] == 'E'))
    {
        valueEnd++;
    }

    try
    {
        return std::stod(json.substr(valueStart, valueEnd - valueStart));
    }
    catch (...)
    {
        return 0.0;
    }
}

bool ExtractJsonBool(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\":";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos)
        return false;

    size_t valueStart = keyPos + searchKey.length();

    // Skip whitespace
    while (valueStart < json.length() && std::isspace(json[valueStart]))
        valueStart++;

    // Check for true/false
    if (json.substr(valueStart, 4) == "true")
        return true;

    return false;
}

} // namespace ECRealtimeBridge
