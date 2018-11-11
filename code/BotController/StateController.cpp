/*
 * StateController.cpp
 *
 *  Created on: 23.08.2018
 *      Author: JochenAlt
 */

#include "Arduino.h"
#include <BotMemory.h>
#include <Util.h>

#include <MenuController.h>
#include <StateController.h>
#include <BotController.h>


void StateControllerConfig::print() {
	StateControllerConfig defValue;
	defValue.initDefaultValues();
	logger->println("state controller configuration:");
	logger->print("   angle=");
	logger->print(angleWeight);
	logger->print("(");
	logger->print(defValue.angleWeight);
	logger->print(")");
	logger->print(" angularSpeed=");
	logger->print(angularSpeedWeight);
	logger->print("(");
	logger->println(defValue.angularSpeedWeight);
	logger->print("   intBallPos=");
	logger->print(ballPosIntegratedWeight);
	logger->print("(");
	logger->print(defValue.ballPosIntegratedWeight);
	logger->print(")");
	logger->print(" ballPos=");
	logger->print(ballPositionWeight);
	logger->print("(");
	logger->print(defValue.ballPositionWeight);
	logger->print(")");
	logger->print(" ballSpeed=");
	logger->print(ballVelocityWeight);
	logger->print("(");
	logger->print(defValue.ballVelocityWeight);
	logger->print(")");
	logger->print(" ballAccel=");
	logger->print(ballAccelWeight);
	logger->print("(");
	logger->print(defValue.ballAccelWeight);
	logger->println(")");
	logger->print("   intBodyPos=");
	logger->print(bodyPosIntegratedWeight);
	logger->print("(");
	logger->print(defValue.bodyPosIntegratedWeight);
	logger->print(")");
	logger->print(" bodyPosition=");
	logger->print(bodyPositionWeight);
	logger->print("(");
	logger->print(defValue.bodyPositionWeight);
	logger->print(")");
	logger->print(" bodySpeed=");
	logger->print(bodyVelocityWeight);
	logger->print("(");
	logger->print(defValue.bodyVelocityWeight);
	logger->print(")");
	logger->print(" bodyAccel=");
	logger->print(bodyAccelWeight);
	logger->print("(");
	logger->print(defValue.bodyAccelWeight);
	logger->println(")");
	logger->print("   omega=");
	logger->print(omegaWeight);
	logger->print("(");
	logger->print(defValue.omegaWeight);
	logger->println(")");
}

void StateControllerConfig::initDefaultValues() {

	// initialize the weights used for the state controller per
	// computed state dimension
	// state controller consists of
	// (angle, angular speed,
	//  ball position, ball speed, ball acceleration,
	//  body position, body speed, body acceleration,
	// omega)
	angleWeight				= 2200.0; // 39.0;
	angularSpeedWeight		= 1400.0; // 21.00;

	ballPosIntegratedWeight   = 0;
	ballPositionWeight		= -1.5; // 1.5;
	ballVelocityWeight		= 0.0;
	ballAccelWeight			= 0.0; // 1.3

	bodyPosIntegratedWeight   = 0;
	bodyPositionWeight		= 0.0;
	bodyVelocityWeight		= 9.0; // 9.0
	bodyAccelWeight			= 0.0;

	omegaWeight				= 0.0;
}


void ControlPlane::reset () {
			lastTargetAngle = 0;
			lastBodyPos = 0; // absolute body position of last loop
			lastBallPos = 0;
			lastBodySpeed = 0;
			lastBallSpeed = 0;
			lastTargetBodyPos = 0;
			lastTargetBallPos = 0;
			lastTargetBallSpeed = 0;
			lastTargetBodySpeed = 0;
			filteredSpeed = 0;
			speed = 0;
			ballPosIntegrated = 0;
			bodyPosIntegrated = 0;

			// add an FIR Filter with 15Hz to the output of the controller in order to increase gain of state controller
			outputSpeedFilter.init(FIR::LOWPASS,
					         1.0e-3f  			/* allowed ripple in passband in amplitude is 0.1% */,
							 1.0e-6 			/* supression in stop band is -60db */,
							 SampleFrequency, 	/* 200 Hz */
							 15.0f  			/* low pass cut off frequency */);

			inputBallAccel.init(FIR::LOWPASS,
					         1.0e-3f  			/* allowed ripple in passband in amplitude is 0.1% */,
							 1.0e-6 			/* supression in stop band is -60db */,
							 SampleFrequency, 	/* 200 Hz */
							 30.0f  			/* low pass cut off frequency */);

			inputBodyAccel.init(FIR::LOWPASS,
					          1.0e-3f  			/* allowed ripple in passband in amplitude is 0.1% */,
							  1.0e-6 			/* supression in stop band is -60db */,
							  SampleFrequency,  /* 200 Hz */
							  30.0f  			/* low pass cut off frequency */);
}

float ControlPlane::getBodyPos() {
	return lastBodyPos;
}

float ControlPlane::getBallPos() {
	return lastBallPos;
}


void ControlPlane::update(bool log, float dT,
		const State& current, const State& target,
		float currentOmega, float targetOmega,
		const IMUSamplePlane &sensor) {

	if (dT) {
		// limit position/speed/accel error to that of max tilt error
		const float maxPositionError = 50.0;

		// target angle out of acceleration, assume tan(x) = x
		float targetAngle = target.accel/Gravity;

		// target angularVelocity out of acceleration
		float targetAngularVelocity = (targetAngle - lastTargetAngle)*dT;

		// compute pos,speed,accel of the ball
		float absBallPos   		= current.pos;
		float absBallSpeed 		= current.speed;
		float absBallAccel 		= inputBallAccel.update(current.accel); // lowpass the acceleration with 30 Hz

		// compute pos,speed,accel in he centre of gravity of the body
		float absBodyPos   		= current.pos + sensor.angle * CentreOfGravityHeight;
		float absBodySpeed 		= (absBodyPos - lastBodyPos)/dT;
		float absBodyAccel 		= inputBodyAccel.update((absBodySpeed - lastBodySpeed) / dT); // lowpass the acceleration with 30 Hz

		// compute target position,speed, and accel
		float targetBodyPos 	= target.pos;
		float targetBodySpeed	= target.speed;
		float targetBodyAccel	= (target.speed - lastTargetBodySpeed)/dT;

		float targetBallPos	 	= target.pos - targetAngle * CentreOfGravityHeight;
		float targetBallSpeed 	= (targetBallPos - lastTargetBallPos)/dT;
		float targetBallAccel 	= (targetBallAccel - lastTargetBallSpeed)/dT;	// does not need to be filtered, it is generated in a smooth way already

		// errors
		float error_tilt			= (sensor.angle-targetAngle)/MaxTiltAngle;						// 39
		float error_angular_speed	= (sensor.angularVelocity-targetAngularVelocity)/MaxTiltAngle;	// 21

		float error_ball_position 	= (absBallPos 	- targetBallPos);		    	// 1.5
		ballPosIntegrated 			+= error_ball_position*dT;
		float error_ball_velocity 	= (absBallSpeed	- targetBallSpeed); 			// [0]
		float error_ball_accel		= (absBallAccel	- targetBallAccel);			// 1.3

		float error_body_position	= (absBodyPos	- targetBodyPos); 			// [0]
		bodyPosIntegrated 			+= error_body_position*dT;

		float error_body_velocity	= (absBodySpeed	- targetBodySpeed);			// 9
		float error_body_accel		= (absBodyAccel	- targetBodyAccel);    		// [0]

		float error_centripedal     = targetOmega * target.speed;
		StateControllerConfig& config = memory.persistentMem.ctrlConfig;
		/*
		if (abs(config.ballPositionWeight) > 0.01)
			error_ball_position = constrain(error_ball_position,  -config.angleWeight*MaxTiltAngle/config.ballPositionWeight, -config.angleWeight*MaxTiltAngle/config.ballPositionWeight);
		if (abs(config.bodyPositionWeight) > 0.01)
			error_body_position = constrain(error_body_position,  -config.angleWeight*MaxTiltAngle/config.bodyPositionWeight, -config.angleWeight*MaxTiltAngle/config.bodyPositionWeight);
		*/
		// sum up all weighted errors
		float error =	+ config.angleWeight*error_tilt + config.angularSpeedWeight*error_angular_speed
						- config.ballPosIntegratedWeight*ballPosIntegrated - config.ballPositionWeight*error_ball_position - config.ballVelocityWeight*error_ball_velocity - config.ballAccelWeight*error_ball_accel
						- config.bodyPosIntegratedWeight*bodyPosIntegrated - config.bodyPositionWeight*error_body_position - config.bodyVelocityWeight*error_body_velocity - config.bodyAccelWeight*error_body_accel
						+ config.omegaWeight * error_centripedal;

		if (log) {
			if (memory.persistentMem.logConfig.debugStateLog) {
				logger->print("currV=");
				logger->print(current.speed);
				logger->print(" currA=");
				logger->print(sensor.angle);
				logger->print(" currA'=");
				logger->print(sensor.angularVelocity);
				logger->print(" targA=");
				logger->print(targetAngle);

				logger->print(" body=(");
				logger->print(absBodyPos);
				logger->print(",");
				logger->print(absBodySpeed);
				logger->print(",");
				logger->print(absBallAccel);
				logger->print(") ball=(");
				logger->print(absBallPos);
				logger->print(",");
				logger->print(absBallSpeed);
				logger->print(",");
				logger->print(absBallAccel);
				logger->print(")");

				logger->print("error=(");
				logger->print(error_tilt);
				logger->print(",");
				logger->print(error_angular_speed);
				logger->print("|");
				logger->print(ballPosIntegrated);
				logger->print(",");
				logger->print(error_ball_position);
				logger->print(",");
				logger->print(error_ball_velocity);
				logger->print(",");
				logger->print(error_ball_accel);
				logger->print("|");
				logger->print(bodyPosIntegrated);
				logger->print(",");
				logger->print(error_body_position);
				logger->print(",");
				logger->print(error_body_velocity);
				logger->print(",");
				logger->print(error_body_accel);
				logger->print("|=");
				logger->print(error);
				logger->print(")");
			}
		}
		float accel = constrain(error,-MaxBotAccel, MaxBotAccel);

		// accelerate if not on max speed already
		if ((sgn(speed) != sgn(accel)) ||
			(abs(speed) < MaxBotSpeed)) {
			speed -= accel * dT;
			speed = constrain(speed, -MaxBotSpeed, + MaxBotSpeed);
		}
		// in order to increase gain of state controller, filter with FIR 15Hz 4th order
		filteredSpeed = outputSpeedFilter.update(speed);

		lastTargetAngle = targetAngle;
		lastBodyPos = absBodyPos;
		lastBodySpeed = absBodySpeed;

		lastBallPos = absBallPos;
		lastBallSpeed = absBallSpeed;

		lastTargetBodyPos = targetBodyPos;
		lastTargetBodySpeed = targetBodySpeed;

		lastTargetBallPos = targetBallPos;
		lastTargetBallSpeed = targetBallSpeed;


		if (log)
			if (memory.persistentMem.logConfig.debugStateLog) {
				logger->print(" output=(");
				logger->print(accel);
				logger->print(",");
				logger->print(speed);
				logger->print(",");
				logger->print(filteredSpeed);
				logger->print(")");
			}
	};
}

void StateController::setup(MenuController* menuCtrl) {
	registerMenuController(menuCtrl);
	reset();
}

void StateController::reset() {
	planeX.reset();
	planeY.reset();
	rampedTargetMovement.reset();
}

void StateController::update(float dT,
							 const IMUSample& sensorSample,
							 const BotMovement& currentMovement,
							 const BotMovement& targetBotMovement) {

	uint32_t start = millis();
	// ramp up target speed and omega with a trapezoid profile of constant acceleration
	rampedTargetMovement.rampUp(targetBotMovement, dT);
	bool log = logTimer.isDue_ms(1000,millis());
	if (log && memory.persistentMem.logConfig.debugStateLog)
		logger->print("   planeX:");
	planeX.update(log, dT,
					currentMovement.x, rampedTargetMovement.x,
					currentMovement.omega, rampedTargetMovement.omega,
					sensorSample.plane[Dimension::X]);

	if (log && memory.persistentMem.logConfig.debugStateLog) {
		logger->println();
		logger->print("   planeY:");
	}
	planeY.update(log, dT,
					currentMovement.y, rampedTargetMovement.y,
					currentMovement.omega, rampedTargetMovement.omega,
					sensorSample.plane[Dimension::Y]);
	if (log && memory.persistentMem.logConfig.debugStateLog) {
		logger->println();
	}
	uint32_t end = millis();
	avrLoopTime = (avrLoopTime + ((float)(end - start)*0.001))*0.5;
}

float StateController::getSpeedX() {
	return planeX.filteredSpeed;
}
float StateController::getSpeedY() {
	return planeY.filteredSpeed;
}

float StateController::getOmega() {
	return rampedTargetMovement.omega;
}

void StateController::printHelp() {
	command->println();
	command->println("State controller");
	command->println();

	command->println("q/Q - angle weight");
	command->println("a/A - angular speed weight");
	command->println();
	command->println("e/E - ball pos integrated weight");
	command->println("w/W - ball position weight");
	command->println("s/S - ball speed weight");
	command->println("r/r - ball accel weight");
	command->println();
	command->println("d/D - body pos integrated weight");
	command->println("f/f - body position weight");
	command->println("t/T - body speed weight");
	command->println("g/G - body accel weight");
	command->println("z/Z - omega weight");
	command->println("b   - balance on/off");

	command->println("0   - set null");

	command->println();
	command->println("ESC");
}


void StateController::menuLoop(char ch, bool continously) {

		bool cmd = true;
		StateControllerConfig& config = memory.persistentMem.ctrlConfig;
		switch (ch) {
		case 'h':
			printHelp();
			break;
		case 'b':
			BotController::getInstance().balanceMode(BotController::getInstance().isBalancing()?
											BotController::BotMode::OFF:
											BotController::BotMode::BALANCING);
			if (BotController::getInstance().isBalancing())
				logger->println("balancing mode on");
			else
				logger->println("balancing mode off");
			break;
		case '0':
			config.angleWeight = 0.0;
			config.angularSpeedWeight = 0.0;
			config.ballPosIntegratedWeight = 0.0;
			config.ballPositionWeight = 0.0;
			config.ballVelocityWeight = 0.;
			config.ballAccelWeight = 0.0;
			config.bodyPosIntegratedWeight = 0.0;
			config.bodyPositionWeight = 0.;
			config.bodyVelocityWeight = 0.0;
			config.bodyAccelWeight = 0.;
			config.omegaWeight = 0.;
			break;
		case 'q':
			config.angleWeight -= continously?2.0:0.5;
			config.print();
			cmd =true;
			break;
		case 'Q':
			config.angleWeight += continously?2.0:0.5;
			config.print();
			cmd = true;
			break;
		case 'e':
			config.ballPosIntegratedWeight-= continously?1.0:0.2;
			config.print();
			cmd = true;
			break;
		case 'E':
			config.ballPosIntegratedWeight += continously?1.0:0.2;
			config.print();
			cmd = true;
			break;
		case 'd':
			config.bodyPosIntegratedWeight-= continously?1.0:0.2;
			config.print();
			cmd =true;
			break;
		case 'D':
			config.bodyPosIntegratedWeight += continously?1.0:0.2;
			config.print();
			cmd = true;
			break;


		case 'a':
			config.angularSpeedWeight -= continously?2.0:0.5;
			config.print();
			cmd =true;
			break;
		case 'A':
			config.angularSpeedWeight += continously?2.0:0.5;
			config.print();
			cmd = true;
			break;
		case 'w':
			config.ballPositionWeight -= continously?0.05:0.01;
			config.print();
			cmd =true;
			break;
		case 'W':
			config.ballPositionWeight += continously?0.05:0.01;
			config.print();
			cmd = true;
			break;
		case 'y':
			config.ballVelocityWeight-= continously?0.05:0.01;
			config.print();
			cmd =true;
			break;
		case 'Y':
			config.ballVelocityWeight += continously?0.05:0.01;
			config.print();
			cmd = true;
			break;
		case 'r':
			config.ballAccelWeight-= continously?0.05:0.01;
			config.print();
			cmd =true;
			break;
		case 'R':
			config.ballAccelWeight += continously?0.05:0.01;
			config.print();
			cmd = true;
			break;
		case 'f':
			config.bodyPositionWeight +=continously?0.05:0.01;
			config.print();
			cmd = true;
			break;
		case 'F':
			config.bodyPositionWeight-= continously?0.05:0.01;
			config.print();
			cmd =true;
			break;
		case 't':
			config.bodyVelocityWeight += continously?0.05:0.01;
			config.print();

			cmd = true;
			break;
		case 'T':
			config.bodyVelocityWeight -= continously?0.05:0.01;
			config.print();
			cmd =true;
			break;
		case 'g':
			config.bodyAccelWeight += continously?0.05:0.01;
			config.print();
			cmd = true;
			break;
		case 'G':
			config.bodyAccelWeight -= continously?0.05:0.01;
			config.print();
			cmd = true;
			break;

		default:
			cmd = false;
			break;
		}
		if (cmd) {
			command->print(">");
		}
}

