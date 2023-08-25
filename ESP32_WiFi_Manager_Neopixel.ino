#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>

// 포트 80에 AsyncWebServer 개체 생성
AsyncWebServer server(80);

// events에 이벤트 소스 만들기
AsyncEventSource events("/events");

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "공유기 ID";
const char* PARAM_INPUT_2 = "공유기 비번";
const char* PARAM_INPUT_3 = "사용할 IP";

//HTML 양식에서 값을 저장하는 변수
String ssid;
String pass;
String ip;

// 입력 값을 영구적으로 저장하는 파일 경로
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";

IPAddress localIP;
//IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

// Timer variables (check wifi)
unsigned long previousMillis = 0;
const long interval = 10000;  // Wi-Fi 연결을 기다리는 간격(밀리초)

// WS2812B 어드레서블 RGB LED
#define STRIP_1_PIN    48  // LED가 연결된 GPIO
#define STRIP_2_PIN    17  // LED가 연결된 GPIO
#define LED_COUNT  5  // LED 개수
#define BRIGHTNESS 50  // 네오픽셀 밝기, 0(최소) ~ 255(최대)
Adafruit_NeoPixel strip1(LED_COUNT, STRIP_1_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(LED_COUNT, STRIP_2_PIN, NEO_GRB + NEO_KHZ800);

// 센서 개체 만들기
Adafruit_BME280 bme;         // BME280 ESP32 I2C 연결 

//센서 판독값을 유지하는 변수
float temp;
float hum;
float pres;

// 센서 판독값을 유지하는 Json 변수
JSONVar readings;

// 타이머 변수(센서 판독값 가져오기)
unsigned long lastTime = 0;
unsigned long timerDelay = 30000;

//-----------------센서 판독값을 처리하는 함수-----------------//

// BME280 초기화
void initBME(){
  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }
}

// 센서 판독값 가져오기
void getSensorReadings(){
  temp = bme.readTemperature();
  hum = bme.readHumidity();
  pres= bme.readPressure()/100.0F;
}

// 센서 판독값에서 JSON 문자열 반환
String getJSONReadings(){
  readings["temperature"] = String(temp);
  readings["humidity"] =  String(hum);
  readings["pressure"] = String(pres);
  String jsonString = JSON.stringify(readings);
  return jsonString;
}

//온도 및 습도 값에 따라 RGB LED 색상 업데이트
void updateColors(){
  strip1.clear();
  strip2.clear();

  //켜진 LED 개수(온도)
  int tempLEDs;
  if (temp<=0){
    tempLEDs = 1;
  }
  else if (temp>0 && temp<=10){
    tempLEDs = 2;
  }
  else if (temp>10 && temp<=20){
    tempLEDs = 3;
  }
  else if (temp>20 && temp<=30){
    tempLEDs = 4;
  }
  else{
    tempLEDs = 5;
  }

  //온도 표시 LED 켜기
  for(int i=0; i<tempLEDs; i++) {
    strip1.setPixelColor(i, strip1.Color(255, 165, 0));
    strip1.show();   
  }
  
  //켜진 LED 개수(습도)
  int humLEDs = map(hum, 0, 100, 1, LED_COUNT);

  //습도 표시 LED 켜기
  for(int i=0; i<humLEDs; i++) { // For each pixel...
    strip2.setPixelColor(i, strip2.Color(25, 140, 200));
    strip2.show();
  }
}

//-----------------스피프와 파일을 처리하는 함수-----------------//

// SPIFFS 초기화
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  else{
    Serial.println("SPIFFS mounted successfully");
  }
}

// SPIFFS에서 파일 읽기
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}

// SPIFFS에 파일 쓰기
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// WiFi 초기화
bool initWiFi() {
  if(ssid=="" || ip==""){
    Serial.println("Undefined SSID or IP address.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  localIP.fromString(ip.c_str());

  if (!WiFi.config(localIP, gateway, subnet)){
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

void setup() {
  // 디버깅 목적의 직렬 포트
  Serial.begin(115200);
  
  // 스트립 초기화
  strip1.begin();
  strip2.begin();
  
  // 밝기 설정 
  strip1.setBrightness(BRIGHTNESS);
  strip2.setBrightness(BRIGHTNESS);
  
  // BME280 센서 초기화
  initBME();
  
  // 초기화 SPIFFS
  initSPIFFS();

  // SPIFFS에 저장된 값 로드
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  /*Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);*/

  if(initWiFi()) {
    // 스테이션 모드에서 ESP32가 성공적으로 초기화되면 모든 픽셀이 청록색으로 점등됩니다.
    for(int i=0; i<LED_COUNT; i++) { // For each pixel...
      strip1.setPixelColor(i, strip1.Color(0, 255, 128));
      strip2.setPixelColor(i, strip2.Color(0, 255, 128));

      strip1.show();   // 업데이트된 픽셀 색상을 하드웨어로 전송합니다.
      strip2.show();   // 업데이트된 픽셀 색상을 하드웨어로 전송합니다.
    }

    //스테이션 모드에서 웹 서버 처리
    // 루트/웹 페이지 경로
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html");
    });
    server.serveStatic("/", SPIFFS, "/");

    // 최신 센서 판독값 요청
    server.on("/readings", HTTP_GET, [](AsyncWebServerRequest *request){
      getSensorReadings();
      String json = getJSONReadings();
      request->send(200, "application/json", json);
      json = String();
    });

    events.onConnect([](AsyncEventSourceClient *client){
      if(client->lastId()){
        Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
      }
    });
    server.addHandler(&events);
    
    server.begin();
  }
  else {
    //그렇지 않으면 액세스 포인트 모드에서 ESP32를 초기화합니다.
    // 모든 픽셀을 빨간색으로 표시합니다.
    for(int i=0; i<LED_COUNT; i++) { // For each pixel...
      strip1.setPixelColor(i, strip1.Color(255, 0, 0));
      strip2.setPixelColor(i, strip2.Color(255, 0, 0));
      //strip1.setPixelColor(i, strip1.Color(128, 0, 21));
      //strip2.setPixelColor(i, strip2.Color(128, 0, 21));

      strip1.show();   // 업데이트된 픽셀 색상을 하드웨어로 전송합니다.
      strip2.show();   // 업데이트된 픽셀 색상을 하드웨어로 전송합니다.
    }

    // 액세스 포인트 설정
    Serial.println("Setting AP (Access Point)");
    // NULL은 개방형 액세스 포인트를 설정합니다
    WiFi.softAP("ESP-WIFI-MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    // WiFi 관리자 웹 페이지용 웹 서버 루트 URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });
    
    server.serveStatic("/", SPIFFS, "/");
    
    // 양식에 제출된 매개변수 가져오기 
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid 값
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST 패스 값
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // 값을 저장할 파일 쓰기
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          // HTTP POST IP 값
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Write file to save value
            writeFile(SPIFFS, ipPath, ip.c_str());
          }
          //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      // 매개 변수를 저장한 후 ESP32를 다시 시작합니다.
      ESP.restart();
    });
    server.begin();
  }
}

void loop() {
  // ESP32가 스테이션 모드에서 성공적으로 설정된 경우...
  if (WiFi.status() == WL_CONNECTED) {

    //...센서 판독값과 함께 클라이언트에 이벤트를 전송하고 30초마다 색상을 업데이트합니다.
    if (millis() - lastTime > timerDelay) {
      getSensorReadings();
      updateColors();
      
      String message = getJSONReadings();
      events.send(message.c_str(),"new_readings" ,millis());
      lastTime = millis();
    }
  }
}
