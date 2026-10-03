// 雾隐之森：C++17 原生 Windows 图形界面；中文 // 注释说明语法和用途。
#ifndef UNICODE // 如果编译参数未指定 Unicode，则在包含系统头文件前启用。
#define UNICODE // 使系统资源名称与显式 W 版本接口采用宽字符类型。
#endif // 结束 Unicode 宏的兼容处理。
#ifndef NOMINMAX // 防止 Windows 的宏覆盖标准库 min 和 max。
#define NOMINMAX // 定义兼容性宏。
#endif // 结束条件编译。
#include <windows.h> // 引入窗口、鼠标、定时器和 GDI 绘图接口。
#include <windowsx.h> // 引入鼠标消息坐标提取宏。
#include <algorithm> // 使用 min、max、count 等标准算法。
#include <array> // 使用固定长度的九格选择集合。
#include <filesystem> // 使用可执行文件旁的目录保存赛后记录。
#include <fstream> // 使用文件输出流导出 UTF-8 文本。
#include <memory> // 使用 shared_ptr 管理当前与上一局对局。
#include <random> // 使用伪随机引擎决定电脑起点与行动。
#include <string> // 使用 Unicode 宽字符串显示中文。
#include <utility> // 使用 move 将已完成的联网复盘转交给界面保存。
#include <vector> // 保存可点击的界面区域。
#include "game.hpp" // 引入只负责规则与推理的游戏引擎。
#include "lessons.hpp" // 引入三个独立的推理练习。
#include "network.hpp" // 引入房主裁判与受限玩家视图的双人联机接口。
namespace ui { // 命名空间将界面实现与规则引擎分开。
using namespace studio; // 允许直接使用 Game、Action 等引擎名称。
constexpr int WIDTH = 1120, HEIGHT = 780; // 设计画布的逻辑尺寸，窗口缩放时保持比例。
const COLORREF BG=RGB(243,246,245), INK=RGB(23,44,55), MUTED=RGB(104,120,127); // 页面底色、正文与次要文字颜色。
const COLORREF TEAL=RGB(15,117,110), PALE=RGB(225,242,236), CORAL=RGB(204,94,72); // 主色、柔和候选底色与对手标记色。
const COLORREF LINE=RGB(217,227,225), WHITE=RGB(255,255,255), GOLD=RGB(210,154,46); // 边框、卡片与选中状态颜色。
enum class Page { Lobby, Battle, Lesson, Replay, Network, NetBattle }; // 枚举区分本地页面与联网房间、联网对战。
enum class Phase { Cover, Setup, Playing, Thinking, Hold, Finished }; // 定义秘密交接、布置、行动、电脑思考和结束阶段。
enum Id { Home=1, Teach, Review, Begin, Cpu, Hotseat, StyleA, StyleR, StyleD, ScanOn, ScanOff, Assist, Single, Series, Ready, Commit, Attack, Move, Row, Col, Pass, NextRound, Again, LessonCheck, LessonPrev, LessonNext, ReplayPrev, ReplayNext, ReplayStart, ReplayEnd, ViewA, ViewB, Export, ReturnResult, Online, NetHost, NetJoin, NetCancel, NetAgain, CellBase=100 }; // 按钮编号用于把点击转成明确命令。
struct Hit { RECT rect; int id; bool enabled; }; // 保存可点击区域、命令编号和是否可操作。
struct State { // 汇总界面状态，真实对局位置由 Game 保存。
    HWND window=nullptr; Page page=Page::Lobby; Phase phase=Phase::Setup; // 保存窗口和页面阶段。
    bool bot=true, scan=true, assist=true, series=false; Style style=Style::Adaptive; // 默认启用电脑、不限次数侦察与推理辅助。
    std::unique_ptr<forest::Session> net; HWND addressEdit=nullptr,portEdit=nullptr; // 独占联网会话，并保存标准地址输入框的窗口句柄。
    std::wstring addresses; bool networkResult=false; // 保存可分享的网卡地址，并区分当前复盘来自联网还是本地。
    std::shared_ptr<Game> game, last; // 当前游戏和最近完成游戏可同时供复盘使用。
    std::array<std::wstring,2> names{L"玩家一",L"电脑"}, lastNames{L"玩家一",L"电脑"}; // 冻结当前与上一局的显示名称。
    std::array<int,2> starts{0,0}, score{0,0}; int round=1, viewer=0, setupPlayer=0; // 保存起点选择、系列比分和当前私密视角。
    bool coverForSetup=false; int selected=0; Kind kind=Kind::Attack; // 区分交接用途，并保存尚未确认的行动。
    int lesson=0, replay=0, replayView=0, historyOffset=0; // 保存教学页、复盘时间点与公开记录滚动位置。
    std::array<bool,9> checked{}; std::array<bool,3> completed{}; bool checkedCorrect=false; // 教学题的勾选状态和完成标记。
    std::wstring feedback, notice, privateNote; // 当前教学反馈、公开提示和本人的侦察结果。
    std::array<std::wstring,2> privateNotes{}; // 分别保留两位玩家自己的最近一次侦察结果。
    std::vector<Hit> hits; double scale=1.0; int offsetX=0,offsetY=0; // 保存点击区域与显示坐标换算参数。
    std::mt19937 rng{std::random_device{}()}; // 每次运行重新播种；电脑仅获得自己的 View。
} s; // 创建唯一界面状态对象。
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
void syncNetworkControls() { // 按窗口比例定位标准输入框，切换页面时隐藏它们。
    if(!s.window || !s.addressEdit || !s.portEdit) return; // 离屏检查或控件尚未创建时直接返回。
    RECT client{}; GetClientRect(s.window,&client); double scale=std::min(client.right/static_cast<double>(WIDTH),client.bottom/static_cast<double>(HEIGHT)); // 从实际客户区计算控件缩放。
    int x=static_cast<int>((client.right-WIDTH*scale)/2),y=static_cast<int>((client.bottom-HEIGHT*scale)/2); // 计算画布居中偏移。
    MoveWindow(s.portEdit,x+static_cast<int>(64*scale),y+static_cast<int>(320*scale),static_cast<int>(210*scale),static_cast<int>(44*scale),TRUE); // 端口输入框对应左卡片。
    MoveWindow(s.addressEdit,x+static_cast<int>(602*scale),y+static_cast<int>(320*scale),static_cast<int>(450*scale),static_cast<int>(44*scale),TRUE); // 地址输入框对应右卡片。
    bool show=s.page==Page::Network,edit=!s.net || s.net->phase()==forest::Phase::Idle || s.net->phase()==forest::Phase::Error; // 联网期间锁定连接参数。
    ShowWindow(s.portEdit,show?SW_SHOW:SW_HIDE); ShowWindow(s.addressEdit,show?SW_SHOW:SW_HIDE); EnableWindow(s.portEdit,edit); EnableWindow(s.addressEdit,edit); // 同步控件的可见性和编辑状态。
} // 结束标准控件布局。
void invalidate() { s.hits.clear(); syncNetworkControls(); if(s.window) InvalidateRect(s.window,nullptr,FALSE); } // 丢弃旧点击表并刷新控件，避免切换阶段时误触。
bool active() { return s.game && !s.game->finished() && s.page==Page::Battle; } // 查询是否存在正在进行的对局。
int count(const std::array<bool,9>& values) { return static_cast<int>(std::count(values.begin(),values.end(),true)); } // 统计可能位置或选中格子的数量。
std::wstring cells(const std::array<bool,9>& values) { // 将九格布尔集合转换成可读格号。
    std::wstring out; for(int i=0;i<9;++i) if(values[i]) { if(!out.empty()) out+=L"、"; out+=std::to_wstring(i+1); } // 枚举所有为真的格子。
    return out.empty()?L"无":out; // 空集合给出明确文字，避免空白提示。
} // 结束集合格式化。
std::wstring actorName(int actor,bool replay=false) { return (replay?s.lastNames:s.names).at(actor); } // 根据当前或复盘上下文取得玩家名称。
std::wstring publicEvent(const Event& e,bool replay=false) { // 只输出公开行动；移动终点与侦察结果不会出现在这里。
    const auto name=actorName(e.actor,replay); // 取相应对局的玩家名称。
    if(e.action.kind==Kind::Move) return name+L"移动了一格"; // 移动不公开方向或目标格。
    if(e.action.kind==Kind::Attack) return name+L"攻击 "+std::to_wstring(e.action.target)+(e.hit?L" · 命中":L" · 未命中"); // 攻击公开落点和结果。
    return name+L"侦察第 "+std::to_wstring(e.action.target+1)+(e.action.kind==Kind::ScanRow?L" 行":L" 列"); // 侦察区域公开，结果另行私密显示。
} // 结束公开记录格式化。
void header(HDC dc) { // 绘制所有页面共用的导航。
    fill(dc,rect(0,0,WIDTH,96),INK); // 深色导航栏提供稳定的视觉层次。
    text(dc,L"雾隐之森",rect(36,18,250,43),30,WHITE,true); // 使用用户确定的游戏名称。
    text(dc,L"MIST FOREST  /  隐藏信息博弈",rect(38,62,350,23),12,RGB(170,201,198)); // 简短副标题说明游戏主题。
    button(dc,Home,L"对战大厅",rect(625,28,138,42),s.page==Page::Lobby); // 返回大厅按钮。
    button(dc,Teach,L"推理课堂",rect(778,28,138,42),s.page==Page::Lesson); // 打开教学练习按钮。
    button(dc,Review,L"对局复盘",rect(931,28,153,42),s.page==Page::Replay,s.last!=nullptr); // 仅有已结束对局时开放复盘。
} // 结束页头。
void footer(HDC dc,const std::wstring& label) { text(dc,label,rect(38,741,1045,25),13,MUTED); } // 显示页脚的操作帮助与阶段信息。
void lobby(HDC dc) { // 绘制入口与对战选项。
    card(dc,rect(36,122,710,599)); card(dc,rect(768,122,316,599)); // 将配置与规则介绍分成两张卡片。
    text(dc,L"每次出手，都是一条线索。",rect(64,154,650,57),30,INK,true); // 强调本游戏的信息推理主题。
    text(dc,L"看不见对手，仍能读懂局势。移动、攻击，或消耗一回合侦察；侦察不限次数。",rect(65,219,622,55),17,MUTED); // 介绍可反复使用的三种行动。
    text(dc,L"01   选择对手",rect(65,294,610,25),14,TEAL,true); // 对手类型区标题。
    button(dc,Cpu,L"人机练习",rect(65,330,189,44),s.bot); button(dc,Hotseat,L"双人交接",rect(267,330,189,44),!s.bot); button(dc,Online,L"双人联机 →",rect(469,330,192,44)); // 本地对战与独立联网入口。
    text(dc,L"02   电脑风格",rect(65,397,610,25),14,TEAL,true); // 电脑策略配置标题。
    button(dc,StyleA,L"主动进攻",rect(65,430,189,43),s.style==Style::Aggressive,s.bot); // 主动攻击策略。
    button(dc,StyleR,L"隐蔽反击",rect(267,430,189,43),s.style==Style::Reactive,s.bot); // 等待对手攻击的策略。
    button(dc,StyleD,L"灵活换位",rect(469,430,192,43),s.style==Style::Adaptive,s.bot); // 会侦察与调整位置的策略。
    button(dc,ScanOn,L"无限侦察",rect(65,499,141,40),s.scan); button(dc,ScanOff,L"原版",rect(217,499,125,40),!s.scan); // 侦察版不限次数，原版仍可关闭该规则。
    button(dc,Assist,s.assist?L"推理辅助：开":L"推理辅助：关",rect(359,499,302,40),s.assist); // 辅助展示可由玩家关闭。
    button(dc,Single,L"单局练习",rect(65,554,291,40),!s.series); button(dc,Series,L"三局两胜",rect(370,554,291,40),s.series); // 可选单局或累计两胜的系列赛。
    button(dc,Begin,L"选择秘密起点  →",rect(65,629,596,55),true); // 开始配置好的比赛。
    text(dc,L"3 × 3  /  隐藏信息",rect(793,150,265,30),18,TEAL,true); // 右侧规则区标题。
    for(int i=0;i<9;++i) { // 绘制装饰性的九格缩略图，不包含真实对局状态。
        RECT r=rect(797+(i%3)*82,202+(i/3)*64,70,52); card(dc,r,i==4?TEAL:PALE,LINE,12); // 中心格突出显示以形成视觉焦点。
        text(dc,std::to_wstring(i+1),r,20,i==4?WHITE:TEAL,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); // 显示格号。
    } // 缩略图绘制完成。
    text(dc,L"移动",rect(795,416,240,25),17,INK,true); text(dc,L"上下左右一步，可以与对手同格。",rect(795,449,252,48),15,MUTED); // 说明合法移动。
    text(dc,L"攻击 / 侦察",rect(795,512,250,25),17,INK,true); text(dc,L"攻击本格或邻格，命中即胜。\n侦察检查一行或一列，占用一回合。",rect(795,545,252,73),15,MUTED); // 说明两类信息获取行动。
    text(dc,L"每局上限 80 次行动；未决不计胜场。",rect(795,647,253,43),13,MUTED); // 明示本版本控制体验时长的规则。
    footer(dc,L"双人联机各用一台电脑；双人交接共用屏幕。可先到推理课堂练习，再进入实战。"); // 区分远端对战与本地交接。
} // 结束大厅绘制。
void networkLobby(HDC dc) { // 绘制建房、输入朋友地址与连接状态。
    card(dc,rect(36,122,514,599)); card(dc,rect(574,122,510,599)); // 两张卡片分别表示房主与客人。
    bool ready=!s.net || s.net->phase()==forest::Phase::Idle || s.net->phase()==forest::Phase::Error; // 空闲或失败后允许重新连接。
    text(dc,L"创建联机房间",rect(64,151,450,44),27,INK,true); text(dc,L"加入朋友的房间",rect(602,151,450,44),27,INK,true); // 两种连接角色。
    text(dc,L"你是玩家一。把本机地址与端口分享给朋友，对方加入后各自选择秘密起点。",rect(64,214,452,68),17,MUTED); // 说明房主流程。
    text(dc,L"你是玩家二。输入房主的 IPv4 地址，并在左侧填写与房主相同的端口。",rect(602,214,449,68),17,MUTED); // 说明客人流程。
    text(dc,L"房间端口（创建 / 加入共用）",rect(64,285,450,27),15,TEAL,true); text(dc,L"房主地址",rect(602,285,450,27),15,TEAL,true); // 对应原生输入框标签。
    card(dc,rect(64,320,210,44),WHITE,LINE,8); card(dc,rect(602,320,450,44),WHITE,LINE,8); // 离屏预览中也保留输入区边框。
    button(dc,NetHost,L"创建房间 · 等待朋友",rect(64,395,452,52),true,ready); button(dc,NetJoin,L"连接房主",rect(602,395,450,52),true,ready); // 建房和连接互斥，避免覆盖当前会话。
    text(dc,L"可分享的本机地址",rect(64,472,452,27),16,INK,true); text(dc,s.addresses.empty()?L"连接网络后再创建房间。":s.addresses,rect(64,508,452,65),16,TEAL); // 列出联网接口提供的本机 IPv4 地址。
    text(dc,L"房间规则："+std::wstring(s.scan?L"侦察不限次数，每次一回合。":L"原版，关闭侦察。")+L"\n单局对战；结束后房主可再开一局。",rect(64,605,452,74),15,MUTED); // 联网采用房主规则并支持重赛。
    std::wstring status=!s.notice.empty()?s.notice:(s.net?s.net->status():L"输入地址后连接；同机测试可填 127.0.0.1。"); // 优先显示输入校验错误。
    text(dc,status,rect(602,481,450,112),16,TEAL); // 显示等待、失败原因与连接进度。
    button(dc,NetCancel,ready?L"返回大厅":L"取消连接",rect(602,629,450,49),false); // 始终保留明确的取消与恢复路径。
    footer(dc,L"同一局域网可直连；异地可使用虚拟局域网或可达公网地址。房主负责结算，双方各自显示私密视角。"); // 明确地址连接的网络条件。
} // 结束联网入口绘制。
void cover(HDC dc) { // 全屏交接页不绘制任何真实位置、私有结果或候选集合。
    card(dc,rect(218,175,684,495)); text(dc,L"秘密视角交接",rect(265,217,588,54),32,INK,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); // 显示交接主题。
    text(dc,L"请将屏幕交给 "+s.names[s.viewer],rect(265,295,588,55),26,TEAL,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); // 指明下一位可观看者。
    text(dc,L"另一位玩家请暂时回避。\n准备好后，再显示你的棋盘和线索。",rect(298,385,522,85),18,MUTED,false,DT_CENTER|DT_WORDBREAK); // 明确本地双人依赖回避约定。
    button(dc,Ready,L"我已准备好 · 显示我的视角",rect(317,533,486,61),true); // 确认后才绘制秘密信息。
    footer(dc,L"交接期间，角色位置、选择结果和私人侦察记录均已隐藏。 "); // 强调当前屏幕的安全边界。
} // 结束交接页。
RECT cellRect(int cell) { return rect(82+((cell-1)%3)*148,226+((cell-1)/3)*148,136,136); } // 统一九格的绘制与点击坐标。
void marker(HDC dc,RECT r,int player,int offset=0) { // 在一个格子中绘制玩家圆形标记，同格时可错开。
    RECT dot=rect(r.left+44+offset,r.top+43,48,48); card(dc,dot,player==0?TEAL:CORAL,player==0?TEAL:CORAL,48); // 使用颜色区分玩家。
    text(dc,player==0?L"一":L"二",dot,20,WHITE,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); // 圆点内加汉字，避免只靠颜色辨认。
} // 结束角色标记绘制。
void board(HDC dc,const std::array<bool,9>& possible,int own,int other,bool lesson=false,bool reveal=false) { // 绘制棋盘；对战时 other 必须为 0，禁止显示对手。
    for(int cell=1;cell<=9;++cell) { // 依次绘制九格。
        RECT r=cellRect(cell); bool selected=lesson?s.checked[cell-1]:s.selected==cell; // 教学允许多选，对战只保留一个目标。
        if(!lesson && (s.kind==Kind::ScanRow || s.kind==Kind::ScanCol) && s.selected>0) selected=inScan(cell,s.kind,s.kind==Kind::ScanRow?(s.selected-1)/3:(s.selected-1)%3); // 侦察选择高亮整行或整列。
        const bool hint=possible[cell-1]; card(dc,r,selected?RGB(255,245,219):(hint?PALE:RGB(249,250,249)),selected?GOLD:LINE,18,selected?3:1); // 用不同颜色表达选择和推理候选。
        text(dc,std::to_wstring(cell),rect(r.left+13,r.top+9,35,31),19,INK,true); // 左上角显示格号。
        if(hint && !lesson) text(dc,L"可能",rect(r.left+75,r.top+12,49,23),12,TEAL); // 明确候选集合不是概率。
        if(cell==own) marker(dc,r,reveal?0:s.viewer,cell==other?-21:0); // 当前视角只显示自身；复盘显示真实玩家一。
        if(reveal && cell==other) marker(dc,r,1,cell==own?22:0); // 只有复盘和结束状态允许显示对手标记。
        if(lesson && s.checked[cell-1]) text(dc,L"已选",rect(r.left+34,r.top+64,70,31),19,TEAL,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE); // 多选状态有文字反馈。
        bool enabled=lesson || ((s.page==Page::Battle || s.page==Page::NetBattle) && (s.phase==Phase::Setup || s.phase==Phase::Playing)); // 本地与联网仅允许当前可操作阶段选格。
        if(s.page==Page::NetBattle && s.phase==Phase::Playing && s.net) { // 联网合法性只依据自己的受限视图。
            Action a{s.kind,s.kind==Kind::ScanRow?(cell-1)/3:s.kind==Kind::ScanCol?(cell-1)%3:cell}; bool legal=s.net->legal(a); enabled=enabled&&legal; // 转换行列并检查射程与回合。
            if(legal) text(dc,L"可选",rect(r.left+13,r.bottom-28,66,21),12,TEAL); // 同步显示合法目标提示。
        } // 结束联网格子校验。
        if(s.page==Page::Battle && s.phase==Phase::Playing && s.game) { // 对战中标出当前行动方式的合法格子。
            Action a{s.kind,(s.kind==Kind::ScanRow?(cell-1)/3:(s.kind==Kind::ScanCol?(cell-1)%3:cell))}; // 将格子选择转换成格号或行列索引。
            const bool legal=s.game->legal(a); enabled=enabled&&legal; // 不合法区域不接受点击。
            if(legal) text(dc,L"可选",rect(r.left+13,r.bottom-28,66,21),12,TEAL); // 提供轻量的合法行动提示。
        } // 结束合法区域计算。
        s.hits.push_back({r,CellBase+cell,enabled}); // 登记棋盘点击区域。
    } // 九格全部绘制完毕。
} // 结束棋盘绘制。
void history(HDC dc) { // 显示可滚动的公开行动记录，所有私有信息在其他区域显示。
    text(dc,L"公开行动记录",rect(664,538,290,26),16,INK,true); // 记录标题。
    if(!s.game || s.game->events().empty()) { text(dc,L"尚未行动。攻击落点会留在这里。",rect(664,578,380,54),15,MUTED); return; } // 空记录提示。
    const auto& events=s.game->events(); const int n=static_cast<int>(events.size()); // 引用记录，避免复制整局。
    const int end=std::max(0,n-s.historyOffset),start=std::max(0,end-3); // 窗口显示三条，可滚轮查看较早记录。
    for(int i=start;i<end;++i) text(dc,std::to_wstring(i+1)+L"  "+publicEvent(events[i]),rect(664,577+(i-start)*31,386,28),14,MUTED); // 格式化时不会读取秘密移动终点。
    text(dc,L"滚轮查看更早记录",rect(902,540,153,23),11,MUTED); // 简短交互提示。
} // 结束记录面板。
void battle(HDC dc) { // 绘制选起点、对战或结果页面。
    if(s.phase==Phase::Cover) { cover(dc); return; } // 交接页提前返回，避免背后绘制私密数据。
    card(dc,rect(36,122,570,599)); card(dc,rect(638,122,446,599)); // 棋盘与操作面板。
    const bool setup=s.phase==Phase::Setup,done=s.phase==Phase::Finished; // 保存常用状态判断。
    const std::wstring who=setup?s.names[s.setupPlayer]:s.names[s.viewer]; // 起点阶段显示当前设置玩家。
    text(dc,setup?L"选择你的秘密起点":(done?L"本局结束 · 全部位置公开":who+L"的棋盘"),rect(64,146,514,43),25,INK,true); // 明确当前是否公开真相。
    text(dc,setup?L"九个格子都可以选择，同格也合法。":(done?L"进入复盘，查看双方如何一步步推理。":L"圆点是你的位置；浅绿色表示对手仍可能在此。"),rect(65,193,515,28),14,MUTED); // 棋盘图例。
    std::array<bool,9> possible{}; int own=0,other=0; // 缺省不显示任何秘密信息。
    if(!setup && s.game) { // 已建局才读取允许当前页面使用的状态。
        if(done) { auto snap=s.game->snapshots().back(); own=snap.positions[0]; other=snap.positions[1]; } // 结束后显示双方真实位置。
        else { auto view=s.game->view(s.viewer); own=view.own; if(s.assist) possible=view.possible; } // 进行中只读取当前玩家的受限视图。
    } // 完成棋盘数据选择。
    board(dc,possible,own,other,false,done); // 绘制九格与合法目标。
    text(dc,setup?L"点击格子，再确认。另一位玩家请回避。":(s.assist?L"推理辅助已开 · 候选位置不是等概率分布":L"推理辅助已关闭 · 根据记录自行判断"),rect(65,688,519,24),13,MUTED); // 辅助模式状态。
    if(setup) { // 起点确认面板。
        text(dc,who+L" · 布置角色",rect(664,157,393,43),24,INK,true); // 当前玩家名。
        text(dc,L"先选一个格子作为起点。\n开局后，你可以移动、攻击或侦察。侦察不限次数，每次占一回合。",rect(664,229,380,104),18,MUTED); // 起点阶段说明新规则。
        text(dc,s.selected?L"已选择 "+std::to_wstring(s.selected)+L" 号格":L"等待选择…",rect(664,362,380,44),23,TEAL,true); // 只有当前设置玩家能看到的起点选择。
        button(dc,Commit,L"确认秘密起点",rect(664,434,393,58),true,s.selected>0); // 必须选格才可继续。
        text(dc,L"侦察版："+std::wstring(s.scan?L"开启":L"关闭")+L"\n首局玩家一先手；系列赛每局交换先手。",rect(664,548,380,76),15,MUTED); // 显示已选规则。
    } else if(done) { // 本局结束的操作面板。
        const int winner=s.game->winner(); std::wstring result=winner==2?L"本局未决":s.names[winner]+L"获胜"; // winner 为 2 表示达到上限。
        text(dc,result,rect(664,164,393,54),31,TEAL,true); // 结果主标题。
        text(dc,winner==2?L"已达到 80 次行动，未决不增加胜场。":L"攻击命中。现在可以公开双方位置，回看关键选择。",rect(664,237,389,87),18,MUTED); // 解释结束原因。
        if(s.series) text(dc,L"系列比分  "+std::to_wstring(s.score[0])+L" : "+std::to_wstring(s.score[1])+(s.score[0]>=2||s.score[1]>=2?L"  · 已决出胜者":L"  · 先到两胜"),rect(664,334,390,40),20,INK,true); // 三局两胜的比分显示。
        button(dc,Review,L"逐步复盘这一局",rect(664,403,393,56),true); // 开放完整复盘。
        const bool next=s.series && s.score[0]<2 && s.score[1]<2; // 未决允许加赛直到有人取得两胜。
        button(dc,next?NextRound:Again,next?L"继续下一局":L"再来一场",rect(664,480,393,51)); // 继续系列或重置比赛。
        history(dc); // 在结束页保留最后几次公开行动。
    } else { // 普通行动、思考或双人确认结果阶段。
        const auto view=s.game->view(s.viewer); const bool play=s.phase==Phase::Playing; // 保存当前视角与可交互状态。
        text(dc,s.phase==Phase::Thinking?L"电脑正在思考…":(s.phase==Phase::Hold?L"查看行动结果":L"选择这一回合的行动"),rect(664,151,393,42),23,INK,true); // 阶段提示。
        button(dc,Attack,L"攻击  [A]",rect(664,205,190,47),s.kind==Kind::Attack,play); button(dc,Move,L"移动  [M]",rect(867,205,190,47),s.kind==Kind::Move,play); // 两种基础行动。
        button(dc,Row,L"侦察一行  [R]",rect(664,266,190,43),s.kind==Kind::ScanRow,play&&view.scanAvailable); button(dc,Col,L"侦察一列  [C]",rect(867,266,190,43),s.kind==Kind::ScanCol,play&&view.scanAvailable); // 只在原版或非行动回合禁用侦察。
        std::wstring hint=s.kind==Kind::Move?L"点击上下左右邻格，确认后移动。":s.kind==Kind::Attack?L"点击本格或上下左右邻格，确认后攻击。":L"点击该行 / 列中的任意格，确认后侦察。"; // 不同模式的目标选择解释。
        text(dc,hint,rect(664,327,393,44),14,MUTED); // 自动换行避免长说明截断。
        if(s.phase==Phase::Hold) button(dc,Pass,L"隐藏我的视角 · 交给下一位",rect(664,380,393,51),true); // 私人结果被读完后再交接。
        else button(dc,Commit,s.phase==Phase::Thinking?L"等待电脑行动":L"确认行动  [Enter]",rect(664,380,393,51),true,play&&s.selected>0); // 确认按钮使误点格子不会直接行动。
        card(dc,rect(664,450,393,72),PALE,PALE,12); // 信息提示面板。
        text(dc,s.privateNote.empty()?s.notice:s.privateNote,rect(677,460,368,55),14,TEAL); // 本人的侦察历史结果明确标记时间，不当作当前真相。
        history(dc); // 显示公共历史。
    } // 所有对战阶段绘制完成。
    footer(dc,L"第 "+std::to_wstring(s.round)+L" 局  ·  "+(s.game?std::to_wstring(s.game->events().size()):L"0")+L" / 80 次行动   |   选格 1–9 · A 攻击 · M 移动 · R/C 侦察 · Enter 确认"); // 快捷键和时长状态。
} // 结束对战页。
void networkHistory(HDC dc) { // 联机只展示会话提供的脱敏公共事件。
    text(dc,L"公开行动记录",rect(664,538,280,26),16,INK,true); const auto& events=s.net->publicEvents(); // 这些事件不包含对手移动终点与私人侦察结果。
    if(events.empty()) { text(dc,L"双方准备好后开始记录。",rect(664,578,380,40),15,MUTED); return; } // 尚未行动时显示说明。
    int end=std::max(0,static_cast<int>(events.size())-s.historyOffset),start=std::max(0,end-3); // 计算可滚动的三条记录范围。
    for(int i=start;i<end;++i) text(dc,std::to_wstring(i+1)+L"  "+publicEvent(events[i]),rect(664,577+(i-start)*31,386,28),14,MUTED); // 与本地模式使用相同公开文案。
    text(dc,L"滚轮查看更早记录",rect(902,540,153,23),11,MUTED); // 提示历史记录滚动。
} // 结束联机历史面板。
void networkBattle(HDC dc) { // 用受限视图绘制联网局面，无需在客人端创建未结束的完整对局。
    if(!s.net) return; // 会话不存在时避免访问空指针。
    const auto phase=s.net->phase(); // 读取已经校验存在的会话阶段。
    card(dc,rect(36,122,570,599)); card(dc,rect(638,122,446,599)); // 复用棋盘与操作区的尺寸。
    const bool setup=phase==forest::Phase::Setup,done=phase==forest::Phase::Finished,play=phase==forest::Phase::Playing; // 保存常用的联网阶段判断。
    auto replay=s.net->replay(); std::array<bool,9> possible{}; int own=0,other=0; // 只有终局对象可以提供全局位置。
    if(done && replay) { const auto& snap=replay->snapshots().back(); own=snap.positions[0]; other=snap.positions[1]; } // 终局开放真实位置。
    else if(s.net->hasView()) { const auto view=s.net->view(); own=view.own; if(s.assist) possible=view.possible; } // 进行中只读取本人信息。
    else if(s.net->hasStart()) own=s.starts[s.viewer]; // 等待另一人布置时只显示自己已提交的起点。
    text(dc,done?L"本局结束 · 全部位置公开":setup?L"选择你的秘密起点":s.names[s.viewer]+L"的棋盘",rect(64,146,514,43),25,INK,true); // 当前视角标题。
    text(dc,setup?L"双方各自选格；允许与对手位于同一格。":L"每台电脑只显示自己的对战视角。",rect(65,193,515,28),14,MUTED); // 说明双机私密视角。
    board(dc,possible,own,other,false,done); // 使用同一棋盘绘图与联机合法性校验。
    text(dc,s.assist?L"推理辅助已开 · 候选位置不是等概率分布":L"推理辅助已关 · 根据公开记录自行判断",rect(65,688,519,24),13,MUTED); // 本机辅助设置不影响另一台电脑。
    if(phase==forest::Phase::Error) { // 网络错误中止本局，不允许继续提交动作。
        text(dc,L"连接已中断",rect(664,160,390,44),27,CORAL,true); text(dc,s.net->status(),rect(664,240,390,130),17,MUTED); // 显示会话返回的具体失败原因。
        button(dc,Online,L"返回联机房间",rect(664,406,393,53),true); text(dc,L"可重新创建或加入房间。未结束的对局不生成完整复盘。",rect(664,492,390,91),16,MUTED); // 提供重新连接入口。
    } else if(setup || !s.net->hasView()) { // 尚未取得比赛视图时，只处理秘密起点或等待。
        text(dc,setup?L"布置你的角色":L"起点已就绪",rect(664,155,390,48),26,INK,true); // 区分可选格与等待朋友。
        text(dc,setup?L"点击格子并确认。你的起点不会显示在朋友的游戏画面中。":L"等待朋友确认起点，双方准备好后自动开局。",rect(664,231,390,106),18,MUTED); // 给出设置流程反馈。
        text(dc,setup && s.selected?L"已选择 "+std::to_wstring(s.selected)+L" 号格":s.net->status(),rect(664,348,390,64),18,TEAL); // 显示选择或当前等待状态。
        button(dc,Commit,setup?L"确认秘密起点":L"等待朋友准备",rect(664,432,393,53),true,setup && s.selected>0); // 提交后禁用，防止改动已准备起点。
        text(dc,s.net->scanEnabled()?L"侦察不限次数，每次消耗一回合。\n每局 80 次行动；再来一局时交换先手。":L"房主选择原版，关闭侦察。\n每局 80 次行动；再来一局时交换先手。",rect(664,540,390,95),16,MUTED); // 房间规则以房主配置为准。
    } else if(done) { // 结束后双方都可以立即查看完整复盘。
        int winner=s.net->winner(); text(dc,winner==2?L"本局未决":s.names[winner]+L"获胜",rect(664,160,390,55),29,TEAL,true); // 显示最终结果。
        text(dc,winner==2?L"达到 80 次行动，双方未分胜负。":L"攻击命中。现在可以回看真实位置与双方推理。",rect(664,239,390,95),18,MUTED); // 说明终局原因。
        button(dc,Review,L"逐步复盘这一局",rect(664,377,393,53),true,replay!=nullptr); // 复用已有赛后教学功能。
        button(dc,NetAgain,s.net->isHost()?L"再来一局 · 交换先手":L"等待房主开始下一局",rect(664,450,393,51),false,s.net->isHost() && s.net->connected()); // 重赛由房主发起，双方重新选位置。
        networkHistory(dc); // 结束页仍显示公开记录。
    } else { // 已经开局，根据回合决定能否操作。
        const auto view=s.net->view(); text(dc,play?L"轮到你行动":L"等待朋友行动…",rect(664,151,393,42),24,INK,true); // 清晰区分谁在行动。
        button(dc,Attack,L"攻击  [A]",rect(664,205,190,47),s.kind==Kind::Attack,play); button(dc,Move,L"移动  [M]",rect(867,205,190,47),s.kind==Kind::Move,play); // 基础行动按钮。
        button(dc,Row,L"侦察一行  [R]",rect(664,266,190,43),s.kind==Kind::ScanRow,play&&view.scanAvailable); button(dc,Col,L"侦察一列  [C]",rect(867,266,190,43),s.kind==Kind::ScanCol,play&&view.scanAvailable); // 侦察开启后不随使用次数禁用。
        text(dc,s.kind==Kind::Move?L"移动到上下左右邻格。":s.kind==Kind::Attack?L"攻击本格或上下左右邻格。":L"选择一行或一列；每次侦察占一回合。",rect(664,327,393,44),14,MUTED); // 当前行动提示。
        button(dc,Commit,play?L"确认行动  [Enter]":L"等待对方 / 房主确认",rect(664,380,393,51),true,play&&s.selected>0); // 网络提交后禁用，直到新的回合到达。
        card(dc,rect(664,450,393,72),PALE,PALE,12); const auto& note=s.net->privateNote(); // 取得自己的最新侦察信息。
        text(dc,!s.notice.empty()?s.notice:note.empty()?s.net->status():note,rect(677,460,368,55),14,TEAL); // 结果带时间，不误作持续追踪。
        networkHistory(dc); // 仅展示双方共有的信息。
    } // 结束所有联网阶段分支。
    footer(dc,L"双人联机  ·  "+std::to_wstring(s.net->actionCount())+L" / 80 次行动   |   选格 1–9 · A 攻击 · M 移动 · R/C 侦察 · Enter 确认"); // 联机专属页脚。
} // 结束联网对战页面。
void classroom(HDC dc) { // 绘制交互式教学题。
    const auto& lesson=lessons()[s.lesson]; card(dc,rect(36,122,570,599)); card(dc,rect(638,122,446,599)); // 获取当前关卡并创建双栏布局。
    text(dc,L"练习 "+std::to_wstring(s.lesson+1)+L" / 3",rect(64,147,480,40),25,INK,true); // 显示进度。
    text(dc,L"点选所有可能格子，再提交你的判断。",rect(65,192,510,26),15,MUTED); // 多选提示。
    std::array<bool,9> none{}; board(dc,none,0,0,true); // 题目棋盘只显示玩家选择，不提前展示答案。
    text(dc,L"已选择："+cells(s.checked),rect(65,688,510,24),14,TEAL); // 文字列出勾选集合。
    text(dc,lesson.title,rect(664,152,390,52),24,INK,true); // 当前教学主题。
    text(dc,lesson.story,rect(664,221,388,107),17,MUTED); // 公开的行动情境。
    text(dc,lesson.task,rect(664,343,389,54),17,INK,true); // 需要用户回答的问题。
    button(dc,LessonCheck,L"检查我的推理",rect(664,416,393,52),true); // 精确集合匹配，不只判断选中部分是否正确。
    card(dc,rect(664,488,393,133),s.checkedCorrect?PALE:RGB(246,248,247),LINE,12); // 用反馈区解释错误或成功原因。
    text(dc,s.feedback.empty()?L"提示：移动只能上下左右一步。把所有符合线索的情况都保留下来。":s.feedback,rect(679,503,363,103),15,s.checkedCorrect?TEAL:MUTED); // 解释区保持足够换行空间。
    button(dc,LessonPrev,L"上一题",rect(664,649,119,43),false,s.lesson>0); // 返回上一题。
    button(dc,LessonNext,s.lesson==2?L"完成，去实战":L"下一题 →",rect(798,649,259,43),true,s.completed[s.lesson]); // 首次答对后才能前进。
    footer(dc,L"推理课堂   |   三道题分别练习攻击范围、移动后的集合更新、结合未命中信息排除位置。"); // 教学目标不声称已验证的教育效果。
} // 结束教学页。
std::wstring exactEvent(const Event& e) { // 仅在结束后的复盘里描述当时的真实行动。
    std::wstring out=publicEvent(e,true); // 先包含公开部分。
    if(e.action.kind==Kind::Move) out+=L"，真实终点："+std::to_wstring(e.action.target); // 赛后允许公开移动终点。
    if(e.action.kind==Kind::ScanRow || e.action.kind==Kind::ScanCol) out+=e.scanPositive?L"，当时有对手":L"，当时没有对手"; // 赛后可展示侦察结果。
    return out; // 返回完整行动说明。
} // 结束赛后行动描述。
void replayPage(HDC dc) { // 显示已结束游戏的逐步状态。
    if(!s.last) return; // 没有结束的游戏时不访问任何快照。
    const auto& snap=s.last->snapshots().at(s.replay); const int end=static_cast<int>(s.last->snapshots().size())-1; // 快照 0 为初始状态，每次有效行动再加一个。
    card(dc,rect(36,122,570,599)); card(dc,rect(638,122,446,599)); // 创建复盘双栏。
    text(dc,L"真相与当时的判断",rect(64,147,510,40),25,INK,true); text(dc,L"青色为玩家一，橙色为玩家二；浅绿是所选视角的候选集合。",rect(65,192,511,28),13,MUTED); // 区分真实位置与推理范围。
    board(dc,snap.possible[s.replayView],snap.positions[0],snap.positions[1],false,true); // 对已结束对局展示真相。
    text(dc,L"当时的候选："+cells(snap.possible[s.replayView]),rect(65,687,510,25),14,TEAL); // 显示具体集合供课堂解释。
    text(dc,L"第 "+std::to_wstring(s.replay)+L" / "+std::to_wstring(end)+L" 次行动",rect(664,153,390,46),26,INK,true); // 时间点指示。
    text(dc,s.replay==0?L"双方秘密选择起点，尚未获得任何行动线索。":exactEvent(s.last->events()[s.replay-1]),rect(664,219,390,82),18,MUTED); // 解释进入当前快照的动作。
    text(dc,L"查看谁当时的推理？",rect(664,321,390,27),15,INK,true); // 视角切换说明。
    button(dc,ViewA,s.lastNames[0],rect(664,357,190,42),s.replayView==0); button(dc,ViewB,s.lastNames[1],rect(867,357,190,42),s.replayView==1); // 两人的信息集合不同。
    button(dc,ReplayStart,L"起点",rect(664,424,87,45),false,s.replay>0); button(dc,ReplayPrev,L"上一步",rect(762,424,91,45),false,s.replay>0); // 向前回溯。
    button(dc,ReplayNext,L"下一步",rect(864,424,92,45),true,s.replay<end); button(dc,ReplayEnd,L"结局",rect(967,424,90,45),false,s.replay<end); // 向后推进。
    card(dc,rect(664,492,393,118),PALE,PALE,12); // 推理解释面板。
    std::wstring reason=L"初始九格皆有可能。位置集合只使用当时已经得到的信息。"; // 初始信息状态。
    if(s.replay>0) { // 根据动作类型提供可解释更新规则。
        const auto& e=s.last->events()[s.replay-1]; // 引用产生该状态的事件。
        if(e.actor!=s.replayView) reason=e.action.kind==Kind::Move?L"对手移动：将原候选集合扩展为上下左右一步能到达的格子。":e.action.kind==Kind::Attack?L"对手攻击：其位置必在目标格或上下左右邻格，与原候选集合取交集。":L"对手侦察：这一回合没有移动，也没有暴露自身所在范围。"; // 观察别人的动作。
        else reason=e.action.kind==Kind::Attack?(e.hit?L"命中，游戏结束。":L"自己攻击未命中：排除刚刚攻击的目标格。"):(e.action.kind==Kind::Move?L"自己移动不会改变对手的位置候选，但会改变自己的攻击范围。":L"自己的侦察结果：保留有对手的区域，或排除没有对手的区域。信息对应侦察当时。 "); // 自身动作提供的证据。
    } // 完成解释选择。
    text(dc,reason,rect(679,507,362,85),15,TEAL); // 展示当前更新的具体含义。
    button(dc,Export,L"导出完整记录",rect(664,644,190,47)); // 按需导出，比赛中不提供这个按钮。
    bool result=s.networkResult?(s.net && s.net->phase()==forest::Phase::Finished && s.net->replay()==s.last):(s.game && s.game==s.last && s.game->finished()); // 只有当前结束的同一局可以返回结果页。
    button(dc,ReturnResult,L"返回本局结果",rect(867,644,190,47),true,result); // 复盘后返回本地比分或联网重赛入口。
    footer(dc,s.notice.empty()?L"赛后复盘公开双方位置；复盘过程中不允许继续操作已结束的对局。":s.notice); // 显示导出状态或默认提示。
} // 结束复盘页面。
void draw(HDC dc) { // 所有图形通过统一入口绘制。
    s.hits.clear(); fill(dc,rect(0,0,WIDTH,HEIGHT),BG); header(dc); // 清空旧命中区域，重新生成当前画面的点击地图。
    if(s.page==Page::Lobby) lobby(dc); else if(s.page==Page::Battle) battle(dc); else if(s.page==Page::Lesson) classroom(dc); else if(s.page==Page::Network) networkLobby(dc); else if(s.page==Page::NetBattle) networkBattle(dc); else replayPage(dc); // 按页面枚举派发本地、联机和学习绘制。
} // 结束画面入口。
void prepareRound() { // 准备下一局，不在设置阶段创建对局对象以免泄露未选择的位置。
    KillTimer(s.window,1); s.game.reset(); s.networkResult=false; s.starts={0,0}; s.setupPlayer=0; s.viewer=0; s.selected=0; s.kind=Kind::Attack; // 清空回合私密数据并切换为本地对局。
    s.privateNotes={L"",L""}; s.privateNote.clear(); s.notice=L"选择一次行动，获得新的线索。"; s.historyOffset=0; // 初始化说明和记录位置。
    s.names={L"玩家一",s.bot?L"电脑":L"玩家二"}; s.page=Page::Battle; s.coverForSetup=true; s.phase=s.bot?Phase::Setup:Phase::Cover; // 双人选起点前先遮蔽屏幕。
    invalidate(); // 显示新的起点选择或交接页。
} // 结束准备。
void finish() { // 记录结束后的比赛状态，开放复盘。
    s.phase=Phase::Finished; s.selected=0; s.last=s.game; s.lastNames=s.names; s.privateNote.clear(); // 仅已结束游戏成为可复盘对象。
    if(s.game->winner()<2) ++s.score[s.game->winner()]; // 未决不累计胜场。
} // 结束胜负处理。
void afterAction(int actor) { // 在引擎执行成功后同步界面。
    const auto& e=s.game->events().back(); s.notice=publicEvent(e); s.selected=0; s.historyOffset=0; // 重置目标选择，并更新公开提示。
    if(e.action.kind==Kind::ScanRow || e.action.kind==Kind::ScanCol) s.privateNotes[actor]=L"第 "+std::to_wstring(s.game->events().size())+L" 次行动侦察结果："+(e.scanPositive?L"该区域当时有对手。":L"该区域当时没有对手。")+L" 对手移动后需重新推理。"; // 私密结果带时间，避免当成持续雷达。
    s.privateNote=s.privateNotes[s.viewer]; // 只取当前玩家自己的结果。
    if(s.game->finished()) finish(); // 命中或达到上限则结束。
    else if(!s.bot) s.phase=Phase::Hold; // 双人模式先让行动者阅读结果，随后主动遮蔽交接。
    else if(s.game->turn()==1) { s.phase=Phase::Thinking; if(s.window) SetTimer(s.window,1,700,nullptr); } // 延迟少许显示电脑回合，不阻塞窗口消息循环。
    else { s.phase=Phase::Playing; s.viewer=0; s.kind=Kind::Attack; } // 电脑结束后交还给人类玩家。
    invalidate(); // 更新操作区域与候选集合。
} // 结束状态同步。
void setupCommit() { // 确认一名玩家的秘密起点。
    if(s.selected<1) return; // 尚未选择格子时不进行布置。
    s.starts[s.setupPlayer]=s.selected; s.selected=0; // 保存已选格子并清空显示选择。
    if(s.bot) s.starts[1]=std::uniform_int_distribution<int>(1,9)(s.rng); // 电脑独立随机选择起点，允许与玩家同格。
    else if(s.setupPlayer==0) { s.setupPlayer=1; s.viewer=1; s.coverForSetup=true; s.phase=Phase::Cover; invalidate(); return; } // 双人模式转交第二名玩家设置。
    const int first=(s.round-1)%2; s.game=std::make_shared<Game>(s.starts[0],s.starts[1],s.scan,first); // 每局交换先手，构造真实对局。
    s.viewer=s.bot?0:first; s.coverForSetup=false; // 人机始终显示人类视角，双人显示本局先手。
    if(!s.bot) s.phase=Phase::Cover; // 双人开战前遮蔽一次，确保先手玩家得到正确视角。
    else if(first==1) { s.phase=Phase::Thinking; if(s.window) SetTimer(s.window,1,700,nullptr); } else s.phase=Phase::Playing; // 电脑先手时自动触发。
    invalidate(); // 显示开战状态。
} // 结束起点确认。
std::string utf8(const std::wstring& w) { // 将 Windows UTF-16 字符串转换为 UTF-8，供导出文本使用。
    int size=WideCharToMultiByte(CP_UTF8,0,w.c_str(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr); std::string out(size,'\0'); // 先计算需要的字节数。
    WideCharToMultiByte(CP_UTF8,0,w.c_str(),static_cast<int>(w.size()),out.data(),size,nullptr,nullptr); return out; // 执行编码转换并返回字节串。
} // 结束编码转换。
void exportReplay() { // 将最近结束的一局保存为便于报告使用的文本。
    if(!s.last || !s.last->finished()) return; // 强制赛后导出，避免进行中泄露位置。
    try { // 捕获路径与文件写入异常，显示可理解的提示。
        wchar_t module[32768]{}; GetModuleFileNameW(nullptr,module,32768); auto dir=std::filesystem::path(module).parent_path()/L"replays"; std::filesystem::create_directories(dir); // 在程序旁创建记录目录。
        SYSTEMTIME time{}; GetLocalTime(&time); wchar_t filename[128]{}; swprintf(filename,128,L"replay-%04u%02u%02u-%02u%02u%02u-%03u.txt",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds); // 时间戳避免覆盖现有记录。
        std::ofstream file(dir/filename,std::ios::binary); if(!file) throw std::runtime_error("write"); // 打开输出文件并检查是否成功。
        file<<"\xEF\xBB\xBF"<<utf8(L"雾隐之森 · 赛后完整记录\r\n本文件公开双方位置，仅在对局结束后生成。\r\n"); // UTF-8 BOM 兼容常见文本编辑器。
        for(std::size_t i=0;i<s.last->snapshots().size();++i) { // 按实际行动顺序输出每个快照。
            const auto& snap=s.last->snapshots()[i]; std::wstring line=L"\r\n步骤 "+std::to_wstring(i)+L"："+(i?exactEvent(s.last->events()[i-1]):L"秘密起点"); // 当前动作或初始状态。
            line+=L"\r\n玩家一位置："+std::to_wstring(snap.positions[0])+L"；玩家二位置："+std::to_wstring(snap.positions[1]); // 赛后真实位置。
            line+=L"\r\n玩家一候选："+cells(snap.possible[0])+L"\r\n玩家二候选："+cells(snap.possible[1])+L"\r\n"; file<<utf8(line); // 保存当时的双方推理集合。
        } // 所有快照输出完毕。
        file.flush(); if(!file) throw std::runtime_error("write"); s.notice=L"已保存到程序旁的 replays 文件夹："+std::wstring(filename); // 写入成功后提示路径。
    } catch(const std::exception&) { s.notice=L"记录保存失败，请确认程序所在文件夹允许写入。"; } // 不因导出失败退出应用。
} // 结束导出功能。
bool navigate(Page page) { // 离开正在进行的对局时明确确认，防止无意丢失本局。
    bool online=s.net && s.net->phase()!=forest::Phase::Idle && s.net->phase()!=forest::Phase::Error && s.net->phase()!=forest::Phase::Finished; // 等待连接和正在进行的联网局都算活动会话。
    if((active() || online) && MessageBoxW(s.window,L"离开会结束当前对局；联机时会断开连接。未结束的对局不计胜负。是否离开？",L"离开当前对局",MB_YESNO|MB_ICONQUESTION)!=IDYES) return false; // 由玩家决定是否放弃当前比赛。
    if(active()) { KillTimer(s.window,1); s.game.reset(); } // 停止被放弃的本地比赛与电脑计时。
    if(s.net && (online || page!=Page::Replay)) s.net->close(); // 只有终局进入复盘时保留连接，以便稍后重赛。
    s.page=page; s.selected=0; s.notice.clear(); return true; // 新页面不继承目标选择与旧操作提示。
} // 结束导航处理。
void syncNetworkState() { // 把网络会话阶段转换为界面阶段，真实位置始终留在裁判内部。
    if(!s.net) return; // 尚未建立会话时无需同步。
    const auto phase=s.net->phase(); s.notice.clear(); s.selected=0; s.historyOffset=0; // 新状态到达后取消旧选择与错误信息。
    s.viewer=s.net->player(); s.setupPlayer=s.viewer; s.names={L"玩家一",L"玩家二"}; // 玩家编号由建房或加入确定。
    if(auto completed=s.net->takeCompletedReplay()) { s.last=std::move(completed); s.lastNames=s.names; s.networkResult=true; } // 独立接收终局通知，即使同次轮询又重赛或断线也保留复盘。
    if(phase==forest::Phase::Setup || phase==forest::Phase::Playing || phase==forest::Phase::Waiting) { // 比赛开始、换人或下一局就绪。
        s.page=Page::NetBattle; s.phase=phase==forest::Phase::Setup?Phase::Setup:phase==forest::Phase::Playing?Phase::Playing:Phase::Thinking; s.kind=Kind::Attack; // 联网无需共用屏幕交接。
        if(phase==forest::Phase::Setup) s.starts={0,0}; // 新一局清除旧的起点显示。
    } else if(phase==forest::Phase::Finished) { // 只有完整终局才能进入公开复盘。
        s.last=s.net->replay(); s.lastNames=s.names; s.networkResult=true; s.phase=Phase::Finished; if(s.page!=Page::Replay) s.page=Page::NetBattle; // 双方获得相同终局回放。
    } else if(phase==forest::Phase::Error) { // 连接失败或中途断线。
        s.phase=Phase::Thinking; if(s.page!=Page::Network && s.page!=Page::Replay) s.page=Page::NetBattle; // 保留已结束的复盘，未结束局显示断线页。
    } // 空闲、监听和连接阶段留在房间页面。
    invalidate(); // 刷新按钮、文本和原生输入框。
} // 结束网络状态同步。
void connectNetwork(bool host) { // 校验输入后异步建房或连接，不阻塞界面等待另一人。
    if(s.page!=Page::Network || !s.net) return; // 连接命令只属于房间页面。
    wchar_t portText[16]{},addressText[64]{}; GetWindowTextW(s.portEdit,portText,16); GetWindowTextW(s.addressEdit,addressText,64); // 读取标准输入框文本。
    std::wstring portValue=portText; unsigned int port=0; // 手动解析受限长度的端口，避免异常中断窗口回调。
    if(portValue.empty() || portValue.size()>5 || portValue.find_first_not_of(L"0123456789")!=std::wstring::npos) { s.notice=L"请输入 1 到 65535 之间的端口。"; return; } // 拒绝空白、非数字与过长输入。
    for(wchar_t c:portValue) port=port*10+static_cast<unsigned int>(c-L'0'); // 十进制累加，五位数不会溢出。
    if(port==0 || port>65535) { s.notice=L"端口范围为 1 到 65535。"; return; } // 零号端口与越界端口不能分享。
    std::string address; for(wchar_t c:std::wstring(addressText)) { if(c>127) { s.notice=L"请输入房主的 IPv4 地址，例如 192.168.1.8。"; return; } address.push_back(static_cast<char>(c)); } // 地址使用 ASCII，实际 IPv4 合法性由网络模块检查。
    s.notice.clear(); if(host) s.net->host(static_cast<unsigned short>(port),s.scan); else s.net->join(address,static_cast<unsigned short>(port)); // 启动非阻塞连接流程。
    if(s.window) SetTimer(s.window,2,50,nullptr); // 通过短计时器收发状态，不创建后台阻塞操作。
    syncNetworkState(); // 立即显示监听或连接阶段。
} // 结束连接输入处理。
void command(int id) { // 将鼠标或键盘产生的命令映射到界面行为。
    if(id>=CellBase+1 && id<=CellBase+9) { // 九个格子共用一段选择逻辑。
        int cell=id-CellBase; if(s.page==Page::Lesson) { s.checked[cell-1]=!s.checked[cell-1]; s.feedback.clear(); s.checkedCorrect=false; } // 教学格子可重复点选取消。
        else if((s.page==Page::Battle || s.page==Page::NetBattle) && (s.phase==Phase::Setup || s.phase==Phase::Playing)) s.selected=cell; // 两类对战都只在可操作阶段改变选择。
        invalidate(); return; // 选格后刷新页面并结束当前命令。
    } // 结束选格命令分支。
    switch(id) { // 按命令编号执行唯一对应的操作。
        case Home: navigate(Page::Lobby); break; // 返回大厅，可保留已结束的复盘。
        case Teach: navigate(Page::Lesson); break; // 打开独立课堂练习。
        case Review: if(s.last && navigate(Page::Replay)) { s.replay=0; s.replayView=0; } break; // 从初始快照开始复盘。
        case Online: if(navigate(Page::Network)) { s.game.reset(); if(!s.net) s.net=std::make_unique<forest::Session>(); s.addresses.clear(); for(const auto& address:forest::localAddresses()) { if(!s.addresses.empty()) s.addresses+=L"\n"; s.addresses+=std::wstring(address.begin(),address.end()); } } break; // 打开独立联网页面并列出本机接口。
        case NetHost: connectNetwork(true); break; case NetJoin: connectNetwork(false); break; // 分别创建和加入朋友的房间。
        case NetCancel: if(s.net) { bool idle=s.net->phase()==forest::Phase::Idle || s.net->phase()==forest::Phase::Error; s.net->close(); s.notice.clear(); if(idle) s.page=Page::Lobby; } break; // 正在连接时取消，空闲时返回大厅。
        case NetAgain: if(s.page==Page::NetBattle && s.net && s.net->newRound()) syncNetworkState(); break; // 房主结束后开始新一局，客人按钮禁用。
        case Cpu: s.bot=true; break; case Hotseat: s.bot=false; break; // 设置对手类型。
        case StyleA: s.style=Style::Aggressive; break; case StyleR: s.style=Style::Reactive; break; case StyleD: s.style=Style::Adaptive; break; // 设置电脑策略。
        case ScanOn: s.scan=true; break; case ScanOff: s.scan=false; break; case Assist: s.assist=!s.assist; break; // 设置侦察与推理辅助。
        case Single: s.series=false; break; case Series: s.series=true; break; // 设置单局或系列赛。
        case Begin: case Again: s.round=1; s.score={0,0}; prepareRound(); break; // 新比赛清空比分。
        case NextRound: ++s.round; prepareRound(); break; // 下一局保留比分并交换先手。
        case Ready: s.phase=s.coverForSetup?Phase::Setup:Phase::Playing; s.privateNote=s.privateNotes[s.viewer]; s.kind=Kind::Attack; break; // 交接确认后显示自己的视角。
        case Attack: s.kind=Kind::Attack; s.selected=0; break; case Move: s.kind=Kind::Move; s.selected=0; break; // 切换行动时清空旧目标。
        case Row: s.kind=Kind::ScanRow; s.selected=0; break; case Col: s.kind=Kind::ScanCol; s.selected=0; break; // 行与列侦察采用同样的格子选择。
        case Commit: // 起点或行动确认按钮。
            if(s.page==Page::NetBattle && s.net && s.selected>0) { // 联机操作交给会话校验并发送给裁判。
                bool accepted=false; if(s.net->phase()==forest::Phase::Setup) { int chosen=s.selected; accepted=s.net->chooseStart(chosen); if(accepted) s.starts[s.viewer]=chosen; } // 私密起点提交后等待对方。
                else if(s.net->phase()==forest::Phase::Playing) accepted=s.net->act({s.kind,s.kind==Kind::ScanRow?(s.selected-1)/3:s.kind==Kind::ScanCol?(s.selected-1)%3:s.selected}); // 只在本人的行动回合提交动作。
                if(accepted) syncNetworkState(); else s.notice=L"该行动当前不可执行，请重新选择。"; // 提交成功立即锁定等待，避免双击发送。
            } else if(s.page==Page::Battle && s.phase==Phase::Setup) setupCommit(); // 本地起点确认保持原流程。
            else if(s.page==Page::Battle && s.phase==Phase::Playing && s.game && s.selected>0) { // 只接受当前合法行动阶段的目标。
                Action a{s.kind,s.kind==Kind::ScanRow?(s.selected-1)/3:s.kind==Kind::ScanCol?(s.selected-1)%3:s.selected}; int actor=s.game->turn(); // 构造对应的格号或行列索引。
                if(s.game->act(a)) afterAction(actor); else { s.selected=0; s.notice=L"该行动不合法，请重新选择。"; } // 引擎再次检查，防止界面状态造成非法操作。
            } break; // 结束确认处理。
        case Pass: if(s.page==Page::Battle && s.phase==Phase::Hold && s.game && !s.game->finished()) { s.viewer=s.game->turn(); s.phase=Phase::Cover; s.coverForSetup=false; s.privateNote.clear(); } break; // 检查交接状态后再访问对局，避免旧输入触发空指针。
        case LessonCheck: { // 比较整个候选集合，而不是只看某一个正确格。
            const auto& lesson=lessons()[s.lesson]; s.checkedCorrect=s.checked==lesson.answer; // std::array 的 == 比较全部九个元素。
            if(s.checkedCorrect) { s.completed[s.lesson]=true; s.feedback=L"正确！"+lesson.explanation; } // 正确时解释推理并解锁下一题。
            else { std::array<bool,9> extra{},missing{}; for(int i=0;i<9;++i) { extra[i]=s.checked[i]&&!lesson.answer[i]; missing[i]=!s.checked[i]&&lesson.answer[i]; } s.feedback=L"还差一步。多选："+cells(extra)+L"；漏选："+cells(missing)+L"。\n"+lesson.explanation; } // 错误时指出具体差异，便于学习。
            break; } // 结束题目提交。
        case LessonPrev: if(s.lesson>0) --s.lesson; s.checked.fill(false); s.feedback.clear(); s.checkedCorrect=false; break; // 返回上一关并重置选择。
        case LessonNext: if(s.lesson==2) s.page=Page::Lobby; else ++s.lesson; s.checked.fill(false); s.feedback.clear(); s.checkedCorrect=false; break; // 进入下一关或返回实战大厅。
        case ReplayPrev: s.replay=std::max(0,s.replay-1); break; case ReplayNext: s.replay=std::min(static_cast<int>(s.last->snapshots().size())-1,s.replay+1); break; // 单步移动复盘时间点。
        case ReplayStart: s.replay=0; break; case ReplayEnd: s.replay=static_cast<int>(s.last->snapshots().size())-1; break; // 跳至开局或结局。
        case ViewA: s.replayView=0; break; case ViewB: s.replayView=1; break; case Export: exportReplay(); break; // 切换推理视角或导出完整记录。
        case ReturnResult: if(s.networkResult && s.net && s.net->phase()==forest::Phase::Finished && s.net->replay()==s.last) { s.page=Page::NetBattle; s.phase=Phase::Finished; } else if(!s.networkResult && s.game && s.game==s.last && s.game->finished()) { s.page=Page::Battle; s.phase=Phase::Finished; } break; // 返回对应模式的结束页面。
        default: break; // 未知按钮编号不执行操作。
    } // 结束命令分发。
    invalidate(); // 所有操作后按新状态重绘。
} // 结束命令处理。
void keyboard(WPARAM key) { // 提供与鼠标一致的少量快捷键。
    int id=0; if(key>='1' && key<='9') id=CellBase+static_cast<int>(key-'0'); // 数字键选择对应格子。
    if(key==VK_RETURN) id=s.page==Page::Lesson?LessonCheck:(s.phase==Phase::Cover?Ready:s.phase==Phase::Hold?Pass:Commit); // 回车执行当前页的主要操作。
    if((s.page==Page::Battle || s.page==Page::NetBattle) && s.phase==Phase::Playing) { if(key=='A') id=Attack; if(key=='M') id=Move; if(key=='R') id=Row; if(key=='C') id=Col; } // 两类对战均支持行动快捷键。
    for(const auto& hit:s.hits) if(hit.id==id && hit.enabled) { command(id); break; } // 快捷键必须对应当前画面启用的按钮。
} // 结束键盘处理。
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) { // Windows 消息回调接收绘图、鼠标、键盘与关闭事件。
    switch(message) { // 按系统消息类别处理。
        case WM_CREATE: { // 创建可编辑的地址与端口字段，默认隐藏在其他页面。
            s.window=window; s.addressEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"127.0.0.1",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,nullptr,nullptr,nullptr); // 使用原生文本框支持选择、复制与粘贴。
            s.portEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"39091",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,0,0,1,1,window,nullptr,nullptr,nullptr); // 端口只接受数字输入。
            for(HWND edit:{s.addressEdit,s.portEdit}) { SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE); SendMessageW(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8)); } // 设置系统字体和左右留白。
            SendMessageW(s.addressEdit,EM_SETLIMITTEXT,15,0); SendMessageW(s.portEdit,EM_SETLIMITTEXT,5,0); return 0; // 限制 IPv4 与端口的输入长度。
        } // 结束原生控件创建。
        case WM_ERASEBKGND: return 1; // 使用双缓冲完整绘制背景，避免系统重复擦除导致闪烁。
        case WM_SIZE: invalidate(); return 0; // 窗口尺寸变化后重新计算等比缩放。
        case WM_GETMINMAXINFO: { auto* info=reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize={850,640}; return 0; } // 限制最小窗口尺寸，保持正文可读。
        case WM_PAINT: { // 绘制一帧，所有内容先画到内存位图再一次显示。
            PAINTSTRUCT ps{}; HDC screen=BeginPaint(window,&ps); RECT client{}; GetClientRect(window,&client); // 开始绘图并取得实际客户区大小。
            const int w=client.right,h=client.bottom; HDC memory=CreateCompatibleDC(screen); HBITMAP bitmap=CreateCompatibleBitmap(screen,std::max(1,w),std::max(1,h)); HGDIOBJ old=SelectObject(memory,bitmap); // 创建内存画布。
            fill(memory,client,BG); s.scale=std::min(w/static_cast<double>(WIDTH),h/static_cast<double>(HEIGHT)); s.offsetX=static_cast<int>((w-WIDTH*s.scale)/2); s.offsetY=static_cast<int>((h-HEIGHT*s.scale)/2); // 计算等比例显示与居中边距。
            SetMapMode(memory,MM_ANISOTROPIC); SetWindowExtEx(memory,WIDTH,HEIGHT,nullptr); SetViewportExtEx(memory,static_cast<int>(WIDTH*s.scale),static_cast<int>(HEIGHT*s.scale),nullptr); SetViewportOrgEx(memory,s.offsetX,s.offsetY,nullptr); // 将逻辑设计坐标映射到当前客户区。
            draw(memory); SetMapMode(memory,MM_TEXT); SetViewportOrgEx(memory,0,0,nullptr); BitBlt(screen,0,0,w,h,memory,0,0,SRCCOPY); // 完成一帧后拷贝到屏幕。
            SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); EndPaint(window,&ps); return 0; // 释放本帧的临时绘图资源。
        } // 结束绘图消息。
        case WM_LBUTTONUP: { SetFocus(window); POINT point{static_cast<LONG>((GET_X_LPARAM(lParam)-s.offsetX)/std::max(0.01,s.scale)),static_cast<LONG>((GET_Y_LPARAM(lParam)-s.offsetY)/std::max(0.01,s.scale))}; for(auto it=s.hits.rbegin();it!=s.hits.rend();++it) if(it->enabled && PtInRect(&it->rect,point)) { int id=it->id; command(id); break; } return 0; } // 点击按钮后焦点回主窗，缩小窗口时避免除零。
        case WM_KEYDOWN: keyboard(wParam); return 0; // 处理数字、行动方式和确认快捷键。
        case WM_MOUSEWHEEL: { // 支持两种比赛模式的公共记录滚动。
            int size=s.page==Page::NetBattle && s.net?static_cast<int>(s.net->publicEvents().size()):s.page==Page::Battle && s.game?static_cast<int>(s.game->events().size()):0; // 从当前模式取得日志长度。
            if(size>0) { int delta=GET_WHEEL_DELTA_WPARAM(wParam)>0?1:-1; s.historyOffset=std::clamp(s.historyOffset+delta,0,std::max(0,size-3)); invalidate(); } return 0; // 滚动只改变可见范围。
        } // 结束滚轮消息处理。
        case WM_TIMER: if(wParam==2) { if(s.net && s.net->phase()!=forest::Phase::Idle && s.net->poll()) syncNetworkState(); } else if(wParam==1) { KillTimer(window,1); if(s.page==Page::Battle && s.bot && s.game && !s.game->finished() && s.game->turn()==1) { Action a=chooseAction(s.game->view(1),s.style,s.rng); if(s.game->act(a)) afterAction(1); } } return 0; // 联网关闭后停止轮询，避免覆盖本地游戏的玩家名称。
        case WM_DESTROY: KillTimer(window,1); KillTimer(window,2); if(s.net) s.net->close(); PostQuitMessage(0); return 0; // 关闭时及时释放监听、连接与计时器。
        default: return DefWindowProcW(window,message,wParam,lParam); // 其余标准行为交给系统处理。
    } // 结束系统消息分发。
} // 结束窗口过程。
} // 结束 ui 命名空间。
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) { // Unicode 图形程序入口，不创建控制台窗口。
    SetProcessDPIAware(); WNDCLASSW wc{}; wc.lpfnWndProc=ui::windowProc; wc.hInstance=instance; wc.lpszClassName=L"NineGridStudioWindow"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION); // 注册高 DPI 感知窗口类。
    if(!RegisterClassW(&wc)) return 1; // 窗口类注册失败时返回错误。
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0); // 获取当前屏幕可用区域，避免初始窗口超出屏幕。
    int w=std::min(1160,static_cast<int>(work.right-work.left)-40),h=std::min(842,static_cast<int>(work.bottom-work.top)-40); // 选择适应当前屏幕的初始窗口大小。
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"雾隐之森 · 双人联机与推理实验室",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,(work.left+work.right-w)/2,(work.top+work.bottom-h)/2,w,h,nullptr,nullptr,instance,nullptr); // 主窗为原生输入控件预留绘图区域。
    if(!window) return 1; // 窗口创建失败时停止启动。
    ShowWindow(window,show); UpdateWindow(window); MSG msg{}; // 显示窗口并准备接收系统消息。
    while(GetMessageW(&msg,nullptr,0,0)>0) { if((GetFocus()==ui::s.addressEdit || GetFocus()==ui::s.portEdit) && IsDialogMessageW(window,&msg)) continue; TranslateMessage(&msg); DispatchMessageW(&msg); } // 输入框支持 Tab 切换，其他页面使用游戏快捷键。
    return static_cast<int>(msg.wParam); // 返回系统退出状态码。
} // 结束程序入口。
