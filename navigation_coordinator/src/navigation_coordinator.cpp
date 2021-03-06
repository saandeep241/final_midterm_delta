#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <geometry_msgs/Twist.h>
#include <iostream>
#include <string>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Quaternion.h>
#include <des_pub_state_service/ServiceMsg.h>
#include <traj_builder/traj_builder.h>
#include <math.h>


using namespace std;

nav_msgs::Odometry current_state;
geometry_msgs::PoseStamped current_pose;

ros::ServiceClient client;

void currStateCallback(const nav_msgs::Odometry &odom)
{
    current_state = odom;
    current_pose.pose = current_state.pose.pose;
}

void stop(){
    des_pub_state_service::ServiceMsg srv;
    srv.request.start_pos = current_pose;
    srv.request.goal_pos = current_pose;
    srv.request.mode = "0"; 
    if (client.call(srv))
    {
        ROS_INFO("STOP");
    }
  
}

double min_angle(double dang) {
    while (dang > M_PI) dang -= 2.0 * M_PI;
    while (dang < -M_PI) dang += 2.0 * M_PI;
    return dang;
}



float check_angle(float initial_angle,float desired_change)
{   
    double actual_angle = current_state.pose.pose.orientation.z;
    initial_angle = asin(initial_angle)*2; //0
    actual_angle = asin(actual_angle)*2;  //160
    double diff = min_angle(initial_angle+desired_change-actual_angle);
	ROS_INFO("angles  initial=[%f],current=[%f],desired=[%f],correction=[%f]",initial_angle,actual_angle,desired_change,diff);
    if (diff>0)
        {
		
        return diff;
        }
    else
        {
        return -diff;
        }
	
}


bool rotate(float psi)

{
    //this method just rotates the robot , this is useful for rotating to the respective direction and also amcl
    bool success = true;
    TrajBuilder trajBuilder;
    des_pub_state_service::ServiceMsg srv;
    geometry_msgs::PoseStamped start_pose;
    geometry_msgs::PoseStamped goal_pose_rot;
    string mode;
    start_pose.pose = current_state.pose.pose;

    bool success_rotate;
  
    ROS_INFO("Rotating at an angle of %f",psi);
  
    // rotate
    goal_pose_rot = trajBuilder.xyPsi2PoseStamped(current_pose.pose.position.x,
                                                  current_pose.pose.position.y,
                                                  psi); 
    srv.request.start_pos = current_pose;
    srv.request.goal_pos = goal_pose_rot;
    srv.request.mode = "2"; 
    if (client.call(srv))
    {
        success_rotate = srv.response.success;
        ROS_INFO("rotate success%d", success_rotate);
    }
    ros::spinOnce();
	return success_rotate;
	
}

float calculate_angle_correction(float goal_pose_x, float goal_pose_y)
{
    geometry_msgs::PoseStamped start_pose;
    start_pose.pose = current_state.pose.pose;
    double x_start = start_pose.pose.position.x;
    double y_start = start_pose.pose.position.y;
    double x_end = goal_pose_x;
    double y_end = goal_pose_y;
    double dx = x_end - x_start;
    double dy = y_end - y_start;
    double des_psi = atan2(dy, dx);
    return des_psi;


}

bool translate(float goal_pose_x, float goal_pose_y,float des_psi, bool rot)
{	

    bool success = true;
    TrajBuilder trajBuilder;
    des_pub_state_service::ServiceMsg srv;
    geometry_msgs::PoseStamped start_pose;
    geometry_msgs::PoseStamped goal_pose_trans;
   // geometry_msgs::PoseStamped goal_pose_rot;
    string mode;
    start_pose.pose = current_state.pose.pose;


    bool success_translate;
    if (rot==true){
        rotate(des_psi);
    }


    /*double x_start = start_pose.pose.position.x;
    double y_start = start_pose.pose.position.y;
    double x_end = goal_pose_x;
    double y_end = goal_pose_y;
    double dx = x_end - x_start;
    double dy = y_end - y_start;
    double des_psi = atan2(dy, dx);*/

	


    // forward
    goal_pose_trans = trajBuilder.xyPsi2PoseStamped(goal_pose_x,
                                                    goal_pose_y,
                                                    des_psi); // keep des_psi, change x,y
    srv.request.start_pos = current_pose;
    srv.request.goal_pos = goal_pose_trans;
    srv.request.mode = "1"; // 
    if (client.call(srv))
    {
        success_translate = srv.response.success;
        ROS_INFO("translate success%d", success_translate);
    }
    ros::spinOnce();

    // if fail to forward
    if (!success_translate)
    {
        ROS_INFO("Cannot move, obstacle. braking");
        srv.request.start_pos = current_pose;
        srv.request.goal_pos = current_pose; 
        srv.request.mode = "3";              
        client.call(srv);
        success = false;
    }
    ros::spinOnce();

    return success;
}

void init_mobot(float goal_pose_x, float goal_pose_y, float des_psi,bool rot)
{
    int retry_ctr = 0;
    bool success = translate(goal_pose_x, goal_pose_y,des_psi,rot);
    while (!success) {
        success = translate(goal_pose_x,goal_pose_y,des_psi,rot);
    }
}

void backUp()
{
    ROS_INFO("Backing up");
    TrajBuilder trajBuilder;
    des_pub_state_service::ServiceMsg srv;
    geometry_msgs::PoseStamped start_pose;

    start_pose.pose = current_state.pose.pose;

    srv.request.start_pos = current_pose;
    srv.request.goal_pos = current_pose;
    srv.request.mode = "4"; 
    if (client.call(srv))
    {
        bool success_backup = srv.response.success;
        ROS_INFO("backup %d", success_backup);
    }
    ros::spinOnce();
}




int main(int argc, char **argv)
{
    ros::init(argc, argv, "navigation_coordinator");
    ros::NodeHandle n;

    vector<geometry_msgs::PoseStamped> plan_points;

    client = n.serviceClient<des_pub_state_service::ServiceMsg>("des_state_publisher_service");

    ros::Subscriber current_state_sub = n.subscribe("/current_state", 1, currStateCallback);

    TrajBuilder trajBuilder;

    float x_table1 = 3.815;
    float y_table1 = 0.460;

    float x_table2 = 0.430;
    float y_table2 =2.380;

    float x_home = 0.0;
    float y_home = 0.0;
    float head_home=0.0;


    /*float x1 = 3.115;
    float y1 = 0.404;

    float x2 = 0.7;
    float y2 = 0.0;

    float x3 = 0.75;
    float y3 = 1.905;*/


   //ROS_INFO("Rotating for AMCL");
    //rotate(0.3);
   //ROS_INFO("Rotating for AMCL");
    //rotate(-0.3);
  //ROS_INFO("Rotating for AMCL");
  //rotate(0.3);
  //ROS_INFO("Roating for AMCL");
  //rotate(-0.3);M_PI-asin(prev_angle)*2
       
    //backUp();
    /*double prev_angle = current_state.pose.pose.orientation.z;
    double des_angle = M_PI-asin(prev_angle)*2;
    ros::spinOnce();
    rotate(des_angle/2);
    double correction = check_angle(prev_angle,des_angle/2);
    ros::Duration(0.5).sleep();
    ros::spinOnce();
    rotate(correction);
    prev_angle = current_state.pose.pose.orientation.z;
    ros::Duration(0.5).sleep();
   ros::spinOnce();
    rotate(des_angle/2);
    correction = check_angle(prev_angle,des_angle);
    ros::spinOnce();
    rotate(correction);
    //ros::Duration(0.5).sleep();
  // ros::Duration(0.5).sleep();
     /*rotate(M_PI/3);
    //rotate(0);
    ros::Duration(0.5).sleep();
    rotate(M_PI/3);
    //rotate(0);
    ros::Duration(0.5).sleep();*/
    
    //ros::Duration(0.5).sleep();
    //ros::Duration(0.5).sleep();
    //rotate(correction);*/
    float des_psi = calculate_angle_correction(x_table1,y_table1);
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);
    ROS_INFO("Starting from home 1");
    init_mobot(x_table1/3,y_table1,des_psi,true);
       
    ROS_INFO("After step 1");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);

    ROS_INFO("Starting from home 2");
    /* float cur_heading = asin(current_state.pose.pose.orientation.z)*2;
    if (cur_heading==des_psi){
        init_mobot(x_table1/2,y_table1,0,true);
        }
    else
    {
        init_mobot(x_table1/2,y_table1,des_psi-cur_heading,true);
    }*/
    des_psi = calculate_angle_correction(2*x_table1/3,y_table1);
    init_mobot(x_table1/2,y_table1,0,true);
    ROS_INFO("After step 2");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);

    /*ROS_INFO("Starting from home 3");
    des_psi = calculate_angle_correction(3*x_table1/4,y_table1);
    init_mobot(3*x_table1/4,y_table1,des_psi,true);
    ROS_INFO("After step 3");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);*/

    ROS_INFO("Starting from home 4");
    des_psi = calculate_angle_correction(x_table1,y_table1);
    init_mobot(x_table1,y_table1,0,true);
    ROS_INFO("After step 4");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);
    ROS_INFO("Should have reached table 1");
    backUp();

    ROS_INFO("After backup1");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);
    ROS_INFO("The above is after backup");



    //des_psi = calculate_angle_correction(x_table2,current_state.pose.pose.position.y);
    //init_mobot(x_table2,current_state.pose.pose.position.y,des_psi,true);


    des_psi = calculate_angle_correction(x_table2,y_table2-1.96);
    init_mobot(x_table2,y_home,des_psi,true);




    des_psi = calculate_angle_correction(x_table2,y_table2-0.96);
    init_mobot(x_table2,y_table2,des_psi,true);


    des_psi = calculate_angle_correction(x_table2,y_table2);
    init_mobot(x_table2,y_table2,des_psi,true);


    

    /*des_psi = calculate_angle_correction(x_table2,y_table2-1);
    init_mobot(x_table2,y_table2/4,des_psi,true);
    ROS_INFO("After table 1, step 1");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);

    /*des_psi = calculate_angle_correction(x_table2,2*y_table2/3);
    init_mobot(x_table2,y_table2/4,des_psi,true);
    ROS_INFO("After table 1, step 2");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);*/

    /*des_psi = calculate_angle_correction(x_table2,3*y_table2/4);
    init_mobot(x_table2,y_table2/4,des_psi,true);
    ROS_INFO("After table 1, step 3");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);*/


    /*des_psi = calculate_angle_correction(x_table2,y_table2);
    init_mobot(x_table2,y_table2/4,des_psi,true);
    ROS_INFO("After table 1, step 4");
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);*/



    backUp(); 

   des_psi = calculate_angle_correction(x_home,y_home);
   ROS_INFO("Going home now");
   init_mobot(x_home,y_home,des_psi,true);
   rotate(head_home);
   ROS_INFO("Should have reached home");

    /*init_mobot(0,0,des_psi,true);
    ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Current state heading=[%f]",current_state.pose.pose.orientation.z);
    ROS_INFO("Should have reached table 1");
    backUp();
    ROS_INFO("Turning to check to send it to home");*/


    

    
    /*ROS_INFO("Current state x=[%f],y=[%f]",current_state.pose.pose.position.x,current_state.pose.pose.position.y);
    ROS_INFO("Coming back from target 1");
	ROS_INFO("sending the robot to center");
    init_mobot(2.00,0.15,0,false);
	ROS_INFO("After step 1");
    init_mobot(1.5,0.05,0,false);
	ROS_INFO("After step 2");
    init_mobot(x2, y2, 0,false);
	ROS_INFO("After step 3");
    prev_angle = current_state.pose.pose.orientation.z;
    rotate(M_PI/6);
    rotate(M_PI/6);
    rotate(M_PI/6);
    correction = check_angle(prev_angle,M_PI/2);
    ROS_INFO("Moving to target 2");
    init_mobot(x3, y3, 0,false);*/
    
    

   // ROS_INFO("Going home");
    //init_mobot(x1, y1, 0,true);

    //float x_l = current_pose.pose.position.x;
    //float y_l = current_pose.pose.position.y;

    // stop everything
    stop();
    // ROS_INFO("STEP 5");
    // tryMove(-8, current_pose.pose.position.y - 0.05, 1);

    ros::spin();

    return 0;
}
