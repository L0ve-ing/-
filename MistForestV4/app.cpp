#ifndef UNICODE // 显式启用宽字符 Windows 接口。
#define UNICODE // 让系统资源名称使用 Unicode。
#endif // 结束宽字符兼容设置。
#ifndef NOMINMAX // 避免系统宏覆盖标准库函数。
#define NOMINMAX // 禁用旧版最小值最大值宏。
#endif // 结束兼容设置。
#include <windows.h> // 使用原生窗口和 GDI 绘图。
#include <commdlg.h> // 系统地图保存与打开对话框。
#include "map_files.hpp" // 本地地图文件读写与原子保存。
#include <windowsx.h> // 使用鼠标坐标提取工具。
#include <algorithm> // 使用计数与范围限制。
#include <filesystem> // 导出记录到程序所在目录。
#include <fstream> // 输出 UTF-8 文本记录。
#include <sstream> // 拼接可读的复盘信息。
#include "game.hpp" // 第三版职业与回合规则。
#include "network.hpp" // 联网只提供受限玩家视角。
#include "lessons.hpp" // 保留三格地图的三个推理关卡。
namespace ui { // 界面状态和规则分离。
using namespace mist; // 简化规则类型名称。
constexpr int WIDTH=1120,HEIGHT=780; // 使用固定逻辑画布并按窗口缩放。
const COLORREF BG=RGB(241,245,239),INK=RGB(22,43,36),MUTED=RGB(100,117,107),TEAL=RGB(26,110,78),PALE=RGB(222,239,225),CORAL=RGB(188,87,67),LINE=RGB(211,224,213),WHITE=RGB(255,255,255),GOLD=RGB(205,153,45); // 森林主题的底色与信息颜色。
enum class Page { Lobby,Battle,Lesson,Replay,Network,Editor }; // 联网对战和本地对战共享渲染，但使用不同数据源。
enum class Phase { Cover,Setup,Playing,Waiting,Hold,Finished,Error }; // 区分私密交接、布置、行动与终局。
enum Id { Home=1,Teach,Review,Small,Heroes,Heroes4,Cpu,Hotseat,Online,Begin,Style0,Style1,Style2,Assist,Series,Ready,Commit,Pass,Again,ReturnResult,Attack,Move,Row,Col,Dash,Follow,Lead,Wait,Undo,Clear,NetHost,NetJoin,NetCancel,ReplayPrev,ReplayNext,ReplayStart,ReplayEnd,View0,View1,Export,LessonCheck,LessonPrev,LessonNext,MapEdit,MapUse,BrushGround,BrushWall,BrushLake,BrushLine,BrushErase,MapSave,MapLoad,MapApply,MapReset,MapEmpty,MapUndo,CellBase=100,RoleBase=200,HorizontalBase=300,VerticalBase=400 }; // 命令编号，棋盘和职业使用独立编号区间。
enum class Brush { Ground,Wall,Lake,Line,Erase }; // 编辑器分别绘制空地、整格墙、湖泊与格边墙，橡皮擦可清除两类对象。
struct Hit { RECT rect; int id; bool enabled; }; // 保存自绘按钮的可点击区域。
struct State { // 当前窗口的界面状态。
    HWND window=nullptr,addressEdit=nullptr,portEdit=nullptr; Page page=Page::Lobby; Phase phase=Phase::Setup; // 原生控件与当前页面。
    Mode mode=Mode::Heroes; bool bot=true,online=false,assist=true,series=false,coverSetup=false,lastOnline=false; int style=2,viewer=0,setupPlayer=0,round=1; // 大厅选项与私密视角。
    Map customMap=defaultMap(Mode::Heroes),editMap=defaultMap(Mode::Heroes),roundMap=defaultMap(Mode::Heroes); bool customEnabled=false,editorStarted=false; Brush brush=Brush::Wall; std::vector<Map> mapUndo; std::filesystem::path editorPath; std::wstring editorNotice; // 地图草稿、本局快照与编辑器历史独立保存。
    std::array<int,2> starts{},score{}; std::array<Role,2> roles{Role::Sniper,Role::Sniper}; Role selectedRole=Role::Sniper; int selected=0; Kind kind=Kind::Attack; Action plan; // 起点、职业和待确认回合。
    std::shared_ptr<Game> game,last; std::unique_ptr<forest::Session> net; std::wstring notice,addresses; // 本地裁判、最近回放与网络会话。
    int replay=0,replayView=0,historyOffset=0,lesson=0; std::array<bool,9> checked{}; std::array<bool,3> completed{}; std::wstring feedback; // 复盘和课堂状态。
    std::vector<Hit> hits; double scale=1; int offsetX=0,offsetY=0; std::mt19937 rng{std::random_device{}()}; // 点击地图与电脑随机数。
} s; // 单窗口状态对象。
RECT rect(int x,int y,int w,int h) { return RECT{x,y,x+w,y+h}; } // 将左上角与宽高转换为 Win32 矩形。
void fill(HDC dc,RECT r,COLORREF color) { HBRUSH b=CreateSolidBrush(color); FillRect(dc,&r,b); DeleteObject(b); } // 创建画刷填充矩形并及时释放资源。
void card(HDC dc,RECT r,COLORREF color=WHITE,COLORREF border=LINE,int radius=16,int width=1) { // 绘制带圆角与边框的面板。
    HBRUSH b=CreateSolidBrush(color); HPEN p=CreatePen(PS_SOLID,width,border); // 创建本次绘图用的画刷与画笔。
    HGDIOBJ oldB=SelectObject(dc,b),oldP=SelectObject(dc,p); // 保存原绘图对象，之后恢复。
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius); // 按逻辑坐标绘制圆角矩形。
    SelectObject(dc,oldB); SelectObject(dc,oldP); DeleteObject(b); DeleteObject(p); // 恢复 DC 状态并释放临时对象。
} // 结束面板绘制函数。
void text(HDC dc,const std::wstring& value,RECT r,int size=16,COLORREF color=INK,bool bold=false,UINT format=DT_LEFT|DT_WORDBREAK) { // 输出自动换行的中文文字。
    HFONT font=CreateFontW(-size,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI"); // 使用清晰的中文系统字体。
    HGDIOBJ old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color); // 设置透明背景和文字颜色。
    DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&r,format|DT_NOPREFIX); // DT_NOPREFIX 防止文字中的 & 被当成快捷键标记。
    SelectObject(dc,old); DeleteObject(font); // 恢复旧字体，防止 GDI 对象泄漏。
} // 结束文本绘制函数。
void button(HDC dc,int id,const std::wstring& label,RECT r,bool primary=false,bool enabled=true) { // 绘制按钮，同时登记鼠标可点击区域。
    card(dc,r,enabled?(primary?TEAL:WHITE):RGB(235,239,238),primary?TEAL:LINE,12); // 用底色区分主要操作和禁用操作。
    text(dc,label,r,16,enabled?(primary?WHITE:INK):MUTED,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); // 居中显示按钮名称。
    s.hits.push_back({r,id,enabled}); // 点击时只派发 enabled 为真的按钮。
} // 结束按钮函数。
void controls() { // 标准输入框只在联网入口显示。
    if(!s.window || !s.portEdit || !s.addressEdit) return; // 无窗口的离屏检查跳过控件。
    RECT client{}; GetClientRect(s.window,&client); double scale=std::min(client.right/double(WIDTH),client.bottom/double(HEIGHT)); int x=int((client.right-WIDTH*scale)/2),y=int((client.bottom-HEIGHT*scale)/2); // 与画布使用相同缩放。
    MoveWindow(s.portEdit,x+int(65*scale),y+int(320*scale),int(210*scale),int(44*scale),TRUE); MoveWindow(s.addressEdit,x+int(602*scale),y+int(320*scale),int(450*scale),int(44*scale),TRUE); // 对齐端口和地址区域。
    bool show=s.page==Page::Network,edit=!s.net || s.net->phase()==forest::Phase::Idle || s.net->phase()==forest::Phase::Error; // 连接期间不允许修改已提交地址。
    for(HWND control:{s.portEdit,s.addressEdit}) { ShowWindow(control,show?SW_SHOW:SW_HIDE); EnableWindow(control,edit); } // 同步可见性与可编辑状态。
} // 结束原生控件布局。
void invalidate() { s.hits.clear(); controls(); if(s.window) InvalidateRect(s.window,nullptr,FALSE); } // 阶段改变先丢弃旧按钮，避免连击旧命令。
void resetPlan() { s.plan.steps.clear(); s.selected=0; s.kind=Kind::Attack; s.notice.clear(); } // 新回合和交接清除未提交动作。
View currentView() { return s.online?s.net->view():s.game->view(s.viewer); } // 进行中所有棋盘信息来自本人的受限视图。
Map selectedMap() { return s.mode==Mode::Heroes && s.customEnabled?s.customMap:defaultMap(s.mode); } // 只有五格地图可选择自定义地形。
Map battleMap() { return s.online && s.net?s.net->terrain():s.roundMap; } // 选点阶段也必须使用已确定的本局地图。
std::wstring playerName(int p) { return L"玩家"+std::to_wstring(p+1); } // 统一玩家编号文案。
std::wstring modeName(Mode mode) { return mode==Mode::Heroes?L"5×5 职业版":mode==Mode::Heroes4?L"4×4 职业版":L"3×3 侦察版"; } // 三格地图保留侦察，两张职业地图不再提供侦察。
void header(HDC dc) { // 所有页面共用顶部导航。
    fill(dc,rect(0,0,WIDTH,96),INK); text(dc,L"雾隐之森",rect(36,18,330,43),30,WHITE,true); text(dc,L"MIST FOREST  /  职业对抗",rect(38,62,370,24),12,RGB(167,199,171)); // 标题和版本主题。
    button(dc,Home,L"对战大厅",rect(625,28,138,42),s.page==Page::Lobby); button(dc,Teach,L"推理课堂",rect(778,28,138,42),s.page==Page::Lesson); button(dc,Review,L"对局复盘",rect(931,28,153,42),s.page==Page::Replay,s.last!=nullptr); // 无完整终局记录时禁用复盘。
} // 结束顶部导航。
void footer(HDC dc,const std::wstring& label) { text(dc,label,rect(38,742,1040,25),13,MUTED); } // 底部显示回合或操作帮助。
void lobby(HDC dc) { // 设置地图、对战方式和基础辅助。
    card(dc,rect(36,122,710,599)); card(dc,rect(768,122,316,599)); text(dc,L"迷雾之中，掌握你的节奏。",rect(64,151,652,52),29,INK,true); // 大厅主视觉。
    text(dc,L"编辑你的战场，用墙体与湖泊改变对抗；职业对战已移除侦察。",rect(65,215,620,61),17,MUTED); // 简介回合变化。
    button(dc,Small,L"3×3 无限侦察",rect(65,293,189,47),s.mode==Mode::Scout); button(dc,Heroes4,L"4×4 四职业",rect(267,293,189,47),s.mode==Mode::Heroes4); button(dc,Heroes,L"5×5 四职业",rect(469,293,192,47),s.mode==Mode::Heroes && !s.customEnabled); // 三张地图各有入口，选中地图同时用于人机、同机和联机。
    button(dc,Cpu,L"人机练习",rect(65,366,189,44),s.bot); button(dc,Hotseat,L"双人交接",rect(267,366,189,44),!s.bot); button(dc,Online,L"双人联机 →",rect(469,366,192,44)); // 保留本地与网络对战。
    button(dc,Style0,L"主动进攻",rect(65,439,189,42),s.style==0,s.bot); button(dc,Style1,L"隐蔽反击",rect(267,439,189,42),s.style==1,s.bot); button(dc,Style2,L"灵活换位",rect(469,439,192,42),s.style==2,s.bot); // 电脑风格不读取隐藏坐标。
    button(dc,Assist,s.assist?L"推理辅助：开":L"推理辅助：关",rect(65,517,288,43),s.assist); button(dc,Series,s.series?L"本地三局两胜":L"本地单局练习",rect(368,517,293,43),s.series); // 联网单局可重赛，本地支持系列比分。
    button(dc,MapEdit,L"地图 DIY · 编辑 / 保存",rect(65,575,288,36)); button(dc,MapUse,L"使用 DIY 地图",rect(368,575,293,36),s.mode==Mode::Heroes && s.customEnabled); // 编辑器和自定义地图开关独立于对战方式。
    button(dc,Begin,L"选择角色与秘密起点  →",rect(65,624,596,59),true); // 正式进入本地开局。
    text(dc,L"四种行动风格",rect(792,152,267,35),21,TEAL,true); // 右侧展示职业差异。
    for(int i=0;i<4;++i) { auto role=static_cast<Role>(i); text(dc,roleName(role),rect(793,218+i*105,264,27),19,INK,true); std::wstring line=i==0?L"射程3，选择有利攻击位置。":i==1?L"移动2步，可在攻击前后拆分。":i==2?L"移动后攻击，额外冲刺恰好2步。":L"射程2，跟随敌人或移动带动敌人。"; text(dc,line,rect(793,253+i*105,261,56),15,MUTED); } // 简洁解释职业定位。
    footer(dc,L"职业版无侦察；5×5支持DIY地形，4×4为空地图；3×3保留无限侦察。"); // 公开两张职业地图的差异与时长规则。
} // 结束大厅。
void room(HDC dc) { // 双机联机入口沿用地址直连。
    card(dc,rect(36,122,514,599)); card(dc,rect(574,122,510,599)); bool idle=!s.net || s.net->phase()==forest::Phase::Idle || s.net->phase()==forest::Phase::Error; // 当前可否建立新连接。
    text(dc,L"创建房间",rect(64,151,450,44),27,INK,true); text(dc,L"加入朋友",rect(602,151,450,44),27,INK,true); // 两种网络身份。
    text(dc,L"房主决定地图。双方连通后，各自选择角色与秘密起点。",rect(64,220,450,65),17,MUTED); text(dc,L"输入房主可达的 IPv4 地址，端口填写在左侧。",rect(602,220,450,65),17,MUTED); // 显示操作顺序。
    text(dc,L"端口（创建 / 加入共用）",rect(64,285,451,27),15,TEAL,true); text(dc,L"房主 IPv4 地址",rect(602,285,450,27),15,TEAL,true); // 输入框标签。
    card(dc,rect(65,320,210,44)); card(dc,rect(602,320,450,44)); // 原生控件覆盖在这些框上。
    button(dc,NetHost,L"创建房间 · 等待朋友",rect(64,395,452,52),true,idle); button(dc,NetJoin,L"连接房主",rect(602,395,450,52),true,idle); // 连接请求使用非阻塞轮询。
    text(dc,L"可分享的本机地址",rect(64,480,450,28),16,INK,true); text(dc,s.addresses.empty()?L"请查看你的虚拟局域网或网卡地址。":s.addresses,rect(64,519,450,87),16,TEAL); // 地址仅供玩家选择和分享。
    text(dc,modeName(s.mode)+(heroMode(s.mode)?L"；职业对战无侦察。":L"；侦察不限次数。")+L"\n房主地图自动同步给加入者。",rect(64,633,450,58),15,MUTED); // 统一房间规则。
    text(dc,!s.notice.empty()?s.notice:s.net?s.net->status():L"默认端口39091；同机测试填127.0.0.1。",rect(602,485,450,118),16,TEAL); button(dc,NetCancel,idle?L"返回大厅":L"取消连接",rect(602,633,450,48)); // 提供可恢复错误提示。
    footer(dc,L"双方需使用同一新版。异地需可互通的虚拟局域网或公网映射；房主担任可信裁判。"); // 明确版本与连通前提。
} // 结束联机入口。
RECT cellRect(Mode mode,int cell) { int n=size(mode),gap=n==5?88:n==4?110:148,width=n==5?80:n==4?100:136; return rect(82+((cell-1)%n)*gap,226+((cell-1)/n)*gap,width,width); } // 三种边长使用相应格子尺寸，棋盘均保持在左侧区域内。
RECT edgeRect(bool horizontal,int index) { int row=horizontal?index/5:index/6,col=horizontal?index%5:index%6; return horizontal?rect(78+col*88+6,222+row*88-6,76,12):rect(78+col*88-6,222+row*88+6,12,76); } // 线形墙位于相邻格子的公共边，点击区域避开交点。
void drawWalls(HDC dc,Mode mode,const Map& map,bool editing=false) { // 线形墙在格子之上绘制，实际碰撞使用无厚度边线。
    if(mode!=Mode::Heroes) return; // 只有五格地图具有可编辑线形墙。
    for(int direction=0;direction<2;++direction) for(int i=0;i<30;++i) { bool horizontal=direction==0,wall=horizontal?map.horizontal[i]:map.vertical[i]; if(!wall && !editing) continue; RECT r=edgeRect(horizontal,i); int row=horizontal?i/5:i/6,col=horizontal?i%5:i%6; // 固定索引与地图文件、裁判保持一致。
        HPEN pen=CreatePen(wall?PS_SOLID:PS_DOT,wall?6:1,wall?RGB(143,80,45):RGB(161,182,170)); HGDIOBJ old=SelectObject(dc,pen); int x=78+col*88,y=222+row*88; MoveToEx(dc,x,y,nullptr); LineTo(dc,x+(horizontal?88:0),y+(horizontal?0:88)); SelectObject(dc,old); DeleteObject(pen); // 虚线表示可编辑边，棕色实线表示已存在的墙。
        if(editing && (s.brush==Brush::Line || s.brush==Brush::Erase)) s.hits.push_back({r,(horizontal?HorizontalBase:VerticalBase)+i,true}); // 墙工具与橡皮擦可以点击格边。
    } // 绘制所有横边与竖边。
} // 结束线形墙绘制。
void marker(HDC dc,RECT r,int player,int offset=0) { int diameter=r.right-r.left<100?30:45; RECT dot=rect((r.left+r.right-diameter)/2+offset,(r.top+r.bottom-diameter)/2,diameter,diameter); card(dc,dot,player==0?TEAL:CORAL,player==0?TEAL:CORAL,diameter); text(dc,player==0?L"一":L"二",dot,16,WHITE,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); } // 同格时把两人的圆点错开。
Step stepFor(int cell,Mode mode) { return {s.kind,s.kind==Kind::Row?(cell-1)/size(mode):s.kind==Kind::Col?(cell-1)%size(mode):cell}; } // 棋盘点击转换为动作目标。
void board(HDC dc,Mode mode,const Mask& possible,int own,int other,bool reveal=false,bool lesson=false,int preview=0,const Map* terrain=nullptr) { // 进行中 other 恒为零，避免绘制隐藏敌人。
    const Map map=terrain?*terrain:defaultMap(mode); bool editing=s.page==Page::Editor; // 各页面显式使用本局或编辑草稿的地图。
    for(int cell=1;cell<=size(mode)*size(mode);++cell) { RECT r=cellRect(mode,cell); bool blocked=!valid(mode,cell,map),lake=map.cells[cell-1]==Terrain::Lake; bool selected=lesson?s.checked[cell-1]:s.page==Page::Battle && s.phase==Phase::Setup && s.selected==cell; // 禁入格不能用于选点或技能目标。
        card(dc,r,lake?RGB(200,227,238):blocked?INK:selected?RGB(255,243,213):possible[cell-1]?PALE:RGB(248,250,247),selected?GOLD:LINE,12,selected?3:1); // 颜色区分候选与当前选择。
        text(dc,lake?L"湖泊":blocked?L"方墙":std::to_wstring(cell),rect(r.left+7,r.top+5,55,22),heroMode(mode)?14:19,blocked && !lake?WHITE:INK,true); // 地形用颜色和文字同时标记。
        if(lake) text(dc,L"≈",rect(r.left+21,r.top+29,48,36),28,RGB(63,132,163),true,DT_CENTER); // 湖面纹理帮助区分可穿过攻击的湖泊。
        if(cell==own) marker(dc,r,reveal?0:s.viewer,own==other?-15:0); // 先绘制本人位置。
        if(reveal && cell==other) marker(dc,r,1,own==other?15:0); // 标记本人或赛后公开双方。
        if(preview==cell && preview!=own) text(dc,L"预览",rect(r.left+9,r.top+31,65,25),16,TEAL,true); // 计划不改变真实位置，只绘制预览文字。
        if(lesson && selected) text(dc,L"已选",rect(r.left+32,r.top+63,70,31),18,TEAL,true); // 教学采用多选状态。
        bool enabled=lesson || (editing && s.brush!=Brush::Line) || (s.page==Page::Battle && s.phase==Phase::Setup && !blocked); // 起点阶段所有合法格都可选。
        if(s.page==Page::Battle && s.phase==Phase::Playing) { auto a=s.plan; a.steps.push_back(stepFor(cell,mode)); enabled=validate(currentView(),a,true); if(enabled) text(dc,L"可选",rect(r.left+7,r.bottom-23,50,18),11,TEAL); } // 依照当前计划位置检查下一步目标。
        s.hits.push_back({r,CellBase+cell,enabled}); // 登记棋盘交互区域。
    } // 结束所有格子的绘制。
    drawWalls(dc,mode,map,editing); // 最后绘制墙边，使其点击优先于相邻格子。
} // 结束棋盘。
void cover(HDC dc) { card(dc,rect(218,175,684,495)); text(dc,L"秘密视角交接",rect(264,225,589,51),31,INK,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); text(dc,L"请把屏幕交给"+playerName(s.viewer),rect(264,322,589,49),26,TEAL,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); text(dc,L"另一位玩家请暂时回避。\n确认后显示角色、棋盘与私人信息。",rect(298,405,521,74),18,MUTED,false,DT_CENTER); button(dc,Ready,L"我已准备好 · 显示我的视角",rect(317,535,486,60),true); } // 交接页不绘制任何私密局面。
std::wstring privateText(const View& view) { // 只读取自己视图中的私人报告。
    std::wstring out; const auto& scan=view.scanReport; if(scan.turn>0) out=L"第"+std::to_wstring(scan.turn)+L"回合侦察：第"+std::to_wstring(scan.target+1)+(scan.kind==Kind::Row?L"行":L"列")+(scan.positive?L"当时有对手。":L"当时无对手。")+L"移动后需更新。"; // 侦察结果附获得时刻。
    const auto& follow=view.followReport; if(follow.turn>0) { if(!out.empty()) out+=L"\n"; out+=L"第"+std::to_wstring(follow.turn)+L"回合被带动/跟随："+std::to_wstring(follow.steps)+L"步，受阻跳过"+std::to_wstring(follow.blocked)+L"步。"; } // 受阻信息仅受影响玩家可见。
    return out.empty()?(heroMode(view.mode)?L"职业对战无侦察。组合完成后点击确认，才会执行本回合。":L"侦察不限次数。选择行动后点击确认。"):out; // 无私人报告时显示操作说明。
} // 结束私有文本。
const std::vector<Event>& events() { return s.online?s.net->publicEvents():s.game->events(); } // 本地完整事件也只能通过公开格式化输出。
std::wstring plannedText(const Action& plan) { // 尚未执行的计划不能显示命中或侦察结果。
    std::wstring out; for(auto step:plan.steps) { if(!out.empty()) out+=L" → "; if(step.kind==Kind::Move) out+=L"移动/等待到"+std::to_wstring(step.target); else if(step.kind==Kind::Dash) out+=L"冲刺到"+std::to_wstring(step.target); else if(step.kind==Kind::Lead) out+=L"移动到"+std::to_wstring(step.target)+L"并带动"; else if(step.kind==Kind::Attack) out+=L"攻击"+std::to_wstring(step.target); else if(step.kind==Kind::Follow) out+=L"指定跟随"; else out+=L"侦察第"+std::to_wstring(step.target+1)+(step.kind==Kind::Row?L"行":L"列"); } return out; // 只描述即将执行的操作。
} // 结束计划说明。
void history(HDC dc) { if((s.online && !s.net) || (!s.online && !s.game)) return; const auto& log=events(); int end=std::max(0,int(log.size())-s.historyOffset),start=std::max(0,end-2); for(int i=start;i<end;++i) text(dc,std::to_wstring(i+1)+L" "+actionText(log[i]),rect(664,642+(i-start)*27,391,26),12,MUTED); } // 底部显示可滚动的两条公开回合。
void battle(HDC dc) { // 本地与联网使用相同棋盘交互。
    if(s.phase==Phase::Cover) { cover(dc); return; } // 保密交接阶段不访问对局信息。
    card(dc,rect(36,122,570,599)); card(dc,rect(638,122,446,599)); bool setup=s.phase==Phase::Setup,done=s.phase==Phase::Finished; // 两栏为棋盘和行动。
    bool has=s.online?s.net && s.net->hasView():s.game!=nullptr; Mode mode=s.online && s.net?s.net->mode():s.mode; View view; if(has) view=currentView(); const Map map=battleMap(); // 可用信息均来自本人视图。
    std::shared_ptr<Game> replay=s.online && s.net?s.net->replay():s.game; Mask possible{}; int own=has?view.own:s.starts[s.viewer],other=0,preview=own; // 等待选点时只显示自己的位置。
    if(has && s.assist) possible=view.possible; // 可选显示候选集合。
    if(done && replay) { own=replay->state().positions[0]; other=replay->state().positions[1]; possible={}; } // 只有終局允许看到两个真实位置。
    if(has && s.phase==Phase::Playing) validate(view,s.plan,true,nullptr,&preview); // 预览合法计划最后落点。
    text(dc,setup?L"选择角色与秘密起点":done?L"本局结束 · 位置公开":playerName(s.viewer)+L"的棋盘",rect(64,146,514,43),25,INK,true); text(dc,modeName(mode)+(mode==Mode::Heroes?L" · 方墙 / 边墙 / 湖泊":mode==Mode::Heroes4?L" · 全部16格可进入":L" · 可选择原地等待")+(has && !setup && heroMode(mode)?L" · 对方："+roleName(view.roles[1-s.viewer]):L""),rect(65,195,510,26),14,MUTED); // 地图说明；双方就绪后显示对方已公开的职业。
    board(dc,mode,possible,own,other,done,false,preview,&map); text(dc,s.assist?L"浅绿为未排除的候选；“预览”尚未实际执行。":L"推理辅助已关闭；依据公开记录自行判断。",rect(65,688,516,25),13,MUTED); // 棋盘图例。
    if(setup) { // 双方分别选择角色和秘密起点。
        text(dc,playerName(s.viewer)+L" · 布置角色",rect(664,151,390,43),25,INK,true); // 当前选择者。
        if(heroMode(mode)) { for(int i=0;i<4;++i) button(dc,RoleBase+i,roleName(static_cast<Role>(i)),rect(664+(i%2)*203,225+(i/2)*58,190,45),s.selectedRole==static_cast<Role>(i)); text(dc,roleHelp(s.selectedRole),rect(664,368,391,115),17,TEAL); } // 两张职业地图使用同一套角色选择和能力解释。
        else text(dc,L"经典九格地图。移动、攻击、侦察选一项；移动可留在原地。",rect(664,241,391,130),18,MUTED); // 三格版不启用职业技能。
        text(dc,s.selected?L"秘密起点："+std::to_wstring(s.selected)+L"号格":L"在左侧选择你的起点。",rect(664,507,391,42),22,INK,true); button(dc,Commit,L"确认职业与起点",rect(664,582,393,56),true,s.selected>0); // 选定后同时锁定职业与位置。
    } else if(s.phase==Phase::Error) { text(dc,L"连接已中断",rect(664,158,391,47),27,CORAL,true); text(dc,s.net?s.net->status():L"网络连接不可用。",rect(664,238,391,130),17,MUTED); button(dc,Online,L"返回联机房间",rect(664,421,393,55),true); } // 断线后不能继续提交隐藏动作。
    else if(done && replay) { int winner=replay->state().winner; text(dc,winner==2?L"本局未决":playerName(winner)+L"获胜",rect(664,162,393,54),30,TEAL,true); text(dc,L"攻击命中立即结束；未执行的后续动作不会结算。",rect(664,245,390,87),18,MUTED); // 解释组合回合命中后的终止规则。
        if(!s.online && s.series) text(dc,L"系列比分 "+std::to_wstring(s.score[0])+L" : "+std::to_wstring(s.score[1]),rect(664,344,390,40),22,INK,true); // 本地系列赛计分。
        button(dc,Review,L"逐步复盘这一局",rect(664,422,393,53),true); button(dc,Again,s.online && !s.net->isHost()?L"等待房主再来一局":L"再来一局 · 交换先手",rect(664,497,393,51),false,!s.online || (s.net->isHost() && s.net->connected())); history(dc); // 房主控制联网重赛。
    } else if(!has) { text(dc,L"等待朋友准备",rect(664,161,392,48),27,INK,true); text(dc,s.net?s.net->status():L"等待另一位玩家选择起点。",rect(664,248,391,110),18,MUTED); } // 两人未就绪时不显示敌方角色选择。
    else { bool play=s.phase==Phase::Playing; Role role=view.roles[s.viewer]; text(dc,play?L"安排你的回合":s.phase==Phase::Hold?L"查看行动结果":L"等待对方行动…",rect(664,147,393,42),24,INK,true); // 当前行动状态。
        text(dc,heroMode(mode)?roleName(role)+L" · 射程"+std::to_wstring(range(mode,role))+(role==Role::Ink?L" · 冲刺冷却"+std::to_wstring(view.cooldown[s.viewer]):L""):L"三格侦察版 · 射程1",rect(664,193,393,25),15,TEAL,true); // 两张职业地图都显示射程与冷却状态。
        button(dc,Attack,L"攻击",rect(664,235,123,39),s.kind==Kind::Attack,play); button(dc,Move,L"移动",rect(799,235,123,39),s.kind==Kind::Move,play); button(dc,Wait,L"原地等待",rect(934,235,123,39),false,play); // 普通动作选择。
        if(heroMode(mode)) { button(dc,Dash,L"冲刺2步",rect(664,287,123,39),s.kind==Kind::Dash,play&&role==Role::Ink&&view.cooldown[s.viewer]==0); button(dc,Follow,L"指定跟随",rect(799,287,123,39),false,play&&role==Role::Shadow); button(dc,Lead,L"移动带动",rect(934,287,123,39),s.kind==Kind::Lead,play&&role==Role::Shadow); } // 职业对战只显示职业技能，不提供侦察入口。
        else { button(dc,Row,L"侦察一行",rect(664,287,190,39),s.kind==Kind::Row,play); button(dc,Col,L"侦察一列",rect(867,287,190,39),s.kind==Kind::Col,play); } // 三格侦察版保留不限次数的行列侦察。
        button(dc,Undo,L"撤销一步",rect(664,339,190,39),false,play&&!s.plan.steps.empty()); button(dc,Clear,L"清空计划",rect(867,339,190,39),false,play&&!s.plan.steps.empty()); // 编辑尚未提交的完整回合。
        std::wstring plan=s.plan.steps.empty()?L"选择行动方式，再点击左侧目标格。":plannedText(s.plan); text(dc,plan,rect(664,395,392,68),15,MUTED); // 计划只描述操作，不预报尚未产生的结果。
        if(s.phase==Phase::Hold) button(dc,Pass,L"隐藏视角 · 交给下一位",rect(664,479,393,51),true); else button(dc,Commit,play?L"确认本回合  [Enter]":L"等待对方结算",rect(664,479,393,51),true,play&&validate(view,s.plan)); // 确认前不消耗任何资源。
        card(dc,rect(664,548,393,78),PALE,PALE,10); text(dc,s.notice.empty()?privateText(view):s.notice,rect(676,558,370,60),13,TEAL); history(dc); // 私人技能提示和公共记录分开。
    } // 结束各对战阶段。
    footer(dc,modeName(mode)+L" · "+std::to_wstring(has?view.turns:0)+L" / 80 回合  |  行动按顺序加入计划，确认后统一结算；滚轮查看记录。"); // 子动作不会额外增加回合数。
} // 结束对战页面。
void classroom(HDC dc) { // 课堂仍使用三格侦察规则，允许原地等待。
    const auto& lesson=studio::lessons()[s.lesson]; card(dc,rect(36,122,570,599)); card(dc,rect(638,122,446,599)); // 复用三个已有推理情境。
    text(dc,L"推理练习 "+std::to_wstring(s.lesson+1)+L" / 3",rect(64,151,510,42),26,INK,true); text(dc,L"选中所有可能位置，再提交判断。",rect(65,197,511,24),15,MUTED); board(dc,Mode::Scout,{},0,0,false,true); // 课堂只绘制自己的多选结果。
    text(dc,lesson.title,rect(664,153,391,52),24,INK,true); text(dc,lesson.story,rect(664,228,391,115),17,MUTED); text(dc,lesson.task,rect(664,357,391,56),17,INK,true); // 题目情境与任务。
    button(dc,LessonCheck,L"检查我的推理",rect(664,431,393,50),true); card(dc,rect(664,502,393,119),PALE,PALE); text(dc,s.feedback.empty()?L"移动可原地等待，也可上下左右一步；保留所有符合现有线索的位置。":s.feedback,rect(676,514,369,94),15,TEAL); // 新版等待规则的教学提示。
    button(dc,LessonPrev,L"上一题",rect(664,653,121,43),false,s.lesson>0); button(dc,LessonNext,s.lesson==2?L"完成，去实战":L"下一题",rect(800,653,257,43),true,s.completed[s.lesson]); footer(dc,L"课堂使用3×3地图；职业版无侦察，5×5地图需同时考虑角色射程、墙体与湖泊。"); // 明确课堂不套用职业射程或大地图障碍。
} // 结束课堂。
void replayPage(HDC dc) { // 完整复盘只有终局裁判对象可用。
    if(!s.last) return; // 尚无已结束对局时跳过。
    const auto& snap=s.last->snapshots().at(s.replay); int end=int(s.last->events().size()); // 快照零为初始状态。
    card(dc,rect(36,122,570,599)); card(dc,rect(638,122,446,599)); text(dc,L"真相与当时的判断",rect(64,149,513,42),25,INK,true); text(dc,L"青色玩家一，橙色玩家二；浅绿为所选视角候选。",rect(65,196,510,25),14,MUTED); // 解释全知位置与私人候选的区别。
    board(dc,snap.mode,snap.possible[s.replayView],snap.positions[0],snap.positions[1],true,false,0,&snap.map); text(dc,modeName(snap.mode),rect(65,688,511,25),14,MUTED); // 复盘依据该局自身的地图尺寸。
    text(dc,L"第 "+std::to_wstring(s.replay)+L" / "+std::to_wstring(end)+L" 回合",rect(664,155,391,44),26,INK,true); text(dc,s.replay?actionText(s.last->events()[s.replay-1],true):L"双方秘密选择角色与起点。",rect(664,224,391,130),18,MUTED); // 展示实际执行的完整动作，不显示未执行计划。
    button(dc,View0,L"玩家一的推理",rect(664,382,190,43),s.replayView==0); button(dc,View1,L"玩家二的推理",rect(867,382,190,43),s.replayView==1); // 对比两人的候选集合。
    button(dc,ReplayStart,L"起点",rect(664,453,87,43),false,s.replay>0); button(dc,ReplayPrev,L"上回合",rect(762,453,91,43),false,s.replay>0); button(dc,ReplayNext,L"下回合",rect(864,453,92,43),true,s.replay<end); button(dc,ReplayEnd,L"结局",rect(967,453,90,43),false,s.replay<end); // 按完整回合回溯。
    text(dc,heroMode(snap.mode)?roleName(snap.roles[0])+L" 对阵 "+roleName(snap.roles[1])+L"\n冲刺冷却："+std::to_wstring(snap.cooldown[0])+L" / "+std::to_wstring(snap.cooldown[1]):L"移动和等待共享公开提示；侦察信息只代表获得时刻。",rect(664,532,391,76),17,TEAL); // 两张职业地图的复盘都显示角色与历史技能状态。
    button(dc,Export,L"导出完整记录",rect(664,644,190,48)); bool current=s.lastOnline?s.net && s.net->phase()==forest::Phase::Finished && s.net->replay()==s.last:s.game && s.game==s.last; button(dc,ReturnResult,L"返回本局结果",rect(867,644,190,48),true,current); footer(dc,s.notice.empty()?L"完整位置只在结束后公开；受阻跟随会保留原地并继续后续复制。":s.notice); // 保留重赛入口。
} // 结束复盘。
void editor(HDC dc) { // 地图编辑器不显示任何对战秘密位置。
    card(dc,rect(36,122,570,599)); card(dc,rect(638,122,446,599)); text(dc,L"地图 DIY · 5×5",rect(64,146,514,43),25,INK,true); text(dc,L"点击格子添加地形；线形墙工具点击格子边。",rect(65,195,510,26),14,MUTED); // 左侧编辑方式。
    board(dc,Mode::Heroes,{},0,0,false,false,0,&s.editMap); text(dc,L"棕色实线是墙；虚线是可编辑边；蓝色是湖泊。",rect(65,688,516,25),13,MUTED); // 地形图例包含形状与颜色。
    text(dc,L"塑造你的战场",rect(664,151,392,43),25,INK,true); button(dc,BrushGround,L"空地",rect(664,220,190,42),s.brush==Brush::Ground); button(dc,BrushWall,L"方形墙",rect(867,220,190,42),s.brush==Brush::Wall); // 空地允许站立，方墙占满一个格子。
    button(dc,BrushLake,L"湖泊",rect(664,274,190,42),s.brush==Brush::Lake); button(dc,BrushLine,L"线形墙",rect(867,274,190,42),s.brush==Brush::Line); button(dc,BrushErase,L"橡皮擦",rect(664,328,190,42),s.brush==Brush::Erase); button(dc,MapUndo,L"撤销修改",rect(867,328,190,42),false,!s.mapUndo.empty()); // 橡皮擦可清除格子地形或格边墙。
    text(dc,L"墙：阻挡移动和攻击。\n湖泊：不能站人，攻击可穿过。\n全部25格可编辑，至少保留一格空地。",rect(664,389,392,82),16,MUTED); // 简洁明确的地形规则。
    button(dc,MapEmpty,L"全空地图",rect(664,483,190,37)); button(dc,MapReset,L"恢复默认",rect(867,483,190,37)); button(dc,MapSave,L"保存到本地…",rect(664,532,190,42),false,validMap(Mode::Heroes,s.editMap)); button(dc,MapLoad,L"打开地图…",rect(867,532,190,42)); // 保存和打开通过系统文件对话框完成。
    text(dc,s.editorNotice.empty()?L"保存文件后可分享；点击下方按钮应用到对战。":s.editorNotice,rect(664,589,392,49),13,TEAL); button(dc,MapApply,L"使用此地图 · 返回大厅",rect(664,653,393,43),true,validMap(Mode::Heroes,s.editMap)); // 草稿应用后可用于人机、本地与联机。
    footer(dc,L"线形墙再次点击即可移除；空地工具只清除整格地形，清除边墙请用线形墙或橡皮擦。"); // 避免误解格子与墙边是两种独立对象。
} // 结束地图编辑器绘制。
void rememberMap() { if(s.mapUndo.size()>=100) s.mapUndo.erase(s.mapUndo.begin()); s.mapUndo.push_back(s.editMap); s.editorNotice.clear(); } // 最多保存一百步编辑历史。
void changeMapCell(int cell) { if(cell<1 || cell>25 || s.brush==Brush::Line) return; Terrain value=s.brush==Brush::Wall?Terrain::Wall:s.brush==Brush::Lake?Terrain::Lake:Terrain::Ground; if(s.editMap.cells[cell-1]!=value) { rememberMap(); s.editMap.cells[cell-1]=value; } } // 点击格子只修改这一格地形。
void mapFileDialog(bool save) { // 文件选择允许玩家自行命名并选择保存位置。
    if(!s.window) { return; } if(save && !validMap(Mode::Heroes,s.editMap)) { s.editorNotice=L"至少保留一格空地后才能保存。"; return; } // 无窗口检查不打开交互对话框。
    wchar_t executable[32768]{},filename[32768]{}; GetModuleFileNameW(nullptr,executable,32768); auto folder=std::filesystem::path(executable).parent_path()/L"maps"; std::error_code error; std::filesystem::create_directories(folder,error); std::wstring initial=folder.wstring(); // 默认将玩家地图保存到程序旁的地图目录。
    std::wstring chosen=s.editorPath.empty()?(save?L"forest-map.mistmap":L""):s.editorPath.wstring(); lstrcpynW(filename,chosen.c_str(),32768); OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=s.window; dialog.lpstrFile=filename; dialog.nMaxFile=32768; dialog.lpstrInitialDir=initial.c_str(); // 文件名支持中文路径。
    dialog.lpstrFilter=L"雾隐之森地图 (*.mistmap)\0*.mistmap\0所有文件 (*.*)\0*.*\0\0"; dialog.lpstrDefExt=L"mistmap"; dialog.lpstrTitle=save?L"保存自定义地图":L"打开自定义地图"; dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST); // 覆盖已有文件由系统明确提示。
    if(!(save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))) { if(CommDlgExtendedError()) s.editorNotice=L"文件窗口打开失败，请重试。"; return; } // 取消对话框不会改变地图。
    if(save) { if(saveMapFile(filename,s.editMap,&s.editorNotice)) { s.editorPath=filename; s.editorNotice=L"已保存："+s.editorPath.filename().wstring(); } } // 保存失败保持原文件和当前编辑草稿。
    else { Map loaded; if(loadMapFile(filename,loaded,&s.editorNotice)) { rememberMap(); s.editMap=loaded; s.editorPath=filename; s.editorNotice=L"已打开："+s.editorPath.filename().wstring(); } } // 加载成功后也能撤销回原草稿。
} // 结束地图文件选择。
void draw(HDC dc) { s.hits.clear(); fill(dc,rect(0,0,WIDTH,HEIGHT),BG); header(dc); if(s.page==Page::Lobby) lobby(dc); else if(s.page==Page::Network) room(dc); else if(s.page==Page::Battle) battle(dc); else if(s.page==Page::Lesson) classroom(dc); else if(s.page==Page::Editor) editor(dc); else replayPage(dc); } // 绘制前重新生成点击映射。
bool active() { return s.online?s.net && s.net->phase()!=forest::Phase::Idle && s.net->phase()!=forest::Phase::Error && s.net->phase()!=forest::Phase::Finished:s.page==Page::Battle && s.phase!=Phase::Finished; } // 设置阶段也算尚未完成的对局。
bool navigate(Page page) { // 离开当前局需要明确确认，避免误触丢局。
    if(active() && MessageBoxW(s.window,L"离开将结束当前对局；联机时会断开连接。是否离开？",L"离开对局",MB_YESNO|MB_ICONQUESTION)!=IDYES) return false; // 用户决定是否放弃未结束局。
    if(s.window) KillTimer(s.window,1); // 停止本地电脑定时器。
    if(s.net && (active() || page!=Page::Replay)) s.net->close(); // 按导航目标关闭旧连接。
    s.page=page; resetPlan(); return true; // 结束后台电脑或连接，并切换页面。
} // 结束导航。
void prepareRound() { // 本地一局开始前清除秘密数据。
    s.roundMap=selectedMap(); s.online=false; s.game.reset(); s.starts={}; s.roles={}; s.viewer=0; s.setupPlayer=0; s.selectedRole=Role::Sniper; s.page=Page::Battle; s.coverSetup=true; s.phase=s.bot?Phase::Setup:Phase::Cover; resetPlan(); s.historyOffset=0; invalidate(); // 本地双人需先交接再选点。
} // 结束本地准备。
void finishLocal() { s.phase=Phase::Finished; s.last=s.game; s.lastOnline=false; if(s.game->state().winner<2) ++s.score[s.game->state().winner]; resetPlan(); } // 一次终局仅由动作后的流程计分。
void afterLocal() { // 引擎执行后决定交给电脑还是下一玩家。
    resetPlan(); s.historyOffset=0; if(s.game->finished()) finishLocal(); else if(!s.bot) s.phase=Phase::Hold; else if(s.game->state().turn==1) { s.phase=Phase::Waiting; if(s.window) SetTimer(s.window,1,650,nullptr); } else { s.viewer=0; s.phase=Phase::Playing; } invalidate(); // 私人结果在本地双人交接前可先阅读。
} // 结束本地动作同步。
void setupLocal() { // 记录职业与起点，两者在同一确认动作锁定。
    if(!valid(s.mode,s.selected,s.roundMap)) return; // 不接受禁入格或图外起点。
    s.starts[s.setupPlayer]=s.selected; s.roles[s.setupPlayer]=s.selectedRole; resetPlan(); // 只写入当前玩家的选择。
    if(s.bot) { do { s.starts[1]=1+int(s.rng()%(size(s.mode)*size(s.mode))); } while(!valid(s.mode,s.starts[1],s.roundMap)); s.roles[1]=static_cast<Role>(s.rng()%4); } // 电脑独立选角色与位置，允许同格。
    else if(s.setupPlayer==0) { s.viewer=1; s.setupPlayer=1; s.selectedRole=Role::Sniper; s.phase=Phase::Cover; invalidate(); return; } // 第二名玩家在遮罩后选点。
    int first=(s.round-1)%2; s.game=std::make_shared<Game>(s.starts[0],s.starts[1],s.mode,s.roles,first,s.roundMap); s.coverSetup=false; s.viewer=s.bot?0:first; // 每局交替先手。
    if(!s.bot) s.phase=Phase::Cover; else if(first==1) { s.phase=Phase::Waiting; if(s.window) SetTimer(s.window,1,650,nullptr); } else s.phase=Phase::Playing; invalidate(); // 双方准备齐备才正式显示职业信息。
} // 结束本地选点。
void syncNetwork() { // 只同步会话提供的本人信息。
    if(!s.net) return; // 没有网络会话时直接返回。
    s.viewer=s.net->player(); s.mode=s.net->mode(); resetPlan(); s.historyOffset=0; auto phase=s.net->phase(); // 旧的计划不能跨越网络状态继续提交。
    if(auto completed=s.net->takeCompletedReplay()) { s.last=completed; s.lastOnline=true; } // 即使紧接着收到重赛或断线，仍保存刚结束的复盘。
    if(phase==forest::Phase::Setup) { s.page=Page::Battle; s.phase=Phase::Setup; s.starts={}; s.selectedRole=Role::Sniper; } // 每次重赛重新选择职业。
    else if(phase==forest::Phase::Playing || phase==forest::Phase::Waiting) { s.page=Page::Battle; s.phase=phase==forest::Phase::Playing?Phase::Playing:Phase::Waiting; } // 等待权威结果时禁用动作。
    else if(phase==forest::Phase::Finished) { s.phase=Phase::Finished; if(s.page!=Page::Replay) s.page=Page::Battle; } // 赛后可保留当前复盘页面。
    else if(phase==forest::Phase::Error) { s.phase=Phase::Error; if(s.page!=Page::Network && s.page!=Page::Replay) s.page=Page::Battle; } // 失败时显示重连入口。
    invalidate(); // 原生控件也随页面更新。
} // 结束网络状态同步。
void connectNetwork(bool host) { // 从输入框读取明确的地址和端口。
    if(s.page!=Page::Network || !s.net) return; // 连接命令只在房间页面有效。
    wchar_t portText[16]{},addressText[64]{}; GetWindowTextW(s.portEdit,portText,16); GetWindowTextW(s.addressEdit,addressText,64); std::wstring value=portText; // 标准输入框内容限制长度。
    if(value.empty() || value.size()>5 || value.find_first_not_of(L"0123456789")!=std::wstring::npos) { s.notice=L"请输入1到65535之间的端口。"; return; } // 禁止空、非数字与过长端口。
    unsigned int port=0; for(auto c:value) port=port*10+unsigned(c-L'0'); if(!port || port>65535) { s.notice=L"端口范围为1到65535。"; return; } // 安全解析十进制端口。
    std::string address; for(auto c:std::wstring(addressText)) { if(c>127) { s.notice=L"此版连接地址需要填写IPv4。"; return; } address+=static_cast<char>(c); } // 地址参数不允许中文和网址。
    if(host) s.net->host(static_cast<unsigned short>(port),s.mode,selectedMap()); else s.net->join(address,static_cast<unsigned short>(port)); if(s.window) SetTimer(s.window,2,50,nullptr); syncNetwork(); // 异步连接通过短计时器推进。
} // 结束连接入口。
void addStep(Step step) { // 编辑计划时只检查本人的能力与预计位置。
    if(s.page!=Page::Battle || s.phase!=Phase::Playing) return; // 只有本人回合允许编辑。
    auto next=s.plan; next.steps.push_back(step); if(validate(currentView(),next,true,&s.notice)) s.plan=std::move(next); // 错误保留原计划并显示具体原因。
} // 结束计划追加。
std::string utf8(const std::wstring& text) { int length=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr); std::string result(length,'\0'); WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),result.data(),length,nullptr,nullptr); return result; } // 导出采用UTF-8编码。
void exportReplay() { // 赛后导出按回合保存的完整记录。
    if(!s.last || !s.last->finished()) return; // 比赛进行中不能导出全知记录。
    try { wchar_t module[32768]{}; GetModuleFileNameW(nullptr,module,32768); auto folder=std::filesystem::path(module).parent_path()/L"replays"; std::filesystem::create_directories(folder); SYSTEMTIME t{}; GetLocalTime(&t); wchar_t name[100]{}; swprintf(name,100,L"mist-%04u%02u%02u-%02u%02u%02u-%03u.txt",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds); std::ofstream out(folder/name,std::ios::binary); if(!out) throw std::runtime_error("write"); // 自动创建记录目录和唯一文件名。
        out<<"\xEF\xBB\xBF"<<utf8(L"雾隐之森 · 完整赛后复盘\r\n"+modeName(s.last->state().mode)+L"\r\n"); out<<"MAP "<<encodeMap(s.last->state().map)<<"\r\n"; for(std::size_t i=0;i<s.last->snapshots().size();++i) { auto& snap=s.last->snapshots()[i]; std::wstring line=L"回合"+std::to_wstring(i)+L"："+(i?actionText(s.last->events()[i-1],true):L"双方秘密起点")+L"\r\n玩家一："+roleName(snap.roles[0])+L"@"+std::to_wstring(snap.positions[0])+L"；玩家二："+roleName(snap.roles[1])+L"@"+std::to_wstring(snap.positions[1])+L"\r\n"; out<<utf8(line); } out.flush(); if(!out) throw std::runtime_error("write"); s.notice=L"完整记录已保存到程序旁的replays文件夹。"; // 只有已结束对局才能公开完整动作与位置。
    } catch(const std::exception&) { s.notice=L"保存失败，请确认程序所在文件夹可写。"; } // 文件错误不会关闭游戏。
} // 结束导出。
void command(int id) { // 将所有点击统一转换成可验证操作。
    if(id>CellBase && id<=CellBase+25) { int cell=id-CellBase; if(s.page==Page::Editor) changeMapCell(cell); else if(s.page==Page::Lesson && cell<=9) { s.checked[cell-1]=!s.checked[cell-1]; s.feedback.clear(); } else if(s.page==Page::Battle && s.phase==Phase::Setup && valid(s.mode,cell,battleMap())) s.selected=cell; else if(s.page==Page::Battle && s.phase==Phase::Playing) addStep(stepFor(cell,s.mode)); invalidate(); return; } // 棋盘三种用途分开处理。
    if(s.page==Page::Editor && ((id>=HorizontalBase && id<HorizontalBase+30) || (id>=VerticalBase && id<VerticalBase+30))) { if(s.brush==Brush::Line || s.brush==Brush::Erase) { bool horizontal=id<VerticalBase; int index=id-(horizontal?HorizontalBase:VerticalBase); bool value=horizontal?s.editMap.horizontal[index]:s.editMap.vertical[index]; if(s.brush==Brush::Line || value) { rememberMap(); (horizontal?s.editMap.horizontal[index]:s.editMap.vertical[index])=s.brush==Brush::Line?!value:false; } } invalidate(); return; } // 墙边命令与格子命令分开，防止误画邻格。
    if(id>=RoleBase && id<RoleBase+4) { if(s.page==Page::Battle && s.phase==Phase::Setup) s.selectedRole=static_cast<Role>(id-RoleBase); invalidate(); return; } // 职业只能在布置阶段选择。
    switch(id) { // 按钮行为按编号分派。
        case Home: if(navigate(Page::Lobby)) s.online=false; break; case Teach: if(navigate(Page::Lesson)) s.online=false; break; // 离开对战时会先确认。
        case Review: if(s.last && navigate(Page::Replay)) { s.replay=0; s.replayView=0; } break; // 默认从初始局面复盘。
        case Small: s.mode=Mode::Scout; break; case Heroes4: s.mode=Mode::Heroes4; break; case Heroes: s.mode=Mode::Heroes; s.customEnabled=false; break; case Cpu: s.bot=true; break; case Hotseat: s.bot=false; break; // 大厅配置三种地图和本地对战方式。
        case MapEdit: if(navigate(Page::Editor)) { s.online=false; if(!s.editorStarted) { s.editMap=s.customMap; s.editorStarted=true; } } break; // 第一次载入现有五格布局，以后保留尚未应用的草稿。
        case MapUse: s.mode=Mode::Heroes; s.customEnabled=true; break; // 启用最近一次应用的自定义地图。
        case BrushGround: s.brush=Brush::Ground; break; case BrushWall: s.brush=Brush::Wall; break; case BrushLake: s.brush=Brush::Lake; break; case BrushLine: s.brush=Brush::Line; break; case BrushErase: s.brush=Brush::Erase; break; // 编辑工具仅改变后续点击的行为。
        case MapUndo: if(!s.mapUndo.empty()) { s.editMap=s.mapUndo.back(); s.mapUndo.pop_back(); s.editorNotice.clear(); } break; // 恢复最近一步之前的地图。
        case MapEmpty: rememberMap(); s.editMap=Map{}; break; case MapReset: rememberMap(); s.editMap=defaultMap(Mode::Heroes); break; // 两种重置均可撤销。
        case MapSave: mapFileDialog(true); break; case MapLoad: mapFileDialog(false); break; // 打开系统保存和读取窗口。
        case MapApply: if(validMap(Mode::Heroes,s.editMap)) { s.customMap=s.editMap; s.customEnabled=true; s.mode=Mode::Heroes; s.online=false; s.page=Page::Lobby; } else s.editorNotice=L"至少保留一格空地后才能开始对战。"; break; // 对战仅使用已应用的有效地图。
        case Style0: s.style=0; break; case Style1: s.style=1; break; case Style2: s.style=2; break; case Assist: s.assist=!s.assist; break; case Series: s.series=!s.series; break; // 辅助和本地系列设置。
        case Begin: s.round=1; s.score={}; prepareRound(); break; // 新比赛清零比分。
        case Online: if(navigate(Page::Network)) { s.online=true; s.game.reset(); if(!s.net) s.net=std::make_unique<forest::Session>(); s.addresses.clear(); for(auto address:forest::localAddresses()) { if(!s.addresses.empty()) s.addresses+=L"\n"; s.addresses+=std::wstring(address.begin(),address.end()); } } break; // 联网页面一次查询本机地址。
        case NetHost: connectNetwork(true); break; case NetJoin: connectNetwork(false); break; // 发起连接请求。
        case NetCancel: if(s.net) { bool idle=s.net->phase()==forest::Phase::Idle || s.net->phase()==forest::Phase::Error; s.net->close(); s.notice.clear(); if(idle) { s.page=Page::Lobby; s.online=false; } } break; // 取消等待或者回大厅。
        case Ready: if(s.phase==Phase::Cover) { s.phase=s.coverSetup?Phase::Setup:Phase::Playing; resetPlan(); } break; // 本地遮罩确认后才显示视角。
        case Attack: s.kind=Kind::Attack; break; case Move: s.kind=Kind::Move; break; case Row: s.kind=Kind::Row; break; case Col: s.kind=Kind::Col; break; case Dash: s.kind=Kind::Dash; break; case Lead: s.kind=Kind::Lead; break; // 选择下一步的类型，不清除此前计划。
        case Follow: if(s.phase==Phase::Playing) addStep({Kind::Follow,1-s.viewer}); break; // 两名玩家各一个单位，因此直接指定另一人。
        case Wait: if(s.page==Page::Battle && s.phase==Phase::Playing) { int own=currentView().own; if(validate(currentView(),s.plan,true,nullptr,&own)) addStep({Kind::Move,own}); } break; // 在计划当前位置等待，不暴露真实位置。
        case Undo: if(!s.plan.steps.empty()) s.plan.steps.pop_back(); s.notice.clear(); break; case Clear: resetPlan(); break; // 只修改尚未执行的草案。
        case Commit: if(s.page==Page::Battle && s.phase==Phase::Setup) { if(s.online) { int cell=s.selected; if(s.net->chooseStart(cell,s.selectedRole)) { s.starts[s.viewer]=cell; syncNetwork(); } } else setupLocal(); } else if(s.page==Page::Battle && s.phase==Phase::Playing) { if(s.online) { if(s.net->act(s.plan)) syncNetwork(); } else if(s.game && s.game->act(s.plan)) afterLocal(); } break; // 一次提交完整回合，双方执行同一规则校验。
        case Pass: if(!s.online && s.page==Page::Battle && s.phase==Phase::Hold && s.game && !s.game->finished()) { s.viewer=s.game->state().turn; s.phase=Phase::Cover; s.coverSetup=false; resetPlan(); } break; // 行动者看完自己的结果后主动交接。
        case Again: if(s.online) { if(s.net && s.net->newRound()) syncNetwork(); } else { if(!s.series || s.score[0]>=2 || s.score[1]>=2) s.score={}; ++s.round; prepareRound(); } break; // 所有重赛都交换先手，系列赛未完成时保留比分。
        case ReturnResult: if(s.lastOnline && s.net && s.net->replay()==s.last && s.net->phase()==forest::Phase::Finished) { s.online=true; s.page=Page::Battle; s.phase=Phase::Finished; } else if(!s.lastOnline && s.game==s.last) { s.online=false; s.page=Page::Battle; s.phase=Phase::Finished; } break; // 复盘后返回对应对局。
        case ReplayPrev: s.replay=std::max(0,s.replay-1); break; case ReplayNext: if(s.last) s.replay=std::min(int(s.last->events().size()),s.replay+1); break; case ReplayStart: s.replay=0; break; case ReplayEnd: if(s.last) s.replay=int(s.last->events().size()); break; // 回合级时间导航。
        case View0: s.replayView=0; break; case View1: s.replayView=1; break; case Export: exportReplay(); break; // 查看私人候选或导出赛后数据。
        case LessonCheck: { const auto& lesson=studio::lessons()[s.lesson]; if(s.checked==lesson.answer) { s.completed[s.lesson]=true; s.feedback=L"正确！"+lesson.explanation; } else s.feedback=L"还有遗漏或多选。"+lesson.explanation; break; } // 对整套候选集合精确检查。
        case LessonPrev: if(s.lesson>0) --s.lesson; s.checked={}; s.feedback.clear(); break; case LessonNext: if(s.lesson==2) s.page=Page::Lobby; else ++s.lesson; s.checked={}; s.feedback.clear(); break; // 教学进度按钮由当前完成状态控制。
        default: break; // 未知编号不改变游戏。
    } // 结束命令分派。
    invalidate(); // 所有动作结束后更新按钮地图和画面。
} // 结束统一命令入口。
void keyboard(WPARAM key) { int id=0; if(key==VK_RETURN) id=s.page==Page::Lesson?LessonCheck:s.phase==Phase::Cover?Ready:s.phase==Phase::Hold?Pass:Commit; if(s.page==Page::Battle && s.phase==Phase::Playing) { if(key=='A') id=Attack; if(key=='M') id=Move; if(key=='R') id=Row; if(key=='C') id=Col; if(key=='Z') id=Undo; } if((s.page==Page::Lesson || s.mode==Mode::Scout) && key>='1' && key<='9') id=CellBase+int(key-'0'); for(auto hit:s.hits) if(hit.id==id && hit.enabled) { command(id); break; } } // 键盘只触发当前画面启用的操作。
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) { // Windows窗口消息分派。
    switch(message) { // 绘图、计时和输入互不阻塞。
        case WM_CREATE: { s.window=window; s.addressEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"127.0.0.1",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,nullptr,nullptr,nullptr); s.portEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"39091",WS_CHILD|WS_TABSTOP|ES_NUMBER,0,0,1,1,window,nullptr,nullptr,nullptr); for(HWND edit:{s.addressEdit,s.portEdit}) SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE); SendMessageW(s.addressEdit,EM_SETLIMITTEXT,15,0); SendMessageW(s.portEdit,EM_SETLIMITTEXT,5,0); return 0; } // 原生输入框支持复制粘贴。
        case WM_ERASEBKGND: return 1; case WM_SIZE: invalidate(); return 0; // 双缓冲避免背景闪烁。
        case WM_GETMINMAXINFO: { auto* info=reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize={900,670}; return 0; } // 最小窗口确保二十五格可读。
        case WM_PAINT: { PAINTSTRUCT ps{}; HDC screen=BeginPaint(window,&ps); RECT client{}; GetClientRect(window,&client); int w=client.right,h=client.bottom; HDC dc=CreateCompatibleDC(screen); HBITMAP bitmap=CreateCompatibleBitmap(screen,std::max(1,w),std::max(1,h)); HGDIOBJ old=SelectObject(dc,bitmap); fill(dc,client,BG); s.scale=std::min(w/double(WIDTH),h/double(HEIGHT)); s.offsetX=int((w-WIDTH*s.scale)/2); s.offsetY=int((h-HEIGHT*s.scale)/2); SetMapMode(dc,MM_ANISOTROPIC); SetWindowExtEx(dc,WIDTH,HEIGHT,nullptr); SetViewportExtEx(dc,std::max(1,int(WIDTH*s.scale)),std::max(1,int(HEIGHT*s.scale)),nullptr); SetViewportOrgEx(dc,s.offsetX,s.offsetY,nullptr); draw(dc); SetMapMode(dc,MM_TEXT); SetViewportOrgEx(dc,0,0,nullptr); BitBlt(screen,0,0,w,h,dc,0,0,SRCCOPY); SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); EndPaint(window,&ps); return 0; } // 在内存完成整帧后一次显示。
        case WM_LBUTTONUP: { SetFocus(window); POINT p{LONG((GET_X_LPARAM(lParam)-s.offsetX)/std::max(.01,s.scale)),LONG((GET_Y_LPARAM(lParam)-s.offsetY)/std::max(.01,s.scale))}; for(auto it=s.hits.rbegin();it!=s.hits.rend();++it) if(it->enabled && PtInRect(&it->rect,p)) { int id=it->id; command(id); break; } return 0; } // 点击匹配最上层的有效按钮。
        case WM_KEYDOWN: keyboard(wParam); return 0; // 快捷键与鼠标共用校验流程。
        case WM_MOUSEWHEEL: if(s.page==Page::Battle && ((s.online && s.net && s.net->hasView()) || (!s.online && s.game))) { int delta=GET_WHEEL_DELTA_WPARAM(wParam)>0?1:-1; s.historyOffset=std::clamp(s.historyOffset+delta,0,std::max(0,int(events().size())-2)); invalidate(); } return 0; // 滚动仅影响公开历史。
        case WM_TIMER: if(wParam==2) { if(s.online && s.net && s.net->phase()!=forest::Phase::Idle && s.net->poll()) syncNetwork(); } else if(wParam==1) { KillTimer(window,1); if(!s.online && s.bot && s.game && !s.game->finished() && s.game->state().turn==1 && s.page==Page::Battle) { auto a=chooseAction(s.game->view(1),s.style,s.rng); if(s.game->act(a)) afterLocal(); } } return 0; // 网络与电脑行动使用不同短计时器。
        case WM_DESTROY: KillTimer(window,1); KillTimer(window,2); if(s.net) s.net->close(); PostQuitMessage(0); return 0; // 退出时释放连接，不残留房间监听。
        default: return DefWindowProcW(window,message,wParam,lParam); // 默认系统窗口行为。
    } // 结束系统消息处理。
} // 结束窗口过程。
} // 结束界面命名空间。
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) { // 宽字符图形入口，不创建控制台。
    SetProcessDPIAware(); WNDCLASSW wc{}; wc.lpfnWndProc=ui::windowProc; wc.hInstance=instance; wc.lpszClassName=L"MistForestV4"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION); if(!RegisterClassW(&wc)) return 1; // 注册高分屏感知窗口类。
    RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0); int w=std::min(1160,int(area.right-area.left)-40),h=std::min(842,int(area.bottom-area.top)-40); HWND window=CreateWindowExW(0,wc.lpszClassName,L"雾隐之森 · 地图DIY版",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,(area.left+area.right-w)/2,(area.top+area.bottom-h)/2,w,h,nullptr,nullptr,instance,nullptr); if(!window) return 1; ShowWindow(window,show); UpdateWindow(window); MSG msg{}; // 创建居中窗口并显示首帧。
    while(GetMessageW(&msg,nullptr,0,0)>0) { if((GetFocus()==ui::s.addressEdit || GetFocus()==ui::s.portEdit) && IsDialogMessageW(window,&msg)) continue; TranslateMessage(&msg); DispatchMessageW(&msg); } return int(msg.wParam); // 标准消息循环保持界面响应。
} // 结束程序入口。
