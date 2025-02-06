// ********* 필수 라이브러리 포함 *********
#include <Arduino.h>               // 아두이노 기본 라이브러리
#include <Adafruit_NeoPixel.h>      // WS2812B LED 스트립을 제어하는 라이브러리
#include <WiFi.h>                   // Wi-Fi 연결을 위한 라이브러리
#include <AsyncTCP.h>                // 비동기 TCP 통신을 위한 라이브러리
#include <ESPAsyncWebServer.h>       // 비동기 웹 서버를 구현하기 위한 라이브러리
#include "SPIFFS.h"                  // SPIFFS(파일 시스템) 사용을 위한 라이브러리
#include <Arduino_JSON.h>            // JSON 데이터 처리를 위한 라이브러리
#include <Adafruit_BME280.h>         // BME280 센서를 제어하는 라이브러리
#include <Adafruit_Sensor.h>         // Adafruit 센서 인터페이스 라이브러리

// ********* 웹 서버 및 이벤트 소스 객체 생성 *********
AsyncWebServer server(80);           // ESP32 웹 서버 객체 생성 (포트 80)
AsyncEventSource events("/events");  // 이벤트 소스 생성 (SSE 방식으로 클라이언트와 데이터 교환)

// ********* HTTP 요청에서 전달된 매개변수 이름 정의 *********
const char* PARAM_INPUT_1 = "ssid";  // Wi-Fi SSID 입력값
const char* PARAM_INPUT_2 = "pass";  // Wi-Fi 비밀번호 입력값
const char* PARAM_INPUT_3 = "ip";    // IP 주소 입력값

// ********* Wi-Fi 설정값 저장 변수 *********
String ssid;
String pass;
String ip;

// ********* Wi-Fi 설정값을 저장할 SPIFFS 파일 경로 *********
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";

// ********* 네트워크 설정 *********
IPAddress localIP;
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

// ********* Wi-Fi 연결 상태 확인 타이머 *********
unsigned long previousMillis = 0;
const long interval = 10000;  // 10초 동안 Wi-Fi 연결을 시도

// ********* WS2812B RGB LED 스트립 설정 *********
#define STRIP_1_PIN 27  // 첫 번째 LED 스트립 연결 GPIO 핀
#define STRIP_2_PIN 32  // 두 번째 LED 스트립 연결 GPIO 핀
#define LED_COUNT 5  // LED 개수
#define BRIGHTNESS 50  // LED 밝기 (0 ~ 255)

// WS2812B LED 스트립 객체 생성
Adafruit_NeoPixel strip1(LED_COUNT, STRIP_1_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(LED_COUNT, STRIP_2_PIN, NEO_GRB + NEO_KHZ800);

// ********* BME280 센서 객체 생성 *********
Adafruit_BME280 bme;         // ESP32와 I2C 방식으로 연결됨 (GPIO 21 = SDA, GPIO 22 = SCL)

// ********* 센서 데이터를 저장할 변수 선언 *********
float temp;
float hum;
float pres;

// ********* 센서 데이터를 JSON 형식으로 저장하기 위한 변수 선언 *********
JSONVar readings;

// ********* 센서 데이터 업데이트 타이머 설정 *********
unsigned long lastTime = 0;
unsigned long timerDelay = 30000;  // 30초마다 센서 데이터 업데이트

// ********* BME280 센서 초기화 함수 *********
void initBME(){
  if (!bme.begin(0x76)) { // I2C 주소 0x76
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);  // 센서를 찾을 수 없으면 무한 루프
  }
}

// ********* 센서 데이터 읽기 함수 *********
void getSensorReadings(){
  temp = bme.readTemperature();
  hum = bme.readHumidity();
  pres = bme.readPressure() / 100.0F;  // 압력을 hPa 단위로 변환
}

// ********* 센서 데이터를 JSON 문자열로 변환하는 함수 *********
String getJSONReadings(){
  readings["temperature"] = String(temp);
  readings["humidity"] = String(hum);
  readings["pressure"] = String(pres);
  return JSON.stringify(readings);  // JSON 형식 문자열 반환
}

// ********* 온도 및 습도에 따라 RGB LED 색상 업데이트 *********
void updateColors(){
  strip1.clear();
  strip2.clear();

  // 온도 값에 따라 점등할 LED 개수 결정
  int tempLEDs;
  if (temp <= 0){
    tempLEDs = 1;
  }
  else if (temp > 0 && temp <= 10){
    tempLEDs = 2;
  }
  else if (temp > 10 && temp <= 20){
    tempLEDs = 3;
  }
  else if (temp > 20 && temp <= 30){
    tempLEDs = 4;
  }
  else{
    tempLEDs = 5;
  }

  // 온도 값에 따라 LED 색상 변경 (주황색)
  for(int i = 0; i < tempLEDs; i++) {
    strip1.setPixelColor(i, strip1.Color(255, 165, 0));
    strip1.show();
  }

  // 습도 값에 따라 점등할 LED 개수 결정 (0~100% → 1~5개의 LED 사용)
  int humLEDs = map(hum, 0, 100, 1, LED_COUNT);

  // 습도 값에 따라 LED 색상 변경 (파란색)
  for(int i = 0; i < humLEDs; i++) {
    strip2.setPixelColor(i, strip2.Color(25, 140, 200));
    strip2.show();
  }
}

// ********* SPIFFS 파일 시스템 초기화 함수 *********
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  else{
    Serial.println("SPIFFS mounted successfully");
  }
}

// ********* SPIFFS에서 파일을 읽는 함수 *********
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

// ********* SPIFFS에 데이터를 저장하는 함수 *********
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
    Serial.println("- write failed");
  }
}

// ********* Wi-Fi 초기화 및 연결 함수 *********
bool initWiFi() {
  if(ssid == "" || ip == ""){
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
  // 시리얼 포트 초기화 (디버깅용)
  Serial.begin(115200);

  // LED 스트립 초기화
  strip1.begin();
  strip2.begin();

  // LED 밝기 설정
  strip1.setBrightness(BRIGHTNESS);
  strip2.setBrightness(BRIGHTNESS);

  // BME280 센서 초기화
  initBME();

  // SPIFFS(파일 시스템) 초기화
  initSPIFFS();

  // SPIFFS에서 저장된 Wi-Fi 설정값 불러오기
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  /*Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);*/

  // Wi-Fi 연결 시도
  if(initWiFi()) {
    // Wi-Fi 연결 성공 시 모든 LED를 청록색으로 변경
    for(int i = 0; i < LED_COUNT; i++) { 
      strip1.setPixelColor(i, strip1.Color(0, 255, 128)); // Teal color
      strip2.setPixelColor(i, strip2.Color(0, 255, 128));

      strip1.show();   // 변경된 LED 색상을 하드웨어에 전송
      strip2.show();   
    }

    // 웹 서버 설정 (Station Mode)
    // 클라이언트에서 "/" 요청 시 SPIFFS에서 index.html 파일 제공
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html");
    });

    // 정적 파일 제공 (CSS, JS 등)
    server.serveStatic("/", SPIFFS, "/");

    // 클라이언트에서 최신 센서 데이터를 요청하면 JSON 응답 반환
    server.on("/readings", HTTP_GET, [](AsyncWebServerRequest *request){
      getSensorReadings(); // 최신 센서 데이터 가져오기
      String json = getJSONReadings(); // JSON 형식으로 변환
      request->send(200, "application/json", json); // JSON 데이터 응답
      json = String(); // 메모리 절약을 위해 문자열 초기화
    });

    // Server-Sent Events (SSE) 설정
    events.onConnect([](AsyncEventSourceClient *client){
      if(client->lastId()){
        Serial.printf("클라이언트 재연결! 마지막 메시지 ID: %u\n", client->lastId());
      }
    });
    server.addHandler(&events);
    
    // 웹 서버 시작
    server.begin();
  }
  else {
    // Wi-Fi 연결 실패 시 AP(Access Point) 모드 활성화
    // 모든 LED를 빨간색으로 변경하여 Wi-Fi 설정 필요 상태 표시
    for(int i = 0; i < LED_COUNT; i++) {
      strip1.setPixelColor(i, strip1.Color(255, 0, 0)); // Red color
      strip2.setPixelColor(i, strip2.Color(255, 0, 0));
      strip1.show();  
      strip2.show();  
    }

    // AP(Access Point) 모드 설정
    Serial.println("Setting AP (Access Point)");
    WiFi.softAP("ESP-WIFI-MANAGER", NULL); // 비밀번호 없는 공개 AP 생성

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    // Wi-Fi 설정 웹 페이지 제공
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });

    // 정적 파일 제공
    server.serveStatic("/", SPIFFS, "/");
    
    // 클라이언트에서 Wi-Fi 설정값을 입력하면 저장 후 ESP 재시작
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      for(int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()) {
          // SSID 저장
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // 비밀번호 저장
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          // IP 주소 저장
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            writeFile(SPIFFS, ipPath, ip.c_str());
          }
        }
      }
      // Wi-Fi 설정 완료 후 ESP 재부팅
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart();
    });

    // AP 모드에서 웹 서버 시작
    server.begin();
  }
}

void loop() {
  // Wi-Fi 연결 상태 확인
  if (WiFi.status() == WL_CONNECTED) {

    // 30초마다 센서 데이터 업데이트 및 클라이언트에 전송
    if (millis() - lastTime > timerDelay) {
      getSensorReadings(); // 센서 데이터 가져오기
      updateColors(); // LED 색상 업데이트
      
      // 센서 데이터를 JSON 형식으로 변환 후 클라이언트에 전송 (SSE 방식)
      String message = getJSONReadings();
      events.send(message.c_str(),"new_readings", millis());
      lastTime = millis();
    }
  }
}

