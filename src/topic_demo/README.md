# topic_demo 服务客户端两种写法对照（Python）

详细场景与解法见：

→ [ROS2_回调阻塞与执行器问题.md](../../docs/ROS2_回调阻塞与执行器问题.md)

## 快速对照

| 可执行文件 | 写法 | 探测服务 |
|------------|------|----------|
| `py_topic` | 常驻 `spin` + `add_done_callback` | `service_is_ready()` |
| `client_future` | `spin_until_future_complete` | `wait_for_service` |

```bash
source ~/robot_workspaces/ros_ws/install/setup.bash
ros2 run topic_demo py_topic
ros2 run topic_demo client_future
```
