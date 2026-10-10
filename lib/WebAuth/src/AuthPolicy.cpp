#include "AuthPolicy.h"
#include <string.h>

bool AuthPolicy::isPublicRoute(const String& path, bool is_get) {
    if (is_get) {
        return path == "/login" || path == "/api/auth/status";
    }
    return path == "/api/auth/login" || path == "/api/auth/credentials" || path == "/api/auth/logout";
}

AuthDecision AuthPolicy::decide(const String& path, bool is_get, bool ap_mode, bool configured, bool authenticated) {
    if (ap_mode) return AuthDecision::ALLOW;
    if (isPublicRoute(path, is_get)) return AuthDecision::ALLOW;
    if (authenticated) return AuthDecision::ALLOW;
    // Solo le pagine vengono reindirizzate: API e POST /update ricevono un errore
    if (is_get && !path.startsWith("/api/")) return AuthDecision::REDIRECT_LOGIN;
    return configured ? AuthDecision::UNAUTHORIZED : AuthDecision::SETUP_REQUIRED;
}

String AuthPolicy::extractCookie(const String& header, const char* name) {
    if (!name) return String();
    const size_t name_len = strlen(name);
    const size_t len = header.length();
    size_t pos = 0;
    while (pos < len) {
        // Un cookie per segmento separato da ';'
        size_t end = pos;
        while (end < len && header[end] != ';') end++;

        size_t start = pos;
        while (start < end && header[start] == ' ') start++;
        size_t eq = start;
        while (eq < end && header[eq] != '=') eq++;
        if (eq < end) {
            size_t key_end = eq;
            while (key_end > start && header[key_end - 1] == ' ') key_end--;
            if (key_end - start == name_len && strncmp(header.c_str() + start, name, name_len) == 0) {
                size_t v_start = eq + 1;
                size_t v_end = end;
                while (v_start < v_end && header[v_start] == ' ') v_start++;
                while (v_end > v_start && header[v_end - 1] == ' ') v_end--;
                return header.substring(v_start, v_end);
            }
        }
        pos = end + 1;
    }
    return String();
}
