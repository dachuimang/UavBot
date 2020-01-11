/**
 * @file Controller.cpp
 * @author Dan Oates (WPI Class of 2020)
 */
#include "Controller.h"
#include <Imu.h>
#include <Bluetooth.h>
#include <Props.h>
#include <CppUtil.h>
#include <PID.h>
using Props::f_prop_min;
using Props::f_prop_max;
using CppUtil::clamp;
using CppUtil::sqa;

/**
 * Namespace Definitions
 */
namespace Controller
{
	// Physical Constants
	const float inr_xx = 1.15e-03f;	// Inertia x-axis [kg*m^2]
	const float inr_yy = 1.32e-03f;	// Inertia y-axis [kg*m^2]
	const float inr_zz = 2.24e-03f;	// Inertia z-axis [kg*m^2]
	const float mass = 0.546f;		// Total mass [kg]
	const float gravity = 9.807f;	// Gravity [m/s^2]

	// Control Constants
	const float f_ctrl = 50.0f;		// Control freq [Hz]
	const float f_rat_min = 0.10;	// Min prop thrust ratio
	const float f_rat_max = 0.90;	// Max prop thrust ratio
	const float pole_az = -8.0f;	// Accel z-axis pole [s^-1]

	// Quat X-axis Control
	const float pole_qx = -5.0f;		// Triple pole [s^-1]
	const float qx_kp_adj = +0.000f;	// P-gain adj [N*m/rad]
	const float qx_ki_adj = +0.000f;	// I-gain adj [N*m/(rad*s)]
	const float qx_kd_adj = +0.000f;	// D-gain adj [N*m/(rad/s)]

	// Quat Y-axis Control
	const float pole_qy = -5.0f;		// Triple pole [s^-1]
	const float qy_kp_adj = +0.000f;	// P-gain adj [N*m/rad]
	const float qy_ki_adj = +0.000f;	// I-gain adj [N*m/(rad*s)]
	const float qy_kd_adj = +0.000f;	// D-gain adj [N*m/(rad/s)]

	// Quat Z-axis Control
	const float pole_qz = -3.0f;		// Triple pole [s^-1]
	const float qz_kp_adj = +0.000f;	// P-gain adj [N*m/rad]
	const float qz_ki_adj = +0.000f;	// I-gain adj [N*m/(rad*s)]
	const float qz_kd_adj = +0.000f;	// D-gain adj [N*m/(rad/s)]

	// Derived Constants
	const float t_ctrl_s = 1.0f / f_ctrl;
	const float t_ctrl_us = 1e6f * t_ctrl_s;
	const float acc_max = (4.0f * f_prop_max / mass);
	const float acc_mag_min = acc_max * f_rat_min;
	const float acc_mag_max = acc_max * f_rat_max;
	const float acc_mag_max_sq = sqa(acc_mag_max);
	const float f_lin_min = 4.0f * f_prop_max * f_rat_min;
	const float f_lin_max = 4.0f * f_prop_max * f_rat_max;

	// Quat X-axis PID Controller
	const float qx_kp = +6.0f * inr_xx * powf(pole_qx, 2.0f) + qx_kp_adj;
	const float qx_ki = -2.0f * inr_xx * powf(pole_qx, 3.0f) + qx_ki_adj;
	const float qx_kd = -6.0f * inr_xx * powf(pole_qx, 1.0f) + qx_kd_adj;
	PID quat_x_pid(qx_kp, qx_ki, qx_kd, -HUGE_VALF, +HUGE_VALF, f_ctrl);

	// Quat Y-axis PID Controller
	const float qy_kp = +6.0f * inr_yy * powf(pole_qy, 2.0f) + qy_kp_adj;
	const float qy_ki = -2.0f * inr_yy * powf(pole_qy, 3.0f) + qy_ki_adj;
	const float qy_kd = -6.0f * inr_yy * powf(pole_qy, 1.0f) + qy_kd_adj;
	PID quat_y_pid(qy_kp, qy_ki, qy_kd, -HUGE_VALF, +HUGE_VALF, f_ctrl);

	// Quat Z-axis PID Controller
	const float qz_kp = +6.0f * inr_zz * powf(pole_qz, 2.0f) + qz_kp_adj;
	const float qz_ki = -2.0f * inr_zz * powf(pole_qz, 3.0f) + qz_ki_adj;
	const float qz_kd = -6.0f * inr_zz * powf(pole_qz, 1.0f) + qz_kd_adj;
	PID quat_z_pid(qz_kp, qz_ki, qz_kd, -HUGE_VALF, +HUGE_VALF, f_ctrl);

	// Accel Z-axis PID Controller
	const float az_kp = 0.0f;
	const float az_ki = -mass * pole_az;
	const float az_kd = 0.0f;
	PID acc_z_pid(az_kp, az_ki, az_kd, f_lin_min, f_lin_max, f_ctrl);

	// Vectors and matrices
	Vector<3> x_hat;
	Vector<3> y_hat;
	Vector<3> z_hat;
	Matrix<4, 3> D_bar_ang;
	Matrix<4, 1> D_bar_lin;
	Vector<4> f_props;

	// Quaternion PID saturation flag
	bool quat_sat = false;

	// Flags
	bool init_complete = false;
}

/**
 * @brief Inits controller
 */
void Controller::init()
{
	if (!init_complete)
	{
		// Initialize x-axis unit vector
		x_hat(0) = +1.000000e+00f;
		x_hat(1) = +0.000000e+00f;
		x_hat(2) = +0.000000e+00f;

		// Initialize y-axis unit vector
		y_hat(0) = +0.000000e+00f;
		y_hat(1) = +1.000000e+00f;
		y_hat(2) = +0.000000e+00f;

		// Initialize z-axis unit vector
		z_hat(0) = +0.000000e+00f;
		z_hat(1) = +0.000000e+00f;
		z_hat(2) = +1.000000e+00f;

		// Initialize inv angular moment arm
		D_bar_ang(0, 0) = +2.688172e+00f;
		D_bar_ang(0, 1) = -2.688172e+00f;
		D_bar_ang(0, 2) = +4.545455e+00f;
		D_bar_ang(1, 0) = -2.688172e+00f;
		D_bar_ang(1, 1) = -2.688172e+00f;
		D_bar_ang(1, 2) = -4.545455e+00f;
		D_bar_ang(2, 0) = +2.688172e+00f;
		D_bar_ang(2, 1) = +2.688172e+00f;
		D_bar_ang(2, 2) = -4.545455e+00f;
		D_bar_ang(3, 0) = -2.688172e+00f;
		D_bar_ang(3, 1) = +2.688172e+00f;
		D_bar_ang(3, 2) = +4.545455e+00f;

		// Initialize inv linear moment arm
		D_bar_lin(0, 0) = +2.500000e-01f;
		D_bar_lin(1, 0) = +2.500000e-01f;
		D_bar_lin(2, 0) = +2.500000e-01f;
		D_bar_lin(3, 0) = +2.500000e-01f;

		// Set init flag
		init_complete = true;
	}
}

/**
 * @brief Runs one control loop iteration to calculate prop forces
 */
void Controller::update()
{
	// Get state and commands
	Quat ang_pos = Imu::get_ang_pos();
	Vector<3> lin_acc_loc = Imu::get_lin_acc();
	Vector<3> lin_acc_cmd = Bluetooth::get_lin_acc_cmd();
	float ang_z_cmd = Bluetooth::get_ang_z_cmd();

	// Adjust accel for gravity
	lin_acc_cmd(2) += gravity;
	
	// Accel command limiting
	lin_acc_cmd(2) = clamp(lin_acc_cmd(2), acc_mag_min, acc_mag_max);
	float norm_xy = hypot(lin_acc_cmd(0), lin_acc_cmd(1));
	float norm_xy_max = sqrtf(acc_mag_max_sq - sqa(lin_acc_cmd(2)));
	float p = norm_xy_max / norm_xy;
	if (p < 1.0f)
	{
		lin_acc_cmd(0) *= p;
		lin_acc_cmd(1) *= p;
	}

	// Orientation cmd
	Quat q_z(z_hat, ang_z_cmd);
	Quat q_cmd = q_z;
	float norm_acc = norm(lin_acc_cmd);
	if (norm_acc > 0)
	{
		Vector<3> acc_hat = lin_acc_cmd / norm_acc;
		float cos_z = cos(ang_z_cmd);
		float sin_z = sin(ang_z_cmd);
		float t_x = asinf(sin_z*acc_hat(0) - cos_z*acc_hat(1));
		float t_y = asinf((cos_z*acc_hat(0) + sin_z*acc_hat(1)) / cosf(t_x));
		Quat q_y(y_hat, t_y);
		Quat q_x(x_hat, t_x);
		q_cmd = q_z * q_y * q_x;
	}

	// Quaternion control
	Quat ang_err = inv(q_cmd) * ang_pos;
	if (ang_err.w < 0.0f) ang_err = -ang_err;
	Vector<3> tau_cmd;
	tau_cmd(0) = quat_x_pid.update(-ang_err.x, 0.0f, quat_sat);
	tau_cmd(1) = quat_y_pid.update(-ang_err.y, 0.0f, quat_sat);
	tau_cmd(2) = quat_z_pid.update(-ang_err.z, 0.0f, quat_sat);
	Vector<4> f_ang = D_bar_ang * tau_cmd;

	// Acceleration z-axis cmd
	lin_acc_cmd(2) -= gravity;
	Vector<3> acc_cmd_loc = inv(ang_pos) * lin_acc_cmd;
	float acc_z_cmd = acc_cmd_loc(2);

	// Acceleration z-axis control
	float f_lin_sca = acc_z_pid.update(acc_z_cmd - lin_acc_loc(2));
	Vector<4> f_lin = D_bar_lin * Vector<1>(f_lin_sca);

	// Force regulator controller
	float p_min = 1.0f;
	quat_sat = false;
	for (uint8_t i = 0; i < 4; i++)
	{
		float p =
			(f_ang(i) > 0.0f) ? ((f_prop_max - f_lin(i)) / f_ang(i)) :
			(f_ang(i) < 0.0f) ? ((f_prop_min - f_lin(i)) / f_ang(i)) :
			1.0f;
		if ((0.0f < p) && (p < p_min))
		{
			p_min = p;
			quat_sat = true;
		}
	}
	f_props = p_min * f_ang + f_lin;
}

/**
 * @brief Resets controller to startup state
 */
void Controller::reset()
{
	quat_x_pid.reset();
	quat_y_pid.reset();
	quat_z_pid.reset();
	acc_z_pid.reset();
	quat_sat = false;
	f_props = Vector<4>();
}

/**
 * @brief Returns prop forces [N]
 */
const Vector<4>& Controller::get_f_props()
{
	return f_props;
}
