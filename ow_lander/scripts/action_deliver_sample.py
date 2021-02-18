#!/usr/bin/env python2

# The Notices and Disclaimers for Ocean Worlds Autonomy Testbed for Exploration
# Research and Simulation can be found in README.md in the root directory of
# this repository.

import rospy
from tf.transformations import quaternion_from_euler
from tf.transformations import euler_from_quaternion
from moveit_msgs.msg import PositionConstraint
from geometry_msgs.msg import Quaternion
from shape_msgs.msg import SolidPrimitive
from moveit_msgs.msg import RobotTrajectory
from trajectory_msgs.msg import JointTrajectory
from trajectory_msgs.msg import JointTrajectoryPoint


deliver_sample_traj = RobotTrajectory()


def cascade_plans (plan1, plan2):
     # Create a new trajectory object
    new_traj = RobotTrajectory()
    # Initialize the new trajectory to be the same as the planned trajectory
    traj_msg = JointTrajectory()
    # Get the number of joints involved
    n_joints1 = len(plan1.joint_trajectory.joint_names)
    n_joints2 = len(plan2.joint_trajectory.joint_names)
    # Get the number of points on the trajectory
    n_points1 = len(plan1.joint_trajectory.points)
    n_points2 = len(plan2.joint_trajectory.points)
    # Store the trajectory points
    points1 = list(plan1.joint_trajectory.points)

    points2 = list(plan2.joint_trajectory.points)
    end_time = plan1.joint_trajectory.points[n_points1-1].time_from_start
    start_time =  plan1.joint_trajectory.points[0].time_from_start
    duration =  end_time - start_time
    # add a time toleracne between  successive plans
    time_tolerance = rospy.Duration.from_sec(0.1)
    

    for i in range(n_points1):
        point = JointTrajectoryPoint()
        point.time_from_start = plan1.joint_trajectory.points[i].time_from_start
        point.velocities = list(plan1.joint_trajectory.points[i].velocities)
        point.accelerations = list(plan1.joint_trajectory.points[i].accelerations)
        point.positions = plan1.joint_trajectory.points[i].positions
        points1[i] = point
        traj_msg.points.append(point)
        end_time = plan1.joint_trajectory.points[i].time_from_start
        
    for i in range(n_points2):
        point = JointTrajectoryPoint()
        point.time_from_start = plan2.joint_trajectory.points[i].time_from_start + end_time +time_tolerance
        point.velocities = list(plan2.joint_trajectory.points[i].velocities)
        point.accelerations = list(plan2.joint_trajectory.points[i].accelerations)
        point.positions = plan2.joint_trajectory.points[i].positions
        #points1[i] = point
        traj_msg.points.append(point)
    
    traj_msg.joint_names = plan1.joint_trajectory.joint_names
    traj_msg.header.frame_id = plan1.joint_trajectory.header.frame_id
    new_traj.joint_trajectory = traj_msg
    return new_traj   


def deliver_sample(move_arm, robot, args):
  """
  :type move_arm: class 'moveit_commander.move_group.MoveGroupCommander'
  :type args: List[bool, float, float, float]
  """
  move_arm.set_planner_id("RRTstar")
  x_delivery = args.delivery.x
  y_delivery = args.delivery.y
  z_delivery = args.delivery.z

  # after sample collect
  mypi = 3.14159
  d2r = mypi/180
  r2d = 180/mypi

  goal_pose = move_arm.get_current_pose().pose
  # position was found from rviz tool
  goal_pose.position.x = x_delivery
  goal_pose.position.y = y_delivery
  goal_pose.position.z = z_delivery

  r = -179
  p = -20
  y = -90

  q = quaternion_from_euler(r*d2r, p*d2r, y*d2r)
  goal_pose.orientation = Quaternion(q[0], q[1], q[2], q[3])

  move_arm.set_pose_target(goal_pose)

  plan_a = move_arm.plan()
  

  if len(plan_a.joint_trajectory.points) == 0:  # If no plan found, abort
    return False

  # rotate scoop to deliver sample at current location...

  # adding position constraint on the solution so that the tip doesnot diverge to get to the solution.
  pos_constraint = PositionConstraint()
  pos_constraint.header.frame_id = "base_link"
  pos_constraint.link_name = "l_scoop"
  pos_constraint.target_point_offset.x = 0.1
  pos_constraint.target_point_offset.y = 0.1
  # rotate scoop to deliver sample at current location begin
  pos_constraint.target_point_offset.z = 0.1
  pos_constraint.constraint_region.primitives.append(
      SolidPrimitive(type=SolidPrimitive.SPHERE, dimensions=[0.01]))
  pos_constraint.weight = 1

  # using euler angles for own verification..

  r = +180
  p = 90  # 45 worked get
  y = -90
  q = quaternion_from_euler(r*d2r, p*d2r, y*d2r)
  
  start_state = plan_a.joint_trajectory.points[len(plan_a.joint_trajectory.points)-1].positions
  
  cs = robot.get_current_state()

  # adding antenna state and grinder state  to the robot states.
  # grider state obstained from rviz
  
  new_value =  (0,0) + start_state[:5] + (-0.1407739555464298,) + (start_state [5],)
  
  
  cs.joint_state.position = new_value # modify current state of robot to the end state of the previous plan
  
  move_arm.set_start_state(cs)

  goal_pose.orientation = Quaternion(q[0], q[1], q[2], q[3])
  
  #position value obtained from rviz scoop
  goal_pose.position.x = 0.55127
  goal_pose.position.y = -0.4213
  goal_pose.position.z = 0.77815
  
  move_arm.set_pose_target(goal_pose)
  plan_b = move_arm.plan()

  if len(plan_b.joint_trajectory.points) == 0:  # If no plan found, send the previous plan only
    return plan_a

  #plan = move_arm.go(wait=True)
  #move_arm.stop()
  #move_arm.clear_pose_targets()
  deliver_sample_traj = cascade_plans (plan_a, plan_b)

  move_arm.set_planner_id("RRTconnect")

  return deliver_sample_traj
