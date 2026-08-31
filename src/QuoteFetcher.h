#pragma once
#include <windows.h>
#include <string>
#include <vector>

// 报价获取模块：通过腾讯行情接口（qt.gtimg.cn）拉取证券当前价格。
// 接口返回 GBK 编码文本，内部转 UTF-16 后解析。
class QuoteFetcher {
public:
    // A股交易时间判定：周一至周五，且处于 09:30–11:30 或 13:00–15:00。
    // 午休、周末、非工作日返回 false（此时行情接口返回的是收盘旧值，无需拉取）。
    static bool IsMarketOpen();

    // 根据逗号分隔的代码串（如 "sh600519,sz000001"）拉取报价。
    // 成功时 out 填充每行 "名称  现价"，返回 true；失败返回 false（out 不变）。
    bool Fetch(const std::wstring& codes, std::vector<std::wstring>& out);

private:
    // 发起 HTTP GET，返回原始多字节响应体（GBK）。
    bool HttpGet(const std::wstring& url, std::vector<char>& body);
    // 解析腾讯返回文本，提取 名称+现价。
    void Parse(const std::string& text, std::vector<std::wstring>& out);
};
