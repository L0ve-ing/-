#include "game.hpp" // 引入支持自定义地图的游戏规则声明。
#include <algorithm> // 使用计数、最小值和最大值算法。
#include <cmath> // 使用绝对值进行距离与线段计算。
#include <functional> // 使用递归函数枚举重骑的一至四步冲锋路线。
#include <stdexcept> // 使用异常拒绝非法开局参数。
namespace mist { // 所有实现属于游戏规则命名空间。
int size(Mode mode) { return static_cast<int>(mode); } // 枚举值与棋盘边长一致。
bool heroMode(Mode mode) { return mode==Mode::Heroes4 || mode==Mode::Heroes; } // 两种职业地图共享角色技能与回合预算。
bool validMode(int mode) { return mode==3 || mode==4 || mode==5; } // 只允许三格侦察与四格、五格职业模式。
bool validRole(int role) { return role>=0 && role<5; } // 五种职业的编号范围。
Map defaultMap(Mode mode) { Map map; if(mode==Mode::Heroes) for(int cell:{1,5,13,21,25}) map.cells[cell-1]=Terrain::Wall; return map; } // 默认五格地图沿用四角和中心方墙，其余默认地图全为地面。
bool validMap(Mode mode,const Map& map) { // 自定义地形仅在五格职业地图生效。
    if(!validMode(size(mode))) return false; // 防止非法边长使坐标越界。
    int standable=0; for(int i=0;i<25;++i) { int kind=static_cast<int>(map.cells[i]); if(kind<0 || kind>3 || (mode!=Mode::Heroes && kind!=0)) return false; if(i<size(mode)*size(mode) && (kind==0 || kind==3)) ++standable; } // 检查四种地形并统计地面与漩涡站位。
    if(mode!=Mode::Heroes) { for(bool wall:map.horizontal) if(wall) return false; for(bool wall:map.vertical) if(wall) return false; } // 三格侦察及四格职业版维持无障碍地图。
    return standable>0; // 同格站立合法，所以至少一个地面或漩涡即可开局。
} // 结束地图结构检查。
std::string encodeMap(const Map& map) { std::string out; out.reserve(85); for(auto cell:map.cells) out.push_back(static_cast<char>('0'+static_cast<int>(cell))); for(bool wall:map.horizontal) out.push_back(wall?'1':'0'); for(bool wall:map.vertical) out.push_back(wall?'1':'0'); return out; } // 固定顺序编码地形、水平墙和竖直墙，适用于保存与网络传输。
bool decodeMap(Mode mode,const std::string& text,Map& map) { // 先写临时地图，全部成功后才替换调用者数据。
    if(text.size()!=85) return false; // 固定长度避免缺失字段或额外数据。
    Map decoded; // 临时地图接收通过格式检查的字段。
    for(int i=0;i<25;++i) { if(text[i]<'0' || text[i]>'3') return false; decoded.cells[i]=static_cast<Terrain>(text[i]-'0'); } // 地形允许地面、方墙、湖泊和漩涡。
    for(int i=0;i<30;++i) { if((text[i+25]!='0' && text[i+25]!='1') || (text[i+55]!='0' && text[i+55]!='1')) return false; decoded.horizontal[i]=text[i+25]=='1'; decoded.vertical[i]=text[i+55]=='1'; } // 边墙只允许关闭和开启。
    if(!validMap(mode,decoded)) return false; // 无站位或模式不兼容时保留原数据。
    map=decoded; return true; // 地图有效且可站人时原子替换。
} // 结束地图解码。
bool valid(Mode mode,int cell,const Map& map) { return validMode(size(mode)) && cell>=1 && cell<=size(mode)*size(mode) && (map.cells[cell-1]==Terrain::Ground || map.cells[cell-1]==Terrain::Whirlpool); } // 地面与漩涡都可站立，方墙和湖泊不可站立。
bool valid(Mode mode,int cell) { return valid(mode,cell,defaultMap(mode)); } // 兼容旧调用，使用该模式的默认地图。
int distance(Mode mode,int a,int b) { int n=size(mode); return std::abs((a-1)/n-(b-1)/n)+std::abs((a-1)%n-(b-1)%n); } // 曼哈顿距离为行列差绝对值之和。
bool canMove(Mode mode,int from,int to,const Map& map) { // 普通移动与每一步冲刺使用同一个地形碰撞规则。
    if(!valid(mode,from,map) || !valid(mode,to,map) || distance(mode,from,to)>1) return false; // 移动只能等待或到相邻地面。
    if(from==to) return true; // 原地等待不跨过边墙。
    int n=size(mode),r=(from-1)/n,c=(from-1)%n,tr=(to-1)/n,tc=(to-1)%n; // 计算移动前后所在行列。
    return r==tr?!map.vertical[r*6+std::max(c,tc)]:!map.horizontal[std::max(r,tr)*5+c]; // 跨列检查竖直边墙，跨行检查水平边墙。
} // 结束单步移动检测。
bool segmentBox(double sx,double sy,double ex,double ey,double left,double top,double right,double bottom) { // 线段与封闭矩形相交，零宽或零高矩形表示一段线形墙。
    double start[2]={sx,sy},end[2]={ex,ey},lower[2]={left,top},upper[2]={right,bottom},lo=0,hi=1; // 交点参数初始范围为整条线段。
    for(int axis=0;axis<2;++axis) { double delta=end[axis]-start[axis]; // 逐轴缩小交点的参数范围。
        if(delta==0) { if(start[axis]<lower[axis] || start[axis]>upper[axis]) return false; } // 平行且在障碍范围外则没有交点。
        else { double x=(lower[axis]-start[axis])/delta,y=(upper[axis]-start[axis])/delta; if(x>y) std::swap(x,y); lo=std::max(lo,x); hi=std::min(hi,y); if(lo>hi+1e-12) return false; } // 包含边界和端点，因此擦边或碰角也被遮挡。
    } // 两轴仍有公共参数说明相交。
    return true; // 命中墙体的内部、边缘或端点。
} // 结束通用几何相交计算。
bool clearShot(Mode mode,int a,int b,const Map& map) { // 从起点格中心连到目标格中心，检查全部墙体。
    int n=size(mode); if(!valid(mode,a,map) || b<1 || b>n*n || map.cells[b-1]==Terrain::Wall) return false; // 湖泊可以成为攻击目标，方墙不能。
    double sx=(a-1)%n,sy=(a-1)/n,ex=(b-1)%n,ey=(b-1)/n; // 格子中心采用零起点横纵坐标。
    for(int cell=1;cell<=n*n;++cell) if(map.cells[cell-1]==Terrain::Wall) { double x=(cell-1)%n,y=(cell-1)/n; if(segmentBox(sx,sy,ex,ey,x-0.5,y-0.5,x+0.5,y+0.5)) return false; } // 只检查方墙，湖泊不会遮挡视线。
    for(int i=0;i<30;++i) { if(map.horizontal[i]) { double x=i%5,y=i/5-0.5; if(segmentBox(sx,sy,ex,ey,x-0.5,y,x+0.5,y)) return false; } if(map.vertical[i]) { double x=i%6-0.5,y=i/6; if(segmentBox(sx,sy,ex,ey,x,y-0.5,x,y+0.5)) return false; } } // 每条边墙按一格长封闭线段检测。
    return true; // 起点和目标之间没有任何不透明障碍。
} // 结束攻击遮挡检查。
bool clearShot(Mode mode,int a,int b) { return clearShot(mode,a,b,defaultMap(mode)); } // 兼容默认地图的攻击检查。
int range(Mode mode,Role role) { return mode==Mode::Scout?1:role==Role::Sniper?3:role==Role::Shadow?2:1; } // 按模式和职业决定射程。
bool canAttack(Mode mode,Role role,int from,int target,const Map& map) { return !(heroMode(mode) && role==Role::Heavy) && clearShot(mode,from,target,map) && distance(mode,from,target)<=range(mode,role); } // 重骑没有普通攻击，其余职业须同时满足视线与曼哈顿射程。
bool canAttack(Mode mode,Role role,int from,int target) { return canAttack(mode,role,from,target,defaultMap(mode)); } // 兼容默认地图的射程检查。
bool inRegion(Mode mode,int cell,Kind kind,int target,const Map& map) { int n=size(mode); return valid(mode,cell,map) && target>=0 && target<n && (kind==Kind::Row?(cell-1)/n==target:kind==Kind::Col && (cell-1)%n==target); } // 仅合法站位能成为侦察信息中的敌方位置。
bool inRegion(Mode mode,int cell,Kind kind,int target) { return inRegion(mode,cell,kind,target,defaultMap(mode)); } // 兼容旧行列判断接口。
std::vector<int> dashPath(Mode mode,int from,int target,const Map& map) { // 枚举距离恰好为二的两步路径，空向量表示目标不合法或无法到达。
    if(!valid(mode,from,map) || !valid(mode,target,map) || distance(mode,from,target)!=2) return {}; // 白墨冲刺必须恰好距离二，不能一步或原地使用。
    int n=size(mode); for(int delta:{-n,n,-1,1}) { int mid=from+delta; if(canMove(mode,from,mid,map) && canMove(mode,mid,target,map)) return {mid,target}; } // 找到逐步不穿地形或边墙的最短路径，竖直方向优先。
    return {}; // 被禁入格阻挡且两步内无路时拒绝该终点。
} // 结束冲刺路径搜索。
std::vector<int> dashPath(Mode mode,int from,int target) { return dashPath(mode,from,target,defaultMap(mode)); } // 兼容默认地图的冲刺路径接口。
int copiedMove(Mode mode,int follower,int from,int to,const Map& map) { // 复制一段实际移动向量，不根据敌方终点瞬移。
    int n=size(mode),row=(follower-1)/n+(to-1)/n-(from-1)/n,col=(follower-1)%n+(to-1)%n-(from-1)%n; // 将敌人的行列位移加到自己的位置。
    if(row<0 || row>=n || col<0 || col>=n || !canMove(mode,follower,row*n+col+1,map)) return follower; // 越界、方墙、湖泊或线形墙挡路时跳过这一步。
    return row*n+col+1; // 同格允许，因此不检查是否与敌人重叠。
} // 结束跟随位移。
int copiedMove(Mode mode,int follower,int from,int to) { return copiedMove(mode,follower,from,to,defaultMap(mode)); } // 兼容默认地图的跟随接口。
std::wstring roleName(Role role) { return role==Role::Sniper?L"狙击手":role==Role::Wolf?L"座狼":role==Role::Ink?L"白墨":role==Role::Shadow?L"随影":L"重骑"; } // 五种职业名称。
std::wstring roleHelp(Role role) { // 用简短文本解释职业的回合组合。
    if(role==Role::Sniper) return L"射程3；移动或攻击选一项。"; // 职业对战移除侦察，狙击手保持单一主行动。
    if(role==Role::Wolf) return L"射程1；可移动合计2步，在攻击前后自由分配。"; // 座狼的移动预算属于整个回合。
    if(role==Role::Ink) return L"射程1；可先移动1步再攻击；行动前后冲刺恰好2步，随后两个自己的回合不可冲刺。"; // 白墨免费移动和冲刺分别计数。
    if(role==Role::Shadow) return L"射程2；可标记后在敌方回合跟随，也可移动时带动对方；受阻则跳过并通知本人。"; // 两种方向的复制都不额外消耗被带动者回合。
    return L"护甲1点，无普通攻击。每回合移动最多2步或冲锋1–4步；第1步后可转向一次，不可后退。第2步起伤敌，路线保密。"; // 重骑选择时完整展示冲锋方向与伤害规则。
} // 结束职业说明。
bool validate(const View& v,const Action& a,bool prefix,std::wstring* why,int* preview) { // 只用本人状态校验，不接触隐藏敌人位置。
    auto reject=[&](const wchar_t* message) { if(why) *why=message; return false; }; // 用局部匿名函数统一输出错误。
    if(v.player<0 || v.player>1 || !validMap(v.mode,v.map) || !valid(v.mode,v.own,v.map) || !validRole(static_cast<int>(v.roles[v.player])) || v.winner!=-1 || v.turn!=v.player) return reject(L"现在不是你的有效行动回合。"); // 短路检查避免非法地图和玩家编号访问数组。
    if(a.steps.size()>4 || (!prefix && a.steps.empty())) return reject(L"请至少安排一个行动；最多四个子动作。"); // 冲锋与原有组合行动都不会超过四个步骤。
    Role role=v.roles[v.player]; int own=v.own,moves=0,main=0,dashes=0; bool afterMain=false; // 记录预算及主行动顺序。
    if(heroMode(v.mode) && role==Role::Heavy) { // 重骑的移动与冲锋是彼此独立的完整回合。
        if(a.steps.empty()) { if(preview) *preview=own; if(why) why->clear(); return true; } // 空前缀可供界面开始编辑，完整行动已在上方拒绝。
        if(a.steps.front().kind==Kind::Move) { // 普通移动计划允许一步或两步，并可用原地等待消耗一步。
            if(a.steps.size()>2) return reject(L"重骑每回合最多普通移动两步。"); // 移速二不允许出现第三个移动步骤。
            for(const auto& step:a.steps) { if(step.kind!=Kind::Move) return reject(L"重骑的普通移动回合不能混入攻击或冲锋。"); if(!canMove(v.mode,own,step.target,v.map)) return reject(L"移动只能原地或上下左右一步，不能进入方墙、湖泊或跨越线形墙。"); own=step.target; } // 逐步检查地形、边墙和预览位置。
        } else if(a.steps.front().kind==Kind::Charge) { // 冲锋路径由一至四个对手不可见的相邻目标格组成。
            int currentDr=0,currentDc=0,n=size(v.mode); // 保存第一步确定或第二步重新调整后的方向。
            for(std::size_t i=0;i<a.steps.size();++i) { const auto& step=a.steps[i]; // 逐格检查冲锋，避免终点合法但中途穿墙。
                if(step.kind!=Kind::Charge) return reject(L"重骑的冲锋回合不能混入普通移动或其他行动。"); // 冲锋占用整个回合。
                if(!canMove(v.mode,own,step.target,v.map) || distance(v.mode,own,step.target)!=1) return reject(L"冲锋每一步必须进入相邻可站立格，不能等待或穿越障碍。"); // 冲锋每个步骤必须产生一格位移。
                int dr=(step.target-1)/n-(own-1)/n,dc=(step.target-1)%n-(own-1)%n; // 计算本步单位方向向量。
                if(i==0) currentDr=dr,currentDc=dc; // 第一步确定初始直线方向。
                else if(dr!=currentDr || dc!=currentDc) { if(i!=1) return reject(L"冲锋只能在走完第一格后立即调整一次方向。"); if(dr==-currentDr && dc==-currentDc) return reject(L"冲锋不能向后折返。"); currentDr=dr; currentDc=dc; } // 第二步可改向一次，第三和第四步必须保持该方向。
                own=step.target; // 后续步骤从当前冲锋位置继续。
            } // 完成整条秘密冲锋路径的几何检查。
        } else return reject(L"重骑只能选择普通移动或冲锋，不能进行普通攻击。"); // 重骑依靠冲锋造成伤害。
        if(preview) *preview=own; // 向界面返回重骑计划的最终预览格。
        if(why) why->clear(); // 成功时清除调用方保存的旧错误。
        return true; // 接受完整计划或合法前缀。
    } // 结束重骑专属回合校验。
    if((v.mode==Mode::Scout || role==Role::Sniper || role==Role::Shadow) && a.steps.size()>1) return reject(L"本职业每回合选择一个主行动。"); // 基础模式与这两个职业不能移动并攻击。
    for(std::size_t i=0;i<a.steps.size();++i) { const auto step=a.steps[i]; // 按玩家安排的顺序检查每个子动作。
        if(step.kind==Kind::Move || step.kind==Kind::Lead) { // 普通移动允许选自身格等待，随影可选择带动对方。
            if(step.kind==Kind::Lead && (!heroMode(v.mode) || role!=Role::Shadow)) return reject(L"只有随影可以移动并带动对方。"); // 带动能力属于随影专属操作。
            if(!canMove(v.mode,own,step.target,v.map)) return reject(L"移动只能原地或上下左右一步，不能进入方墙、湖泊或跨越线形墙。"); // 避免斜移、跨行和全部移动障碍。
            if(++moves>(heroMode(v.mode) && role==Role::Wolf?2:1)) return reject(L"本回合普通移动次数已用完。"); // 座狼两次，其余一次。
            if(heroMode(v.mode) && role==Role::Ink && afterMain) return reject(L"白墨普通移动只能安排在攻击之前。"); // 攻击后只能用额外冲刺。
            own=step.target; // 更新后续攻击使用的预览位置。
        } else if(step.kind==Kind::Dash) { // 白墨额外技能不占主行动。
            if(!heroMode(v.mode) || role!=Role::Ink || v.cooldown[v.player]>0 || ++dashes>1) return reject(L"只有冷却完成的白墨可以冲刺一次。"); // 职业、次数与冷却同时检查。
            if(i!=0 && i+1!=a.steps.size()) return reject(L"冲刺只能放在本回合行动的最前或最后。"); // 禁止把冲刺夹在普通移动与攻击中间。
            if(dashPath(v.mode,own,step.target,v.map).empty()) return reject(L"冲刺必须沿合法路径移动恰好两步，不能跨越墙体或经过湖泊。"); // 冲刺采用逐步可达路径而非穿墙瞬移。
            own=step.target; // 更新冲刺后的攻击或结束位置。
        } else { // 其余类型都消耗唯一主行动。
            if(++main>1) return reject(L"每回合只能使用一次攻击或指定跟随；侦察仅在三格版提供。"); // 主行动不允许重复使用。
            afterMain=true; // 标记主行动已经安排。
            if(step.kind==Kind::Attack) { if(!canAttack(v.mode,role,own,step.target,v.map)) return reject(L"目标为方墙、超出射程，或攻击连线碰到墙体。"); } // 使用子动作当时的位置计算攻击，湖泊不挡攻击。
            else if(step.kind==Kind::Row || step.kind==Kind::Col) { if(heroMode(v.mode)) return reject(L"职业对战已移除侦察技能。"); if(step.target<0 || step.target>=size(v.mode)) return reject(L"侦察行列超出地图。"); } // 三格版保留无限侦察，职业版一律禁止。
            else if(step.kind==Kind::Follow) { if(!heroMode(v.mode) || role!=Role::Shadow || step.target!=1-v.player) return reject(L"随影只能指定另一名玩家跟随。"); } // 两人各一个单位，标记对象明确。
            else return reject(L"未知行动类型。"); // 冲锋只属于前面已经单独校验的重骑，其他非法枚举也被拒绝。
        } // 结束单个子动作分类。
    } // 全部子动作预算与位置已检查。
    if(preview) *preview=own; // 可选返回值供棋盘显示最终预览位置。
    if(why) why->clear(); // 成功时清除旧错误。
    return true; // 合法等待、仅移动或仅冲刺也会消耗一个完整回合。
} // 结束行动校验。
Mask hiddenChargeDestinations(Mode mode,const Map& map,const Mask& starts) { // 合并所有合法秘密冲锋的可能终点，防止候选集合泄露真实路线或步数。
    Mask result{}; int n=size(mode); // 结果只记录一至四步后可到达的格子。
    for(int start=1;start<=n*n;++start) if(starts[start-1]) { // 从观察者此前保留的每个敌方候选位置开始枚举。
        std::function<void(int,int,int,int)> grow=[&](int position,int currentDr,int currentDc,int depth) { // 递归状态保存位置、当前方向和已走步数。
            if(depth==4) return; // 冲锋最多前进四格。
            for(int target=1;target<=n*n;++target) { // 枚举下一步的所有地图格并筛选相邻可达位置。
                if(!canMove(mode,position,target,map) || distance(mode,position,target)!=1) continue; // 每一步必须实际移动且不能穿越地形或边墙。
                int dr=(target-1)/n-(position-1)/n,dc=(target-1)%n-(position-1)%n; // 计算候选步骤的单位方向。
                if(depth>0 && (dr!=currentDr || dc!=currentDc) && (depth!=1 || (dr==-currentDr && dc==-currentDc))) continue; // 仅第二步可以改向且不能折返，后续必须直行。
                result[target-1]=true; grow(target,dr,dc,depth+1); // 任意一至四步路径终点都可能成为本回合最终位置。
            } // 结束下一冲锋格枚举。
        }; // 结束单一起点的递归函数定义。
        grow(start,0,0,0); // 第一步不受既有方向约束。
    } // 结束全部隐藏起点枚举。
    return result; // 返回只由旧候选、公开地图与职业规则决定的保守集合。
} // 结束秘密冲锋候选合并。
Event publicEvent(const Event& event) { // 构造可在对局中广播且不会泄露秘密路线的事件。
    Event out=event; out.scanPositive=false; out.action.steps.clear(); bool chargeAdded=false; // 私人侦察结果和完整动作序列不能直接离开裁判。
    for(auto step:event.action.steps) { if(step.kind==Kind::Charge) { if(chargeAdded) continue; chargeAdded=true; step.target=0; } else if(step.kind==Kind::Move || step.kind==Kind::Dash || step.kind==Kind::Lead) step.target=0; out.action.steps.push_back(step); } // 整段冲锋压缩为一次发动提示，其他秘密位移仅清除终点。
    return out; // 伤害、护甲破损与致命结果继续作为公开信息。
} // 结束事件脱敏。
bool samePublic(const Event& a,const Event& b) { auto x=publicEvent(a),y=publicEvent(b); if(x.actor!=y.actor || x.hit!=y.hit || x.damaged!=y.damaged || x.armorBlocked!=y.armorBlocked || x.action.steps.size()!=y.action.steps.size()) return false; for(std::size_t i=0;i<x.action.steps.size();++i) if(x.action.steps[i].kind!=y.action.steps[i].kind || x.action.steps[i].target!=y.action.steps[i].target) return false; return true; } // 逐字段比较公开路径、伤害和护甲结果，不比较秘密字段。
std::wstring actionText(const Event& e,bool reveal) { // 组合回合日志，进行中不泄露移动坐标。
    std::wstring out=L"玩家"+std::to_wstring(e.actor+1)+L"："; bool charged=false; // 显示编号从一开始，并记录是否需要追加冲锋结果。
    for(std::size_t i=0;i<e.action.steps.size();++i) { const auto& step=e.action.steps[i]; if(step.kind==Kind::Charge && !reveal && i>0) continue; if(out.back()!=L'：') out+=L" → "; // 公开日志把整段秘密冲锋合并为一次发动提示。
        if(step.kind==Kind::Move || step.kind==Kind::Dash) out+=(step.kind==Kind::Move?L"移动/等待":L"冲刺")+(reveal?L"到"+std::to_wstring(step.target):std::wstring()); // 普通移动不说明是否实际离开原格。
        else if(step.kind==Kind::Lead) out+=L"移动并带动对方"+(reveal?L"到"+std::to_wstring(step.target):std::wstring()); // 只公开带动行为，不公开移动方向或终点。
        else if(step.kind==Kind::Attack) out+=L"攻击"+std::to_wstring(step.target)+(e.hit?L"致命命中":e.armorBlocked?L"命中，护甲破损":L"未中"); // 攻击目标公开，护甲吸收与致命结果也公开。
        else if(step.kind==Kind::Charge) { charged=true; out+=reveal?L"冲锋到"+std::to_wstring(step.target):L"发动冲锋"; } // 对局中隐藏整条冲锋路线，复盘才逐格展示实际路径。
        else if(step.kind==Kind::Follow) out+=L"指定跟随"; // 标记会在敌方下一回合生效。
        else out+=L"侦察第"+std::to_wstring(step.target+1)+(step.kind==Kind::Row?L"行":L"列")+(reveal?(e.scanPositive?L"有目标":L"无目标"):L""); // 侦察结果仅赛后公开。
    } // 结束序列格式化。
    if(charged && e.damaged) out+=e.hit?(e.armorBlocked?L"；途中击破护甲后致命命中":L"；途中致命命中"):L"；途中命中，护甲破损"; // 伤害附在整段冲锋之后，避免误称为最后一步发生。
    return out; // 返回本回合的可读记录。
} // 结束日志文字。
int Game::whirlpoolDestination(int position) const { // 计算一次轮首吸引的唯一终点，不产生连锁吸引。
    if(!valid(state_.mode,position,state_.map)) return position; // 输入不是可站立格时保持原位。
    for(int cell=1;cell<=size(state_.mode)*size(state_.mode);++cell) if(state_.map.cells[cell-1]==Terrain::Whirlpool && distance(state_.mode,position,cell)==1 && canMove(state_.mode,position,cell,state_.map)) return cell; // 按格号从小到大选择相邻且没有边墙阻挡的漩涡。
    return position; // 周边没有可达漩涡时不移动。
} // 结束单个位置的漩涡吸引计算。
void Game::applyRoundWhirlpools(int reportTurn) { // 开局和每轮开始时同时吸引双方，并保持隐藏位置推理一致。
    const auto before=state_.positions; std::array<int,2> after{whirlpoolDestination(before[0]),whirlpoolDestination(before[1])}; // 两人的终点都依据同一轮开始前的局面计算。
    state_.positions=after; // 同格站立合法，因此双方可同时被吸入同一漩涡。
    for(int player=0;player<2;++player) if(after[player]!=before[player]) state_.whirlpoolReports[player]={reportTurn,before[player],after[player]}; // 只有实际被吸引的玩家收到自己的私人位移报告。
    for(int observer=0;observer<2;++observer) { Mask next{}; for(int candidate=1;candidate<=size(state_.mode)*size(state_.mode);++candidate) if(state_.possible[observer][candidate-1]) next[whirlpoolDestination(candidate)-1]=true; state_.possible[observer]=next; } // 每位观察者把未知敌人的所有候选位置按公开地形规则同步推进。
} // 结束每轮同时漩涡结算。
Game::Game(int p0,int p1,Mode mode,std::array<Role,2> roles,int first,std::optional<Map> map) { // 构造函数统一验证地图、职业和先手。
    state_.map=map.value_or(defaultMap(mode)); // 未传入自定义地图时沿用各模式默认地图。
    if(!validMap(mode,state_.map) || !valid(mode,p0,state_.map) || !valid(mode,p1,state_.map) || !validRole(static_cast<int>(roles[0])) || !validRole(static_cast<int>(roles[1])) || first<0 || first>1) throw std::invalid_argument("Invalid setup"); // 拒绝无站位地图、不可站立起点和非法输入。
    state_.mode=mode; state_.positions={p0,p1}; state_.roles=roles; state_.turn=first; // 保存初始完整局面，同格允许。
    for(int player=0;player<2;++player) state_.armor[player]=heroMode(mode) && roles[player]==Role::Heavy?1:0; // 只有职业模式中的重骑开局拥有一点公开护甲。
    for(auto& mask:state_.possible) for(int cell=1;cell<=25;++cell) mask[cell-1]=valid(mode,cell,state_.map); // 初始候选包含实际地图中的地面与漩涡格。
    applyRoundWhirlpools(1); snapshots_.push_back(state_); // 开局视作第一轮开始，先统一结算漩涡再保存初始复盘快照。
} // 结束构造。
View Game::view(int player) const { // 返回自己的位置、公开状态和仅属于本人的私人报告。
    if(player<0 || player>1) throw std::invalid_argument("Invalid player"); // 防止非法编号访问双人数组。
    View out; out.mode=state_.mode; out.own=state_.positions[player]; out.player=player; out.turn=state_.turn; out.winner=state_.winner; out.turns=state_.turns; // 复制基础回合信息与本人真实位置。
    out.roles=state_.roles; out.armor=state_.armor; out.cooldown=state_.cooldown; out.following=state_.following; out.possible=state_.possible[player]; // 复制双方公开状态和本人的敌方候选集合。
    out.followReport=state_.followReports[player]; out.scanReport=state_.scanReports[player]; out.whirlpoolReport=state_.whirlpoolReports[player]; out.map=state_.map; return out; // 私人技能报告不会进入另一名玩家的视图。
} // 结束受限玩家视图构造。
bool Game::legal(const Action& action) const { return validate(view(state_.turn),action); } // 规则合法性不使用对手真位置。
void Game::move(int actor,int target,bool exactStep,bool copying,bool publicTarget) { // 单次移动同时更新自动跟随和双方知识。
    const auto before=state_.positions; const int enemy=1-actor; state_.positions[actor]=target; // 先保存两个真实起点，再执行主动移动。
    if(copying) state_.positions[enemy]=copiedMove(state_.mode,before[enemy],before[actor],target,state_.map); // 跟随移动遵守实际地图，不再触发另一轮跟随。
    bool blocked=copying && target!=before[actor] && state_.positions[enemy]==before[enemy]; // 无实际位移不算遇到障碍。
    if(copying && target!=before[actor]) { ++state_.followReports[enemy].steps; if(blocked) ++state_.followReports[enemy].blocked; } // 仅向被带动或主动跟随的玩家累计复制步数与受阻通知。
    for(int observer=0;observer<2;++observer) { Mask next{}; // 为每位观察者分别枚举与自身感知一致的可能位置。
        for(int candidate=1;candidate<=size(state_.mode)*size(state_.mode);++candidate) { if(!state_.possible[observer][candidate-1]) continue; // 跳过此前已经排除的敌人位置。
            std::array<int,2> imagined=before; imagined[1-observer]=candidate; // 假想状态只替换观察者不知道的位置。
            for(int dest=1;dest<=size(state_.mode)*size(state_.mode);++dest) { // 对隐藏的单步移动枚举全部可能终点。
                if((actor==observer || publicTarget) && dest!=target) continue; // 本人提交的终点确知，确实公开的位移也可供对手确定。
                if(!canMove(state_.mode,imagined[actor],dest,state_.map) || (exactStep && distance(state_.mode,imagined[actor],dest)!=1)) continue; // 所有候选移动都要避开地形与边墙，冲刺中的一步必定移动。
                auto after=imagined; after[actor]=dest; if(copying) after[enemy]=copiedMove(state_.mode,imagined[enemy],imagined[actor],dest,state_.map); // 对每个假想状态模拟相同地图上的跟随规则。
                bool imaginedBlocked=copying && dest!=imagined[actor] && after[enemy]==imagined[enemy]; // 跟随者知道自己是否收到受阻提示。
                if(after[observer]==state_.positions[observer] && !(observer==enemy && copying && imaginedBlocked!=blocked)) next[after[1-observer]-1]=true; // 同时匹配本人位置与私人障碍反馈。
            } // 结束隐藏位移枚举。
        } // 结束候选来源枚举。
        state_.possible[observer]=next; // 把可能终点集合保存给这个观察者。
    } // 两人的推理更新完成。
} // 结束普通移动与跟随结算。
bool Game::act(const Action& action) { // 完整行动先验证再执行，非法组合不会产生部分效果。
    if(!legal(action)) return false; // 非法计划不会产生任何局面变化。
    int actor=state_.turn,enemy=1-actor; bool copying=state_.following[enemy],dashed=false,charged=action.steps.front().kind==Kind::Charge; Mask chargePrior=state_.possible[enemy]; Event event; event.actor=actor; // 记录跟随关系，并在秘密冲锋前保存对手原有的攻击者候选集合。
    if(copying) state_.followReports[enemy]={state_.turns+1,0,0}; // 新的跟随回合开启一份私人报告。
    for(std::size_t index=0;index<action.steps.size();++index) { const auto& step=action.steps[index]; event.action.steps.push_back(step); // 只保存实际执行的子动作，致命命中后的冲锋余段不执行。
        if(step.kind==Kind::Move) move(actor,step.target,false,copying); // 可等待的普通移动。
        else if(step.kind==Kind::Lead) { if(!copying) state_.followReports[enemy]={state_.turns+1,0,0}; move(actor,step.target,false,true); } // 随影主动带动对方，已有跟随也只复制一次。
        else if(step.kind==Kind::Dash) { dashed=true; for(int cell:dashPath(state_.mode,state_.positions[actor],step.target,state_.map)) move(actor,cell,true,copying); } // 冲刺路径逐步绕开墙湖结算，随影同步复制每一步。
        else if(step.kind==Kind::Charge) { // 重骑冲锋路线保密，但经过的第二至第四格可以造成伤害。
            move(actor,step.target,true,copying); // 每个冲锋步骤都按隐藏的一格移动更新位置与候选集合。
            if(index>0) { // 第一步只用于起势，从第二个实际执行步骤开始检查接触。
                bool contact=state_.positions[actor]==state_.positions[enemy]; // 跟随结算后双方同格才构成本步接触。
                for(int observer=0;observer<2;++observer) for(int cell=1;cell<=25;++cell) if(state_.possible[observer][cell-1]) { int forbidden=observer==actor?state_.positions[actor]:state_.positions[observer]; state_.possible[observer][cell-1]=contact?cell==state_.positions[actor]:cell!=forbidden; } // 双方依据公开接触结果收缩候选，同时保留隐藏路线的其他可能。
                if(contact) { event.damaged=true; if(state_.armor[enemy]>0) { --state_.armor[enemy]; event.armorBlocked=true; } else { event.hit=true; state_.winner=actor; break; } } // 护甲吸收本次伤害后继续已选路线，无护甲的接触才立即停止冲锋并结束对局。
            } // 结束可造成伤害的冲锋步骤处理。
        } // 结束秘密冲锋结算。
        else if(step.kind==Kind::Follow) state_.following[actor]=true; // 标记消耗本回合，下一敌方回合自动跟随。
        else if(step.kind==Kind::Attack) { // 攻击在当前子动作位置结算。
            event.damaged=step.target==state_.positions[enemy]; // 对手若已跟随移动，以其移动后的实际位置为准。
            for(int cell=1;cell<=25;++cell) { state_.possible[enemy][cell-1]=state_.possible[enemy][cell-1] && canAttack(state_.mode,state_.roles[actor],cell,step.target,state_.map); state_.possible[actor][cell-1]=state_.possible[actor][cell-1] && (event.damaged?cell==step.target:cell!=step.target); } // 双方分别依据当前地形、射程来源与接触信息缩小集合。
            if(event.damaged) { if(state_.armor[enemy]>0) { --state_.armor[enemy]; event.armorBlocked=true; } else { event.hit=true; state_.winner=actor; break; } } // 重骑护甲承受第一次攻击，护甲耗尽后的命中才立即结束对局。
        } else { // 其余合法动作都是行列侦察。
            event.scanPositive=inRegion(state_.mode,state_.positions[enemy],step.kind,step.target,state_.map); // 三格侦察获得这一时刻的结果。
            state_.scanReports[actor]={state_.turns+1,step.target,step.kind,event.scanPositive}; // 保存仅侦察者可见的带时刻信息。
            for(int cell=1;cell<=25;++cell) state_.possible[actor][cell-1]=state_.possible[actor][cell-1] && (inRegion(state_.mode,cell,step.kind,step.target,state_.map)==event.scanPositive); // 只更新侦察者的知识。
        } // 结束动作类型结算。
    } // 本回合有效子动作已全部执行或命中提前结束。
    if(charged) state_.possible[enemy]=hiddenChargeDestinations(state_.mode,state_.map,chargePrior); // 对手只得到所有一至四步合法终点的并集，候选集合不会泄露实际路线、长度或终点。
    state_.following[enemy]=false; state_.cooldown[actor]=dashed?2:std::max(0,state_.cooldown[actor]-1); // 跟随只持续这一敌方回合，冲刺后封锁两个自己的回合。
    ++state_.turns; if(state_.winner==-1 && state_.turns>=80) state_.winner=2; // 上限按完整回合统计，子动作不各占一回合。
    if(state_.winner==-1) { state_.turn=enemy; if(state_.turns%2==0) applyRoundWhirlpools(state_.turns+1); } // 每两次行动进入新一轮，双方在下一次行动前同时受到一次漩涡吸引。
    events_.push_back(event); snapshots_.push_back(state_); return true; // 保存完整回合和快照。
} // 结束裁判执行。
const Snapshot& Game::state() const { return state_; } // 完整状态只能在裁判或赛后使用。
const std::vector<Snapshot>& Game::snapshots() const { return snapshots_; } // 返回赛后快照的常量引用。
const std::vector<Event>& Game::events() const { return events_; } // 返回完整事件的常量引用。
bool Game::finished() const { return state_.winner!=-1; } // 非负赢家或未决状态都表示终局。
Action chooseAction(const View& view,int style,std::mt19937& rng) { // 电脑枚举有限的合法计划，并只根据受限候选位置评分。
    std::vector<Action> plans; const auto role=view.roles[view.player]; // 收集合法回合候选。
    auto add=[&](Action a) { if(validate(view,a)) plans.push_back(std::move(a)); }; // 在加入评分前执行与玩家相同的规则检查。
    std::vector<Action> prefixes{{}}; // 不移动直接攻击也是一个选择，三格模式还可侦察。
    std::vector<Action> heavyMoves; // 重骑的单步移动前缀用于继续枚举第二步。
    for(int cell=1;cell<=size(view.mode)*size(view.mode);++cell) { // 为各职业生成合法普通移动或冲刺前缀。
        Action move{{{Kind::Move,cell}}}; if(validate(view,move)) { add(move); if(heroMode(view.mode) && (role==Role::Wolf || role==Role::Ink)) prefixes.push_back(move); if(heroMode(view.mode) && role==Role::Heavy) heavyMoves.push_back(move); } // 座狼与白墨可接主行动，重骑可接第二个移动步骤。
        if(heroMode(view.mode) && role==Role::Shadow) add({{{Kind::Lead,cell}}}); // 随影可选择本回合移动并带动对方。
        if(heroMode(view.mode) && role==Role::Ink && view.cooldown[view.player]==0) { Action dash{{{Kind::Dash,cell}}}; if(validate(view,dash)) { add(dash); prefixes.push_back(dash); } } // 白墨冷却就绪时考虑冲刺调整位置。
    } // 完成第一段移动枚举。
    if(heroMode(view.mode) && role==Role::Wolf) { auto initial=prefixes; for(const auto& p:initial) if(p.steps.size()==1) for(int cell=1;cell<=size(view.mode)*size(view.mode);++cell) { auto a=p; a.steps.push_back({Kind::Move,cell}); if(validate(view,a)) { add(a); prefixes.push_back(a); } } } // 座狼考虑先走两步再攻击。
    if(heroMode(view.mode) && role==Role::Heavy) for(const auto& p:heavyMoves) for(int cell=1;cell<=size(view.mode)*size(view.mode);++cell) { auto a=p; a.steps.push_back({Kind::Move,cell}); add(a); } // 重骑普通移动回合最多走两步，也允许某一步原地等待。
    if(heroMode(view.mode) && role==Role::Heavy) { std::function<void(Action)> grow=[&](Action path) { if(!path.steps.empty()) add(path); if(path.steps.size()==4) return; for(int cell=1;cell<=size(view.mode)*size(view.mode);++cell) { auto next=path; next.steps.push_back({Kind::Charge,cell}); if(validate(view,next,true)) grow(std::move(next)); } }; grow({}); } // 递归枚举所有一至四步、最多转向一次且不折返的秘密冲锋。
    for(const auto& p:prefixes) for(int cell=1;cell<=size(view.mode)*size(view.mode);++cell) if(view.possible[cell-1]) { auto a=p; a.steps.push_back({Kind::Attack,cell}); add(a); } // 只对可能的敌方位置尝试攻击。
    if(view.mode==Mode::Scout) for(int region=0;region<size(view.mode);++region) { add({{{Kind::Row,region}}}); add({{{Kind::Col,region}}}); } // 仅三格侦察模式生成行列侦察计划。
    if(heroMode(view.mode) && role==Role::Shadow) add({{{Kind::Follow,1-view.player}}}); // 随影偶尔利用跟随获取位移信息。
    int count=static_cast<int>(std::count(view.possible.begin(),view.possible.end(),true)); double best=-1e9; Action choice; // 准备比较计划得分。
    for(const auto& a:plans) { int own=view.own; validate(view,a,false,nullptr,&own); double score=std::uniform_real_distribution<double>(0,2)(rng); bool shot=false; // 少量随机分数打破平局。
        for(const auto& step:a.steps) { // 按动作给予进攻、信息或职业偏好的分数。
            if(step.kind==Kind::Attack) { shot=true; score+=count==1?100:style==0?20:style==1?8:12; } // 确定命中时所有风格都优先进攻。
            if(step.kind==Kind::Row || step.kind==Kind::Col) { int inside=0; for(int cell=1;cell<=25;++cell) inside+=view.possible[cell-1] && inRegion(view.mode,cell,step.kind,step.target,view.map); score+=std::min(inside,count-inside)*(style==2?4:1)-3; } // 不把候选集合当概率，仅衡量最坏分区缩小量。
            if(step.kind==Kind::Dash) score-=1.5; // 避免随意耗掉冲刺。
            if(step.kind==Kind::Follow) score+=style==1?7:2; // 给隐蔽风格跟随偏好。
            if(step.kind==Kind::Charge) { std::size_t index=&step-&a.steps[0]; if(index>0 && view.possible[step.target-1]) score+=style==0?12:8; else score-=0.5; } // 重骑偏向让可造成伤害的冲锋步骤经过候选格。
        } // 完成行动类型评分。
        int nearest=20; for(int cell=1;cell<=25;++cell) if(view.possible[cell-1]) nearest=std::min(nearest,distance(view.mode,own,cell)); // 仅用候选位置评估移动后的接近程度。
        if(!shot) score+=(style==1?nearest:-nearest)*0.8; // 无攻击时考虑移动接近或避开对手。
        if(score>best) { best=score; choice=a; } // 选择最高分计划。
    } // 合法移动至少含原地等待，因此候选一定非空。
    return choice; // 返回完整且合法的回合序列。
} // 结束受限电脑策略。
} // 结束规则命名空间。
