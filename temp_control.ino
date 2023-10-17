// BTB16 triac

//#define DEBUG

// #define USE_BUZZER

// #define USE_ERROR

#define USBCON 1


// include the SevenSegmentTM1637 library
#include "SevenSegmentExtended.h"

#include "digitalWriteFast.h"


#define PIN_CLK 10 // define CLK pin (any digital pin)
#define PIN_DIO 11 // define DIO pin (any digital pin)

#define S0 4
#define S1 6
#define S2 7
#define S3 8
#define S4 9
#define S5 12

#define TRIAC 3
#define ZERO_CROSSING 2

#define TEMPERATURE_SENSOR A1

#define FAN_BUZZER 5

#define CYCLE_CORRECTION 0

#define FIRE_LENGTH 1000 // Fire length in us

#define SECOND 1000 // second length in milliseconds

const uint8_t switch_pins[] = {S0, S1, S2, S3, S4, S5};
int previous_switch_states[] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
const int all_high_switch_states[] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
SevenSegmentExtended display(PIN_CLK, PIN_DIO);

const int wattage_delay_lookup[] = {8645, 8069, 7615, 7223, 6868, 6538, 6225, 5924, 5631, 5343, 5059, 4774, 4488, 4198, 3901, 3593, 3270, 2927, 2552, 2128, 1615, 866};

int current_wattage = 800;
int firing_delay = 5924;

unsigned long zero_time = 0;
volatile bool to_fire = false;

enum power_states
{
  OFF,
  ARMED,
  ON_WATTAGE,
  TIMER_SET,
  TIMER_ON,
  ERROR
};

power_states current_power_state = OFF;
power_states previous_power_state = OFF;

#ifdef USE_ERROR
enum error_states {
  NO_ERROR,
  NO_TEMPERATURE_SENSOR
};

error_states current_error_state = NO_ERROR;
#endif

int timer_minutes = 0;
int timer_seconds = 0;
unsigned long timer_previous_millis = 0;


unsigned long last_tone = 0;

unsigned long last_switch_pressed = 0;

int fan_state = HIGH;

// run setup code
void setup()
{
  pinMode(FAN_BUZZER, OUTPUT);
  
  display.begin();           // initializes the display
  display.setBacklight(100); // set the brightness to 100 %
  display.clear();
  display.print("NIC");

  for (int i = 0; i < 256; i++) {
    analogWrite(FAN_BUZZER, i);
    delay(10);
  }

  display.print("----");

  delay(1000);

#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("init");
#endif
  for (uint8_t x : switch_pins)
  {
    pinMode(x, INPUT_PULLUP);
  }
  int temp_switch_states[] = {digitalReadFast(S0), digitalReadFast(S1), digitalReadFast(S2), digitalReadFast(S3), digitalReadFast(S4), digitalReadFast(S5)};
  memcpy(previous_switch_states, temp_switch_states, sizeof(previous_switch_states));
  pinMode(ZERO_CROSSING, INPUT_PULLUP);
  pinMode(TRIAC, OUTPUT);
  digitalWriteFast(TRIAC, HIGH);

  #ifdef __AVR_ATmega328P__
  attachInterrupt(digitalPinToInterrupt(ZERO_CROSSING), on_zero_crossing, RISING);
  #elif defined(CH32V20x)
  attachInterrupt(ZERO_CROSSING, GPIO_Mode_IPU, on_zero_crossing, EXTI_Mode_Interrupt, EXTI_Trigger_Rising);
  #endif


  digitalWriteFast(FAN_BUZZER, fan_state);
};

// run loop (forever)
void loop()
{
  if (to_fire && (current_power_state == ON_WATTAGE || current_power_state == TIMER_ON))
  {
    delayMicroseconds(firing_delay);
    digitalWriteFast(TRIAC, LOW);
    delayMicroseconds(FIRE_LENGTH);
    digitalWriteFast(TRIAC, HIGH);
    delayMicroseconds(10000 - CYCLE_CORRECTION - FIRE_LENGTH);
    digitalWriteFast(TRIAC, LOW);
    delayMicroseconds(FIRE_LENGTH);
    digitalWriteFast(TRIAC, HIGH);
    to_fire = false;
  }

#ifdef USE_ERROR  
  int temp = analogRead(TEMPERATURE_SENSOR);
  if (temp > 800) {
    current_power_state = ERROR;
    current_error_state = NO_TEMPERATURE_SENSOR;
  }
#endif

  int current_switch_states[] = {digitalReadFast(S0), digitalReadFast(S1), digitalReadFast(S2), digitalReadFast(S3), digitalReadFast(S4), digitalReadFast(S5)};
  

#ifdef USE_BUZZER
  if (millis() > last_tone)
  {
    digitalWriteFast(FAN_BUZZER, fan_state);
  }
#endif

  if (current_power_state == TIMER_ON)
  {
    digitalWriteFast(TRIAC, HIGH);
    unsigned long current_millis = millis();
    if (current_millis - timer_previous_millis >= SECOND)
    {
      timer_previous_millis = current_millis;
      timer_seconds -= 1;
      if (timer_seconds < 0)
      {
        timer_minutes -= 1;
        if (timer_minutes < 0)
        {
          timer_minutes = 0;
          timer_seconds = 0;
          current_power_state = OFF;
#ifdef USE_BUZZER
          tone(FAN_BUZZER, 1000, 3000);
#endif
          last_tone = millis() + 3000;
#ifdef DEBUG
          Serial.println("Timer expired");
#endif
          goto timer_finished;
        }
        else
        {
          timer_seconds = 59;
        }
      }
      showTime();
#ifdef DEBUG
      int temp = analogRead(TEMPERATURE_SENSOR);
      display.printNumber(temp);
      Serial.print(timer_minutes);
      Serial.print(":");
      Serial.println(timer_seconds);
#endif

    }
  }
  

  if (memcmp(current_switch_states, previous_switch_states, sizeof(current_switch_states)) == 0)
  {
    return;
  }


  if (millis() - last_switch_pressed < 250)
  {
    return;
  }
  
  if (memcmp(current_switch_states, all_high_switch_states, sizeof(current_switch_states)) == 0)
  {
    goto copy_switches;
  }

  digitalWriteFast(TRIAC, HIGH);

  last_switch_pressed = millis();

#ifdef DEBUG
  Serial.print(current_power_state);
  Serial.print(" Switches changed ");
  Serial.print(current_switch_states[0]);
  Serial.print(current_switch_states[1]);
  Serial.print(current_switch_states[2]);
  Serial.print(current_switch_states[3]);
  Serial.print(current_switch_states[4]);
  Serial.print(current_switch_states[5]);
  Serial.print(" ");
  Serial.println();
#endif

  #ifdef __AVR_ATmega328P__
  #ifdef USE_BUZZER
  tone(FAN_BUZZER, 1000, 100);
  last_tone = millis() + 100;
  #endif
  #endif

  switch (current_power_state)
  {
  case ON_WATTAGE:
    if (current_switch_states[5] == LOW)
    {
      current_power_state = OFF;
      break;
    }

    if (current_switch_states[2] == LOW)
    {
      current_power_state = TIMER_SET;
      break;
    }

    if (current_switch_states[1] == LOW)
    {
      current_wattage -= 200;
      if (current_wattage < 400)
      {
        current_wattage = 400;
      }
    }

    if (current_switch_states[4] == LOW)
    {
      current_wattage += 200;
      if (current_wattage > 2000)
      {
        current_wattage = 2000;
      }
    }
    firing_delay = wattage_to_delay(current_wattage);
    display.printNumber(current_wattage);

    break;

  case TIMER_SET:
    if (current_switch_states[2] == LOW)
    {
      current_power_state = TIMER_ON;
    }
    if (current_switch_states[5] == LOW)
    {
      current_power_state = OFF;
    }

    if (current_switch_states[1] == LOW)
    {
      timer_minutes -= 1;
      if (timer_minutes < 0)
      {
        timer_minutes = 0;
      }
    }
    if (current_switch_states[4] == LOW)
    {
      timer_minutes += 1;
      if (timer_minutes > 99)
      {
        timer_minutes = 99;
      }
    }
    showTime();
    break;

  case TIMER_ON:
    if (current_switch_states[2] == LOW)
    {
      current_power_state = TIMER_SET;
    }
    if (current_switch_states[5] == LOW)
    {
      current_power_state = OFF;
    }

    break;

  case ARMED:
    if (current_switch_states[3] == LOW)
    {
      current_power_state = ON_WATTAGE;
      current_wattage = 800;
      firing_delay = wattage_to_delay(current_wattage);
    }
    if (current_switch_states[5] == LOW)
    {
      current_power_state = OFF;
    }
    break;

  case OFF:
    digitalWriteFast(TRIAC, HIGH);
    if (current_switch_states[5] == LOW)
    {
      current_power_state = ARMED;
    }
    break;

  
  case ERROR:
    digitalWriteFast(TRIAC, HIGH);
    break;

  }

timer_finished:

  if (current_power_state != previous_power_state)
  {
#ifdef DEBUG
    Serial.print("Power state changed to ");
    Serial.println(current_power_state);
#endif
    display.clear();
    switch (current_power_state)
    {
    case OFF:
      display.print("----");
      break;

    case ARMED:
      display.print("ON");
      break;

    case ON_WATTAGE:
      display.printNumber(current_wattage);
      to_fire = true;
      break;

    case TIMER_ON:
      timer_previous_millis = millis();
      showTime();
      break;

    case TIMER_SET:
      display.print("SET");
      break;

#ifdef USE_ERROR
    case ERROR:
      fan_state = LOW;
      switch (current_error_state) {
        case NO_TEMPERATURE_SENSOR:
          display.print("ERROR: NO TEMP SENSOR");
          break;
      }
      break;
#endif
    }
    previous_power_state = current_power_state;
  }

copy_switches:

  memcpy(previous_switch_states, current_switch_states, sizeof(current_switch_states));
}

void on_zero_crossing()
{
  to_fire = true;
}

void showTime()
{
  display.printTime(timer_minutes, timer_seconds, false);
}

int wattage_to_delay(int wattage)
{
  return wattage_delay_lookup[wattage / 100 - 1];
}