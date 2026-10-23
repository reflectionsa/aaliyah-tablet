// The plugin's options. nEVR passes them as a flat JSON object whose values are all strings:
// {"log_states":"false","limits.max":"5"}. This reads exactly that shape and nothing more.
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "mod.h"

namespace mod {

namespace {

std::map<std::string, std::string> g_args;

// A JSON string at `p` (just after its opening quote); `p` ends after the closing quote.
bool ReadString(const char*& p, std::string& out) {
    out.clear();
    while (*p && *p != '"') {
        if (*p != '\\') {
            out += *p++;
            continue;
        }
        p++;
        switch (*p) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                // \uXXXX as UTF-8 (a surrogate pair's halves each on their own: rare in options).
                char hex[5] = {};
                for (int i = 0; i < 4; i++) {
                    if (!p[1 + i]) return false;
                    hex[i] = p[1 + i];
                }
                unsigned c = static_cast<unsigned>(strtoul(hex, nullptr, 16));
                p += 4;
                if (c < 0x80) {
                    out += static_cast<char>(c);
                } else if (c < 0x800) {
                    out += static_cast<char>(0xC0 | (c >> 6));
                    out += static_cast<char>(0x80 | (c & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (c >> 12));
                    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (c & 0x3F));
                }
                break;
            }
            default: return false;
        }
        p++;
    }
    if (*p != '"') return false;
    p++;
    return true;
}

void SkipSpace(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
}

}  // namespace

void SetArgs(const char* json) {
    g_args.clear();
    const char* p = json;
    SkipSpace(p);
    if (*p++ != '{') return;
    for (;;) {
        SkipSpace(p);
        if (*p == '}' || !*p) return;
        std::string key, value;
        if (*p++ != '"' || !ReadString(p, key)) return;
        SkipSpace(p);
        if (*p++ != ':') return;
        SkipSpace(p);
        if (*p++ != '"' || !ReadString(p, value)) return;
        g_args[key] = value;
        SkipSpace(p);
        if (*p == ',') p++;
    }
}

std::string Arg(const char* key, const char* fallback) {
    auto it = g_args.find(key);
    return it == g_args.end() ? std::string(fallback) : it->second;
}

bool ArgBool(const char* key, bool fallback) {
    auto it = g_args.find(key);
    if (it == g_args.end()) return fallback;
    const std::string& v = it->second;
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return fallback;
}

long long ArgNumber(const char* key, long long min, long long max, long long fallback) {
    auto it = g_args.find(key);
    if (it == g_args.end() || it->second.empty()) return fallback;
    char* end = nullptr;
    long long n = strtoll(it->second.c_str(), &end, 10);
    if (!end || *end) return fallback;
    return n < min ? min : n > max ? max : n;
}

}  // namespace mod
