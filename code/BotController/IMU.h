/*
 * IMUController.h
 *
 *  Created on: 21.08.2018
 *      Author: JochenAlt
 */

#ifndef IMU_IMUCONTROLLER_H_
#define IMU_IMUCONTROLLER_H_

#include <libraries/MenuController.h>
#include <MPU9250/MPU9250.h>
#include <Filter/KalmanFilter.h>
#include <Kinematics.h>
#include <TimePassedBy.h>

class IMUConfig {
	public:
		void initDefaultValues();

		void print();

	float nullOffsetX;
	float nullOffsetY;
	float kalmanNoiseVariance;
};



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

	IMUSamplePlane plane[3];
};


class IMU : public Menuable {
public:

	virtual ~IMU() {};
	IMU() {};

	IMU& getInstance() {
		static IMU instance;
		return instance;
	}

	void setup(MenuController* menuCtrl);

	void setNoiseVariance(float noiseVariance);

	void loop();

	bool isValid();
	// stateful method to indicate that a new value from IMU is available. returns true only once per new value
	// This is the main timer determining the sample frequency
	bool isNewValueAvailable(float &dT /* time since last call in [s] */);

	IMUSample& getSample() { return currentSample; };


	// call when stable and upright before starting up
	void calibrate();
	virtual void printHelp();
	virtual void menuLoop(char ch, bool continously);


private:
	int init();

	float getAngleRad(Dimension dim);
	float getAngularVelocity(Dimension dim);
	void updateFilter();
	MPU9250* mpu9250 = NULL;
	KalmanFilter kalman[3]; // one kalman filter per dimension
	float noiseVariance = 0.1; // noise variance used in Kalman filter. The bigger, the more noise, default is 0.03;

	IMUSample currentSample;
	IMUSample lastSample;
	bool valueIsUpdated = false;
	bool logIMUValues = false;
	uint32_t lastInvocationTime_us = 0;
	float sampleRate_us = 0;

	float dT = 0;
	TimePassedBy logTimer;
};

#endif /* IMU_IMUCONTROLLER_H_ */
