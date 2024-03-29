#include "Common.h"
#include "QuadControl.h"

#include "Utility/SimpleConfig.h"

#include "Utility/StringUtils.h"
#include "Trajectory.h"
#include "BaseController.h"
#include "Math/Mat3x3F.h"


#ifdef __PX4_NUTTX
#include <systemlib/param/param.h>
#endif
#include <iostream>


void QuadControl::Init()
{
	BaseController::Init();

	// variables needed for integral control
	integratedAltitudeError = 0;

#ifndef __PX4_NUTTX
	// Load params from simulator parameter system
	ParamsHandle config = SimpleConfig::GetInstance();

	// Load parameters (default to 0)
	kpPosXY = config->Get(_config + ".kpPosXY", 0);
	kpPosZ = config->Get(_config + ".kpPosZ", 0);
	KiPosZ = config->Get(_config + ".KiPosZ", 0);

	kpVelXY = config->Get(_config + ".kpVelXY", 0);
	kpVelZ = config->Get(_config + ".kpVelZ", 0);

	kpBank = config->Get(_config + ".kpBank", 0);
	kpYaw = config->Get(_config + ".kpYaw", 0);

	kpPQR = config->Get(_config + ".kpPQR", V3F());

	maxDescentRate = config->Get(_config + ".maxDescentRate", 100);
	maxAscentRate = config->Get(_config + ".maxAscentRate", 100);
	maxSpeedXY = config->Get(_config + ".maxSpeedXY", 100);
	maxAccelXY = config->Get(_config + ".maxHorizAccel", 100);

	maxTiltAngle = config->Get(_config + ".maxTiltAngle", 100);

	minMotorThrust = config->Get(_config + ".minMotorThrust", 0);
	maxMotorThrust = config->Get(_config + ".maxMotorThrust", 100);
#else
	// load params from PX4 parameter system
	//TODO
	param_get(param_find("MC_PITCH_P"), &Kp_bank);
	param_get(param_find("MC_YAW_P"), &Kp_yaw);
#endif
}


VehicleCommand QuadControl::GenerateMotorCommands(float collThrustCmd, V3F momentCmd)
{

	


	//Perpendicular distance from x-axis to the motor.  
	float l = L / (sqrtf(2.f));
	//Forces along each axis.
	float pBar = momentCmd.x / l;
	float qBar = momentCmd.y / l;
	float rBar = -momentCmd.z / kappa;

	//Total thrust commanded.
	float cBar = collThrustCmd;

	//Magical equation to find individual motor thrusts. 
	//Motors producing thrust F1 and F4, physically rotate clockwise. 
	//F2 and F3 rotate counter-clockwise. 
	float f1 = (pBar + qBar + rBar + cBar) / 4.f;  // Front left F1
	float f2 = (-pBar + qBar - rBar + cBar) / 4.f; // Front right F2
	float f3 = (pBar - qBar - rBar + cBar) / 4.f;  // Rear left F3
	float f4 = (-pBar - qBar + rBar + cBar) / 4.f; // Rear right F4

	cmd.desiredThrustsN[0] = CONSTRAIN(f1, minMotorThrust, maxMotorThrust);
	cmd.desiredThrustsN[1] = CONSTRAIN(f2, minMotorThrust, maxMotorThrust);
	cmd.desiredThrustsN[2] = CONSTRAIN(f3, minMotorThrust, maxMotorThrust);
	cmd.desiredThrustsN[3] = CONSTRAIN(f4, minMotorThrust, maxMotorThrust);

	return cmd;
}
V3F QuadControl::BodyRateControl(V3F pqrCmd, V3F pqr)
{
	// Calculate a desired 3-axis moment given a desired and current body rate
	// INPUTS: 
	//   pqrCmd: desired body rates [rad/s]
	//   pqr: current or estimated body rates [rad/s]
	// OUTPUT:
	//   return a V3F containing the desired moments for each of the 3 axes

	// HINTS: 
	//  - you can use V3Fs just like scalars: V3F a(1,1,1), b(2,3,4), c; c=a-b;
	//  - you'll need parameters for moments of inertia Ixx, Iyy, Izz
	//  - you'll also need the gain parameter kpPQR (it's a V3F)

	V3F momentCmd;

	

	V3F momentsOfInertia = V3F(Ixx, Iyy, Izz);
	//Capture error between desired and estimated bodyrates.
	V3F pqr_err = pqrCmd - pqr;
	momentCmd = momentsOfInertia * kpPQR * (pqr_err);
	

	return momentCmd;
}

// returns a desired roll and pitch rate 
V3F QuadControl::RollPitchControl(V3F accelCmd, Quaternion<float> attitude, float collThrustCmd)
{
	// Calculate a desired pitch and roll angle rates based on a desired global
	//   lateral acceleration, the current attitude of the quad, and desired
	//   collective thrust command
	// INPUTS: 
	//   accelCmd: desired acceleration in global XY coordinates [m/s2]
	//   attitude: current or estimated attitude of the vehicle
	//   collThrustCmd: desired collective thrust of the quad [N]
	// OUTPUT:
	//   return a V3F containing the desired pitch and roll rates. The Z
	//     element of the V3F should be left at its default value (0)

	// HINTS: 
	//  - we already provide rotation matrix R: to get element R[1,2] (python) use R(1,2) (C++)
	//  - you'll need the roll/pitch gain kpBank
	//  - collThrustCmd is a force in Newtons! You'll likely want to convert it to acceleration first

	V3F pqrCmd;
	Mat3x3F R = attitude.RotationMatrix_IwrtB();



	float r11 = R(0, 0);
	float r12 = R(0, 1);
	float r13 = R(0, 2);
	float r21 = R(1, 0);
	float r22 = R(1, 1);
	float r23 = R(1, 2);
	float r33 = R(2, 2);
	//Matrix helper for solving equation
	float v[9];
	v[0] = r21;
	v[1] = -r11;
	v[2] = 0;
	v[3] = r22;
	v[4] = -r12;
	v[5] = 0;
	v[6] = 0;
	v[7] = 0;
	v[8] = 0;

	Mat3x3F rMulMatrix = Mat3x3F(v);
	float cAccel = collThrustCmd / mass;

	if (collThrustCmd > 0.0)
	{

		V3F bActual = V3F(r13, r23, 0.f);
		//Get commanded XY bank positional values
		float bXCommanded = accelCmd.x / -cAccel;
		float bYCommanded = accelCmd.y / -cAccel;

		V3F bCommanded = V3F(bXCommanded, bYCommanded, 0.f);
		bCommanded.constrain(-maxTiltAngle, maxTiltAngle);
		// Error between commanded and actual XY bank positional values
		V3F bError = bCommanded - bActual;
		//Roll and Pitch in world/global frame. 
		V3F bCommandedDot = kpBank * bError;

		//Roll and Pitch rates of the vehicle in body frame.
		pqrCmd = (1 / r33) * (rMulMatrix * bCommandedDot);
	}
	else
	{
		// "negative thrust command"
		pqrCmd = 0.0;
		collThrustCmd = 0.0;
	}



	return pqrCmd;
}

float QuadControl::AltitudeControl(float posZCmd, float velZCmd, float posZ, float velZ, Quaternion<float> attitude, float accelZCmd, float dt)
{
	// Calculate desired quad thrust based on altitude setpoint, actual altitude,
	//   vertical velocity setpoint, actual vertical velocity, and a vertical 
	//   acceleration feed-forward command
	// INPUTS: 
	//   posZCmd, velZCmd: desired vertical position and velocity in NED [m]
	//   posZ, velZ: current vertical position and velocity in NED [m]
	//   accelZCmd: feed-forward vertical acceleration in NED [m/s2]
	//   dt: the time step of the measurements [seconds]
	// OUTPUT:
	//   return a collective thrust command in [N]

	// HINTS: 
	//  - we already provide rotation matrix R: to get element R[1,2] (python) use R(1,2) (C++)
	//  - you'll need the gain parameters kpPosZ and kpVelZ
	//  - maxAscentRate and maxDescentRate are maximum vertical speeds. Note they're both >=0!
	//  - make sure to return a force, not an acceleration
	//  - remember that for an upright quad in NED, thrust should be HIGHER if the desired Z acceleration is LOWER

	Mat3x3F R = attitude.RotationMatrix_IwrtB();
	float thrust = 0;



	//Proportional Term. Error in commanded vs actual position is captured. 
	float posZError = posZCmd - posZ;
	float proportionalTerm = kpPosZ * posZError;

	//Restrict commanded velocity to the range {-maxDescentRate - maxAscentRate}
	velZCmd = CONSTRAIN(velZCmd, -maxDescentRate, maxAscentRate);
	//Derivative term. Capture error in target and actual velocity.
	float posZDotError = velZCmd - velZ;
	float derivativeTerm = kpVelZ * posZDotError;

	//Integral term. Cummulative positional error over time.
	integratedAltitudeError += posZError * dt;
	float integralTerm = KiPosZ * integratedAltitudeError;

	//Second order derivative of Position Z, akin to acceleration.
	float uBarOne = proportionalTerm + derivativeTerm + integralTerm + accelZCmd;

	float bZActual = R(2, 2);

	float cAccel = (CONST_GRAVITY - uBarOne) / bZActual;
	thrust = (mass * cAccel);
	thrust = CONSTRAIN(thrust, (minMotorThrust) * 4.f, (maxMotorThrust) * 4.f);




	return thrust;
}

// returns a desired acceleration in global frame
V3F QuadControl::LateralPositionControl(V3F posCmd, V3F velCmd, V3F pos, V3F vel, V3F accelCmdFF)
{
	// Calculate a desired horizontal acceleration based on 
	//  desired lateral position/velocity/acceleration and current pose
	// INPUTS: 
	//   posCmd: desired position, in NED [m]
	//   velCmd: desired velocity, in NED [m/s]
	//   pos: current position, NED [m]
	//   vel: current velocity, NED [m/s]
	//   accelCmdFF: feed-forward acceleration, NED [m/s2]
	// OUTPUT:
	//   return a V3F with desired horizontal accelerations. 
	//     the Z component should be 0
	// HINTS: 
	//  - use the gain parameters kpPosXY and kpVelXY
	//  - make sure you limit the maximum horizontal velocity and acceleration
	//    to maxSpeedXY and maxAccelXY

	// make sure we don't have any incoming z-component
	accelCmdFF.z = 0;
	velCmd.z = 0;
	posCmd.z = pos.z;

	// we initialize the returned desired acceleration to the feed-forward value.
	// Make sure to _add_, not simply replace, the result of your controller
	// to this variable
	V3F accelCmd = accelCmdFF;


	//Proportional term. Error in target vs desired XY positions.
	V3F posError = posCmd - pos;
	V3F proportionalTerm = posError.operator*(kpPosXY);

	//Constrain lateral velocity to the given range.
	velCmd.constrain(-maxSpeedXY, maxSpeedXY);

	//Derivative term. Target vs desired lateral velocity.
	V3F velDotError = velCmd - vel;
	V3F derivativeTerm = kpVelXY * velDotError;

	// Adds feedforward acceleration.
	accelCmd += proportionalTerm + derivativeTerm;
	accelCmd.constrain(-maxAccelXY, maxAccelXY);



	return accelCmd;
}

// returns desired yaw rate
float QuadControl::YawControl(float yawCmd, float yaw)
{
	// Calculate a desired yaw rate to control yaw to yawCmd
	// INPUTS: 
	//   yawCmd: commanded yaw [rad]
	//   yaw: current yaw [rad]
	// OUTPUT:
	//   return a desired yaw rate [rad/s]
	// HINTS: 
	//  - use fmodf(foo,b) to unwrap a radian angle measure float foo to range [0,b]. 
	//  - use the yaw control gain parameter kpYaw

	float yawRateCmd = 0;
	// Ensure the target is within range of 0 to 2 * pi
	yawCmd = fmodf(yawCmd, 2. * F_PI);

	//Proportional term. 
	float posError = yawCmd - yaw;
	if (posError > F_PI) {
		posError = posError - 2.0 * F_PI;
	}
	else if (posError < -F_PI) {
		posError = posError + 2.0 * F_PI;
	}
	float proportionalTerm = kpYaw * posError;
	yawRateCmd = proportionalTerm;

	return yawRateCmd;

}

VehicleCommand QuadControl::RunControl(float dt, float simTime)
{
	curTrajPoint = GetNextTrajectoryPoint(simTime);

	float collThrustCmd = AltitudeControl(curTrajPoint.position.z, curTrajPoint.velocity.z, estPos.z, estVel.z, estAtt, curTrajPoint.accel.z, dt);

	// reserve some thrust margin for angle control
	float thrustMargin = .1f * (maxMotorThrust - minMotorThrust);
	collThrustCmd = CONSTRAIN(collThrustCmd, (minMotorThrust + thrustMargin) * 4.f, (maxMotorThrust - thrustMargin) * 4.f);

	V3F desAcc = LateralPositionControl(curTrajPoint.position, curTrajPoint.velocity, estPos, estVel, curTrajPoint.accel);

	V3F desOmega = RollPitchControl(desAcc, estAtt, collThrustCmd);
	desOmega.z = YawControl(curTrajPoint.attitude.Yaw(), estAtt.Yaw());

	V3F desMoment = BodyRateControl(desOmega, estOmega);

	return GenerateMotorCommands(collThrustCmd, desMoment);
}
