#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <stdlib.h>
#include <config.h>

// Address of main board (MAC Adress: 68:B6:B3:37:F1:64 )
uint8_t main_macAddr[] = {0x68, 0xB6, 0xB3, 0x37, 0xF1, 0x64};
esp_now_peer_info_t peerInfo;

int32_t wifi_channel;
int lhand, rhand;
float lx, ly, rx, ry;
int16_t lAcX, lAcY, lAcZ, rAcX, rAcY, rAcZ;

// type 1 
typedef struct message_struct{
    uint8_t type = 1;
    int lhand;
    int rhand;
} message_struct;

// type 2
typedef struct status_struct{
    uint8_t type = 2;
    bool esp_main;
    bool esp_player;
    bool send_recv;
} status_struct;

volatile status_struct  global_status;
volatile message_struct global_data;

// ---------------------------------------------------------
// MPU Setting and Comfigure
void setup_mpu(uint8_t address){
    Wire.beginTransmission(address);
    Wire.write(0x6B);
    Wire.write(0);
    Wire.endTransmission(true);
}

void connect_mpu(){
    Wire.begin(4, 5);
    setup_mpu(0x68);
    setup_mpu(0x69);
  
    printf("MPU6050 success initializing.\n");
}

// ---------------------------------------------------------
// WIFI Setting and configure
void connect_wifi(){
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    wifi_channel = 6;
    // int n = WiFi.scanNetworks();
    // for (int i = 0; i < n; i++) {
    //     if (WiFi.SSID(i) == WIFI_SSID) {
    //         wifi_channel = WiFi.channel(i);
    //         break;
    //     }
    // }
    printf("Found target WiFi on Channel: %d\n", wifi_channel);

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    printf("wifi success initializing.\n");
}

// ----------------------------------------------------------------------
// ESP-NOW Connect and Recv
void connect_espnow(){
    if (esp_now_init() != ESP_OK){
        printf("Error initializing ESP-NOW\n");
        return;
    }
    esp_now_register_recv_cb(esp_recv);

    // 1. ดึงค่า Channel ที่บอร์ดหลักกำลังเกาะเร้าเตอร์อยู่
    int32_t wifi_channel = WiFi.channel(); 
    
    // 2. ใส่ MAC Address ของบอร์ด Player (ใช้เลข 6 เสมอ)
    memcpy(peerInfo.peer_addr, player_macAddr, 6); 
    
    // 3. ตั้งค่าให้ ESP-NOW ส่งข้อมูลไปที่ Channel เดียวกับเร้าเตอร์
    peerInfo.channel = wifi_channel;  
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        printf("Failed to add peer\n");
        return;
    }
    
    printf("ESP-NOW initialized on Channel: %d\n", wifi_channel);
}

void esp_recv(const esp_now_recv_info_t * esp_now_info, const uint8_t *dataRecv, int len){
    uint8_t incoming_type = dataRecv[0];

    if (incoming_type == 1)
        memcpy((void *)&global_data, dataRecv, sizeof(global_data));
    else if (incoming_type == 2)
        memcpy((void *)&global_status, dataRecv, sizeof(global_status));
    
    // for debug
    // printf("Unknown Struct Type received!\n");
}

// -------------------------------------------------------------------------------------------------
// Check Hand player for send to Main ESP32
int check_hand(int x, int y){
    if (abs(x) < 20 && abs(y) < 20)
        return 0;
    else if (y > 60 && y < 120)
        return 1;
    else if (abs(y) < 20 && abs(y) < 180)
        return 2;
    return -1;
}

void read_mpu6050() {
    Wire.beginTransmission(0x68);
    Wire.write(0x3B);  // เริ่มดึงข้อมูลจาก Register ของ Accel X
    Wire.endTransmission(false);

    Wire.requestFrom((uint16_t)(0x68), (uint8_t)6, true);
    lAcX = Wire.read()<<8 | Wire.read();  
    lAcY = Wire.read()<<8 | Wire.read();  
    lAcZ = Wire.read()<<8 | Wire.read();

    Wire.beginTransmission(0x69);
    Wire.write(0x3B); 
    Wire.endTransmission(false);

    Wire.requestFrom((uint16_t)0x69, (uint8_t)6, true);
    rAcX = Wire.read()<<8 | Wire.read();  
    rAcY = Wire.read()<<8 | Wire.read();  
    rAcZ = Wire.read()<<8 | Wire.read();

    lx = atan2(lAcY, lAcZ) * (180.0 / 3.1415926535);
    ly = atan2(-lAcX, sqrt((long)lAcY * lAcY + (long)lAcZ * lAcZ)) * (180.0 / 3.1415926535);

    rx = atan2(rAcY, rAcZ) * (180.0 / 3.1415926535);
    ry = atan2(-rAcX, sqrt((long)rAcY * rAcY + (long)rAcZ * rAcZ)) * (180.0 / 3.1415926535);

    lhand = check_hand(lx, ly);
    rhand = check_hand(rx, ry);

    // for debug
    // printf("Right: %.2f, %.2f\n", rx, ry);
    // printf("Left:  %.2f, %.2f\n", lx, ly);
    // printf("right: %d | left %d\n\n", lhand, rhand);

    global_data.lhand = lhand;
    global_data.rhand = rhand;

    esp_now_send(main_macAddr, (uint8_t *) &global_data, sizeof(global_data));
}

// -----------------------------------------------------------------------------
// Main Program
void setup(){
    Serial.begin(115200);

    connect_wifi();
    connect_espnow();
    connect_mpu();

    global_status.esp_main = 0;
    global_status.esp_player = 1;
    global_status.send_recv = 0;
    esp_now_send(main_macAddr, (uint8_t *) &global_status, sizeof(global_status));

    while (!global_status.esp_main){
        // for debug
        // printf("ESP32 main not found!\n");

        delay(40);
    }
    printf("ESP32 main found.\n");

    printf("setup is success.\n");
}

void loop(){
    while (global_status.send_recv){
        read_mpu6050();
        delay(150);
    }

    // for debug
    // read_sensors()

    delay(150);
}