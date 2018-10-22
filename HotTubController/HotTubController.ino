#include <bitswap.h>
#include <chipsets.h>
#include <color.h>
#include <colorpalettes.h>
#include <colorutils.h>
#include <controller.h>
#include <cpp_compat.h>
#include <dmx.h>
#include <FastLED.h>
#include <fastled_config.h>
#include <fastled_delay.h>
#include <fastled_progmem.h>
#include <fastpin.h>
#include <fastspi.h>
#include <fastspi_bitbang.h>
#include <fastspi_dma.h>
#include <fastspi_nop.h>
#include <fastspi_ref.h>
#include <fastspi_types.h>
#include <hsv2rgb.h>
#include <led_sysdefs.h>
#include <lib8tion.h>
#include <noise.h>
#include <pixelset.h>
#include <pixeltypes.h>
#include <platforms.h>
#include <power_mgt.h>

#include <DallasTemperature.h>

#include <OneWire.h> 
#include <DallasTemperature.h>

#include <LiquidCrystal.h>
#include <TimerOne.h>
#include <PID_v1.h>

#define NUM_LEDS 1
#define DATA_PIN A3
CRGB leds[NUM_LEDS];

// Ideal starting temp. Can be changed as we go (eg: with buttons).
float setTemperature = 20.00;
int temperatureReading;

// Pump control - D2, D3
int radiatorPumpControl = 2;
bool isRadiatorPumpOn = true; // default
int peltierAndPumpControl = 3;
bool isPeltierRunning = false;

// Temp sensors (plural) - D4
int tempSpiPin = 4;

// Push buttons
int pushButton0 = A5;
int pushButton1 = A6;
int pushButton2 = A7;

//originally using an H-Bridge, to allow
//us to "steal" heat from the hot tub as necessary
//int pwmPin = 12;
//int dirPin = 13;

OneWire oneWire(tempSpiPin);
DallasTemperature sensors(&oneWire);
// Hard wired sensor addresses
DeviceAddress hotSide = { 0x28, 0xFF, 0x5C, 0xCC, 0x61, 0x16, 0x03, 0x7D };
DeviceAddress oilTemp   = { 0x28, 0xFF, 0xC2, 0x51, 0x61, 0x16, 0x04, 0x97 };
DeviceAddress coldSide = { 0x28, 0xFF, 0xAE, 0xFE, 0x61, 0x16, 0x03, 0x17 };

                // Adaptive Node Pins
                //B2 to 4, B1 to 5, D6 to 6, 
                //A0 to 11, A1 to 12, A2 to 13, A4 to 14
LiquidCrystal lcd(10, 9, 6, A0, A1, A2, A4);

int PrintLoopCounter = 0;

void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

float GetDeviceTemp(DeviceAddress deviceAddress)
{
  return sensors.getTempC(deviceAddress);
}

void setup() {
  pinMode(radiatorPumpControl, OUTPUT);
  pinMode(peltierAndPumpControl, OUTPUT);
  digitalWrite(peltierAndPumpControl, LOW);
  digitalWrite(radiatorPumpControl, HIGH);
  
  FastLED.addLeds<WS2812, DATA_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
  leds[0] = CRGB::Red;
  FastLED.show();
  
  Serial.begin(9600);
  Serial.print("Hello. Initializing.");

  // 16x2
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Hot Tub Peltier");
  lcd.setCursor(0,1);
  lcd.print("Controller 0.01");

  analogReference(INTERNAL);

  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  // Needed if sensors are changed/added
  //if (!sensors.getAddress(hotSide, 0)) Serial.println("Unable to find address for Device 0");
  //if (!sensors.getAddress(oilTemp, 1)) Serial.println("Unable to find address for Device 1");
  //if (!sensors.getAddress(coldSide, 2)) Serial.println("Unable to find address for Device 2");

  sensors.setResolution(hotSide, 12);
  sensors.setResolution(oilTemp, 12);
  sensors.setResolution(coldSide, 12);

  // show the addresses we found on the bus
  Serial.print("Hot Side Sensor Address: ");
  printAddress(hotSide);
  Serial.println();

  Serial.print("Oil Sensor Address: ");
  printAddress(oilTemp);
  Serial.println();

  Serial.print("Cold Side Sensor Address: ");
  printAddress(coldSide);
  Serial.println();

  
}

int ledDisplay = 0;
void blinkledloop(){ 
  if (ledDisplay++ < 50)
  {
    // RED -> Green, GREEN -> Red, Blue...sigh.
    leds[0].red   = 100;
    leds[0].green = 0;
    leds[0].blue  = 0;
  }
  else
  {
    leds[0] = CRGB::Black;
  }

  if (ledDisplay > 100)
  {
    ledDisplay = 0;
  }
  
  FastLED.show();
}

// Menu States:
//  0-None
//  1-???
//  2-???
int CurrentMenuUI = 0;

void loop() {
  blinkledloop();

  if (PrintLoopCounter++ >= 500) {
    sensors.requestTemperatures();

    float hT = GetDeviceTemp(hotSide);
    float cT = GetDeviceTemp(coldSide);
    float oT = GetDeviceTemp(oilTemp);

    Serial.print("Hot temp: ");
    Serial.print(hT);
    Serial.print(" 'C ");
    Serial.print("Cold temp: ");
    Serial.print(cT);
    Serial.print(" 'C ");
    Serial.print("Oil temp: ");
    Serial.print(oT);
    Serial.println(" 'C");
                
    PrintLoopCounter = 0;

    // TODO:
    // Self diagnostic by monitoring current (total current of system, deducing actual
    // hardware usage via math (pumps current usage is consistent/identifiable)
    
    if (oT > 60.00)
    {
      Serial.println("Critical heat exchanger temp; shutting down.");
      // This turns OFF the peltier (and pumps)
      digitalWrite(peltierAndPumpControl, LOW);
      isPeltierRunning = false;
      // This turns ON the radiator pump (as it is default ON when low)
      digitalWrite(radiatorPumpControl, LOW);
      isRadiatorPumpOn = true;
    }

    if (hT < 42 && !isPeltierRunning)
    {
      Serial.println("Turninng on peltier");
      digitalWrite(peltierAndPumpControl, HIGH);
      isPeltierRunning = true;
    }

    // This should be around 42-44 depending on calibration
    if (hT >= 46  && isPeltierRunning)
    {
      Serial.println("Turninng off peltier");
      digitalWrite(peltierAndPumpControl, LOW);
      isPeltierRunning = false;
      
      //digitalWrite(radiatorPumpControl, LOW);
      //isRadiatorPumpOn = true;
    }

    if (cT >= 40)
    {
      if (!isRadiatorPumpOn)
      {
        Serial.println("Running a bit hot on cold side, turning on radiator.");
      }
        digitalWrite(radiatorPumpControl, LOW);
        isRadiatorPumpOn = true;
    }
    
    if (cT > 21 && cT < 36)
    {
        // Overriden if extraction of heat from room is necessary
        if (isRadiatorPumpOn)
        {
          Serial.println("Cool enough to shut off radiator.");
        }
        digitalWrite(radiatorPumpControl, HIGH);
        isRadiatorPumpOn = false;
    }

    if (false && cT < 21)
    {
      if (!isRadiatorPumpOn)
      {
        Serial.println("Running a bit on the cold side, turning on radiator.");
      }
        digitalWrite(radiatorPumpControl, LOW);
        isRadiatorPumpOn = true;
    }
    
    lcd.clear();

    // First row
    lcd.print("H: ");
    lcd.print(hT);
    lcd.print("(");
    lcd.print(setTemperature);
    lcd.print(")S: ");
    lcd.print(cT);
    lcd.print(" O: ");
    lcd.print(oT);

    //Second row - menu
    lcd.setCursor(0,1);
    switch(CurrentMenuUI)
    {
      case 0:
        lcd.print("       (M)(+)(-)");
        break;
      default:
        break;
    }
  }
}
