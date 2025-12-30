#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <limits>
#include <cmath>      // for std::isnan
#include <algorithm>  // for std::sort
#include <iomanip>    // for std::setprecision
using namespace std;

// 初始資金（用來模擬 & 算報酬率）
const double INITIAL = 10000.0;

// 一天的資料：日期 + 多檔股票價格
struct DayData {
    string date;
    vector<double> prices;  // 跟 g_symbols 對應
};

// 全域變數
vector<string> g_symbols;    // 股票代號列表（從 header 讀）
vector<DayData> g_data;      // 每天的所有股票資料

// --------------------------------------------------
// loadFile：讀 multistocks.csv → 填 g_symbols, g_data
//   假設格式：Date,AAPL,MSFT,... （第一欄是日期）
// --------------------------------------------------
bool loadFile(const string& filename)
{
    auto trim = [](const string& s) -> string {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
        };

    auto splitByComma = [&](const string& line) -> vector<string> {
        vector<string> tokens;
        string token;
        stringstream ss(line);
        while (getline(ss, token, ',')) {
            tokens.push_back(trim(token));
        }
        return tokens;
        };

    ifstream fin(filename);
    if (!fin.is_open()) {
        cerr << "無法開啟檔案: " << filename << "\n";
        return false;
    }

    g_symbols.clear();
    g_data.clear();

    string line;

    // ========== 讀 header ==========
    if (!getline(fin, line)) {
        cerr << "檔案是空的: " << filename << "\n";
        return false;
    }

    auto headerTokens = splitByComma(line);
    if (headerTokens.size() < 2) {
        cerr << "header 欄位太少: " << line << "\n";
        return false;
    }

    // headerTokens[0] = "Date"
    for (size_t i = 1; i < headerTokens.size(); ++i) {
        g_symbols.push_back(headerTokens[i]);
    }

    // ========== 讀每天資料 ==========
    while (getline(fin, line)) {
        if (line.find_first_not_of(" \t\r\n") == string::npos) continue;

        auto tokens = splitByComma(line);
        if (tokens.size() != headerTokens.size()) {
            cerr << "欄位數不符，略過此行: " << line << "\n";
            continue;
        }

        DayData day;
        day.date = tokens[0];
        day.prices.reserve(g_symbols.size());

        bool ok = true;
        for (size_t i = 1; i < tokens.size(); ++i) {
            try {
                double v = stod(tokens[i]);
                day.prices.push_back(v);
            }
            catch (...) {
                cerr << "數值轉換失敗，略過此行: " << tokens[i]
                    << " (line: " << line << ")\n";
                ok = false;
                break;
            }
        }
        if (!ok) continue;

        g_data.push_back(std::move(day));
    }

    return true;
}

// --------------------------------------------------
// 小工具：找 symbol index
// --------------------------------------------------
int findSymbolIndex(const string& symbol) {
    for (size_t i = 0; i < g_symbols.size(); ++i) {
        if (g_symbols[i] == symbol) return (int)i;
    }
    return -1;
}

// --------------------------------------------------
// 計算簡單移動平均 (SMA)：前 n-1 天為 NaN
// --------------------------------------------------
vector<double> calcSMA(const vector<double>& p, int n) {
    int N = (int)p.size();
    vector<double> sma(N, numeric_limits<double>::quiet_NaN());
    if (n < 1 || n > N) return sma;

    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += p[i];
    sma[n - 1] = sum / n;

    for (int i = n; i < N; i++) {
        sum += p[i] - p[i - n];
        sma[i] = sum / n;
    }
    return sma;
}

// --------------------------------------------------
// 模擬結果：最後資金 + 交易次數
// --------------------------------------------------
struct SimResult {
    double finalCapital;
    int tradeCount;
};

// --------------------------------------------------
// 模擬策略（只在指定 index 區間內交易）
//   初始資金 10000，整股交易，區間最後一天強制平倉（也算 1 次交易）
//   規則：第 i 天偵測交叉 → 第 i 天收盤價成交
// --------------------------------------------------
SimResult simulateWithCapitalRange(
    const vector<double>& prices,
    const vector<double>& smaS,
    const vector<double>& smaL,
    int startIdx,
    int endIdx
) {
    double cash = INITIAL;
    int shares = 0;
    int trades = 0;

    int N = (int)prices.size();
    if (N == 0) return { INITIAL, 0 };
    if (startIdx < 0) startIdx = 0;
    if (endIdx >= N)   endIdx = N - 1;
    if (startIdx >= endIdx) return { INITIAL, 0 };

    if (startIdx < 1) startIdx = 1;

    for (int i = startIdx; i <= endIdx; ++i) {

        double dPrev = smaS[i - 1] - smaL[i - 1];
        double dNow = smaS[i] - smaL[i];

        if (std::isnan(dPrev) || std::isnan(dNow)) continue;

        // =============================
        // ★★ 新增：如果是第一天（i == startIdx），禁止 BUY （無視黃金交叉）
        // =============================
        bool isFirstDay = (i == startIdx);

        // BUY：黃金交叉
        if (!isFirstDay && shares == 0 && dPrev < 0 && dNow > 0) {
            int buyShares = (int)(cash / prices[i]);
            if (buyShares > 0) {
                shares += buyShares;
                cash -= (double)buyShares * prices[i];
                trades++;
            }
        }
        // SELL：死亡交叉
        else if (shares > 0 && dPrev > 0 && dNow < 0) {
            cash += (double)shares * prices[i];
            shares = 0;
            trades++;
        }
    }

    // 區間最後一天強制平倉
    if (shares > 0) {
        cash += (double)shares * prices[endIdx];
        shares = 0;
        trades++;
    }

    return { cash, trades };
}

// --------------------------------------------------
// 每一組 short/long 的結果，用來排序 & 輸出
// --------------------------------------------------
struct BruteResult {
    int s;
    int l;
    double finalCapital;
    int trades;
};

// --------------------------------------------------
// 對單一 symbol：brute force 並把前 topN 名 append 到同一個 CSV
//   檔案格式（整檔）：
//   排名,短期,長期,最終獲利,報酬率,交易次數
//   AAPL 的 20 筆
//   空行
//   MMM,,,,,
//   MMM 的 20 筆
//   空行
//   KO,,,,,
//   ...
//   ★ 金額 & 報酬率用雙引號包起來，讓 Excel 當文字，不會吃精度。
// --------------------------------------------------
void bruteForceAndAppend(
    const vector<double>& prices,
    int startIdx,
    int endIdx,
    const string& label,
    ofstream& fout,
    bool isFirstSymbol,
    int topN = 20
) {
    const int MAXN = 256;
    int N = (int)prices.size();

    // 預先把所有 period 的 SMA 算好
    vector<vector<double>> allSMA(MAXN + 1);
    for (int n = 1; n <= MAXN; n++) {
        allSMA[n] = calcSMA(prices, n);
    }

    double bestCapital = -1e18;
    int bestS = -1, bestL = -1;

    vector<BruteResult> results;
    results.reserve(MAXN * MAXN);

    // 算出所有組合
    for (int s = 1; s <= MAXN; s++) {
        for (int l = 1; l <= MAXN; l++) {
            SimResult sr = simulateWithCapitalRange(
                prices, allSMA[s], allSMA[l],
                startIdx, endIdx
            );
            results.push_back({ s, l, sr.finalCapital, sr.tradeCount });

            if (sr.finalCapital > bestCapital) {
                bestCapital = sr.finalCapital;
                bestS = s;
                bestL = l;
            }
        }
    }

    // Console 上顯示一下這檔的最佳組合
    cout << "\n==== " << label << " ====\n";
    cout << "最佳組合： short=" << bestS
        << " long=" << bestL
        << " final_capital=" << bestCapital << "\n";

    // 排序：依 finalCapital 由大到小
    sort(results.begin(), results.end(),
        [](const BruteResult& a, const BruteResult& b) {
            if (a.finalCapital != b.finalCapital)
                return a.finalCapital > b.finalCapital;   // 資金多的在前

            int da = std::abs(a.s - a.l);
            int db = std::abs(b.s - b.l);
            if (da != db)
                return da > db;                            // 距離短的在前

            if (a.s != b.s)
                return a.s < b.s;                          // 再用 s 當第三鍵
            return a.l < b.l;                              // 最後用 l
        });

    // Console 印出前 topN 名
    cout << "\n排名\t短期\t長期\t最終獲利\t報酬率\t交易次數\n";
    cout << fixed << setprecision(4);
    for (int i = 0; i < topN && i < (int)results.size(); ++i) {
        const auto& r = results[i];
        double ret = (r.finalCapital / INITIAL - 1.0) * 100.0;
        cout << (i + 1) << "\t"
            << r.s << "\t"
            << r.l << "\t"
            << r.finalCapital << "\t"
            << ret << "\t"
            << r.trades << "\n";
    }

    // ===== 寫進同一個 CSV 檔 =====
    // 第一檔（例如 AAPL）就直接寫排名資料；
    // 之後的 MMM/KO/V/CAT 先插一行「MMM,,,,,」，再空一行，再寫排名。
    if (!isFirstSymbol) {
        fout << label << ",,,,,\n\n";  // 分段標題 + 空白行
    }

    // 這邊用文字輸出：把數值包在雙引號裡
    for (int i = 0; i < topN && i < (int)results.size(); ++i) {
        const auto& r = results[i];
        double ret = (r.finalCapital / INITIAL - 1.0) * 100.0;

        std::ostringstream capSs;
        capSs << std::fixed << std::setprecision(30) << r.finalCapital;
        std::string capStr = capSs.str();

        std::ostringstream retSs;
        retSs << std::fixed << std::setprecision(4) << ret;
        std::string retStr = retSs.str();

        // ★ 在前面加一個單引號，讓 Excel 當文字
        std::string capField = "'" + capStr;
        std::string retField = "'" + retStr;

        fout << (i + 1) << ","     // 排名（數字）
            << r.s << ","         // 短期
            << r.l << ","         // 長期
            << capField << ","    // 最終獲利（文字）
            << retField << ","    // 報酬率（文字）
            << r.trades << "\n";  // 交易次數（數字）
    }
    fout << "\n";  // 每檔最後再空一行，視覺上比較像你貼的樣子

    cout << "寫入完成：" << label << "\n";
}

// --------------------------------------------------
// 針對單一 symbol：取出 prices、找 2024 範圍、呼叫 bruteForceAndAppend
// --------------------------------------------------
void runForSymbol(
    const string& symbol,
    ofstream& fout,
    bool isFirstSymbol,
    int topN = 20
) {
    int symIdx = findSymbolIndex(symbol);
    if (symIdx == -1) {
        cerr << "找不到 symbol: " << symbol << "\n";
        return;
    }

    vector<double> prices;
    vector<string> dates;
    prices.reserve(g_data.size());
    dates.reserve(g_data.size());

    for (auto& d : g_data) {
        prices.push_back(d.prices[symIdx]);
        dates.push_back(d.date);
    }

    if (prices.empty()) {
        cerr << "沒有任何 " << symbol << " 資料\n";
        return;
    }

    // 找出「日期字串含 /2024 的起訖 index」
    int start2024 = -1;
    int end2024 = -1;
    for (int i = 0; i < (int)dates.size(); ++i) {
        if (dates[i].find("/2024") != string::npos) {
            if (start2024 == -1) start2024 = i;
            end2024 = i;  // 不斷更新，最後就是最後一筆 2024
        }
    }

    if (start2024 == -1) {
        cerr << "找不到 2024 的 " << symbol << " 資料\n";
        return;
    }

    cout << "\n=== Symbol: " << symbol << " ===\n";
    cout << "2024 起訖 index: " << start2024 << " ~ " << end2024 << "\n";
    cout << "2024 交易天數: " << (end2024 - start2024 + 1) << "\n";

    bruteForceAndAppend(
        prices,
        start2024,
        end2024,
        symbol,
        fout,
        isFirstSymbol,
        topN
    );
}

// --------------------------------------------------
// main：讀檔 → 針對 AAPL, MMM, KO, V, CAT 各跑一次
//   輸出到同一個 sma_rank_all.csv，用你貼的那種分段格式
// --------------------------------------------------
int main() {
    string filename = "multistocks.csv";

    if (!loadFile(filename)) {
        return 1;
    }

    cout << "股票數量: " << g_symbols.size() << "\n";
    cout << "總天數: " << g_data.size() << "\n";

    // 想要輸出的 symbol 列表
    // 如果只要 AAPL, MMM, KO, V，就把 "CAT" 拿掉就好
    vector <string> targetSymbols = { "AAPL", "MMM", "KO", "V", "CAT" };

    ofstream fout("sma_rank_all.csv");
    if (!fout.is_open()) {
        cerr << "無法開啟輸出檔案 sma_rank_all.csv\n";
        return 1;
    }

    // 第一行欄位名稱（只寫一次）
    fout << "排名,短期,長期,最終獲利,報酬率,交易次數\n\n";

    bool first = true;
    for (const auto& sym : targetSymbols) {
        runForSymbol(sym, fout, first, 20);
        first = false;
    }

    fout.close();
    cout << "\n全部完成，輸出檔：sma_rank_all.csv\n";
    return 0;
}
