/*
MAKE SENSORS CALL METHODS WHEN PEOPLE ENTER UNDER THEM
 AND ALSO WHEN PEOPLE LEAVE THEIR SENSING AREA



/* Includes ------------------------------------------------------------------*/
#include <Arduino.h>
#include <Wire.h>
#include <vl53l1x_x_nucleo_53l1a1_class.h>
#include <stmpe1600_class.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include <vector>
#include <CircularBuffer.h>
#include <Person.h>

#ifdef ARDUINO_SAM_DUE
   #define DEV_I2C Wire1
#elif defined(ARDUINO_ARCH_STM32)
   #define DEV_I2C Wire
#elif defined(ARDUINO_ARCH_AVR)
   #define DEV_I2C Wire
#else
   #define DEV_I2C Wire
#endif

#define SerialPort Serial

//For AVR compatibility where D8 and D2 are undefined
#ifndef D8
#define D8 8
#endif

#ifndef D2
#define D2 2
#endif

// Components.
STMPE1600DigiOut *xshutdown_top;
STMPE1600DigiOut *xshutdown_left;
STMPE1600DigiOut *xshutdown_right;
VL53L1_X_NUCLEO_53L1A1 *sensor_vl53l1_top;
VL53L1_X_NUCLEO_53L1A1 *sensor_vl53l1_left;
VL53L1_X_NUCLEO_53L1A1 *sensor_vl53l1_right;

std::vector<Person> active_persons;
bool sensor_in_use[] = {false, false, false};
double threshold = 50;

int person_count = 0;

CircularBuffer<double,10> l_sensor_prev_vals;
CircularBuffer<double,10> t_sensor_prev_vals;
CircularBuffer<double,10> r_sensor_prev_vals;

int l_person = -1;
int t_person = -1;
int r_person = -1;

/* Setup --------------------------------------------------------------------- */

int find_in_active_persons(int sensor_code, double measurement) {
   int closest_match_index = -1;
   double closest_match_difference = 9999;

   for( int i = 0; i < active_persons.size(); i++ ) {
      double difference = abs(active_persons[i].measurement - measurement);
      if( difference < closest_match_difference ) {
         closest_match_index = i;
         closest_match_difference = difference;
      }
   }

   Serial.print("After searching for a suitable match the index is - ");
   Serial.println(closest_match_index);

   if( closest_match_difference > threshold ) {
      Serial.println("No match found");
      // No person in active_persons list matches this person

      closest_match_index = -1;

      // Sensor is either left or right
      if( sensor_code !=  1 ) {
         Person p = Person(sensor_code, measurement);
         active_persons.push_back(p);
         closest_match_index = active_persons.size() - 1;
         Serial.print("Closest match index is now - ");
         Serial.println(closest_match_index);
      } else {
         // Person not found and they are at center sensor
         // This shouldn't occur
         // TODO: Handle this error situation
      }
      
   }

   return closest_match_index;
}

// Returns address of found person
int person_entered_sensor(int sensor_code, double measurement) {
   // Sensor is not in use
   sensor_in_use[sensor_code] = true;

   // Create pointer to what we think is the current person
   int cur_person_index = find_in_active_persons(sensor_code, measurement);

   if( cur_person_index == -1 ) {
      // Stop execution
      return -1;
   }
      
   // Check that person can move to current sensor
   switch (active_persons[cur_person_index].cur_position) {
      case 0:
      case 2:
         if( sensor_code != 1 ) {
            // Error
         }
         break;
      case 1:
         if( sensor_code != 0 || sensor_code != 2 ) {
            // Error
         }
         break;
   }
      
   active_persons[cur_person_index].cur_position = sensor_code;
   return cur_person_index;
}

void person_vacated_sensor(int sensor_code, int cur_person) {
   // Person starts at 0. 0 - 2 = 2
   // Person starts ar 2. 2 - 0 = 2
   // Person is anywhere in between != 2
   int person_path_value = active_persons[cur_person].start_position - sensor_code;
   Serial.print("PERSON PATH VALUE - ");
   Serial.println(person_path_value);
   if( person_path_value == 2 ) {
      // Person has made full successful path between sensors
      person_count++;
      active_persons.erase(active_persons.begin() + cur_person);
   } else if( person_path_value == -2 ) {
      person_count--;
      active_persons.erase(active_persons.begin() + cur_person);
   }
}

void setup()
{
   // Led.
   pinMode(13, OUTPUT);

   // Initialize serial for output.
   SerialPort.begin(115200);
   SerialPort.println("Starting...");

   // Initialize I2C bus.
   DEV_I2C.begin();

   // Create VL53L1X top component.
   xshutdown_top = new STMPE1600DigiOut(&DEV_I2C, GPIO_15, (0x42 * 2));
   sensor_vl53l1_top = new VL53L1_X_NUCLEO_53L1A1(&DEV_I2C, xshutdown_top, A2);

   // Switch off VL53L1X top component.
   sensor_vl53l1_top->VL53L1_Off();

   // Create (if present) VL53L1X left component.
   xshutdown_left = new STMPE1600DigiOut(&DEV_I2C, GPIO_14, (0x43 * 2));
   sensor_vl53l1_left = new VL53L1_X_NUCLEO_53L1A1(&DEV_I2C, xshutdown_left, D8);

   //Switch off (if present) VL53L1X left component.
   sensor_vl53l1_left->VL53L1_Off();

   // Create (if present) VL53L1X right component.
   xshutdown_right = new STMPE1600DigiOut(&DEV_I2C, GPIO_15, (0x43 * 2));
   sensor_vl53l1_right = new VL53L1_X_NUCLEO_53L1A1(&DEV_I2C, xshutdown_right, D2);

   // Switch off (if present) VL53L1X right component.
   sensor_vl53l1_right->VL53L1_Off();

   //Initialize all the sensors
   sensor_vl53l1_top->InitSensor(0x10);
   sensor_vl53l1_left->InitSensor(0x12);
   sensor_vl53l1_right->InitSensor(0x14);
}

void loop()
{

   //delay(1000);

   int status;
   uint8_t ready = 0;
   uint16_t distance;

   //Led off
   digitalWrite(13, LOW);

   //Start measurement
   sensor_vl53l1_top->VL53L1X_StartRanging();
   sensor_vl53l1_left->VL53L1X_StartRanging();
   sensor_vl53l1_right->VL53L1X_StartRanging();

   //Poll for measurement completion top sensor
   do
   {
      sensor_vl53l1_top->VL53L1X_CheckForDataReady(&ready);
   }
   while (!ready);

   //Led on
   digitalWrite(13, HIGH);

   /*

   //Get distance top
   status = sensor_vl53l1_top->VL53L1X_GetDistance(&distance);

   if (status == VL53L1_ERROR_NONE)
   {
      
      if(distance > 700) {
         Serial.println("Distance not in threshold");
         // Not in person scanning range
         return;
      }

      Serial.print("Adding value to circular buffer - ");
      // Add distance onto CircularBuffer
      t_sensor_prev_vals.unshift(distance);
      Serial.println(distance);

      // Check for person

      // Check if enough valid measurements have been taken in order to start checking if a person could be walking underneath
      if(t_sensor_prev_vals.isFull()) {
         Serial.println("Searching for possible person");
         double t_biggest = 0.0;
         double t_smallest = 99999999;
         double t_avg = 0.0;
         // Loop through all 5 elements of CircularBuffer
         for(int i = 0; i < 5; i++) {
            double cur = t_sensor_prev_vals[i];
            // Log biggest measurement
            if(cur > t_biggest) {
               t_biggest = cur;
            }
            // Log smallest measurement
            if(cur < t_smallest) {
               t_smallest = cur;
            }
            t_avg += cur;
         }

         Serial.println("Data gathered");

         // Get difference between the biggest and smallest measurements
         double diff = abs(t_biggest - t_smallest);
         // If the difference fits in our threshold, a person could has been measured
         if( diff < threshold ) {
            // Found person
            Serial.println("Found a person");
            if( !sensor_in_use[1] ) {
               Serial.println("Sensor was not in use");
               int p_i = person_entered_sensor(1, t_avg/5);
               Serial.print("The person who was returned is at index - ");
               Serial.println(p_i);
               if(p_i >= 0 ) {
                  Serial.println("Found a valid person");
                  t_person = p_i;
               }
            }
         } else {
            Serial.println("Top in use - turning to not in use");
            // Sensor has not detected possible person
            // Check of sensor was in use and set to available if so
            if(sensor_in_use[1]) {
               Serial.println("Sensor was in use");
               sensor_in_use[1] = false;
               t_sensor_prev_vals.clear();
               person_vacated_sensor(1, t_person);
            }
         }
      }
   }

   //Clear interrupt
   status = sensor_vl53l1_top->VL53L1X_ClearInterrupt();

   */

   //Poll for measurement completion left sensor
   do
   {
      sensor_vl53l1_left->VL53L1X_CheckForDataReady(&ready);
   }
   while (!ready);

   //Get distance left
   status = sensor_vl53l1_left->VL53L1X_GetDistance(&distance);

   if (status == VL53L1_ERROR_NONE)
   {
      if(distance > 700) {
         Serial.println("Distance not in threshold");
         // Not in person scanning range
         return;
      }

      // Add distance onto CircularBuffer
      l_sensor_prev_vals.unshift(distance);
      Serial.println(distance);

      /*if(l_sensor_prev_vals.isFull()) {
         double l_biggest = 0.0;
         double l_smallest = 99999999;
         double l_avg = 0.0;
         // Loop through all 5 elements of CircularBuffer
         for(int i = 0; i < 5; i++) {
            double cur = l_sensor_prev_vals[i];
            // Log biggest measurement
            if(cur > l_biggest) {
               l_biggest = cur;
            }
            // Log smallest measurement
            if(cur < l_smallest) {
               l_smallest = cur;
            }
            l_avg += cur;
         }
      }

      Serial.println("Data gathered");

      // Get difference between the biggest and smallest measurements
      double diff = abs(l_biggest - l_smallest);
      // If the difference fits in our threshold, a person could has been measured
      Serial.print("Threshold - ");
      Serial.println(diff);
      if( diff < threshold ) {
         // Found person
         Serial.println("Found a person");
         if( !sensor_in_use[0] ) {
            Serial.println("Sensor was not in use");
            int p_i = person_entered_sensor(0, l_avg/5);
            Serial.print("The person who was returned is at index - ");
            Serial.println(p_i);
            if(p_i >= 0 ) {
               Serial.println("Found a valid person");
               l_person = p_i;
            }
         }
      } else {
         Serial.println("Top in use - turning to not in use");
         // Sensor has not detected possible person
         // Check of sensor was in use and set to available if so
         if(sensor_in_use[0]) {
            Serial.println("Sensor was in use");
            sensor_in_use[1] = false;
            l_sensor_prev_vals.clear();
            person_vacated_sensor(0, l_person);
         }
      }*/
   }

   //Clear interrupt
   status = sensor_vl53l1_left->VL53L1X_ClearInterrupt();
   /*

   //Poll for measurement completion right sensor
   do
   {
      sensor_vl53l1_right->VL53L1X_CheckForDataReady(&ready);
   }
   while (!ready);

   //Get distance right
   status = sensor_vl53l1_right->VL53L1X_GetDistance(&distance);

   if (status == VL53L1_ERROR_NONE)
   {
      r_sensor_prev_vals.unshift(distance);

      // Check for person
      double r_biggest = 0.0;
      double r_smallest = 99999999;
      double r_avg = 0.0;
      if(r_sensor_prev_vals.isFull()) {
         for(int i = 0; i < 5; i++) {
            double cur = r_sensor_prev_vals[i];
            if(cur > r_biggest) {
               r_biggest = cur;
            }
            if(cur < r_smallest) {
               r_smallest = cur;
            }
            r_avg += cur;
         }

         double diff = abs(r_biggest - r_smallest);
         if( diff < threshold ) {
            // Found person
            r_person = person_entered_sensor(2, r_avg/5);
         } else {
            // Sensor has not detected possible person
            // Check of sensor was in use and set to available if so
            if(sensor_in_use[2]) {
               Serial.println("Right in use - turning to not in use");
               sensor_in_use[2] = false;
               person_vacated_sensor(0, r_person);
            }
         }
      }
   }

   //Clear interrupt
   status = sensor_vl53l1_right->VL53L1X_ClearInterrupt();

   */

   //Stop measurement on all sensors
   sensor_vl53l1_top->VL53L1X_StopRanging();
   sensor_vl53l1_left->VL53L1X_StopRanging();
   sensor_vl53l1_right->VL53L1X_StopRanging();


   Serial.print("******** ");
   Serial.print(person_count);
   Serial.println(" ********");
}
