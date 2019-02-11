//  Jan/2019, ETHZ, Jaeyoung Lim, jalim@student.ethz.ch

#include "hilbert_loco_planner/hilbert_loco_planner.h"

using namespace std;
//Constructor
HilbertLocoPlanner::HilbertLocoPlanner(const ros::NodeHandle& nh, const ros::NodeHandle& nh_private):
  nh_(nh),
  nh_private_(nh_private),
  loco_(3),
  hilbert_map_(nh, nh_private){

}
HilbertLocoPlanner::~HilbertLocoPlanner() {
  //Destructor
}

void HilbertLocoPlanner::setHilbertMap() {
    //TODO: bind this with hilbertmap

    loco_.setDistanceAndGradientFunction(
            std::bind(&HilbertLocoPlanner::getOccProbAndGradientVector, this,
                      std::placeholders::_1, std::placeholders::_2));
    loco_.setMapResolution(hilbert_map_.voxel_size());
}

bool HilbertLocoPlanner::getTrajectoryTowardGoal(const mav_msgs::EigenTrajectoryPoint& start,
                                            const mav_msgs::EigenTrajectoryPoint& goal,
                                            mav_trajectory_generation::Trajectory* trajectory){
    CHECK_NOTNULL(trajectory);
    trajectory->clear();
    mav_msgs::EigenTrajectoryPoint start_point = start;
    mav_msgs::EigenTrajectoryPoint goal_point = goal;

    // Check if we're already at the goal!
    constexpr double kGoalReachedRange = 0.01;
    if ((goal_point.position_W - start_point.position_W).norm() <
        kGoalReachedRange) {
        if (verbose_) {
            ROS_INFO("[Voxblox Loco Planner] Goal already reached!");
        }
        return true;
    }

    Eigen::Vector3d distance_to_waypoint =
            goal_point.position_W - start_point.position_W;
    double planning_distance =
            (goal_point.position_W - start_point.position_W).norm();
    Eigen::Vector3d direction_to_waypoint = distance_to_waypoint.normalized();

    if (planning_distance > planning_horizon_m_) {
        goal_point.position_W =
                start_point.position_W + planning_horizon_m_ * direction_to_waypoint;
        planning_distance = planning_horizon_m_;
    }

    bool success = getTrajectoryBetweenWaypoints(start_point, goal_point, trajectory);

    return success;
}

bool HilbertLocoPlanner::getTrajectoryTowardGoalFromInitialTrajectory(
        double start_time,
        const mav_trajectory_generation::Trajectory& trajectory_in,
        const mav_msgs::EigenTrajectoryPoint& goal,
        mav_trajectory_generation::Trajectory* trajectory) {
    mav_msgs::EigenTrajectoryPoint start;
    start_time = std::min(trajectory->getMaxTime(), start_time);
    bool success =  mav_trajectory_generation::sampleTrajectoryAtTime(trajectory_in, start_time, &start);
    if (!success) {
        return false;
    }
}

bool HilbertLocoPlanner::getTrajectoryBetweenWaypoints(
        const mav_msgs::EigenTrajectoryPoint& start,
        const mav_msgs::EigenTrajectoryPoint& goal,
        mav_trajectory_generation::Trajectory* trajectory) {
//    CHECK(esdf_map_);

    ROS_DEBUG_STREAM("[Voxblox Loco Planner] Start: "
                             << start.position_W.transpose()
                             << " goal: " << goal.position_W.transpose());

    constexpr double kTotalTimeScale = 1.2;
    double total_time =
            kTotalTimeScale * mav_trajectory_generation::computeTimeVelocityRamp(
                    start.position_W, goal.position_W,
                    constraints_.v_max, constraints_.a_max);

    // Check that start and goal aren't basically the same thing...
    constexpr double kMinTime = 0.01;
    if (total_time < kMinTime) {
        return false;
    }

    // If we're doing hotstarts, need to save the previous d_p.
    loco_.setupFromTrajectoryPoints(start, goal, num_segments_, total_time);
    Eigen::VectorXd x0, x;
    loco_.getParameterVector(&x0);
    x = x0;
    loco_.solveProblem();

    // Check if this path is collision-free.
    constexpr double kCollisionSamplingDt = 0.1;
    mav_msgs::EigenTrajectoryPoint::Vector path;
    bool success = false;
    int i = 0;
    for (i = 0; i < num_random_restarts_; i++) {
        loco_.getTrajectory(trajectory);
        mav_trajectory_generation::sampleWholeTrajectory(
                *trajectory, kCollisionSamplingDt, &path);
        success = isPathCollisionFree(path);
        if (success) {
            // Awesome, collision-free path.
            break;
        }

        // Otherwise let's do some random restarts.
        x = x0 + random_restart_magnitude_ * Eigen::VectorXd::Random(x.size());
        loco_.setParameterVector(x);
        loco_.solveProblem();
    }

    if (success) {
        // TODO(helenol): Retime the trajectory!
    }

    if (verbose_) {
        ROS_INFO("[Voxblox Loco Planner] Found solution (%d) after %d restarts.",
                 success, i);
    }
    return success;
}

double HilbertLocoPlanner::getOccProb(const Eigen::Vector3d& position) const {
    double occprob = 0.0;
    if (!hilbert_map_.getOccProbAtPosition(position, occprob)) {
        return 0.0;
    }
    return occprob;
}

double HilbertLocoPlanner::getOccProbAndGradient(const Eigen::Vector3d& position, Eigen::Vector3d* gradient) const {
    double occprob = 0.0;
    if(!hilbert_map_.getOccProbAndGradientAtPosition(position, occprob, gradient)){
        return 0.0;
    }
    return occprob;

}

double HilbertLocoPlanner::getOccProbAndGradientVector(const Eigen::VectorXd& position, Eigen::VectorXd* gradient) const {
    CHECK_EQ(position.size(), 3);
    if (gradient == nullptr) {
        return getOccProbAndGradient(position, nullptr);
    }
    Eigen::Vector3d gradient_3d;
    double occprob = getOccProbAndGradient(position, &gradient_3d);
    *gradient = gradient_3d;
    return occprob;
}

bool HilbertLocoPlanner::isPathCollisionFree(const mav_msgs::EigenTrajectoryPointVector& path) const {
    return false;
}
bool HilbertLocoPlanner::isPathFeasible(const mav_msgs::EigenTrajectoryPointVector& path) const {
    return true;
}