#define _GNU_SOURCE

#include <H1B3C/peripherals/gpio.h>
#include <H1B3C/proxies.h>
#include <H1B3C/tpm-proxy.h>
#include <H1B3C/usb-proxy.h>

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <common/logger.h>
#include <common/runtime/console.h>
#include <common/runtime/thread.h>

#ifdef SINGLE_THREAD

void H1B3C_StartProxies(bool disable_tpm, bool disable_usb) {
  (void)disable_tpm;
  (void)disable_usb;
  LOG_VBS("Proxies are disabled in SINGLE_THREAD builds\n");
}

void H1B3C_RestartUSBProxy(void) {}
void H1B3C_SuspendUSBProxy(void) {}
void H1B3C_ResumeUSBProxy(void) {}

void H1B3C_StopProxies(void) {}

#else

static bool tpm_proxy_started;
static bool usb_proxy_started;

static bool path_exists(const char *path) {
  return access(path, F_OK) == 0;
}

static bool module_loaded(const char *name) {
  char path[128];

  if (snprintf(path, sizeof(path), "/sys/module/%s", name) >= (int)sizeof(path)) return false;
  return path_exists(path);
}

static bool confirm_without(const char *missing, const char *what) {
  char message[160];

  LOG_ERR("%s is not available\n", missing);
  snprintf(message, sizeof(message), "Press ENTER to continue without %s...", what);

  if (!console_wait_enter(message)) LOG_ERR("stdin is closed, continuing without %s\n", what);

  return false;
}

static bool tpm_proxy_requirements(void) {
  if (!module_loaded("tpm_vtpm_proxy")) return confirm_without("tpm_vtpm_proxy", "the vTPM proxy");
  if (!path_exists("/dev/vtpmx")) return confirm_without("/dev/vtpmx", "the vTPM proxy");
  return true;
}

static bool usb_proxy_requirements(void) {
  if (!module_loaded("dummy_hcd")) return confirm_without("dummy_hcd", "the USB proxy");
  if (!module_loaded("raw_gadget")) return confirm_without("raw_gadget", "the USB proxy");
  if (!path_exists("/dev/raw-gadget")) return confirm_without("/dev/raw-gadget", "the USB proxy");
  if (access("/dev/raw-gadget", R_OK | W_OK) != 0) {
    return confirm_without("/dev/raw-gadget (not writable)", "the USB proxy");
  }
  if (!path_exists("/sys/class/udc/dummy_udc.0")) {
    return confirm_without("dummy_udc.0", "the USB proxy");
  }
  return true;
}

void H1B3C_StartProxies(bool disable_tpm, bool disable_usb) {
  if (geteuid() != 0) return;

  if (!disable_tpm && tpm_proxy_requirements()) {
    if (TPM_ProxyStart()) {
      tpm_proxy_started = true;
      LOG("vtpm-proxy started\n");
    } else {
      LOG_ERR("Failed to start vtpm-proxy\n");
    }
  }

  if (!disable_usb && usb_proxy_requirements()) {
    if (USB_ProxyStart()) {
      usb_proxy_started = true;
      GPIO_SetInputOverride(0, 5, false);
      LOG("usb-proxy started\n");
    } else {
      LOG_ERR("Failed to start usb-proxy\n");
    }
  }
}

void H1B3C_SuspendUSBProxy(void) {
  if (!usb_proxy_started) return;

  USB_ProxyStop();
}

void H1B3C_ResumeUSBProxy(void) {
  if (!usb_proxy_started) return;

  if (USB_ProxyStart()) {
    GPIO_SetInputOverride(0, 5, false);
    LOG("usb-proxy restarted\n");
  } else {
    usb_proxy_started = false;
    LOG_ERR("Failed to restart usb-proxy\n");
  }
}

void H1B3C_RestartUSBProxy(void) {
  H1B3C_SuspendUSBProxy();
  H1B3C_ResumeUSBProxy();
}

void H1B3C_StopProxies(void) {
  if (tpm_proxy_started) {
    TPM_ProxyStop();
    tpm_proxy_started = false;
  }

  if (usb_proxy_started) {
    USB_ProxyStop();
    usb_proxy_started = false;
  }
}

#endif /* SINGLE_THREAD */
