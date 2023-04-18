#include <time.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <WifiConfig.h>
#include <BluetoothSerial.h>

#define DATABASE_URL "https://dgr-alarmes-default-rtdb.firebaseio.com/"
#define LOG_TYPE_ACTIVATED 0
#define LOG_TYPE_DISABLED 1
#define LOG_TYPE_TRIGGERED 2

#define PIR_PIN 2
#define BUZZER_PIN 4
#define RESET_PIN 23

#define RESET_DELAY 3000
#define SELF_UPDATE_DELAY 3000

FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbdo;
FirebaseData stream;
FirebaseJson json;

String macAddress;
unsigned int selfUpdateTime;
unsigned int alarmTime;
unsigned int resetTime;
bool resetButtonPressed = false;

bool activeAlarm;
bool triggeredAlarm = false;

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
    }
    Firebase.RTDB.beginStream(&stream, "/device/"+macAddress+"");
    if(Firebase.RTDB.getBool(&fbdo, "/device/"+macAddress+"/active")){
      activeAlarm = fbdo.to<bool>();
    }
    else activeAlarm = false;
    if(!Firebase.RTDB.setBool(&fbdo, "/device/"+macAddress+"/triggered", false)){
      Serial.println(fbdo.errorReason().c_str());
    }
    Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);
  }
  else{
    Serial.println(F("Problema na conexão com Firebase."));  
  }
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
        if(!triggeredAlarm) noTone(BUZZER_PIN);
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

void BTWifiSetup(){
  BluetoothSerial SerialBT; 
  SerialBT.begin("DGR-"+random(9999));
  Serial.println(F("Esperando credenciais Wi-Fi via Bluetooth..."));
  while(1){
    Serial.print(F("."));
    if (SerialBT.available()>0){
      String data = SerialBT.readString();
      Serial.println(data);
      return
    }
  }  
}

void alarm(){
  if(millis()-alarmTime< 5*60*1000){
    tone(BUZZER_PIN, 1000);
  }
  else{
    triggeredAlarm = false;
    noTone(BUZZER_PIN);
    selfUpdateTime = millis();
    if(!Firebase.RTDB.setBool(&fbdo, "device/"+macAddress+"/triggered", false))
        Serial.println(fbdo.errorReason().c_str());
  }
}

void createLog(int TYPE){
  time_t now = time(NULL);
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

  ESP.restart();  
}

//-------------------------- SETUP -------------------------
void setup(){
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
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
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.
    // BTWifiSetup();
    if (testWifi()) Serial.println(F("Conectado ao Wi-fi com sucesso!"));
  }
  macAddress = WiFi.macAddress();
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);  
  firebaseConfig();
  selfUpdateTime = millis();
}

// -------------------- LOOP --------------------

void loop(){
  Firebase.ready();
  //lógica só é aceita se o alarme estiver ligado

  if(activeAlarm){
    //verifica sensor de proximidade e dispara o alarme
    if(digitalRead(PIR_PIN)){
      if(!triggeredAlarm){
        triggeredAlarm = true;
        alarm();
        selfUpdateTime = millis();
        if (Firebase.RTDB.setBool(&fbdo, "device/"+macAddress+"/triggered", true))
          createLog(LOG_TYPE_TRIGGERED);
        else
          Serial.println(fbdo.errorReason().c_str());
        alarmTime = millis();
      }
    }
  }else{
    if(triggeredAlarm){
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
