/*
 * Smartlamp.ino
 * 
 * When paired with the right hardware, is a "smart lamp" for sunrise wakeups, including features like
 * 
 *  - Fading in the light in with the sunrise, or at a specified time, over a period of time
 *  - Fading in music at the specified time, over a different or same period of time
 *  
 *  Future potential features:
 *  - Color temperature changing to match time of day (IE blue-white in the morning, trending towards orange in the evening)
 *  - Bluetooth connection to phone for alarm time setting, color setting, etc
 *  
 * Also has a lot of extra potential capabilities, IE a "World mood" indicator (Scrapes social media/news websites for keywords, generates color based on that)
 * 
 * Liscense details:
 * 
 * Arduino program:
 * Copyright 2018 Jacob Field
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 * Libraries:
 * 
 * FastLED:
 * https://github.com/FastLED/FastLED
 * MIT Liscense, Copyright 2013 FastLED
 * 
 * Dusk2Dawn:
 * https://github.com/dmkishi/Dusk2Dawn
 * No liscense given, assumed all rights reserved
 * 
 * DFPlayerMini:
 * https://www.dfrobot.com/wiki/index.php/DFPlayer_Mini_SKU:DFR0299
 * Created 2016-12-07
 * By [Angelo qiao](Angelo.qiao@dfrobot.com)
 * 
 * GNU Lesser General Public License.
 * See <http://www.gnu.org/licenses/> for details.
 * All above must be included in any redistribution
 * 
 * NTPtimeESP:
 * https://github.com/SensorsIot/NTPtimeESP
 * Author: Andreas Spiess V1.0 2016-6-28
 * Based on work from John Lassen: http://www.john-lassen.de/index.php/projects/esp-8266-arduino-ide-webconfig
 * No liscense given, assumed all rights reserved
 * 
 * All other code is my own. These library liscenses are probably not needed since my program only contains references to the libraries, not actual code from them. 
 * This is probably not a bad thing to do, though.
 * 
 */

//-----------------------------------Libraries-----------------------------------

//FastLED code
#include <FastLED.h>
#define LED_PIN     13
#define NUM_LEDS    60
CRGB leds[NUM_LEDS];

//Dusk2Dawn code
#include <Dusk2Dawn.h>
//1300 E 9th ST, The Dalles, OR
#define lat 45.5955
#define lon -121.1734
#define timeZone -8
//Initiate library with a location and timezone
Dusk2Dawn myHome(lat, lon, timeZone);

//DFPlayer Mini code
#include "DFRobotDFPlayerMini.h"
DFRobotDFPlayerMini myDFPlayer;
HardwareSerial Serial1(2); // pin 16=RX, pin 17=TX, on FireBeetle ESP32. May be different for other ESP32 boards

//NTPtimeESP code
#include <NTPtimeESP.h>
#define DEBUG_ON
NTPtime NTPch("ch.pool.ntp.org");// Choose server pool as required
char *ssid      = "8friends"; // Set you WiFi SSID (IE the name of your network)
char *password  = "Mrs. Field has a cow.";//Set you WiFi password, as you would enter it normally
strDateTime dateTime;

//-----------------------------------In-program Variables-----------------------------------

//LED brightness tracking variables
byte maxBrightness = 255; //This is an 8-bit value, the final brightness of the LEDS
byte brightnessShallan = 0; // Current brightness tracking variable
int ledFadeAmount = 1; // How much to increase the brightness each cycle
unsigned long ledFrequency = 0; //How much time (In MS) to wait till we increase the brightness again

//Timers so we don't have to use delays
unsigned long timer1 = 0;
unsigned long timer2 = 0;

//Music volume tracking variables
unsigned int musicFrequency = 0;
byte musicFadeAmount = 1;
byte volume = 0;
byte maxVolume = 25;

//Tracking variable for bleeding edge on Capactive button
int touchValue = 77; //start it out at about the nominal value

//Alarm tracking variables
boolean doAlarm = false;
boolean fadingIn = false;
boolean sustainAlarm = true;
byte sunriseTimes[] = {0, 0};
boolean lightState = true; //True means on, false means off

//Alarm settings
byte alarmHour = 7;// in 24 hour format, the hour at which the alarm should trigger
byte alarmMinute = 000; // To be used in conjunction with above variable to set the minutes of the hour at which to trigger the alarm
unsigned int musicFadeTime = 30; //Music fade-in time in minutes
unsigned int ledFadeTime = 15; //LED Fade in time in minutes
boolean sunriseAlarm = false;

//debug variables
boolean debug = false; //When set to true, will bypass normal alarm times and always run the alarm
boolean doAlarmTwo = true;

//-----------------------------------void Setup-----------------------------------

void setup() {

  pinMode(26, INPUT);

  delay(5000);

  Serial.begin(115200);

  //FastLED code
  //Add LEDs to the library
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(128);
  //Set initial color for all LEDS to white
  for (byte x = 0; x <= NUM_LEDS; x++) {
    leds[x] = CRGB(255, 0, 32); //Turn LEDs red so we know that it's still initialzing
    delay(10);
    FastLED.show();
  }
  Serial.println("LEDs initialized!");

  //NTtimeESP code
  Serial.println();
  Serial.println("Booted");
  Serial.println("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin (ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("Wifi connected!");

  //DFPlayer MP3 code
  Serial1.begin(9600); // Begin communications with MP3 player, connected to IO 16 and 17
  if (!myDFPlayer.begin(Serial1)) {  //Wait for the serial connection to initialize
    Serial.println("Waiting for Serial connection to DFPlayer");
    while (true);
  }
  Serial.println("Serial connection established!"); // Let user know the connection was successful
  myDFPlayer.setTimeOut(500); //Set serial communictaion time out 500ms
  //Initialize settings
  myDFPlayer.volume(maxVolume);  //Set volume value (0~30).
  myDFPlayer.EQ(DFPLAYER_EQ_NORMAL); //Set EQ to normal
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD); //Set the default device to SD
  myDFPlayer.outputSetting(true, 0); //Enable the DAC, set gain to 0
  //myDFPlayer.sleep(); // Put the module to sleep for now
  myDFPlayer.enableLoop(); // Enable song looping

  ledFrequency = (ledFadeTime * 60000) / (maxBrightness / ledFadeAmount); //Divide the desired fade time by the final brightness (/ fade amount) to get the frequency
  musicFrequency = (musicFadeTime * 60000) / (maxVolume / musicFadeAmount); //Same with volume for music

  //turn LEDS green to signal that all initialized well
  for (byte x = 0; x <= NUM_LEDS; x++) {
    leds[x] = CRGB(0, 255, 128); // Change this depending on what type of white you want, I should set up a color pallette of different white hues
    FastLED.show();
    delay(10);
  }
  FastLED.setBrightness(0); //Turn LEDs off
  for (byte x = 0; x <= NUM_LEDS; x++) {
    leds[x] = CRGB(255, 200, 64); // Change this depending on what type of white you want, I should set up a color pallette of different white hues
  }
  FastLED.show();

}

//-----------------------------------void Loop-----------------------------------

void loop() {

  getTime();
  if(dateTime.valid){
    //NTPch.printDateTime(dateTime);
    convertTime(calculateSunrise());
    Serial.println("Time of sunrise: ");
    Serial.print(sunriseTimes[0]);
    Serial.print(":");
    Serial.println(sunriseTimes[1]);
  }


  //Look at capacitive button value, update storage variable
  touchValue = (0.75 * touchValue) + (0.25 * touchRead(27)); //D4 on the Firebeetle ESP32 - Can't go lower than a 9:1 ratio, or it won't recover


  //Alarm triggering if statements
  //run alarm preperation code
  //run alarm fade-in loop code (with cancel-to end code)
  //run alarm sustain loop code (with cancel-to end code)
  //run light-on loop code with button interpretation
  //run end all code to main loop with time checking, light color temp adjusting, and normal lamp operation with button checking code
  if(!sunriseAlarm && dateTime.hour == alarmHour && dateTime.minute == alarmMinute && dateTime.valid){
    doAlarm = true;
  } else if (sunriseAlarm && dateTime.hour == sunriseTimes[0] && dateTime.minute == sunriseTimes[1]){
    doAlarm = true;
  } else if (debug && doAlarmTwo){
    doAlarm = true;
  }

  //Main alarm control structure, triggered only by above if statements
  if (doAlarm) {

    initiateAlarm(); //Set the LEDS to the correct color, begin music, reset variables, etc

    while (fadingIn) {//Begin while(fadingIn) code
      
      //If it's time for a brightness update, do that
      if (millis() - timer1 >= ledFrequency) {
        Serial.println("Brightness update triggered");
        if (brightnessShallan < maxBrightness) {
          Serial.println("Brightness updated!");
          brightnessShallan += ledFadeAmount;
          updateBrightness();
          Serial.println(brightnessShallan);
        }
        timer1 = millis();
      }

      //If it's time for a music volume update, do that
      if (millis() - timer2 >= musicFrequency) { //was musicFrequency
        Serial.println("Volume update triggered");
        if (volume < maxVolume) {
          Serial.println("Volume updated!");
          Serial.println(volume);
          volumeUp();
        }
        timer2 = millis();
      }

      //Look at capacitive button value, update storage variable
      touchValue = (0.75 * touchValue) + (0.25 * touchRead(27)); //D4 on the Firebeetle ESP32
      if(touchValue < 38 && touchValue >= 30){
        endAlarm();
      } 

      if(brightnessShallan == maxBrightness && volume == maxVolume){
        fadingIn = false;
      }

      delay(1); // Small delay, since the capacitive button function seems to work better this way. May not be needed, other functions may take enough time as is.
    } // End while(fadingIn) loop

    sustainAlarm = true;

    while(sustainAlarm){//Begin sustaining alarm code (keeps light on and music playing once at max brightness)
      //Look at capacitive button value, update storage variable
      touchValue = (0.75 * touchValue) + (0.25 * touchRead(27)); //D4 on the Firebeetle ESP32
      if(touchValue < 38 && touchValue >= 30){
        endAlarm();
      } 
      delay(1); // Small delay, since the capacitive button function seems to work better this way. May not be needed, other functions may take enough time as is.
    }//End while(sustainAlarm) loop
  }//End if(doAlarm)

  //Light control code - Update eventually with "pulseIn" (rising edge to falling edge time measurment style) for short/long presses for different inputs
  if(touchValue < 38 && touchValue >= 30){
    if(lightState){
      endLight();
    } else if(!lightState){
      beginLight();
    }
  }

  delay(1); // Small delay, since the capacitive button function seems to work better this way. May not be needed, other functions may take enough time as is.
  
}

//-----------------------------------Custom functions-----------------------------------

int calculateSunrise () {
  /*  Available methods are sunrise() and sunset(). Arguments are year, month,
     day, and if Daylight Saving Time is in effect.
  */
  int homeSunrise = myHome.sunrise(dateTime.year, dateTime.month, dateTime.day, true);
  int homeSunset = myHome.sunset(dateTime.year, dateTime.month, dateTime.day, true);
  //int homeSunrise = myHome.sunrise(2018, 5, 14, true);
  //int homeSunset = myHome.sunset(2018, 5, 14, true);

/*
  //Time of expected sunrise/set in minutes since midnight. -1 if no sunrise or sunset is expected
  Serial.print("Expected time of sunrise in minutes-since-midnight: ");
  Serial.println(homeSunrise); //315

  //Convert time to 24 hour format from minutes-since-midnight format
  char time1[6];
  Dusk2Dawn::min2str(time1, homeSunrise);
  Serial.print("Expected time of sunrise in 24 hour time: ");
  Serial.println(time1); // 06:35

  char time2[6];
  Dusk2Dawn::min2str(time2, homeSunset);
  Serial.print("Expected time of sunset in 24 hour time: ");
  Serial.println(time2); // 20:28
  */

  return homeSunrise; // change to whatever we actually want for the NTP time feature
}

void updateBrightness() {
  FastLED.setBrightness(brightnessShallan);
  FastLED.show();
}

void getTime() {
  // first parameter: Time zone in floating point (for India); second parameter: 1 for European summer time; 2 for US daylight saving time; 0 for no DST adjustment; (contributed by viewwer, not tested by me)
  dateTime = NTPch.getNTPtime(-8.0, 2);

  // check dateTime.valid before using the returned time
  // Use "setSendInterval" or "setRecvTimeout" if required
  if(dateTime.valid){
    NTPch.printDateTime(dateTime);

    byte actualHour = dateTime.hour;
    byte actualMinute = dateTime.minute;
    byte actualsecond = dateTime.second;
    int actualyear = dateTime.year;
    byte actualMonth = dateTime.month;
    byte actualday = dateTime.day;
    byte actualdayofWeek = dateTime.dayofWeek;
  }
}

void updateVolume(byte vol) {
  myDFPlayer.volume(vol);
  volume = vol;
}

void volumeUp() {
  myDFPlayer.volumeUp();
  volume += 1;
}

void volumeDown() {
  myDFPlayer.volumeDown();
  volume -= 1;
}

void beginSong() {
  myDFPlayer.loop(1);  //Play the first mp3
}

void endSong() {
  //Fade volume out quickly, within 1/3 of a second
  for (byte y = volume; y > 0; y --) {
    Serial.print("Volume out: ");
    Serial.println(volume);
    updateVolume(y);
    delay(10);
  }
  myDFPlayer.pause();  //pause the mp3
}

void endAlarm() {
  endSong();
  fadingIn = false;
  doAlarm = false;
  sustainAlarm = false;
  if(debug){
    doAlarmTwo = false;
  }
  delay(1000);
}

void endLight(){
  Serial.println("Brightness off triggered");
  Serial.println(brightnessShallan);
  for (byte z = brightnessShallan; z > 0; z --) {
    Serial.print("Brightness down: ");
    Serial.println(z);
    brightnessShallan = z;
    updateBrightness();
    delay(3);
  }
  brightnessShallan = 0;
  updateBrightness();
  lightState = false;
  touchValue = 57;
}

void beginLight(){
  for (byte y = 0; y < maxBrightness; y ++) {
    Serial.print("Brightness up: ");
    Serial.println(y);
    brightnessShallan = y;
    updateBrightness();
    delay(3);
  }
  brightnessShallan = maxBrightness;
  updateBrightness();
  lightState = true;
  touchValue = 57;
}

void convertTime(int minToSunrise){
  int tempContainer1 = minToSunrise;

  sunriseTimes[0] = tempContainer1 / 60; //Divide the number of minutes by 60 to get hours- Here we're relying on a byte's inability to store decimal numbers to get rid of the .X hours left over
  sunriseTimes[1] = tempContainer1 - sunriseTimes[0] * 60; //To find leftover minutes, subtrack the amount of whole hours times 60 from the total number of minutes.
}

void initiateAlarm(){
  for (byte x = 0; x <= NUM_LEDS; x++) {
      leds[x] = CRGB(255, 200, 64); // Change this depending on what type of white you want, I should set up a color pallette of different white hues
    }
    updateVolume(1);
    beginSong();

    timer1 = millis(); // Initialize timer
    timer2 = timer1;

    fadingIn = true;
}

