// Copyright 2022 Chen Jun
// Licensed under the MIT License.

// OpenCV
#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/dnn/all_layers.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

// STL
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "armor_detector/armor_detector.hpp"
#include "armor_detector/number_classifier.hpp"

namespace rm_auto_aim
{
NumberClassifier::NumberClassifier(
  const std::string & model_path, const std::string & label_path, const double thre)
: threshold(thre)
{
  net_ = cv::dnn::readNetFromONNX(model_path);

  std::ifstream label_file(label_path);
  std::string line;
  while (std::getline(label_file, line)) {
    class_names_.push_back(line[0]);
  }
}

void NumberClassifier::extractNumbers(const cv::Mat & src, std::vector<Armor> & armors)
{
  // Height scaling factor
  const float height_factor = 2.0;
  // Image height after wrap
  const int warp_height = 28;
  // Image height after wrap
  /// Large armor
  const float large_warp_width = 50;
  /// Small armor
  const float small_warp_width = 35;
  // Number ROI size
  const cv::Size roi_size(20, 28);

  for (auto & armor : armors) {
    // Scaling height
    auto left_height_diff = armor.left_light.bottom - armor.left_light.top;
    auto right_height_diff = armor.right_light.bottom - armor.right_light.top;

    auto left_center = (armor.left_light.top + armor.left_light.bottom) / 2;
    auto right_center = (armor.right_light.top + armor.right_light.bottom) / 2;

    auto top_left = left_center - left_height_diff / 2 * height_factor;
    auto top_right = right_center - right_height_diff / 2 * height_factor;
    auto bottom_left = left_center + left_height_diff / 2 * height_factor;
    auto bottom_right = right_center + right_height_diff / 2 * height_factor;

    // Warp perspective transform
    cv::Point2f number_vertices[4] = {bottom_left, top_left, top_right, bottom_right};
    const int warp_width = armor.armor_type == LARGE ? large_warp_width : small_warp_width;
    cv::Point2f target_vertices[4] = {
      cv::Point(0, warp_height - 1),
      cv::Point(0, 0),
      cv::Point(warp_width - 1, 0),
      cv::Point(warp_width - 1, warp_height - 1),
    };
    cv::Mat number_image;
    auto rotation_matrix = cv::getPerspectiveTransform(number_vertices, target_vertices);
    cv::warpPerspective(src, number_image, rotation_matrix, cv::Size(warp_width, warp_height));

    // Get ROI
    number_image =
      number_image(cv::Rect(cv::Point((warp_width - roi_size.width) / 2, 0), roi_size));

    // Binarize
    cv::cvtColor(number_image, number_image, cv::COLOR_RGB2GRAY);
    cv::threshold(number_image, number_image, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    armor.number_img = number_image;
  }
}

void NumberClassifier::fcClassify(std::vector<Armor> & armors)
{
  for (auto & armor : armors) {
    cv::Mat image = armor.number_img.clone();

    // Normalize
    image = image / 255.0;

    // Create blob from image
    cv::Mat blob;
    cv::dnn::blobFromImage(image, blob, 1., cv::Size(28, 20), cv::Scalar(0), false, false);

    // Set the input blob for the neural network
    net_.setInput(blob);
    // Forward pass the image blob through the model
    cv::Mat outputs = net_.forward();

    // Do softmax
    float max_prob = *std::max_element(outputs.begin<float>(), outputs.end<float>());
    cv::Mat softmax_prob;
    cv::exp(outputs - max_prob, softmax_prob);
    float sum = static_cast<float>(cv::sum(softmax_prob)[0]);
    softmax_prob /= sum;

    double confidence;
    cv::Point class_id_point;
    minMaxLoc(softmax_prob.reshape(1, 1), nullptr, &confidence, nullptr, &class_id_point);
    int label_id = class_id_point.x;

    armor.confidence = confidence;
    armor.number = class_names_[label_id];

    std::stringstream result_ss;
    result_ss << armor.number << ":_" << std::fixed << std::setprecision(1)
              << armor.confidence * 100.0 << "%";
    armor.classfication_result = result_ss.str();

    armors.erase(
      std::remove_if(
        armors.begin(), armors.end(),
        [this](const Armor & armor) { return armor.confidence < threshold; }),
      armors.end());
  }
}
}  // namespace rm_auto_aim
