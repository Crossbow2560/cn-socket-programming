#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace Config {
// Update these values before uploading from Arduino IDE.
const char *WIFI_SSID = "WIFI SSID";
const char *WIFI_PASSWORD = "WIFI PASS";

const char *NODE_ID = "Node 1";
const char *SERVER_HOST = "SERVER IP";
const uint16_t SERVER_PORT = 5005;

const uint8_t RED_LED_PIN = 4;
const uint8_t GREEN_LED_PIN = 5;

const unsigned long SERIAL_BAUD = 115200;
const unsigned long WIFI_RETRY_MS = 10000;
const unsigned long TLS_RETRY_MS = 5000;
const unsigned long AUTO_UPDATE_MS = 1000;
}

// Root CA / server certificate used by the Python TLS server.
static const char SERVER_CA[] PROGMEM = R"PEM(
Paste Certificate Here
)PEM";

enum SignalState {
  SIGNAL_RED,
  SIGNAL_YELLOW,
  SIGNAL_GREEN
};

class HardwareController {
public:
  void begin() {
    pinMode(Config::RED_LED_PIN, OUTPUT);
    pinMode(Config::GREEN_LED_PIN, OUTPUT);
    setSignal(SIGNAL_RED);
  }

  void setSignal(SignalState state) {
    currentSignal = state;
    switch (state) {
      case SIGNAL_RED:
        digitalWrite(Config::RED_LED_PIN, LOW);
        digitalWrite(Config::GREEN_LED_PIN, HIGH);
        break;
      case SIGNAL_GREEN:
        digitalWrite(Config::RED_LED_PIN, HIGH);
        digitalWrite(Config::GREEN_LED_PIN, LOW);
        break;
      case SIGNAL_YELLOW:
        // With two LEDs only, show YELLOW as both LEDs on.
        digitalWrite(Config::RED_LED_PIN, LOW);
        digitalWrite(Config::GREEN_LED_PIN, LOW);
        break;
    }
  }

  SignalState getSignal() const {
    return currentSignal;
  }

private:
  SignalState currentSignal = SIGNAL_RED;
};

class TrafficLogic {
public:
  void begin() {
    randomSeed((uint32_t)esp_random());
  }

  void setVehicleCount(int count) {
    vehicleCount = constrain(count, 0, 200);
  }

  int getVehicleCount() const {
    return vehicleCount;
  }

  void toggleAutoMode() {
    autoMode = !autoMode;
  }

  void setAutoMode(bool enabled) {
    autoMode = enabled;
  }

  bool isAutoMode() const {
    return autoMode;
  }

  void setSignal(SignalState signal) {
    currentSignal = signal;
  }

  SignalState getSignal() const {
    return currentSignal;
  }

  bool shouldRunAutoUpdate(unsigned long now) const {
    return autoMode && (now - lastAutoUpdateMs >= Config::AUTO_UPDATE_MS);
  }

  void updateAuto(unsigned long now) {
    lastAutoUpdateMs = now;

    if (currentSignal == SIGNAL_RED) {
      vehicleCount += random(3, 9);
    } else if (currentSignal == SIGNAL_GREEN) {
      vehicleCount -= random(5, 11);
    } else {
      vehicleCount += random(0, 3);
    }

    vehicleCount = constrain(vehicleCount, 0, 200);
  }

private:
  int vehicleCount = 20;
  bool autoMode = false;
  SignalState currentSignal = SIGNAL_RED;
  unsigned long lastAutoUpdateMs = 0;
};

class SecureTrafficClient {
public:
  void begin() {
    client.setCACert(SERVER_CA);
  }

  void loop() {
    ensureWiFi();
    ensureTls();
    readIncomingMessages();
  }

  bool isConnected() {
    return client.connected();
  }

  bool sendVehicleCount(const char *nodeId, int vehicleCount) {
    if (!client.connected()) {
      return false;
    }

    String payload = "{\"node_id\":\"";
    payload += escapeJson(nodeId);
    payload += "\",\"vehicle_count\":";
    payload += String(vehicleCount);
    payload += "}\n";

    size_t written = client.print(payload);
    if (written != payload.length()) {
      Serial.println("Send failed, closing TLS connection.");
      client.stop();
      return false;
    }

    Serial.print("Sent: ");
    Serial.println(payload);
    return true;
  }

  bool pollSignal(SignalState &signalOut) {
    if (!hasPendingSignal) {
      return false;
    }

    signalOut = pendingSignal;
    hasPendingSignal = false;
    return true;
  }

private:
  WiFiClientSecure client;
  String rxBuffer;
  unsigned long lastWiFiAttemptMs = 0;
  unsigned long lastTlsAttemptMs = 0;
  bool hasPendingSignal = false;
  SignalState pendingSignal = SIGNAL_RED;

  void ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
      return;
    }

    unsigned long now = millis();
    if (now - lastWiFiAttemptMs < Config::WIFI_RETRY_MS) {
      return;
    }

    lastWiFiAttemptMs = now;
    Serial.print("Connecting WiFi to ");
    Serial.println(Config::WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
  }

  void ensureTls() {
    if (WiFi.status() != WL_CONNECTED || client.connected()) {
      return;
    }

    unsigned long now = millis();
    if (now - lastTlsAttemptMs < Config::TLS_RETRY_MS) {
      return;
    }

    lastTlsAttemptMs = now;
    Serial.print("Opening TLS connection to ");
    Serial.print(Config::SERVER_HOST);
    Serial.print(":");
    Serial.println(Config::SERVER_PORT);

    if (!client.connect(Config::SERVER_HOST, Config::SERVER_PORT)) {
      Serial.println("TLS connect failed.");
      return;
    }

    Serial.println("TLS connected.");
  }

  void readIncomingMessages() {
    while (client.connected() && client.available()) {
      char c = static_cast<char>(client.read());
      if (c == '\n') {
        handleMessage(rxBuffer);
        rxBuffer = "";
      } else if (c != '\r') {
        rxBuffer += c;
      }
    }
  }

  void handleMessage(const String &line) {
    if (line.length() == 0) {
      return;
    }

    String signalValue = extractJsonString(line, "signal");
    if (signalValue.length() == 0) {
      Serial.print("Ignoring unrecognized message: ");
      Serial.println(line);
      return;
    }

    SignalState parsed = parseSignal(signalValue);
    pendingSignal = parsed;
    hasPendingSignal = true;

    Serial.print("Received signal: ");
    Serial.println(signalValue);
  }

  static String extractJsonString(const String &json, const char *key) {
    String token = "\"";
    token += key;
    token += "\"";

    int keyPos = json.indexOf(token);
    if (keyPos < 0) {
      return "";
    }

    int colonPos = json.indexOf(':', keyPos + token.length());
    if (colonPos < 0) {
      return "";
    }

    int firstQuote = json.indexOf('"', colonPos + 1);
    if (firstQuote < 0) {
      return "";
    }

    int secondQuote = json.indexOf('"', firstQuote + 1);
    if (secondQuote < 0) {
      return "";
    }

    return json.substring(firstQuote + 1, secondQuote);
  }

  static String escapeJson(const char *input) {
    String escaped;
    while (*input != '\0') {
      if (*input == '"' || *input == '\\') {
        escaped += '\\';
      }
      escaped += *input++;
    }
    return escaped;
  }

  static SignalState parseSignal(const String &value) {
    if (value == "GREEN") {
      return SIGNAL_GREEN;
    }
    if (value == "YELLOW") {
      return SIGNAL_YELLOW;
    }
    return SIGNAL_RED;
  }
};

HardwareController hardware;
TrafficLogic logic;
SecureTrafficClient trafficClient;
String serialBuffer;
bool pendingSend = true;
bool wasConnected = false;

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  <number>      Set vehicle count (0-200) and send immediately");
  Serial.println("  send          Send the current vehicle count now");
  Serial.println("  auto on       Enable automatic count updates");
  Serial.println("  auto off      Disable automatic count updates");
  Serial.println("  auto          Toggle automatic count updates");
  Serial.println("  status        Print current node state");
  Serial.println();
}

void printStatus() {
  Serial.print("Node: ");
  Serial.println(Config::NODE_ID);
  Serial.print("WiFi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");
  Serial.print("TLS: ");
  Serial.println(trafficClient.isConnected() ? "connected" : "disconnected");
  Serial.print("Vehicles: ");
  Serial.println(logic.getVehicleCount());
  Serial.print("Auto mode: ");
  Serial.println(logic.isAutoMode() ? "ON" : "OFF");
  Serial.print("Signal: ");
  switch (logic.getSignal()) {
    case SIGNAL_GREEN:
      Serial.println("GREEN");
      break;
    case SIGNAL_YELLOW:
      Serial.println("YELLOW");
      break;
    default:
      Serial.println("RED");
      break;
  }
}

void handleSerialLine(String line) {
  // Serial Monitor replaces the Tkinter slider/buttons.
  line.trim();
  if (line.length() == 0) {
    return;
  }

  if (line == "send") {
    pendingSend = true;
    return;
  }

  if (line == "auto on") {
    logic.setAutoMode(true);
    Serial.println("Auto mode enabled.");
    return;
  }

  if (line == "auto off") {
    logic.setAutoMode(false);
    Serial.println("Auto mode disabled.");
    return;
  }

  if (line == "auto") {
    logic.toggleAutoMode();
    Serial.println(logic.isAutoMode() ? "Auto mode enabled." : "Auto mode disabled.");
    return;
  }

  if (line == "status") {
    printStatus();
    return;
  }

  bool validNumber = true;
  for (size_t i = 0; i < line.length(); ++i) {
    if (!isDigit(line[i])) {
      validNumber = false;
      break;
    }
  }

  if (!validNumber) {
    Serial.print("Unknown command: ");
    Serial.println(line);
    printHelp();
    return;
  }

  int newCount = line.toInt();
  logic.setVehicleCount(newCount);
  Serial.print("Vehicle count set to ");
  Serial.println(logic.getVehicleCount());
  pendingSend = true;
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      handleSerialLine(serialBuffer);
      serialBuffer = "";
    } else if (c != '\r') {
      serialBuffer += c;
    }
  }
}

void setup() {
  Serial.begin(Config::SERIAL_BAUD);
  delay(200);

  hardware.begin();
  logic.begin();
  trafficClient.begin();

  Serial.println("Secure traffic node starting (ESP32).");
  printHelp();
  printStatus();
}

void loop() {
  readSerialCommands();
  trafficClient.loop();

  bool connectedNow = trafficClient.isConnected();
  if (connectedNow && !wasConnected) {
    Serial.println("TLS link is ready, syncing current vehicle count.");
    pendingSend = true;
  }
  wasConnected = connectedNow;

  SignalState signal;
  if (trafficClient.pollSignal(signal)) {
    logic.setSignal(signal);
    hardware.setSignal(signal);
    printStatus();
  }

  unsigned long now = millis();
  if (logic.shouldRunAutoUpdate(now)) {
    logic.updateAuto(now);
    Serial.print("Auto-updated vehicles to ");
    Serial.println(logic.getVehicleCount());
    pendingSend = true;
  }

  if (pendingSend && connectedNow) {
    if (trafficClient.sendVehicleCount(Config::NODE_ID, logic.getVehicleCount())) {
      pendingSend = false;
    }
  }
}
