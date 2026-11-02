#include "QuoteFetcher.h"
#include <winhttp.h>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

namespace {

// 简单按分隔符切分 std::string
void SplitA(const std::string& s, char sep, std::vector<std::string>& out) {
    out.clear();
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
}

// 简单按分隔符切分 std::wstring
void SplitW(const std::wstring& s, wchar_t sep, std::vector<std::wstring>& out) {
    out.clear();
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
}

} // namespace

// 把用户填写的代码规范化：纯数字代码自动补市场前缀，已带前缀的保持不变。
// 前缀规则（首位数字，均经 qt.gtimg.cn 实测验证）：
//   6 -> sh（沪市 A 股 60/68）；5 -> sh（沪市基金/ETF 50/51/52/56/58）
//   0/3 -> sz（深市 A 股 000/001/002/300）；1 -> sz（深市基金/ETF 15/16/18）
//   4/8 -> bj（北交所 43/83/87）
// 其余首位不映射，原样提交（由接口返回 none_match 暴露，不做兜底猜测）。
static std::wstring NormalizeCodes(const std::wstring& codes) {
    std::wstring res;
    size_t start = 0;
    while (start < codes.size()) {
        size_t comma = codes.find(L',', start);
        std::wstring tok = (comma == std::wstring::npos) ? codes.substr(start) : codes.substr(start, comma - start);
        // 去前后空白
        size_t a = tok.find_first_not_of(L" \t");
        size_t b = tok.find_last_not_of(L" \t");
        if (a == std::wstring::npos) { start = (comma == std::wstring::npos) ? codes.size() : comma + 1; continue; }
        tok = tok.substr(a, b - a + 1);

        if (!tok.empty() && tok[0] != L's' && tok[0] != L'S' && tok[0] != L'b' && tok[0] != L'B') {
            // 纯数字：按首位补前缀
            wchar_t c = tok[0];
            if (c == L'6' || c == L'5') tok = L"sh" + tok;
            else if (c == L'0' || c == L'3' || c == L'1') tok = L"sz" + tok;
            else if (c == L'4' || c == L'8') tok = L"bj" + tok;
        }
        if (!res.empty()) res += L",";
        res += tok;
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return res;
}

bool QuoteFetcher::IsMarketOpen() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    // 周末（周六=6，周日=0）休市
    if (st.wDayOfWeek == 0 || st.wDayOfWeek == 6) return false;
    int minutes = st.wHour * 60 + st.wMinute;
    // 上午盘 09:30–11:30，下午盘 13:00–15:00（含端点）
    bool am = (minutes >= 9 * 60 + 30) && (minutes <= 11 * 60 + 30);
    bool pm = (minutes >= 13 * 60)      && (minutes <= 15 * 60);
    return am || pm;
}

bool QuoteFetcher::Fetch(const std::wstring& codes, std::vector<std::wstring>& out) {
    if (codes.empty()) return false;

    std::wstring url = L"https://qt.gtimg.cn/q=" + NormalizeCodes(codes);

    std::vector<char> body;
    if (!HttpGet(url, body)) return false;

    Parse(std::string(body.data(), body.size()), out);
    return true;
}

bool QuoteFetcher::HttpGet(const std::wstring& url, std::vector<char>& body) {
    body.clear();

    // 解析 URL 组件
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256] = { 0 };
    wchar_t path[1024] = { 0 };
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;

    HINTERNET hSess = WinHttpOpen(L"SysHUD/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return false;

    // 短超时：连接 3s / 发送 3s / 接收 5s，避免卡住采集线程
    int rx = 3000, ry = 3000, rrv = 5000, rs = 5000;
    WinHttpSetTimeouts(hSess, rx, ry, rrv, rs);

    HINTERNET hConn = WinHttpConnect(hSess, uc.lpszHostName, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", uc.lpszUrlPath,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return false; }

    bool ok = false;
    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD size = 0, downloaded = 0;
        do {
            size = 0;
            if (!WinHttpQueryDataAvailable(hReq, &size)) break;
            if (size == 0) { ok = true; break; }
            size = std::min(size, 65536UL);
            DWORD old = (DWORD)body.size();
            body.resize(old + size);
            if (!WinHttpReadData(hReq, body.data() + old, size, &downloaded)) break;
            body.resize(old + downloaded);
        } while (size > 0);
        ok = true;
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return ok && !body.empty();
}

void QuoteFetcher::Parse(const std::string& text, std::vector<std::wstring>& out) {
    out.clear();
    std::vector<std::string> lines;
    SplitA(text, '\n', lines);

    for (const std::string& line : lines) {
        // 形如： v_sh600519="1~名称~600519~1297.40~昨收~今开~...";
        // 腾讯接口使用 ~ 分隔字段。只取代码（变量名后缀，含市场前缀）与当前价，不显示中文名称。
        if (line.size() < 2 || line[0] != 'v' || line[1] != '_') continue;
        size_t eq = line.find('=');
        size_t q1 = line.find('"', eq);
        if (eq == std::string::npos || q1 == std::string::npos) continue;
        size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;

        // 代码 = v_ 之后、= 之前的变量名（如 sh600519）
        std::string code = line.substr(2, eq - 2);

        std::string payload = line.substr(q1 + 1, q2 - q1 - 1);
        if (payload.empty()) continue;

        // 整段做 GBK->UTF-16 转换（名称含中文，价格纯 ASCII，统一转最稳）
        std::wstring wpayload;
        int need = MultiByteToWideChar(CP_ACP, 0, payload.c_str(), (int)payload.size(), nullptr, 0);
        if (need > 0) {
            wpayload.resize(need);
            MultiByteToWideChar(CP_ACP, 0, payload.c_str(), (int)payload.size(), &wpayload[0], need);
        }
        std::vector<std::wstring> wf;
        SplitW(wpayload, L'~', wf);
        if (wf.size() < 4) continue;

        // 字段3=当前价；过滤异常价格（空或 "0.00" 视为无数据）
        if (wf[3].empty() || wf[3] == L"0.00") continue;

        std::wstring wcode(code.begin(), code.end());
        out.push_back(wcode + L"  " + wf[3]);
    }
}
