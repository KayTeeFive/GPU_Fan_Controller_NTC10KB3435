// ======================================================
// Arduino UNO Fan Controller with NTC 10K (B=3435)
// ======================================================

#include <avr/wdt.h>

// ---------- Configuration & Constants ----------
namespace Config {
    const int NTC_PIN = A3;
    const int FAN_PIN = 9;
    const int LED_PIN = 13;

    const float R0 = 10000.0;
    const float T0 = 25.0 + 273.15;
    const float BETA = 3435.0;
    const float R_PULLDOWN = 10000.0;

    const float TEMP_HYSTERESIS = 0.5;
    const int WATCHDOG_TIMEOUT = WDTO_2S;
}

struct FanPoint {
    float tempC;
    float pwmPercent;
};

// Fan curve definition: { temperature (°C), PWM (%) }
const FanPoint FAN_CURVE[] = {
    { 39.9,  0.0  },
    { 40.0, 10.0  },
    { 50.0, 30.0  },
    { 70.0, 60.0  },
    { 80.0, 100.0 }
};

const int CURVE_POINTS = sizeof(FAN_CURVE) / sizeof(FAN_CURVE[0]);

// ---------- Global State ----------
bool g_fanIsOn = false;
float g_stableTempC = NAN;
float g_tempStartShift = 0; // Calculated at runtime based on curve

// ------------------------------------------------------
// Hardware Interface
// ------------------------------------------------------

void setFanPWM(int pwmValue) {
    // Ensure PWM stays within valid 8-bit range
    analogWrite(Config::FAN_PIN, constrain(pwmValue, 0, 255));
}

void setLed(bool state) {
    digitalWrite(Config::LED_PIN, state ? HIGH : LOW);
}

// ------------------------------------------------------
// Fan Control Logic
// ------------------------------------------------------

float getFanPWMPercent(float tempC) {
    // 1. Check if temperature is below the absolute minimum (Fan OFF)
    if (tempC <= FAN_CURVE[0].tempC) {
        g_fanIsOn = false;
        return FAN_CURVE[0].pwmPercent;
    }

    // 2. Startup threshold: prevent frequent oscillations near the start point
    if (!g_fanIsOn && tempC <= g_tempStartShift) {
        g_fanIsOn = false;
        return FAN_CURVE[0].pwmPercent;
    }

    // 3. Fan is active
    g_fanIsOn = true;

    // 4. Linear interpolation across the curve segments
    for (int i = 0; i < CURVE_POINTS - 1; i++) {
        const FanPoint& p1 = FAN_CURVE[i];
        const FanPoint& p2 = FAN_CURVE[i + 1];

        if (tempC >= p1.tempC && tempC <= p2.tempC) {
            return p1.pwmPercent + (tempC - p1.tempC) * (p2.pwmPercent - p1.pwmPercent) / (p2.tempC - p1.tempC);
        }
    }

    // 5. Above max threshold
    return 100.0;
}

// ------------------------------------------------------
// Initialization & Test Sequences
// ------------------------------------------------------

void runStartupSequence() {
    Serial.println("=== Fan Controller Startup Test ===");

    // Test sequence: 100% -> 20% -> 100% -> 20%
    for (int i = 0; i < 2; i++) {
        setFanPWM(255);
        Serial.println("FAN: MAX (1s)");
        delay(1000);
        wdt_reset();

        setFanPWM((int)(0.2 * 255));
        Serial.println("FAN: 20% (1s)");
        delay(1000);
        wdt_reset();
    }
    Serial.println("Startup Test Complete.");
}

// ------------------------------------------------------
// Main Lifecycle
// ------------------------------------------------------

void setup() {
    pinMode(Config::FAN_PIN, OUTPUT);
    pinMode(Config::LED_PIN, OUTPUT);

    // Initialize hardware to a safe state (Full Speed)
    setFanPWM(255);
    wdt_enable(Config::WATCHDOG_TIMEOUT);

    Serial.begin(9600);
    delay(1000);
    setLed(false);

    // Dynamically calculate start shift (Point 1 + 5°C offset)
    g_tempStartShift = FAN_CURVE[1].tempC + 5.0;

    runStartupSequence();
}

void loop() {
    wdt_reset();

    // 1. Read NTC divider voltage
    const float voltage = analogRead(Config::NTC_PIN) * (5.0 / 1023.0);

    // Guard against division by zero/invalid sensor reading
    if (voltage < 0.01) {
        setFanPWM(255);
        Serial.println("CRITICAL: Sensor disconnect (Low Voltage)!");
        return;
    }

    // 2. Calculate NTC resistance
    const float Rntc = Config::R_PULLDOWN * (5.0 / voltage - 1.0);

    // 3. Error Checking (Open circuit or Short circuit)
    if (voltage > 4.9 || Rntc <= 0 || Rntc > 100000.0) {
        setFanPWM(255);
        Serial.println("ERROR: NTC Sensor Failure!");

        // Error indication: Rapid LED blink
        for (int i = 0; i < 5; i++) {
            setLed(true);
            delay(100);
            setLed(false);
            delay(100);
            wdt_reset();
        }
        delay(500);
        return;
    }

    // 4. Temperature Conversion (Beta Equation)
    const float tempK = 1.0 / ((1.0 / Config::T0) + (1.0 / Config::BETA) * log(Rntc / Config::R0));
    const float tempC = tempK - 273.15;

    // 5. Apply Temperature Hysteresis
    if (isnan(g_stableTempC)) {
        g_stableTempC = tempC;
    }
    if (abs(tempC - g_stableTempC) >= Config::TEMP_HYSTERESIS) {
        g_stableTempC = tempC;
    }

    // 6. Fan Speed Calculation
    const float pwmPercent = getFanPWMPercent(g_stableTempC);
    const int pwmValue = (int)(pwmPercent * 255.0 / 100.0);
    setFanPWM(pwmValue);

    // 7. Telemetry Output
    Serial.print("V: "); Serial.print(voltage, 2);
    Serial.print("V | T_used: "); Serial.print(g_stableTempC, 1);
    Serial.print(" | T_cur: "); Serial.print(tempC, 1);
    Serial.print(" | PWM: "); Serial.print(pwmPercent, 1);
    Serial.println("%");

    setLed(true);
    delay(500);
}
