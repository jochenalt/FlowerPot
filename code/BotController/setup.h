#ifndef SETUP_H_
#define SETUP_H_


#include <math.h>

// --- general constants ---
const float OneMicrosecond_s = 0.000001;
const float Gravity = 9.81;											// [m/s^2]
const float Gravity_mm = Gravity*1000.0;							// [mm/s^2]


// --- mechanical constants ---
const float CentreOfGravityHeight = 180; 							// [mm] center of gravity height from ground
const float MaxBotSpeed = 1800; 									// [mm/s] max speed of bot
const float MaxBotOmega= 6.0; 										// [rad/s] max vertical turn speed of bot
const float MaxBotOmegaAccel= 0.1; 									// [rad/s^2] max omega aceleration of bot
const float MaxBotAccel= 1000.0;							 		// [mm/s^2] max acceleration of bot
const float MaxBotAccelAccel= 100.0;							 	// [mm/s^3] max acceleration acceleration of bot
const float MaxTiltAngle = atan(MaxBotAccel/Gravity_mm); 			// [rad] max tilt angle, 5�
const float MaxAngularVelocityAngle =
		            MaxBotSpeed/CentreOfGravityHeight ; 			// [rad/s]

// --- Teensy ---
#define LED_PIN 13					// blinking LED on Teensy

// -- power relay ---
#define POWER_RELAY_PIN 0 			// HIGH turns on relay that turns on motor power


// --- IMU ---
// possible values of sample frequency depend on IMU MP9150 are 1000/n with n=0..32,
// i.e. 90Hz, 100Hz, 111Hz, 125Hz, 142Hz, 166 Hz, 200Hz
const int SampleFrequency 					= 200; 					// [Hz] loop time as imposed by IMU frequency
const float SamplingTime 					= 1.0/SampleFrequency; 	// [s] sampling time of the general loop
#define IMU_INTERRUPT_PIN 20										// pin that listens to interrupts coming from IMU when a new measurement is in da house
#define IMU_I2C_ADDRESS 0x69										// MPU9050 i2c address


// ---  Brushless motors   ---
// PWM ist used for the brushless motors
// max PWM value is (1<<pwmResolution)-1
const int pwmResolution = 10;
const int MaxBrushlessDriverFrequency = 1000;
// Pins for Drotek L6234 breakout, need to be PWM pins
//                                            PWM1,PWM2,PWM3
const int BrushlessDriverPWMPins[3][3] = {  { 2,    3,   4},
											{ 5,    6,   7},
											{ 8,    9,   10}};

// all L6234 are connected to one enable pin
#define BRUSHLESS_DRIVER_ENABLE_PIN  24

// --- Encoder ---
// optical encoder input, needs to be interrupt pins
//                                ENCA, ENCB
const int EncoderPins[3][2] = { { 12,   11 },
                                { 26,   25 },
							    { 28,   27 }};


#endif /* SETUP_H_ */
