#include "game.hpp" // 引入第三版游戏规则声明。
#include <algorithm> // 使用计数、最小值和最大值算法。
#include <cmath> // 使用绝对值进行距离与线段计算。
#include <stdexcept> // 使用异常拒绝非法开局参数。
namespace mist { // 所有实现属于游戏规则命名空间。
int size(Mode mode) { return static_cast<int>(mode); } // 枚举值与棋盘边长一致。
bool heroMode(Mode mode) { return mode==Mode::Heroes4 || mode==Mode::Heroes; } // 两种职业地图共享角色技能与回合预算。
bool validMode(int mode) { return mode==3 || mode==4 || mode==5; } // 只允许三格侦察与四格、五格职业模式。
bool validRole(int role) { return role>=0 && role<4; } // 四种职业的编号范围。
bool valid(Mode mode,int cell) { return validMode(size(mode)) && cell>=1 && cell<=size(mode)*size(mode) && !(mode==Mode::Heroes && (cell==1 || cell==5 || cell==13 || cell==21 || cell==25)); } // 五格中心与四个角格全程禁入，四格地图没有禁入格。
int distance(Mode mode,int a,int b) { int n=size(mode); return std::abs((a-1)/n-(b-1)/n)+std::abs((a-1)%n-(b-1)%n); } // 曼哈顿距离为行列差绝对值之和。
bool clearShot(Mode mode,int a,int b) { // 用线段和轴对齐方格相交算法判断遮挡。
    if(!valid(mode,a) || !valid(mode,b)) return false; // 起点或目标非法时不能攻击。
    if(mode!=Mode::Heroes) return true; // 三格与四格地图没有攻击障碍。
    double start[2]={static_cast<double>((a-1)%5),static_cast<double>((a-1)/5)}; // 把格号转为零起点坐标中的格子中心。
    double end[2]={static_cast<double>((b-1)%5),static_cast<double>((b-1)/5)},lo=0,hi=1; // 线段参数范围为零到一。
    for(int axis=0;axis<2;++axis) { // 分别计算与中心方格横向、纵向区间的交集。
        double delta=end[axis]-start[axis]; // 此坐标轴的方向分量。
        if(delta==0) { if(start[axis]<1.5 || start[axis]>2.5) return true; } // 平行且在障碍外时，整条线段不会相交。
        else { double x=(1.5-start[axis])/delta,y=(2.5-start[axis])/delta; if(x>y) std::swap(x,y); lo=std::max(lo,x); hi=std::min(hi,y); if(lo>hi+1e-12) return true; } // 求进入与离开障碍的线段参数。
    } // 两轴均有公共参数代表碰到方格内部、边或角。
    return false; // 相交的攻击被中心障碍阻挡。
} // 结束攻击遮挡检查。
int range(Mode mode,Role role) { return mode==Mode::Scout?1:role==Role::Sniper?3:role==Role::Shadow?2:1; } // 按模式和职业决定射程。
bool canAttack(Mode mode,Role role,int from,int target) { return valid(mode,from) && valid(mode,target) && distance(mode,from,target)<=range(mode,role) && clearShot(mode,from,target); } // 攻击允许距离零，且必须同时满足视线。
bool inRegion(Mode mode,int cell,Kind kind,int target) { int n=size(mode); return valid(mode,cell) && target>=0 && target<n && (kind==Kind::Row?(cell-1)/n==target:kind==Kind::Col && (cell-1)%n==target); } // 行列侦察不受攻击遮挡限制。
std::vector<int> dashPath(Mode mode,int from,int target) { // 枚举距离恰好为二的两步路径，空向量表示目标不合法或无法到达。
    if(!valid(mode,from) || !valid(mode,target) || distance(mode,from,target)!=2) return {}; // 白墨冲刺必须恰好距离二，不能一步或原地使用。
    int n=size(mode); for(int delta:{-n,n,-1,1}) { int mid=from+delta; if(valid(mode,mid) && distance(mode,from,mid)==1 && distance(mode,mid,target)==1) return {mid,target}; } // 找到一条合法最短路径，竖直方向优先。
    return {}; // 被禁入格阻挡且两步内无路时拒绝该终点。
} // 结束冲刺路径搜索。
int copiedMove(Mode mode,int follower,int from,int to) { // 复制一段实际移动向量，不根据敌方终点瞬移。
    int n=size(mode),row=(follower-1)/n+(to-1)/n-(from-1)/n,col=(follower-1)%n+(to-1)%n-(from-1)%n; // 将敌人的行列位移加到自己的位置。
    if(row<0 || row>=n || col<0 || col>=n || !valid(mode,row*n+col+1)) return follower; // 越界或禁入格时跳过这一步。
    return row*n+col+1; // 同格允许，因此不检查是否与敌人重叠。
} // 结束跟随位移。
std::wstring roleName(Role role) { return role==Role::Sniper?L"狙击手":role==Role::Wolf?L"座狼":role==Role::Ink?L"白墨":L"随影"; } // 四种职业名称。
std::wstring roleHelp(Role role) { // 用简短文本解释职业的回合组合。
    if(role==Role::Sniper) return L"射程3；移动、攻击或侦察选一项。"; // 狙击手保持单一主行动。
    if(role==Role::Wolf) return L"射程1；可移动合计2步，在攻击前后自由分配。"; // 座狼的移动预算属于整个回合。
    if(role==Role::Ink) return L"射程1；可先移动1步再攻击；行动前后冲刺恰好2步，随后两个自己的回合不可冲刺。"; // 白墨免费移动和冲刺分别计数。
    return L"射程2；可标记后在敌方回合跟随，也可移动时带动对方；受阻则跳过并通知本人。"; // 两种方向的复制都不额外消耗被带动者回合。
} // 结束职业说明。
bool validate(const View& v,const Action& a,bool prefix,std::wstring* why,int* preview) { // 只用本人状态校验，不接触隐藏敌人位置。
    auto reject=[&](const wchar_t* message) { if(why) *why=message; return false; }; // 用局部匿名函数统一输出错误。
    if(v.player<0 || v.player>1 || !valid(v.mode,v.own) || !validRole(static_cast<int>(v.roles[v.player])) || v.winner!=-1 || v.turn!=v.player) return reject(L"现在不是你的有效行动回合。"); // 短路检查避免非法玩家编号访问数组。
    if(a.steps.size()>4 || (!prefix && a.steps.empty())) return reject(L"请至少安排一个行动；最多四个子动作。"); // 白墨和座狼的合法组合最多四个操作。
    Role role=v.roles[v.player]; int own=v.own,moves=0,main=0,dashes=0; bool afterMain=false; // 记录预算及主行动顺序。
    if((v.mode==Mode::Scout || role==Role::Sniper || role==Role::Shadow) && a.steps.size()>1) return reject(L"本职业每回合选择一个主行动。"); // 基础模式与这两个职业不能移动并攻击。
    for(std::size_t i=0;i<a.steps.size();++i) { const auto step=a.steps[i]; // 按玩家安排的顺序检查每个子动作。
        if(step.kind==Kind::Move || step.kind==Kind::Lead) { // 普通移动允许选自身格等待，随影可选择带动对方。
            if(step.kind==Kind::Lead && (!heroMode(v.mode) || role!=Role::Shadow)) return reject(L"只有随影可以移动并带动对方。"); // 带动能力属于随影专属操作。
            if(!valid(v.mode,step.target) || distance(v.mode,own,step.target)>1) return reject(L"普通移动只能原地或上下左右一步，不能进入禁入格。"); // 避免斜移、跨行和障碍。
            if(++moves>(heroMode(v.mode) && role==Role::Wolf?2:1)) return reject(L"本回合普通移动次数已用完。"); // 座狼两次，其余一次。
            if(heroMode(v.mode) && role==Role::Ink && afterMain) return reject(L"白墨普通移动只能安排在攻击或侦察之前。"); // 攻击后只能用额外冲刺。
            own=step.target; // 更新后续攻击使用的预览位置。
        } else if(step.kind==Kind::Dash) { // 白墨额外技能不占主行动。
            if(!heroMode(v.mode) || role!=Role::Ink || v.cooldown[v.player]>0 || ++dashes>1) return reject(L"只有冷却完成的白墨可以冲刺一次。"); // 职业、次数与冷却同时检查。
            if(i!=0 && i+1!=a.steps.size()) return reject(L"冲刺只能放在本回合行动的最前或最后。"); // 禁止把冲刺夹在普通移动与攻击中间。
            if(dashPath(v.mode,own,step.target).empty()) return reject(L"冲刺必须沿合法路径移动恰好两步，不能经过禁入格。"); // 冲刺采用可达路径而非穿墙瞬移。
            own=step.target; // 更新冲刺后的攻击或结束位置。
        } else { // 其余类型都消耗唯一主行动。
            if(++main>1) return reject(L"攻击、侦察和指定跟随每回合只能选择一项。"); // 不允许攻击后侦察或重复攻击。
            afterMain=true; // 标记主行动已经安排。
            if(step.kind==Kind::Attack) { if(!canAttack(v.mode,role,own,step.target)) return reject(L"目标为禁入格、超出射程，或攻击连线碰到中心障碍。"); } // 使用子动作当时的位置计算攻击。
            else if(step.kind==Kind::Row || step.kind==Kind::Col) { if(step.target<0 || step.target>=size(v.mode)) return reject(L"侦察行列超出地图。"); } // 每回合均可侦察，行列索引必须合法。
            else if(step.kind==Kind::Follow) { if(!heroMode(v.mode) || role!=Role::Shadow || step.target!=1-v.player) return reject(L"随影只能指定另一名玩家跟随。"); } // 两人各一个单位，标记对象明确。
            else return reject(L"未知行动类型。"); // 网络中无效枚举也被拒绝。
        } // 结束单个子动作分类。
    } // 全部子动作预算与位置已检查。
    if(preview) *preview=own; // 可选返回值供棋盘显示最终预览位置。
    if(why) why->clear(); // 成功时清除旧错误。
    return true; // 合法等待、仅移动或仅冲刺也会消耗一个完整回合。
} // 结束行动校验。
Event publicEvent(const Event& event) { Event out=event; out.scanPositive=false; for(auto& step:out.action.steps) if(step.kind==Kind::Move || step.kind==Kind::Dash || step.kind==Kind::Lead) step.target=0; return out; } // 脱敏副本不泄露三种移动的终点与侦察结果。
bool samePublic(const Event& a,const Event& b) { auto x=publicEvent(a),y=publicEvent(b); if(x.actor!=y.actor || x.hit!=y.hit || x.action.steps.size()!=y.action.steps.size()) return false; for(std::size_t i=0;i<x.action.steps.size();++i) if(x.action.steps[i].kind!=y.action.steps[i].kind || x.action.steps[i].target!=y.action.steps[i].target) return false; return true; } // 逐字段比较公开事件，不比较秘密字段。
std::wstring actionText(const Event& e,bool reveal) { // 组合回合日志，进行中不泄露移动坐标。
    std::wstring out=L"玩家"+std::to_wstring(e.actor+1)+L"："; // 显示编号从一开始。
    for(const auto& step:e.action.steps) { if(out.back()!=L'：') out+=L" → "; // 用箭头保持动作顺序。
        if(step.kind==Kind::Move || step.kind==Kind::Dash) out+=(step.kind==Kind::Move?L"移动/等待":L"冲刺")+(reveal?L"到"+std::to_wstring(step.target):std::wstring()); // 普通移动不说明是否实际离开原格。
        else if(step.kind==Kind::Lead) out+=L"移动并带动对方"+(reveal?L"到"+std::to_wstring(step.target):std::wstring()); // 只公开带动行为，不公开移动方向或终点。
        else if(step.kind==Kind::Attack) out+=L"攻击"+std::to_wstring(step.target)+(e.hit?L"命中":L"未中"); // 攻击目标和结果始终公开。
        else if(step.kind==Kind::Follow) out+=L"指定跟随"; // 标记会在敌方下一回合生效。
        else out+=L"侦察第"+std::to_wstring(step.target+1)+(step.kind==Kind::Row?L"行":L"列")+(reveal?(e.scanPositive?L"有目标":L"无目标"):L""); // 侦察结果仅赛后公开。
    } // 结束序列格式化。
    return out; // 返回本回合的可读记录。
} // 结束日志文字。
Game::Game(int p0,int p1,Mode mode,std::array<Role,2> roles,int first) { // 构造函数统一验证地图、职业和先手。
    if(!valid(mode,p0) || !valid(mode,p1) || !validRole(static_cast<int>(roles[0])) || !validRole(static_cast<int>(roles[1])) || first<0 || first>1) throw std::invalid_argument("Invalid setup"); // 拒绝禁入格起点和非法输入。
    state_.mode=mode; state_.positions={p0,p1}; state_.roles=roles; state_.turn=first; // 保存初始完整局面，同格允许。
    for(auto& mask:state_.possible) for(int cell=1;cell<=25;++cell) mask[cell-1]=valid(mode,cell); // 初始候选只包含本模式可进入的格。
    snapshots_.push_back(state_); // 保存开局供赛后重建。
} // 结束构造。
View Game::view(int player) const { if(player<0 || player>1) throw std::invalid_argument("Invalid player"); return {state_.mode,state_.positions[player],player,state_.turn,state_.winner,state_.turns,state_.roles,state_.cooldown,state_.following,state_.possible[player],state_.followReports[player],state_.scanReports[player]}; } // 只返回自己的真实位置与私人报告。
bool Game::legal(const Action& action) const { return validate(view(state_.turn),action); } // 规则合法性不使用对手真位置。
void Game::move(int actor,int target,bool exactStep,bool copying) { // 单次移动同时更新自动跟随和双方知识。
    const auto before=state_.positions; const int enemy=1-actor; state_.positions[actor]=target; // 先保存两个真实起点，再执行主动移动。
    if(copying) state_.positions[enemy]=copiedMove(state_.mode,before[enemy],before[actor],target); // 跟随移动不再触发另一轮跟随。
    bool blocked=copying && target!=before[actor] && state_.positions[enemy]==before[enemy]; // 无实际位移不算遇到障碍。
    if(copying && target!=before[actor]) { ++state_.followReports[enemy].steps; if(blocked) ++state_.followReports[enemy].blocked; } // 仅向被带动或主动跟随的玩家累计复制步数与受阻通知。
    for(int observer=0;observer<2;++observer) { Mask next{}; // 为每位观察者分别枚举与自身感知一致的可能位置。
        for(int candidate=1;candidate<=size(state_.mode)*size(state_.mode);++candidate) { if(!state_.possible[observer][candidate-1]) continue; // 跳过此前已经排除的敌人位置。
            std::array<int,2> imagined=before; imagined[1-observer]=candidate; // 假想状态只替换观察者不知道的位置。
            for(int dest=1;dest<=size(state_.mode)*size(state_.mode);++dest) { // 对隐藏的单步移动枚举全部可能终点。
                if(actor==observer && dest!=target) continue; // 自己主动提交的终点是确知信息。
                if(!valid(state_.mode,dest) || distance(state_.mode,imagined[actor],dest)>1 || (exactStep && distance(state_.mode,imagined[actor],dest)!=1)) continue; // 冲刺路径中的一步必定移动，普通移动允许等待。
                auto after=imagined; after[actor]=dest; if(copying) after[enemy]=copiedMove(state_.mode,imagined[enemy],imagined[actor],dest); // 对每个假想状态模拟同样的跟随规则。
                bool imaginedBlocked=copying && dest!=imagined[actor] && after[enemy]==imagined[enemy]; // 跟随者知道自己是否收到受阻提示。
                if(after[observer]==state_.positions[observer] && !(observer==enemy && copying && imaginedBlocked!=blocked)) next[after[1-observer]-1]=true; // 同时匹配本人位置与私人障碍反馈。
            } // 结束隐藏位移枚举。
        } // 结束候选来源枚举。
        state_.possible[observer]=next; // 把可能终点集合保存给这个观察者。
    } // 两人的推理更新完成。
} // 结束普通移动与跟随结算。
bool Game::act(const Action& action) { // 完整行动先验证再执行，非法组合不会产生部分效果。
    if(!legal(action)) return false; // 非法计划不会产生任何局面变化。
    int actor=state_.turn,enemy=1-actor; bool copying=state_.following[enemy],dashed=false; Event event; event.actor=actor; // 记录本回合触发的跟随关系。
    if(copying) state_.followReports[enemy]={state_.turns+1,0,0}; // 新的跟随回合开启一份私人报告。
    for(const auto& step:action.steps) { event.action.steps.push_back(step); // 只保存实际执行的子动作，命中后的步骤不执行。
        if(step.kind==Kind::Move) move(actor,step.target,false,copying); // 可等待的普通移动。
        else if(step.kind==Kind::Lead) { if(!copying) state_.followReports[enemy]={state_.turns+1,0,0}; move(actor,step.target,false,true); } // 随影主动带动对方，已有跟随也只复制一次。
        else if(step.kind==Kind::Dash) { dashed=true; for(int cell:dashPath(state_.mode,state_.positions[actor],step.target)) move(actor,cell,true,copying); } // 冲刺路径逐步结算，随影同步复制每一步。
        else if(step.kind==Kind::Follow) state_.following[actor]=true; // 标记消耗本回合，下一敌方回合自动跟随。
        else if(step.kind==Kind::Attack) { // 攻击在当前子动作位置结算。
            event.hit=step.target==state_.positions[enemy]; // 对手若已跟随移动，以其移动后的实际位置为准。
            for(int cell=1;cell<=25;++cell) { state_.possible[enemy][cell-1]=state_.possible[enemy][cell-1] && canAttack(state_.mode,state_.roles[actor],cell,step.target); state_.possible[actor][cell-1]=state_.possible[actor][cell-1] && (event.hit?cell==step.target:cell!=step.target); } // 双方分别依据射程来源与命中信息缩小集合。
            if(event.hit) { state_.winner=actor; break; } // 命中立即终止本回合剩余计划与整局。
        } else { // 其余合法动作都是行列侦察。
            event.scanPositive=inRegion(state_.mode,state_.positions[enemy],step.kind,step.target); // 侦察获得这一时刻的结果，不受中心视线遮挡。
            state_.scanReports[actor]={state_.turns+1,step.target,step.kind,event.scanPositive}; // 保存仅侦察者可见的带时刻信息。
            for(int cell=1;cell<=25;++cell) state_.possible[actor][cell-1]=state_.possible[actor][cell-1] && (inRegion(state_.mode,cell,step.kind,step.target)==event.scanPositive); // 只更新侦察者的知识。
        } // 结束动作类型结算。
    } // 本回合有效子动作已全部执行或命中提前结束。
    state_.following[enemy]=false; state_.cooldown[actor]=dashed?2:std::max(0,state_.cooldown[actor]-1); // 跟随只持续这一敌方回合，冲刺后封锁两个自己的回合。
    ++state_.turns; if(state_.winner==-1 && state_.turns>=80) state_.winner=2; // 上限按完整回合统计，子动作不各占一回合。
    if(state_.winner==-1) state_.turn=enemy; // 继续中的比赛交给另一人。
    events_.push_back(event); snapshots_.push_back(state_); return true; // 保存完整回合和快照。
} // 结束裁判执行。
const Snapshot& Game::state() const { return state_; } // 完整状态只能在裁判或赛后使用。
const std::vector<Snapshot>& Game::snapshots() const { return snapshots_; } // 返回赛后快照的常量引用。
const std::vector<Event>& Game::events() const { return events_; } // 返回完整事件的常量引用。
bool Game::finished() const { return state_.winner!=-1; } // 非负赢家或未决状态都表示终局。
Action chooseAction(const View& view,int style,std::mt19937& rng) { // 电脑枚举有限的合法计划，并只根据受限候选位置评分。
    std::vector<Action> plans; const auto role=view.roles[view.player]; // 收集合法回合候选。
    auto add=[&](Action a) { if(validate(view,a)) plans.push_back(std::move(a)); }; // 在加入评分前执行与玩家相同的规则检查。
    std::vector<Action> prefixes{{}}; // 不移动直接攻击或侦察也是一个选择。
    for(int cell=1;cell<=size(view.mode)*size(view.mode);++cell) { // 为各职业生成合法普通移动或冲刺前缀。
        Action move{{{Kind::Move,cell}}}; if(validate(view,move)) { add(move); if(heroMode(view.mode) && (role==Role::Wolf || role==Role::Ink)) prefixes.push_back(move); } // 座狼与白墨允许移动后主行动。
        if(heroMode(view.mode) && role==Role::Shadow) add({{{Kind::Lead,cell}}}); // 随影可选择本回合移动并带动对方。
        if(heroMode(view.mode) && role==Role::Ink && view.cooldown[view.player]==0) { Action dash{{{Kind::Dash,cell}}}; if(validate(view,dash)) { add(dash); prefixes.push_back(dash); } } // 白墨冷却就绪时考虑冲刺调整位置。
    } // 完成第一段移动枚举。
    if(heroMode(view.mode) && role==Role::Wolf) { auto initial=prefixes; for(const auto& p:initial) if(p.steps.size()==1) for(int cell=1;cell<=size(view.mode)*size(view.mode);++cell) { auto a=p; a.steps.push_back({Kind::Move,cell}); if(validate(view,a)) { add(a); prefixes.push_back(a); } } } // 座狼考虑先走两步再攻击。
    for(const auto& p:prefixes) for(int cell=1;cell<=size(view.mode)*size(view.mode);++cell) if(view.possible[cell-1]) { auto a=p; a.steps.push_back({Kind::Attack,cell}); add(a); } // 只对可能的敌方位置尝试攻击。
    for(int region=0;region<size(view.mode);++region) { add({{{Kind::Row,region}}}); add({{{Kind::Col,region}}}); } // 所有职业都可以无限次选择侦察。
    if(heroMode(view.mode) && role==Role::Shadow) add({{{Kind::Follow,1-view.player}}}); // 随影偶尔利用跟随获取位移信息。
    int count=static_cast<int>(std::count(view.possible.begin(),view.possible.end(),true)); double best=-1e9; Action choice; // 准备比较计划得分。
    for(const auto& a:plans) { int own=view.own; validate(view,a,false,nullptr,&own); double score=std::uniform_real_distribution<double>(0,2)(rng); bool shot=false; // 少量随机分数打破平局。
        for(const auto& step:a.steps) { // 按动作给予进攻、信息或职业偏好的分数。
            if(step.kind==Kind::Attack) { shot=true; score+=count==1?100:style==0?20:style==1?8:12; } // 确定命中时所有风格都优先进攻。
            if(step.kind==Kind::Row || step.kind==Kind::Col) { int inside=0; for(int cell=1;cell<=25;++cell) inside+=view.possible[cell-1] && inRegion(view.mode,cell,step.kind,step.target); score+=std::min(inside,count-inside)*(style==2?4:1)-3; } // 不把候选集合当概率，仅衡量最坏分区缩小量。
            if(step.kind==Kind::Dash) score-=1.5; // 避免随意耗掉冲刺。
            if(step.kind==Kind::Follow) score+=style==1?7:2; // 给隐蔽风格跟随偏好。
        } // 完成行动类型评分。
        int nearest=20; for(int cell=1;cell<=25;++cell) if(view.possible[cell-1]) nearest=std::min(nearest,distance(view.mode,own,cell)); // 仅用候选位置评估移动后的接近程度。
        if(!shot) score+=(style==1?nearest:-nearest)*0.8; // 无攻击时考虑移动接近或避开对手。
        if(score>best) { best=score; choice=a; } // 选择最高分计划。
    } // 合法移动至少含原地等待，因此候选一定非空。
    return choice; // 返回完整且合法的回合序列。
} // 结束受限电脑策略。
} // 结束规则命名空间。
