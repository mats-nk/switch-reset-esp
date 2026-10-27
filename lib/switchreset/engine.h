// Platform-independent reset engine: talks to the switch through a Platform object.
// On the ESP32 the Platform wraps UART + web UI; in tools/host_test it wraps a simulated switch.
#pragma once
#include <stdint.h>
#include <map>
#include <string>
#include "profiles.h"
#include "textutil.h"

class Platform {
 public:
  virtual ~Platform() {}
  virtual int readByte() = 0;  // next received byte or -1
  virtual void writeStr(const std::string& s) = 0;
  virtual void flushInput() = 0;
  virtual void openPort(uint32_t baud) = 0;
  virtual uint32_t nowMs() = 0;
  virtual void sleepMs(uint32_t ms) = 0;
  virtual void terminalOut(const std::string& raw) = 0;       // mirror of the serial data
  virtual void log(char level, const std::string& msg) = 0;   // level: i=info o=ok w=warning e=error
  virtual bool ask(const std::string& label, bool secret, std::string& answer) = 0;  // false = aborted
  virtual bool aborted() = 0;
  virtual void baudChanged(uint32_t baud) = 0;
};

class ResetEngine {
 public:
  explicit ResetEngine(Platform& p) : P(p), prof_(nullptr) {}
  bool run(const Profile& profile, int modeIdx = 0);  // true = sequence done; errors are reported via log('e')
  const std::string& lastError() const { return err_; }

 private:
  enum Wait { W_PROMPT, W_FINAL, W_FAIL };

  size_t readChunk(std::string& clean);
  void sendText(const std::string& s) { P.writeStr(s); }
  void sendAnswer(const char* a);
  std::string wake();
  bool connect(std::string& firstAnswer);
  Wait waitFor(const Rule* rules, int nRules, int timeoutS, std::string& total, bool rebootOk = false);
  bool askCred(const char* label, bool secret, std::string& out);
  bool login();
  bool runStep(const Step& s);
  bool fail();

  Platform& P;
  const Profile* prof_;
  bool rebooted_ = false;
  bool bootMode_ = false;  // current mode runs at the bootloader prompt
  std::string err_;
  std::map<std::string, std::string> creds_;
  tu::AnsiFilter ansi_;
};
