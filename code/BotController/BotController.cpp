/*
 * BotController.cpp
 *
 *  Created on: 21.08.2018
 *      Author: JochenAlt
 */

#include <types.h>
#include <Util.h>

#include <BotController.h>
#include <BotMemory.h>
#include <TimePassedBy.h>

const int LifterEnablePin = 31;
const int LifterIn1Pin = 29;
const int LifterIn2Pin = 30;
const int LifterEncoderAPin = 35;
const int LifterEncoderBPin = 36;
const int LifterCurrentSensePin = A22;

const int LifterCPR = 48;

void BotController::setup() {
	registerMenuController(&menuController);
	power.setup();
	ballDrive.setup(&menuController);
	imu.setup(&menuController);
	imu.setup();
	state.setup(&menuController);
	lifter.setup(&menuController);
	lifter.setupMotor(LifterEnablePin, LifterIn1Pin, LifterIn2Pin,LifterCurrentSensePin);
	lifter.setupEncoder(LifterEncoderAPin, LifterEncoderBPin, LifterCPR);

	performanceLogTimer.setRate(5000);
}

void BotController::printHelp() {
	command->println();
	command->println("Bot Menu");
	command->println();
	command->println("e - ball engine");
	command->println("s - state controller");
	command->println("i - imu");
	command->println("l - lifter");
	command->println("p - power on/off");
	command->println("b - balance on");

	command->println();
	command->println("1 - performance log on");
	command->println("2 - calibration log on");
	command->println("3 - debug log on");

	command->println("m - save configuration to epprom");
}

void BotController::menuLoop(char ch, bool continously) {
	bool cmd = true;
	switch (ch) {
	case 'b':
		balanceMode((mode==BALANCE)?OFF:BALANCE);
		break;
	case 'p':
		if (power.isMotorOn()) {
			command->println("turning motor power off");
			power.motorPower(false);
		} else {
			command->println("turning motor power on");
			power.motorPower(true);
		}
		break;

	case 'e':
		ballDrive.pushMenu();
		break;
	case 'm':
		memory.save();
		break;
	case 'l':
		lifter.pushMenu();
		break;
	case 'i':
		imu.pushMenu();
		break;
	case 's':
		state.pushMenu();
		break;
	case '1':
		memory.persistentMem.logConfig.performanceLog = !memory.persistentMem.logConfig.performanceLog;
		break;
	case '2':
		memory.persistentMem.logConfig.calibrationLog = !memory.persistentMem.logConfig.calibrationLog;
		break;
	case '3':
		memory.persistentMem.logConfig.debugBalanceLog = !memory.persistentMem.logConfig.debugBalanceLog;
		break;

	case 'h':
		printHelp();
		memory.println();
		break;
	default:
		cmd = false;
		break;
	}
	if (cmd) {
		command->print(">");
	}
}

void BotController::loop() {
	// performance measurement
	uint32_t now = micros();

	// give other libraries some time
	yield();

	// drive motors
	ballDrive.loop();

	// react on serial line
	menuController.loop();

	// check if new IMU orientation is there
	imu.loop();

	// drive the lifter
	lifter.loop();

	uint32_t imuTime = micros()-now;

	// run main balance loop. Timing is determined by IMU that sends an
	// interrupt everytime a new value is there.
	float dT = 0; // set by isNewValueAvailable
	if ((mode == BALANCE) && imu.isNewValueAvailable(dT)) {
		IMUSample sensorSample = imu.getSample();

		// apply inverse kinematics to get { speed (x,y), omega } out of wheel speed
		BotMovement currentMovement;
		ballDrive.getSpeed(sensorSample.plane[Dimension::X].angle, sensorSample.plane[Dimension::Y].angle,
				           currentMovement.speedX, currentMovement.speedY, currentMovement.omega);

		// compute new movement out of current angle, angular velocity, velocity, position
		state.update(dT, currentMovement, sensorSample, targetBotMovement);
		uint32_t balanceTime = micros()-now;

		// apply kinematics to compute wheel speed out of x,y, omega
		// and set speed of each wheel

		ballDrive.setSpeed( state.getSpeedX(), state.getSpeedY(), state.getOmega(),
				            sensorSample.plane[Dimension::X].angle,sensorSample.plane[Dimension::Y].angle);


		if (memory.persistentMem.logConfig.debugBalanceLog) {
			logger->print("a=(");
			logger->print(degrees(sensorSample.plane[Dimension::X].angle));
			logger->print(",");
			logger->print(degrees(sensorSample.plane[Dimension::Y].angle));
			logger->print(") ");
			logger->print("a'=(");
			logger->print(degrees(sensorSample.plane[Dimension::X].angularVelocity));
			logger->print(",");
			logger->print(degrees(sensorSample.plane[Dimension::Y].angularVelocity));
			logger->print(") ");

			logger->print("v=(");
			logger->print(currentMovement.speedX);
			logger->print(",");
			logger->print(currentMovement.speedY);
			logger->print(") ");
			logger->print(" state=(");
			logger->print(state.getSpeedX());
			logger->print(",");
			logger->print(state.getSpeedY());
			logger->print(")");
		}
		if (memory.persistentMem.logConfig.performanceLog) {
			logger->print(" t=(dT=");
			logger->print(dT*1000.0);
			logger->print("ms,imu=");
			logger->print(imuTime);
			logger->print("us,bal=");
			logger->print(balanceTime);
			logger->print("us,eng=");
			logger->print(ballDrive.engine.getAvrLoopTime()*1000.0);
			logger->print("ms");
			logger->println(")");
		}
	}
}



