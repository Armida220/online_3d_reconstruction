#include <iostream>
#include <fstream>
#include <string>
#include <opencv2/opencv_modules.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/stitching/detail/matchers.hpp>
//#include <opencv2/calib3d.hpp>
//#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/point_cloud.h>
#include <pcl/console/parse.h>
#include <pcl/common/transforms.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/point_types.h>
//#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/registration/transformation_estimation.h>
#include <pcl/registration/transformation_estimation_svd.h>
#include <pcl/registration/transformation_estimation_svd_scale.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/features/normal_3d.h>
#include <pcl/registration/icp.h>
#include <pcl/surface/mls.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/surface/gp3.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/ModelCoefficients.h>
#include <pcl/filters/project_inliers.h>
#include <pcl/surface/concave_hull.h>
//#include <pcl/PCLPointCloud2.h>
#include <time.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <stdio.h>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/cudafeatures2d.hpp>
#include <thread>
#include <mutex>

using namespace std;
using namespace cv;
using namespace cv::detail;

typedef vector <double> record_t;
typedef vector <record_t> data_t;

class RawImageData {
public:
	int img_num;
	Mat rgb_image;
	Mat disparity_image;
	Mat segment_label;
	Mat double_disparity_image;
	
	double time;	//NSECS
	double tx;
	double ty;
	double tz;
	double qx;
	double qy;
	double qz;
	double qw;
};

//accepted images with secondary data
class ImageData {
public:
	RawImageData* raw_img_data_ptr;
	
	ImageFeatures features;	//has features.keypoints and features.descriptors
	cuda::GpuMat gpu_descriptors;
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr keypoints3D;
	vector<bool> keypoints3D_ROI_Points;
	
	pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 t_mat_MAVLink;
	pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 t_mat_FeatureMatched;
	
};

class Pose {

public:
Pose(int argc, char* argv[]);
// Default command line args
vector<int> img_numbers;
double minDisparity = 64;
int boundingBox = 20;
int rows = 0, cols = 0, cols_start_aft_cutout = 0;
int jump_pixels = 10;
int seq_len = -1;
int blur_kernel = 1;	//31 is a good number
double dist_nearby = 2;	//in meters
int good_matched_imgs = 0;
std::mutex mu;

bool mesh_surface = false;
bool smooth_surface = false;
int polynomial_order = 2;
double search_radius = 0.02;//, sqr_gauss_param = 0.02;
bool downsample = false;
unsigned int min_points_per_voxel = 1;

//image data
vector<RawImageData> rawImageDataVec;
vector<ImageData> acceptedImageDataVec;

//variables for k-D tree of UAV locations of accepted images
const int featureMatchingThreshold = 100;
const double z_threshold = 0.05;

double voxel_size = 0.1; //in meters
bool visualize = false;
bool align_point_cloud = false;
string read_PLY_filename0 = "";
string read_PLY_filename1 = "";
string currentDateTimeStr;
double focallength = 16.0 / 1000 / 3.75 * 1000000;
double baseline = 600.0 / 1000;
int cutout_ratio = 8;	//how much ratio of masking is to be done on left side of image as this area is not covered in stereo disparity images.
string calib_file = "cam13calib.yml";
Mat Q;
const string imageNumbersFile = "images/image_numbers.txt";
const string dataFilesPrefix = "data_files/";
const string pose_file = "pose.txt";
const string images_times_file = "images.txt";
const string heading_data_file = "hdg.txt";
const string imagePrefix = "/mnt/win/WORK/kentland19jul/22m_extracted_data/left_rect/";
const string disparityPrefix = "/mnt/win/WORK/kentland19jul/22m_extracted_data/disparities/";
const string segmentlblPrefix = "segmentlabels/";
string folder = "/mnt/win/WORK/pose_estimation_output/";

//indices in pose and heading data files
const int tx_ind=3,ty_ind=4,tz_ind=5,qx_ind=6,qy_ind=7,qz_ind=8,qw_ind=9;//,hdg_ind=3;
//translation and rotation between image and head of hexacopter
const double trans_x_hi = -0.300;
const double trans_y_hi = -0.040;
const double trans_z_hi = -0.350;
const double PI = 3.141592653589793238463;
const double theta_xi = -1.1408 * PI / 180;
const double theta_yi = 1.1945 * PI / 180;
bool only_MAVLink = false;
bool dont_downsample = false;
bool dont_icp = false;

//PROCESS: get times in NSECS from images_times_data and search for corresponding or nearby entry in pose_data and heading_data
data_t pose_data;		//header.seq,secs,NSECS,position.x,position.y,position.z,orientation.x,orientation.y,orientation.z,orientation.w
//data_t heading_data;	//header.seq,secs,NSECS,rostime,heading_in_degs
data_t images_times_data;	//header.seq,secs,NSECS
//vectors to store trigger times for easier searching
vector<double> images_times_seq;
vector<double> pose_times_seq;
//vector<double> heading_times_seq;

bool displayUAVPositions = false;
bool wait_at_visualizer = true;
bool log_stuff = true;
bool preview = false;
float match_conf = 0.3f;
string save_log_to = "";
int range_width = 30;		//matching will be done between range_width number of sequential images.
bool use_segment_labels = false;
bool release = true;

bool segment_cloud = false;
bool segment_cloud_only = false;
double segment_dist_threashold = voxel_size;		//0.1
double convexhull_dist_threshold = 2.5 * voxel_size;	//0.25
double convexhull_alpha = 1.5 * voxel_size;				//0.15
//int size_cloud_divider = 10;				//10

ofstream log_file;	//logging stuff
Ptr<FeaturesFinder> finder;
Ptr<cuda::DescriptorMatcher> matcher = cv::cuda::DescriptorMatcher::createBFMatcher(cv::NORM_HAMMING);

//dumb variables -> try to remove them
pcl::PointCloud<pcl::PointXYZRGB>::Ptr hexPos_cloud;
int last_hexPos_cloud_points = 0;
boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer_online;
bool run3d_reconstruction = true;

bool test_bad_data_rejection = false;

//declaring functions
void readCalibFile();
void printUsage();
string type2str(int type);
int parseCmdArgs(int argc, char** argv);
void readPoseFile();
void populateData();
void createPtCloud(int img_index, pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb);
void createSingleImgPtCloud(int accepted_img_index, pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb);
void transformPtCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb, pcl::PointCloud<pcl::PointXYZRGB>::Ptr transformed_cloudrgb, pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 transform);
void createPlaneFittedDisparityImages(int i);
pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 generateTmat(int current_idx);
int binarySearchUsingTime(vector<double> seq, int l, int r, double time);
int binarySearchImageTime(int l, int r, int imageNumber);
int data_index_finder(int image_number);
void printPoints(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud, int num);
const string currentDateTime();
double getMean(Mat disp_img, bool planeFitted);
double getVariance(Mat disp_img, bool planeFitted);
boost::shared_ptr<pcl::visualization::PCLVisualizer> visualize_pt_cloud(bool showcloud, pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb, bool showmesh, pcl::PolygonMesh &mesh, string pt_cloud_name);
void visualize_pt_cloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb, string pt_cloud_name);
void visualize_pt_cloud_update(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb, string pt_cloud_name, boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer);
pcl::PointCloud<pcl::PointXYZRGB>::Ptr read_PLY_File(string point_cloud_filename);
void save_pt_cloud_to_PLY_File(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudrgb, string &writePath);
pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 generate_tf_of_Matched_Keypoints(ImageData &currentImageDataObj, bool &acceptDecision);
pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 runICPalignment(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_in, pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_out);
pcl::PointCloud<pcl::PointXYZRGB>::Ptr downsamplePtCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloudrgb, bool combinedPtCloud);
void orbcudaPairwiseMatching();
void smoothPtCloud();
void meshSurface();
pcl::PointXYZRGB generateUAVpos(int current_idx);
pcl::PointXYZRGB transformPoint(pcl::PointXYZRGB hexPosMAVLink, pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 T_SVD_matched_pts);
void populateDisparityImages(int start_index, int end_index);
void readDisparityImage(int i);
void populateSegmentLabelMaps(int start_index, int end_index);
void readSegmentLabelMap(int i);
void populateImages(int start_index, int end_index);
void readImage(int i);
void populateDoubleDispImages(int start_index, int end_index);
void displayPointCloudOnline(pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud_combined_copy, 
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud_hexPos_FM, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud_hexPos_MAVLink, int cycle, bool last_cycle);
void createAndTransformPtCloud(int accepted_img_index, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloudrgb_return);
void findNormalOfPtCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud);
ImageData findFeatures(int img_idx);
int generate_Matched_Keypoints_Point_Cloud
	(ImageData &currentImageDataObj, 
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr &current_img_matched_keypoints, 
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr &fitted_cloud_matched_keypoints);
void segmentCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloudrgb_orig);
pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 basicBundleAdjustmentErrorCalculator
			(pcl::PointCloud<pcl::PointXYZRGB>::Ptr current_img_matched_keypoints, pcl::PointCloud<pcl::PointXYZRGB>::Ptr fitted_cloud_matched_keypoints,
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_current_inliers, pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_prior_inliers,
			pcl::registration::TransformationEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Matrix4 T_SVD_matched_pts, double threshold,
			double &avg_inliers_err, int &inliers);
double distanceCalculator(RawImageData* img_obj_ptr_src, RawImageData* img_obj_ptr_dst);



};
