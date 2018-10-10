/*
 * IMUController.h
 *
 *  Created on: 21.08.2018
 *      Author: JochenAlt
 */

#ifndef IMU_IMUCONTROLLER_H_
#define IMU_IMUCONTROLLER_H_

#include <MenuController.h>
#include <MPU9250/src/MPU9250.h>
#include <Filter/KalmanFilter.h>
#include <Kinematics.h>


class IMUSamplePlane {
public:
	IMUSamplePlane();
	IMUSamplePlane(float angle, float angularVelocity);
	IMUSamplePlane(const IMUSamplePlane& t);
	IMUSamplePlane& operator=(const IMUSamplePlane& t);

	float angle = 0;   			// [rad]
	float angularVelocity = 0;	// [rad/s]
};

class IMUSample{
public:
	IMUSample();
	IMUSample(const IMUSamplePlane& x, const IMUSamplePlane& y, const IMUSamplePlane& z);
	IMUSample(const IMUSample& t);
	IMUSample& operator=(const IMUSample& t);

	IMUSamplePlane x;
	IMUSamplePlane y;
	IMUSamplePlane z;
};


class IMU : public Menuable {
public:
	enum Dimension { X=0,Y=1,Z=2 };

	virtual ~IMU() {};
	IMU() {};

	IMU& getInstance() {
		static IMU instance;
		return instance;
	}

	void setup(MenuController* menuCtrl);
	void setup();
	void loop();

	// stateful method to indicate that a new value from IMU is available. returns true only once per new value
	// This is the main timer determining the sample frequency
	bool isNewValueAvailable(float &dT /* time since last call in [s] */);

	IMUSample getSample() { return currentSample; };


	// call when stable and upright before starting up
	void calibrate();
	virtual void printHelp();
	virtual void menuLoop(char ch);

private:

	float getAngleRad(Dimension dim);
	float getAngularVelocity(Dimension dim);
	void updateFilter();
	MPU9250* mpu9250 = NULL;
	KalmanFilter kalman[3]; // one kalman filter per dimension

	IMUSample currentSample;
	bool valueIsUpdated = false;
	bool logIMUValues = false;
	uint32_t lastInvocationTime_ms = 0;
	uint32_t averageTime_us = 0;
	float dT = 0;

	matrix33_t nullMatrix; // rotation matrix that turns the IMU's orientation into the absolut null position
};

#endif /* IMU_IMUCONTROLLER_H_ */
