#define _GNU_SOURCE

#include <H1B3C/tpm-proxy.h>
#include <H1B3C/peripherals/sps.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <common/logger.h>
#include <common/runtime/control.h>
#include <common/runtime/thread.h>

#ifdef SINGLE_THREAD

bool TPM_ProxyStart(void) {
  return false;
}

void TPM_ProxyStop(void) {}

bool TPM_ProxyTransact(const uint8_t *cmd, size_t cmd_len, uint8_t *resp, size_t resp_cap,
                       size_t *resp_len) {
  (void)cmd;
  (void)cmd_len;
  (void)resp;
  (void)resp_cap;
  if (resp_len != NULL) *resp_len = 0;
  return false;
}

#else

#define TPM_LOCALITY_0_SPI_BASE 0x00D40000u
#define TPM_SPI_MAX_TRANSFER 64u
#define TPM_SPI_HEADER_SIZE 4u
#define TPM_TIS_MAX_BURST 63u
#define TPM_TIS_FIFO_SIZE 2048u
#define TPM_TIS_MAX_RESPONSE 4096u

#define TPM_ACCESS 0x000u
#define TPM_STS 0x018u
#define TPM_DATA_FIFO 0x024u
#define TPM_DID_VID 0xF00u

#define TPM_ACCESS_ACTIVE_LOCALITY 0x20u
#define TPM_ACCESS_REQUEST_USE 0x02u

#define TPM_STS_COMMAND_READY 0x40u
#define TPM_STS_GO 0x20u
#define TPM_STS_DATA_AVAIL 0x10u
#define TPM_STS_DATA_EXPECT 0x08u
#define TPM_STS_RESPONSE_RETRY 0x02u
#define TPM_STS_BURST_COUNT_SHIFT 8u
#define TPM_STS_BURST_COUNT_MASK 0xFFFFu

#define TPM_GOOGLE_DID_VID 0x00281AE0u

#define TPM2_ST_NO_SESSIONS 0x8001u
#define TPM2_CC_SET_LOCALITY 0x20001000u
#define TPM2_CC_STARTUP 0x00000144u
#define TPM2_CC_GET_CAPABILITY 0x0000017Au
#define TPM2_CAP_COMMANDS 0x00000002u
#define TPM2_CAP_TPM_PROPERTIES 0x00000006u
#define TPM2_CC_FIRST 0x0000011Fu
#define TPM_PT_TOTAL_COMMANDS 0x00000129u
#define TPM2_SU_CLEAR 0x0000u

#define TPM_RC_SUCCESS 0x000u
#define TPM_RC_INITIALIZE 0x100u
#define TPM_RC_FAILURE 0x101u
#define TPM_RC_SIZE 0x095u
#define TPM_RC_COMMAND_SIZE 0x142u
#define TPM_RC_LOCALITY 0x907u

#define TPM_PROXY_READY_TIMEOUT_MS 60000u
#define TPM_PROXY_RECOVER_TIMEOUT_MS 10000u
#define TPM_PROXY_RESPONSE_TIMEOUT_MS 30000u
#define TPM_PROXY_LOCALITY_TIMEOUT_MS 2000u
#define TPM_PROXY_IDLE_TIMEOUT_MS 2000u
#define TPM_PROXY_STALL_TIMEOUT_MS 1000u
#define TPM_PROXY_DIDVID_STALL_TIMEOUT_MS 250u
#define TPM_PROXY_DEASSERT_TIMEOUT_MS 50u
#define TPM_PROXY_DEASSERT_SETTLE_MS 5u

#define TPM_PROXY_CACHE_ENTRIES 2

#define VTPM_PROXY_CONTROL_PATH "/dev/vtpmx"
#define VTPM_PROXY_IOC_NEW_DEV 0xC014A100UL
#define VTPM_PROXY_FLAG_TPM2 1u

typedef struct {
  uint32_t flags;
  uint32_t tpm_num;
  uint32_t fd;
  uint32_t major;
  uint32_t minor;
} VtpmProxyNewDev;

typedef struct {
  uint8_t cmd[22];
  size_t cmd_len;
  uint8_t resp[TPM_TIS_MAX_RESPONSE];
  size_t resp_len;
  bool valid;
} TpmCacheEntry;

static atomic_bool proxy_stop;
static pthread_mutex_t proxy_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t proxy_thread;
static bool proxy_started;

static pthread_mutex_t transact_lock = PTHREAD_MUTEX_INITIALIZER;
static bool tpm_ready;

static TpmCacheEntry tpm_cache[TPM_PROXY_CACHE_ENTRIES];

static uint64_t tpm_proxy_now_ms(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void tpm_proxy_sleep_ns(uint64_t ns) {
  struct timespec ts = {
    .tv_sec = (time_t)(ns / 1000000000u),
    .tv_nsec = (long)(ns % 1000000000u),
  };

  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

static void tpm_proxy_sleep_ms(uint32_t ms) {
  tpm_proxy_sleep_ns((uint64_t)ms * 1000000u);
}

static bool tpm_proxy_cancelled(void) {
  return atomic_load(&proxy_stop) || runtime_is_shutting_down();
}

static uint32_t tpm_read_be32(const uint8_t *buf) {
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) |
         (uint32_t)buf[3];
}

static uint32_t tpm_read_le32(const uint8_t *buf) {
  return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
         ((uint32_t)buf[3] << 24);
}

static void tpm_put_be32(uint8_t *buf, uint32_t value) {
  buf[0] = (uint8_t)(value >> 24);
  buf[1] = (uint8_t)(value >> 16);
  buf[2] = (uint8_t)(value >> 8);
  buf[3] = (uint8_t)value;
}

static bool tpm_make_response(uint32_t rc, uint8_t *resp, size_t cap, size_t *resp_len) {
  if (resp == NULL || resp_len == NULL || cap < 10) return false;

  resp[0] = (uint8_t)(TPM2_ST_NO_SESSIONS >> 8);
  resp[1] = (uint8_t)TPM2_ST_NO_SESSIONS;
  tpm_put_be32(resp + 2, 10);
  tpm_put_be32(resp + 6, rc);
  *resp_len = 10;
  return true;
}

static bool tpm_spi_header(bool read, uint32_t offset, size_t size, uint8_t *header) {
  uint32_t address = TPM_LOCALITY_0_SPI_BASE + offset;

  if (size < 1 || size > TPM_SPI_MAX_TRANSFER) return false;

  header[0] = (uint8_t)((read ? 0x80u : 0u) | (size - 1u));
  header[1] = (uint8_t)(address >> 16);
  header[2] = (uint8_t)(address >> 8);
  header[3] = (uint8_t)address;
  return true;
}

static bool tpm_spi_wait_ready(uint32_t timeout_ms) {
  uint64_t deadline = tpm_proxy_now_ms() + timeout_ms;

  for (;;) {
    uint8_t input = 0;
    uint8_t output = 0;

    if (tpm_proxy_cancelled()) return false;
    if (!SPS_HostTransfer(&input, &output, 1)) return false;
    if ((output & 1u) != 0) return true;
    if (tpm_proxy_now_ms() >= deadline) return false;

    tpm_proxy_sleep_ns(100000u);
  }
}

static void tpm_spi_end_transaction(void) {
  (void)SPS_HostSetChipSelect(false);
  (void)SPS_HostWaitDeassert(TPM_PROXY_DEASSERT_TIMEOUT_MS);
  tpm_proxy_sleep_ms(TPM_PROXY_DEASSERT_SETTLE_MS);
}

static bool tpm_spi_wait_idle(void) {
  uint64_t deadline = tpm_proxy_now_ms() + TPM_PROXY_IDLE_TIMEOUT_MS;

  while (!tpm_proxy_cancelled()) {
    uint64_t now = tpm_proxy_now_ms();
    uint32_t slice;

    if (now >= deadline) return false;

    slice = (uint32_t)(deadline - now);
    if (slice > 20) slice = 20;
    if (SPS_HostWaitIdle(slice)) return true;
  }

  return false;
}

static bool tpm_spi_read(uint32_t offset, size_t size, uint8_t *out) {
  uint8_t header[TPM_SPI_HEADER_SIZE];
  uint8_t response[TPM_SPI_HEADER_SIZE];
  uint8_t zeros[TPM_SPI_MAX_TRANSFER];
  bool ok = false;

  if (!tpm_spi_header(true, offset, size, header)) return false;
  memset(zeros, 0, size);

  (void)tpm_spi_wait_idle();
  if (!SPS_HostSetChipSelect(true)) return false;

  if (!SPS_HostTransfer(header, response, sizeof(header))) goto out;
  if ((response[sizeof(response) - 1] & 1u) == 0 &&
      !tpm_spi_wait_ready(TPM_PROXY_STALL_TIMEOUT_MS)) {
    goto out;
  }
  if (!SPS_HostTransfer(zeros, out, size)) goto out;

  ok = true;

out:
  tpm_spi_end_transaction();
  return ok;
}

static bool tpm_spi_write(uint32_t offset, const uint8_t *data, size_t size) {
  uint8_t header[TPM_SPI_HEADER_SIZE];
  uint8_t response[TPM_SPI_HEADER_SIZE];
  uint8_t scratch[TPM_SPI_MAX_TRANSFER];
  bool ok = false;

  if (!tpm_spi_header(false, offset, size, header)) return false;

  (void)tpm_spi_wait_idle();
  if (!SPS_HostSetChipSelect(true)) return false;

  if (!SPS_HostTransfer(header, response, sizeof(header))) goto out;
  if ((response[sizeof(response) - 1] & 1u) == 0 &&
      !tpm_spi_wait_ready(TPM_PROXY_STALL_TIMEOUT_MS)) {
    goto out;
  }
  if (!SPS_HostTransfer(data, scratch, size)) goto out;

  ok = true;

out:
  tpm_spi_end_transaction();
  return ok;
}

static bool tis_read_reg(uint32_t offset, uint8_t *data, size_t size) {
  if (size == 0 || size > TPM_SPI_MAX_TRANSFER) return false;
  return tpm_spi_read(offset, size, data);
}

static bool tis_write_reg(uint32_t offset, const uint8_t *data, size_t size) {
  if (size == 0 || size > TPM_SPI_MAX_TRANSFER) return false;
  return tpm_spi_write(offset, data, size);
}

static bool tis_read_access(uint8_t *value) {
  return tis_read_reg(TPM_ACCESS, value, 1);
}

static bool tis_read_sts(uint32_t *sts) {
  uint8_t buf[4];

  if (!tis_read_reg(TPM_STS, buf, sizeof(buf))) return false;
  *sts = tpm_read_le32(buf);
  return true;
}

static bool tis_write_sts(uint32_t value) {
  uint8_t buf[4];

  buf[0] = (uint8_t)value;
  buf[1] = (uint8_t)(value >> 8);
  buf[2] = (uint8_t)(value >> 16);
  buf[3] = (uint8_t)(value >> 24);
  return tis_write_reg(TPM_STS, buf, sizeof(buf));
}

static bool tis_wait_sts(uint32_t mask, bool clear, uint32_t timeout_ms, uint32_t *value) {
  uint64_t deadline = tpm_proxy_now_ms() + timeout_ms;

  for (;;) {
    uint32_t sts;

    if (tpm_proxy_cancelled()) return false;
    if (!tis_read_sts(&sts)) return false;

    if (clear ? (sts & mask) == 0 : (sts & mask) != 0) {
      if (value != NULL) *value = sts;
      return true;
    }

    if (tpm_proxy_now_ms() >= deadline) {
      LOG_VBS("timed out waiting for TPM STS mask 0x%08x (STS=0x%08x)\n", mask, sts);
      return false;
    }

    tpm_proxy_sleep_ms(1);
  }
}

static bool tis_wait_ready_until(uint64_t deadline) {
  while (!tpm_proxy_cancelled()) {
    uint8_t buf[4];
    uint32_t did_vid;

    if (tis_read_reg(TPM_DID_VID, buf, sizeof(buf))) {
      did_vid = tpm_read_le32(buf);
      if (did_vid == TPM_GOOGLE_DID_VID) return true;
    }

    if (tpm_proxy_now_ms() >= deadline) return false;
    tpm_proxy_sleep_ms(50);
  }

  return false;
}

static bool tis_request_locality(uint32_t timeout_ms) {
  uint64_t deadline = tpm_proxy_now_ms() + timeout_ms;
  uint8_t access = 0;

  if (!tis_read_access(&access)) return false;
  if ((access & TPM_ACCESS_ACTIVE_LOCALITY) != 0) return true;

  access = TPM_ACCESS_REQUEST_USE;
  if (!tis_write_reg(TPM_ACCESS, &access, 1)) return false;

  for (;;) {
    if (tpm_proxy_cancelled()) return false;
    if (!tis_read_access(&access)) return false;
    if ((access & TPM_ACCESS_ACTIVE_LOCALITY) != 0) return true;
    if (tpm_proxy_now_ms() >= deadline) return false;
    tpm_proxy_sleep_ms(1);
  }
}

static bool tis_write_command(const uint8_t *command, size_t len) {
  size_t offset = 0;

  while (offset < len) {
    uint32_t sts;
    uint32_t burst;
    size_t chunk;

    if (tpm_proxy_cancelled()) return false;
    if (!tis_read_sts(&sts)) return false;

    burst = (sts >> TPM_STS_BURST_COUNT_SHIFT) & TPM_STS_BURST_COUNT_MASK;
    if (burst == 0) {
      if (!tis_wait_sts(TPM_STS_DATA_EXPECT, false, TPM_PROXY_RESPONSE_TIMEOUT_MS, NULL)) {
        return false;
      }
      continue;
    }

    chunk = burst;
    if (chunk > TPM_TIS_MAX_BURST) chunk = TPM_TIS_MAX_BURST;
    if (chunk > len - offset) chunk = len - offset;
    if (!tis_write_reg(TPM_DATA_FIFO, command + offset, chunk)) return false;
    offset += chunk;
  }

  return tis_wait_sts(TPM_STS_DATA_EXPECT, true, TPM_PROXY_RESPONSE_TIMEOUT_MS, NULL);
}

static uint32_t tis_next_fifo_chunk(size_t remaining, uint32_t timeout_ms) {
  uint64_t deadline = tpm_proxy_now_ms() + timeout_ms;

  for (;;) {
    uint32_t sts;
    uint32_t burst;

    if (tpm_proxy_cancelled()) return 0;
    if (!tis_read_sts(&sts)) return 0;

    burst = (sts >> TPM_STS_BURST_COUNT_SHIFT) & TPM_STS_BURST_COUNT_MASK;
    if (burst != 0) {
      if (burst > TPM_TIS_MAX_BURST) burst = TPM_TIS_MAX_BURST;
      if (burst > remaining) burst = (uint32_t)remaining;
      return burst;
    }

    if (tpm_proxy_now_ms() >= deadline) return 0;
    tpm_proxy_sleep_ms(1);
  }
}

static bool tis_read_response(uint8_t *resp, size_t cap, size_t *resp_len) {
  size_t have = 0;
  uint32_t total;

  if (cap < 10) return false;

  while (have < 10) {
    uint32_t chunk = tis_next_fifo_chunk(10 - have, TPM_PROXY_RESPONSE_TIMEOUT_MS);

    if (chunk == 0) return false;
    if (!tis_read_reg(TPM_DATA_FIFO, resp + have, chunk)) return false;
    have += chunk;
  }

  total = tpm_read_be32(resp + 2);
  if (total < 10 || total > TPM_TIS_MAX_RESPONSE || total > cap) {
    LOG_VBS("invalid TPM response size %u\n", total);
    return false;
  }

  while (have < total) {
    uint32_t chunk = tis_next_fifo_chunk(total - have, TPM_PROXY_RESPONSE_TIMEOUT_MS);

    if (chunk == 0) return false;
    if (!tis_read_reg(TPM_DATA_FIFO, resp + have, chunk)) return false;
    have += chunk;
  }

  *resp_len = total;
  return true;
}

static bool tis_send_command(const uint8_t *command, size_t len, uint8_t *resp, size_t cap,
                             size_t *resp_len) {
  bool ok = false;
  uint32_t sts;

  if (!tis_write_sts(TPM_STS_COMMAND_READY)) goto out;
  if (!tis_wait_sts(TPM_STS_COMMAND_READY, false, TPM_PROXY_RESPONSE_TIMEOUT_MS, &sts)) goto out;

  if (!tis_write_command(command, len)) goto out;
  if (!tis_write_sts(TPM_STS_GO)) goto out;

  if (!tis_wait_sts(TPM_STS_DATA_AVAIL | TPM_STS_RESPONSE_RETRY, false,
                    TPM_PROXY_RESPONSE_TIMEOUT_MS, &sts)) {
    goto out;
  }
  if ((sts & TPM_STS_RESPONSE_RETRY) != 0) {
    LOG_VBS("TPM requested a response retry\n");
    goto out;
  }

  if (!tis_read_response(resp, cap, resp_len)) goto out;
  ok = true;

out:
  (void)tis_write_sts(TPM_STS_COMMAND_READY);
  return ok;
}

static bool tis_startup(void) {
  uint8_t command[12];
  uint8_t response[64];
  size_t response_len = 0;
  uint32_t rc;

  command[0] = (uint8_t)(TPM2_ST_NO_SESSIONS >> 8);
  command[1] = (uint8_t)TPM2_ST_NO_SESSIONS;
  tpm_put_be32(command + 2, sizeof(command));
  tpm_put_be32(command + 6, TPM2_CC_STARTUP);
  command[10] = (uint8_t)(TPM2_SU_CLEAR >> 8);
  command[11] = (uint8_t)TPM2_SU_CLEAR;

  if (!tis_send_command(command, sizeof(command), response, sizeof(response), &response_len)) {
    return false;
  }
  if (response_len < 10) return false;

  rc = tpm_read_be32(response + 6);
  if (rc != TPM_RC_SUCCESS && rc != TPM_RC_INITIALIZE) {
    LOG_VBS("TPM startup returned 0x%08x\n", rc);
    return false;
  }

  return true;
}

static bool tis_ensure_ready(uint32_t timeout_ms) {
  uint64_t deadline = tpm_proxy_now_ms() + timeout_ms;

  if (!tis_wait_ready_until(deadline)) return false;

  for (;;) {
    if (tpm_proxy_cancelled()) return false;
    if (tis_request_locality(TPM_PROXY_LOCALITY_TIMEOUT_MS) && tis_startup()) return true;
    if (tpm_proxy_now_ms() >= deadline) return false;
    tpm_proxy_sleep_ms(500);
  }
}

static bool tis_transact(const uint8_t *command, size_t cmd_len, uint8_t *resp, size_t resp_cap,
                         size_t *resp_len) {
  uint32_t total;
  uint32_t cc;

  if (cmd_len < 10) return tpm_make_response(TPM_RC_COMMAND_SIZE, resp, resp_cap, resp_len);

  total = tpm_read_be32(command + 2);
  if (total != cmd_len || total < 10) {
    return tpm_make_response(TPM_RC_COMMAND_SIZE, resp, resp_cap, resp_len);
  }
  if (total > TPM_TIS_FIFO_SIZE) return tpm_make_response(TPM_RC_SIZE, resp, resp_cap, resp_len);

  cc = tpm_read_be32(command + 6);
  if (cc == TPM2_CC_SET_LOCALITY) {
    uint8_t locality = cmd_len > 10 ? command[10] : 0;

    if (locality != 0) return tpm_make_response(TPM_RC_LOCALITY, resp, resp_cap, resp_len);
    if (!tis_request_locality(TPM_PROXY_LOCALITY_TIMEOUT_MS)) return false;
    return tpm_make_response(TPM_RC_SUCCESS, resp, resp_cap, resp_len);
  }

  if (cc != TPM2_CC_STARTUP) (void)tis_startup();

  return tis_send_command(command, cmd_len, resp, resp_cap, resp_len);
}

bool TPM_ProxyTransact(const uint8_t *cmd, size_t cmd_len, uint8_t *resp, size_t resp_cap,
                       size_t *resp_len) {
  bool ok;

  if (cmd == NULL || resp == NULL || resp_len == NULL) return false;

  *resp_len = 0;

  pthread_mutex_lock(&transact_lock);

  if (!tpm_ready) tpm_ready = tis_ensure_ready(TPM_PROXY_RECOVER_TIMEOUT_MS);

  if (tpm_ready) {
    ok = tis_transact(cmd, cmd_len, resp, resp_cap, resp_len);
  } else {
    ok = false;
  }

  if (!ok) tpm_ready = false;

  pthread_mutex_unlock(&transact_lock);
  return ok;
}

static void tpm_getcap_pdu(uint8_t *pdu, uint32_t capability, uint32_t property, uint32_t count) {
  pdu[0] = (uint8_t)(TPM2_ST_NO_SESSIONS >> 8);
  pdu[1] = (uint8_t)TPM2_ST_NO_SESSIONS;
  tpm_put_be32(pdu + 2, 22);
  tpm_put_be32(pdu + 6, TPM2_CC_GET_CAPABILITY);
  tpm_put_be32(pdu + 10, capability);
  tpm_put_be32(pdu + 14, property);
  tpm_put_be32(pdu + 18, count);
}

static void tpm_cache_store(size_t index, const uint8_t *cmd, size_t cmd_len, const uint8_t *resp,
                            size_t resp_len) {
  TpmCacheEntry *entry;

  if (index >= TPM_PROXY_CACHE_ENTRIES) return;
  if (cmd_len > sizeof(tpm_cache[0].cmd) || resp_len > sizeof(tpm_cache[0].resp)) return;

  entry = &tpm_cache[index];
  memcpy(entry->cmd, cmd, cmd_len);
  entry->cmd_len = cmd_len;
  memcpy(entry->resp, resp, resp_len);
  entry->resp_len = resp_len;
  entry->valid = true;
}

static bool tpm_prime_cache_once(void) {
  uint8_t total_pdu[22];
  uint8_t commands_pdu[22];
  uint8_t response[TPM_TIS_MAX_RESPONSE];
  size_t response_len = 0;
  uint32_t total;

  tpm_getcap_pdu(total_pdu, TPM2_CAP_TPM_PROPERTIES, TPM_PT_TOTAL_COMMANDS, 1);
  if (!TPM_ProxyTransact(total_pdu, sizeof(total_pdu), response, sizeof(response),
                         &response_len)) {
    return false;
  }

  tpm_cache_store(0, total_pdu, sizeof(total_pdu), response, response_len);

  if (response_len < 27) return true;

  total = tpm_read_be32(response + 23);
  if (total == 0 || total > 0x1000) return true;

  tpm_getcap_pdu(commands_pdu, TPM2_CAP_COMMANDS, TPM2_CC_FIRST, total);
  if (!TPM_ProxyTransact(commands_pdu, sizeof(commands_pdu), response, sizeof(response),
                         &response_len)) {
    return false;
  }

  tpm_cache_store(1, commands_pdu, sizeof(commands_pdu), response, response_len);
  return true;
}

static bool tpm_prepare(uint32_t timeout_ms) {
  uint64_t deadline = tpm_proxy_now_ms() + timeout_ms;

  for (;;) {
    if (tpm_prime_cache_once()) return true;
    if (tpm_proxy_cancelled()) return false;
    if (tpm_proxy_now_ms() >= deadline) return false;
    tpm_proxy_sleep_ms(500);
  }
}

static bool tpm_transact_cached(const uint8_t *cmd, size_t cmd_len, uint8_t *resp, size_t resp_cap,
                                size_t *resp_len) {
  size_t i;

  for (i = 0; i < TPM_PROXY_CACHE_ENTRIES; i++) {
    TpmCacheEntry *entry = &tpm_cache[i];

    if (!entry->valid || entry->cmd_len != cmd_len) continue;
    if (memcmp(entry->cmd, cmd, cmd_len) != 0) continue;
    if (entry->resp_len > resp_cap) return false;

    memcpy(resp, entry->resp, entry->resp_len);
    *resp_len = entry->resp_len;
    return true;
  }

  return TPM_ProxyTransact(cmd, cmd_len, resp, resp_cap, resp_len);
}

static int vtpm_proxy_open(char *path, size_t path_size) {
  VtpmProxyNewDev dev;
  int control_fd;
  int length;

  control_fd = open(VTPM_PROXY_CONTROL_PATH, O_RDWR | O_CLOEXEC);
  if (control_fd < 0) {
    LOG_VBS("cannot open %s: %s\n", VTPM_PROXY_CONTROL_PATH, strerror(errno));
    return -1;
  }

  memset(&dev, 0, sizeof(dev));
  dev.flags = VTPM_PROXY_FLAG_TPM2;

  if (ioctl(control_fd, VTPM_PROXY_IOC_NEW_DEV, &dev) != 0) {
    LOG_ERR("VTPM_PROXY_IOC_NEW_DEV failed: %s\n", strerror(errno));
    close(control_fd);
    return -1;
  }

  close(control_fd);

  length = snprintf(path, path_size, "/dev/tpm%u", dev.tpm_num);
  if (length < 0 || (size_t)length >= path_size) {
    LOG_ERR("vTPM chardev path is too long\n");
    close((int)dev.fd);
    return -1;
  }

  return (int)dev.fd;
}

static ssize_t tpm_read_retry(int fd, uint8_t *buf, size_t len) {
  for (;;) {
    ssize_t count = read(fd, buf, len);

    if (count < 0 && errno == EINTR) {
      if (tpm_proxy_cancelled()) return -1;
      continue;
    }

    return count;
  }
}

static bool tpm_read_command(int fd, uint8_t *buf, size_t cap, size_t *cmd_len) {
  ssize_t count = tpm_read_retry(fd, buf, cap);
  size_t have;
  uint32_t total;

  if (count < 0) return false;
  if (count == 0) {
    errno = EPIPE;
    return false;
  }

  have = (size_t)count;

  while (have < 10) {
    count = tpm_read_retry(fd, buf + have, cap - have);
    if (count <= 0) return false;
    have += (size_t)count;
  }

  total = tpm_read_be32(buf + 2);
  if (total < 10 || total > cap) {
    *cmd_len = have;
    return true;
  }

  while (have < total) {
    count = tpm_read_retry(fd, buf + have, total - have);
    if (count <= 0) return false;
    have += (size_t)count;
  }

  *cmd_len = total;
  return true;
}

static bool tpm_write_all(int fd, const uint8_t *buf, size_t len) {
  size_t offset = 0;

  while (offset < len) {
    ssize_t count = write(fd, buf + offset, len - offset);

    if (count < 0 && errno == EINTR) {
      if (tpm_proxy_cancelled()) return false;
      continue;
    }
    if (count <= 0) return false;

    offset += (size_t)count;
  }

  return true;
}

static void tpm_proxy_serve(void) {
  uint8_t command[TPM_TIS_FIFO_SIZE];
  uint8_t response[TPM_TIS_MAX_RESPONSE];
  char device_path[32];
  bool open_error_logged = false;
  int fd = -1;

  for (;;) {
    struct pollfd poll_fd;
    size_t command_len = 0;
    size_t response_len = 0;
    int ret;

    if (tpm_proxy_cancelled()) break;

    if (fd < 0) {
      fd = vtpm_proxy_open(device_path, sizeof(device_path));
      if (fd < 0) {
        if (!open_error_logged) {
          LOG_ERR("cannot create the vTPM chardev; retrying\n");
          open_error_logged = true;
        }
        tpm_proxy_sleep_ms(1000);
        continue;
      }

      open_error_logged = false;
      LOG("vTPM chardev registered: %s\n", device_path);
    }

    poll_fd.fd = fd;
    poll_fd.events = POLLIN;
    poll_fd.revents = 0;

    ret = poll(&poll_fd, 1, 250);
    if (ret < 0) {
      if (errno == EINTR) continue;

      LOG_ERR("vTPM poll failed: %s\n", strerror(errno));
      close(fd);
      fd = -1;
      tpm_proxy_sleep_ms(500);
      continue;
    }
    if (ret == 0) continue;

    if ((poll_fd.revents & POLLIN) == 0) {
      if ((poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        LOG("vTPM chardev disconnected\n");
        close(fd);
        fd = -1;
        tpm_proxy_sleep_ms(500);
      }
      continue;
    }

    if (!tpm_read_command(fd, command, sizeof(command), &command_len)) {
      if (tpm_proxy_cancelled()) break;

      LOG_ERR("vTPM chardev read failed: %s\n", strerror(errno));
      close(fd);
      fd = -1;
      tpm_proxy_sleep_ms(500);
      continue;
    }

    if (!tpm_transact_cached(command, command_len, response, sizeof(response), &response_len)) {
      LOG("TPM command failed\n");
      if (!tpm_make_response(TPM_RC_FAILURE, response, sizeof(response), &response_len)) {
        continue;
      }
    }

    if (!tpm_write_all(fd, response, response_len)) {
      if (tpm_proxy_cancelled()) break;

      LOG_ERR("vTPM chardev write failed: %s\n", strerror(errno));
      close(fd);
      fd = -1;
      tpm_proxy_sleep_ms(500);
    }
  }

  if (fd >= 0) close(fd);
}

static void *tpm_proxy_main(void *arg) {
  bool ready;

  (void)arg;

  thread_ctx_init(THREAD_ROLE_WORKER, "vtpm-proxy");
  LOG("waiting for the emulated TPM\n");

  pthread_mutex_lock(&transact_lock);
  tpm_ready = tis_ensure_ready(TPM_PROXY_READY_TIMEOUT_MS);
  ready = tpm_ready;
  pthread_mutex_unlock(&transact_lock);

  if (!ready) {
    if (!tpm_proxy_cancelled()) LOG_ERR("emulated TPM did not become ready\n");
    return NULL;
  }

  LOG("emulated TPM is ready\n");

  if (!tpm_prepare(TPM_PROXY_READY_TIMEOUT_MS) && !tpm_proxy_cancelled()) {
    LOG("failed to prime the TPM capability cache\n");
  }

  tpm_proxy_serve();
  return NULL;
}

bool TPM_ProxyStart(void) {
  int err;

  pthread_mutex_lock(&proxy_lock);

  if (proxy_started) {
    pthread_mutex_unlock(&proxy_lock);
    return true;
  }

  atomic_store(&proxy_stop, false);

  err = pthread_create(&proxy_thread, NULL, tpm_proxy_main, NULL);
  if (err != 0) {
    LOG_ERR("Failed to start the vTPM proxy: %s\n", strerror(err));
    pthread_mutex_unlock(&proxy_lock);
    return false;
  }

  (void)pthread_setname_np(proxy_thread, "vtpm-proxy");
  proxy_started = true;

  pthread_mutex_unlock(&proxy_lock);
  return true;
}

void TPM_ProxyStop(void) {
  pthread_t thread;
  bool started;

  pthread_mutex_lock(&proxy_lock);
  started = proxy_started;
  thread = proxy_thread;
  proxy_started = false;
  atomic_store(&proxy_stop, true);
  pthread_mutex_unlock(&proxy_lock);

  if (started) pthread_join(thread, NULL);
}

#endif /* SINGLE_THREAD */
