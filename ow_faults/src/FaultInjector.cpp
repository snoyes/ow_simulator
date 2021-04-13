// The Notices and Disclaimers for Ocean Worlds Autonomy Testbed for Exploration
// Research and Simulation can be found in README.md in the root directory of
// this repository.

#include "ow_faults/FaultInjector.h"
#include <algorithm>

using namespace std;
using namespace ow_lander;

constexpr std::bitset<10> FaultInjector::isPanTiltExecutionError;
constexpr std::bitset<10> FaultInjector::isArmExecutionError;
constexpr std::bitset<10> FaultInjector::isPowerSystemFault;


FaultInjector::FaultInjector(ros::NodeHandle node_handle)
{
  auto joint_states_str = "joint_states";
  m_joint_state_sub = node_handle.subscribe(string("/_original/") + joint_states_str, 10,
    &FaultInjector::jointStateCb, this);
  m_joint_state_pub = node_handle.advertise<sensor_msgs::JointState>(joint_states_str, 10);

  auto ft_sensor_dist_pitch_str = "ft_sensor_dist_pitch";
  m_dist_pitch_ft_sensor_sub = node_handle.subscribe(string("/_original/") + ft_sensor_dist_pitch_str,
    10, &FaultInjector::distPitchFtSensorCb, this);
  m_dist_pitch_ft_sensor_pub = node_handle.advertise<geometry_msgs::WrenchStamped>(ft_sensor_dist_pitch_str, 10);

  //power fault publishers and subs
    // will return to checking state of charge once that returns
  m_power_soc_sub = node_handle.subscribe("/_original/power_system_node/state_of_charge", 1000, &FaultInjector::powerSOCListener, this); 
  m_fault_power_state_of_charge_pub = node_handle.advertise<std_msgs::Float64>("/power_system_node/state_of_charge", 1000);
  m_fault_power_temp_pub = node_handle.advertise<std_msgs::Float64>("/temporary/power_fault/temp_increase", 10);

  //antenna fault publishers and subs
  m_fault_ant_pan_sub = node_handle.subscribe("/_original/ant_pan_position_controller/command", 
                                              3, 
                                              &FaultInjector::antennaePanFaultCb, 
                                              this);
  m_fault_ant_tilt_sub = node_handle.subscribe("/_original/ant_tilt_position_controller/command", 
                                              3, 
                                              &FaultInjector::antennaeTiltFaultCb, 
                                              this);
  m_fault_ant_pan_pub = node_handle.advertise<std_msgs::Float64>("/ant_pan_position_controller/command", 10);
  m_fault_ant_tilt_pub = node_handle.advertise<std_msgs::Float64>("/ant_tilt_position_controller/command", 10);

  // topic for system fault messages, see Faults.msg, Arm.msg, Power.msg, PTFaults.msg
  m_fault_status_pub = node_handle.advertise<ow_faults::SystemFaults>("/system_faults_status", 10); 
  m_arm_fault_status_pub = node_handle.advertise<ow_faults::ArmFaults>("/arm_faults_status", 10); 
  m_power_fault_status_pub = node_handle.advertise<ow_faults::PowerFaults>("/power_faults_status", 10); 
  m_antennae_fault_status_pub = node_handle.advertise<ow_faults::PTFaults>("/pt_faults_status", 10);

  srand (static_cast <unsigned> (time(0)));
}

void FaultInjector::faultsConfigCb(ow_faults::FaultsConfig& faults, uint32_t level)
{
  // This is where we would check to see if faults is different from m_faults
  // if we wanted to change some state based on that.

  // Store current set of faults for later use
  m_faults = faults;
}

// Creating Fault Messages 
template<typename fault_msg>
void FaultInjector::setFaultsMessageHeader(fault_msg& msg){
  msg.header.stamp = ros::Time::now();
  msg.header.frame_id = "world";
}

void FaultInjector::setSystemFaultsMessage(ow_faults::SystemFaults& msg, std::bitset<10> systemFaultsBitmask) {
  setFaultsMessageHeader(msg);
  msg.value = systemFaultsBitmask.to_ullong();
}

template<typename fault_msg>
void FaultInjector::setComponentFaultsMessage(fault_msg& msg, ComponentFaults value) {
  setFaultsMessageHeader(msg);
  msg.value = static_cast<uint>(value);
}

void FaultInjector::publishAntennaeFaults(const std_msgs::Float64& msg, bool encoder, bool torque, float& m_faultValue, ros::Publisher& m_publisher){
  std_msgs::Float64 out_msg;
  
  if (!(encoder || torque)) {
    m_faultValue = msg.data;
  }
  out_msg.data = m_faultValue;
  m_publisher.publish(out_msg);
}

// Note for torque sensor failure, we are finding whether or not the hardware faults for antenna are being triggered. 
// Given that, this is separate from the torque sensor implemented by Ussama.
void FaultInjector::antennaePanFaultCb(const std_msgs::Float64& msg){
  publishAntennaeFaults(msg, 
                        m_faults.ant_pan_encoder_failure, 
                        m_faults.ant_pan_effort_failure, 
                        m_faultPanValue, m_fault_ant_pan_pub );
}

void FaultInjector::antennaeTiltFaultCb(const std_msgs::Float64& msg){
  publishAntennaeFaults(msg, 
                        m_faults.ant_tilt_encoder_failure, 
                        m_faults.ant_tilt_effort_failure, 
                        m_faultTiltValue, m_fault_ant_tilt_pub );
}

void FaultInjector::powerFaultCb(){
  std_msgs::Float64 soc_msg;
  std_msgs::Float64 thermal_msg;
  if (m_faults.low_state_of_charge_power_failure || m_faults.instantaneous_capacity_loss_power_failure) {
    if (m_faults.low_state_of_charge_power_failure && m_faults.instantaneous_capacity_loss_power_failure) {
      //both faults are on
      setPowerFaultValues("SOC", 0.05, .06);    
    } 
    else if (m_faults.low_state_of_charge_power_failure){
      // Fault is a range ( anything < 10%)
      setPowerFaultValues("SOC-LOW", 0.001, .01);
    } 
    else if (m_faults.instantaneous_capacity_loss_power_failure) {
      // (most recent and current). If the % difference is > 5% and no other tasks in progress, then fault. 
      setPowerFaultValues("SOC-CAP-LOSS", 0.05, .06);
    } 
    soc_msg.data = powerStateOfChargeValue;
    m_fault_power_state_of_charge_pub.publish(soc_msg);
  } 
  else { //no faults, set to SOC
      powerStateOfChargeValue = originalSOC;
      soc_msg.data = powerStateOfChargeValue;
      m_fault_power_state_of_charge_pub.publish(soc_msg);
  }
  if(m_faults.thermal_power_failure){
    // if > 50 degrees C, then consider fault. 
    setPowerFaultValues("THERMAL", 50, 90);
    thermal_msg.data = powerTemperatureOverloadValue;
    m_fault_power_temp_pub.publish(thermal_msg);
  } else {
    powerTemperatureOverloadValue = NAN;
  }
}

float FaultInjector::getRandomFloatFromRange( float min_val, float max_val){
  return min_val + (max_val - min_val) * (rand() / float(RAND_MAX));
}

void FaultInjector::setPowerFaultValues(const string& powerType, float min_val, float max_val){
  float percent_difference = getRandomFloatFromRange(powerStateOfChargeValue*min_val, powerStateOfChargeValue*max_val);
  float min_power_default = 0.5;
  float soc_fail_default = 10;
  float newPowerStateOfChargeValue = ((powerStateOfChargeValue-percent_difference) < min_power_default) ?  min_power_default : (powerStateOfChargeValue-percent_difference);

  if (powerType == "SOC" || powerType == "SOC-LOW"){
    // value needs to be between 0 and 9.99, decreased until 0
    if (powerStateOfChargeValue > soc_fail_default){
      powerStateOfChargeValue = soc_fail_default;
    } else {
      powerStateOfChargeValue = newPowerStateOfChargeValue;
    }
  }
  else if (powerType == "SOC-CAP-LOSS"){
    // cap loss error have increase of 5-10 % of the SOC
    powerStateOfChargeValue = newPowerStateOfChargeValue;
  }
  else if (powerType == "THERMAL"){
    if (isnan(powerTemperatureOverloadValue)){
      powerTemperatureOverloadValue = getRandomFloatFromRange(min_val, max_val);
    } else {
      float change_in_val = getRandomFloatFromRange(.01, 1);
      powerTemperatureOverloadValue += change_in_val;
    }
  }
}

void FaultInjector::powerSOCListener(const std_msgs::Float64& msg)
{
  originalSOC = msg.data;
  powerFaultCb();
}

void FaultInjector::jointStateCb(const sensor_msgs::JointStateConstPtr& msg)
{
  // Populate the map once here.
  // This assumes the collection of joints will never change.
  if (m_joint_state_indices.empty()) {
    for (int j = 0; j < NUM_JOINTS; j ++) {
      int index = findPositionInGroup(msg->name, joint_names[j]);
      if (index >= 0)
        m_joint_state_indices.push_back(index);
    }
  }

  sensor_msgs::JointState output(*msg);

  ow_faults::SystemFaults system_faults_msg;
  ow_faults::ArmFaults arm_faults_msg;
  ow_faults::PTFaults pt_faults_msg;
  ow_faults::PowerFaults power_faults_msg;

  ComponentFaults hardwareFault =  ComponentFaults::Hardware;
  std::bitset<10> systemFaultsBitmask{};

  // Set failed sensor values to 0
  unsigned int index;

  checkArmFaults();
  checkAntFaults();

  //pant tilt faults
  if (m_faults.ant_pan_encoder_failure && findJointIndex(J_ANT_PAN, index)) {
    output.position[index] = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.ant_pan_effort_failure && findJointIndex(J_ANT_PAN, index)) {
    output.effort[index]  = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.ant_tilt_encoder_failure && findJointIndex(J_ANT_TILT, index)) {
    output.position[index]  = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.ant_tilt_effort_failure && findJointIndex(J_ANT_TILT, index)) {
    output.effort[index]  = FAULT_ZERO_TELEMETRY;
  }

  if (m_antFault){
    systemFaultsBitmask |= isPanTiltExecutionError;
    setComponentFaultsMessage(pt_faults_msg, hardwareFault);
  }

  //arm faults
  if (m_faults.shou_yaw_encoder_failure && findJointIndex(J_SHOU_YAW, index)) {
    output.position[index]  = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.shou_yaw_effort_failure && findJointIndex(J_SHOU_YAW, index)) {
    output.effort[index]  = FAULT_ZERO_TELEMETRY;
  }

  if (m_faults.shou_pitch_encoder_failure && findJointIndex(J_SHOU_PITCH, index)) {
    output.position[index]  = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.shou_pitch_effort_failure && findJointIndex(J_SHOU_PITCH, index)) {
    output.effort[index]  = FAULT_ZERO_TELEMETRY;
  }

  if (m_faults.prox_pitch_encoder_failure && findJointIndex(J_PROX_PITCH, index)) {
    output.position[index]  = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.prox_pitch_effort_failure && findJointIndex(J_PROX_PITCH, index)) {
    output.effort[index]  = FAULT_ZERO_TELEMETRY;
  }

  if (m_faults.dist_pitch_encoder_failure && findJointIndex(J_DIST_PITCH, index)) {
    output.position[index]  = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.dist_pitch_effort_failure && findJointIndex(J_DIST_PITCH, index)) {
    output.effort[index]  = FAULT_ZERO_TELEMETRY;
  }

  if (m_faults.hand_yaw_encoder_failure && findJointIndex(J_HAND_YAW, index)) {
    output.position[index]  = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.hand_yaw_effort_failure && findJointIndex(J_HAND_YAW, index)) {
    output.effort[index]  = FAULT_ZERO_TELEMETRY;
  }

  if (m_faults.scoop_yaw_encoder_failure && findJointIndex(J_SCOOP_YAW, index)) {
    output.position[index]  = FAULT_ZERO_TELEMETRY;
  }
  if (m_faults.scoop_yaw_effort_failure && findJointIndex(J_SCOOP_YAW, index)) {
    output.effort[index]  = FAULT_ZERO_TELEMETRY;
  }

  if (m_armFault) {
    systemFaultsBitmask |= isArmExecutionError;
    setComponentFaultsMessage(arm_faults_msg, hardwareFault);
  }

<<<<<<< HEAD
    // power faults
  if (m_faults.low_state_of_charge_power_failure || m_faults.instantaneous_capacity_loss_power_failure || m_faults.thermal_power_failure) {
    systemFaultsBitmask |= isPowerSystemFault;
    setFaultsMessage(power_faults_msg, hardwareFault);
  } 
=======
  // power faults
  if(m_faults.low_state_of_charge_power_failure) {
    // Fault is a range ( anything < 10%)
    soc_msg.data = 2.2;
    m_fault_power_state_of_charge_pub.publish(soc_msg);
    setComponentFaultsMessage(power_faults_msg, hardwareFault);
    systemFaultsBitmask |= isPowerSystemFault;
  }
  if(m_faults.instantaneous_capacity_loss_power_failure) {
    // (most recent and current). If the % difference is > 5% and no other tasks in progress, then fault. 
    soc_msg.data = 98.5; //random now but should be >5% more than the previous value
    m_fault_power_state_of_charge_pub.publish(soc_msg);
    setComponentFaultsMessage(power_faults_msg, hardwareFault);
    systemFaultsBitmask |= isPowerSystemFault;
  }
  if(m_faults.thermal_power_failure){
    // if > 50 degrees C, then consider fault. 
    std_msgs::Float64 thermal_msg;
    setPowerTemperatureFaultValue(true);
    thermal_msg.data = powerTemperatureOverloadValue;
    m_fault_power_temp_pub.publish(thermal_msg);
    setComponentFaultsMessage(power_faults_msg, hardwareFault);
    systemFaultsBitmask |= isPowerSystemFault;
  } else {
    setPowerTemperatureFaultValue(false);
  }
>>>>>>> melodic-devel

  setSystemFaultsMessage(system_faults_msg, systemFaultsBitmask);
  
  m_joint_state_pub.publish(output);
  m_fault_status_pub.publish(system_faults_msg);
  m_arm_fault_status_pub.publish(arm_faults_msg);
  m_antennae_fault_status_pub.publish(pt_faults_msg);
  m_power_fault_status_pub.publish(power_faults_msg);

}

void FaultInjector::distPitchFtSensorCb(const geometry_msgs::WrenchStamped& msg)
{
  if (!m_faults.groups.ft_sensor_faults.enable) {
    m_dist_pitch_ft_sensor_pub.publish(msg);
    return;
  }

  auto out_msg = msg;

  if (m_faults.groups.ft_sensor_faults.zero_signal_failure) {
    out_msg.wrench.force = geometry_msgs::Vector3();
    out_msg.wrench.torque = geometry_msgs::Vector3();
  }

  auto mean = m_faults.groups.ft_sensor_faults.signal_bias_failure;
  auto stddev = m_faults.groups.ft_sensor_faults.signal_noise_failure;
  // TODO: consider optimizing this by re-creating the distribution only when
  // mean and stddev values change
  auto normal_dist = std::normal_distribution<float>(mean, stddev); 
  out_msg.wrench.force.x += normal_dist(m_random_generator);
  out_msg.wrench.force.y += normal_dist(m_random_generator);
  out_msg.wrench.force.z += normal_dist(m_random_generator);
  out_msg.wrench.torque.x += normal_dist(m_random_generator);
  out_msg.wrench.torque.y += normal_dist(m_random_generator);
  out_msg.wrench.torque.z += normal_dist(m_random_generator);

  m_dist_pitch_ft_sensor_pub.publish(out_msg);
}

void FaultInjector::checkArmFaults(){
  m_armFault = (m_faults.shou_yaw_encoder_failure || m_faults.shou_yaw_effort_failure ||             
                m_faults.shou_pitch_encoder_failure || m_faults.shou_pitch_effort_failure ||
                m_faults.prox_pitch_encoder_failure || m_faults.prox_pitch_effort_failure || 
                m_faults.dist_pitch_encoder_failure || m_faults.dist_pitch_effort_failure ||
                m_faults.hand_yaw_encoder_failure || m_faults.hand_yaw_effort_failure ||
                m_faults.scoop_yaw_encoder_failure || m_faults.scoop_yaw_effort_failure);
}

void FaultInjector::checkAntFaults(){
  m_antFault = (m_faults.ant_pan_encoder_failure || m_faults.ant_pan_effort_failure || 
                m_faults.ant_tilt_encoder_failure || m_faults.ant_tilt_effort_failure);
}

template<typename group_t, typename item_t>
int FaultInjector::findPositionInGroup(const group_t& group, const item_t& item)
{
  auto iter = std::find(group.begin(), group.end(), item);
  if (iter == group.end())
    return -1;
  return iter - group.begin();
}

bool FaultInjector::findJointIndex(const unsigned int joint, unsigned int& out_index)
{
  if(joint >= NUM_JOINTS)
    return false;

  out_index = m_joint_state_indices[joint];
  return true;
}
