// Robot Template app
//
// Framework for creating applications that control the Kobuki robot

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_error.h"
#include "app_timer.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_serial.h"
#include "nrf_drv_spi.h"


#include "app_uart.h"
#include "nrf_uarte.h"

#include "buckler.h"
#include "display.h"
//#include "kobukiActuator.h"
//#include "kobukiSensorPoll.h"
//#include "kobukiSensorTypes.h"
#include "kobukiUtilities.h"
#include "mpu9250.h"
#include "simple_ble.h"

#include <time.h>

#include "states.h"

#define UART_RX              NRF_GPIO_PIN_MAP(0, 8)
#define UART_TX              NRF_GPIO_PIN_MAP(0, 6)
#define UART_TX_BUF_SIZE     256
#define UART_RX_BUF_SIZE     256

int bump_val;
bool header = false;
bool val = true;
int count = 0;

static uint8_t bump_cliff = 0;

void uart_handle (app_uart_evt_t * p_event) {
  // just call app error handler
  static uint8_t data_array[5];
  static uint8_t index = 0;
  uint32_t err_code;
  if(p_event->evt_type == APP_UART_DATA){
    err_code = app_uart_get(&data_array[index]);
    ++index;
    if(index > 2 && index <= 5 && data_array[index-1] == 0xFF){
      if(data_array[index - 3] == 0xAA) bump_cliff = data_array[index-2];
      index = 0;
    }
    if(index >= 5) index = 0;
  }else if (p_event->evt_type == APP_UART_COMMUNICATION_ERROR) {
    
    // display_write("error com", DISPLAY_LINE_0);
    //  APP_ERROR_HANDLER(p_event->data.error_communication);
      
  } else if (p_event->evt_type == APP_UART_FIFO_ERROR) {
    
      display_write("error fifo", DISPLAY_LINE_0);
    APP_ERROR_HANDLER(p_event->data.error_code);
  }
}

// initialization of UART
void uart_init(void) {
  uint32_t err_code;

  // configure RX and TX pins
  // no RTS or CTS pins with flow control disabled
  // no parity
  // baudrate 115200
  const app_uart_comm_params_t comm_params = {
    UART_RX, UART_TX,
    0, 0, APP_UART_FLOW_CONTROL_DISABLED,
    false,
    NRF_UARTE_BAUDRATE_115200
  };

  // actually initialize UART
  APP_UART_FIFO_INIT(&comm_params, UART_RX_BUF_SIZE, UART_TX_BUF_SIZE,
        uart_handle, APP_IRQ_PRIORITY_LOW, err_code);
  APP_ERROR_CHECK(err_code);
}


// I2C manager
NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

// global variables
KobukiSensors_t sensors = {0};

//// Intervals for advertising and connections
static simple_ble_config_t ble_config = {
        // c0:98:e5:49:xx:xx
        .platform_id       = 0x49,    // used as 4th octect in device BLE address
        .device_id         = 0x0013, // TODO: replace with your lab bench number
        .adv_name          = "wheelchair", // used in advertisements if there is room
        .adv_interval      = MSEC_TO_UNITS(1000, UNIT_0_625_MS),
        .min_conn_interval = MSEC_TO_UNITS(100, UNIT_1_25_MS),
        .max_conn_interval = MSEC_TO_UNITS(200, UNIT_1_25_MS),
};

//4607eda0-f65e-4d59-a9ff-84420d87a4ca
static simple_ble_service_t robot_service = {{
    .uuid128 = {0xca,0xa4,0x87,0x0d,0x42,0x84,0xff,0xA9,
                0x59,0x4D,0x5e,0xf6,0xa0,0xed,0x69,0x49}
}};



// TODO: Declare characteristics and variables for your service
static simple_ble_char_t driving_left_char = {.uuid16 = 0x100a};
static simple_ble_char_t driving_right_char = {.uuid16 = 0x100b};
static simple_ble_char_t bump_cliff_char = {.uuid16 = 0x100c};

static int16_t target_left = 0;
static int16_t target_right = 0;


// Intervals for advertising and connections

// TODO: Declare characteristics and variables for your service

simple_ble_app_t* simple_ble_app;

void ble_evt_write(ble_evt_t const* p_ble_evt) {
    // TODO: logic for each characteristic and related state changes
}

void print_state(states current_state){
  switch(current_state){
  case OFF:
    display_write("OFF", DISPLAY_LINE_0);
    break;
    }
}

extern const nrf_serial_t * serial_ref;

/* void getCliffBump(){ */

/*   uint8_t headerByte = 0x0; */
/*   uint8_t header2Byte = 0x0; */
/*   uint8_t targetVal = 0x0; */
/*   clock_t before = clock(); */
/*   //Timeout after 100ms if byte not received */
  
/*   while((clock() < before + CLOCKS_PER_SEC / 10) && app_uart_get(&headerByte) != NRF_SUCCESS); */
/*   if(headerByte == 0xAA){ */
/*     before = clock(); */
/*     while((clock() < before + CLOCKS_PER_SEC / 10) && app_uart_get(&header2Byte) != NRF_SUCCESS); */

/*     before = clock(); */
/*     while((clock() < before + CLOCKS_PER_SEC/10) && app_uart_get(&targetVal) != NRF_SUCCESS); */
    
/*     if(header2Byte == 0xFF){ */
/* 	bump_cliff = targetVal; */
/*     } */
/*   } */
/* } */

void sendLeftRight(){
    //range of encoder values -170 -> 170
  
    bool leftNegative = (target_left < 0);
    bool rightNegative = (target_right < 0);

    uint8_t sendLeft = (uint8_t) (target_left & 0xFF);
    uint8_t sendRight = (uint8_t) (target_right & 0xFF);

    uint8_t metadata = (((target_left == -500 && target_right == -500) && 1) << 7) | (leftNegative << 1) | rightNegative;
    
    while(app_uart_put(0xFF) != NRF_SUCCESS);
    nrf_delay_ms(10);
    
    while(app_uart_put(sendLeft) != NRF_SUCCESS);
    nrf_delay_ms(10);

    while(app_uart_put(sendRight) != NRF_SUCCESS);
    nrf_delay_ms(10);
    
    while(app_uart_put(metadata) != NRF_SUCCESS);
    nrf_delay_ms(10);
    
    while(app_uart_put(0xAB) != NRF_SUCCESS);
    nrf_delay_ms(10);

    
}

int main(void) {
  ret_code_t error_code = NRF_SUCCESS;

  // initialize RTT library
  error_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(error_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();
  printf("Log initialized!\n");

  // Setup BLE
  simple_ble_app = simple_ble_init(&ble_config);

  simple_ble_add_service(&robot_service);

 
  // TODO: Register your characteristics
simple_ble_add_characteristic(1, 1, 0, 0,
    sizeof(target_left), (int16_t*)&target_left,
    &robot_service, &driving_left_char);
  
simple_ble_add_characteristic(1, 1, 0, 0,
   sizeof(target_right), (int16_t*)&target_right,
   &robot_service, &driving_right_char);

  simple_ble_add_characteristic(1, 0, 0, 0,
    sizeof(bump_cliff), (uint8_t*)&bump_cliff,
    &robot_service, &bump_cliff_char);
  
  // Start Advertising
  simple_ble_adv_only_name();

  // initialize LEDs
  nrf_gpio_pin_dir_set(23, NRF_GPIO_PIN_DIR_OUTPUT);
  nrf_gpio_pin_dir_set(24, NRF_GPIO_PIN_DIR_OUTPUT);
  nrf_gpio_pin_dir_set(25, NRF_GPIO_PIN_DIR_OUTPUT);

  // initialize display
  nrf_drv_spi_t spi_instance = NRF_DRV_SPI_INSTANCE(1);
  nrf_drv_spi_config_t spi_config = {
    .sck_pin = BUCKLER_LCD_SCLK,
    .mosi_pin = BUCKLER_LCD_MOSI,
    .miso_pin = BUCKLER_LCD_MISO,
    .ss_pin = BUCKLER_LCD_CS,
    .irq_priority = NRFX_SPI_DEFAULT_CONFIG_IRQ_PRIORITY,
    .orc = 0,
    .frequency = NRF_DRV_SPI_FREQ_4M,
    .mode = NRF_DRV_SPI_MODE_2,
    .bit_order = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST
  };
  error_code = nrf_drv_spi_init(&spi_instance, &spi_config, NULL, NULL);
  APP_ERROR_CHECK(error_code);
  display_init(&spi_instance);
  printf("Display initialized!\n");

  display_write("INIT UART", DISPLAY_LINE_0);
  uart_init();
  printf("UART initialized\n");
  states state = OFF;

  char buf[16];
  // loop forever, running state machine
  while (1) {
    sprintf(buf, "BUMP: %d", bump_cliff);
    display_write(buf, DISPLAY_LINE_0);
    sendLeftRight();
    nrf_delay_ms(10);
  }
}
