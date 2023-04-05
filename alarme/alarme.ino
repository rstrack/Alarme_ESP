#include <NTPClient.h>

#include <WifiConfig.h>

#if defined(ESP32) || defined(PICO_RP2040)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include "FS.h"

#include <WiFiUdp.h>

#include <Firebase_ESP_Client.h>

#include <addons/TokenHelper.h>

//lib com dados sensíveis
#include <WifiConfig.h>

#define DATABASE_URL "https://dgr-alarmes-default-rtdb.firebaseio.com/"

#define INI_FILE "/net.ini"

//tipos do objeto log:
#define LOG_TYPE_ACTIVATED 0
#define LOG_TYPE_DISABLED 1
#define LOG_TYPE_TRIGGERED 2

//delay para prevenção de leituras duplicadas, spam de clicks e etc
#define SELF_UPDATE_DELAY 3000

//intervalo de tempo para geração de novo token do firebase
#define REFRESH_TOKEN_INTERVAL (30*60*1000)

//TESTE
unsigned int timer;

FirebaseData fbdo;
FirebaseData stream;

FirebaseAuth auth;
FirebaseConfig config;

FirebaseJson json;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String macAddress;

bool activeAlarm;
bool triggeredAlarm = false;

//----- alarm vars -----
int freq = 500;
int _time = 10;
bool up = true;

unsigned int selfUpdateTime;
unsigned int alarmTime;

// -------------------- SETUP --------------------

void setup(){
  Serial.begin(115200);

  pinMode(5, INPUT);
  pinMode(4, OUTPUT);

  if (!SPIFFS.begin())
    while (1)
      Serial.println("SPIFFS.begin() falhou");

  macAddress = WiFi.macAddress();

  File file = iniFile();

  String buffer = file.readString();

  String ssid = buffer.substring(0,buffer.indexOf('\n'));
  String passphrase = buffer.substring(buffer.indexOf('\n')+1);
  
  file.close();
  WiFi.begin(ssid, passphrase);

  if (testWifi())
  {
    Serial.println("Conectado ao Wi-fi com sucesso!");
  }
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  timeClient.begin();
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }

  firebaseConfig();

  if (!Firebase.RTDB.beginStream(&stream, "/device/"+macAddress+""))
    Serial.printf("stream begin error, %s\n\n", stream.errorReason().c_str());

  if(Firebase.RTDB.getBool(&fbdo, "/device/"+macAddress+"/active"))
    activeAlarm = fbdo.to<bool>();
  else activeAlarm = false;

  if(!Firebase.RTDB.setBool(&fbdo, "/device/"+macAddress+"/triggered", false))
    Serial.println(fbdo.errorReason().c_str());
  
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeoutCallback);
  selfUpdateTime = millis();
  timer = millis();
}

// -------------------- LOOP --------------------

void loop(){
  Firebase.ready();
  if(millis()-timer > 5000){
    Serial.println(ESP.getFreeHeap());
    timer = millis();
  }
  //lógica só é aceita se o alarme estiver ligado
  if(activeAlarm){
    //se alarme estiver disparado, emite som
    if(triggeredAlarm){
      alarm();  
    }
    //verifica sensor de proximidade e dispara o alarme
    if(digitalRead(5)){
      if(!triggeredAlarm){
        triggeredAlarm = true;
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
}

// -------------------- FUNÇÕES --------------------
File iniFile(){
  File file = SPIFFS.open(INI_FILE, "r");
  if (!file) {
    Serial.print("Arquivo ");
    Serial.print(INI_FILE);
    Serial.print(" não existe");
    Serial.println("Criando novo arquivo...");

    File file = SPIFFS.open(INI_FILE, "w");
    //hard code
    int bytesWritten = file.print(String(WIFI_SSID) + "\n" + String(WIFI_PASSWORD));
    if (bytesWritten > 0) {
      Serial.println("Arquivo foi escrito com sucesso");
      Serial.println(bytesWritten);
   
    } else {
      Serial.println("Falha ao escrever arquivo");
    }
 
    file.close();
    file = SPIFFS.open(INI_FILE, "r");
    }
  return file;
}

bool testWifi(void)
{
  int c = 0;
  Serial.println("Esperando por conexão Wi-fi");
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED)
    {
      return true;
    }
    delay(500);
    Serial.print("*");
    c++;
  }
  Serial.println("");
  Serial.println("Tempo para conexão esgotado");
  return false;
}

void firebaseConfig(){
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;

  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  config.signer.preRefreshSeconds = 45 * 60;

  #if defined(ESP8266)
    // In ESP8266 required for BearSSL rx/tx buffer for large data handle, increase Rx size as needed.
    fbdo.setBSSLBufferSize(2048 /* Rx buffer size in bytes from 512 - 16384 */, 2048 /* Tx buffer size in bytes from 512 - 16384 */);
    stream.setBSSLBufferSize(2048 /* Rx in bytes, 512 - 16384 */, 512 /* Tx in bytes, 512 - 16384 */);
  #endif

  // Limit the size of response payload to be collected in FirebaseData
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);

  Firebase.reconnectWiFi(true);

  String path = "device/"+macAddress+"";
  //String mask = WiFi.macAddress();
  //String mask = "teste";

  if (Firebase.ready()){
    if (!Firebase.RTDB.get(&fbdo, path.c_str())){
      Serial.println("Dispositivo não encontrado no firebase, criando documento...");
      json.clear();
      json.add("active", false);
      json.add("triggered", false);
      if (Firebase.RTDB.set(&fbdo, path, &json))
        Serial.printf("Dispositivo Criado\n%s\n", fbdo.payload().c_str());
      else
        Serial.println(fbdo.errorReason());
      }
    else
      Serial.println("Dispositivo ja existente no Firebase");
  }
  else{
    Serial.println("Problema na conexão com Firebase.");  
  }
}

void streamCallback(FirebaseStream data){
  if (data.dataType() == "null") {
    Serial.println("Sem dados disponíveis no callback da stream de dados");
  }
  else{
    if(millis() - selfUpdateTime > SELF_UPDATE_DELAY){
      Serial.println("Atualização recebida");
      Serial.println(data.dataPath());
      if(data.dataPath()=="/active"){
        activeAlarm = !activeAlarm;
        
        if(activeAlarm)
          createLog(LOG_TYPE_ACTIVATED);
        else createLog(LOG_TYPE_DISABLED);
        Serial.print("Alarme ativo: ");
        Serial.println(activeAlarm);
      }else if (data.dataPath()=="/triggered"){
        triggeredAlarm = !triggeredAlarm;
        Serial.print("Alarme disparado: ");
        Serial.println(triggeredAlarm);
        freq=500;
        up = true;
      }
    }else Serial.println("Dados modificados pelo próprio dispositivo e/ou spam, ignorando...");
  }
}

void streamTimeoutCallback(bool timeout)
{
  if (timeout)
    Serial.println("stream timed out, resuming...\n");

  if (!stream.httpConnected())
    Serial.printf("error code: %d, reason: %s\n\n", stream.httpCode(), stream.errorReason().c_str());
}

void alarm(){
  if(millis()-alarmTime< 5*60*1000){
    if (up && freq < 1800){
      tone(4, freq, _time);
      delay(_time);
      freq = freq + 100;
    }
    else if(up && freq == 1800){
      up = false;
      tone(4, freq, _time);
      delay(_time);
      freq = freq - 100;
    }
    else if (!up && freq==500){
      up = true;
      tone(4, freq, _time);
      delay(_time);
      freq = freq + 100;
    }
    else{
      tone(4, freq, _time);
      delay(_time);
      freq = freq - 100;
    }
  }
  else{
    triggeredAlarm = false;
    selfUpdateTime = millis();
    if(!Firebase.RTDB.setBool(&fbdo, "device/"+macAddress+"/triggered", false))
        Serial.println(fbdo.errorReason().c_str());
  }
  
}

void createLog(int TYPE){
  time_t now = timeClient.getEpochTime();
  json.clear();
  json.add("time", now);
  json.add("type", TYPE);
  if (Firebase.RTDB.pushJSON(&fbdo, "log/"+macAddress+"", &json)) {
    Serial.print("Registro adicionado: tipo ");
    Serial.println(TYPE);
  } else {
    Serial.println("Error adding log");
    Serial.println(fbdo.errorReason().c_str());
  }
}
