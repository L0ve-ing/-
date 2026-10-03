#pragma once // 预处理：防止头文件被重复包含。
#include <array> // 引入固定大小的数组。
#include <memory> // 引入共享所有权的智能指针。
#include <random> // 引入可复现随机数引擎。
#include <string> // 引入中文宽字符串。
#include <vector> // 引入动作序列容器。
namespace mist { // 命名空间：隔离第三版规则接口。
enum class Mode { Scout=3, Heroes4=4, Heroes=5 }; // 枚举：三格侦察版、四格与五格职业版，数值等于棋盘边长。
enum class Role { Sniper, Wolf, Ink, Shadow }; // 枚举：狙击手、座狼、白墨和随影。
enum class Kind { Move, Attack, Row, Col, Dash, Follow, Lead }; // 枚举：普通移动、攻击、行列侦察、冲刺、标记跟随和移动时带动对方。
using Mask=std::array<bool,25>; // 类型别名：最多二十五格的候选集合。
struct FollowReport { int turn=0,steps=0,blocked=0; }; // 私人报告记录最近一次跟随时复制的步数和受阻次数。
struct ScanReport { int turn=0,target=0; Kind kind=Kind::Row; bool positive=false; }; // 私人侦察结果保留获得时刻与区域。
struct Step { Kind kind=Kind::Move; int target=0; }; // 聚合结构：格号从一开始，行列索引从零开始。
struct Action { std::vector<Step> steps; }; // 一回合由按顺序执行的子动作组成。
struct View { // 受限视图：不包含敌人的真实位置。
    Mode mode=Mode::Scout; int own=0,player=0,turn=0,winner=-1,turns=0; // 当前模式、本人位置、身份、行动者、胜负和回合数。
    std::array<Role,2> roles{Role::Sniper,Role::Sniper}; // 双方职业在选点完成后公开。
    std::array<int,2> cooldown{}; std::array<bool,2> following{}; Mask possible{}; // 公开冷却、跟随标记及本人推理集合。
    FollowReport followReport; ScanReport scanReport; // 仅本人的跟随障碍反馈与侦察结果可以离开裁判。
}; // 结束玩家视图。
struct Snapshot { // 完整快照只允许裁判与赛后复盘读取。
    Mode mode=Mode::Scout; std::array<int,2> positions{}; std::array<Role,2> roles{}; // 双方真实位置和角色。
    std::array<int,2> cooldown{}; std::array<bool,2> following{}; std::array<Mask,2> possible{}; // 每人的技能与候选集合。
    int turn=0,winner=-1,turns=0; // 当前行动者、胜负与已执行回合数。
    std::array<FollowReport,2> followReports{}; std::array<ScanReport,2> scanReports{}; // 裁判分别保存两人的私人技能反馈。
}; // 结束快照。
struct Event { int actor=0; Action action; bool hit=false,scanPositive=false; }; // 事件保存实际执行的动作；移动终点只可赛后公开。
int size(Mode mode); // 返回棋盘边长。
bool heroMode(Mode mode); // 四格与五格地图均采用四职业规则。
bool validMode(int mode); bool validRole(int role); // 检查网络与界面输入的枚举范围。
bool valid(Mode mode,int cell); // 五格地图的中心与四角禁入，四格地图所有格均可进入。
int distance(Mode mode,int a,int b); // 计算行差与列差绝对值之和。
bool clearShot(Mode mode,int a,int b); // 判断两格中心连线是否避开中心封闭方格，擦边也算相交。
int range(Mode mode,Role role); // 三格版射程一；职业版按角色取值。
bool canAttack(Mode mode,Role role,int from,int target); // 同时检查射程与中心遮挡。
bool inRegion(Mode mode,int cell,Kind kind,int target); // 判断某个位置是否落在指定行列。
std::vector<int> dashPath(Mode mode,int from,int target); // 求距离恰好为二且避开禁入格的两步路径，优先竖直方向。
int copiedMove(Mode mode,int follower,int from,int to); // 按敌人单步位移复制移动，越界或禁入格阻挡时原地停留。
bool validate(const View& view,const Action& action,bool prefix=false,std::wstring* why=nullptr,int* preview=nullptr); // 校验完整回合或正在编辑的前缀，并返回预览位置。
std::wstring roleName(Role role); std::wstring roleHelp(Role role); // 返回玩家可读的职业名称与规则。
std::wstring actionText(const Event& event,bool reveal=false); // 默认隐藏移动终点；赛后可显示完整动作。
Event publicEvent(const Event& event); // 清除所有秘密移动目标与侦察结果。
bool samePublic(const Event& a,const Event& b); // 校验重建复盘是否与实时公开记录一致。
class Game { // 裁判拥有完整状态，外部只能通过校验后的行动改变它。
public: // 公开接口供本地界面和联网裁判使用。
    Game(int p0,int p1,Mode mode=Mode::Scout,std::array<Role,2> roles={Role::Sniper,Role::Sniper},int first=0); // 创建双方秘密选点完成后的对局。
    View view(int player) const; bool legal(const Action& action) const; bool act(const Action& action); // 返回受限视图、检查并执行完整回合。
    const Snapshot& state() const; const std::vector<Snapshot>& snapshots() const; const std::vector<Event>& events() const; // 完整信息仅供裁判和赛后使用。
    bool finished() const; // 是否已经命中或达到八十个回合上限。
private: // 私有成员不能由界面任意修改。
    Snapshot state_; std::vector<Snapshot> snapshots_; std::vector<Event> events_; // 保存当前局面和按回合记录。
    void move(int actor,int target,bool exactStep,bool copying); // 执行单步移动、随影复制及双方知识更新。
}; // 结束裁判类。
Action chooseAction(const View& view,int style,std::mt19937& rng); // 电脑只使用允许获取的视图，风格零进攻、一反击、二灵活。
} // 结束命名空间。
