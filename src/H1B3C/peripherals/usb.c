#include <H1B3C/peripherals/usb.h>
#include <H1B3C/peripherals/m3.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <common/common.h>
#include <common/logger.h>
#include <common/peripherals/define.h>

#define USB_IRQ 193
#define USB_MAX_PACKET 64
#define USB_ENDPOINTS 16
#define USB_MAX_OUT_BUFFER (1u << 20)
#define USB_IN_SCRATCH_SIZE (1u << 16)

#define GRSTCTL_AHBIDLE (1u << 31)
#define GRSTCTL_CSFTRST (1u << 0)

#define GAHBCFG_GLBLINTRMSK (1u << 0)

#define DCTL_SFTDISCON (1u << 1)
#define DCTL_CGOUTNAK (1u << 10)
#define DCTL_CGNPINNAK (1u << 8)

#define GINTSTS_GINNAKEFF (1u << 6)
#define GINTSTS_GOUTNAKEFF (1u << 7)
#define GINTSTS_USBRST (1u << 12)
#define GINTSTS_IEPINT (1u << 18)
#define GINTSTS_OEPINT (1u << 19)

#define DXEPCTL_MPS_MASK 0x7FF
#define DXEPCTL_USBACTEP (1u << 15)
#define DXEPCTL_DPID (1u << 16)
#define DXEPCTL_NAKSTS (1u << 17)
#define DXEPCTL_STALL (1u << 21)
#define DXEPCTL_CNAK (1u << 26)
#define DXEPCTL_SNAK (1u << 27)
#define DXEPCTL_SETD0PID (1u << 28)
#define DXEPCTL_SETD1PID (1u << 29)
#define DXEPCTL_EPDIS (1u << 30)
#define DXEPCTL_EPENA (1u << 31)

#define DIEPINT_XFERCOMPL (1u << 0)
#define DOEPINT_XFERCOMPL (1u << 0)
#define DOEPINT_SETUP (1u << 3)
#define DOEPINT_STSPHSERCVD (1u << 5)

#define USB_EPREG_CTL 0x00u
#define USB_EPREG_INT 0x08u
#define USB_EPREG_TSIZ 0x10u
#define USB_EPREG_DMA 0x14u
#define USB_EPREG_DTXFSTS 0x18u
#define USB_EPREG_DMAB 0x1Cu

#define DESC_BYTES_MASK 0xFFFFu
#define DESC_SR (1u << 24)
#define DESC_IOC (1u << 25)
#define DESC_SP (1u << 26)
#define DESC_LAST (1u << 27)
#define DESC_STATUS_MASK (0x3u << 30)
#define DESC_HOST_READY (0x0u << 30)
#define DESC_DMA_DONE (0x2u << 30)

enum {
  USB_EP0_IDLE = 0,
  USB_EP0_IN_WAIT,
  USB_EP0_OUT_WAIT,
  USB_EP0_IN_SENT,
  USB_EP0_STATUS_WAIT,
  USB_EP0_STATUS_SENT,
};

enum {
  USB_EP0_XFER_NONE = 0,
  USB_EP0_XFER_IN,
  USB_EP0_XFER_STATUS,
};

enum {
  USB_HOST_OP_SETUP = 0,
  USB_HOST_OP_EP_OUT,
  USB_HOST_OP_EP_IN_DONE,
  USB_HOST_OP_EP0_DONE,
  USB_HOST_OP_RESET,
};

typedef struct UsbHostEvent {
  unsigned kind;
  uint8_t address;
  uint8_t setup[8];
  uint8_t *data;
  size_t len;
  struct UsbHostEvent *next;
} UsbHostEvent;

typedef struct {
  uint32_t gintsts;
  uint32_t diepctl[USB_ENDPOINTS];
  uint32_t diepint[USB_ENDPOINTS];
  uint32_t dieptsiz[USB_ENDPOINTS];
  uint32_t diepdma[USB_ENDPOINTS];
  uint32_t doepctl[USB_ENDPOINTS];
  uint32_t doepint[USB_ENDPOINTS];
  uint32_t doeptsiz[USB_ENDPOINTS];
  uint32_t doepdma[USB_ENDPOINTS];
  uint32_t dieptxf[USB_ENDPOINTS];
  uint8_t *out_data[USB_ENDPOINTS];
  size_t out_len[USB_ENDPOINTS];
  size_t out_cap[USB_ENDPOINTS];
  bool in_inflight[USB_ENDPOINTS];
  bool out_enabled[USB_ENDPOINTS];
  bool endpoint_enabled[USB_ENDPOINTS * 2];
  bool pending_enable[USB_ENDPOINTS * 2];
  uint16_t endpoint_mps[USB_ENDPOINTS * 2];
  uint8_t ep0_setup[8];
  uint8_t ep0_phase;
  uint8_t ep0_transfer;
  uint16_t ep0_length;
  bool usb_configured;
  bool post_reset_pending;
  bool connected;
  bool want_connect;
} UsbState;

DEFINE_BASIC_REGISTER(gotgctl)
DEFINE_BASIC_REGISTER(gotgint)
DEFINE_BASIC_REGISTER(gahbcfg)
DEFINE_BASIC_REGISTER(gusbcfg)
DEFINE_BASIC_REGISTER(grstctl)
DEFINE_BASIC_REGISTER(gintsts)
DEFINE_BASIC_REGISTER(gintmsk)
DEFINE_BASIC_REGISTER(grxstsr)
DEFINE_BASIC_REGISTER(grxstsp)
DEFINE_BASIC_REGISTER(grxfsiz)
DEFINE_BASIC_REGISTER(gnptxfsiz)
DEFINE_BASIC_REGISTER(ggpio)
DEFINE_BASIC_REGISTER(guid)
DEFINE_BASIC_REGISTER(gsnpsid)
DEFINE_BASIC_REGISTER(ghwcfg1)
DEFINE_BASIC_REGISTER(ghwcfg2)
DEFINE_BASIC_REGISTER(ghwcfg3)
DEFINE_BASIC_REGISTER(ghwcfg4)
DEFINE_BASIC_REGISTER(gdfifocfg)
DEFINE_BASIC_REGISTER(dcfg)
DEFINE_BASIC_REGISTER(dctl)
DEFINE_BASIC_REGISTER(dsts)
DEFINE_BASIC_REGISTER(diepmsk)
DEFINE_BASIC_REGISTER(doepmsk)
DEFINE_BASIC_REGISTER(daint)
DEFINE_BASIC_REGISTER(daintmsk)
DEFINE_BASIC_REGISTER(dvbusdis)
DEFINE_BASIC_REGISTER(dvbuspulse)
DEFINE_BASIC_REGISTER(dthrctl)
DEFINE_BASIC_REGISTER(diepempmsk)
DEFINE_BASIC_REGISTER(pcgcctl)

static const BasicRegister usb_registers[] = {
  BASIC_REGISTER_ENTRY(REG_USB_GOTGCTL_OFFSET, gotgctl),
  BASIC_REGISTER_ENTRY(REG_USB_GOTGINT_OFFSET, gotgint),
  BASIC_REGISTER_ENTRY(REG_USB_GAHBCFG_OFFSET, gahbcfg),
  BASIC_REGISTER_ENTRY(REG_USB_GUSBCFG_OFFSET, gusbcfg),
  BASIC_REGISTER_ENTRY(REG_USB_GRSTCTL_OFFSET, grstctl),
  BASIC_REGISTER_ENTRY(REG_USB_GINTSTS_OFFSET, gintsts),
  BASIC_REGISTER_ENTRY(REG_USB_GINTMSK_OFFSET, gintmsk),
  BASIC_REGISTER_ENTRY(REG_USB_GRXSTSR_OFFSET, grxstsr),
  BASIC_REGISTER_ENTRY(REG_USB_GRXSTSP_OFFSET, grxstsp),
  BASIC_REGISTER_ENTRY(REG_USB_GRXFSIZ_OFFSET, grxfsiz),
  BASIC_REGISTER_ENTRY(REG_USB_GNPTXFSIZ_OFFSET, gnptxfsiz),
  BASIC_REGISTER_ENTRY(REG_USB_GGPIO_OFFSET, ggpio),
  BASIC_REGISTER_ENTRY(REG_USB_GUID_OFFSET, guid),
  BASIC_REGISTER_ENTRY(REG_USB_GSNPSID_OFFSET, gsnpsid),
  BASIC_REGISTER_ENTRY(REG_USB_GHWCFG1_OFFSET, ghwcfg1),
  BASIC_REGISTER_ENTRY(REG_USB_GHWCFG2_OFFSET, ghwcfg2),
  BASIC_REGISTER_ENTRY(REG_USB_GHWCFG3_OFFSET, ghwcfg3),
  BASIC_REGISTER_ENTRY(REG_USB_GHWCFG4_OFFSET, ghwcfg4),
  BASIC_REGISTER_ENTRY(REG_USB_GDFIFOCFG_OFFSET, gdfifocfg),
  BASIC_REGISTER_ENTRY(REG_USB_DCFG_OFFSET, dcfg),
  BASIC_REGISTER_ENTRY(REG_USB_DCTL_OFFSET, dctl),
  BASIC_REGISTER_ENTRY(REG_USB_DSTS_OFFSET, dsts),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPMSK_OFFSET, diepmsk),
  BASIC_REGISTER_ENTRY(REG_USB_DOEPMSK_OFFSET, doepmsk),
  BASIC_REGISTER_ENTRY(REG_USB_DAINT_OFFSET, daint),
  BASIC_REGISTER_ENTRY(REG_USB_DAINTMSK_OFFSET, daintmsk),
  BASIC_REGISTER_ENTRY(REG_USB_DVBUSDIS_OFFSET, dvbusdis),
  BASIC_REGISTER_ENTRY(REG_USB_DVBUSPULSE_OFFSET, dvbuspulse),
  BASIC_REGISTER_ENTRY(REG_USB_DTHRCTL_OFFSET, dthrctl),
  BASIC_REGISTER_ENTRY(REG_USB_DIEPEMPMSK_OFFSET, diepempmsk),
  BASIC_REGISTER_ENTRY(REG_USB_PCGCCTL_OFFSET, pcgcctl),
};

static UsbState usb_state;
static const UsbHostOps *usb_host_ops;
static Component *usb_component;
static uint8_t usb_in_scratch[USB_IN_SCRATCH_SIZE];

static pthread_mutex_t usb_host_lock = PTHREAD_MUTEX_INITIALIZER;
static UsbHostEvent *usb_host_head;
static UsbHostEvent *usb_host_tail;

static int usb_endpoint_index(bool is_in, unsigned endpoint) {
  return (int)((is_in ? USB_ENDPOINTS : 0u) + endpoint);
}

static bool usb_decode_endpoint(uint64_t offset, bool *is_in, unsigned *endpoint,
                                unsigned *reg) {
  uint64_t relative;

  if (offset >= REG_USB_DIEP_BLOCK_OFFSET && offset < REG_USB_DOEP_BLOCK_OFFSET) {
    *is_in = true;
    relative = offset - REG_USB_DIEP_BLOCK_OFFSET;
  } else if (offset >= REG_USB_DOEP_BLOCK_OFFSET &&
             offset < REG_USB_DOEP_BLOCK_OFFSET + REG_USB_DOEP_BLOCK_COUNT * REG_USB_ENDPOINT_STRIDE) {
    *is_in = false;
    relative = offset - REG_USB_DOEP_BLOCK_OFFSET;
  } else {
    return false;
  }

  *endpoint = (unsigned)(relative / REG_USB_ENDPOINT_STRIDE);
  *reg = (unsigned)(relative % REG_USB_ENDPOINT_STRIDE);

  return *endpoint < USB_ENDPOINTS;
}

static bool usb_mem_read(uint32_t address, void *data, size_t len) {
  if (ctx.uc == NULL) return false;
  return uc_mem_read(ctx.uc, address, data, len) == UC_ERR_OK;
}

static bool usb_mem_write(uint32_t address, const void *data, size_t len) {
  if (ctx.uc == NULL) return false;
  return uc_mem_write(ctx.uc, address, data, len) == UC_ERR_OK;
}

static bool usb_read_descriptor(uint32_t address, uint32_t *flags, uint32_t *buffer) {
  uint8_t raw[8];

  if (!usb_mem_read(address, raw, sizeof(raw))) return false;

  *flags = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) |
           ((uint32_t)raw[3] << 24);
  *buffer = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8) | ((uint32_t)raw[6] << 16) |
            ((uint32_t)raw[7] << 24);
  return true;
}

static bool usb_write_descriptor(uint32_t address, uint32_t flags, uint32_t buffer) {
  uint8_t raw[8];

  raw[0] = (uint8_t)flags;
  raw[1] = (uint8_t)(flags >> 8);
  raw[2] = (uint8_t)(flags >> 16);
  raw[3] = (uint8_t)(flags >> 24);
  raw[4] = (uint8_t)buffer;
  raw[5] = (uint8_t)(buffer >> 8);
  raw[6] = (uint8_t)(buffer >> 16);
  raw[7] = (uint8_t)(buffer >> 24);

  return usb_mem_write(address, raw, sizeof(raw));
}

static uint32_t usb_daint(void) {
  uint32_t value = 0;
  unsigned endpoint;

  for (endpoint = 0; endpoint < USB_ENDPOINTS; endpoint++) {
    if (usb_state.diepint[endpoint] != 0) value |= 1u << endpoint;
    if (usb_state.doepint[endpoint] != 0) value |= 1u << (16u + endpoint);
  }

  return value & daintmsk;
}

static void usb_update_irq(void) {
  uint32_t daint = usb_daint();
  uint32_t status;

  if ((daint & 0xFFFFu) != 0) {
    usb_state.gintsts |= GINTSTS_IEPINT;
  } else {
    usb_state.gintsts &= ~GINTSTS_IEPINT;
  }

  if ((daint & 0xFFFF0000u) != 0) {
    usb_state.gintsts |= GINTSTS_OEPINT;
  } else {
    usb_state.gintsts &= ~GINTSTS_OEPINT;
  }

  gintsts = usb_state.gintsts;
  status = usb_state.gintsts & gintmsk;

  if ((gahbcfg & GAHBCFG_GLBLINTRMSK) != 0 && status != 0) {
    M3_PendIrq(USB_IRQ);
  } else {
    M3_UnpendIrq(USB_IRQ);
  }
}

static bool usb_out_append(unsigned endpoint, const uint8_t *data, size_t len) {
  size_t needed;
  size_t capacity;
  uint8_t *grown;

  if (endpoint >= USB_ENDPOINTS) return false;
  if (len == 0) return true;
  if (len > USB_MAX_OUT_BUFFER - usb_state.out_len[endpoint]) {
    LOG_ERR("USB: OUT buffer overflow on endpoint %u\n", endpoint);
    return false;
  }

  needed = usb_state.out_len[endpoint] + len;

  if (needed > usb_state.out_cap[endpoint]) {
    capacity = usb_state.out_cap[endpoint] == 0 ? 256 : usb_state.out_cap[endpoint];

    while (capacity < needed) {
      if (capacity > USB_MAX_OUT_BUFFER / 2) {
        capacity = USB_MAX_OUT_BUFFER;
        break;
      }
      capacity *= 2;
    }

    grown = realloc(usb_state.out_data[endpoint], capacity);

    if (grown == NULL) {
      LOG_ERR("USB: failed to grow OUT buffer on endpoint %u\n", endpoint);
      return false;
    }

    usb_state.out_data[endpoint] = grown;
    usb_state.out_cap[endpoint] = capacity;
  }

  memcpy(usb_state.out_data[endpoint] + usb_state.out_len[endpoint], data, len);
  usb_state.out_len[endpoint] = needed;
  return true;
}

static void usb_out_consume(unsigned endpoint, size_t len) {
  if (endpoint >= USB_ENDPOINTS) return;
  if (len >= usb_state.out_len[endpoint]) {
    usb_state.out_len[endpoint] = 0;
    return;
  }

  memmove(usb_state.out_data[endpoint], usb_state.out_data[endpoint] + len,
          usb_state.out_len[endpoint] - len);
  usb_state.out_len[endpoint] -= len;
}

static void usb_reset_controller(void) {
  unsigned endpoint;

  for (endpoint = 0; endpoint < USB_ENDPOINTS; endpoint++) {
    usb_state.diepctl[endpoint] = 0;
    usb_state.diepint[endpoint] = 0;
    usb_state.dieptsiz[endpoint] = 0;
    usb_state.diepdma[endpoint] = 0;
    usb_state.doepctl[endpoint] = 0;
    usb_state.doepint[endpoint] = 0;
    usb_state.doeptsiz[endpoint] = 0;
    usb_state.doepdma[endpoint] = 0;
    usb_state.in_inflight[endpoint] = false;
    usb_state.out_enabled[endpoint] = false;
    usb_state.out_len[endpoint] = 0;
  }

  memset(usb_state.endpoint_enabled, 0, sizeof(usb_state.endpoint_enabled));
  memset(usb_state.pending_enable, 0, sizeof(usb_state.pending_enable));
  memset(usb_state.endpoint_mps, 0, sizeof(usb_state.endpoint_mps));

  usb_state.usb_configured = false;
  usb_state.post_reset_pending = false;
  usb_state.ep0_phase = USB_EP0_IDLE;
  usb_state.ep0_transfer = USB_EP0_XFER_NONE;
  usb_state.ep0_length = 0;
}

static void usb_notify_enable(unsigned endpoint, bool is_in, uint32_t value) {
  int index = usb_endpoint_index(is_in, endpoint);
  uint16_t mps = (uint16_t)(value & DXEPCTL_MPS_MASK);
  uint8_t address = (uint8_t)(is_in ? (0x80u | endpoint) : endpoint);

  if (endpoint == 0 || usb_host_ops == NULL) return;

  if (mps == 0) mps = USB_MAX_PACKET;
  usb_state.endpoint_mps[index] = mps;

  if (!usb_state.usb_configured) {
    usb_state.pending_enable[index] = true;
    return;
  }

  if (usb_state.endpoint_enabled[index]) return;

  usb_state.endpoint_enabled[index] = true;
  usb_host_ops->endpoint_enable(address, 2, mps, 0);
}

static void usb_flush_endpoint_enables(void) {
  unsigned index;

  if (usb_host_ops == NULL) return;

  for (index = 0; index < USB_ENDPOINTS * 2; index++) {
    uint8_t address;
    bool is_in;

    if (!usb_state.pending_enable[index]) continue;
    if (index == 0 || index == USB_ENDPOINTS) continue;

    usb_state.pending_enable[index] = false;
    is_in = index >= USB_ENDPOINTS;
    address = (uint8_t)(is_in ? (0x80u | (index - USB_ENDPOINTS)) : index);

    if (!is_in) usb_state.out_enabled[index] = false;
    if (usb_state.endpoint_enabled[index]) continue;

    usb_state.endpoint_enabled[index] = true;
    usb_host_ops->endpoint_enable(address, 2, usb_state.endpoint_mps[index], 0);
  }
}

static bool usb_read_in_chain(unsigned endpoint, size_t max_len, size_t *out_len) {
  uint32_t pointer = usb_state.diepdma[endpoint];
  uint32_t offset = 0;
  size_t total = 0;
  unsigned count;

  if (pointer == 0) return false;

  for (count = 0; count < 16; count++) {
    uint32_t flags;
    uint32_t buffer;
    uint32_t length;

    if (!usb_read_descriptor(pointer + offset, &flags, &buffer)) return false;
    if ((flags & DESC_STATUS_MASK) != DESC_HOST_READY) return false;

    length = flags & DESC_BYTES_MASK;

    if (length != 0) {
      if (length > USB_IN_SCRATCH_SIZE - total) {
        LOG_ERR("USB: IN chain too large on endpoint %u\n", endpoint);
        return false;
      }

      if (!usb_mem_read(buffer, usb_in_scratch + total, length)) return false;
    }

    total += length;
    flags &= ~(DESC_BYTES_MASK | DESC_STATUS_MASK);
    flags |= DESC_DMA_DONE;

    if (!usb_write_descriptor(pointer + offset, flags, buffer)) return false;

    if ((flags & DESC_LAST) != 0) break;

    offset += 8;
  }

  if (count == 16) return false;

  if (max_len != 0 && total > max_len) total = max_len;

  *out_len = total;
  return true;
}

static void usb_handle_ep0_in(void) {
  size_t len;

  if (usb_host_ops == NULL) return;

  if (usb_state.ep0_phase == USB_EP0_IN_WAIT) {
    if (!usb_read_in_chain(0, usb_state.ep0_length, &len)) return;

    usb_state.ep0_phase = USB_EP0_IN_SENT;
    usb_state.ep0_transfer = USB_EP0_XFER_IN;
    usb_host_ops->ep0_in(usb_in_scratch, len);
    return;
  }

  if (usb_state.ep0_phase == USB_EP0_STATUS_WAIT) {
    usb_state.ep0_phase = USB_EP0_STATUS_SENT;
    usb_state.ep0_transfer = USB_EP0_XFER_STATUS;
    usb_host_ops->ep0_status();
  }
}

static void usb_handle_bulk_in(unsigned endpoint) {
  size_t len;

  if (usb_host_ops == NULL) return;
  if (usb_state.in_inflight[endpoint]) return;
  if (!usb_read_in_chain(endpoint, 0, &len)) return;

  usb_state.in_inflight[endpoint] = true;
  usb_host_ops->ep_in((uint8_t)(0x80u | endpoint), usb_in_scratch, len);
}

static void usb_drain_out(unsigned endpoint) {
  uint32_t pointer;
  uint32_t flags;
  uint32_t buffer;
  uint32_t remaining;
  size_t take;

  if (!usb_state.out_enabled[endpoint]) return;
  if (usb_state.out_len[endpoint] == 0) return;

  pointer = usb_state.doepdma[endpoint];
  if (pointer == 0) return;

  if (!usb_read_descriptor(pointer, &flags, &buffer)) return;

  remaining = flags & DESC_BYTES_MASK;
  if (remaining == 0) remaining = USB_MAX_PACKET;

  take = usb_state.out_len[endpoint];
  if (take > remaining) take = remaining;

  if (!usb_mem_write(buffer, usb_state.out_data[endpoint], take)) {
    LOG_ERR("USB: failed to write OUT data to 0x%08x\n", buffer);
    return;
  }

  usb_out_consume(endpoint, take);

  flags &= ~(DESC_BYTES_MASK | DESC_STATUS_MASK | DESC_SR);
  flags |= (remaining - take) & DESC_BYTES_MASK;
  flags |= DESC_DMA_DONE;

  if (!usb_write_descriptor(pointer, flags, buffer)) return;

  usb_state.out_enabled[endpoint] = false;
  usb_state.doepint[endpoint] |= DOEPINT_XFERCOMPL;
  usb_state.gintsts |= GINTSTS_OEPINT;
  usb_update_irq();
}

static void usb_arm_out(unsigned endpoint) {
  uint8_t address;

  if (endpoint == 0) return;

  usb_state.out_enabled[endpoint] = true;

  if (usb_state.out_len[endpoint] != 0) {
    usb_drain_out(endpoint);
    return;
  }

  if (usb_host_ops == NULL) return;

  address = (uint8_t)endpoint;
  usb_host_ops->out_enable(address);
}

static void usb_arm_in(unsigned endpoint) {
  if (endpoint == 0) {
    usb_handle_ep0_in();
  } else {
    usb_handle_bulk_in(endpoint);
  }
}

static void usb_write_diepctl(unsigned endpoint, uint32_t value) {
  if ((value & DXEPCTL_SETD0PID) != 0) value &= ~DXEPCTL_DPID;
  if ((value & DXEPCTL_SETD1PID) != 0) value |= DXEPCTL_DPID;
  if ((value & DXEPCTL_CNAK) != 0) value &= ~DXEPCTL_NAKSTS;
  if ((value & DXEPCTL_SNAK) != 0) value |= DXEPCTL_NAKSTS;
  if ((value & DXEPCTL_EPDIS) != 0) value &= ~DXEPCTL_EPENA;

  value &= ~(DXEPCTL_CNAK | DXEPCTL_SNAK | DXEPCTL_SETD0PID | DXEPCTL_SETD1PID |
             DXEPCTL_EPDIS);

  usb_state.diepctl[endpoint] = value;

  if (endpoint == 0 && (value & DXEPCTL_STALL) != 0) {
    usb_state.ep0_phase = USB_EP0_IDLE;
    usb_state.ep0_transfer = USB_EP0_XFER_NONE;

    if (usb_host_ops != NULL) usb_host_ops->ep0_stall();
    return;
  }

  if ((value & DXEPCTL_USBACTEP) != 0) usb_notify_enable(endpoint, true, value);
  if ((value & DXEPCTL_EPENA) != 0) usb_arm_in(endpoint);
}

static void usb_write_doepctl(unsigned endpoint, uint32_t value) {
  if ((value & DXEPCTL_SETD0PID) != 0) value &= ~DXEPCTL_DPID;
  if ((value & DXEPCTL_SETD1PID) != 0) value |= DXEPCTL_DPID;
  if ((value & DXEPCTL_CNAK) != 0) value &= ~DXEPCTL_NAKSTS;
  if ((value & DXEPCTL_SNAK) != 0) value |= DXEPCTL_NAKSTS;
  if ((value & DXEPCTL_EPDIS) != 0) value &= ~DXEPCTL_EPENA;

  value &= ~(DXEPCTL_CNAK | DXEPCTL_SNAK | DXEPCTL_SETD0PID | DXEPCTL_SETD1PID |
             DXEPCTL_EPDIS);

  usb_state.doepctl[endpoint] = value;

  if (endpoint == 0 && (value & DXEPCTL_STALL) != 0) {
    usb_state.ep0_phase = USB_EP0_IDLE;
    usb_state.ep0_transfer = USB_EP0_XFER_NONE;

    if (usb_host_ops != NULL) usb_host_ops->ep0_stall();
    return;
  }

  if ((value & DXEPCTL_USBACTEP) != 0) usb_notify_enable(endpoint, false, value);
  if ((value & DXEPCTL_EPENA) != 0) usb_arm_out(endpoint);
}

static void usb_write_dctl(uint32_t value) {
  bool disconnected;

  dctl = value;

  if ((value & DCTL_CGOUTNAK) != 0) usb_state.gintsts &= ~GINTSTS_GOUTNAKEFF;
  if ((value & DCTL_CGNPINNAK) != 0) usb_state.gintsts &= ~GINTSTS_GINNAKEFF;

  disconnected = (value & DCTL_SFTDISCON) != 0;

  if (disconnected && usb_state.connected) {
    usb_state.connected = false;
    usb_state.want_connect = false;
    if (usb_host_ops != NULL) usb_host_ops->disconnect();
  } else if (!disconnected && !usb_state.connected) {
    usb_state.connected = true;
    usb_state.want_connect = true;
    if (usb_host_ops != NULL) usb_host_ops->connect();
    usb_state.want_connect = false;
  }

  usb_update_irq();
}

static void usb_handle_setup(const uint8_t setup[8]) {
  bool direction_in = (setup[0] & 0x80u) != 0;
  uint16_t length = (uint16_t)(setup[6] | ((uint16_t)setup[7] << 8));
  uint32_t pointer;
  uint32_t flags;
  uint32_t buffer;

  memcpy(usb_state.ep0_setup, setup, 8);

  if (setup[0] == 0 && setup[1] == 9) {
    uint16_t value = (uint16_t)(setup[2] | ((uint16_t)setup[3] << 8));

    if (value != 0) {
      usb_state.usb_configured = true;
      usb_flush_endpoint_enables();
    } else {
      usb_state.usb_configured = false;
    }
  }

  usb_state.ep0_length = length;

  if (length != 0) {
    usb_state.ep0_phase = direction_in ? USB_EP0_IN_WAIT : USB_EP0_OUT_WAIT;
  } else {
    usb_state.ep0_phase = USB_EP0_STATUS_WAIT;
  }

  pointer = usb_state.doepdma[0];
  if (pointer == 0) return;
  if (!usb_read_descriptor(pointer, &flags, &buffer)) return;

  if (!usb_mem_write(buffer, setup, 8)) return;

  flags &= ~(DESC_BYTES_MASK | DESC_STATUS_MASK | DESC_SR);
  flags |= DESC_DMA_DONE | DESC_SR | (USB_MAX_PACKET - 8);

  if (!usb_write_descriptor(pointer, flags, buffer)) return;

  usb_state.doepint[0] |= DOEPINT_SETUP | DOEPINT_XFERCOMPL;
  usb_state.gintsts |= GINTSTS_OEPINT;
  usb_update_irq();
}

static void usb_handle_ep_out(uint8_t address, const uint8_t *data, size_t len) {
  unsigned endpoint = address & 0x7Fu;
  static bool post_reset_logged;

  if (endpoint == 0 || endpoint >= USB_ENDPOINTS) return;

  if (endpoint == 4 && len == 14 && memcmp(data + 8, "\xba\xcc\xd0\x0a", 4) == 0 &&
      data[12] == 0 && data[13] == 7) {
    usb_state.post_reset_pending = true;
  }

  if (!usb_out_append(endpoint, data, len)) return;
  if (usb_state.out_enabled[endpoint]) usb_drain_out(endpoint);

  if (usb_state.post_reset_pending && !post_reset_logged) {
    post_reset_logged = true;
    LOG("USB: CCD post-reset requested; guest reboot is not wired up yet\n");
  }
}

static void usb_handle_ep_in_done(uint8_t address) {
  unsigned endpoint = address & 0x7Fu;

  if (endpoint == 0 || endpoint >= USB_ENDPOINTS) return;

  usb_state.in_inflight[endpoint] = false;
  usb_state.diepint[endpoint] |= DIEPINT_XFERCOMPL;
  usb_state.gintsts |= GINTSTS_IEPINT;
  usb_update_irq();
  usb_handle_bulk_in(endpoint);

  if (endpoint == 4 && usb_state.post_reset_pending) {
    usb_state.post_reset_pending = false;
  }
}

static void usb_handle_ep0_done(void) {
  uint32_t pointer;

  if (usb_state.ep0_transfer == USB_EP0_XFER_NONE) return;

  pointer = usb_state.doepdma[0];

  if (pointer != 0) {
    uint32_t flags;
    uint32_t buffer;

    if (usb_read_descriptor(pointer, &flags, &buffer)) {
      flags &= ~(DESC_BYTES_MASK | DESC_STATUS_MASK | DESC_SR);
      flags |= DESC_DMA_DONE | USB_MAX_PACKET;

      if (usb_write_descriptor(pointer, flags, buffer)) {
        if (usb_state.ep0_transfer == USB_EP0_XFER_IN) {
          usb_state.doepint[0] |= DOEPINT_XFERCOMPL;
        } else {
          usb_state.doepint[0] |= DOEPINT_XFERCOMPL | DOEPINT_STSPHSERCVD;
        }

        usb_state.gintsts |= GINTSTS_OEPINT;
      }
    }
  }

  usb_state.ep0_transfer = USB_EP0_XFER_NONE;
  usb_state.ep0_phase = USB_EP0_IDLE;
  usb_update_irq();
}

static void usb_handle_host_reset(void) {
  usb_reset_controller();
  usb_state.gintsts |= GINTSTS_USBRST;
  usb_update_irq();
}

static void usb_process_host_event(UsbHostEvent *event) {
  switch (event->kind) {
    case USB_HOST_OP_SETUP:
      usb_handle_setup(event->setup);
      break;
    case USB_HOST_OP_EP_OUT:
      usb_handle_ep_out(event->address, event->data, event->len);
      break;
    case USB_HOST_OP_EP_IN_DONE:
      usb_handle_ep_in_done(event->address);
      break;
    case USB_HOST_OP_EP0_DONE:
      usb_handle_ep0_done();
      break;
    case USB_HOST_OP_RESET:
      usb_handle_host_reset();
      break;
    default:
      break;
  }
}

static void usb_component_poll(Component *self) {
  (void)self;
  USB_Poll();
}

void USB_Poll(void) {
  for (;;) {
    UsbHostEvent *event;

    pthread_mutex_lock(&usb_host_lock);
    event = usb_host_head;

    if (event != NULL) {
      usb_host_head = event->next;
      if (usb_host_head == NULL) usb_host_tail = NULL;
    }

    pthread_mutex_unlock(&usb_host_lock);

    if (event == NULL) return;

    usb_process_host_event(event);
    free(event->data);
    free(event);
  }
}

static bool usb_enqueue_host_event(UsbHostEvent *event) {
  event->next = NULL;

  pthread_mutex_lock(&usb_host_lock);

  if (usb_host_tail != NULL) {
    usb_host_tail->next = event;
  } else {
    usb_host_head = event;
  }

  usb_host_tail = event;
  pthread_mutex_unlock(&usb_host_lock);
  return true;
}

void USB_SetHostOps(const UsbHostOps *ops) {
  usb_host_ops = ops;
}

void USB_HostSetup(const uint8_t setup[8]) {
  UsbHostEvent *event = calloc(1, sizeof(*event));

  if (event == NULL) {
    LOG_ERR("USB: failed to allocate SETUP event\n");
    return;
  }

  event->kind = USB_HOST_OP_SETUP;
  memcpy(event->setup, setup, 8);
  usb_enqueue_host_event(event);
}

void USB_HostEpOut(uint8_t address, const uint8_t *data, size_t len) {
  UsbHostEvent *event;

  if (len > USB_MAX_OUT_BUFFER) {
    LOG_ERR("USB: dropping oversized OUT transfer (%zu bytes)\n", len);
    return;
  }

  event = calloc(1, sizeof(*event));

  if (event == NULL) {
    LOG_ERR("USB: failed to allocate EP_OUT event\n");
    return;
  }

  event->kind = USB_HOST_OP_EP_OUT;
  event->address = address;

  if (len != 0) {
    event->data = malloc(len);

    if (event->data == NULL) {
      free(event);
      LOG_ERR("USB: failed to allocate EP_OUT data\n");
      return;
    }

    memcpy(event->data, data, len);
    event->len = len;
  }

  usb_enqueue_host_event(event);
}

void USB_HostEpInDone(uint8_t address) {
  UsbHostEvent *event = calloc(1, sizeof(*event));

  if (event == NULL) {
    LOG_ERR("USB: failed to allocate EP_IN_DONE event\n");
    return;
  }

  event->kind = USB_HOST_OP_EP_IN_DONE;
  event->address = address;
  usb_enqueue_host_event(event);
}

void USB_HostEp0Done(void) {
  UsbHostEvent *event = calloc(1, sizeof(*event));

  if (event == NULL) {
    LOG_ERR("USB: failed to allocate EP0_DONE event\n");
    return;
  }

  event->kind = USB_HOST_OP_EP0_DONE;
  usb_enqueue_host_event(event);
}

void USB_HostReset(void) {
  UsbHostEvent *event = calloc(1, sizeof(*event));

  if (event == NULL) {
    LOG_ERR("USB: failed to allocate RESET event\n");
    return;
  }

  event->kind = USB_HOST_OP_RESET;
  usb_enqueue_host_event(event);
}

static uint64_t USB_Read(Component *self, uint64_t offset, unsigned size) {
  bool is_in;
  unsigned endpoint;
  unsigned reg;
  const BasicRegister *entry;

  (void)size;

  if (offset == REG_USB_GAHBCFG_OFFSET) return gahbcfg;
  if (offset == REG_USB_GUSBCFG_OFFSET) return gusbcfg;
  if (offset == REG_USB_GRSTCTL_OFFSET) return GRSTCTL_AHBIDLE;
  if (offset == REG_USB_GINTSTS_OFFSET) return usb_state.gintsts;
  if (offset == REG_USB_GINTMSK_OFFSET) return gintmsk;
  if (offset == REG_USB_GRXFSIZ_OFFSET) return grxfsiz;
  if (offset == REG_USB_GNPTXFSIZ_OFFSET) return gnptxfsiz;
  if (offset == REG_USB_GGPIO_OFFSET) return ggpio;
  if (offset == REG_USB_GSNPSID_OFFSET) return 0x4F54280A;
  if (offset == REG_USB_DAINT_OFFSET) return usb_daint();
  if (offset == REG_USB_DAINTMSK_OFFSET) return daintmsk;
  if (offset == REG_USB_DCFG_OFFSET) return dcfg;
  if (offset == REG_USB_DCTL_OFFSET) return dctl;
  if (offset == REG_USB_DSTS_OFFSET) return dsts;
  if (offset == REG_USB_DIEPMSK_OFFSET) return diepmsk;
  if (offset == REG_USB_DOEPMSK_OFFSET) return doepmsk;
  if (offset == REG_USB_DIEPEMPMSK_OFFSET) return diepempmsk;

  if (offset >= REG_USB_DIEPTXF_BLOCK_OFFSET &&
      offset < REG_USB_DIEPTXF_BLOCK_OFFSET +
                   REG_USB_DIEPTXF_BLOCK_COUNT * REG_USB_DIEPTXF_STRIDE) {
    endpoint = (unsigned)((offset - REG_USB_DIEPTXF_BLOCK_OFFSET) / REG_USB_DIEPTXF_STRIDE) + 1u;
    return usb_state.dieptxf[endpoint];
  }

  if (usb_decode_endpoint(offset, &is_in, &endpoint, &reg)) {
    switch (reg) {
      case USB_EPREG_CTL:
        return is_in ? usb_state.diepctl[endpoint] : usb_state.doepctl[endpoint];
      case USB_EPREG_INT:
        return is_in ? usb_state.diepint[endpoint] : usb_state.doepint[endpoint];
      case USB_EPREG_TSIZ:
        return is_in ? usb_state.dieptsiz[endpoint] : usb_state.doeptsiz[endpoint];
      case USB_EPREG_DMA:
        return is_in ? usb_state.diepdma[endpoint] : usb_state.doepdma[endpoint];
      case USB_EPREG_DTXFSTS:
        return 0x1000;
      case USB_EPREG_DMAB:
      default:
        return 0;
    }
  }

  entry = basic_register_find(usb_registers, ARRAY_LEN(usb_registers), offset);
  if (entry != NULL) return *entry->value;

  component_log_unhandled(self, "read", offset);
  return 0;
}

static void USB_Write(Component *self, uint64_t offset, unsigned size, uint64_t value) {
  bool is_in;
  unsigned endpoint;
  unsigned reg;
  const BasicRegister *entry;
  uint32_t written = (uint32_t)value;

  (void)size;

  if (offset == REG_USB_GAHBCFG_OFFSET) {
    gahbcfg = written;
    usb_update_irq();
    return;
  }

  if (offset == REG_USB_GUSBCFG_OFFSET) {
    gusbcfg = written;
    return;
  }

  if (offset == REG_USB_GRSTCTL_OFFSET) {
    if ((written & GRSTCTL_CSFTRST) != 0) usb_reset_controller();
    return;
  }

  if (offset == REG_USB_GINTSTS_OFFSET) {
    usb_state.gintsts &= ~written;
    usb_update_irq();
    return;
  }

  if (offset == REG_USB_GINTMSK_OFFSET) {
    gintmsk = written;
    usb_update_irq();
    return;
  }

  if (offset == REG_USB_GRXFSIZ_OFFSET) {
    grxfsiz = written;
    return;
  }

  if (offset == REG_USB_GNPTXFSIZ_OFFSET) {
    gnptxfsiz = written;
    return;
  }

  if (offset == REG_USB_GGPIO_OFFSET) {
    ggpio = written;
    return;
  }

  if (offset == REG_USB_DCFG_OFFSET) {
    dcfg = written;
    return;
  }

  if (offset == REG_USB_DCTL_OFFSET) {
    usb_write_dctl(written);
    return;
  }

  if (offset == REG_USB_DIEPMSK_OFFSET) {
    diepmsk = written;
    return;
  }

  if (offset == REG_USB_DOEPMSK_OFFSET) {
    doepmsk = written;
    return;
  }

  if (offset == REG_USB_DAINTMSK_OFFSET) {
    daintmsk = written;
    usb_update_irq();
    return;
  }

  if (offset == REG_USB_DIEPEMPMSK_OFFSET) {
    diepempmsk = written;
    return;
  }

  if (offset >= REG_USB_DIEPTXF_BLOCK_OFFSET &&
      offset < REG_USB_DIEPTXF_BLOCK_OFFSET +
                   REG_USB_DIEPTXF_BLOCK_COUNT * REG_USB_DIEPTXF_STRIDE) {
    endpoint = (unsigned)((offset - REG_USB_DIEPTXF_BLOCK_OFFSET) / REG_USB_DIEPTXF_STRIDE) + 1u;
    usb_state.dieptxf[endpoint] = written;
    return;
  }

  if (usb_decode_endpoint(offset, &is_in, &endpoint, &reg)) {
    switch (reg) {
      case USB_EPREG_CTL:
        if (is_in) {
          usb_write_diepctl(endpoint, written);
        } else {
          usb_write_doepctl(endpoint, written);
        }
        break;
      case USB_EPREG_INT:
        if (is_in) {
          usb_state.diepint[endpoint] &= ~written;
        } else {
          usb_state.doepint[endpoint] &= ~written;
        }
        usb_update_irq();
        break;
      case USB_EPREG_TSIZ:
        if (is_in) {
          usb_state.dieptsiz[endpoint] = written;
        } else {
          usb_state.doeptsiz[endpoint] = written;
        }
        break;
      case USB_EPREG_DMA:
        if (is_in) {
          usb_state.diepdma[endpoint] = written;
        } else {
          usb_state.doepdma[endpoint] = written;
        }
        break;
      case USB_EPREG_DMAB:
      case USB_EPREG_DTXFSTS:
      default:
        break;
    }
    return;
  }

  entry = basic_register_find(usb_registers, ARRAY_LEN(usb_registers), offset);
  if (entry != NULL) {
    *entry->value = written;
    return;
  }

  component_log_unhandled(self, "write", offset);
}

Component *USB_Initialize(void) {
  unsigned endpoint;

  usb_host_ops = NULL;
  usb_host_head = NULL;
  usb_host_tail = NULL;

  memset(&usb_state, 0, sizeof(usb_state));

  for (endpoint = 0; endpoint < USB_ENDPOINTS; endpoint++) {
    usb_state.out_data[endpoint] = NULL;
    usb_state.out_cap[endpoint] = 0;
  }

  gotgctl = 0;
  gotgint = 0;
  gahbcfg = 0;
  gusbcfg = 0;
  grstctl = 0;
  usb_state.gintsts = 0;
  gintmsk = 0;
  grxstsr = 0;
  grxstsp = 0;
  grxfsiz = 0;
  gnptxfsiz = 0;
  ggpio = 0;
  guid = 0;
  gsnpsid = 0;
  ghwcfg1 = 0;
  ghwcfg2 = 0;
  ghwcfg3 = 0;
  ghwcfg4 = 0;
  gdfifocfg = 0;
  dcfg = 0x08000000;
  dctl = DCTL_SFTDISCON;
  dsts = 0;
  diepmsk = 0;
  doepmsk = 0;
  daint = 0;
  daintmsk = 0;
  dvbusdis = 0;
  dvbuspulse = 0;
  dthrctl = 0;
  diepempmsk = 0;
  pcgcctl = 0;

  usb_component = component_register("USB", &usb_state, USB_Read, USB_Write);

  if (usb_component != NULL) component_set_poll(usb_component, usb_component_poll);

  return usb_component;
}
