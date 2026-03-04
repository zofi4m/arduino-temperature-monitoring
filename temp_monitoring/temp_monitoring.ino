#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
 
#define ONE_WIRE_BUS A1

#define RED_BUTTON 2

#define LED_RED 6
#define LED_GREEN 5
#define LED_BLUE 3

#define ENC_A A2
#define ENC_B A3

LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD address 0x27.
//-----SENSOR SETUP---------
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress inAddr, outAddr;
//--temperature--
const bool TIN_ID = 0;
const bool TOUT_ID = 1;
float tempIN;
float tempOUT;
float outMin = 1000;
float outMax = -1000;
//--LED comfort indicator---


//--encoder--
#define ENC_DEBOUNCE 100
volatile int encoderPos = 0;
int lastEncoder = 0;
byte currentStateA;
unsigned long lastInterruptTime = 0;

//----menu----
enum UIState {PAGE_CURRENT, PAGE_MINMAX, UI_OPTIONS}; //UI_OPTIONS stores number of options
byte currUIState = PAGE_CURRENT;
byte degreeChar[8] = {
  B00110,
  B01001,
  B01001,
  B00110,
  B00000,
  B00000,
  B00000,
  B00000
};

/*
temp IN:   25,4°C
temp OUT:  25,4°C
MAX temp:  25,4°C
MIN temp:  25,4°C
*/

struct DebouncedButton  //to keep button related variables in one place
{
  byte pin;
  byte state = HIGH;
  unsigned long lastChange = 0;
};
//--buttons--
DebouncedButton greenBtn;
DebouncedButton redBtn;

void setupPCI(){
  PCICR |=  (1 << PCIE1);                                                
  PCMSK1 |= (1 << PCINT10);
}

void setupInputs() {
  pinMode(RED_BUTTON, INPUT_PULLUP);
  redBtn.pin = RED_BUTTON;

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  setupPCI();
}

void setupSensors() {
  sensors.begin();
  sensors.setResolution(12);
}

void setupLCD() {
  lcd.init();
  lcd.clear();
  lcd.createChar(0, degreeChar);
  lcd.backlight();
}

void setupLED() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
}

int readEncoder() {
  // read volatile encoder state
  noInterrupts();
  int currentPos = encoderPos;
  interrupts();
  return currentPos;
}

bool debounceRead(DebouncedButton &btn, unsigned long debounceDelay = 30) {
  int reading = digitalRead(btn.pin);
  unsigned long now = millis();

  if (reading != btn.state) 
  {
    // actual state change —> reset timer
    if (now - btn.lastChange > debounceDelay) 
    {
      btn.state = reading;
      btn.lastChange = now;
      return true; // stable change detected
    }
  } else //if (reading != digitalRead(btn.pin)) 
  {
    btn.lastChange = now; // reset timer when stable or bouncing  
  }
  return false; // no stable change yet
}

void handleEncoder(int newPos) {
  int delta = newPos - lastEncoder;
  
  int state = currUIState + delta;
  if (state < 0) state += UI_OPTIONS;
  if (state >= UI_OPTIONS) state -= UI_OPTIONS;
  currUIState = state;
  
  lastEncoder = newPos;
}

void handleButtons() {
  bool redChanged = debounceRead(redBtn);
  if (redChanged && redBtn.state == LOW) resetMinMax();
  //add green for toggling LED?
}

void updateLEDColor(float temperature) {
  static byte lastState = 4;
  
  bool red = LOW, green = LOW, blue = LOW;
  byte state;
  if (temperature >= 18.0 && temperature <= 24.0) { // COMFORTABLE
    state = 0;
    green = HIGH;
  } else if ( temperature > 24.0 ) {  // HOT
    state = 1;
    red = HIGH;
  } else {  // COLD
    state = 2;
    blue = HIGH;
  }

  if (lastState == state) return;

  digitalWrite(LED_RED, red);
  digitalWrite(LED_GREEN, green);
  digitalWrite(LED_BLUE, blue);
}

void handleSensors() {
  // request interval and time needed for the sensor to convert to 12 bits
  static const unsigned long REQ_INTERVAL = 1000, CONV_TIME = 750;
  static unsigned long last_request = 0, last_conversion = 0;
  unsigned long now = millis();

  if (now - last_request > REQ_INTERVAL){
    sensors.requestTemperatures();
    last_request = now;
    last_conversion = now + CONV_TIME;
  }

  if (now > last_conversion) {
    updateTemperature();
  }
  
}

void updateTemperature() {
  tempIN = sensors.getTempC(inAddr);
  tempOUT = sensors.getTempC(outAddr);
  if (tempOUT != DEVICE_DISCONNECTED_C) {
    if (tempOUT < outMin) outMin = tempOUT;
    if (tempOUT > outMax) outMax = tempOUT;
  }
  updateLEDColor(tempOUT);
}

void resetMinMax() {
  outMax = -9999; outMin = 9999;
  if (tempOUT != DEVICE_DISCONNECTED_C) {
    if (tempOUT < outMin) outMin = tempOUT;
    if (tempOUT > outMax) outMax = tempOUT;
  }
}

void drawUI() {
  static byte lastState = 255;
  bool UIStateChanged = currUIState != lastState;

  if (UIStateChanged) {
    drawLabels();     // draw layout once
    lastState = currUIState;
  }
  drawTempValues(UIStateChanged);
}

void drawLabels() {
  lcd.clear();
  switch (currUIState) {
    case PAGE_CURRENT:
      printLabel(0, "Temp IN:");
      printLabel(1, "Temp OUT:");
      break;
    case PAGE_MINMAX:
      printLabel(0, "T MIN:");
      printLabel(1, "T MAX:");
      break;
  }
}

void drawTempValues(const bool UIStateChanged) {
  static float lastIN = 9999;
  static float lastOUT = 9999;
  static float lastMin = 9999;
  static float lastMax = -9999;

  switch (currUIState) {
    case PAGE_CURRENT:
      if (tempIN != lastIN || UIStateChanged) {
        printTemp(0, tempIN);
        lastIN = tempIN;
      }
      if (tempOUT != lastOUT || UIStateChanged) {
        printTemp(1, tempOUT);
        lastOUT = tempOUT;
      }
      break;
    case PAGE_MINMAX:
      if (outMin != lastMin || UIStateChanged) {
        printTemp(0, outMin);
        lastMin = outMin;
      }
      if (outMax != lastMax || UIStateChanged) {
        printTemp(1, outMax);
        lastMax = outMax;
      }
      break;
  }
}

void printTemp(byte row, float value) {
    lcd.setCursor(9, row);     
    lcd.print("      ");     // clear 6 columns
    byte col = 13 - getFloatWidth(value); //leaving 2 places for unit
    lcd.setCursor(col, row);
    lcd.print(value, 1);
    lcd.write(0);     // degree symbol
    lcd.print("C");
}

byte getFloatWidth(float number) {
  //assuming 1 digit precision
  byte width; 
  if (number > 9.9) width = 3; // ex. 10.2 
  else if (number < -9.9) width = 4; // ex -20.3
  else width = 2; // ex. 3.2
  return width;
}

void printLabel(byte row, const char* text) {
  lcd.setCursor(0, row);
  lcd.print(text);
}

void updatePlot(char sep = ',') {
  static const unsigned long PLOT_INTERVAL = 10000; //10 seconds
  static unsigned long last_plot = 0;
  
  unsigned long now = millis();
  if (now - last_plot > PLOT_INTERVAL) {
    Serial.print(tempIN);
    Serial.print(sep);
    Serial.print(tempOUT);
    Serial.print(sep);
    Serial.print(outMin);
    Serial.print(sep);
    Serial.println(outMax);
    last_plot = now;
  }
}

void drawAddress(const DeviceAddress &address, byte id) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Address at id: ");
  lcd.print(id);
  lcd.setCursor(0,1);

  for(byte i = 0; i < 8; i++)
  {
    if(address[i] < 0x10) lcd.print("0");
    lcd.print(address[i], HEX);
  }
}

void searchDeviceAdresses() {
  oneWire.reset_search();
  DeviceAddress address;
  byte deviceID;
  while (oneWire.search(address))
  {
    if (OneWire::crc8(address, 7) != address[7]) {
      Serial.println("Incorrect OneWire address!!!");
      break;
    }
    drawAddress(address, deviceID);
    if (deviceID == 0) memcpy(inAddr, address, 8);
    if (deviceID == 1) memcpy(outAddr, address, 8);
    
    deviceID++;
    delay(2000);
  }
  oneWire.reset_search();
}

void setup()
{
  Serial.begin(9600);
  setupLED();
  setupLCD();
  setupInputs();
  setupSensors();

  sensors.requestTemperatures();
  searchDeviceAdresses();
  updateTemperature();
}
 
/*
 * Main function, get and show the temperature
 */
void loop()
{ 
  int newPos = readEncoder();
  handleEncoder(newPos);
  handleButtons();
  handleSensors();
  drawUI();
  updatePlot();
}

// pin change interrupt, based on lab example
ISR(PCINT1_vect){  
  currentStateA = digitalRead(ENC_A);
  if(currentStateA != LOW)
    return;

  noInterrupts();
  unsigned long interruptTime = millis();

  if(interruptTime - lastInterruptTime > ENC_DEBOUNCE){
    if(digitalRead(ENC_B) == LOW)
      encoderPos--;
    else 
      encoderPos++;

    lastInterruptTime = interruptTime;
  }
  interrupts();
}