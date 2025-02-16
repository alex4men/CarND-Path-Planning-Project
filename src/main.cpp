#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  // Start in the middle lane
  int lane = 1;

  // A reference velocity to target in mph
  double ref_vel = 0;
  double max_vel = 49.5;
  double gap_behind = 10; // Margin from the car behind for the lane change
  double gap_forward = 30;

  h.onMessage([&ref_vel, &gap_behind, &gap_forward, &max_vel, &map_waypoints_x, &map_waypoints_y, 
               &map_waypoints_s, &map_waypoints_dx, &map_waypoints_dy, &lane]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];


          int prev_size = previous_path_x.size();

          double car_current_s = car_s;
          if (prev_size > 0) {
            car_s = end_path_s;
          }

          bool too_close = false;
          bool left_occupied = false;
          bool right_occupied = false;
          double left_lane_speed = max_vel;
          double center_lane_speed = max_vel;
          double right_lane_speed = max_vel;
          double target_vel = max_vel;

          // Find ref_v to use
          for (int i = 0; i < sensor_fusion.size(); i++) {
            // debug
            // std::cout << "Car: " << i << ", " << sensor_fusion[i][0] << " at " << sensor_fusion[i][6] << ", " << sensor_fusion[i][5] << std::endl;
            float d = sensor_fusion[i][6];
            double vx = sensor_fusion[i][3];
            double vy = sensor_fusion[i][4];
            double check_speed = sqrt(vx * vx + vy * vy);
            double check_car_s = sensor_fusion[i][5];
            double check_car_current_s = check_car_s;

            check_car_s += ((double)prev_size * 0.02 * check_speed); //if using previous points can project s value out in the future
            // Check if the car is in my lane
            if ((2 + 4 * lane - 2) < d && d < (2 + 4 * lane + 2)) {
              // check s values greater than mine and s gap
              if ((check_car_s > car_s) && ((check_car_s - car_s) < gap_forward)) {
                too_close = true;
                target_vel = check_speed*2.24 < target_vel ? check_speed*2.24 : target_vel;
              }
              // Emergency stop if something is blocking our way
              if (0 < check_car_current_s - car_current_s && check_car_current_s - car_current_s < 10) { 
                too_close = true;
                target_vel = 0;
              }

            }
            // Check if left lane is occupied
            if (lane == 0) {
              left_occupied = true;
            } else if ((2 + 4 * (lane - 1) - 2) < d && d < (2 + 4 * (lane - 1) + 2)) {
              // check s values greater than mine and s gap
              if ((check_car_s > (car_s - gap_behind)) && ((check_car_s - car_s) < gap_forward)) {
                left_occupied = true;
              } else if (((check_car_s - car_s) >= gap_forward) && ((check_car_s - car_s) < gap_forward + 10)) {
                left_lane_speed = check_speed*2.24 < left_lane_speed ? check_speed*2.24 : left_lane_speed;
              }

            }
            // Check if right lane is occupied
            if (lane == 2) {
              right_occupied = true;
            } else if ((2 + 4 * (lane + 1) - 2) < d && d < (2 + 4 * (lane + 1) + 2)) {
              // check s values greater than mine and s gap
              if ((check_car_s > (car_s - gap_behind)) && ((check_car_s - car_s) < gap_forward)) {
                right_occupied = true;
              } else if (((check_car_s - car_s) >= gap_forward) && ((check_car_s - car_s) < gap_forward + 10)) {
                right_lane_speed = check_speed*2.24 < right_lane_speed ? check_speed*2.24 : right_lane_speed;
              }
            }
          }
          // Make a dicision what to do next
          if (too_close) {
            if ((!left_occupied) && (!right_occupied)) { // If in the middle lane
              if (left_lane_speed > right_lane_speed) {
                lane -= 1;
                target_vel = left_lane_speed;
              } else {
                lane += 1;
                target_vel = right_lane_speed;
              }
            } else if ((!left_occupied) && (target_vel < left_lane_speed)) { // To the left
              lane -= 1;
              target_vel = left_lane_speed;
            } else if ((!right_occupied) && (target_vel < right_lane_speed)) { // To the right
              lane += 1;
              target_vel = right_lane_speed;
            } // else  - stay in the lane and follow the leading vehicle
          } 
          // debug
          // std::cout << "Left: " << left_occupied << ", " << left_lane_speed 
          //           << ", right: " << right_occupied << ", " << right_lane_speed << std::endl;


          // A list of widely spaced (x, y) waypoints, evenly spaced at 30m
          // Later it will be interpolated with a spline
          vector<double> ptsx;
          vector<double> ptsy;

          // Reference x, y, yaw states
          // Either we will reference the starting point as where the car is 
          // or at the previous path's end point
          double ref_x = car_x;
          double ref_y = car_y;
          double ref_yaw = deg2rad(car_yaw);

          // If previous size is almost empty, use the car as starting reference
          if (prev_size < 2) {
            // Use two points that make the path tangent to the car
            double prev_car_x = car_x - cos(car_yaw);
            double prev_car_y = car_y - sin(car_yaw);
            
            ptsx.push_back(prev_car_x);
            ptsx.push_back(car_x);

            ptsy.push_back(prev_car_y);
            ptsy.push_back(car_y);
          } else { // Use the previous path's end point as starting reference
            // Redefine reference state as previous path end point
            ref_x = previous_path_x[prev_size - 1];
            ref_y = previous_path_y[prev_size - 1];

            double ref_x_prev = previous_path_x[prev_size - 2];
            double ref_y_prev = previous_path_y[prev_size - 2];
            ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);

            // Use two points that make the path tangent to the previous path's end point
            ptsx.push_back(ref_x_prev);
            ptsx.push_back(ref_x);

            ptsy.push_back(ref_y_prev);
            ptsy.push_back(ref_y);
          }

          // In Frenet add evenly 30m spaced points ahead of the starting reference
          vector<double> next_wp0 = getXY(car_s + 30, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
          vector<double> next_wp1 = getXY(car_s + 60, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
          vector<double> next_wp2 = getXY(car_s + 90, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);

          ptsx.push_back(next_wp0[0]);
          ptsx.push_back(next_wp1[0]);
          ptsx.push_back(next_wp2[0]);

          ptsy.push_back(next_wp0[1]);
          ptsy.push_back(next_wp1[1]);
          ptsy.push_back(next_wp2[1]);

          for (int i = 0; i < ptsx.size(); i++) {
            // Shift car reference angle to 0 degrees, that makes math easier
            // Basically transform (shift -> rotation) global coordinates to the car's local coordinates
            // Where the origin is the last point of the previous path (ref_x, ref_y)
            double shift_x = ptsx[i] - ref_x;
            double shift_y = ptsy[i] - ref_y;

            ptsx[i] = (shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw));
            ptsy[i] = (shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw));

          }

          // Create a spline
          tk::spline s;

          // Set (x, y) points to the spline
          s.set_points(ptsx, ptsy);

          // Define the actual (x, y) points we will use for the planner 
          // that the car will visit sequentially every .02 seconds
          vector<double> next_x_vals;
          vector<double> next_y_vals;

          // Start with all of the previous path points from last time
          for (int i = 0; i < previous_path_x.size(); i++) {
            next_x_vals.push_back(previous_path_x[i]);
            next_y_vals.push_back(previous_path_y[i]);
          }

          // Calculate how to break up spline points so that we travel at our desired reference velocity
          double target_x = 30.0;
          double target_y = s(target_x);
          double target_dist = sqrt((target_x) * (target_x) + (target_y) * (target_y));

          double x_add_on = 0;


          // Fill up the rest of our path planner after filling it with previous points,
          // here we will always output 50 points
          for (int i = 1; i <= 50 - previous_path_x.size(); i++) {

            // Keep the target velocity
            if (ref_vel > target_vel + 0.224) {
              ref_vel -= 0.224;
            } else if (ref_vel < target_vel - 0.2) {
              ref_vel += 0.2;
            } else {
              ref_vel = target_vel;
            }
            
            double N = (target_dist / (0.02 * ref_vel / 2.24)); // 2.24 - to convert from mph to m/s

            double x_point = x_add_on + (target_x) / N;
            double y_point = s(x_point);

            x_add_on = x_point;

            double x_ref = x_point;
            double y_ref = y_point;

            // Rotate back to normal after rotating it earlier (rotation -> shift)
            x_point = (x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw));
            y_point = (x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw));

            x_point += ref_x;
            y_point += ref_y;

            next_x_vals.push_back(x_point);
            next_y_vals.push_back(y_point);
          }




          json msgJson;
          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  
  h.run();
}