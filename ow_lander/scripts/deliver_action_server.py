#!/usr/bin/env python2

# The Notices and Disclaimers for Ocean Worlds Autonomy Testbed for Exploration
# Research and Simulation can be found in README.md in the root directory of
# this repository.

import rospy
import actionlib
import ow_lander.msg
import sys
import copy
import moveit_commander
import moveit_msgs.msg
import geometry_msgs.msg
from math import pi
from std_msgs.msg import String
from sensor_msgs.msg import JointState
from gazebo_msgs.msg import LinkStates
from moveit_commander.conversions import pose_to_list
import math
import constants
import utils
import action_deliver_sample

from LanderInterface import MoveItInterface
from LanderInterface import LinkStateSubscriber
from trajectory_async_execution import TrajectoryAsyncExecuter
from moveit_msgs.msg import RobotTrajectory



class DeliverActionServer(object):
    
    def __init__(self,name):
        self._action_name = name
        self._server = actionlib.SimpleActionServer(self._action_name, ow_lander.msg.DeliverAction, execute_cb=self.on_deliver_action, auto_start = False)
        self._server.start()
        # Action Feedback/Result
        self._fdbk = ow_lander.msg.UnstowFeedback()
        self._result = ow_lander.msg.UnstowResult()
        self._current_link_state = LinkStateSubscriber()
        self._interface = MoveItInterface()
        self._timeout = 0.0
        self.trajectory_async_executer = TrajectoryAsyncExecuter()
        self.trajectory_async_executer.connect("arm_controller")
        self.deliver_sample_traj = RobotTrajectory()
        
    
    def _update_feedback(self):
        self._ls =  self._current_link_state._link_value
        self._fdbk.current.x = self._ls.x
        self._fdbk.current.y = self._ls.y
        self._fdbk.current.z = self._ls.z
        self._server.publish_feedback(self._fdbk)

        
        
    def _update_motion(self, goal):
        print("Deliver sample activity started")
        self.deliver_sample_traj  = action_deliver_sample.deliver_sample(self._interface.move_arm,self._interface.robot, self._interface.moveit_fk, goal)
        #plan = self._interface.move_arm.plan(goal)
        n_points = len(self.deliver_sample_traj.joint_trajectory.points)
        start_time =   self.deliver_sample_traj.joint_trajectory.points[0].time_from_start
        end_time = self.deliver_sample_traj.joint_trajectory.points[n_points-1].time_from_start
        self._timeout = end_time -start_time
        
    def on_deliver_action(self,goal):
        plan = self._update_motion(goal)
        success = False

        self.trajectory_async_executer.execute(self.deliver_sample_traj.joint_trajectory,
                                           done_cb=None,
                                           active_cb=None,
                                           feedback_cb=None)

        # Record start time
        start_time = rospy.get_time()

        def now_from_start(start):
            #return rospy.get_time() - start
            return rospy.Duration(secs=rospy.get_time() - start)

        while ((now_from_start(start_time) < self._timeout)):

           self._update_feedback()
           
        success = self.trajectory_async_executer.wait()
        
            
        if success:
            self._result.final.x = self._fdbk.current.x
            self._result.final.y = self._fdbk.current.y 
            self._result.final.z = self._fdbk.current.z 
            rospy.loginfo('%s: Succeeded' % self._action_name)
            self._server.set_succeeded(self._result)
    
if __name__ == '__main__':
    rospy.init_node('Deliver')
    server = DeliverActionServer(rospy.get_name())
    rospy.spin()
        
  
