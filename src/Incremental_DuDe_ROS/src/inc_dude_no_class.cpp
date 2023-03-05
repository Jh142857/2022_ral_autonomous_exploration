//ROS
#include "ros/ros.h"
#include "std_msgs/String.h"
#include "nav_msgs/GetMap.h"
#include "nav_msgs/Odometry.h"
#include "visualization_msgs/MarkerArray.h"

//openCV
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <opencv2/imgproc/imgproc.hpp>



//DuDe
#include "wrapper.hpp"



class Stable_graph
{
	public:
	
//	Nodes
	std::vector<std::vector<cv::Point> > Region_contour;
	std::vector<cv::Point> Region_centroid;
	std::vector<std::set<int> > Region_connections;

// Edges
	std::vector<cv::Point> diagonal_centroid;
	std::vector<std::set<int> > diagonal_connections;
	
	Stable_graph(){
		int b=2;
	}

	~Stable_graph(){
	}
	
	void init_with_wrapper(){
		int a=2;
	}

};





class ROS_handler
{
	ros::NodeHandle n;
	
	image_transport::ImageTransport it_;
	image_transport::Subscriber image_sub_;
	image_transport::Publisher image_pub_;	
	cv_bridge::CvImagePtr cv_ptr;
		
	std::string mapname_;
	ros::Subscriber map_sub_;
	bool saved_map_;
	
	ros::Timer timer;
	
	std::vector<std::vector<cv::Point> > Convex_Marker_;
	ros::Publisher markers_pub_ ;
	nav_msgs::MapMetaData Map_Info_;
	
	ros::Subscriber odom_sub_;
	nav_msgs::Odometry Odom_Info_;
	
	float robot_position_[2];
	cv::Point robot_position_image_;
	std::vector <cv::Point> path_;
	cv::Point position_cm_;
	float distance;

	float safety_distance;
	
	int threshold_;	
	std::vector<std::vector<cv::Point> > contour_vector;
	
	float Decomp_threshold_;
	

	bool first_time;	
	cv::Mat Stable_Image;
	Stable_graph Stable;
	cv::Rect previous_rect;
	
	geometry_msgs::Pose current_origin_;

	std::vector <float> time_vector;
	
	public:
		ROS_handler(const std::string& mapname, float threshold) : mapname_(mapname), saved_map_(false), it_(n), Decomp_threshold_(threshold)
		{


			ROS_INFO("Waiting for the map");
			map_sub_ = n.subscribe("map", 1, &ROS_handler::mapCallback, this);
			ros::Subscriber chatter_sub_ = n.subscribe("chatter", 1000, &ROS_handler::chatterCallback, this);
			odom_sub_ = n.subscribe("pose_corrected", 1, &ROS_handler::odomCallback, this);
			timer = n.createTimer(ros::Duration(0.5), &ROS_handler::metronomeCallback, this);
			image_pub_ = it_.advertise("/tagged_image", 1);
			
			cv_ptr.reset (new cv_bridge::CvImage);
			cv_ptr->encoding = "mono8";

			markers_pub_ = n.advertise<visualization_msgs::Marker>( "skeleton_marker_", 10 );

			Map_Info_.resolution=0.05; //default;
			Map_Info_.width=4000; //default;
			Map_Info_.height=4000; //default;
			position_cm_ = cv::Point(0,0); 
			distance=0;
			safety_distance = 1;
			
			first_time = true;
			
			current_origin_.position.x=0;
			current_origin_.position.y=0;
			current_origin_.position.z=0;
			
		}

		~ROS_handler()
		{

		}


/////////////////////////////	
// ROS CALLBACKS			
////////////////////////////////		
		void chatterCallback(const std_msgs::String::ConstPtr& msg)
		{
		  ROS_INFO("I heard: [%s]", msg->data.c_str());  
		}
		
//////////////////////////////////		
		void mapCallback(const nav_msgs::OccupancyGridConstPtr& map)
		{
			
			cv::Mat grad;
			float pixel_Tau = Decomp_threshold_ / Map_Info_.resolution;			

			DuDe_OpenCV_wrapper wrapp;
			wrapp.set_Tau(Decomp_threshold_);
			wrapp.set_pixel_Tau(pixel_Tau);
							
			Map_Info_ = map-> info;						
			clock_t begin = clock();
			
			clock_t begin_process, end_process;
			double elapsed_secs_process;// = double(end_process - begin_process) / CLOCKS_PER_SEC;
//			end_process=clock();   elapsed_secs_process = double(end_process - begin_process) / CLOCKS_PER_SEC;			std::cerr<<"Time elapsed in process "<< elapsed_secs_process*1000 << " ms"<<std::endl;

			{
			std::cout <<"Map_Info_.resolution  " << Map_Info_.resolution << std::endl;
			std::cout <<"Pixel_Tau  " << pixel_Tau << std::endl;


			ROS_INFO("Received a %d X %d map @ %.3f m/pix",
				map->info.width,
				map->info.height,
				map->info.resolution);
			 } 
			 
			 if( (map->info.origin.position.x != current_origin_.position.x) || (map->info.origin.position.y != current_origin_.position.y)){
				 adjust_stable_contours();
			 }
			 
			cv_ptr->header = map->header;
			
	///////////////////////////////////
	// Occupancy Grid to Image
			begin_process = clock();
			
			cv::Mat img(map->info.height, map->info.width, CV_8U);
			img.data = (unsigned char *)(&(map->data[0]) );

			int gap = safety_distance / Map_Info_.resolution;

			cv::Rect Enbigger_Rect(gap, gap, img.cols, img.rows);			

			cv::Mat Occ_image(map->info.height + 2*gap, map->info.width + 2*gap, CV_8U,255);
			cv::Rect Occ_Rect(0, 0, Occ_image.cols, Occ_image.rows);
			img.copyTo(Occ_image(Enbigger_Rect));

			cv::Rect resize_rect;
			cv::Mat black_image;
			cv::Mat image_cleaned = clean_image(Occ_image, black_image);
			
			end_process=clock();   elapsed_secs_process = double(end_process - begin_process) / CLOCKS_PER_SEC;			std::cerr<<"Time elapsed in process transform "<< elapsed_secs_process*1000 << " ms"<<std::endl<<std::endl;

	//////////////////////////////////////////////////////////
	//// Decomposition
			begin_process = clock();

			cv::Mat stable_drawing = cv::Mat::zeros(Occ_image.size().height, Occ_image.size().width, CV_8UC1);
			drawContours(stable_drawing, Stable.Region_contour, -1, 255, -1, 8);
			
			cv::Mat working_image = image_cleaned & ~stable_drawing;
			cv::Mat will_be_destroyed = working_image.clone();
			
			std::vector<std::vector<cv::Point> > Differential_contour;
			cv::findContours(will_be_destroyed, Differential_contour, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );
			
			
			// multiple contours

			vector<vector<cv::Point> > big_contours_vector;
			for(int i=0; i < Differential_contour.size(); i++){
				float current_area = cv::contourArea(Differential_contour[i]);
				if(current_area >gap*gap){
					big_contours_vector.push_back(Differential_contour[i]);
				}
			}
			if(first_time) resize_rect = cv::boundingRect(big_contours_vector[0]);
			else resize_rect= previous_rect;

			// Match between old and new
			vector<vector<cv::Point> > connected_contours, unconnected_contours;
			std::vector<cv::Point> unconnected_centroids, connected_centroids;
			vector< vector <int > > conection_prev_new;
			for(int i=0;i < Stable.Region_contour.size();i++){
				bool is_stable_connected = false;
				for(int j=0;j < big_contours_vector.size();j++){
					int connected = 0;
					cv::Point centroid;
					are_contours_connected(Stable.Region_contour[i], big_contours_vector[j] , centroid, connected);
					if (connected>0){
//							cout << "contour "<< j << " in region " <<i<< " is connected to stable region "<<k << endl;
						vector <int> pair;
						pair.push_back(i);
						pair.push_back(j);
						conection_prev_new.push_back(pair);
						is_stable_connected = true;
//						cout << "Old contour " << i<<" connected to new "<< j << endl;
					}
				}
				if(is_stable_connected == false){
					unconnected_contours.push_back(Stable.Region_contour[i]);
					unconnected_centroids.push_back(Stable.Region_centroid[i]);
//					cout << "Old contour " << i<<" is not connected  "<< endl;
				}
				else{
					connected_contours.push_back(Stable.Region_contour[i]);
					connected_centroids.push_back(Stable.Region_centroid[i]);
				}
			}

/*
			cout << "number of growing regions " << conection_prev_new.size() << endl;
			cout << "connected_contours.size " << connected_contours.size() << endl;
			cout << "unconnected_contours.size " << unconnected_contours.size() << endl;
			cout << "Sum " << connected_contours.size() + unconnected_contours.size() << endl;
			cout << "Original " << Stable.Region_contour.size() << endl;
//*/			
			
			
			
			//Draw image with expanded contours matched
			cv::Mat expanded_drawing = cv::Mat::zeros(Occ_image.size().height, Occ_image.size().width, CV_8UC1);
			drawContours(expanded_drawing, connected_contours,  -1, 2, -1, 8);

			for(int i=0; i < conection_prev_new.size();i++){
				drawContours(expanded_drawing, big_contours_vector, conection_prev_new[i][1] , 2, -1, 8);
			}



			will_be_destroyed = expanded_drawing.clone();			
			std::vector<std::vector<cv::Point> > Expanded_contour;
			cv::findContours(will_be_destroyed, Expanded_contour, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE );

			if(first_time){
				Expanded_contour.clear();
				Expanded_contour = big_contours_vector;
				first_time=false;
			}

			cout << "Expanded_contour.size "<<Expanded_contour.size()<< endl;
/*
 			for (std::vector<vector<cv::Point> >::iterator it = Stable.Region_contour.begin() ; it != Stable.Region_contour.end(); ++it){
				int a=2;
			}
*/

			
			end_process=clock();   elapsed_secs_process = double(end_process - begin_process) / CLOCKS_PER_SEC;			std::cerr<<"Time elapsed in Pre-Decomp "<< elapsed_secs_process*1000 << " ms"<<std::endl<<std::endl;


			// Decompose in several wrappers
			begin_process = clock();

			vector<DuDe_OpenCV_wrapper> wrapper_vector(Expanded_contour.size());
			for(int i = 0; i <Expanded_contour.size();i++){
				cv::Mat Temporal_Image = cv::Mat::zeros(Occ_image.size().height, Occ_image.size().width, CV_8UC1);								
				cv::Mat temporal_image_cut = cv::Mat::zeros(Occ_image.size().height, Occ_image.size().width, CV_8UC1);								
				drawContours(Temporal_Image, Expanded_contour, i, 255, -1, 8);
				image_cleaned.copyTo(temporal_image_cut,Temporal_Image);
				
				wrapper_vector[i].set_pixel_Tau(pixel_Tau);			
				
				resize_rect |= wrapper_vector[i].Decomposer(temporal_image_cut);
			}	




		// Paint differential contours
			vector<vector<cv::Point> > joint_contours = unconnected_contours;
			vector<cv::Point> joint_centroids = unconnected_centroids;
			
			for(int i = 0; i < wrapper_vector.size();i++){
				for(int j = 0; j < wrapper_vector[i].Decomposed_contours.size();j++){
					joint_contours.push_back(wrapper_vector[i].Decomposed_contours[j]);
					joint_centroids.push_back(wrapper_vector[i].contours_centroid[j]);
				}
			}	
			
			end_process=clock();   elapsed_secs_process = double(end_process - begin_process) / CLOCKS_PER_SEC;			std::cerr<<"Time elapsed in process multiple Decomp "<< elapsed_secs_process*1000 << " ms"<<std::endl<<std::endl;
//			time_vector.push_back(elapsed_secs_process*1000);


	///////////////////
	//// Build stable graph
			begin_process = clock();

			Stable.Region_contour  = joint_contours;
			Stable.Region_centroid = joint_centroids;
			previous_rect = resize_rect;

			end_process=clock();   elapsed_secs_process = double(end_process - begin_process) / CLOCKS_PER_SEC;			std::cerr<<"Time elapsed in process Stable Graph "<< elapsed_secs_process*1000 << " ms"<<std::endl<<std::endl;


	////////////
	//Draw Image
			begin_process = clock();

			cv::Mat Drawing = cv::Mat::zeros(Occ_image.size().height, Occ_image.size().width, CV_8UC1);	
			

			for(int i = 0; i <joint_contours.size();i++){
				drawContours(Drawing, joint_contours, i, i+1, -1, 8);
			}
			cv::flip(Drawing,Drawing,0);
			for(int i = 0; i < joint_centroids.size();i++){
				stringstream mix;      mix<<i;				std::string text = mix.str();
				putText(Drawing, text, cv::Point(joint_centroids[i].x, Occ_image.size().height - joint_centroids[i].y ), cv::FONT_HERSHEY_SCRIPT_SIMPLEX, 0.5, joint_centroids.size()+1, 1, 8);
			}	

/*
			cv::Mat Drawing2 = cv::Mat::zeros(Occ_image.size().height, Occ_image.size().width, CV_8UC1);	
		
			for(int i = 0; i <unconnected_contours.size();i++){
				drawContours(Drawing2, unconnected_contours, i, i+1, -1, 8);
			}	
			cv::flip(Drawing2,Drawing2,0);
			for(int i = 0; i <unconnected_centroids.size();i++){
				stringstream mix;      mix<<i;				std::string text = mix.str();
				putText(Drawing2, text, cv::Point(unconnected_centroids[i].x, Occ_image.size().height - unconnected_centroids[i].y ), cv::FONT_HERSHEY_SCRIPT_SIMPLEX, 0.5, unconnected_centroids.size()+1, 1, 8);
			}	

			cv::flip(expanded_drawing,expanded_drawing,0);
//*/
			
	////////////////////////
//			cv::Mat croppedRef(Occ_image, resize_rect);			
			resize_rect.y=Occ_image.size().height - (resize_rect.y + resize_rect.height);// because of the flipping images
			resize_rect = resize_rect & Occ_Rect;
			
		
			cv::Mat croppedRef(Drawing , resize_rect);			
			cv::Mat croppedImage;
			croppedRef.copyTo(croppedImage);	
			
			end_process=clock();   elapsed_secs_process = double(end_process - begin_process) / CLOCKS_PER_SEC;			std::cerr<<"Time elapsed in process Paint "<< elapsed_secs_process*1000 << " ms"<<std::endl<<std::endl;

			grad = croppedImage;
//			grad = Drawing;
//			grad = Glued_Image;
//			grad = Drawing_Diff;
//			grad = expanded_drawing;
//			grad = stable_drawing;
//			grad = working_image;

//			grad = Occ_image;

	//////////////////////
	/////PUBLISH
			cv_ptr->encoding = "32FC1";
			grad.convertTo(grad, CV_32F);
			grad.copyTo(cv_ptr->image);////most important
			
//			publish_Contour();
	//////////
	/////Time
			clock_t end = clock();
			double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
			std::cout <<"Current Distance  " << distance << std::endl;
			std::cerr<<"Time elapsed  "<< elapsed_secs*1000 << " ms"<<std::endl<<std::endl;
			time_vector.push_back(elapsed_secs*1000);

			std::cerr<<"Time vector  "<< std::endl;			
			for(int i=0;i <time_vector.size();i++){
				std::cerr<<time_vector[i]<< std::endl;			
			}
		}

/////////////////////////
		void odomCallback(const nav_msgs::Odometry& msg){		
			robot_position_[0] =  msg.pose.pose.position.x;
			robot_position_[1] =  msg.pose.pose.position.y;
			
			cv::Point temp_Point(100*robot_position_[0], 100*robot_position_[1]);
			if(path_.size()>0) distance += cv::norm(temp_Point - position_cm_);

			path_.push_back(temp_Point);
			position_cm_ = temp_Point;
			/*
			 distance=0;
			for(int i=1; i < path_.size(); i++){
				distance += cv::norm(path_[i] - path_[i-1]);
			}

			// */
//			cout<< "Current distance "<< distance*0.01 << endl;
		}
		
/////////////////		
		void metronomeCallback(const ros::TimerEvent&)
		{
//		  ROS_INFO("tic tac");
		  publish_Image();
//		  publish_Contour();
		}


////////////////////////
// PUBLISHING METHODS		
////////////////////////////		
		void publish_Image(){
			image_pub_.publish(cv_ptr->toImageMsg());
//			cv::imshow("OPENCV_WINDOW", cv_ptr->image);
//			cv::waitKey(3);
		}

////////////////////////////
		void publish_Contour(){

			visualization_msgs::Marker marker;
			
			marker.header.frame_id = "map";
			marker.header.stamp = ros::Time();
			marker.ns = "my_namespace";
			marker.id = 0;
			marker.type = visualization_msgs::Marker::POINTS;
			marker.action = visualization_msgs::Marker::ADD;
			marker.pose.orientation.x = 0.0;
			marker.pose.orientation.y = 0.0;
			marker.pose.orientation.z = 0.0;
			marker.pose.orientation.w = 1.0;
			marker.scale.x = 0.2;
			marker.scale.y = 0.2;
			marker.scale.z = 0.2;
			marker.color.a = 1.0; // Don't forget to set the alpha!
			marker.color.r = 0.0;
			marker.color.g = 0.0;
			marker.color.b = 1.0;
			
			
			for(int i=0;i< Convex_Marker_.size();i++){
				for(int j=0;j< Convex_Marker_[i].size();j++){

					geometry_msgs::Point point;

					point.z = 0;//.1*j;				
					
					point.x = Convex_Marker_[i].at(j).x;
					point.y = Convex_Marker_[i].at(j).y;

					point.x -= Map_Info_.width/2;
					point.y -= Map_Info_.height/2;

					point.x *= Map_Info_.resolution;
					point.y *= -Map_Info_.resolution;

					marker.points.push_back(point);
//					std::cout <<"Points X:  "<< Voronoi_Marker_[i].at(j).x + myROI_.x<<"   Y:  "<< Voronoi_Marker_[i].at(j).y + myROI_.y << std::endl;
//					std::cout <<"Points X:  "<< point.x <<"   Y:  "<< point.y << std::endl;
				}
			}
			geometry_msgs::Point point;	
			markers_pub_.publish(marker);
		}


/////////////////////////
//// UTILITY
/////////////////////////

		cv::Mat clean_image3(cv::Mat Occ_Image, cv::Mat &black_image){
			//////////////////////////////	
			//Occupancy Image to Free Space	
			std::cout << "Cleaning Image..... "; 		double start_cleaning = getTime();
			cv::Mat open_space = Occ_Image<10;
			black_image = Occ_Image>90 & Occ_Image<=100;		
			cv::Mat Median_Image, Image_in, cut_image ;
			{
				cout << "Entering........ ";
				cv::dilate(black_image, black_image, cv::Mat(), cv::Point(-1,-1), 4, cv::BORDER_CONSTANT, cv::morphologyDefaultBorderValue() );			
				cout << "dilated........ ";
				cv::medianBlur(open_space, Median_Image, 15);
				cout << "Median Blur........ ";
				Image_in = Median_Image & ~black_image;
				cout << "And........ ";
				Image_in.copyTo(cut_image);			
				cout << "copy........ ";
			}
			double end_cleaning = getTime();  cout << "done, it last "<<(end_cleaning-start_cleaning)<< " ms"  << endl;	
			return cut_image;
		}


		cv::Mat clean_image(cv::Mat Occ_Image, cv::Mat &black_image){
			//Occupancy Image to Free Space	
			cv::Mat open_space = Occ_Image<10;
			black_image = Occ_Image>90 & Occ_Image<=100;		
			cv::Mat Median_Image, out_image, temp_image ;

			cv::dilate(black_image, black_image, cv::Mat(), cv::Point(-1,-1), 4, cv::BORDER_CONSTANT, cv::morphologyDefaultBorderValue() );			// inflate obstacle

			int filter_size=8;
			cv::boxFilter(open_space, temp_image, -1, cv::Size(filter_size, filter_size), cv::Point(-1,-1), false, cv::BORDER_DEFAULT ); // filter open_space
			Median_Image = temp_image > filter_size*filter_size/2;  // threshold in filtered
			out_image = Median_Image & ~black_image;// Open space without obstacles

			return out_image;
		}



		void are_contours_connected(vector<cv::Point> first_contour, vector<cv::Point> second_contour, cv::Point &centroid, int &number_of_ones ){
			
			vector< cv::Point > closer_point;
			cv::Point acum(0,0);
			int threshold=2;
			
			for(int i=0; i<first_contour.size();i++){
				for(int j=0; j< second_contour.size();j++){
					float distance;
					distance = cv::norm(first_contour[i] -  second_contour[j] );
					if(distance < threshold){
						cv::Point point_to_add;
						point_to_add.x = (first_contour[i].x + second_contour[j].x)/2;
						point_to_add.y = (first_contour[i].y + second_contour[j].y)/2;
						
						closer_point.push_back(point_to_add);
						acum += point_to_add;						
					 }					
				}
			}

			number_of_ones = closer_point.size();
			centroid.x = acum.x/number_of_ones;
			centroid.y = acum.y/number_of_ones;
		}
		
		void adjust_stable_contours(){
			int a;
			cout<<"Adjusting Contours "<< endl << endl;

			cv::Point correction;
			
// considering constant resolution
			correction.x = (current_origin_.position.x - Map_Info_.origin.position.x) / Map_Info_.resolution;
			correction.y = (current_origin_.position.y - Map_Info_.origin.position.y) / Map_Info_.resolution;
			
			for(int i=0; i < Stable.Region_contour.size();i++){
				Stable.Region_centroid[i] += correction;
				for(int j=0; j < Stable.Region_contour[i].size();j++){
					Stable.Region_contour[i][j] += correction;
//					Stable.Region_contour[i][j] = cartesian_to_pixel(pixel_to_cartesian(Stable.Region_contour[i][j]));
				}
			}
				
			current_origin_ = Map_Info_.origin;
			
			
		}
		
		cv::Point pixel_to_cartesian(cv::Point point_in){
			cv::Point point_out;
			point_out.x = point_in.x * Map_Info_.resolution + current_origin_.position.x;
			point_out.y = point_in.y * Map_Info_.resolution + current_origin_.position.y;
			
			return point_out;
		}
		
		cv::Point cartesian_to_pixel(cv::Point point_in){
			cv::Point point_out;
			point_out.x = (point_in.x - Map_Info_.origin.position.x) / Map_Info_.resolution ;
			point_out.y = (point_in.y - Map_Info_.origin.position.y) / Map_Info_.resolution ;			
			
			
			return point_out;
		}
};










int main(int argc, char **argv)
{

  ros::init(argc, argv, "Dual_Decomposer");
  
  std::string mapname = "map";
  
  float decomp_th=3;
  if (argc ==2){ decomp_th = atof(argv[1]); }


  


	ROS_handler mg(mapname, decomp_th);
//	ros::NodeHandle n;
	

//	ros::Subscriber sub = n.subscribe("chatter", 1000, chatterCallback);
// to create a subscriber, you can do this (as above):
//  ros::Subscriber subPC = n.subscribe<sensor_msgs::PointCloud2> ("camera/depth/points", 1, callback);
  ros::spin();

  return 0;
}
