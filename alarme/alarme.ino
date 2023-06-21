#include <BluetoothSerial.h>
#include <Firebase_ESP_Client.h>
#include <Secrets.h>
#include <time.h>
#include <WiFi.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>

#define DATABASE_URL "https://dgr-alarmes-default-rtdb.firebaseio.com/"
#define LOG_TYPE_ACTIVATED 0
#define LOG_TYPE_DISABLED 1
#define LOG_TYPE_TRIGGERED 2

#define PIR_PIN 13
#define BUZZER_PIN 14
#define RF_PIN 27
#define RESET_PIN 23
#define BT_LED_PIN 33
#define WIFI_LED_PIN 32

#define RESET_DELAY 3000
#define SELF_UPDATE_DELAY 3000

#define NOTIFICATION_TOPIC "/topics/"
#define NOTIFICATION_TITLE "AVISO"
#define NOTIFICATION_BODY "Alarme Disparado"
#define CONTENT_AVAILABLE "application/json"
#define TIME_TO_LIVE "10"
#define PRIORITY "high"

FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbdo;
FirebaseData stream;
FirebaseJson json;

String macAddress;
unsigned int lastControlClickTime = 0;
unsigned int selfUpdateTime;
unsigned int resetTime;
bool resetButtonPressed = false;

bool activeAlarm = false;
bool triggeredAlarm = false;

const char* ntpServer = "pool.ntp.org";
// -------------------- FUNÇÕES --------------------
void BTWifiSetup() {
  BluetoothSerial SerialBT;
  String deviceName = "DGR-" + String(random(9999));
  SerialBT.begin(deviceName);
  Serial.println(F("Esperando credenciais Wi-Fi via Bluetooth..."));
  digitalWrite(BT_LED_PIN, HIGH);
  while (1) {
    if (SerialBT.available()) {
      String data = SerialBT.readString();
      WiFi.begin(data.substring(0, data.indexOf('\n')).c_str(), data.substring(data.indexOf('\n') + 1).c_str());
      return;
    }
  }
}

void buzzerResponse(bool active) {
  if (active) {
    digitalWrite(BUZZER_PIN, LOW);
    delay(400);
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    delay(400);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    delay(400);
    digitalWrite(BUZZER_PIN, HIGH);
  }
}

void createLog(int TYPE) {
  time_t now = getTime();
  json.clear();
  json.add("time", now);
  json.add("type", TYPE);
  if (Firebase.RTDB.pushJSON(&fbdo, "log/" + macAddress + "", &json)) {
    Serial.print(F("Registro adicionado: tipo "));
    Serial.println(TYPE);
  } else {
    Serial.println(F("Erro ao adicionar registro (log)"));
    Serial.println(fbdo.errorReason().c_str());
  }
}


void firebaseConfig() {
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;
  // config.token_status_callback = tokenStatusCallback;  // see addons/TokenHelper.h
  config.signer.preRefreshSeconds = 45 * 60;
  fbdo.setResponseSize(2048);
  stream.setResponseSize(2048);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  char path[32];
  sprintf(path, "device/%s", macAddress.c_str());
  if (Firebase.ready()) {
    if (!Firebase.RTDB.get(&fbdo, path)) {
      Serial.println("Dispositivo não encontrado no firebase, criando documento...");
      json.clear();
      json.add("active", false);
      json.add("triggered", false);
      if (Firebase.RTDB.set(&fbdo, path, &json)) {
        Serial.println("Dispositivo Criado\n");
        Serial.println(fbdo.payload().c_str());
      } else {
        Serial.println(fbdo.errorReason());
      }
    } else {
      Serial.println(F("Dispositivo ja existente no Firebase"));
      if (Firebase.RTDB.getBool(&fbdo, "/device/" + macAddress + "/active")) {
        activeAlarm = fbdo.to<bool>();
      }
      if (!Firebase.RTDB.setBool(&fbdo, "/device/" + macAddress + "/triggered", false)) {
        Serial.println(fbdo.errorReason().c_str());
      }
    }
    Firebase.RTDB.beginStream(&stream, "/device/" + macAddress + "/active");
    if (!Firebase.RTDB.beginStream(&stream, "/device/" + macAddress + "/active"))
      Serial.printf("stream begin error, %s\n\n", stream.errorReason().c_str());
    // Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);
  } else {
    Serial.println(F("Problema na conexão com Firebase."));
  }
  Firebase.FCM.setServerKey(FCM_SERVER_KEY);
}

unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return (0);
  }
  time(&now);
  return now;
}

void resetConfig() {
  WiFi.disconnect(false, true);
  delay(3000);
  digitalWrite(WIFI_LED_PIN, LOW);
  digitalWrite(BT_LED_PIN, LOW);
  ESP.restart();
}

// Lógica para botão de reset -> 5 segundos pressionado para limpeza de dados wi-fi e reinicialização do ESP
void resetControl(){
  if (digitalRead(RESET_PIN) && !resetButtonPressed) {
    resetTime = millis();
    resetButtonPressed = true;
  }
  if (!digitalRead(RESET_PIN) && resetButtonPressed) {
    resetButtonPressed = false;
  }
  if (digitalRead(RESET_PIN) && resetButtonPressed && millis() - resetTime >= RESET_DELAY) {
    resetConfig();
  }
}

//controle do controle RF
void RFControl(){
  if (digitalRead(RF_PIN) && millis() > lastControlClickTime + 1000) {
    lastControlClickTime = millis();
    if (!activeAlarm) {
      activeAlarm = true;
      selfUpdateTime = millis();
      if (!Firebase.RTDB.setBool(&fbdo, "device/" + macAddress + "/active", true)){
        Serial.println("Erro ao atualizar RTDB (active: false -> true)");
        Serial.println(fbdo.errorReason().c_str());
      }
    } else {
      activeAlarm = false;
      selfUpdateTime = millis();
      if (!Firebase.RTDB.setBool(&fbdo, "device/" + macAddress + "/active", false)){
        Serial.println("Erro ao atualizar RTDB (active: true -> false)");
        Serial.println(fbdo.errorReason().c_str());
      }
    }
    buzzerResponse(activeAlarm);
  }
}

void sendNotification() {
  FCM_Legacy_HTTP_Message msg;
  String mac = macAddress;
  mac.replace(":", "");
  msg.targets.to = NOTIFICATION_TOPIC + mac;
  msg.options.content_available = CONTENT_AVAILABLE;
  msg.options.time_to_live = TIME_TO_LIVE;
  msg.options.priority = PRIORITY;
  msg.payloads.notification.title = NOTIFICATION_TITLE;
  msg.payloads.notification.body = NOTIFICATION_BODY;
  if (!Firebase.FCM.send(&fbdo, &msg)) {
    Serial.println("Erro ao enviar notificação");
    Serial.println(fbdo.errorReason().c_str());
  }
}

// Leitura e manipulação de dados da stream
void streamProcessing(){
  if (!Firebase.RTDB.readStream(&stream))
    Serial.printf("Erro na leitura da transmissão, %s\n\n", stream.errorReason().c_str());
  // verifica timeout da stream de dados
  if (stream.streamTimeout()){
    Serial.println(F("Transmissão expirou, retomando...\n"));
    if (!stream.httpConnected())
      Serial.printf("Código de erro: %d, motivo: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
  }
  // Verifica se algum dado foi alterado no RTDB
  if (stream.streamAvailable()){
    if (millis() - selfUpdateTime > SELF_UPDATE_DELAY) {
      Serial.println(F("Atualização recebida"));
      activeAlarm = stream.boolData();
      if (!activeAlarm){
        triggeredAlarm = false;
        if (!Firebase.RTDB.setBool(&fbdo, "device/" + macAddress + "/triggered", false))
          Serial.println(fbdo.errorReason().c_str());
        createLog(LOG_TYPE_DISABLED);
      } else createLog(LOG_TYPE_ACTIVATED);
      Serial.print(F("Alarme ativo: "));
      Serial.println(activeAlarm ? "true" : "false");
      buzzerResponse(activeAlarm);
    } else Serial.println(F("Dados modificados pelo próprio dispositivo e/ou spam, ignorando..."));
  }
}

bool testWifi(void) {
  Serial.println("Esperando por conexão Wi-fi");
  while (WiFi.status() != WL_CONNECTED) {
    if(digitalRead(RESET_PIN)){
      resetConfig();
    }
    delay(500);
    Serial.print("*");
  }
  return true;
}

//-------------------------- SETUP -------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(RESET_PIN, INPUT);
  pinMode(RF_PIN, INPUT_PULLDOWN);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BT_LED_PIN, OUTPUT);
  pinMode(WIFI_LED_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, HIGH);
  WiFi.begin();

  wifi_config_t wifi_config;
  esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

  const char* ssid = reinterpret_cast<const char*>(wifi_config.sta.ssid);
  if (strlen(ssid) != 0) {
    if (testWifi()) {
      Serial.println("Conectado à rede WiFi ");
      Serial.println(ssid);
      digitalWrite(WIFI_LED_PIN, HIGH);
    }
    
  } else {
    Serial.println("Não há configurações salvas de WiFi");
    BTWifiSetup();
    if (testWifi()) {
      Serial.println(F("Conectado ao Wi-fi com sucesso!"));
      WiFi.setAutoReconnect(true);
      WiFi.persistent(true);
      digitalWrite(WIFI_LED_PIN, LOW);
      digitalWrite(BT_LED_PIN, LOW);
      delay(3000);
      ESP.restart();
    }
  }
  macAddress = WiFi.macAddress();
  configTime(0, 0, ntpServer);
  firebaseConfig();
  selfUpdateTime = millis();
}

// -------------------- LOOP --------------------

void loop() {
  // Reinicia o ESP caso o firebase não esteja funcionando
  if(!Firebase.ready()) ESP.restart();
  streamProcessing();
  RFControl();

  // Verificação do estado do alarme -> sensor PIR e sirene
  if (activeAlarm) {
    if (digitalRead(PIR_PIN)) {
      if (!triggeredAlarm) {
        triggeredAlarm = true;
        digitalWrite(BUZZER_PIN, LOW);
        selfUpdateTime = millis();
        if (Firebase.RTDB.setBool(&fbdo, "device/" + macAddress + "/triggered", true)){
          createLog(LOG_TYPE_TRIGGERED);
        } else{
          Serial.println("Erro ao atualizar RTDB (triggered: false -> true)");
          Serial.println(fbdo.errorReason().c_str());
        }
        sendNotification();
      }
    }
  } else {
    if (triggeredAlarm) {
      triggeredAlarm = false;
      digitalWrite(BUZZER_PIN, HIGH);
      if (!Firebase.RTDB.setBool(&fbdo, "device/" + macAddress + "/triggered", false)){
        Serial.println("Erro ao atualizar RTDB (triggered: true -> false)");
        Serial.println(fbdo.errorReason().c_str());
      }
    }
  }
  
  resetControl();
}
