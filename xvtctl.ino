#include <SPI.h>
#include <ADF4350.h>

#define COM_PIN 10 // sets pin 10 to be the slave-select pin for PLL
ADF4350 PLL(COM_PIN); // declares object PLL of type ADF4350. Will initialize it below.

const int LED         = LED_BUILTIN;

const int PIN_PTT     = 9; // pin that is polled to check rig PTT status
const int PIN_BAND    = A8; // analog input for band data, allow tx only on correct IF band 
const int PIN_INHIBIT = 7; // this one inhibits rig RF output

const int PIN_TXON    = 2; // high while controller is in tx mode
const int PIN_TRANS_RX = 3; // high while transitioning to rx
const int PIN_TRANS_TX = 4; // high while transitioning to tx

const int LEVEL_PTT_ACTIVE = LOW;

const int LEVEL_INHIBIT = HIGH;
const int LEVEL_TXON = HIGH;
const int LEVEL_TRANS_TX = HIGH;
const int LEVEL_TRANS_RX = HIGH;

const int seq_delay_ms = 200;

bool debug = true;

String input_string = "";         // a string to hold incoming data
boolean string_complete = false;  // whether the string is complete

bool tx = !LEVEL_TXON; // set this to HIGH to trigger PTT sequence. Set to LOW to trigger reverse sequence.
int tx_sequence = 2; // initialize at 2 to get TRANS_RX to trigger once on startup.

int freq = 1970;

unsigned long tx_sequence_started = 0; // used to track ptt sequence progress

bool ptt = !LEVEL_PTT_ACTIVE; // used to track ptt level

void setup() {
  pinMode(LED,         OUTPUT);
  pinMode(PIN_PTT,     INPUT_PULLUP);
  pinMode(PIN_INHIBIT, OUTPUT);
  pinMode(PIN_TXON,    OUTPUT);
  pinMode(PIN_TRANS_TX, OUTPUT);
  pinMode(PIN_TRANS_RX, OUTPUT);
 
  Serial.begin(9600);
  input_string.reserve(200);
  delay(1000);
  serial_header();

  digitalWrite(PIN_INHIBIT, LEVEL_INHIBIT);
  digitalWrite(PIN_TXON, !LEVEL_TXON);
  digitalWrite(PIN_TRANS_TX, !LEVEL_TRANS_TX);
  digitalWrite(PIN_TRANS_RX, !LEVEL_TRANS_RX);

  Serial.println("SPI init...");
  SPI.begin();          // for communicating with DDS/PLLs
  SPI.setClockDivider(4);
  SPI.setDataMode(SPI_MODE0);
  delay(500); // give it a sec to warm up
  
  Serial.println("PLL init...");
  PLL.initialize(freq, 10); // initialize the PLL to output 400 Mhz, using an
                          // onboard reference of 10Mhz

  Serial.println("Ready.");
}

void loop() {
  handle_serial_input();
  if (string_complete) {
    handle_input_string();
    string_complete = false;
    input_string = "";
    serial_prompt();
  }

  handle_ptt();
  handle_tx_sequence();

  // Idle a bit...
  delay(10);
}

void serial_header() {
  Serial.println("LA1FTA XvtCtl version 0.0.0");
}

void serial_prompt() {
  Serial.print("# ");  
}

void handle_serial_input() {
  if (Serial.available() > 0) {
    // get the new byte:
    char inChar = (char)Serial.read();
    
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n' || inChar == '\r') {
      // Both Carriage Return and Linefeed characters trig command parsing.
      // If someone uses both, the second will just be parsed as an empty command and ignored.
      // This ensures that all "client" configurations work
      string_complete = true;
    } else {
      // add it to the input_string:
      input_string += inChar;
    }
  }
}

void handle_input_string() {
  Serial.println(input_string); 

  input_string.toUpperCase();
  
  if(input_string.startsWith("FREQ ")) {
    handle_cmd_freq(input_string);
  } else if (input_string.startsWith("HELP")) {
    handle_cmd_help(input_string);
  } else if (input_string.startsWith("DEBUG ")) {
    handle_cmd_debug(input_string);
  } 
}

void handle_ptt() {
  bool ptt_read;
  ptt_read = digitalRead(PIN_PTT);
  if(ptt_read != ptt) {
    // PTT level changed
    ptt = ptt_read;
    if(ptt_read == LEVEL_PTT_ACTIVE) { // pin gets pulled to LOW on TX
      // XXX: Read band data, and verify TRX is on the correct band before allowing tx.
      Serial.println("PTT ON");
      set_tx(true);
    } else {
      Serial.println("PTT OFF");
      set_tx(false);
    }
  }
}

void handle_cmd_freq(String input_string) {
  int freq = input_string.substring(input_string.lastIndexOf(" ")).toInt();
  if((freq < 1870) || (freq > 2000)) {
    Serial.println("Frequency out of range...");
    return;
  }
  Serial.print("Set LO = ");
  Serial.println(freq);
}

void handle_cmd_help(String input_string) {
  serial_header();
  Serial.println("\
  Available commands:\
  FREQ <1870-2000>\
  HELP\
  DEBUG <0,1>\
  ");
}

void handle_cmd_debug(String input_string) {
  if(input_string.endsWith("1")) {
    debug = true;
  } else {
    debug = false;
  }
}

void set_tx(bool level) {
  tx = level;
  if(debug) {
    Serial.print("tx ");
    Serial.println(tx);
  }
  tx_sequence_started = millis() - seq_delay_ms;
}

void handle_tx_sequence() {
  // This function is run every loop iteration.
  // It's purpose is to see if there's some pin switching that needs to be done.

  // Return early if it's not time yet...
  if((tx_sequence_started + seq_delay_ms) > millis()) return;
  //Serial.print(".");

  // And if we're not in a sequence...
  //Serial.println(tx_sequence);
  if((ptt == LEVEL_PTT_ACTIVE && tx_sequence >= 2) || (ptt == !LEVEL_PTT_ACTIVE && tx_sequence <= 0)) return;
  //Serial.print("O");

  tx_sequence_started = millis();  

  // Sequencing
  if(ptt == LEVEL_PTT_ACTIVE) { // Going up
    tx_sequence ++;
    switch(tx_sequence) {
      
      case 1:
        // Set TX and TRANS_TX pin
        if(debug) Serial.println("--trans tx");
        digitalWrite(PIN_TXON, LEVEL_TXON);
        digitalWrite(PIN_TRANS_TX, LEVEL_TRANS_TX);
        // In case sequence reversed while running, make sure to unset the opposite transition pin
        digitalWrite(PIN_TRANS_RX, !LEVEL_TRANS_RX);
        break;
      case 2:
        // Reset TRANS_TX and lower INHIBIT
        if(debug) Serial.println("--txon");
        if(debug) Serial.println("--end trans tx");
        if(debug) Serial.println("--inhibit off");
        digitalWrite(PIN_TRANS_TX, !LEVEL_TRANS_TX);
        digitalWrite(PIN_INHIBIT, !LEVEL_INHIBIT); // Allow transceiver to send RF power
        break;
    }
  } else if(ptt != LEVEL_PTT_ACTIVE) { // Going down
    tx_sequence --;
    switch(tx_sequence) {
      case 0:
        digitalWrite(PIN_TRANS_RX, !LEVEL_TRANS_RX);
        if(debug) Serial.println("--end trans rx");
        break;
      case 1:
        // Unset TX and set TRANS_RX pin
        if(debug) Serial.println("--txoff");
        if(debug) Serial.println("--trans rx");
        if(debug) Serial.println("--inhibit on");
        digitalWrite(PIN_TXON, !LEVEL_TXON);
        digitalWrite(PIN_TRANS_RX, LEVEL_TRANS_RX);
        // In case sequence reversed while running, make sure to unset the opposite transition pin
        digitalWrite(PIN_TRANS_TX, !LEVEL_TRANS_TX);
        digitalWrite(PIN_INHIBIT, LEVEL_INHIBIT); // Allow transceiver to send RF power
        break;
    }
  }
  if(debug) {
    Serial.print("tx_sequence ");
    Serial.println(tx_sequence);
  }
}
