#include "game.hpp" // 预处理包含：导入游戏类型及接口声明。
#include <algorithm> // 预处理包含：引入计数、最大值和最小值等算法。
#include <cstdlib> // 预处理包含：引入整数绝对值 std::abs。
#include <stdexcept> // 预处理包含：引入参数错误异常。
namespace studio { // 命名空间：以下实现属于 studio。
bool validCell(int cell) { // 函数定义：检查格子编号是否合法。
    return cell >= 1 && cell <= 9; // return 返回：两个比较都为真时才是有效格子。
} // 右花括号：结束编号检查。
int distance(int a, int b) { // 函数定义：计算横向距离与纵向距离之和。
    if (!validCell(a) || !validCell(b)) throw std::invalid_argument("Invalid cell"); // if 条件判断：任何编号非法时抛出异常。
    return std::abs((a - 1) / 3 - (b - 1) / 3) + std::abs((a - 1) % 3 - (b - 1) % 3); // / 为整除、% 为取余：编号转行列后求曼哈顿距离。
} // 右花括号：结束距离函数。
bool inScan(int cell, Kind kind, int target) { // 函数定义：侦察行列都采用从零开始的编号。
    if (!validCell(cell) || target < 0 || target > 2) return false; // 短路或运算：非法参数不会落入任何侦察区域。
    if (kind == Kind::ScanRow) return (cell - 1) / 3 == target; // 相等比较：行号等于目标行时返回真。
    if (kind == Kind::ScanCol) return (cell - 1) % 3 == target; // 相等比较：列号等于目标列时返回真。
    return false; // 返回假：移动和攻击不是侦察类型。
} // 右花括号：结束区域检查。
Game::Game(int p0, int p1, bool enableScan, int first) { // 作用域解析符 ::：定义 Game 的构造函数。
    if (!validCell(p0) || !validCell(p1) || (first != 0 && first != 1)) throw std::invalid_argument("Invalid initial state"); // 构造前检查位置与先手编号。
    state_.positions = {p0, p1}; // 聚合赋值：记录双方秘密起点，同格不禁止。
    for (auto& knowledge : state_.possible) knowledge.fill(true); // 范围 for 与引用：开始时双方都不能排除任何格子。
    state_.scanAvailable = {enableScan, enableScan}; // 聚合赋值：依据模式为双方开启或关闭不限次数的侦察。
    state_.turn = first; // 成员赋值：指定先手。
    state_.winner = -1; // 成员赋值：负一表示尚未结束。
    snapshots_.push_back(state_); // 成员函数调用：保存行动前的初始快照。
} // 右花括号：结束构造函数。
View Game::view(int player) const { // const 限定：读取玩家视图，不改变比赛。
    if (player != 0 && player != 1) throw std::invalid_argument("Invalid player"); // 参数检查：阻止数组越界。
    return {state_.positions[player], state_.possible[player], state_.scanAvailable[player], lastAttack_[1 - player], misses_[player], actions_[player]}; // 聚合返回：只输出本人的位置和可推理信息。
} // 右花括号：结束视图函数。
bool Game::legal(Action action) const { // const 函数定义：规则检查没有副作用。
    if (finished()) return false; // 提前返回：终局后不能继续行动。
    const int own = state_.positions[state_.turn]; // const 局部变量：取得当前玩家自身位置。
    if (action.kind == Kind::Move) return validCell(action.target) && distance(own, action.target) == 1; // 与运算短路：移动必须恰好一步且不能原地等待。
    if (action.kind == Kind::Attack) return validCell(action.target) && distance(own, action.target) <= 1; // 攻击允许自身格和上下左右一格。
    if (action.kind == Kind::ScanRow || action.kind == Kind::ScanCol) return state_.scanAvailable[state_.turn] && action.target >= 0 && action.target < 3; // 侦察须由本局规则开启且行列合法，次数不限。
    return false; // 返回假：拒绝未知枚举值。
} // 右花括号：结束行动检查。
bool Game::act(Action action) { // 成员函数定义：裁判执行当前玩家的一步。
    if (!legal(action)) return false; // 提前返回：非法操作不消耗回合、不写入日志。
    const int actor = state_.turn; // 常量：本次行动者。
    const int enemy = 1 - actor; // 算术表达式：玩家 0 与 1 互为对手。
    Event event{actor, action, false, false, L"", L""}; // 聚合初始化：宽字符串以 L 前缀支持中文界面。
    const std::wstring name = L"玩家 " + std::to_wstring(actor + 1); // 字符串拼接：显示时将内部编号转换为 1 或 2。
    lastAttack_[actor] = action.kind == Kind::Attack; // 比较结果赋值：侦察和移动都不是攻击。
    ++actions_[actor]; // 前置递增：记录一个有效行动。
    if (action.kind == Kind::Move) { // 分支：执行保密的相邻移动。
        state_.positions[actor] = action.target; // 更新裁判真实位置：终点不写进公共文字。
        std::array<bool, 9> expanded{}; // 零初始化：先创建全假的新候选集合。
        for (int from = 1; from <= 9; ++from) { // for 循环：枚举对方原先认为可能的位置。
            if (!state_.possible[enemy][from - 1]) continue; // continue 跳过：已排除的来源不会产生新候选。
            for (int to = 1; to <= 9; ++to) if (distance(from, to) == 1) expanded[to - 1] = true; // 只扩散至实际可走到的上下左右，不保留原地等待。
        } // 右花括号：结束来源枚举。
        state_.possible[enemy] = expanded; // 集合赋值：观察者更新移动后的对手可能位置。
        misses_[actor] = 0; // 重置计数：换位结束连续未命中的攻击序列。
        event.publicText = name + L" 移动了一格。"; // 公共日志：不公开方向和终点。
    } else if (action.kind == Kind::Attack) { // else if 分支：执行攻击，攻击者保持原位。
        event.hit = action.target == state_.positions[enemy]; // 相等判断：裁判用真实位置结算命中。
        for (int cell = 1; cell <= 9; ++cell) state_.possible[enemy][cell - 1] = state_.possible[enemy][cell - 1] && distance(cell, action.target) <= 1; // 观察者根据射程限制攻击者来源位置。
        if (event.hit) { // 分支：命中立即结束，不再等待对手行动。
            state_.winner = actor; // 赋值：本次行动者获胜。
            state_.possible[actor].fill(false); // 终局知识更新：命中证明对手就在攻击目标。
            state_.possible[actor][action.target - 1] = true; // 索引减一：保留唯一被命中的格子。
            misses_[actor] = 0; // 清零：命中不是未命中序列。
        } else { // else 分支：未命中只排除对手当前的一个格子。
            state_.possible[actor][action.target - 1] = false; // 布尔赋值：攻击目标在这个时刻没有对手。
            ++misses_[actor]; // 前置递增：累计连续失败的攻击。
        } // 右花括号：结束命中判定。
        event.publicText = name + L" 攻击 " + std::to_wstring(action.target) + (event.hit ? L"：命中，获胜！" : L"：未命中。"); // 三目运算符：选择相应公开结果。
    } else { // 其余合法类型均为行侦察或列侦察。
        event.scanPositive = inScan(state_.positions[enemy], action.kind, action.target); // 裁判计算侦察时刻的二值结果。
        for (int cell = 1; cell <= 9; ++cell) state_.possible[actor][cell - 1] = state_.possible[actor][cell - 1] && (inScan(cell, action.kind, action.target) == event.scanPositive); // 交集更新：按有无对手保留区域或补集。
        misses_[actor] = 0; // 清零：侦察也中断连续攻击。
        event.publicText = name + L" 侦察第 " + std::to_wstring(action.target + 1) + (action.kind == Kind::ScanRow ? L" 行。" : L" 列。"); // 行列公开，但结果不进入公共文字。
        event.privateText = event.scanPositive ? L"侦察结果：该区域有对手。" : L"侦察结果：该区域没有对手。"; // 私人文字：结果只向行动者展示。
    } // 右花括号：结束三类行动。
    events_.push_back(event); // 动态数组追加：保留完整事件用于回放。
    if (state_.winner == -1 && events_.size() >= 80) state_.winner = 2; // 上限检查：共 80 步仍未命中则公开判和，命中优先。
    if (state_.winner == -1) state_.turn = enemy; // 条件切换：只有继续中的比赛才交给对手。
    snapshots_.push_back(state_); // 追加快照：终局行动同样完整记录。
    return true; // 返回真：此次行动成功执行。
} // 右花括号：结束行动执行。
int Game::turn() const { return state_.turn; } // 简短 const 访问器：读取当前行动者。
int Game::winner() const { return state_.winner; } // 简短 const 访问器：读取胜负状态。
bool Game::finished() const { return state_.winner != -1; } // 不等比较：赢家非负即代表已经结束。
const std::vector<Event>& Game::events() const { return events_; } // 常量引用：避免复制整个事件列表。
const std::vector<Snapshot>& Game::snapshots() const { return snapshots_; } // 常量引用：提供只读的完整回放数据。
namespace { // 匿名命名空间：辅助函数只在本实现文件中可见。
int randomIndex(int size, std::mt19937& random) { // 引用参数：沿用调用者的随机引擎以便复现。
    return std::uniform_int_distribution<int>(0, size - 1)(random); // 临时分布对象与调用运算符：均匀选择合法下标。
} // 右花括号：结束随机下标函数。
Action moveChoice(const View& view, bool evade, std::mt19937& random) { // 辅助函数：只用合法视图追近或避开候选区域。
    std::vector<int> choices; // 动态数组：存放评分最高的移动终点。
    int best = -100000; // 初始评分：足够小，确保至少一个邻格被选入。
    for (int cell = 1; cell <= 9; ++cell) { // 枚举九个格子，随后筛出相邻位置。
        if (distance(view.own, cell) != 1) continue; // continue：排除非法移动。
        int nearest = 9, reach = 0, sum = 0; // 多变量声明：最近距离、可攻击候选数和总距离。
        for (int enemy = 1; enemy <= 9; ++enemy) { // 枚举所有候选敌方格子。
            if (!view.possible[enemy - 1]) continue; // 跳过：当前知识已排除的格子。
            const int gap = distance(cell, enemy); // 常量：移动后与该候选位置的距离。
            nearest = std::min(nearest, gap); // 求最小值：更新最近候选距离。
            reach += gap <= 1 ? 1 : 0; // 条件表达式：统计移动后射程内候选格数量。
            sum += gap; // 累加赋值：衡量与全部候选位置的距离。
        } // 右花括号：结束候选位置枚举。
        const int score = evade ? nearest * 100 + sum : reach * 30 - nearest * 10 - sum; // 三目选择：避让优先拉远，追近优先扩大有效射程。
        if (score > best) { best = score; choices.clear(); } // 新高分出现时丢弃旧的候选终点。
        if (score == best) choices.push_back(cell); // 相同评分保留：稍后随机打破平局。
    } // 右花括号：结束移动终点评分。
    return {Kind::Move, choices[randomIndex(static_cast<int>(choices.size()), random)]}; // 显式类型转换与聚合返回：输出一个合法移动。
} // 右花括号：结束移动选择。
} // 右花括号：结束匿名命名空间。
Action chooseAction(const View& view, Style style, std::mt19937& random) { // 策略入口：参数中没有裁判、对手真实坐标或完整日志。
    if (!validCell(view.own)) throw std::invalid_argument("Invalid view"); // 防御检查：阻止用非法己方坐标选择行动。
    std::vector<int> shots; // 动态数组：存放可攻击且未被排除的格子。
    const int count = static_cast<int>(std::count(view.possible.begin(), view.possible.end(), true)); // 标准算法：统计候选格数，不把它当作真实概率。
    for (int cell = 1; cell <= 9; ++cell) if (view.possible[cell - 1] && distance(view.own, cell) <= 1) shots.push_back(cell); // 双重条件：候选且射程可达才值得尝试攻击。
    const auto attack = [&]() -> Action { return {Kind::Attack, shots[randomIndex(static_cast<int>(shots.size()), random)]}; }; // 捕获引用的 lambda：调用前保证 shots 非空。
    if (count == 1 && !shots.empty()) return attack(); // 确定命中优先：即使反击风格也不放弃唯一已知目标。
    if (style == Style::Aggressive) return shots.empty() ? moveChoice(view, false, random) : attack(); // 主动策略：有目标即攻击，否则追近。
    if (style == Style::Reactive) { // 分支：反击策略围绕对手刚公开的攻击行动。
        if (view.enemyLastAttacked && !shots.empty()) return attack(); // 对手刚开火且候选可达时立即反击。
        return moveChoice(view, true, random); // 其他时候避开候选区域；这个基准策略不使用侦察。
    } // 右花括号：结束反击策略。
    if (view.consecutiveMisses >= 2) return moveChoice(view, false, random); // 适应策略：连续两枪落空后换位，避免长期站桩。
    if (view.enemyLastAttacked && !shots.empty()) return attack(); // 信息新鲜时优先把握反击窗口。
    if (view.scanAvailable && count >= 4) { // 条件分支：侦察开启且候选较多时可再次侦察，每次仍消耗回合。
        Action bestScan{Kind::ScanRow, 0}; // 聚合初始化：保存候选侦察动作。
        int balance = 0; // 初值：衡量侦察结果能否把候选集合分为两组。
        for (Kind kind : {Kind::ScanRow, Kind::ScanCol}) { // 范围循环：分别检查行与列。
            for (int region = 0; region < 3; ++region) { // 循环：检查每条行或列。
                int inside = 0; // 计数器：当前区域内仍可能藏人的格子数。
                for (int cell = 1; cell <= 9; ++cell) if (view.possible[cell - 1] && inScan(cell, kind, region)) ++inside; // 统计候选集合与区域的交集大小。
                const int value = std::min(inside, count - inside); // 最小值：两种侦察结果都能缩小集合时评分较高。
                if (value > balance) { balance = value; bestScan = {kind, region}; } // 更均衡的分割替换当前方案。
            } // 右花括号：结束区域枚举。
        } // 右花括号：结束侦察类型枚举。
        if (balance >= 2 && (shots.empty() || randomIndex(100, random) < 35)) return bestScan; // 条件随机选择：侦察消耗一回合，因此不总是优先使用。
    } // 右花括号：结束侦察选择。
    if (!shots.empty() && randomIndex(100, random) < 75) return attack(); // 保留进攻倾向，同时给移动留出机会。
    return moveChoice(view, false, random); // 默认动作：朝可能藏人的区域移动。
} // 右花括号：结束电脑策略。
} // 右花括号：结束 studio 命名空间。
