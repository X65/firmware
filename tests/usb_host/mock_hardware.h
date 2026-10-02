#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "register_bits.h"

#define CFG_TUH_ENABLED 1
#define CFG_TUSB_MCU 1
#define OPT_MCU_RP2040 1
#define CFG_TUH_RPI_PIO_USB 0
#define CFG_TUH_MAX3421 0
#define CFG_TUH_DEVICE_MAX 16
#define CFG_TUH_HUB 5
#define TU_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define TU_BIT(n) (1u << (n))
#define TU_ATTR_ALWAYS_INLINE
#define TU_ASSERT(condition) do { if (!(condition)) return false; } while (0)
#define __tusb_irq_path_func(name) name
#define pico_trace(...) ((void)0)
#define TU_LOG(...) ((void)0)
#define USB_MAX_ENDPOINTS 16
#define USB_HOST_INTERRUPT_ENDPOINTS 15
#define USB_DPRAM_MAX 4096
#define USBCTRL_IRQ 0
#define PICO_SHARED_IRQ_HANDLER_HIGHEST_ORDER_PRIORITY 0

using uint = unsigned;

// Writes to Pico atomic aliases affect the underlying register immediately.
// STOP_TRANS self-clears; tests explicitly inject bus completions and errors.
struct Register {
  uint32_t value = 0;
  Register *target = nullptr;
  enum Operation { WRITE, SET, CLEAR } operation = WRITE;
  bool sie_ctrl = false;

  operator uint32_t() const { return target ? target->value : value; }
  Register &operator=(uint32_t bits) {
    Register &reg = target ? *target : *this;
    if (operation == SET) reg.value |= bits;
    else if (operation == CLEAR) reg.value &= ~bits;
    else reg.value = bits;
    if (reg.sie_ctrl) reg.value &= ~USB_SIE_CTRL_STOP_TRANS_BITS;
    return *this;
  }
};
using io_rw_32 = Register;

struct UsbRegs {
  Register sie_ctrl, sie_status, buf_status, buf_cpu_should_handle;
  Register dev_addr_ctrl, nak_poll, inte, ints, int_ep_ctrl;
  Register sof_rd, pwr, main_ctrl;
  Register int_ep_addr_ctrl[15];
};
static UsbRegs registers, set_alias, clear_alias;
#define usb_hw (&registers)
#define usb_hw_set (&set_alias)
#define usb_hw_clear (&clear_alias)

struct usb_host_dpram_t {
  uint8_t setup_packet[8];
  struct { Register ctrl; } int_ep_ctrl[15], int_ep_buffer_ctrl[15];
  Register epx_ctrl, epx_buf_ctrl;
  uint8_t epx_data[128];
};
static usb_host_dpram_t dpram;
static uint8_t interrupt_dpram[4096];
#define usbh_dpram (&dpram)
#define USBCTRL_DPRAM_BASE reinterpret_cast<uintptr_t>(interrupt_dpram)

enum { TUSB_DIR_OUT, TUSB_DIR_IN };
enum { TUSB_XFER_CONTROL, TUSB_XFER_ISOCHRONOUS, TUSB_XFER_BULK, TUSB_XFER_INTERRUPT };
enum tusb_speed_t { TUSB_SPEED_LOW, TUSB_SPEED_FULL, TUSB_SPEED_INVALID };
enum xfer_result_t { XFER_RESULT_SUCCESS, XFER_RESULT_FAILED, XFER_RESULT_STALLED };
enum { EPSTATE_IDLE, EPSTATE_ACTIVE, EPSTATE_PENDING, EPSTATE_PENDING_SETUP };
struct tusb_rhport_init_t {};
struct tusb_desc_endpoint_t {
  uint8_t bEndpointAddress;
  struct { uint8_t xfer; } bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
};
struct hw_endpoint_t {
  uint8_t ep_addr, next_pid, state, dev_addr, interrupt_num, transfer_type;
  bool need_pre;
  uint16_t max_packet_size;
  uint8_t *dpram_buf, *user_buf;
  uint16_t remaining_len, xferred_len;
};

static constexpr uint32_t SIE_CTRL_BASE = USB_SIE_CTRL_PULLDOWN_EN_BITS | USB_SIE_CTRL_EP0_INT_1BUF_BITS;
static constexpr uint32_t SIE_CTRL_BASE_MASK = SIE_CTRL_BASE | USB_SIE_CTRL_SOF_EN_BITS |
                                               USB_SIE_CTRL_KEEP_ALIVE_EN_BITS;
static uint8_t tu_edpt_number(uint8_t ep) { return ep & 15; }
static uint8_t tu_edpt_dir(uint8_t ep) { return ep >> 7; }
static uint16_t tu_edpt_packet_size(const tusb_desc_endpoint_t *ep) { return ep->wMaxPacketSize; }
static uint32_t hw_data_offset(const uint8_t *) { return 0; }
static tusb_speed_t tuh_speed_get(uint8_t) { return TUSB_SPEED_FULL; }
tusb_speed_t hcd_port_speed_get(uint8_t);

static unsigned wait_us;
static void (*during_wait)();
static void busy_wait_us(unsigned us) {
  wait_us += us;
  assert(usb_hw->int_ep_ctrl == 0);
  if (during_wait) during_wait();
}
static void busy_wait_at_least_cycles(unsigned) {}
static void tight_loop_contents() {}
static void rp2usb_critical_enter() {}
static void rp2usb_critical_exit() {}
static void rp2usb_init() {}
static void irq_remove_handler(unsigned, void (*)()) {}
static void irq_add_shared_handler(unsigned, void (*)(), unsigned) {}
static void irq_set_enabled(unsigned, bool) {}
static void reset_block(unsigned) {}
static void unreset_block_wait(unsigned) {}

struct Completion {
  uint8_t address, endpoint;
  uint32_t length;
  xfer_result_t result;
  uint32_t polling;
};
static std::vector<Completion> completions;
static unsigned removals;
static void hcd_event_xfer_complete(uint8_t address, uint8_t endpoint, uint32_t length,
                                    xfer_result_t result, bool) {
  completions.push_back({address, endpoint, length, result, usb_hw->int_ep_ctrl});
}
static void hcd_event_device_remove(uint8_t, bool) { ++removals; }
static void hcd_event_device_attach(uint8_t, bool) {}

// Model TinyUSB's buffer ownership/accounting, independent of HCD scheduling.
static void rp2usb_reset_transfer(hw_endpoint_t *ep) {
  ep->state = EPSTATE_IDLE;
  ep->remaining_len = ep->xferred_len = 0;
  ep->user_buf = nullptr;
}
static uint16_t prepare_buffer(hw_endpoint_t *ep, bool rx) {
  const uint16_t length = std::min(ep->remaining_len, ep->max_packet_size);
  ep->remaining_len -= length;
  uint16_t control = length | USB_BUF_CTRL_AVAIL;
  if (ep->next_pid) control |= USB_BUF_CTRL_DATA1_PID;
  ep->next_pid ^= 1;
  if (!rx) {
    control |= USB_BUF_CTRL_FULL;
    if (length) ep->user_buf += length;
  }
  if (!ep->remaining_len) control |= USB_BUF_CTRL_LAST;
  return control;
}
static void rp2usb_buffer_start(hw_endpoint_t *ep, io_rw_32 *ep_reg, io_rw_32 *buf_reg, bool rx) {
  uint32_t control = prepare_buffer(ep, rx);
  if (ep->remaining_len && !ep->interrupt_num) {
    control |= uint32_t(prepare_buffer(ep, rx)) << 16;
    *ep_reg = *ep_reg | EP_CTRL_DOUBLE_BUFFERED_BITS;
  } else {
    *ep_reg = *ep_reg & ~EP_CTRL_DOUBLE_BUFFERED_BITS;
  }
  *buf_reg = control;
}
static void rp2usb_xfer_start(hw_endpoint_t *ep, io_rw_32 *ep_reg, io_rw_32 *buf_reg,
                             uint8_t *buffer, void *, uint16_t length) {
  ep->state = EPSTATE_ACTIVE;
  ep->remaining_len = length;
  ep->xferred_len = 0;
  ep->user_buf = buffer;
  rp2usb_buffer_start(ep, ep_reg, buf_reg, tu_edpt_dir(ep->ep_addr) == TUSB_DIR_IN);
}
static bool rp2usb_xfer_continue(hw_endpoint_t *ep, io_rw_32 *, io_rw_32 *buf_reg,
                                uint8_t id, bool rx) {
  if (ep->state != EPSTATE_ACTIVE) return false;
  const uint16_t control = uint32_t(*buf_reg) >> (16 * id);
  assert(!(control & USB_BUF_CTRL_AVAIL));
  assert(bool(control & USB_BUF_CTRL_FULL) == rx);
  ep->xferred_len += control & USB_BUF_CTRL_LEN_MASK;
  return (control & USB_BUF_CTRL_LAST) || (control & USB_BUF_CTRL_LEN_MASK) < ep->max_packet_size;
}

static void reset_hardware() {
  registers = UsbRegs{};
  set_alias = UsbRegs{};
  clear_alias = UsbRegs{};
  dpram = usb_host_dpram_t{};
  registers.sie_ctrl.sie_ctrl = true;
#define ALIAS(field) \
  set_alias.field.target = clear_alias.field.target = &registers.field; \
  set_alias.field.operation = Register::SET; \
  clear_alias.field.operation = Register::CLEAR
  ALIAS(sie_status);
  ALIAS(buf_status);
  ALIAS(nak_poll);
  ALIAS(inte);
  ALIAS(int_ep_ctrl);
#undef ALIAS
  completions.clear();
  removals = wait_us = 0;
  during_wait = nullptr;
}
