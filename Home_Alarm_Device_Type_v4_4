/** 
 * AD2SmartThings v4_4.1
 * Couple your Ademco/Honeywell Alarm to your SmartThings Graph using an AD2PI, an Arduino and a ThingShield
 * The Arduino passes all your alarm messages to your SmartThings Graph where they can be processed by the Device Type
 * Use the Device Type to control your alarm or use SmartApps to integrate with other events in your home graph
 *
 *
 ****************************************************************************************************************************
 * Libraries:
 * 
 * An enhanced SmartThings Library was created by  Dan G Ogorchock & Daniel J Ogorchock and their version is required for this implementation.
 * Their enhanced library can found at:
 * https://github.com/DanielOgorchock/ST_Anything/tree/master/Arduino/libraries/SmartThings
 *
 * SoftwareSerial library was default library provided with Arduino IDE
 *
 *
 ****************************************************************************************************************************
 *
 * Pin Configuration for AD2Pi to Arduino Mega
 * Use standard jumper wires to connect:
 *  Jumper   AD2PI   Mega
 *    GND    6       GND
 *   3.3V    1       3.3V
 *    RX     10       19
 *    TX     8        18
 *
 * Pin Configuration for Arduino Mega to ThingSheild
 * Use standard jumper wires to connect:
 *  Jumper      Mega  ThingShield
 *    TX        14        2   
 *    RX        15        3
 *    
 *
 *
 * Credit: thanks to github contributor vassilisv for the intial idea and to AlarmDecoder.com for suggesting to use
 * serial out feature of the AD2Pi to connect to the Arduino card.  This project also benefitted imenseley from code 
 * shared by SmartThings contributor  @craig
 * 
 * Thanks to Dan G Ogorchock & Daniel J Ogorchock for the updated SmartThings library.  This library is required for the ThingShield
 * to use the hardware serial port on the Mega and for general performance enhancements.
 */


#include <SoftwareSerial.h>
#include <SmartThings.h>  //be sure you are using the library from ST_ANYTHING which has support for hardware serial on the Arduino Mega

/*************************************************** User Settings ***************************************************
 * This section contains parameters that need to be set during initial setup.*/

// Set the number of zones in your system
#define numZones      36

// You have the option to set your security code in the Device Handler or here in the sketch.  Your security code must be set.
// The code in the Device Handler takes priority if set.
String securityCode = "";

boolean isDebugEnabled=true;  //set to true to debug

/************************************************* End User Settings *************************************************/

#define PIN_LED       13
#define BUFFER_SIZE   300 // max message length from ADT

SmartThingsCallout_t messageCallout; // call out function forward declaration
SmartThings smartthing(HW_SERIAL3, messageCallout);  //constructor for hardware serial port with ST_Anything Library

// set global variables
char buffer[BUFFER_SIZE];  //stores characters in AD2Pi to build up a message
int bufferIdx;  //counts characters as they come in from AD2Pi
String previousStr;
String previousPowerStatus;
String previousChimeStatus;
String previousAlarmStatus;
String previousActiveZone;
String previousInactiveList;
String previousSendData;

int lastZone;  //stores the last zone number to fault to compare as faults are cycled by system
int zoneStatusList[numZones + 1]; //stores each zone's status.  Adding 1 to numZones since element 0 will be a count of faulted zones and elements greater than 0 will equote to specific zone number  

void setup() {
  // initialize AD2 serial stream
  Serial1.begin(115200);                
  bufferIdx = 0;

  //debug stuff
  if (isDebugEnabled) {
    Serial.begin(9600);         // setup serial with a baud rate of 9600
    Serial.println("setup..");  // print out 'setup..' on start
  }
  // set SmartThings Shield LED
  smartthing.shieldSetLED(0, 0, 0); // shield led is off
  
  //initialize array counter and zones to 0.  0 = inactive, 1 = active
  for (int i = 0; i < (numZones + 1); i = i + 1) {
    zoneStatusList[i] = 0;
  }
  lastZone = 0;
}

void loop() {
  char data;
  // run smartthing logic
  smartthing.run();
  // capture IT-100 messages
  if(Serial1.available() > 0) {  
    data = Serial1.read();   
    // if end of message then evaluate and send to the cloud
    if (data == '\r' && bufferIdx > 0) { 
      processAD2();
    }
    // otherwise continue build array from message (ignore \n)
    else if (data != '\n')  {
      buffer[bufferIdx] = data; 
      bufferIdx++;
      // check for buffer overruns
      if (bufferIdx >= BUFFER_SIZE) 
      {
        smartthing.send("ER:Buffer overrun"); 
        bufferIdx = 0;
      }
    }
  }
}  

//Process AD2 messages
void processAD2() {
  // create String object
  buffer[bufferIdx] = '\0'; //adds null at end of buffer
  bufferIdx = 0; // reset  counter for next message
  String str(buffer);
  serialLog (str);
  //handle AD2Pi messages

 //first, check to see if  message is disarmed system status repeating over and over
  if (str.equals(previousStr))  {   //&& previousStr.indexOf("DISARMED")>=0
    // do nothing to avoid excessive logging to SmartThings hub and quickly return to loop
    return;
  }
  
  //message is different than previous message
  previousStr=str;
    
  //check for AD2Pi messages that do not require action
  //ToDo !RFX may contain data on devices, such as low battery.  !RFX handler may be worth adding in the future
  if (str.indexOf("!RFX:") >= 0 || str.indexOf("!EXP:") >= 0 || str.equals("!>null") || str.equals("!REL") || str.equals("!LRR") || str.equals("!AUI")) {
    // do nothing
    return;
  } 

  //Build and forward a message to the device handler in SmartThings. Only send information that represents an new status
  String sendData; // alarm panel combined status
  String sendMessage; //alarm panel stats + keypad message

  //AD2Pi messages that should be passed to device handler to display but no action required
  if (str.indexOf("!CONFIG") >= 0) {
    String sendMessage = ("|||||" + str.substring(8,18));
    smartthing.send(sendMessage);
    delay (3000);
    previousSendData = "";  //trip process to send subsequent message to smartthings
    previousAlarmStatus = "";
    return;
  } 
  if (str.indexOf("!Sending") >= 0) {
    String sendMessage = ("|||||"+ str);
    smartthing.send(sendMessage);
    previousSendData = "";  //trip process to send subsequent message to smartthings
    previousAlarmStatus = "";
    return;
  } 

  // AD2Pi messages that can be handled at Arduino without device handler
  if (str.indexOf("Hit * for faults") >= 0) {
    String sendCommand = "***";
    Serial1.println(sendCommand);  //send AD2Pi the command to pass on to Alarm Panel
    return;
  }
  
  /** 
   * rawPanelCode Data Description
   * The following was gathered from http://www.alarmdecoder.com/wiki/index.php/Protocol
   * 
   * 1 Indicates if the panel is READY
   * 2 Indicates if the panel is ARMED AWAY
   * 3 Indicates if the panel is ARMED HOME
   * 4 Indicates if the keypad backlight is on
   * 5 Indicates if the keypad is in programming mode
   * 6 Number (1-7) indicating how many beeps are associated with the message
   * 7 Indicates that a zone has been bypassed
   * 8 Indicates if the panel is on AC power
   * 9 Indicates if the chime is enabled
   * 10  Indicates that an alarm has occurred. This is sticky and will be cleared after a second disarm.
   * 11  Indicates that an alarm is currently sounding. This is cleared after the first disarm.
   * 12  Indicates that the battery is low
   * 13  Indicates that entry delay is off (ARMED INSTANT/MAX)
   * 14  Indicates that there is a fire
   * 15  Indicates a system issue
   * 16  Indicates that the panel is only watching the perimeter (ARMED STAY/NIGHT)
   * 17  System specific error report
   * 18  Ademco or DSC Mode A or D
   * 19  Unused
   * 20  Unused
   *
   * Unpack AD2Pi messages, evaluate if they represent changes to alarm panel and forward only those messages that represent status changes
   * Build message to send to device handler as follows:
   * powerStatus|chimeStatus|alarmStatus|activeZone|inactiveList|keypadMsg
   */

  String rawPanelCode = getValue(str, ',', 0);
  //During exit now messages sometimes an extra [ appears at the beginning of the rawPanelCode, remove it if found
  rawPanelCode.replace("[[", "[");
  
  String zoneString = getValue(str, ',', 1);
  int zoneNumber = zoneString.toInt();
  String rawPanelBinary = getValue(str, ',', 2);
  String keypadMsg = getValue(str, ',', 3);
  
  //During exit now messages sometimes the alarm messages run together and there are 2 messages in one line.  The follow code detects that sitation and extracts the message.
  //Example: [0011000100000000----],017,[f70000071017008008020000000000],"A[0011000100000000----],016,[f70000071016008008020000000000],"ARMED ***STAY***May Exit Now  16"
  if (keypadMsg.indexOf("[") >= 0 && keypadMsg.indexOf("]") >= 0) {
    keypadMsg = getValue(str, ',', 6);
  }
  keypadMsg.replace("\"", "");
  keypadMsg.trim();
  while (keypadMsg.indexOf("  ") >= 0) {
    keypadMsg.replace("  ", " ");
  }
  
  //boolean zoneBypass = (rawPanelCode.substring(7,8) == "1") ? true : false;
  //boolean systemError = (rawPanelCode.substring(15,16 == "1") ? true : false;

  //Determine power status at alarm panel
  String powerStatus;
  if (rawPanelCode.substring(8,9) == "0") { //AC Power Indicator
    powerStatus = "BN"; // Battery Normal
    if (rawPanelCode.substring(12,13) == "1") { //Low Battery Indicator
      powerStatus = "BL"; // Battery Low
    }
  } else {
    powerStatus = "AC";
  }

  //Determine chime status
  String chimeStatus = (rawPanelCode.substring(9,10) == "1") ? "chimeOn" : "chimeOff";

  //Determine alarm status
  String alarmStatus;
  if (rawPanelCode.substring(11,12) == "1") {
    alarmStatus = "alarm";
  } else if (rawPanelCode.substring(2,3) == "0" && rawPanelCode.substring(3,4) == "0") {
    alarmStatus = "disarmed";
  } else if (rawPanelCode.substring(2,3) == "1") {
      alarmStatus = "armedAway";
  } else if (rawPanelCode.substring(3,4) == "1") {
    alarmStatus = "armedStay";
  }
  if (keypadMsg.indexOf("Exit Now") >= 0) {  
    alarmStatus.replace("armed", "arming");
  } 

  //If alarm was previously alarming, security code needs to be entered again to clear
  if (rawPanelCode.substring(10,11) == "1") { //slot 10 is 'sticky' and requires disarm to be entered again to clear
    keypadMsg = "Press Disarm To Clear Previous Alarm";
  } 
  
  //Check for faults
  //each alarm message contains a listed fault which is either a active fault or an inactive fault representing the previous fault 
  //active faults are are noted by a "0" as the first number in the rawPanelCode if the alarm is in a disarmed state or are implicit to an alarm state
  String activeZone;   //zone that is being reported active by the alarm panel
  String inactiveList; //zones that are no longer active
  
  //the alarm panel message reports either an active zone, if any, or will report the last zone taht was active
  //this codd ensures 1) non-active faults are not passed on to the device handler, 2) resets active zone array and triggers a reset of any fault states in the device handler
  if (rawPanelCode.substring(1,2) == "1" || rawPanelCode.substring(2,3) == "1" || rawPanelCode.substring(3,4) == "1" ) {  //Indicates Panel Not Faulted 
    //ToDo: is this true with device faults, such as wireless sensors with low battery, etc.. ?
    //ToDo: is this true when zone bypass has been enabled?
    //ToDo: may need to replace using the "1" at bit postions 2,3 or 4 with the keypadMsg of "disarmed", "armed", "arming" if the above creates issues
    inactiveList = "allClear:" + String(numZones);  //when processed by device handler, will reset all zones
    getActiveList(1, numZones+1);
   
   //detect new faults 
  } else if (zoneStatusList[zoneNumber] == 0) {
    //New Fault, mark active and send to SmartThings
    lastZone = zoneNumber;
    zoneStatusList[zoneNumber] = 1;
    zoneStatusList[0] = zoneStatusList[0] + 1;
    //Send only 1 new fault since others were previously sent
    activeZone = String(zoneNumber);
    serialLog("New fault detected: " + activeZone);
    previousStr=""; //need to run through logic a second time to clear the zoneList
			
    //evaluate cases where 1 fault is repeating
  } else if (zoneNumber == lastZone && zoneStatusList[0] == 1) {
    //Do nothing: Only 1 fault repeating
    serialLog("Single fault repeating: zoneNumber(" + String(zoneNumber) + ") == lastZone(" + String(lastZone) + ")");
 
    //evaluate three cases where two or more zones are active  
  } else if (zoneNumber == lastZone && zoneStatusList[0] > 1) {
    //Faults(s) dropped from list.  Gather a list of those zones and mark inactive.
    inactiveList = getActiveList(1, numZones + 1);
    //Since we don't know what zone, remove current zone and set back to active in zoneStatusList array and update counter
    //the dropped zones would be all zones from lastZone to zoneNumber.  Insert sub-routine to zero those zones out.
    //Issue: sometimes a motion detector can trick this subroutine by rapidly going on and off and trigger multiple messages out of rotation
    inactiveList.replace(String(zoneNumber) + ",", "");
    zoneStatusList[zoneNumber] = 1;
    zoneStatusList[0] = zoneStatusList[0] + 1;
    serialLog ("zoneNumber(" + String(zoneNumber) + ") == lastZone(" + String(lastZone) + "): Faults Dropped from list and marked inactive: " + String(inactiveList));
    previousStr=""; //logic requires two run throughs to complete proper update of zoneStatusList
		
  } else if (zoneNumber < lastZone) {
    //Fault list starting over, determine if any faults dropped from list between zone 1 and current zone and also between the lastZone and numZones
    inactiveList = getActiveList(1, zoneNumber);
    //if fault list is starting over at zone1, need to check from lastZone+1 and zoneNumber
    if (zoneNumber == 1) {
      inactiveList = inactiveList + getActiveList(lastZone+1, numZones+1);
    }
    serialLog ("zoneNumber(" + String(zoneNumber) + ") < lastZone(" + String(lastZone) + "): Faults Dropped from list and marked inactive: " + String(inactiveList));

    lastZone = zoneNumber;
    previousStr=""; //logic requires two run throughs to complete proper update of zoneStatusList

  } else if (zoneNumber > lastZone) {
    //Fault list progressing, determine if any faults dropped from list between previous and current zone
    inactiveList = getActiveList(lastZone + 1, zoneNumber);
    serialLog("zoneNumber(" + String(zoneNumber) + ") > lastZone(" + String(lastZone) + "): Faults Dropped from list and marked inactive: " + String(inactiveList));        
    previousStr=""; //logic requires two run throughs to complete proper update of zoneStatusList
    lastZone = zoneNumber;
  }

  //build the message to send to the device handler on the hub
  if (powerStatus == previousPowerStatus) {
    sendMessage = "|";
  } else {
    sendMessage = powerStatus + "|";
    previousPowerStatus = powerStatus;
  }
  if (chimeStatus == previousChimeStatus) {
    sendMessage = sendMessage + "|";
  } else {
    sendMessage = sendMessage + chimeStatus + "|";
    previousChimeStatus = chimeStatus;
  }
  if (alarmStatus == previousAlarmStatus) {
    sendMessage = sendMessage + "|";
  } else {
    sendMessage = sendMessage + alarmStatus + "|";
    previousAlarmStatus = alarmStatus;
  }
  if (activeZone == previousActiveZone) {
    sendMessage = sendMessage + "|";
  } else {
    sendMessage = sendMessage + String(activeZone) + "|";
    previousActiveZone = activeZone;
  }
  if (inactiveList == previousInactiveList) {
    sendMessage = sendMessage + "|";
  } else {
    sendMessage = sendMessage + String(inactiveList) + "|";
    previousInactiveList = inactiveList;
  }
  sendData = sendMessage;
  sendMessage = sendMessage + keypadMsg;
 
  // Messages longer than 63 characters sometimes do not send to SmartThings.  Check length and truncate message if longer than 63 characters.
  if (sendMessage.length() > 63) {
    sendMessage.remove(63);  
  }      
  if (sendMessage.indexOf("|||||") >= 0 || sendData == previousSendData) {  
    //prevents sending multiple rotating faults with different keypad messages from flooding smartthings hub.  
    //Last new keypad message remains displayed in message tile on smartphone.  ToDo: should we design custom message???
  } else {
    //now send an alarm panel update to SmartThings
    serialLog("Sent to SmartThings:" + sendMessage);
    smartthing.send(sendMessage);   
  }
    previousSendData = sendData;
}

void messageCallout(String message) { 
  if(message.length() > 0) { //avoids processing ping from hub
    // Parse message from SmartThings
    String msgType = getValue(message, '|', 0);
    String msgSecurityCode = getValue(message, '|', 1);
    // Check to see if security code was set in the device handler and if not set it to the security code from this sketch
    msgSecurityCode = (msgSecurityCode != "") ? msgSecurityCode : securityCode;
    String msgCmd = getValue(message, '|', 2);
  		
    if (msgType.equals("CODE")) {
      //Check to make sure a security code is set and if not update the device handler keypad message
      if (msgSecurityCode == "") {
        serialLog( "Security code not set.  Updated SmartThings.");
        smartthing.send(String("||disarmed|||Alarm security not set!"));\
        return;
      }
    
      //Check to see if arming away and if alarm is ready, if not send notification that alarm cannot be armed.
      //This won't work for arming stay since motions could be active that don't affect arm stay.
      if (msgCmd == "2" && zoneStatusList[0] > 0) {
        serialLog( "Alarm not ready, cannot arm.  Updated SmartThings.");
        smartthing.send(String("||disarmed|||Alarm not ready; cannot arm"));\
      } else {
        String sendCommand = msgSecurityCode + msgCmd;
        Serial1.println(sendCommand);  //send AD2Pi the command to pass on to Alarm Panel
        //serialLog("Sent AD2Pi: " + sendCommand);
      }
    } else if (msgType.equals("CONF")) {
      Serial1.println("C" + msgCmd);  //send configuration command to AD2Pi
      serialLog("Sent AD2Pi: C" + msgCmd);
    } else if (msgType.equals("FUNC")) {
      //serialLog("Sending AD2Pi ASCII:" + msgCmd);
      if (msgCmd.equals("A")) {
        Serial1.write(1);
        Serial1.write(1);
        Serial1.write(1);
      } else if (msgCmd.equals("B")) {
        Serial1.write(2);
        Serial1.write(2);
        Serial1.write(2);
      } else if (msgCmd.equals("C")) {
        Serial1.write(3);
        Serial1.write(3);
        Serial1.write(3);
      }
    }
  }
}

String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i) == separator || i == maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

String getActiveList(int start, int end) {
  String faultList; 
  for (int i = start; i < end; ++i) {
    if (zoneStatusList[i] == 1) { 
      faultList = faultList + String(i) + ",";
      zoneStatusList[i] = 0;  //when using getActiveList to return the deactive list, you want to zero out the deactive zones.
    }
  }
 
  //Update Count in element 0  
  int faultCount = 0;
  for (int i = 1; i < numZones + 1; ++i) {
    if (zoneStatusList[i] == 1) { 
      faultCount = faultCount + 1;
    }
  }
  zoneStatusList[0] = faultCount;
  if (isDebugEnabled) {
    printArray(zoneStatusList, numZones + 1);
  }
  return faultList;
}


void printArray(int *a, int n) {
  for (int i = 0; i < n; i++) {
    if (i == 0) {
      Serial.print("Count:");
    } else {
      Serial.print(String(i) + ":");
    }
    Serial.print(a[i], DEC);
    Serial.print(' ');
  }
  Serial.println();
}

void serialLog(String serialMsg) {
  if (isDebugEnabled) {
    Serial.println(serialMsg);
  }
}
