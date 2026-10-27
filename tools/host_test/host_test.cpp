// Host-side tests: runs the real engine against simulated switches (virtual time, no hardware).
#include <stdio.h>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "../../lib/switchreset/engine.h"
#include "../../lib/switchreset/textutil.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg)                                         \
  do {                                                           \
    if (cond) { g_pass++; }                                      \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
  } while (0)

struct Trans { std::string out, next; };
struct State {
  std::string prompt;
  bool silent = false;
  std::map<std::string, Trans> on;
};

class Sim : public Platform {
 public:
  std::map<std::string, State> st;
  std::string cur;
  uint32_t simBaud = 9600, curBaud = 0, t = 0, abortAt = 0;
  bool garbageWrongBaud = true;
  std::deque<int> rx;
  std::string line;
  std::vector<std::string> received;
  std::vector<std::pair<char, std::string> > logs;
  std::map<std::string, std::string> answers;
  std::vector<std::string> asked;

  void out(const std::string& s) { for (size_t i = 0; i < s.size(); i++) rx.push_back((unsigned char)s[i]); }
  void enter(const std::string& state) { cur = state; if (!st[cur].silent) out(st[cur].prompt); }
  void handleLine(const std::string& l) {
    if (simBaud == 0 || curBaud != simBaud) {
      if (garbageWrongBaud && simBaud != 0) out("\xfe\x80\xe0\xfe\x80\xe0\xfe\x80");
      return;
    }
    State& s = st[cur];
    if (s.silent) return;
    received.push_back(l);
    out(l + "\r\n");
    std::map<std::string, Trans>::iterator it = s.on.find(l);
    if (it != s.on.end()) {
      out(it->second.out);
      enter(it->second.next);
    } else if (l.empty()) {
      out(s.prompt);
    } else {
      out("% Invalid input detected at '^' marker.\r\n");
      out(s.prompt);
    }
  }

  int readByte() override { if (rx.empty()) return -1; int b = rx.front(); rx.pop_front(); return b; }
  void writeStr(const std::string& s) override {
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '\r') { handleLine(line); line.clear(); } else line += s[i];
    }
  }
  void flushInput() override { rx.clear(); }
  void openPort(uint32_t b) override { curBaud = b; rx.clear(); line.clear(); }
  uint32_t nowMs() override { return t; }
  void sleepMs(uint32_t ms) override { t += ms; }
  void terminalOut(const std::string&) override {}
  void log(char l, const std::string& m) override { logs.push_back(std::make_pair(l, m)); }
  bool ask(const std::string& label, bool, std::string& a) override {
    asked.push_back(label);
    if (!answers.count(label)) return false;
    a = answers[label];
    return true;
  }
  bool aborted() override { return abortAt && t >= abortAt; }
  void baudChanged(uint32_t) override {}

  bool hasLog(char lvl, const std::string& sub) const {
    for (size_t i = 0; i < logs.size(); i++)
      if (logs[i].first == lvl && logs[i].second.find(sub) != std::string::npos) return true;
    return false;
  }
  bool got(const std::string& l) const {
    for (size_t i = 0; i < received.size(); i++) if (received[i] == l) return true;
    return false;
  }
};

static void add(Sim& s, const std::string& st, const std::string& prompt, bool silent = false) {
  s.st[st].prompt = prompt;
  s.st[st].silent = silent;
}
static void on(Sim& s, const std::string& st, const std::string& line, const std::string& out,
               const std::string& next) {
  Trans tr; tr.out = out; tr.next = next;
  s.st[st].on[line] = tr;
}

static void ciscoCore(Sim& s) {
  add(s, "priv", "Switch#");
  add(s, "confirm1", "[confirm]");
  add(s, "vlan1", ""); add(s, "vlan2", "");
  add(s, "save", ""); add(s, "rel", "");
  add(s, "dead", "", true);
  on(s, "priv", "write erase", "Erasing the nvram filesystem will remove all configuration files! Continue? ", "confirm1");
  on(s, "confirm1", "", "[OK]\r\nErase of nvram: complete\r\n", "priv");
  on(s, "priv", "delete flash:vlan.dat", "Delete filename [vlan.dat]?", "vlan1");
  on(s, "vlan1", "", "Delete flash:/vlan.dat? [confirm]", "vlan2");
  on(s, "vlan2", "", "", "priv");
  on(s, "priv", "reload", "System configuration has been modified. Save? [yes/no]: ", "save");
  on(s, "save", "no", "Proceed with reload? [confirm]", "rel");
  on(s, "rel", "", "\r\n*Mar  1 00:05:10.000: %SYS-5-RELOAD: Reload requested\r\n", "dead");
}

static bool runProfile(Sim& s, int idx, int mode = 0) {
  ResetEngine e(s);
  return e.run(PROFILES[idx], mode);
}

static void test_text() {
  printf("text helpers\n");
  CHECK(tu::endsWithPrompt("x\r\nSwitch#", PROMPT_GENERIC), "cisco priv prompt");
  CHECK(tu::endsWithPrompt("Switch(config-if)# ", PROMPT_GENERIC), "config prompt");
  CHECK(tu::endsWithPrompt("ProCurve Switch 2530-24G#", PROMPT_GENERIC), "hp prompt with spaces");
  CHECK(!tu::endsWithPrompt("Press any key to continue", PROMPT_GENERIC), "no prompt");
  CHECK(tu::endsWithUnpriv("Switch>") && !tu::endsWithUnpriv("Switch#"), "unpriv");
  CHECK(tu::endsWithPrompt("<Huawei>", PROMPT_HUAWEI) && tu::endsWithPrompt("[~HUAWEI]", PROMPT_HUAWEI), "huawei");
  CHECK(!tu::endsWithPrompt("<>", PROMPT_HUAWEI), "huawei empty");
  CHECK(tu::endsWithShell("root@sw:RE:0% ") && tu::endsWithPrompt("root@sw>", PROMPT_JUNIPER), "junos");
  CHECK(tu::isRommon("rommon 1 > ") && tu::isRommon("switch: ") && tu::isRommon("loader>") && !tu::isRommon("Switch#"), "rommon");
  CHECK(tu::endsWithUserPrompt("\r\nUsername: ") && tu::endsWithUserPrompt("login:") && tu::endsWithPassPrompt("Password:"), "login prompts");
  CHECK(tu::globFind("save the config (y/n)?", "save*(y/n)") && !tu::globFind("(y/n) save", "save*(y/n)"), "glob order");
  CHECK(tu::globFind("continue [y/n]", "(y/n)|[y/n]"), "glob alternatives");
  std::string m;
  CHECK(tu::containsError("% Invalid input detected", &m) && m == "% Invalid input", "error match");
  CHECK(!tu::containsError("Switch# all good", &m), "no error");
  CHECK(tu::isReadable("Switch>\r\n") && !tu::isReadable("\xfe\x80\xe0\xfe\x80\xe0") && !tu::isReadable("  \r\n"), "readable");
  tu::AnsiFilter f; std::string o; const char* a = "\x1b[31mred\x1b[0m\r\nok\x1b(Bx";
  for (const char* p = a; *p; p++) f.feed((unsigned char)*p, o);
  CHECK(o == "red\nokx", "ansi filter");
}

static void test_cisco_with_login() {
  printf("cisco: login prompt, enable, full reset\n");
  Sim s; ciscoCore(s);
  add(s, "login", "\r\nUsername: "); add(s, "pw", "Password: "); add(s, "user", "Switch>");
  on(s, "login", "admin", "", "pw"); on(s, "pw", "secret", "", "user");
  on(s, "user", "enable", "", "priv");
  s.answers["Username"] = "admin"; s.answers["Password"] = "secret";
  s.enter("login");
  CHECK(runProfile(s, 0), "run ok");
  CHECK(s.cur == "dead", "switch rebooted");
  CHECK(s.asked.size() == 2, "credentials asked exactly once each");
  CHECK(s.got("enable") && s.got("write erase") && s.got("delete flash:vlan.dat") && s.got("reload"), "commands sent");
  CHECK(s.hasLog('o', "Reset commands sent"), "success logged");
}

static void test_cisco_initial_dialog() {
  printf("cisco: initial configuration dialog + press return\n");
  Sim s; ciscoCore(s);
  add(s, "dialog", "Would you like to enter the initial configuration dialog? [yes/no]: ");
  add(s, "ret", "");
  on(s, "dialog", "no", "\r\nPress RETURN to get started!\r\n", "ret");
  on(s, "ret", "", "", "priv");
  s.enter("dialog");
  CHECK(runProfile(s, 0), "run ok");
  CHECK(s.got("no") && s.cur == "dead", "answered dialog with Enter and finished");
}

static void test_wrong_type() {
  printf("wrong switch type -> error\n");
  Sim s; add(s, "priv", "Switch#"); s.enter("priv");
  CHECK(!runProfile(s, 0), "must fail");
  CHECK(s.hasLog('e', "reported an error") && s.hasLog('e', "% Invalid input"), "error reported");
}

static void test_login_rejected() {
  printf("login rejected\n");
  Sim s;
  add(s, "login", "\r\nUsername: "); add(s, "pw", "Password: ");
  on(s, "login", "admin", "", "pw"); on(s, "pw", "bad", "\r\n% Login invalid\r\n", "login");
  s.answers["Username"] = "admin"; s.answers["Password"] = "bad";
  s.enter("login");
  CHECK(!runProfile(s, 0), "must fail");
  CHECK(s.hasLog('e', "Login rejected"), "login error");
}

static void test_rommon() {
  printf("rommon\n");
  Sim s; add(s, "rom", "\r\nrommon 1 > "); s.enter("rom");
  CHECK(!runProfile(s, 0), "must fail");
  CHECK(s.hasLog('e', "bootloader/ROMMON"), "rommon error");
}

static void test_no_response() {
  printf("no response / abort\n");
  { Sim s; s.simBaud = 0; CHECK(!runProfile(s, 0), "must fail");
    CHECK(s.hasLog('e', "does not answer at any baud rate"), "silent error"); }
  { Sim s; s.simBaud = 0; s.abortAt = 3000; CHECK(!runProfile(s, 0), "abort fails");
    CHECK(s.hasLog('e', "Aborted by user"), "abort message"); }
}

static void test_hp() {
  printf("hp procurve\n");
  Sim s; add(s, "p", "ProCurve Switch 2530-24G#"); add(s, "q", ""); add(s, "dead", "", true);
  on(s, "p", "erase startup-config", "This will delete the startup configuration and reboot the switch.\r\nContinue [y/n]? ", "q");
  on(s, "q", "y", "", "dead");
  s.enter("p");
  CHECK(runProfile(s, 1) && s.cur == "dead", "hp reset");
}

static void test_aruba_cx_autobaud() {
  printf("aruba cx: auto baud detection (38400)\n");
  Sim s; s.simBaud = 38400;
  add(s, "p", "switch# "); add(s, "e", ""); add(s, "sv", ""); add(s, "go", ""); add(s, "dead", "", true);
  on(s, "p", "erase startup-config", "This will securely erase all customer data. Continue (y/n)? ", "e");
  on(s, "e", "y", "Performing factory reset...\r\n", "p");
  on(s, "p", "boot system", "Do you want to save the current configuration (y/n)? ", "sv");
  on(s, "sv", "n", "The system will boot. Continue (y/n)? ", "go");
  on(s, "go", "y", "", "dead");
  s.enter("p");
  CHECK(runProfile(s, 2) && s.cur == "dead", "cx reset");
  CHECK(s.hasLog('w', "unreadable answer") && s.hasLog('o', "38400"), "baud scan logged");
}

static void test_juniper() {
  printf("juniper: shell -> cli -> zeroize\n");
  Sim s; add(s, "sh", "root@sw:RE:0% "); add(s, "cli", "root@sw> "); add(s, "z", ""); add(s, "dead", "", true);
  on(s, "sh", "cli", "", "cli"); on(s, "cli", "request system zeroize",
     "warning: System will be rebooted and may not boot without configuration\r\nErase all data? [yes,no] (no) ", "z");
  on(s, "z", "yes", "", "dead");
  s.enter("sh");
  CHECK(runProfile(s, 3) && s.cur == "dead", "junos reset");
}

static void test_huawei() {
  printf("huawei vrp\n");
  Sim s; add(s, "p", "<Huawei>"); add(s, "a", ""); add(s, "b", ""); add(s, "c", ""); add(s, "dead", "", true);
  on(s, "p", "reset saved-configuration", "Warning: The action will delete the saved configuration. Continue? [Y/N]:", "a");
  on(s, "a", "y", "Info: Succeeded in clearing the configuration.\r\n", "p");
  on(s, "p", "reboot", "Warning: The configuration has been modified, save? [Y/N]:", "b");
  on(s, "b", "n", "System will reboot! Continue? [Y/N]:", "c");
  on(s, "c", "y", "", "dead");
  s.enter("p");
  CHECK(runProfile(s, 6) && s.cur == "dead", "huawei reset");
}


static void test_modes() {
  printf("reset modes\n");
  {  // Cisco: config only -> no reload, stays in priv mode
    Sim s; ciscoCore(s); s.enter("priv");
    CHECK(runProfile(s, 0, 1), "cisco cfg-only ok");
    CHECK(s.got("write erase") && s.got("delete flash:vlan.dat") && !s.got("reload") && s.cur == "priv", "no reload sent");
    CHECK(s.hasLog('o', "NOT rebooted"), "no-reboot message");
  }
  {  // Cisco: full factory reset, two confirmations, then silence
    Sim s; add(s, "priv", "Switch#"); add(s, "c1", "[confirm]"); add(s, "c2", "[confirm]"); add(s, "dead", "", true);
    on(s, "priv", "factory-reset all", "The factory reset operation is irreversible for all operations. Are you sure? ", "c1");
    on(s, "c1", "", "Proceed with factory reset? ", "c2");
    on(s, "c2", "", "\r\nFactory reset in progress ...\r\n", "dead");
    s.enter("priv");
    CHECK(runProfile(s, 0, 2) && s.cur == "dead", "cisco factory-reset all (two confirms)");
    CHECK(s.hasLog('o', "now reboots"), "reboot message");
  }
  {  // Cisco: full factory reset, single confirmation then silence
    Sim s; add(s, "priv", "Switch#"); add(s, "c1", "[confirm]"); add(s, "dead", "", true);
    on(s, "priv", "factory-reset all", "Are you sure? ", "c1");
    on(s, "c1", "", "", "dead");
    s.enter("priv");
    CHECK(runProfile(s, 0, 2) && s.cur == "dead", "cisco factory-reset all (one confirm)");
  }
  {  // HP: erase all
    Sim s; add(s, "p", "ProCurve Switch 2530-24G#"); add(s, "q", ""); add(s, "dead", "", true);
    on(s, "p", "erase all", "This will erase everything and reboot. Continue [y/n]? ", "q"); on(s, "q", "y", "", "dead");
    s.enter("p");
    CHECK(runProfile(s, 1, 1) && s.cur == "dead" && s.got("erase all"), "hp erase all");
  }
  {  // Aruba CX: erase all zeroize
    Sim s; s.simBaud = 115200; add(s, "p", "switch# "); add(s, "q", ""); add(s, "dead", "", true);
    on(s, "p", "erase all zeroize", "This will securely erase all customer data. Continue (y/n)? ", "q"); on(s, "q", "y", "", "dead");
    s.enter("p");
    CHECK(runProfile(s, 2, 2) && s.cur == "dead", "cx erase all zeroize");
  }
  {  // Aruba CX: config only
    Sim s; s.simBaud = 115200; add(s, "p", "switch# "); add(s, "q", "");
    on(s, "p", "erase startup-config", "Continue (y/n)? ", "q"); on(s, "q", "y", "Done.\r\n", "p");
    s.enter("p");
    CHECK(runProfile(s, 2, 1) && s.cur == "p" && !s.got("boot system"), "cx cfg only");
  }
  {  // Juniper: zeroize media
    Sim s; add(s, "cli", "root@sw> "); add(s, "z", ""); add(s, "dead", "", true);
    on(s, "cli", "request system zeroize media", "Erase all data? [yes,no] (no) ", "z"); on(s, "z", "yes", "", "dead");
    s.enter("cli");
    CHECK(runProfile(s, 3, 1) && s.cur == "dead", "junos zeroize media");
  }
  {  // EXOS: standard reset
    Sim s; add(s, "p", "X440G2.1 # "); add(s, "q", ""); add(s, "dead", "", true);
    on(s, "p", "unconfigure switch", "Do you want to unconfigure the switch? (y/n) ", "q"); on(s, "q", "y", "", "dead");
    s.enter("p");
    CHECK(runProfile(s, 4, 1) && s.cur == "dead" && s.got("unconfigure switch"), "exos standard reset");
  }
  {  // Huawei: erase saved config only
    Sim s; add(s, "p", "<Huawei>"); add(s, "a", "");
    on(s, "p", "reset saved-configuration", "Continue? [Y/N]:", "a"); on(s, "a", "y", "Info: Succeeded.\r\n", "p");
    s.enter("p");
    CHECK(runProfile(s, 6, 1) && s.cur == "p" && !s.got("reboot"), "huawei cfg only");
  }
  {  // invalid mode
    Sim s; add(s, "p", "Switch#"); s.enter("p");
    CHECK(!runProfile(s, 0, 9) && s.hasLog('e', "Invalid reset mode"), "invalid mode rejected");
  }
  {  // every profile has at least one mode with at least one step
    bool okAll = true;
    for (int i = 0; i < PROFILE_COUNT; i++) {
      if (PROFILES[i].modeCount < 1) okAll = false;
      for (int m = 0; m < PROFILES[i].modeCount; m++) if (PROFILES[i].modes[m].stepCount < 1) okAll = false;
    }
    CHECK(okAll, "profile table sane");
  }
}

static void test_bootloader() {
  printf("cisco bootloader mode (switch: prompt)\n");
  {
    Sim s; add(s, "b", "switch: "); add(s, "q", ""); add(s, "dead", "", true);
    on(s, "b", "flash_init", "Initializing Flash...\r\nflashfs[0]: 5 files, 1 directories\r\ndone.\r\n", "b");
    on(s, "b", "load_helper", "", "b");
    on(s, "b", "del flash:config.text", "Are you sure you want to delete \"flash:config.text\" (y/n)?", "q");
    on(s, "q", "y", "File \"flash:config.text\" deleted\r\n", "b");
    on(s, "b", "del flash:private-config.text", "Error deleting \"flash:private-config.text\" (No such file or directory)\r\n", "b");
    on(s, "b", "del flash:vlan.dat", "Are you sure you want to delete \"flash:vlan.dat\" (y/n)?", "q");
    on(s, "b", "boot", "Loading \"flash:/c2960-lanbasek9-mz.150-2.SE11.bin\"...\r\n", "dead");
    s.enter("b");
    CHECK(runProfile(s, 0, 3), "bootloader sequence ok");
    CHECK(s.cur == "dead" && s.got("flash_init") && s.got("del flash:config.text") && s.got("del flash:vlan.dat") && s.got("boot"), "commands sent, switch booted");
    CHECK(s.hasLog('w', "optional step"), "missing private-config tolerated");
    CHECK(s.hasLog('o', "now reboots"), "boot reported");
  }
  {  // not in bootloader: normal prompt -> clear error, nothing sent
    Sim s; add(s, "priv", "Switch#"); s.enter("priv");
    CHECK(!runProfile(s, 0, 3) && s.hasLog('e', "not at the bootloader prompt") && !s.got("flash_init"), "refuses outside bootloader");
  }
}

int main() {
  test_text();
  test_cisco_with_login();
  test_cisco_initial_dialog();
  test_wrong_type();
  test_login_rejected();
  test_rommon();
  test_no_response();
  test_hp();
  test_aruba_cx_autobaud();
  test_juniper();
  test_huawei();
  test_modes();
  test_bootloader();
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
