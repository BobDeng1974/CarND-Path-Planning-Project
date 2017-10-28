#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"

//#include "utility.h"
#include "map.h"
#include "behavior.h"
#include "trajectory.h"
#include "cost.h"
#include "predictions.h"

#include "params.h"

#include <map>

using namespace std;

// for convenience
using json = nlohmann::json;


// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}


int main() {
  uWS::Hub h;

  //////////////////////////////////////////////////////////////////////
  Map map(map_file_);
  map.plot();

  bool start = true;
  double ref_vel = 0.0; // mph
  vector<vector<double>> ref_path_s;
  vector<vector<double>> ref_path_d;
  //////////////////////////////////////////////////////////////////////


  h.onMessage([&map, &ref_vel, &start, &ref_path_s, &ref_path_d](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;


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
          	vector<double> previous_path_x = j[1]["previous_path_x"];
          	vector<double> previous_path_y = j[1]["previous_path_y"];

          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
            vector<vector<double>> sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

            //////////////////////////////////////////////////////////////////////


            map.testError(car_x, car_y, car_yaw);

            int prev_size = previous_path_x.size();
            cout << "prev_size=" << prev_size << " car_x=" << car_x << " car_y=" << car_y << " car_s=" << 
                    car_s << " car_d=" << car_d << " car_speed=" << car_speed << " ref_vel=" << ref_vel << endl;

            prev_size = min(prev_size, param_truncated_prev_size);

            vector<double> frenet = map.getFrenet(car_x, car_y, deg2rad(car_yaw));
            cout << "car_frenet_s=" << frenet[0] << " car_frenet_d=" << frenet[1] << endl;

            car_s = frenet[0];
            car_d = frenet[1];

            if (start)
            {
              struct trajectory_jmt traj_jmt = JMT_init(car_s, car_d);
              ref_path_s = traj_jmt.path_s;
              ref_path_d = traj_jmt.path_d;
              start = false;
            }

            // 6 car predictions x 50 points x 2 coord (x,y): 6 objects predicted over 1 second horizon
            std::map<int, vector<vector<double> > > predictions = generate_predictions(sensor_fusion, car_s, car_d, param_nb_points /* 50 */);

            if (prev_size > 0)
            {
              //car_s = end_path_s;
              //car_d = end_path_d;
              frenet = map.getFrenet(previous_path_x[prev_size-1], previous_path_y[prev_size-1], deg2rad(car_yaw));
              car_s = frenet[0];
              car_d = frenet[1];
            }

            int car_lane = get_lane(car_d);

            vector<vector<double>> targets = behavior_planner_find_targets(sensor_fusion, previous_path_x.size(), car_lane, 
                                                                           car_s, car_d, ref_vel /* car_vel */);

            vector<double> costs;
            vector<vector<vector<double>>> trajectories;
            vector<vector<vector<double>>> paths_s;
            vector<vector<vector<double>>> paths_d;

            int target_lane;
            for (int i = 0; i < targets.size(); i++)
            {
              target_lane = targets[i][0];
              double target_vel = targets[i][1];
              double target_time = 2.0; // TODO should be behavior_planner job

              // vector of (traj_x, traj_y)
              vector<vector<double>> trajectory;
              if (param_trajectory_jmt)
              {
                struct trajectory_jmt traj_jmt;

                // generate JMT trajectory in s and d: converted then to (x,y) for trajectory output
                traj_jmt = generate_trajectory_jmt(target_lane, target_vel, target_time, map, car_x, car_y, car_yaw, 
                                                     car_s, car_d, previous_path_x, previous_path_y, prev_size, ref_path_s, ref_path_d);
                trajectory = traj_jmt.trajectory;
                paths_s.push_back(traj_jmt.path_s);
                paths_d.push_back(traj_jmt.path_d);
              }
              else
              {
                // generate SPLINE trajectory in x and y
                trajectory = generate_trajectory(target_lane, target_vel, target_time, map, car_x, car_y, car_yaw, 
                                                     car_s, car_d, previous_path_x, previous_path_y, prev_size);
              }

              double cost = cost_function(trajectory, target_lane, target_vel, predictions, sensor_fusion, car_lane);
              costs.push_back(cost);
              trajectories.push_back(trajectory);
            }

            double min_cost = 1e10;
            int min_cost_index = 0;
            for (int i = 0; i < costs.size(); i++)
            {
              if (costs[i] < min_cost)
              {
                min_cost = costs[i];
                min_cost_index = i;
              }
            }
            target_lane = targets[min_cost_index][0];
            ref_vel = targets[min_cost_index][1];
            if (param_trajectory_jmt)
            {
              ref_path_s = paths_s[min_cost_index];
              ref_path_d = paths_d[min_cost_index];
            }

            if (target_lane != car_lane)
            {
              cout << "====================> CHANGE LANE: lowest cost for target " << min_cost_index << " = (target_lane=" << target_lane
                   << " target_vel=" << ref_vel << " car_lane=" << car_lane << " cost="<< min_cost << ")" << endl;
            }


            //////////////////////////////////////////////////////////////////////


          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	msgJson["next_x"] = trajectories[min_cost_index][0]; //next_x_vals;
          	msgJson["next_y"] = trajectories[min_cost_index][1]; //next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

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
