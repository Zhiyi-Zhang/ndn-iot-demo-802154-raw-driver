#include <nrf_gpio.h>
#include <nrfx_gpiote.h>
#include <nrfx_uarte.h>
#include <nrf_802154.h>
#include <nrf_temp.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>

#include <encode/interest.h>

#define MAX_MESSAGE_SIZE 17
#define CHANNEL          23

/* hardware configuration / muxing */

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

/* global state variables */

static volatile bool m_tx_done;
static volatile bool m_tx_failed;
static volatile nrf_802154_tx_error_t m_tx_errorcode;

/* functions */

static void nop(void)
{
  __asm__ __volatile__("nop":::);
}

static void init_radio(const uint8_t extended_address[], const uint8_t pan_id[], const uint8_t short_address[], bool promisc)
{
  printf("\r\ninit 802.15.4 driver\r\n");

  nrf_802154_init();
  nrf_802154_short_address_set(short_address);
  nrf_802154_extended_address_set(extended_address);
  nrf_802154_pan_id_set(pan_id);
  if(promisc)
    nrf_802154_promiscuous_set(true);
  nrf_802154_tx_power_set(-20);
  nrf_802154_channel_set(CHANNEL);
  nrf_802154_receive();

  printf("TX power currently set to %ddBm\r\n", (int)nrf_802154_tx_power_get());
}

static void init_packet(uint8_t message[MAX_MESSAGE_SIZE], const uint8_t pan_id[], const uint8_t short_address[])
{
  bzero(message, MAX_MESSAGE_SIZE);

  // frame header:
  message[0] = 0x41;		// FCF, valued 0x9841
  message[1] = 0x98;		// == 1001.1000.0100.0001
  //                    \\\_Data frame
  //                  .\_Security off
  //                 \_No more frames
  //                \_no ACK wanted
  //               \_PAN ID compressed
  //             .\_reserved
  //           \\_reserved
  //        .\\_DST address is short
  //      \\_IEEE802.15.4 frame
  //    \\_SRC address is short
  //
  message[2] = 0xff;		// sequence number -- filled in later
  message[3] = pan_id[0];		// PAN ID
  message[4] = pan_id[1];
  message[5] = short_address[0];	// short DST addr
  message[6] = short_address[1];
  message[7] = 0;			// short SRC addr
  message[8] = 0;
  // end of header
}

static int blink_led(int i) {
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

int main(void)
{
  char name_string[] = "/aaa/bbb/ccc/ddd";
  ndn_name_t name;
  ndn_name_from_string(&name, name_string, sizeof(name_string));

  blink_led(1);

  /*
   * This example project sends 802.15.4 packets containing an increasing
   * packet ID and the currently measured temperature.
   * It writes received packets (from the same channel / PAN) to a UART,
   * while flashing a LED.
   * So you can flash two devices and they should see each other,
   * when close to each other.
   * (TX power is reduced to minimum for this testing scenario.)
   */
  const uint8_t extended_address[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  const uint8_t pan_id[]           = {0xd0, 0x0f};
  const uint8_t short_address[]    = {0x12, 0x34};

  uint8_t message[MAX_MESSAGE_SIZE];
  uint32_t packet_id = 0;

  if(NRFX_SUCCESS != nrfx_gpiote_init())
    while(1) { /* endless */ };

  blink_led(2);

  nrfx_gpiote_out_init(led, &led_config);

  if(NRFX_SUCCESS != nrfx_uarte_init(&uart0, &uart_config, NULL))
    while(1) { /* endless */ };

  blink_led(3);

  init_radio(extended_address, pan_id, short_address, false);
  init_packet(message, pan_id, short_address);

  nrf_temp_init();

  blink_led(4);

  while(1) {
    // set payload:
    message[2] = packet_id&0xff; // sequence number in header
    // custom fields:
    *(uint32_t*)&message[ 9] = packet_id; // packet id
    if(NRF_TEMP->EVENTS_DATARDY) {
      *(uint32_t*)&message[13] = nrf_temp_read();
      NRF_TEMP->EVENTS_DATARDY = 0;
    } else {
      *(uint32_t*)&message[13] = 0xffff;
    }
    NRF_TEMP->TASKS_START = 1; // start temp-measurement for next cycle

    printf("TX starting transmit %lu:\r\n", packet_id);
    m_tx_done = false;
    m_tx_failed = false;
    unsigned delay_loops = 0;
    if(nrf_802154_transmit(message, sizeof(message), true)) {
      while(!m_tx_done && !m_tx_failed && (delay_loops < 4)) {
        for(uint32_t i = 0; i < 0x500000; ++i)
          nop(); // FIXME: replace by proper powerdown
        ++delay_loops;
      }
      if(m_tx_done) {
        printf("TX finished.\r\n");
        packet_id++;
      } else if(m_tx_failed) {
        printf("TX failed due to busy: %u\r\n",
               (unsigned)m_tx_errorcode);
      } else {
        printf("TX TIMEOUT!\r\n");
      }
    } else {
      printf("TX FAILED!\r\n");
    }
  }
}

void nrf_802154_transmitted(const uint8_t * p_frame, uint8_t * p_ack, uint8_t length, int8_t power, uint8_t lqi)
{
  (void) p_frame;
  (void) length;
  (void) power;
  (void) lqi;

  m_tx_done = true;

  if (p_ack != NULL)
    {
      nrf_802154_buffer_free(p_ack);
    }
}

void nrf_802154_transmit_failed(const uint8_t * p_frame, nrf_802154_tx_error_t error)
{
  (void) p_frame;

  m_tx_failed = true;
  m_tx_errorcode = error;
}

void nrf_802154_received(uint8_t * p_data, uint8_t length, int8_t power, uint8_t lqi)
{
  nrfx_gpiote_out_toggle(led);

  printf("RX frame, power %d, lqi %u, payload len %u: ",
         (int) power, (unsigned) lqi, (unsigned) length);
  for(int i = 0; i < length; ++i)
    printf("%02x ", p_data[i]);
  if(length == MAX_MESSAGE_SIZE+2)
    printf("-- temp: %5.2fC", (*(uint32_t*)&p_data[13])/4.);
  printf("\r\n");

  nrf_802154_buffer_free(p_data);
}
