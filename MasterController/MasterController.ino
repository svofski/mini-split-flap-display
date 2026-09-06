#include <Wire.h>
#include <errno.h>
#include "SerialCommand.h" // SerialCommand by Steven Cogswell and Stefan Rado

#define MAX_SPLIT_FLAPS 16

SerialCommand cmd;

uint8_t splitflaps[MAX_SPLIT_FLAPS] {};
uint8_t n_splitflaps = 0;

uint8_t budget_homing = 10;
uint8_t budget_typing = 10;

// Console commands: 

// t HELLO         type HELLO from line start
// a addr          assign address (only valid when there is a single display connected)
//   a 0x11	set display i2c address = 0x11
// z addr offset   set homing offset
//   z 0x11 3      display at 0x11 homes to position 3
// d addr div      set tracking divider
// 1 0x13 A        send 'A' to addr 0x13
// r addr n        speed adjust, add n * 8 us to pulse time (signed byte)

static uint8_t response_buf[16];

// print buffer
static char text[MAX_SPLIT_FLAPS + 1];

void printhex(uint8_t val)
{
    if (val < 16) Serial.print('0');
    Serial.print(val, 16);
}

void hexdump(const uint8_t * buf, uint8_t len)
{
    for (uint16_t i = 0; i < len; ++i) {
        printhex(buf[i]);
        Serial.print(' ');
    }
}

// return state or -1 if error
int8_t check_display(uint8_t addr, bool verbose=true, bool save=false)
{
    Wire.beginTransmission(addr);
    Wire.write('q');
    Wire.endTransmission();

    static constexpr uint8_t status_sz = 9;

    Wire.requestFrom((uint8_t)addr, status_sz); // expect response S F D aa zz pp ss tt rr

    uint8_t i = 0;
    while (Wire.available() && i < sizeof(response_buf)) {
        response_buf[i++] = Wire.read();
    }

    //if (verbose) hexdump(response_buf, i);

    if (i < status_sz) {
        return -1;
    }
    if (memcmp_P(response_buf, PSTR("SFD"), 3) == 0) {
        uint8_t addr = response_buf[3];
        int8_t zero = static_cast<int8_t>(response_buf[4]);
        uint8_t pos = response_buf[5];
        uint8_t state = response_buf[6];
        uint8_t tracking_div = response_buf[7];
        int16_t speed_adj = static_cast<int8_t>(response_buf[8]);
        if (verbose) {
            Serial.print(F("SFD 0x")); Serial.print(addr, 16); 
            Serial.print(F(" home:"));
            if (zero >= 0) Serial.print('+'); Serial.print(zero, 10); 
            Serial.print(F(" pos:")); Serial.print(pos);
            Serial.print(F(" state:")); Serial.print(state);
            Serial.print(F(" div:")); Serial.print(tracking_div);
            Serial.print(F(" speed:")); Serial.print(speed_adj * 8);
        }

        if (save) {
            if (n_splitflaps >= MAX_SPLIT_FLAPS) {
                return -1;
            }
            else {
                splitflaps[n_splitflaps] = addr;
                ++n_splitflaps;
            }
        }

        return state;

    }

    return -1;
}

int8_t wait_for_idle(uint8_t addr)
{
    int8_t result;
    do {
        result = check_display(addr, false, false);
    } while (result >= 0 && (result != 6) && (result != 0));

    return result;
}

int8_t count_busy_units()
{
    int8_t count = 0;
    for (uint8_t i = 0; i < n_splitflaps; ++i) {
        if (check_display(splitflaps[i], false, false) != 6) ++count;
    }

    return count;
}

void wait_budget(uint8_t budget)
{
    uint8_t n_busy;
    do {
        n_busy = count_busy_units();
        delay(10);
    } while (n_busy >= budget);
}

void scan_i2c()
{
    memset(splitflaps, 0, sizeof(splitflaps));
    n_splitflaps = 0;

    Serial.println(F("Scanning..."));
    int nDevices = 0;
    for (byte address = 1; address < 127; ++address) {
        // The i2c_scanner uses the return value of
        // the Wire.endTransmission to see if
        // a device did acknowledge to the address.
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();

        if (error == 0) {
            if (check_display(address, true, true) < 0) {
                Serial.print(F(" ERROR"));
            }

            Serial.println();

            ++nDevices;
        } 
        else if (error == 4) {
            Serial.print(F("Unknown error at address 0x"));
            if (address < 16) {
                Serial.print('0');
            }
            Serial.println(address, HEX);
        }
    }
    if (nDevices == 0) {
        Serial.println(F("No I2C devices found\n"));
    }

    Serial.print(F("Found ")); Serial.print(n_splitflaps);
    Serial.print(F(" split-flap displays: "));
    hexdump(splitflaps, n_splitflaps);
    Serial.println();
}

void sendAck()
{
    Serial.println(F("OK"));
}

void sendNak()
{
    Serial.println(F("ERROR"));
}

void send_to_display(uint8_t addr, char ch)
{
    Wire.beginTransmission(addr);
    Wire.write(ch);
    Wire.endTransmission();
}

void cmdSingleChar()
{
    const char *arg = cmd.next();
    uint8_t addr = 255;

    errno = 0;
    int valor = strtol(arg, nullptr, 0);
    if (errno == 0 && valor > 0 && valor < 128) {
        addr = valor;
    }
    if (addr == 255) return;

    arg = cmd.next();
    if (arg == nullptr) return;

    send_to_display(addr, *arg);    
}

void print_text(const char *arg)
{
    uint8_t len = min(strlen(arg), n_splitflaps);

    char msg[MAX_SPLIT_FLAPS];
    memcpy(&msg[0], arg, len);

    int8_t remains = (int8_t)len;

    do {
        for (uint8_t i = 0; i < len; ++i) {
            uint8_t c = msg[i];
            if (c == 0)
                continue;

            wait_budget(budget_typing);
            switch (c) {
                case 'A'...'Z':
                case '0'...'9':
                case '-':
                case ' ':
                case 'h':
                    send_to_display(splitflaps[i], c);
                    break;
                default:
                    break;
            }
            msg[i] = 0;
            --remains;
        }
    } while (remains > 0);
}

// build text[] buffer from multiple args and return length
int8_t prepare_text(const char * first)
{
    uint8_t pos = 0;

    while(1) {
        const char *arg = first ? first : cmd.next();
        first = nullptr;
        if (arg == nullptr) break;
        if (pos > 0) {
            text[pos++] = '_';
        }
        for (uint8_t n = 0; (pos < MAX_SPLIT_FLAPS) && (arg[n] != 0); ++pos, ++n) {
            text[pos] = arg[n];
        }
    }
    text[pos] = 0;

    return pos;
}

void cmdType()
{
    if (prepare_text(nullptr) == 0) {
        sendNak();
        return;
    }
    print_text(text);
    sendAck();    
}

void cmdPrintDefault(const char *cmd)
{
    if (prepare_text(cmd) == 0) {
        sendNak();
        return;
    }
    cmdHome();
    print_text(text);
}

void cmdHome()
{
    Serial.print(F("Homing: "));

    static uint8_t homed[MAX_SPLIT_FLAPS];
    for (uint8_t i = 0; i < n_splitflaps; ++i) homed[i] = 0;

    uint8_t n;
    uint8_t n_homed;
    do {
        for (n = 0, n_homed = 0; n < n_splitflaps; ++n) {
            if (homed[n] == 0) {
                wait_budget(budget_homing);
                printhex(splitflaps[n]); Serial.print('.');
                send_to_display(splitflaps[n], 'h');
                homed[n] = 1;
                delay(10);
            }
            else {
                ++n_homed;
            }
        }
    } while (n_homed < n_splitflaps);
    Serial.println();
    sendAck();
}

void cmdPrint()
{
    cmdHome();
    cmdType();
    sendAck();
}

void cmdAssignAddress()
{
    if (n_splitflaps != 1) {
        Serial.print(F("address can be set only when there is a single display on the bus; now: ")); Serial.println(n_splitflaps);
        sendNak();
        return;
    }

    const char *arg1 = cmd.next();
    if (arg1 != nullptr) {
        errno = 0;
        int valor = strtol(arg1, nullptr, 0);
        if (errno == 0 && valor > 0 && valor < 128) {
            uint8_t addr = static_cast<uint8_t>(valor);
            Serial.print(F("set address to 0x")); printhex(addr); Serial.println();
            Wire.beginTransmission(splitflaps[0]);
            Wire.write('a');
            Wire.write(addr);
            Wire.write(static_cast<uint8_t>(~addr));
            Wire.endTransmission();
            sendAck();
            scan_i2c();
            return;
        }
    }

    sendNak();
}

int get_int_arg(int min_value, int max_value, int default_value)
{
    const char *arg = cmd.next();
    if (arg == nullptr) return default_value;
    errno = 0;
    int valor = strtol(arg, nullptr, 0);
    if (errno == 0 && valor >= min_value && valor <= max_value) {
        return valor;
    }

    return default_value;
}

void cmdSetHomingOffset()
{
    uint8_t addr = get_int_arg(1, 254, 255);
    uint8_t offset = get_int_arg(-37, 37, 127);

    if (addr == 255 || offset == 127) {
        sendNak();
        return;
    }

    for (uint8_t i = 0; i < n_splitflaps; ++i) {
        if (splitflaps[i] == addr) {
            Wire.beginTransmission(addr);
            Wire.write('z');
            Wire.write(static_cast<uint8_t>(offset));
            Wire.endTransmission();
            sendAck();
            return;
        }
    }

    sendNak();
}

void cmdSetSpeedAdjust()
{
    uint8_t addr = get_int_arg(1, 254, 255);
    int16_t offset = get_int_arg(-128, 127, 256);

    if (addr == 255 || offset == 256) {
        sendNak();
        return;
    }

    for (uint8_t i = 0; i < n_splitflaps; ++i) {
        if (splitflaps[i] == addr) {
            Wire.beginTransmission(addr);
            Wire.write('r');
            Wire.write(static_cast<uint8_t>(offset));
            Wire.endTransmission();
            sendAck();
            return;
        }
    }

    sendNak();
}

void cmdSetTrackingDiv()
{
    uint8_t addr = get_int_arg(1, 254, 255);
    uint8_t tracking_div = get_int_arg(1, 255, 0);

    if (addr == 255 || tracking_div == 0) {
        sendNak();
        return;
    }

    for (uint8_t i = 0; i < n_splitflaps; ++i) {
        if (splitflaps[i] == addr) {
            Wire.beginTransmission(addr);
            Wire.write('d');
            Wire.write(tracking_div);
            Wire.endTransmission();
            sendAck();
            return;
        }
    }

    sendNak();
}

void cmdVersion()
{
    Serial.println(F("SFMC 0.1"));
    sendAck();
}

void cmdStatus()
{
    scan_i2c();
    Serial.print(F("Power budget: homing: ")); Serial.print(budget_homing);
    Serial.print(F(" typing: ")); Serial.println(budget_typing);
    sendAck();
}

void setup() {
    Wire.begin();
    Serial.begin(115200);

    cmd.addCommand("p", cmdPrint); // print text on split-flaps with re-homing
    cmd.addCommand("t", cmdType); // print text on split-flaps without homing (use lowercase h for space)
    cmd.addCommand("a", cmdAssignAddress); // assign address X to the only split-flap display on the bus
    cmd.addCommand("z", cmdSetHomingOffset); // set homing offset to split-flap display N = X
    cmd.addCommand("d", cmdSetTrackingDiv); // set tracking divider
    cmd.addCommand("v", cmdVersion); // print version string
    cmd.addCommand("s", cmdStatus); // print status of every connected display
    cmd.addCommand("1", cmdSingleChar); // send char to address, e.g. "1 0x10 A" 
    cmd.addCommand("h", cmdHome); // home all displays
    cmd.addCommand("r", cmdSetSpeedAdjust); // speed adjust x8, e.g. r 0x10 100 (add 800us)
    cmd.setDefaultHandler(cmdPrintDefault);

    cmdVersion();
    delay(500);
    cmdStatus();
}

enum track_sensor_result_t { TS_IDLE, TS_PULSE, TS_MARKER };
struct diags_buf_t {
    uint8_t first;
    int16_t tracking_min, tracking_max;
    int16_t sensor_value;
    uint8_t ts;
    int8_t current_pos;
    uint8_t last;
} __attribute__((packed));

void getDiags(uint8_t addr)
{
    Wire.requestFrom((int)addr, sizeof(diags_buf_t));

    uint8_t i = 0;
    while (Wire.available() && i < sizeof(response_buf)) {
        response_buf[i++] = Wire.read();
    }
    while(Wire.available()) (void)Wire.read();

    diags_buf_t * diags = reinterpret_cast<diags_buf_t *>(&response_buf[0]);
    Serial.print(diags->tracking_min); Serial.print(' ');
    Serial.print(diags->tracking_max); Serial.print(' ');
    Serial.print(diags->sensor_value); Serial.print(' ');
    Serial.print(diags->ts); Serial.print(' ');
    Serial.print(diags->current_pos); 
    //Serial.print(':'); hexdump(response_buf, i);
    Serial.println();

}

void loop() {
    cmd.readSerial();
    //getDiags(0x10);
}

/* vim: set filetype=cpp: */
