#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>      

// Timer to keep track of heartbeat
static unsigned long heartbeat_timer = 0;
static bool wifiConnected = false;
static bool mqttConnected = false;
static unsigned long lastReconnectAttempt = 0;
static unsigned long lastReconnectError = 0;

// Variables for omron
static bool omron_response = false;
static unsigned long omron_start_time = 0;
static unsigned long send_command_start_time = 0;
static unsigned long omron_response_length = 0;
const int command_length = 7;
byte command[command_length] = {0xFE, 0x4, 0x3, 0x0, 0x34, 0x1, 0x0};

// Pin Definition
// Wemos LED
const int LED_PIN = 2;

// RX (D2) | TX (D1)
SoftwareSerial ESPserial(4, 5);

// Use for secure TLS MQTT
WiFiClientSecure wifiClient;

// WiFi Credentials
char wifi_ssid[50]="WiFi_ssid";
char wifi_password[50]="WiFi_password";

// MQTT Connection info
char server[30] = "mqtt.mediumone.com";
int port = 61620;

char project_mqtt[20] = "Project_MQTT_ID";
char api_key[50] = "API_key";
char username[50] = "User_MQTT_ID";
char password[50]="User_password";

char pub_topic[100]="";
char sub_topic[100]="";
char mqtt_username[100]="";
char mqtt_password[150]="";

// Setup function
void setup() {
  Serial.begin(9600);
  ESPserial.begin(9600);  
  
  while(!Serial){}
  
  delay(2000);
  Serial.println("");
  Serial.println("##### M1 Cloud IO #####");
  Serial.println("Init hardware");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // Connect to the WiFi.
  wifiConnect();
  
  // Setup
  setMQTTPassword();
  setMQTTUsername();
  setPubTopic();
  setSubTopic();

  Serial.println("Ready");
}

// Connect WiFi function
void wifiConnect() {
  wifiConnected = false;
  digitalWrite(LED_PIN, HIGH);
  Serial.print("Connecting to wifi ");
  Serial.println(wifi_ssid);
  WiFi.begin(wifi_ssid, wifi_password);
}

// Setup MQTTUsername
char * setMQTTUsername() {
  memset(mqtt_username, 0, sizeof(mqtt_username));
  strcat(mqtt_username,(char*) project_mqtt);
  strcat(mqtt_username,"/");
  strcat(mqtt_username,(char*) username);
  return mqtt_username;
}

// Setup MQTTPassword
char * setMQTTPassword() {
  memset(mqtt_password, 0, sizeof(mqtt_password));
  strcat(mqtt_password,(char*) api_key);
  strcat(mqtt_password,"/");
  strcat(mqtt_password,(char*) password);
  return mqtt_password;
}

// Setup PubTopic
char * setPubTopic() {
  memset(pub_topic, 0, sizeof(pub_topic));
  strcat(pub_topic,"0/");
  strcat(pub_topic,(char*) project_mqtt);
  strcat(pub_topic,"/");
  strcat(pub_topic,(char*) username);
  strcat(pub_topic,"/esp8266/");
  return pub_topic;
}

// Setup SubTopic
char * setSubTopic() {
  memset(sub_topic, 0, sizeof(sub_topic));
  strcat(sub_topic,"1/");
  strcat(sub_topic,(char*) project_mqtt);
  strcat(sub_topic,"/");
  strcat(sub_topic,(char*) username);
  strcat(sub_topic,"/esp8266/event");
  return sub_topic;
}

PubSubClient client(server, port, callback, wifiClient);

// Connect to MQTT function
boolean connectMQTT()
{ 
  client.disconnect();
  String ipaddress = WiFi.localIP().toString();
  char lan_ip[15]="";
  for(unsigned int i=0;i<15;i++)
    lan_ip[i]=ipaddress[i];

  String mac_address_s = WiFi.macAddress();

  char mac_address_c[18];
  for(int i=0;i<18;i++)
    mac_address_c[i] = mac_address_s[i];
  
  String ssid_s = WiFi.SSID();
  char ssid[20]="";
  for(unsigned int i=0;i<20;i++) {
    if (i < ssid_s.length()) {
      ssid[i]=ssid_s[i];
    }
  }

  if (client.connect((char*) mqtt_username,(char*) mqtt_username,(char*) mqtt_password)) {
    Serial.println("<Notification> Connected to MQTT broker");
    mqttConnected = true;
    
    char total_payload[300] = "{\"event_data\":{\"mqtt_connected\":true}}";
    Serial.print("<Notification> Sending connect msg: ");
    Serial.println(total_payload);

    if (client.publish((char*) pub_topic, total_payload)) {
      Serial.println("<Notification> Publish success ");
    } else {
      Serial.println("<Error> Publish failed: ");
      Serial.println(String(client.state()));
    }
    if (client.subscribe((char *)sub_topic,1)){
      Serial.println("<Notification> Successfully subscribed");
    } 
  }
  else {
    Serial.println("<Error> MQTT connect failed");
    mqttConnected = false;
    long now = millis();
    lastReconnectError = now;
  }
  return client.connected();
}

// Send subscribed data to device
void callback(char* topic, byte* payload, unsigned int length) 
{  
  // handle message arrived
  Serial.print("<Subscribe> Received data: ");
  unsigned int i=0;
  char message_buff[100];
  for(i=0; i<length; i++) {
    message_buff[i] = payload[i];
    // transmit out UART
    ESPserial.write(payload[i]);
    Serial.print(payload[i],HEX);
    Serial.print(',');
  }
  Serial.println("");
}

// Loop function
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      unsigned int long now = millis();
      // try to reconnect after 5 seconds if there was an error
      if (now - lastReconnectError > 5000) {
        // try to reconnect after 1 second
        if (now - lastReconnectAttempt > 1000) {
          lastReconnectAttempt = now;
          // Attempt to reconnect
          if (connectMQTT()) {
            // reset timer to 0 milliseconds after successful reconnect
            lastReconnectAttempt = 0;
          }
        }
      }
    } else {
      // Client connected
      client.loop();
    }
  } else {
    //Serial.println("<Error> WIFI Status not connected ");
  }
  uart_loop_to_mqtt();
  heartbeat_loop();
  uart_send_command_loop();
}

// Heart loop function for every 15 minute heartbeat
void heartbeat_loop() {
  // 10000 = 10 sec
  // 60000 = 1 min
  // 600000 = 10 min
  // 900000 = 15 min
  if ((millis()- heartbeat_timer) > 900000) {
    Serial.print("<INFO> millis: ");
    Serial.print(millis());
    Serial.print("     heartbeat_timer:");
    Serial.println(heartbeat_timer);
    heartbeat_timer = millis();
   
    String payload = "{\"event_data\":{\"millis\":";
    payload += millis();
    payload += ",\"heartbeat\":true}}";
    
    if (client.loop()){
      Serial.print("<Notification> Sending heartbeat: ");
      Serial.println(payload);
  
      if (client.publish((char *) pub_topic, (char*) payload.c_str()) ) {
        Serial.println("<Notification> Publish ok");
      }
      else {
        Serial.print("<Error> Failed to publish heartbeat: ");
        Serial.println(String(client.state()));
      }
    }
  } 
}

void uart_send_command_loop() {
  if (client.loop()) {
    unsigned long now = millis();
    unsigned int i=0;
    if (now - send_command_start_time > 30000) {
      for(i=0; i<command_length; i++) {
        // transmit out UART
        ESPserial.write(command[i]);
        //Serial.print(command[i],HEX);
        //Serial.print(',');
      }
      send_command_start_time = now;
    }
  }
}

void uart_loop_to_mqtt()
{
    static int buffer[100];
    readline(ESPserial.read(), buffer, 100);
        
    if (omron_response) {
      unsigned long now = millis();
      // make sure at least 2 seconds since start of message
      if (now - omron_start_time > 2000) {
        // transmit buffer (assuming 2 sec is enough)
        generate_json_event(buffer);
      }
    }
}

void readline(int readch, int *buffer, int len)
{
  static int pos = 0;

  if (readch >= 0) {
    if(readch == 0xFE) {
        pos = 0;
        omron_response_length = 0;
        omron_response = true;
        omron_start_time = millis();
    }

    if (pos < len-1) {
          buffer[pos++] = readch;
          buffer[pos] = 0;
          omron_response_length = pos;
    }
  }
}

void generate_json_event(int* buffer) {
    char total_payload[300] = "{\"event_data\":{\"omron_response\":\[";
    bool first_entry = true;

    for(unsigned int i=0; i<omron_response_length; i++) {
      if (!first_entry) {
        strcat(total_payload, ",");
      }
      first_entry = false;
      char val[10];
      itoa(buffer[i], val, 10);
      strcat(total_payload, val);
    }
    strcat(total_payload, "]}}");
    Serial.println("");
    Serial.print("<Notification> Generated JSON event: ");
    Serial.println(total_payload);

    // send to cloud
    send_msg(total_payload);

    // reset response flag
    omron_response = false;
}

void send_msg(char *buffer) 
{
  if (!client.loop()) {
    connectMQTT();
  }
  
  if (client.loop()){
    Serial.print("<UART> Sending payload to cloud: ");
    Serial.println(buffer);
    if (client.publish((char *)pub_topic, (char*) buffer)) {
      Serial.println("<UART> Publish ok");
    }
    else {
      Serial.println("<Error> Publish failed");
    }
  }
}
