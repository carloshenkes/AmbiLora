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


#define LEDPIN 25
uint64_t chipid;


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

String mensagensRecebidas;
String mensagensEnviar;

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
    display.drawString(0, 20,ssid);
    display.display();
    tentativas_wifi++;
    if(WiFi.status() == WL_CONNECTED){
      display.clear();
      display.display();
      display.drawString(0, 30, "Conectado ao WiFi "+String(ssid));
      display.display();
      isServer = 0; // Sou o Server!!
      return;
    }
    delay(1000);
  }
  if(WiFi.status() != WL_CONNECTED){
    display.clear();
    display.display();
    Serial.println("Não Encontrou nehuma rede");
    display.drawString(0, 30, "Não conectado ao WiFi");
    display.display();
  }
}

void init_MQTT(int tentativas,String msgEnvia){
  MQTT.setServer("192.168.1.40", 1883);
  int tentConn = 0;
  while (tentConn<tentativas){
    if (MQTT.connect("ESP32Client")){
      char saidaMQTT[255];
      msgEnvia.toCharArray(saidaMQTT,255);
      MQTT.publish("loramesh",saidaMQTT);
      return;
    }
    else{
      Serial.print("Falha na conexao ao broker - Estado: ");
      Serial.print(MQTT.state());
      delay(2000);
    }
    delay(1000);
    tentConn++;
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
  Serial.println("Para Node:"+ destino);
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
    Serial.print(meusVizinhos[i]);  
    Serial.print(" ");
  }  
  Serial.println("}"); 

  Serial.print("Servidores: {");
  for (int i = 0; i < sizeof(meusServidores); i++) { 
    Serial.print(meusServidores[i]);     
    Serial.print(" ");
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
    incoming = "mensagem n e para mim";
    mensagem= incoming;
    printScreen();
    delay(150);
    return;
  }

  if(incoming.isEmpty()){
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
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();
  delay(200);

  // Posicionamento dos nós na rede
  switch (incomingMsgHand) {
    case 0:
      if(!arrayIncludeElement(meusServidores,sender,maxTableArrayServers)){
        Serial.println("SERVIDOR"+sender);
        arrayAddElement(meusServidores,sender,maxTableArrayServers);
        display.drawString(0, 32, "NOVO: " + sender); 
      }
      destino = sender;
      break;
    case 1:
      if(!arrayIncludeElement(meusVizinhos,sender,maxTableArrayVizinhos)){
        Serial.println("VIZINHO"+sender);
        arrayAddElement(meusVizinhos,sender,maxTableArrayVizinhos);
        display.drawString(0, 32, "NOVO: " + sender);
      }
      if(isServer!=0){
        destino = sender;
      }
      break;
    case 2:
      Serial.println("VIZINHODO");
      break;      
    default:
      break;
  }

  if(incoming=="ola" || '[' == incoming.charAt(0)){
    return;
  }
  if(isServer==0){
    mensagensEnviar += incoming;
  }else{
    mensagensEnviar += incoming+"|";
  }
  Serial.println("Mensagens: "+mensagensEnviar);
}

int gasReader(){
  return analogRead(MQ_analog);
}

void makeData(long value){
  if(isServer!=0){
    String json;
    json="{\"n\":"+String(enderecoLocal)+",\"s\":"+String(gasReader())+",\"u\":"+String(value)+"}|";
    mensagensEnviar +=json;
    Serial.println(mensagensEnviar);
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

void setup() {
  pinMode(OLED_RST,OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH); 
  delay(1000);
  pinMode(MQ_analog, INPUT);
  pinMode(LEDPIN,OUTPUT);
  display.init();
  display.flipScreenVertically();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.clear();
  Serial.begin(115200);
  while (!Serial);
  Serial.println("AmbiLora V0.4");
  display.drawString(0, 00, "AmbiLora V0.4");
  display.display();

  //configWifi(4); // ao comentar não é iniciado

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
  display.drawString(0, 10, "MOD LoRa Iniciado com Sucesso");
  display.drawString(0, 20, "AmbiLora iniciado...");
  display.display();
  delay(1500);
  display.clear();
  display.display();

  sendMessage(mensagem, 255);

  chipid=ESP.getEfuseMac();
  String MAC = String((uint16_t)(chipid>>32), HEX); 
  MAC += String((uint32_t)chipid, HEX);
  Serial.println(MAC);
  if(isServer==0){
    init_MQTT(2,mensagem);
  }
  
}

void loop() {
  if (millis() - lastSendTime > intervalo) {
    if(msgCount>6){
      Serial.println("MENSAGENS: "+mensagensEnviar);
      if (isServer==0){
        init_MQTT(3,mensagensEnviar);
      }
      mensagem = sendTable(); // envia tabela de servers
      sendMessage(mensagem, 255);
      mensagensRecebidas="";
      mensagensEnviar="";
      msgCount = 0;
    }
    else{
      mensagem = mensagensRecebidas;
      if (isServer==0){
        Serial.print("Dest "+destino);
        Serial.println("Env"+msgCount);
        sendMessage("ola", 255);
        digitalWrite(LEDPIN, HIGH);
      }
      else{
        digitalWrite(LEDPIN, LOW);
        if(isServer==1){//se for vizinho direto encaminho para o servidor
            destino = meusServidores[0];
            Serial.println("Destino: "+destino);
            sendMessage(mensagensEnviar, destino);
        }
        else{
            destino = meusVizinhos[0];
            sendMessage(mensagensEnviar, destino);
            Serial.println("Destino: "+destino);
        }
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