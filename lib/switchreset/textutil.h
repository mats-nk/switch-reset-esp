// Small text helpers (no regex: the ESP32 build stays small and stack-friendly).
#pragma once
#include <stddef.h>
#include <string>
#include "profiles.h"

namespace tu {

std::string toLower(std::string s);
std::string rtrim(const std::string& s);
std::string trim(const std::string& s);
bool endsWith(const std::string& s, const char* suffix);
std::string lastN(const std::string& s, size_t n);
std::string repr(const std::string& s);  // Python-like quoted representation for log messages

// Case-insensitive pattern search; `lowerHay` must already be lower case. See Rule in profiles.h.
bool globFind(const std::string& lowerHay, const char* pattern);

bool endsWithPrompt(const std::string& s, PromptKind kind);  // normal CLI prompt at end of text
bool endsWithUnpriv(const std::string& s);                    // "name>" user-mode prompt (needs enable)
bool endsWithShell(const std::string& s);                     // Junos shell prompt "user@host:RE:0%"
bool endsWithBootPrompt(const std::string& s);                // Cisco bootloader prompt "switch:"
bool isRommon(const std::string& s);                          // bootloader / ROMMON prompt
bool endsWithUserPrompt(const std::string& s);                // "Username:" / "login:"
bool endsWithPassPrompt(const std::string& s);                // "Password:"
bool containsError(const std::string& s, std::string* matched);
bool containsLoginFail(const std::string& s);
bool isReadable(const std::string& s);  // >= 80 % printable -> baud rate is probably right

// Byte for the web terminal: returns -1 for bytes that should be dropped, else a safe byte.
int sanitizeForTerminal(int b);

// Streaming filter that strips ANSI escape sequences and CR.
class AnsiFilter {
 public:
  void feed(int b, std::string& out);

 private:
  int st_ = 0;
};

}  // namespace tu
