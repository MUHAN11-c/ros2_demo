# my_package 服务客户端两种写法对照

详细场景与解法见：

→ [ROS2_回调阻塞与执行器问题.md](../../docs/ROS2_回调阻塞与执行器问题.md)

## 快速对照

| 可执行文件 | 写法 | 探测服务 |
|------------|------|----------|
| `my_node` | 常驻 `spin` + callback | `service_is_ready()` |
| `client_future` | `spin_until_future_complete` | `wait_for_service` |

```bash
source ~/robot_workspaces/ros_ws/install/setup.bash
ros2 run my_package my_node          # 终端1
ros2 run my_package client_future    # 终端2
```
