# ROS 2 热门项目怎么处理「等结果 / 卡死」问题

> 零基础友好版：先认名词，再看 **❌ 错例 / ✅ 正例**（每行 API 都写清「干什么」）。  
> 练习：`my_package`（C++）、`topic_demo`（Python）。

---

## 0. 先认这些词（看不懂代码就先翻这里）

| 名词 | 一句话是什么 | 生活类比 |
|------|--------------|----------|
| **节点 Node** | 你写的一个 ROS 程序单元（一个进程里常有一个） | 一家店的前台 |
| **话题 Topic** | 广播：谁爱听谁订；发了不保证对方马上处理 | 店里广播喇叭 |
| **服务 Service** | 一问一答：发请求，等对方算完给回复 | 打电话点菜，对方报「好了」 |
| **客户端 Client** | 打电话的那一方（发请求） | 顾客 |
| **服务端 Server** | 接电话、算完写回复的那一方 | 厨房 |
| **回调 callback** | 「某件事发生时，框架自动调用的函数」 | 电话铃响 → 你接起来说的那段话 |
| **定时器 Timer** | 每隔固定时间自动调一次你的函数 | 闹钟 |
| **执行器 / spin** | 循环：有就绪的回调就执行；没 spin，回调几乎不会跑 | 前台一直在岗接待 |
| **Future** | 「以后才有结果」的收据；先拿到收据，结果到了再取 | 取餐号 |
| **阻塞 / 干等** | 当前函数卡住不返回，别的回调进不来 | 拿着电话不挂，门口没人管 |
| **非阻塞 / 异步** | 发完立刻返回，结果以后用回调再处理 | 留言后挂电话，门铃响了再开门 |

### 最常用 API 词典（背这几条就够看懂后面例子）

| API（C++ / Python 写法接近） | 干什么 | 会不会卡住当前函数 |
|------------------------------|--------|--------------------|
| `rclcpp::init` / `rclpy.init` | 启动 ROS 客户端库，程序开头调用一次 | 否 |
| `create_node` / `Node(...)` | 创建一个节点（前台开张） | 否 |
| `create_client(服务类型, 服务名)` | 做一个「打电话的话筒」；**不等于**对方已上线 | 否 |
| `create_service(服务类型, 服务名, 回调)` | 开一个服务：有人打电话就进你的回调 | 否 |
| `wait_for_service(超时)` | **站着等**对方开门，最多等这么久；超时返回 false | **会**（最多等超时时间） |
| `service_is_ready()` | **扫一眼**对方现在能不能接；立刻给 true/false | **不会** |
| `async_send_request` / `call_async` | 把请求发出去，立刻返回一个 Future（取餐号） | **发的时候不会等结果** |
| `spin_until_future_complete(node, future, 超时)` | **边值班边盯着取餐号**，直到有结果或超时；适合**单独脚本** | **会**（占着这个程序等） |
| `async_send_request(req, 回调)` / `future.add_done_callback(回调)` | 发请求 + 登记：「结果到了再自动调这个函数」 | 发完立刻返回 |
| `future.get()` / `future.result()` | 从 Future 里取出真正的回复内容 | 若结果还没到，**可能卡住**（所以只在「已完成」时取） |
| `rclcpp::spin(node)` / `rclpy.spin(node)` | **一直值班**：不停处理定时器、订阅、服务、客户端回调 | **会一直停在这行**（直到 Ctrl+C） |
| `rclcpp::ok()` / `rclpy.ok()` | ROS 还在正常跑吗？（例如有没有 Ctrl+C） | 否 |

> **一条铁律：**  
> 程序里**已经**在 `spin(node)` 了 → 不要再在回调里用 `spin_until_future_complete`。  
> 单独小脚本、没有长期 spin → 可以用 `wait_for_service` + `spin_until_future_complete`。

---

## 先记住一句话

节点像**一个前台服务员**：电话（服务）、点菜（话题）、闹钟（定时器）都靠他。  
如果他拿着电话**干等 10 分钟**，门口排队全停。

热门项目规矩：

> **前台只做短事；要等很久 → 记一笔账，有结果再回来处理。**

---

## 1. Nav2：走到桌子旁再抓

### 你想做的事

走到桌子 → 到了 → 再抓取。

### ❌ 错例（按钮回调里死等）

```python
# on_start_button = 「开始按钮」被按下时，框架自动调用的回调函数
def on_start_button(msg):
    # goToPose：给导航发「去这个点」的目标（类似发 Action 目标）
    navigator.goToPose(table_pose)

    # isTaskComplete：问导航「这次任务做完了吗？」→ True/False
    # while + pass：什么也不干，一直转圈等到 True —— 这就是阻塞
    # 问题：这段时间 spin 没空处理激光、急停等其它回调
    while not navigator.isTaskComplete():
        pass

    # start_grasping：开始抓取（可能永远轮不到急停回调）
    start_grasping()
```

### ✅ Nav2 式正例（发完就走 + 短轮询）

```python
# state：自己记的「现在做到哪一步了」（状态机）
# IDLE=闲着, GOING=正在去, ARRIVED=到了, GRASP=在抓
state = 'IDLE'

def on_start_button(msg):
    global state
    # 只负责下单：发导航目标后立刻返回，不在这里死等
    navigator.goToPose(table_pose)   # API：发目标（不等到站）
    state = 'GOING'                  # 记一笔账：「正在路上」

# on_timer：定时器回调，例如每 0.1 秒来一次（像行为树「拍一下」）
def on_timer():
    global state
    if state == 'GOING':
        # 还没到：本拍直接 return，把时间让给其它回调
        if not navigator.isTaskComplete():
            return
        state = 'ARRIVED'            # 到了，改状态
    if state == 'ARRIVED':
        start_grasping()             # 到了才抓
        state = 'GRASP'
```

**读法：** 按钮回调只负责「下单」；定时器负责「到了没？到了再抓」。  
中间 `spin` 还能处理激光、急停。

---

## 2. MoveIt：开爪 → 下降 → 闭合（顺序任务）

### 你想做的事

必须先开爪成功，再下降（顺序不能乱）。

### ❌ 错例（一个函数里卡住等）

```cpp
void do_pick() {
  // 下面三个都是「伪代码函数名」，意思是函数内部会 wait 好几秒
  open_gripper_and_wait();   // 打开夹爪，并且干等直到成功
  descend();                 // 下降
  close_gripper_and_wait();  // 闭合夹爪，再干等
  // 常驻节点里这样写：整个 spin 被卡住，其它回调进不来
}
```

### ✅ 脚本可以干等（像 MoveIt 教程）

```cpp
// main：程序入口。这种「跑一遍就退出」的脚本可以阻塞
int main() {
  // setPoseTarget：设置机械臂目标位姿（想去哪里）
  move_group.setPoseTarget(pose);
  // move()：规划并运动；教程脚本里会等动完才返回（阻塞 OK）
  move_group.move();
  return 0;  // 事情做完，进程结束
}
```

### ✅ 常驻节点用状态机（像正式抓取任务）

```cpp
// enum：枚举，列出任务可能处在的几个阶段
enum class Phase { Open, WaitOpen, Descend, Close, Done };
Phase phase_ = Phase::Open;  // 当前阶段，从「开爪」开始

void on_timer() {  // 定时器每次滴答调用；必须短、不能阻塞
  switch (phase_) {  // 根据当前阶段决定这一拍做什么

    case Phase::Open:
      // service_is_ready()：立刻问「夹爪服务现在能调吗？」
      // 返回 false → 本拍跳过，下拍再试（不卡住）
      if (!gripper_client_->service_is_ready()) return;

      {
        // make_shared<Request>()：在堆上新建一份「请求报文」
        auto req = std::make_shared<Gripper::Request>();
        req->command = "open";  // 请求内容：打开夹爪

        // async_send_request(请求, 回调函数)：
        //   1) 马上把请求发出去
        //   2) 本函数立刻继续往下跑（不等回复）
        //   3) 对方回复到达后，spin 会自动调用后面的 lambda
        gripper_client_->async_send_request(req, [this](auto future) {
          // future.get()：取出回复；这里只在「结果已到」的回调里取，安全
          if (future.get()->success)
            phase_ = Phase::Descend;  // 开爪成功 → 下一阶段改为「下降」
        });
      }
      phase_ = Phase::WaitOpen;  // 先进入「等开爪回执」
      break;

    case Phase::WaitOpen:
      break;  // 本拍什么都不做；等上面的回调把 phase_ 改掉

    case Phase::Descend:
      send_descend_command();  // 发下降命令（实际项目也常用 Action/异步）
      phase_ = Phase::Close;
      break;

    case Phase::Close:
      /* 同样用 async_send_request 闭合夹爪 */
      phase_ = Phase::Done;
      break;

    case Phase::Done:
      break;
  }
}
```

**读法：**  
业务上：开爪 → 下降 → 闭合（有顺序）。  
代码上：每一拍只做一点点，**从不在回调里 sleep / wait**。

---

## 3. ros2_control：电机环 vs 配置服务

### ❌ 错例（控制环里等服务）

```cpp
// update()：控制环函数，假设每 1ms 必须跑完一次
void update() {
  // wait_for_service(100ms)：最多干等 100 毫秒等服务上线
  // 在 1ms 环里等 100ms → 电机控制直接烂掉
  if (!client_->wait_for_service(100ms)) return;

  hw_->read();   // 读编码器等硬件数据
  // … PID 计算 …
  hw_->write();  // 写电机指令
}
```

### ✅ ros2_control 式正例（两条线）

```cpp
// —— 手术台：控制环，只碰硬件，绝对不调慢服务 ——
void update() {
  hw_->read();    // 读传感器
  // 几微秒～几毫秒的 PID
  hw_->write();   // 写执行器
}

// —— 护士站：服务回调，可以慢一点 ——
// 有人调用「切换控制器」服务时，框架会调这个函数
void on_switch_controller_service(Request req, Response & res) {
  // req：对方发来的请求；res：你要填的回复
  // switch_controller：真正换控制器的业务函数
  res.ok = switch_controller(req.name);
  // 注意：这条线不在 update() 里，所以不会拖死电机环
}
```

**读法：** 快的永远快；慢的走另一扇门。

---

## 4. 你的服务练习：对照两种「程序身份」

### 4.1 调试脚本（可以干等）≈ `client_future`

完整文件：`my_package/src/client_future.cpp`、`topic_demo/.../client_future.py`

```cpp
// ========== C++：脚本客户端（跑完就退出）==========
rclcpp::init(argc, argv);  // 启动 ROS
// make_shared：创建一个叫 client_future 的节点
auto node = rclcpp::Node::make_shared("client_future");
// create_client：创建「加两个整数」服务的客户端话筒
// AddTwoInts = 服务类型；"add_two_ints" = 服务名字（必须和服务端一致）
auto client = node->create_client<AddTwoInts>("add_two_ints");

// wait_for_service(1s)：最多等 1 秒；服务出现→true；超时→false
// while：超时就再试，直到服务上线（或用户 Ctrl+C）
while (!client->wait_for_service(1s)) {
  // rclcpp::ok()：系统还在跑吗？Ctrl+C 后会变 false
  if (!rclcpp::ok()) return 1;
}

// make_shared<Request>()：新建请求；a、b 是要加的两个数
auto request = std::make_shared<AddTwoInts::Request>();
request->a = 41;
request->b = 1;

// async_send_request：发出请求，马上返回 Future（取餐号），此时结果可能还没到
auto future = client->async_send_request(request);

// spin_until_future_complete：本脚本唯一的「值班」
// 边处理 ROS 通信，边等 Future 完成；最多等 5 秒
rclcpp::spin_until_future_complete(node, future, 5s);

// future.get()：从取餐号取出真正的回复（里面有 sum）
auto result = future.get();
// result->sum 就是 41+1 的答案
```

```python
# ========== Python：同上 ==========
rclpy.init(args=args)
node = rclpy.create_node('client_future')          # 创建节点
client = node.create_client(AddTwoInts, 'add_two_ints')  # 创建客户端

# wait_for_service(timeout_sec=1.0)：最多阻塞 1 秒等服务上线
while not client.wait_for_service(timeout_sec=1.0):
    if not rclpy.ok():                             # 是否被 Ctrl+C 打断
        return 1

request = AddTwoInts.Request()                     # 新建请求对象
request.a = 41
request.b = 1

# call_async：异步发请求，返回 Future（≈ C++ 的 async_send_request）
future = client.call_async(request)

# spin_until_future_complete：边 spin 边等 Future，最多 5 秒
rclpy.spin_until_future_complete(node, future, timeout_sec=5.0)

# future.result()：取回复（≈ C++ 的 future.get()）
print(future.result().sum)                         # 打印两数之和
```

### 4.2 一直跑的节点（不能干等）≈ `my_node` / `py_topic`

完整文件：`my_package/src/my_node.cpp`、`topic_demo/.../my_topic.py`

```cpp
// ========== C++：常驻节点里发服务请求 ==========
void send_request() {
  // service_is_ready()：立刻返回 bool，不卡住
  // false → 服务还没好，本拍跳过（下次定时器再试）
  if (!client_->service_is_ready()) return;

  auto req = std::make_shared<AddTwoInts::Request>();
  req->a = 1;
  req->b = 2;

  // 第二个参数是「结果到了时要跑的函数」（lambda 回调）
  // 发完本函数立刻结束；1+2 的答案在回调里打印
  client_->async_send_request(req, [this](auto future) {
    // 只有进了这个回调，才说明回复已经到了
    auto result = future.get();           // 取回复
    RCLCPP_INFO(..., "sum=%ld", result->sum);  // 打日志
  });
}

// main 里只有这一句长期运行（不要再 spin_until_future_complete）
rclcpp::spin(node);  // 一直值班：定时器、订阅、服务、上面的结果回调都会被它驱动
```

```python
# ========== Python：常驻节点 ==========
def send_request(self):
    # service_is_ready()：非阻塞问「服务在不在」
    if not self.client_.service_is_ready():
        return

    request = AddTwoInts.Request()
    request.a = 1
    request.b = 2

    # call_async：发出去，拿到 Future
    future = self.client_.call_async(request)
    # add_done_callback：登记「Future 完成时调用 on_result」
    # （≈ C++ 把回调直接传给 async_send_request）
    future.add_done_callback(self.on_result)

def on_result(self, future):
    # 框架在结果就绪时自动调用；这里再取数
    result = future.result()
    self.get_logger().info('sum=%d' % result.sum)

# main：长期 spin
rclpy.spin(node)
```

### 对照表

| | 脚本 `client_future` | 常驻 `my_node` |
|--|----------------------|----------------|
| 像谁 | MoveIt 教程脚本 | Nav2 / 抓取任务节点 |
| 问服务在不在 | `wait_for_service`（可挡几秒） | `service_is_ready`（瞬间） |
| 等结果 | `spin_until_future_complete` | **结果回调**，禁止再 `spin_until…` |
| 取结果 | `get()` / `result()`（等完再取） | 同样在回调里 `get()` / `result()` |

---

## 5. 同节点自己调自己 —— 死锁小剧场

### ❌ 错例

```cpp
// 同一个节点里：既 create_service 又 create_client，服务名还一样
void on_timer() {  // 定时器回调正在占用「前台」
  // 发出请求（给自己）
  auto fut = client_->async_send_request(req);

  // spin_until_future_complete：想在这里干等结果
  // 但服务端回调也需要同一个 spin 才能执行 → 你占着线等自己接电话 → 死锁
  rclcpp::spin_until_future_complete(shared_from_this(), fut);
}
```

像：自己给自己打电话还占线不挂。

### ✅ 热门项目式解法

**解法 A（最常见）：拆成两个节点**

```text
终端1: gripper_driver   —— 只 create_service（接电话）
终端2: pick_task        —— 只 create_client + 状态机（打电话）
```

**解法 B（教学/简单任务）：异步留言**

```cpp
void on_timer() {
  // 先扫一眼服务在不在（不阻塞）
  if (!client_->service_is_ready()) return;

  // 发请求 + 登记回调：on_timer 马上结束，前台空出来
  // 服务端回调能跑完；之后结果回调再进下面的 lambda
  client_->async_send_request(req, [this](auto fut) {
    handle_gripper_ok(fut.get());  // fut.get()：取回复，再做后续逻辑
  });
}
```

---

## 6. 其它场景的迷你实例

### 6.1 激光回调里重计算

```cpp
// laser_cb：每收到一帧激光，框架就调用一次（订阅回调）
void laser_cb(const LaserScan::SharedPtr msg) {
  run_heavy_icp(*msg);  // ❌ 很重的算法卡 200ms → 其它回调排队
}

void laser_cb(const LaserScan::SharedPtr msg) {
  queue_.push(*msg);    // ✅ 只把数据放进队列（很快），立刻返回
}
// 另一个线程/节点：从 queue_ 取数据再慢慢算
```

### 6.2 TF 查询（坐标变换）

```cpp
// lookupTransform(目标坐标系, 源坐标系, 时间, 超时)
// 意思：查「base 相对 map 的位姿」；若还没有，最多干等超时时间
tf_->lookupTransform("map", "base", tf2::TimePointZero, 5s);  // ❌ 可能干等 5 秒

// canTransform：只问「现在有没有这个变换？」→ true/false，不干等
if (!tf_->canTransform("map", "base", tf2::TimePointZero)) {
  return;  // 本拍没有，下拍再试
}
// 确认有了再查，一般很快返回
auto t = tf_->lookupTransform("map", "base", tf2::TimePointZero);
```

### 6.3 定时器比周期还慢

```cpp
void on_timer() { very_slow_work(); }   // ❌ 周期 10ms，活却要 50ms

void on_timer() { flag_do_work_ = true; }  // ✅ 只竖旗子；慢活放到别处做
```

---

## 7. 一张「从错到正」总表

| 场景 | ❌ 错例关键词 | ✅ 热门项目式正例 |
|------|---------------|-------------------|
| 导航再抓 | 回调里 `while not complete` | Action + 状态 / `isTaskComplete` 短问（§1） |
| 开爪再下降 | 一个函数里连续 wait | 状态机 + `async_send_request` 回调（§2） |
| 电机环 | `update` 里 `wait_for_service` | 控制环与配置 Service 拆开（§3） |
| 调服务 | 常驻节点里 `spin_until_future_complete` | `service_is_ready` + 结果回调（§4） |
| 自己调自己 | 同步等本节点服务 | 拆节点或 async（§5） |
| 激光 / TF | 回调里长计算 / 长 timeout | 入队 / `canTransform`（§6） |

---

## 8. 练习怎么跑

```bash
source ~/robot_workspaces/ros_ws/install/setup.bash

# 终端1：常驻节点（里面有服务端 + callback 客户端）
ros2 run my_package my_node
# 终端2：脚本客户端（wait_for_service + spin_until_future_complete）
ros2 run my_package client_future

# Python 同理
ros2 run topic_demo py_topic
ros2 run topic_demo client_future
```

对照带注释的完整源码：

- `my_package/src/my_node.cpp`、`client_future.cpp`
- `topic_demo/topic_demo/my_topic.py`、`client_future.py`

---

## 附录 A：C++ / Python API 对照（带含义）

| 你想做的事 | Python | C++ | 含义（人话） |
|------------|--------|-----|--------------|
| 启动 ROS | `rclpy.init` | `rclcpp::init` | 程序开头初始化 |
| 创建节点 | `Node(...)` / `create_node` | `std::make_shared<Node>` | 开一个前台 |
| 创建客户端 | `create_client(类型, 名)` | `create_client<类型>(名)` | 拿起话筒 |
| 创建服务端 | `create_service(类型, 名, 回调)` | `create_service<类型>(名, 回调)` | 开始接电话 |
| 阻塞等服务上线 | `wait_for_service(t)` | `wait_for_service(t)` | 门口干等，最多 t |
| 立刻问服务在不在 | `service_is_ready()` | `service_is_ready()` | 扫一眼灯亮没 |
| 异步发请求 | `call_async(req)` | `async_send_request(req)` | 下单拿取餐号 |
| 登记「好了再叫我」 | `future.add_done_callback(cb)` | `async_send_request(req, cb)` | 门铃到了再处理 |
| 脚本里等结果 | `spin_until_future_complete` | 同名 | 边值班边盯取餐号 |
| 取回复内容 | `future.result()` | `future.get()` | 打开餐盒看答案 |
| 长期值班 | `rclpy.spin(node)` | `rclcpp::spin(node)` | 一直处理各种回调 |
| 打日志 | `node.get_logger().info(...)` | `RCLCPP_INFO(node->get_logger(), ...)` | 打印信息 |

## 附录 B：两个易混对（务必分清）

| 对比 | A | B | 怎么选 |
|------|---|---|--------|
| 问服务 | `wait_for_service` | `service_is_ready` | 脚本启动用 A；定时器/回调里用 B |
| 等结果 | `spin_until_future_complete` | 结果 callback | 单独脚本用 A；已有 `spin` 的节点用 B |

**总记：** 热门项目 = **慢事异步化 + 顺序用状态/行为树 + 前台回调保持短**。  
看代码时先查 **§0 词典**，再对照每一行注释里的「这个 API 干什么」。
