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
#include "nrf_drv_spi.h"

#include "buckler.h"
#include "display.h"
#include "kobukiActuator.h"
#include "kobukiSensorPoll.h"
#include "kobukiSensorTypes.h"
#include "kobukiUtilities.h"
#include "lsm9ds1.h"
#include "simple_ble.h"

#include "states.h"

// I2C manager
NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

// global variables
KobukiSensors_t sensors = {0};
bool cliff_is_right = false;
bool bump_is_right = false;
bool prev_cliff_state = false;
bool prev_bump_state = false;

// Intervals for advertising and connections
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
                0x59,0x4D,0x5e,0xf6,0xa0,0xed,0x07,0x46}
}};

// TODO: Declare characteristics and variables for your service
static simple_ble_char_t driving_left_char = {.uuid16 = 0x100b};
static simple_ble_char_t driving_right_char = {.uuid16 = 0x100c};
static int driving_left = 0;
static int driving_right = 0;


simple_ble_app_t* simple_ble_app;

void ble_evt_write(ble_evt_t const* p_ble_evt) {
    // TODO: logic for each characteristic and related state changes
  //   if (simple_ble_is_char_event(p_ble_evt, &driving_state_char)) {
  // }
}

// Return true if a cliff has been seen
// Save information about which cliff
static bool check_cliff(KobukiSensors_t* sensors, bool* cliff_is_right) {
  *cliff_is_right = false;
  if (sensors->cliffLeft || sensors->cliffCenter || sensors->cliffRight) {
    if (!sensors->cliffLeft) {
      *cliff_is_right = true;
    }
    return true;
  }
  return false;
}

// Return true if a bump has been detected
// Save information about which bump
static bool check_bump(KobukiSensors_t* sensors, bool* bump_is_right) {
  *bump_is_right = false;
  if (sensors->bumps_wheelDrops.bumpLeft || sensors->bumps_wheelDrops.bumpCenter || sensors->bumps_wheelDrops.bumpRight) {
    if (!sensors->bumps_wheelDrops.bumpLeft) {
      *bump_is_right = true;
    }
    return true;
  }
  return false;
}

// Send update in state via advertisement
// buffer[0] if cliff present
// buffer[1] if cliff is right
// buffer[2] if bump is present
// buffer[3] if bump is right
void set_payload(bool cliff, bool bump) {
  static uint8_t buffer[4] = {0};
  if (!cliff && !bump) {
    buffer[0] = 0;
    buffer[1] = 0;
    buffer[2] = 0;
    buffer[3] = 0;
  }

  if (cliff) {
    buffer[0] = 1;
    if (cliff_is_right) {
      buffer[1] = 1;
    } else {
      buffer[1] = 0;
    }
  }
  if (bump) {
    buffer[2] = 1;    
    if (bump_is_right) {
      buffer[3] = 1;
    } else {
      buffer[3] = 0;
    }
  }

  simple_ble_adv_manuf_data(buffer, 4);
}

void print_state(states current_state){
  switch(current_state){
  case OFF:
    display_write("OFF", DISPLAY_LINE_0);
    break;
  case ON:
    if (check_bump(&sensors, &bump_is_right) && check_cliff(&sensors, &cliff_is_right)) {
      display_write("ON: both", DISPLAY_LINE_0);
    } else if (check_bump(&sensors, &bump_is_right)) {
      display_write("ON: bump", DISPLAY_LINE_0);
    } else if (check_cliff(&sensors, &cliff_is_right)) {
      display_write("ON: cliff", DISPLAY_LINE_0);
    } else {
      display_write("ON: none", DISPLAY_LINE_0);
    }
    // display_write("ON", DISPLAY_LINE_0);
    break;
  } 
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
    sizeof(driving_left), (uint8_t*)&driving_left,
    &robot_service, &driving_left_char);
  
  simple_ble_add_characteristic(1, 1, 0, 0,
    sizeof(driving_right), (uint8_t*)&driving_right,
    &robot_service, &driving_right_char);

  // Start Advertising
  // simple_ble_adv_only_name();

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

  // initialize i2c master (two wire interface)
  nrf_drv_twi_config_t i2c_config = NRF_DRV_TWI_DEFAULT_CONFIG;
  i2c_config.scl = BUCKLER_SENSORS_SCL;
  i2c_config.sda = BUCKLER_SENSORS_SDA;
  i2c_config.frequency = NRF_TWIM_FREQ_100K;
  error_code = nrf_twi_mngr_init(&twi_mngr_instance, &i2c_config);
  APP_ERROR_CHECK(error_code);
  lsm9ds1_init(&twi_mngr_instance);
  printf("IMU initialized!\n");

  // initialize Kobuki
  kobukiInit();
  printf("Kobuki initialized!\n");

  states state = ON;

  // loop forever, running state machine
  while (1) {
    // read sensors from robot
    int status = kobukiSensorPoll(&sensors);

    print_state(state);

    switch(state) {
      case OFF: {
        kobukiDriveDirect(0, 0);
        break;
      }
      case ON: {
        kobukiDriveDirect(driving_left, driving_right);
        // set_payload(&sensors);
        bool cliff = check_cliff(&sensors, &cliff_is_right);
        bool bump = check_bump(&sensors, &bump_is_right);
        if (prev_cliff_state != cliff || prev_bump_state != bump) {
          // advertise change in state if readings are new
          set_payload(cliff, bump);
          prev_cliff_state = cliff;
          prev_bump_state = bump;
        } 
        break;
      }
    }
  }
}