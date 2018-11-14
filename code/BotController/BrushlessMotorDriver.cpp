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

const float maxAngleError = radians(30);						// limit for PID controller
const float maxAdvancePhaseAngle = radians(10);					// maximum phase between voltage and current due to EMF
const float RevPerSecondPerVolt = 5;							// motor constant of Maxon EC max 40 W
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

void precomputeSVPMWave() {
	const int maxPWMValue = (1<<pwmResolution)-1;
	const float spaceVectorScaleUpFactor = 1.15; // empiric value to reach full pwm scale
	static boolean initialized = false;
	if (!initialized) {
		for (int i = 0;i<svpwmArraySize;i++) {
			float angle = float(i)/ float(svpwmArraySize) * (TWO_PI);
			float phaseA = sin(angle);
			float phaseB = sin(angle + M_PI*2.0/3.0);
			float phaseC = sin(angle + M_PI*4.0/3.0);

			// trick to avoid the switch of 6 phases everyone else is doing, neat, huh?
			float voff = (min(phaseA, min(phaseB, phaseC)) + max(phaseA, max(phaseB, phaseC)))/2.0;
			svpwmTable[i] =   (phaseA - voff)/2.0*spaceVectorScaleUpFactor*maxPWMValue;;

			// if you want to use plain sin waves:
			// svpwmTable[i] =  (phaseA/2.0 + 0.5)*maxPWMValue;;
		}
		initialized = true;
	}
}


void MotorConfig::initDefaultValues() {
	// at slow speeds PID controller is aggressivly keeping the position
	pid_position.Kp = 1.5;
	pid_position.Ki = 1.2;
	pid_position.Kd = 0.0;

	pid_speed.Kp = .9;
	pid_speed.Ki = 0.5;
	pid_speed.Kd = 0.02;

	pid_lifter.Kp = 0.01;
	pid_lifter.Ki = 0.005;
	pid_lifter.Kd = 0.0;

}

void MotorConfig::print() {
	logger->println("motor controller configuration:");
	logger->print("   PID (speed=0)  : ");
	logger->print("P=");
	logger->print(pid_position.Kp);
	logger->print(" I=");
	logger->print(pid_position.Ki);
	logger->print(" D=");
	logger->println(pid_position.Kd);
	logger->print("   PID (speed=max): ");
	logger->print("P=");
	logger->print(pid_speed.Kp);
	logger->print(" I=");
	logger->print(pid_speed.Ki);
	logger->print(" D=");
	logger->println(pid_speed.Kd);
	logger->println();
	logger->println("lifter controller configuration:");
	logger->print("   PID (speed=max): ");
	logger->print("P=");
	logger->print(pid_lifter.Kp);
	logger->print(" I=");
	logger->print(pid_lifter.Ki);
	logger->print(" D=");
	logger->println(pid_lifter.Kd);

}


int BrushlessMotorDriver::getPWMValue(float torque, float angle_rad) {
	// map input angle to 0..2*PI
	if (angle_rad < 0)
		angle_rad += (int)(abs(angle_rad)/TWO_PI + 1.0)*TWO_PI;

	int angleIndex = ((int)(angle_rad / TWO_PI * svpwmArraySize)) % svpwmArraySize;
	if ((angleIndex < 0) || (angleIndex > svpwmArraySize))
		fatalError("getPWMValue: idx out of bounds");

	return  torque * svpwmTable[angleIndex];
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

	// low pass the speed with 50 Hz
	speedFilter.init(1000/50,SampleFrequency);
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
	analogWriteFrequency(input1Pin, 50000);
	analogWriteFrequency(input2Pin, 50000);
	analogWriteFrequency(input3Pin, 50000);

	// these pins have to have PWM functionality
	pinMode(input1Pin, OUTPUT);
	pinMode(input1Pin, OUTPUT);
	pinMode(input1Pin, OUTPUT);

	// enable all enable lines at once (Drotek L6234 board has all enable lines connected)
	pinMode(enablePin, OUTPUT);
	digitalWrite(enablePin, LOW); // start with disabled motor, ::enable turns it on
}

void BrushlessMotorDriver::setupEncoder(int EncoderAPin, int EncoderBPin, int CPR) {
	encoderAPin = EncoderAPin;
	encoderBPin = EncoderBPin;

	encoderCPR = CPR;
	encoder = new Encoder(encoderAPin, encoderBPin);
}


float BrushlessMotorDriver::turnReferenceAngle() {
	uint32_t now_us = micros();
	if (lastTurnTime_us == 0) {
		// first call of this, we dont have dT
		// be aware that in this case, we return 0
		lastTurnTime_us = now_us;
	}
	uint32_t timePassed_us = now_us - lastTurnTime_us;

	// check for overflow on micros() (happens every 70 minutes at teensy's frequency of 120MHz)
	if (now_us < lastTurnTime_us) {
		logger->println("time overflow!!!");
		timePassed_us = (4294967295 - timePassed_us) + now_us;
		logger->print("timepassed=");
		logger->println(timePassed_us);
	}
	lastTurnTime_us = now_us;
	float timePassed_s = (float)timePassed_us/1000000.0;
	if (timePassed_s > (2.0/(float)SampleFrequency)) {
		// this happens if this methods is not called often enough. Mostly
		// when serial communications takes place
		// logger->println("turnReferenceAngle's dT too big!!!!");
		// logger->print(timePassed_s*1000.0);
		// logger->println("ms");
	}

	// increase reference speed to reach target speed
	float speedDiff = targetMotorSpeed - currentReferenceMotorSpeed;
	// limit speed diff to
	speedDiff = constrain(speedDiff, -abs(targetAcc)*timePassed_s, +abs(targetAcc)*timePassed_s);
	currentReferenceMotorSpeed += speedDiff;

	// increase reference angle with given speed
	float prevReferenceAngle = referenceAngle;
	referenceAngle += currentReferenceMotorSpeed * TWO_PI * timePassed_s;

	// if speed is too high, reference angle runs away of the real angle as
	// indicated by the encoder. Limit that in order to prevent a increasing gap
	if (abs(referenceAngle - encoderAngle) > maxAngleError) {
		referenceAngle = constrain(referenceAngle,
										encoderAngle - maxAngleError,
										encoderAngle + maxAngleError);
		currentReferenceMotorSpeed = sgn(currentReferenceMotorSpeed) * abs(prevReferenceAngle-referenceAngle)/(timePassed_s*TWO_PI);
	}

	return timePassed_s;
}

void BrushlessMotorDriver::reset() {
	setMotorSpeed(0);
	readEncoder();

	referenceAngle = encoderAngle;
	lastReferenceAngle = encoderAngle;

	magneticFieldAngle = 0;				// [rad] angle of the induced magnetic field 0=1 = 2PI
	advanceAngle = 0;
	currentReferenceMotorSpeed = 0;		// [rev/s]
	measuredMotorSpeed = 0;				// [rev/s]
}

void BrushlessMotorDriver::resetEncoder() {
	if (encoder != NULL) {
		// reset all varables
		lastEncoderPosition = 0;
		encoderAngle = 0;
		encoder->write(0);
		readEncoder();
		if (abs(encoderAngle) > 0.01) {
			logger->print("encoderAngle=");
			logger->print(degrees(encoderAngle));
			fatalError("resetEncoder failed");
		}
	}
}

void BrushlessMotorDriver::readEncoder() {
	if (encoder == NULL) {
		// without encoder assume perfect motor
		encoderAngle = referenceAngle;
	}
	else {
		// find encoder position and increment the encoderAngle accordingly
		int32_t encoderPosition= encoder->read();
		encoderAngle += ((float)(lastEncoderPosition - encoderPosition))/(float)encoderCPR*TWO_PI/4.0;
		lastEncoderPosition = encoderPosition;
	}
}

// set the pwm values matching the current magnetic field angle
void BrushlessMotorDriver::sendPWMDuty(float torque) {
	int pwmValueA = getPWMValue(torque, magneticFieldAngle);
	int pwmValueB = getPWMValue(torque, magneticFieldAngle + 1.0*TWO_PI/3.0);
	int pwmValueC = getPWMValue(torque, magneticFieldAngle + 2.0*TWO_PI/3.0);
	analogWrite(input1Pin, pwmValueA);
	analogWrite(input2Pin, pwmValueB);
	analogWrite(input3Pin, pwmValueC);
}


// call me as often as possible
bool BrushlessMotorDriver::loop() {
	if (enabled) {

		// frequency of motor control is 1000Hz max
		uint32_t now = millis();
		if (now - lastLoopCall_ms < 1000/MaxBrushlessDriverFrequency)
			return false;
		lastLoopCall_ms = now;

		// turn reference angle along the set speed
		float timePassed_s = turnReferenceAngle();
		if (timePassed_s > 0) {
			// read the current encoder value
			float prevEncoderAngle = encoderAngle;
			readEncoder();

			// compute position error as input for PID controller
			float errorAngle = referenceAngle - encoderAngle;

			// carry out posh PID controller. Outcome is used to compute magnetic field angle (between -90� and +90�) and torque.
			// if pid's outcome is 0, magnetic field is like encoder's angle, and torque is 0
			float speedRatio = min(abs(currentReferenceMotorSpeed)/maxRevolutionSpeed,1.0);
			float controlOutput = pid.update(memory.persistentMem.motorControllerConfig.pid_position, memory.persistentMem.motorControllerConfig.pid_speed,
											-maxAngleError /* min */,maxAngleError /* max */, speedRatio,
											errorAngle,  timePassed_s);

			// estimate the current shift of current behind voltage (back EMF). This is typically set to increase linearly with the voltage
			// which is proportional to the torque for the PWM output
			// (according to https://www.digikey.gr/en/articles/techzone/2017/jan/why-and-how-to-sinusoidally-control-three-phase-brushless-dc-motors)
			// (according to "Advance Angle Calculation for Improvement of the Torque-to Current Ratio of Brushless DC Motor Drives")
			float advanceAnglePhaseShift = (currentReferenceMotorSpeed/maxRevolutionSpeed)*maxAdvancePhaseAngle;

			// torque is max at -90/+90 degrees
			// (https://www.roboteq.com/index.php/applications/100-how-to/359-field-oriented-control-foc-made-ultra-simple)
            advanceAngle = radians(90) * sigmoid(20.0 /* derivation at 0 */, controlOutput/maxAngleError);

			float torque = abs(controlOutput)/maxAngleError;

			// set magnetic field relative to rotor's position
			magneticFieldAngle = encoderAngle + advanceAngle + advanceAnglePhaseShift;

			// if the motor is not able to follow the magnetic field , limit the reference angle accordingly
			referenceAngle = constrain(referenceAngle, encoderAngle - maxAngleError, encoderAngle  + maxAngleError);

			measuredMotorSpeed = speedFilter.addSample((encoderAngle-prevEncoderAngle)/TWO_PI/timePassed_s);
			lastReferenceAngle = referenceAngle; // required to compute speed

			// send new pwm value to motor
			sendPWMDuty(min(abs(torque),1.0));

			return true;
		}
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
	return (reverse?-1.0:1.0)*encoderAngle;
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

void BrushlessMotorDriver::enable(bool doit) {
	enabled = doit;
	if (enabled) {
		// startup procedure to find the angle of the motor's rotor
		// - turn magnetic field with min torque (120� max) until encoder recognizes a significant movement
		// - turn in other direction until this movement until encoder gives original position
		// if the encoder does not indicate a movement, increase torque and try again

		// if (encoder == NULL)
		// 	delete encoder;
		// encoder = new Encoder(encoderAPin, encoderBPin);

		// enable driver, but PWM has no duty cycle yet.
		sendPWMDuty(0);
		digitalWrite(enablePin, HIGH);

		// startup calibration works by turning the magnetic field until the encoder
		// indicates the rotor being aligned with the field
		// During calibration, run a loop that
		// - measure the encoder
		// - turn the magnetic field in the direction of the deviation as indicated by the encoder
		// - if the encoders indicates no movement, increase torque
		// quit the loop if torque is above a certain threshold with encoder at 0
		// end calibration by setting the current reference angle to the measured rotors position
		int tries = 0;
		const int maxTries = 3;
		bool repeat = true;
		while ((tries++ <= maxTries) && (repeat == true)) {
			logger->print("enable motor ");
			logger->print(motorNo);
			logger->print(":");


			repeat = false;

			lastLoopCall_ms = 0;				// time of last loop call
			referenceAngle = 0;					// [rad] target angle of the rotor
			lastReferenceAngle = 0;				// [rad]
			currentReferenceMotorSpeed = 0;		// [rev/s]
			targetMotorSpeed = 0;
			targetAcc = 0;
			advanceAngle = 0;
			lastTurnTime_us = 0;
			pid.reset();

			// in case of a nth try start at a different angle
			magneticFieldAngle = (tries-1)*radians(360/3);
			float lastLoopEncoderAngle = 0;

			resetEncoder();
			reset();
			readEncoder();
			pid_setup.reset();

			float targetTorque = 0.0;
			float lastTorque = 0;
			float maxTorque = 0.5;
			float lastTime_us = micros();
			uint32_t now_us = micros();
			float maxEncoderAngle = 0;
			float elapsedTime = 0;
			const float timeOut = 2.0;
			bool torqueReduced = false; // after first movement is detected, torque is reduced once
			if ((abs(encoderAngle) > 0.1)) {
				logger->print("encoderAngle=");
				logger->print(degrees(encoderAngle));
				fatalError("wrong encoder initialization");
			}

			while ((targetTorque < maxTorque) && (elapsedTime < timeOut)) { // quit when above 80% torque or timeout of 5. happened
				now_us = micros();
				float dT = (now_us - lastTime_us)/1000000.0;
				lastTime_us = now_us;
				elapsedTime += dT;
				if ((int)(targetTorque/maxTorque*10.) > (int)(lastTorque/maxTorque*10.)) {
					logger->print(10-(int)(targetTorque/maxTorque*10.));
					logger->print("(m");
					logger->print(degrees(magneticFieldAngle),1);
					logger->print(" e");
					logger->print(degrees(encoderAngle),1);
					logger->print(" t");
					logger->print(targetTorque,1);

					logger->print(") ");

					lastTorque = targetTorque;
				}

				// let the magnetic field turn with 1 rev/s towards the encoder value different from 0

				float magneticFieldAngularSpeed = pid_setup.update(PIDControllerConfig(0.1,0.0,0.0), encoderAngle, dT, -radians(30), radians(30));
				magneticFieldAngle -= sgn(encoderAngle)*min(radians(2.0),abs(magneticFieldAngularSpeed)); // this is a P-controller that turns the magnetic field towards the direction of the encoder

				// logger->print(dT*100);
				// logger->print(" ");
				sendPWMDuty(targetTorque);
				delay(1);

				// if encoder indicates no movement, we can increase torque a bit until the motor moves
				// if there is movement, decrease the torque and let the motor turn until the rotor is
				// in line with the magnetic field
				readEncoder();
				if (abs(maxEncoderAngle) < abs(encoderAngle))
					maxEncoderAngle = encoderAngle;
				float encoderAngleDiff = encoderAngle - lastLoopEncoderAngle;
				lastLoopEncoderAngle = encoderAngle;
				float encoderResolution = TWO_PI/((float)encoderCPR)*2.0;
				if (abs(encoderAngleDiff) < encoderResolution && abs(encoderAngle) < encoderResolution) {
					targetTorque += dT*8.0;
					targetTorque = min(targetTorque, maxTorque);
					torqueReduced = false;
				}

				// as soon a movement is detected, reduce the torque since friction has been overcome
				// and we want to avoid to push the encoder even more into a deviation
				if (!torqueReduced && (abs(encoderAngleDiff) > encoderResolution)) {
					targetTorque *= 0.8; // ratio between gliding friction and stiction
					targetTorque = min(targetTorque, maxTorque);
					torqueReduced = true;
				}

				if (memory.persistentMem.logConfig.calibrationLog) {
					logger->print("mag=");
					logger->print(degrees(magneticFieldAngle));
					logger->print("enc=");
					logger->print(degrees(encoderAngle));
					logger->print(" to=");
					logger->print(targetTorque);
					logger->print(" ti=");
					logger->print(elapsedTime);
					logger->println();
				}
			}

			// if almost no movement happened, rotor could be located in a singularity.
			// This would be bad, rotor would never find its position during rotation.
			// We should move at least 2�
			repeat  = (targetTorque < maxTorque) || (elapsedTime >= timeOut);
			if (repeat) {
				logger->print(" failed(");
				logger->print(degrees(maxEncoderAngle),1);
				logger->print(",");
				logger->print(targetTorque,1);
				logger->print("PWM,");
				logger->print(elapsedTime,1);
				logger->println("s) ");
			}
		} // while retry
		readEncoder();
		referenceAngle = magneticFieldAngle;
		encoderAngle = magneticFieldAngle;
		lastReferenceAngle = magneticFieldAngle;

		if (tries > maxTries) {
			digitalWrite(enablePin, HIGH);
			enabled = false;
			logger->println(" failed.");
		} else
			logger->println(" done.");
	}
	else {
		digitalWrite(enablePin, LOW);
	}
}


void BrushlessMotorDriver::printHelp() {
	command->println();

	command->println("brushless motor menu");
	command->println();
	command->println("0 - stop");
	command->println("+ - inc speed");
	command->println("- - dec speed");
	command->println("* - inc acc");
	command->println("_ - dec acc");
	command->println("r - revert direction");
	command->println("T/t - increase torque");
	command->println("P/p - increase PIs controller p");
	command->println("I/i - increase PIs controller i");
	command->println("D/d - increase PIs controller d");

	command->println("e - enable");

	command->println("ESC");
}

void BrushlessMotorDriver::menuLoop(char ch, bool continously) {

		bool cmd = true;
		bool pidChange = false;
		switch (ch) {
		case '0':
			menuSpeed = 0;
			setSpeed(menuSpeed,  menuAcc);
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
				memory.persistentMem.motorControllerConfig.pid_position.Kd += 0.005;
			else
				memory.persistentMem.motorControllerConfig.pid_speed.Kd += 0.005;
			pidChange = true;

			break;
		case 'd':
			if (abs(currentReferenceMotorSpeed) < 15)
				memory.persistentMem.motorControllerConfig.pid_position.Kd -= 0.005;
			else
				memory.persistentMem.motorControllerConfig.pid_speed.Kd -= 0.005;
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
			command->print("PID(pos)=");
			command->print(memory.persistentMem.motorControllerConfig.pid_position.Kp);
			command->print(",");
			command->print(memory.persistentMem.motorControllerConfig.pid_position.Ki);
			command->print(",");
			command->print(memory.persistentMem.motorControllerConfig.pid_position.Kd);
			command->println(")");
			command->print("PID(speed)=");
			command->print(memory.persistentMem.motorControllerConfig.pid_speed.Kp);
			command->print(",");
			command->print(memory.persistentMem.motorControllerConfig.pid_speed.Ki);
			command->print(",");
			command->print(memory.persistentMem.motorControllerConfig.pid_speed.Kd);
			command->println(")");
		}
		if (cmd) {
			command->print("v_set");
			command->print(menuSpeed);
            command->print("rev/s v= ");
            command->print(getSpeed());
            command->print("rev/s angle");
            command->print(degrees(getIntegratedAngle()));
            command->print("�");

			command->print(" a=");
			command->print(menuAcc);
			command->print(" T=");
			command->print(menuTorque);
			command->print(" t=");

			command->print(micros());
			if (menuEnable)
				command->print(" enabled");
			else
				command->print(" disabled");
			command->println();

			command->print(">");
		}
}

