cmake_minimum_required(VERSION 3.8)
project(my_package)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(example_interfaces REQUIRED)

# 常驻节点：pub/sub/timer/service + callback 客户端
add_executable(my_node src/my_node.cpp)
ament_target_dependencies(my_node rclcpp std_msgs example_interfaces)

# 单独客户端：Future + spin_until_future_complete
add_executable(client_future src/client_future.cpp)
ament_target_dependencies(client_future rclcpp example_interfaces)

install(TARGETS my_node client_future
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
