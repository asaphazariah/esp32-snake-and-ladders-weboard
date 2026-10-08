/*
 * =========================================================
 *  ESP32 Snakes & Ladders — Jumbotron Controller
 * =========================================================
 *
 *  Hardware:
 *    - SPI OLED 128x64 (SSD1306) — dice display, game events
 *    - I2C LCD 16x2            — player position scoreboard
 *    - Passive Buzzer          — sound effects
 *    - Button: Player Select   — add player / reset (long press)
 *    - Button: Start/Roll      — start game / roll dice
 *
 *  Pin Config (from 1.docx):
 *    OLED CLK  = GPIO 18
 *    OLED MOSI = GPIO 23
 *    OLED RST  = GPIO 4
 *    OLED DC   = GPIO 2
 *    OLED CS   = GPIO 5
 *    LCD SDA   = GPIO 27
 *    LCD SCL   = GPIO 33
 *    BTN PlayerSelect = GPIO 26 (INPUT_PULLUP)
 *    BTN Start/Roll   = GPIO 32 (INPUT_PULLUP)
 *    Buzzer Signal     = GPIO 13
 *
 *  Libraries Required (install via Arduino Library Manager):
 *    - U8g2         (for SPI OLED)
 *    - LiquidCrystal_I2C (for I2C LCD)
 *    - ArduinoWebsockets  by Gil Maimon
 *    - ArduinoJson        by Benoit Blanchon
 *    - WiFi               (built-in for ESP32)
 *
 *  Board: ESP32 Dev Module
 * =========================================================
 */

#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Wire.h>

using namespace websockets;

// ── WiFi Credentials ─────────────────────────────────────
const char* WIFI_SSID     = "Nothing Phone (4a)";
const char* WIFI_PASSWORD = "ragul123";


// ── Server Config ────────────────────────────────────────
// Set this to the IP of the computer running mock_server.py
// Find it by running 'ipconfig' on Windows or 'ifconfig' on Mac/Linux
const char* WS_SERVER_HOST = "192.168.157.172";
const uint16_t WS_SERVER_PORT = 8765;

// ── Pin Definitions ──────────────────────────────────────
// SPI OLED
#define OLED_CLK   18
#define OLED_MOSI  23
#define OLED_RST   4
#define OLED_DC    2
#define OLED_CS    5

// I2C LCD
#define LCD_SDA    27
#define LCD_SCL    33
#define LCD_ADDR   0x27   // Common I2C address; try 0x3F if 0x27 doesn't work

// Buttons (INPUT_PULLUP — press = LOW)
#define BTN_PLAYER 26     // Player Select button
#define BTN_ROLL   32     // Start / Roll button

// Buzzer
#define BUZZER_PIN 13

// ── Hardware Objects ─────────────────────────────────────
// OLED: SSD1306, 128x64, 4-wire SPI
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI oled(
    U8G2_R0,    // rotation
    OLED_CS,    // CS
    OLED_DC,    // DC
    OLED_RST    // RST
);

// LCD: 16x2 I2C (custom SDA/SCL pins set in setup)
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// WebSocket client
WebsocketsClient wsClient;

// ── Game State ───────────────────────────────────────────
struct Player {
    int    id;
    String name;
    String color;
    String colorHex;
    int    position;
};

Player players[8];
int    numPlayers   = 0;
int    currentTurn  = 0;
bool   gameStarted  = false;
bool   gameOver     = false;
int    playerCounter = 0;   // for naming new players
int    lastDiceValue = 0;

// ── Button State ─────────────────────────────────────────
bool   btnPlayerPressed    = false;
unsigned long btnPlayerDownTime = 0;
unsigned long lastBtnPlayerRelease = 0;

bool   btnRollPressed      = false;
unsigned long lastBtnRollPress = 0;

const unsigned long DEBOUNCE_MS   = 250;
const unsigned long LONG_PRESS_MS = 2000;

// ── WebSocket State ──────────────────────────────────────
bool   wsConnected = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 3000;

// ── LCD Scroll State ─────────────────────────────────────
unsigned long lastLcdPage = 0;
int lcdPage = 0;
// ── Forward Declarations ─────────────────────────────────
void animateSnakeBite(const char* name, int from, int to);
void animateLadderClimb(const char* name, int from, int to);
void animateWinnerOLED(const char* name);
void oledShowMessage(const char* line1, const char* line2, const char* line3);
void oledShowTurn(const char* playerName, int playerNum);
void updateLcdFull();
void updateLcdPage(int page);
void playTone(int freq, int duration);
void playDiceSound();
void playSnakeSound();
void playLadderSound();
void playWinSound();
void playMelody_Start();

// ==========================================================
//  SETUP
// ==========================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Snakes & Ladders ESP32 Controller ===");

    // Buttons
    pinMode(BTN_PLAYER, INPUT_PULLUP);
    pinMode(BTN_ROLL,   INPUT_PULLUP);

    // Buzzer
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // I2C for LCD (custom pins)
    Wire.begin(LCD_SDA, LCD_SCL);
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Snakes & Ladders");
    lcd.setCursor(0, 1);
    lcd.print("Starting...");

    // OLED
    oled.begin();
    oledShowMessage("SNAKES &", "LADDERS", "Starting...");

    // WiFi
    connectWiFi();

    // WebSocket
    setupWebSocket();
}

// ==========================================================
//  LOOP
// ==========================================================

void loop() {
    // Poll WebSocket
    if (wsConnected) {
        wsClient.poll();
    } else {
        // Try to reconnect
        if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = millis();
            connectWebSocket();
        }
    }

    // Handle buttons
    handleButtons();

    // Update LCD scoreboard (page rotation for >4 players)
    updateLcdScoreboard();
}

// ==========================================================
//  WiFi CONNECTION
// ==========================================================

void connectWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    oledShowMessage("WiFi", "Connecting...", WIFI_SSID);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi...");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());

        String ip = WiFi.localIP().toString();
        oledShowMessage("WiFi OK!", ip.c_str(), "");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Connected!");
        lcd.setCursor(0, 1);
        lcd.print(ip);

        playTone(1000, 100);
        delay(1000);
    } else {
        Serial.println("\nWiFi FAILED!");
        oledShowMessage("WiFi FAILED", "Check SSID/Pass", "Restarting...");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi FAILED!");
        delay(3000);
        ESP.restart();
    }
}

// ==========================================================
//  WebSocket CONNECTION
// ==========================================================

void setupWebSocket() {
    wsClient.onMessage(onWebSocketMessage);
    wsClient.onEvent(onWebSocketEvent);
    connectWebSocket();
}

void connectWebSocket() {
    String url = "ws://" + String(WS_SERVER_HOST) + ":" + String(WS_SERVER_PORT);
    Serial.print("Connecting WebSocket: ");
    Serial.println(url);

    oledShowMessage("Connecting", "WebSocket...", WS_SERVER_HOST);

    wsConnected = wsClient.connect(WS_SERVER_HOST, WS_SERVER_PORT, "/");

    if (wsConnected) {
        Serial.println("WebSocket connected!");
        oledShowMessage("READY!", "Add players", "with BTN 2");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Ready! Add");
        lcd.setCursor(0, 1);
        lcd.print("players...");
        playTone(1500, 100);
    } else {
        Serial.println("WebSocket failed! Will retry...");
        oledShowMessage("WS Failed", "Retrying...", WS_SERVER_HOST);
    }
}

void onWebSocketEvent(WebsocketsEvent event, String data) {
    switch (event) {
        case WebsocketsEvent::ConnectionOpened:
            Serial.println("WS: Connection opened");
            wsConnected = true;
            break;
        case WebsocketsEvent::ConnectionClosed:
            Serial.println("WS: Connection closed");
            wsConnected = false;
            oledShowMessage("Disconnected", "Reconnecting", "...");
            break;
        case WebsocketsEvent::GotPing:
            break;
        case WebsocketsEvent::GotPong:
            break;
    }
}

// ==========================================================
//  WebSocket MESSAGE HANDLER
// ==========================================================

void onWebSocketMessage(WebsocketsMessage message) {
    Serial.print("WS recv: ");
    Serial.println(message.data());

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, message.data());
    if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type = doc["type"];

    // ── Full state sync ──
    if (strcmp(type, "game_state") == 0) {
        numPlayers = 0;
        JsonArray arr = doc["players"].as<JsonArray>();
        for (JsonObject p : arr) {
            if (numPlayers < 8) {
                players[numPlayers].id       = p["id"];
                players[numPlayers].name     = p["name"].as<String>();
                players[numPlayers].color    = p["color"].as<String>();
                players[numPlayers].colorHex = p["colorHex"].as<String>();
                players[numPlayers].position = p["position"];
                numPlayers++;
            }
        }
        currentTurn = doc["current_turn"] | 0;
        gameStarted = doc["game_started"] | false;
        gameOver    = doc["game_over"] | false;
        playerCounter = numPlayers;

        Serial.printf("State sync: %d players, turn=%d, started=%d\n",
                       numPlayers, currentTurn, gameStarted);
        updateLcdFull();
    }

    // ── Player added ──
    else if (strcmp(type, "player_added") == 0) {
        if (numPlayers < 8) {
            JsonObject p = doc["player"];
            players[numPlayers].id       = p["id"];
            players[numPlayers].name     = p["name"].as<String>();
            players[numPlayers].color    = p["color"].as<String>();
            players[numPlayers].colorHex = p["colorHex"].as<String>();
            players[numPlayers].position = p["position"];
            numPlayers++;
            playerCounter = numPlayers;

            String msg1 = players[numPlayers - 1].name + " joined!";
            String msg2 = String(numPlayers) + " player(s)";
            oledShowMessage("PLAYER ADDED", msg1.c_str(), msg2.c_str());
            playTone(800, 100);
            delay(50);
            playTone(1200, 100);

            updateLcdFull();
            Serial.printf("Player added: %s (total: %d)\n",
                           players[numPlayers - 1].name.c_str(), numPlayers);
        }
    }

    // ── Game started ──
    else if (strcmp(type, "game_started") == 0) {
        gameStarted = true;
        gameOver = false;
        currentTurn = doc["current_turn"] | 0;

        // Sync positions
        JsonArray arr = doc["players"].as<JsonArray>();
        int i = 0;
        for (JsonObject p : arr) {
            if (i < 8) {
                players[i].position = p["position"];
                i++;
            }
        }

        oledShowMessage("GAME ON!", players[currentTurn].name.c_str(), "rolls first!");
        playMelody_Start();
        updateLcdFull();
        Serial.println("Game started!");
    }

    // ── Dice result ──
    else if (strcmp(type, "dice_result") == 0) {
        int playerId = doc["player_id"];
        int value    = doc["value"];
        lastDiceValue = value;

        String pName = getPlayerName(playerId);

        // Dice animation on OLED
        animateDiceOLED(value);

        // Buzzer rattle
        playDiceSound();

        // Show result
        String line1 = pName + " rolled";
        String line2 = "[ " + String(value) + " ]";
        oledShowDice(line1.c_str(), value);

        Serial.printf("Dice: %s rolled %d\n", pName.c_str(), value);
    }

    // ── Player moved ──
    else if (strcmp(type, "player_moved") == 0) {
        int playerId = doc["player_id"];
        int fromPos  = doc["from"];
        int toPos    = doc["to"];

        for (int i = 0; i < numPlayers; i++) {
            if (players[i].id == playerId) {
                players[i].position = toPos;
                break;
            }
        }
        updateLcdFull();

        Serial.printf("Moved: player %d from %d to %d\n", playerId, fromPos, toPos);
    }

    // ── Player renamed (from laptop keyboard) ──
    else if (strcmp(type, "player_renamed") == 0) {
        int pId = doc["id"];
        const char* newName = doc["name"];
        for (int i = 0; i < numPlayers; i++) {
            if (players[i].id == pId) {
                players[i].name = String(newName);
                break;
            }
        }
        updateLcdFull();
        if (!gameStarted) {
            oledShowMessage("NAME UPDATED", newName, "Ready!");
        }
        Serial.printf("Player %d renamed to: %s\n", pId + 1, newName);
    }

    // ── Snake hit ──
    else if (strcmp(type, "snake_hit") == 0) {
        int playerId = doc["player_id"];
        int fromPos  = doc["from"];
        int toPos    = doc["to"];

        for (int i = 0; i < numPlayers; i++) {
            if (players[i].id == playerId) {
                players[i].position = toPos;
                break;
            }
        }

        String pName = getPlayerName(playerId);
        animateSnakeBite(pName.c_str(), fromPos, toPos);
        updateLcdFull();

        Serial.printf("Snake! %s: %d -> %d\n", pName.c_str(), fromPos, toPos);
    }

    // ── Ladder hit ──
    else if (strcmp(type, "ladder_hit") == 0) {
        int playerId = doc["player_id"];
        int fromPos  = doc["from"];
        int toPos    = doc["to"];

        for (int i = 0; i < numPlayers; i++) {
            if (players[i].id == playerId) {
                players[i].position = toPos;
                break;
            }
        }

        String pName = getPlayerName(playerId);
        animateLadderClimb(pName.c_str(), fromPos, toPos);
        updateLcdFull();

        Serial.printf("Ladder! %s: %d -> %d\n", pName.c_str(), fromPos, toPos);
    }

    // ── Turn changed ──
    else if (strcmp(type, "turn_changed") == 0) {
        currentTurn = doc["current_turn"] | 0;

        if (currentTurn < numPlayers) {
            String msg = players[currentTurn].name + "'s turn";
            oledShowTurn(players[currentTurn].name.c_str(), currentTurn + 1);
        }

        Serial.printf("Turn: %d (%s)\n", currentTurn,
                       currentTurn < numPlayers ? players[currentTurn].name.c_str() : "?");
    }

    // ── Game won ──
    else if (strcmp(type, "game_won") == 0) {
        gameOver = true;
        const char* winnerName = doc["player_name"];

        animateWinnerOLED(winnerName);

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("** WINNER! **");
        lcd.setCursor(0, 1);
        lcd.print(winnerName);

        Serial.printf("WINNER: %s\n", winnerName);
    }

    // ── Game reset ──
    else if (strcmp(type, "game_reset") == 0) {
        numPlayers = 0;
        currentTurn = 0;
        gameStarted = false;
        gameOver = false;
        playerCounter = 0;
        lastDiceValue = 0;

        oledShowMessage("READY!", "Select Players", "with BTN 2");
        playTone(400, 200);

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Ready! Add/Select");
        lcd.setCursor(0, 1);
        lcd.print("players (BTN 2)");

        Serial.println("Game reset — ready to select players from start!");
    }

    // ── Error ──
    else if (strcmp(type, "error") == 0) {
        const char* errMsg = doc["message"];
        oledShowMessage("ERROR", errMsg, "");
        playTone(200, 500);
        Serial.printf("Error: %s\n", errMsg);
    }
}

// ==========================================================
//  BUTTON HANDLING
// ==========================================================

void handleButtons() {
    unsigned long now = millis();

    // ── Player Select Button (GPIO 26) ──
    bool playerBtnState = (digitalRead(BTN_PLAYER) == LOW);

    if (playerBtnState && !btnPlayerPressed) {
        // Button just pressed
        btnPlayerPressed = true;
        btnPlayerDownTime = now;
    }

    if (playerBtnState && btnPlayerPressed) {
        // Check for long press while held
        if ((now - btnPlayerDownTime) >= LONG_PRESS_MS) {
            // Long press detected — reset game
            btnPlayerPressed = false; // prevent re-trigger
            Serial.println("BTN: Long press -> Reset Game");
            oledShowMessage("RESETTING", "Game...", "");
            playTone(300, 300);

            if (wsConnected) {
                StaticJsonDocument<64> doc;
                doc["type"] = "reset_game";
                String json;
                serializeJson(doc, json);
                wsClient.send(json);
            }
            delay(500); // debounce after long press
        }
    }

    if (!playerBtnState && btnPlayerPressed) {
        // Button released
        unsigned long pressDuration = now - btnPlayerDownTime;
        btnPlayerPressed = false;

        if (pressDuration >= DEBOUNCE_MS && pressDuration < LONG_PRESS_MS) {
            // Short press
            if (now - lastBtnPlayerRelease < DEBOUNCE_MS) return; // extra debounce
            lastBtnPlayerRelease = now;

            if (!gameStarted && !gameOver) {
                // Rotate back to 1 if already at 8 players
                if (numPlayers >= 8) {
                    playerCounter = 1;
                } else {
                    playerCounter++;
                }
                String name = "Player " + String(playerCounter);
                Serial.printf("BTN: Cycle/Add player '%s' (current: %d)\n", name.c_str(), numPlayers);
                playTone(800, 40);

                if (wsConnected) {
                    StaticJsonDocument<128> doc;
                    doc["type"] = "add_player";
                    doc["name"] = name;
                    String json;
                    serializeJson(doc, json);
                    wsClient.send(json);
                }
            } else {
                // User Requirement 4: While playing, pressing player select exits back to start!
                Serial.println("BTN: Player Select pressed during game -> EXIT TO START");
                oledShowMessage("EXITING...", "Returning to", "Player Select");
                playTone(400, 200);

                if (wsConnected) {
                    StaticJsonDocument<64> doc;
                    doc["type"] = "reset_game";
                    String json;
                    serializeJson(doc, json);
                    wsClient.send(json);
                }
            }
        }
    }

    // ── Start / Roll Button (GPIO 32) ──
    bool rollBtnState = (digitalRead(BTN_ROLL) == LOW);

    if (rollBtnState && !btnRollPressed) {
        btnRollPressed = true;

        if (now - lastBtnRollPress < DEBOUNCE_MS) {
            btnRollPressed = false;
            return;
        }
        lastBtnRollPress = now;

        if (!gameStarted && !gameOver && numPlayers >= 2) {
            // Start game
            Serial.println("BTN: Start Game");
            if (wsConnected) {
                StaticJsonDocument<64> doc;
                doc["type"] = "start_game";
                String json;
                serializeJson(doc, json);
                wsClient.send(json);
            }
        } else if (gameStarted && !gameOver) {
            // Roll dice
            Serial.println("BTN: Roll Dice");
            if (wsConnected) {
                StaticJsonDocument<64> doc;
                doc["type"] = "roll_dice";
                String json;
                serializeJson(doc, json);
                wsClient.send(json);
            }
        } else if (!gameStarted && numPlayers < 2) {
            oledShowMessage("Need 2+", "players to", "start!");
            playTone(200, 200);
        }
    }

    if (!rollBtnState && btnRollPressed) {
        btnRollPressed = false;
    }
}

// ==========================================================
//  OLED DISPLAY FUNCTIONS
// ==========================================================

void oledShowMessage(const char* line1, const char* line2, const char* line3) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_helvB10_tr);
    oled.drawStr(0, 14, line1);
    oled.setFont(u8g2_font_helvR08_tr);
    oled.drawStr(0, 34, line2);
    oled.drawStr(0, 50, line3);
    oled.sendBuffer();
}

void oledShowDice(const char* playerInfo, int value) {
    oled.clearBuffer();

    // Player name at top
    oled.setFont(u8g2_font_helvR08_tr);
    oled.drawStr(0, 10, playerInfo);

    // Large dice value in center
    oled.setFont(u8g2_font_logisoso32_tn);
    char valStr[4];
    snprintf(valStr, sizeof(valStr), "%d", value);
    int w = oled.getStrWidth(valStr);
    oled.drawStr((128 - w) / 2, 55, valStr);

    // Dice box
    oled.drawRFrame(36, 18, 56, 48, 8);

    oled.sendBuffer();
}

void oledShowTurn(const char* playerName, int playerNum) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_helvR08_tr);
    oled.drawStr(0, 10, "YOUR TURN!");

    oled.setFont(u8g2_font_helvB14_tr);
    oled.drawStr(0, 35, playerName);

    char numStr[16];
    snprintf(numStr, sizeof(numStr), "Player %d", playerNum);
    oled.setFont(u8g2_font_helvR08_tr);
    oled.drawStr(0, 55, numStr);
    oled.drawStr(80, 55, "Press Roll");

    oled.sendBuffer();
}

void animateSnakeBite(const char* name, int from, int to) {
    // 1. Initial shock effect
    oled.clearBuffer();
    oled.setFont(u8g2_font_helvB14_tr);
    oled.drawStr((128 - oled.getStrWidth("OH NO!")) / 2, 28, "OH NO!");
    oled.setFont(u8g2_font_helvB10_tr);
    oled.drawStr((128 - oled.getStrWidth("SNAKE BITE!")) / 2, 48, "SNAKE BITE!");
    oled.drawRFrame(4, 4, 120, 56, 4);
    oled.sendBuffer();
    tone(BUZZER_PIN, 900, 100);
    delay(300);

    // 2. Animated Chomping Jaws (fangs closing in!)
    for (int step = 0; step <= 8; step++) {
        oled.clearBuffer();
        int fangClose = step * 2; // fangs move inward
        
        // Upper jaw fangs pointing down
        for (int x = 20; x < 108; x += 16) {
            oled.drawTriangle(x, 0, x + 8, fangClose + 14, x + 16, 0);
        }
        // Lower jaw fangs pointing up
        for (int x = 28; x < 100; x += 16) {
            oled.drawTriangle(x, 63, x + 8, 63 - (fangClose + 14), x + 16, 63);
        }

        oled.setFont(u8g2_font_helvB10_tr);
        if (step >= 5) {
            oled.drawStr((128 - oled.getStrWidth("CHOMP!")) / 2, 36, "CHOMP!");
        } else {
            oled.drawStr((128 - oled.getStrWidth("SNAKE!")) / 2, 36, "SNAKE!");
        }
        oled.sendBuffer();
        
        // Descending bite tone
        tone(BUZZER_PIN, 800 - (step * 70), 30);
        delay(60);
    }
    
    // Slither / slide down sound
    for (int freq = 700; freq >= 180; freq -= 25) {
        tone(BUZZER_PIN, freq, 25);
        delay(25);
    }
    noTone(BUZZER_PIN);

    // 3. Drop down position result
    oled.clearBuffer();
    oled.setFont(u8g2_font_helvB10_tr);
    oled.drawStr(8, 20, name);
    oled.setFont(u8g2_font_helvR08_tr);
    oled.drawStr(8, 36, "Slid down:");
    
    char posStr[24];
    snprintf(posStr, sizeof(posStr), "%d  -->  %d", from, to);
    oled.setFont(u8g2_font_helvB14_tr);
    oled.drawStr(8, 58, posStr);
    oled.sendBuffer();
    
    tone(BUZZER_PIN, 180, 400);
    delay(1200);
}

void animateLadderClimb(const char* name, int from, int to) {
    int ladderNotes[] = {262, 330, 392, 523, 659, 784, 1047}; // 7 ascending notes
    
    // Animated climbing ladder (rungs scrolling upward)
    for (int frame = 0; frame < 14; frame++) {
        oled.clearBuffer();
        
        // Text on sides
        oled.setFont(u8g2_font_helvB10_tr);
        oled.drawStr(4, 30, "LADDER");
        oled.drawStr(4, 46, "CLIMB!");
        
        oled.drawStr(94, 38, "UP!");

        // Center vertical rails
        oled.drawVLine(56, 0, 64);
        oled.drawVLine(76, 0, 64);
        
        // Rungs scrolling upwards
        int offset = (frame * 3) % 12;
        for (int y = -12; y < 70; y += 12) {
            int rungY = y - offset;
            if (rungY >= 0 && rungY < 64) {
                oled.drawHLine(56, rungY, 21);
            }
        }
        
        // Climber dot moving up
        int dotY = 56 - (frame * 3);
        if (dotY >= 10) {
            oled.drawDisc(66, dotY, 3);
        }
        
        oled.sendBuffer();
        
        // Play rising notes
        if (frame < 7) {
            tone(BUZZER_PIN, ladderNotes[frame], 90);
        } else {
            tone(BUZZER_PIN, ladderNotes[6] + (frame - 7) * 80, 90);
        }
        delay(90);
    }
    noTone(BUZZER_PIN);

    // Show final climbed position
    oled.clearBuffer();
    oled.setFont(u8g2_font_helvB10_tr);
    oled.drawStr(8, 20, name);
    oled.setFont(u8g2_font_helvR08_tr);
    oled.drawStr(8, 36, "Climbed up to:");
    
    char posStr[24];
    snprintf(posStr, sizeof(posStr), "%d  ==>  %d", from, to);
    oled.setFont(u8g2_font_helvB14_tr);
    oled.drawStr(8, 58, posStr);
    oled.sendBuffer();
    
    tone(BUZZER_PIN, 1200, 200);
    delay(1200);
}

void animateWinnerOLED(const char* name) {
    // Fanfare melody notes & durations
    int winNotes[] = {523, 523, 523, 659, 784, 1047, 784, 1047};
    int winDurs[]  = {140, 140, 140, 350, 250, 450, 200, 600};

    for (int frame = 0; frame < 8; frame++) {
        oled.clearBuffer();
        
        // Fireworks / starburst rays
        int cx = 64, cy = 32;
        for (int a = 0; a < 8; a++) {
            float angle = a * (3.14159 / 4.0) + (frame * 0.2);
            int r1 = 18 + (frame % 3) * 4;
            int r2 = 28 + (frame % 3) * 6;
            int x1 = cx + cos(angle) * r1;
            int y1 = cy + sin(angle) * r1;
            int x2 = cx + cos(angle) * r2;
            int y2 = cy + sin(angle) * r2;
            oled.drawLine(x1, y1, x2, y2);
        }
        
        // Alternating Trophy / Winner Box
        if (frame % 2 == 0) {
            oled.drawRFrame(12, 12, 104, 42, 6);
            oled.setDrawColor(1);
        } else {
            oled.drawBox(12, 12, 104, 42);
            oled.setDrawColor(0); // Inverted text on solid box
        }
        
        oled.setFont(u8g2_font_helvB10_tr);
        int w1 = oled.getStrWidth("WINNER!");
        oled.drawStr((128 - w1) / 2, 28, "WINNER!");
        
        int w2 = oled.getStrWidth(name);
        oled.drawStr((128 - w2) / 2, 46, name);
        
        oled.setDrawColor(1); // restore draw color
        oled.sendBuffer();
        
        tone(BUZZER_PIN, winNotes[frame], winDurs[frame]);
        delay(winDurs[frame] + 40);
    }
    noTone(BUZZER_PIN);
    
    // Hold final celebration screen
    oled.clearBuffer();
    oled.drawRFrame(2, 2, 124, 60, 4);
    oled.drawRFrame(4, 4, 120, 56, 4);
    oled.setFont(u8g2_font_helvB14_tr);
    int w1 = oled.getStrWidth("CHAMPION!");
    oled.drawStr((128 - w1) / 2, 26, "CHAMPION!");
    oled.setFont(u8g2_font_helvB10_tr);
    int w2 = oled.getStrWidth(name);
    oled.drawStr((128 - w2) / 2, 48, name);
    oled.sendBuffer();
}

void animateDiceOLED(int finalValue) {
    // Show random values cycling for animation effect and play random buzzer tones
    for (int i = 0; i < 12; i++) {
        int randomVal = random(1, 7);
        oled.clearBuffer();
        oled.setFont(u8g2_font_logisoso32_tn);
        char valStr[4];
        snprintf(valStr, sizeof(valStr), "%d", randomVal);
        int w = oled.getStrWidth(valStr);
        oled.drawStr((128 - w) / 2, 50, valStr);
        oled.drawRFrame(36, 12, 56, 48, 8);
        oled.sendBuffer();
        
        tone(BUZZER_PIN, random(500, 1200), 30);
        delay(100 + (i * 10));  // gradually slower, more delay
    }
}

// ==========================================================
//  LCD SCOREBOARD
// ==========================================================

void updateLcdFull() {
    if (numPlayers == 0) return;
    updateLcdPage(0);
}

void updateLcdScoreboard() {
    // Only scroll if > 4 players
    if (numPlayers <= 4) return;

    unsigned long now = millis();
    if (now - lastLcdPage > LCD_PAGE_INTERVAL) {
        lastLcdPage = now;
        int totalPages = (numPlayers + 3) / 4; // 4 players per page (2 per line)
        lcdPage = (lcdPage + 1) % totalPages;
        updateLcdPage(lcdPage);
    }
}

void updateLcdPage(int page) {
    lcd.clear();

    int startIdx = page * 4;

    // Line 1: two players
    lcd.setCursor(0, 0);
    for (int i = startIdx; i < startIdx + 2 && i < numPlayers; i++) {
        char buf[9];
        snprintf(buf, sizeof(buf), "P%d:%-3d ", players[i].id + 1, players[i].position);
        lcd.print(buf);
    }

    // Line 2: next two players
    lcd.setCursor(0, 1);
    for (int i = startIdx + 2; i < startIdx + 4 && i < numPlayers; i++) {
        char buf[9];
        snprintf(buf, sizeof(buf), "P%d:%-3d ", players[i].id + 1, players[i].position);
        lcd.print(buf);
    }

    // If game started and this page has the current player, add marker
    if (gameStarted && currentTurn >= startIdx && currentTurn < startIdx + 4) {
        // Show arrow or asterisk next to current player
        int relIdx = currentTurn - startIdx;
        int row = relIdx / 2;
        int col = (relIdx % 2) * 8 + 7; // after the position value
        if (col < 16) {
            lcd.setCursor(col, row);
            lcd.print("*");
        }
    }
}

// ==========================================================
//  BUZZER SOUND EFFECTS
// ==========================================================

void playTone(int freq, int duration) {
    tone(BUZZER_PIN, freq, duration);
    delay(duration);
    noTone(BUZZER_PIN);
}

void playDiceSound() {
    // Final roll sound from old code
    for (int i = 200; i <= 1000; i += 100) {
        tone(BUZZER_PIN, i, 20);
        delay(20);
    }
    noTone(BUZZER_PIN);
}

void playSnakeSound() {
    // Descending tone — uh oh!
    for (int freq = 800; freq >= 200; freq -= 50) {
        tone(BUZZER_PIN, freq, 30);
        delay(40);
    }
    noTone(BUZZER_PIN);
}

void playLadderSound() {
    // Ascending tone — yay!
    for (int freq = 300; freq <= 1200; freq += 75) {
        tone(BUZZER_PIN, freq, 30);
        delay(35);
    }
    noTone(BUZZER_PIN);
}

void playWinSound() {
    // Victory fanfare
    int melody[] = {523, 659, 784, 1047, 784, 1047};
    int durations[] = {150, 150, 150, 300, 150, 500};
    for (int i = 0; i < 6; i++) {
        tone(BUZZER_PIN, melody[i], durations[i]);
        delay(durations[i] + 30);
    }
    noTone(BUZZER_PIN);
}

void playMelody_Start() {
    // Short "game start" jingle
    int notes[] = {660, 880, 1100};
    for (int i = 0; i < 3; i++) {
        tone(BUZZER_PIN, notes[i], 100);
        delay(130);
    }
    noTone(BUZZER_PIN);
}

// ==========================================================
//  HELPER FUNCTIONS
// ==========================================================

String getPlayerName(int playerId) {
    for (int i = 0; i < numPlayers; i++) {
        if (players[i].id == playerId) {
            return players[i].name;
        }
    }
    return "Player ?";
}
