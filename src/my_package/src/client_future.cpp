// =============================================================================
// 【学习 · Future 等待客户端】client_future.cpp
//
// 机制：
//   本程序没有长期 rclcpp::spin。
//   wait_for_service 等服务上线 → async_send_request → spin_until_future_complete
//   等结果 → get → 退出。
//
// 对照：
//   my_node.cpp = 常驻 spin + service_is_ready + callback
//
// 运行：先 ros2 run my_package my_node，再本程序。
// =============================================================================

#include <chrono>
#include <cinttypes>
#include <memory>

#include "example_interfaces/srv/add_two_ints.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;
using AddTwoInts = example_interfaces::srv::AddTwoInts;

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  // 脚本式节点：不继承类也可以 Node::make_shared
  auto node = rclcpp::Node::make_shared("client_future");
  auto client = node->create_client<AddTwoInts>("add_two_ints");

  // =========================================================================
  // wait_for_service(timeout)
  //   - 阻塞最多 timeout，服务出现返回 true，超时返回 false
  //   - 适合：启动阶段「一直等到能调」
  // service_is_ready()
  //   - 立刻返回当前是否可用，不阻塞
  //   - 适合：已在 spin 的 timer/回调里「查一下再决定发不发」
  // 本文件是脚本：用 while + wait_for_service 轮询
  // =========================================================================
  while (!client->wait_for_service(1s)) {
    if (!rclcpp::ok()) {
      RCLCPP_ERROR(node->get_logger(), "Interrupted while waiting for service.");
      return 1;
    }
    RCLCPP_INFO(node->get_logger(), "wait_for_service(1s) timed out, retry...");
  }
  RCLCPP_INFO(node->get_logger(), "service ready (wait_for_service returned true)");

  auto request = std::make_shared<AddTwoInts::Request>();
  request->a = 41;
  request->b = 1;

  // =========================================================================
  // 【写法 A · Future】
  // async_send_request(req)  —— 非阻塞发送，返回 Future
  // spin_until_future_complete(node, future, timeout)
  //   —— 本程序唯一的 spin：边处理 ROS 回调，边等 Future
  //   —— 返回 SUCCESS / TIMEOUT / INTERRUPTED
  // future.get()             —— SUCCESS 后再取结果；未完成时 get 会阻塞
  // remove_pending_request   —— 失败时清掉未完成请求，避免泄漏
  //
  // 注意：外面若已有 spin(node)，不要再用本写法（见 my_node.cpp）
  // =========================================================================
  auto result_future = client->async_send_request(request);

  auto ret = rclcpp::spin_until_future_complete(node, result_future, 5s);
  if (ret != rclcpp::FutureReturnCode::SUCCESS) {
    RCLCPP_ERROR(node->get_logger(), "spin_until_future_complete failed");
    client->remove_pending_request(result_future);
    rclcpp::shutdown();
    return 1;
  }

  auto result = result_future.get();
  RCLCPP_INFO(
    node->get_logger(),
    "[future] %" PRId64 " + %" PRId64 " = %" PRId64,
    request->a, request->b, result->sum);

  rclcpp::shutdown();
  return 0;
}
