/**
 * @file main.h
 * @author SenMorgan https://github.com/SenMorgan
 * @date 2023-01-16
 *
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <Arduino_JSON.h>
#include "LittleFS.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// Interval to wait for Wi-Fi connection (milliseconds)
#define WIFI_TIMEOUT_MS 10000

// Maximum water temperature for emergency powering on relay with notification
#define WATER_ALARM_TEMP_HI 90.0
#define WATER_ALARM_TEMP_LO 85.0

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 5
#define DHT_PIN      14
#define STATUS_LED   2
#define RELAY_PIN    4
#define LED_GREEN    12
#define LED_BLUE     13
#define LED_RED      15
#define BUZZER_PIN   10

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

DHT_Unified dht(DHT_PIN, DHT11);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature dallas(&oneWire);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create an Event Source on /events
AsyncEventSource events("/events");

// Json Variable to Hold Sensor Readings
JSONVar readings;

// Search for parameter in HTTP POST request
const char *PARAM_INPUT_1 = "ssid";
const char *PARAM_INPUT_2 = "pass";
const char *PARAM_INPUT_3 = "ip";
const char *PARAM_INPUT_4 = "gateway";

// Variables to save values from HTML form
String ssid, pass, ip, gateway;

// File paths to save input values permanently
const char *ssid_path = "/ssid.txt";
const char *pass_path = "/pass.txt";
const char *ip_path = "/ip.txt";
const char *gateway_path = "/gateway.txt";

IPAddress local_ip;
// IPAddress local_ip(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress local_gateway;
IPAddress subnet(255, 255, 0, 0);

String status_led_state;
boolean restart, relay_state, buzzer_state;
float air_temp, air_hum, water_temp;
float water_temp_threshold_hi = 60.0;
float water_temp_threshold_lo = 55.0;

// Get Sensor data and return JSON object
String get_sensors_data()
{
    readings["temperature"] = air_temp;
    readings["humidity"] = air_hum;
    readings["water_temp"] = water_temp;
    readings["water_temp_threshold_hi"] = water_temp_threshold_hi;
    readings["water_temp_threshold_lo"] = water_temp_threshold_lo;

    String jsonString = JSON.stringify(readings);
    return jsonString;
}

// Initialize LittleFS
void fs_init()
{
    if (!LittleFS.begin())
    {
        Serial.println("An error has occurred while mounting LittleFS");
    }
    else
    {
        Serial.println("LittleFS mounted successfully");
    }
}

// Read File from LittleFS
String file_read(fs::FS &fs, const char *path)
{
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path, "r");
    if (!file || file.isDirectory())
    {
        Serial.println("- failed to open file for reading");
        return String();
    }

    String fileContent;
    while (file.available())
    {
        fileContent = file.readStringUntil('\n');
        break;
    }
    file.close();
    return fileContent;
}

// Write file to LittleFS
void file_write(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, "w");
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- file written");
    }
    else
    {
        Serial.println("- frite failed");
    }
    file.close();
}

// Initialize WiFi
bool wifi_init()
{
    unsigned long timestamp = 0;

    if (ssid == "" || ip == "")
    {
        Serial.println("Undefined SSID or IP address.");
        return false;
    }

    WiFi.mode(WIFI_STA);
    local_ip.fromString(ip.c_str());
    local_gateway.fromString(gateway.c_str());

    if (!WiFi.config(local_ip, local_gateway, subnet))
    {
        Serial.println("STA Failed to configure");
        return false;
    }
    WiFi.begin(ssid.c_str(), pass.c_str());

    Serial.print("Connecting to WiFi");
    timestamp = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - timestamp < WIFI_TIMEOUT_MS)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" Connected");

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Failed to connect.");
        return false;
    }

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    return true;
}

String processor(const String &var)
{
    if (var == "RELAY_STATE")
    {
        return relay_state ? "ON" : "OFF";
    }
    return String();
}

void kalorifer_web_page()
{
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(LittleFS, "/index.html", "text/html", false, processor); });

    server.serveStatic("/", LittleFS, "/");

    // Route to set GPIO state to HIGH
    server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                    digitalWrite(RELAY_PIN, HIGH);
                    digitalWrite(STATUS_LED, LOW);
                    relay_state = true;
                    request->send(LittleFS, "/index.html", "text/html", false, processor); });

    // Route to set GPIO state to LOW
    server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                    digitalWrite(RELAY_PIN, LOW);
                    digitalWrite(STATUS_LED, HIGH);
                    relay_state = false;
                    request->send(LittleFS, "/index.html", "text/html", false, processor); });

    // Request for the latest sensor readings
    server.on("/readings", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                    String json = get_sensors_data();
                    request->send(200, "application/json", json);
                    json = String(); });

    events.onConnect([](AsyncEventSourceClient *client)
                     {
                    if(client->lastId()){
                    Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
                    }
                    // send event with message "hello!", id current millis
                    // and set reconnect delay to 1 second
                    client->send("hello!", NULL, millis(), 10000); });
    server.addHandler(&events);

    // Start web server
    server.begin();
}

void wifi_manager_web_page()
{
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP(AP_SSID, AP_PASS);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(LittleFS, "/wifimanager.html", "text/html"); });

    server.serveStatic("/", LittleFS, "/");

    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
              {
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            file_write(LittleFS, ssid_path, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            file_write(LittleFS, pass_path, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Write file to save value
            file_write(LittleFS, ip_path, ip.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            gateway = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(gateway);
            // Write file to save value
            file_write(LittleFS, gateway_path, gateway.c_str());
          }
          //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      restart = true;
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip); });
    server.begin();
}

void setup()
{
    // Serial port for debugging purposes
    Serial.begin(115200);

    fs_init();

    pinMode(STATUS_LED, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    digitalWrite(STATUS_LED, HIGH);

    dallas.begin();

    dht.begin();

    // Load values saved in LittleFS
    ssid = file_read(LittleFS, ssid_path);
    pass = file_read(LittleFS, pass_path);
    ip = file_read(LittleFS, ip_path);
    gateway = file_read(LittleFS, gateway_path);
    Serial.println(ssid);
    Serial.println(pass);
    Serial.println(ip);
    Serial.println(gateway);

    if (wifi_init())
    {
        kalorifer_web_page();
    }
    else
    {
        wifi_manager_web_page();
    }
}

void send_data_to_web_page(uint32_t interval)
{
    static uint32_t timestamp;

    if ((millis() - timestamp) > interval)
    {
        // Send Events to the client with the Sensor Readings Every 30 seconds
        events.send("ping", NULL, millis());
        events.send(get_sensors_data().c_str(), "new_readings", millis());
        timestamp = millis();
    }
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    analogWrite(LED_RED, r);
    analogWrite(LED_GREEN, g);
    analogWrite(LED_BLUE, b);
}

void read_sensors(uint32_t interval)
{
    static uint32_t timestamp;

    if (millis() - timestamp > interval)
    {
        // Get temperature event and print its value.
        sensors_event_t event;
        dht.temperature().getEvent(&event);
        if (!isnan(event.temperature))
        {
            air_temp = event.temperature;
            Serial.print(F("DHT temp: "));
            Serial.print(air_temp);
            Serial.println(F("°C"));
        }
        else
        {
            Serial.println(F("Error reading temperature!"));
        }
        // Get humidity event and print its value.
        dht.humidity().getEvent(&event);
        if (!isnan(event.relative_humidity))
        {
            air_hum = event.relative_humidity;
            Serial.print(F("DHT hum: "));
            Serial.print(air_hum);
            Serial.println(F("%"));
        }
        else
        {
            Serial.println(F("Error reading humidity!"));
        }

        dallas.requestTemperatures();
        float val = dallas.getTempCByIndex(0);
        if (val != DEVICE_DISCONNECTED_C)
        {
            water_temp = val;
            Serial.print("DS18B20 Temperature: ");
            Serial.println(water_temp);
        }
        else
        {
            Serial.println("Error reading temperature!");
        }

        if (water_temp >= 95.0)
        {
            set_rgb(255, 0, 0);
        }
        else if (water_temp >= 80.0)
        {
            set_rgb(255, 140, 0);
        }
        else if (water_temp >= 60.0)
        {
            set_rgb(255, 255, 0);
        }
        else if (water_temp >= 35.0)
        {
            set_rgb(0, 255, 255);
        }
        else
        {
            set_rgb(0, 0, 0);
        }

        timestamp = millis();
    }
}

void relay_process()
{
    static bool cooling_state;

    if (water_temp >= water_temp_threshold_hi && !cooling_state)
    {
        digitalWrite(RELAY_PIN, HIGH);
        digitalWrite(STATUS_LED, LOW);
        cooling_state = true;
        relay_state = true;
    }
    else if (water_temp <= water_temp_threshold_lo && cooling_state)
    {
        digitalWrite(RELAY_PIN, LOW);
        digitalWrite(STATUS_LED, LOW);
        cooling_state = false;
        relay_state = true;
    }
}

void temp_alarm()
{
    static bool alarm_state;

    if (water_temp >= WATER_ALARM_TEMP_HI && !alarm_state)
    {
        // Toggle kalorifer motor relay a few times for notification
        for (int i = 0; i < 3; i++)
        {
            digitalWrite(RELAY_PIN, LOW);
            delay(500);
            digitalWrite(RELAY_PIN, HIGH);
            delay(500);
        }

        alarm_state = true;
        relay_state = true;
        buzzer_state = true;
    }
    else if (water_temp <= WATER_ALARM_TEMP_LO && alarm_state)
    {
        alarm_state = false;
        buzzer_state = false;
    }
}

void buzzer_process(uint32_t interval)
{
    static boolean prev_state, buzzing;
    static uint32_t last_time;

    if (buzzer_state)
    {
        if (prev_state != buzzer_state)
            prev_state = buzzer_state;

        if (millis() - last_time > interval)
        {
            last_time = millis();
            buzzing = !buzzing;
            digitalWrite(BUZZER_PIN, buzzing);
        }
    }
    else if (buzzer_state != prev_state)
    {
        prev_state = buzzer_state;
        digitalWrite(BUZZER_PIN, LOW);
    }
}

void loop()
{
    if (restart)
    {
        delay(5000);
        ESP.restart();
    }

    read_sensors(1000);
    relay_process();
    buzzer_process(500);

    send_data_to_web_page(3000);
}
