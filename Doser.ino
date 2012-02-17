#include <Wire.h>
#include <EEPROM.h> 
#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif
#include <DS1302.h> //from http://www.henningkarlsen.com/electronics/library.php?id=5
#include "LCDi2c4bit.h"
#include "mcp23xx.h"
#include "Doser.h"
#include "IRremote.h"
#include "MenuBackend.h"

#define PWM_BACKLIGHT_PIN      9  // pwm-controlled LED backlight
#define CONFIG_PIN             8  // pwm-controlled LED backlight
#define IR_PIN                12  // Sensor data-out pin, wired direct
#define ATO_FS1               15  // Analog 1 - float switch 1
#define ATO_FS2               16  // Analog 2 - float switch 2
#define ATO_FS3               17  // Analog 3 - float switch 3
#define ATO_RELAY              4  // Digital 4 - 5V relay for pump

// has to be the last entry in this list
#define IFC_KEY_SENTINEL       7  // this must always be the last in the list
#define MAX_FUNCTS                  IFC_KEY_SENTINEL

/*
 * IFC = internal function codes
 *
 * these are logical mappings of physical IR keypad keys to internal callable functions.
 * its the way we soft-map keys on a remote to things that happen when you press those keys.
 */
#define IFC_MENU               0  // enter menu mode
#define IFC_UP                 1  // up-arrow
#define IFC_DOWN               2  // down-arrow
#define IFC_LEFT               3  // left-arrow
#define IFC_RIGHT              4  // right-arrow
#define IFC_OK                 5  // Select/OK/Confirm btn
#define IFC_CANCEL             6  // Cancel/back/exit

uint8_t backlight_min      = 255; // color-independant 'intensity'
uint8_t backlight_max      = 255; // color-independant 'intensity'
uint16_t minCounter        =   0;
uint16_t tempMinHolder     =   0; // this is used for holding the temp value in menu setting
char strTime[20];                 // temporary array for time output
uint8_t psecond            =   0; // previous second
uint8_t backupTimer        =   0; // timer that will count seconds since the ATO started. 
uint8_t backupMax          =  10; // max seconds for timer to run. If this is reached, kill the ATO. (adjust timer based on your pump output)
uint8_t sPos               =   1; // position for setting
uint8_t lcd_in_use_flag    =   0; // is the LCD in use. Used for IR reading
uint8_t ATO_FS1_STATE      =   0; // State holder for flat switch 1
uint8_t ATO_FS2_STATE      =   0; // State holder for flat switch 2
uint8_t ATO_FS3_STATE      =   0; // State holder for flat switch 3

uint8_t pumps[] = { 3, 5, 6, 11, 10}; // pins for the pumps
uint8_t val[] = {0,255}; 
uint8_t i, global_mode,ts, tmi, th, tdw, tdm, tmo, ty;//variables for time
uint16_t key;
boolean first = true;
// this is a temporary holding area that we write to, key by key; and then dump all at once when the user finishes the last one
uint16_t ir_key_bank1[MAX_FUNCTS+1];
decode_results results;

// this is used in learn-mode, to prompt the user and assign enums to internal functions
struct _ir_keypress_mapping {
  long key_hex;
  uint8_t internal_funct_code;
  char funct_name[16];
}

ir_keypress_mapping[MAX_FUNCTS+1] = {
  { 0x00, IFC_MENU,          "Menu"           }
 ,{ 0x00, IFC_UP,            "Up Arrow"       }
 ,{ 0x00, IFC_DOWN,          "Down Arrow"     }
 ,{ 0x00, IFC_LEFT,          "Left Arrow"     }
 ,{ 0x00, IFC_RIGHT,         "Right Arrow"    }
 ,{ 0x00, IFC_OK,            "Confirm/Select" }
 ,{ 0x00, IFC_CANCEL,        "Back/Cancel"    }
 ,{ 0x00, IFC_KEY_SENTINEL,  "NULL"           }
};

//Init the Real Time Clock
DS1302 rtc(13, 7, 2);

//Init the MCP port expander for LCD
MCP23XX lcd_mcp = MCP23XX(LCD_MCP_DEV_ADDR);

//Init the LCD
LCDI2C4Bit lcd = LCDI2C4Bit(LCD_MCP_DEV_ADDR, LCD_PHYS_LINES, LCD_PHYS_ROWS, PWM_BACKLIGHT_PIN);

//this controls the menu backend and the event generation
MenuBackend menu = MenuBackend(menuUseEvent,menuChangeEvent);
//beneath is list of menu items needed to build the menu
MenuItem settings = MenuItem("Settings");
MenuItem mi_clock = MenuItem("Clock");
MenuItem mi_sleep = MenuItem("Display Sleep");
MenuItem mi_pump1 = MenuItem("Pump 1");
MenuItem mi_pump1_review = MenuItem("Review");
MenuItem mi_pump1_set = MenuItem("Set");
MenuItem mi_pump1_calibrate = MenuItem("Calibrate");
MenuItem mi_pump2 = MenuItem("Pump 2");
MenuItem mi_pump2_review = MenuItem("Review");
MenuItem mi_pump2_set = MenuItem("Set");
MenuItem mi_pump2_calibrate = MenuItem("Calibrate");
MenuItem mi_pump3 = MenuItem("Pump 3");
MenuItem mi_pump3_review = MenuItem("Review");
MenuItem mi_pump3_set = MenuItem("Set");
MenuItem mi_pump3_calibrate = MenuItem("Calibrate");
MenuItem mi_pump4 = MenuItem("Pump 4");
MenuItem mi_pump4_review = MenuItem("Review");
MenuItem mi_pump4_set = MenuItem("Set");
MenuItem mi_pump4_calibrate = MenuItem("Calibrate");
MenuItem mi_pump5 = MenuItem("Pump 5");
MenuItem mi_pump5_review = MenuItem("Review");
MenuItem mi_pump5_set = MenuItem("Set");
MenuItem mi_pump5_calibrate = MenuItem("Calibrate");
MenuItem mi_ATO = MenuItem("ATO");
MenuItem mi_ATO_set = MenuItem("Set timeot");


IRrecv irrecv(IR_PIN);

// Init a Time-data structure
Time t;

void setup()
{
  Serial.begin(9600);
  Serial.println("Hello");
  Wire.begin();

  // Initialize the pump pins
  pinMode(pumps[0], OUTPUT);      // pump 1
  pinMode(pumps[1], OUTPUT);      // pump 2
  pinMode(pumps[2], OUTPUT);      // pump 3
  pinMode(pumps[3], OUTPUT);      // pump 4
  pinMode(pumps[4], OUTPUT);      // pump 5
  // Enable config pin's pullup
  pinMode(CONFIG_PIN,INPUT);
  digitalWrite(CONFIG_PIN,HIGH);
  pinMode(ATO_FS1, INPUT);
  // Enable Float switch 1 pin's pullup
  digitalWrite(ATO_FS1,HIGH);
  pinMode(ATO_FS2, INPUT);
  // Enable Float switch 2 pin's pullup
  digitalWrite(ATO_FS2,HIGH);
  pinMode(ATO_FS3, INPUT);
  // Enable Float switch 3 pin's pullup
  digitalWrite(ATO_FS3,HIGH);
  pinMode(ATO_RELAY, OUTPUT);
  

  //start IR sensor
  irrecv.enableIRIn();
 

 
  // Setup Serial connection
  //start LCD
  lcd.init();
  lcd.SetInputKeysMask(LCD_MCP_INPUT_PINS_MASK);
  lcd.backLight(255);


 // Set the clock to run-mode, and disable the write protection
//    rtc.halt(false);
//    rtc.writeProtect(false);
//  // The following lines can be commented out to use the values already stored in the DS1302
//    rtc.setDOW(WEDNESDAY);        // Set Day-of-Week to FRIDAY
//    rtc.setTime(22, 58, 00);     // Set the time to 12:00:00 (24hr format)
//    rtc.setDate(11, 01, 2012);   // Set the date to August 6th, 2010
  rtc.setTCR(TCR_D1R2K);
  if (digitalRead(CONFIG_PIN) == LOW){
    enter_setup_mode();
  }

  menuSetup();
 //read remote keys from EEPROM  
  for (i=0; i<=MAX_FUNCTS; i++) {
    EEPROM_readAnything(40 + i*sizeof(key), key);
    ir_keypress_mapping[i].key_hex = key;
  }
}

void loop()
{
  // Get data from the DS1302
  t = rtc.getTime();
  uint8_t second = t.sec;
  if (global_mode == 0) {     //  home screen
    onKeyPress();
  }//global_mode == 0
  else if (global_mode == 1){ //we're in menu
    if (first){
      lcd.clear_L1();
      lcd.clear_L2();
      lcd.clear_L3();
      lcd.cursorTo(0,0);
      lcd.print((char*)menu.getCurrent().getName());
      first=false;
    }
    show_menu();
  } //global_mode == 1
  else if (global_mode == 3){
    set_time();
  }//global_mode == 3
  else{//we're somewhere else?
    
  }
  if (psecond != second){
      psecond = second;
      run_sec();
  }
  delay(50);
}


/*********************************/
/****** RUN ONCE PER SECOND ******/
/*********************************/
void run_sec( void ){
  do_ATO();
  if (global_mode == 0){
    update_pump(psecond%5,val[psecond%2]);
    //  lcd.cursorTo(0,0);
    //  lcd.print(psecond%5);
    //  lcd.print(":");
    //  lcd.print(val[psecond%2]);
    //  lcd.print("  ");
    analogWrite(pumps[psecond%5],val[psecond%2]);
  }
  if (global_mode!=3){
    update_clock(3,0);
  }
}


/*******************************/
/****** PRINT TIME AT X,Y ******/
/*******************************/
void update_clock(uint8_t x, uint8_t y){
  lcd.cursorTo(x,y);
  lcd.print(rtc.getTimeStr());
  if (global_mode == 3){
    lcd.print(" ");
  }else{
    lcd.print("  ");
  }
  lcd.print(rtc.getDateStr());
  if (global_mode == 3){
    lcd.print(rtc.getTime().dow);
  }
}


/********************************/
/****** INITIAL SETUP MODE ******/
/********************************/
void enter_setup_mode( void )  {

  uint8_t setup_finished = 0;
  uint8_t idx = 0, i = 0;
  uint8_t blink_toggle = 0;
  uint8_t blink_count = 0;
  float ratio;
  uint8_t eeprom_index = 0;
  uint8_t key_pressed = 0;


  lcd.clear();
  lcd.send_string("Remote Learning", LCD_CURS_POS_L1_HOME);

  idx = 0;
  while (!setup_finished) {
    if (!strcmp(ir_keypress_mapping[idx].funct_name, "NULL")) {
      setup_finished = 1;   // signal we're done with the whole list  
      goto done_learn_mode;
    }  

    // we embed the index inside our array of structs, so that even if the user comments-out blocks
    // of it, we still have the same index # for the same row of content
    eeprom_index = ir_keypress_mapping[idx].internal_funct_code;

    // prompt the user for which key to press
    lcd.send_string(ir_keypress_mapping[idx].funct_name, LCD_CURS_POS_L2_HOME+1);
    delay(300);

    blink_toggle = 1;
    blink_count = 0;

    /*
     * non-blocking poll for a keypress
     */

    while ( (key = get_input_key()) == 0 ) {

      if (blink_toggle == 1) {
        blink_toggle = 0;
        lcd.clear_L2();  // clear the string
        delay(300);  // debounce
      } 
      else {
        blink_toggle = 1;
        ++blink_count;
        lcd.send_string(ir_keypress_mapping[idx].funct_name, LCD_CURS_POS_L2_HOME+1);  // redraw the string
        delay(600);  // debounce
      }


      // check if we should exit (user got into this mode but had 2nd thoughts ;)
      if ( blink_count >= 30 ) {    // change the value of '30' if you need more time to find your keys ;)
        setup_finished = 1;
        global_mode = 0;           // back to main 'everyday use' mode

        lcd.clear();
        lcd.send_string("Abandon SETUP", LCD_CURS_POS_L1_HOME);


        /*
         * read LAST GOOD soft-set IR-key mappings from EEPROM
         */

        for (i=0; i<=MAX_FUNCTS; i++) {
          EEPROM_readAnything(40 + i*sizeof(key), key);
          ir_keypress_mapping[i].key_hex = key;
        }

        delay(1000);

        lcd.clear();
        return;

      } // if blink count was over the limit (ie, a user timeout)

    } // while


    // if we got here, a non-blank IR keypress was detected!
    lcd.send_string("*", LCD_CURS_POS_L2_HOME);
    lcd.send_string(ir_keypress_mapping[idx].funct_name, LCD_CURS_POS_L2_HOME+1);  // redraw the string

    delay(1000);  // debounce a little more


    // search the list of known keys to make sure this isn't a dupe or mistake
    // [tbd]


    // accept this keypress and save it in the array entry that matches this internal function call
    ir_key_bank1[eeprom_index] = key;

    idx++;  // point to the next one

    irrecv.resume(); // we just consumed one key; 'start' to receive the next value
    delay(300);

  } // while



done_learn_mode:
  global_mode = 0;           // back to main 'everyday use' mode
  lcd.clear();
  lcd.send_string("Learning Done", LCD_CURS_POS_L1_HOME);
  delay(500);
  lcd.send_string("Saving Key Codes", LCD_CURS_POS_L2_HOME);

  // copy (submit) all keys to the REAL working slots
  for (i=0; i<=MAX_FUNCTS; i++) {
    ir_keypress_mapping[i].key_hex = ir_key_bank1[i];
    EEPROM_writeAnything(40 + i*sizeof(ir_key_bank1[i]), ir_key_bank1[i]);    // blocks of 4 bytes each (first 40 are reserved, though)
    ratio = (float)i / (float)idx;

    delay(50);
  }
  first = true;
  delay(1000);

  lcd.clear();
}

/*******************************/
/****** GET INFRARED KEY ******/
/******************************/
long get_input_key( void ) {
  long my_result;
  long last_value = results.value;   // save the last one in case the new one is a 'repeat code'

  if (irrecv.decode(&results)) {

    // fix repeat codes (make them look like truly repeated keys)
    if (results.value == 0xffffffff) {

      if (last_value != 0xffffffff) {  
        results.value = last_value;
      } 
      else {
        results.value = 0;
      }

    }

    if (results.value != 0xffffffff) {
      my_result = results.value;
    } 
    else {
      my_result = last_value;  // 0;
    }

    irrecv.resume();    // we just consumed one key; 'start' to receive the next value

      return results.value; //my_result;
  }
  else {
    return 0;   // no key pressed
  }
}


/**************************************/
/****** SET PUMP pump TO PWM val ******/
/**************************************/
void update_pump(uint8_t pump, uint8_t val){
  //      pinMode(pumps[pump], val);
  //      char tmpStr[5];
  //      sprintf(tmpStr,"%d:%03d ",pump+1, val);
  if (pump*7 < 20){
    lcd.cursorTo(0,pump*7);
  }
  else{
    lcd.cursorTo(1,(pump-3)*7+4);
  }
  lcd.print(pump+1);
  lcd.print(":");
  lcd.print(val);
  lcd.print("  ");
}


/**********************/
/****** SET PUMP ******/
/**********************/
void set_pump(uint8_t pump){
  
}

/****************************/
/****** CALIBRATE PUMP ******/
/****************************/
void cal_pump(uint8_t pump){
  
}

/*************************/
/****** REVIEW PUMP ******/
/*************************/
void review_pump(uint8_t pump){
  
}

/**************************/
/****** AUTO TOP-OFF ******/
/**************************/
void do_ATO(){

  ATO_FS1_STATE = digitalRead(ATO_FS1);
  ATO_FS2_STATE = digitalRead(ATO_FS2);
  ATO_FS3_STATE = digitalRead(ATO_FS3);

#ifdef DEBUG
  Serial.print("mainSwitchState: ");
  Serial.println(mainSwitchState); 
  Serial.print("pumpSwitchState: ");
  Serial.println(pumpSwitchState); 
#endif

  if ( (backupTimer < backupMax) && (ATO_FS1_STATE == LOW) && (ATO_FS2_STATE == LOW) && (ATO_FS3_STATE == LOW)){ // LOW because we are pulling down the pins when switches activate
    //all is good. turn ATO on
//    digitalWrite(redLED,HIGH);
    digitalWrite(ATO_RELAY, HIGH);
    backupTimer++;
  }
  else if ((ATO_FS1_STATE == HIGH) && (ATO_FS2_STATE == LOW)){
    // water level is good. reset timer
    digitalWrite(ATO_RELAY, LOW);
//    digitalWrite(redLED,LOW);
    backupTimer = 0;
  }
  else if (ATO_FS3_STATE == HIGH){
    // backup float switch is on, something is wrong.
    digitalWrite(ATO_RELAY, LOW);
  }
  else if (backupTimer >= backupMax){
    // Pump has been running for too long, something is wrong.
    digitalWrite(ATO_RELAY, LOW);
//    digitalWrite(redLED,HIGH);
  }
  else{
    // RO/DI water level too low
    digitalWrite(ATO_RELAY, LOW);
//    digitalWrite(redLED,HIGH);
  }
}


/**************************/
/****** SET THE TIME ******/
/**************************/
void set_time( void ){
  key = get_input_key();
  if (key == 0) {
    return;
  }
  // key = OK
  if (key == ir_keypress_mapping[IFC_OK].key_hex ) {
     // Set the clock to run-mode, and disable the write protection
    rtc.halt(false);
    rtc.writeProtect(false);
  // The following lines can be commented out to use the values already stored in the DS1302
    rtc.setDOW(tdw);        // Set Day-of-Week to FRIDAY
    rtc.setTime(th, tmi, ts);     // Set the time to 12:00:00 (24hr format)
    rtc.setDate(tdm, tmo, ty);   // Set the date to August 6th, 2010
    //    RTC.setDate(ts, tmi, th, tdw, tdm, tmo, ty);
    global_mode = 0;
    lcd.clear();
    first=true;
  }

  // key = Up
  else if (key == ir_keypress_mapping[IFC_UP].key_hex){
    if (sPos == 1){
      if (th < 23) {
        th++;
      } 
      else {
        th = 0;  // wrap around
      }
    }
    else if (sPos == 2){
      if (tmi < 59) {
        tmi++;
      } 
      else {
        tmi = 0;  // wrap around
      }
    }
    else if (sPos == 3){
      if (ts < 59) {
        ts++;
      } 
      else {
        ts = 0;  // wrap around
      }
    }
    else if (sPos == 4){
      if (tdm < 31) {
        tdm++;
      } 
      else {
        tdm = 1;  // wrap around
      }
    }
    else if (sPos == 5){
      if (tmo < 12) {
        tmo++;
      } 
      else {
        tmo = 1;  // wrap around
      }
    }
    else if (sPos == 6){
      if (ty < 99) {
        ty++;
      } 
      else {
        ty = 0;  // wrap around
      }
    }
    else if (sPos == 7){
      if (tdw < 7) {
        tdw++;
      } 
      else {
        tdw = 1;  // wrap around
      }
    }
    delay (100);
    sprintf(strTime,"%02d:%02d:%02d %02d.%02d.%02d %d",th, tmi, ts, tdm, tmo, ty, tdw);
//    update_clock(3,0);   
  }


  // key = Down
  else if (key == ir_keypress_mapping[IFC_DOWN].key_hex){ 

    if (sPos == 1){
      if (th > 0) {
        th--;
      } 
      else {
        th = 23;  // wrap around
      }
    }
    else if (sPos == 2){
      if (tmi > 0) {
        tmi--;
      } 
      else {
        tmi = 59;  // wrap around
      }
    }
    else if (sPos == 3){
      if (ts > 0) {
        ts--;
      } 
      else {
        ts = 59;  // wrap around
      }
    }
    else if (sPos == 4){
      if (tdm > 1) {
        tdm--;
      } 
      else {
        tdm = 31;  // wrap around
      }
    }
    else if (sPos == 5){
      if (tmo > 1) {
        tmo--;
      } 
      else {
        tmo = 12;  // wrap around
      }
    }
    else if (sPos == 6){
      if (ty > 1) {
        ty--;
      } 
      else {
        ty = 99;  // wrap around
      }
    }
    else if (sPos == 7){
      if (tdw > 1) {
        tdw--;
      } 
      else {
        tdw = 7;  // wrap around
      }   
    }
    delay (100);
    sprintf(strTime,"%02d:%02d:%02d %02d/%02d/%02d %d",th, tmi, ts, tdm, tmo, ty, tdw);
    update_clock(3,0);   
  }


  // key = Left
  else if (key == ir_keypress_mapping[IFC_LEFT].key_hex){
    if (sPos > 1) {
      sPos--;
    } 
    else {
      sPos = 7;  // wrap around
    }
    delay (100);
  }


  // key = Right
  else if (key == ir_keypress_mapping[IFC_RIGHT].key_hex){ 
    if (sPos < 7) {
      sPos++;
    } 
    else {
      sPos = 1;  // wrap around
    }
    delay (100);
  }

  // key = Cancel
  else if (key == ir_keypress_mapping[IFC_CANCEL].key_hex){
    lcd.clear();
    global_mode = 0;
    delay (100);
    first = true;
  } 
  delay(100);
  irrecv.resume(); // we just consumed one key; 'start' to receive the next value

}
/*********************************/
/****** NORMAL MODE HANDLER ******/
/*********************************/
void onKeyPress( void )
{

  key = get_input_key();
  Serial.print(key,HEX);
  if (key == 0) {
    return;   // try again to sync up on an IR start-pulse
  }

  // key = MENU
  else if (key == ir_keypress_mapping[IFC_MENU].key_hex) {
    global_mode = 1;
  }

  // key = UP
  else if (key == ir_keypress_mapping[IFC_UP].key_hex) {
  }

  // key = DOWN
  else if (key == ir_keypress_mapping[IFC_DOWN].key_hex) {
  }

  // key = LEFT
  else if (key == ir_keypress_mapping[IFC_LEFT].key_hex) {
  }

  // key = RIGHT
  else if (key == ir_keypress_mapping[IFC_RIGHT].key_hex) {
  }

  // key = OK
  else if (key == ir_keypress_mapping[IFC_OK].key_hex) {
    //do something
  }

  // key = Cancel
  else if (key == ir_keypress_mapping[IFC_CANCEL].key_hex) {
    //do something
  }

  else{
    Serial.println("unsupported");
  }

  delay(100);
  irrecv.resume();
}

/***********************/
/****** MAIN MENU ******/
/***********************/
void show_menu( void ) {
//  Serial.println("In MENU");
  key = get_input_key();
  if (key == 0) {
    return;
  }
  Serial.print("Key is ");
  Serial.println(key,HEX);

  if (key == ir_keypress_mapping[IFC_OK].key_hex ) {
    Serial.println("OK");
    menu.use();
    
  }
  else if (key == ir_keypress_mapping[IFC_DOWN].key_hex){
    Serial.println("DOWN");
    menu.moveUp();
    delay (100);
  }
  else if (key == ir_keypress_mapping[IFC_UP].key_hex){ 
    Serial.println("UP");
    menu.moveDown();
    delay (100);
  }
  else if (key == ir_keypress_mapping[IFC_LEFT].key_hex){
    Serial.println("LEFT");
    menu.moveLeft();
    delay (100);
  }
  else if (key == ir_keypress_mapping[IFC_RIGHT].key_hex){ 
    Serial.println("RIGHT");
    menu.moveRight();
    delay (100);
  }
  else if (key == ir_keypress_mapping[IFC_CANCEL].key_hex){
    Serial.println("BACK");
    if (menu.getCurrent().getLeft() == 0){
      lcd.clear_L1();
      lcd.clear_L2();
      lcd.clear_L3();
      global_mode = 0;
    }else{
      menu.moveLeft();
    }
    delay (100);
  } 

  delay(100);

  irrecv.resume(); // we just consumed one key; 'start' to receive the next value

}




/***********************************/
/****** HANDLE MENU SELECTION ******/
/***********************************/
void menuUseEvent(MenuUseEvent used)
{
//	if (used.item == setDelay) //comparison agains a known item
//	{
//		Serial.println("menuUseEvent found Dealy (D)");
//	}

    if (used.item == mi_clock){
            global_mode=3;
      Serial.println("Yes, item is clock");
      lcd.clear();
      update_clock(2,0);   
      lcd.send_string("Use arrows to adjust", LCD_CURS_POS_L2_HOME);
      lcd.send_string("HH:MM:SS DD.MM.YY DW",LCD_CURS_POS_L4_HOME);

      set_time();
    }else if(used.item == mi_pump1_set){
    }else if(used.item == mi_pump2_set){
    }else if(used.item == mi_pump3_set){
    }else if(used.item == mi_pump4_set){
    }else if(used.item == mi_pump5_set){
    }else if(used.item == mi_pump1_calibrate){
    }else if(used.item == mi_pump2_calibrate){
    }else if(used.item == mi_pump3_calibrate){
    }else if(used.item == mi_pump4_calibrate){
    }else if(used.item == mi_pump5_calibrate){
    }else if(used.item == mi_pump1_review || used.item == mi_pump1){
    }else if(used.item == mi_pump2_review || used.item == mi_pump2){
    }else if(used.item == mi_pump3_review || used.item == mi_pump3){
    }else if(used.item == mi_pump4_review || used.item == mi_pump4){
    }else if(used.item == mi_pump5_review || used.item == mi_pump5){
    }else if(used.item == mi_ATO_set){
    }else {
//      lcd.cursorTo(2,0);
//      lcd.print("Used ");
//      lcd.printL((char*)used.item.getName(), 15);
    }
}

/*
	This is an important function
	Here we get a notification whenever the user changes the menu
	That is, when the menu is navigated
*/

/*****************************/
/****** MENU NAVIGATION ******/
/*****************************/
void menuChangeEvent(MenuChangeEvent changed)
{
  Serial.print("Menu change ");
  Serial.print(changed.from.getName());
  Serial.print("->");
  Serial.println(changed.to.getName());

  if (global_mode == 1){
    if (changed.to.getLeft() == 0){
      lcd.clear_L2();
      lcd.cursorTo(0,0);
      lcd.printL((char*)changed.to.getName(), 20);
    }else{ 
//      lcd.cursorTo(1,0);
//      lcd.print("   >                ");
      lcd.cursorTo(1,3);
      lcd.print("> ");
      lcd.printL((char*)changed.to.getName(), 15);
    }
  }
}



/***************************/
/****** INIT THE MENU ******/
/***************************/
void menuSetup()
{
  //add the settings menu to the menu root
  menu.getRoot().add(settings);
  //setup the settings menu item
  settings.addBefore(mi_pump1);
  settings.addAfter(mi_ATO);
  settings.addRight(mi_clock);
      mi_clock.addBefore(mi_sleep);
      mi_clock.addAfter(mi_sleep);
      mi_clock.addLeft(settings);
      mi_sleep.addAfter(mi_clock);
      mi_sleep.addLeft(settings);
  mi_pump1.addBefore(mi_pump2);
  mi_pump1.addAfter(settings);
  mi_pump1.addRight(mi_pump1_review);
    mi_pump1_review.addBefore(mi_pump1_set);
    mi_pump1_review.addAfter(mi_pump1_calibrate);
    mi_pump1_review.addLeft(mi_pump1);
    mi_pump1_set.addBefore(mi_pump1_calibrate);
    mi_pump1_set.addAfter(mi_pump1_review);
    mi_pump1_set.addLeft(mi_pump1);
    mi_pump1_calibrate.addAfter(mi_pump1_set);
    mi_pump1_calibrate.addBefore(mi_pump1_review);
    mi_pump1_calibrate.addLeft(mi_pump1);
  mi_pump2.addBefore(mi_pump3);
  mi_pump2.addAfter(mi_pump1);
  mi_pump2.addRight(mi_pump2_review);
    mi_pump2_review.addBefore(mi_pump2_set);
    mi_pump2_review.addAfter(mi_pump2_calibrate);
    mi_pump2_review.addLeft(mi_pump2);
    mi_pump2_set.addBefore(mi_pump2_calibrate);
    mi_pump2_set.addAfter(mi_pump2_review);
    mi_pump2_set.addLeft(mi_pump2);
    mi_pump2_calibrate.addAfter(mi_pump2_set);
    mi_pump2_calibrate.addBefore(mi_pump2_review);
    mi_pump2_calibrate.addLeft(mi_pump2);
  mi_pump3.addBefore(mi_pump4);
  mi_pump3.addAfter(mi_pump2);
  mi_pump3.addRight(mi_pump3_review);
    mi_pump3_review.addBefore(mi_pump3_set);
    mi_pump3_review.addAfter(mi_pump3_calibrate);
    mi_pump2_review.addLeft(mi_pump2);
    mi_pump3_set.addBefore(mi_pump3_calibrate);
    mi_pump3_set.addAfter(mi_pump3_review);
    mi_pump3_set.addLeft(mi_pump3);
    mi_pump3_calibrate.addAfter(mi_pump3_set);
    mi_pump3_calibrate.addBefore(mi_pump3_review);
    mi_pump3_calibrate.addLeft(mi_pump3);
  mi_pump4.addBefore(mi_pump5);
  mi_pump4.addAfter(mi_pump3);
  mi_pump4.addRight(mi_pump4_review);
    mi_pump4_review.addBefore(mi_pump4_set);
    mi_pump4_review.addAfter(mi_pump4_calibrate);
    mi_pump4_review.addLeft(mi_pump4);
    mi_pump4_set.addBefore(mi_pump4_calibrate);
    mi_pump4_set.addAfter(mi_pump4_review);
    mi_pump4_set.addLeft(mi_pump4);
    mi_pump4_calibrate.addAfter(mi_pump4_set);
    mi_pump4_calibrate.addBefore(mi_pump4_review);
    mi_pump4_calibrate.addLeft(mi_pump4);
  mi_pump5.addBefore(mi_ATO);
  mi_pump5.addAfter(mi_pump4);
  mi_pump5.addRight(mi_pump5_review);
    mi_pump5_review.addBefore(mi_pump5_set);
    mi_pump5_review.addAfter(mi_pump5_calibrate);
    mi_pump5_review.addLeft(mi_pump5);
    mi_pump5_set.addBefore(mi_pump5_calibrate);
    mi_pump5_set.addAfter(mi_pump5_review);
    mi_pump5_set.addLeft(mi_pump5);
    mi_pump5_calibrate.addAfter(mi_pump5_set);
    mi_pump5_calibrate.addBefore(mi_pump5_review);
    mi_pump5_calibrate.addLeft(mi_pump5);
  mi_ATO.addBefore(settings);
  mi_ATO.addAfter(mi_pump5);
  mi_ATO.addRight(mi_ATO_set);
    mi_ATO_set.addLeft(mi_ATO);
    
  menu.moveDown();
}

