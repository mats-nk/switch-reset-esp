// Switch profiles: baud rate, prompt type and the reset modes (command sequences) per vendor.
#pragma once
#include <stdint.h>

// A dialog rule. `pattern` is matched case-insensitively against the tail of the output:
//   '*' = any characters, '|' = alternatives. Patterns are written in lower case.
// `answer`: "\r" = just Enter, " " = space (no Enter), anything else is sent followed by Enter.
// `final` = nothing more to do after the answer (the switch is rebooting).
struct Rule {
  const char* pattern;
  const char* answer;
  bool final;
};

struct Step {
  const char* description;
  const char* command;
  const Rule* rules;
  int ruleCount;
  bool optional;   // errors in this step are only warnings
  int timeoutS;    // 0 = send the command and do not wait for a prompt (e.g. `boot`)
  bool rebootOk;   // silence after the dialog was answered = the switch rebooted (counts as success)
};

// One selectable reset sequence for a switch type (mode 0 is the default).
struct Mode {
  const char* name;
  const char* description;
  const Step* steps;
  int stepCount;
  bool bootloader;  // sequence runs at the bootloader prompt (`switch:`): no login, user power-cycles first
};

enum PromptKind { PROMPT_GENERIC, PROMPT_JUNIPER, PROMPT_HUAWEI };

struct Profile {
  const char* name;
  uint32_t baud;
  PromptKind prompt;
  const char* enableCommand;  // e.g. "enable" on Cisco/HP/Arista, nullptr = none
  bool junosShell;            // Juniper: login may land in the shell (%) -> run `cli`
  const char* shellToCli;
  const Mode* modes;
  int modeCount;
  const char* hint;
};

extern const Profile PROFILES[];
extern const int PROFILE_COUNT;
