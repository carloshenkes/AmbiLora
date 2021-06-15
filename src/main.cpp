#include <SPI.h>      
#include <LoRa.h>
#include <WiFi.h>
#include "SSD1306.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>

#define MQ_analog 35 //pino analógico do sensor de gás
int gas = 0;

#define PIN_SDA 4
#define PIN_SCL 15

//Pinout! Customized for TTGO LoRa32 V2.0 Oled Board!
#define SX1278_SCK  5    // GPIO5  -- SX1278's SCK
#define SX1278_MISO 19   // GPIO19 -- SX1278's MISO
#define SX1278_MOSI 27   // GPIO27 -- SX1278's MOSI
#define SX1278_CS   18   // GPIO18 -- SX1278's CS
#define SX1278_RST  14   // GPIO14 -- SX1278's RESET
#define SX1278_DI0  26   // GPIO26 -- SX1278's IRQ(Interrupt Request)

#define OLED_ADDR   0x3c  // OLED's ADDRESS
#define OLED_SDA  4
#define OLED_SCL  15
#define OLED_RST  16

const char* ssid = "Henkes";
const char* password =  "henkes456";
const char* broker_mqtt = "192.168.1.40";
int broker_port = 1883;
WiFiClient espClient;
PubSubClient MQTT(espClient); 

#define LORA_BAND 915E6
//433E6 for Asia
//866E6 for Europe
//915E6 for North America

#define PABOOST true

// LoRaWAN Parameters
#define TXPOWER 14
#define SPREADING_FACTOR 12
#define BANDWIDTH 125000
#define CODING_RATE 5
#define PREAMBLE_LENGTH 8
#define SYNC_WORD 0x34
SSD1306 display(OLED_ADDR, OLED_SDA, OLED_SCL);

int tempRead;
int humRead;

#define LEDPIN 2
uint64_t chipid;
String MAC;

String otherValues= "";



// Este node e servidor
// 0 = Servidor internet
// 1 = Vizinho de servidor internet
// 2 = Vizinho com um Vizinho de um servidor internet 
// 3 = Isolado de todos
byte isServer = 1;
String nodeFunction[4] = {"SERVER","VIZINHO","VIZINHODO","ISOLADO"};

byte const maxTableArrayVizinhos = 32; // quantidade de vizinhos pode ser aumentada conform memoria disponivel
byte myNeighbours[maxTableArrayVizinhos] = {}; // address of vizinhos directos

byte const maxTableArrayServers = 4; // quantidade de servidores ao qual tenho acesso pode ser aumentada
byte myServers[maxTableArrayServers]     = {}; // address dos servidores que encontrei

byte localAddress = 101;     // Este sou eu!!!!! 
byte destination = 0xFF;     // broadcast send

int interval = 4000;       // intervalo entre envios
String message = "" ;//Eu sou o "+String(localAddress);    // Envia esta mensagem

byte msgCount     = 0;        // numero de mensagens enviadas
long lastSendTime = 0;        // last send time



void configForLoRaWAN(){
  LoRa.setTxPower(TXPOWER);
  LoRa.setSpreadingFactor(SPREADING_FACTOR);
  LoRa.setSignalBandwidth(BANDWIDTH);
  LoRa.setCodingRate4(CODING_RATE);
  LoRa.setPreambleLength(PREAMBLE_LENGTH);
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.crc();
}

void configWifi(){
  WiFi.begin(ssid, password);
  int tentativas_wifi = 0;
  while (tentativas_wifi < 5) {
    Serial.println("Conectando ao WiFi..");
    display.drawString(0, 10, "Conectando ao WiFi: ");
    display.drawString(0, 20,String(ssid));
    display.display();
    tentativas_wifi++;
    Serial.println(tentativas_wifi);
    delay(1000);
  }
  if(WiFi.status() == WL_CONNECTED){
    display.clear();
    display.display();
    Serial.println("Conectado ao WiFi "+String(ssid));
    display.drawString(0, 30, "Conectado ao WiFi "+String(ssid));
    display.display();
    isServer = 0; // Sou o Server!!
  }
  else{
    display.clear();
    display.display();
    Serial.println("Não Encontrou nehuma rede");
    display.drawString(0, 30, "Não conectado ao WiFi");
    display.display();
  }
}

void printScreen() {
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.setColor(BLACK);
  display.fillRect(0, 0, 127, 30);
  display.display();
  display.setColor(WHITE);
  display.drawString(0, 00, String(LORA_BAND/1000000)+" LoRa " + nodeFunction[isServer]);
  display.drawString(0, 10,"Me: " + String(localAddress) + " To: " + String(destination) + " N: " + String(msgCount));
  display.drawString(0, 20, "Tx: " + message);
  display.display();
}

void sendMessage(String outgoing, byte destination) {
  LoRa.beginPacket();
  LoRa.write(destination);
  LoRa.write(localAddress);
  LoRa.write(isServer);
  LoRa.write(msgCount);
  LoRa.write(outgoing.length());
  LoRa.print(outgoing);
  LoRa.endPacket();
  printScreen();
  
  Serial.println("Enviar Mensagem " + String(msgCount) + " para Node: "+ String(destination));
  Serial.println("Mensagem: " + message); 
  Serial.println();
  delay(1000);
  msgCount++;
}

boolean arrayIncludeElement(byte array[], byte element, byte max) {
  for (int i = 0; i < max; i++) {
    if (array[i] == element) {
      return true;
    }
  }
  return false;
}

void arrayAddElement(byte array[], byte element, byte max) {
  for (int i = 0; i < max; i++) {
    if (array[i] == 0) {
      array[i] = element;
      return;
    }
  }
}


void printVizinhos(){
  Serial.print("Vizinhos: {");
  for (int i = 0; i < sizeof(myNeighbours); i++) { 
    Serial.print(String(myNeighbours[i]));  Serial.print(" ");
  }  
  Serial.println("}"); 

  Serial.print("Servidores: {");
  for (int i = 0; i < sizeof(myServers); i++) { 
    Serial.print(String(myServers[i]));     Serial.print(" ");
  }
  Serial.println("}"); 
}

void onReceive(int packetSize) {
  // Serial.println("error");
  if (packetSize == 0) return;

  int recipient = LoRa.read();
  byte sender = LoRa.read();
  byte incomingMsgHand = LoRa.read();
  byte incomingMsgId = LoRa.read();
  byte incomingLength = LoRa.read();

  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length()) {
    Serial.println("error: message length does not match length");
    incoming = "message length error";
    return;
  }

  // Caso a Mensagem não for para mim ou para todos
  if (recipient != localAddress && recipient != 0xFF) {
    Serial.println("Esta mensagem não é para mim.");
    incoming = "message is not for me";
    message= incoming;
    printScreen();
    delay(150);
    return;
  }

  display.setColor(BLACK);
  display.fillRect(0, 32, 127, 61);
  display.display();

  display.setColor(WHITE);
  display.drawLine(0,31,127,31);
  display.drawString(0, 32, "Rx: " + incoming); // Mensagem recebida

  //Novo vizinho
  if(!arrayIncludeElement(myNeighbours,sender,maxTableArrayVizinhos)){
    arrayAddElement(myNeighbours,sender,maxTableArrayVizinhos);
    //display.drawString(0, 32, "NOVO: " + String(sender)); 
  }
  display.drawString(0, 42, "FR:"  + String(sender)
                          + " TO:" + String(recipient)
                          + " LG:" + String(incomingLength)
                          + " ID:" + String(incomingMsgId));
  display.drawString(0, 52, "RSSI: " + String(LoRa.packetRssi())
                          + " SNR: " + String(LoRa.packetSnr()));
  display.display();
  printVizinhos();

  Serial.println("Handshake: "+String(incomingMsgHand));
  Serial.println("Recebido de: 0x" + String(sender, HEX));
  Serial.println("Enviar Para: 0x" + String(recipient, HEX));
  Serial.println("Mensagem ID: " + String(incomingMsgId));
  Serial.println("Mensagem length: " + String(incomingLength));
  Serial.println("Msg: " + incoming);
  
  otherValues += incoming;
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();
  delay(1000);

  // Posicionamento dos nós na rede
  switch (incomingMsgHand) {
    case 0: // Caso Handshake for 0
      if(!arrayIncludeElement(myServers,sender,maxTableArrayServers)){
        Serial.println("Encontrei um SERVIDOR! "+sender);
        arrayAddElement(myServers,sender,maxTableArrayServers);
        display.drawString(0, 32, "NOVO: " + String(sender)); 
      }
      destination = sender;
      break;
    case 1:// Caso Handshake for 1
      if(!arrayIncludeElement(myNeighbours,sender,maxTableArrayVizinhos)){
        Serial.println("Encontrei nó VIZINHO do Server! "+sender);
        arrayAddElement(myNeighbours,sender,maxTableArrayVizinhos);
        display.drawString(0, 32, "NOVO: " + String(sender));
      }
      if(isServer!=0){
        destination = sender;
      }
      break;
    case 2: // Caso Handshake for 2
      Serial.println("Encontrei nó VIZINHO DO VIZINHO do Server!");
      break;      
    default:
      break;
  }

}

const size_t CAPACITY = JSON_ARRAY_SIZE(12);
StaticJsonDocument<CAPACITY> doc;
JsonArray array = doc.to<JsonArray>();
String Values;

void makeData(String value){
  array.add(MAC); //<- Lora MAC
  array.add(analogRead(MQ_analog));//ler gas
  array.add(value);
  // serialize the array and send the result to Serial
  // serialize the array and send the result to Serial
  serializeJson(doc, Values);
  serializeJson(doc, Serial);
  Serial.println("");
}

void init_MQTT(void)
{
    //MQTT.setServer(broker_mqtt, broker_port);
    /* Informa que todo dado que chegar do broker pelo tópico definido em MQTT_SUB_TOPIC
       Irá fazer a função mqtt_callback ser chamada (e essa, por sua vez, irá escrever
       os dados recebidos no Serial Monitor */
    //MQTT.setCallback(mqtt_callback);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String msg_broker;
    char c;
    /* Obtem a string do payload (dados) recebido */
    for(int i = 0; i < length; i++) 
    {
       c = (char)payload[i];
       msg_broker += c;
    }
    Serial.print("[MQTT] Dados recebidos do broker: ");        
    Serial.println(msg_broker);        
}

String sendTable(){
  const size_t CAPACITY = JSON_ARRAY_SIZE(4);
      StaticJsonDocument<CAPACITY> doc;
      JsonArray array = doc.to<JsonArray>();
        for (int i = 0; i < sizeof(myServers); i++) { 
            array.add(myServers[i]);
       }
      String Values;
      serializeJson(doc, Values);
      return Values;
}

void printSensor(){
  Serial.print("MQ135 = ");
  Serial.print(analogRead(MQ_analog));
  Serial.println("ppm"); 
}

void setup() {
  pinMode(OLED_RST,OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH); 
  delay(1000);

  //configura pino analog. do sensor como entrada
  pinMode(MQ_analog, INPUT);

  pinMode(LEDPIN,OUTPUT);

  display.init();
  display.flipScreenVertically();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.clear();

  Serial.begin(115200);
  while (!Serial);
  Serial.println("MESHLORA");
  display.drawString(0, 00, "CEH CO2 MeshLoRa V0.2");
  display.display();
  
  configWifi();

  LoRa.setPins(SX1278_CS, SX1278_RST, SX1278_DI0);

  configForLoRaWAN();

  if (!LoRa.begin(LORA_BAND)){
    Serial.println("Falha de inicialização do Modulo LoRa. Verifique as conexões.");
    display.drawString(0, 10, "Falha ao iniciar o LoRa");
    display.drawString(0, 20, "Cheque as conexões");
    display.display();
    while (true);                      
  }

  LoRa.receive();
  display.clear();
  display.display();
  Serial.println("MOD LoRa Iniciado com Sucesso");
  display.drawString(0, 10, "MOD LoRa Iniciado com Sucesso");
  display.drawString(0, 20, "LoRa Mesh iniciado...");
  display.display();
  delay(1500);
  display.clear();
  display.display();

  // MAC do LoRa
  chipid=ESP.getEfuseMac();
  //Serial.printf("ESP32 Chip ID = %04X",(uint16_t)(chipid>>32));//print High 2 bytes
  //Serial.printf("%08X\n",(uint32_t)chipid);//print Low 4bytes.
  MAC = String((uint16_t)(chipid>>32), HEX); 
  MAC += String((uint32_t)chipid, HEX);
  Serial.println(MAC);
}

void loop() {
  if (millis() - lastSendTime > interval) {
    
    Serial.print("Destino = ");
    Serial.println(destination);
    Serial.println(msgCount);
    
    if(msgCount>10){
      message = sendTable();
      sendMessage(message, 255);
      otherValues="";
      msgCount = 0;
    }
    else{
      message = Values;
      if (isServer==0){
        digitalWrite(LEDPIN, HIGH);
        otherValues += Values;
        Serial.print("Server otherVal"+otherValues);
      }
      else{
        digitalWrite(LEDPIN, LOW);
        destination = myNeighbours[0];
        sendMessage(message, destination);
        printSensor();
      }
    }
    msgCount++;
    lastSendTime = millis();
    interval = random(interval) + 20000;
    LoRa.receive();
    makeData(String(lastSendTime));
  }
  
  int packetSize = LoRa.parsePacket();
  
  if (packetSize){
    onReceive(packetSize);
  }

}