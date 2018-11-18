/*
 * BLDCController.cpp
 *
 *  Created on: 29.07.2018
 *      Author: JochenAlt
 */

#include <Arduino.h>
#include <setup.h>
#include <Util.h>
#include <BotMemory.h>
#include <BrushlessMotorDriver.h>

#include <TimePassedBy.h>

const float maxAngleError = radians(10);						// limit for PID controller
const float minTorqueRatio = 0.1;								// minimum percentage of torque when in position
const float maxAdvancePhaseAngle = radians(10);					// maximum phase between voltage and current due to EMF
const float RevPerSecondPerVolt = 4;							// motor constant of Maxon EC max 40 W
const float voltage = 16;										// [V] coming from the battery to server the motors
const float maxRevolutionSpeed = voltage*RevPerSecondPerVolt; 	// [rev/s]


// function that looks like
//      1|  -------
//       |/
// ------/--------
//      /|
//     / |
// ---   |-1
// (but smooth of course)
float sigmoid(float gain /* derivation at 0 */, float x) {
    return 1.0-2.0/(1.0 + exp(gain*2.0 * x));
}


// array to store pre-computed values of space vector wave form (SVPWM)
// array size is choosen by having a maximum difference of 1% in two subsequent table items,
// i.e. we have a precision of 1%. The rest is compensated by the optical
// encoder with a precision of 0.1%
#define svpwmArraySize 244 // manually set such that two adjacent items have a difference of 2 of 255 at most (approx. 1%)
int svpwmTable[svpwmArraySize];

void setPwmTable(int i, int value) {
	if ((i<0) || (i> svpwmArraySize))
		fatalError("pwmTable:Idx out of bound");
	svpwmTable[i] = value;
}

int getPwmTable(int i) {
	if ((i<0) || (i> svpwmArraySize))
		fatalError("pwmTable:Idx out of bound");
	return svpwmTable[i];
}

void precomputeSVPMWave() {
	const int maxPWMValue = (1<<pwmResolution)-1;
	const float spaceVectorScaleUpFactor = 1.15; // empiric value to reach full pwm scale
	static boolean initialized = false;
	if (!initialized) {
		for (int i = 0;i<svpwmArraySize;i++) {
			float angle = float(i) / float(svpwmArraySize) * (TWO_PI);
			float phaseA = sin(angle);
			float phaseB = sin(angle + M_PI*2.0/3.0);
			float phaseC = sin(angle + M_PI*4.0/3.0);

			// trick to avoid the switch of 6 phases everyone else is doing, neat, huh?
			float voff = (min(phaseA, min(phaseB, phaseC)) + max(phaseA, max(phaseB, phaseC)))/2.0;
			setPwmTable(i,(phaseA - voff)/2.0*spaceVectorScaleUpFactor*maxPWMValue);

			// if you want to use plain sin waves:
			setPwmTable(i,(phaseA/2.0 + 0.5)*maxPWMValue);
		}
		initialized = true;
	}
}



void MotorConfig::initDefaultValues() {
	// at slow speeds PID controller is aggressivly keeping the position
	pid_position.Kp = 0.5;
	pid_position.Ki = 0.2;
	pid_position.Kd = 0.002;

	pid_speed.Kp = 0.1;
	pid_speed.Ki = 0.1;
	pid_speed.Kd = 0.001;

	pid_lifter.Kp = 0.01;
	pid_lifter.Ki = 0.005;
	pid_lifter.Kd = 0.0;

	phaseAAngle[0] = 0;
	phaseAAngle[1] = radians(341.19);
	phaseAAngle[2] = 0;
}

void MotorConfig::print() {
	loggingln("motor controller configuration:");
	logging("   PID (speed=0)  : ");
	logging("(");
	logging(pid_position.Kp,3);
	logging(",");
	logging(pid_position.Ki,3);
	logging(",");
	logging(pid_position.Kd,4);
	loggingln(")");
	logging("   PID (speed=max): ");
	logging("(");
	logging(pid_speed.Kp,3);
	logging(",");
	logging(pid_speed.Ki,3);
	logging(",");
	logging(pid_speed.Kd,4);
	loggingln(")");
	logging("   rotorAngle=(");
	logging(degrees(phaseAAngle[0]),1);
	logging(",");
	logging(degrees(phaseAAngle[1]),1);
	logging(",");
	logging(degrees(phaseAAngle[2]),1);
	loggingln(")");
	loggingln("lifter controller configuration:");
	logging("   PID (speed=max): ");
	logging("(");
	logging(pid_lifter.Kp);
	logging(",");
	logging(pid_lifter.Ki);
	logging(",");
	logging(pid_lifter.Kd);
	loggingln(")");
}


int BrushlessMotorDriver::getPWMValue(float torque, float angle_rad) {
	// map input angle to 0..2*PI

	// clear negative angles (fmod does not do this)
	if (angle_rad < 0)
		angle_rad += (-angle_rad/TWO_PI + 1.0)*TWO_PI;

	angle_rad = fmod(angle_rad, TWO_PI);

	// compute index in precomputed pwm array
	int angleIndex = ((int)(angle_rad / TWO_PI * svpwmArraySize));
	if ((angleIndex < 0) || (angleIndex > svpwmArraySize))
		fatalError("getPWMValue: idx out of bounds");

	return  torque * getPwmTable(angleIndex);
}

BrushlessMotorDriver::BrushlessMotorDriver() {
	// initialize precomputed spvm values
	// first invocation does the initialization
	precomputeSVPMWave();

}

void BrushlessMotorDriver::setup( int motorNo, MenuController* menuCtrl, bool reverse) {
	this->motorNo = motorNo;
	this->reverse = reverse;
	registerMenuController(menuCtrl);

	// initialize SPI bus used to communicate with AS5047D magnetic encoders
	// (this is done once only)
	AS5047D::setupBus(MOSI_PIN, MISO_PIN, SCK_PIN);

}

void BrushlessMotorDriver::setupMotor(int EnablePin, int Input1Pin, int Input2Pin, int Input3Pin) {
	// there's only one enable pin that has a short cut to EN1, EN2, and EN3 from L6234
	enablePin = EnablePin;

	input1Pin = Input1Pin;
	input2Pin = Input2Pin;
	input3Pin = Input3Pin;

	// setup L6234 input PWM pins
	analogWriteResolution(pwmResolution);

	// choose a frequency that just can't be heard
	analogWriteFrequency(input1Pin, 20000);
	analogWriteFrequency(input2Pin, 20000);
	analogWriteFrequency(input3Pin, 20000);

	// these pins have to have PWM functionality
	pinMode(input1Pin, OUTPUT);
	pinMode(input1Pin, OUTPUT);
	pinMode(input1Pin, OUTPUT);

	// enable all enable lines at once (Drotek L6234 board has all enable lines connected)
	pinMode(enablePin, OUTPUT);
	digitalWrite(enablePin, LOW); // start with disabled motor, ::enable turns it on
}

void BrushlessMotorDriver::setupEncoder(uint8_t clientSelectPin) {

	// initialize SPI's CS
	magEncoder.setup(clientSelectPin);
}


void BrushlessMotorDriver::turnReferenceAngle(float dT) {
	// increase reference speed to reach target speed
	float speedDiff = targetMotorSpeed - currentReferenceMotorSpeed;
	// limit speed diff to
	speedDiff = constrain(speedDiff, -abs(targetAcc)*dT, +abs(targetAcc)*dT);
	currentReferenceMotorSpeed += speedDiff;
	currentReferenceMotorSpeed = constrain(currentReferenceMotorSpeed, -maxRevolutionSpeed, + maxRevolutionSpeed);
	/*

	logging("tar=");
	logging(targetMotorSpeed);
	logging("speeddif=");
	logging(speedDiff);
	logging("targetAcc=");
	logging(targetAcc);
	logging("currV");
	logging(currentReferenceMotorSpeed);
	logging("ref");
	logging(referenceAngle);
	loggingln();
*/
	// increase reference angle with given speed
	referenceAngle += currentReferenceMotorSpeed * TWO_PI * dT;
}

void BrushlessMotorDriver::reset() {
	targetMotorSpeed = 0;
	targetAcc = MaxWheelAcceleration;
	magEncoder.reset();
	readEncoderAngle();
	magneticFieldAngle = getEncoderAngle();				// [rad] angle of the induced magnetic field 0=1 = 2PI
	referenceAngle = magneticFieldAngle;
	lastReferenceAngle = magneticFieldAngle;

	currentReferenceMotorSpeed = 0;		// [rev/s]
	measuredMotorSpeed = 0;				// [rev/s]
	lastTurnTime_us = 0;
	pid.reset();

							logging("a=");
							logging(degrees(getEncoderAngle()));
							logging(" ref=");
							logging(degrees(referenceAngle));
							logging(" vref=");
							logging(degrees(currentReferenceMotorSpeed));
							logging(" mag=");
							logging(degrees(magneticFieldAngle));
							logging(" v=");
							logging(targetMotorSpeed,3);
							loggingln();

}



// set the pwm values matching the current magnetic field angle
void BrushlessMotorDriver::sendPWMDuty(float torque) {
	float pwmValueA = getPWMValue(torque, magneticFieldAngle );
	float pwmValueB = getPWMValue(torque, magneticFieldAngle + 1.0*TWO_PI/3.0);
	float pwmValueC = getPWMValue(torque, magneticFieldAngle + 2.0*TWO_PI/3.0);
	analogWrite(input1Pin, pwmValueA);
	analogWrite(input2Pin, pwmValueB);
	analogWrite(input3Pin, pwmValueC);
}


// call me as often as possible
bool BrushlessMotorDriver::loop() {
	if (enabled) {

		// frequency of motor control is 1000Hz max
		uint32_t now = millis();
		uint32_t timePassed = now - lastLoopCall_ms;
		if (timePassed < 1000/MaxBrushlessDriverFrequency) {
			lastLoopCall_ms = now;
			return false;
		}
		lastLoopCall_ms = now;

		float dT = ((float)timePassed)/1000.0;

		// turn reference angle along the set speed
		turnReferenceAngle(dT);

		// read the current encoder value
		float prevEncoderAngle = getEncoderAngle();

		// read new angle from sensor
		readEncoderAngle();

		// compute position error as input for PID controller
		float errorAngle = referenceAngle - getEncoderAngle() ;

		// carry out posh PID controller. Outcome is used to compute magnetic field angle (between -90� and +90�) and torque.
		// if pid's outcome is 0, magnetic field is like encoder's angle, and torque is 0
		float speedRatio = min(abs(currentReferenceMotorSpeed)/maxRevolutionSpeed,1.0);
		float controlOutput = pid.update(memory.persistentMem.motorControllerConfig.pid_position, memory.persistentMem.motorControllerConfig.pid_speed,
										-maxAngleError /* min */,maxAngleError /* max */, speedRatio,
											errorAngle,  dT);

		// estimate the current shift of current behind voltage (back EMF). This is typically set to increase linearly with the voltage
		// which is proportional to the torque for the PWM output
		// (according to https://www.digikey.gr/en/articles/techzone/2017/jan/why-and-how-to-sinusoidally-control-three-phase-brushless-dc-motors)
		// (according to "Advance Angle Calculation for Improvement of the Torque-to Current Ratio of Brushless DC Motor Drives")
		float advanceAnglePhaseShift = (currentReferenceMotorSpeed/maxRevolutionSpeed)*maxAdvancePhaseAngle;

		// torque is max at -90/+90 degrees
		// (https://www.roboteq.com/index.php/applications/100-how-to/359-field-oriented-control-foc-made-ultra-simple)
         float advanceAngle = radians(90) * sigmoid(40.0 /* derivation at 0 */, controlOutput/maxAngleError);

		float torque = abs(controlOutput)/maxAngleError;

		// set magnetic field relative to rotor's position
		magneticFieldAngle = getEncoderAngle() + advanceAngle + radians(90);

		measuredMotorSpeed = (getEncoderAngle()-prevEncoderAngle)/TWO_PI/dT;
		lastReferenceAngle = referenceAngle; // required to compute speed

		// send new pwm value to motor
		sendPWMDuty(min(abs(torque),1.0));
/*
						static TimePassedBy timer;
						if (timer.isDue_ms(2000,millis())) {
							logging("a=");
							logging(degrees(getEncoderAngle()));
							logging(" aa=");
							logging(degrees(advanceAngle));
							logging(" sr=");
							logging(speedRatio);
							logging(" co=");
							logging(degrees(controlOutput));

							logging(" e=");
							logging(degrees(errorAngle));
							logging(" ref=");
							logging(degrees(referenceAngle));
							logging(" mag=");
							logging(degrees(magneticFieldAngle));
							logging(" v=");
							logging(currentReferenceMotorSpeed,1);
							logging(" tv=");
							logging(targetMotorSpeed,1);
							logging(" torque=");
							logging(torque,2);
							logging(" dT=");
							logging(dT,4);

							loggingln();
							*/
		return true;
	}

	return false;
}

void BrushlessMotorDriver::setMotorSpeed(float speed /* rotations per second */, float acc /* rotations per second^2 */) {
	targetMotorSpeed = (reverse?-1.0:1.0)*speed;
	targetAcc = acc;
}

float BrushlessMotorDriver::getMotorSpeed() {
	return (reverse?-1.0:1.0)*measuredMotorSpeed;
}

float BrushlessMotorDriver::getIntegratedMotorAngle() {
	return (reverse?-1.0:1.0)*getEncoderAngle();
}

void BrushlessMotorDriver::setSpeed(float speed /* rotations per second */, float acc /* rotations per second^2 */) {
	setMotorSpeed((reverse?-1.0:1.0)*speed/GearBoxRatio,acc);
}

float BrushlessMotorDriver::getSpeed() {
	return getMotorSpeed()*GearBoxRatio;
}

float BrushlessMotorDriver::getIntegratedAngle() {
	return getIntegratedMotorAngle()*GearBoxRatio;
}

float BrushlessMotorDriver::getEncoderAngle() {
	return magEncoder.getAngle() - 	memory.persistentMem.motorControllerConfig.phaseAAngle[motorNo];
}

void BrushlessMotorDriver::readEncoderAngle() {
	magEncoder.readAngle();
}

void BrushlessMotorDriver::enable(bool doit) {
	if (doit && !enabled) {
		reset();
		magneticFieldAngle = getEncoderAngle();
		digitalWrite(enablePin, HIGH);
		enabled = true;
		return;
	}
	if (!doit && enabled) {
		digitalWrite(enablePin, LOW);
		enabled = false;
		return;
	}
}

void BrushlessMotorDriver::calibrate() {
	logging("calibrating motor[");
	logging(motorNo);
	logging("]");

	enable(true);
	analogWrite(input1Pin, (1<<pwmResolution)/2.0);
	analogWrite(input2Pin, 0);
	analogWrite(input3Pin, 0);
	delay(1000);

	reset();
	float nullAngle = magEncoder.readAngle();
	logging(" angle of phase A =");
	logging(degrees(nullAngle),3,1);
	logging("(");
	logging(nullAngle,2);
	loggingln("rad)");
	magneticFieldAngle = 0;
	sendPWMDuty(0.0);

	memory.persistentMem.motorControllerConfig.phaseAAngle[motorNo] = nullAngle;
	enable(false);
}

void BrushlessMotorDriver::printHelp() {
	loggingln();

	loggingln("brushless motor menu");
	loggingln();
	loggingln("0 - stop");
	loggingln("+ - inc speed");
	loggingln("- - dec speed");
	loggingln("* - inc acc");
	loggingln("_ - dec acc");
	loggingln("r - revert direction");
	loggingln("c - calibrate");
	loggingln("T/t - increase torque");
	loggingln("P/p - increase PIs controller p");
	loggingln("I/i - increase PIs controller i");
	loggingln("D/d - increase PIs controller d");

	loggingln("e - enable");

	loggingln("ESC");
}

void BrushlessMotorDriver::menuLoop(char ch, bool continously) {

		bool cmd = true;
		bool pidChange = false;
		switch (ch) {
		case '0':
			menuSpeed = 0;
			setSpeed(menuSpeed,  menuAcc);
			break;
		case 'c':
			calibrate();
			break;
		case '+':
			if (abs(menuSpeed) < 2)
				menuSpeed += 0.05;
			else
				menuSpeed += 1.0;

			setSpeed(menuSpeed,  menuAcc);
			break;
		case '-':
			if (abs(menuSpeed) < 2)
				menuSpeed -= 0.05;
			else
				menuSpeed -= 1.0;
			setSpeed(menuSpeed,  menuAcc);
			break;
		case '*':
			menuAcc++;
			setSpeed(menuSpeed,  menuAcc);
			break;
		case '_':
			menuAcc--;
			setSpeed(menuSpeed,  menuAcc);
			break;
		case 'r':
			menuSpeed = -menuSpeed;
			setSpeed(menuSpeed,  menuAcc);
			break;
		case 'P':
			if (abs(currentReferenceMotorSpeed) < 15)
				memory.persistentMem.motorControllerConfig.pid_position.Kp  += 0.02;
			else
				memory.persistentMem.motorControllerConfig.pid_speed.Kp  += 0.02;

			pidChange = true;
			break;
		case 'p':
			if (abs(currentReferenceMotorSpeed) < 15)
				memory.persistentMem.motorControllerConfig.pid_position.Kp -= 0.02;
			else
				memory.persistentMem.motorControllerConfig.pid_speed.Kp -= 0.02;
			pidChange = true;
			break;
		case 'D':
			if (abs(currentReferenceMotorSpeed) < 15)
				memory.persistentMem.motorControllerConfig.pid_position.Kd += 0.0001;
			else
				memory.persistentMem.motorControllerConfig.pid_speed.Kd += 0.0001;
			pidChange = true;

			break;
		case 'd':
			if (abs(currentReferenceMotorSpeed) < 15)
				memory.persistentMem.motorControllerConfig.pid_position.Kd -= 0.0001;
			else
				memory.persistentMem.motorControllerConfig.pid_speed.Kd -= 0.0001;
			pidChange = true;
			break;
		case 'I':
			if (abs(currentReferenceMotorSpeed) < 15)
				memory.persistentMem.motorControllerConfig.pid_position.Ki += 0.02;
			else
				memory.persistentMem.motorControllerConfig.pid_speed.Ki += 0.02;
			pidChange = true;
			break;
		case 'i':
			if (abs(currentReferenceMotorSpeed) < 15)
				memory.persistentMem.motorControllerConfig.pid_position.Ki -= 0.02;
			else
				memory.persistentMem.motorControllerConfig.pid_speed.Ki -= 0.02;
			pidChange = true;
			break;
		case 'e':
			menuEnable = menuEnable?false:true;
			menuSpeed = 0;
			enable(menuEnable);
			break;
		case 'h':
			printHelp();
			break;
		default:
			cmd = false;
			break;
		}

		if (pidChange) {
			logging("PID(pos)=(");
			logging(memory.persistentMem.motorControllerConfig.pid_position.Kp,3);
			logging(",");
			logging(memory.persistentMem.motorControllerConfig.pid_position.Ki,3);
			logging(",");
			logging(memory.persistentMem.motorControllerConfig.pid_position.Kd,4);
			loggingln(")");
			logging("PID(speed)=(");
			logging(memory.persistentMem.motorControllerConfig.pid_speed.Kp,3);
			logging(",");
			logging(memory.persistentMem.motorControllerConfig.pid_speed.Ki,3);
			logging(",");
			logging(memory.persistentMem.motorControllerConfig.pid_speed.Kd,4);
			loggingln(")");
		}
		if (cmd) {
			logging("v_set=");
			logging(menuSpeed,1);
            logging(" rev/s v=");
            logging(getSpeed(),1);
            logging(" rev/s v=");
            logging(measuredMotorSpeed,1);

            logging("rev/s angle=");
            logging(degrees(getIntegratedAngle()),1);
            logging("�");

			logging(" a=");
			logging(menuAcc);
			logging(" T=");
			logging(menuTorque);
			logging(" t=");

			logging(micros());
			if (menuEnable)
				logging(" enabled");
			else
				logging(" disabled");
			loggingln();

			logging(">");
		}
}

