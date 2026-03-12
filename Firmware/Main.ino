#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <Arduino.h>
#include <PubSubClient.h>
#include <config.h>

#define TOPIC_PREFIX   "b6810504052/game"

#define RED_GPIO        42
#define YELLOW_GPIO     41
#define GREEN_GPIO      40

#define RED_1           42 
#define RED_2           14          
#define RED_3           1       

#define RED_4           21         
#define RED_5           13            
#define RED_6           2           

#define JOY_X_PIN       4
#define JOY_Y_PIN       5
#define JOY_SW_PIN      6
#define TFT_CS          10
#define TFT_RST         9
#define TFT_DC          8
#define BUZZER_PIN      15

uint8_t player_macAddr[] = {0x68, 0xB6, 0xB3, 0x38, 0x02, 0x4C};
esp_now_peer_info_t peerInfo;

WiFiClient wifiClient;
PubSubClient mqtt(MQTT_BROKER, 1883, wifiClient);

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

typedef struct message_struct{
    uint8_t type = 1;
    int lhand;
    int rhand;
} message_struct;

typedef struct status_struct{
    uint8_t type = 2;
    bool esp_main;
    bool esp_player;
    bool send_recv;
} status_struct;

volatile status_struct  global_status;
volatile message_struct global_data;

bool rcheck, lcheck;

enum State { SELECT_GAME, SELECT_DIFF, PLAYING, GAME_OVER };
State currentState = SELECT_GAME;

int selectedGame = 1;
int selectedDiff = 1;
unsigned long lastJoyMove = 0;
unsigned long lastDebounceTime = 0;

void playStartSound() {
    tone(BUZZER_PIN, 1200, 150); 
    delay(150);
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, HIGH); 
}

void playCorrectSound() {
    tone(BUZZER_PIN, 1047, 100); delay(100); // Note C6
    tone(BUZZER_PIN, 1319, 100); delay(100); // Note E6
    tone(BUZZER_PIN, 1568, 100); delay(100); // Note G6
    tone(BUZZER_PIN, 2093, 200); delay(200); // Note C7
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, HIGH); 
}

void playWrongSound() {
    tone(BUZZER_PIN, 250, 250); delay(250);  
    tone(BUZZER_PIN, 150, 500); delay(500);  
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, HIGH); 
}

void connect_wifi(){
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    printf("Connecting to WiFi %s.\n", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        printf(".");
        fflush(stdout);
        delay(500);
    }
    printf("\nWiFi connected.\n");
}

void connect_mqtt(){
    printf("Connecting to MQTT broker at %s.\n", MQTT_BROKER);
    if (!mqtt.connect("", MQTT_USER, MQTT_PASS)) {
        printf("Failed to connect to MQTT broker.\n");
        for (;;) {}
    }
    printf("MQTT broker connected.\n");
}

void publishGameStats(int game_type, int local_win, int local_lose, int diff) {
    if (!mqtt.connected()) connect_mqtt(); 

    int total_rounds = local_win + local_lose;

    if (game_type == 1) { 
        mqtt.publish(TOPIC_PREFIX "/speed/rounds", String(total_rounds).c_str());
        mqtt.publish(TOPIC_PREFIX "/speed/win", String(local_win).c_str());
        mqtt.publish(TOPIC_PREFIX "/speed/lose", String(local_lose).c_str());
        mqtt.publish(TOPIC_PREFIX "/history/most_played_game", "Speed Game"); 
    } else if (game_type == 2) { 
        mqtt.publish(TOPIC_PREFIX "/memory/rounds", String(total_rounds).c_str());
        mqtt.publish(TOPIC_PREFIX "/memory/win", String(local_win).c_str());
        mqtt.publish(TOPIC_PREFIX "/memory/lose", String(local_lose).c_str());
        mqtt.publish(TOPIC_PREFIX "/history/most_played_game", "Memory Game"); 
    }

    String diff_str = "Easy";
    if (diff == 2) diff_str = "Medium";
    else if (diff == 3) diff_str = "Hard";
    mqtt.publish(TOPIC_PREFIX "/history/most_played_diff", diff_str.c_str());

    printf(">>> Session MQTT Stats Published! <<<\n");
}

void esp_recv(const esp_now_recv_info_t * esp_now_info, const uint8_t *dataRecv, int len){
    uint8_t incoming_type = dataRecv[0];
    if (incoming_type == 1) memcpy((void *)&global_data, dataRecv, sizeof(global_data));
    else if (incoming_type == 2) memcpy((void *)&global_status, dataRecv, sizeof(global_status));
}

void connect_espnow(){
    if (esp_now_init() != ESP_OK) return;
    esp_now_register_recv_cb(esp_recv);
    int32_t wifi_channel = WiFi.channel();
    memcpy(peerInfo.peer_addr, player_macAddr, 6);
    peerInfo.channel = wifi_channel;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void all_low(){
    digitalWrite(RED_1, 0); digitalWrite(RED_2, 0); digitalWrite(RED_3, 0);
    digitalWrite(RED_4, 0); digitalWrite(RED_5, 0); digitalWrite(RED_6, 0);
}

void RED_active(int side, int n){
    if (side == 0){ 
        if (n == 0) digitalWrite(RED_1, 1);
        else if (n == 1) digitalWrite(RED_2, 1);
        else if (n == 2) digitalWrite(RED_3, 1);
    }else if (side == 1){   
        if (n == 0) digitalWrite(RED_4, 1);
        else if (n == 1) digitalWrite(RED_5, 1);
        else if (n == 2) digitalWrite(RED_6, 1);
    } 
}

int game(int n, int times, int sleep_time, int delay_time){
    int w = 0, l = 0;
    int lrand[n], rrand[n];

    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10, 60);
    tft.setTextColor(ST77XX_ORANGE);
    tft.setTextSize(2);
    if (n > 1) tft.println("MEMORIZE!"); 
    else tft.println("READY!");          

    for (int i=0;i < n; i++){
        lrand[i] = esp_random() % 3;
        rrand[i] = esp_random() % 3;
        all_low();
        RED_active(0, lrand[i]);
        RED_active(1, rrand[i]);
        delay(delay_time);
    }
    delay(sleep_time);

    for (int i=0; i < n; i++){
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(5, 30);
        tft.setTextColor(ST77XX_CYAN);
        tft.setTextSize(2);
        tft.println("YOUR TURN");
        
        // วาดหัวข้อเวลา
        tft.setCursor(10, 70);
        tft.setTextColor(ST77XX_YELLOW);
        tft.print("TIME: ");

        int j=0;
        lcheck = 0;
        rcheck = 0;
        do {
            lcheck = (lrand[i] == global_data.lhand);
            rcheck = (rrand[i] == global_data.rhand);
            

            float timeLeft = ((times - j) * 200.0) / 1000.0;
            
            tft.setCursor(70, 70);
            
            if (timeLeft > 2.0) {
                tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
            } else {
                tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
            }
            
            tft.print(timeLeft, 1);
            tft.print("s ");

            j++;
            delay(200);
        } while (j < times && !(rcheck && lcheck));

        // เคลียร์จอเพื่อโชว์ผลลัพธ์ ถูก/ผิด
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(15, 60);
        tft.setTextSize(2);
        
        if (rcheck && lcheck){
            all_low();
            tft.setTextColor(ST77XX_GREEN);
            tft.println("CORRECT!");
            playCorrectSound();
            w++;
        }else{
            tft.setTextColor(ST77XX_RED);
            tft.setCursor(30, 60); 
            tft.println("WRONG!");
            playWrongSound();
            l++;
        }
        delay(1000); 
        all_low();
    }
    return w;
}

void drawBar(int y, const char* text, uint16_t themeColor, bool selected) {
    if (selected) {
        tft.fillRect(10, y, 108, 22, themeColor);
        tft.setTextColor(ST77XX_BLACK);
    } else {
        tft.fillRect(10, y, 108, 22, ST77XX_BLACK);
        tft.drawRect(10, y, 108, 22, themeColor);
        tft.setTextColor(themeColor);
    }
    tft.setTextSize(1);
    tft.setCursor(18, y + 7);
    tft.print(text);
}

void printMenuGame() { 
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(12, 15); 
    tft.setTextColor(ST77XX_WHITE); 
    tft.setTextSize(1);
    tft.println("=== SELECT GAME ===");
    drawBar(40, "Speed Game", ST77XX_CYAN, selectedGame == 1);
    drawBar(70, "Memory Game", ST77XX_CYAN, selectedGame == 2);
}

void printMenuDiff() { 
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10, 10); 
    tft.setTextColor(ST77XX_WHITE); 
    tft.setTextSize(1);
    tft.println("=== DIFFICULTY ===");
    drawBar(30, "Easy", ST77XX_GREEN, selectedDiff == 1);
    drawBar(55, "Medium", ST77XX_YELLOW, selectedDiff == 2);
    drawBar(80, "Hard", ST77XX_RED, selectedDiff == 3);
    drawBar(105, "<- Back", ST77XX_WHITE, selectedDiff == 4);
}

void setup() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH); 
    Serial.begin(115200);

    tft.initR(INITR_BLACKTAB); 
    tft.setRotation(0); 
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(5, 50); tft.setTextColor(ST77XX_WHITE); tft.println("Initializing...");
    
    pinMode(JOY_SW_PIN, INPUT_PULLUP);
    pinMode(RED_1, OUTPUT); pinMode(RED_2, OUTPUT); pinMode(RED_3, OUTPUT);
    pinMode(RED_4, OUTPUT); pinMode(RED_5, OUTPUT); pinMode(RED_6, OUTPUT);

    connect_wifi();
    connect_mqtt();
    connect_espnow();

    pinMode(RED_GPIO, OUTPUT);
    pinMode(GREEN_GPIO, OUTPUT);
    digitalWrite(RED_GPIO, 1);
    digitalWrite(GREEN_GPIO, 0);

    while (!global_status.esp_player){ delay(40); }

    global_status.esp_main = 1;
    esp_now_send(player_macAddr, (uint8_t *) &global_status, sizeof(global_status));

    digitalWrite(RED_GPIO, 0);
    digitalWrite(GREEN_GPIO, 1);
    printMenuGame();
}

void loop(){
    if (!mqtt.connected()) connect_mqtt();
    mqtt.loop();

    int joyY = analogRead(JOY_Y_PIN);
    bool swPressed = (digitalRead(JOY_SW_PIN) == LOW);

    if (millis() - lastJoyMove > 300) {
        if (joyY < 1000) {
            if (currentState == SELECT_GAME) { selectedGame = (selectedGame == 1) ? 2 : 1; printMenuGame(); }
            else if (currentState == SELECT_DIFF) { selectedDiff = (selectedDiff > 1) ? selectedDiff - 1 : 4; printMenuDiff(); }
            lastJoyMove = millis();
        } 
        else if (joyY > 3000) {
            if (currentState == SELECT_GAME) { selectedGame = (selectedGame == 2) ? 1 : 2; printMenuGame(); } 
            else if (currentState == SELECT_DIFF) { selectedDiff = (selectedDiff < 4) ? selectedDiff + 1 : 1; printMenuDiff(); }
            lastJoyMove = millis();
        }
    }

    if (swPressed && (millis() - lastDebounceTime > 500)) {
        lastDebounceTime = millis();
        
        if (currentState == SELECT_GAME) {
            currentState = SELECT_DIFF;
            selectedDiff = 1;
            printMenuDiff();
        } 
        else if (currentState == SELECT_DIFF) {
            if (selectedDiff == 4) {
                currentState = SELECT_GAME;
                printMenuGame();
            } else {
                currentState = PLAYING;
                tft.fillScreen(ST77XX_BLACK);
                tft.setCursor(15, 60); 
                tft.setTextColor(ST77XX_CYAN); tft.setTextSize(2);
                tft.println("START..."); 
                tft.setTextSize(1);
                playStartSound();
            }
        }
        else if (currentState == GAME_OVER) {
            currentState = SELECT_GAME;
            printMenuGame();
        }
    }

    if (currentState == PLAYING) {
        global_status.send_recv = true;
        esp_now_send(player_macAddr, (uint8_t *) &global_status, sizeof(global_status));
        delay(100);

        int local_win = 0, local_lose = 0;
        int p_n = 1, p_times = 25, p_sleep = 0, p_delay = 200;

        if (selectedGame == 1) { // Speed Game
            int i_max = 10;
            if (selectedDiff == 1)      { p_times = 35; p_delay = 500; }
            else if (selectedDiff == 2) { p_times = 25; p_delay = 300; }
            else if (selectedDiff == 3) { p_times = 15; p_delay = 150; }

            for (int i=0; i < i_max; i++){
                if (game(1, p_times, 0, p_delay)) local_win++;
                else local_lose++;
                delay(700); 
            }
        } 
        else if (selectedGame == 2) { // Memory Game
            if (selectedDiff == 1)      { p_n = 3; p_times = 35; p_sleep = 2500; p_delay = 1500; }
            else if (selectedDiff == 2) { p_n = 5; p_times = 25; p_sleep = 2000; p_delay = 1000; }
            else if (selectedDiff == 3) { p_n = 7; p_times = 15; p_sleep = 1000; p_delay = 500;  }

            local_win = game(p_n, p_times, p_sleep, p_delay);
            local_lose = p_n - local_win;
        }

        global_status.send_recv = false;
        esp_now_send(player_macAddr, (uint8_t *) &global_status, sizeof(global_status));
        
        currentState = GAME_OVER;

        publishGameStats(selectedGame, local_win, local_lose, selectedDiff);
        
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(10, 20); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2); tft.println("GAME OVER");
        tft.setTextSize(1);
        tft.setCursor(10, 60); tft.setTextColor(ST77XX_GREEN); tft.print("WIN : "); tft.println(local_win);
        tft.setCursor(10, 80); tft.setTextColor(ST77XX_RED); tft.print("LOSE: "); tft.println(local_lose);
        tft.setCursor(10, 110); tft.setTextColor(ST77XX_YELLOW); tft.println("Press button to");
        tft.setCursor(10, 125); tft.println("return to menu");
    }
    delay(200);
}