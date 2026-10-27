#include "profiles.h"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define STEP(d, c, r, opt, t, rb) {d, c, r, COUNT(r), opt, t, rb}
#define STEP0(d, c, opt, t, rb) {d, c, nullptr, 0, opt, t, rb}  // step without dialog rules
#define MODE(n, d, s) {n, d, s, COUNT(s), false}
#define MODEB(n, d, s) {n, d, s, COUNT(s), true}  // bootloader mode

// ---- Cisco IOS / IOS-XE ----------------------------------------------------
static const Rule cisco_erase_r[] = {{"[confirm]", "\r", false}};
static const Rule cisco_vlan_r[] = {{"delete filename*?", "\r", false}, {"[confirm]", "\r", false}};
static const Rule cisco_reload_r[] = {{"[yes/no]", "no", false}, {"[confirm]", "\r", true}};
static const Rule cisco_factory_r[] = {{"[confirm]", "\r", false}, {"[yes/no]", "yes", false}};
#define CISCO_ERASE STEP("Erase startup config", "write erase", cisco_erase_r, false, 30, false)
#define CISCO_VLAN STEP("Delete VLAN database (vlan.dat)", "delete flash:vlan.dat", cisco_vlan_r, true, 30, false)
#define CISCO_RELOAD STEP("Reboot", "reload", cisco_reload_r, false, 30, false)
#define CISCO_FACTORY STEP("Factory reset (all)", "factory-reset all", cisco_factory_r, false, 30, true)
static const Step cisco_default[] = {CISCO_ERASE, CISCO_VLAN, CISCO_RELOAD};
static const Step cisco_cfg[] = {CISCO_ERASE, CISCO_VLAN};
static const Step cisco_full[] = {CISCO_FACTORY};

// Older Catalyst (2960/3560/3750 ...): bootloader prompt "switch:" (MODE button held while powering on)
static const Rule cisco_bootdel_r[] = {{"(y/n)", "y", false}};
static const Step cisco_boot[] = {
    STEP0("Initialize flash filesystem", "flash_init", false, 60, false),
    STEP0("Load helper", "load_helper", true, 30, false),
    STEP("Delete config.text", "del flash:config.text", cisco_bootdel_r, true, 30, false),
    STEP("Delete private-config.text", "del flash:private-config.text", cisco_bootdel_r, true, 30, false),
    STEP("Delete vlan.dat", "del flash:vlan.dat", cisco_bootdel_r, true, 30, false),
    STEP0("Boot the switch", "boot", false, 0, false),
};
static const Mode cisco_modes[] = {
    MODE("Config reset + reboot (default)", "Erases startup-config and vlan.dat, then reboots.", cisco_default),
    MODE("Erase config only (no reboot)",
         "Erases startup-config and vlan.dat. The switch keeps running with its current configuration until it is reloaded.",
         cisco_cfg),
    MODE("Full factory reset (Catalyst 9000, IOS-XE 16.9+)",
         "'factory-reset all': wipes configuration, logs, keys and other user data, then reboots. Older IOS "
         "(2960/3560/3750) does not have this command - use the bootloader mode below for those.",
         cisco_full),
    MODEB("Bootloader reset (older Catalyst 2960/3560/3750 ...)",
          "Before starting: power-cycle the switch while holding the MODE button until the 'switch:' prompt appears. "
          "Then deletes config.text, private-config.text and vlan.dat and boots. If 'no service password-recovery' is "
          "set, the switch instead asks at that point whether to reset to the default configuration - answer y.",
          cisco_boot),
};

// ---- HP ProCurve / Aruba OS-Switch ----------------------------------------
static const Rule hp_r[] = {{"[y/n]", "y", true}};
static const Step hp_default[] = {STEP("Erase startup config + reboot", "erase startup-config", hp_r, false, 30, false)};
static const Step hp_full[] = {STEP("Erase everything + reboot", "erase all", hp_r, false, 30, false)};
static const Mode hp_modes[] = {
    MODE("Config reset + reboot (default)", "'erase startup-config' restores factory defaults and reboots.", hp_default),
    MODE("Full factory reset ('erase all')",
         "Erases configuration, passwords and logs, then reboots. Not available on every model/software version.",
         hp_full),
};

// ---- Aruba CX --------------------------------------------------------------
static const Rule cx_erase_r[] = {{"(y/n)", "y", false}};
static const Rule cx_boot_r[] = {{"save*(y/n)", "n", false}, {"(y/n)", "y", true}};
static const Rule cx_zero_r[] = {{"(y/n)", "y", true}};
#define CX_ERASE STEP("Erase startup config", "erase startup-config", cx_erase_r, false, 30, false)
#define CX_BOOT STEP("Reboot", "boot system", cx_boot_r, false, 30, false)
static const Step cx_default[] = {CX_ERASE, CX_BOOT};
static const Step cx_cfg[] = {CX_ERASE};
static const Step cx_full[] = {STEP("Erase all customer data + reboot", "erase all zeroize", cx_zero_r, false, 30, false)};
static const Mode cx_modes[] = {
    MODE("Config reset + reboot (default)", "Erases the startup configuration, then reboots.", cx_default),
    MODE("Erase config only (no reboot)", "Erases the startup configuration; no reboot.", cx_cfg),
    MODE("Full factory reset ('erase all zeroize')",
         "Securely erases all customer data (config, logs, keys) and reboots into factory defaults.", cx_full),
};

// ---- Juniper EX ------------------------------------------------------------
static const Rule junos_r[] = {{"[yes,no]", "yes", true}};
static const Step junos_default[] = {STEP("Full reset (zeroize) + reboot", "request system zeroize", junos_r, false, 30, false)};
static const Step junos_media[] = {STEP("Zeroize incl. media wipe + reboot", "request system zeroize media", junos_r, false, 30, false)};
static const Mode junos_modes[] = {
    MODE("Zeroize + reboot (default)", "Removes all configuration, logs and keys; the switch reboots to factory defaults.", junos_default),
    MODE("Zeroize with media wipe (slow)",
         "Like zeroize, but also overwrites the storage media. Can take a long time.", junos_media),
};

// ---- Extreme EXOS ----------------------------------------------------------
static const Rule exos_r[] = {{"(y/n)|[y/n]", "y", true}};
static const Step exos_default[] = {STEP("Full reset + reboot", "unconfigure switch all", exos_r, false, 30, false)};
static const Step exos_std[] = {STEP("Standard reset + reboot", "unconfigure switch", exos_r, false, 30, false)};
static const Mode exos_modes[] = {
    MODE("Full reset incl. all settings (default)", "'unconfigure switch all': also clears the management configuration.", exos_default),
    MODE("Standard reset ('unconfigure switch')",
         "Resets the configuration but, unlike 'all', keeps some persistent settings (see the EXOS documentation).",
         exos_std),
};

// ---- Arista EOS ------------------------------------------------------------
static const Rule eos_erase_r[] = {{"[confirm]|[yes/no", "\r", false}};
static const Rule eos_reload_r[] = {{"yes/no", "no", false}, {"[confirm]", "\r", true}};
#define EOS_ERASE STEP("Erase startup config", "write erase", eos_erase_r, false, 30, false)
static const Step eos_default[] = {EOS_ERASE, STEP("Reboot", "reload", eos_reload_r, false, 30, false)};
static const Step eos_cfg[] = {EOS_ERASE};
static const Mode eos_modes[] = {
    MODE("Config reset + reboot (default)", "Erases the startup configuration and reboots (into ZTP mode).", eos_default),
    MODE("Erase config only (no reboot)", "Erases the startup configuration; reload later.", eos_cfg),
};

// ---- Huawei VRP ------------------------------------------------------------
static const Rule vrp_reset_r[] = {{"[y/n]", "y", false}};
static const Rule vrp_reboot_r[] = {{"save*[y/n]", "n", false}, {"[y/n]", "y", true}};
#define VRP_RESET STEP("Erase saved configuration", "reset saved-configuration", vrp_reset_r, false, 30, false)
static const Step vrp_default[] = {VRP_RESET, STEP("Reboot", "reboot", vrp_reboot_r, false, 30, false)};
static const Step vrp_cfg[] = {VRP_RESET};
static const Mode vrp_modes[] = {
    MODE("Config reset + reboot (default)", "Erases the saved configuration and reboots.", vrp_default),
    MODE("Erase saved config only (no reboot)", "Erases the saved configuration; it takes effect at the next reboot.", vrp_cfg),
};

const Profile PROFILES[] = {
    {"Cisco IOS / IOS-XE (Catalyst 2960/3560/3750/9200/9300 ...)", 9600, PROMPT_GENERIC, "enable", false, nullptr,
     cisco_modes, COUNT(cisco_modes), "Login is usually not needed on a factory-fresh switch; otherwise username/password are asked."},
    {"HP ProCurve / Aruba OS-Switch (2530/2540/2920/2930 ...)", 9600, PROMPT_GENERIC, "enable", false, nullptr,
     hp_modes, COUNT(hp_modes), "Restores factory defaults and reboots."},
    {"Aruba CX (6100/6200/6300/8xxx ...)", 115200, PROMPT_GENERIC, nullptr, false, nullptr, cx_modes, COUNT(cx_modes),
     "Factory default login: admin (no password)."},
    {"Juniper EX (Junos)", 9600, PROMPT_JUNIPER, nullptr, true, "cli", junos_modes, COUNT(junos_modes),
     "Login usually 'root'. Zeroize erases EVERYTHING incl. logs."},
    {"Extreme Networks EXOS / Switch Engine (X440/X450/X460/X620/X670 ...)", 9600, PROMPT_GENERIC, nullptr, false,
     nullptr, exos_modes, COUNT(exos_modes), "Default login: admin (no password). Not for VOSS/ERS (Avaya)."},
    {"Arista EOS (7010/7050/7060/7280 ...)", 9600, PROMPT_GENERIC, "enable", false, nullptr, eos_modes,
     COUNT(eos_modes), "Login usually 'admin' (no password)."},
    {"Huawei VRP (S5700/S6700 ...)", 9600, PROMPT_HUAWEI, nullptr, false, nullptr, vrp_modes, COUNT(vrp_modes),
     "Default login: admin / admin@huawei.com"},
};
const int PROFILE_COUNT = COUNT(PROFILES);
