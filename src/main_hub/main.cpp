#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <LoRa.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#include "ResQConfig.h"
#include "ResQProtocol.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
  #warning "Using secrets.example.h - copy it to secrets.h and edit your WiFi credentials"
#endif

static WebSocketsServer ws(HUB_WS_PORT);

static uint32_t g_hub_id              = 0;
static uint32_t g_last_heartbeat_ms   = 0;
static uint32_t g_lora_packet_count   = 0;
static uint32_t g_lora_dropped_count  = 0;

static void send_hello(uint8_t client_id) {
  JsonDocument doc;
  doc["type"] = "hello";
  char id_hex[9];
  snprintf(id_hex, sizeof(id_hex), "%08X", g_hub_id);
  doc["hub_id"] = id_hex;
  doc["ts"]     = (uint32_t)millis();
  doc["fw"]     = FW_VERSION;

  String out;
  serializeJson(doc, out);
  ws.sendTXT(client_id, out);
}

static void on_ws_event(uint8_t client_id, WStype_t type, uint8_t* payload, size_t length) {
  (void)payload;
  (void)length;
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = ws.remoteIP(client_id);
      Serial.printf("[WS] client #%u connected from %s\n", client_id, ip.toString().c_str());
      send_hello(client_id);
      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("[WS] client #%u disconnected\n", client_id);
      break;
    default:
      break;
  }
}

static void on_lora_packet(int packet_size) {
  if (packet_size < (int)sizeof(ResQ::SOSPacket)) {
    ++g_lora_dropped_count;
    while (LoRa.available()) LoRa.read();
    return;
  }

  uint8_t buf[sizeof(ResQ::SOSPacket)];
  for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = (uint8_t)LoRa.read();
  while (LoRa.available()) LoRa.read();

  ResQ::SOSPacket* pkt = reinterpret_cast<ResQ::SOSPacket*>(buf);
  if (!ResQ::verify_sos_packet(*pkt)) {
    ++g_lora_dropped_count;
    Serial.println("[LoRa] verify failed - dropped");
    return;
  }

  ++g_lora_packet_count;
  digitalWrite(PIN_LED_LORA, HIGH);

  JsonDocument doc;
  doc["type"] = "lora_rx";
  doc["rssi"] = LoRa.packetRssi();
  doc["snr"]  = LoRa.packetSnr();
  doc["ts"]   = (uint32_t)millis();

  JsonObject p = doc["packet"].to<JsonObject>();
  char id_hex[9];
  snprintf(id_hex, sizeof(id_hex), "%08X", pkt->device_id);
  p["device_id"]        = id_hex;
  p["packet_type"]      = (pkt->packet_type == ResQ::PKT_SOS) ? "sos"
                        : (pkt->packet_type == ResQ::PKT_TRIAGE) ? "triage"
                        : "heartbeat";
  p["sequence"]         = pkt->sequence;
  p["triage"]           = pkt->triage_level;
  p["heart_rate"]       = pkt->heart_rate;
  p["spo2"]             = pkt->spo2;
  p["battery_pct"]      = pkt->battery_pct;
  p["last_g_force_x10"] = pkt->last_g_force_x10;
  p["hop_count"]        = pkt->hop_count;

  String out;
  serializeJson(doc, out);
  ws.broadcastTXT(out);

  Serial.printf("[LoRa] id=%s type=%u seq=%u triage=%u HR=%u SpO2=%u rssi=%d -> %u clients\n",
                id_hex, pkt->packet_type, pkt->sequence, pkt->triage_level,
                pkt->heart_rate, pkt->spo2, LoRa.packetRssi(), ws.connectedClients());

  digitalWrite(PIN_LED_LORA, LOW);
}

static void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("resq-mainhub");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("[WiFi] connecting to %s", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println("\n[WiFi] timeout - restarting");
      ESP.restart();
    }
  }
  Serial.println();
  Serial.printf("[WiFi] %s  ip=%s  rssi=%d dBm\n",
                WIFI_SSID, WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

static void init_lora() {
  SPI.begin();
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LoRa] init failed - check wiring");
    while (true) {
      digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
      delay(150);
    }
  }
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();
  LoRa.onReceive(on_lora_packet);
  LoRa.receive();
  Serial.printf("[LoRa] listening @ %.1f MHz SF%d BW%.0f kHz\n",
                LORA_FREQUENCY / 1e6, LORA_SPREADING_FACTOR, LORA_BANDWIDTH / 1e3);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_LED_LORA,   OUTPUT);

  g_hub_id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF);

  Serial.println();
  Serial.println(F("================================"));
  Serial.printf( "  %s\n", BOARD_NAME);
  Serial.printf( "  FW %s\n", FW_VERSION);
  Serial.printf( "  Hub ID: %08X\n", g_hub_id);
  Serial.println(F("================================"));

  connect_wifi();
  init_lora();

  ws.begin();
  ws.onEvent(on_ws_event);
  Serial.printf("[WS] listening on ws://%s:%d/ws\n",
                WiFi.localIP().toString().c_str(), HUB_WS_PORT);
}

void loop() {
  ws.loop();

  const uint32_t now = millis();
  if (now - g_last_heartbeat_ms >= HUB_HEARTBEAT_MS) {
    g_last_heartbeat_ms = now;

    JsonDocument doc;
    doc["type"]     = "heartbeat";
    char id_hex[9];
    snprintf(id_hex, sizeof(id_hex), "%08X", g_hub_id);
    doc["hub_id"]   = id_hex;
    doc["ts"]       = now;
    doc["uptime_s"] = now / 1000;
    doc["packets"]  = g_lora_packet_count;
    doc["dropped"]  = g_lora_dropped_count;
    doc["clients"]  = ws.connectedClients();
    doc["rssi"]     = WiFi.RSSI();

    String out;
    serializeJson(doc, out);
    ws.broadcastTXT(out);

    digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] lost - reconnecting");
    connect_wifi();
  }
}
