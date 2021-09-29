// The Notices and Disclaimers for Ocean Worlds Autonomy Testbed for Exploration
// Research and Simulation can be found in README.md in the root directory of
// this repository.

// TODO:
//   1. Make diff image publish less frequently (must be done in ow_dynamic_terrain)
//   2. Compute appropriate psuedo force duration based on dig_linear arguments
//   3. Try and get persistent service clients to work

#include <ros/ros.h>

#include "RegolithSpawner.h"

// DEBUG CODE
#include <ros/console.h>

int main(int argc, char* argv[]) 
{
  // DEBUG CODE
  if (ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, 
    ros::console::levels::Debug)) { // Change the level to fit your needs
    ros::console::notifyLoggerLevelsChanged();
  }

  // initialize ROS
  ros::init(argc, argv, "regolith_node");
  ros::NodeHandle nh("regolith_node");

  auto rs = RegolithSpawner(&nh);
  
  if (!rs.initialize()) {
    ROS_ERROR("Failed to initialize regolith node");
    return 1;
  }

  // DEBUG CODE
  // sleep(10.0);
  // rs.spawnRegolithInScoop();

  ros::spin();

  return 0;
}