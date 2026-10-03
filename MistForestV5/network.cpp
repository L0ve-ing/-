#ifndef NOMINMAX // 避免系统宏覆盖标准算法。
#define NOMINMAX // 关闭系统最小值与最大值宏。
#endif // 结束兼容性宏。
#include <winsock2.h> // 先包含第二版套接字接口。
#include <ws2tcpip.h> // 使用 IPv4 地址解析和查询。
#include "network.hpp" // 引入职业版网络接口。
#include <algorithm> // 使用范围查找和最小值。
#include <chrono> // 使用单调时钟控制连接超时。
#include <sstream> // 使用有界文本协议。
#include <utility> // 使用移动语义与通知交换。
namespace forest { // 联网模块命名空间。
namespace { // 内部辅助工具不向界面暴露。
using Clock=std::chrono::steady_clock; // 超时计时不受系统日期调整影响。
constexpr int Version=6,ConnectSeconds=10,SilentSeconds=30; // 漩涡、重骑与公开护甲使用第六版协议，拒绝旧规则混连。
constexpr std::size_t MaxLine=8192,MaxBuffer=65536; // 严格限制消息和收发缓冲区。
void closeSocket(SOCKET& socket) { if(socket!=INVALID_SOCKET) closesocket(socket); socket=INVALID_SOCKET; } // 安全地关闭并清空句柄。
bool makeNonblocking(SOCKET socket) { u_long value=1; return ioctlsocket(socket,FIONBIO,&value)==0; } // 禁止界面线程阻塞等待网络。
bool completed(std::istringstream& in) { in>>std::ws; return !in.bad() && in.eof(); } // 不接受消息尾部的额外字段。
bool flag(int n) { return n==0 || n==1; } // 布尔字段仅接受零和一。
int maskOf(const mist::Mask& mask) { int bits=0; for(int i=0;i<25;++i) if(mask[i]) bits|=1<<i; return bits; } // 九格、十六格与二十五格地图统一编码成整数位集合。
void writeAction(std::ostream& out,const mist::Action& a) { out<<a.steps.size(); for(auto step:a.steps) out<<' '<<static_cast<int>(step.kind)<<' '<<step.target; } // 按顺序写出一回合的子动作。
bool readAction(std::istream& in,mist::Action& a,bool empty=false) { int count; if(!(in>>count) || count<(empty?0:1) || count>4) return false; for(int i=0;i<count;++i) { int kind,target; if(!(in>>kind>>target) || kind<0 || kind>7 || target<0 || target>25) return false; a.steps.push_back({static_cast<mist::Kind>(kind),target}); } return true; } // 在分配动作列表前限制数量，并允许编号七的冲锋步骤。
bool sameView(const mist::View& a,const mist::View& b) { return a.mode==b.mode && mist::encodeMap(a.map)==mist::encodeMap(b.map) && a.own==b.own && a.player==b.player && a.turn==b.turn && a.winner==b.winner && a.turns==b.turns && a.roles==b.roles && a.armor==b.armor && a.cooldown==b.cooldown && a.following==b.following && a.possible==b.possible && a.followReport.turn==b.followReport.turn && a.followReport.steps==b.followReport.steps && a.followReport.blocked==b.followReport.blocked && a.scanReport.turn==b.scanReport.turn && a.scanReport.target==b.scanReport.target && a.scanReport.kind==b.scanReport.kind && a.scanReport.positive==b.scanReport.positive && a.whirlpoolReport.turn==b.whirlpoolReport.turn && a.whirlpoolReport.from==b.whirlpoolReport.from && a.whirlpoolReport.to==b.whirlpoolReport.to; } // 完整复盘必须复现地图、公开护甲以及本人的全部私人报告。
} // 结束内部工具。
struct Session::Impl { // 实现对象拥有房主裁判和网络缓冲。
    bool winsock=false,hostRole=false,handshaken=false,connecting=false,ownReady=false,readyView=false,dirty=true,pending=false; // 会话身份与输入锁。
    SOCKET listener=INVALID_SOCKET,peer=INVALID_SOCKET; Phase stage=Phase::Idle; // 当前套接字与阶段。
    mist::Mode map=mist::Mode::Scout; int round=1,first=0; std::array<int,2> starts{}; std::array<mist::Role,2> roles{}; // 地图和仅房主持有的开局信息。
    mist::Map terrain=mist::defaultMap(mist::Mode::Scout); // 地形在握手时确认，一次连接中的重赛始终沿用它。
    mist::View ownView; std::vector<mist::Event> events; std::shared_ptr<mist::Game> game,finishedReplay,pendingCompleted; // 受限视图、公共事件与仅终局开放的回放。
    std::wstring message=L"尚未连接。"; std::string incoming,outgoing; // 界面提示和传输缓冲。
    Clock::time_point deadline=Clock::now(),lastReceive=Clock::now(),lastPing=Clock::now(); // 超时和心跳计时。
    Impl() { WSADATA data{}; winsock=WSAStartup(MAKEWORD(2,2),&data)==0; if(!winsock) fail(L"无法初始化网络组件。"); } // 每份会话持有一份系统网络引用。
    ~Impl() { disconnect(); if(winsock) WSACleanup(); } // 先关套接字再释放网络引用。
    int localPlayer() const { return hostRole?0:1; } // 房主始终是玩家一。
    void disconnect() { closeSocket(listener); closeSocket(peer); handshaken=false; connecting=false; pending=false; incoming.clear(); outgoing.clear(); } // 传输释放不会删除已完成回放通知。
    void fail(const std::wstring& reason) { disconnect(); stage=Phase::Error; message=reason; dirty=true; } // 错误后立即停止接收更多行动。
    void clearRound() { starts={}; roles={}; ownReady=false; readyView=false; pending=false; ownView={}; ownView.mode=map; ownView.map=terrain; ownView.player=localPlayer(); game.reset(); finishedReplay.reset(); events.clear(); stage=Phase::Setup; message=L"已连接，请选择职业和秘密起点。"; dirty=true; } // 重赛清局面但保留地图和尚未交付的完成记录。
    bool queue(const std::string& line) { if(line.size()>MaxLine || outgoing.size()+line.size()+1>MaxBuffer) { fail(L"网络消息积压或过长。"); return false; } outgoing+=line+'\n'; return true; } // 消息按换行分帧并限制积压。
    void setPhase() { stage=ownView.winner>=0?(finishedReplay?Phase::Finished:Phase::Waiting):ownView.turn==localPlayer() && !pending?Phase::Playing:Phase::Waiting; message=stage==Phase::Finished?L"对局结束，可以复盘。":stage==Phase::Playing?L"轮到你安排行动。":L"等待对方行动。"; dirty=true; } // 更新自己是否可行动。
    void sendState(const mist::Event* event) { // 只发送客人能知道的字段。
        auto v=game->view(1); mist::Event safe; if(event) safe=mist::publicEvent(*event); // 普通移动、冲锋路线与敌方私人反馈从网络包中删除。
        std::ostringstream out; out<<"STATE "<<round<<' '<<v.turns<<' '<<v.turn<<' '<<v.winner<<' '<<v.own<<' '<<static_cast<int>(v.roles[0])<<' '<<static_cast<int>(v.roles[1])<<' '<<v.armor[0]<<' '<<v.armor[1]<<' '<<v.cooldown[0]<<' '<<v.cooldown[1]<<' '<<v.following[0]<<' '<<v.following[1]<<' '<<maskOf(v.possible); // 公开状态包含双方角色与护甲，而坐标只包含客人本人。
        out<<' '<<v.followReport.turn<<' '<<v.followReport.steps<<' '<<v.followReport.blocked<<' '<<v.scanReport.turn<<' '<<static_cast<int>(v.scanReport.kind)<<' '<<v.scanReport.target<<' '<<v.scanReport.positive; // 仅发送客人自己的跟随和侦察反馈。
        out<<' '<<v.whirlpoolReport.turn<<' '<<v.whirlpoolReport.from<<' '<<v.whirlpoolReport.to; // 漩涡牵引的起点和终点是客人私有信息。
        out<<' '<<(event?safe.actor:-1)<<' '<<safe.hit<<' '<<safe.damaged<<' '<<safe.armorBlocked<<' '; writeAction(out,safe.action); queue(out.str()); // 最新事件同步致命伤、实际接触、护甲吸收与公开行动。
    } // 结束受限状态编码。
    void beginIfReady() { if(!starts[0] || !starts[1]) return; game=std::make_shared<mist::Game>(starts[0],starts[1],map,roles,first,terrain); ownView=game->view(0); readyView=true; setPhase(); sendState(nullptr); } // 双方选点完成才按握手地形公开角色并建局。
    void apply(const mist::Action& action) { // 仅权威房主调用完整裁判。
        if(!game->act(action)) { fail(L"收到非法回合计划。"); return; } // 原子执行失败不会继续游戏。
        auto e=game->events().back(); events.push_back(mist::publicEvent(e)); ownView=game->view(0); sendState(&e); // 双方各得受限视图，公共日志保持一致。
        if(game->finished()) { // 终局后才发送完整起点和实际执行的序列。
            finishedReplay=game; pendingCompleted=game; std::ostringstream out; out<<"REPLAY "<<round<<' '<<starts[0]<<' '<<starts[1]<<' '<<first<<' '<<game->events().size(); // 初始角色和地图已经在状态中确认。
            for(const auto& full:game->events()) { out<<' '; writeAction(out,full.action); } queue(out.str()); // 包含命中前实际执行的动作，不包含未执行计划。
        } // 结束终局分支。
        setPhase(); // 房主立即显示行动结果。
    } // 结束权威行动结算。
    bool readState(std::istringstream& in) { // 客人严格检查受限状态的字段和回合序号。
        int serial,count,turn,winner,own,r0,r1,a0,a1,c0,c1,f0,f1,bits,ft,fs,fb,st,sk,sr,sp,wt,wf,wto,actor,hit,damaged,blocked; mist::Action action; // 用整数接收字段后再检查枚举、布尔与私人报告范围。
        if(!(in>>serial>>count>>turn>>winner>>own>>r0>>r1>>a0>>a1>>c0>>c1>>f0>>f1>>bits>>ft>>fs>>fb>>st>>sk>>sr>>sp>>wt>>wf>>wto>>actor>>hit>>damaged>>blocked) || !readAction(in,action,true) || !completed(in)) return false; // 缺少字段或多余尾部均拒绝。
        if(serial!=round || !ownReady || !mist::valid(map,own,terrain) || !mist::validRole(r0) || !mist::validRole(r1) || !flag(a0) || !flag(a1) || c0<0 || c0>2 || c1<0 || c1>2 || !flag(f0) || !flag(f1) || !flag(sp) || !flag(hit) || !flag(damaged) || !flag(blocked) || turn<0 || turn>1 || winner<-1 || winner>2 || bits<=0 || bits>=(1<<25) || ft<0 || ft>count || fs<0 || fs>3 || fb<0 || fb>fs || st<0 || st>count || (sk!=2 && sk!=3) || sr<0 || sr>=mist::size(map) || (mist::heroMode(map) && (st!=0 || sp!=0))) return false; // 站位、护甲、技能与职业对战禁侦察规则必须成立。
        const bool noPull=wt==0 && wf==0 && wto==0; const bool hasPull=wt>0 && wt<=count+1 && wt%2==1 && mist::valid(map,wf,terrain) && mist::valid(map,wto,terrain) && wf!=wto && terrain.cells[wto-1]==mist::Terrain::Whirlpool && mist::canMove(map,wf,wto,terrain); // 私人漩涡报告只能记录轮次开始时的一步合法牵引。
        if(!noPull && !hasPull) return false; // 混合空字段、越界格或被边墙阻挡的伪造报告均无效。
        const int initialArmor0=mist::heroMode(map) && r0==static_cast<int>(mist::Role::Heavy)?1:0,initialArmor1=mist::heroMode(map) && r1==static_cast<int>(mist::Role::Heavy)?1:0; // 仅职业模式的重骑开局获得一点护甲，三格侦察模式忽略职业能力。
        if(count==0 && (readyView || actor!=-1 || hit || damaged || blocked || !action.steps.empty() || winner!=-1 || turn!=first || r1!=static_cast<int>(roles[1]) || a0!=initialArmor0 || a1!=initialArmor1 || (noPull && own!=starts[1]) || (hasPull && (wt!=1 || wf!=starts[1] || wto!=own)))) return false; // 首份状态允许构造函数先把客人从原起点吸到漩涡。
        if(count!=0 && (!readyView || count!=ownView.turns+1 || count>80 || ownView.winner!=-1 || actor!=ownView.turn || action.steps.empty())) return false; // 防止重复状态、抢回合和跨局消息。
        if(count>0 && ((winner==-1 && turn!=1-actor) || (winner>=0 && turn!=actor) || (hit && winner!=actor) || (!hit && winner>=0 && winner!=2) || (winner==2 && count!=80) || (winner==-1 && count==80) || r0!=static_cast<int>(ownView.roles[0]) || r1!=static_cast<int>(ownView.roles[1]) || (hit && !damaged) || (blocked && (!damaged || hit)) || (damaged && !hit && !blocked))) return false; // 胜负、致命伤与护甲吸收的组合必须自洽。
        if(count>0) { int victim=1-actor; std::array<int,2> nextArmor{a0,a1}; if(nextArmor[actor]!=ownView.armor[actor]) return false; if(blocked) { if(ownView.armor[victim]!=1 || nextArmor[victim]!=0) return false; } else if(nextArmor[victim]!=ownView.armor[victim]) return false; if(damaged && ownView.armor[victim]==1 && !blocked) return false; if(hit && ownView.armor[victim]!=0) return false; } // 只有受到实际伤害的一方能由一点护甲降为零，吸收后不能同时致命。
        if(count>0 && (wt<ownView.whirlpoolReport.turn || (wt==ownView.whirlpoolReport.turn && (wf!=ownView.whirlpoolReport.from || wto!=ownView.whirlpoolReport.to)) || (wt>ownView.whirlpoolReport.turn && (count%2!=0 || wt!=count+1 || (hasPull && wto!=own))))) return false; // 新的私人牵引报告只会在每两次行动后的下一轮开始出现。
        for(auto step:action.steps) { if((step.kind==mist::Kind::Move || step.kind==mist::Kind::Dash || step.kind==mist::Kind::Lead || step.kind==mist::Kind::Charge) && step.target!=0) return false; if(step.kind==mist::Kind::Attack && (step.target<1 || step.target>mist::size(map)*mist::size(map) || terrain.cells[step.target-1]==mist::Terrain::Wall)) return false; if((step.kind==mist::Kind::Row || step.kind==mist::Kind::Col) && (mist::heroMode(map) || step.target>=mist::size(map))) return false; if(step.kind==mist::Kind::Follow && step.target!=1-actor) return false; } // 普通移动、技能位移和冲锋路线必须清零，攻击目标与跟随对象保持公开。
        if(count>0) { const auto actingRole=static_cast<mist::Role>(actor==0?r0:r1); bool allMove=true,allCharge=true; for(auto step:action.steps) { allMove=allMove && step.kind==mist::Kind::Move; allCharge=allCharge && step.kind==mist::Kind::Charge; if(step.kind==mist::Kind::Charge && (!mist::heroMode(map) || actingRole!=mist::Role::Heavy)) return false; if(step.kind==mist::Kind::Attack && mist::heroMode(map) && actingRole==mist::Role::Heavy) return false; } if(mist::heroMode(map) && actingRole==mist::Role::Heavy && !((allMove && action.steps.size()<=2) || (allCharge && action.steps.size()<=4))) return false; } // 职业模式重骑只能移动或冲锋；三格侦察模式忽略所携带的职业枚举。
        ownView={}; ownView.mode=map; ownView.own=own; ownView.player=1; ownView.turn=turn; ownView.winner=winner; ownView.turns=count; ownView.roles={static_cast<mist::Role>(r0),static_cast<mist::Role>(r1)}; ownView.armor={a0,a1}; ownView.cooldown={c0,c1}; ownView.following={f0!=0,f1!=0}; ownView.followReport={ft,fs,fb}; ownView.scanReport={st,sr,static_cast<mist::Kind>(sk),sp!=0}; ownView.whirlpoolReport={wt,wf,wto}; ownView.map=terrain; // 显式保存公开状态与客人本人的全部私人字段，避免聚合字段变化造成错位。
        for(int i=0;i<25;++i) { ownView.possible[i]=(bits&(1<<i))!=0; if(ownView.possible[i] && !mist::valid(map,i+1,terrain)) return false; } // 候选集合不能包含方墙、湖泊或图外格。
        if(count>0) events.push_back({actor,action,hit!=0,false,damaged!=0,blocked!=0}); // 公共事件保留伤害与护甲吸收，私人侦察结果恒为假。
        readyView=true; pending=false; setPhase(); return true; // 收到裁定才解除输入锁。
    } // 结束状态解码。
    bool readReplay(std::istringstream& in) { // 从完整序列重建并核对终局。
        int serial,p0,p1,start,count; if(!(in>>serial>>p0>>p1>>start>>count) || serial!=round || ownView.winner<0 || finishedReplay || !mist::valid(map,p0,terrain) || p1!=starts[1] || start!=first || count!=ownView.turns || count<1 || count>80) return false; // 未结束的比赛不能解锁完整信息，回放起点必须可站人。
        auto rebuilt=std::make_shared<mist::Game>(p0,p1,map,ownView.roles,first,terrain); // 使用握手确认的地形和角色复演。
        for(int i=0;i<count;++i) { mist::Action a; if(!readAction(in,a) || !rebuilt->act(a) || !mist::samePublic(rebuilt->events().back(),events[i])) return false; } // 重放必须合法且不能改写公共记录。
        if(!completed(in) || !rebuilt->finished() || !sameView(rebuilt->view(1),ownView)) return false; // 检查最终本人视图，包括跟随障碍提示。
        finishedReplay=rebuilt; pendingCompleted=rebuilt; setPhase(); return true; // 完成通知不会被紧随其后的重赛消息覆盖。
    } // 结束完整复盘校验。
    bool receiveLine(const std::string& line) { // 将一行协议分派到当前身份允许的操作。
        std::istringstream in(line); std::string command; if(!(in>>command)) return false; // 空白行无效。
        if(!handshaken) { // 首先验证协议版本和地图。
            int version; if(hostRole) { if(command!="HELLO" || !(in>>version) || !completed(in)) return false; if(version!=Version) { fail(L"版本不一致，请双方使用漩涡与重骑版新程序。"); return true; } handshaken=true; clearRound(); queue("WELCOME "+std::to_string(Version)+" "+std::to_string(mist::size(map))+" "+mist::encodeMap(terrain)); return true; } // 房主在开局选点前同步完整地形。
            int mode; std::string encoded; mist::Map parsed; if(command!="WELCOME" || !(in>>version>>mode>>encoded) || !completed(in) || version!=Version || !mist::validMode(mode) || !mist::decodeMap(static_cast<mist::Mode>(mode),encoded,parsed)) return false; map=static_cast<mist::Mode>(mode); terrain=parsed; handshaken=true; clearRound(); return true; // 客人先验证整张地图，成功后才原子替换并开放选点。
        } // 握手结束后才能选择角色与起点。
        if(command=="PING") return completed(in) && queue("PONG"); // 心跳请求不消耗回合。
        if(command=="PONG") return completed(in); // 心跳不改变游戏回合。
        if(hostRole) { // 房主仅接受客人的选点和回合请求。
            if(command=="START") { int serial,cell,role; if(!(in>>serial>>cell>>role) || !completed(in) || serial!=round || game || starts[1] || !mist::valid(map,cell,terrain) || !mist::validRole(role)) return false; starts[1]=cell; roles[1]=static_cast<mist::Role>(role); beginIfReady(); return true; } // 每局起点与角色只能提交一次，湖泊和方墙禁止选点。
            if(command=="ACT") { int serial,expected; mist::Action a; if(!(in>>serial>>expected) || !readAction(in,a) || !completed(in) || serial!=round || !game || game->finished() || expected!=game->state().turns || game->state().turn!=1 || !game->legal(a)) return false; apply(a); return true; } // 对整回合计划一次校验，杜绝重复和越权执行。
            return false; // 客人不能伪造状态或发起重赛。
        } // 房主分支结束。
        if(command=="STATE") return readState(in); // 状态只包含本人可见字段。
        if(command=="REPLAY") return readReplay(in); // 客人接收权威视图与终局。
        if(command=="ROUND") { int serial,start; if(!(in>>serial>>start) || !completed(in) || stage!=Phase::Finished || serial!=round+1 || start!=1-first) return false; round=serial; first=start; clearRound(); return true; } // 重赛只在终局后交换先手。
        return false; // 未知消息明确失败。
    } // 结束协议解析。
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
Session::Session():impl_(std::make_unique<Impl>()) {} // 创建独占实现对象。
Session::~Session()=default; // 智能指针自动释放网络资源。
void Session::close() { impl_->disconnect(); impl_->clearRound(); impl_->pendingCompleted.reset(); impl_->round=1; impl_->first=0; impl_->stage=Phase::Idle; impl_->message=L"尚未连接。"; } // 主动关闭允许下一次全新连接。
bool Session::host(unsigned short port,mist::Mode mode) { return host(port,mode,mist::defaultMap(mode)); } // 兼容原有界面调用，省略地形时采用该模式默认地图。
bool Session::host(unsigned short port,mist::Mode mode,const mist::Map& terrain) { // 创建仅允许两名玩家的自定义地图房间。
    close(); auto& s=*impl_; s.hostRole=true; if(!s.winsock || !port || !mist::validMode(mist::size(mode)) || !mist::validMap(mode,terrain)) { s.fail(L"端口或地图设置无效，地图至少需要一个可站立格。"); return false; } s.map=mode; s.terrain=terrain; s.ownView.mode=mode; s.ownView.map=terrain; s.ownView.player=0; // 完整验证地形后才开放监听。
    s.listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); const BOOL exclusive=TRUE; // 建立 TCP 监听并要求端口独占。
    if(s.listener==INVALID_SOCKET || setsockopt(s.listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))!=0 || !makeNonblocking(s.listener)) { s.fail(L"无法创建房间套接字。"); return false; } // 配置失败时统一清理。
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_ANY); address.sin_port=htons(port); // 监听所有本机 IPv4 网卡。
    if(bind(s.listener,reinterpret_cast<const sockaddr*>(&address),sizeof(address))!=0 || listen(s.listener,1)!=0) { s.fail(L"房间端口被占用，请换一个端口。"); return false; } // 端口冲突不修改系统设置。
    s.stage=Phase::Listening; s.message=L"房间已创建，等待朋友加入。"; return true; // 界面仍可取消等待。
} // 结束建房。
bool Session::join(const std::string& ipv4,unsigned short port) { // 与上一版本一样使用非阻塞 IPv4 直连。
    close(); auto& s=*impl_; s.hostRole=false; sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(port); // 清理旧连接并准备目标。
    if(!s.winsock || !port || ipv4.size()>15 || inet_pton(AF_INET,ipv4.c_str(),&address.sin_addr)!=1) { s.fail(L"请输入有效 IPv4 地址和端口。"); return false; } // 防止界面意外把网址当成 IP。
    s.peer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); if(s.peer==INVALID_SOCKET || !makeNonblocking(s.peer)) { s.fail(L"无法创建连接。"); return false; } // 套接字始终为非阻塞。
    s.stage=Phase::Connecting; s.connecting=true; s.deadline=Clock::now()+std::chrono::seconds(ConnectSeconds); s.message=L"正在连接房主……"; // 连接失败最多等待十秒。
    int result=connect(s.peer,reinterpret_cast<const sockaddr*>(&address),sizeof(address)); if(result==0) s.connectedTransport(); else if(WSAGetLastError()!=WSAEWOULDBLOCK) { s.fail(L"无法连接房主，请检查地址与端口。"); return false; } return true; // 最终成功由后续轮询确认。
} // 结束加入。
bool Session::poll() { impl_->tick(); bool changed=impl_->dirty; impl_->dirty=false; return changed; } // 只在可见状态改变时请求重绘。
bool Session::chooseStart(int cell,mist::Role role) { // 提交本人的秘密位置和所选职业。
    auto& s=*impl_; if(!s.handshaken || s.stage!=Phase::Setup || s.ownReady || !mist::valid(s.map,cell,s.terrain) || !mist::validRole(static_cast<int>(role))) return false; // 双击、湖泊、方墙和错误阶段均拒绝。
    s.starts[s.localPlayer()]=cell; s.roles[s.localPlayer()]=role; s.ownReady=true; s.stage=Phase::Waiting; s.message=L"已准备，等待另一位玩家。"; s.dirty=true; // 立即锁定起点。
    if(s.hostRole) s.beginIfReady(); else s.queue("START "+std::to_string(s.round)+" "+std::to_string(cell)+" "+std::to_string(static_cast<int>(role))); return s.stage!=Phase::Error; // 客人的秘密信息只发给可信裁判。
} // 结束起点提交。
bool Session::act(const mist::Action& action) { // 提交本回合计划，不允许在等待确认时再次发送。
    auto& s=*impl_; if(!s.handshaken || !s.readyView || s.stage!=Phase::Playing || s.pending || !mist::validate(s.ownView,action)) return false; // 本地合法性和裁判复验共同保护状态。
    if(s.hostRole) s.apply(action); else { std::ostringstream out; out<<"ACT "<<s.round<<' '<<s.ownView.turns<<' '; writeAction(out,action); s.pending=true; s.stage=Phase::Waiting; s.message=L"已提交，等待房主结算。"; s.dirty=true; s.queue(out.str()); } return s.stage!=Phase::Error; // 客人等待权威状态才继续行动。
} // 结束行动请求。
bool Session::newRound() { auto& s=*impl_; if(!s.hostRole || !s.handshaken || s.stage!=Phase::Finished || s.round>=1000000000) return false; ++s.round; s.first=1-s.first; s.clearRound(); return s.queue("ROUND "+std::to_string(s.round)+" "+std::to_string(s.first)); } // 重赛保留连接并交换先手。
Phase Session::phase() const { return impl_->stage; } // 返回会话阶段。
bool Session::isHost() const { return impl_->hostRole; } // 是否为可信房主。
bool Session::connected() const { return impl_->handshaken && impl_->peer!=INVALID_SOCKET; } // 握手完成才显示已连接。
int Session::player() const { return impl_->localPlayer(); } // 玩家编号从零开始。
mist::Mode Session::mode() const { return impl_->map; } // 返回由房主决定的地图。
const mist::Map& Session::terrain() const { return impl_->terrain; } // 返回开局和重赛统一使用的公开地形。
bool Session::hasView() const { return impl_->readyView; } // 双方准备完成后才有对战视图。
bool Session::hasStart() const { return impl_->ownReady; } // 本人是否已经锁定起点。
mist::View Session::view() const { return impl_->ownView; } // 按值返回副本，外部不能改内部局面。
const std::vector<mist::Event>& Session::publicEvents() const { return impl_->events; } // 仅返回脱敏日志。
const std::wstring& Session::status() const { return impl_->message; } // 返回中文连接提示。
std::shared_ptr<mist::Game> Session::replay() const { return impl_->finishedReplay; } // 完整信息仅在终局开放。
std::shared_ptr<mist::Game> Session::takeCompletedReplay() { return std::exchange(impl_->pendingCompleted,{}); } // 每份终局通知只交付一次。
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
