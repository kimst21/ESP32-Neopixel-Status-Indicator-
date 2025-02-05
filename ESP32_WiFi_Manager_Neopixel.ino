#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>

// 포트 80(HTTP 기본 포트)에서 웹 서버 실행
AsyncWebServer server(80);

// 이벤트 소스를 생성하여 실시간 데이터를 전송하는 WebSocket 기능을 추가
AsyncEventSource events("/events");

// HTML 폼을 통해 Wi-Fi 정보를 입력받는 파라미터 정의
const char* PARAM_INPUT_1 = "공유기 ID";
const char* PARAM_INPUT_2 = "공유기 비번";
const char* PARAM_INPUT_3 = "사용할 IP";

// 입력된 Wi-Fi 정보 저장 변수
String ssid;
String pass;
String ip;

// Wi-Fi 정보를 영구 저장할 파일 경로 (SPIFFS 사용)
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";

// IP 설정 관련 변수
IPAddress localIP;
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

// Wi-Fi 연결 상태 확인 타이머
unsigned long previousMillis = 0;
const long interval = 10000;  // 10초 동안 Wi-Fi 연결을 대기

// WS2812B LED 스트립 설정
#define STRIP_1_PIN 17   // 첫 번째 LED 스트립의 GPIO 핀
#define STRIP_2_PIN 4    // 두 번째 LED 스트립의 GPIO 핀
#define LED_COUNT 5      // LED 개수
#define BRIGHTNESS 50    // 밝기 설정 (0~255)
Adafruit_NeoPixel strip1(LED_COUNT, STRIP_1_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(LED_COUNT, STRIP_2_PIN, NEO_GRB + NEO_KHZ800);

// BME280 센서 객체 생성 (온도, 습도, 기압 측정)
Adafruit_BME280 bme;

// 센서 측정값을 저장할 변수
float temp;
float hum;
float pres;

// 센서 데이터를 JSON 형태로 저장할 변수
JSONVar readings;

// 센서 데이터 수집 주기 설정
unsigned long lastTime = 0;
unsigned long timerDelay = 30000;  // 30초마다 측정

//-----------------센서 초기화 및 데이터 처리 함수-----------------//

// BME280 센서를 초기화하는 함수
void initBME() {
  if (!bme.begin(0x76)) {  // I2C 주소 0x76 사용
    Serial.println("BME280 센서를 찾을 수 없습니다. 배선 확인 필요!");
    while (1);  // 센서가 없으면 무한 루프
  }
}

// BME280 센서에서 데이터를 읽는 함수
void getSensorReadings() {
  temp = bme.readTemperature();  // 온도 읽기
  hum = bme.readHumidity();      // 습도 읽기
  pres = bme.readPressure() / 100.0F; // 기압 읽기 (hPa 단위 변환)
}

// 센서 데이터를 JSON 문자열로 변환하는 함수
String getJSONReadings() {
  readings["temperature"] = String(temp);
  readings["humidity"] = String(hum);
  readings["pressure"] = String(pres);
  return JSON.stringify(readings);
}

// 온도와 습도 값을 기반으로 LED 색상을 업데이트하는 함수
void updateColors() {
  strip1.clear();
  strip2.clear();

  // 온도에 따라 점등할 LED 개수 결정
  int tempLEDs;
  if (temp <= 0) {
    tempLEDs = 1;
  } else if (temp <= 10) {
    tempLEDs = 2;
  } else if (temp <= 20) {
    tempLEDs = 3;
  } else if (temp <= 30) {
    tempLEDs = 4;
  } else {
    tempLEDs = 5;
  }

  // 온도 표시 LED 점등 (주황색)
  for (int i = 0; i < tempLEDs; i++) {
    strip1.setPixelColor(i, strip1.Color(255, 165, 0));
    strip1.show();
  }

  // 습도에 따라 점등할 LED 개수 결정 (0~100% → 1~LED_COUNT로 매핑)
  int humLEDs = map(hum, 0, 100, 1, LED_COUNT);

  // 습도 표시 LED 점등 (파란색)
  for (int i = 0; i < humLEDs; i++) {
    strip2.setPixelColor(i, strip2.Color(25, 140, 200));
    strip2.show();
  }
}

//-----------------SPIFFS 파일 시스템 처리 함수-----------------//

// SPIFFS 파일 시스템을 초기화하는 함수
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 마운트 실패");
  } else {
    Serial.println("SPIFFS 마운트 성공");
  }
}

// SPIFFS에서 파일을 읽는 함수
String readFile(fs::FS &fs, const char *path) {
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    return String();
  }
  return file.readStringUntil('\n');
}

// SPIFFS에 파일을 저장하는 함수
void writeFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, FILE_WRITE);
  if (file) {
    file.print(message);
  }
}

//-----------------Wi-Fi 초기화 및 웹 서버 설정-----------------//

// Wi-Fi 연결을 시도하는 함수
bool initWiFi() {
  if (ssid == "" || ip == "") {
    return false;
  }

  WiFi.mode(WIFI_STA);
  localIP.fromString(ip.c_str());

  if (!WiFi.config(localIP, gateway, subnet)) {
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      return false;
    }
  }

  return true;
}

void setup() {
  Serial.begin(115200); // 디버깅을 위한 시리얼 통신 초기화 (속도: 115200bps)

  strip1.begin();  // 첫 번째 WS2812B LED 스트립 초기화
  strip2.begin();  // 두 번째 WS2812B LED 스트립 초기화

  strip1.setBrightness(BRIGHTNESS);  // 첫 번째 LED 스트립의 밝기 설정
  strip2.setBrightness(BRIGHTNESS);  // 두 번째 LED 스트립의 밝기 설정

  initBME();  // BME280 센서 초기화 (I2C 주소: 0x76)
  initSPIFFS();  // SPIFFS (내부 저장소) 초기화 - Wi-Fi 설정값을 저장하는 데 사용됨

  // SPIFFS에서 Wi-Fi 정보 읽기
  ssid = readFile(SPIFFS, ssidPath);  // 저장된 SSID 불러오기
  pass = readFile(SPIFFS, passPath);  // 저장된 Wi-Fi 비밀번호 불러오기
  ip = readFile(SPIFFS, ipPath);      // 저장된 IP 주소 불러오기

  // Wi-Fi 연결 시도
  if (initWiFi()) {  // Wi-Fi에 성공적으로 연결되었을 경우
    // ---------- 웹 서버 설정 ---------- //

    // HTTP GET 요청 시 index.html 전송 (SPIFFS에서 로드)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html");
    });

    // 정적 파일 제공 (CSS, JS 등)
    server.serveStatic("/", SPIFFS, "/");

    // HTTP GET 요청 시 센서 데이터를 JSON 형식으로 전송
    server.on("/readings", HTTP_GET, [](AsyncWebServerRequest *request) {
      getSensorReadings();  // 최신 센서 데이터 가져오기
      request->send(200, "application/json", getJSONReadings());  // JSON 응답 전송
    });

    // 새로운 클라이언트가 SSE(Server-Sent Events) 스트림에 연결될 때 실행
    events.onConnect([](AsyncEventSourceClient *client) {
      // 클라이언트가 마지막으로 받은 메시지 ID 출력 (디버깅용)
      if (client->lastId()) {
        Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
      }
    });

    // 웹 서버에 SSE 이벤트 핸들러 추가
    server.addHandler(&events);

    // 웹 서버 시작
    server.begin();
  }
}
