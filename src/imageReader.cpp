#include "imageReader.hpp"

using json = nlohmann::json;

namespace cam_cad {

ImageReader::ImageReader(){}

bool ImageReader::readPoints (std::string filename_, std::vector<point>* points_) {
    std::ifstream input_stream;
    input_stream.open(filename_);
    json input;
    std::string input_string = " ";

    if (input_stream.is_open()) {
        std::cout << "openned input stream" << std::endl;
        input_stream >> input;
        input_string = input.dump(); 
    }
    else {
        std::cout << "failed to open file:" << filename_ << std::endl; 
        return false;
    }

    //parse input string for points

    //jump to start of points section of JSON
    uint16_t read_index = input_string.find("points");
    input_string = input_string.substr(read_index);

    //add all points to point vector
    uint16_t num_points = 0;
    point current_point; 
    current_point.x = 0;
    current_point.y = 0;
    read_index = 0;
    uint16_t index_ticker = 0;

    while (input_string.at(read_index) != '}') {
        std::string scoord = "";
        while (std::isdigit(input_string.at(read_index))) {
            scoord = scoord + input_string.at(read_index);
            read_index ++;
        }

        if (scoord != "") {
            if (index_ticker ==0) current_point.x = std::stoi(scoord);
            else if (index_ticker == 1) current_point.y = std::stoi(scoord);
            index_ticker ++;
        }

        if (index_ticker == 2) {
            index_ticker = 0;
            points_->push_back(current_point);
            point temp_point = points_->back();
            num_points++;
        }

        if (!std::isdigit(input_string.at(read_index))) read_index ++;

    }

    return true;

}

void ImageReader::scalePoints (std::vector<point>* points_, float scale_) {
    //scale points based on image scale (for CAD images)
    uint16_t num_points = points_->size(); 

    //pop each point out of the vector, update value and push to back to retain order of the vector 
    for (uint16_t point_index = 0; point_index < num_points; point_index ++) {
        point current_point = points_->at(0);

        points_->erase(points_->begin());
        current_point.x *= scale_; 
        current_point.y *= scale_;

        points_->push_back(current_point);
    }
    
}

void ImageReader::densifyPoints (std::vector<point>* points_, uint8_t density_index_) {
    //add additional point between existing points according to scale
    //will help to converge solution
    //want to maintain order of vector for display and intuitive purposes
    uint16_t init_length = points_->size();
 

    for (uint16_t point_index = 0; point_index < init_length; point_index ++) {
        point current_start_point = points_->at(0);
        point current_end_point = points_->at(1);

        points_->erase(points_->begin());

        //determine angle between points 
        float slope, theta;

        if ((current_end_point.x-current_start_point.x) == 0) {
            theta = M_PI/2;
        }
        else if ((current_end_point.y-current_start_point.y) == 0) {
            theta = 0;
        }
        else {
            slope = (float)(current_end_point.y-current_start_point.y)/(float)(current_end_point.x-current_start_point.x);
            theta = std::atan(std::abs(slope));
        }

        //determine distance between points 
        float dist = std::sqrt(std::pow(std::abs(current_end_point.x-current_start_point.x),2) 
                               + std::pow(std::abs(current_end_point.y-current_start_point.y),2));

        //number of points added between each reference point should be the same for both images for 1:1 mapping
        float interval = dist/(density_index_+1);

        //determine delta x and y values based on quadrant
        float dx, dy; 

        //first quadrant: 
        if ((current_end_point.y-current_start_point.y) >= 0 && (current_end_point.x-current_start_point.x) >= 0) {
            dx = interval*(std::cos(theta)); 
            dy = interval*(std::sin(theta));
        }
        //second quadrant 
        else if ((current_end_point.y-current_start_point.y) >= 0 && (current_end_point.x-current_start_point.x) < 0) {
            dx = -interval*(std::cos(theta)); 
            dy = interval*(std::sin(theta));
        }
        //third quadrant 
        else if ((current_end_point.y-current_start_point.y) < 0 && (current_end_point.x-current_start_point.x) <= 0) {
            dx = -interval*(std::cos(theta)); 
            dy = -interval*(std::sin(theta));
        }
        //fourth quadrant 
        else if ((current_end_point.y-current_start_point.y) < 0 && (current_end_point.x-current_start_point.x) > 0) {
            dx = interval*(std::cos(theta)); 
            dy = -interval*(std::sin(theta));
        }

        //push the start point first to conserve the order of the vector 
        points_->push_back(current_start_point); 

        //push the rest of the interpolated points, trending toward the current end point 
        uint16_t current_x_coord = current_start_point.x + dx; 
        uint16_t current_y_coord = current_start_point.y + dy;

        for (uint8_t i = 0; i < density_index_; i++) {
            point current_inter_point(current_x_coord,current_y_coord);
            points_->push_back(current_inter_point);

            current_x_coord += dx;
            current_y_coord += dy;
        }

    }

}

void ImageReader::populateCloud (std::vector<point>* points_, pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_, uint16_t init_z_pos_) {
    
    uint16_t num_points = points_->size();

    for (uint16_t point_index = 0; point_index < num_points; point_index ++) {
        pcl::PointXYZ current_3D_point(points_->at(point_index).x, points_->at(point_index).y, init_z_pos_);
        cloud_->push_back(current_3D_point);
    }
}

void ImageReader::originCloudxy (pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_) {
    
    uint16_t num_points = cloud_->size();

    // determine min x and y values
    uint32_t min_x = 100000, min_y = 100000;
    for (uint16_t point_index = 0; point_index < num_points; point_index ++) {
        if (cloud_->at(point_index).x < min_x) min_x = cloud_->at(point_index).x;
        if (cloud_->at(point_index).y < min_y) min_y = cloud_->at(point_index).y;
    }

    // shift all points back to abutt origin
    for (uint16_t point_index = 0; point_index < num_points; point_index ++) {
        cloud_->at(point_index).x -= min_x;
        cloud_->at(point_index).y -= min_y;
    }

}

void ImageReader::rotateCWxy(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_) {
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

}