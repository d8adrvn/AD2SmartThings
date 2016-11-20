/** 
 * AD2SmartThings v4_4_8
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
 * This section contains parameters that need to be set during initial setup.                                        */

// Set the highest numbered zone in your system.  This is not the total number of zones, but the highest zone number.
#define numZones      36

/* You have the option to set your security code in the Device Handler or here in the sketch.  Your security code
 * must be set.  The code in the Device Handler takes priority if set.                                               */
String securityCode = "";

/* Debugging has been included in this Arduino sketch as well as the SmartThings device handler.  To debug the Arduino sketch, set the 
 * isDebugEnabled variable to true, upload the code, and launch the Serial Monitor.  When debugging the Arduino it is also useful to set
 * the isDebugEnabled variable to true in the SmartThings device handler to confirm messages sent from the Arduino to SmartThings.
 * You can view debug messages in SmartThings in Live Logging.                                                       */
boolean isDebugEnabled = false;

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
  Serial1.begin(115200 );           
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

  //first, check to see if new message is the same as previous, in which case do nothing
  if (str.equals(previousStr))  {   
    // do nothing to avoid excessive logging to SmartThings hub and quickly return to loop
    return;
  }
  
  //message is different than previous message
  previousStr=str;
    
  //Check for AD2Pi messages that do not require action
  //ToDo !RFX may contain data on devices, such as low battery.  !RFX handler may be worth adding in the future
  if (str.indexOf("!RFX:") >= 0 || str.equals("!>null") || str.equals("!REL") || str.equals("!LRR") || str.equals("!AUI")) {
    // do nothing
    return;
  } 

  /* Zone expanders report zones faulting and restoring unlike zones on the main alarm panel board.  Each zone expander is addressed differently
   * starting with 07 and next expander is 08 and so on:
   
     Address  Zones
       07     9-16
       08     17-24
       09     25-32
       10     33-40
       11     41-48
   
   * When a zone on a zone expander faults, the message will include the zone expander address, zone number 01-08, and 01 for fault and 00 for restore.
   * Example !EXP:07,07,01 means the 7th zone on the 07 address expander is currently faulting or alarm panel zone 15 faulted.
   * These !EXP messages will always come through along with a keypad message update if the alarm is disarmed.  These messages are only useful when the
   * alarm is armed stay but since these messages don't include the Bit field with overall alarm status we can only determine if the alarm is armed stay
   * by a previous message value.  If alarm panel is not armed stay skip these messages.                                                                */

  if (previousAlarmStatus.indexOf("Stay") == -1 && str.indexOf("!EXP:") >= 0) {
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
    previousStr = "";  //trip algorithm to send subsequent message to smartthings
    return;
  } 
  
 /* Each time a command is sent from SmartThings to the alarm panel, the AD2Pi confirms the command was sent to the
 * panel with '!Sending..done'. These messages can be useful during the initial installation to confirm whether the
 * AD2Pi is properly connected to the alarm panel. There may be up to five periods between 'Sending' and 'done'.
 * If there are four or more periods, then the keypress has likely timed out and you should confirm the wiring between
 * AD2Pi and the Alarm Consule.  */

  if (str.indexOf("....") >= 0) {
    String sendMessage = ("|||||Communication Trouble");
    smartthing.send(sendMessage);
    previousStr = "";  //trip algorithm to send subsequent message to smartthings
    return;
  }
  
  if (str.startsWith("!Sending")) {
    return;
  } 

  /* By default the alarm panel doesn't display individual faults but they can be displayed by hitting the * key.  If the panel is disarmed via SmartThings, the * key
   * is automatically included during disarm.  But if the panel is disarmed via keypad, the panel may prompt for the * key.  The code below will look for the prompt
   * and hit the * key. The messages do vary from panel to panel because the message is set by the installer.  If for some reason this isn't working for you, validate
   * the message displayed on your alarm panel and make sure the text between the quotes matches what is displayed. */
  if (str.indexOf("* for faults") >= 0 || str.indexOf("* key") >= 0) {
    String sendCommand = "***";
    Serial1.println(sendCommand);  //send AD2Pi the command to pass on to Alarm Panel
    return;
  }
  
  /*
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

  // Declare variables
  String keypadMsg;
  String powerStatus;
  String chimeStatus;
  String alarmStatus;

  String rawPanelCode = getValue(str, ',', 0);
  //During exit now messages sometimes an extra [ appears at the beginning of the rawPanelCode, remove it if found
  rawPanelCode.replace("[[", "[");
  
  String zoneString = getValue(str, ',', 1);
  int zoneNumber = zoneString.toInt();
  String rawPanelBinary = getValue(str, ',', 2);

  // Zone expander messages do not include the bit field that includes overall alarm status, skip the processing of that data if message is from zone expander
  if (str.indexOf("!EXP:") == -1) {
    keypadMsg = getValue(str, ',', 3);
    
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
    if (rawPanelCode.substring(8,9) == "0") { //AC Power Indicator
      powerStatus = "BN"; // Battery Normal
      if (rawPanelCode.substring(12,13) == "1") { //Low Battery Indicator
        powerStatus = "BL"; // Battery Low
      }
    } else {
      powerStatus = "AC";
    }
  
    //Determine chime status
    chimeStatus = (rawPanelCode.substring(9,10) == "1") ? "chimeOn" : "chimeOff";
  
    //Determine alarm status
    if (rawPanelCode.substring(11,12) == "1") {
      alarmStatus = "alarm";
    } else if (rawPanelCode.substring(2,3) == "0" && rawPanelCode.substring(3,4) == "0") {
      alarmStatus = "disarmed";
    } else if (rawPanelCode.substring(2,3) == "1") {
        alarmStatus = "armedAway";
    } else if (rawPanelCode.substring(3,4) == "1") {
      alarmStatus = "armedStay";
    }
    if (keypadMsg.indexOf("Exit Now") >= 0 || keypadMsg.indexOf("exit now") >= 0) {  
      alarmStatus.replace("armed", "arming");
    } 
  
    //If alarm was previously alarming, security code needs to be entered again to clear
    if (rawPanelCode.substring(10,11) == "1") { //slot 10 is 'sticky' and requires disarm to be entered again to clear
      keypadMsg = "Press Disarm To Clear Previous Alarm";
    }
  }
  
  //Check for faults
  //each alarm message contains a listed fault which is either a active fault or an inactive fault representing the previous fault 
  //active faults are are noted by a "0" as the first number in the rawPanelCode if the alarm is in a disarmed state or are implicit to an alarm state
  String activeZone;   //zone that is being reported active by the alarm panel
  String inactiveList; //zones that are no longer active
  
  /* Zone expanders report zones faulting and restoring unlike zones on the main alarm panel board.  These messages are only useful when the
   * alarm is armed stay so if alarm panel is not armed stay don't process these messages.                                                  */
  
  if (previousAlarmStatus.indexOf("Stay") >= 0 && str.indexOf("!EXP:") >= 0) {
    // Since zone expander messages don't include the overall alarm status, default power, chime, and alarm status values to previous values so they are not reset to SmartThings
    powerStatus = previousPowerStatus;
    chimeStatus = previousChimeStatus;
    alarmStatus = previousAlarmStatus;
    
    // Get zone expander address value so the zone number can be calculated
    String expanderAddress = getValue(rawPanelCode, ':', 1);
    int expanderInt = expanderAddress.toInt();
    
    // Calculate the alarm zone number
    switch (expanderInt) {
      case 7:
        zoneNumber = zoneNumber + 8;
        break;
      case 8:
        zoneNumber = zoneNumber + 16;
        break;
      case 9:
        zoneNumber = zoneNumber + 24;
        break;
      case 10:
        zoneNumber = zoneNumber + 32;
        break;
      case 11:
        zoneNumber = zoneNumber + 40;
        break;
      default:
        // Something is not right so ignore the message
        return;
    }
    
    // 01 means active and 00 means restored
    if (rawPanelBinary == "01") {
      activeZone = String(zoneNumber);
      serialLog("New fault detected on zone expander: " + activeZone);
    } else {
      inactiveList = String(zoneNumber);
      serialLog("Fault restored on zone expander: " + inactiveList);
    }

  /* The alarm panel message reports either an active zone, if any, or will report the last zone that was active.  This code ensures:
   * 1) non-active faults are not passed on to the device handler, 2) resets active zone array and triggers a reset of any fault states in the device handler.
   *
   * Since zone expander's report faults and restores and these are only useful when the alarm is armed stay, only reset active zone if alarm reports:
   * Disarm Ready or Armed Away */
   
  } else if (rawPanelCode.substring(1,2) == "1" || rawPanelCode.substring(2,3) == "1") {
    //ToDo: is this true with device faults, such as wireless sensors with low battery, etc.. ?
    //ToDo: is this true when zone bypass has been enabled?
    inactiveList = "allClear:" + String(numZones);  //when processed by device handler, will reset all zones
    getActiveList(1, numZones+1);
  
  /* After a zone expander !EXP message, a normal alarm panel message immediately follows these messages.  Since zone expanders report when a zone has restored
   * we need to prevent the normal message from being processed so that the allClear message is not sent to SmartThings.  !EXP messages are only useful when alarm
   * is armed stay so check if alarm is armed stay.  However if the alarm was just armed stay send the allClear reset to SmartThings.                            */
  
  } else if (rawPanelCode.substring(3,4) == "1" ) {
    if (alarmStatus != previousAlarmStatus) {
      inactiveList = "allClear:" + String(numZones);  //when processed by device handler, will reset all zones
      getActiveList(1, numZones+1);
    }

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
  if (isDebugEnabled) {
    Serial.print ("Message: ");
    Serial.println(sendMessage);
  }
 
  // Messages longer than 63 characters sometimes do not send to SmartThings.  Check length and truncate message if longer than 63 characters.
  if (sendMessage.length() > 63) {
    sendMessage.remove(63);  
  }      
  if (sendMessage.startsWith("|||||") || sendData == previousSendData) {  
    //prevents sending multiple rotating faults with different keypad messages from flooding smartthings hub.  
    //Last new keypad message remains displayed in message tile on smartphone.  
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
        smartthing.send(String("||disarmed|||Alarm security code not set!"));\
        return;
      }
    
      //Check to see if arming away and if alarm is ready, if not send notification that alarm cannot be armed.
      //This won't work for arming stay since motions could be active that don't affect arm stay.
      if (msgCmd == "2" && zoneStatusList[0] > 0) {
        serialLog( "Alarm not ready, cannot arm.  Updated SmartThings.");
        smartthing.send(String("||disarmed|||Alarm not ready. Cannot arm."));\

      //go ahead and process the command and send to the AD2Pi which in turns forwards to the alarm console
      } else {
        String sendCommand = msgSecurityCode + msgCmd;
        Serial1.println(sendCommand);  
        //serialLog("Sent AD2Pi: " + sendCommand);
		
        /* The following code will perform a soft refresh of the device handler status.  This can be activated by pressing the disarm
         * button while the alarm is already disarmed. */
         serialLog("msgCmd: " + msgCmd + "   previousAlarmStatus: " + previousAlarmStatus);
        if (msgCmd.startsWith("1") && previousAlarmStatus == "disarmed") {
          previousPowerStatus = "";
          previousChimeStatus = "";
          previousAlarmStatus = "";
          previousSendData = "";
        }
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

String getActiveList(int startNum, int endNum) {
  String faultList; 
  for (int i = startNum; i < endNum; ++i) {
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
      Serial.print("Active Zone Count and Zone Status:");
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
