#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN 22
#define SS_PIN 5
#define ENA_PIN 21
#define IN1_PIN 26
#define IN2_PIN 27
#define TRIGGER_PIN 4 // Button to trigger the motor (connected to GND when pressed)

MFRC522 mfrc522(SS_PIN, RST_PIN);
String currentHead = "No Head found";

String getUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uid += String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.trim();
  uid.toUpperCase();
  return uid;
}

void stopEngine() {
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, HIGH);
  analogWrite(ENA_PIN, 0);
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  
  pinMode(ENA_PIN, OUTPUT);
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  
  pinMode(TRIGGER_PIN, INPUT_PULLUP); 
  stopEngine();
  
  Serial.println("\n--- OmniDrive: System bereit ---");
}

void loop() {
  // 1. Check for RFID card and identify the tool
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String tagUID = getUID();
    if (tagUID == "7C 7A EA 2F") currentHead = "drill";
    else if (tagUID == "26 81 1F D2") currentHead = "screwdriver";
    else currentHead = "unknown";
    
    Serial.println("recognized: " + currentHead + " (Waiting for trigger...)");
    delay(500); 
  }

  // 2. Check if the trigger button is pressed and control the motor accordingly
  if (digitalRead(TRIGGER_PIN) == LOW) { // LOW = Taster ist gedrückt (mit GND verbunden)
    if (currentHead == "drill") {
      digitalWrite(IN1_PIN, HIGH); digitalWrite(IN2_PIN, LOW); analogWrite(ENA_PIN, 255);
    } else if (currentHead == "screwdriver") {
      digitalWrite(IN1_PIN, HIGH); digitalWrite(IN2_PIN, LOW); analogWrite(ENA_PIN, 120);
    } else {
      stopEngine(); // unrecognized tool, stop the motor
    }
  } else {
    stopEngine(); // button not pressed, stop the motor
  }
}