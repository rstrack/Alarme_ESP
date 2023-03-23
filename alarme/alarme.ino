#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include "time.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define pinoPir 0
#define pinoPiezo 2
#define pinoBotao 4


//VERIFICAR VARIAVEIS WI-FI
const char* ssid = "VIVOFIBRA-1848"; //MUDAR SSID E SENHA
const char* senha = "emotionalDamage";

//Enter Firebase web API Key
#define API_KEY "AIzaSyCZF489PCdctl1v_B96gW777jKdhkqysNk" //OCULTAR SE FOR PUBLICO, criar um secrets.h

// Enter Authorized Email and Password
#define USER_EMAIL "projetohardware2023@gmail.com"
#define USER_PASSWORD "clone12" //OCULTAR

// Enter Realtime Database URL
#define DATABASE_URL "projeto-hardware-646d2-default-rtdb.firebaseio.com"

FirebaseData Firebase_dataObject;
FirebaseAuth authentication;
FirebaseConfig config;

String UID;

// Database main path 
String database_path;

String controle_path = "/controle";
String horario_path = "/horario";

//Updated in every loop
String parent_path;

int t_horario;

FirebaseJson json;

const char* ntpServer = "pool.ntp.org";


//var globais
int alarmeTocando = 0;
int leituraPir = 0;
int alarmeAcionado = 0; //SIMULA UM DADO QUE VEM DO BANCO
int comando = 0; //SIMULA UM DADO QUE VEM DO BANCO
float tempoLigado;

//pooling de 30s
unsigned long previous_time = 0;
unsigned long Delay = 30000;

void setup() {

  Serial.begin(115200);
  pinMode(pinoPir, INPUT);
  pinMode(pinoPiezo, OUTPUT);
  pinMode(pinoBotao, INPUT);
  
  setup_wifi();
  configurarFirebase();
  buscarUID();
  database_path = "/Dados/" + UID + "/Leituras_Alarme";
}

void loop() {
  
  if (Firebase.ready() && (millis() - previous_time > Delay || previous_time == 0)){
    parent_path= database_path + "/" + String(t_horario);
    enviarComando(); 
  }

}
void setup_wifi() {

  delay(1000);
  Serial.println("");
  Serial.print("Conectando com ");
  Serial.println(ssid);

  WiFi.begin(ssid, senha);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.println("IP: ");
  Serial.println(WiFi.localIP());
}

void enviarComando(){
  //CRIAR NO FLUTTER UM ALARME_ACIONADO E COMANDO_ALARME PRA FUNCIONAR
  if(Firebase.getInt("ALARME_ACIONADO") == 1){//RECEBER UM INTEIRO QUE ACIONA O ALARME
    if (leituraPir == HIGH){            
      digitalWrite(pinoPiezo, HIGH);
      leituraPir = digitalRead(pinoPir); 
      t_horario = PegarHorario();
      Serial.print ("horario: ");
      Serial.println (t_horario);
      //json.set(controle_path.c_str(), String(bme.readTemperature()));
      json.set(horario_path, String(t_horario));
      Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&Firebase_dataObject, parent_path.c_str(), &json) ? "ok" : Firebase_dataObject.errorReason().c_str());  
      
      if (alarmeTocando == LOW) 
      {
        Serial.println("Movimento detectado!"); 
        alarmeTocando = HIGH;
      }
    } 
    else if(Firebase.getInt("COMANDO_ALARME") == 1 || (digitalRead(botaoPin) == 1))//SE CLICOU PARA DESLIGAR VIA APP OU CLICOU BOTAO NO MICROCONTROLADOR
    {
      digitalWrite(pinoPiezo, LOW); // turn LED OFF

      if (alarmeTocando == HIGH)
      {
        Serial.println("Acabou movimento!");  // print on output change
        alarmeTocando = LOW;
      }
    } 
  }else{
    digitalWrite(pinoPir, 0);
  } 
}

//horario
unsigned long PegarHorario() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return(0);
  }
  time(&now);
  return now;
}

void buscarUID(){

  Serial.println("Pegando UID do usuario...");
  while ((authentication.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  UID = authentication.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(UID);
}

void configurarFirebase(){
  configTime(0, 0, ntpServer);

  config.api_key = API_KEY;
  authentication.user.email = USER_EMAIL;
  authentication.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;

  config.token_status_callback = tokenStatusCallback; 
  config.max_token_generation_retry = 5;
  Firebase.begin(&config, &authentication);
}
