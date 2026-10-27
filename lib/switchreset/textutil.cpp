#include "textutil.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace tu {

std::string toLower(std::string s) {
  for (size_t i = 0; i < s.size(); i++)
    if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] + 32);
  return s;
}

std::string rtrim(const std::string& s) {
  size_t e = s.size();
  while (e > 0 && isspace((unsigned char)s[e - 1])) e--;
  return s.substr(0, e);
}

std::string trim(const std::string& s) {
  std::string r = rtrim(s);
  size_t b = 0;
  while (b < r.size() && isspace((unsigned char)r[b])) b++;
  return r.substr(b);
}

bool endsWith(const std::string& s, const char* suffix) {
  size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string lastN(const std::string& s, size_t n) { return s.size() > n ? s.substr(s.size() - n) : s; }

std::string repr(const std::string& s) {
  std::string o = "'";
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '\t') o += "\\t";
    else if (c == '\'') o += "\\'";
    else if (c == '\\') o += "\\\\";
    else if (c < 32 || c >= 127) {
      char b[8];
      snprintf(b, sizeof b, "\\x%02x", c);
      o += b;
    } else o += (char)c;
  }
  return o + "'";
}

static bool altMatch(const std::string& hay, const std::string& alt) {
  size_t pos = 0, s = 0;
  for (;;) {
    size_t star = alt.find('*', s);
    std::string part = alt.substr(s, star == std::string::npos ? std::string::npos : star - s);
    if (!part.empty()) {
      size_t f = hay.find(part, pos);
      if (f == std::string::npos) return false;
      pos = f + part.size();
    }
    if (star == std::string::npos) break;
    s = star + 1;
  }
  return true;
}

bool globFind(const std::string& hay, const char* pattern) {
  std::string pat(pattern);
  size_t start = 0;
  for (;;) {
    size_t bar = pat.find('|', start);
    std::string alt = pat.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
    if (!alt.empty() && altMatch(hay, alt)) return true;
    if (bar == std::string::npos) break;
    start = bar + 1;
  }
  return false;
}

// ---- prompt detection (equivalent of the regexes in the Python tool) ------
static bool setGeneric(char c) {  // [\w\-.()/@:~ ]
  return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '(' || c == ')' || c == '/' ||
         c == '@' || c == ':' || c == '~' || c == ' ';
}
static bool setNoSpace(char c) { return c != ' ' && setGeneric(c); }  // [\w\-.()/@:~]
static bool setJuniper(char c) {                                       // [\w\-.@:]
  return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '@' || c == ':';
}

static bool markerAfterSet(const std::string& s, const char* markers, bool (*ok)(char)) {
  std::string t = rtrim(s);
  if (t.size() < 2) return false;
  char last = t[t.size() - 1];
  if (last == 0 || !strchr(markers, last)) return false;
  return ok(t[t.size() - 2]);
}

static bool bracketed(const std::string& t, char open, char close, const char* forbid) {
  if (t.empty() || t[t.size() - 1] != close) return false;
  size_t n = 0;
  for (size_t i = t.size() - 1; i > 0;) {
    i--;
    char c = t[i];
    if (c == open) return n >= 1 && n <= 60;
    if (c == '\n' || c == '\r' || strchr(forbid, c)) return false;
    if (++n > 60) return false;
  }
  return false;
}

bool endsWithPrompt(const std::string& s, PromptKind kind) {
  switch (kind) {
    case PROMPT_JUNIPER: return markerAfterSet(s, ">#", setJuniper);
    case PROMPT_HUAWEI: {
      std::string t = rtrim(s);
      return bracketed(t, '<', '>', "<>") || bracketed(t, '[', ']', "[]");
    }
    default: return markerAfterSet(s, ">#", setGeneric);
  }
}

bool endsWithUnpriv(const std::string& s) { return markerAfterSet(s, ">", setNoSpace); }
bool endsWithShell(const std::string& s) { return markerAfterSet(s, "%", setJuniper); }

bool endsWithBootPrompt(const std::string& s) { return endsWith(toLower(rtrim(s)), "switch:"); }

bool isRommon(const std::string& s) {
  std::string l = toLower(s);
  size_t p = 0;
  while ((p = l.find("rommon ", p)) != std::string::npos) {  // "rommon 1 >"
    size_t q = p + 7, d = q;
    while (d < l.size() && isdigit((unsigned char)l[d])) d++;
    if (d > q) {
      if (d < l.size() && l[d] == ' ') d++;
      if (d < l.size() && l[d] == '>') return true;
    }
    p += 7;
  }
  if (l.find("loader>") != std::string::npos || l.find("loader >") != std::string::npos) return true;
  if (l.find("u-boot>") != std::string::npos || l.find("u-boot >") != std::string::npos) return true;
  if (!l.empty() && l[l.size() - 1] == ' ') l.erase(l.size() - 1);
  return endsWith(l, "switch:") || endsWith(l, "boot>") || endsWith(l, "boot >");
}

bool endsWithUserPrompt(const std::string& s) {
  std::string t = toLower(rtrim(s));
  return endsWith(t, "username:") || endsWith(t, "user name:") || endsWith(t, "login:") || endsWith(t, "user:");
}

bool endsWithPassPrompt(const std::string& s) {
  std::string t = toLower(rtrim(s));
  return endsWith(t, "password:") || endsWith(t, "passphrase:") || endsWith(t, "pass:");
}

bool containsError(const std::string& s, std::string* matched) {
  static const char* kErr[] = {"% invalid input", "%invalid input", "% ambiguous", "%ambiguous",
                               "% incomplete", "%incomplete", "%error", "% unknown command",
                               "%unknown command", "invalid command", "unrecognized command",
                               "unknown command", "error:", "permission denied", "authorization failed",
                               "not permitted", "no such file"};
  std::string l = toLower(s);
  size_t best = std::string::npos, bestLen = 0;
  for (size_t i = 0; i < sizeof(kErr) / sizeof(kErr[0]); i++) {
    size_t p = l.find(kErr[i]);
    if (p != std::string::npos && p < best) {
      best = p;
      bestLen = strlen(kErr[i]);
    }
  }
  if (best == std::string::npos) return false;
  if (matched) *matched = s.substr(best, bestLen);
  return true;
}

bool containsLoginFail(const std::string& s) {
  static const char* kFail[] = {"login invalid", "login incorrect", "authentication failed", "access denied",
                                "bad passwords", "invalid password", "login failed",
                                "invalid username or password"};
  std::string l = toLower(s);
  for (size_t i = 0; i < sizeof(kFail) / sizeof(kFail[0]); i++)
    if (l.find(kFail[i]) != std::string::npos) return true;
  return false;
}

bool isReadable(const std::string& s) {
  if (trim(s).empty()) return false;
  size_t ok = 0;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char)s[i];
    if ((c >= 32 && c < 127) || c == '\r' || c == '\n' || c == '\t') ok++;
  }
  return ok * 10 >= s.size() * 8;
}

int sanitizeForTerminal(int b) {
  if (b == 0x1B || b == '\r' || b == '\n' || b == '\t' || b == 8 || (b >= 32 && b < 127)) return b;
  if (b >= 0x80) return '?';
  return -1;
}

void AnsiFilter::feed(int b, std::string& out) {
  switch (st_) {
    case 0:
      if (b == 0x1B) st_ = 1;
      else if (b != '\r' && b != 0) out.push_back((char)b);
      return;
    case 1:  // after ESC
      if (b == '[') st_ = 2;
      else if (b == '(' || b == ')') st_ = 3;
      else st_ = 0;  // ESC = / ESC > / other two-byte sequences
      return;
    case 2:  // CSI ... final letter
      if (isalpha(b)) st_ = 0;
      return;
    default:  // charset designation: one more byte
      st_ = 0;
      return;
  }
}

}  // namespace tu
