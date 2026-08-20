// =============================================================================
// 【学习 · 常驻节点 + callback 客户端】my_node.cpp
//
// 机制：
//   main 里 rclcpp::spin(node) 长期运行，驱动 timer / 订阅 / 服务 / 客户端响应。
//   客户端用 async_send_request(req, callback)：发完立刻返回，结果到了再进回调。
//
// 对照：
//   client_future.cpp = 单独程序 + wait_for_service + spin_until_future_complete
//
// 注意：
//   本文件已有 spin，禁止再 spin_until_future_complete（嵌套 spin / 易死锁）。
//   同节点既当服务端又当客户端时，必须用 callback（或 MultiThreaded+Reentrant）。
// =============================================================================

#include <chrono>
#include <cinttypes>
#include <functional>
#include <memory>
#include <string>

#include "example_interfaces/srv/add_two_ints.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;
using std::placeholders::_1;  // std::bind 占位符：回调第 1 个参数
using std::placeholders::_2;  // 服务回调第 2 个参数（response）
using AddTwoInts = example_interfaces::srv::AddTwoInts;

class MyNode : public rclcpp::Node
{
public:
  explicit MyNode(const std::string & name)
  : Node(name), count_(0), request_sent_(false)
  {
    // API: create_publisher<Msg>(话题名, QoS/队列深度)
    publisher_ = this->create_publisher<std_msgs::msg::String>("topic", 10);

    // ---------- 写法对照：订阅回调 ----------
    // A) lambda（适合短逻辑）
    // subscriber_ = this->create_subscription<std_msgs::msg::String>(
    //   "topic", 10,
    //   [this](const std_msgs::msg::String::SharedPtr msg) {
    //     RCLCPP_INFO(this->get_logger(), "Received: %s", msg->data.c_str());
    //   });
    // B) std::bind + 成员函数（适合长逻辑、可复用）
    //    _1 = 消息参数；必须绑 this，否则成员函数无法调用
    subscriber_ = this->create_subscription<std_msgs::msg::String>(
      "topic", 10, std::bind(&MyNode::subscriber_callback, this, _1));

    // ---------- 写法对照：定时器 ----------
    // A) lambda: create_wall_timer(1s, [this]() { ... });
    // B) bind:   create_wall_timer(1s, std::bind(&MyNode::timer_callback, this));
    // wall_timer = 墙上时钟（真实时间）；仿真时钟场景再学 use_sim_time
    timer_ = this->create_wall_timer(1s, std::bind(&MyNode::timer_callback, this));

    // ---------- 写法对照：服务端 ----------
    // A) lambda 两参数 (request, response)，填 response，void 返回
    // B) bind 成员函数（本例）
    // API: create_service<SrvType>(服务名, 回调)
    // 注意：服务名必须与客户端完全一致；回调返回后框架自动把 response 发回
    service_ = this->create_service<AddTwoInts>(
      "add_two_ints", std::bind(&MyNode::add_two_ints_callback, this, _1, _2));

    // API: create_client<SrvType>(服务名) —— 只创建句柄，不代表服务端已上线
    client_ = this->create_client<AddTwoInts>("add_two_ints");
  }

  void timer_callback()
  {
    auto message = std_msgs::msg::String();
    message.data = "Hello, ROS2! " + std::to_string(count_++);
    publisher_->publish(message);
    RCLCPP_INFO(this->get_logger(), "Published: %s", message.data.c_str());

    // 只发一次，避免刷屏；响应由 spin 触发进 callback
    if (!request_sent_) {
      send_client_request_callback();
      request_sent_ = true;
    }
  }

  void subscriber_callback(const std_msgs::msg::String::SharedPtr msg)
  {
    RCLCPP_INFO(this->get_logger(), "Received: %s", msg->data.c_str());
  }

  void add_two_ints_callback(
    const std::shared_ptr<AddTwoInts::Request> request,
    std::shared_ptr<AddTwoInts::Response> response)
  {
    // 业务：写 response；不要自己 publish
    response->sum = request->a + request->b;
    // PRId64：跨平台打印 int64_t（不要用 %d）
    RCLCPP_INFO(
      this->get_logger(),
      "Service got: %" PRId64 " + %" PRId64 " -> %" PRId64,
      request->a, request->b, response->sum);
  }

  // =========================================================================
  // 【写法 B · callback 客户端】适合：外面已经有 spin 的常驻节点
  //
  // API 流程：
  //   1) service_is_ready()     —— 非阻塞，查一眼服务在不在
  //   2) make_shared<Request>() —— 请求要在堆上，async 持有
  //   3) async_send_request(req, cb) —— 发完立刻返回；cb 在响应到达时由 spin 调用
  //
  // 与 wait_for_service 区别（重要）：
  //   service_is_ready()  : 立刻返回 bool，不卡住当前回调（适合 timer 里轮询）
  //   wait_for_service(t) : 最多阻塞 t，直到上线或超时（适合启动阶段循环等待）
  //   常驻节点的 timer/回调里优先 service_is_ready；单独脚本用 while+wait_for_service
  // =========================================================================
  void send_client_request_callback()
  {
    // 非阻塞探测：服务还没起来就跳过，下次 timer 再试
    if (!client_->service_is_ready()) {
      RCLCPP_WARN(this->get_logger(), "service_is_ready()==false, skip; retry next tick");
      request_sent_ = false;
      return;
    }

    auto request = std::make_shared<AddTwoInts::Request>();
    request->a = 1;
    request->b = 2;

    // SharedFuture：响应就绪后 get() 取结果；这里在 lambda 里取，不阻塞本函数
    client_->async_send_request(
      request,
      [this](rclcpp::Client<AddTwoInts>::SharedFuture future) {
        auto result = future.get();
        RCLCPP_INFO(
          this->get_logger(),
          "[callback] Result: %" PRId64, result->sum);
      });

    RCLCPP_INFO(this->get_logger(), "[callback] request sent (non-blocking)");
  }

private:
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscriber_;
  rclcpp::Client<AddTwoInts>::SharedPtr client_;
  rclcpp::Service<AddTwoInts>::SharedPtr service_;
  size_t count_;
  bool request_sent_;
};

int main(int argc, char ** argv)
{
  // API: init → 创建节点 → spin（阻塞）→ shutdown
  rclcpp::init(argc, argv);
  auto node = std::make_shared<MyNode>("my_node");
  // spin：处理所有就绪回调，直到 Ctrl+C / shutdown
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
