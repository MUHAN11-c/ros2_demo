# =============================================================================
# 【学习 · Future 等待客户端】client_future.py
#
# 机制：
#   无长期 spin。wait_for_service → call_async → spin_until_future_complete
#   → result() → 退出。
#
# 对照：my_topic.py = 常驻 spin + service_is_ready + add_done_callback
#
# 运行：先 ros2 run topic_demo py_topic，再本程序。
# =============================================================================

import sys

import rclpy
from example_interfaces.srv import AddTwoInts


def main(args=None):
    rclpy.init(args=args)
    node = rclpy.create_node('client_future')
    client = node.create_client(AddTwoInts, 'add_two_ints')

    # =========================================================================
    # wait_for_service(timeout_sec)
    #   阻塞最多 timeout_sec；服务出现 True，超时 False
    #   适合启动阶段 while 轮询
    # service_is_ready()
    #   立刻返回当前是否可用，不阻塞
    #   适合已在 spin 的 timer/回调里（见 my_topic.py）
    # =========================================================================
    while not client.wait_for_service(timeout_sec=1.0):
        if not rclpy.ok():
            node.get_logger().error('Interrupted while waiting for service.')
            node.destroy_node()
            rclpy.shutdown()
            return 1
        node.get_logger().info('wait_for_service(1.0) timed out, retry...')
    node.get_logger().info('service ready (wait_for_service returned True)')

    request = AddTwoInts.Request()
    request.a = 41
    request.b = 1

    # =========================================================================
    # 【写法 A · Future】
    # call_async(request)  —— 非阻塞发送
    # spin_until_future_complete(node, future, timeout_sec)
    #   —— 本程序唯一的 spin：边处理回调边等 Future
    # future.done() / future.result()
    #   —— 判断完成；取结果（未完成时 result 可能抛异常，故先 done）
    #
    # 注意：外面若已有 rclpy.spin(node)，不要再用本写法
    # =========================================================================
    future = client.call_async(request)
    rclpy.spin_until_future_complete(node, future, timeout_sec=5.0)

    if future.done():
        try:
            result = future.result()
        except Exception as e:
            node.get_logger().error('Service call failed: %r' % (e,))
            node.destroy_node()
            rclpy.shutdown()
            return 1
        node.get_logger().info(
            '[future] %d + %d = %d' % (request.a, request.b, result.sum))
    else:
        node.get_logger().error('spin_until_future_complete timeout')
        node.destroy_node()
        rclpy.shutdown()
        return 1

    node.destroy_node()
    rclpy.shutdown()
    return 0


if __name__ == '__main__':
    sys.exit(main())
