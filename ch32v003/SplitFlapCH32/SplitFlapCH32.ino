// qre1113 sensor board, the values of the resisors for 5V VCC are
// 330   LED cathode
// 200K  phototransistor collector
// 1K (default) output

// these options need configuration update in the package
// go to %USERHOME%\Documents\Arduino\hardware\ch32v\arduino_core_ch32\variants\CH32V00x\CH32V003F4\variant_CH32V003F4.h
// for SERIAL
//     #define                         UART_MODULE_ENABLED
// for I2C
//     #define                         I2C_MODULE_ENABLED
// 
//#define UART_MODULE_ENABLED 0
//#define I2C_MODULE_ENABLED 1


#include <Arduino.h>
#include <EEPROM.h>

#ifdef I2C_MODULE_ENABLED
#warning BUILDING I2C VERSION
#include <Wire.h>
#define SLAVE_ADDR 0x10
#endif

#define EEPROM_ADDR_CHECKSUM      0
#define EEPROM_ADDR_I2C_ADDR      1
#define EEPROM_ADDR_HOMING_ADJUST 2
#define EEPROM_SIZE               3


#ifdef UART_MODULE_ENABLED
#warning BUILDING SERIAL VERSION
#endif

enum state_t {
  ST_IDLE = 0,
  ST_HOMING1,  
  ST_HOMING2,
  ST_HOMING3,
  ST_SEEK,
  ST_PAUSE,
  ST_PAUSE_RESTART
};

enum track_sensor_result_t { TS_IDLE, TS_PULSE, TS_MARKER };

constexpr int8_t HOMING_ADJUST = -2; //-2;

constexpr int SERVO_PIN = PD3;
constexpr int SPEED_STOP = 180/2 - 2;

constexpr int SERVO_PULSE_MIN = 3276;
constexpr int SERVO_PULSE_MAX = 6553;

int16_t tracking_max = 300;
int16_t tracking_min = 0;
int16_t tracking_out = 0;

int16_t pulse_time = 0;
int16_t avg_pulse_time = 0;

constexpr int STEP_MILLIS = 1;
constexpr int tracking_div = 92;
constexpr int INVERT = 1;

int8_t homing_skips = 0;
int8_t current_pos = 0;
int8_t seek_pos = 0;
state_t state = ST_IDLE;

uint8_t my_i2c_address = SLAVE_ADDR;
int8_t  my_homing_adjust = HOMING_ADJUST;

track_sensor_result_t track_sensor(int16_t x);
void eeprom_load_settings();
void eeprom_save_settings();

constexpr int SERVO_SPEED_1 = (SERVO_PULSE_MIN + SERVO_PULSE_MAX) / 2 + 400;//1 * 180;

long start_time, prev_millis;

#ifdef I2C_MODULE_ENABLED
volatile int i2c_command;
volatile uint8_t i2c_acknowledge;

void sendDiagnostics();

static int8_t set_new_addr()
{
  uint8_t addr, naddr;

  if (Wire.available()) 
    addr = Wire.read();
  else
    return -1;
  if (Wire.available())
    naddr = Wire.read();
  else
    return -1;

  if (addr == static_cast<uint8_t>(~naddr)) {
    my_i2c_address = addr;
    eeprom_save_settings();
    Wire.end();
    Wire.begin(my_i2c_address);
    return 0;
  }

  return -1;
}

static int8_t set_new_adjust()
{
  if (Wire.available()) {
    my_homing_adjust = static_cast<int8_t>(Wire.read());
    eeprom_save_settings();
    return 0;
  }

  return -1;
}

static void onReceive(int num_bytes)
{
    (void)num_bytes;
    while(Wire.available()) {
        i2c_command = (uint8_t)Wire.read();
        switch (i2c_command) {
          case 'q':
            i2c_acknowledge = 1;
            while (Wire.available()) Wire.read();
            return;
          case 'a': // set addr: new addr, new addr inverted
            set_new_addr();
            break;
          case 'z': // set homing adjust
            set_new_adjust();
            break;
        }
    }
}

static void onRequest(void)
{
    if (i2c_acknowledge) {
        i2c_acknowledge = 0;
        Wire.write("SFD");
        Wire.write(my_i2c_address);
        Wire.write((uint8_t)my_homing_adjust);
        Wire.write(current_pos);
        Wire.write(state);
    }
    else {
      sendDiagnostics();
    }
}
#endif

void servo_start()
{
    analogWrite(SERVO_PIN, SERVO_SPEED_1);
}

void servo_stop()
{
    analogWrite(SERVO_PIN, (SERVO_PULSE_MIN + SERVO_PULSE_MAX) / 2); 
}

void eeprom_load_settings()
{
  uint8_t csum = 0;
  for (uint8_t adr = 1; adr < EEPROM_SIZE; ++adr) {
    csum += EEPROM[adr];
  }
  if (csum == EEPROM[EEPROM_ADDR_CHECKSUM]) {
    my_i2c_address = EEPROM[EEPROM_ADDR_I2C_ADDR];
    my_homing_adjust = static_cast<int8_t>(EEPROM[EEPROM_ADDR_HOMING_ADJUST]);
  }
}

void eeprom_save_settings()
{
  uint8_t csum = 0;
  csum += EEPROM[EEPROM_ADDR_I2C_ADDR] = my_i2c_address;
  csum += EEPROM[EEPROM_ADDR_HOMING_ADJUST] = static_cast<uint8_t>(my_homing_adjust);
  EEPROM[EEPROM_ADDR_CHECKSUM] = csum;
  EEPROM.commit();
}

void setup()
{
  my_i2c_address = SLAVE_ADDR;
  my_homing_adjust = HOMING_ADJUST;
  EEPROM.begin();
  eeprom_load_settings();  

#ifdef UART_MODULE_ENABLED  
  Serial.begin(115200);
#endif  
  delay(3000);
#ifdef UART_MODULE_ENABLED  
  Serial.println("hello.jpg");
#endif  

#ifdef I2C_MODULE_ENABLED
  Wire.begin(my_i2c_address);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
#endif

  pinMode(PD3, INPUT_PULLUP);

  pinMode(SERVO_PIN, OUTPUT);
  analogWriteFrequency(50);
  analogWriteResolution(16);
  analogWrite(SERVO_PIN, SERVO_SPEED_1);  
  prev_millis = start_time = millis();

  state = ST_HOMING1;
  homing_skips = 20;

  i2c_acknowledge = 0;
  i2c_command = -1;
}

track_sensor_result_t track_sensor(int16_t x)
{
  track_sensor_result_t result = TS_IDLE;

  if (x > tracking_max) 
    tracking_max = x;
  else 
    tracking_max -= (tracking_max - x) / tracking_div;

  if (x < tracking_min)
    tracking_min = x;
  else
    tracking_min += (x - tracking_min) / tracking_div;

#ifdef UART_MODULE_ENABLED
  Serial.print(tracking_min); Serial.print(' '); Serial.print(tracking_max); Serial.print(' ');
#endif

  // set hysteresis threshold
  int16_t median = (tracking_min + tracking_max) / 2;
  int16_t thresh_low = median - 40;
  int16_t thresh_high = median + 40;

  int16_t prev_tracking_out = tracking_out;
  if (tracking_out == 0 && x > thresh_high) {
      tracking_out = 1;
  }
  else if (tracking_out != 0 && x < thresh_low) {
      tracking_out = 0;
  }

  if (prev_tracking_out != INVERT && tracking_out == INVERT) {
      pulse_time = 0;
  }
  else if (tracking_out == INVERT) {
      pulse_time += 1;
  }
  else if (prev_tracking_out == INVERT && tracking_out != INVERT) {
      avg_pulse_time = (3 * avg_pulse_time + pulse_time) / 4;
      if (INVERT == 0) {
        int thresh = avg_pulse_time * 13 / 8; //7 / 4; //5 / 3;
        result = TS_PULSE;
        if (pulse_time > thresh) {
          result = TS_MARKER;
        }
      }
      else {
        int thresh = avg_pulse_time / 2;
        result = TS_PULSE;
        if (pulse_time <= thresh) {
          result = TS_MARKER;
        }
      }
  }

  return result;
}

#ifdef I2C_MODULE_ENABLED
struct diags_buf_t {
    int16_t tracking_min, tracking_max;
    int16_t sensor_value;
    track_sensor_result_t ts;
    int8_t current_pos;
} __attribute__((packed));

static diags_buf_t diags[2];
uint8_t diags_wr_index = 0;
uint8_t diags_rd_index = 1;

void updateDiagnostics(int16_t sensor_value, track_sensor_result_t ts)
{
    diags[diags_wr_index].tracking_min = tracking_min;
    diags[diags_wr_index].tracking_max = tracking_max;
    diags[diags_wr_index].sensor_value = sensor_value;
    diags[diags_wr_index].ts = ts;
    diags[diags_wr_index].current_pos = current_pos;
}

void wire_write_i16(int16_t value)
{
    Wire.write((value >> 8) & 0xff);
    Wire.write(value & 0xff);
}

void sendDiagnostics()
{
    wire_write_i16(diags[diags_rd_index].tracking_min);
    wire_write_i16(diags[diags_rd_index].tracking_max);
    wire_write_i16(diags[diags_rd_index].sensor_value);
    Wire.write(diags[diags_rd_index].ts);
    Wire.write(diags[diags_rd_index].current_pos);
    diags_wr_index ^= 1;
    diags_rd_index ^= 1;
}
#endif

void loop() {
  long now = millis();
  if (1 || now - prev_millis > STEP_MILLIS) {
    int c;
#ifdef UART_MODULE_ENABLED
    c = Serial.read();
#endif
#ifdef I2C_MODULE_ENABLED
    c = i2c_command;
    i2c_command = -1;
#endif
    if (c == 'h') {
      state = ST_HOMING1;
      homing_skips = 20;
      servo_start();
    }
    else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
      int pos = -1;

      if (c >= 'A' && c <= 'Z') {
        pos = c - 'A' + 1;
      }
      else if (c >= '0' && c <= '9') {
        pos = 27 + (c - '0');
      }
      else if (c == '-') {
        pos = 37;
      }

      state = ST_SEEK;
      servo_start();      
      seek_pos = pos;
    }

    prev_millis = now;
    int sensorValue1 = analogRead(A2);
    track_sensor_result_t ts = track_sensor(sensorValue1);    
#ifdef UART_MODULE_ENABLED
    Serial.print(sensorValue1); Serial.print(' '); Serial.print(ts * 100); Serial.print(' '); Serial.println(current_pos);
#endif
#ifdef I2C_MODULE_ENABLED
    updateDiagnostics(sensorValue1, ts);
#endif

    switch (ts) {
      case TS_MARKER:
        if (state == ST_HOMING2) {
          current_pos = my_homing_adjust + 38;
          seek_pos = 0;
          state = ST_SEEK;
        }
        else {
          current_pos = (current_pos + 1) % 38;
        }
        
        //if (state == ST_HOMING3) {
        //  seek_pos = 0;
        //  state = ST_SEEK;
        //}
        //else
        if (state == ST_SEEK && seek_pos == current_pos) {
            servo_stop();
            state = ST_PAUSE_RESTART;
            start_time = now;
        }
        break;
      case TS_PULSE:
        current_pos = (current_pos + 1) % 38;
        if (state == ST_HOMING1) {
          if (homing_skips > 0) {
            if (--homing_skips == 0) state = ST_HOMING2;
          }
        }
        if (state == ST_SEEK) {
          if (current_pos == seek_pos) {
            servo_stop();
            //state = ST_IDLE;
            state = ST_PAUSE_RESTART;
            start_time = now;
          }
        }
        break;
      case TS_IDLE:
      default:
        break;
    }
  }
}
/* vim: set filetype=cpp: */
