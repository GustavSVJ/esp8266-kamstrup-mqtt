#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "gcm.h"
#include "mbusparser.h"
#include "secrets.h"
#include <ArduinoOTA.h>

const size_t headersize = 11;
const size_t footersize = 3;
uint8_t encryption_key[16];
uint8_t authentication_key[16];
uint8_t receiveBuffer[500];
uint8_t decryptedFrameBuffer[500];
VectorView decryptedFrame(decryptedFrameBuffer, 0);
MbusStreamParser streamParser(receiveBuffer, sizeof(receiveBuffer));
mbedtls_gcm_context m_ctx;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long currentMillis;

unsigned long latestReceivedDataMillis;
const unsigned long expectedDataInterval = 15000;

void setup() {
  delay(10000);

  pinMode(LED_BUILTIN, OUTPUT);

  for (int i = 0; i < 5; i++)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
  });

  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
  ArduinoOTA.onError([](ota_error_t error) {});
  ArduinoOTA.begin();

  mqttClient.setServer(mqttServer, mqttPort);
  
  while (!mqttClient.connected()) {
    mqttClient.connect(mqttClientID, mqttUser, mqttPassword, (String(mqtt_topic) + "/status/esp").c_str(), 0, true, "offline");
  }

  mqttClient.publish((String(mqtt_topic) + "/status/esp").c_str(), "online", true);

  Serial.begin(2400);
  Serial.swap();
  
  hexStr2bArr(encryption_key, conf_key, sizeof(encryption_key));
  hexStr2bArr(authentication_key, conf_authkey, sizeof(authentication_key));
}

void loop() {
  currentMillis = millis();

  while (Serial.available() > 0) {
    if (streamParser.pushData(Serial.read())) {
      VectorView frame = streamParser.getFrame();
      if (streamParser.getContentType() == MbusStreamParser::COMPLETE_FRAME) {
        if (!decrypt(frame))
        {
          latestReceivedDataMillis = currentMillis;
          sendmsg(String(mqtt_topic) + "/status/kamstrup", "decryption failed");
          return;
        }
        latestReceivedDataMillis = currentMillis;
        sendmsg(String(mqtt_topic) + "/status/kamstrup", "ok");

        MeterData md = parseMbusFrame(decryptedFrame);
        sendData(md);
      }
      else
      {
        latestReceivedDataMillis = currentMillis;
        sendmsg(String(mqtt_topic) + "/status/kamstrup", "parsing failed");
      }
    }
  }

  if (currentMillis - latestReceivedDataMillis > expectedDataInterval)
  {
    latestReceivedDataMillis = currentMillis;
    sendmsg(String(mqtt_topic) + "/status/kamstrup", "missing output");
  }
  
  ArduinoOTA.handle();
  mqttClient.loop();
}


void sendData(MeterData md) {
  if (md.activePowerPlusValid)
    sendmsg(String(mqtt_topic) + "/power/activePowerPlus", String(md.activePowerPlus));
  if (md.activePowerMinusValid)
    sendmsg(String(mqtt_topic) + "/power/activePowerMinus", String(md.activePowerMinus));
  if (md.activePowerPlusValidL1)
    sendmsg(String(mqtt_topic) + "/power/activePowerPlusL1", String(md.activePowerPlusL1));
  if (md.activePowerMinusValidL1)
    sendmsg(String(mqtt_topic) + "/power/activePowerMinusL1", String(md.activePowerMinusL1));
  if (md.activePowerPlusValidL2)
    sendmsg(String(mqtt_topic) + "/power/activePowerPlusL2", String(md.activePowerPlusL2));
  if (md.activePowerMinusValidL2)
    sendmsg(String(mqtt_topic) + "/power/activePowerMinusL2", String(md.activePowerMinusL2));
  if (md.activePowerPlusValidL3)
    sendmsg(String(mqtt_topic) + "/power/activePowerPlusL3", String(md.activePowerPlusL3));
  if (md.activePowerMinusValidL3)
    sendmsg(String(mqtt_topic) + "/power/activePowerMinusL3", String(md.activePowerMinusL3));
  if (md.reactivePowerPlusValid)
    sendmsg(String(mqtt_topic) + "/power/reactivePowerPlus", String(md.reactivePowerPlus));
  if (md.reactivePowerMinusValid)
    sendmsg(String(mqtt_topic) + "/power/reactivePowerMinus", String(md.reactivePowerMinus));

  if (md.powerFactorValidL1)
    sendmsg(String(mqtt_topic) + "/power/powerFactorL1", String(md.powerFactorL1));
  if (md.powerFactorValidL2)
    sendmsg(String(mqtt_topic) + "/power/powerFactorL2", String(md.powerFactorL2));
  if (md.powerFactorValidL3)
    sendmsg(String(mqtt_topic) + "/power/powerFactorL3", String(md.powerFactorL3));
  if (md.powerFactorTotalValid)
    sendmsg(String(mqtt_topic) + "/power/powerFactorTotal", String(md.powerFactorTotal));

  if (md.voltageL1Valid)
    sendmsg(String(mqtt_topic) + "/voltage/L1", String(md.voltageL1));
  if (md.voltageL2Valid)
    sendmsg(String(mqtt_topic) + "/voltage/L2", String(md.voltageL2));
  if (md.voltageL3Valid)
    sendmsg(String(mqtt_topic) + "/voltage/L3", String(md.voltageL3));

  if (md.centiAmpereL1Valid)
    sendmsg(String(mqtt_topic) + "/current/L1", String(md.centiAmpereL1 / 100.));
  if (md.centiAmpereL2Valid)
    sendmsg(String(mqtt_topic) + "/current/L2", String(md.centiAmpereL2 / 100.));
  if (md.centiAmpereL3Valid)
    sendmsg(String(mqtt_topic) + "/current/L3", String(md.centiAmpereL3 / 100.));

  if (md.activeImportWhValid)
    sendmsg(String(mqtt_topic) + "/energy/activeImportKWh", String(md.activeImportWh / 1000.));
  if (md.activeExportWhValid)
    sendmsg(String(mqtt_topic) + "/energy/activeExportKWh", String(md.activeExportWh / 1000.));
  if (md.activeImportWhValidL1)
    sendmsg(String(mqtt_topic) + "/energy/activeImportKWhL1", String(md.activeImportWhL1 / 1000.));
  if (md.activeExportWhValidL1)
    sendmsg(String(mqtt_topic) + "/energy/activeExportKWhL1", String(md.activeExportWhL1 / 1000.));
  if (md.activeImportWhValidL2)
    sendmsg(String(mqtt_topic) + "/energy/activeImportKWhL2", String(md.activeImportWhL2 / 1000.));
  if (md.activeExportWhValidL2)
    sendmsg(String(mqtt_topic) + "/energy/activeExportKWhL2", String(md.activeExportWhL2 / 1000.));
  if (md.activeImportWhValidL3)
    sendmsg(String(mqtt_topic) + "/energy/activeImportKWhL3", String(md.activeImportWhL3 / 1000.));
  if (md.activeExportWhValidL3)
    sendmsg(String(mqtt_topic) + "/energy/activeExportKWhL3", String(md.activeExportWhL3 / 1000.));

  if (md.reactiveImportWhValid)
    sendmsg(String(mqtt_topic) + "/energy/reactiveImportKWh", String(md.reactiveImportWh / 1000.));
  if (md.reactiveExportWhValid)
    sendmsg(String(mqtt_topic) + "/energy/reactiveExportKWh", String(md.reactiveExportWh / 1000.));
}

bool decrypt(const VectorView& frame) {

  if (frame.size() < headersize + footersize + 12 + 18) {
    return false;
  }

  memcpy(decryptedFrameBuffer, &frame.front(), frame.size());

  uint8_t system_title[8];
  memcpy(system_title, decryptedFrameBuffer + headersize + 2, 8);

  uint8_t initialization_vector[12];
  memcpy(initialization_vector, system_title, 8);
  memcpy(initialization_vector + 8, decryptedFrameBuffer + headersize + 14, 4);

  uint8_t additional_authenticated_data[17];
  memcpy(additional_authenticated_data, decryptedFrameBuffer + headersize + 13, 1);
  memcpy(additional_authenticated_data + 1, authentication_key, 16);

  uint8_t authentication_tag[12];
  memcpy(authentication_tag, decryptedFrameBuffer + headersize + frame.size() - headersize - footersize - 12, 12);

  uint8_t cipher_text[frame.size() - headersize - footersize - 18 - 12];
  memcpy(cipher_text, decryptedFrameBuffer + headersize + 18, frame.size() - headersize - footersize - 12 - 18);

  uint8_t plaintext[sizeof(cipher_text)];

  mbedtls_gcm_init(&m_ctx);
  int success = mbedtls_gcm_setkey(&m_ctx, MBEDTLS_CIPHER_ID_AES, encryption_key, sizeof(encryption_key) * 8);
  
  if (0 != success) {
    return false;
  }
  success = mbedtls_gcm_auth_decrypt(&m_ctx, sizeof(cipher_text), initialization_vector, sizeof(initialization_vector),
                                     additional_authenticated_data, sizeof(additional_authenticated_data), authentication_tag, sizeof(authentication_tag),
                                     cipher_text, plaintext);
  if (0 != success) {
    return false;
  }

  mbedtls_gcm_free(&m_ctx);

  //copy replace encrypted data with decrypted for mbusparser library. Checksum not updated. Hopefully not needed
  memcpy(decryptedFrameBuffer + headersize + 18, plaintext, sizeof(plaintext));
  decryptedFrame = VectorView(decryptedFrameBuffer, frame.size());

  return true;
}

void hexStr2bArr(uint8_t* dest, const char* source, int bytes_n)
{
  uint8_t* dst = dest;
  uint8_t* end = dest + sizeof(bytes_n);
  unsigned int u;

  while (dest < end && sscanf(source, "%2x", &u) == 1)
  {
    *dst++ = u;
    source += 2;
  }
}


void sendmsg(String topic, String payload) {
  if (mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    mqttClient.publish(topic.c_str(), payload.c_str());
    delay(10);
  } else {
    delay(60*1000);
    ESP.restart();
  }
}
