#define _GNU_SOURCE

#include <H1B3C/usb-proxy.h>
#include <H1B3C/peripherals/usb.h>

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/raw_gadget.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <common/logger.h>
#include <common/runtime/thread.h>

#define USB_PROXY_UDC "dummy_udc.0"
#define USB_PROXY_DRIVER "dummy_udc"
#define USB_PROXY_MAX_ENDPOINTS 32
#define USB_PROXY_MAX_XFER 4096
#define USB_PROXY_CONTROL_TIMEOUT_MS 1000

#ifndef SINGLE_THREAD

enum {
  USB_PROXY_MSG_CONNECT = 0,
  USB_PROXY_MSG_DISCONNECT,
  USB_PROXY_MSG_EP_ENABLE,
  USB_PROXY_MSG_EP_DISABLE,
  USB_PROXY_MSG_OUT_ENABLE,
  USB_PROXY_MSG_EP_IN,
  USB_PROXY_MSG_EP0_IN,
  USB_PROXY_MSG_EP0_STATUS,
  USB_PROXY_MSG_EP0_STALL,
  USB_PROXY_MSG_EP_STALL,
};

typedef struct UsbProxyMessage {
  unsigned kind;
  uint8_t address;
  uint8_t type;
  uint16_t max_packet;
  uint8_t interval;
  uint8_t *data;
  size_t len;
  struct UsbProxyMessage *next;
} UsbProxyMessage;

typedef struct UsbProxyPacket {
  uint8_t *data;
  size_t len;
  struct UsbProxyPacket *next;
} UsbProxyPacket;

typedef struct {
  bool used;
  uint8_t address;
  int handle;
  bool is_in;
  _Atomic bool stop;
  bool thread_started;
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  bool out_enabled;
  UsbProxyPacket *in_head;
  UsbProxyPacket *in_tail;
} UsbProxyEndpoint;

typedef struct {
  bool started;
  bool stopping;
  pthread_t main_thread;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  UsbProxyMessage *head;
  UsbProxyMessage *tail;
  int gadget_fd;
  pthread_t gadget_thread;
  bool gadget_thread_started;
  _Atomic bool gadget_stop;
  UsbProxyEndpoint endpoints[USB_PROXY_MAX_ENDPOINTS];
  bool pending_out[USB_PROXY_MAX_ENDPOINTS];
  UsbProxyPacket *pending_in[USB_PROXY_MAX_ENDPOINTS];
  pthread_mutex_t control_lock;
  pthread_cond_t control_cond;
  bool control_ready;
  bool control_abort;
  unsigned control_kind;
  uint8_t *control_data;
  size_t control_len;
} UsbProxy;

static UsbProxy proxy;

static pthread_once_t signal_once = PTHREAD_ONCE_INIT;

static void usb_proxy_signal_handler(int signo) {
  (void)signo;
}

static void usb_proxy_install_signal(void) {
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = usb_proxy_signal_handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  (void)sigaction(SIGUSR1, &action, NULL);
}

static void usb_proxy_join_thread(pthread_t thread) {
  struct timespec deadline;

  if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
    (void)pthread_join(thread, NULL);
    return;
  }

  deadline.tv_sec += 2;

  if (pthread_timedjoin_np(thread, NULL, &deadline) != 0) (void)pthread_detach(thread);
}

static int usb_proxy_endpoint_index(uint8_t address) {
  unsigned endpoint = address & 0x7Fu;

  if (endpoint == 0 || endpoint >= 16) return -1;
  return (int)((address & 0x80u) != 0 ? 16u + endpoint : endpoint);
}

static void usb_proxy_free_packets(UsbProxyPacket *packet) {
  while (packet != NULL) {
    UsbProxyPacket *next = packet->next;

    free(packet->data);
    free(packet);
    packet = next;
  }
}

static bool usb_proxy_enqueue(UsbProxyMessage *message) {
  message->next = NULL;

  pthread_mutex_lock(&proxy.lock);

  if (proxy.stopping) {
    pthread_mutex_unlock(&proxy.lock);
    free(message->data);
    free(message);
    return false;
  }

  if (proxy.tail != NULL) {
    proxy.tail->next = message;
  } else {
    proxy.head = message;
  }

  proxy.tail = message;
  pthread_cond_signal(&proxy.cond);
  pthread_mutex_unlock(&proxy.lock);
  return true;
}

static UsbProxyMessage *usb_proxy_message_alloc(unsigned kind) {
  UsbProxyMessage *message = calloc(1, sizeof(*message));

  if (message == NULL) {
    LOG_ERR("USB proxy: failed to allocate message\n");
    return NULL;
  }

  message->kind = kind;
  return message;
}

static void usb_proxy_host_connect(void) {
  UsbProxyMessage *message = usb_proxy_message_alloc(USB_PROXY_MSG_CONNECT);

  if (message != NULL) usb_proxy_enqueue(message);
}

static void usb_proxy_host_disconnect(void) {
  UsbProxyMessage *message = usb_proxy_message_alloc(USB_PROXY_MSG_DISCONNECT);

  if (message != NULL) usb_proxy_enqueue(message);
}

static void usb_proxy_host_endpoint_enable(uint8_t address, uint8_t type, uint16_t max_packet,
                                           uint8_t interval) {
  UsbProxyMessage *message = usb_proxy_message_alloc(USB_PROXY_MSG_EP_ENABLE);

  if (message == NULL) return;

  message->address = address;
  message->type = type;
  message->max_packet = max_packet;
  message->interval = interval;
  usb_proxy_enqueue(message);
}

static void usb_proxy_host_endpoint_disable(uint8_t address) {
  UsbProxyMessage *message = usb_proxy_message_alloc(USB_PROXY_MSG_EP_DISABLE);

  if (message == NULL) return;

  message->address = address;
  usb_proxy_enqueue(message);
}

static void usb_proxy_host_out_enable(uint8_t address) {
  UsbProxyMessage *message = usb_proxy_message_alloc(USB_PROXY_MSG_OUT_ENABLE);

  if (message == NULL) return;

  message->address = address;
  usb_proxy_enqueue(message);
}

static void usb_proxy_host_ep_in(uint8_t address, const uint8_t *data, size_t len) {
  UsbProxyMessage *message = usb_proxy_message_alloc(USB_PROXY_MSG_EP_IN);

  if (message == NULL) return;

  message->address = address;
  message->len = len;

  if (len != 0) {
    message->data = malloc(len);

    if (message->data == NULL) {
      LOG_ERR("USB proxy: failed to allocate IN data\n");
      free(message);
      return;
    }

    memcpy(message->data, data, len);
  }

  usb_proxy_enqueue(message);
}

static void usb_proxy_host_ep_stall(uint8_t address) {
  UsbProxyMessage *message = usb_proxy_message_alloc(USB_PROXY_MSG_EP_STALL);

  if (message == NULL) return;

  message->address = address;
  usb_proxy_enqueue(message);
}

static void usb_proxy_control_response(unsigned kind, const uint8_t *data, size_t len) {
  pthread_mutex_lock(&proxy.control_lock);

  free(proxy.control_data);
  proxy.control_data = NULL;
  proxy.control_len = 0;
  proxy.control_kind = kind;
  proxy.control_ready = true;

  if (len != 0) {
    proxy.control_data = malloc(len);

    if (proxy.control_data != NULL) {
      memcpy(proxy.control_data, data, len);
      proxy.control_len = len;
    }
  }

  pthread_cond_broadcast(&proxy.control_cond);
  pthread_mutex_unlock(&proxy.control_lock);
}

static void usb_proxy_host_ep0_in(const uint8_t *data, size_t len) {
  usb_proxy_control_response(USB_PROXY_MSG_EP0_IN, data, len);
}

static void usb_proxy_host_ep0_status(void) {
  usb_proxy_control_response(USB_PROXY_MSG_EP0_STATUS, NULL, 0);
}

static void usb_proxy_host_ep0_stall(void) {
  usb_proxy_control_response(USB_PROXY_MSG_EP0_STALL, NULL, 0);
}

static const UsbHostOps usb_proxy_host_ops = {
  .connect = usb_proxy_host_connect,
  .disconnect = usb_proxy_host_disconnect,
  .endpoint_enable = usb_proxy_host_endpoint_enable,
  .endpoint_disable = usb_proxy_host_endpoint_disable,
  .out_enable = usb_proxy_host_out_enable,
  .ep0_in = usb_proxy_host_ep0_in,
  .ep0_status = usb_proxy_host_ep0_status,
  .ep0_stall = usb_proxy_host_ep0_stall,
  .ep_in = usb_proxy_host_ep_in,
  .ep_stall = usb_proxy_host_ep_stall,
};

static ssize_t usb_proxy_ep0_write(int fd, const uint8_t *data, size_t len) {
  struct usb_raw_ep_io *io = malloc(sizeof(*io) + len);
  ssize_t result;

  if (io == NULL) return -1;

  io->ep = 0;
  io->flags = 0;
  io->length = (uint32_t)len;
  if (len != 0) memcpy(io->data, data, len);

  result = ioctl(fd, USB_RAW_IOCTL_EP0_WRITE, io);
  free(io);
  return result;
}

static ssize_t usb_proxy_ep0_read(int fd, size_t len) {
  struct usb_raw_ep_io *io = malloc(sizeof(*io) + len);
  ssize_t result;

  if (io == NULL) return -1;

  io->ep = 0;
  io->flags = 0;
  io->length = (uint32_t)len;

  result = ioctl(fd, USB_RAW_IOCTL_EP0_READ, io);
  free(io);
  return result;
}

static void usb_proxy_ep0_stall(int fd) {
  (void)ioctl(fd, USB_RAW_IOCTL_EP0_STALL, 0);
}

static ssize_t usb_proxy_ep_write(int fd, int handle, const uint8_t *data, size_t len) {
  struct usb_raw_ep_io *io = malloc(sizeof(*io) + len);
  ssize_t result;

  if (io == NULL) return -1;

  io->ep = (uint16_t)handle;
  io->flags = 0;
  io->length = (uint32_t)len;
  if (len != 0) memcpy(io->data, data, len);

  result = ioctl(fd, USB_RAW_IOCTL_EP_WRITE, io);
  free(io);
  return result;
}

static ssize_t usb_proxy_ep_read(int fd, int handle, uint8_t *data, size_t cap) {
  struct usb_raw_ep_io *io = malloc(sizeof(*io) + cap);
  ssize_t result;

  if (io == NULL) return -1;

  io->ep = (uint16_t)handle;
  io->flags = 0;
  io->length = (uint32_t)cap;

  result = ioctl(fd, USB_RAW_IOCTL_EP_READ, io);

  if (result > 0) {
    size_t copied = (size_t)result;

    if (copied > cap) copied = cap;
    memcpy(data, io->data, copied);
  }

  free(io);
  return result;
}

static void usb_proxy_endpoint_queue_in(UsbProxyEndpoint *endpoint, uint8_t *data, size_t len) {
  UsbProxyPacket *packet = calloc(1, sizeof(*packet));

  if (packet == NULL) {
    free(data);
    LOG_ERR("USB proxy: failed to allocate IN packet\n");
    return;
  }

  packet->data = data;
  packet->len = len;

  pthread_mutex_lock(&endpoint->lock);

  if (endpoint->in_tail != NULL) {
    endpoint->in_tail->next = packet;
  } else {
    endpoint->in_head = packet;
  }

  endpoint->in_tail = packet;
  pthread_cond_signal(&endpoint->cond);
  pthread_mutex_unlock(&endpoint->lock);
}

static void *usb_proxy_in_worker(void *arg) {
  UsbProxyEndpoint *endpoint = arg;
  char name[16];

  snprintf(name, sizeof(name), "usb-ep-%02x", endpoint->address);
  thread_ctx_init(THREAD_ROLE_WORKER, name);

  for (;;) {
    UsbProxyPacket *packet;

    pthread_mutex_lock(&endpoint->lock);

    while (!endpoint->stop && endpoint->in_head == NULL) {
      pthread_cond_wait(&endpoint->cond, &endpoint->lock);
    }

    if (endpoint->stop) {
      pthread_mutex_unlock(&endpoint->lock);
      break;
    }

    packet = endpoint->in_head;
    endpoint->in_head = packet->next;
    if (endpoint->in_head == NULL) endpoint->in_tail = NULL;
    pthread_mutex_unlock(&endpoint->lock);

    for (;;) {
      ssize_t result = usb_proxy_ep_write(proxy.gadget_fd, endpoint->handle, packet->data,
                                          packet->len);

      if (result >= 0) {
        USB_HostEpInDone(endpoint->address);
        break;
      }

      if (errno == ESHUTDOWN || (errno == EINTR && endpoint->stop)) break;

      usleep(50000);
    }

    free(packet->data);
    free(packet);
  }

  return NULL;
}

static void *usb_proxy_out_worker(void *arg) {
  UsbProxyEndpoint *endpoint = arg;
  char name[16];
  uint8_t buffer[USB_PROXY_MAX_XFER];

  snprintf(name, sizeof(name), "usb-ep-%02x", endpoint->address);
  thread_ctx_init(THREAD_ROLE_WORKER, name);

  for (;;) {
    for (;;) {
      ssize_t result;

      pthread_mutex_lock(&endpoint->lock);

      while (!endpoint->stop && !endpoint->out_enabled) {
        pthread_cond_wait(&endpoint->cond, &endpoint->lock);
      }

      if (endpoint->stop) {
        pthread_mutex_unlock(&endpoint->lock);
        return NULL;
      }

      endpoint->out_enabled = false;
      pthread_mutex_unlock(&endpoint->lock);

      result = usb_proxy_ep_read(proxy.gadget_fd, endpoint->handle, buffer, sizeof(buffer));

      if (result >= 0) {
        USB_HostEpOut(endpoint->address, buffer, (size_t)result);
        break;
      }

      if (errno == ESHUTDOWN) return NULL;
      if (errno == EINTR && endpoint->stop) return NULL;

      usleep(50000);
    }
  }
}

static void usb_proxy_endpoint_stop(UsbProxyEndpoint *endpoint) {
  if (!endpoint->used) return;

  pthread_mutex_lock(&endpoint->lock);
  endpoint->stop = true;
  endpoint->out_enabled = false;
  pthread_cond_broadcast(&endpoint->cond);
  pthread_mutex_unlock(&endpoint->lock);

  if (endpoint->handle >= 0 && proxy.gadget_fd >= 0) {
    (void)ioctl(proxy.gadget_fd, USB_RAW_IOCTL_EP_DISABLE, (unsigned long)endpoint->handle);
  }

  if (endpoint->thread_started) {
    (void)pthread_kill(endpoint->thread, SIGUSR1);
    usb_proxy_join_thread(endpoint->thread);
    endpoint->thread_started = false;
  }

  pthread_mutex_lock(&endpoint->lock);
  usb_proxy_free_packets(endpoint->in_head);
  endpoint->in_head = NULL;
  endpoint->in_tail = NULL;
  endpoint->used = false;
  endpoint->handle = -1;
  pthread_mutex_unlock(&endpoint->lock);
}

static void usb_proxy_endpoint_start(uint8_t address, uint8_t type, uint16_t max_packet,
                                     uint8_t interval) {
  int index = usb_proxy_endpoint_index(address);
  UsbProxyEndpoint *endpoint;
  struct usb_endpoint_descriptor descriptor;
  long handle;
  UsbProxyPacket *pending;

  if (index < 0 || proxy.gadget_fd < 0) return;

  endpoint = &proxy.endpoints[index];
  if (endpoint->used) return;

  memset(&descriptor, 0, sizeof(descriptor));
  descriptor.bLength = USB_DT_ENDPOINT_SIZE;
  descriptor.bDescriptorType = USB_DT_ENDPOINT;
  descriptor.bEndpointAddress = address;
  descriptor.bmAttributes = type;
  descriptor.wMaxPacketSize = htole16(max_packet);
  descriptor.bInterval = interval;

  handle = ioctl(proxy.gadget_fd, USB_RAW_IOCTL_EP_ENABLE, &descriptor);

  if (handle < 0) {
    LOG_ERR("USB proxy: failed to enable endpoint 0x%02x: %s\n", address, strerror(errno));
    return;
  }

  endpoint->address = address;
  endpoint->handle = (int)handle;
  endpoint->is_in = (address & 0x80u) != 0;
  endpoint->stop = false;
  endpoint->out_enabled = false;
  endpoint->in_head = NULL;
  endpoint->in_tail = NULL;
  endpoint->used = true;

  if (pthread_create(&endpoint->thread, NULL,
                     endpoint->is_in ? usb_proxy_in_worker : usb_proxy_out_worker,
                     endpoint) != 0) {
    LOG_ERR("USB proxy: failed to start endpoint 0x%02x worker\n", address);
    (void)ioctl(proxy.gadget_fd, USB_RAW_IOCTL_EP_DISABLE, (unsigned long)handle);
    endpoint->used = false;
    endpoint->handle = -1;
    return;
  }

  {
    char name[16];

    snprintf(name, sizeof(name), "usb-ep-%02x", address);
    (void)pthread_setname_np(endpoint->thread, name);
  }

  endpoint->thread_started = true;

  pending = proxy.pending_in[index];
  proxy.pending_in[index] = NULL;
  while (pending != NULL) {
    UsbProxyPacket *next = pending->next;

    pending->next = NULL;
    usb_proxy_endpoint_queue_in(endpoint, pending->data, pending->len);
    free(pending);
    pending = next;
  }

  if (proxy.pending_out[index]) {
    proxy.pending_out[index] = false;
    pthread_mutex_lock(&endpoint->lock);
    endpoint->out_enabled = true;
    pthread_cond_broadcast(&endpoint->cond);
    pthread_mutex_unlock(&endpoint->lock);
  }
}

static void usb_proxy_context_request(int fd, const uint8_t *setup, size_t setup_len) {
  struct usb_ctrlrequest request;
  struct timespec deadline = { 0 };
  unsigned kind;
  bool ready = false;
  bool abort = false;
  uint8_t *data = NULL;
  size_t len = 0;

  if (setup_len < sizeof(request)) return;

  memcpy(&request, setup, sizeof(request));

  pthread_mutex_lock(&proxy.control_lock);
  free(proxy.control_data);
  proxy.control_data = NULL;
  proxy.control_len = 0;
  proxy.control_ready = false;
  proxy.control_abort = false;
  pthread_mutex_unlock(&proxy.control_lock);

  for (unsigned attempt = 0; attempt < 2 && !ready && !abort; attempt++) {
    if (attempt != 0) {
      pthread_mutex_lock(&proxy.control_lock);
      free(proxy.control_data);
      proxy.control_data = NULL;
      proxy.control_len = 0;
      proxy.control_ready = false;
      pthread_mutex_unlock(&proxy.control_lock);
    }

    USB_HostSetup(setup);

    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
      deadline.tv_sec += USB_PROXY_CONTROL_TIMEOUT_MS / 1000;
      deadline.tv_nsec = 0;
    } else {
      deadline.tv_sec += USB_PROXY_CONTROL_TIMEOUT_MS / 1000;
      deadline.tv_nsec += (long)(USB_PROXY_CONTROL_TIMEOUT_MS % 1000) * 1000000L;

      if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
      }
    }

    pthread_mutex_lock(&proxy.control_lock);

    while (!proxy.control_ready) {
      int result = pthread_cond_timedwait(&proxy.control_cond, &proxy.control_lock, &deadline);

      if (proxy.control_abort) {
        abort = true;
        break;
      }

      if (result == ETIMEDOUT) break;
    }

    if (proxy.control_ready) {
      ready = true;
      kind = proxy.control_kind;
      data = proxy.control_data;
      len = proxy.control_len;
      proxy.control_data = NULL;
      proxy.control_len = 0;
      proxy.control_ready = false;
    }

    pthread_mutex_unlock(&proxy.control_lock);
  }

  if (!ready) {
    LOG_ERR("USB proxy: control transfer timed out "
            "(type=0x%02x request=0x%02x value=0x%04x index=0x%04x length=%u)\n",
            request.bRequestType, request.bRequest, le16toh(request.wValue),
            le16toh(request.wIndex), le16toh(request.wLength));
    usb_proxy_ep0_stall(fd);
    USB_HostEp0Done();
    free(data);
    return;
  }

  if (abort) {
    free(data);
    return;
  }

  if (kind == USB_PROXY_MSG_EP0_IN) {
    uint16_t length = le16toh(request.wLength);

    if (length != 0) {
      size_t send = len;

      if (send > length) send = length;
      (void)usb_proxy_ep0_write(fd, data, send);
    } else {
      (void)usb_proxy_ep0_read(fd, 0);
    }
  } else if (kind == USB_PROXY_MSG_EP0_STATUS) {
    (void)usb_proxy_ep0_read(fd, 0);
  } else {
    usb_proxy_ep0_stall(fd);
  }

  free(data);
  USB_HostEp0Done();
}

static void *usb_proxy_gadget_thread(void *arg) {
  int fd = (int)(intptr_t)arg;
  uint8_t buffer[sizeof(struct usb_raw_event) + USB_PROXY_MAX_XFER];

  thread_ctx_init(THREAD_ROLE_WORKER, "usb-gadget");

  while (!proxy.gadget_stop) {
    struct usb_raw_event *event = (struct usb_raw_event *)buffer;
    int result;

    memset(buffer, 0, sizeof(buffer));
    event->length = USB_PROXY_MAX_XFER;

    result = ioctl(fd, USB_RAW_IOCTL_EVENT_FETCH, event);

    if (result < 0) {
      if (errno == EINTR) continue;
      break;
    }

    if (event->type == USB_RAW_EVENT_CONTROL) {
      usb_proxy_context_request(fd, event->data, event->length);
    } else if (event->type == USB_RAW_EVENT_RESET) {
      USB_HostReset();
    } else if (event->type == USB_RAW_EVENT_DISCONNECT) {
      USB_HostReset();
    }
  }

  return NULL;
}

static void usb_proxy_gadget_stop(void) {
  unsigned index;

  proxy.gadget_stop = true;

  pthread_mutex_lock(&proxy.control_lock);
  proxy.control_abort = true;
  pthread_cond_broadcast(&proxy.control_cond);
  pthread_mutex_unlock(&proxy.control_lock);

  for (index = 0; index < USB_PROXY_MAX_ENDPOINTS; index++) {
    usb_proxy_endpoint_stop(&proxy.endpoints[index]);
  }

  if (proxy.gadget_thread_started) {
    (void)pthread_kill(proxy.gadget_thread, SIGUSR1);
    usb_proxy_join_thread(proxy.gadget_thread);
    proxy.gadget_thread_started = false;
  }

  if (proxy.gadget_fd >= 0) {
    close(proxy.gadget_fd);
    proxy.gadget_fd = -1;
  }

  for (index = 0; index < USB_PROXY_MAX_ENDPOINTS; index++) {
    usb_proxy_free_packets(proxy.pending_in[index]);
    proxy.pending_in[index] = NULL;
    proxy.pending_out[index] = false;
  }
}

static void usb_proxy_gadget_start(void) {
  struct usb_raw_init init;
  int fd;

  if (proxy.gadget_fd >= 0) return;

  fd = open("/dev/raw-gadget", O_RDWR | O_CLOEXEC);

  if (fd < 0) {
    LOG_ERR("USB proxy: failed to open /dev/raw-gadget: %s\n", strerror(errno));
    return;
  }

  memset(&init, 0, sizeof(init));
  snprintf((char *)init.driver_name, sizeof(init.driver_name), "%s", USB_PROXY_DRIVER);
  snprintf((char *)init.device_name, sizeof(init.device_name), "%s", USB_PROXY_UDC);
  init.speed = USB_SPEED_FULL;

  if (ioctl(fd, USB_RAW_IOCTL_INIT, &init) < 0) {
    LOG_ERR("USB proxy: raw-gadget init failed: %s\n", strerror(errno));
    close(fd);
    return;
  }

  if (ioctl(fd, USB_RAW_IOCTL_RUN, 0) < 0) {
    LOG_ERR("USB proxy: raw-gadget run failed: %s\n", strerror(errno));
    close(fd);
    return;
  }

  proxy.gadget_fd = fd;
  proxy.gadget_stop = false;

  if (pthread_create(&proxy.gadget_thread, NULL, usb_proxy_gadget_thread,
                     (void *)(intptr_t)fd) != 0) {
    LOG_ERR("USB proxy: failed to start gadget thread\n");
    close(fd);
    proxy.gadget_fd = -1;
    return;
  }

  (void)pthread_setname_np(proxy.gadget_thread, "usb-gadget");
  proxy.gadget_thread_started = true;
  LOG("USB CCD gadget bound to %s\n", USB_PROXY_UDC);
}

static void usb_proxy_handle_message(UsbProxyMessage *message) {
  int index = usb_proxy_endpoint_index(message->address);

  switch (message->kind) {
    case USB_PROXY_MSG_CONNECT:
      usb_proxy_gadget_start();
      break;
    case USB_PROXY_MSG_DISCONNECT:
      usb_proxy_gadget_stop();
      break;
    case USB_PROXY_MSG_EP_ENABLE:
      usb_proxy_endpoint_start(message->address, message->type, message->max_packet,
                               message->interval);
      break;
    case USB_PROXY_MSG_EP_DISABLE:
      if (index >= 0) usb_proxy_endpoint_stop(&proxy.endpoints[index]);
      break;
    case USB_PROXY_MSG_OUT_ENABLE:
      if (index < 0) break;

      if (proxy.endpoints[index].used) {
        pthread_mutex_lock(&proxy.endpoints[index].lock);
        proxy.endpoints[index].out_enabled = true;
        pthread_cond_broadcast(&proxy.endpoints[index].cond);
        pthread_mutex_unlock(&proxy.endpoints[index].lock);
      } else {
        proxy.pending_out[index] = true;
      }
      break;
    case USB_PROXY_MSG_EP_IN:
      if (index < 0) break;

      if (proxy.endpoints[index].used) {
        usb_proxy_endpoint_queue_in(&proxy.endpoints[index], message->data, message->len);
        message->data = NULL;
      } else {
        UsbProxyPacket *packet = calloc(1, sizeof(*packet));

        if (packet != NULL) {
          packet->data = message->data;
          packet->len = message->len;
          message->data = NULL;

          if (proxy.pending_in[index] != NULL) {
            UsbProxyPacket *tail = proxy.pending_in[index];

            while (tail->next != NULL) tail = tail->next;
            tail->next = packet;
          } else {
            proxy.pending_in[index] = packet;
          }
        }
      }
      break;
    case USB_PROXY_MSG_EP_STALL:
      if (index >= 0 && proxy.endpoints[index].used && proxy.endpoints[index].handle >= 0 &&
          proxy.gadget_fd >= 0) {
        (void)ioctl(proxy.gadget_fd, USB_RAW_IOCTL_EP_SET_HALT,
                    (unsigned long)proxy.endpoints[index].handle);
      }
      break;
    default:
      break;
  }
}

static void *usb_proxy_main(void *arg) {
  (void)arg;

  thread_ctx_init(THREAD_ROLE_WORKER, "usb-proxy");
  LOG("USB proxy ready\n");

  for (;;) {
    UsbProxyMessage *message;

    pthread_mutex_lock(&proxy.lock);

    while (!proxy.stopping && proxy.head == NULL) {
      pthread_cond_wait(&proxy.cond, &proxy.lock);
    }

    if (proxy.stopping) {
      pthread_mutex_unlock(&proxy.lock);
      break;
    }

    message = proxy.head;
    proxy.head = message->next;
    if (proxy.head == NULL) proxy.tail = NULL;
    pthread_mutex_unlock(&proxy.lock);

    usb_proxy_handle_message(message);
    free(message->data);
    free(message);
  }

  usb_proxy_gadget_stop();
  LOG("USB proxy stopped\n");
  return NULL;
}

bool USB_ProxyStart(void) {
  unsigned index;

  if (proxy.started) return true;

  memset(&proxy, 0, sizeof(proxy));
  proxy.gadget_fd = -1;
  proxy.head = NULL;
  proxy.tail = NULL;

  if (pthread_mutex_init(&proxy.lock, NULL) != 0) return false;
  if (pthread_cond_init(&proxy.cond, NULL) != 0) return false;
  if (pthread_mutex_init(&proxy.control_lock, NULL) != 0) return false;
  if (pthread_cond_init(&proxy.control_cond, NULL) != 0) return false;

  for (index = 0; index < USB_PROXY_MAX_ENDPOINTS; index++) {
    if (pthread_mutex_init(&proxy.endpoints[index].lock, NULL) != 0) return false;
    if (pthread_cond_init(&proxy.endpoints[index].cond, NULL) != 0) return false;
    proxy.endpoints[index].handle = -1;
  }

  pthread_once(&signal_once, usb_proxy_install_signal);

  USB_SetHostOps(&usb_proxy_host_ops);

  if (pthread_create(&proxy.main_thread, NULL, usb_proxy_main, NULL) != 0) {
    USB_SetHostOps(NULL);
    return false;
  }

  (void)pthread_setname_np(proxy.main_thread, "usb-proxy");
  proxy.started = true;
  return true;
}

void USB_ProxyStop(void) {
  if (!proxy.started) return;

  pthread_mutex_lock(&proxy.lock);
  proxy.stopping = true;
  pthread_cond_broadcast(&proxy.cond);
  pthread_mutex_unlock(&proxy.lock);

  (void)pthread_join(proxy.main_thread, NULL);
  proxy.started = false;
  USB_SetHostOps(NULL);
}

#else

bool USB_ProxyStart(void) {
  return false;
}

void USB_ProxyStop(void) {}

#endif /* SINGLE_THREAD */
