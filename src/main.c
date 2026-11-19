#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <H1B3C/emulator.h>
#include <H1B3C/proxies.h>
#include <common/common.h>
#include <common/runtime/console.h>
#include <common/runtime/control.h>

static void get_chipemu(EmulatorChips chip) {
  switch (chip) {
    case H1B3C:
      ctx.chipemu.initialize = Havn_Initialize;
      ctx.chipemu.start_emulation = Havn_StartEmulation;
      ctx.chipemu.end_emulation = Havn_EndEmulation;
      ctx.chipemu.reboot = Havn_Reboot;
      return;
    default:
      emu_exit(RC_CHIPEMU_UNSUPPORTED);
  }
}

int main(int argc, char **argv) {
  const char *chip;
  const char *rom_path;
  const char *fw_path;
  const char *console_arg;
  const char *flash_dir_arg;
  const char *home;
  int path_len;
  int no_persist;
  int show_version;
  int wait_for_enter;
  int disable_tpm;
  int disable_usb;
  ConsoleMode console_mode = CONSOLE_NONE;

  ctx_init();
  runtime_init();

  ctx.persist_flash = true;
  home = getenv("HOME");

  if (home != NULL && home[0] != '\0') {
    path_len = snprintf(ctx.flash_dir, sizeof(ctx.flash_dir),
                        "%s/.local/share/gscemu-c/flash", home);

    if (path_len < 0 || path_len >= (int)sizeof(ctx.flash_dir)) ctx.persist_flash = false;
  } else {
    ctx.persist_flash = false;
  }

  ctx.ac = argcheck_new(argc, argv);
  if (!ctx.ac) emu_exit(RC_ARGCHECK_FAILED);

  argcheck_set_description(ctx.ac, "gscemulator ported to C");

  chip = argcheck_fequals(ctx.ac, "--chip", "-c", "Chip to emulate [ h1b3c | h1c3m | h1d3c | h1d3m | npcx ]");
  rom_path = argcheck_fequals(ctx.ac, "--bootrom", "-r", "BootROM binary to use (Overrides bundled BootROM binary for specified chip)");
  fw_path = argcheck_fequals(ctx.ac, "--firmware", "-f", "Firmware binary to use (Overrides bundled firmware binary for specified chip)");
  console_arg = argcheck_fequals(ctx.ac, "--console", NULL,
                                 "Console output [ none | stdio | pty ]");
  flash_dir_arg = argcheck_fequals(ctx.ac, "--flash_dir", NULL,
                                   "Directory for persisted flash images");
  no_persist = argcheck_fbool(ctx.ac, "--no-persist", NULL,
                              "Do not read or write persisted flash");
  ctx.verbose = argcheck_fbool(ctx.ac, "--verbose", "-v", "Verbose output");
  show_version = argcheck_fbool(ctx.ac, "--version", NULL, "Show version information");
  wait_for_enter = argcheck_fbool(ctx.ac, "--wait-for-enter", NULL,
                                  "Wait for ENTER before starting emulation");
  disable_tpm = argcheck_fbool(ctx.ac, "--disable-tpm-chardev", NULL,
                               "Do not start the vTPM chardev proxy");
  disable_usb = argcheck_fbool(ctx.ac, "--disable-usb-proxy", NULL,
                               "Do not start the CCD USB proxy");

  if (show_version) {
    print_version();
    emu_exit(RC_SUCCESS);
  }

  argcheck_check_help(ctx.ac);

  if (flash_dir_arg != NULL && flash_dir_arg[0] != '\0') {
    path_len = snprintf(ctx.flash_dir, sizeof(ctx.flash_dir), "%s", flash_dir_arg);

    if (path_len < 0 || path_len >= (int)sizeof(ctx.flash_dir)) {
      emu_exit(RC_ARG_PATH_TOO_LONG);
    }

    if (!no_persist) ctx.persist_flash = true;
  }

  if (no_persist) ctx.persist_flash = false;

  if (console_arg == NULL) {
#ifdef SINGLE_THREAD
    console_mode = CONSOLE_NONE;
#else
    console_mode = isatty(STDIN_FILENO) ? CONSOLE_STDIO : CONSOLE_NONE;
#endif
  } else if (strcmp(console_arg, "none") == 0) {
    console_mode = CONSOLE_NONE;
  } else if (strcmp(console_arg, "stdio") == 0 || strcmp(console_arg, "pts") == 0) {
    console_mode = CONSOLE_STDIO;
  } else if (strcmp(console_arg, "pty") == 0) {
    console_mode = CONSOLE_PTY;
  } else {
    LOG_ERR("Invalid console mode: %s\n", console_arg);
    emu_exit(RC_ARG_CONSOLE_INVALID);
  }

  if (rom_path != NULL) {
    ctx.firmware.useBundledROM = false;
    path_len = snprintf(ctx.firmware.bootrom, sizeof(ctx.firmware.bootrom), "%s", rom_path);
    if (path_len < 0 || (size_t)path_len >= sizeof(ctx.firmware.bootrom)) {
      LOG_ERR("BootROM path is too long\n");
      emu_exit(RC_ARG_PATH_TOO_LONG);
    }
  }

  if (fw_path != NULL) {
    ctx.firmware.useBundledFW = false;
    path_len = snprintf(ctx.firmware.progfw, sizeof(ctx.firmware.progfw), "%s", fw_path);
    if (path_len < 0 || (size_t)path_len >= sizeof(ctx.firmware.progfw)) {
      LOG_ERR("Firmware path is too long\n");
      emu_exit(RC_ARG_PATH_TOO_LONG);
    }
  }

  /* The default chip is H1B3C. */
  if (!chip) chip = "h1b3c";

  if (strcmp(chip, "h1b3c") == 0) {
    ctx.chip = H1B3C;
  } else {
    LOG_ERR("Invalid chip passed!\n");
    emu_exit(RC_ARG_CHIP_INVALID);
  }

  get_chipemu(ctx.chip);

  ctx.chipemu.initialize();
  if (!ctx.chipemu.ready) emu_exit(RC_CHIPEMU_FAILED_INIT);

#ifdef SINGLE_THREAD
  if (console_mode != CONSOLE_NONE) {
    LOG_ERR("--console %s is not supported with SINGLE_THREAD\n",
            console_mode == CONSOLE_PTY ? "pty" : "stdio");
    emu_exit(RC_ARG_CONSOLE_INVALID);
  }
#else
  if (console_mode != CONSOLE_NONE && !console_start(console_mode)) {
    emu_exit(RC_CHIPEMU_FAILED_INIT);
  }
#endif

  if (wait_for_enter) {
    LOG("Waiting for ENTER to start emulation...\n");
    fflush(stdout);
    (void)console_wait_enter(NULL);
  }

  H1B3C_StartProxies(disable_tpm != 0, disable_usb != 0);

  runtime_start_emulation();
  runtime_run();

  return RC_SUCCESS;
}
