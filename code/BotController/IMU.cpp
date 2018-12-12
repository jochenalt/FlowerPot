/*
 * IMUController.cpp
 *
 *  Created on: 21.08.2018
 *      Author: JochenAlt
 */

#include <Arduino.h>
#include <MPU9250/MPU9250.h>
#include <TimePassedBy.h>
#include <Filter/KalmanFilter.h>
#include <IMU.h>
#include <setup.h>
#include <libraries/Util.h>
#include <types.h>
#include <BotMemory.h>
#include <libraries/I2CPortScanner.h>

// instantiated in main.cpp
extern i2c_t3* IMUWire;

// if the interrupt has been missed, use this emergency timer
// to ask the IMU anyhow.
TimePassedBy updateTimer(SamplingTime*1000.0 /* [ms] */); // twice the usual sampling frquency


IMUConfig& imuConfig = memory.persistentMem.imuControllerConfig;


void IMUConfig::initDefaultValues() {
	// these null values can be calibrated and set in EEPROM
	nullOffsetX = radians(1.41);
	nullOffsetY = radians(3.20);
	kalmanNoiseVariance = 0.1; // noise variance, default is 0.03, the higher the more noise is filtered
}

void IMUConfig::print() {
	loggingln("imu configuration");
	logging("   null=(");
	logging(degrees(nullOffsetX),3,2);
	logging(",");
	logging(degrees(nullOffsetY),3,2);
	loggingln("))");
	logging("   kalman noise variance=");
	loggingln(kalmanNoiseVariance,1,3);

}


IMUSamplePlane::IMUSamplePlane() {
	angle = 0;
	angularVelocity = 0;
}

IMUSamplePlane::IMUSamplePlane(float angle, float angularVelocity) {
	this->angle = angle;
	this->angularVelocity = angularVelocity;
}

IMUSamplePlane::IMUSamplePlane(const IMUSamplePlane& t) {
	this->angle = t.angle;
	this->angularVelocity = t.angularVelocity;
}

IMUSamplePlane& IMUSamplePlane::operator=(const IMUSamplePlane& t) {
	this->angle = t.angle;
	this->angularVelocity = t.angularVelocity;
	return * this;
}

IMUSample::IMUSample() {};

IMUSample::IMUSample(const IMUSamplePlane& x, const IMUSamplePlane& y, const IMUSamplePlane& z) {
	plane[Dimension::X] = x;
	plane[Dimension::Y] = y;
	plane[Dimension::Z] = z;
}

IMUSample::IMUSample(const IMUSample& t) {
	this->plane[0] = t.plane[0];
	this->plane[1] = t.plane[1];
	this->plane[2] = t.plane[2];
}

IMUSample& IMUSample::operator=(const IMUSample& t) {
	this->plane[0] = t.plane[0];
	this->plane[1] = t.plane[1];
	this->plane[2] = t.plane[2];

	return *this;
}


bool IMU::isValid() {
	if (!(millis() - updateTimer.mLastCall_ms < 2000/SampleFrequency))
		logging("IMU frequency too low");
	if (!(abs(currentSample.plane[X].angle) < MaxTiltAngle))
		logging("X tilt ange too high");
	if (!(abs(currentSample.plane[Y].angle) < MaxTiltAngle))
		logging("Y tilt ange too high");
	if (!(abs(currentSample.plane[X].angularVelocity) < MaxTiltAngle/SamplingTime))
		logging("X angular velocity ange too high");
	if (!(abs(currentSample.plane[Y].angularVelocity) < MaxTiltAngle/SamplingTime))
		logging("Y X angular velocity too high");

	bool result = ((millis() - updateTimer.mLastCall_ms < 2000/SampleFrequency) &&
			(abs(currentSample.plane[X].angle) < MaxTiltAngle) &&
			(abs(currentSample.plane[Y].angle) < MaxTiltAngle) &&
			(abs(currentSample.plane[X].angularVelocity) < MaxTiltAngle/SamplingTime) &&
			(abs(currentSample.plane[Y].angularVelocity) < MaxTiltAngle/SamplingTime));
	if (!result) {
		logging("t=");
		logging(millis() - updateTimer.mLastCall_ms);
		logging("a=");
		logging(currentSample.plane[X].angle,3,1);
		logging(",");
		logging(currentSample.plane[Y].angle,3,1);
		logging("w=");
		logging(currentSample.plane[X].angularVelocity,3,1);
		logging(",");
		logging(currentSample.plane[Y].angularVelocity,3,1);
		loggingln();
	}
	return result;
}

void IMU::setup(MenuController *newMenuCtrl) {
	registerMenuController(newMenuCtrl);

	// in case of repeated calls of setup, delete old memory
	if (mpu9250 != NULL) {
		delete mpu9250;
	}

	// initialize high speed I2C to IMU
	IMUWire = &Wire;
	IMUWire->begin(I2C_MASTER, 0, I2C_PINS_18_19, I2C_PULLUP_INT, I2C_RATE_800);
	IMUWire->setDefaultTimeout(4000); // 4ms default timeout

	// doI2CPortScan(F("I2C"),IMUWire , logger);
	mpu9250 = new MPU9250FIFO(IMUWire,IMU_I2C_ADDRESS,I2C_RATE_800);

	int status = mpu9250->begin();
	if (status < 0)
		fatalError("I2C-IMU setup failed ");

	status = init();
	if (status < 0)
		fatalError("I2C-IMU init failed ");

	// clean up if failed
	if (status < 0) {
		if (mpu9250 != NULL)
			delete mpu9250;
		mpu9250 = NULL;
	}
}

int IMU::init() {
	// enable FIFO mode
	int status = mpu9250->enableFifo(true,true,false,false);

	// setting the accelerometer full scale range to +/-2G
	status =mpu9250->setAccelRange(MPU9250::ACCEL_RANGE_2G);

	// setting the gyroscope full scale range to +/-500 deg/s
	status = mpu9250->setGyroRange(MPU9250::GYRO_RANGE_250DPS);

	// setting low pass bandwith
	// status = mpu9250->setDlpfBandwidth(MPU9250::DLPF_BANDWIDTH_92HZ); // kalman filter does the rest

	// set update rate of IMU to 200 Hz
	// the interrupt indicating new data will fire with that frequency
	status = mpu9250->setSrd(1000/SampleFrequency-1); // datasheet: Data Output Rate = 1000 / (1 + SRD)*


	mpu9250->setGyroBiasX_rads(0);
	mpu9250->setGyroBiasY_rads(0);
	mpu9250->setGyroBiasZ_rads(0);

	mpu9250->setAccelCalX(0,1.0);
	mpu9250->setAccelCalY(0,1.0);
	mpu9250->setAccelCalZ(0,1.0);

	mpu9250->setMagCalX(0.0, 1.0);
	mpu9250->setMagCalY(0.0, 1.0);
	mpu9250->setMagCalZ(0.0, 1.0);

	// enable aforementioned interrupt
	// attachInterrupt(IMU_INTERRUPT_PIN, imuInterrupt, RISING);
	// mpu9250->enableDataReadyInterrupt();

	// initialize Kalman filter
	kalman[Dimension::X].setup(0);
	kalman[Dimension::Y].setup(0);
	kalman[Dimension::Z].setup(0);

	kalman[Dimension::X].setNoiseVariance(imuConfig.kalmanNoiseVariance);
	kalman[Dimension::Y].setNoiseVariance(imuConfig.kalmanNoiseVariance);
	kalman[Dimension::Z].setNoiseVariance(imuConfig.kalmanNoiseVariance);

	accelFilter[Dimension::X].init(0);
	accelFilter[Dimension::Y].init(0);
	accelFilter[Dimension::Z].init(0);
	gyroFilter[Dimension::X].init(0);
	gyroFilter[Dimension::Y].init(0);
	gyroFilter[Dimension::Z].init(0);

	return status;
}

void IMU::calibrate() {
	loggingln("calibrate imu");
	int status = mpu9250->calibrateAccel();
	if (status != 1) {
		logging("accl calibration status error");
		loggingln(status);
		fatalError("fatal error");
	}

	status = mpu9250->calibrateGyro();
	if (status != 1) {
		logging("accl calibration status error");
		loggingln(status);
		fatalError("fatal error");
	}

	status = init();

	if (status != 1) {
		logging("status error");
		loggingln(status);
		fatalError("fatal error");
	}
	// measure for 1s, run kalman filter and take final orientation as null value
	uint32_t now = millis();
	imuConfig.nullOffsetX = 0;
	imuConfig.nullOffsetY = 0;

	// let kalman filter run and calibrate for 1s
	while (millis() - now < 2000) {
		loop(micros());
	}

	imuConfig.nullOffsetX = kalman[Dimension::X].getAngle();
	imuConfig.nullOffsetY = kalman[Dimension::Y].getAngle();

	imuConfig.print();
}

void IMU::loop(uint32_t now_us) {
	if (mpu9250) {
		if (updateTimer.isDue()) {
			uint32_t sampleTime_us = now_us-lastInvocationTime_us;
			sampleRate_us = (sampleRate_us + sampleTime_us)/2.0;

			// read raw values
			int status = mpu9250->readFifo();
			if (status != 1) {
				fatalError("loop IMU status error ");
				loggingln(status);
			}

			// fetch all IMU samples since the last loop
			// (IMU has 1000 Hz sample frequency, we sample at 250 Hz, so typically we average 4 samples )
			// and compute the averae
			const int MaxSampleSize = 100;
			float samples[MaxSampleSize];
			size_t noOfSamples;
			mpu9250->getFifoAccelX_mss(&noOfSamples,samples);
			if (noOfSamples > MaxSampleSize)
				fatalError("MaxSampleSize too small");
			for (unsigned i = 0;i<noOfSamples;i++)
				accelFilter[Dimension::X].update(samples[i]);
			mpu9250->getFifoAccelY_mss(&noOfSamples,samples);
			for (unsigned i = 0;i<noOfSamples;i++)
				accelFilter[Dimension::Y].update(samples[i]);
			mpu9250->getFifoAccelZ_mss(&noOfSamples,samples);
			for (unsigned i = 0;i<noOfSamples;i++)
				accelFilter[Dimension::Z].update(samples[i]);

			mpu9250->getFifoGyroX_rads(&noOfSamples,samples);
			for (unsigned i = 0;i<noOfSamples;i++)
				gyroFilter[Dimension::X].update(samples[i]);
			mpu9250->getFifoGyroX_rads(&noOfSamples,samples);
			for (unsigned i = 0;i<noOfSamples;i++)
				gyroFilter[Dimension::Y].update(samples[i]);
			mpu9250->getFifoGyroX_rads(&noOfSamples,samples);
			for (unsigned i = 0;i<noOfSamples;i++)
				gyroFilter[Dimension::Z].update(samples[i]);

			// compute dT for kalman filter
			dT = ((float)(sampleTime_us))/1000000.0;
			lastInvocationTime_us = now_us;

			// turn the coordinate system of the IMU into that one of the bot:
			// front wheel points to the x-axis, y-axis is
			// for use of the kalman filter, we need to break the convention and
			// denote the coordsystem for angular velocity in the direction of the according axis
			// I.e. the angular velocity in the x-axis denotes the speed of the tilt angle in direction of x
			float accelX = accelFilter[Dimension::X].get();
			float accelY = accelFilter[Dimension::Y].get();
			float accelZ = accelFilter[Dimension::Z].get();

			float angularVelocity[3];
			angularVelocity[Dimension::X] = gyroFilter[Dimension::Y].get();
			angularVelocity[Dimension::Y] = gyroFilter[Dimension::X].get();
			angularVelocity[Dimension::Z] = gyroFilter[Dimension::Z].get();

			float tilt[3];
			tilt[Dimension::X] = atan2( accelX, sqrt(accelZ*accelZ + accelY*accelY)) - imuConfig.nullOffsetX;
			tilt[Dimension::Y] = atan2(-accelY, sqrt(accelZ*accelZ + accelX*accelX)) - imuConfig.nullOffsetY;
			tilt[Dimension::Z] = accelZ;


			// save previous sample
			lastSample = currentSample;

			for (int i = 0;i<3;i++) {
				// invoke kalman filter separately per plane
				kalman[i].update(tilt[i], angularVelocity[i], dT);
				currentSample.plane[i].angle = kalman[i].getAngle();
				currentSample.plane[i].angularVelocity = kalman[i].getRate();
				// currentSample.plane[i].angularVelocity = (kalman[i].getAngle() - lastSample.plane[i].angle) / dT;
			}

			// indicate that new value is available
			// next call of isNewValueAvailable will return true one time
			valueIsUpdated = true;

			if (logIMUValues) {
				if (logTimer.isDue_ms(50,millis())) {
					logging("dT=");
					logging(dT,1,3);
					logging("a=(X:");
					logging(degrees(tilt[Dimension::X]),2,2);
					logging("/");
					logging(degrees(angularVelocity[Dimension::X]),2,2);
					logging("Y:");
					logging(degrees(tilt[Dimension::Y]),2,2);
					logging("/");
					logging(degrees(angularVelocity[Dimension::Y]),2,2);
					logging("Z:");
					logging(degrees(tilt[Dimension::Z]),2,2);
					logging("/");
					logging(degrees(angularVelocity[Dimension::Z]),2,2);

					logging(" angle=(");
					logging(degrees(getAngleRad(Dimension::X)),2,2);
					logging(",");
					logging(degrees(getAngleRad(Dimension::Y)),2,2);
					logging(")");

					logging(" us=");
					logging(sampleRate_us);
					logging(" f=");
					logging(1000000.0/sampleRate_us);
					loggingln("Hz");

				}
			}
		}
	}
}

// returns true once when a new value is available.
bool IMU::isNewValueAvailable(float &dT) {
	bool tmp = valueIsUpdated;
	valueIsUpdated = false;
	dT = this->dT;
	return tmp;
}

float IMU::getAngleRad(Dimension dim) {
	return currentSample.plane[dim].angle;
}

float IMU::getAngularVelocity(Dimension dim) {
	return currentSample.plane[dim].angularVelocity;
}

void IMU::setNoiseVariance(float noiseVariance) {
	kalman[0].setNoiseVariance(noiseVariance);
	kalman[1].setNoiseVariance(noiseVariance);
	kalman[2].setNoiseVariance(noiseVariance);
}

void IMU::printHelp() {
	loggingln("IMU controller");
	loggingln("r    - read values");
	loggingln("c    - calibrate ");
	loggingln("n/M  - set kalman noise variance");

	loggingln("ESC");
}

void IMU::menuLoop(char ch, bool continously) {
	bool cmd = true;
	switch (ch) {
	case 'r':
		logIMUValues = !logIMUValues;
		break;
	case 'h':
		printHelp();
		break;
	case 'c':
		calibrate();
		break;
	case 'N':
		if (imuConfig.kalmanNoiseVariance < 1.0)
			imuConfig.kalmanNoiseVariance += 0.01;
		logging("kalman noise variance ");
		loggingln(imuConfig.kalmanNoiseVariance ,1,3);
		setNoiseVariance(imuConfig.kalmanNoiseVariance);
		break;
	case 'n':
		if (imuConfig.kalmanNoiseVariance > 0.01)
			imuConfig.kalmanNoiseVariance -= 0.01;
		logging("kalman noise variance ");
		loggingln(imuConfig.kalmanNoiseVariance ,1,3);
		setNoiseVariance(imuConfig.kalmanNoiseVariance);
		break;
	case 27:
		popMenu();
		return;
		break;
	default:
		cmd = false;
		break;
	}

	if (cmd) {
		logging("readvalue=");
		logging(logIMUValues);
		loggingln(" >");
	}
}
