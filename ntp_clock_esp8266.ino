#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─────────────────────────────────────────
//  CONFIGURATION
// ─────────────────────────────────────────
const char* PRIMARY_SSID  = "JP-2.4G";
const char* PRIMARY_PASS  = "Prathop@1";
const char* AP_SSID       = "NTP-Clock-Setup";
const char* AP_PASS       = "12345678";

#define LCD_I2C_ADDR  0x27
#define LCD_COLS      16
#define LCD_ROWS      2
#define I2C_SDA_PIN   4
#define I2C_SCL_PIN   5
#define BUZZER_PIN    15
#define BTN_DISMISS1  14
#define BTN_DISMISS2  12
#define BTN_HOLIDAY   13

const long UTC_OFFSET_SEC = 19800;

// ─────────────────────────────────────────
//  SCHEDULE
// ─────────────────────────────────────────
struct Alarm {
  int   hour;
  int   minute;
  const char* label;
  int   beepCount;
  int   beepMs;
};

Alarm normalAlarms[] = {
  {  4,  0, "Revise",        4, 3000 },
  {  5,  0, "Write/Revise",  5,  500 },
  {  5, 30, "Do Exercise",   5,  500 },
  {  6,  0, "Time 4 School", 5,  500 },
  { 17,  0, "Start Reading", 5,  500 },
  { 18, 30, "Break",         5,  500 },
  { 19,  0, "Start Writing", 5,  500 },
  { 20, 30, "Dinner",        5,  500 },
  { 21, 30, "Time for Bed",  5,  500 },
};
int normalCount = 9;

Alarm tutionAlarms[] = {
  {  4,  0, "Revise",        4, 3000 },
  {  5,  0, "Write/Revise",  5,  500 },
  {  5, 30, "Do Exercise",   5,  500 },
  {  6,  0, "Time 4 School", 5,  500 },
  { 17,  0, "Start Reading", 5,  500 },
  { 18, 30, "Time 4 Maths",  5,  500 },
  { 20, 30, "Dinner",        5,  500 },
  { 21, 30, "Time for Bed",  5,  500 },
};
int tutionCount = 8;

Alarm saturdayAlarms[] = {
  {  4,  0, "Revise",        4, 3000 },
  {  5,  0, "Write/Revise",  5,  500 },
  {  5, 30, "Do Exercise",   5,  500 },
  {  6,  0, "Time 4 School", 5,  500 },
  { 17,  0, "Start Reading", 5,  500 },
  { 18, 30, "Break",         5,  500 },
  { 19,  0, "Start Writing", 5,  500 },
  { 20, 30, "Dinner",        5,  500 },
  { 21, 30, "Time for Bed",  5,  500 },
};
int saturdayCount = 9;

// ─────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────
bool usingAP   = false;
bool ntpSynced = false;

String newSSID = "";
String newPASS = "";
bool   credentialsReceived = false;

bool isTutionDay  = false;
bool isSatHoliday = false;
bool isHoliday    = false;

bool normalFired[9] = {false};
bool tutionFired[8] = {false};
bool satFired[9]    = {false};

bool alarm4amActive    = false;
bool alarm4amDismissed = false;
unsigned long alarm4amStart = 0;

unsigned long lastD5Press = 0, lastD6Press = 0, lastD7Press = 0;
bool lastD5State = HIGH, lastD6State = HIGH, lastD7State = HIGH;

int g_year, g_month, g_day, g_weekday;
int lastDay = -1;

unsigned long lastWiFiCheck = 0;
#define WIFI_CHECK_INTERVAL 30000

// ─────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
WiFiUDP            udp;
NTPClient          ntp(udp, "pool.ntp.org", UTC_OFFSET_SEC, 60000);
ESP8266WebServer   server(80);

const char* DAYS[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                         "Jul","Aug","Sep","Oct","Nov","Dec"};
const uint8_t DAYS_IN_MONTH[] = {31,28,31,30,31,30,31,31,30,31,30,31};

// ─────────────────────────────────────────
//  HELPERS
// ─────────────────────────────────────────
bool isLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

void epochToDate(unsigned long epoch) {
  g_weekday = (epoch / 86400 + 4) % 7;
  unsigned long days = epoch / 86400;
  int year = 1970;
  while (true) {
    unsigned long diy = isLeapYear(year) ? 366 : 365;
    if (days < diy) break;
    days -= diy; year++;
  }
  int month = 0;
  while (month < 12) {
    uint8_t dim = DAYS_IN_MONTH[month];
    if (month == 1 && isLeapYear(year)) dim = 29;
    if (days < dim) break;
    days -= dim; month++;
  }
  g_year = year; g_month = month + 1; g_day = (int)days + 1;
}

void lcdPrint(const String& row0, const String& row1) {
  char r0[17], r1[17];
  snprintf(r0, sizeof(r0), "%-16s", row0.c_str());
  snprintf(r1, sizeof(r1), "%-16s", row1.c_str());
  lcd.setCursor(0, 0); lcd.print(r0);
  lcd.setCursor(0, 1); lcd.print(r1);
}

String formatTime12hr(int h, int m, int s) {
  const char* period = (h < 12) ? "AM" : "PM";
  int h12 = h % 12; if (h12 == 0) h12 = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d %s", h12, m, s, period);
  return String(buf);
}

String formatDate() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%s %02d %s %d",
           DAYS[g_weekday], g_day, MONTHS[g_month - 1], g_year);
  return String(buf);
}

bool bothDismissPressed() {
  return (digitalRead(BTN_DISMISS1) == LOW && digitalRead(BTN_DISMISS2) == LOW);
}

bool beepCycle(int count, int durationMs, int pauseMs = 500) {
  for (int i = 0; i < count; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    int e = 0;
    while (e < durationMs) {
      if (bothDismissPressed()) { digitalWrite(BUZZER_PIN, LOW); return true; }
      delay(100); e += 100;
    }
    digitalWrite(BUZZER_PIN, LOW);
    e = 0;
    while (e < pauseMs) {
      if (bothDismissPressed()) return true;
      delay(100); e += 100;
    }
  }
  return false;
}

void continuousBeep(const char* label) {
  while (true) {
    ntp.update();
    unsigned long ep = ntp.getEpochTime();
    int hh = (ep % 86400) / 3600, mm = (ep % 3600) / 60, ss = ep % 60;
    lcd.setCursor(0, 0); lcd.print(formatTime12hr(hh, mm, ss));
    lcd.setCursor(0, 1);
    String row1 = String("  ") + label + "!!      ";
    lcd.print(row1.substring(0, 16));
    digitalWrite(BUZZER_PIN, HIGH);
    int e = 0;
    while (e < 1000) {
      if (bothDismissPressed()) { digitalWrite(BUZZER_PIN, LOW); return; }
      delay(100); e += 100;
    }
    digitalWrite(BUZZER_PIN, LOW);
    e = 0;
    while (e < 2000) {
      if (bothDismissPressed()) return;
      delay(100); e += 100;
    }
  }
}

void handleButton(int pin, unsigned long &lastPress, bool &lastState, void (*onPress)()) {
  bool state = digitalRead(pin);
  if (state == LOW && lastState == HIGH && millis() - lastPress > 300) {
    lastPress = millis(); onPress();
  }
  lastState = state;
}

void resetDayFiredFlags() {
  for (int i = 0; i < 9; i++) normalFired[i] = false;
  for (int i = 0; i < 8; i++) tutionFired[i] = false;
  for (int i = 0; i < 9; i++) satFired[i]    = false;
  alarm4amActive = false; alarm4amDismissed = false;
}

// ─────────────────────────────────────────
//  WEB SERVER PAGES
// ─────────────────────────────────────────
void handleRoot() {
  String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>NTP Clock Setup</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: Arial, sans-serif;
      background: #1a1a2e;
      color: #eee;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      padding: 20px;
    }
    .card {
      background: #16213e;
      border-radius: 16px;
      padding: 32px 28px;
      width: 100%;
      max-width: 380px;
      box-shadow: 0 8px 32px rgba(0,0,0,0.4);
    }
    h2 {
      text-align: center;
      margin-bottom: 8px;
      font-size: 22px;
      color: #e94560;
    }
    p.sub {
      text-align: center;
      font-size: 13px;
      color: #aaa;
      margin-bottom: 24px;
    }
    label {
      display: block;
      font-size: 13px;
      color: #aaa;
      margin-bottom: 6px;
    }
    input {
      width: 100%;
      padding: 12px 14px;
      border-radius: 8px;
      border: 1px solid #0f3460;
      background: #0f3460;
      color: #fff;
      font-size: 15px;
      margin-bottom: 18px;
      outline: none;
    }
    input:focus { border-color: #e94560; }
    button {
      width: 100%;
      padding: 13px;
      background: #e94560;
      color: white;
      border: none;
      border-radius: 8px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
    }
    button:active { background: #c73652; }
    .icon { text-align: center; font-size: 40px; margin-bottom: 12px; }
  </style>
</head>
<body>
  <div class='card'>
    <h2>NTP Clock WiFi Setup</h2>
    <p class='sub'>Connect your NTP clock to WiFi</p>
    <form method='POST' action='/connect'>
      <label>WiFi Network Name (SSID)</label>
      <input type='text' name='ssid' placeholder='Enter SSID' required autocomplete='off'>
      <label>Password</label>
      <input type='password' name='pass' placeholder='Enter Password' autocomplete='off'>
      <button type='submit'>Connect &#8594;</button>
    </form>
  </div>
</body>
</html>
)rawhtml";
  server.send(200, "text/html", html);
}

void handleConnect() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    newSSID = server.arg("ssid");
    newPASS = server.arg("pass");
    newSSID.trim();

    Serial.print("Received SSID: ["); Serial.print(newSSID); Serial.println("]");
    Serial.print("Password length: "); Serial.println(newPASS.length());

    String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Connecting...</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #1a1a2e;
      color: #eee;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
    }
    .card {
      background: #16213e;
      border-radius: 16px;
      padding: 40px 28px;
      text-align: center;
      max-width: 340px;
      width: 90%;
    }
    .spinner {
      width: 48px; height: 48px;
      border: 5px solid #0f3460;
      border-top-color: #e94560;
      border-radius: 50%;
      animation: spin 1s linear infinite;
      margin: 0 auto 20px;
    }
    @keyframes spin { to { transform: rotate(360deg); } }
    h2 { color: #e94560; margin-bottom: 10px; }
    p  { color: #aaa; font-size: 14px; }
  </style>
</head>
<body>
  <div class='card'>
    <div class='spinner'></div>
    <h2>Credentials Received!</h2>
    <p>The clock is now connecting to your WiFi.<br><br>
    You can close this page.<br>
    Check the LCD for status.</p>
  </div>
</body>
</html>
)rawhtml";
    server.send(200, "text/html", html);
    credentialsReceived = true;
  } else {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  }
}

void handleNotFound() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

// ─────────────────────────────────────────
//  WIFI HELPERS
// ─────────────────────────────────────────
bool connectToWiFi(const char* ssid, const char* pass, int timeoutSecs = 20) {
  Serial.print("Connecting to: "); Serial.println(ssid);
  WiFi.disconnect();
  delay(300);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < timeoutSecs * 2) {
    delay(500); tries++;
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi status: "); Serial.println(WiFi.status());
  return WiFi.status() == WL_CONNECTED;
}

void startAP() {
  WiFi.disconnect();
  delay(300);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP started. IP: "); Serial.println(ip);

  server.on("/", handleRoot);
  server.on("/connect", HTTP_POST, handleConnect);
  server.onNotFound(handleNotFound);
  server.begin();

  usingAP   = true;
  ntpSynced = false;

  lcdPrint("Connect to WiFi:", AP_SSID);
  delay(2000);
  lcdPrint("Open browser:", ip.toString());
  delay(2000);
  lcdPrint("Waiting for", "credentials...");
}

void stopAP() {
  server.stop();
  WiFi.softAPdisconnect(true);
  usingAP = false;
  delay(300);
}

void syncNTP() {
  lcdPrint("Syncing NTP...", "please wait");
  ntp.begin();
  for (int attempt = 0; attempt < 3; attempt++) {
    if (ntp.forceUpdate()) {
      ntpSynced = true;
      lcdPrint("Time synced!", "");
      delay(1000);
      return;
    }
    delay(1000);
  }
  lcdPrint("NTP failed!", "Will retry...");
  delay(1500);
  ntpSynced = false;
}

bool isPrimaryAvailable() {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == PRIMARY_SSID) return true;
  }
  return false;
}

// ─────────────────────────────────────────
//  SETUP WIFI
// ─────────────────────────────────────────
void setupWiFi() {
  lcdPrint("Connecting to", String(PRIMARY_SSID).substring(0, 16));

  if (connectToWiFi(PRIMARY_SSID, PRIMARY_PASS)) {
    usingAP = false;
    lcdPrint("Connected!", WiFi.localIP().toString());
    delay(1500);
    syncNTP();
    return;
  }

  // Primary failed — start AP portal
  lcdPrint("Primary failed!", "Starting AP...");
  delay(1000);
  startAP();
}

// ─────────────────────────────────────────
//  RECONNECT WATCHDOG
// ─────────────────────────────────────────
void checkAndReconnect() {
  if (millis() - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
  lastWiFiCheck = millis();

  // If in AP mode, skip reconnect check (handled in loop)
  if (usingAP) return;

  if (WiFi.status() != WL_CONNECTED) {
    lcdPrint("WiFi lost!", "Reconnecting...");
    delay(500);

    if (connectToWiFi(PRIMARY_SSID, PRIMARY_PASS)) {
      lcdPrint("Reconnected!", WiFi.localIP().toString());
      delay(1500);
      syncNTP();
      return;
    }

    // Primary not available — start AP
    lcdPrint("Cannot reconnect", "Starting AP...");
    delay(1000);
    startAP();
    return;
  }

  // Retry NTP if not synced
  if (!ntpSynced) syncNTP();
}

// ─────────────────────────────────────────
//  BUTTON CALLBACKS
// ─────────────────────────────────────────
void onD5Press() {
  isSatHoliday = !isSatHoliday;
  if (isSatHoliday) {
    int daysUntilSat = (6 - g_weekday + 7) % 7;
    int satDay = g_day + daysUntilSat, satMonth = g_month, satYear = g_year;
    while (true) {
      uint8_t dim = DAYS_IN_MONTH[satMonth - 1];
      if (satMonth == 2 && isLeapYear(satYear)) dim = 29;
      if (satDay <= dim) break;
      satDay -= dim; satMonth++;
      if (satMonth > 12) { satMonth = 1; satYear++; }
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "Sat %02d %s %d", satDay, MONTHS[satMonth - 1], satYear);
    lcdPrint("Sat is Holiday!", String(buf));
    delay(3000);
  } else {
    lcdPrint("Sat not holiday", "School on Sat!");
    delay(1500);
  }
}

void onD6Press() {
  isTutionDay = !isTutionDay;
  if (isTutionDay) lcdPrint("Maths Tution",   "Day set!");
  else              lcdPrint("Normal Day set!", "No tution today");
  delay(1500);
}

void onD7Press() {
  isHoliday = !isHoliday;
  if (isHoliday) lcdPrint("Holiday!",      "No reminders");
  else            lcdPrint("Back to normal", "clock mode");
  delay(1500);
}

// ─────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN,   OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  pinMode(BTN_DISMISS1, INPUT_PULLUP);
  pinMode(BTN_DISMISS2, INPUT_PULLUP);
  pinMode(BTN_HOLIDAY,  INPUT_PULLUP);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.init();
  lcd.backlight();

  lcdPrint("  NTP  Clock  ", "  ESP8266  ");
  delay(1200);

  WiFi.mode(WIFI_STA);
  setupWiFi();
}

// ─────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────
void loop() {

  // ── AP mode: serve web portal, wait for credentials ──
  if (usingAP) {
    server.handleClient();

    if (credentialsReceived) {
      credentialsReceived = false;
      lcdPrint("Got credentials!", "Connecting...");
      delay(500);

      stopAP();

      if (connectToWiFi(newSSID.c_str(), newPASS.c_str())) {
        lcdPrint("Connected!", WiFi.localIP().toString());
        delay(1500);
        syncNTP();
      } else {
        lcdPrint("Failed!", "Restarting AP..");
        delay(1500);
        newSSID = "";
        newPASS = "";
        startAP();
      }
    }
    return;
  }

  // ── Periodic reconnect check ──
  checkAndReconnect();

  // ── No NTP yet ──
  if (!ntpSynced) {
    lcdPrint("Waiting for", "time sync...");
    delay(500);
    return;
  }

  ntp.update();

  unsigned long epoch = ntp.getEpochTime();
  int h = (epoch % 86400) / 3600;
  int m = (epoch % 3600)  / 60;
  int s =  epoch % 60;

  epochToDate(epoch);

  if (g_day != lastDay) {
    lastDay = g_day;
    resetDayFiredFlags();
    isTutionDay  = false;
    isSatHoliday = false;
    isHoliday    = false;
  }

  handleButton(BTN_DISMISS1, lastD5Press, lastD5State, onD5Press);
  handleButton(BTN_DISMISS2, lastD6Press, lastD6State, onD6Press);
  handleButton(BTN_HOLIDAY,  lastD7Press, lastD7State, onD7Press);

  if (isHoliday) {
    char r0[17], r1[17];
    snprintf(r0, sizeof(r0), "%-16s", formatTime12hr(h, m, s).c_str());
    snprintf(r1, sizeof(r1), "%-16s", formatDate().c_str());
    lcd.setCursor(0, 0); lcd.print(r0);
    lcd.setCursor(0, 1); lcd.print(r1);
    delay(1000); return;
  }

  if (isSatHoliday) {
    char r0[17];
    snprintf(r0, sizeof(r0), "%-16s", formatTime12hr(h, m, s).c_str());
    lcd.setCursor(0, 0); lcd.print(r0);
    lcd.setCursor(0, 1); lcd.print("Sat Holiday!    ");
    delay(1000); return;
  }

  Alarm* activeAlarms; bool* firedFlags; int activeCount;
  if (g_weekday == 6 && !isSatHoliday) {
    activeAlarms = saturdayAlarms; firedFlags = satFired;    activeCount = saturdayCount;
  } else if (isTutionDay) {
    activeAlarms = tutionAlarms;   firedFlags = tutionFired; activeCount = tutionCount;
  } else {
    activeAlarms = normalAlarms;   firedFlags = normalFired; activeCount = normalCount;
  }

  for (int i = 0; i < activeCount; i++) {
    if (firedFlags[i]) continue;
    if (h != activeAlarms[i].hour || m != activeAlarms[i].minute) continue;

    firedFlags[i] = true;
    const char* label = activeAlarms[i].label;
    int bCount = activeAlarms[i].beepCount;
    int bMs    = activeAlarms[i].beepMs;

    if (activeAlarms[i].hour == 4 && activeAlarms[i].minute == 0) {
      alarm4amActive = true; alarm4amDismissed = false; alarm4amStart = millis();

      while (!alarm4amDismissed) {
        ntp.update();
        unsigned long ep2 = ntp.getEpochTime();
        int hh2 = (ep2 % 86400) / 3600, mm2 = (ep2 % 3600) / 60;

        if (hh2 == 4 && mm2 >= 30) {
          continuousBeep("WAKE UP"); alarm4amDismissed = true; break;
        }
        lcd.setCursor(0, 0); lcd.print(formatTime12hr(hh2, mm2, ep2 % 60));
        lcd.setCursor(0, 1); lcd.print("  ** WAKE UP ** ");

        bool dismissed = beepCycle(4, 3000, 500);
        if (dismissed || bothDismissPressed()) {
          alarm4amDismissed = true; digitalWrite(BUZZER_PIN, LOW);
          lcdPrint("Alarm stopped!", "Good morning :)"); delay(2000); break;
        }

        for (int w = 0; w < 600; w++) {
          if (bothDismissPressed()) {
            alarm4amDismissed = true;
            lcdPrint("Alarm stopped!", "Good morning :)"); delay(2000); break;
          }
          ntp.update(); ep2 = ntp.getEpochTime();
          hh2 = (ep2 % 86400) / 3600; mm2 = (ep2 % 3600) / 60;
          lcd.setCursor(0, 0); lcd.print(formatTime12hr(hh2, mm2, ep2 % 60));
          lcd.setCursor(0, 1); lcd.print("  ** WAKE UP ** ");
          if (hh2 == 4 && mm2 >= 30) break;
          delay(100);
        }
      }
      return;
    }

    lcd.setCursor(0, 0); lcd.print(formatTime12hr(h, m, s));
    lcd.setCursor(0, 1);
    String row1 = String(label) + "                ";
    lcd.print(row1.substring(0, 16));
    beepCycle(bCount, bMs, 300);
    break;
  }

  char r0[17], r1[17];
  snprintf(r0, sizeof(r0), "%-16s", formatTime12hr(h, m, s).c_str());
  snprintf(r1, sizeof(r1), "%-16s", formatDate().c_str());
  lcd.setCursor(0, 0); lcd.print(r0);
  lcd.setCursor(0, 1); lcd.print(r1);
  delay(1000);
}