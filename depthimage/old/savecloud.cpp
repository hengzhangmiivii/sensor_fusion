#include <ros/ros.h>
#include <std_msgs/Bool.h>
#include <std_msgs/Int32.h>
#include <sensor_msgs/PointCloud2.h>
#include <nav_msgs/Odometry.h>
#include <pcl/point_cloud.h>
#include <pcl/common/transforms.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl_ros/point_cloud.h>
#include <tf/tf.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>

#include "Eigen/Core"
#include "Eigen/Dense"
#include "Eigen/LU"

#include <time.h>

using namespace std;

typedef pcl::PointXYZI PointA;
typedef pcl::PointCloud<PointA> CloudA;
typedef pcl::PointCloud<PointA>::Ptr CloudAPtr;

#include <sensor_fusion/create_matrix.h>
#include <sensor_fusion/savePCDFile.h>

class SaveCloud{
    private:
        ros::NodeHandle nh;
        
        ros::Subscriber odom_sub;
        ros::Subscriber cloud_sub;
        ros::Publisher cloud_pub;
        ros::Publisher stop_pub;

        // for lcl
        CloudAPtr save_cloud;
        CloudAPtr old_cloud;
        nav_msgs::Odometry init_odom;
        nav_msgs::Odometry odom_;
        Eigen::Matrix4f transform_matrix;
        Eigen::Matrix4f inverse_transform_matrix;
        int count;
        int save_count;
        bool save_flag;

		int pcd_count;
        
        // waypoint
        bool waypoint_flag;

        // odom flag
        bool odom_flag;
        double threshold;
        double distance;
        double meter;
        nav_msgs::Odometry old_odom;
        nav_msgs::Odometry new_odom;

    public:
        SaveCloud();
        void odomCallback(const nav_msgs::OdometryConstPtr msg);
        void cloudCallback(const sensor_msgs::PointCloud2ConstPtr msg);
};

SaveCloud::SaveCloud()
    : nh("~"), save_cloud(new CloudA), old_cloud(new CloudA)
{
    nh.getParam("save_count", save_count);
    nh.getParam("threshold", threshold);

    odom_sub        = nh.subscribe("/odom"     , 10, &SaveCloud::odomCallback,      this);
    cloud_sub       = nh.subscribe("/cloud"    , 10, &SaveCloud::cloudCallback,     this);

    cloud_pub       = nh.advertise<sensor_msgs::PointCloud2>("/cloud/lcl", 10);
    stop_pub        = nh.advertise<std_msgs::Bool>("/stop", 10);

    init_odom.header.frame_id = "/odom";
    init_odom.child_frame_id = "/base_link";
    init_odom.pose.pose.position.x = 0.0;
    init_odom.pose.pose.position.y = 0.0;
    init_odom.pose.pose.position.z = 0.0;
    init_odom.pose.pose.orientation.x = 0.0;
    init_odom.pose.pose.orientation.y = 0.0;
    init_odom.pose.pose.orientation.z = 0.0;
    init_odom.pose.pose.orientation.w = 0.0;
    odom_ = init_odom;

    count = 0;

    save_flag = false;

    waypoint_flag = false;
    
    // odom flag
    odom_flag = false;
    distance = 0;
    meter = 0;

	pcd_count = 0;
}

void SaveCloud::odomCallback(const nav_msgs::OdometryConstPtr msg)
{
    odom_ = *msg;
    transform_matrix = create_matrix(odom_, 1.0);

    if(!odom_flag){
        printf("init odom\n");
        old_odom = *msg;
        odom_flag = true;
    }
    else{
        new_odom = *msg;
        double dt = sqrt( pow((new_odom.pose.pose.position.x - old_odom.pose.pose.position.x), 2) + 
                pow((new_odom.pose.pose.position.y - old_odom.pose.pose.position.y), 2) );
        distance += dt;
		meter += dt;
        old_odom = *msg;

    }

    if(threshold<meter)
	{
        save_flag = true;
	}
}


void SaveCloud::cloudCallback(const sensor_msgs::PointCloud2ConstPtr msg)
{

    std_msgs::Bool stop;
	double vel = sqrt( pow(odom_.twist.twist.linear.x, 2) + pow(odom_.twist.twist.linear.y, 2) );
    if(save_flag){
		stop.data = true;
		
		if(vel < 0.001){
			CloudAPtr input_cloud(new CloudA);
			CloudAPtr transform_cloud(new CloudA);
			CloudAPtr threshold_cloud(new CloudA);
			CloudAPtr inverse_cloud(new CloudA);

			pcl::fromROSMsg(*msg, *input_cloud);
			pcl::transformPointCloud(*input_cloud, *transform_cloud, transform_matrix);

			for(size_t i=0;i<input_cloud->points.size();i++){
				double distance = sqrt(pow(input_cloud->points[i].x, 2)+
						pow(input_cloud->points[i].y, 2)+
						pow(input_cloud->points[i].z, 2));
				if(distance < 30)
					threshold_cloud->points.push_back(transform_cloud->points[i]);
			}

			// Eigen::Matrix4f inverse_transform_matrix = transform_matrix.inverse();
			// pcl::transformPointCloud(*threshold_cloud, *inverse_cloud, inverse_transform_matrix);
			// *save_cloud += *inverse_cloud;

			*save_cloud += *threshold_cloud;

			if(count == save_count){
				sensor_msgs::PointCloud2 pc2;
				pcl::toROSMsg(*save_cloud, pc2);
				// pc2.header.frame_id = msg->header.frame_id;  
				pc2.header.frame_id = init_odom.header.frame_id;
				pc2.header.stamp = ros::Time::now();
				cloud_pub.publish(pc2);

				savePCDFile(save_cloud, pcd_count);
		
				save_cloud->points.clear();

				save_flag = false;
				count = 0;
				meter = 0.0;
				pcd_count++;
				cout<<"Finish Save Cloud"<<endl;
			}
			else if(count%100==0){
				printf("count:%4d size:%6d max:%d\n", 
						count, 
						int(save_cloud->points.size()),
						save_count);
			}
			count++;
		}
	}

    else{
		printf("distance:%.2f meter:%.2f threshold:%.2f \n", distance, meter, threshold); 
        // cout<<"Waitting Arrive WayPoint"<<endl;
        stop.data = false;
    }

    stop_pub.publish(stop);
}

int main(int argc, char**argv)
{
    ros::init(argc, argv, "save_cloud");

    SaveCloud savecloud;;
    
    ros::spin();
    return 0;
}
