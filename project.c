#include <nrf_gpio.h>
#include <nrfx_gpiote.h>
#include <nrfx_uarte.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>

#include <adaptation/ndn-nrf52840.h>
#include <face/direct-face.h>

static const uint32_t led = NRF_GPIO_PIN_MAP(0,13);

static const nrfx_gpiote_out_config_t led_config = {
  .init_state = GPIOTE_CONFIG_OUTINIT_Low,
  .task_pin = false
};

const nrfx_uarte_t uart0 = NRFX_UARTE_INSTANCE(0);

static const nrfx_uarte_config_t uart_config = {
  .pseltxd = NRF_GPIO_PIN_MAP(1,10),
  .pselrxd = NRF_GPIO_PIN_MAP(1,11),
  .pselcts = NRF_UARTE_PSEL_DISCONNECTED,
  .pselrts = NRF_UARTE_PSEL_DISCONNECTED,
  .hwfc = NRFX_UART_DEFAULT_CONFIG_HWFC,
  .parity = NRFX_UART_DEFAULT_CONFIG_PARITY,
  .baudrate = NRFX_UART_DEFAULT_CONFIG_BAUDRATE,
  .interrupt_priority = NRFX_UART_DEFAULT_CONFIG_IRQ_PRIORITY
};

/* functions */

static void nop(void)
{
  __asm__ __volatile__("nop":::);
}

static void blink_led(int i) {
  const uint32_t pin = NRF_GPIO_PIN_MAP(0,12 + i); // LED
  nrf_gpio_cfg_output(pin);

  int counter = 0;
  while (counter < 10) {
    nrf_gpio_pin_toggle(pin);
    for(uint32_t i = 0; i < 0x320000; ++i)
      nop();
    counter++;
  }
}

int
on_data_callback(const uint8_t* data, uint32_t data_size)
{
  (void)data;
  (void)data_size;
  return 0;
}

int
on_interest_timeout_callback(const uint8_t* interest, uint32_t interest_size)
{
  (void)interest;
  (void)interest_size;
  blink_led(interest_size);
  return 0;
}

int
on_interest(const uint8_t* interest, uint32_t interest_size)
{
  blink_led(3);
  return 0;
}

void
on_error_callback(int error_code)
{
  blink_led(error_code);
}

int
main(void)
{
  // initialization
  if (NRFX_SUCCESS != nrfx_gpiote_init())
    while(1) { /* endless */ };
  nrfx_gpiote_out_init(led, &led_config);
  if (NRFX_SUCCESS != nrfx_uarte_init(&uart0, &uart_config, NULL))
    while(1) { /* endless */ };

  ndn_forwarder_t* forwarder;
  ndn_nrf52840_802154_face_t* nrf_face;
  ndn_direct_face_t* direct_face;
  const uint8_t extended_address[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  const uint8_t pan_id[]           = {0xd0, 0x0f};
  const uint8_t short_address[]    = {0x12, 0x34};

  ndn_interest_t interest;
  ndn_interest_init(&interest);
  char name_string[] = "/ndn/bbb/ccc/ddd";
  ndn_name_from_string(&interest.name, name_string, sizeof(name_string));
  uint8_t interest_block[256] = {0};
  ndn_encoder_t encoder;
  encoder_init(&encoder, interest_block, 256);
  ndn_interest_tlv_encode(&encoder, &interest);

  char prefix_string[] = "/ndn";
  ndn_name_t prefix;
  ndn_name_from_string(&prefix, prefix_string, sizeof(prefix_string));

  forwarder = ndn_forwarder_init();
  direct_face = ndn_direct_face_construct(124);
  nrf_face = ndn_nrf52840_802154_face_construct(123, extended_address,
                                                pan_id, short_address, false, on_error_callback);
  // blink_led(1);
  ndn_forwarder_fib_insert(&prefix, &nrf_face->intf, 1);
  ndn_direct_face_express_interest(&interest.name,
                                   interest_block, encoder.offset,
                                   on_data_callback, on_interest_timeout_callback);
  ndn_face_send(&nrf_face->intf, &interest.name, interest_block, encoder.offset);
  // blink_led(2);

  // blink_led(1);
  // ndn_direct_face_register_prefix(&prefix, on_interest);
  // blink_led(2);
  return 0;
}
