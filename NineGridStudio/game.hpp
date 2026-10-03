#pragma once // 预处理指令：同一编译单元只包含本头文件一次。
#include <array> // 预处理包含：引入固定长度数组 std::array。
#include <random> // 预处理包含：引入可复现的随机数引擎。
#include <string> // 预处理包含：引入宽字符字符串 std::wstring。
#include <vector> // 预处理包含：引入动态数组 std::vector。
namespace studio { // namespace 命名空间：避免本游戏名称与其他代码冲突。
enum class Kind { Move, Attack, ScanRow, ScanCol }; // 强类型枚举：定义移动、攻击、行侦察与列侦察。
enum class Style { Aggressive, Reactive, Adaptive }; // 强类型枚举：定义主动、反击和适应三种电脑风格。
struct Action { // struct 结构体：保存玩家提交的一次行动。
    Kind kind; // 枚举成员：行动类型。
    int target; // 整数成员：格子编号为 1 到 9，侦察行列编号为 0 到 2。
}; // 分号：结束 Action 类型定义。
struct View { // struct 结构体：电脑只能读取自身位置与公开信息推导的知识。
    int own; // 整数成员：当前玩家自己的格子。
    std::array<bool, 9> possible; // 模板数组：九个布尔值表示对手可能位置，不表示概率。
    bool scanAvailable; // 布尔成员：本局规则是否允许自己不限次数地侦察，每次仍占一个回合。
    bool enemyLastAttacked; // 布尔成员：对手上一次行动是否为攻击。
    int consecutiveMisses; // 整数成员：本轮连续攻击未命中的次数，其他行动清零。
    int actions; // 整数成员：自己已经执行的有效行动数。
}; // 分号：结束 View 类型定义。
struct Event { // struct 结构体：分开保存公共日志与仅行动者可见的信息。
    int actor; // 整数成员：行动者编号，0 或 1。
    Action action; // 结构体成员：完整行动，移动终点仅供裁判与结束后回放使用。
    bool hit; // 布尔成员：此次攻击是否命中。
    bool scanPositive; // 布尔成员：侦察是否发现对手，仅供行动者与结束后回放使用。
    std::wstring publicText; // 宽字符串成员：比赛中可以向双方显示的文字。
    std::wstring privateText; // 宽字符串成员：侦察结果仅向行动者显示，其他行动为空。
}; // 分号：结束 Event 类型定义。
struct Snapshot { // struct 结构体：初始局面及每步后的完整回放快照。
    std::array<int, 2> positions; // 固定数组：裁判保存的双方真实位置，对战中不可直接展示。
    std::array<std::array<bool, 9>, 2> possible; // 嵌套数组：双方各自掌握的候选位置集合。
    std::array<bool, 2> scanAvailable; // 固定数组：双方是否允许侦察，使用后仍保持开启。
    int turn; // 整数成员：下一位行动者；终局保留最后行动者。
    int winner; // 整数成员：-1 表示进行中，0 或 1 表示赢家，2 表示达到上限的和局。
}; // 分号：结束 Snapshot 类型定义。
class Game { // class 类：裁判统一管理规则和隐藏状态。
public: // 访问控制：下列成员可由界面和测试调用。
    Game(int p0, int p1, bool enableScan = true, int first = 0); // 构造函数声明：创建一局，同格合法，非法初始值抛出异常。
    View view(int player) const; // const 成员函数：返回指定玩家允许读取的信息。
    bool legal(Action action) const; // const 成员函数：检查当前玩家的行动是否合法。
    bool act(Action action); // 成员函数：执行合法行动；失败时保持所有状态不变。
    int turn() const; // const 成员函数：读取当前轮次。
    int winner() const; // const 成员函数：读取胜负状态。
    bool finished() const; // const 成员函数：判断是否结束。
    const std::vector<Event>& events() const; // 常量引用返回：读取日志；完整行动必须在界面层保密。
    const std::vector<Snapshot>& snapshots() const; // 常量引用返回：读取裁判快照，仅供结束后复盘。
private: // 访问控制：外部无法直接修改隐藏状态。
    Snapshot state_; // 类型成员：当前局面。
    std::array<bool, 2> lastAttack_{}; // 花括号初始化：双方最近一次行动默认都不是攻击。
    std::array<int, 2> misses_{}; // 花括号初始化：双方连续未命中次数从零开始。
    std::array<int, 2> actions_{}; // 花括号初始化：双方有效行动数从零开始。
    std::vector<Event> events_; // 动态数组成员：保存按时间排列的行动。
    std::vector<Snapshot> snapshots_; // 动态数组成员：保存初始与每步后的局面。
}; // 分号：结束 Game 类定义。
int distance(int a, int b); // 自由函数声明：返回两个合法格子之间的曼哈顿距离。
bool validCell(int cell); // 自由函数声明：判断格子编号是否在 1 到 9 之间。
bool inScan(int cell, Kind kind, int target); // 自由函数声明：判断格子是否在指定的侦察行列内。
Action chooseAction(const View& view, Style style, std::mt19937& random); // 自由函数声明：策略只接收 View，不能读取对手真实位置。
} // 右花括号：结束 studio 命名空间。
