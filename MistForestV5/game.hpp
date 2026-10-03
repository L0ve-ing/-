#pragma once // 预处理：防止头文件被重复包含。
#include <array> // 引入固定大小的数组。
#include <memory> // 引入共享所有权的智能指针。
#include <optional> // 引入可省略的自定义地图参数。
#include <random> // 引入可复现随机数引擎。
#include <string> // 引入中文宽字符串。
#include <vector> // 引入动作序列容器。
namespace mist { // 命名空间：隔离包含自定义地图的游戏规则接口。
enum class Mode { Scout=3, Heroes4=4, Heroes=5 }; // 枚举：三格侦察版、四格与五格职业版，数值等于棋盘边长。
enum class Terrain { Ground,Wall,Lake,Whirlpool }; // 地形枚举：漩涡可站立且透明，方墙和湖泊沿用原有阻挡规则。
struct Map { std::array<Terrain,25> cells{}; std::array<bool,30> horizontal{},vertical{}; }; // 地图保存二十五格地形与各三十条水平、竖直边墙。
Map defaultMap(Mode mode); bool validMap(Mode mode,const Map& map); // 获取默认地图并验证地形、边墙和至少一个可站立格。
std::string encodeMap(const Map& map); bool decodeMap(Mode mode,const std::string& text,Map& map); // 将地图编码为八十五个字符；解码失败不改变原地图。
enum class Role { Sniper, Wolf, Ink, Shadow, Heavy }; // 枚举：原有四职业和拥有护甲、能够冲锋的重骑。
enum class Kind { Move, Attack, Row, Col, Dash, Follow, Lead, Charge }; // 枚举：原有动作及对手不可见路线的重骑冲锋步骤。
using Mask=std::array<bool,25>; // 类型别名：最多二十五格的候选集合。
struct FollowReport { int turn=0,steps=0,blocked=0; }; // 私人报告记录最近一次跟随时复制的步数和受阻次数。
struct ScanReport { int turn=0,target=0; Kind kind=Kind::Row; bool positive=false; }; // 私人侦察结果保留获得时刻与区域。
struct WhirlpoolReport { int turn=0,from=0,to=0; }; // 私人报告记录本人在轮次开始时被漩涡吸引的起点和终点。
struct Step { Kind kind=Kind::Move; int target=0; }; // 聚合结构：格号从一开始，行列索引从零开始。
struct Action { std::vector<Step> steps; }; // 一回合由按顺序执行的子动作组成。
struct View { // 受限视图：不包含敌人的真实位置。
    Mode mode=Mode::Scout; int own=0,player=0,turn=0,winner=-1,turns=0; // 当前模式、本人位置、身份、行动者、胜负和回合数。
    std::array<Role,2> roles{Role::Sniper,Role::Sniper}; std::array<int,2> armor{}; // 双方职业、重骑剩余护甲均属于公开信息。
    std::array<int,2> cooldown{}; std::array<bool,2> following{}; Mask possible{}; // 公开冷却、跟随标记及本人推理集合。
    FollowReport followReport; ScanReport scanReport; WhirlpoolReport whirlpoolReport; // 仅本人的跟随、侦察和漩涡位移报告可以离开裁判。
    Map map; // 地形公开，受限视图允许双方读取同一地图。
}; // 结束玩家视图。
struct Snapshot { // 完整快照只允许裁判与赛后复盘读取。
    Mode mode=Mode::Scout; std::array<int,2> positions{}; std::array<Role,2> roles{}; std::array<int,2> armor{}; // 双方真实位置、角色和公开护甲状态。
    std::array<int,2> cooldown{}; std::array<bool,2> following{}; std::array<Mask,2> possible{}; // 每人的技能与候选集合。
    int turn=0,winner=-1,turns=0; // 当前行动者、胜负与已执行回合数。
    std::array<FollowReport,2> followReports{}; std::array<ScanReport,2> scanReports{}; std::array<WhirlpoolReport,2> whirlpoolReports{}; // 裁判分别保存两人的私人技能和漩涡反馈。
    Map map; // 复盘同时保留开局所用的全部地形与边墙。
}; // 结束快照。
struct Event { int actor=0; Action action; bool hit=false,scanPositive=false,damaged=false,armorBlocked=false; }; // 命中表示致命，受伤表示接触目标，护甲破损后对局继续。
int size(Mode mode); // 返回棋盘边长。
bool heroMode(Mode mode); // 四格与五格地图均采用五职业规则。
bool validMode(int mode); bool validRole(int role); // 检查网络与界面输入的枚举范围。
bool valid(Mode mode,int cell); // 按默认地图判断站位，默认五格地图的中心与四角为方墙。
bool valid(Mode mode,int cell,const Map& map); // 根据实际地图检查格号和是否可以站人。
bool canMove(Mode mode,int from,int to,const Map& map); // 检查原地等待或上下左右一步，移动不能越过任何边墙。
int distance(Mode mode,int a,int b); // 计算行差与列差绝对值之和。
bool clearShot(Mode mode,int a,int b); // 按默认地图判断两格中心连线是否避开墙体，擦边也算相交。
bool clearShot(Mode mode,int a,int b,const Map& map); // 线段碰到方墙或边墙即被挡住，湖泊允许攻击穿过和选为目标。
int range(Mode mode,Role role); // 三格版射程一；职业版按角色取值。
bool canAttack(Mode mode,Role role,int from,int target); // 按默认地图同时检查射程与墙体遮挡。
bool canAttack(Mode mode,Role role,int from,int target,const Map& map); // 按实际地图同时检查射程与所有墙体遮挡。
bool inRegion(Mode mode,int cell,Kind kind,int target); // 判断某个位置是否落在指定行列。
bool inRegion(Mode mode,int cell,Kind kind,int target,const Map& map); // 按实际地图检查站人位置是否落在指定行列。
std::vector<int> dashPath(Mode mode,int from,int target); // 求距离恰好为二且避开禁入格的两步路径，优先竖直方向。
std::vector<int> dashPath(Mode mode,int from,int target,const Map& map); // 求恰好两步且逐步避开地形与边墙的冲刺路径。
int copiedMove(Mode mode,int follower,int from,int to); // 按敌人单步位移复制移动，越界或禁入格阻挡时原地停留。
int copiedMove(Mode mode,int follower,int from,int to,const Map& map); // 复制位移遇到方墙、湖泊、边墙或边界时跳过这一步。
bool validate(const View& view,const Action& action,bool prefix=false,std::wstring* why=nullptr,int* preview=nullptr); // 校验完整回合或正在编辑的前缀，并返回预览位置。
std::wstring roleName(Role role); std::wstring roleHelp(Role role); // 返回玩家可读的职业名称与规则。
std::wstring actionText(const Event& event,bool reveal=false); // 默认隐藏移动终点；赛后可显示完整动作。
Event publicEvent(const Event& event); // 清除所有秘密移动目标与侦察结果。
bool samePublic(const Event& a,const Event& b); // 校验重建复盘是否与实时公开记录一致。
class Game { // 裁判拥有完整状态，外部只能通过校验后的行动改变它。
public: // 公开接口供本地界面和联网裁判使用。
    Game(int p0,int p1,Mode mode=Mode::Scout,std::array<Role,2> roles={Role::Sniper,Role::Sniper},int first=0,std::optional<Map> map=std::nullopt); // 创建对局；省略地图时使用该模式的默认地图。
    View view(int player) const; bool legal(const Action& action) const; bool act(const Action& action); // 返回受限视图、检查并执行完整回合。
    const Snapshot& state() const; const std::vector<Snapshot>& snapshots() const; const std::vector<Event>& events() const; // 完整信息仅供裁判和赛后使用。
    bool finished() const; // 是否已经命中或达到八十个回合上限。
private: // 私有成员不能由界面任意修改。
    Snapshot state_; std::vector<Snapshot> snapshots_; std::vector<Event> events_; // 保存当前局面和按回合记录。
    void move(int actor,int target,bool exactStep,bool copying,bool publicTarget=false); // 执行单步移动、随影复制及双方知识更新，可选参数供确实公开的位移使用。
    int whirlpoolDestination(int position) const; void applyRoundWhirlpools(int reportTurn); // 每轮同时吸引双方一次并更新私人报告与候选集合。
}; // 结束裁判类。
Action chooseAction(const View& view,int style,std::mt19937& rng); // 电脑只使用允许获取的视图，风格零进攻、一反击、二灵活。
} // 结束命名空间。
