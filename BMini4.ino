#include <ESP8266WiFi.h>
#include <Servo.h>

// ═════════════════════════════════════════════════════════════════════════
// DEBUG - Comment out for production
// ═════════════════════════════════════════════════════════════════════════
// #define DEBUG_LEVEL_INFO

#if defined(DEBUG_LEVEL_INFO)
#define DEBUG_INFO(x)                                                          \
  Serial.print("[INFO] ");                                                     \
  Serial.println(x)
#define DEBUG_INFO_F(x, y)                                                     \
  Serial.print("[INFO] ");                                                     \
  Serial.print(x);                                                             \
  Serial.println(y)
#else
#define DEBUG_INFO(x)
#define DEBUG_INFO_F(x, y)
#endif

// ═════════════════════════════════════════════════════════════════════════
// PIN CONFIGURATION
// ═════════════════════════════════════════════════════════════════════════
#define D0 16
#define D1 5
#define D2 4
#define D3 0
#define D4 2
#define D5 14
#define D6 12
#define D7 13
#define D8 15
#define RX 3
#define TX 1
#define A0 17

#define RGB_PWM_OFF 1023
#define RGB_PWM_ON 0

const int PIN_MOTOR_F = D6, PIN_MOTOR_R = D2;
const int PIN_SERVO = D1, PIN_BUZZER = D5;
const int PIN_HEADLIGHTS = D4, PIN_BACKLIGHTS = D7;
const int PIN_IND_LEFT = D0, PIN_IND_RIGHT = D3;
const int PIN_RGB_RED = TX, PIN_RGB_GREEN = RX, PIN_RGB_BLUE = D8;

// ═════════════════════════════════════════════════════════════════════════
// CONSTANTS
// ═════════════════════════════════════════════════════════════════════════
const char* SSID = "BMini4", *PSK = "26032009";
const int SERVO_MIN = 45, SERVO_MAX = 135, SERVO_CENTER = 90, STEERING_THRESHOLD = 6;
const int PWM_MAX = 1023, GAS_STEP = 204, REVERSE_GAS_STEP = 1023; // was 275, temporary full speed
const int BRAKE_PULSE_MS = 200, BRAKE_LONG_MS = 1200, BLINK_INTERVAL_MS = 500;
const int TELEM_INTERVAL_MS = 3000, KEEPALIVE_INTERVAL = 1000;

// Battery (150k/33k divider)
const float BATTERY_MIN_V = 3.0, BATTERY_MAX_V = 4.2, VOLTAGE_DIVIDER = 4.545, ADC_REF = 3.3;

// Sound frequencies
const int FREQ_HORN = 2200, FREQ_CONNECT = 1800, FREQ_DISCONNECT = 1200, FREQ_CLICK = 2800;

// ═════════════════════════════════════════════════════════════════════════
// PRECOMPUTED TABLES
// ═════════════════════════════════════════════════════════════════════════
uint8_t sineTable[100];
uint8_t rainbowTable[360][3];

void buildLookupTables() {
  for (int i = 0; i < 100; i++)
    sineTable[i] = (uint8_t)(127.5 + 127.5 * sin(i * 2.0 * PI / 100.0));
  for (int h = 0; h < 360; h++) {
    int i = (int)(h / 60.0) % 6;
    float f = (h / 60.0) - i, p = 0, q = 1 - f, t = f;
    float r, g, b;
    switch (i) {
    case 0:
      r = 1;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = 1;
      b = p;
      break;
    case 2:
      r = p;
      g = 1;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = 1;
      break;
    case 4:
      r = t;
      g = p;
      b = 1;
      break;
    case 5:
      r = 1;
      g = p;
      b = q;
      break;
    }
    rainbowTable[h][0] = r * 255;
    rainbowTable[h][1] = g * 255;
    rainbowTable[h][2] = b * 255;
  }
}

// ═════════════════════════════════════════════════════════════════════════
// STATE
// ═════════════════════════════════════════════════════════════════════════
enum IndMode { IND_OFF = 0, IND_LEFT = 1, IND_RIGHT = 2 };
enum RgbAnimation {
  RGB_OFF = 0,
  RGB_SOLID,
  RGB_BREATHE,
  RGB_BLINK,
  RGB_STROBE,
  RGB_PULSE,
  RGB_RAINBOW,
  RGB_FIRE,
  RGB_SPARKLE,
  RGB_COMET,
  RGB_WAVE,
  RGB_POLICE
};
enum ConnState {
  CONN_DISCONNECTED,
  CONN_CONNECTING,
  CONN_CONNECTED,
  CONN_LOST
} connState = CONN_DISCONNECTED;

struct {
  bool forward = true, headlights = false, soundEnabled = true;
  int servo = SERVO_CENTER, gasLevel = 0;
  IndMode indMode = IND_OFF;
  bool brake = false, brakeLongTriggered = false, brakePulseActive = false;
  unsigned long brakeStart = 0, brakePulseStart = 0;
} state;

struct {
  uint8_t r = 0, g = 0, b = 0, brightness = 255, mode = 0;
  bool active = false, statusIndicator = true;
  uint32_t lastUpdate = 0, targetColor = 0;
  uint16_t animStep = 0, animSpeed = 50;
} rgb;

Servo steeringServo;
WiFiServer server(80);
WiFiClient client;

unsigned long lastClientActivity = 0, lastKeepalive = 0;
int batteryPercent = 0;

// ═════════════════════════════════════════════════════════════════════════
// BATTERY
// ═════════════════════════════════════════════════════════════════════════
int readBatteryLevel() {
  uint32_t sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(A0);
    delay(10);
  }
  float v = (sum / 10.0 / 1024.0) * ADC_REF * VOLTAGE_DIVIDER;
  int pct = map(v * 100, BATTERY_MIN_V * 100, BATTERY_MAX_V * 100, 0, 100);
  return constrain(pct, 0, 100);
}

// ═════════════════════════════════════════════════════════════════════════
// RGB
// ═════════════════════════════════════════════════════════════════════════
void setRgbColor(uint8_t r, uint8_t g, uint8_t b) {
  r =  (r * rgb.brightness) / 255;
  g = (g * rgb.brightness) / 255;
  b = (b * rgb.brightness) / 255;
  analogWrite(PIN_RGB_RED, map(r, 0, 255, RGB_PWM_OFF, RGB_PWM_ON));
  analogWrite(PIN_RGB_GREEN, map(g, 0, 255, RGB_PWM_OFF, RGB_PWM_ON));
  analogWrite(PIN_RGB_BLUE, map(b, 0, 255, RGB_PWM_OFF, RGB_PWM_ON));
  rgb.r = r;
  rgb.g = g;
  rgb.b = b;
}

void updateRgbAnimation() {
  if (rgb.statusIndicator) {
    uint32_t now = millis();
    switch (connState) {
    case CONN_CONNECTING:
      if (now - rgb.lastUpdate > 20) {
        rgb.lastUpdate = now;
        rgb.animStep = (rgb.animStep + 1) % 100;
        setRgbColor(0, 0, (255 * sineTable[rgb.animStep]) / 255);
      }
      return;
    // case CONN_CONNECTED:
    //     if(now-lastClientActivity<500)setRgbColor(0,255,0);
    //     else setRgbColor(0,0,0);
    //     return;
    case CONN_LOST:
      if (now - rgb.lastUpdate > 300) {
        rgb.lastUpdate = now;
        rgb.animStep = !rgb.animStep;
        setRgbColor(rgb.animStep ? 255 : 0, 0, 0);
      }
      return;
    default:
      break;
    }
    if (batteryPercent < 30 && batteryPercent > 0) {
      if (now - rgb.lastUpdate > 20) {
        rgb.lastUpdate = now;
        rgb.animStep = (rgb.animStep + 1) % 100;
        uint8_t br = sineTable[rgb.animStep];
        setRgbColor((255 * br) / 255, (128 * br) / 255, 0);
      }
      return;
    }
  }

  if (!rgb.active || rgb.mode == RGB_OFF) {
    setRgbColor(0, 0, 0);
    return;
  }
  uint32_t now = millis();
  uint8_t tr = (rgb.targetColor >> 16) & 0xFF,
          tg = (rgb.targetColor >> 8) & 0xFF, tb = rgb.targetColor & 0xFF;

  switch (rgb.mode) {
  case RGB_SOLID:
    setRgbColor(tr, tg, tb);
    break;
  case RGB_BREATHE:
    if (now - rgb.lastUpdate > rgb.animSpeed) {
      rgb.lastUpdate = now;
      rgb.animStep = (rgb.animStep + 1) % 100;
      uint8_t br = sineTable[rgb.animStep];
      setRgbColor((tr * br) / 255, (tg * br) / 255, (tb * br) / 255);
    }
    break;
  case RGB_BLINK:
    if (now - rgb.lastUpdate > rgb.animSpeed * 10) {
      rgb.lastUpdate = now;
      rgb.animStep = !rgb.animStep;
      if (rgb.animStep)
        setRgbColor(tr, tg, tb);
      else
        setRgbColor(0, 0, 0);
    }
    break;
  case RGB_STROBE:
    if (now - rgb.lastUpdate > rgb.animSpeed * 2) {
      rgb.lastUpdate = now;
      rgb.animStep = !rgb.animStep;
      if (rgb.animStep)
        setRgbColor(tr, tg, tb);
      else
        setRgbColor(0, 0, 0);
    }
    break;
  case RGB_PULSE:
    if (now - rgb.lastUpdate > rgb.animSpeed) {
      rgb.lastUpdate = now;
      rgb.animStep++;
      if (rgb.animStep >= 200)
        rgb.animStep = 0;
      uint8_t br = rgb.animStep < 50    ? map(rgb.animStep, 0, 50, 0, 255)
                   : rgb.animStep < 150 ? 0
                                        : map(rgb.animStep, 150, 200, 0, 255);
      setRgbColor((tr * br) / 255, (tg * br) / 255, (tb * br) / 255);
    }
    break;
  case RGB_RAINBOW:
    if (now - rgb.lastUpdate > rgb.animSpeed) {
      rgb.lastUpdate = now;
      rgb.animStep = (rgb.animStep + 1) % 360;
      setRgbColor(rainbowTable[rgb.animStep][0], rainbowTable[rgb.animStep][1],
                  rainbowTable[rgb.animStep][2]);
    }
    break;
  case RGB_FIRE:
    if (now - rgb.lastUpdate > rgb.animSpeed) {
      rgb.lastUpdate = now;
      uint8_t f1 = random(150, 255), f2 = random(50, 150);
      setRgbColor((tr * f1) / 255, (tg * f2) / 255, 0);
    }
    break;
  case RGB_SPARKLE:
    if (now - rgb.lastUpdate > rgb.animSpeed * 2) {
      rgb.lastUpdate = now;
      if (random(0, 3) == 0)
        setRgbColor(tr, tg, tb);
      else
        setRgbColor(0, 0, 0);
    }
    break;
  case RGB_COMET:
    if (now - rgb.lastUpdate > rgb.animSpeed) {
      rgb.lastUpdate = now;
      rgb.animStep = (rgb.animStep + 1) % 100;
      uint8_t br = rgb.animStep < 20 ? map(rgb.animStep, 0, 20, 0, 255)
                                     : map(rgb.animStep, 20, 100, 255, 0);
      setRgbColor((tr * br) / 255, (tg * br) / 255, (tb * br) / 255);
    }
    break;
  case RGB_WAVE:
    if (now - rgb.lastUpdate > rgb.animSpeed) {
      rgb.lastUpdate = now;
      rgb.animStep = (rgb.animStep + 2) % 100;
      uint8_t w = sineTable[rgb.animStep];
      setRgbColor((tr * w) / 255, (tg * w) / 255, (tb * w) / 255);
    }
    break;
  case RGB_POLICE:
    if (now - rgb.lastUpdate > rgb.animSpeed * 3) {
      rgb.lastUpdate = now;
      rgb.animStep = (rgb.animStep + 1) % 4;
      if (rgb.animStep < 2)
        setRgbColor(255, 0, 0);
      else
        setRgbColor(0, 0, 255);
    }
    break;
  }
}

// ═════════════════════════════════════════════════════════════════════════
// COMMANDS
// ═════════════════════════════════════════════════════════════════════════
void handleRgb(const char *a) {
  char mode[32] = {0}, col[8] = {0};
  sscanf(a, "%31s %7s", mode, col);
  if (strcmp(mode, "off") == 0) {
    rgb.active = false;
    rgb.mode = RGB_OFF;
    setRgbColor(0, 0, 0);
    return;
  }
  if (strlen(col) == 6)
    rgb.targetColor = strtol(col, NULL, 16);
  rgb.active = true;
  rgb.animStep = 0;
  if (strcmp(mode, "on") == 0 || strcmp(mode, "solid") == 0)
    rgb.mode = RGB_SOLID;
  else if (strcmp(mode, "breathe") == 0)
    rgb.mode = RGB_BREATHE;
  else if (strcmp(mode, "blink") == 0)
    rgb.mode = RGB_BLINK;
  else if (strcmp(mode, "strobe") == 0)
    rgb.mode = RGB_STROBE;
  else if (strcmp(mode, "pulse") == 0)
    rgb.mode = RGB_PULSE;
  else if (strcmp(mode, "rainbow") == 0)
    rgb.mode = RGB_RAINBOW;
  else if (strcmp(mode, "fire") == 0)
    rgb.mode = RGB_FIRE;
  else if (strcmp(mode, "sparkle") == 0)
    rgb.mode = RGB_SPARKLE;
  else if (strcmp(mode, "comet") == 0)
    rgb.mode = RGB_COMET;
  else if (strcmp(mode, "wave") == 0)
    rgb.mode = RGB_WAVE;
  else if (strcmp(mode, "police") == 0)
    rgb.mode = RGB_POLICE;
  else
    rgb.mode = RGB_SOLID;
}

void handleBrightness(const char *a) {
  int v = atoi(a);
  if (v >= 0 && v <= 255)
    // a remap cuz my leds doesn't turn on if pwn < 192
    v = map(v, 0, 255, 192, 255);
    v = map(v, 192, 255, 0, 255);
    
    rgb.brightness = v;
}
void handleSpeed(const char *a) {
  int v = atoi(a);
  if (v >= 10 && v <= 200)
    rgb.animSpeed = v;
}
void handleStatus(const char *a) {
  rgb.statusIndicator = (strcmp(a, "on") == 0);
}
void handleSound(const char *a) { state.soundEnabled = (strcmp(a, "on") == 0); }

void handleServo(const char *a) {
  int v = atoi(a);
  if (abs(v - state.servo) >= STEERING_THRESHOLD) {
    v = constrain(v, SERVO_MIN, SERVO_MAX);
    steeringServo.write(v);
    state.servo = v;
  }
}

void handleBeep(const char *a) {
  strcmp(a, "on") == 0 ? tone(PIN_BUZZER, FREQ_HORN) : noTone(PIN_BUZZER);
}

void handleHead(const char *a) {
  bool on = (strcmp(a, "on") == 0);
  digitalWrite(PIN_HEADLIGHTS, on ? HIGH : LOW);
  state.headlights = on;
  if (state.soundEnabled)
    tone(PIN_BUZZER, FREQ_CLICK, 30);
}

void handleInd(const char *a) {
  if (strcmp(a, "left") == 0)
    state.indMode = IND_LEFT;
  else if (strcmp(a, "right") == 0)
    state.indMode = IND_RIGHT;
  else {
    state.indMode = IND_OFF;
    digitalWrite(PIN_IND_LEFT, LOW);
    digitalWrite(PIN_IND_RIGHT, LOW);
  }
  if (state.soundEnabled && state.indMode != IND_OFF)
    tone(PIN_BUZZER, FREQ_CLICK, 30);
}

void handleGas(const char *a) {
  int v = atoi(a);
  if (v >= 0 && v <= 5)
    v = constrain(v+1, 0, 5); // temporary, remove this line later.
    state.gasLevel = v;
}

void setMotor(int pwm, bool dir = state.forward) {
  analogWrite(dir ? PIN_MOTOR_R : PIN_MOTOR_F, 0);
  analogWrite(dir ? PIN_MOTOR_F : PIN_MOTOR_R, pwm);
}

void updateMotor() {
  if (state.brakePulseActive)
    return;
  int pwm = state.brakeLongTriggered ? REVERSE_GAS_STEP
            : state.brake            ? 0
                                     : min(state.gasLevel * GAS_STEP, PWM_MAX);
  setMotor(pwm);
}

void handleBrake(const char *a) {
  if (strcmp(a, "on") == 0) {
    state.brake = true;
    state.brakeStart = millis();
    state.brakeLongTriggered = false;
    digitalWrite(PIN_BACKLIGHTS, HIGH);
    setRgbColor(255, 0, 0);
    if (state.gasLevel > 0) {
      setMotor(PWM_MAX, !state.forward);
      state.brakePulseActive = true;
      state.brakePulseStart = millis();
    }
    state.gasLevel = 0;
  } else {
    state.brake = false;
    state.gasLevel = 0;
    state.forward = true;
    state.brakeLongTriggered = false;
    digitalWrite(PIN_BACKLIGHTS, LOW);
    setRgbColor(0, 0, 0);
  }
}

void processCmd(char *cmd) {
  struct {
    const char *p;
    void (*h)(const char *);
  } h[] = {{"servo ", handleServo},
           {"beep ", handleBeep},
           {"head ", handleHead},
           {"ind ", handleInd},
           {"rgb ", handleRgb},
           {"brightness ", handleBrightness},
           {"speed ", handleSpeed},
           {"status ", handleStatus},
           {"sound ", handleSound},
           {"gas ", handleGas},
           {"brake ", handleBrake},
           {"ping", [](const char *) {
              if (client.connected())
                client.println("PONG");
            }}};
  for (int i = 0; i < 12; i++) {
    size_t len = strlen(h[i].p);
    if (strncmp(cmd, h[i].p, len) == 0) {
      h[i].h(cmd + len);
      lastClientActivity = millis();
      return;
    }
  }
}

// ═════════════════════════════════════════════════════════════════════════
// CONNECTION
// ═════════════════════════════════════════════════════════════════════════
void handleClient() {
  if (client && !client.connected()) {
    client.stop();
    connState = CONN_LOST;
    if (state.soundEnabled)
      tone(PIN_BUZZER, FREQ_DISCONNECT, 100);
  }
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();
    if (newClient) {
      client = newClient;
      connState = CONN_CONNECTED;
      lastClientActivity = millis();
      if (state.soundEnabled)
        tone(PIN_BUZZER, FREQ_CONNECT, 100);
    } else {
      if (connState == CONN_CONNECTED)
        connState = CONN_DISCONNECTED;
      return;
    }
  }
  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() > 0)
      processCmd((char *)line.c_str());
  }
  unsigned long now = millis();
  if (client.connected() && now - lastKeepalive > KEEPALIVE_INTERVAL) {
    client.println("OK");
    lastKeepalive = now;
  }
  if (client.connected() && now - lastClientActivity > 30000) {
    client.stop();
    connState = CONN_LOST;
  }
}

// ═════════════════════════════════════════════════════════════════════════
// ANIMATIONS
// ═════════════════════════════════════════════════════════════════════════
void handleAnimations() {
  unsigned long now = millis();
  if (state.brakePulseActive && now - state.brakePulseStart >= BRAKE_PULSE_MS) {
    state.brakePulseActive = false;
    setMotor(0);
  }
  static unsigned long lastTelem = 0;
  if (client.connected() && now - lastTelem >= TELEM_INTERVAL_MS) {
    batteryPercent = readBatteryLevel();
    client.print("TELEM:");
    client.println(batteryPercent);
    lastTelem = now;
  }
  updateMotor();
  static unsigned long lastBlink = 0;
  static bool blinkState = false;
  if (now - lastBlink >= BLINK_INTERVAL_MS) {
    blinkState = !blinkState;
    lastBlink = now;
    if (state.indMode == IND_LEFT) {
      digitalWrite(PIN_IND_LEFT, blinkState);
      digitalWrite(PIN_IND_RIGHT, LOW);
    } else if (state.indMode == IND_RIGHT) {
      digitalWrite(PIN_IND_RIGHT, blinkState);
      digitalWrite(PIN_IND_LEFT, LOW);
    }
    if (state.brakeLongTriggered) {
      digitalWrite(PIN_BACKLIGHTS, blinkState);
      setRgbColor(blinkState * 255, 0, 0);
    }
  }
  if (state.brake && !state.brakeLongTriggered &&
      now - state.brakeStart > BRAKE_LONG_MS) {
    state.brakeLongTriggered = true;
    state.forward = false;
  }
}

// ═════════════════════════════════════════════════════════════════════════
// SETUP & LOOP
// ═════════════════════════════════════════════════════════════════════════
void setup() {
#ifdef DEBUG_LEVEL_INFO
  Serial.begin(9600);
  delay(100);
#endif
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID, PSK, 1, 0, 1);
  server.begin();
  buildLookupTables();
  pinMode(PIN_MOTOR_F, OUTPUT);
  pinMode(PIN_MOTOR_R, OUTPUT);
  pinMode(PIN_HEADLIGHTS, OUTPUT);
  pinMode(PIN_BACKLIGHTS, OUTPUT);
  pinMode(PIN_IND_LEFT, OUTPUT);
  pinMode(PIN_IND_RIGHT, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RGB_RED, OUTPUT);
  pinMode(PIN_RGB_GREEN, OUTPUT);
  pinMode(PIN_RGB_BLUE, OUTPUT);
  pinMode(A0, INPUT);
  analogWrite(PIN_MOTOR_F, 0);
  analogWrite(PIN_MOTOR_R, 0);
  digitalWrite(PIN_HEADLIGHTS, LOW);
  digitalWrite(PIN_BACKLIGHTS, LOW);
  digitalWrite(PIN_IND_LEFT, LOW);
  digitalWrite(PIN_IND_RIGHT, LOW);
  noTone(PIN_BUZZER);
  setRgbColor(0, 0, 0);
  steeringServo.attach(PIN_SERVO);
  steeringServo.write(SERVO_CENTER);
  connState = CONN_CONNECTING;
  DEBUG_INFO("BMini4 Ready");
}

void loop() {
  handleClient();
  handleAnimations();
  if (!state.brake)
    updateRgbAnimation();
  yield();
}