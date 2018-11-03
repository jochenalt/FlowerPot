/*
 * StateController.h
 *
 *  Created on: 23.08.2018
 *      Author: JochenAlt
 */

#ifndef STATECONTROLLER_H_
#define STATECONTROLLER_H_

#include <Filter/FIRFilter.h>
#include <types.h>
#include <setup.h>
#include <IMU.h>

class ControlPlane {
	public:
		void init ();
		float targetAngle;			// expected angle out of acceleration
		float bodyVelocity;			// absolute velocity of body
		float targetBallPos;		// absolute to-be position of the bot
		float lastTargetAngle;
		float lastTargetBodyPos;
		float lastTargetBallPos;
		float lastAbsBallPos;
		float lastAbsBodyPos;		// absolute as-is position of last loop
		float lastBodySpeed;
		float lastBallSpeed;
		float lastTargetSpeed;
		float absBodyAccel;
		float absBallAccel;
		float errorAngle;
		float errorAngularVelocity;
		float errorBallPosition;
		float errorBodyPosition;
		float errorBallVelocity;
		float errorBodyVelocity;
		float errorBodyAccel;
		float errorBallAccel;

		float speed;			// speed in x direction [mm/s]
		float error;			// current error of control loop used to compute the acceleration
		float accel;			// final acceleration out of the control loop
		float filteredSpeed;

		FIR::Filter outputSpeedFilter;
		FIR::Filter inputBallAccel;
		FIR::Filter inputBodyAccel;

		// compute new speed in the given pane, i.e. returns the error correction that keeps the bot balanced and on track
		void update(float dT,
						float pActualSpeed, float pToBeSpeed, float targetAccel,
						float pActualOmega, float pToBeOmega,
						float pTilt, float pAngularSpeed);
		void print();
};


class StateController : public Menuable {
public:
	StateController() {};
	virtual ~StateController() {};

	void setup(MenuController* menuCtrl);
	void loop();

	virtual void printHelp();
	virtual void menuLoop(char ch, bool continously);

	void update( float dT, const BotMovement& currentMovement,
			 	 const IMUSample& sensorSample,
				 const BotMovement& targetMovement);

	float getSpeedX();
	float getSpeedY();
	float getOmega();

private:
	ControlPlane planeX;
	ControlPlane planeY;

	BotMovement rampedTargetMovement;
};

#endif /* STATECONTROLLER_H_ */
