#include "mock_hardware.h"
#include "../../src/tinyusb_rp6502/hcd_rp2040.c"

static uint8_t data[128];

static void open_endpoint(uint8_t address, uint8_t endpoint, uint8_t type, uint16_t size = 64) {
  const tusb_desc_endpoint_t descriptor = {endpoint, {type}, size, 1};
  assert(hcd_edpt_open(0, address, &descriptor));
}

static void reset() {
  reset_hardware();
  assert(hcd_init(0, nullptr));
  usb_hw->sie_status = SIE_CTRL_SPEED_FULL << USB_SIE_STATUS_SPEED_LSB;
  open_endpoint(1, 0, TUSB_XFER_CONTROL);
  open_endpoint(1, 0x81, TUSB_XFER_BULK);
  open_endpoint(2, 0x82, TUSB_XFER_INTERRUPT, 8);
  assert(hcd_edpt_xfer(0, 2, 0x82, data, 8));
}

static uint32_t interrupt_mask() {
  return TU_BIT(edpt_find(2, 0x82)->interrupt_num);
}

static void complete_buffer(hw_endpoint_t *ep, uint16_t length) {
  const bool rx = tu_edpt_dir(ep->ep_addr) == TUSB_DIR_IN;
  const uint16_t control = length | USB_BUF_CTRL_LAST | (rx ? USB_BUF_CTRL_FULL : 0);
  if (ep->interrupt_num) {
    *dpram_int_ep_buffer_ctrl(ep->interrupt_num) = control;
    usb_hw->buf_status = usb_hw->buf_status | TU_BIT(ep->interrupt_num * 2 + (rx ? 0 : 1));
  } else {
    usbh_dpram->epx_buf_ctrl = control;
    usb_hw->buf_status = usb_hw->buf_status | 1u;
  }
  usb_hw->sie_status = usb_hw->sie_status | USB_SIE_STATUS_TRANS_COMPLETE_BITS;
}

static void irq(uint32_t extra = 0) {
  uint32_t status = extra;
  if (usb_hw->buf_status) status |= USB_INTS_BUFF_STATUS_BITS;
  if (usb_hw->sie_status & USB_SIE_STATUS_TRANS_COMPLETE_BITS) status |= USB_INTS_TRANS_COMPLETE_BITS;
  usb_hw->ints = status;
  hcd_int_handler(0, true);
  usb_hw->ints = 0;
}

static void transaction_complete() {
  usb_hw->sie_status = usb_hw->sie_status | USB_SIE_STATUS_TRANS_COMPLETE_BITS;
  irq();
}

static void assert_completion(size_t index, uint8_t address, uint8_t endpoint,
                               uint32_t length, xfer_result_t result = XFER_RESULT_SUCCESS) {
  const Completion &event = completions.at(index);
  assert(event.address == address && event.endpoint == endpoint);
  assert(event.length == length && event.result == result);
}

static void test_interrupt_does_not_complete_data() {
  reset();
  assert(hcd_edpt_xfer(0, 1, 0x81, data, 64));
  const uint32_t buffer = usbh_dpram->epx_buf_ctrl;
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
  complete_buffer(edpt_find(2, 0x82), 8);
  irq();
  assert(completions.size() == 1);
  assert_completion(0, 2, 0x82, 8);
  assert(epx->state == EPSTATE_ACTIVE && usbh_dpram->epx_buf_ctrl == buffer);
  complete_buffer(epx, 64);
  irq();
  assert(completions.size() == 2);
  assert_completion(1, 1, 0x81, 64);
}

static void test_simultaneous_completions_and_queued_transfer() {
  reset();
  open_endpoint(3, 0x83, TUSB_XFER_BULK);
  assert(hcd_edpt_xfer(0, 1, 0x81, data, 64));
  assert(hcd_edpt_xfer(0, 3, 0x83, data, 64));
  complete_buffer(epx, 64);
  complete_buffer(edpt_find(2, 0x82), 8);
#if PICO_RP2350
  // A NAK-stop from the old transaction must not preempt/re-arm the next one.
  irq(USB_INTS_EPX_STOPPED_ON_NAK_BITS);
#else
  irq(USB_INTS_HOST_SOF_BITS);
#endif
  assert(completions.size() == 2);
  assert_completion(0, 1, 0x81, 64);
  assert_completion(1, 2, 0x82, 8);
  assert(epx == edpt_find(3, 0x83) && epx->state == EPSTATE_ACTIVE);
  assert(epx->next_pid == 1); // primed exactly once
}

static void test_zlp(bool rx, bool buffer_status) {
  reset();
  const uint8_t endpoint = rx ? 0x80 : 0;
  assert(hcd_edpt_xfer(0, 1, endpoint, nullptr, 0));
  assert(epx_phase == EPX_ZLP && usb_hw->int_ep_ctrl == 0);
  if (buffer_status) complete_buffer(epx, 0);
  transaction_complete();
  assert(completions.size() == 1);
  assert_completion(0, 1, endpoint, 0);
  assert(completions[0].polling == 0); // restore only after consuming the IRQ
  assert(epx->state == EPSTATE_IDLE && usb_hw->int_ep_ctrl == interrupt_mask());
  transaction_complete(); // a stale global completion cannot report it twice
  assert(completions.size() == 1);
}

static void test_short_zero_packet_in_data_transfer() {
  reset();
  assert(hcd_edpt_xfer(0, 1, 0x81, data, 64));
  transaction_complete(); // global latch alone is insufficient for DATA
  assert(completions.empty());
  complete_buffer(epx, 0);
  irq();
  assert(completions.size() == 1);
  assert_completion(0, 1, 0x81, 0);
}

static void report_during_quiescence() {
  complete_buffer(edpt_find(2, 0x82), 8);
}

static void test_setup_drains_prior_latch_and_preserves_report() {
  reset();
  during_wait = report_during_quiescence;
  const uint8_t setup[8] = {};
  assert(hcd_setup_send(0, 1, setup));
  during_wait = nullptr;
  assert(wait_us == 1000 && usb_hw->int_ep_ctrl == 0);
  assert(!(usb_hw->sie_status & USB_SIE_STATUS_TRANS_COMPLETE_BITS));
  assert(usb_hw->buf_status != 0);
  irq(); // the old report completes, SETUP does not
  assert(completions.size() == 1);
  assert_completion(0, 2, 0x82, 8);
  assert(epx_phase == EPX_SETUP && epx->state == EPSTATE_ACTIVE);
  assert(hcd_edpt_xfer(0, 2, 0x82, data, 8));
  assert(usb_hw->int_ep_ctrl == 0); // re-arming cannot undo suppression
  transaction_complete();
  assert(completions.size() == 2);
  assert_completion(1, 1, 0, 8);
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
}

static void test_polling_restores_live_endpoints() {
  reset();
  const uint8_t setup[8] = {};
  assert(hcd_setup_send(0, 1, setup));
  open_endpoint(3, 0x83, TUSB_XFER_INTERRUPT, 8);
  const uint32_t expected = TU_BIT(edpt_find(3, 0x83)->interrupt_num);
  assert(hcd_edpt_close(0, 2, 0x82));
  assert(usb_hw->int_ep_ctrl == 0);
  transaction_complete();
  assert(usb_hw->int_ep_ctrl == expected);
}

static void test_error_precedes_completion(uint32_t error, bool setup_phase) {
  reset();
  const uint8_t setup[8] = {};
  if (setup_phase) assert(hcd_setup_send(0, 1, setup));
  else assert(hcd_edpt_xfer(0, 1, 0x80, nullptr, 0));
  open_endpoint(3, 0x83, TUSB_XFER_BULK);
  assert(hcd_edpt_xfer(0, 3, 0x83, data, 64));
  if (!setup_phase) complete_buffer(epx, 0);
  usb_hw->sie_status = usb_hw->sie_status | USB_SIE_STATUS_TRANS_COMPLETE_BITS;
  irq(error | USB_INTS_HOST_SOF_BITS);
  assert(completions.size() == 1);
  assert_completion(0, 1, setup_phase ? 0 : 0x80, 0,
                    error == USB_INTS_STALL_BITS ? XFER_RESULT_STALLED : XFER_RESULT_FAILED);
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
  if (error != USB_INTS_STALL_BITS) {
    // STOP_TRANS needs a frame to settle before the queued transfer starts.
    assert(epx->state == EPSTATE_IDLE);
    irq(USB_INTS_HOST_SOF_BITS);
  }
  assert(epx == edpt_find(3, 0x83) && epx->state == EPSTATE_ACTIVE);
  assert(completions.size() == 1);
}

static void test_abort_restores_polling(bool setup_phase) {
  reset();
  const uint8_t setup[8] = {};
  if (setup_phase) assert(hcd_setup_send(0, 1, setup));
  else assert(hcd_edpt_xfer(0, 1, 0x80, nullptr, 0));
  assert(hcd_edpt_abort_xfer(0, 1, 0));
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
  transaction_complete();
  assert(completions.empty());
}

static void test_device_close_restores_polling() {
  reset();
  const uint8_t setup[8] = {};
  assert(hcd_setup_send(0, 1, setup));
  hcd_device_close(0, 1);
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
  transaction_complete();
  assert(completions.empty());
}

static void test_disconnect_drops_entire_snapshot() {
  reset();
  assert(hcd_edpt_xfer(0, 1, 0x80, nullptr, 0));
  open_endpoint(3, 0x83, TUSB_XFER_BULK);
  assert(hcd_edpt_xfer(0, 3, 0x83, data, 64));
  complete_buffer(epx, 0);
  usb_hw->sie_status = USB_SIE_STATUS_TRANS_COMPLETE_BITS; // root disconnected
  irq(USB_INTS_HOST_CONN_DIS_BITS | USB_INTS_HOST_SOF_BITS);
  assert(removals == 1 && completions.empty());
  assert(usb_hw->int_ep_ctrl == 0 && ep_pool[0].state == EPSTATE_IDLE);
  assert(edpt_find(3, 0x83)->state == EPSTATE_IDLE);
}

static void test_queued_setup() {
  reset();
  assert(hcd_edpt_xfer(0, 1, 0x81, data, 64));
  const uint8_t setup[8] = {};
  assert(hcd_setup_send(0, 1, setup));
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
  complete_buffer(epx, 64);
  irq();
  assert(completions.size() == 1);
  assert(epx_phase == EPX_SETUP && usb_hw->int_ep_ctrl == 0);
  transaction_complete();
  assert(completions.size() == 2);
  assert_completion(1, 1, 0, 8);
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
}

static void test_queued_transfer_phase(uint16_t length) {
  reset();
  assert(hcd_edpt_xfer(0, 1, 0x81, data, 64));
  assert(hcd_edpt_xfer(0, 1, 0x80, data, length));
  complete_buffer(epx, 64);
  irq();
  assert(completions.size() == 1);
  assert(epx_phase == (length ? EPX_DATA : EPX_ZLP));
  assert(usb_hw->int_ep_ctrl == (length ? interrupt_mask() : 0));
  if (length) {
    transaction_complete();
    assert(completions.size() == 1);
    complete_buffer(epx, length);
  }
  transaction_complete();
  assert(completions.size() == 2);
  assert_completion(1, 1, 0x80, length);
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
}

static void test_preempted_zlp() {
  reset();
  assert(hcd_edpt_xfer(0, 1, 0x80, nullptr, 0));
  assert(hcd_edpt_xfer(0, 1, 0x81, data, 64));
#if PICO_RP2350
  irq(USB_INTS_EPX_STOPPED_ON_NAK_BITS);
#else
  irq(USB_INTS_HOST_SOF_BITS);
  irq(USB_INTS_HOST_SOF_BITS);
#endif
  assert(epx == edpt_find(1, 0x81) && epx_phase == EPX_DATA);
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
  complete_buffer(epx, 64);
  irq();
  assert(completions.size() == 1);
  assert(epx_phase == EPX_ZLP && usb_hw->int_ep_ctrl == 0);
  transaction_complete();
  assert(completions.size() == 2);
  assert_completion(1, 1, 0x80, 0);
  assert(usb_hw->int_ep_ctrl == interrupt_mask());
}

int main() {
  test_interrupt_does_not_complete_data();
  test_simultaneous_completions_and_queued_transfer();
  for (bool rx : {false, true}) {
    for (bool buffer_status : {false, true}) test_zlp(rx, buffer_status);
  }
  test_short_zero_packet_in_data_transfer();
  test_setup_drains_prior_latch_and_preserves_report();
  test_polling_restores_live_endpoints();
  for (bool setup_phase : {false, true}) {
    for (uint32_t error : {USB_INTS_STALL_BITS, USB_INTS_ERROR_RX_TIMEOUT_BITS,
                           USB_INTS_ERROR_DATA_SEQ_BITS}) {
      test_error_precedes_completion(error, setup_phase);
    }
    test_abort_restores_polling(setup_phase);
  }
  test_device_close_restores_polling();
  test_disconnect_drops_entire_snapshot();
  test_queued_setup();
  test_queued_transfer_phase(0);
  test_queued_transfer_phase(32);
  test_preempted_zlp();
  std::printf("RP%d: USB completion regression tests passed\n", PICO_RP2350 ? 2350 : 2040);
}
