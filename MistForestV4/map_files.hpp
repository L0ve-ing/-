#pragma once // 防止地图文件工具被重复包含。
#ifndef NOMINMAX // 独立包含本头文件时也避免系统宏覆盖标准库函数。
#define NOMINMAX // 关闭旧式最小值和最大值宏。
#endif // 结束系统宏兼容设置。
#include <windows.h> // 使用同目录临时文件及系统原子替换接口。
#include <filesystem> // 文件路径支持中文名称。
#include <fstream> // 读取用户选择的本地地图。
#include <sstream> // 严格解析地图文件的头部和内容。
#include "game.hpp" // 地图文件与裁判共用同一套地形编码和校验。
namespace mist { // 地图持久化属于游戏规则配套工具。
inline bool loadMapFile(const std::filesystem::path& path,Map& map,std::wstring* why=nullptr) { // 仅完整读取并验证成功后才替换目标地图。
    auto fail=[&](const wchar_t* message) { if(why) *why=message; return false; }; // 统一中文错误提示。
    std::error_code error; auto length=std::filesystem::file_size(path,error); if(error || length>4096) return fail(L"地图无法读取或文件过大。"); // 限制输入大小，防止异常文件影响界面。
    std::ifstream input(path,std::ios::binary); if(!input) return fail(L"无法打开地图文件。"); // 文件只以只读模式打开。
    std::string bytes(static_cast<std::size_t>(length),'\0'); if(length && !input.read(bytes.data(),static_cast<std::streamsize>(length))) return fail(L"地图文件读取不完整。"); // 按检查过的长度读取。
    std::istringstream data(bytes); std::string header,encoded,extra; int version=0; Map candidate; // 在临时对象中解析，保留现有地图。
    if(!(data>>header>>version>>encoded) || header!="MIST_FOREST_MAP" || version!=1 || (data>>extra) || !decodeMap(Mode::Heroes,encoded,candidate)) return fail(L"地图格式无效，或没有可站立的空地。"); // 拒绝未知版本、额外字段和非法地形。
    map=candidate; if(why) why->clear(); return true; // 全部校验通过后一次性提交新地图。
} // 结束地图读取。
inline bool saveMapFile(const std::filesystem::path& path,const Map& map,std::wstring* why=nullptr) { // 保存失败时保留原文件。
    auto fail=[&](const wchar_t* message) { if(why) *why=message; return false; }; // 统一保存错误。
    if(!validMap(Mode::Heroes,map)) return fail(L"请至少保留一格可站立的空地。"); // 同格开局允许，因此至少一格即可。
    std::filesystem::path temp; HANDLE file=INVALID_HANDLE_VALUE; // 临时文件必须与目标文件位于同一目录。
    for(int attempt=0;attempt<16 && file==INVALID_HANDLE_VALUE;++attempt) { temp=path; temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(attempt); file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr); } // 独占创建，避免覆盖任何现有临时文件。
    if(file==INVALID_HANDLE_VALUE) return fail(L"无法保存，请选择可写的文件夹。"); // 创建失败不会修改原地图文件。
    const std::string bytes="MIST_FOREST_MAP 1\n"+encodeMap(map)+"\n"; DWORD written=0; bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file); CloseHandle(file); // 写完并同步临时文件后再提交。
    if(ok) ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0; // 在目标目录内替换文件，失败时旧文件仍可使用。
    if(!ok) { DeleteFileW(temp.c_str()); return fail(L"保存失败，原地图文件已保留。"); } // 仅删除本次创建的临时文件。
    if(why) { why->clear(); } return true; // 成功后清除旧提示。
} // 结束地图保存。
} // 结束地图文件命名空间。
