/*
 * Kinematics.cpp
 *
 * Created: 30.11.2012 16:23:25
 * Author: JochenAlt
 */ 


#include "Arduino.h"
#include "Kinematics.h"
#include "libraries/Util.h"
#include "setup.h"


void logMatrix(float m[3][3]) {
	logger->print("| ");logger->print(m[0][0],4);logger->print(" ");
	logger->print(m[0][1],4);logger->print(" ");
	logger->print(m[0][2],4);logger->println("|");
	logger->print("| ");logger->print(m[1][0],4);logger->print(" ");
	logger->print(m[1][1],4);logger->print(" ");
	logger->print(m[1][2],4);logger->println("|");
	logger->print("| ");logger->print(m[2][0],4);logger->print(" ");
	logger->print(m[2][1],4);logger->print(" ");
	logger->print(m[2][2],4);logger->println("|");
}

// compute construction matrix Kinematix::cm and compute its inverse matrix Kinematix::icm
void Kinematix::setupConstructionMatrix() {
		float a = -1.0/WheelRadius;
		float cos_phi = cos(WheelAngleRad);
		float sin_phi = sin(WheelAngleRad);
		
		// define the construction matrix
		ASSIGN(cm[0],                      0,  a*cos_phi,         -a*sin_phi);
		ASSIGN(cm[1], -a*sqrt(3.)/2.*cos_phi, -a*cos_phi/2.,      -a*sin_phi);
		ASSIGN(cm[2],  a*sqrt(3.)/2.*cos_phi, -a*cos_phi/2.,      -a*sin_phi);

		// compute inverse of construction matrix
		float det_denominator = 
					     ((cm[0][0]) * cm[1][1] * cm[2][2]) +
 			             ((cm[0][1]) * cm[1][2] * cm[2][0]) +
			             ((cm[0][2]) * cm[1][0] * cm[2][1]) -
			             ((cm[2][0]) * cm[1][1] * cm[0][2]) -
			             ((cm[2][1]) * cm[1][2] * cm[0][0]) -
			             ((cm[2][2]) * cm[1][0] * cm[0][1]);

		float detRezi = 1.0 / det_denominator;
		ASSIGN(icm[0],
			detRezi*(((cm[1][1]) * cm[2][2] - (cm[1][2]) * cm[2][1])),
			detRezi*(((cm[0][2]) * cm[2][1] - (cm[0][1]) * cm[2][2])),
			detRezi*(((cm[0][1]) * cm[1][2] - (cm[0][2]) * cm[1][1])));
		ASSIGN(icm[1],
			detRezi*(((cm[1][2]) * cm[2][0] - (cm[1][0]) * cm[2][2])),
			detRezi*(((cm[0][0]) * cm[2][2] - (cm[0][2]) * cm[2][0])),
			detRezi*(((cm[0][2]) * cm[1][0] - (cm[0][0]) * cm[1][2])));
		ASSIGN(icm[2],
			detRezi*(((cm[1][0]) * cm[2][1] - (cm[1][1]) * cm[2][0])),
			detRezi*(((cm[0][1]) * cm[2][0] - (cm[0][0]) * cm[2][1])),
			detRezi*(((cm[0][0]) * cm[1][1] - (cm[0][1]) * cm[1][0])));
}


// tiltRotationMatrix (TRM) is the rotation matrix that is able to compensate the position where
// the ball touches the ground. This points moves if the robot tilts, so when doing forward and inverse kinematics
// this angle needs to be taken into account when the wheel speed is computed out of x,y, omega
void Kinematix::computeTiltRotationMatrix(float pTiltX, float pTiltY) {
	
	// do it only if tilt angles have changed
	// This is actually important, since forward
	// and inverse kinematics is done once per loop with identical angles,
	// so this doubles performance
	if  (tiltCompensationMatrixComputed &&
		(lastTiltX == pTiltX) &&
		(lastTiltY == pTiltY))
		return;
	
	tiltCompensationMatrixComputed = true;
	lastTiltX = pTiltX;
	lastTiltY = pTiltY;
	
	// pre-compute sin and cos
	float sinX = sin(pTiltY);
	float cosX = cos(pTiltY);
	float sinY = sin(pTiltX);
	float cosY = cos(pTiltX);

	// compute Tilt Rotation Matrix (TRM), which is a standard 2d rotation matrix around Y and X
	ASSIGN(trm[0],       cosY,     0,      sinY);
	ASSIGN(trm[1],  sinX*sinY,  cosX,-sinX*cosY);
	ASSIGN(trm[2], -cosX*sinY,  sinX, cosX*cosY);
}

// compute speed of all motors depending from the speed in the IMU's coordinate system in (Vx, Vy, OmegaZ) 
// corrected by the tilt of the imu pTiltX, pTiltY 
void Kinematix::computeWheelSpeed( float pVx /* mm */, float pVy /* mm */, float pOmegaZ /* rev/s */,
		float pTiltX, float pTiltY,
		float wheelSpeed[3]) {

	pVx = -pVx;
	pVy = -pVy;

	// this matrix depends on the tilt angle and corrects the kinematics 
	// due to the slightly moved touch point of the ball
	computeTiltRotationMatrix(pTiltX, pTiltY);

	// rotate construction matrix by tilt (by multiplying with tilt rotation matrix)
	// compute only those fields that are required afterwards (so we need only 10 out of 81 multiplications of a regular matrix multiplication)
	float m01_11=  cm[0][1] * trm[1][1];
	float m01_21 = cm[0][1] * trm[2][1];
	float m10_00 = cm[1][0] * trm[0][0];
	float m10_10 = cm[1][0] * trm[1][0];
	float m10_20 = cm[1][0] * trm[2][0];
	float m11_11 = cm[1][1] * trm[1][1];
	float m11_21 = cm[1][1] * trm[2][1];
	float m02_02 = cm[0][2] * trm[0][2];
	float m02_22 = cm[0][2] * trm[2][2];
	float m02_12 = cm[0][2] * trm[1][2];

	float  lVz = pOmegaZ * BallRadius;

	// compute wheel's speed in rad/s by (wheel0,wheel1,wheel2) = Construction-Matrix * Tilt-Compensation Matrix * (Vx, Vy, Omega)
	wheelSpeed[0] = (( m01_11          + m02_12) * pVx  + (         -m02_02) * pVy  + (          -m01_21 - m02_22) * lVz)  ;
	wheelSpeed[1] = (( m10_10 + m11_11 + m02_12) * pVx  + (-m10_00 - m02_02) * pVy  + ( -m10_20 - m11_21 - m02_22) * lVz) ;
	wheelSpeed[2] = ((-m10_10 + m11_11 + m02_12) * pVx  + ( m10_00 - m02_02) * pVy  + (  m10_20 - m11_21 - m02_22) * lVz) ;

	// outcome is [rad/s], convert to [rev/s]
	wheelSpeed[0] /= TWO_PI;
	wheelSpeed[1] /= TWO_PI;
	wheelSpeed[2] /= TWO_PI;
}

// compute actual speed in the coord-system of the IMU out of the encoder's data depending on the given tilt
void Kinematix::computeActualSpeed( float wheelSpeed[3],
									float tiltX, float tiltY,
									float& vx, float& vy, float& omega) {
	// this matrix depends on the tilt angle and corrects the kinematics 
	// due to the moving touch point of the ball
	computeTiltRotationMatrix(tiltX,tiltY);

	// compute the sparse result of the construction matrix * tilt compensation matrix
	// (multiply only those fields that are required afterwards, so we have only 10 instead of 81 multiplications)
	float m00_01 = trm[0][0] * icm[0][1];
	float m02_20 = trm[0][2] * icm[2][0];
	float m10_01 = trm[1][0] * icm[0][1];
	float m11_10 = trm[1][1] * icm[1][0];
	float m11_11 = trm[1][1] * icm[1][1];
	float m12_20 = trm[1][2] * icm[2][0];
	float m20_01 = trm[2][0] * icm[0][1];
	float m21_10 = trm[2][1] * icm[1][0];
	float m21_11 = trm[2][1] * icm[1][1];
	float m22_20 = trm[2][2] * icm[2][0];

	// compute inverse kinematics
	vx    =         ( m11_10          + m12_20)  * wheelSpeed[0]
			      + ( m10_01 + m11_11 + m12_20)  * wheelSpeed[1]
				  + (-m10_01 + m11_11 + m12_20)  * wheelSpeed[2];
	vy    =         (-m02_20)                    * wheelSpeed[0]
  				  + (-m00_01 - m02_20)           * wheelSpeed[1]
		          + ( m00_01 - m02_20)           * wheelSpeed[2];
	omega =      (  (-m21_10 - m22_20)           * wheelSpeed[0]
				  + (-m20_01 - m21_11 - m22_20)  * wheelSpeed[1]
				  + ( m20_01 - m21_11 - m22_20)  * wheelSpeed[2]) / BallRadius;
}


void Kinematix::setup() {
	setupConstructionMatrix();
}

void Kinematix::testKinematics() {
	
	setupConstructionMatrix();

	logger->println(F("construction matrix"));
	logMatrix(cm);
		
	float lVx,lVy,lOmega;
	lVx = 0;
	lVy = 0;
	lOmega=35.0;
	logger->print(F("Vx="));
	
	logger->print(lVx);
	logger->print(F(" Vy="));
	logger->print(lVy);
	logger->print(F(" Omega="));
	logger->print(lOmega);
	logger->println();

	float lTiltX,lTiltY;
	lTiltX = 0;
	lTiltY = 0;

	logger->print(F("TiltX="));
	logger->print(lTiltX);
	logger->print(F(" TiltY="));
	logger->print(lTiltY);
	logger->println();
	
	float pWheel_speed[3] = {0,0,0};
	 computeWheelSpeed( lVx, lVy, lOmega,
	 					lTiltX, lTiltY,
						pWheel_speed);
	float lWheel1 = pWheel_speed[0];
	float lWheel2 = pWheel_speed[1];
	float lWheel3 = pWheel_speed[2];
	
	logger->print(F("W1="));
	logger->print(lWheel1);
	logger->print(F(" W2="));
	logger->print(lWheel2);
	logger->print(F(" W3="));
	logger->print(lWheel3);
	logger->println();
}	

void Kinematix::testInverseKinematics() {
	
	setupConstructionMatrix();
	float icm00 = icm[0][0];
	float icm01 = icm[0][1];
	float icm02 = icm[0][2];
	float icm10 = icm[1][0];
	float icm11 = icm[1][1];
	float icm12 = icm[1][2];
	float icm20 = icm[2][0];
	float icm21 = icm[2][1];
	float icm22 = icm[2][2];

	logger->println(F("inverse construction matrix"));
	logger->print(icm00);logger->print(" ");
	logger->print(icm01);logger->print(" ");
	logger->print(icm02);logger->println(" ");
	logger->print(icm10);logger->print(" ");
	logger->print(icm11);logger->print(" ");
	logger->print(icm12);logger->println(" ");
	logger->print(icm20);logger->print(" ");
	logger->print(icm21);logger->print(" ");
	logger->print(icm22);logger->println(" ");

	// speed of wheels in �/s
	float lWheel1 = -758.9;
	float lWheel2 = 36.4;
	float lWheel3 = -133.7;
	
	logger->print(F("W1="));
	logger->print(lWheel1);
	logger->print(F(" W2="));
	logger->print(lWheel2);
	logger->print(F(" W3="));
	logger->print(lWheel3);
	logger->println();
						
	float  lTiltX,lTiltY;
	lTiltX = 20.0;
	lTiltY = -15;
	logger->print(F("TiltX="));
	logger->print(lTiltX);
	logger->print(F(" TiltY="));
	logger->print(lTiltY);logger->println();
	
	// this matrix depends on the tilt angle and corrects the 
	computeTiltRotationMatrix(lTiltX,lTiltY);

	float lVx = 0;
	float lVy = 0;
	float lOmega = 0;
	
	float wheel[3] = {0,0,0};
	wheel[0] = lWheel1;
	wheel[1] = lWheel2;
	wheel[2] = lWheel3;
	
	computeActualSpeed( wheel,
				lVx, lVy, lTiltX, lTiltY,lOmega);
				

	logger->print(F("Vx="));
	logger->print(lVx);
	logger->print(F(" Vy="));
	logger->print(lVy);
	logger->print(F(" Omega="));
	logger->print(lOmega);
	logger->println();
}


void Kinematix::testPerformanceKinematics() {
	
	logger->println(F("Kinematics performance"));
	unsigned long start =	millis();
	unsigned long end =	millis();
	logger->print("End ms=");
	logger->println(end-start);
	
	int i = 0;
	logger->println(F("Start"));
	start =	millis();
	for (i = 0;i<1000;i++) {
		float lVx,lVy,lOmega;
		lVx = 300;
		lVy = -100;
		lOmega=35;

		float lTiltX = 20;
		float lTiltY = 15;
		float pWheel_speed[3] = {0,0,0};
		computeWheelSpeed( lVx, lVy, lOmega,
	 					lTiltX, lTiltY,
						pWheel_speed);
						
		computeActualSpeed(pWheel_speed,0,0,
					lVx, lVy, lOmega);
	}
	end = millis();
	logger->println(F("Stop"));

	logger->print((end-start),DEC);
	logger->print("ms for ");
	logger->print(i,DEC);
	logger->print(" loops, ");
	logger->print(float((end-start)) / float(i));
	logger->println("ms");
}

void Kinematix::testTRM() {
	for (int j = 1;j<20;j=j+5) {
		float error  = 0;
		for (float i = 0.0;i<2*PI;i=i+0.1) {
			float x,y;
			x = sin(float(i)) * j;
			y = cos(float(i)) * j;

			computeTiltRotationMatrix(x,y);

			float sin_tilt_x = sin(radians(x));
			float cos_tilt_x = cos(radians(x));
			float sin_tilt_y = sin(radians(y));
			float cos_tilt_y = cos(radians(y));

			error += (abs(sin_tilt_x)==0)?0: abs ((trm[0][0] - sin_tilt_x) / sin_tilt_x);
			error += (sin_tilt_y==0)?0:abs (((trm[0][2]) - sin_tilt_y) / sin_tilt_y);

			error += (sin_tilt_x*sin_tilt_y == 0)?0:abs (((trm[1][0]) - sin_tilt_x*sin_tilt_y) /(sin_tilt_x*sin_tilt_y));
			error += (cos_tilt_x==0)?0:abs ((((trm[1][1]),14) - cos_tilt_x) / cos_tilt_x);
			error += (sin_tilt_x*cos_tilt_y==0)?0:abs ((((trm[1][2]),14) - (-sin_tilt_x*cos_tilt_y)) / (sin_tilt_x*cos_tilt_y));

			error += (cos_tilt_x*sin_tilt_y==0)?0:abs ((((trm[2][0]),14) + cos_tilt_x*sin_tilt_y) / (cos_tilt_x*sin_tilt_y));
			error += (sin_tilt_x==0)?0:abs ((((trm[2][1]),14) - sin_tilt_x) / sin_tilt_x);
			error += (cos_tilt_x*cos_tilt_y==0)?0:abs ((((trm[2][2]),14) - cos_tilt_x*cos_tilt_y) / (cos_tilt_x*cos_tilt_y));

			logger->print(int(error),DEC);
		}	
		error = 0;
	}	
}
