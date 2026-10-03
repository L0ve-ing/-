#ifndef NOMINMAX // 条件编译：避免 Windows 宏遮蔽标准库最小值与最大值函数。
#define NOMINMAX // 宏定义：关闭 Windows 的 min 和 max 宏。
#endif // 条件结束：完成兼容性宏设置。
#include <winsock2.h> // 包含指令：必须先于 Windows 头文件引入第二版套接字接口。
#include <ws2tcpip.h> // 包含指令：提供 IPv4 地址转换与网卡地址查询。
#include "network.hpp" // 包含指令：导入公开会话接口。
#include <algorithm> // 标准库头文件：提供查找、比较和最小值算法。
#include <chrono> // 标准库头文件：提供不受系统时间调整影响的连接计时。
#include <sstream> // 标准库头文件：提供仅含数字的文本协议编解码。
#include <utility> // 标准库头文件：提供移动赋值所需工具。
namespace forest { // 命名空间：以下实现属于雾隐之森联网模块。
namespace { // 匿名命名空间：内部协议辅助函数不暴露给其他源文件。
using Clock = std::chrono::steady_clock; // 类型别名：使用单调时钟计算超时。
constexpr int Version = 2; // 编译期常量：第二版协议支持可重复侦察。
constexpr std::size_t MaxLine = 4096; // 编译期常量：每条协议行最长四千零九十六字节。
constexpr std::size_t MaxBuffer = 65536; // 编译期常量：限制输入和输出队列，防止无界增长。
constexpr int ConnectSeconds = 10; // 编译期常量：连接与握手各最多等待十秒。
constexpr int SilentSeconds = 30; // 编译期常量：已连接后持续三十秒收不到任何消息则断开。
void closeSocket(SOCKET& socket) { // 引用参数函数：关闭并重置一个套接字句柄。
    if (socket != INVALID_SOCKET) closesocket(socket); // 条件调用：有效套接字才执行释放。
    socket = INVALID_SOCKET; // 赋值：后续重复清理保持安全。
} // 右花括号：结束套接字释放函数。
bool makeNonblocking(SOCKET socket) { // 辅助函数：设置非阻塞收发，避免界面线程停顿。
    u_long enabled = 1; // 局部变量：非零表示开启非阻塞模式。
    return ioctlsocket(socket, FIONBIO, &enabled) == 0; // 地址运算符：把模式指针传给系统并检查返回值。
} // 右花括号：结束非阻塞设置。
bool completed(std::istringstream& input) { // 辅助函数：检查一行是否已准确读完。
    input >> std::ws; // 流提取：允许末尾空白字符。
    return !input.bad() && input.eof(); // 布尔表达式：禁止合法消息后附带额外字段。
} // 右花括号：结束解析结束检查。
bool flag(int value) { return value == 0 || value == 1; } // 简短函数：协议中的布尔字段仅接受零和一。
int maskOf(const std::array<bool, 9>& possible) { // 辅助函数：将九格候选集合压缩为九位整数。
    int mask = 0; // 初始化：空集合对应零。
    for (int cell = 0; cell < 9; ++cell) if (possible[cell]) mask |= 1 << cell; // 位运算与循环：为每个候选格设置对应位。
    return mask; // 返回：协议中不需要传输中文或结构体内存。
} // 右花括号：结束候选集合压缩。
std::wstring publicText(int actor, studio::Action action, bool hit) { // 辅助函数：由脱敏字段生成公共日志。
    const std::wstring name = L"玩家 " + std::to_wstring(actor + 1); // 字符串拼接：界面编号从一开始。
    if (action.kind == studio::Kind::Move) return name + L" 移动了一格。"; // 提前返回：移动终点永远不写入公开信息。
    if (action.kind == studio::Kind::Attack) return name + L" 攻击 " + std::to_wstring(action.target) + (hit ? L"：命中，获胜！" : L"：未命中。"); // 条件表达式：只公开攻击目标和是否命中。
    return name + L" 侦察第 " + std::to_wstring(action.target + 1) + (action.kind == studio::Kind::ScanRow ? L" 行。" : L" 列。"); // 返回：侦察区域公开，结果保密。
} // 右花括号：结束公共日志生成。
struct Note { // 内部结构体：保存一位玩家最近一次私有侦察结果。
    int at = 0; // 默认成员初始化：零表示本局尚未侦察。
    int kind = 2; // 默认成员初始化：二表示行侦察。
    int target = 0; // 默认成员初始化：行列索引从零开始。
    int positive = 0; // 默认成员初始化：侦察二值结果。
}; // 分号：结束私有备注结构体。
std::wstring noteText(const Note& note) { // 辅助函数：只为本地玩家构建私密提示。
    if (note.at == 0) return L""; // 提前返回：没有侦察记录时不显示旧信息。
    return L"第 " + std::to_wstring(note.at) + L" 次行动：第 " + std::to_wstring(note.target + 1) + (note.kind == 2 ? L" 行" : L" 列") + (note.positive ? L"当时有对手。" : L"当时没有对手。") + L"对手移动后，结果可能过时。"; // 返回：明确结果所属时刻，防止被当作持续探测。
} // 右花括号：结束私密提示生成。
bool sameView(const studio::View& a, const studio::View& b) { // 辅助函数：复盘重建后核对客户端收到的最终本人视图。
    return a.own == b.own && a.possible == b.possible && a.scanAvailable == b.scanAvailable && a.enemyLastAttacked == b.enemyLastAttacked && a.consecutiveMisses == b.consecutiveMisses && a.actions == b.actions; // 布尔表达式：所有公开或本人字段必须一致。
} // 右花括号：结束视图核对。
} // 右花括号：结束内部辅助命名空间。
struct Session::Impl { // 隐藏实现结构体：由 Session 独占，不向界面暴露裁判信息。
    bool winsock = false, hostRole = false, handshaken = false, connecting = false, ownReady = false, readyView = false, dirty = true; // 布尔成员：分别表示资源、身份、握手、连接、起点、视图和刷新状态。
    bool scan = true, pending = false; // 布尔成员：侦察规则和客人待确认行动。
    SOCKET listener = INVALID_SOCKET, peer = INVALID_SOCKET; // 套接字成员：监听器和唯一对手连接。
    Phase stage = Phase::Idle; // 枚举成员：新会话处于空闲阶段。
    int round = 1, first = 0, currentTurn = -1, result = -1, count = 0; // 整数成员：局号、先手、当前玩家、结果和有效行动数。
    std::array<int, 2> starts{0, 0}; // 数组成员：仅房主裁判拥有双方提交的起点。
    studio::View ownView{}; // 聚合零初始化：尚未开局时不携带任何真实对手信息。
    std::array<Note, 2> notes{}; // 数组成员：房主分别维护双方的私密结果。
    std::vector<studio::Event> events; // 动态数组成员：只保存可以公开的脱敏事件。
    std::shared_ptr<studio::Game> game, finishedReplay; // 共享指针成员：前者仅房主持有，后者仅终局后可读取。
    std::shared_ptr<studio::Game> pendingCompleted; // 共享指针成员：保留尚未交付界面的终局通知，不因下一局或断线丢失。
    std::wstring privateMessage, message = L"尚未连接。"; // 宽字符串成员：分别用于私密提示与连接状态。
    std::string incoming, outgoing; // 字符串缓冲区：处理分片接收和部分发送。
    Clock::time_point deadline = Clock::now(), lastReceive = Clock::now(), lastPing = Clock::now(); // 时间点成员：控制连接与存活超时。
    Impl() { // 构造函数：为该对象获取一份 Winsock 使用引用。
        WSADATA data{}; // 聚合初始化：清空系统网络信息结构体。
        winsock = WSAStartup(MAKEWORD(2, 2), &data) == 0; // 系统调用：请求第二版套接字 API。
        if (!winsock) fail(L"无法初始化 Windows 网络组件。"); // 条件分支：保存可显示的初始化错误。
    } // 右花括号：结束隐藏实现构造。
    ~Impl() { // 析构函数：按资源获取顺序的逆序进行清理。
        disconnect(); // 函数调用：先关闭全部套接字。
        if (winsock) WSACleanup(); // 条件调用：仅释放成功申请的网络引用。
    } // 右花括号：结束隐藏实现析构。
    int localPlayer() const { return hostRole ? 0 : 1; } // 简短常量函数：房主固定为零，客人固定为一。
    void disconnect() { // 成员函数：仅释放传输资源，保留可供界面读取的错误或终局记录。
        closeSocket(listener); // 函数调用：停止接受新连接。
        closeSocket(peer); // 函数调用：关闭当前对手。
        handshaken = false; // 赋值：清除已连通状态。
        connecting = false; // 赋值：清除异步连接状态。
        incoming.clear(); // 容器调用：丢弃未完成的接收消息。
        outgoing.clear(); // 容器调用：丢弃未发送的请求。
        pending = false; // 赋值：没有连接时不继续等待行动确认。
    } // 右花括号：结束传输释放。
    void fail(const std::wstring& reason) { // 成员函数：统一记录错误并关闭连接。
        disconnect(); // 函数调用：错误后不再执行已排队的消息。
        stage = Phase::Error; // 枚举赋值：界面可提供返回或重新建房。
        message = reason; // 字符串赋值：保存适合用户阅读的原因。
        dirty = true; // 赋值：通知界面重绘状态。
    } // 右花括号：结束错误处理。
    void clearRound() { // 成员函数：开始新局时清除上一局隐藏信息与输入锁。
        starts = {0, 0}; // 数组赋值：双方重新秘密选点。
        notes = {}; // 聚合赋值：私密侦察记录从空开始。
        ownReady = false; // 布尔赋值：本机尚未提交起点。
        readyView = false; // 布尔赋值：双方就绪前不提供对局视图。
        pending = false; // 布尔赋值：没有未确认行动。
        ownView = {}; // 聚合赋值：清空上一局的位置与候选集合。
        currentTurn = -1; // 整数赋值：开局前没有行动者。
        result = -1; // 整数赋值：新局尚无结果。
        count = 0; // 整数赋值：行动序号从零开始。
        game.reset(); // 智能指针调用：释放上一局裁判对象。
        finishedReplay.reset(); // 智能指针调用：新局不能查看上一局遗留的隐藏状态。
        events.clear(); // 容器调用：公共日志从空开始。
        privateMessage.clear(); // 容器调用：清除上局私有提示。
        stage = Phase::Setup; // 枚举赋值：进入双方同时选点阶段。
        message = L"已连接，请秘密选择自己的起点。"; // 宽字符串赋值：提示选点，不暴露对方是否已选具体位置。
        dirty = true; // 赋值：让界面响应新局状态。
    } // 右花括号：结束新局重置。
    bool queue(const std::string& text) { // 成员函数：将完整文本消息加入有界发送队列。
        if (text.size() > MaxLine || outgoing.size() + text.size() + 1 > MaxBuffer) { fail(L"网络消息过长，连接已结束。"); return false; } // 边界检查：拒绝超长消息和积压。
        outgoing += text + '\n'; // 字符串追加：换行作为消息边界，支持 TCP 分片。
        return true; // 返回真：消息已入队，下一次 poll 非阻塞发送。
    } // 右花括号：结束消息排队。
    void setPlayingPhase() { // 成员函数：从裁判结果或本人视图决定界面状态。
        if (result >= 0) { // 分支：已收到终局结果。
            stage = finishedReplay ? Phase::Finished : Phase::Waiting; // 条件表达式：客人先完成复盘校验，再提供结束界面。
            message = finishedReplay ? (result == 2 ? L"本局达到 80 次行动，记为未决和局。" : L"本局结束，可以查看完整复盘。") : L"本局结束，正在接收复盘。"; // 字符串赋值：区分结果接收和复盘可用。
        } else { // 否则分支：对局继续。
            stage = currentTurn == localPlayer() && !pending ? Phase::Playing : Phase::Waiting; // 条件表达式：只有本人回合且无待确认请求时开放操作。
            message = stage == Phase::Playing ? L"轮到你行动。" : L"等待对方行动。"; // 状态文字：帮助两台电脑上的玩家同步轮次。
        } // 右花括号：结束结束或进行中的选择。
        dirty = true; // 布尔赋值：视图或阶段变化需要重绘。
    } // 右花括号：结束对局阶段更新。
    void appendPublic(const studio::Event& source) { // 成员函数：裁判原始事件必须经过这里脱敏。
        studio::Event safe = source; // 按值复制：不修改裁判用于复盘的完整事件。
        if (safe.action.kind == studio::Kind::Move) safe.action.target = 0; // 条件赋值：移动终点不允许流出公开日志。
        safe.scanPositive = false; // 布尔赋值：公共事件始终移除侦察结果。
        safe.privateText.clear(); // 字符串清空：公共事件始终移除私密提示。
        events.push_back(std::move(safe)); // 移动追加：保存安全的日志副本。
    } // 右花括号：结束公共日志脱敏。
    void sendState() { // 成员函数：房主仅为客人编码其允许查看的状态。
        const studio::View remote = game->view(1); // 常量值：取得玩家一自己的视图，绝不发送玩家零的真实位置。
        const Note& note = notes[1]; // 常量引用：只读取客人本人的侦察备注。
        std::ostringstream output; // 输出字符串流：逐字段构造可校验的 ASCII 消息。
        output << "STATE " << round << ' ' << count << ' ' << currentTurn << ' ' << result << ' ' << remote.own << ' ' << maskOf(remote.possible) << ' ' << remote.scanAvailable << ' ' << remote.enemyLastAttacked << ' ' << remote.consecutiveMisses << ' ' << remote.actions << ' ' << note.at << ' ' << note.kind << ' ' << note.target << ' ' << note.positive; // 插入运算符：写入本人信息与私有侦察记录。
        if (events.empty()) output << " -1 0 0 0"; // 条件分支：初始状态没有上一条公共事件。
        else { const auto& event = events.back(); output << ' ' << event.actor << ' ' << static_cast<int>(event.action.kind) << ' ' << event.action.target << ' ' << event.hit; } // 否则分支：追加已经脱敏的最新事件。
        queue(output.str()); // 函数调用：将状态交给非阻塞发送队列。
    } // 右花括号：结束客人状态编码。
    void sendReplay() { // 成员函数：终局后才传输完整起点与动作。
        std::ostringstream output; // 局部字符串流：拼接完整复盘的紧凑表示。
        output << "REPLAY " << round << ' ' << starts[0] << ' ' << starts[1] << ' ' << first << ' ' << static_cast<int>(scan) << ' ' << count; // 写入：本局初始条件和记录长度。
        for (const auto& event : game->events()) output << ' ' << static_cast<int>(event.action.kind) << ' ' << event.action.target; // 范围循环：终局时允许传送完整移动终点。
        queue(output.str()); // 函数调用：排在终局 STATE 后面，客户端按序校验。
    } // 右花括号：结束完整复盘发送。
    void publish() { // 成员函数：房主执行合法行动后同时更新本地视图与客人状态。
        ownView = game->view(0); // 赋值：房主界面只拿到自己的允许视图。
        readyView = true; // 布尔赋值：对局已经正式开始。
        currentTurn = game->turn(); // 函数调用：从裁判读取下一位行动者。
        result = game->winner(); // 函数调用：从裁判读取结束状态。
        count = static_cast<int>(game->events().size()); // 显式转换：规则最多八十步，可安全转为整数。
        privateMessage = noteText(notes[0]); // 函数调用：只给房主展示自己的侦察备注。
        if (game->finished()) { finishedReplay = game; pendingCompleted = game; } // 条件赋值：终局解锁完整快照，并保留一次可跨阶段读取的完成通知。
        sendState(); // 函数调用：把另一位玩家自己的状态发给对方。
        if (stage == Phase::Error) return; // 提前返回：编码或队列错误不能被后续状态覆盖。
        if (game->finished()) sendReplay(); // 条件调用：完整行动数据只在比赛结束后发送。
        if (stage != Phase::Error) setPlayingPhase(); // 条件调用：发送成功后更新本地操作阶段。
    } // 右花括号：结束房主状态发布。
    void beginIfReady() { // 成员函数：双方秘密起点齐备后创建裁判。
        if (!starts[0] || !starts[1]) return; // 提前返回：只有一方选好时继续保密等待。
        game = std::make_shared<studio::Game>(starts[0], starts[1], scan, first); // 工厂函数：相同起点也允许，先手由本局约定决定。
        publish(); // 函数调用：分别发送双方自己的初始视图。
    } // 右花括号：结束双方就绪检查。
    void apply(studio::Action action) { // 成员函数：只有通过回合与合法性校验后才可调用。
        if (!game->act(action)) { fail(L"行动校验失败，连接已结束。"); return; } // 防御分支：裁判拒绝动作时不尝试继续同步。
        const auto& event = game->events().back(); // 常量引用：读取刚执行的完整事件。
        if (action.kind == studio::Kind::ScanRow || action.kind == studio::Kind::ScanCol) notes[event.actor] = {static_cast<int>(game->events().size()), static_cast<int>(action.kind), action.target, static_cast<int>(event.scanPositive)}; // 条件赋值：侦察可反复使用，每次覆盖行动者最新私有结果。
        appendPublic(event); // 函数调用：将日志脱敏后再供界面与协议读取。
        publish(); // 函数调用：同步合法行动的结果。
    } // 右花括号：结束合法行动执行。
    bool readState(std::istringstream& input) { // 成员函数：客人检查一条状态更新，拒绝超界和乱序字段。
        int serial, steps, next, winnerValue, own, mask, available, attacked, misses, actions, actor, kind, target, hit; // 局部整数：按协议顺序接收字段。
        Note note; // 局部结构体：接收客人本人侦察信息。
        if (!(input >> serial >> steps >> next >> winnerValue >> own >> mask >> available >> attacked >> misses >> actions >> note.at >> note.kind >> note.target >> note.positive >> actor >> kind >> target >> hit) || !completed(input)) return false; // 提取与短路：字段缺失或多余均拒绝。
        if (serial != round || !ownReady || result != -1 || steps != (readyView ? count + 1 : 0) || steps > 80 || !flag(next) || winnerValue < -1 || winnerValue > 2 || !studio::validCell(own) || mask < 1 || mask > 511 || !flag(available) || available != static_cast<int>(scan) || !flag(attacked) || misses < 0 || misses > 80 || actions < 0 || actions > 80) return false; // 校验：状态必须属于当前局并严格前进一步。
        if (note.at < 0 || note.at > steps || note.kind < 2 || note.kind > 3 || note.target < 0 || note.target > 2 || !flag(note.positive) || !flag(hit)) return false; // 校验：私有信息必须落在有效时间与区域内。
        if (steps == 0) { // 分支：双方刚就绪的初始状态。
            if (actor != -1 || kind != 0 || target != 0 || hit != 0 || next != first || winnerValue != -1 || own != starts[1] || note.at != 0 || mask != 511 || actions != 0 || misses != 0 || attacked != 0) return false; // 校验：初始状态不能夹带额外事件或错误起点。
        } else { // 否则分支：每条后续状态恰好对应一次公开行动。
            if (actor != currentTurn || kind < 0 || kind > 3 || (kind == 0 && target != 0) || (kind == 1 && !studio::validCell(target)) || (kind >= 2 && (target < 0 || target > 2)) || (hit && kind != 1)) return false; // 校验：移动终点必须被抹去，公开目标必须有效。
            if (winnerValue == -1 && next != 1 - actor) return false; // 校验：未结束行动必须交换回合。
            if (winnerValue >= 0 && next != actor) return false; // 校验：结束时裁判保留最后行动者。
            if ((winnerValue == 0 || winnerValue == 1) && (!hit || winnerValue != actor)) return false; // 校验：只有命中者能够获胜。
            if (hit && winnerValue != actor) return false; // 校验：命中不能继续对局或判给另一方。
            if (winnerValue == 2 && (steps != 80 || hit)) return false; // 校验：未决和局只在第八十个未命中行动后产生。
            if (winnerValue == -1 && steps == 80) return false; // 校验：达到动作上限必须结束。
            const studio::Action action{static_cast<studio::Kind>(kind), target}; // 聚合初始化：目标只含已脱敏的公开字段。
            events.push_back({actor, action, hit != 0, false, publicText(actor, action, hit != 0), L""}); // 容器追加：客户端公共事件的私密字段恒为空。
        } // 右花括号：结束初始或后续状态校验。
        ownView = {own, {}, available != 0, attacked != 0, misses, actions}; // 聚合赋值：只替换客人自身的受限视图。
        for (int cell = 0; cell < 9; ++cell) ownView.possible[cell] = (mask & (1 << cell)) != 0; // 位运算循环：展开候选集合。
        notes[1] = note; // 赋值：客人只保存自己的侦察备注。
        privateMessage = noteText(note); // 函数调用：生成带时刻的中文结果。
        currentTurn = next; // 整数赋值：同步下一位行动者。
        result = winnerValue; // 整数赋值：同步终局或进行中状态。
        count = steps; // 整数赋值：确认此次请求对应的行动序号。
        readyView = true; // 布尔赋值：已有可供界面读取的个人状态。
        pending = false; // 布尔赋值：权威结果到达后解除请求锁。
        setPlayingPhase(); // 函数调用：根据当前玩家和复盘是否齐备调整阶段。
        return true; // 返回真：状态通过基本协议校验。
    } // 右花括号：结束客人状态解析。
    bool readReplay(std::istringstream& input) { // 成员函数：终局时用完整动作重建并校验复盘。
        int serial, p0, p1, firstValue, scanValue, steps; // 局部变量：读取初始条件。
        if (!(input >> serial >> p0 >> p1 >> firstValue >> scanValue >> steps) || serial != round || result < 0 || finishedReplay || !studio::validCell(p0) || !studio::validCell(p1) || p1 != starts[1] || firstValue != first || scanValue != static_cast<int>(scan) || steps != count || steps < 1 || steps > 80) return false; // 防御检查：只有当前终局可以提供匹配的回放。
        auto rebuilt = std::make_shared<studio::Game>(p0, p1, scan, first); // 智能指针工厂：从真实初始局面逐步重放。
        for (int index = 0; index < steps; ++index) { // 循环：完整动作最多八十个。
            int kind, target; // 局部变量：每次读取一条完整行动。
            if (!(input >> kind >> target) || kind < 0 || kind > 3 || !rebuilt->act({static_cast<studio::Kind>(kind), target})) return false; // 校验：任何非法移动、攻击或侦察使整份回放失效。
            const auto& full = rebuilt->events().back(); // 常量引用：从本地规则生成公开与私有结果。
            const auto& safe = events[static_cast<std::size_t>(index)]; // 常量引用：取此前实时收到的公共事件。
            if (full.actor != safe.actor || full.action.kind != safe.action.kind || (full.action.kind != studio::Kind::Move && full.action.target != safe.action.target) || full.hit != safe.hit) return false; // 一致性检查：终局数据不能改写对局中的公开历史。
            if (index + 1 == notes[1].at && (full.actor != 1 || static_cast<int>(full.action.kind) != notes[1].kind || full.action.target != notes[1].target || static_cast<int>(full.scanPositive) != notes[1].positive)) return false; // 一致性检查：最后一次本人侦察必须符合实际棋局。
        } // 右花括号：结束行动重放。
        if (!completed(input) || !rebuilt->finished() || rebuilt->winner() != result || !sameView(rebuilt->view(1), ownView)) return false; // 最终校验：结果与自己的已知状态都必须一致。
        finishedReplay = std::move(rebuilt); // 移动赋值：仅在全部验证通过后解锁复盘。
        pendingCompleted = finishedReplay; // 共享赋值：后续同批次新局或断线消息不会抹去已完成比赛。
        setPlayingPhase(); // 函数调用：切换到可查看完整回放的结束页面。
        return true; // 返回真：回放完整且与实时数据一致。
    } // 右花括号：结束复盘解析。
    bool receiveLine(const std::string& line) { // 成员函数：解析一条完整、有界的协议消息。
        std::istringstream input(line); // 字符串流：仅将数字字段转换为整数。
        std::string command; // 字符串变量：接收消息类型。
        if (!(input >> command)) return false; // 提前返回：空行不是有效协议消息。
        if (!handshaken) { // 分支：握手成功前只接受版本及房间信息。
            if (hostRole) { // 分支：房主等待客人发送协议版本。
                int version; // 整数变量：读取客人版本。
                if (command != "HELLO" || !(input >> version) || !completed(input)) return false; // 字段检查：握手前不能夹带游戏行动。
                if (version != Version) { fail(L"双方游戏版本不同，请使用同一份新版程序。"); return true; } // 版本检查：不兼容时明确提示重下程序。
                handshaken = true; // 布尔赋值：确认双方协议一致。
                clearRound(); // 函数调用：开启双方秘密选点。
                queue("WELCOME " + std::to_string(Version) + " " + std::to_string(static_cast<int>(scan)) + " " + std::to_string(round) + " " + std::to_string(first)); // 排队：告诉客人房间规则与当前先手。
                return true; // 返回真：房主握手完成。
            } // 右花括号：结束房主握手分支。
            int version, scanValue, serial, firstValue; // 局部变量：接收房主的协议与规则。
            if (command != "WELCOME" || !(input >> version >> scanValue >> serial >> firstValue) || !completed(input)) return false; // 检查：客人只能接受完整欢迎消息。
            if (version != Version) { fail(L"双方游戏版本不同，请使用同一份新版程序。"); return true; } // 版本检查：拒绝无法解释的旧规则。
            if (!flag(scanValue) || serial != 1 || firstValue != 0) return false; // 范围检查：新连接始终从第一局房主先手开始。
            scan = scanValue != 0; // 布尔赋值：客人采用房主选择的侦察规则。
            round = serial; // 整数赋值：同步局号。
            first = firstValue; // 整数赋值：同步先手。
            handshaken = true; // 布尔赋值：客人完成版本确认。
            clearRound(); // 函数调用：进入自己的秘密选点页面。
            return true; // 返回真：欢迎消息已处理。
        } // 右花括号：结束握手分支。
        if (command == "PING") return completed(input) && queue("PONG"); // 心跳请求：无游戏数据，只证明连接仍有响应。
        if (command == "PONG") return completed(input); // 心跳回应：收到任意完整消息都会刷新存活时间。
        if (hostRole) { // 分支：房主只接受客人的选点或行动请求。
            if (command == "START") { // 分支：客人提交秘密起点。
                int serial, cell; // 局部整数：分别为当前局号与秘密位置。
                if (!(input >> serial >> cell) || !completed(input) || serial != round || game || starts[1] != 0 || !studio::validCell(cell)) return false; // 校验：防止重复或越界起点改变已经布置的棋局。
                starts[1] = cell; // 裁判赋值：客人位置只存在房主私有状态中。
                beginIfReady(); // 函数调用：如果房主也已选点，则正式开局。
                return true; // 返回真：已接受客人的秘密位置。
            } // 右花括号：结束起点请求处理。
            if (command == "ACT") { // 分支：客人请求执行一次行动。
                int serial, expected, kind, target; // 局部整数：使用局号和预期序号防止重复执行。
                if (!(input >> serial >> expected >> kind >> target) || !completed(input) || serial != round || !game || game->finished() || expected != count || game->turn() != 1 || kind < 0 || kind > 3 || !game->legal({static_cast<studio::Kind>(kind), target})) return false; // 严格校验：过期、重复、越权或非法行动一律拒绝且不改变棋局。
                apply({static_cast<studio::Kind>(kind), target}); // 函数调用：仅权威裁判执行一次已验证动作。
                return true; // 返回真：动作结果已排队同步。
            } // 右花括号：结束客人行动请求。
            return false; // 返回假：房主不接受客人伪造状态、回放或新局命令。
        } // 右花括号：结束房主协议分支。
        if (command == "STATE") return readState(input); // 客人分支：受限本人状态由房主提供。
        if (command == "REPLAY") return readReplay(input); // 客人分支：完整数据只允许在终局后读取。
        if (command == "ROUND") { // 分支：房主在结束后发起再来一局。
            int serial, firstValue; // 局部整数：新局号与交换后的先手。
            if (!(input >> serial >> firstValue) || !completed(input) || stage != Phase::Finished || serial != round + 1 || firstValue != 1 - first) return false; // 严格校验：不允许跳过当前对局或重复重置。
            round = serial; // 赋值：确认新的局号。
            first = firstValue; // 赋值：上一局后手变为本局先手。
            clearRound(); // 函数调用：清空对局并重新选择秘密起点。
            return true; // 返回真：新局准备完成。
        } // 右花括号：结束新局命令处理。
        return false; // 返回假：未知消息禁止悄悄执行。
    } // 右花括号：结束协议分发。
    void flush() { // 成员函数：在有限循环内发送已排队的字节。
        for (int attempts = 0; attempts < 32 && peer != INVALID_SOCKET && !connecting && !outgoing.empty(); ++attempts) { // 循环限制：单次界面计时器不会无限占用时间。
            const int length = static_cast<int>(std::min<std::size_t>(outgoing.size(), 4096)); // 长度转换：每次发送最多四千零九十六字节。
            const int sent = send(peer, outgoing.data(), length, 0); // 非阻塞发送：返回实际成功写入的字节数。
            if (sent > 0) { outgoing.erase(0, static_cast<std::size_t>(sent)); continue; } // 部分发送处理：只移除已经送出的前缀。
            if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) return; // 可重试错误：系统缓冲暂满时留到下一次 poll。
            fail(L"发送失败，对方可能已离开房间。"); // 其他错误：结束当前连接并保留界面提示。
            return; // 提前返回：错误后不再访问无效套接字。
        } // 右花括号：结束有界发送循环。
    } // 右花括号：结束队列发送。
    void receive() { // 成员函数：读取分片字节并逐行解析。
        for (int attempts = 0; attempts < 32 && peer != INVALID_SOCKET; ++attempts) { // 有界循环：不会因恶意持续输入阻塞界面。
            char buffer[2048]; // 栈上字符数组：接收一小段 TCP 数据。
            const int received = recv(peer, buffer, static_cast<int>(sizeof(buffer)), 0); // 非阻塞读取：允许收到半行或多行消息。
            if (received == 0) { fail(L"对方已断开连接，可以返回重新建房或加入。"); return; } // 有序关闭：及时停止等待对手。
            if (received == SOCKET_ERROR) { // 分支：读取失败时区分暂时无数据与真正错误。
                if (WSAGetLastError() == WSAEWOULDBLOCK) return; // 提前返回：当前没有数据属于正常情况。
                fail(L"网络连接中断，可以返回重新建房或加入。"); // 错误处理：物理断线或套接字失败。
                return; // 提前返回：不再读取已关闭连接。
            } // 右花括号：结束读取错误分支。
            incoming.append(buffer, static_cast<std::size_t>(received)); // 字符串追加：保留跨越多次接收的消息片段。
            if (incoming.size() > MaxBuffer) { fail(L"收到过量网络数据，连接已结束。"); return; } // 缓冲上限：拒绝无限积累的对方输入。
            for (;;) { // 内层循环：提取当前缓冲中已经完整到达的行。
                const std::size_t end = incoming.find('\n'); // 字符串查找：换行符确定一条消息边界。
                if (end == std::string::npos) break; // 跳出：半行保留到下次接收。
                if (end > MaxLine) { fail(L"收到过长网络消息，连接已结束。"); return; } // 行长度检查：限制解析开销。
                const std::string line = incoming.substr(0, end); // 子串复制：获得独立完整消息。
                incoming.erase(0, end + 1); // 容器删除：移除已取出的消息及换行。
                if (!receiveLine(line)) { fail(L"收到无效、重复或过期的游戏消息，连接已结束。"); return; } // 协议错误：拒绝继续使用可能不同步的局面。
                lastReceive = Clock::now(); // 时间赋值：只有完整合法消息才算存活。
                if (peer == INVALID_SOCKET) return; // 防御返回：版本不兼容等分支可能已经关闭连接。
            } // 右花括号：结束完整行解析。
            if (incoming.size() > MaxLine) { fail(L"收到过长网络消息，连接已结束。"); return; } // 半行长度检查：没有换行也不能无限增长。
        } // 右花括号：结束有界接收循环。
    } // 右花括号：结束消息接收。
    void connectedTransport() { // 成员函数：TCP 连通后进入版本握手。
        connecting = false; // 布尔赋值：传输连接已经建立。
        deadline = Clock::now() + std::chrono::seconds(ConnectSeconds); // 时间赋值：重新为握手计时。
        lastReceive = Clock::now(); // 时间赋值：初始化存活记录。
        lastPing = Clock::now(); // 时间赋值：连接后稍等再发送心跳。
        const BOOL enabled = TRUE; // 常量：同时启用系统层连接存活检测。
        setsockopt(peer, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&enabled), sizeof(enabled)); // 系统调用：TCP 底层也可识别长期失联。
        message = L"已连接，正在确认游戏版本。"; // 状态赋值：传输已连通但选点尚未开放。
        dirty = true; // 布尔赋值：提示界面更新连接进度。
        if (!hostRole) queue("HELLO " + std::to_string(Version)); // 客人发送：房主等到版本一致后再开放游戏。
    } // 右花括号：结束 TCP 就绪处理。
    void tick() { // 成员函数：一次界面计时器所需的全部非阻塞工作。
        if (listener != INVALID_SOCKET) { // 分支：建房后尚未接入朋友。
            peer = accept(listener, nullptr, nullptr); // 非阻塞接受：没有朋友到达时立即返回。
            if (peer != INVALID_SOCKET) { // 分支：收到唯一一位客人的连接。
                closeSocket(listener); // 关闭监听：房间仅容纳两个人。
                if (!makeNonblocking(peer)) { fail(L"无法配置网络连接。"); return; } // 防御处理：不能阻塞界面线程。
                connectedTransport(); // 函数调用：启动有限时间的版本握手。
            } else if (WSAGetLastError() != WSAEWOULDBLOCK) { fail(L"房间监听失败，请返回重新建房。"); return; } // 监听错误：允许用户重新创建会话。
        } // 右花括号：结束新连接接受。
        if (peer == INVALID_SOCKET) return; // 提前返回：等待朋友时没有更多传输工作。
        if (connecting) { // 分支：检查异步连接是否已完成。
            fd_set writable, errors; // 局部集合：分别检查可写和异常连接状态。
            FD_ZERO(&writable); // 宏调用：清空可写集合。
            FD_ZERO(&errors); // 宏调用：清空异常集合。
            FD_SET(peer, &writable); // 宏调用：监听该连接的建立结果。
            FD_SET(peer, &errors); // 宏调用：同时监听明确的连接错误。
            timeval immediate{}; // 零初始化：select 必须立即返回，不等待网络。
            const int selected = select(0, nullptr, &writable, &errors, &immediate); // 非阻塞状态查询：Winsock 忽略第一个参数。
            if (selected == SOCKET_ERROR) { fail(L"无法检查连接状态。"); return; } // 错误处理：套接字状态查询失败。
            if (selected > 0) { // 分支：连接已经成功或失败。
                int error = 0, size = sizeof(error); // 局部变量：读取系统记录的实际连接错误。
                if (getsockopt(peer, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0 || error != 0 || FD_ISSET(peer, &errors)) { fail(L"无法连接房主，请检查地址、端口和房间是否已创建。"); return; } // 失败处理：包括没有服务器和地址不可达。
                connectedTransport(); // 函数调用：TCP 成功后进入版本握手。
            } // 右花括号：结束连接完成分支。
        } // 右花括号：结束异步连接检查。
        if (!handshaken && Clock::now() > deadline) { fail(L"连接或版本确认超时，请检查网络后重试。"); return; } // 超时检查：不让界面无限等待失败连接。
        if (connecting) return; // 提前返回：连接未完成前不能收发应用消息。
        flush(); // 函数调用：先发出已经排队的握手或玩家请求。
        if (peer == INVALID_SOCKET) return; // 防御返回：发送错误可能已经关闭套接字。
        receive(); // 函数调用：处理对方的完整消息和断线。
        if (peer == INVALID_SOCKET) return; // 防御返回：无效消息或断线已经结束本次处理。
        if (handshaken && Clock::now() - lastReceive > std::chrono::seconds(SilentSeconds)) { fail(L"对方长时间没有响应，连接已结束。"); return; } // 存活检查：拔网线等无有序关闭场景也能最终恢复。
        if (handshaken && Clock::now() - lastPing > std::chrono::seconds(5)) { queue("PING"); lastPing = Clock::now(); } // 定时心跳：空闲选点与思考期间维持连接检测。
        flush(); // 函数调用：尽快送出本次收到请求后生成的响应。
    } // 右花括号：结束会话推进。
}; // 分号：结束隐藏实现定义。
Session::Session() : impl_(std::make_unique<Impl>()) {} // 构造函数与初始化列表：创建独占隐藏实现。
Session::~Session() = default; // 默认析构：独占指针自动释放网络实现。
void Session::close() { // 成员函数：让同一个对象能够重新建房或加入。
    impl_->disconnect(); // 调用：关闭旧连接。
    impl_->clearRound(); // 调用：清除旧对局和复盘。
    impl_->pendingCompleted.reset(); // 智能指针调用：主动离开或全新连接才清除尚未领取的完成通知。
    impl_->round = 1; // 赋值：新连接从第一局开始。
    impl_->first = 0; // 赋值：新房间默认房主先手。
    impl_->stage = Phase::Idle; // 赋值：恢复空闲阶段。
    impl_->message = L"尚未连接。"; // 赋值：恢复初始提示。
} // 右花括号：结束主动关闭。
bool Session::host(unsigned short port, bool scan) { // 成员函数：创建一个等待朋友加入的房间。
    close(); // 调用：重新建房前清理原连接。
    auto& state = *impl_; // 引用别名：缩短实现对象的访问表达式。
    state.hostRole = true; // 赋值：本地作为玩家零和权威裁判。
    state.scan = scan; // 赋值：该房间双方统一采用此侦察规则。
    if (!state.winsock || port == 0) { state.fail(L"无法创建房间，请使用 1 到 65535 的端口。"); return false; } // 参数检查：端口零不会自动选择隐藏端口。
    state.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // 系统调用：创建 IPv4 TCP 监听器。
    if (state.listener == INVALID_SOCKET) { state.fail(L"无法创建网络套接字。"); return false; } // 错误分支：资源不足或网络组件失败。
    const BOOL exclusive = TRUE; // 常量：避免其他进程复用同一个房间端口。
    if (setsockopt(state.listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0 || !makeNonblocking(state.listener)) { state.fail(L"无法配置房间网络。"); return false; } // 配置：要求端口独占且绝不阻塞。
    sockaddr_in address{}; // 聚合初始化：清空监听地址结构体。
    address.sin_family = AF_INET; // 赋值：使用 IPv4 地址族。
    address.sin_addr.s_addr = htonl(INADDR_ANY); // 字节序转换：接受本机所有网卡到该端口的连接。
    address.sin_port = htons(port); // 字节序转换：网络协议中的端口使用大端顺序。
    if (bind(state.listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(state.listener, 1) != 0) { state.fail(L"无法开启房间，端口可能已被占用，请换一个端口。"); return false; } // 系统调用：绑定并监听唯一客人。
    state.stage = Phase::Listening; // 赋值：尚未握手前一直显示等待朋友。
    state.message = L"房间已创建，等待朋友加入。"; // 宽字符串赋值：提示房主分享地址与端口。
    return true; // 返回真：监听已建立，不代表已有客人。
} // 右花括号：结束建房。
bool Session::join(const std::string& ipv4, unsigned short port) { // 成员函数：发起不阻塞界面的直连请求。
    close(); // 调用：清除之前的连接或错误。
    auto& state = *impl_; // 引用别名：访问本次会话实现。
    state.hostRole = false; // 赋值：本地为客人玩家一。
    sockaddr_in address{}; // 聚合初始化：准备服务器地址。
    address.sin_family = AF_INET; // 赋值：仅支持明确的 IPv4 地址。
    address.sin_port = htons(port); // 字节序转换：设置远端监听端口。
    if (!state.winsock || port == 0 || ipv4.size() > 15 || inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1) { state.fail(L"请输入有效的 IPv4 地址和 1 到 65535 的端口。"); return false; } // 参数检查：避免主机名解析阻塞界面。
    state.peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // 系统调用：创建客人的 TCP 套接字。
    if (state.peer == INVALID_SOCKET || !makeNonblocking(state.peer)) { state.fail(L"无法创建网络连接。"); return false; } // 错误处理：不能以阻塞模式继续。
    state.stage = Phase::Connecting; // 枚举赋值：界面显示正在连接。
    state.connecting = true; // 布尔赋值：下一次 poll 检查异步连接结果。
    state.deadline = Clock::now() + std::chrono::seconds(ConnectSeconds); // 时间赋值：限制失败连接的等待时长。
    state.message = L"正在连接房主……"; // 字符串赋值：用户可返回取消本次连接。
    const int connected = connect(state.peer, reinterpret_cast<const sockaddr*>(&address), sizeof(address)); // 非阻塞调用：连接进行中会立即返回可重试状态。
    if (connected == 0) state.connectedTransport(); // 分支：本机等场景可能立即连通。
    else if (WSAGetLastError() != WSAEWOULDBLOCK) { state.fail(L"无法连接该地址，请检查房主是否已创建房间。"); return false; } // 错误分支：明确失败时立即提示。
    return true; // 返回真：连接已开始，最终结果由 poll 更新。
} // 右花括号：结束加入房间。
bool Session::poll() { // 成员函数：界面建议每五十毫秒调用一次。
    impl_->tick(); // 函数调用：一次有限的非阻塞收发与协议推进。
    const bool changed = impl_->dirty; // 局部常量：记录本轮是否有可见状态变化。
    impl_->dirty = false; // 布尔赋值：避免仅因心跳产生无意义的重绘。
    return changed; // 返回值：调用者可据此刷新界面。
} // 右花括号：结束轮询入口。
bool Session::chooseStart(int cell) { // 成员函数：当前玩家提交唯一秘密起点。
    auto& state = *impl_; // 引用别名：简化实现访问。
    if (!state.handshaken || state.stage != Phase::Setup || state.ownReady || !studio::validCell(cell)) return false; // 提前返回：重复点击、错误阶段和非法格子不改变状态。
    state.starts[state.localPlayer()] = cell; // 数组赋值：客人只保存自己，房主私有裁判分别保存双方。
    state.ownReady = true; // 布尔赋值：立即锁定起点，防止下一帧重复提交。
    state.stage = Phase::Waiting; // 枚举赋值：自己就绪后等待对方。
    state.message = L"起点已隐藏，等待对方准备。"; // 状态文字：不会显示秘密坐标给对手。
    state.dirty = true; // 布尔赋值：请求界面刷新。
    if (state.hostRole) state.beginIfReady(); // 房主分支：双方齐备时直接创建裁判。
    else state.queue("START " + std::to_string(state.round) + " " + std::to_string(cell)); // 客人分支：向房主提交本局秘密起点。
    return state.stage != Phase::Error; // 返回真：合法起点已保存或发送。
} // 右花括号：结束起点提交。
bool Session::legal(studio::Action action) const { // 常量成员函数：依据局部受限视图检查当前请求。
    const auto& state = *impl_; // 常量引用：本检查不能修改状态。
    if (!state.handshaken || state.stage != Phase::Playing || !state.readyView || state.pending || state.result != -1 || state.currentTurn != state.localPlayer()) return false; // 阶段检查：双方不能抢回合或重复提交。
    if (action.kind == studio::Kind::Move) return studio::validCell(action.target) && studio::distance(state.ownView.own, action.target) == 1; // 移动检查：只能上下左右走一步。
    if (action.kind == studio::Kind::Attack) return studio::validCell(action.target) && studio::distance(state.ownView.own, action.target) <= 1; // 攻击检查：自己格子也可以攻击。
    if (action.kind == studio::Kind::ScanRow || action.kind == studio::Kind::ScanCol) return state.scan && state.ownView.scanAvailable && action.target >= 0 && action.target < 3; // 侦察检查：启用后可反复使用，每次仍占一个回合。
    return false; // 返回假：拒绝未定义枚举。
} // 右花括号：结束本地合法性检查。
bool Session::act(studio::Action action) { // 成员函数：发起一个经过本地合法性检查的行动。
    if (!legal(action)) return false; // 提前返回：非法操作不发送、不消耗行动。
    auto& state = *impl_; // 引用别名：访问会话实现。
    if (state.hostRole) state.apply(action); // 房主分支：由本地权威裁判直接结算。
    else { // 客人分支：请求到达房主后才会获得最终状态。
        state.pending = true; // 布尔赋值：发送之前就锁定输入，阻止连击。
        state.stage = Phase::Waiting; // 枚举赋值：等待权威裁定期间不能继续行动。
        state.message = L"行动已提交，等待房主确认。"; // 状态赋值：说明短暂等待的原因。
        state.dirty = true; // 布尔赋值：立即禁用界面行动按钮。
        state.queue("ACT " + std::to_string(state.round) + " " + std::to_string(state.count) + " " + std::to_string(static_cast<int>(action.kind)) + " " + std::to_string(action.target)); // 请求编码：局号与行动序号防止迟到或重复消息生效。
    } // 右花括号：结束身份分支。
    return state.stage != Phase::Error; // 返回真：请求已有效执行或排队。
} // 右花括号：结束行动提交。
bool Session::newRound() { // 成员函数：只允许房主在完整结束后开始新局。
    auto& state = *impl_; // 引用别名：访问会话状态。
    if (!state.hostRole || !state.handshaken || state.stage != Phase::Finished || state.round >= 1000000000) return false; // 权限与溢出检查：客人不能擅自清空当前比赛。
    ++state.round; // 前置递增：旧请求与新局具有不同编号。
    state.first = 1 - state.first; // 算术赋值：每局交换先手，减少固定先手偏差。
    state.clearRound(); // 函数调用：双方重新秘密选点。
    return state.queue("ROUND " + std::to_string(state.round) + " " + std::to_string(state.first)); // 排队：通知客人同步进入下一局。
} // 右花括号：结束下一局创建。
bool Session::isHost() const { return impl_->hostRole; } // 常量访问器：返回本地房主身份。
bool Session::connected() const { return impl_->handshaken && impl_->peer != INVALID_SOCKET; } // 常量访问器：版本确认成功且连接存在才算连通。
int Session::player() const { return impl_->localPlayer(); } // 常量访问器：读取本地玩家编号。
Phase Session::phase() const { return impl_->stage; } // 常量访问器：读取操作阶段。
bool Session::scanEnabled() const { return impl_->scan; } // 常量访问器：读取是否允许无限次侦察。
bool Session::hasStart() const { return impl_->ownReady; } // 常量访问器：读取自己的秘密起点提交状态。
bool Session::hasView() const { return impl_->readyView; } // 常量访问器：判断是否已有合法个人视图。
studio::View Session::view() const { return impl_->ownView; } // 按值访问器：返回副本，调用方不能修改会话内容。
int Session::turn() const { return impl_->currentTurn; } // 常量访问器：读取权威行动者编号。
int Session::winner() const { return impl_->result; } // 常量访问器：读取权威胜负结果。
int Session::actionCount() const { return impl_->count; } // 常量访问器：读取本局有效行动序号。
const std::vector<studio::Event>& Session::publicEvents() const { return impl_->events; } // 常量引用访问器：只返回脱敏历史。
const std::wstring& Session::privateNote() const { return impl_->privateMessage; } // 常量引用访问器：只返回自己的最新侦察提示。
const std::wstring& Session::status() const { return impl_->message; } // 常量引用访问器：读取适合展示的网络状态。
std::shared_ptr<studio::Game> Session::replay() const { return impl_->finishedReplay; } // 共享指针访问器：进行中始终为空，结束后才能看完整局面。
std::shared_ptr<studio::Game> Session::takeCompletedReplay() { return std::exchange(impl_->pendingCompleted, {}); } // 交换表达式：完成记录交给界面一次，并清空通知槽避免重复计数。
std::vector<std::string> localAddresses() { // 自由函数：仅在建房页面调用一次以便分享地址。
    WSADATA data{}; // 聚合初始化：准备独立的 Winsock 使用引用。
    std::vector<std::string> result; // 动态数组：保存去重后的本机 IPv4 地址。
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return result; // 初始化检查：失败时返回空地址列表。
    char hostname[256]{}; // 字符数组：接收本机网络主机名。
    addrinfo hints{}, *addresses = nullptr; // 结构体与指针：只请求 IPv4 TCP 地址。
    hints.ai_family = AF_INET; // 赋值：排除尚未支持的 IPv6 地址。
    hints.ai_socktype = SOCK_STREAM; // 赋值：查询 TCP 使用的网络地址。
    hints.ai_flags = AI_PASSIVE; // 赋值：请求可用于本机监听的地址。
    if (gethostname(hostname, static_cast<int>(sizeof(hostname))) == 0 && getaddrinfo(hostname, nullptr, &hints, &addresses) == 0) { // 系统查询：根据本机名字枚举已配置网卡。
        for (auto* address = addresses; address != nullptr; address = address->ai_next) { // 链表循环：逐个读取 IPv4 地址。
            char text[INET_ADDRSTRLEN]{}; // 固定数组：保存标准点分十进制地址。
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ai_addr); // 指针转换：查询已限制为 IPv4，因此可以解释其结构。
            if (inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text)) && std::string(text) != "0.0.0.0" && std::string(text).rfind("127.", 0) != 0 && std::find(result.begin(), result.end(), text) == result.end()) result.emplace_back(text); // 条件追加：排除重复、未绑定和回环地址。
        } // 右花括号：结束网卡地址枚举。
        freeaddrinfo(addresses); // 释放调用：系统分配的地址链表必须归还。
    } // 右花括号：结束地址查询成功分支。
    WSACleanup(); // 清理调用：释放本函数申请的网络引用。
    return result; // 返回：可能包含有线、无线和虚拟局域网地址，供用户选择。
} // 右花括号：结束本机地址查询。
} // 右花括号：结束 forest 命名空间。
