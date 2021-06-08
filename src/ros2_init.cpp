#include <stdexcept>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

#include "rclcpp/rclcpp.hpp"
#include "dynamic_graph_bridge/ros2_init.hh"

#include "dynamic_graph_bridge_msgs/msg/matrix.hpp"
#include "dynamic_graph_bridge_msgs/msg/vector.hpp"
#include "dynamic_graph_bridge_msgs/srv/run_command.hpp"
#include "dynamic_graph_bridge_msgs/srv/run_python_file.hpp"
#include <dynamic-graph/python/interpreter.hh>

namespace dynamicgraph {
  
struct GlobalRos {
  
  boost::shared_ptr<rclcpp::Node> nodeHandle;
  boost::shared_ptr<rclcpp::executors::MultiThreadedExecutor> mtExecutor;
};
  
GlobalRos ros;

class DualThreadedNode : public rclcpp::Node
{
public:
  DualThreadedNode()
    : Node("ros2_sot_interpreter_service"),
      interpreter_()
  {
    /* These define the callback groups
     * They don't really do much on their own, but they have to exist in order to
     * assign callbacks to them. They're also what the executor looks for when trying to run multiple threads
     */
    callback_group_subscriber1_ = this->create_callback_group(
      rclcpp::CallbackGroupType::MutuallyExclusive);
    callback_group_subscriber2_ = this->create_callback_group(
      rclcpp::CallbackGroupType::MutuallyExclusive);

    // Each of these callback groups is basically a thread
    // Everything assigned to one of them gets bundled into the same thread
    auto sub1_opt = rclcpp::SubscriptionOptions();
    sub1_opt.callback_group = callback_group_subscriber1_;
    auto sub2_opt = rclcpp::SubscriptionOptions();
    sub2_opt.callback_group = callback_group_subscriber2_;

    srv_run_cmd_ = this->create_service<dynamic_graph_bridge_msgs::srv::RunCommand>(
      "run_command",
      rclcpp::QoS(10),
      // std::bind is sort of C++'s way of passing a function
      // If you're used to function-passing, skip these comments
      std::bind(
        &DualThreadedNode::run_command_cb,  // First parameter is a reference to the function
        this,                               // What the function should be bound to
        std::placeholders::_1),             // At this point we're not positive of all the
                                            // parameters being passed
                                            // So we just put a generic placeholder
                                            // into the binder
                                            // (since we know we need ONE parameter)
      sub1_opt);                  // This is where we set the callback group.
                                  // This subscription will run with callback group subscriber1

    srv_run_py_file_ = this->create_service<dynamic_graph_bridge_msgs::srv::RunPythonFile>(
      "run_script",
      rclcpp::QoS(10),
      std::bind(
        &DualThreadedNode::run_script_cb,
        this,
        std::placeholders::_1),
      sub2_opt);
  }

private:
  /**
   * Simple function for generating a timestamp
   * Used for somewhat ineffectually demonstrating that the multithreading doesn't cripple performace
   */
  std::string timing_string()
  {
    rclcpp::Time time = this->now();
    return std::to_string(time.nanoseconds());
  }

  /**
   * Every time the Publisher publishes something, all subscribers to the topic get poked
   * This function gets called when Subscriber1 is poked (due to the std::bind we used when defining it)
   */
  void run_command_cb
  (const std::shared_ptr<dynamic_graph_bridge_msgs::srv::RunCommand::Request> request,
   std::shared_ptr<dynamic_graph_bridge_msgs::srv::RunCommand::Response> response)
  {
    interpreter_.python(req.input, res.result, res.standardoutput, res.standarderror);
  }

  /**
   * This function gets called when Subscriber2 is poked
   * Since it's running on a separate thread than Subscriber 1, it will run at (more-or-less) the same time!
   */
  void run_script_cb
  (const std::shared_ptr<dynamic_graph_bridge_msgs::srv::RunPythonFile::Request> request,
   std::shared_ptr<dynamic_graph_bridge_msgs::srv::RunPythonFile::Response> response)
  {
      interpreter_.runPythonFile(req.input);
      res.result = "File parsed";  // FIX: It is just an echo, is there a way to
      // have a feedback?
  }

  rclcpp::CallbackGroup::SharedPtr callback_group_subscriber1_;
  rclcpp::CallbackGroup::SharedPtr callback_group_subscriber2_;
  rclcpp::Service<dynamic_graph_bridge_msgs::srv::RunCommand>::SharedPtr srv_run_cmd_;
  rclcpp::Service<dynamic_graph_bridge_msgs::srv::RunPythonFile>::SharedPtr srv_run_py_file_;

  // interpreter
  python::Interpreter interpreter_;
};

rclcpp::Node& rosInit(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  // You MUST use the MultiThreadedExecutor to use, well, multiple threads
  rclcpp::executors::MultiThreadedExecutor executor;
  auto subnode = std::make_shared<DualThreadedNode>();  // This contains BOTH subscriber callbacks.
                                                        // They will still run on different threads
                                                        // One Node. Two callbacks. Two Threads
  executor.add_node(subnode);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}

}  // end of namespace dynamicgraph.
