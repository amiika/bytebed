#pragma once
#include <string>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <vector>
#include <map>

// Minimal mock for Arduino String to make compiler.cpp compile in standard C++
class String {
private:
    std::string str;
public:
    String() : str("") {}
    String(const char* c) : str(c ? c : "") {}
    String(char c) : str(1, c) {}
    String(int val) : str(std::to_string(val)) {}
    String(const std::string& s) : str(s) {}

    const char* c_str() const { return str.c_str(); }
    int length() const { return str.length(); }
    void trim() {
        str.erase(0, str.find_first_not_of(" \t\r\n"));
        str.erase(str.find_last_not_of(" \t\r\n") + 1);
    }
    bool startsWith(const char* prefix) const { return str.find(prefix) == 0; }
    bool endsWith(const char* suffix) const {
        if (str.length() >= strlen(suffix)) return str.compare(str.length() - strlen(suffix), strlen(suffix), suffix) == 0;
        return false;
    }
    String substring(int from, int to = -1) const {
        if (to == -1) return String(str.substr(from));
        return String(str.substr(from, to - from));
    }
    int toInt() const { return std::atoi(str.c_str()); }
    void remove(int index, int count) { str.erase(index, count); }

    bool operator==(const char* other) const { return str == other; }
    bool operator==(const String& other) const { return str == other.str; }
    bool operator!=(const char* other) const { return str != other; }
    
    // Required by std::map to sort keys
    bool operator<(const String& other) const { return str < other.str; } 

    // --- STR MATH OPERATORS ---
    String operator+(const String& other) const { return String(str + other.str); }
    String operator+(const char* other) const { return String(str + other); }
    String operator+(char c) const { return String(str + c); }
    
    // The missing += operators that caused the crash!
    String& operator+=(const String& other) { str += other.str; return *this; }
    String& operator+=(const char* other) { str += other; return *this; }
    String& operator+=(char c) { str += c; return *this; }
    
    char operator[](int index) const { return str[index]; }
};

inline String operator+(const char* lhs, const String& rhs) { return String(lhs) + rhs; }

// Stub out hardware-specific macros
#define IRAM_ATTR