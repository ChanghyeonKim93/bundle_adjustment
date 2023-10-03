#include <iostream>
#include <random>
#include <vector>

#include "core/hybrid_visual_odometry/pose_optimizer.h"
#include "core/util/timer.h"
#include "eigen3/Eigen/Dense"
#include "eigen3/Eigen/Geometry"
#include "opencv4/opencv2/core.hpp"
#include "opencv4/opencv2/highgui.hpp"
#include "opencv4/opencv2/imgproc.hpp"

void GeneratePoseOnlyBundleAdjustmentSimulationData(
    const size_t num_points, const Eigen::Isometry3f &pose_world_to_current,
    const size_t n_cols, const size_t n_rows, const float fx, const float fy,
    const float cx, const float cy,
    std::vector<Eigen::Vector3f> &true_world_position_list,
    std::vector<Eigen::Vector2f> &true_pixel_list,
    std::vector<Eigen::Vector3f> &world_position_list,
    std::vector<Eigen::Vector2f> &pixel_list) {
  // Generate 3D points and projections
  std::random_device rd;
  std::mt19937 gen(rd());

  const float z_default = 1.2f;
  const float z_deviation = 5.0f;
  const float x_deviation = 1.7f;
  const float y_deviation = 1.3f;
  const float pixel_error = 0.0f;
  std::uniform_real_distribution<float> dist_x(-x_deviation, x_deviation);
  std::uniform_real_distribution<float> dist_y(-y_deviation, y_deviation);
  std::uniform_real_distribution<float> dist_z(0, z_deviation);
  std::normal_distribution<float> dist_pixel(0, pixel_error);

  for (size_t index = 0; index < num_points; ++index) {
    Eigen::Vector3f world_position;
    world_position.x() = dist_x(gen);
    world_position.y() = dist_y(gen);
    world_position.z() = dist_z(gen) + z_default;

    const Eigen::Vector3f local_position =
        pose_world_to_current.inverse() * world_position;

    Eigen::Vector2f pixel;
    const float inverse_z = 1.0 / local_position.z();
    pixel.x() = fx * local_position.x() * inverse_z + cx;
    pixel.y() = fy * local_position.y() * inverse_z + cy;

    true_world_position_list.push_back(world_position);
    true_pixel_list.push_back(pixel);
  }

  for (size_t index = 0; index < num_points; ++index) {
    const Eigen::Vector2f &true_pixel = true_pixel_list[index];
    const Eigen::Vector3f &world_position = true_world_position_list[index];

    world_position_list.push_back(world_position);

    Eigen::Vector2f pixel = true_pixel;
    pixel.x() += dist_pixel(gen);
    pixel.y() += dist_pixel(gen);
    pixel_list.push_back(pixel);
  }
}

int main() {
  try {
    // Camera parameters
    const size_t n_cols = 640;
    const size_t n_rows = 480;
    const float fx = 338.0;
    const float fy = 338.0;
    const float cx = 320.0;
    const float cy = 240.0;

    Eigen::Isometry3f pose_world_to_current_frame;
    pose_world_to_current_frame.linear() =
        Eigen::AngleAxisf(-0.3, Eigen::Vector3f::UnitY()).toRotationMatrix();
    pose_world_to_current_frame.translation().x() = 0.4;
    pose_world_to_current_frame.translation().y() = 0.012;
    pose_world_to_current_frame.translation().z() = -0.5;

    // Generate 3D points and projections
    constexpr size_t num_points = 100000;
    std::vector<Eigen::Vector3f> true_world_position_list;
    std::vector<Eigen::Vector2f> true_pixel_list;
    std::vector<Eigen::Vector3f> world_position_list;
    std::vector<Eigen::Vector2f> pixel_list;
    GeneratePoseOnlyBundleAdjustmentSimulationData(
        num_points, pose_world_to_current_frame, n_cols, n_rows, fx, fy, cx, cy,
        true_world_position_list, true_pixel_list, world_position_list,
        pixel_list);

    // Make initial guess
    Eigen::Isometry3f pose_world_to_current_native_solver;
    Eigen::Isometry3f pose_world_to_current_initial_guess;
    pose_world_to_current_initial_guess = Eigen::Isometry3f::Identity();
    pose_world_to_current_initial_guess.translation().x() -= 0.2;
    pose_world_to_current_initial_guess.translation().y() -= 0.5;
    pose_world_to_current_native_solver = pose_world_to_current_initial_guess;

    // 1) native solver
    std::unique_ptr<visual_navigation::analytic_solver::PoseOptimizer>
        pose_optimizer = std::make_unique<
            visual_navigation::analytic_solver::PoseOptimizer>();
    visual_navigation::analytic_solver::Summary summary;
    visual_navigation::analytic_solver::Options options;
    options.iteration_handle.max_num_iterations = 100;
    options.convergence_handle.threshold_cost_change = 1e-6;
    options.convergence_handle.threshold_step_size = 1e-6;
    options.outlier_handle.threshold_huber_loss = 1.5;
    options.outlier_handle.threshold_outlier_rejection = 2.5;
    std::vector<bool> mask_inlier;
    pose_optimizer->SolveMonocularPoseOnlyBundleAdjustment6Dof(
        world_position_list, pixel_list, fx, fy, cx, cy,
        pose_world_to_current_native_solver, mask_inlier, options, &summary);
    std::cout << summary.BriefReport() << std::endl;

    // Compare results
    std::cout << "Compare pose:\n";

    Eigen::Matrix<float, 3, 4> pose_true;
    Eigen::Matrix<float, 3, 4> pose_initial_guess;
    Eigen::Matrix<float, 3, 4> pose_native_solver;

    pose_true << pose_world_to_current_frame.linear(),
        pose_world_to_current_frame.translation();
    std::cout << "truth:\n" << pose_true << std::endl;

    pose_initial_guess << pose_world_to_current_initial_guess.linear(),
        pose_world_to_current_initial_guess.translation();
    std::cout << "Initial guess:\n" << pose_initial_guess << std::endl;

    pose_native_solver << pose_world_to_current_native_solver.linear(),
        pose_world_to_current_native_solver.translation();
    std::cout << "Estimated (native solver):\n"
              << pose_native_solver << std::endl;

    // Draw images
    std::vector<Eigen::Isometry3f> debug_pose_list =
        pose_optimizer->GetDebugPoses();

    for (size_t iter = 0; iter < debug_pose_list.size(); ++iter) {
      const Eigen::Isometry3f &pose_world_to_current_temp =
          debug_pose_list[iter];
      std::vector<Eigen::Vector2f> projected_pixel_list;
      for (size_t index = 0; index < num_points; ++index) {
        const Eigen::Vector3f local_position =
            pose_world_to_current_temp.inverse() * world_position_list[index];

        Eigen::Vector2f pixel;
        const float inverse_z = 1.0 / local_position.z();
        pixel.x() = fx * local_position.x() * inverse_z + cx;
        pixel.y() = fy * local_position.y() * inverse_z + cy;

        projected_pixel_list.push_back(pixel);
      }

      cv::Mat image_blank = cv::Mat::zeros(cv::Size(n_cols, n_rows), CV_8UC3);
      for (size_t index = 0; index < pixel_list.size(); ++index) {
        const Eigen::Vector2f &pixel = pixel_list[index];
        const Eigen::Vector2f &projected_pixel = projected_pixel_list[index];
        cv::circle(image_blank, cv::Point2f(pixel.x(), pixel.y()), 4,
                   cv::Scalar(255, 0, 0), 1);
        cv::circle(image_blank,
                   cv::Point2f(projected_pixel.x(), projected_pixel.y()), 2,
                   cv::Scalar(0, 0, 255), 1);
      }
      cv::imshow("optimization process visualization", image_blank);
      cv::waitKey(0);
    }
  } catch (std::exception &e) {
    std::cout << "e.what(): " << e.what() << std::endl;
  }

  return 0;
};