#include <Arduino.h>
#include <PowerMode.h>
#include "WiFi.h"
#include "VideoStream.h"
#include "StreamIO.h"
#include "RTSP.h"
#include "MotionDetection.h"
#include "VideoStreamOverlay.h"
#include "AmebaFatFS.h"
#include "Base64.h"

#define CHANNEL 0 // 视频通道

// Google Script和Line Notify的详细信息
String myScript = "/macros/s/AKfycbysRbKDPiUzqPapcYmh9tjBy5Gy_BghSuBVHtFtyiX8qtCuLNHc6F_NxH2by2YvyhZFDQ/exec";
String myFoldername = "&myFoldername=AMB82";
String myFilename = "&myFilename=image.jpg";
String myImage = "&myFile=";

char ssid[] = "dlink-540-5G"; // WiFi SSID
char pass[] = "bmelab540"; // WiFi密码
int status = WL_IDLE_STATUS;

uint32_t img_addr = 0;
uint32_t img_len = 0;
VideoSetting config(VIDEO_D1, CAM_FPS, VIDEO_H264_JPEG, 1);
RTSP rtsp;
StreamIO videoStreamer(1, 1);
AmebaFatFS fs;
WiFiSSLClient wifiClient;
#define WAKEUP_SOURCE 0
unsigned long previousMillis = 0;
const long interval = 5000;
unsigned int imageCounter = 0; // 用于生成唯一文件名的计数器

#if (WAKEUP_SOURCE == 0)
#define CLOCK 0
#define SLEEP_DURATION 25
uint32_t PM_AONtimer_setting[2] = {CLOCK, SLEEP_DURATION};
#define WAKUPE_SETTING (uint32_t)(PM_AONtimer_setting)
#else
#define WAKUPE_SETTING 0
#endif

void setup() {
    Serial.begin(115200);
    PowerMode.begin(DEEPSLEEP_MODE, WAKEUP_SOURCE, WAKUPE_SETTING);

    while (status != WL_CONNECTED) {
        Serial.print("Attempting to connect to WPA SSID: ");
        Serial.println(ssid);
        status = WiFi.begin(ssid, pass);
        delay(2000);
    }
    WiFiCon();

    Camera.configVideoChannel(CHANNEL, config);
    Camera.videoInit();
    rtsp.configVideo(config);
    rtsp.begin();
    videoStreamer.registerInput(Camera.getStream(CHANNEL));
    videoStreamer.registerOutput(rtsp);
    if (videoStreamer.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }

    Camera.channelBegin(CHANNEL);
    Serial.println("Video RTSP Streaming Start");
    OSD.configVideo(CHANNEL, config);
    OSD.begin();
    Serial.println("Motion Detecting");
    delay(2000);

    // 从 SD 卡读取计数器值
    if (fs.begin()) {
        File counterFile = fs.open("/counter.txt");
        if (counterFile) {
            String counterValue = counterFile.readStringUntil('\n');
            imageCounter = counterValue.toInt();
            counterFile.close();
        }
        fs.end();
    }
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;

        Serial.println("Capturing and Uploading Photo");

        if (!fs.begin()) {
            StreamEnd();
            pinMode(LED_B, OUTPUT);
            digitalWrite(LED_B, HIGH);
            Serial.println("[ERROR] SD Card Mount Failed !!!");
            while (1);
        }

        // 生成唯一文件名
        String filename = "/image_" + String(imageCounter) + ".jpg";
        String filepath = String(fs.getRootPath()) + filename;
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

        file = fs.open(filepath);
        unsigned int fileSize = file.size();
        uint8_t *fileinput = (uint8_t *)malloc(fileSize + 1);
        file.read(fileinput, fileSize);
        fileinput[fileSize] = '\0';
        file.close();
        fs.end();

        String imageFile = "data:image/jpeg;base64,";
        char output[base64_enc_len(3)];
        for (unsigned int i = 0; i < fileSize; i++) {
            base64_encode(output, (char *)(fileinput++), 3);
            if (i % 3 == 0) {
                imageFile += urlencode(String(output));
            }
        }

        Serial.println("[INFO] Uploading file to Google Drive...");
        String Data = myFoldername + "&myFilename=" + filename + myImage;
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

        // 增加计数器并保存到 SD 卡
        imageCounter++;
        if (fs.begin()) {
            File counterFile = fs.open("/counter.txt");
            if (counterFile) {
                counterFile.println(imageCounter);
                counterFile.close();
            }
            fs.end();
        }

        delay(100);
        PowerMode.start();
    } else {
        Serial.print(".");
    }
}

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
    delay(50);
    digitalWrite(LED_B, LOW);
}
