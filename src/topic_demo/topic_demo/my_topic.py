# =============================================================================
# 【学习 · 常驻节点 + callback 客户端】my_topic.py
#
# 机制：
#   main 里 rclpy.spin(node) 长期运行。
#   客户端：call_async + add_done_callback，发完立刻返回。
#
# 对照：
#   client_future.py = wait_for_service + spin_until_future_complete
#
# 注意：
#   已有 spin，不要再 spin_until_future_complete。
#   Python: call_async ≈ C++ async_send_request
#           add_done_callback ≈ C++ 传 callback 的 async_send_request
#           future.result() ≈ C++ future.get()
# =============================================================================

import rclpy
from rclpy.node import Node
from std_msgs.msg import String
from example_interfaces.srv import AddTwoInts


class MyTopic(Node):
    def __init__(self, name: str):
        super().__init__(name)
        self.count_ = 0
        self.request_sent_ = False

        # API: create_publisher(MsgType, topic, qos_depth)
        self.publisher_ = self.create_publisher(String, 'topic', 10)

        # ---------- 写法对照：订阅 ----------
        # A) lambda（短逻辑）
        # self.subscriber_ = self.create_subscription(
        #     String, 'topic',
        #     lambda msg: self.get_logger().info('Received: %s' % msg.data),
        #     10)
        # B) 成员函数（长逻辑、可复用）—— Python 直接传方法，无需 std::bind
        self.subscriber_ = self.create_subscription(
            String, 'topic', self.subscriber_callback, 10)

        # ---------- 写法对照：定时器 ----------
        # A) lambda: self.create_timer(1.0, lambda: ...)
        # B) 成员函数（本例）
        self.timer_ = self.create_timer(1.0, self.timer_callback)

        # ---------- 写法对照：服务端 ----------
        # API: create_service(SrvType, name, callback)
        # 回调签名 (request, response) -> response；必须 return response
        # A) lambda 也可以，但要注意必须返回 response
        self.service_ = self.create_service(
            AddTwoInts, 'add_two_ints', self.add_two_ints_callback)

        # API: create_client(SrvType, name) —— 只建句柄，不代表服务已上线
        self.client_ = self.create_client(AddTwoInts, 'add_two_ints')
        self.get_logger().info('MyTopic started (callback client style)')

    def subscriber_callback(self, msg: String):
        self.get_logger().info('Received: %s' % msg.data)

    def timer_callback(self):
        msg = String()
        msg.data = 'Hello, ROS2! ' + str(self.count_)
        self.publisher_.publish(msg)
        self.get_logger().info('Published: %s' % msg.data)
        self.count_ += 1

        if not self.request_sent_:
            self.send_client_request_callback()
            self.request_sent_ = True

    def add_two_ints_callback(self, request, response):
        response.sum = request.a + request.b
        self.get_logger().info(
            'Service got: %d + %d -> %d' % (request.a, request.b, response.sum))
        return response  # Python 必须 return；C++ 是写进 response 指针即可

    # =========================================================================
    # 【写法 B · callback】
    # API:
    #   service_is_ready()     —— 非阻塞：现在能不能调？
    #   call_async(request)    —— 非阻塞发送，返回 Future
    #   future.add_done_callback(cb) —— 结果就绪时由 spin 调用 cb
    #
    # service_is_ready vs wait_for_service:
    #   service_is_ready()          : 立刻 bool，不卡住（适合 timer）
    #   wait_for_service(timeout)   : 阻塞最多 timeout（适合启动脚本）
    # =========================================================================
    def send_client_request_callback(self):
        if not self.client_.service_is_ready():
            self.get_logger().warn('service_is_ready()==False, skip; retry next tick')
            self.request_sent_ = False
            return

        request = AddTwoInts.Request()
        request.a = 1
        request.b = 2

        future = self.client_.call_async(request)
        future.add_done_callback(self.response_callback)
        self.get_logger().info('[callback] request sent (non-blocking)')

    def response_callback(self, future):
        try:
            result = future.result()
        except Exception as e:
            self.get_logger().error('Service call failed: %r' % (e,))
            return
        self.get_logger().info('[callback] Result: %d' % result.sum)


def main(args=None):
    rclpy.init(args=args)
    node = MyTopic('my_topic')
    # spin：处理所有回调，直到 Ctrl+C
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
