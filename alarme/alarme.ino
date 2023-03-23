#if defined(ESP32) || defined(PICO_RP2040)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include "FS.h"

#include <Firebase_ESP_Client.h>

#include <addons/TokenHelper.h>

#define API_KEY "AIzaSyCr1m4GZ9pY9oAF-FDEOkKEfJjc2ONydOM"
#define FIREBASE_PROJECT_ID "dgr-alarmes"

#define USER_EMAIL "root@root.com"
#define USER_PASSWORD "root123"

#define INI_FILE "/net.ini"

FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

String macAddress;

// -------------------- SETUP --------------------

void setup(){
  Serial.begin(115200);

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

  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  #if defined(ESP8266)
    // In ESP8266 required for BearSSL rx/tx buffer for large data handle, increase Rx size as needed.
    fbdo.setBSSLBufferSize(2048 /* Rx buffer size in bytes from 512 - 16384 */, 2048 /* Tx buffer size in bytes from 512 - 16384 */);
  #endif

    // Limit the size of response payload to be collected in FirebaseData
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);

  Firebase.reconnectWiFi(true);

  String documentPath = "device/"+macAddress+"";
  //String mask = WiFi.macAddress();
  //String mask = "teste";

  if (Firebase.ready()){
    if (!Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())){
      Serial.println("Dispositivo não encontrado no firebase, criando documento...");
      FirebaseJson content;
      String documentPath = "device/"+macAddress+"";
      content.set("fields/active/booleanValue", false);
      content.set("fields/triggered/booleanValue", false);
      if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw()))
        Serial.printf("Dispositivo Criado\n%s\n", fbdo.payload().c_str());
      else
        Serial.println(fbdo.errorReason());
      }
    else
      Serial.println(fbdo.errorReason());
  }
  else{
    Serial.println("Problema na conexão com Firebase.");  
  }
}

// -------------------- LOOP --------------------

void loop(){

}

// -------------------- FUNCTIONS --------------------

File iniFile(){
  File file = SPIFFS.open(INI_FILE, "r");
  if (!file) {
    Serial.print("Arquivo ");
    Serial.print(INI_FILE);
    Serial.print(" não existe");
    Serial.println("Criando novo arquivo...");

    File file = SPIFFS.open(INI_FILE, "w");
    //hard code
    int bytesWritten = file.print("ALHN-D2D9 2.4G\nPwbDv7N=ay");
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
