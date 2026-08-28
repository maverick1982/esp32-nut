#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <cstring>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cctype>

class String : public std::string {
public:
    String() : std::string() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(int val) : std::string(std::to_string(val)) {}
    String(unsigned int val) : std::string(std::to_string(val)) {}
    String(long val) : std::string(std::to_string(val)) {}
    String(unsigned long val) : std::string(std::to_string(val)) {}
    String(float val, int decimals = 2) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(decimals) << val;
        *this = ss.str();
    }
    String(double val, int decimals = 2) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(decimals) << val;
        *this = ss.str();
    }

    int indexOf(char c) const {
        size_t pos = find(c);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    int indexOf(const String& s) const {
        size_t pos = find(s);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    int indexOf(const char* s) const {
        size_t pos = find(s);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    String substring(unsigned int from, unsigned int to) const {
        if (from >= length()) return "";
        return substr(from, to - from);
    }

    String substring(unsigned int from) const {
        if (from >= length()) return "";
        return substr(from);
    }

    void trim() {
        while (!empty() && isspace(front())) erase(begin());
        while (!empty() && isspace(back())) pop_back();
    }

    int toInt() const {
        try {
            return std::stoi(*this);
        } catch (...) {
            return 0;
        }
    }

    float toFloat() const {
        try {
            return std::stof(*this);
        } catch (...) {
            return 0.0f;
        }
    }

    void toLowerCase() {
        std::transform(begin(), end(), begin(), [](unsigned char c){ return std::tolower(c); });
    }

    void toUpperCase() {
        std::transform(begin(), end(), begin(), [](unsigned char c){ return std::toupper(c); });
    }

    bool equalsIgnoreCase(const String& s) const {
        if (length() != s.length()) return false;
        for (size_t i = 0; i < length(); i++) {
            if (std::tolower((unsigned char)(*this)[i]) != std::tolower((unsigned char)s[i])) return false;
        }
        return true;
    }

    bool startsWith(const String& prefix) const {
        if (prefix.length() > length()) return false;
        return substr(0, prefix.length()) == prefix;
    }

    bool endsWith(const String& suffix) const {
        if (suffix.length() > length()) return false;
        return substr(length() - suffix.length()) == suffix;
    }

    char charAt(unsigned int index) const {
        if (index >= length()) return 0;
        return (*this)[index];
    }
};

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        while (size--) {
            if (write(*buffer++)) n++;
            else break;
        }
        return n;
    }

    size_t print(const char* s) {
        if (!s) return 0;
        return write((const uint8_t*)s, strlen(s));
    }

    size_t print(const String& s) {
        return write((const uint8_t*)s.c_str(), s.length());
    }

    size_t print(int n) {
        return print(String(n));
    }

    size_t println(const char* s = "") {
        size_t n = print(s);
        n += print("\n");
        return n;
    }

    size_t println(const String& s) {
        size_t n = print(s);
        n += print("\n");
        return n;
    }

    size_t println(int n) {
        return println(String(n));
    }

    template<typename... Args>
    size_t printf(const char* format, Args... args) {
        char buf[512];
        int len = snprintf(buf, sizeof(buf), format, args...);
        if (len > 0) {
            return write((const uint8_t*)buf, len);
        }
        return 0;
    }
};

class SerialMock : public Print {
public:
    size_t write(uint8_t c) override {
        std::cout << (char)c;
        return 1;
    }
    size_t write(const uint8_t *buffer, size_t size) override {
        if (buffer && size > 0) {
            std::cout.write((const char*)buffer, size);
        }
        return size;
    }
};

static SerialMock Serial;

inline uint32_t millis() {
    return 0;
}

inline void delay(uint32_t ms) {}

#endif // MOCK_ARDUINO_H
