#include <SPI.h>      
#include <LoRa.h>
#include <WiFi.h>
#include "SSD1306.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>

#define MQ_analog 13 
int gas = 0;

#define PIN_SDA 4
#define PIN_SCL 15

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
//433E6 Asia
//866E6 Europe
//915E6 North America

#define PABOOST true

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
byte isServer = 1;
String nodeFunction[4] = {"SERVER","VIZINHO","VIZINHODO","ISOLADO"};

byte const maxTableArrayVizinhos = 32; 
byte meusVizinhos[maxTableArrayVizinhos] = {}; 

byte const maxTableArrayServers = 4;
byte meusServidores[maxTableArrayServers] = {}; 

byte enderecoLocal = 110;
byte destino = 0xFF;

int intervalo = 4000;

String mensagem = "ola" ;

byte msgCount     = 0;
long lastSendTime = 0;

const size_t CAPACITY = JSON_OBJECT_SIZE(1)+ JSON_ARRAY_SIZE(2)+20;
StaticJsonDocument<CAPACITY> doc;
//JsonArray array = doc.to<JsonArray>();
String mensagensRecebidas;

void configForLoRaWAN(){
  LoRa.setTxPower(TXPOWER);
  LoRa.setSpreadingFactor(SPREADING_FACTOR);
  LoRa.setSignalBandwidth(BANDWIDTH);
  LoRa.setCodingRate4(CODING_RATE);
  LoRa.setPreambleLength(PREAMBLE_LENGTH);
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.crc();
}

void configWifi(int tentativas){
  WiFi.begin(ssid, password);
  int tentativas_wifi = 0;
  while (tentativas_wifi < tentativas) {
    Serial.println("Conectando ao WiFi..");
    display.drawString(0, 10, "Conectando ao WiFi: ");
    display.drawString(0, 20,String(ssid));
    display.display();
    tentativas_wifi++;
    Serial.print(" "+tentativas_wifi);
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
  display.drawString(0, 10,"Me: " + String(enderecoLocal) + " To: " + String(destino) + " N: " + String(msgCount));
  display.drawString(0, 20, "Tx: " + mensagem);
  display.display();
}

void sendMessage(String mensagem, byte destino) {
  LoRa.beginPacket();
  LoRa.write(destino);
  LoRa.write(enderecoLocal);
  LoRa.write(isServer);
  LoRa.write(msgCount);
  LoRa.write(mensagem.length());
  LoRa.print(mensagem);
  LoRa.endPacket();
  printScreen();
  Serial.print("Enviar Mensagem " + msgCount);
  Serial.println(" para Node: "+ destino);
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
  for (int i = 0; i < sizeof(meusVizinhos); i++) { 
    Serial.print(String(meusVizinhos[i]));  Serial.print(" ");
  }  
  Serial.println("}"); 

  Serial.print("Servidores: {");
  for (int i = 0; i < sizeof(meusServidores); i++) { 
    Serial.print(String(meusServidores[i]));     Serial.print(" ");
  }
  Serial.println("}"); 
}

void onReceive(int packetSize) {
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

  // Caso a Mensagem não for para mim ou para todos
  if (recipient != enderecoLocal && recipient != 0xFF) {
    Serial.println("Esta mensagem não é para mim."+ recipient);
    incoming = "mensagem n e para mim";
    mensagem= incoming;
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
  if(!arrayIncludeElement(meusVizinhos,sender,maxTableArrayVizinhos)){
    arrayAddElement(meusVizinhos,sender,maxTableArrayVizinhos);
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
  Serial.println("Conteudo: " + incoming);
  
  otherValues += incoming;
  mensagensRecebidas += incoming;
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();
  delay(1000);

  // Posicionamento dos nós na rede
  switch (incomingMsgHand) {
    case 0: // Caso Handshake for 0
      if(!arrayIncludeElement(meusServidores,sender,maxTableArrayServers)){
        Serial.println("Encontrei um SERVIDOR! "+sender);
        arrayAddElement(meusServidores,sender,maxTableArrayServers);
        display.drawString(0, 32, "NOVO: " + String(sender)); 
      }
      destino = sender;
      break;
    case 1:// Caso Handshake for 1
      if(!arrayIncludeElement(meusVizinhos,sender,maxTableArrayVizinhos)){
        Serial.println("Encontrei nó VIZINHO do Server! "+sender);
        arrayAddElement(meusVizinhos,sender,maxTableArrayVizinhos);
        display.drawString(0, 32, "NOVO: " + String(sender));
      }
      if(isServer!=0){
        destino = sender;
      }
      break;
    case 2: // Caso Handshake for 2
      Serial.println("Encontrei nó VIZINHO DO VIZINHO do Server!");
      break;      
    default:
      break;
  }

}

int gasReader(){
  return analogRead(MQ_analog);
}

void makeData(long value){
  if(isServer!=0){
    doc["node"]=enderecoLocal;
    doc["sensor"]=gasReader();
    doc["uptime"]=value;
    serializeJson(doc, mensagensRecebidas);
    Serial.println(" JSON ");
    serializeJson(doc, Serial);
    Serial.println(" ");
  }
  
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String msg_broker;
    char c;
    /* Obtem a string do payload (dados) recebido */
    for(int i = 0; i < length; i++) {
       c = (char)payload[i];
       msg_broker += c;
    }
    Serial.print("[MQTT] Dados recebidos do broker: ");        
    Serial.println(msg_broker);        
}

void init_MQTT(int tentativas){
  MQTT.setServer(broker_mqtt, broker_port);
  int tentConn = 0;
  while (tentConn<tentativas){
    Serial.println("Conectando ao broker MQTT...");
    if (MQTT.connect("ESP32Client")){
      Serial.println("Conectado ao broker!");
    }
    else{
      Serial.print("Falha na conexao ao broker - Estado: ");
      Serial.print(MQTT.state());
      delay(2000);
    }
    tentConn++;
  }
}

String sendTable(){
  const size_t CAPACITY = JSON_ARRAY_SIZE(4);
      StaticJsonDocument<CAPACITY> doc;
      JsonArray array = doc.to<JsonArray>();
        for (int i = 0; i < sizeof(meusServidores); i++) { 
            array.add(meusServidores[i]);
       }
      String Values;
      serializeJson(doc, Values);
      return Values;
}

void printSensor(){
  Serial.print("MQ135 = ");
  Serial.print(gasReader());
  Serial.println(" ppm"); 
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
  display.drawString(0, 00, "CEH CO2 MeshLoRa V0.3");
  display.display();
  
  //configWifi(4);

  if(isServer==0){
    init_MQTT(5);
  }

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

  chipid=ESP.getEfuseMac();
  //Serial.printf("ESP32 Chip ID = %04X",(uint16_t)(chipid>>32));//print High 2 bytes
  //Serial.printf("%08X\n",(uint32_t)chipid);//print Low 4bytes.
  MAC = String((uint16_t)(chipid>>32), HEX); 
  MAC += String((uint32_t)chipid, HEX);
  Serial.println(MAC);
  char saidaMQTT[15];
  MQTT.publish("loramesh",saidaMQTT);
  MAC.toCharArray(saidaMQTT,15);
  MQTT.publish("loramesh",saidaMQTT);
  
}

void loop() {

  if (millis() - lastSendTime > intervalo) {
    
    
    
    if(msgCount>10){
      mensagem = sendTable(); // envia tabela de servers
      sendMessage(mensagem, 255);
      if (isServer==0){
        char saidaMQTT[400];
        mensagensRecebidas.toCharArray(saidaMQTT,400);
        MQTT.publish("loramesh",saidaMQTT);
      }
      otherValues="";
      mensagensRecebidas="";
      msgCount = 0;
    
    }
    else{
      
      mensagem = mensagensRecebidas;

      if (isServer==0){
        Serial.print("Dest "+destino);
        Serial.println("Env"+msgCount);
        sendMessage("ola", 255);
        digitalWrite(LEDPIN, HIGH);
        otherValues += mensagensRecebidas;
        otherValues = "";
      }
      else{
        digitalWrite(LEDPIN, LOW);
        if(isServer==1){//se for vizinho direto encaminho para o servidor
          if(mensagem != "ola"){
            destino = meusServidores[0];
            Serial.println("Destino: "+destino);
            sendMessage(mensagem, destino);
          }
        }
        else{
          if(mensagem != "ola"){
            destino = meusVizinhos[0];
            sendMessage(mensagem, destino);
            Serial.println("Destino: "+destino);
          }
        }
        //printSensor();
        makeData(lastSendTime);
      }
    }
    
    msgCount++;
    lastSendTime = millis();
    intervalo = random(intervalo) + 20000;
    LoRa.receive();
    
  }
  
  int packetSize = LoRa.parsePacket();
  
  if (packetSize){
    onReceive(packetSize);
  }

}