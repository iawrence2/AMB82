#include <Arduino.h>
#include "PowerMode.h"
#include "WiFi.h"
#include "VideoStream.h"
#include "StreamIO.h"
#include "RTSP.h"
#include "MotionDetection.h"
#include "VideoStreamOverlay.h"
#include "AmebaFatFS.h"
#include "Base64.h"

// User Configuration
#define CHANNEL   0              // Video channel for streaming & snapshot
#define FILENAME  "image.jpg"    // Save as jpg image in SD Card

// Enter your Google Script and Line Notify details
String myScript = "/macros/s/AKfycbysRbKDPiUzqPapcYmh9tjBy5Gy_BghSuBVHtFtyiX8qtCuLNHc6F_NxH2by2YvyhZFDQ/exec";    // Create your Google Apps Script and replace the "myScript" path.
String myFoldername = "&myFoldername=AMB82";                                    // Set the Google Drive folder name to store your file
String myFilename = "&myFilename=image.jpg";                                    // Set the Google Drive file name to store your data
String myImage = "&myFile=";

char ssid[] = "AAAAA";    // your network SSID (name)
char pass[] = "iawrence";        // your network password
int status = WL_IDLE_STATUS;

uint32_t img_addr = 0;
uint32_t img_len = 0;

// Create objects
VideoSetting config(VIDEO_D1, CAM_FPS, VIDEO_H264_JPEG, 1);    // High resolution video for streaming
RTSP rtsp;
StreamIO videoStreamer(1, 1);
AmebaFatFS fs;
WiFiSSLClient wifiClient;

unsigned long previousMillis = 0; // 上一次拍照的時間
const long interval = 60000; // 設定拍照間隔時間，這裡設定為60秒

void setup() {
    Serial.begin(115200);
    PowerMode.begin(STANDBY_MODE, 0, 0); // 初始化待机模式，使用AON timer唤醒

    // attempt to connect to Wifi network:
    while (status != WL_CONNECTED) {
        Serial.print("Attempting to connect to WPA SSID: ");
        Serial.println(ssid);
        status = WiFi.begin(ssid, pass);
        delay(2000);
    }
    WiFiCon();

    // Configure camera video channels for required resolutions and format
    Camera.configVideoChannel(CHANNEL, config);
    Camera.videoInit();

    // Configure RTSP for high resolution video stream information
    rtsp.configVideo(config);
    rtsp.begin();

    // Configure StreamIO object to stream data from high res video channel to RTSP
    videoStreamer.registerInput(Camera.getStream(CHANNEL));
    videoStreamer.registerOutput(rtsp);
    if (videoStreamer.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }

    // Start data stream from high resolution video channel
    Camera.channelBegin(CHANNEL);
    Serial.println("Video RTSP Streaming Start");

    // Configure OSD for drawing on high resolution video stream
    OSD.configVideo(CHANNEL, config);
    OSD.begin();
    Serial.println("");
    Serial.println("================================");
    Serial.println("Motion Detecting");
    Serial.println("================================");
    delay(2000);
}

void loop() {
    unsigned long currentMillis = millis();
    
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;

        Serial.println("Capturing and Uploading Photo");

        // 初始化SD卡
        if (!fs.begin()) {
            StreamEnd();
            pinMode(LED_B, OUTPUT);
            digitalWrite(LED_B, HIGH);
            Serial.println("[ERROR] SD Card Mount Failed !!!");
            while (1);
        }

        // 捕捉照片並保存到SD卡
        String filepath = String(fs.getRootPath()) + String(FILENAME);
        File file = fs.open(filepath);
        if (!file) {
            Serial.println("[ERROR] Failed to open file for reading");
            fs.end();
        }

        CamFlash();
        Camera.getImage(CHANNEL, &img_addr, &img_len);
        file.write((uint8_t *)img_addr, img_len);
        file.close();
        Serial.println("[INFO] Photo Captured ...");

        // 上傳文件到Google Drive
        file = fs.open(filepath);
        unsigned int fileSize = file.size();
        uint8_t *fileinput = (uint8_t *)malloc(fileSize + 1);
        file.read(fileinput, fileSize);
        fileinput[fileSize] = '\0';
        file.close();
        fs.end();

        String imageFile = "data:image/jpeg;base32,";
        char output[base64_enc_len(3)];
        for (unsigned int i = 0; i < fileSize; i++) {
            base64_encode(output, (char *)(fileinput++), 3);
            if (i % 3 == 0) {
                imageFile += urlencode(String(output));
            }
        }

        Serial.println("[INFO] Uploading file to Google Drive...");
        String Data = myFoldername + myFilename + myImage;
        const char *myDomain = "script.google.com";
        String getAll = "", getBody = "";
        Serial.println("[INFO] Connect to " + String(myDomain));

        if (wifiClient.connect(myDomain, 443)) {
            Serial.println("[INFO] Connection successful");
            wifiClient.println("POST " + myScript + " HTTP/1.1");
            wifiClient.println("Host: " + String(myDomain));
            wifiClient.println("Content-Length: " + String(Data.length() + imageFile.length()));
            wifiClient.println("Content-Type: application/x-www-form-urlencoded");
            wifiClient.println("Connection: keep-alive");
            wifiClient.println();
            wifiClient.print(Data);
            for (unsigned int Index = 0; Index < imageFile.length(); Index += 1000) {
                wifiClient.print(imageFile.substring(Index, Index + 1000));
            }
            int waitTime = 10000;
            unsigned int startTime = millis();
            boolean state = false;
            while ((startTime + waitTime) > millis()) {
                delay(100);
                while (wifiClient.available()) {
                    char c = wifiClient.read();
                    if (state) {
                        getBody += String(c);
                    }
                    if (c == '\n') {
                        if (getAll.length() == 0) {
                            state = true;
                        }
                        getAll = "";
                    } else if (c != '\r') {
                        getAll += String(c);
                    }
                    startTime = millis();
                }
                if (getBody.length() > 0) {
                    break;
                }
            }
            wifiClient.stop();
            Serial.println(getBody);
        } else {
            getBody = "Connected to " + String(myDomain) + " failed.";
            Serial.println("[INFO] Connected to " + String(myDomain) + " failed.");
        }
        Serial.println("[INFO] File uploading done.");

        // 延时一分钟进入待机模式
        delay(60000);
        PowerMode.start(); // 进入待机模式
    } else {
        // 在拍照间隔内，打印"."表示程序在运行
        Serial.print(".");
    }
}

// https://www.arduino.cc/reference/en/libraries/urlencode/
String urlencode(String str) {
    const char *msg = str.c_str();
    const char *hex = "0123456789ABCDEF";
    String encodedMsg = "";
    while (*msg != '\0') {
        if (('a' <= *msg && *msg <= 'z') || ('A' <= *msg && *msg <= 'Z') || ('0' <= *msg && *msg <= '9') || *msg == '-' || *msg == '_' || *msg == '.' || *msg == '~') {
            encodedMsg += *msg;
        } else {
            encodedMsg += '%';
            encodedMsg += hex[*msg >> 4];
            encodedMsg += hex[*msg & 15];
        }
        msg++;
    }
    return encodedMsg;
}

void StreamEnd() {
    Camera.camDeinit();
    Camera.videoDeinit();
    OSD.end();
    videoStreamer.end();
    rtsp.end();
}

void WiFiCon() {
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    IPAddress ip = WiFi.localIP();
    Serial.print("[INFO] IP Address: ");
    Serial.println(ip);
}

void CamFlash() {
    pinMode(LED_B, OUTPUT);
    digitalWrite(LED_B, HIGH);
    delay(300);
    digitalWrite(LED_B, LOW);
}
