#include <BluetoothSerial.h>
#include <Firebase_ESP_Client.h>
#include <Secrets.h>
#include <time.h>
#include <WiFi.h>
#include <addons/TokenHelper.h>

#define DATABASE_URL "https://dgr-alarmes-default-rtdb.firebaseio.com/"
#define LOG_TYPE_ACTIVATED 0
#define LOG_TYPE_DISABLED 1
#define LOG_TYPE_TRIGGERED 2

#define PIR_PIN 13
#define BUZZER_PIN 14
#define RF_PIN 27
#define RESET_PIN 33

#define RESET_DELAY 3000
#define SELF_UPDATE_DELAY 3000

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

bool testWifi(void)
{
  Serial.println(F("Esperando por conexão Wi-fi"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("*")); 
  }
  return true;
}

void firebaseConfig(){
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h
  config.signer.preRefreshSeconds = 45 * 60;
  fbdo.setResponseSize(1024);
  stream.setResponseSize(1024);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  char path[32];
  sprintf(path, "device/%s", macAddress.c_str());
  if (Firebase.ready()){
    if (!Firebase.RTDB.get(&fbdo, path)){
      Serial.println(F("Dispositivo não encontrado no firebase, criando documento..."));
      json.clear();
      json.add("active", false);
      json.add("triggered", false);
      if (Firebase.RTDB.set(&fbdo, path, &json)){
        Serial.printf("Dispositivo Criado\n");
        Serial.println(fbdo.payload().c_str());
      }
      else{
        Serial.println(fbdo.errorReason());
      }
    }
    else{
      Serial.println(F("Dispositivo ja existente no Firebase"));
      if(Firebase.RTDB.getBool(&fbdo, "/device/"+macAddress+"/active")){
        activeAlarm = fbdo.to<bool>();
      }
      if(!Firebase.RTDB.setBool(&fbdo, "/device/"+macAddress+"/triggered", false)){
        Serial.println(fbdo.errorReason().c_str());
      }
    }
    Firebase.RTDB.beginStream(&stream, "/device/"+macAddress+"");
    Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);
  }
  else{
    Serial.println(F("Problema na conexão com Firebase."));  
  }
  Firebase.FCM.setServerKey(FCM_SERVER_KEY);
}

void streamCallback(FirebaseStream data){
  if (data.dataType() == "null") {
    Serial.println(F("Sem dados disponíveis no callback da stream de dados"));
  }
  else{
    if(millis() - selfUpdateTime > SELF_UPDATE_DELAY){
      Serial.println(F("Atualização recebida"));
      Serial.println(data.dataPath());
      if(data.dataPath().equals("/active")){
        activeAlarm = data.boolData();
        if(activeAlarm)
          createLog(LOG_TYPE_ACTIVATED);
        else createLog(LOG_TYPE_DISABLED);
        Serial.print(F("Alarme ativo: "));
        Serial.println(activeAlarm);
      }else if (data.dataPath().equals("/triggered")){
        triggeredAlarm = data.boolData();
        Serial.print(F("Alarme disparado: "));
        Serial.println(triggeredAlarm);
        if(!triggeredAlarm) digitalWrite(BUZZER_PIN, HIGH);
      }
    }else Serial.println(F("Dados modificados pelo próprio dispositivo e/ou spam, ignorando..."));
  }
}

void streamTimeoutCallback(bool timeout){
  if (timeout)
    Serial.println(F("stream timed out, resuming...\n"));

  if (!stream.httpConnected()){
    Serial.print("error code: ");
    Serial.print(String(stream.httpCode()));
    Serial.print(", reason: ");
    Serial.println(stream.errorReason().c_str());
  }
}

void sendNotification(){
  FCM_Legacy_HTTP_Message msg;
  String topic = "/topics/";
  String topic2 = macAddress;
  topic2.replace(":", "");
  msg.targets.to = topic + "" + topic2;
  msg.options.content_available = "application/json";
  msg.options.time_to_live = "10";
  msg.options.priority = "high";
  msg.payloads.notification.title = "AVISO";
  msg.payloads.notification.body = "Alarme Disparado";
  if(!Firebase.FCM.send(&fbdo, &msg)){
    Serial.println(fbdo.errorReason().c_str());
  }
}

void BTWifiSetup(){
  BluetoothSerial SerialBT;
  String deviceName = "DGR-"+String(random(9999));
  SerialBT.begin(deviceName);
  Serial.println(F("Esperando credenciais Wi-Fi via Bluetooth..."));
  while(1){
    if (SerialBT.available()){
      String data = SerialBT.readString();
      Serial.println(data);
      WiFi.begin(data.substring(0,data.indexOf('\n')).c_str(), data.substring(data.indexOf('\n')+1).c_str());
      return;
    }
  }  
}

void createLog(int TYPE){
  time_t now = getTime();
  json.clear();
  json.add("time", now);
  json.add("type", TYPE);
  if (Firebase.RTDB.pushJSON(&fbdo, "log/"+macAddress+"", &json)) {
    Serial.print(F("Registro adicionado: tipo "));
    Serial.println(TYPE);
  } else {
    Serial.println(F("Error adding log"));
    Serial.println(fbdo.errorReason().c_str());
  }
}

void resetConfig(){
  WiFi.disconnect();
  delay(3000);
  ESP.restart();  
}

unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return(0);
  }
  time(&now);
  return now;
}

//-------------------------- SETUP -------------------------
void setup(){
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);
  pinMode(RESET_PIN, INPUT);

  WiFi.begin();

  wifi_config_t wifi_config;
  esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

  const char* ssid = reinterpret_cast<const char*>(wifi_config.sta.ssid);
  if (strlen(ssid) != 0) {
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
    }
    Serial.print("Conectado à rede WiFi ");
    Serial.println(ssid);
  } else{
    Serial.println("Não há configurações salvas de WiFi");
    BTWifiSetup();
    if (testWifi()) Serial.println(F("Conectado ao Wi-fi com sucesso!"));
  }
  macAddress = WiFi.macAddress();
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  configTime(0, 0, ntpServer);
  firebaseConfig();
  selfUpdateTime = millis();
}

// -------------------- LOOP --------------------

void loop(){
  Firebase.ready();

  //controle do controle RF
  if(digitalRead(RF_PIN) && millis() > lastControlClickTime + 1000){
    lastControlClickTime = millis();
    if(!activeAlarm){
      activeAlarm = true;
      selfUpdateTime = millis();
      Firebase.RTDB.setBool(&fbdo, "device/"+macAddress+"/active", true);
    }else{
      activeAlarm = false;
      selfUpdateTime = millis();
      Firebase.RTDB.setBool(&fbdo, "device/"+macAddress+"/active", false);
    }
  }

  //lógica só é aceita se o alarme estiver ligado
  if(activeAlarm){
    //verifica sensor de proximidade e dispara o alarme
    if(digitalRead(PIR_PIN)){
      if(!triggeredAlarm){
        triggeredAlarm = true;
        digitalWrite(BUZZER_PIN, LOW);
        selfUpdateTime = millis();
        if (Firebase.RTDB.setBool(&fbdo, "device/"+macAddress+"/triggered", true))
          createLog(LOG_TYPE_TRIGGERED);
        else
          Serial.println(fbdo.errorReason().c_str());
        sendNotification();
      }
    }
  }else{
    if(triggeredAlarm){
      digitalWrite(BUZZER_PIN, HIGH);
      if(!Firebase.RTDB.setBool(&fbdo, "device/"+macAddress+"/triggered", false))
        Serial.println(fbdo.errorReason().c_str());
    }
  }  
  // if(digitalRead(RESET_PIN) && !resetButtonPressed){
  //   resetTime = millis();
  //   resetButtonPressed = true;
  // }
  // if(!digitalRead(RESET_PIN) && resetButtonPressed){
  //   resetButtonPressed = false;
  // }
  // if(digitalRead(RESET_PIN) && resetButtonPressed && resetTime>=RESET_DELAY){
  //   resetConfig();    
  // }
}
