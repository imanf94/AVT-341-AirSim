#include "avt_341/control/pure_pursuit_controller.h"
#include "avt_341/avt_341_utils.h"

namespace avt_341 {
namespace control{

PurePursuitController::PurePursuitController() {
	//default wheelbase and steer angle
	// set to MRZR values
	wheelbase_ = 2.731; // meters
	max_steering_angle_ = 0.69; //39.5 degrees
	max_stable_speed_ = 35.0; //5.0;

	// tunable parameters
	min_lookahead_ = 2.0;
	max_lookahead_ = 25.0;
	k_ = 2.0; //0.5;

	//vehicle state parameters
	veh_x_ = 0.0;
	veh_y_ = 0.0;
	veh_speed_ = 0.0;
}

void PurePursuitController::SetVehicleState(nav_msgs::Odometry state){
// Set the current state of the vehicle, which should be the first pose in the path
	veh_x_ = state.pose.pose.position.x;
	veh_y_ = state.pose.pose.position.y;
	float vx = state.twist.twist.linear.x;
	float vy = state.twist.twist.linear.y;
	veh_speed_ = sqrt(vx*vx + vy*vy);
	veh_heading_ = utils::GetHeadingFromOrientation(state.pose.pose.orientation);
}

avt_341::CarControls PurePursuitController::GetDcFromTraj(nav_msgs::Path traj) {
	//initialize the driving command
	avt_341::CarControls msg;
	//msg.throttle = 0.0;
	//msg.steering = 0.0;
	geometry_msgs::Twist dc;
	dc.linear.x = 0.0;
	dc.angular.z = 0.0;
	dc.linear.y = 0.0;

	//make sure the path contains some points
	int np = traj.poses.size();

	if (np < 2) return msg;

	// extract the path that the vehicle needs to follow
	std::vector<utils::vec2> path;

	//populate the desired path
	path.resize(np);
	for (int i = 0; i < np; i++) {
		path[i] = utils::vec2(traj.poses[i].pose.position.x, traj.poses[i].pose.position.y);
	}

	//calculate the lookahead distance based on current speed
	utils::vec2 currpos(veh_x_, veh_y_);
	float path_length = utils::length(path[np - 1] - currpos);
	float lookahead = k_ * veh_speed_;

	if (lookahead > max_lookahead_)lookahead = max_lookahead_;
	if (lookahead < min_lookahead_)lookahead = min_lookahead_;
	if (lookahead > path_length)lookahead = path_length - 0.01;

	//first find the closest segment on the path , and distance to it
	float closest = 1.0E9f;
	int start_seg = 0;
	for (int i = 0; i < np - 1; i++) {
		float d0 = PointToSegmentDistance(path[i], path[i + 1], currpos);
		if (d0 < closest) {
			closest = d0;
			start_seg = i;
		}
	}

	utils::vec2 goal = path[start_seg];
	float target_speed = desired_speed_;
	if (closest < lookahead) {
		//find point on path at lookahead distance away
		float accum_dist = closest;

		//for (int i=0;i<np-1;i++){
		for (int i = start_seg; i < np - 1; i++) {
			utils::vec2 v = path[i + 1] - path[i];
			float seg_dist = length(v);
			if ((accum_dist + seg_dist) > lookahead) {
				utils::vec2 dir = v / seg_dist;
				float t = lookahead - accum_dist;
				goal = path[i] + dir*t;
				target_speed = desired_speed_; //traj.path[i + 1].speed;
				if (target_speed > max_stable_speed_)target_speed = max_stable_speed_;
				break;
			}
			else {
				accum_dist += seg_dist;
			}
		}
	}

	//find the angle, alpha, between the current orientation and the goal
	utils::vec2 curr_dir(cos(veh_heading_), sin(veh_heading_));
	utils::vec2 to_goal(goal.x - veh_x_,goal.y-veh_y_);
	to_goal = to_goal / utils::length(to_goal);
	float alpha = (float)atan2(to_goal.y, to_goal.x) - (float)atan2(curr_dir.y, curr_dir.x);

	//determine the desired normalized steering angle
	float sangle = (float)atan2(2 * wheelbase_*sin(alpha), lookahead);
	sangle = sangle / max_steering_angle_;
	sangle = std::min(1.0f, sangle);
	sangle = std::max(-1.0f, sangle);
	dc.angular.z = sangle;
	//Use the speed controller to get throttle/braking
	//addjust the target speed so you back off during hard turns
	float adj_speed = target_speed * exp(-0.69*pow(fabs(dc.angular.z), 4.0f));
	speed_controller_.SetSetpoint(adj_speed);
	float throttle = speed_controller_.GetControlVariable(veh_speed_, 0.1f);
	if (throttle < 0.0f) { //braking
		dc.linear.x = 0.0f;
		dc.linear.y = std::max(-1.0f, throttle);
	}
	else {
		dc.linear.y = 0.0f;
		dc.linear.x = std::min(1.0f, throttle);
	}
	msg.throttle = throttle;
	msg.steering = sangle;

	// IMAN ??????????????????
	//std::cout << "steering " << sangle << "/n";
	//std::cout << "throttle " << throttle << "/n";

	return msg;
}

} // namespace control
} // namespace avt_341
