// 本文件使用 C++17；注释语言为中文，// 表示到行末都不执行；编译示例：g++ -std=c++17 -O2 hidden_grid.cpp -o hidden_grid
#include <algorithm> // 预处理指令 #include 引入标准算法头文件；当前游戏文件没有直接调用其中的算法。
#include <array> // 引入 std::array：长度在编译时固定的数组，本程序用它保存两名玩家或九个格子的状态。
#include <cstdlib> // 引入通用工具头文件；这里使用其中的整数绝对值函数 std::abs。
#include <exception> // 引入标准异常基类 std::exception，供退出信号和异常捕获使用。
#include <iostream> // 引入标准输入输出流：std::cin 读键盘，std::cout 输出正文，std::cerr 输出错误。
#include <random> // 引入随机数引擎、种子序列和均匀分布，用于随机选择位置及行动。
#include <sstream> // 引入字符串流 std::istringstream，用于把一整行输入解析成命令或整数。
#include <stdexcept> // 引入 std::invalid_argument 和 std::runtime_error 等标准异常类型。
#include <string> // 引入 std::string 字符串，以及 std::stoi 等字符串处理函数。
#include <vector> // 引入 std::vector 动态数组，用于保存数量不固定的攻击或移动候选格。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
#ifdef _WIN32 // 条件编译：仅当编译器定义了 _WIN32 宏时，编译后面的 Windows 专用代码。
#ifndef NOMINMAX // 条件编译：如果尚未定义 NOMINMAX，才执行下面的定义，避免宏重复定义警告。
#define NOMINMAX // 定义宏，阻止 Windows 头文件生成 min/max 宏，以免与 C++ 标准库名称冲突。
#endif // 结束最近一组 #if、#ifdef 或 #ifndef 条件编译区域。
#include <windows.h> // 引入 Windows 系统接口，用于隐藏键盘输入、检测控制台和设置 UTF-8 编码。
#else // 条件编译的另一分支：上面的宏条件不成立时编译这里的代码，不是运行时的 else。
#include <termios.h> // 引入 Linux/macOS 等 POSIX 系统的终端设置接口，用于关闭输入回显。
#include <unistd.h> // 引入 POSIX 系统接口，提供 isatty 和 STDIN_FILENO 等终端检测功能。
#endif // 结束最近一组 #if、#ifdef 或 #ifndef 条件编译区域。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
namespace hidden_grid { // 定义命名空间，把游戏相关名称放入 hidden_grid，减少与其他代码重名的机会。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
// 游戏内部同样用 1 到 9 编号；下面的距离计算只考虑上下左右，不允许斜向一步。
bool validCell(int cell) { return cell >= 1 && cell <= 9; } // 定义返回 bool 的函数；&& 表示“并且”，仅当格号处于 1 到 9 时返回 true。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
int distance(int a, int b) { // 定义返回 int 的距离函数；参数 a、b 是从 1 开始编号的两个格子。
    return std::abs((a - 1) / 3 - (b - 1) / 3) // return 表达式的前半部分：格号减 1 后整除 3 得到行号，abs 求两行之差的绝对值。
         + std::abs((a - 1) % 3 - (b - 1) % 3); // 加上列距离；% 是取余，得到列号；行差加列差就是只允许上下左右时的曼哈顿距离。
} // 结束 distance 函数体；行距离和列距离已合并为返回值。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
enum class Kind { Move, Attack }; // 定义强类型枚举 Kind：Move 表示移动，Attack 表示攻击；使用时写 Kind::Move 等。
struct Action { Kind kind; int target; }; // 定义行动结构体：kind 保存行动种类，target 保存目标格号；struct 成员默认公开。
enum class Result { Invalid, Moved, Miss, Win }; // 定义行动结果枚举：不合法、移动成功、攻击未命中、攻击获胜。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
class Game { // 定义 Game 类，把真实位置、轮次和规则判定封装在一个对象里；class 成员默认私有。
public: // 访问控制标签：之后的成员可从类外访问，直到遇到另一个访问标签或类定义结束。
    Game(int first, int second) : positions_{first, second} { // 构造函数与类同名、没有返回类型；冒号后是成员初始化列表，把双方起始格存入数组。
        if (!validCell(first) || !validCell(second)) // if 判断起点是否合法；! 表示取反，|| 表示“或者”，任意一方越界就执行下一条语句。
            throw std::invalid_argument("Invalid starting cell"); // throw 抛出参数异常，表示起始格无效；程序转到能处理该异常的 catch。
    } // 结束 Game 构造函数；起始位置已保存并通过范围检查。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    int turn() const { return turn_; } // 只读成员函数返回当前玩家索引 0 或 1；函数后的 const 表示不修改本对象状态。
    int position(int player) const { return positions_.at(player); } // 返回指定玩家的位置；at 会检查数组下标，越界会抛异常；仅裁判及自身位置查询使用。
    bool finished() const { return finished_; } // 只读查询：返回游戏是否已经结束的布尔标记。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    Result act(Action action) { // 定义执行行动的成员函数，按值接收 Action，返回 Result 结果。
        if (finished_ || !validCell(action.target)) return Result::Invalid; // 游戏已结束或目标格越界时提前 return；点号访问结构体成员，不改变位置和回合。
        const int d = distance(positions_[turn_], action.target); // const int 定义本函数内不再修改的整数 d；[] 按当前玩家下标取得位置，再计算到目标的距离。
        if ((action.kind == Kind::Move && d != 1) || // 合法性判断前半部分：== 比较相等、!= 比较不等；移动距离不是 1 就不合法。
            (action.kind == Kind::Attack && d > 1)) return Result::Invalid; // 合法性判断后半部分：攻击距离大于 1 就不合法；距离 0 表示攻击自己所在格。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
        Result result; // 声明结果变量；尚未赋值，但下面每个合法分支都会在读取前为它赋值。
        if (action.kind == Kind::Move) { // 当行动枚举等于 Move 时进入移动分支。
            // 对方占据某格不会阻挡移动；双方允许同格，只有攻击命中才会结束游戏。
            positions_[turn_] = action.target; // = 是赋值：把当前玩家的位置更新为目标格；不检查格子占用，因此允许双方同格。
            result = Result::Moved; // 将本次行动结果设为“移动成功”。
        } else if (positions_[1 - turn_] == action.target) { // 结束移动分支；否则检查攻击是否命中另一名玩家，1 减当前索引得到对手索引。
            finished_ = true; // 命中后把游戏结束标记设置为真。
            return Result::Win; // 立即返回胜利结果，提前结束函数，不再切换行动玩家。
        } else { // 结束前一个 if 分支；当前述条件不成立时，执行这个 else 分支。
            result = Result::Miss; // 将结果设为“未命中”；攻击未命中不会改变角色位置。
        } // 结束未命中分支及整组行动类型判断，继续执行回合切换。
        turn_ = 1 - turn_; // 在 0 和 1 之间切换行动玩家，表示本次合法且未获胜的回合已结束。
        return result; // 返回前面得到的结果，并结束本次函数调用。
    } // 结束 act 成员函数，完成一次行动的判定和状态更新。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
private: // 访问控制标签：之后的成员只能由本类成员及友元访问，不能从普通外部代码直接访问。
    std::array<int, 2> positions_; // 长度固定为 2 的整数数组，分别保存玩家 0 与玩家 1 的真实格号；下划线是命名习惯。
    int turn_ = 0; // 声明当前玩家索引并默认初始化为 0，即第一名玩家先手。
    bool finished_ = false; // 声明结束标记并初始化为 false，表示对局尚未结束。
}; // 结束 Game 类定义；类定义末尾必须带分号。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
// 电脑策略只能读取自身位置与公开行动；决策时不读取对方真实位置。
// 电脑维护对手可能位置集合；只按规则排除不可能格，不根据对手风格加权。
class Bot { // 定义电脑推理类；内部只保存对手的可能位置集合，不保存对手真实位置。
public: // 访问控制标签：之后的成员可从类外访问，直到遇到另一个访问标签或类定义结束。
    Bot() { possible_.fill(true); } // 默认构造函数：fill(true) 将九个元素全部设为真，表示开局时九格皆有可能。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    void observeEnemyMove() { // 定义无返回值的观察函数：对手移动一格后更新可能位置集合。
        std::array<bool, 9> next{}; // {} 值初始化九个布尔元素为 false；next 用于构造更新后的可能位置集合。
        for (int from = 1; from <= 9; ++from) // for 循环依次枚举原来的格号 1 到 9；++from 每轮把格号加 1。
            if (possible_[from - 1]) // 只有原来可能存在对手的格子才继续扩展；格号减 1 转换为从 0 开始的数组下标。
                for (int to = 1; to <= 9; ++to) // 内层循环枚举对手下一步可能到达的所有格子；没有花括号时循环体只有下一条语句。
                    if (distance(from, to) == 1) next[to - 1] = true; // 只保留与原位置上下左右相邻的目的格；移动必须恰好一步，不能原地停留。
        possible_ = next; // 用新数组整体替换旧的可能位置集合；保留的是所有可达邻格的并集。
    } // 结束 observeEnemyMove 函数，对手移动后的可能位置已更新。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    void observeEnemyAttack(int target) { // 接收对手公开的攻击目标格，利用攻击范围反推对手可能站在哪里。
        for (int cell = 1; cell <= 9; ++cell) // 从格号 1 循环到 9；无花括号时，下一条完整语句作为循环体。
            if (distance(cell, target) > 1) possible_[cell - 1] = false; // 距离攻击目标超过一格的位置不可能发起该攻击，将它们从可能集合中排除。
    } // 结束 observeEnemyAttack 函数，无法发起该攻击的位置已排除。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    void observeOwnMiss(int target) { possible_[target - 1] = false; } // 自己攻击未命中说明对手当时不在目标格，将该格对应的布尔元素设为 false。
    bool considersPossible(int cell) const { return possible_.at(cell - 1); } // 只读查询某格是否仍可能有对手；at 会检查下标是否合法。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    Action choose(int ownCell, std::mt19937& random) const { // 决策函数返回 Action；参数 & 是引用，使用调用者的随机引擎；const 禁止修改本对象状态。
        std::vector<int> attacks; // 声明动态整数数组 attacks，保存既在攻击范围内、又可能有对手的格子。
        std::vector<int> moves; // 声明动态整数数组 moves，保存当前评分最高的合法移动目的格。
        int bestCoverage = -1; // 最佳覆盖数量初始为 -1，低于任何合法覆盖数量，保证首个候选能成为最佳。
        for (int cell = 1; cell <= 9; ++cell) { // 循环枚举九个格子；花括号中的多条语句一起构成循环体。
            if (distance(ownCell, cell) <= 1 && possible_[cell - 1]) // && 要求同时满足：格子在自己的攻击范围内，并且对手仍可能在此。
                attacks.push_back(cell); // push_back 将格号追加到攻击候选数组末尾。
            if (distance(ownCell, cell) != 1) continue; // 非上下左右相邻格不能作为移动目标；continue 跳过本轮剩余代码，检查下一个格子。
            int coverage = 0; // 将当前移动目标的覆盖计数清零，随后统计从那里可以攻击多少个可能敌人格。
            for (int enemy = 1; enemy <= 9; ++enemy) // 枚举九个假设的敌人格，用来评价移动目标的覆盖程度。
                if (possible_[enemy - 1] && distance(cell, enemy) <= 1) ++coverage; // 如果敌人格仍可能且在候选位置的攻击范围内，覆盖计数自增 1。
            if (coverage > bestCoverage) { // 如果当前格能覆盖更多可能位置，就进入更新最佳移动选项的分支。
                moves.clear(); // clear 清空原有移动候选；已经发现更好的评分，较差的旧选项不再保留。
                bestCoverage = coverage; // 把最佳覆盖数量更新为当前候选的覆盖数量。
            } // 结束发现更好覆盖数量时的更新分支。
            if (coverage == bestCoverage) moves.push_back(cell); // 覆盖数量与最佳值相同就加入候选列表，允许随后在并列最佳位置中随机选择。
        } // 结束九格遍历；合法攻击候选和最佳移动候选已收集完毕。
        // 有攻击目标时也保留一定概率移动，让电脑不会每次都继续在原地攻击。
        if (!attacks.empty() && std::uniform_int_distribution<int>(0, 3)(random) != 0) // 有攻击目标且均匀抽出的 0、1、2、3 不等于 0 时攻击，即有候选时以 75% 概率攻击。
            return {Kind::Attack, pick(attacks, random)}; // return 用花括号构造 Action：行动为攻击，目标由 pick 在候选数组中随机选取。
        return {Kind::Move, pick(moves, random)}; // 返回移动行动及随机选出的最佳目的格；3×3 棋盘每个格都有合法邻格。
    } // 结束 choose 决策函数；前面的 return 已返回本次攻击或移动。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
private: // 访问控制标签：之后的成员只能由本类成员及友元访问，不能从普通外部代码直接访问。
    static int pick(const std::vector<int>& cells, std::mt19937& random) { // static 成员函数不依赖某个对象；const 引用只读接收候选数组，随机引擎用可修改引用传入。
        return cells.at(std::uniform_int_distribution<std::size_t>(0, cells.size() - 1)(random)); // 均匀生成 0 到 size()-1 的下标，再用 at 取出对应格号；调用前必须保证数组非空。
    } // 结束 pick 辅助函数，返回的是格号，而不是候选数组下标。
    std::array<bool, 9> possible_{}; // 九个布尔值对应九个可能位置；{} 先初始化为 false，Bot 构造函数随后将它们全部设为 true。
}; // 结束 Bot 类定义；成员 possible_ 只保存推理集合，末尾分号不可省略。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
bool interactiveInput() { // 定义终端检测函数，返回标准输入是否连接交互式控制台。
#ifdef _WIN32 // 条件编译：仅当编译器定义了 _WIN32 宏时，编译后面的 Windows 专用代码。
    DWORD mode = 0; // Windows 的 DWORD 是 32 位无符号整数；mode 接收当前控制台输入模式的位标志。
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0; // 获取标准输入句柄并查询模式；&mode 是取地址，查询成功说明输入来自 Windows 控制台。
#else // 条件编译的另一分支：上面的宏条件不成立时编译这里的代码，不是运行时的 else。
    return isatty(STDIN_FILENO) != 0; // POSIX 的 isatty 检查标准输入文件描述符是否对应终端，非零表示是。
#endif // 结束最近一组 #if、#ifdef 或 #ifndef 条件编译区域。
} // 结束 interactiveInput 函数；编译平台决定实际采用哪个终端检测分支。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
// 通过构造与析构配对管理输入回显；正常返回、输入结束或异常展开时自动恢复终端。
class HiddenInput { // 定义输入隐藏管理类：构造时关闭回显，析构时恢复，体现资源获取即初始化（RAII）方式。
public: // 访问控制标签：之后的成员可从类外访问，直到遇到另一个访问标签或类定义结束。
    explicit HiddenInput(bool enabled) { // 构造函数接收是否隐藏输入；explicit 禁止布尔值被隐式转换成 HiddenInput 对象。
        if (!enabled || !interactiveInput()) return; // 未要求隐藏或输入不是交互终端时提前结束构造函数，不调整终端设置。
#ifdef _WIN32 // 条件编译：仅当编译器定义了 _WIN32 宏时，编译后面的 Windows 专用代码。
        handle_ = GetStdHandle(STD_INPUT_HANDLE); // 将 Windows 标准输入的系统句柄保存到成员 handle_。
        if (!GetConsoleMode(handle_, &oldMode_) || // 先读取并保存原输入模式；读取失败时条件为真，|| 会短路而不再执行后半部分。
            !SetConsoleMode(handle_, oldMode_ & ~ENABLE_ECHO_INPUT)) // ~ 按位取反、& 按位与，用于关闭回显位并保留其他模式位；设置失败则条件为真。
            throw std::runtime_error("Cannot disable input echo"); // 关闭输入回显失败时抛出运行时异常，避免继续把秘密输入暴露在屏幕上。
#else // 条件编译的另一分支：上面的宏条件不成立时编译这里的代码，不是运行时的 else。
        if (tcgetattr(STDIN_FILENO, &oldMode_) != 0) // 在 POSIX 终端中读取原属性并保存；返回非零表示读取失败。
            throw std::runtime_error("Cannot read terminal mode"); // 读取终端属性失败时抛出异常，由上层错误处理逻辑接收。
        termios mode = oldMode_; // 复制原终端属性到局部变量，修改副本并保留原值以便稍后恢复。
        mode.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL)); // 按位或 | 合并两个回显标志，~ 取反后用 &= 清除对应位；static_cast 将结果转换为终端标志类型。
        if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) != 0) // 立即应用修改后的终端属性；TCSANOW 表示立即生效，非零返回值表示失败。
            throw std::runtime_error("Cannot disable input echo"); // 关闭输入回显失败时抛出运行时异常，避免继续把秘密输入暴露在屏幕上。
#endif // 结束最近一组 #if、#ifdef 或 #ifndef 条件编译区域。
        changed_ = true; // 记录终端模式确实已被修改，析构时才需要恢复。
    } // 结束 HiddenInput 构造函数；若回显已关闭，changed_ 会记录这一状态。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    ~HiddenInput() { // 析构函数名前加 ~；对象离开作用域时自动调用，包括异常导致的栈展开。
        if (!changed_) return; // 如果构造时没有修改终端模式，析构直接返回，无需恢复。
#ifdef _WIN32 // 条件编译：仅当编译器定义了 _WIN32 宏时，编译后面的 Windows 专用代码。
        SetConsoleMode(handle_, oldMode_); // 在 Windows 中恢复构造前保存的输入模式，例如重新开启键盘回显。
#else // 条件编译的另一分支：上面的宏条件不成立时编译这里的代码，不是运行时的 else。
        tcsetattr(STDIN_FILENO, TCSANOW, &oldMode_); // 在 POSIX 终端中立即恢复先前保存的输入属性。
#endif // 结束最近一组 #if、#ifdef 或 #ifndef 条件编译区域。
    } // 结束 HiddenInput 析构函数，终端设置的恢复处理完成。
    HiddenInput(const HiddenInput&) = delete; // = delete 禁用拷贝构造，避免多个对象共同恢复同一份终端状态。
    HiddenInput& operator=(const HiddenInput&) = delete; // 禁用拷贝赋值运算符；返回类型中的 & 表示对象引用。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
private: // 访问控制标签：之后的成员只能由本类成员及友元访问，不能从普通外部代码直接访问。
    bool changed_ = false; // 标记本对象是否修改过终端设置，初始为否。
#ifdef _WIN32 // 条件编译：仅当编译器定义了 _WIN32 宏时，编译后面的 Windows 专用代码。
    HANDLE handle_ = INVALID_HANDLE_VALUE; // Windows 句柄成员初始化为无效值，后续构造成功时再保存真实输入句柄。
    DWORD oldMode_ = 0; // 保存 Windows 原始输入模式的位标志，初始清零。
#else // 条件编译的另一分支：上面的宏条件不成立时编译这里的代码，不是运行时的 else。
    termios oldMode_{}; // 保存 POSIX 终端原属性；{} 对成员执行值初始化。
#endif // 结束最近一组 #if、#ifdef 或 #ifndef 条件编译区域。
}; // 结束 HiddenInput 类定义，末尾分号不可省略。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
struct Quit : std::exception {}; // 定义继承 std::exception 的空结构体，作为正常退出的异常信号；struct 默认公开继承。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
std::string readLine(const std::string& prompt, bool secret = false) { // 函数返回字符串；提示文字用 const 引用传入以避免复制，secret 默认参数为 false。
    std::cout << prompt << std::flush; // << 向输出流写入提示文字；flush 立即刷新缓冲区，确保用户输入前能看见提示。
    HiddenInput hide(secret); // 创建局部对象控制回显；函数结束或抛出异常时，它会自动析构并恢复终端。
    std::string line; // 声明字符串，用于存放读入的一整行文本。
    if (!std::getline(std::cin, line)) throw Quit{}; // getline 读取整行；遇到输入结束或读取失败时抛出 Quit，统一走退出处理。
    if (secret) std::cout << '\n'; // 秘密输入没有回显，读取后主动输出换行；'\n' 是换行字符字面量。
    if (line == "q" || line == "Q") throw Quit{}; // 输入整行恰好为小写 q 或大写 Q 时，抛出正常退出信号。
    return line; // 将读到的文本返回给调用者，随后局部回显管理对象自动析构。
} // 结束 readLine 函数；局部 hide 对象会析构并恢复可能被关闭的回显。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
int readNumber(const std::string& prompt, int low, int high, bool secret = false) { // 定义整数读取函数：low、high 为允许范围，secret 决定是否隐藏用户输入。
    for (;;) { // for 的初始化、条件和更新表达式都省略，形成无限循环，直到 return 或异常离开。
        std::istringstream input(readLine(prompt, secret)); // 先读取一整行，再用该字符串构造输入流，便于检查输入是否仅包含一个整数。
        int value; // 声明待解析的整数；只有成功读取后才会使用它。
        std::string extra; // 用于检测期望内容之后是否还有多余的非空白文本。
        if ((input >> value) && !(input >> extra) && value >= low && value <= high) // >> 从流中提取数据；要求整数解析成功、无多余字段、数值在闭区间 [low, high] 内。
            return value; // 返回已验证的整数，从而结束输入重试循环和函数。
        std::cout << "输入无效，请输入 " << low << " 到 " << high << " 的整数。\n"; // 输出合法范围的提示；分号结束这条输出语句，随后循环重新请求输入。
    } // 结束本轮整数输入检查；没有成功 return 时，for 循环继续读取。
} // 结束 readNumber 函数；成功时返回整数，输入结束或 q 则通过异常退出。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
Action readAction(const std::string& player) { // 定义人工行动读取函数，接收玩家名称并返回解析好的 Action。
    for (;;) { // for 的初始化、条件和更新表达式都省略，形成无限循环，直到 return 或异常离开。
        std::istringstream input(readLine(player + " 行动（输入隐藏）：", true)); // + 拼接提示字符串；传入 true 隐藏输入，再用字符串流解析命令。
        char command; // char 保存单个命令字符，例如 m（移动）或 a（攻击）。
        int target; // int 保存用户输入的目标格号，随后会检查其范围。
        std::string extra; // 用于检测期望内容之后是否还有多余的非空白文本。
        if ((input >> command >> target) && !(input >> extra) && validCell(target)) { // 要求恰好读到一个字符命令和一个合法格号，并且没有多余字段；此处尚未检查实际距离。
            if (command == 'm' || command == 'M') return {Kind::Move, target}; // 支持大小写移动命令；单引号表示字符字面量，花括号构造移动行动。
            if (command == 'a' || command == 'A') return {Kind::Attack, target}; // 支持大小写攻击命令，返回包含目标格号的攻击行动。
        } // 结束输入字段格式合法的分支；若命令字符未知，则继续显示格式提示。
        std::cout << "格式无效。移动用 m 格号，攻击用 a 格号，退出用 q。\n"; // 解析失败或命令未知时输出帮助，继续下一轮输入，不消耗游戏回合。
    } // 结束本轮行动输入；未成功返回时再次循环读取。
} // 结束 readAction 函数，合法命令会返回 Action 对象。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
void play() { // 定义整局游戏的交互入口；void 表示函数不返回数值。
    std::random_device seed; // 创建用于获取随机种子的设备对象；是否具备真实随机性取决于标准库实现。
    std::mt19937 random(seed()); // 用 seed() 提供的数值初始化梅森旋转伪随机引擎；() 表示调用随机设备。
    std::cout << "=== 九格暗战 ===\n\n" // 开始输出游戏标题；双引号包围字符串，\n 是换行转义，连续两个表示空一行。
                 " 1 | 2 | 3\n" // 相邻字符串字面量会在编译时拼接；本段显示棋盘第一行，竖线只是显示字符。
                 "---+---+---\n" // 继续拼接输出字符串，显示棋盘行与行之间的分隔线。
                 " 4 | 5 | 6\n" // 继续拼接字符串，显示棋盘第二行格号。
                 "---+---+---\n" // 继续拼接输出字符串，显示棋盘行与行之间的分隔线。
                 " 7 | 8 | 9\n\n" // 显示棋盘第三行，并通过两次换行把棋盘与说明分开。
                 "每回合移动一格，或攻击一格。相邻仅指上下左右。\n" // 拼接规则文字，说明每回合只能执行一种行动，并禁止斜向相邻。
                 "攻击可选自己所在格；双方可以同格，命中立即获胜。\n" // 拼接同格与胜负规则的说明；这只是输出文字，真正判定由 Game::act 完成。
                 "移动：m 目标格（如 m 2）；攻击：a 目标格（如 a 5）。\n" // 拼接命令格式说明，示范移动与攻击的输入方式。
                 "秘密输入不会显示，请自行记住位置；任意输入处可用 q 退出。\n" // 提示用户记录自己的位置，并说明 q 退出命令。
                 "双人共用键盘时，另一人请在秘密输入期间回避。\n\n"; // 拼接隐私提示；末尾分号结束从 cout 开始的整条输出语句。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    const bool versusBot = readNumber("1. 双人对战  2. 人机对战，请选择：", 1, 2) == 2; // 读取模式并比较是否为 2，保存为不可重新赋值的布尔值：true 代表人机模式。
    const std::array<std::string, 2> names = {"玩家 1", versusBot ? "电脑" : "玩家 2"}; // 初始化两名玩家的名称；条件运算符 ? : 根据模式选择第二个名称。
    const int first = readNumber("玩家 1 选择起始格（1-9，输入隐藏）：", 1, 9, true); // 隐藏地读取玩家 1 的合法起始格；true 是实参，启用输入隐藏。
    std::cout << "玩家 1 已就位。\n"; // 仅公布玩家已完成选择，不打印起始位置。
    const int second = versusBot // 开始用条件运算符初始化第二个起始格；判断条件为是否人机模式，表达式跨三行书写。
        ? std::uniform_int_distribution<int>(1, 9)(random) // ? 后为条件成立分支：用均匀整数分布随机选择 1 到 9 的起始格，包含两端。
        : readNumber("玩家 2 选择起始格（1-9，输入隐藏）：", 1, 9, true); // : 后为条件不成立分支：双人模式下让玩家 2 秘密输入起始格，并结束赋值语句。
    std::cout << names[1] << "已就位。玩家 1 先手。\n\n"; // 公开第二名玩家已就位和先手安排，不公开任何坐标。
    Game game(first, second); // 用两名玩家的起始位置调用 Game 构造函数，创建裁判对象。
    Bot bot; // 创建电脑推理对象；默认构造函数把九个格子都列为可能位置。
    int round = 1; // 行动编号从 1 开始；此处“回合”指一名玩家的一次行动，不是双方各行动一次。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
    while (!game.finished()) { // while 循环在游戏尚未结束时持续执行；! 对结束标记取反。
        const int player = game.turn(); // 从裁判对象读取当前应行动的玩家索引。
        const bool computerTurn = versusBot && player == 1; // 同时是人机模式且轮到玩家索引 1，才由电脑决策。
        std::cout << "第 " << round << " 回合 — " << names[player] << '\n'; // 输出当前行动编号及行动者名称，让双方知道轮到谁。
        const Action action = computerTurn // 用条件表达式选择行动来源，结果保存为本次回合不可修改的 Action。
            ? bot.choose(game.position(1), random) : readAction(names[player]); // 电脑只得到自身位置与随机引擎；否则读取人工命令，决策接口不传入对手真实位置。
        const Result result = game.act(action); // 将行动交给裁判检查与执行，保存返回的结果枚举。
        if (result == Result::Invalid) { // 如果动作格式正确但违反距离等规则，进入错误提示分支。
            std::cout << "行动不合法：移动须上下左右一格；攻击须在本格或相邻格。\n" // 开始输出不合法行动的规则说明，不打印用户输入的秘密目标。
                         "此次不消耗回合，请重新输入。\n\n"; // 拼接“不消耗回合”的提示，并结束整条输出语句。
            continue; // 立即开始当前循环的下一轮，跳过后面的处理；此处用于非法行动后重新输入。
        } // 结束非法行动处理分支；其中的 continue 已确保该回合不被消耗。
        if (result == Result::Moved) { // 按裁判结果区分移动与攻击；这里处理成功移动。
            std::cout << names[player] << "移动了一格。\n\n"; // 只公布移动发生，不显示方向或终点，保护位置秘密。
            if (versusBot && player == 0) bot.observeEnemyMove(); // 人机模式下玩家 1 移动后，电脑根据“移动一步”这一公开信息更新推理集合。
        } else { // 结束前一个 if 分支；当前述条件不成立时，执行这个 else 分支。
            std::cout << names[player] << "攻击 " << action.target << " 号格，"; // 攻击目标属于公开信息，在行动合法且执行后输出目标格号。
            if (result == Result::Win) { // 判断此次攻击是否已经命中并使对局结束。
                std::cout << "命中！" << names[player] << "获胜！\n"; // 公布攻击命中，以及获胜玩家的名称。
            } else { // 结束前一个 if 分支；当前述条件不成立时，执行这个 else 分支。
                std::cout << "未命中。\n\n"; // 公布攻击失败；双方随后依据这一结果继续推理。
                if (versusBot && player == 0) bot.observeEnemyAttack(action.target); // 玩家攻击未命中时，电脑只使用公开的攻击落点反推玩家位置。
                if (computerTurn) bot.observeOwnMiss(action.target); // 如果是电脑攻击未命中，电脑把该目标格从对手可能位置中排除。
            } // 结束攻击未命中的处理分支，也结束命中与否的条件判断。
        } // 结束攻击处理分支，也结束移动与攻击的结果分类。
        ++round; // 自增运算符 ++ 将行动编号加 1；非法行动早已 continue，因此不计入这里。
    } // 结束一次合法行动的交互处理；while 再次检查游戏是否结束。
} // 结束 play 函数；胜利或退出信号会使整局交互结束。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
} // 结束 hidden_grid 命名空间；后面的 main 位于全局作用域。
// 段落分隔：将不同功能分开阅读；本行不参与程序执行。
int main() { // C++ 程序入口函数；返回 int 状态码，通常 0 表示正常退出，非零表示错误。
#ifdef _WIN32 // 条件编译：仅当编译器定义了 _WIN32 宏时，编译后面的 Windows 专用代码。
    const UINT oldInputCP = GetConsoleCP(); // UINT 是 Windows 无符号整数类型；记录原控制台输入代码页，方便退出时恢复。
    const UINT oldOutputCP = GetConsoleOutputCP(); // 保存原控制台输出代码页，不在后续流程中修改这个保存值。
    SetConsoleCP(CP_UTF8); // 将 Windows 控制台输入代码页切换为 UTF-8，使中文输入输出环境更一致。
    SetConsoleOutputCP(CP_UTF8); // 将 Windows 控制台输出代码页设为 UTF-8，以正确显示源码中的中文字符串。
#endif // 结束最近一组 #if、#ifdef 或 #ifndef 条件编译区域。
    int exitCode = 0; // 初始化程序退出码为 0，表示正常；发生运行错误后会改为 1。
    try { // 开始异常保护区域；其中抛出的异常会交由匹配的 catch 分支处理。
        hidden_grid::play(); // :: 是作用域解析运算符，调用 hidden_grid 命名空间里的游戏主流程函数。
    } catch (const hidden_grid::Quit&) { // 按常量引用捕获正常退出信号；没有变量名，因为无需读取异常内容。
        std::cout << "\n游戏已退出。\n"; // 正常退出时输出提示，不将它当作运行错误。
    } catch (const std::exception& error) { // 捕获其他标准异常的常量引用 error，避免复制并保留实际异常类型的信息。
        std::cerr << "\n运行错误：" << error.what() << '\n'; // 向标准错误流输出异常说明；what() 返回描述错误的字符串。
        exitCode = 1; // 将进程退出码设为非零，告知系统或测试脚本程序失败。
    } // 结束标准异常处理分支，随后执行 Windows 下的等待和编码恢复。
#ifdef _WIN32 // 条件编译：仅当编译器定义了 _WIN32 宏时，编译后面的 Windows 专用代码。
    if (hidden_grid::interactiveInput() && std::cin.good()) { // 仅在交互式输入且流状态正常时等待用户，避免重定向测试或输入结束后卡住。
        std::cout << "按回车关闭窗口……" << std::flush; // 提示回车退出并立即刷新，防止双击运行后窗口马上关闭。
        std::string ignored; // 声明临时字符串接收最后一行输入；名称表示它的内容不再用于后续逻辑。
        std::getline(std::cin, ignored); // 读取并丢弃一行，交互终端通常会等待用户按回车。
    } // 结束等待回车分支，继续恢复程序启动前的控制台编码。
    SetConsoleCP(oldInputCP); // 恢复程序启动前的 Windows 输入代码页。
    SetConsoleOutputCP(oldOutputCP); // 恢复程序启动前的 Windows 输出代码页。
#endif // 结束最近一组 #if、#ifdef 或 #ifndef 条件编译区域。
    return exitCode; // 把状态码返回给操作系统，并结束程序。
} // 结束全局 main 入口函数；前面的 return 已把退出码交给操作系统。
