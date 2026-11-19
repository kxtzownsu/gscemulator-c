#include <stdio.h>
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
  int path_len;
  int show_version;
  int wait_for_enter;
  int disable_tpm;
  int disable_usb;
  int console_pts = 0;

  ctx_init();
  runtime_init();

  ctx.ac = argcheck_new(argc, argv);
  if (!ctx.ac) emu_exit(RC_ARGCHECK_FAILED);

  argcheck_set_description(ctx.ac, "gscemulator ported to C");

  chip = argcheck_fequals(ctx.ac, "--chip", "-c", "Chip to emulate [ h1b3c | h1c3m | h1d3c | h1d3m | npcx ]");
  rom_path = argcheck_fequals(ctx.ac, "--bootrom", "-r", "BootROM binary to use (Overrides bundled BootROM binary for specified chip)");
  fw_path = argcheck_fequals(ctx.ac, "--firmware", "-f", "Firmware binary to use (Overrides bundled firmware binary for specified chip)");
  console_arg = argcheck_fequals(ctx.ac, "--console", NULL, "Console output [ none | pts ]");
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

  if (console_arg == NULL) {
#ifdef SINGLE_THREAD
    console_pts = 0;
#else
    console_pts = isatty(STDIN_FILENO) ? 1 : 0;
#endif
  } else if (strcmp(console_arg, "none") == 0) {
    console_pts = 0;
  } else if (strcmp(console_arg, "pts") == 0) {
    console_pts = 1;
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
  if (console_pts) {
    LOG_ERR("--console pts is not supported with SINGLE_THREAD\n");
    emu_exit(RC_ARG_CONSOLE_INVALID);
  }
#else
  if (console_pts && !console_start()) emu_exit(RC_CHIPEMU_FAILED_INIT);
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
