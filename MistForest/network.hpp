#pragma once // 预处理指令：防止同一编译单元重复包含此联网接口。
#include "game.hpp" // 包含指令：沿用游戏规则定义的行动、视图与复盘类型。
#include <memory> // 标准库头文件：提供独占指针和共享指针。
#include <string> // 标准库头文件：提供地址字符串与中文状态文字。
#include <vector> // 标准库头文件：提供日志与本机地址列表。
namespace forest { // 命名空间：隔离雾隐之森的联网接口。
enum class Phase { Idle, Listening, Connecting, Setup, Waiting, Playing, Finished, Error }; // 强类型枚举：表示空闲、建房、连接、布置、等待、行动、结束和错误。
class Session { // 类定义：一份对象对应一位本地玩家的网络会话。
public: // 公开访问：界面仅通过这些函数读取本人的视图和提交请求。
    Session(); // 构造函数：初始化网络资源，但不创建连接。
    ~Session(); // 析构函数：关闭套接字并释放网络资源。
    bool host(unsigned short port, bool scan); // 成员函数：在指定端口创建双人房间，并决定是否启用无限侦察。
    bool join(const std::string& ipv4, unsigned short port); // 成员函数：异步连接指定 IPv4 地址的房主。
    bool poll(); // 成员函数：非阻塞推进网络收发，返回界面可见状态是否变化。
    bool chooseStart(int cell); // 成员函数：提交自己的秘密起点，提交后立即防止重复确认。
    bool act(studio::Action action); // 成员函数：提交当前回合的一次行动，客人等待房主裁定。
    bool legal(studio::Action action) const; // 常量成员函数：依据自己的视图与当前回合检查合法性。
    bool newRound(); // 成员函数：房主在结束后开启下一局，自动交换先手。
    void close(); // 成员函数：主动离开并恢复空闲状态。
    bool isHost() const; // 常量成员函数：判断本机是否担任房主裁判。
    bool connected() const; // 常量成员函数：只有握手完成的连接才返回真。
    int player() const; // 常量成员函数：房主为玩家零，客人为玩家一。
    Phase phase() const; // 常量成员函数：读取当前联网阶段。
    bool scanEnabled() const; // 常量成员函数：读取双方统一的侦察规则。
    bool hasStart() const; // 常量成员函数：判断自己的起点是否已经提交。
    bool hasView() const; // 常量成员函数：双方就绪后才允许读取完整的本人视图。
    studio::View view() const; // 按值返回：只提供自己的位置和对手候选集合。
    int turn() const; // 常量成员函数：读取当前轮到的玩家，未开局时为负一。
    int winner() const; // 常量成员函数：负一为进行中，零或一为赢家，二为未决和局。
    int actionCount() const; // 常量成员函数：读取本局已经执行的总行动数。
    const std::vector<studio::Event>& publicEvents() const; // 常量引用：日志隐藏移动终点和全部侦察私密字段。
    const std::wstring& privateNote() const; // 常量引用：读取自己最近一次侦察的结果与时效提醒。
    const std::wstring& status() const; // 常量引用：读取适合界面展示的中文状态。
    std::shared_ptr<studio::Game> replay() const; // 共享指针：只有终局后才提供完整复盘。
    std::shared_ptr<studio::Game> takeCompletedReplay(); // 共享指针：取走一次终局通知，即使同次收包已经进入下一局或断线也可接收完成记录。
private: // 私有访问：网络及裁判状态不能通过界面直接访问。
    struct Impl; // 前置声明：将 Windows 套接字类型隐藏在实现文件中。
    std::unique_ptr<Impl> impl_; // 独占指针：负责隐藏实现对象的自动释放。
}; // 分号：结束会话类定义。
std::vector<std::string> localAddresses(); // 自由函数：查询可供朋友连接的本机 IPv4 网卡地址。
} // 右花括号：结束 forest 命名空间。
