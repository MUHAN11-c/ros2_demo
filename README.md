# ros2_demo

ROS 2 学习工作空间（服务客户端：Future 等待 vs callback；回调阻塞与执行器说明）。

## 包

| 包 | 说明 |
|----|------|
| `my_package` | C++：`my_node`（常驻 + callback）、`client_future`（脚本 Future） |
| `topic_demo` | Python：`py_topic`、`client_future` |
| `ros_tutorials/turtlesim` | turtlesim（教程依赖） |

## 文档

- [`docs/ROS2_回调阻塞与执行器问题.md`](docs/ROS2_回调阻塞与执行器问题.md)

## 构建与运行

```bash
cd ros_ws   # 或本仓库根目录
source /opt/ros/jazzy/setup.bash   # 按本机发行版调整
colcon build --packages-select my_package topic_demo
source install/setup.bash

# 终端1
ros2 run my_package my_node
# 终端2
ros2 run my_package client_future
```
