#include "util.h"

namespace cam_cad {

Util::Util() {
    center_image_called_ = false;
}

void Util::getCorrespondences(pcl::CorrespondencesPtr corrs_, 
                              pcl::PointCloud<pcl::PointXYZ>::ConstPtr source_coud_,
                              pcl::PointCloud<pcl::PointXYZ>::ConstPtr target_cloud_,
                              uint16_t max_dist_) {

    pcl::registration::CorrespondenceEstimation<pcl::PointXYZ, pcl::PointXYZ> corr_est;
    corr_est.setInputSource(source_coud_);
    corr_est.setInputTarget(target_cloud_);
    corr_est.determineCorrespondences(*corrs_,max_dist_);

}

void Util::CorrEst (pcl::PointCloud<pcl::PointXYZ>::ConstPtr CAD_cloud_,
                        pcl::PointCloud<pcl::PointXYZ>::ConstPtr camera_cloud_,
                        Eigen::Matrix4d &T_,
                        pcl::CorrespondencesPtr corrs_, 
                        std::string offset_type_) {

    pcl::PointCloud<pcl::PointXYZ>::Ptr proj_cloud (new pcl::PointCloud<pcl::PointXYZ>); 
    pcl::PointCloud<pcl::PointXYZ>::Ptr trans_cloud (new pcl::PointCloud<pcl::PointXYZ>);

    // transform the CAD cloud points to the camera frame
    trans_cloud = this->TransformCloud(CAD_cloud_,T_);

    // project the transformed points to the camera plane
    proj_cloud = this->ProjectCloud(trans_cloud);

    // merge centroids for correspondence estimation (projected -> camera)
    pcl::PointXYZ camera_centroid = Util::GetCloudCentroid(camera_cloud_);
    pcl::PointXYZ proj_centroid = Util::GetCloudCentroid(proj_cloud);

    // merge centers for correspondence estimation (projected -> camera)
    pcl::PointXYZ camera_center = Util::GetCloudCenter(camera_cloud_);
    pcl::PointXYZ proj_center = Util::GetCloudCenter(proj_cloud);

    Eigen::Vector3d offset; 
    
    // offset using centroid
    if (offset_type_ == "centroid") {
        offset(0) = camera_centroid.x - proj_centroid.x;
        offset(1) = camera_centroid.y - proj_centroid.y;
        offset(2) = camera_centroid.z - proj_centroid.z;
    }
    // offset using center 
    else if (offset_type_ == "center") {
        offset(0) = camera_center.x - proj_center.x;
        offset(1) = camera_center.y - proj_center.y;
        offset(2) = camera_center.z - proj_center.z;
    }
    else { 
        offset(0) = 0;
        offset(1) = 0;
        offset(2) = 0;
    }


    Util::OffsetCloud(proj_cloud, offset);

    // get correspondences
    this->getCorrespondences(corrs_, proj_cloud, camera_cloud_, 1000);

}

pcl::PointCloud<pcl::PointXYZ>::Ptr Util::TransformCloud (
    pcl::PointCloud<pcl::PointXYZ>::ConstPtr cloud_, Eigen::Matrix4d &T_) {

    pcl::PointCloud<pcl::PointXYZ>::Ptr trans_cloud (new pcl::PointCloud<pcl::PointXYZ>);
    
    for(uint16_t i=0; i < cloud_->size(); i++) {
        Eigen::Vector4d point (cloud_->at(i).x, cloud_->at(i).y, cloud_->at(i).z, 1);
        Eigen::Vector4d point_transformed = T_*point; 
        pcl::PointXYZ pcl_point_transformed (point_transformed(0), 
            point_transformed(1), point_transformed(2));
        trans_cloud->push_back(pcl_point_transformed);
    }

    return trans_cloud;

}

void Util::TransformCloudUpdate (pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_, 
                                 Eigen::Matrix4d &T_) {
    
    for(uint16_t i=0; i < cloud_->size(); i++) {
        Eigen::Vector4d point (cloud_->at(i).x, cloud_->at(i).y, 
            cloud_->at(i).z, 1);
        Eigen::Vector4d point_transformed = T_*point; 
        pcl::PointXYZ pcl_point_transformed (point_transformed(0), 
            point_transformed(1), point_transformed(2));
        cloud_->at(i) = pcl_point_transformed;
    }

}

pcl::PointCloud<pcl::PointXYZ>::Ptr Util::ProjectCloud 
    (pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_) {

    pcl::PointCloud<pcl::PointXYZ>::Ptr proj_cloud (new pcl::PointCloud<pcl::PointXYZ>);
    
    for(uint16_t i=0; i < cloud_->size(); i++) {
        Eigen::Vector3d point (cloud_->at(i).x, cloud_->at(i).y, cloud_->at(i).z);
        std::optional<Eigen::Vector2d> pixel_projected;
        pixel_projected = camera_model->ProjectPointPrecise(point);
        if (pixel_projected.has_value()) {
            pcl::PointXYZ proj_point (pixel_projected.value()(0), 
                pixel_projected.value()(1), 0);
            proj_cloud->push_back(proj_point);
        }
        
    }

    return proj_cloud;

}

Eigen::Matrix4d Util::QuaternionAndTranslationToTransformMatrix
    (const std::vector<double>& pose_) {

    Eigen::Quaternion<double> quaternion{pose_[0], pose_[1], pose_[2], pose_[3]};
    Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
    T.block(0, 0, 3, 3) = quaternion.toRotationMatrix();
    T(0, 3) = pose_[4];
    T(1, 3) = pose_[5];
    T(2, 3) = pose_[6];
    return T;
}

std::vector<double> Util::TransformMatrixToQuaternionAndTranslation
    (const Eigen::Matrix4d& T_) {

  Eigen::Matrix3d R = T_.block(0, 0, 3, 3);
  Eigen::Quaternion<double> q = Eigen::Quaternion<double>(R);
  std::vector<double> pose{q.w(),   q.x(),   q.y(),  q.z(),
                           T_(0, 3), T_(1, 3), T_(2, 3)};
}

std::shared_ptr<beam_calibration::CameraModel> Util::GetCameraModel () {
    return camera_model;
}

void Util::ReadCameraModel (std::string intrinsics_file_path_) {
    camera_model = beam_calibration::CameraModel::Create(intrinsics_file_path_); 
}

void Util::SetCameraID (uint8_t cam_ID_){
    camera_model->SetCameraID(cam_ID_);
    
}

Eigen::Matrix4d Util::PerturbTransformRadM(const Eigen::Matrix4d& T_in_,
                                     const Eigen::VectorXd& perturbations_) {
  Eigen::Vector3d r_perturb = perturbations_.block(0, 0, 3, 1);
  Eigen::Vector3d t_perturb = perturbations_.block(3, 0, 3, 1);
  Eigen::Matrix3d R_in = T_in_.block(0, 0, 3, 3);
  Eigen::Matrix3d R_out = LieAlgebraToR(r_perturb) * R_in;
  Eigen::Matrix4d T_out;
  T_out.setIdentity();
  T_out.block(0, 3, 3, 1) = T_in_.block(0, 3, 3, 1) + t_perturb;
  T_out.block(0, 0, 3, 3) = R_out;
  return T_out;
}

Eigen::Matrix4d Util::PerturbTransformDegM(const Eigen::Matrix4d& T_in_,
                                     const Eigen::VectorXd& perturbations_) {
  Eigen::VectorXd perturbations_rad(perturbations_);
  perturbations_rad[0] = DegToRad(perturbations_rad[0]);
  perturbations_rad[1] = DegToRad(perturbations_rad[1]);
  perturbations_rad[2] = DegToRad(perturbations_rad[2]);
  return PerturbTransformRadM(T_in_, perturbations_rad);
}

Eigen::MatrixXd Util::RoundMatrix(const Eigen::MatrixXd& M_, const int& precision_) {
  Eigen::MatrixXd Mround(M_.rows(), M_.cols());
  for (int i = 0; i < M_.rows(); i++) {
    for (int j = 0; j < M_.cols(); j++) {
      Mround(i, j) = std::round(M_(i, j) * std::pow(10, precision_)) /
                     std::pow(10, precision_);
    }
  }
  return Mround;
}

void Util::originCloudxy (pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_) {
    
    uint16_t num_points = cloud_->size();

    // determine central x and y values
    float max_x = 0, max_y = 0, min_x = 2048, min_y = 2048;
    for (uint16_t point_index = 0; point_index < num_points; point_index ++) {
        if (cloud_->at(point_index).x > max_x) max_x = cloud_->at(point_index).x; 
        if (cloud_->at(point_index).y > max_y) max_y = cloud_->at(point_index).y; 

        if (cloud_->at(point_index).x < min_x) min_x = cloud_->at(point_index).x; 
        if (cloud_->at(point_index).y < min_y) min_y = cloud_->at(point_index).y;
    }

    float center_x = min_x + (max_x-min_x)/2;
    float center_y = min_y + (max_y-min_y)/2;

    image_offset_x_ = center_x;
    image_offset_y_ = center_y;

    // shift all points back to center on origin
    for (uint16_t point_index = 0; point_index < num_points; point_index ++) {
        cloud_->at(point_index).x -= (int)center_x;
        cloud_->at(point_index).y -= (int)center_y;
    }

    center_image_called_ = true;

}

void Util::OffsetCloudxy (pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_) {
    if (!center_image_called_) {
        printf("FAILED to restore image offset - originCloudxy not previously called by this utility");
        return;
    }

    // restore offset to all points 
    for (uint16_t point_index = 0; point_index < cloud_->size(); point_index ++) {
        cloud_->at(point_index).x += (int)image_offset_x_;
        cloud_->at(point_index).y += (int)image_offset_y_;
    }

}

void Util::rotateCCWxy(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_) {
    // determine max x,y values
    uint32_t max_x = 0, max_y = 0;
    for (uint16_t point_index = 0; point_index < cloud_->size(); point_index ++) {
        if (cloud_->at(point_index).x > max_x) max_x = cloud_->at(point_index).x;
        if (cloud_->at(point_index).y > max_y) max_y = cloud_->at(point_index).y;
    }

    uint32_t min_x = 100000, min_y = 100000;
    for (uint16_t point_index = 0; point_index < cloud_->size(); point_index ++) {
        if (cloud_->at(point_index).x < min_x) min_x = cloud_->at(point_index).x;
        if (cloud_->at(point_index).y < min_y) min_y = cloud_->at(point_index).y;
    }
    
    for (uint16_t index = 0; index < cloud_->size(); index ++) {
        cloud_->at(index).x = max_x - cloud_->at(index).x + min_x;
        uint16_t x_tmp = cloud_->at(index).x;
        cloud_->at(index).x = cloud_->at(index).y;
        cloud_->at(index).y = x_tmp; 
    }
}

void Util::GetCloudScale(pcl::PointCloud<pcl::PointXYZ>::ConstPtr cloud_, 
    const double max_x_dim_, const double max_y_dim_, 
    float& x_scale_, float& y_scale_) {
    
    // get max cloud dimensions in x and y
    float max_x = 0, max_y = 0;
    float min_x = cloud_->at(0).x, min_y = cloud_->at(0).y;
    for(uint16_t point_index = 0; point_index < cloud_->size(); point_index++) {
        if (cloud_->at(point_index).x > max_x) max_x = cloud_->at(point_index).x;
        if (cloud_->at(point_index).y > max_y) max_y = cloud_->at(point_index).y;
        if (cloud_->at(point_index).x < min_x) min_x = cloud_->at(point_index).x;
        if (cloud_->at(point_index).y < min_y) min_y = cloud_->at(point_index).y;
    }

    // CAD unit/pixel 
    x_scale_ = max_x_dim_/(max_x-min_x);
    y_scale_ = max_y_dim_/(max_y-min_y);

}

void Util::ScaleCloud (pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_, float scale_) {
    for (uint16_t i = 0; i < cloud_->size(); i++) {
        cloud_->at(i).x *= scale_;
        cloud_->at(i).y *= scale_;
        cloud_->at(i).z *= scale_;
    }
}

pcl::PointCloud<pcl::PointXYZ>::Ptr Util::ScaleCloud 
    (pcl::PointCloud<pcl::PointXYZ>::ConstPtr cloud_, float scale_) {

    pcl::PointCloud<pcl::PointXYZ>::Ptr scaled_cloud (new pcl::PointCloud<pcl::PointXYZ>);

    for (uint16_t i = 0; i < cloud_->size(); i++) {
        pcl::PointXYZ to_add;
        to_add.x = cloud_->at(i).x * scale_;
        to_add.y = cloud_->at(i).y * scale_;
        to_add.z = cloud_->at(i).z * scale_;
        scaled_cloud->push_back(to_add);
    }

    return scaled_cloud;

}

void Util::ScaleCloud (pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_, 
                        float x_scale_, float y_scale_) {
    for (uint16_t i = 0; i < cloud_->size(); i++) {
        cloud_->at(i).x *= x_scale_;
        cloud_->at(i).y *= y_scale_;
    }
}

void Util::LoadInitialPose (std::string file_name_, 
                            Eigen::Matrix4d &T_, bool inverted_) {
    // load file
    nlohmann::json J;
    std::ifstream file(file_name_);
    file >> J;

    // initial pose
    double w_initial_pose [6]; // x, y, z, alpha, beta, gamma
    double c_initial_pose [6]; // x, y, z, alpha, beta, gamma 

    w_initial_pose[0] = J["pose"][0];
    w_initial_pose[1] = J["pose"][1];
    w_initial_pose[2] = J["pose"][2];
    w_initial_pose[3] = J["pose"][3];
    w_initial_pose[4] = J["pose"][4];
    w_initial_pose[5] = J["pose"][5];

    // remap translations and rotations
    RemapWorldtoCameraCoords(w_initial_pose, c_initial_pose);

    if (inverted_ == false) {
        // load forward transform
        Eigen::VectorXd perturbation(6, 1);
        perturbation << -c_initial_pose[3], 0, 0, 0, 0, 0; 
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, -c_initial_pose[4], 0, 0, 0, 0;
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, 0, -c_initial_pose[5], 0, 0, 0;
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, 0, 0, -c_initial_pose[0], 
            -c_initial_pose[1], -c_initial_pose[2];
        T_ = PerturbTransformDegM(T_, perturbation); 
    }
    else {
        // load inverse transform
        Eigen::VectorXd perturbation(6, 1);
        perturbation << c_initial_pose[3], 0, 0, 0, 0, 0; 
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, c_initial_pose[4], 0, 0, 0, 0;
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, 0, c_initial_pose[5], 0, 0, 0;
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, 0, 0, c_initial_pose[0], 
            c_initial_pose[1], c_initial_pose[2];
        T_ = PerturbTransformDegM(T_, perturbation); 
    }

}

void Util::TransformPose (std::string file_name_, 
    Eigen::Matrix4d &T_, bool inverted_) {
    // load file
    nlohmann::json J;
    std::ifstream file(file_name_);
    file >> J;
    
    // initial pose
    double w_pose [6]; // x, y, z, alpha, beta, gamma
    double c_pose [6]; // x, y, z, alpha, beta, gamma 

    w_pose[0] = J["pose"][0];
    w_pose[1] = J["pose"][1];
    w_pose[2] = J["pose"][2];
    w_pose[3] = J["pose"][3];
    w_pose[4] = J["pose"][4];
    w_pose[5] = J["pose"][5];

    // remap translations and rotations
    RemapWorldtoCameraCoords(w_pose, c_pose);

    // construct the matrix describing the transformation 
    // from the world to the camera frame
    if (inverted_) {
        Eigen::VectorXd perturbation(6, 1);
        perturbation << -c_pose[3], 0, 0, 0, 0, 0; 
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, -c_pose[4], 0, 0, 0, 0;
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, 0, -c_pose[5], 0, 0, 0;
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, 0, 0, -c_pose[0], -c_pose[1], -c_pose[2];
        T_ = PerturbTransformDegM(T_, perturbation); 
    }
    else { 
        Eigen::VectorXd perturbation(6, 1);
        perturbation << c_pose[3], 0, 0, 0, 0, 0; 
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, c_pose[4], 0, 0, 0, 0;
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, 0, c_pose[5], 0, 0, 0;
        T_ = PerturbTransformDegM(T_, perturbation); 
        perturbation << 0, 0, 0, c_pose[0], c_pose[1], c_pose[2];
        T_ = PerturbTransformDegM(T_, perturbation); 
    }


}

void Util::RemapWorldtoCameraCoords (const double (&world_transform_)[6], 
                                     double (&camera_transform_)[6]) {
    // just a rotation
    camera_transform_[0] = -world_transform_[1]; // y -> -x
    camera_transform_[1] = -world_transform_[2]; // z -> -y
    camera_transform_[2] = world_transform_[0]; // x -> z
    camera_transform_[4] = world_transform_[5]; // beta -> alpha
    camera_transform_[5] = -world_transform_[6]; // gamma -> 
    camera_transform_[6] = world_transform_[4]; // alpha -> gamma
}

pcl::ModelCoefficients::Ptr Util::GetCloudPlane
    (pcl::PointCloud<pcl::PointXYZ>::ConstPtr cloud_) {

    pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
    pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
    // Create the segmentation object
    pcl::SACSegmentation<pcl::PointXYZ> seg;
    seg.setOptimizeCoefficients (true);
    seg.setModelType (pcl::SACMODEL_PLANE);
    seg.setMethodType (pcl::SAC_RANSAC);
    seg.setDistanceThreshold (0.01);

    seg.setInputCloud (cloud_);
    seg.segment (*inliers, *coefficients);

    return coefficients;

}

pcl::PointCloud<pcl::PointXYZ>::Ptr Util::BackProject
    (pcl::PointCloud<pcl::PointXYZ>::ConstPtr image_cloud_, 
     pcl::PointCloud<pcl::PointXYZ>::ConstPtr cad_cloud_, 
     pcl::ModelCoefficients::ConstPtr target_plane_) {

    pcl::PointCloud<pcl::PointXYZ>::Ptr back_projected_cloud 
        (new pcl::PointCloud<pcl::PointXYZ>);

    // get cad surface normal and point on the cad plane
    Eigen::Vector3d cad_normal (target_plane_->values[0], 
        target_plane_->values[1], target_plane_->values[2]);
    Eigen::Vector3d cad_point (cad_cloud_->at(0).x, 
        cad_cloud_->at(0).y, cad_cloud_->at(0).z);


    printf ("BACK PROJECT: got plane normal and point \n");

    for (uint32_t i = 0; i < image_cloud_->size(); i++) {
        Eigen::Vector3d image_point (0,0,0);
        Eigen::Vector2i image_pixel (image_cloud_->at(i).x, image_cloud_->at(i).y);
        Eigen::Vector3d ray_unit_vector = camera_model->
            BackProject(image_pixel).value().normalized();
        double prod1 = (image_point - cad_point).dot(cad_normal);

        double len = prod1 / (ray_unit_vector.dot(cad_normal));

        Eigen::Vector3d back_projected_point = image_point - ray_unit_vector * len;

        pcl::PointXYZ back_projected_cloud_point (back_projected_point[0], 
            back_projected_point[1], back_projected_point[2]);

        back_projected_cloud->push_back(back_projected_cloud_point);

    }

    return back_projected_cloud;

}

pcl::PointXYZ Util::GetCloudCentroid(pcl::PointCloud<pcl::PointXYZ>::ConstPtr cloud_) {

    pcl::PointXYZ centroid; 

    pcl::computeCentroid(*cloud_, centroid);

    centroid.z = 0;

    return centroid;

}

pcl::PointXYZ Util::GetCloudCenter(pcl::PointCloud<pcl::PointXYZ>::ConstPtr cloud_) {

    uint16_t num_points = cloud_->size();

    pcl::PointXYZ center_point;

    // determine central x and y values
    float max_x = 0, max_y = 0, min_x = 2048, min_y = 2048;
    for (uint16_t point_index = 0; point_index < num_points; point_index ++) {
        if (cloud_->at(point_index).x > max_x) max_x = cloud_->at(point_index).x; 
        if (cloud_->at(point_index).y > max_y) max_y = cloud_->at(point_index).y; 

        if (cloud_->at(point_index).x < min_x) min_x = cloud_->at(point_index).x; 
        if (cloud_->at(point_index).y < min_y) min_y = cloud_->at(point_index).y;
    }

    float center_x = min_x + (max_x-min_x)/2;
    float center_y = min_y + (max_y-min_y)/2;

    center_point.x = center_x;
    center_point.y = center_y;
    center_point.z = 0;

    return center_point;

}

void Util::OffsetCloud(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_, Eigen::Vector3d offset_) {
    
    for (uint16_t i = 0; i < cloud_->size(); i++) {
        cloud_->at(i).x += offset_(0);
        cloud_->at(i).y += offset_(1);
        cloud_->at(i).z += offset_(2);
    }
}

Eigen::Matrix3d Util::LieAlgebraToR(const Eigen::Vector3d& eps) {
  return SkewTransform(eps).exp();
}

Eigen::Matrix3d Util::SkewTransform(const Eigen::Vector3d& V) {
  Eigen::Matrix3d M;
  M(0, 0) = 0;
  M(0, 1) = -V(2, 0);
  M(0, 2) = V(1, 0);
  M(1, 0) = V(2, 0);
  M(1, 1) = 0;
  M(1, 2) = -V(0, 0);
  M(2, 0) = -V(1, 0);
  M(2, 1) = V(0, 0);
  M(2, 2) = 0;
  return M;
}

double Util::DegToRad(double d) {
  return d * (M_PI / 180);
}

} // namespace cam_cad
