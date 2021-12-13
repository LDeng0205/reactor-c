#include <kobukiActuator.h>
#include <kobukiSensorTypes.h>
#include <kobukiUART.h>
#include <kobukiSensor.h>
#include <kobukiUtilities.h>
#include <kobukiSensorPoll.h>

int8_t incomingByte = -10; // for incoming serial data
uint8_t startSeq = 0xAA;
uint8_t headerSeq = 0xFF;

int8_t leftTarget = 0;
int8_t rightTarget = 0;
int8_t metadata = 0; 

int base_time = 0;
int current_time = 0;

int actualLeft = 0;
int actualRight = 0;

bool shouldRunChair = false;

KobukiSensors_t sensors = {0};
char buff[50];
uint8_t cliffBumpChar[3];
uint8_t receivedEncoderVals[4];

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  Serial2.begin(115200);
  Serial2.setTimeout(100);
  base_time = millis();
  cliffBumpChar[0] = 0xAA;
  cliffBumpChar[1] = 0x00;
  cliffBumpChar[2] = 0xFF; 
}

bool isCliff(){
  return sensors.cliffLeft || sensors.cliffRight || sensors.cliffCenter;
}

bool isBump(){
  return sensors.bumps_wheelDrops.bumpLeft || sensors.bumps_wheelDrops.bumpCenter || sensors.bumps_wheelDrops.bumpRight;
}

void loop() {
  // put your main code here, to run repeatedly:
  if((millis() - 100) > base_time){
      base_time = millis();
      Serial2.write(cliffBumpChar, 3);
      kobukiDriveDirect(receivedEncoderVals[0],receivedEncoderVals[1]);
  }
  
  kobukiSensorPoll(&sensors);
  cliffBumpChar[1] = (isBump() << 1) + isCliff();
  if (Serial2.available() > 0) {
  //Indicates start of sequence
        startSeq = Serial2.read();
        if(startSeq == 0xFF){
            while(Serial2.available() <= 3);
            Serial2.readBytesUntil(0xAB, receivedEncoderVals, 4);
           sprintf(buff, "%d | %d | %d", receivedEncoderVals[0], receivedEncoderVals[1], receivedEncoderVals[2]);
           Serial.println(buff);
        }
  }
}