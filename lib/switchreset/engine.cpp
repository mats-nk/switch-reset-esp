#include "engine.h"
#include <stdio.h>
#include <string.h>

namespace {
const uint32_t kIdleMs = 600;  // output pause after which the tail is analysed
const uint32_t kOtherBauds[] = {9600, 115200, 38400, 57600, 19200};

struct GlobalRule {
  const char* pattern;
  const char* answer;
};
// Dialogs that can appear at any time
const GlobalRule kGlobalRules[] = {
    {"--more--|<--- more --->|---- more ----", " "},
    {"initial configuration dialog?*[yes/no]", "no"},
    {"press return to get started", "\r"},
    {"press any key to continue|press enter to continue", "\r"},
    {"terminate autoinstall?*[yes]", "yes"},
};

std::string num(unsigned long v) {
  char b[24];
  snprintf(b, sizeof b, "%lu", v);
  return b;
}

void trimFront(std::string& s, size_t keep) {
  if (s.size() > keep * 2) s.erase(0, s.size() - keep);
}
}  // namespace

bool ResetEngine::fail() {
  P.log('e', err_);
  return false;
}

size_t ResetEngine::readChunk(std::string& clean) {
  std::string raw;
  size_t n = 0;
  int b;
  while ((b = P.readByte()) >= 0) {
    n++;
    int s = tu::sanitizeForTerminal(b);
    if (s >= 0) raw.push_back((char)s);
    ansi_.feed(b, clean);
    if (n >= 2048) break;  // keep the loop responsive
  }
  if (!raw.empty()) P.terminalOut(raw);
  return n;
}

void ResetEngine::sendAnswer(const char* a) {
  std::string s(a);
  if (s != " " && s != "\r") s += "\r";
  sendText(s);
}

// Sends Enter a few times and collects the answer.
std::string ResetEngine::wake() {
  P.flushInput();
  std::string buf;
  for (int i = 0; i < 3; i++) {
    sendText("\r");
    uint32_t end = P.nowMs() + 2500 / 3;
    while ((int32_t)(end - P.nowMs()) > 0) {
      if (readChunk(buf) == 0) P.sleepMs(5);
      if (P.aborted()) return buf;
    }
  }
  return buf;
}

// Opens the port and finds the right baud rate if needed.
bool ResetEngine::connect(std::string& firstAnswer) {
  uint32_t list[1 + sizeof(kOtherBauds) / sizeof(kOtherBauds[0])];
  int n = 0;
  list[n++] = prof_->baud;
  for (size_t i = 0; i < sizeof(kOtherBauds) / sizeof(kOtherBauds[0]); i++)
    if (kOtherBauds[i] != prof_->baud) list[n++] = kOtherBauds[i];

  bool garbage = false;
  for (int i = 0; i < n; i++) {
    uint32_t baud = list[i];
    P.openPort(baud);
    P.baudChanged(baud);
    P.log('i', "Trying " + num((unsigned long)baud) + " baud ...");
    std::string answer = wake();
    if (P.aborted()) {
      err_ = "Aborted by user.";
      return false;
    }
    if (tu::trim(answer).empty()) continue;
    if (tu::isReadable(answer)) {
      P.log('o', "Switch answers at " + num((unsigned long)baud) + " baud.");
      firstAnswer = answer;
      return true;
    }
    garbage = true;
    P.log('w', num((unsigned long)baud) + " baud: unreadable answer (wrong baud rate?).");
  }

  if (garbage)
    err_ =
        "Data arrives, but no baud rate gives readable text. Possible: switch uses an unusual baud rate, "
        "cable/adapter defective or wrong parity/stop-bit setting.";
  else
    err_ =
        "The switch does not answer at any baud rate. Possible causes:\n"
        "   - Switch is off or still booting (wait 30-120 s)\n"
        "   - Cable is not in the CONSOLE port (e.g. in the management port)\n"
        "   - Wrong cable: a rollover/console cable is needed, not a normal LAN cable\n"
        "   - No RS-232 level converter (MAX3232) between ESP32 and switch, or TX/RX swapped\n"
        "   - Port disabled on the switch (Cisco: mini-USB and RJ45 at once = USB takes priority)";
  return false;
}

bool ResetEngine::askCred(const char* label, bool secret, std::string& out) {
  std::map<std::string, std::string>::iterator it = creds_.find(label);
  if (it != creds_.end()) {
    out = it->second;
    return true;
  }
  if (!P.ask(label, secret, out)) {
    err_ = "Aborted by user (no input received).";
    return false;
  }
  creds_[label] = out;
  return true;
}

// Waits until a prompt appears. Answers known dialogs on the way.
ResetEngine::Wait ResetEngine::waitFor(const Rule* rules, int nRules, int timeoutS, std::string& total,
                                       bool rebootOk) {
  std::string buf;
  int answered = 0;
  total.clear();
  uint32_t lastData = P.nowMs();
  uint32_t endAt = P.nowMs() + (uint32_t)timeoutS * 1000UL;

  while ((int32_t)(endAt - P.nowMs()) > 0) {
    if (P.aborted()) {
      err_ = "Aborted by user.";
      return W_FAIL;
    }
    std::string chunk;
    readChunk(chunk);
    if (!chunk.empty()) {
      buf += chunk;
      total += chunk;
      trimFront(total, 6000);
      trimFront(buf, 1500);
      lastData = P.nowMs();
      continue;
    }
    if (buf.empty() || P.nowMs() - lastData < kIdleMs) {
      P.sleepMs(5);
      continue;
    }

    std::string tail = tu::lastN(buf, 400);
    std::string lt = tu::toLower(tail);
    bool handled = false;

    // global dialogs (More, initial dialog ...)
    for (size_t i = 0; i < sizeof(kGlobalRules) / sizeof(kGlobalRules[0]); i++) {
      if (tu::globFind(lt, kGlobalRules[i].pattern)) {
        sendAnswer(kGlobalRules[i].answer);
        buf.clear();
        handled = true;
        break;
      }
    }
    if (handled) continue;

    // step-specific rules
    for (int i = 0; i < nRules; i++) {
      if (tu::globFind(lt, rules[i].pattern)) {
        P.log('d', std::string("Dialog detected (") + rules[i].pattern + ")");
        sendAnswer(rules[i].answer);
        buf.clear();
        answered++;
        if (rules[i].final) {
          uint32_t fin = P.nowMs() + 6000;  // collect the last output before the reboot
          while ((int32_t)(fin - P.nowMs()) > 0) {
            std::string c;
            if (readChunk(c) == 0) P.sleepMs(5);
            total += c;
          }
          return W_FINAL;
        }
        handled = true;
        break;
      }
    }
    if (handled) continue;

    if (bootMode_) {  // bootloader sequences: only the bootloader prompt counts
      if (tu::endsWithBootPrompt(tail)) return W_PROMPT;
      if (tu::endsWithPrompt(tail, prof_->prompt) || tu::endsWithUserPrompt(tail) || tu::endsWithPassPrompt(tail)) {
        err_ =
            "The switch is not at the bootloader prompt ('switch:'). Power-cycle it while holding the MODE button "
            "until 'switch:' appears, then start again.";
        return W_FAIL;
      }
      sendText("\r");
      buf.clear();
      continue;
    }

    if (tu::containsLoginFail(tail)) {
      err_ =
          "Login rejected (wrong username/password). If the switch is password-protected and the password is "
          "unknown, password recovery via reset button/ROMMON is required.";
      return W_FAIL;
    }
    if (tu::isRommon(tail)) {
      err_ =
          "The switch is in the bootloader/ROMMON (no valid operating system loaded or boot error). This tool "
          "cannot reset anything here - boot an image with 'boot flash:<file>' or reflash.";
      return W_FAIL;
    }
    if (tu::endsWithPrompt(tail, prof_->prompt) || (prof_->junosShell && tu::endsWithShell(tail)))
      return W_PROMPT;

    // No prompt yet: login input needed?
    if (tu::endsWithUserPrompt(tail)) {
      std::string u;
      if (!askCred("Username", false, u)) return W_FAIL;
      sendText(u + "\r");
      buf.clear();
    } else if (tu::endsWithPassPrompt(tail)) {
      std::string p;
      if (!askCred("Password", true, p)) return W_FAIL;
      sendText(p + "\r");
      buf.clear();
    } else {
      sendText("\r");  // unknown output: send Enter again
      buf.clear();
    }
  }
  if (rebootOk && answered > 0) return W_FINAL;  // silence after the dialog: the switch is rebooting
  err_ = "Timeout after " + num((unsigned long)timeoutS) + "s - the switch stopped responding. Last output: " +
         tu::repr(tu::lastN(total, 300));
  return W_FAIL;
}

bool ResetEngine::login() {
  P.log('i', "Logging in to the switch ...");
  sendText("\r");
  std::string text;
  if (waitFor(nullptr, 0, 60, text) == W_FAIL) return false;

  // Juniper: from shell (%) into the CLI
  if (prof_->junosShell && tu::endsWithShell(text)) {
    sendText(std::string(prof_->shellToCli) + "\r");
    std::string t2;
    if (waitFor(nullptr, 0, 20, t2) == W_FAIL) return false;
  }

  // Cisco/HP/Arista: user mode (>) -> enable
  if (prof_->enableCommand && tu::endsWithUnpriv(text)) {
    P.log('i', "User mode detected, switching to privileged mode with 'enable' ...");
    sendText(std::string(prof_->enableCommand) + "\r");
    std::string t2, t3;
    if (waitFor(nullptr, 0, 30, t2) == W_FAIL) return false;
    sendText("\r");
    if (waitFor(nullptr, 0, 15, t3) == W_FAIL) return false;
    if (tu::endsWithUnpriv(t3)) {
      err_ = "'enable' was not accepted - enable password wrong or unknown.";
      return false;
    }
  }
  P.log('o', "Logged in.");
  return true;
}

bool ResetEngine::runStep(const Step& s) {
  P.log('i', std::string("-> ") + s.description + "  (" + s.command + ")");
  P.flushInput();
  sendText(std::string(s.command) + "\r");
  if (s.timeoutS == 0) {  // fire and forget (e.g. `boot`): just show what the switch prints for a few seconds
    uint32_t end = P.nowMs() + 3000;
    while ((int32_t)(end - P.nowMs()) > 0) {
      std::string c;
      if (readChunk(c) == 0) P.sleepMs(5);
    }
    rebooted_ = true;
    return true;
  }
  std::string total;
  Wait w = waitFor(s.rules, s.ruleCount, s.timeoutS, total, s.rebootOk);
  if (w == W_FAIL) return false;
  if (w == W_FINAL) rebooted_ = true;

  std::string m;
  if (w != W_FINAL && tu::containsError(total, &m)) {
    std::string msg = std::string("Command '") + s.command + "' reported an error: '" + m +
                      "'. Output: " + tu::repr(tu::lastN(tu::trim(total), 200));
    if (s.optional) {
      P.log('w', msg + " (optional step, skipped)");
      return true;
    }
    err_ = msg + "\n   Possible: wrong switch type chosen or missing privileges.";
    return false;
  }
  return true;
}

bool ResetEngine::run(const Profile& profile, int modeIdx) {
  prof_ = &profile;
  err_.clear();
  creds_.clear();
  rebooted_ = false;
  if (modeIdx < 0 || modeIdx >= profile.modeCount) {
    err_ = "Invalid reset mode.";
    return fail();
  }
  const Mode& mode = profile.modes[modeIdx];
  P.log('i', std::string("Profile: ") + profile.name);
  P.log('i', std::string("Mode: ") + mode.name);

  bootMode_ = mode.bootloader;

  std::string first;
  if (!connect(first)) return fail();
  if (bootMode_) {
    P.log('i', "Looking for the bootloader prompt ('switch:') ...");
    sendText("\r");
    std::string t;
    if (waitFor(nullptr, 0, 30, t) == W_FAIL) return fail();
    P.log('o', "Bootloader prompt found.");
  } else if (!login()) {
    return fail();
  }
  for (int i = 0; i < mode.stepCount; i++)
    if (!runStep(mode.steps[i])) return fail();

  if (rebooted_) {
    P.log('o', "Reset commands sent - the switch now reboots and loads factory defaults.");
    P.log('i', "   Note: the reboot takes about 2-5 minutes. Reconnect only afterwards.");
  } else {
    P.log('o', "Done - the configuration was erased. The switch was NOT rebooted.");
    P.log('i', "   Reload the switch (or power-cycle it) to start with the factory configuration.");
  }
  return true;
}
