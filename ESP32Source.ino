#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi 设置
const char* ssid = "U";
const char* password = "12345678";

// 服务器地址
const char* serverUrl = "http://152.32.128.213:7860/";

// 按钮引脚
#define BUTTON_PIN 0

// 摄像头引脚定义（适用于AI-Thinker摄像头模块）
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0    
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

int lastButtonState = HIGH; // 按钮状态变量

camera_fb_t *fb = NULL;

// Base64 编码函数
String base64Encode(const uint8_t* data, size_t len) {
    const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    String output;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (int j = 0; j < 4; j++) {
                output += base64_chars[char_array_4[j]];
            }
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j < i + 1; j++) output += base64_chars[char_array_4[j]];
        while (i++ < 3) output += '=';
    }

    return output;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 初始化摄像头
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // 连接WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi连接成功");
  Serial.print("IP地址: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  static bool buttonPressed = false;
  static uint32_t lastDebounceTime = 0;
  const long debounceDelay = 50;
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if (millis() - lastDebounceTime > debounceDelay) {
    if (reading == LOW && !buttonPressed) {
      Serial.println("按钮按下，准备拍照...");
      buttonPressed = true;
      captureAndAnalyze();
    } else if (reading == HIGH && buttonPressed) {
      buttonPressed = false;
      Serial.println("按钮已释放");
    }
  }

  lastButtonState = reading;
}

void captureAndAnalyze() {
  Serial.println("拍摄照片中...");

  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("获取帧缓冲失败");
    return;
  }

  Serial.println("开始Base64编码...");
  String base64Image = base64Encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  String jsonPayload = "{\"data\":[\"data:image/jpeg;base64," + base64Image + "\"]}";

  // 步骤一：调用 /upload_only 接口上传图片
  HTTPClient httpUpload;
  String uploadUrl = String(serverUrl) + "gradio_api/call/analyze_base64";
  httpUpload.begin(uploadUrl.c_str());
  httpUpload.addHeader("Content-Type", "application/json");

  Serial.println("调用 /upload_only 接口...");
  int uploadCode = httpUpload.POST(jsonPayload);

  DynamicJsonDocument docUpload(1024);
  if (uploadCode == HTTP_CODE_OK) {
    String response = httpUpload.getString();
    DeserializationError error = deserializeJson(docUpload, response);

    if (!error) {
      const char* resultText = docUpload["event_id"];
      Serial.print("event id: ");
      Serial.println(String(resultText));

      // 步骤二：调用 /analyze_by_id 接口分析图片
      HTTPClient httpAnalyze;
      String analyzeUrl = String(serverUrl) + "gradio_api/call/analyze_by_id/" + String(resultText);
      httpAnalyze.begin(analyzeUrl.c_str());
      httpAnalyze.addHeader("Accept", "text/event-stream");

      int analyzeCode = httpAnalyze.GET();

      if (analyzeCode == HTTP_CODE_OK) {
        String analyzeResponse = httpAnalyze.getString();
        Serial.println("原始分析响应:");
        // Serial.println(analyzeResponse); // 已注释原始响应打印

        int dataIndex = analyzeResponse.indexOf("data: ");
        if (dataIndex != -1) {
          String jsonData = analyzeResponse.substring(dataIndex + 6);
          int newlineIndex = jsonData.indexOf("\n");
          if (newlineIndex != -1) {
            jsonData = jsonData.substring(0, newlineIndex);
          }

          DynamicJsonDocument docAnalyze(4096);
          DeserializationError analyzeError = deserializeJson(docAnalyze, jsonData);

          if (!analyzeError) {
            JsonArray resultArray = docAnalyze.as<JsonArray>();
            for (JsonVariant item : resultArray) {
              String chineseText = item.as<String>();
              Serial.println(chineseText);

              // 发送到TTS服务器
              HTTPClient httpTTS;
              String ttsServerUrl = "http://192.168.207.131:5000/speak"; // 请确认IP地址
              
              // 新增调试信息
              Serial.print("[DEBUG] 正在请求TTS地址: ");
              Serial.println(ttsServerUrl);
              
              httpTTS.setTimeout(8000);  // 设置8秒超时
              httpTTS.begin(ttsServerUrl.c_str());
              httpTTS.addHeader("Content-Type", "application/json");
              String ttsPayload = "{\"text\": \"" + chineseText + "\"}";
              
              int ttsCode = httpTTS.POST(ttsPayload);
              if (ttsCode == HTTP_CODE_OK) {
                Serial.println("TTS请求成功");
              } else {
                Serial.printf("TTS请求失败[%d]: %s\n", 
                             ttsCode, 
                             httpTTS.errorToString(ttsCode).c_str());
                Serial.println("响应内容: " + httpTTS.getString());
              }
              httpTTS.end();
            }
          } else {
            Serial.println("解析分析结果失败（JSON 格式错误）");
            Serial.println("尝试解析的文本:");
            Serial.println(jsonData);
          }
        } else {
          Serial.println("未找到 data: 字段");
        }
      } else {
        Serial.printf("分析请求失败: %s\n", httpAnalyze.errorToString(analyzeCode).c_str());
      }
      httpAnalyze.end();
    } else {
      Serial.println("解析上传响应失败");
    }
  } else {
    Serial.printf("上传失败: %s\n", httpUpload.errorToString(uploadCode).c_str());
  }
  httpUpload.end();
  Serial.println("操作完成\n");
}