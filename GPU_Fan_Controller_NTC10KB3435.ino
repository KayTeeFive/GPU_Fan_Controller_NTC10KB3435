// ======================================================
// Arduino UNO Fan Controller with NTC 10K (B=3435)
// - Measures temperature using NTC thermistor
// - Controls 4-pin PC fan via PWM (0–5V logic)
// - Prints voltage, temperature and PWM % to Serial
// - Failsafe: sensor error or overheat -> fan MAX
// - Startup visual test sequence
// ======================================================

#include <avr/wdt.h>

// ---------- Pin configuration ----------
const int ntcPin = A3;          // Analog pin connected to NTC divider
const int fanPin = 9;           // PWM output pin for fan control
const int ledPin = 13;          // Status / error LED

// ---------- NTC parameters ----------
const float R0 = 10000.0;       // Resistance at 25°C (Ohms)
const float T0 = 25.0 + 273.15; // Reference temperature in Kelvin
const float B  = 3435.0;        // Beta coefficient
const float Rpd = 10000.0;      // Pull-down resistor value (Ohms)

// ---------- Fan curve definition ----------
// { temperature (°C), PWM (%) }
const float fanCurve[][2] = {
  { 39.9,  0.0  },    // Below this value -> FAN is turned OFF
  { 40.0, 10.0  },
  { 50.0, 30.0  },
  { 70.0, 60.0  },
  { 80.0,100.0  }
};

const int curvePoints = sizeof(fanCurve) / sizeof(fanCurve[0]);

// Hysteresis definitions
const float TEMP_HYSTERESIS = 0.5;                  // 0.5°C hysteresis
const float TEMP_START_SHIFT = fanCurve[1][0] + 5;  // +5 °C fin start temperature
bool fanIsOn = false;                               // Control FAN state
float stableTempC = NAN;                            // Last stabilized temperature

// ------------------------------------------------------
// Fan curve interpolation function
// ------------------------------------------------------
float getFanPWMPercent(float tempC) {

  // Below first point
  if (tempC <= fanCurve[0][0]) {
    fanIsOn = false;
    return fanCurve[0][1];
  }

  // Do not start if tempareture is not high enough
  if (fanIsOn == false && tempC <= TEMP_START_SHIFT) {
    fanIsOn = false;
    return fanCurve[0][1];
  }

  // Now Fan will be started
  fanIsOn = true;

  // Walk through curve segments
  for (int i = 0; i < curvePoints - 1; i++) {
    float t1 = fanCurve[i][0];
    float p1 = fanCurve[i][1];
    float t2 = fanCurve[i + 1][0];
    float p2 = fanCurve[i + 1][1];

    if (tempC >= t1 && tempC <= t2) {
      // Linear interpolation
      return p1 + (tempC - t1) * (p2 - p1) / (t2 - t1);
    }
  }

  // Above last point -> full speed
  return 100.0;
}

// ------------------------------------------------------
// Set PWM output
// ------------------------------------------------------
void setFanPWM(int pwmValue) {
  analogWrite(fanPin, pwmValue);
}

// ------------------------------------------------------
// Startup fan test sequence
// 100% -> 20% -> 100% -> 20%
// ------------------------------------------------------
void pwmTestSequence() {
  Serial.println("=== Fan Controller Startup Test ===");
  setFanPWM(255);
  Serial.println("FAN speed set to MAX for 2 sec...");
  delay(1000);
  wdt_reset();  // Reset WATCHDOG
  delay(1000);
  setFanPWM((int)(0.2 * 255));
  Serial.println("FAN speed set to 20% for 2 sec...");
  delay(1000);
  wdt_reset();  // Reset WATCHDOG
  delay(1000);
  setFanPWM(255);
  Serial.println("FAN speed set to MAX for 2 sec...");
  delay(1000);
  wdt_reset();  // Reset WATCHDOG
  delay(1000);
  setFanPWM((int)(0.2 * 255));
  Serial.println("FAN speed set to 20% for 2 sec...");
  delay(1000);
  wdt_reset();  // Reset WATCHDOG
  delay(1000);
  Serial.println("Fan Controller Startup Test is Finished!");
}

void setup() {
  pinMode(fanPin, OUTPUT);
  pinMode(ledPin, OUTPUT);

  // FAILSAFE
  setFanPWM(255);

  // Start WATCHDOG with 2 second poll
  wdt_enable(WDTO_2S);

  Serial.begin(9600);
  delay(1000);
 
  digitalWrite(ledPin, LOW);

  // Startup fan test sequence
  pwmTestSequence();
}

void loop() {
  wdt_reset();  // Reset WATCHDOG

  // Read voltage from NTC divider
  float voltage = analogRead(ntcPin) * (5.0 / 1023.0);

  // Calculate NTC resistance
  float Rntc = Rpd * (5.0 / voltage - 1.0);

  // Sensor fault detection
  if (voltage < 0.1 || voltage > 4.9 || Rntc <= 0 || Rntc > 100000.0) {
    setFanPWM(255);
    Serial.println("ERROR: NTC sensor failure! Fan forced to 100%");

    // Blink LED to indicate error
    for (int i = 0; i < 5; i++) {
      digitalWrite(ledPin, HIGH);
      delay(100);
      digitalWrite(ledPin, LOW);
      delay(100);
      wdt_reset();  // Reset WATCHDOG
    }

    delay(500);
    wdt_reset();  // Reset WATCHDOG
    return;
  }

  // Calculate temperature using Beta equation
  float tempK = 1.0 / ((1.0 / T0) + (1.0 / B) * log(Rntc / R0));
  float tempC = tempK - 273.15;

  // Initialize stabilized temperature on first run
  if (isnan(stableTempC)) {
    stableTempC = tempC;
  }

  // Apply temperature hysteresis
  float deltaTempC = abs(tempC - stableTempC);
  if (deltaTempC >= TEMP_HYSTERESIS) {
    stableTempC = tempC;
  }

  // Get PWM percentage using stabilized temperature
  float pwmPercent = getFanPWMPercent(stableTempC);

  // Convert percent to PWM value
  int pwmValue = (int)(pwmPercent * 255.0 / 100.0);
  pwmValue = constrain(pwmValue, 0, 255);

  setFanPWM(pwmValue);

  // Debug output
  Serial.print("Voltage: ");
  Serial.print(voltage, 2);
  Serial.print(" V | Temp (used/current/delta): ");
  Serial.print(stableTempC, 1);
  Serial.print("/");
  Serial.print(tempC, 1);
  Serial.print("/");
  Serial.print(deltaTempC, 1);
  Serial.print(" °C | PWM: ");
  Serial.print(pwmPercent, 1);
  Serial.println(" %");

  digitalWrite(ledPin, HIGH);
  delay(500);

  wdt_reset();  // Reset WATCHDOG
}