#define _GNU_SOURCE

#include <H1B3C/proxies.h>

#include <pthread.h>
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

void H1B3C_StopProxies(void) {}

#else

typedef struct {
  const char *name;
  pthread_t thread;
  bool started;
} Proxy;

static pthread_mutex_t proxy_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t proxy_cond = PTHREAD_COND_INITIALIZER;
static bool proxy_stop;

static Proxy tpm_proxy = { .name = "vtpm-proxy" };
static Proxy usb_proxy = { .name = "usb-proxy" };

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
  if (!path_exists("/sys/class/udc/dummy_udc.0")) {
    return confirm_without("dummy_udc.0", "the USB proxy");
  }
  return true;
}

static void *proxy_main(void *arg) {
  Proxy *proxy = arg;

  thread_ctx_init(THREAD_ROLE_WORKER, proxy->name);
  LOG("thread started; emulated transport is not implemented yet\n");

  pthread_mutex_lock(&proxy_lock);
  while (!proxy_stop) pthread_cond_wait(&proxy_cond, &proxy_lock);
  pthread_mutex_unlock(&proxy_lock);
  return NULL;
}

static void proxy_start(Proxy *proxy) {
  if (pthread_create(&proxy->thread, NULL, proxy_main, proxy) != 0) {
    LOG_ERR("Failed to start %s\n", proxy->name);
    return;
  }

  (void)pthread_setname_np(proxy->thread, proxy->name);
  proxy->started = true;
  LOG("%s started\n", proxy->name);
}

void H1B3C_StartProxies(bool disable_tpm, bool disable_usb) {
  if (geteuid() != 0) return;

  proxy_stop = false;

  if (!disable_tpm && tpm_proxy_requirements()) proxy_start(&tpm_proxy);
  if (!disable_usb && usb_proxy_requirements()) proxy_start(&usb_proxy);
}

void H1B3C_StopProxies(void) {
  pthread_mutex_lock(&proxy_lock);
  proxy_stop = true;
  pthread_cond_broadcast(&proxy_cond);
  pthread_mutex_unlock(&proxy_lock);

  if (tpm_proxy.started) {
    pthread_join(tpm_proxy.thread, NULL);
    tpm_proxy.started = false;
  }

  if (usb_proxy.started) {
    pthread_join(usb_proxy.thread, NULL);
    usb_proxy.started = false;
  }
}

#endif /* SINGLE_THREAD */
