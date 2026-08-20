# ROS 2 从零理解：回调为什么会卡死？执行器在干什么？

> 面向：**刚写过话题/服务、还不懂为什么程序会「冻住」** 的读者。  
> 建议顺序：§0 名词 → §1 程序怎么跑 → §2 卡死原理 → §3 两种客户端 → 后面的项目例子。  
> 可跑的练习：`my_package`（C++）、`topic_demo`（Python）。

---

## 这篇在回答什么？

你以后写机器人程序，经常会遇到这种需求：

1. 先走到桌子旁边  
2. **等到真的走到了**  
3. 再去抓东西  

业务上「必须等前一步完成」，这没问题。  
容易错的是：把「等」写成 **当前这个函数一直不返回**。

在 ROS 2 默认写法里，**一个节点通常只有一个「值班员」轮流处理所有事情**。  
某个回调不返回 = 值班员被占住 = 激光、急停、定时器都暂时没人管。

热门项目（Nav2、MoveIt、ros2_control）的共同规矩：

> **值班员只做短事；要等很久 → 先记一笔账，有结果再回来处理。**

---

## 0. 先认这些词（后面每节都会用）

先不要急着看代码。这些词搞不清，后面所有例子都会「看起来像天书」。

### 0.1 通信相关

| 名词 | 一句话是什么 | 生活类比 | 零基础注意 |
|------|--------------|----------|------------|
| **节点 Node** | 你写的一个 ROS 程序单元 | 一家店的前台 | 一个终端 `ros2 run` 通常就是一个进程、里面一个节点 |
| **话题 Topic** | 广播：谁订了谁就能收到 | 店里喇叭 | 发完就走，**不保证**对方立刻处理完 |
| **服务 Service** | 一问一答 | 打电话点菜，对方报「好了」 | 有请求、有回复；回复回来之前，你「想知道答案」 |
| **客户端 Client** | 打电话的那一方 | 顾客 | `create_client` 只是拿起话筒，**不等于**对方已上班 |
| **服务端 Server** | 接电话、算完写回复 | 厨房 | `create_service` 注册一个「有人打来就进这个函数」 |
| **Action** | 长时间任务：可取消、可报进度 | 叫外卖（下单、骑手路上、送到） | 导航、规划常用这个，比服务更适合「走 30 秒」这种事 |

### 0.2 程序怎么被「叫醒」

| 名词 | 一句话是什么 | 生活类比 | 零基础注意 |
|------|--------------|----------|------------|
| **回调 callback** | 某件事发生时，**框架自动调用**你写的函数 | 电话铃响 → 你接起来说的那段话 | **你几乎不自己调用它**；是 spin 帮你调 |
| **定时器 Timer** | 每隔固定时间调一次你的函数 | 闹钟 | `create_wall_timer(1s, 函数)` = 每秒响一次 |
| **订阅回调** | 话题来了一条消息就调一次 | 有人点餐，前台记下来 | 消息来得快，回调就必须短 |
| **服务回调** | 有人发来服务请求就调一次 | 接电话算账 | 算完把答案写进 `response` |

### 0.3 最容易混的三个词

| 名词 | 一句话是什么 | 生活类比 |
|------|--------------|----------|
| **执行器 Executor** | 「值班排班系统」：决定**下一个跑哪个回调** | 店长安排谁接待下一桌 |
| **spin** | 进入值班循环：有就绪回调就执行，否则就等 | 前台一直在岗，不下班 |
| **Future** | 「以后才有结果」的收据 | 取餐号：先拿到号，餐好了再取 |

再加两个动作词：

| 词 | 含义 | 例子 |
|----|------|------|
| **阻塞 / 干等** | 当前函数卡住不返回 | `while True: pass`、`wait_for_service(1秒)`、`sleep(5)` |
| **非阻塞 / 异步** | 发完立刻返回，结果以后再处理 | `call_async` + `add_done_callback` |

### 0.4 最常用 API 词典

| API（C++ / Python 写法接近） | 干什么 | 会不会卡住当前函数 |
|------------------------------|--------|--------------------|
| `rclcpp::init` / `rclpy.init` | 启动 ROS 客户端库，程序开头调用一次 | 否 |
| `create_node` / `Node(...)` | 创建一个节点 | 否 |
| `create_publisher` | 创建一个「往话题上喊」的喇叭 | 否 |
| `create_subscription` | 订话题，并登记「来消息时调哪个函数」 | 否（登记时不卡；**回调跑起来时**才可能卡） |
| `create_client(服务类型, 服务名)` | 做一个打电话的话筒 | 否 |
| `create_service(服务类型, 服务名, 回调)` | 开一个服务 | 否 |
| `wait_for_service(超时)` | **站着等**对方开门，最多等这么久 | **会**（最多等超时时间） |
| `service_is_ready()` | **扫一眼**对方现在能不能接 | **不会** |
| `async_send_request` / `call_async` | 把请求发出去，立刻返回 Future（取餐号） | **发的时候不等结果** |
| `spin_until_future_complete(node, future, 超时)` | 边值班边盯着取餐号，直到有结果或超时 | **会**（占着这个程序等） |
| `async_send_request(req, 回调)` / `future.add_done_callback(回调)` | 发请求 + 登记「结果到了再自动调这个函数」 | 发完立刻返回 |
| `future.get()` / `future.result()` | 从 Future 里取出真正的回复 | 结果还没到时 **可能卡住** |
| `rclcpp::spin(node)` / `rclpy.spin(node)` | **一直值班**，直到 Ctrl+C | **会一直停在这行** |
| `rclcpp::ok()` / `rclpy.ok()` | ROS 还在正常跑吗？（有没有 Ctrl+C） | 否 |

> **一条铁律（先记住，后面会解释为什么）：**  
> 程序里**已经**在 `spin(node)` 了 → 不要再在回调里用 `spin_until_future_complete`。  
> 单独小脚本、没有长期 spin → 可以用 `wait_for_service` + `spin_until_future_complete`。

---

## 1. 一个 ROS 程序到底怎么跑起来？

很多卡死问题，根源是：**不知道回调是谁调用的。**

### 1.1 没有 spin，你写的回调几乎不会跑

下面这个程序「看起来创建了定时器」，但屏幕上什么也不会发生：

```python
# 错误示范：创建了节点和定时器，但没有 spin
node = MyNode('demo')
# 程序走到这里就结束了，定时器一次都没被调用
```

正确骨架永远是这三步：

```text
1. init     ：告诉系统「我要开始用 ROS 了」
2. 创建节点 ：创建 publisher / subscriber / timer / service / client（只是登记）
3. spin     ：进入死循环，真正开始处理事件
```

C++：

```cpp
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);                 // 1) 初始化
  auto node = std::make_shared<MyNode>("my_node");  // 2) 创建节点（构造函数里登记各种回调）
  rclcpp::spin(node);                       // 3) 值班，直到 Ctrl+C
  rclcpp::shutdown();                       // 清理
  return 0;
}
```

Python：

```python
def main():
    rclpy.init()
    node = MyTopic('my_topic')
    rclpy.spin(node)      # 会一直停在这里
    node.destroy_node()
    rclpy.shutdown()
```

**零基础读法：**  
`spin(node)` 不是「可有可无的收尾」。  
它就是这个程序的心脏：**没有它，订阅、定时器、服务都像没插电。**

### 1.2 默认只有一个值班员（单线程执行器）

`rclcpp::spin(node)` / `rclpy.spin(node)` 默认使用 **SingleThreadedExecutor（单线程执行器）**。

人话：

```text
一个值班员，手里一份待办清单：

  - 定时器到点了吗？
  - 话题来消息了吗？
  - 有人调用服务了吗？
  - 客户端的回复到了吗？

规则：
  一次只做一件。
  做完这一件（回调函数 return 了），才做下一件。
  如果这一件一直不 return，清单上后面的全得排队。
```

时间线（默认情况）：

```text
时间 →

spin 循环：  [定时器回调] → [订阅回调] → [服务回调] → [结果回调] → [定时器...]
                 ↑
           必须跑完才能进行下一个
```

所以：**回调函数要短。**  
短 = 很快 return。  
长 = 别人只能干等。

### 1.3 「登记回调」和「执行回调」是两回事

构造函数里这些调用：

- `create_subscription(..., subscriber_callback)`
- `create_wall_timer(1s, timer_callback)`
- `create_service(..., add_two_ints_callback)`

只是在说：**以后发生某件事时，请调用这个函数。**  
真正调用发生在 `spin` 里。

可以记成：

| 阶段 | 你在干什么 | 回调会跑吗 |
|------|------------|------------|
| 构造函数 / `__init__` | 登记「谁负责接哪种电话」 | 通常不会（还没 spin） |
| `spin` | 值班员开始接电话 | 会 |
| 某个回调内部 | 处理这一件事 | 若这里再死等，其它电话没人接 |

---

## 2. 卡死到底是怎么发生的？（核心）

### 2.1 最小错例：定时器里睡 10 秒

假设节点里有：

- 定时器：每 1 秒打印一次  
- 订阅：收到急停话题就停车  

如果定时器这样写：

```python
def timer_callback(self):
    time.sleep(10)   # 当前函数卡住 10 秒不返回
    self.get_logger().info('tick')
```

会发生什么：

```text
t=0s   定时器回调开始，sleep(10)
t=0.1s 急停消息到达  →  订阅回调已经就绪，但值班员还在 sleep
t=9.9s 急停仍然没人处理
t=10s  定时器回调终于 return，值班员才有空去跑急停回调
```

**急停晚了 10 秒。**  
不是 ROS 「坏了」，是你把唯一的值班员占住了。

服务、Action、TF 查询，只要出现「回调里长时间不返回」，都是同一类问题。

### 2.2 阻塞 vs 非阻塞：同一件事的两种写法

「调用加法服务，算出 1+2」可以有两种态度：

| | 阻塞（脚本可以） | 非阻塞（常驻节点必须） |
|--|------------------|------------------------|
| 做法 | 发请求后，**就在这里等到有答案** | 发请求后，**马上回去干别的**；答案到了再打印 |
| 值班员 | 这段时间专门盯这一件事 | 可以继续处理定时器、订阅 |
| API | `spin_until_future_complete` | 结果 callback / `add_done_callback` |
| 适合 | 跑完就退出的小工具 | 一直开着的机器人节点 |

注意：`async_send_request` / `call_async` **本身不阻塞等结果**。  
后面你怎么「等」，才决定会不会卡死。

```text
发请求（异步，很快）
        │
        ├─ 写法 A：spin_until_future_complete  →  程序在这里等到有结果（脚本 OK）
        │
        └─ 写法 B：登记一个回调，当前函数立刻 return  →  结果到了 spin 再调你（常驻节点 OK）
```

### 2.3 为什么「已经在 spin 了，不能再 spin_until」？

可以把它想成：**值班员已经在大厅里转圈接待了。**

- `spin(node)` = 开始转圈接待（外层已经有一个循环）  
- `spin_until_future_complete` = **再开一个转圈**，并且说「不看到取餐号就不结束」

如果这个「再开一个转圈」发生在某个回调里面：

```text
外层 spin
  └─ 正在执行 timer_callback
        └─ 又调用 spin_until_future_complete（想等服务结果）

问题：
  服务端回调也需要外层/同一个执行器来跑。
  但执行器正被 timer_callback 占着，进不去服务回调。
  结果永远等不到  →  死锁（大家互相等，谁也动不了）
```

所以铁律不是「spin_until 这个函数坏」，而是：

> **一个节点不要嵌套两层「值班循环」。**  
> 外层已经 `spin` 了，里面就用回调收结果。

### 2.4 同节点自己调自己，为什么特别容易死锁？

`my_node` 教学里同一个节点既有：

- `create_service("add_two_ints", ...)`  → 厨房  
- `create_client("add_two_ints")`       → 顾客  

这是为了一个进程里就能练。真实项目更常拆成两个节点。

若在定时器里这样写：

```cpp
void on_timer() {
  auto fut = client_->async_send_request(req);
  // 想在这里等到自己厨房算完
  rclcpp::spin_until_future_complete(shared_from_this(), fut);
}
```

过程：

```text
1. 定时器回调占用了唯一值班员
2. 客户端把请求发给本节点的服务端
3. 服务端回调想跑，需要值班员
4. 值班员却在 spin_until 里干等「服务回调跑完」
5. 永远等不到 → 死锁
```

类比：**自己给自己打电话，还占线不挂，对方（也是你）永远接不起来。**

正确做法见 §5：要么拆节点，要么发完就 return、用结果回调。

---

## 3. 你的两个练习程序：请先搞清「程序身份」

写客户端之前先问自己一句：

> **这个程序是「干完一件事就退出」，还是「要一直开着、同时干很多事」？**

| | `client_future` | `my_node` / `py_topic` |
|--|-----------------|------------------------|
| 身份 | 调试脚本 | 常驻机器人节点 |
| 像谁 | MoveIt 教程里跑一遍就结束的 demo | Nav2 / 抓取任务节点 |
| main 里 | **没有**长期 `spin` | **有** `spin(node)` |
| 问服务在不在 | `wait_for_service`（可挡几秒） | `service_is_ready`（瞬间） |
| 等结果 | `spin_until_future_complete` | 结果 callback |
| 跑完 | `return 0` 退出 | Ctrl+C 才停 |

### 3.1 脚本：`client_future` 逐步发生了什么

完整文件：

- C++：`my_package/src/client_future.cpp`  
- Python：`topic_demo/topic_demo/client_future.py`  

时间线：

```text
启动
  │
  ├─ create_client("add_two_ints")     拿起话筒（厨房可能还没开门）
  │
  ├─ while wait_for_service(1秒):      门口等开门；每秒问一次
  │     超时 → 再等 1 秒
  │     Ctrl+C → 退出
  │     开门 → 离开 while
  │
  ├─ 填请求 a=41, b=1
  │
  ├─ async_send_request / call_async   把单子递进去，立刻拿到取餐号（Future）
  │
  ├─ spin_until_future_complete(..., 5秒)
  │     这是本程序唯一的值班：
  │     一边处理 ROS 通信，一边盯着取餐号
  │     厨房（另一个进程 my_node）算出 42，回复到达
  │
  ├─ future.get() / result()           打开餐盒，看到 sum=42
  │
  └─ shutdown，进程结束
```

C++ 逐步注释：

```cpp
rclcpp::init(argc, argv);
auto node = rclcpp::Node::make_shared("client_future");

// AddTwoInts = 服务类型（请求有 a、b，回复有 sum）
// "add_two_ints" = 服务名字，必须和服务端完全一致
auto client = node->create_client<AddTwoInts>("add_two_ints");

// wait_for_service(1s)：最多阻塞 1 秒
//   true  = 这 1 秒内服务出现了
//   false = 等满 1 秒还没有
while (!client->wait_for_service(1s)) {
  if (!rclcpp::ok()) return 1;   // 用户按了 Ctrl+C
}

auto request = std::make_shared<AddTwoInts::Request>();
request->a = 41;
request->b = 1;

// 发送。返回值 Future 现在可能还是「未完成」
auto future = client->async_send_request(request);

// 本脚本唯一的 spin：等到 Future 完成，或 5 秒超时
rclcpp::spin_until_future_complete(node, future, 5s);

auto result = future.get();   // 这里调用时，结果应当已经到了
// result->sum == 42
```

Python 对应关系：

| C++ | Python | 含义 |
|-----|--------|------|
| `async_send_request(req)` | `call_async(req)` | 发出去，拿 Future |
| `future.get()` | `future.result()` | 取回复 |
| `wait_for_service(1s)` | `wait_for_service(timeout_sec=1.0)` | 门口等开门 |

**为什么脚本可以阻塞？**  
因为这个进程的任务就是「发一次、拿到结果、退出」。  
卡住几秒没关系：没有激光、没有急停要它同时管。

### 3.2 常驻节点：`my_node` / `py_topic` 逐步发生了什么

完整文件：

- C++：`my_package/src/my_node.cpp`  
- Python：`topic_demo/topic_demo/my_topic.py`  

这个节点同时做几件事：

- 每秒往 `topic` 发一句话（定时器 + publisher）  
- 订阅同一个话题（练习回调）  
- 提供 `add_two_ints` 服务（厨房）  
- 自己再当客户，异步问一次 1+2（教学用）  

main 里已经有 `spin(node)`，所以客户端**绝不能**再 `spin_until_future_complete`。

时间线：

```text
spin 开始转圈
  │
  ├─ 第 1 秒定时器响
  │     发话题
  │     service_is_ready()?   只扫一眼，不等
  │       还没好 → return，下次再试
  │       好了 → call_async / async_send_request(带回调)
  │              立刻 return  ← 值班员空出来了
  │
  ├─ 中间还可以：再发话题、处理订阅、处理别人打来的服务
  │
  └─ 厨房算完，回复到达
        spin 调用你登记的结果回调
        future.get() / result() 打印 sum
```

C++ 关键片段：

```cpp
void send_client_request_callback()
{
  // 立刻返回 true/false。定时器里必须用这个，不能 wait_for_service
  if (!client_->service_is_ready()) {
    request_sent_ = false;  // 下次定时器再试
    return;
  }

  auto request = std::make_shared<AddTwoInts::Request>();
  request->a = 1;
  request->b = 2;

  // 第二个参数：结果到了时由 spin 调用的函数（lambda）
  client_->async_send_request(
    request,
    [this](rclcpp::Client<AddTwoInts>::SharedFuture future) {
      auto result = future.get();          // 此时结果已到，get 是安全的
      RCLCPP_INFO(..., "%" PRId64, result->sum);
    });

  // 注意：上面那行已经返回了！sum 还没打印，但本函数结束了
}
```

Python 对应：

```python
def send_client_request_callback(self):
    if not self.client_.service_is_ready():
        self.request_sent_ = False
        return

    request = AddTwoInts.Request()
    request.a = 1
    request.b = 2

    future = self.client_.call_async(request)
    # 登记：Future 完成时调用 response_callback
    future.add_done_callback(self.response_callback)
    # 这里立刻返回，不等 1+2 的答案

def response_callback(self, future):
    result = future.result()
    self.get_logger().info('[callback] Result: %d' % result.sum)
```

C++ 和 Python 这里只是「登记回调」的语法不同：

| | C++ | Python |
|--|-----|--------|
| 发请求 | `async_send_request(req, 回调)` 一次完成 | 先 `call_async`，再 `add_done_callback` |
| 取结果 | `future.get()` | `future.result()` |

含义完全一样：**发完就走，门铃响了再开门。**

### 3.3 `wait_for_service` 和 `service_is_ready` 为什么不能混用？

两者都在问：「厨房开门了吗？」差别是**问的方式**。

```text
service_is_ready()
  路过扫一眼灯  →  立刻知道亮/不亮  →  适合已经在值班的回调里

wait_for_service(1秒)
  站在门口等，最多 1 秒  →  这 1 秒内你不干别的  →  适合脚本启动阶段
```

若在每秒一次的定时器里写 `wait_for_service(1s)`：

```text
定时器本该 1 秒结束
但你在里面最多又站 1 秒
订阅、急停、别的定时器全部被推迟
```

所以：

- `client_future`（脚本）→ `while + wait_for_service`  
- `my_node` 的定时器 → `service_is_ready`，不好就 `return`，下次再试  

---

## 4. 看不懂代码时：这些语法在干什么？

零基础卡壳，经常不是 ROS，而是 C++/Python 写法。下面只解释本文会出现的。

### 4.1 「回调」就是一个普通函数

```python
def timer_callback(self):
    ...
self.create_timer(1.0, self.timer_callback)
```

意思：1 秒后（以及之后每秒），请调用 `timer_callback`。  
`create_timer` 的第二个参数是**函数本身**，不是 `timer_callback()`（注意没有括号——加上括号变成「现在立刻调用」）。

### 4.2 C++ 的 `std::bind` 和 lambda

成员函数需要对象才能调用，所以要绑 `this`：

```cpp
// _1 表示「将来真正调用时，第一个参数放这里」（消息）
subscriber_ = this->create_subscription<std_msgs::msg::String>(
  "topic", 10, std::bind(&MyNode::subscriber_callback, this, _1));
```

lambda = 写在原地的短函数：

```cpp
[this](auto future) {
  auto result = future.get();
}
```

`[this]` = 这个短函数里允许使用当前对象的成员（比如 `client_`、`get_logger()`）。  
你暂时只要知道：**这是「结果到了时要执行的那一小段」。**

### 4.3 `shared_ptr` / `make_shared`

C++ 里请求对象常放在堆上，并用智能指针管生命周期：

```cpp
auto request = std::make_shared<AddTwoInts::Request>();
request->a = 1;   // 箭头：通过指针访问里面的字段
```

Python 简单得多：

```python
request = AddTwoInts.Request()
request.a = 1     // 点号访问字段
```

### 4.4 状态机（后面抓取例子会用）

「必须先开爪、再下降」如果写成一个函数里连续 wait，会卡住值班员。  
状态机 = **用一个变量记住做到哪一步**，每次定时器只推进一步：

```text
这一拍：如果状态是「该开爪」→ 发出开爪请求 → 改成「等开爪」→ return
某一拍：开爪结果回调到了 → 把状态改成「该下降」
下一拍：看到状态是「该下降」→ 发下降命令 → return
```

业务顺序还在；代码上每次都很快结束。

---

## 5. Nav2：走到桌子旁再抓

### 你想做的事

走到桌子 → 到了 → 再抓取。  
顺序必须有，但**不能**在按钮回调里死等。

### ❌ 错例（按钮回调里死等）

```python
# on_start_button：开始按钮被按下时，框架自动调用
def on_start_button(msg):
    navigator.goToPose(table_pose)          # 给导航发「去这个点」

    # isTaskComplete()：问导航这次做完了没 → True/False
    # while + pass：空转死等。值班员被占住，激光/急停进不来
    while not navigator.isTaskComplete():
        pass

    start_grasping()
```

问题逐步拆开：

```text
用户按下开始
  → 进入 on_start_button（占用值班员）
  → 导航可能要走 20 秒
  → 这 20 秒里 while 一直转
  → 激光回调、速度发布、急停全部排队
  → 机器人等于「闭着眼睛在走」
```

### ✅ Nav2 式正例（发完就走 + 短轮询）

Nav2 常用 **Action**（长时间任务）+ **行为树**（每隔一小段时间问：这一步好了没？）。

零基础可以先用「一个状态变量 + 定时器」理解同一思想：

```python
# IDLE=闲着, GOING=正在去, ARRIVED=到了, GRASP=在抓
state = 'IDLE'

def on_start_button(msg):
    global state
    navigator.goToPose(table_pose)  # 只下单，不等到站
    state = 'GOING'                 # 记账：正在路上
    # 函数在这里就结束了，值班员去干别的

def on_timer():                     # 例如每 0.1 秒，像行为树「拍一下」
    global state
    if state == 'GOING':
        if not navigator.isTaskComplete():
            return                  # 还没到：本拍什么都不做
        state = 'ARRIVED'
    if state == 'ARRIVED':
        start_grasping()
        state = 'GRASP'
```

**读法：**

- 按钮回调：只负责「开始去」  
- 定时器：只负责「到了没？到了再抓」  
- 中间 0.1 秒空档：spin 可以处理激光和急停  

这就是「逻辑上有顺序，代码上每次都很快返回」。

---

## 6. MoveIt：开爪 → 下降 → 闭合

### 你想做的事

必须先开爪成功，再下降，再闭合。顺序不能乱。

### ❌ 错例（一个函数里卡住等）

```cpp
void do_pick() {
  open_gripper_and_wait();   // 内部 wait 几秒
  descend();
  close_gripper_and_wait();
  // 常驻节点里：整个 spin 被这几秒占满
}
```

### ✅ 脚本可以干等（像 MoveIt 教程）

和 `client_future` 同一类程序：跑完就退出。

```cpp
int main() {
  move_group.setPoseTarget(pose);  // 设定想去的位姿
  move_group.move();               // 教程脚本里可以等动完再返回
  return 0;
}
```

### ✅ 常驻节点用状态机（像正式抓取任务）

```cpp
enum class Phase { Open, WaitOpen, Descend, Close, Done };
Phase phase_ = Phase::Open;

void on_timer() {  // 每一拍必须短
  switch (phase_) {

    case Phase::Open:
      // 扫一眼夹爪服务在不在；不在就等下一拍
      if (!gripper_client_->service_is_ready()) return;

      {
        auto req = std::make_shared<Gripper::Request>();
        req->command = "open";

        // 发出去立刻返回；成功后再改 phase_
        gripper_client_->async_send_request(req, [this](auto future) {
          if (future.get()->success)
            phase_ = Phase::Descend;
        });
      }
      phase_ = Phase::WaitOpen;  // 先进入「等回执」
      break;

    case Phase::WaitOpen:
      break;  // 空拍：等上面的结果回调改 phase_

    case Phase::Descend:
      send_descend_command();
      phase_ = Phase::Close;
      break;

    case Phase::Close:
      /* 同样 async 闭合 */
      phase_ = Phase::Done;
      break;

    case Phase::Done:
      break;
  }
}
```

对应关系：

| 业务步骤 | 代码上怎么表达 |
|----------|----------------|
| 先开爪 | `phase_ == Open` 时发请求 |
| 必须等开爪成功 | 结果回调里才改成 `Descend` |
| 再下降 | 某一拍定时器看到 `Descend` 再发命令 |

**没有**在一个函数里 sleep 到开爪结束。

---

## 7. ros2_control：电机环为什么绝对不能等服务？

控制环可能 **1 秒跑 1000 次**（每 1 毫秒一次）。  
一次 `wait_for_service(100ms)` 就会让电机指令停 100 拍。

### ❌ 错例

```cpp
void update() {  // 假设每 1ms 必须跑完
  if (!client_->wait_for_service(100ms)) return;  // 可能卡 100ms
  hw_->read();
  hw_->write();
}
```

### ✅ 正例：两条线

```cpp
// 手术台：只碰硬件
void update() {
  hw_->read();
  // 很短的 PID
  hw_->write();
}

// 护士站：换控制器、改参数，走 Service，不进 update()
void on_switch_controller_service(Request req, Response & res) {
  res.ok = switch_controller(req.name);
}
```

零基础记住：

> 越快的循环，越不能做慢事（等网络、等服务、读大文件、sleep）。

---

## 8. 同节点自己调自己：解法

### ❌ 错例（§2.4 的代码）

定时器里 `async_send_request` 之后立刻 `spin_until_future_complete`。

### ✅ 解法 A（项目最常见）：拆成两个进程

```text
终端1: gripper_driver   —— 只 create_service（接电话）
终端2: pick_task        —— 只 create_client（打电话）+ 状态机
```

两个进程 = 两个值班员，不会自己占自己的线。

### ✅ 解法 B（教学 / 简单任务）：异步留言

`my_node` 就是这种：同一个节点里既当厨房又当顾客，但顾客用 callback。

```cpp
void on_timer() {
  if (!client_->service_is_ready()) return;
  client_->async_send_request(req, [this](auto fut) {
    handle_gripper_ok(fut.get());
  });
  // on_timer 结束 → 值班员去跑服务回调 → 再跑结果回调
}
```

顺序变成：

```text
定时器发请求并 return
  → 服务回调跑完（算出 sum）
  → 结果回调跑（打印 sum）
```

每一段都短，所以不会死锁。

---

## 9. 其它也会卡的情况（根因相同）

根因都是：**值班员被一件慢事占住了。** 不一定是服务。

### 9.1 激光回调里做超重计算

```cpp
void laser_cb(const LaserScan::SharedPtr msg) {
  run_heavy_icp(*msg);  // ❌ 卡 200ms，其它回调排队
}

void laser_cb(const LaserScan::SharedPtr msg) {
  queue_.push(*msg);    // ✅ 只收件，立刻返回
}
// 另一个线程或另一个节点再慢慢算
```

### 9.2 TF（坐标系变换）干等

机器人常问：「现在底盘相对地图在哪？」

```cpp
// lookupTransform(..., 超时 5秒)：没有就干等，最多 5 秒
tf_->lookupTransform("map", "base", tf2::TimePointZero, 5s);  // ❌

// canTransform：只问「现在有没有」，没有就本拍跳过
if (!tf_->canTransform("map", "base", tf2::TimePointZero)) {
  return;
}
auto t = tf_->lookupTransform("map", "base", tf2::TimePointZero);  // ✅ 确认有了再查
```

和 `service_is_ready` 同一思路：**问一眼，不等。**

### 9.3 定时器比自己的周期还慢

```cpp
void on_timer() { very_slow_work(); }     // ❌ 周期 10ms，活要 50ms，越积越卡
void on_timer() { flag_do_work_ = true; } // ✅ 只竖旗；慢活放到别处
```

### 9.4 关于多线程（先知道有这个东西即可）

复杂节点（Nav2、move_group）有时用 **MultiThreadedExecutor（多线程执行器）** = 多个值班员。  
还要用 **callback group（回调组）** 规定谁可以并行。

零基础阶段：

1. 先保证回调短、会用异步  
2. 不要一上来靠多线程「拯救」阻塞代码  
3. 官方更推荐的稳妥路：少同步等待、多用异步  

---

## 10. 「从错到正」总表

| 场景 | ❌ 错在哪 | ✅ 怎么改 |
|------|-----------|-----------|
| 导航再抓 | 回调里 `while not complete` | 发 Action + 状态 / 短问「好了没」（§5） |
| 开爪再下降 | 一个函数里连续 wait | 状态机 + 结果回调（§6） |
| 电机环 | `update` 里 `wait_for_service` | 控制环和配置服务拆开（§7） |
| 调服务 | 常驻节点里再 `spin_until_future_complete` | `service_is_ready` + 结果回调（§3） |
| 自己调自己 | 同步等本节点服务 | 拆节点或 async（§8） |
| 激光 / TF | 回调里长计算 / 长 timeout | 入队 / `canTransform`（§9） |

---

## 11. 练习怎么跑、日志怎么对上这篇文档

先编译并加载环境：

```bash
cd ~/robot_workspaces/ros_ws
source /opt/ros/jazzy/setup.bash          # 发行版按本机修改
colcon build --packages-select my_package topic_demo
source install/setup.bash
```

### 11.1 先只开常驻节点（看 callback 客户端）

```bash
ros2 run my_package my_node
```

你应该陆续看到类似：

1. `Published: Hello, ROS2! ...` → 定时器在跑  
2. `Received: ...` → 订阅回调在跑  
3. `Service got: 1 + 2 -> 3` → **本节点厨房**算出了结果  
4. `[callback] request sent (non-blocking)` → 请求已发出，函数没死等  
5. `[callback] Result: 3` → 结果回调被 spin 调起来了  

如果第 3 步一直没有、程序「卡住不动」，回头看 §2.4：是不是写成了同步等。

### 11.2 再开脚本客户端（对照 Future 等待）

保持 `my_node` 开着，另开终端：

```bash
source ~/robot_workspaces/ros_ws/install/setup.bash
ros2 run my_package client_future
```

脚本会：

1. 可能打印几次 `wait_for_service(1s) timed out, retry...`（如果节点还没起来）  
2. 然后 `[future] 41 + 1 = 42`  
3. **进程退出**（这就是脚本身份）

Python 同理：

```bash
ros2 run topic_demo py_topic        # 终端1
ros2 run topic_demo client_future   # 终端2
```

### 11.3 建议对照的源码

- `src/my_package/src/my_node.cpp`  
- `src/my_package/src/client_future.cpp`  
- `src/topic_demo/topic_demo/my_topic.py`  
- `src/topic_demo/topic_demo/client_future.py`  

读源码时带着三个问题：

1. main 里有没有长期 `spin`？  
2. 问服务时用的是 `wait_for_service` 还是 `service_is_ready`？  
3. 结果是 `spin_until_future_complete` 取的，还是回调里取的？  

---

## 附录 A：C++ / Python API 对照

| 你想做的事 | Python | C++ | 人话 |
|------------|--------|-----|------|
| 启动 ROS | `rclpy.init` | `rclcpp::init` | 程序开头初始化 |
| 创建节点 | `Node(...)` / `create_node` | `std::make_shared<Node>` | 开一个前台 |
| 创建发布者 | `create_publisher` | 同名 | 拿喇叭 |
| 创建订阅 | `create_subscription` | 同名 | 订一份报纸，来了就调回调 |
| 创建客户端 | `create_client(类型, 名)` | `create_client<类型>(名)` | 拿起话筒 |
| 创建服务端 | `create_service(类型, 名, 回调)` | `create_service<类型>(名, 回调)` | 开始接电话 |
| 阻塞等服务上线 | `wait_for_service(t)` | 同名 | 门口干等，最多 t |
| 立刻问服务在不在 | `service_is_ready()` | 同名 | 扫一眼灯亮没 |
| 异步发请求 | `call_async(req)` | `async_send_request(req)` | 下单拿取餐号 |
| 登记「好了再叫我」 | `future.add_done_callback(cb)` | `async_send_request(req, cb)` | 门铃到了再处理 |
| 脚本里等结果 | `spin_until_future_complete` | 同名 | 边值班边盯取餐号 |
| 取回复 | `future.result()` | `future.get()` | 打开餐盒 |
| 长期值班 | `rclpy.spin(node)` | `rclcpp::spin(node)` | 一直处理回调 |
| 打日志 | `node.get_logger().info(...)` | `RCLCPP_INFO(...)` | 打印信息 |

## 附录 B：两个易混对

| 对比 | A | B | 怎么选 |
|------|---|---|--------|
| 问服务 | `wait_for_service` | `service_is_ready` | 脚本启动用 A；定时器/回调里用 B |
| 等结果 | `spin_until_future_complete` | 结果 callback | 单独脚本用 A；已有 `spin` 的节点用 B |

## 附录 C：零基础自测

能用自己的话答出来，这篇就算读懂了：

1. 为什么创建了定时器却什么都不打印？  
   → 没有 `spin`，回调不会被调用。  
2. 为什么回调里 `sleep(10)` 会让急停变迟钝？  
   → 默认一个值班员；回调不返回，别的回调没法跑。  
3. `async_send_request` 会不会自动等结果？  
   → 不会。等不等取决于你后面用 `spin_until` 还是登记回调。  
4. `my_node` 为什么不能用 `spin_until_future_complete`？  
   → 外层已经在 `spin`；再嵌套等待，服务回调可能永远得不到执行。  
5. 业务上必须「先开爪再下降」，代码上一定要写在一个函数里连续 wait 吗？  
   → 不必。用状态变量记住步骤，每拍只做一点点。

---

**总记三句：**

1. **`spin` 是值班员；默认只有一个，回调必须短。**  
2. **脚本可以门口干等；常驻节点只能扫一眼 + 留言。**  
3. **顺序用状态/行为树表达，不要用「占着值班员死等」表达。**
