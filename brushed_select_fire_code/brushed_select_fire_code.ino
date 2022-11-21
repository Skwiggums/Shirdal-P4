/*
 * Swordfish Firmware
 * (c) 2019 Michael Ireland (Airzone)
 * 
 * No commercial use allowed
 * 
 * Use at your own risk
 * 
 */

#include <Bounce2.h>
#include <EEPROM.h>

// Pin Definitions
#define PIN_SELECT_FIRE_A 3
#define PIN_SELECT_FIRE_B 4
#define PIN_TRIGGER_FULL 5
#define PIN_PUSHER_FET 6
#define PIN_LED_RX 17


// Configuration Options
byte BatteryS = 3;
byte BurstSize = 2;
byte TargetDPSBurst = 10;
byte TargetDPSAuto = 10;

// Modes
#define MODE_NORMAL 1
byte SystemMode = MODE_NORMAL;


// Pusher Controls
// Pusher 3S
#define PULSE_ON_TIME_3S 55
#define PULSE_RETRACT_TIME_3S 70 
// Pusher 4S
#define PULSE_ON_TIME_4S 25   //15
#define PULSE_RETRACT_TIME_4S 85   //25
int PulseOnTime;
int PulseRetractTime;
#define SOLENOID_CYCLE_IDLE 0
#define SOLENOID_CYCLE_PULSE 1
#define SOLENOID_CYCLE_RETRACT 2
#define SOLENOID_CYCLE_COOLDOWN 3
byte CurrentSolenoidCyclePosition = SOLENOID_CYCLE_IDLE;
unsigned long LastSolenoidCycleStarted = 0;


// Firing Controls
#define FIRE_MODE_SINGLE 0
#define FIRE_MODE_BURST 1
#define FIRE_MODE_AUTO 2
#define FIRE_MODE_AUTO_LASTSHOT 3
#define FIRE_MODE_IDLE 4
byte CurrentFireMode = FIRE_MODE_SINGLE; // This is the user request based on the button state
byte ProcessingFireMode = FIRE_MODE_IDLE; // This is what will actually be fired.
bool ExecuteFiring = false; // Set to true when the Solenoid is supposed to move
int TimeBetweenShots = 0; // Calculated to lower ROF
long ShotsToFire = 0; // Number of shots in the queue
unsigned long LastShot = 0; // When the last shot took place.
byte TargetDPS = 10; // This is what the solenoid will operate at. 
bool RequestShot = false; // Set to true to request the firing sequence to commence
bool RequestAutoStop = false; // Set to true to stop Full Auto



// Inputs
#define DebounceWindow 5 // Debounce Window = 5ms
Bounce FireFullTriggerBounce = Bounce();
Bounce ModeSelectABounce = Bounce();
Bounce ModeSelectBBounce = Bounce();


// Physical Switch Status
bool RevTriggerPressed = false; // Rev Trigger is Depressed
bool FireFullTriggerPressed = false; // Fire Trigger is Depressed
bool FireHalfTriggerPressed = false; // Fire Trigger is Depressed
bool ConfigModePressed = false; // Config button is Depressed


// Battery Controls
#define BATTERY_3S_MIN 9.6
#define BATTERY_3S_MAX 12.6
#define BATTERY_4S_MIN 12.8
#define BATTERY_4S_MAX 16.8
#define BATTERY_CALFACTOR 0.0 // Adjustment for calibration.
float BatteryMaxVoltage;
float BatteryMinVoltage;
float BatteryCurrentVoltage = 99.0;
bool BatteryFlat = false;


// Serial Comms
#define SERIAL_INPUT_BUFFER_MAX 25
char SerialInputBuffer[SERIAL_INPUT_BUFFER_MAX];
byte SavedMode = FIRE_MODE_SINGLE;
byte SavedBurstSize = 0;
bool HasSavedMode = false;
bool AutoFire = false;
int AutoFireMotorSpeed = 0;


// EEPROM Addresses
#define ADDR_BURST_SIZE 0
#define ADDR_DPS_BURST 1
#define ADDR_DPS_AUTO 2
#define ADDR_MOTOR_FULL 3
#define ADDR_MOTOR_HALF 4

void setup() {
  // Setial startup
  Serial.begin( 57600 ); // Debugging
  Serial.println( F("Booting") );

  // Set up debouncing
  Serial.println( F("Configuring Debouncing") );  
  
  pinMode(PIN_TRIGGER_FULL, INPUT_PULLUP);
  FireFullTriggerBounce.attach( PIN_TRIGGER_FULL );
  FireFullTriggerBounce.interval( DebounceWindow );

  pinMode(PIN_SELECT_FIRE_A, INPUT_PULLUP);
  ModeSelectABounce.attach( PIN_SELECT_FIRE_A );
  ModeSelectABounce.interval( DebounceWindow );
  
  pinMode(PIN_SELECT_FIRE_B, INPUT_PULLUP);
  ModeSelectBBounce.attach( PIN_SELECT_FIRE_B );
  ModeSelectBBounce.interval( DebounceWindow );   

  Serial.println( F("Debouncing Configured") );

  // Setup Pusher Outputs
  Serial.println( F("Configuring Pusher FET") );
  pinMode( PIN_PUSHER_FET, OUTPUT );
  digitalWrite( PIN_PUSHER_FET, LOW );  


  // LED
  Serial.println( F("Configuring LED") );
  pinMode( PIN_LED_RX, OUTPUT );
  digitalWrite( PIN_LED_RX, LOW );  

  Serial.println( F("Loading EEPROM") );
  LoadEEPROM();

  Serial.println( F("Detecting Battery") );
  // Setup Battery
  if( BatteryS == 3 )
  {
    Serial.println( F("Configuring for 3S Battery") );
    PulseOnTime = PULSE_ON_TIME_3S;
    PulseRetractTime = PULSE_RETRACT_TIME_3S;

    BatteryMaxVoltage = BATTERY_3S_MAX;
    BatteryMinVoltage = BATTERY_3S_MIN;
  }
  else
  {
    Serial.println( F("Configuring for 4S Battery") );
    PulseOnTime = PULSE_ON_TIME_4S;
    PulseRetractTime = PULSE_RETRACT_TIME_4S;

    BatteryMaxVoltage = BATTERY_4S_MAX;
    BatteryMinVoltage = BATTERY_4S_MIN;
  }

  SystemMode = MODE_NORMAL;
  Serial.println( F("Booted") );
}


void LoadEEPROM()
{
  bool CorruptData = false;

  BurstSize = EEPROM.read( ADDR_BURST_SIZE );
  TargetDPSBurst = EEPROM.read( ADDR_DPS_BURST );
  TargetDPSAuto = EEPROM.read( ADDR_DPS_AUTO );

  Serial.println( F("Read from EEPROM") );
  Serial.println( BurstSize );
  Serial.println( TargetDPSBurst );
  Serial.println( TargetDPSAuto );

  if( (BurstSize < 1) || (BurstSize > 99) ) CorruptData = true;
  if( (TargetDPSBurst < 1) || (TargetDPSBurst > 99) ) CorruptData = true;
  if( (TargetDPSAuto < 1) || (TargetDPSAuto > 99) ) CorruptData = true;
  
  FireFullTriggerBounce.update();
  int TriggerStatus = FireFullTriggerBounce.read();
  if( (TriggerStatus == HIGH) || CorruptData )
  {
    Serial.println( F("Something wrong with EEPROM or held trigger while booting") );
    Serial.println( CorruptData );
    Serial.println( TriggerStatus == LOW );
    Serial.println( (TriggerStatus == LOW) || CorruptData );    
    BurstSize = 2;
    TargetDPSBurst = 10;
    TargetDPSAuto = 10;

    EEPROM.write( ADDR_BURST_SIZE, BurstSize );
    EEPROM.write( ADDR_DPS_BURST, TargetDPSBurst );
    EEPROM.write( ADDR_DPS_AUTO, TargetDPSAuto );
  }

  Serial.println( F("Initialised") );
  Serial.println( BurstSize );
  Serial.println( TargetDPSBurst );
  Serial.println( TargetDPSAuto );
}


/*
 * This is a boot time init sub to calcualte the Acceleration and 
 * deceleration ramp rates of the motors.
 */


void loop() {
  ProcessButtons(); // Get User and Sensor input

  // Process Serial input
  if( ProcessSerialInput() )
  {
    ProcessSerialCommand();
  }
  
  // Process Firing Controls
  ProcessFiring();
  ProcessSolenoid();
}

bool ProcessSerialInput()
{
  bool SerialDataAvailable = false;
  if( Serial.available() != 0 )
    SerialDataAvailable = true;
    
  if( !SerialDataAvailable ) return false; // Ignore when there is no serial input
  
  static byte CurrentBufferPosition = 0;

  while(Serial.available() > 0)
  {
    char NextByte = 0;
    if( Serial.available() != 0 )
      NextByte = Serial.read();
    else
      NextByte = 0; //WTF is this happening??

    switch( NextByte )
    {
      case '#': // Starting new command
        CurrentBufferPosition = 0;
        break;
      case '$': // Ending command
        return true; // Jump out.. There's more data in the buffer, but we can read that next time around.
        break;
      case '?': // Presume help - Simulate DS
        SerialInputBuffer[0] = 'D';
        SerialInputBuffer[1] = 'S';
        return true;
        break;
      default: // Just some stuff coming through
        SerialInputBuffer[ CurrentBufferPosition ] = NextByte; // Insert into the buffer
        CurrentBufferPosition ++; // Move the place to the right
        if( CurrentBufferPosition >= SERIAL_INPUT_BUFFER_MAX ) CurrentBufferPosition = (SERIAL_INPUT_BUFFER_MAX - 1);  // Capture Overflows.
    }
  }

  return false;
}

void ProcessSerialCommand()
{
  char CommandHeader[3]; // Place the header into this buffer
  // Copy it using a lazy way
  CommandHeader[0] = SerialInputBuffer[0];
  CommandHeader[1] = SerialInputBuffer[1];
  CommandHeader[2] = 0;

  // Single Fire Full Command - SF
  if( (strcmp( CommandHeader, "SF" ) == 0) && (SystemMode == MODE_NORMAL) )
  {
    AutoFire = true;
    if( !HasSavedMode )
    {
      HasSavedMode = true;
      SavedMode = CurrentFireMode;
      SavedBurstSize = BurstSize;
    }
    CurrentFireMode = FIRE_MODE_SINGLE;
  }

  // Single Fire Half Command - SH
  if( (strcmp( CommandHeader, "SH" ) == 0) && (SystemMode == MODE_NORMAL) )
  {
    AutoFire = true;
    if( !HasSavedMode )
    {
      HasSavedMode = true;
      SavedMode = CurrentFireMode;
      SavedBurstSize = BurstSize;
    }
    CurrentFireMode = FIRE_MODE_SINGLE;
  }

  // Burst Fire Full Command - BF
  if( (strcmp( CommandHeader, "BF" ) == 0) && (SystemMode == MODE_NORMAL) )
  {
    AutoFire = true;
    if( !HasSavedMode )
    {
      HasSavedMode = true;
      SavedMode = CurrentFireMode;
      SavedBurstSize = BurstSize;
    }
    char IntValue[3] = { SerialInputBuffer[3], SerialInputBuffer[4], 0 };
    BurstSize = constrain( atoi( IntValue ), 1, 99 );
    CurrentFireMode = FIRE_MODE_BURST;
  }  

  // Burst Fire Full Command - BH
  if( (strcmp( CommandHeader, "BH" ) == 0) && (SystemMode == MODE_NORMAL) )
  {
    AutoFire = true;
    if( !HasSavedMode )
    {
      HasSavedMode = true;
      SavedMode = CurrentFireMode;
      SavedBurstSize = BurstSize;
    }
    char IntValue[3] = { SerialInputBuffer[3], SerialInputBuffer[4], 0 };
    BurstSize = constrain( atoi( IntValue ), 1, 99 );
    CurrentFireMode = FIRE_MODE_BURST;
  }  

  // Burst Size Command - BS
  if( (strcmp( CommandHeader, "BS" ) == 0) && (SystemMode == MODE_NORMAL) )
  {
    char IntValue[3] = { SerialInputBuffer[3], SerialInputBuffer[4], 0 };
    BurstSize = constrain( atoi( IntValue ), 1, 99 );
    EEPROM.write( ADDR_BURST_SIZE, BurstSize );
  }

  // Full Auto Rate Command - FR
  if( (strcmp( CommandHeader, "FR" ) == 0) && (SystemMode == MODE_NORMAL) )
  {
    char IntValue[3] = { SerialInputBuffer[3], SerialInputBuffer[4], 0 };
    TargetDPSAuto = constrain( atoi( IntValue ), 1, 99 );
    EEPROM.write( ADDR_DPS_AUTO, TargetDPSAuto );
  }
  
  // Burst Rate Command - BR
  if( (strcmp( CommandHeader, "BR" ) == 0) && (SystemMode == MODE_NORMAL) )
  {
    char IntValue[3] = { SerialInputBuffer[3], SerialInputBuffer[4], 0 };
    TargetDPSBurst = constrain( atoi( IntValue ), 1, 99 );
    EEPROM.write( ADDR_DPS_BURST, TargetDPSBurst );
  }
  

  // Query Device Command - QD
  if( strcmp( CommandHeader, "QD" ) == 0 )
  {
    Serial.println( F("#SF-OK$") );
  } 

  // Query Voltage Command - QV
  if( strcmp( CommandHeader, "QV" ) == 0 )
  {
    char VoltBuffer[5];
    sprintf( VoltBuffer, "%3d", (int)(BatteryCurrentVoltage * 10) );
    VoltBuffer[4] = 0;
    VoltBuffer[3] = VoltBuffer[2];
    VoltBuffer[2] = '.';
    Serial.println( VoltBuffer );
  }   

  // Display Settings - DS
  if( strcmp( CommandHeader, "DS" ) == 0 )
  {
    Serial.println( F("--------------------") );
    Serial.println( F("Blaster Settings:") );
    Serial.println( F("--------------------") );    


    Serial.print( F("Burst Size = ") );
    Serial.println( BurstSize );    
    Serial.println( F("Change with #BS-xx$  (xx = 01 - 99)\n") );

    Serial.print( F("ROF Burst = ") );    
    Serial.println( TargetDPSBurst );    
    Serial.println( F("Change with #BR-xx$  (xx = 01 - 99; Pusher Physical Limit Applies)\n") );

    Serial.print( F("ROF Auto = ") );    
    Serial.println( TargetDPSAuto );        
    Serial.println( F("Change with #FR-xx$  (xx = 01 - 99; Pusher Physical Limit Applies)") );
    Serial.println( F("--------------------\n") );

    Serial.println( F("--------------------") ); 
    Serial.println( F("Blaster Status:") );
    Serial.println( F("--------------------") );

    Serial.print( F("Full Trigger State = ") );
    Serial.println( FireFullTriggerPressed );

    Serial.println( F("--------------------\n") );              
  } 
      
}

/*
 * Process the manual commands leading to motor reving
 * 
 * Logic:
 * If AutoRev is being performed, disconnect it when the half trigger is pulled.
 * We are looking for the following events: 
 * If the Half Trigger is pressed, Rev to Speed A
 * If the Rev Trigger is pressed, and the Half Trigger is also pressed, Rev to Speed B
 * If the Rev Trigger is pressed, but the Half Trigger is not, then ignore the command.
 * 
 */

// Process the firing request and queue up some darts to fire.
void ProcessFiring()
{
  if( !(SystemMode == MODE_NORMAL)) // Finish off the stroke unless in running or in ROF config mode
  {
    ShotsToFire = 0;
    if( ProcessingFireMode == FIRE_MODE_AUTO_LASTSHOT )
      ProcessingFireMode = FIRE_MODE_IDLE;
    return;
  }

  static unsigned long InitiatedAutoFire = 0;
  if( AutoFire )
  {
    if( InitiatedAutoFire == 0 ) // Started auto fire process. Start spinning the motors
    {
      InitiatedAutoFire = millis();
      return;
    }
   
    RequestShot = true;
  }
  else
  {
    InitiatedAutoFire = 0;
  }
    

  // Requesting Shot while we were doing nothing special
  if( RequestShot && (ProcessingFireMode == FIRE_MODE_IDLE) )
  {
    ProcessingFireMode = CurrentFireMode;
    switch( ProcessingFireMode )
    {
      case FIRE_MODE_SINGLE:
        ShotsToFire = 1; // Add another shot to the queue
        LastSolenoidCycleStarted = millis();
        ExecuteFiring = true;
        TargetDPS = 99; // Single fire mode is always flat out
        break;
      case FIRE_MODE_BURST:
        ShotsToFire = BurstSize; // Set the burst size
        LastSolenoidCycleStarted = millis();
        ExecuteFiring = true;
        TargetDPS = TargetDPSBurst;
        break;        
      case FIRE_MODE_AUTO:
        ShotsToFire = 9999; // Set to something unreasonably high
        LastSolenoidCycleStarted = millis();
        ExecuteFiring = true;
        TargetDPS = TargetDPSAuto;
        break;        
    }
  }
  else if( RequestAutoStop && (ProcessingFireMode == FIRE_MODE_AUTO) ) // Requesting Stop while firing in Full Auto 
  {
    ProcessingFireMode = FIRE_MODE_AUTO_LASTSHOT;
    LastSolenoidCycleStarted = millis();
    ExecuteFiring = true;

    if( CurrentSolenoidCyclePosition == SOLENOID_CYCLE_PULSE )
    {
      ShotsToFire = 1;
    }
    else
    {
      ShotsToFire = 0;
    }
  }
}

// Make the solenoid do the things it's supposed to do
void ProcessSolenoid()
{
  if( !ExecuteFiring ) // Just skip if there is no firing to execute
  {
    return;
  }

  // Calculate duty cycle whenever the target changes.
  static byte PrevTargetDPS = 0;
  if( PrevTargetDPS != TargetDPS )
  {
    PrevTargetDPS = TargetDPS;
    if( TargetDPS == 99 ) // Full rate
    {
      TimeBetweenShots = 0;
    }
    else
    {
      int PulseOverhead = PulseOnTime + PulseRetractTime;
      int TotalPulseOverhead = PulseOverhead * TargetDPS;
      int FreeMS = 1000 - TotalPulseOverhead;
      if( FreeMS <= 0 )
      {
        TimeBetweenShots = 0; // Pusher won't achieve this rate
      }
      else
      {
        TimeBetweenShots = FreeMS / TargetDPS;
      }
    }
  }

  // We actually have nothing to do
  if( ProcessingFireMode == FIRE_MODE_IDLE )
  {
    return; // Solenoid is idling.
  }

  // We are apparently supposed to fire 0 darts... Typically for end-of-firing scenarios
  if( (ShotsToFire == 0) && (ProcessingFireMode != FIRE_MODE_IDLE) )
  {
    ProcessingFireMode = FIRE_MODE_IDLE;
    CurrentSolenoidCyclePosition = SOLENOID_CYCLE_IDLE;
    digitalWrite( PIN_PUSHER_FET, LOW );
    Serial.println( F("Finished shooting") );
    ExecuteFiring = false;
    if( AutoFire )
    {
      AutoFire = false;
      if( HasSavedMode )
      {
        BurstSize = SavedBurstSize;
        CurrentFireMode = SavedMode;
        RequestShot = false;
        HasSavedMode = false;
      }
    }
    return;    
  }

  // Pulse solenoid on high
  if( (millis() - LastSolenoidCycleStarted) < PulseOnTime )
  {
    if( CurrentSolenoidCyclePosition != SOLENOID_CYCLE_PULSE )
    {
      //Serial.println( F("Start Pulse") );
      
      if( (SystemMode != MODE_NORMAL) ) // Don't fire unless the system m ode is normal
      {
        ShotsToFire = 0;
        Serial.println( F("Mag Out!!") );
        return;
      }
      
    }
    CurrentSolenoidCyclePosition = SOLENOID_CYCLE_PULSE;
    digitalWrite( PIN_PUSHER_FET, HIGH );
    return;
  }

  // Release solenoid for retraction
  if( (millis() - LastSolenoidCycleStarted) < (PulseOnTime + PulseRetractTime) )
  {
    if( CurrentSolenoidCyclePosition != SOLENOID_CYCLE_RETRACT )
    {
      //Serial.println( F("End Pulse") );
    }
    CurrentSolenoidCyclePosition = SOLENOID_CYCLE_RETRACT;
    digitalWrite( PIN_PUSHER_FET, LOW );
    return;      
  }  

  // Wait for the Global Cool Down... i.e. ROF adjustment
  if((millis() - LastSolenoidCycleStarted) < (PulseOnTime + PulseRetractTime + TimeBetweenShots))
  {
    if( CurrentSolenoidCyclePosition != SOLENOID_CYCLE_COOLDOWN )
    {
      //Serial.println( F("Cooling Down") );
    }
    CurrentSolenoidCyclePosition = SOLENOID_CYCLE_COOLDOWN;
    digitalWrite( PIN_PUSHER_FET, LOW );
    return;      
  }

  // We have completed a single solenoid cycle. Return to idle, ready for the next shot.
  CurrentSolenoidCyclePosition = SOLENOID_CYCLE_IDLE;
  ShotsToFire -= 1;
  LastShot = millis();
  LastSolenoidCycleStarted = millis();
  Serial.println( F("Bang!!") );  
}

/*
 * Process input from Buttons and Sensors.
 */
void ProcessButtons()
{
  FireFullTriggerBounce.update(); // Update the pin bounce state
  FireFullTriggerPressed = !(FireFullTriggerBounce.read()); 
  RequestShot = FireFullTriggerBounce.fell(); // Programatically keep track of the request for a shot
  RequestAutoStop = FireFullTriggerBounce.rose();

  // Determine the current firing mode
  ModeSelectABounce.update();
  ModeSelectBBounce.update();
  if( !AutoFire )
  {
    if( ModeSelectABounce.read() == LOW && ModeSelectBBounce.read() == HIGH && CurrentFireMode != FIRE_MODE_AUTO_LASTSHOT )
      CurrentFireMode = FIRE_MODE_AUTO;
    else if( ModeSelectABounce.read() == HIGH && ModeSelectBBounce.read() == HIGH )
      CurrentFireMode = FIRE_MODE_SINGLE;
    else if( ModeSelectABounce.read() == HIGH && ModeSelectBBounce.read() == LOW )
      CurrentFireMode = FIRE_MODE_BURST;
  }
}

// We are toggline between different system states here..
// Also handle the blasted configuration controls here... Because there's no special configuration screen
 
