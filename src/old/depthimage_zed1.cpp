/*
author : Yudai Sadakuni

publish depth image 

subscribe sq_lidar pointcloud
*/

#include <ros/ros.h>
#include "ros/package.h"
#include <tf/tf.h>
#include <tf/transform_listener.h>
#include <pcl/point_cloud.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/common/transforms.h>
#include <pcl_ros/transforms.h>
#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <image_geometry/pinhole_camera_model.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/point_cloud_conversion.h>
#include <sensor_msgs/image_encodings.h>

using namespace std;

typedef pcl::PointCloud<pcl::PointXYZRGB> PointCloudXYZRGB;

ros::Time t;

ros::Publisher pub;
image_transport::Publisher image_pub;

// Frame Name
string target_frame = "/zed1/zed_left_camera";
string source_frame = "/centerlaser";

// subscribe data確認用flag
bool pc_flag = false;
bool camera_flag = false;
bool image_flag = false;

// Callback (SQ_LiDAR)
sensor_msgs::PointCloud pc_;
void pcCallback(const sensor_msgs::PointCloud2ConstPtr& msg)
{
    sensor_msgs::convertPointCloud2ToPointCloud(*msg, pc_);
    t = msg->header.stamp;
    pc_flag = true;
}

//Callback (Camera Infomation)
sensor_msgs::CameraInfoConstPtr camera_;
void cameraCallback(const sensor_msgs::CameraInfoConstPtr& msg)
{
    camera_ = msg;
    camera_flag = true;
}

// Callback(Image)
sensor_msgs::ImageConstPtr image_;
void imageCallback(const sensor_msgs::ImageConstPtr& msg)
{
    image_ = msg;
    image_flag = true;
}

// depth to color
int depth2color(double depth)
{
    int color = 0;
    if(0 < depth && depth<30){
        color = 255*depth/30;
    }
    else{
        color = 255;
    }
    return color;
}

// Colouring Function
void colouring(sensor_msgs::PointCloud2 pc_msg, const sensor_msgs::CameraInfoConstPtr& cinfo_msg, const sensor_msgs::ImageConstPtr& image_msg)
{
    cout<<"ALL GREEN"<<endl;

    cv_bridge::CvImageConstPtr cv_img_ptr;
    try{
      cv_img_ptr = cv_bridge::toCvShare(image_msg);
    }catch (cv_bridge::Exception& e){
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    }
    cv::Mat image(cv_img_ptr->image.rows, cv_img_ptr->image.cols, cv_img_ptr->image.type());
    cv::Mat depth_image(cv_img_ptr->image.rows, cv_img_ptr->image.cols, CV_8UC1);
    
    image = cv_bridge::toCvShare(image_msg)->image;

    image_geometry::PinholeCameraModel cam_model_;
    cam_model_.fromCameraInfo(cinfo_msg);

    pcl::PointCloud<pcl::PointXYZ>::Ptr pcl_cloud(new pcl::PointCloud<pcl::PointXYZ>); // From ROS Msg
    pcl::PointCloud<pcl::PointXYZ>::Ptr trans_cloud(new pcl::PointCloud<pcl::PointXYZ>); // After transformation
    PointCloudXYZRGB::Ptr coloured = PointCloudXYZRGB::Ptr(new PointCloudXYZRGB); // For coloring purposes
    fromROSMsg(pc_msg, *trans_cloud);
    trans_cloud->header.frame_id = target_frame;
    pcl::copyPointCloud(*trans_cloud, *coloured);
 
    bool update = false;
    for (pcl::PointCloud<pcl::PointXYZRGB>::iterator pt = coloured->points.begin(); pt < coloured->points.end(); pt++)
    {
        if ((*pt).x<0) continue;
        cv::Point3d pt_cv(-(*pt).y, -(*pt).z, (*pt).x);
        cv::Point2d uv;
        uv = cam_model_.project3dToPixel(pt_cv);

        if(uv.x>0 && uv.x < image.cols && uv.y > 0 && uv.y < image.rows){
            int distance = depth2color((*pt).z);
            depth_image.at<uchar>(uv.y, uv.x) = distance;
            update = true;
		}
    }
    ROS_INFO("Publish coloured PC");

    //Publish Depth Image
    if(update){
        sensor_msgs::ImagePtr depth_msg = cv_bridge::CvImage(std_msgs::Header(), "mono8", depth_image).toImageMsg();
        image_pub.publish(depth_msg);
    }

	pc_flag = false;
	camera_flag = false;
	image_flag = false;
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "depthimage_zed1");
    ros::NodeHandle n;
    image_transport::ImageTransport it(n);
    tf::TransformListener listener;
    tf::StampedTransform transform;

    ros::Subscriber pc_sub    = n.subscribe("/sq_lidar/points/right", 10, pcCallback); 
    ros::Subscriber cinfo_sub = n.subscribe("/zed1/left/camera_info", 10, cameraCallback);
    ros::Subscriber image_sub = n.subscribe("/zed1/left/image_rect_color/republish", 10, imageCallback);

    image_pub = it.advertise("/zed1/left/depth/sq", 10);

    ros::Rate rate(10);

    while(ros::ok())
    {
        sensor_msgs::PointCloud pc_trans;
        sensor_msgs::PointCloud2 pc2_trans;

        // Transform
        try{
            listener.waitForTransform(target_frame.c_str(), source_frame.c_str(), t, ros::Duration(1.0));
            listener.transformPointCloud(target_frame.c_str(), t, pc_, source_frame.c_str(), pc_trans);
            sensor_msgs::convertPointCloudToPointCloud2(pc_trans, pc2_trans);
        }catch (tf::TransformException& ex) {
            ROS_WARN("[draw_frames] TF exception:\n%s", ex.what());
        }

        // coloring
        if(pc_flag && camera_flag && image_flag){
            colouring(pc2_trans, camera_, image_);
        }

        ros::spinOnce();
        rate.sleep();
    }
    return 0;
}
  
