#include "sound/ym2151.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsp {
namespace {

constexpr int kFreqSh = 16;
constexpr int kEgSh = 16;
constexpr int kLfoSh = 10;
constexpr uint32_t kFreqMask = 0xffff;
constexpr int kEnvBits = 10;
constexpr double kEnvStep = 0.125;
constexpr int kMaxAttIndex = 0x3ff;
constexpr int kMinAttIndex = 0;
constexpr uint32_t kEgAtt = 4, kEgDec = 3, kEgSus = 2, kEgRel = 1, kEgOff = 0;
constexpr int kSinLen = 0x400;
constexpr int kSinMask = 0x3ff;
constexpr int kTlResLen = 256;
constexpr int kTlTabLen = 13 * 2 * kTlResLen;
constexpr uint32_t kEnvQuiet = kTlTabLen >> 3;
constexpr int kRateSteps = 8;
constexpr int kMaxOut = 32767;
constexpr int kMinOut = -32768;

// Pascal's sshr(): shifts the magnitude, so it truncates towards zero.
template <typename T>
T sshr(T value, int shift) {
    return value < 0 ? -(-value >> shift) : (value >> shift);
}

const uint8_t kEgInc[19 * kRateSteps] = {
    0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 1, 1, 0, 1,
    0, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 1, 1, 1, 2,
    1, 2, 1, 2, 1, 2, 1, 2,
    1, 2, 2, 2, 1, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 4, 2, 2, 2, 4,
    2, 4, 2, 4, 2, 4, 2, 4,
    2, 4, 4, 4, 2, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 8, 4, 4, 4, 8,
    4, 8, 4, 8, 4, 8, 4, 8,
    4, 8, 8, 8, 4, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
    16, 16, 16, 16, 16, 16, 16, 16,
    0, 0, 0, 0, 0, 0, 0, 0};

struct RateTables {
    uint8_t select[128];
    uint8_t shift[128];
};

RateTables make_rate_tables() {
    RateTables t{};
    for (int i = 0; i < 32; ++i) {
        t.select[i] = uint8_t(18 * kRateSteps);
        t.shift[i] = 0;
    }
    for (int i = 0; i < 48; ++i) {  // rates 00-11
        t.select[32 + i] = uint8_t((i % 4) * kRateSteps);
        t.shift[32 + i] = uint8_t(11 - i / 4);
    }
    for (int i = 0; i < 16; ++i) {  // rates 12-15
        const int rate = std::min(4 + i, 16);
        t.select[80 + i] = uint8_t(rate * kRateSteps);
        t.shift[80 + i] = 0;
    }
    for (int i = 0; i < 32; ++i) {  // 32 dummy rates (same as 15 3)
        t.select[96 + i] = uint8_t(16 * kRateSteps);
        t.shift[96 + i] = 0;
    }
    return t;
}

const RateTables kRates = make_rate_tables();

const uint32_t kDt2Tab[4] = {0, 384, 500, 608};

const uint8_t kDt1Tab[4 * 32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
    2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8,
    1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5,
    5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16,
    2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22};

const uint16_t kPhaseIncRom[768] = {
    1299, 1300, 1301, 1302, 1303, 1304, 1305, 1306, 1308, 1309, 1310, 1311, 1313, 1314, 1315, 1316,
    1318, 1319, 1320, 1321, 1322, 1323, 1324, 1325, 1327, 1328, 1329, 1330, 1332, 1333, 1334, 1335,
    1337, 1338, 1339, 1340, 1341, 1342, 1343, 1344, 1346, 1347, 1348, 1349, 1351, 1352, 1353, 1354,
    1356, 1357, 1358, 1359, 1361, 1362, 1363, 1364, 1366, 1367, 1368, 1369, 1371, 1372, 1373, 1374,
    1376, 1377, 1378, 1379, 1381, 1382, 1383, 1384, 1386, 1387, 1388, 1389, 1391, 1392, 1393, 1394,
    1396, 1397, 1398, 1399, 1401, 1402, 1403, 1404, 1406, 1407, 1408, 1409, 1411, 1412, 1413, 1414,
    1416, 1417, 1418, 1419, 1421, 1422, 1423, 1424, 1426, 1427, 1429, 1430, 1431, 1432, 1434, 1435,
    1437, 1438, 1439, 1440, 1442, 1443, 1444, 1445, 1447, 1448, 1449, 1450, 1452, 1453, 1454, 1455,
    1458, 1459, 1460, 1461, 1463, 1464, 1465, 1466, 1468, 1469, 1471, 1472, 1473, 1474, 1476, 1477,
    1479, 1480, 1481, 1482, 1484, 1485, 1486, 1487, 1489, 1490, 1492, 1493, 1494, 1495, 1497, 1498,
    1501, 1502, 1503, 1504, 1506, 1507, 1509, 1510, 1512, 1513, 1514, 1515, 1517, 1518, 1520, 1521,
    1523, 1524, 1525, 1526, 1528, 1529, 1531, 1532, 1534, 1535, 1536, 1537, 1539, 1540, 1542, 1543,
    1545, 1546, 1547, 1548, 1550, 1551, 1553, 1554, 1556, 1557, 1558, 1559, 1561, 1562, 1564, 1565,
    1567, 1568, 1569, 1570, 1572, 1573, 1575, 1576, 1578, 1579, 1580, 1581, 1583, 1584, 1586, 1587,
    1590, 1591, 1592, 1593, 1595, 1596, 1598, 1599, 1601, 1602, 1604, 1605, 1607, 1608, 1609, 1610,
    1613, 1614, 1615, 1616, 1618, 1619, 1621, 1622, 1624, 1625, 1627, 1628, 1630, 1631, 1632, 1633,
    1637, 1638, 1639, 1640, 1642, 1643, 1645, 1646, 1648, 1649, 1651, 1652, 1654, 1655, 1656, 1657,
    1660, 1661, 1663, 1664, 1666, 1667, 1669, 1670, 1672, 1673, 1675, 1676, 1678, 1679, 1681, 1682,
    1685, 1686, 1688, 1689, 1691, 1692, 1694, 1695, 1697, 1698, 1700, 1701, 1703, 1704, 1706, 1707,
    1709, 1710, 1712, 1713, 1715, 1716, 1718, 1719, 1721, 1722, 1724, 1725, 1727, 1728, 1730, 1731,
    1734, 1735, 1737, 1738, 1740, 1741, 1743, 1744, 1746, 1748, 1749, 1751, 1752, 1754, 1755, 1757,
    1759, 1760, 1762, 1763, 1765, 1766, 1768, 1769, 1771, 1773, 1774, 1776, 1777, 1779, 1780, 1782,
    1785, 1786, 1788, 1789, 1791, 1793, 1794, 1796, 1798, 1799, 1801, 1802, 1804, 1806, 1807, 1809,
    1811, 1812, 1814, 1815, 1817, 1819, 1820, 1822, 1824, 1825, 1827, 1828, 1830, 1832, 1833, 1835,
    1837, 1838, 1840, 1841, 1843, 1845, 1846, 1848, 1850, 1851, 1853, 1854, 1856, 1858, 1859, 1861,
    1864, 1865, 1867, 1868, 1870, 1872, 1873, 1875, 1877, 1879, 1880, 1882, 1884, 1885, 1887, 1888,
    1891, 1892, 1894, 1895, 1897, 1899, 1900, 1902, 1904, 1906, 1907, 1909, 1911, 1912, 1914, 1915,
    1918, 1919, 1921, 1923, 1925, 1926, 1928, 1930, 1932, 1933, 1935, 1937, 1939, 1940, 1942, 1944,
    1946, 1947, 1949, 1951, 1953, 1954, 1956, 1958, 1960, 1961, 1963, 1965, 1967, 1968, 1970, 1972,
    1975, 1976, 1978, 1980, 1982, 1983, 1985, 1987, 1989, 1990, 1992, 1994, 1996, 1997, 1999, 2001,
    2003, 2004, 2006, 2008, 2010, 2011, 2013, 2015, 2017, 2019, 2021, 2022, 2024, 2026, 2028, 2029,
    2032, 2033, 2035, 2037, 2039, 2041, 2043, 2044, 2047, 2048, 2050, 2052, 2054, 2056, 2058, 2059,
    2062, 2063, 2065, 2067, 2069, 2071, 2073, 2074, 2077, 2078, 2080, 2082, 2084, 2086, 2088, 2089,
    2092, 2093, 2095, 2097, 2099, 2101, 2103, 2104, 2107, 2108, 2110, 2112, 2114, 2116, 2118, 2119,
    2122, 2123, 2125, 2127, 2129, 2131, 2133, 2134, 2137, 2139, 2141, 2142, 2145, 2146, 2148, 2150,
    2153, 2154, 2156, 2158, 2160, 2162, 2164, 2165, 2168, 2170, 2172, 2173, 2176, 2177, 2179, 2181,
    2185, 2186, 2188, 2190, 2192, 2194, 2196, 2197, 2200, 2202, 2204, 2205, 2208, 2209, 2211, 2213,
    2216, 2218, 2220, 2222, 2223, 2226, 2227, 2230, 2232, 2234, 2236, 2238, 2239, 2242, 2243, 2246,
    2249, 2251, 2253, 2255, 2256, 2259, 2260, 2263, 2265, 2267, 2269, 2271, 2272, 2275, 2276, 2279,
    2281, 2283, 2285, 2287, 2288, 2291, 2292, 2295, 2297, 2299, 2301, 2303, 2304, 2307, 2308, 2311,
    2315, 2317, 2319, 2321, 2322, 2325, 2326, 2329, 2331, 2333, 2335, 2337, 2338, 2341, 2342, 2345,
    2348, 2350, 2352, 2354, 2355, 2358, 2359, 2362, 2364, 2366, 2368, 2370, 2371, 2374, 2375, 2378,
    2382, 2384, 2386, 2388, 2389, 2392, 2393, 2396, 2398, 2400, 2402, 2404, 2407, 2410, 2411, 2414,
    2417, 2419, 2421, 2423, 2424, 2427, 2428, 2431, 2433, 2435, 2437, 2439, 2442, 2445, 2446, 2449,
    2452, 2454, 2456, 2458, 2459, 2462, 2463, 2466, 2468, 2470, 2472, 2474, 2477, 2480, 2481, 2484,
    2488, 2490, 2492, 2494, 2495, 2498, 2499, 2502, 2504, 2506, 2508, 2510, 2513, 2516, 2517, 2520,
    2524, 2526, 2528, 2530, 2531, 2534, 2535, 2538, 2540, 2542, 2544, 2546, 2549, 2552, 2553, 2556,
    2561, 2563, 2565, 2567, 2568, 2571, 2572, 2575, 2577, 2579, 2581, 2583, 2586, 2589, 2590, 2593,
};

const uint8_t kLfoNoiseWaveform[256] = {
    0xFF, 0xEE, 0xD3, 0x80, 0x58, 0xDA, 0x7F, 0x94, 0x9E, 0xE3, 0xFA, 0x00, 0x4D, 0xFA, 0xFF, 0x6A,
    0x7A, 0xDE, 0x49, 0xF6, 0x00, 0x33, 0xBB, 0x63, 0x91, 0x60, 0x51, 0xFF, 0x00, 0xD8, 0x7F, 0xDE,
    0xDC, 0x73, 0x21, 0x85, 0xB2, 0x9C, 0x5D, 0x24, 0xCD, 0x91, 0x9E, 0x76, 0x7F, 0x20, 0xFB, 0xF3,
    0x00, 0xA6, 0x3E, 0x42, 0x27, 0x69, 0xAE, 0x33, 0x45, 0x44, 0x11, 0x41, 0x72, 0x73, 0xDF, 0xA2,
    0x32, 0xBD, 0x7E, 0xA8, 0x13, 0xEB, 0xD3, 0x15, 0xDD, 0xFB, 0xC9, 0x9D, 0x61, 0x2F, 0xBE, 0x9D,
    0x23, 0x65, 0x51, 0x6A, 0x84, 0xF9, 0xC9, 0xD7, 0x23, 0xBF, 0x65, 0x19, 0xDC, 0x03, 0xF3, 0x24,
    0x33, 0xB6, 0x1E, 0x57, 0x5C, 0xAC, 0x25, 0x89, 0x4D, 0xC5, 0x9C, 0x99, 0x15, 0x07, 0xCF, 0xBA,
    0xC5, 0x9B, 0x15, 0x4D, 0x8D, 0x2A, 0x1E, 0x1F, 0xEA, 0x2B, 0x2F, 0x64, 0xA9, 0x50, 0x3D, 0xAB,
    0x50, 0x77, 0xE9, 0xC0, 0xAC, 0x6D, 0x3F, 0xCA, 0xCF, 0x71, 0x7D, 0x80, 0xA6, 0xFD, 0xFF, 0xB5,
    0xBD, 0x6F, 0x24, 0x7B, 0x00, 0x99, 0x5D, 0xB1, 0x48, 0xB0, 0x28, 0x7F, 0x80, 0xEC, 0xBF, 0x6F,
    0x6E, 0x39, 0x90, 0x42, 0xD9, 0x4E, 0x2E, 0x12, 0x66, 0xC8, 0xCF, 0x3B, 0x3F, 0x10, 0x7D, 0x79,
    0x00, 0xD3, 0x1F, 0x21, 0x93, 0x34, 0xD7, 0x19, 0x22, 0xA2, 0x08, 0x20, 0xB9, 0xB9, 0xEF, 0x51,
    0x99, 0xDE, 0xBF, 0xD4, 0x09, 0x75, 0xE9, 0x8A, 0xEE, 0xFD, 0xE4, 0x4E, 0x30, 0x17, 0xDF, 0xCE,
    0x11, 0xB2, 0x28, 0x35, 0xC2, 0x7C, 0x64, 0xEB, 0x91, 0x5F, 0x32, 0x0C, 0x6E, 0x00, 0xF9, 0x92,
    0x19, 0xDB, 0x8F, 0xAB, 0xAE, 0xD6, 0x12, 0xC4, 0x26, 0x62, 0xCE, 0xCC, 0x0A, 0x03, 0xE7, 0xDD,
    0xE2, 0x4D, 0x8A, 0xA6, 0x46, 0x95, 0x0F, 0x8F, 0xF5, 0x15, 0x97, 0x32, 0xD4, 0x28, 0x1E, 0x55,
};

struct WaveTables {
    int16_t tl[kTlTabLen];
    uint16_t sin[kSinLen];
    uint32_t d1l[16];
};

WaveTables make_wave_tables() {
    WaveTables t{};
    for (int x = 0; x < kTlResLen; ++x) {
        const double m = std::floor((1 << 16) / std::pow(2.0, (x + 1) * (kEnvStep / 4.0) / 8.0));
        int n = int(uint16_t(std::lround(m)));
        n = sshr(n, 4);
        n = (n & 1) ? sshr(n, 1) + 1 : sshr(n, 1);
        n *= 4;
        t.tl[x * 2 + 0] = int16_t(n);
        t.tl[x * 2 + 1] = int16_t(-n);
        for (int i = 1; i <= 12; ++i) {
            t.tl[x * 2 + 0 + i * 2 * kTlResLen] = int16_t(sshr(int(t.tl[x * 2 + 0]), i));
            t.tl[x * 2 + 1 + i * 2 * kTlResLen] = int16_t(-t.tl[x * 2 + 0 + i * 2 * kTlResLen]);
        }
    }
    for (int i = 0; i < kSinLen; ++i) {
        const double m = std::sin(((i * 2) + 1) * M_PI / kSinLen);
        double o = (m > 0.0) ? 8 * std::log(1.0 / m) / std::log(2.0)
                             : 8 * std::log(-1.0 / m) / std::log(2.0);
        o /= (kEnvStep / 4);
        int n = int(uint16_t(std::lround(2.0 * o)));
        n = (n & 1) ? sshr(n, 1) + 1 : sshr(n, 1);
        t.sin[i] = uint16_t(m >= 0 ? n * 2 : n * 2 + 1);
    }
    for (int i = 0; i < 16; ++i) {
        const double m = (i != 15) ? i * (4.0 / kEnvStep) : (i + 16) * (4.0 / kEnvStep);
        t.d1l[i] = uint32_t(m);
    }
    return t;
}

const WaveTables kWaves = make_wave_tables();

}  // namespace
YM2151::YM2151(uint32_t clock, float amplitude) : clock_(clock), amplitude_(amplitude) {
    init_chip_tables();
    lfo_timer_add_ = uint32_t((1 << kLfoSh) * (clock_ / 64.0) / sample_rate_);
    eg_timer_add_ = uint32_t((1 << kEgSh) * (clock_ / 64.0) / sample_rate_);
    eg_timer_overflow_ = 3 * (1 << kEgSh);
    reset();
}

void YM2151::init_chip_tables() {
    const double scaler = (clock_ / 64.0) / sample_rate_;
    double mult = 1 << (kFreqSh - 10);
    for (int i = 0; i < 768; ++i) {
        const double phaseinc = kPhaseIncRom[i] * scaler;
        freq_[768 + 2 * 768 + i] = uint32_t(phaseinc * mult) & 0xffffffc0u;
        for (int j = 0; j <= 1; ++j) {
            freq_[768 + j * 768 + i] = (freq_[768 + 2 * 768 + i] >> (2 - j)) & 0xffffffc0u;
        }
        for (int j = 3; j <= 7; ++j) {
            freq_[768 + j * 768 + i] = freq_[768 + 2 * 768 + i] << (j - 2);
        }
    }
    for (int i = 0; i < 768; ++i) freq_[i] = freq_[1 * 768];
    for (int j = 8; j <= 9; ++j) {
        for (int i = 0; i < 768; ++i) freq_[768 + j * 768 + i] = freq_[768 + 8 * 768 - 1];
    }

    mult = 1 << kFreqSh;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 32; ++i) {
            const double hz = (kDt1Tab[j * 32 + i] * (clock_ / 64.0)) / (1 << 20);
            const double phaseinc = (hz * kSinLen) / sample_rate_;
            dt1_freq_[(j + 0) * 32 + i] = int32_t(phaseinc * mult);
            dt1_freq_[(j + 4) * 32 + i] = -dt1_freq_[(j + 0) * 32 + i];
        }
    }

    for (int i = 0; i < 32; ++i) {
        double pom = (i != 31) ? i : 30;
        pom = 32 - pom;
        pom = 65536.0 / (pom * 32.0);
        noise_tab_[i] = uint32_t(pom * 64 * scaler);
    }
}

void YM2151::reset() {
    for (int i = 0; i < 32; ++i) {
        oper_[i] = Operator{};
        oper_[i].volume = kMaxAttIndex;
        oper_[i].kc_i = 768;
    }
    eg_timer_ = 0;
    eg_cnt_ = 0;
    lfo_timer_ = 0;
    lfo_counter_ = 0;
    lfo_phase_ = 0;
    lfo_wsel_ = 0;
    pmd_ = 0;
    amd_ = 0;
    lfa_ = 0;
    lfp_ = 0;
    test_ = 0;
    irq_enable_ = 0;
    timer_a_index_ = 0;
    timer_b_index_ = 0;
    timer_a_enabled_ = false;
    timer_b_enabled_ = false;
    timer_a_counter_ = 0;
    timer_b_counter_ = 0;
    noise_ = 0;
    noise_rng_ = 0;
    noise_p_ = 0;
    noise_f_ = noise_tab_[0];
    csm_req_ = 0;
    status_ = 0;
    irq_line_state_ = 0;
    std::memset(chanout_, 0, sizeof(chanout_));
    m2_ = c1_ = c2_ = mem_ = 0;

    write_reg(0x1b, 0);  // CT1, CT2 output pins
    write_reg(0x18, 0);  // LFO frequency
    for (int i = 0x20; i <= 0xff; ++i) write_reg(uint8_t(i), 0);
}

void YM2151::set_irq_bit(uint8_t bit) {
    const uint8_t oldstate = irq_line_state_;
    irq_line_state_ = uint8_t(irq_line_state_ | bit);
    if (oldstate == 0 && irq_handler_) irq_handler_(true);
}

void YM2151::clear_irq_bit(uint8_t bit) {
    const uint8_t oldstate = irq_line_state_;
    irq_line_state_ = uint8_t(irq_line_state_ & ~bit);
    if (oldstate != 0 && irq_line_state_ == 0 && irq_handler_) irq_handler_(false);
}

void YM2151::key_on(int op_index, uint32_t key_set) {
    Operator& op = oper_[op_index];
    if (op.key == 0) {
        op.phase = 0;
        op.state = kEgAtt;
        op.volume += (~op.volume * kEgInc[op.eg_sel_ar + ((eg_cnt_ >> op.eg_sh_ar) & 7)]) / 16;
        if (op.volume <= kMinAttIndex) {
            op.volume = kMinAttIndex;
            op.state = kEgDec;
        }
    }
    op.key |= key_set;
}

void YM2151::key_off(Operator& op, uint32_t key_clr) {
    if (op.key != 0) {
        op.key &= ~key_clr;
        if (op.key == 0 && op.state > kEgRel) op.state = kEgRel;
    }
}

void YM2151::envelope_konkoff(int op_index, uint8_t v) {
    if (v & 0x08) key_on(op_index + 0, 1); else key_off(oper_[op_index + 0], 1);
    if (v & 0x20) key_on(op_index + 1, 1); else key_off(oper_[op_index + 1], 1);
    if (v & 0x10) key_on(op_index + 2, 1); else key_off(oper_[op_index + 2], 1);
    if (v & 0x40) key_on(op_index + 3, 1); else key_off(oper_[op_index + 3], 1);
}

void YM2151::set_connect(int op_index, int channel, uint8_t v) {
    Operator& om1 = oper_[op_index];
    Operator& om2 = oper_[op_index + 1];
    Operator& oc1 = oper_[op_index + 2];

    switch (v & 7) {
        case 0:  // M1---C1---MEM---M2---C2---OUT
            om1.connect = &c1_;
            oc1.connect = &mem_;
            om2.connect = &c2_;
            om1.mem_connect = &m2_;
            break;
        case 1:  // M1------+-MEM---M2---C2---OUT, C1-+
            om1.connect = &mem_;
            oc1.connect = &mem_;
            om2.connect = &c2_;
            om1.mem_connect = &m2_;
            break;
        case 2:  // M1-----------------+-C2---OUT, C1---MEM---M2-+
            om1.connect = &c2_;
            oc1.connect = &mem_;
            om2.connect = &c2_;
            om1.mem_connect = &m2_;
            break;
        case 3:  // M1---C1---MEM------+-C2---OUT, M2-+
            om1.connect = &c1_;
            oc1.connect = &mem_;
            om2.connect = &c2_;
            om1.mem_connect = &c2_;
            break;
        case 4:  // M1---C1-+-OUT, M2---C2-+
            om1.connect = &c1_;
            oc1.connect = &chanout_[channel];
            om2.connect = &c2_;
            om1.mem_connect = &mem_;
            break;
        case 5:  // +----C1----+, M1-+-MEM---M2-+-OUT, +----C2----+
            om1.connect = nullptr;  // special mark
            oc1.connect = &chanout_[channel];
            om2.connect = &chanout_[channel];
            om1.mem_connect = &m2_;
            break;
        case 6:  // M1---C1-+, M2-+-OUT, C2-+
            om1.connect = &c1_;
            oc1.connect = &chanout_[channel];
            om2.connect = &chanout_[channel];
            om1.mem_connect = &mem_;
            break;
        default:  // M1-+, C1-+-OUT, M2-+, C2-+
            om1.connect = &chanout_[channel];
            oc1.connect = &chanout_[channel];
            om2.connect = &chanout_[channel];
            om1.mem_connect = &mem_;
            break;
    }
}

void YM2151::refresh_eg(int op_index) {
    const uint32_t kc = oper_[op_index].kc;
    for (int f = 0; f < 4; ++f) {
        Operator& op = oper_[op_index + f];
        const uint32_t v = kc >> op.ks;
        if ((op.ar + v) < (32 + 62)) {
            op.eg_sh_ar = kRates.shift[(op.ar + v) & 0x7f];
            op.eg_sel_ar = kRates.select[(op.ar + v) & 0x7f];
        } else {
            op.eg_sh_ar = 0;
            op.eg_sel_ar = 17 * kRateSteps;
        }
        op.eg_sh_d1r = kRates.shift[(op.d1r + v) & 0x7f];
        op.eg_sel_d1r = kRates.select[(op.d1r + v) & 0x7f];
        op.eg_sh_d2r = kRates.shift[(op.d2r + v) & 0x7f];
        op.eg_sel_d2r = kRates.select[(op.d2r + v) & 0x7f];
        op.eg_sh_rr = kRates.shift[(op.rr + v) & 0x7f];
        op.eg_sel_rr = kRates.select[(op.rr + v) & 0x7f];
    }
}

void YM2151::write_reg(uint8_t r, uint8_t v) {
    int op_index = (r & 0x07) * 4 + ((r & 0x18) >> 3);
    Operator* op = &oper_[op_index];

    switch (r & 0xe0) {
        case 0x00:
            switch (r) {
                case 0x01:  // LFO reset (bit 1), test register
                    test_ = v;
                    if (v & 2) lfo_phase_ = 0;
                    break;
                case 0x08:
                    envelope_konkoff((v & 7) * 4, v);
                    break;
                case 0x0f:  // noise mode enable, noise period
                    noise_ = v;
                    noise_f_ = noise_tab_[v & 0x1f];
                    break;
                case 0x10:
                    timer_a_index_ = (timer_a_index_ & 0x003) | (uint32_t(v) << 2);
                    break;
                case 0x11:
                    timer_a_index_ = (timer_a_index_ & 0x3fc) | (v & 3);
                    break;
                case 0x12:
                    timer_b_index_ = v;
                    break;
                case 0x14:  // CSM, irq flag reset, irq enable, timer start/stop
                    irq_enable_ = v;
                    if (v & 0x10) {
                        status_ &= ~1u;
                        clear_irq_bit(1);
                    }
                    if (v & 0x20) {
                        status_ &= ~2u;
                        clear_irq_bit(2);
                    }
                    if (v & 0x02) {
                        if (!timer_b_enabled_) {
                            timer_b_counter_ = int32_t(1024 * (256 - timer_b_index_));
                            timer_b_enabled_ = true;
                        }
                    } else {
                        timer_b_enabled_ = false;
                    }
                    if (v & 0x01) {
                        if (!timer_a_enabled_) {
                            timer_a_counter_ = int32_t(64 * (1024 - timer_a_index_));
                            timer_a_enabled_ = true;
                        }
                    } else {
                        timer_a_enabled_ = false;
                    }
                    break;
                case 0x18:  // LFO frequency
                    lfo_overflow_ = uint32_t(1 << ((15 - (v >> 4)) + 3)) * (1 << kLfoSh);
                    lfo_counter_add_ = 0x10 + (v & 0x0f);
                    break;
                case 0x19:
                    if (v & 0x80) pmd_ = int16_t(v & 0x7f); else amd_ = uint8_t(v & 0x7f);
                    break;
                case 0x1b:  // CT2, CT1, LFO waveform
                    ct_ = uint8_t(v >> 6);
                    lfo_wsel_ = uint8_t(v & 3);
                    if (port_handler_) port_handler_(ct_);
                    break;
                default:
                    break;
            }
            break;
        case 0x20: {
            op_index = (r & 7) * 4;
            op = &oper_[op_index];
            Operator& op2 = oper_[op_index + 1];
            Operator& op3 = oper_[op_index + 2];
            Operator& op4 = oper_[op_index + 3];
            switch (r & 0x18) {
                case 0x00:  // RL enable, feedback, connection
                    op->fb_shift = ((v >> 3) & 7) ? uint32_t(((v >> 3) & 7) + 6) : 0;
                    pan_[(r & 7) * 2] = (v & 0x40) ? 0xffffffffu : 0;
                    pan_[(r & 7) * 2 + 1] = (v & 0x80) ? 0xffffffffu : 0;
                    connect_[r & 7] = uint8_t(v & 7);
                    set_connect(op_index, r & 7, uint8_t(v & 7));
                    break;
                case 0x08: {  // key code
                    const uint32_t value = v & 0x7f;
                    if (value != op->kc) {
                        uint32_t kc_channel = (value - (value >> 2)) * 64;
                        kc_channel += 768;
                        kc_channel |= (op->kc_i & 63);
                        op->kc = value;
                        op->kc_i = kc_channel;
                        op2.kc = value;
                        op2.kc_i = kc_channel;
                        op3.kc = value;
                        op3.kc_i = kc_channel;
                        op4.kc = value;
                        op4.kc_i = kc_channel;
                        const uint32_t kc = value >> 2;
                        op->dt1 = dt1_freq_[(op->dt1_i + kc) & 0xff];
                        op->freq = ((freq_[kc_channel + op->dt2] + uint32_t(op->dt1)) * op->mul) >> 1;
                        op2.dt1 = dt1_freq_[(op2.dt1_i + kc) & 0xff];
                        op2.freq = ((freq_[kc_channel + op2.dt2] + uint32_t(op2.dt1)) * op2.mul) >> 1;
                        op3.dt1 = dt1_freq_[(op3.dt1_i + kc) & 0xff];
                        op3.freq = ((freq_[kc_channel + op3.dt2] + uint32_t(op3.dt1)) * op3.mul) >> 1;
                        op4.dt1 = dt1_freq_[(op4.dt1_i + kc) & 0xff];
                        op4.freq = ((freq_[kc_channel + op4.dt2] + uint32_t(op4.dt1)) * op4.mul) >> 1;
                        refresh_eg(op_index);
                    }
                    break;
                }
                case 0x10: {  // key fraction
                    const uint32_t value = v >> 2;
                    if (value != (op->kc_i & 63)) {
                        const uint32_t kc_channel = value | (op->kc_i & 0xffffffc0u);
                        op->kc_i = kc_channel;
                        op2.kc_i = kc_channel;
                        op3.kc_i = kc_channel;
                        op4.kc_i = kc_channel;
                        op->freq = ((freq_[kc_channel + op->dt2] + uint32_t(op->dt1)) * op->mul) >> 1;
                        op2.freq = ((freq_[kc_channel + op2.dt2] + uint32_t(op2.dt1)) * op2.mul) >> 1;
                        op3.freq = ((freq_[kc_channel + op3.dt2] + uint32_t(op3.dt1)) * op3.mul) >> 1;
                        op4.freq = ((freq_[kc_channel + op4.dt2] + uint32_t(op4.dt1)) * op4.mul) >> 1;
                    }
                    break;
                }
                default:  // 0x18: PMS, AMS
                    op->pms = (v >> 4) & 7;
                    op->ams = v & 3;
                    break;
            }
            break;
        }
        case 0x40: {  // DT1, MUL
            const uint32_t olddt1_i = op->dt1_i;
            const uint32_t oldmul = op->mul;
            op->dt1_i = uint32_t(v & 0x70) << 1;
            op->mul = (v & 0x0f) ? uint32_t((v & 0x0f) << 1) : 1;
            if (olddt1_i != op->dt1_i) op->dt1 = dt1_freq_[(op->dt1_i + (op->kc >> 2)) & 0xff];
            if (olddt1_i != op->dt1_i || oldmul != op->mul) {
                op->freq = ((freq_[op->kc_i + op->dt2] + uint32_t(op->dt1)) * op->mul) >> 1;
            }
            break;
        }
        case 0x60:  // TL
            op->tl = uint32_t(v & 0x7f) << (kEnvBits - 7);
            break;
        case 0x80: {  // KS, AR
            const uint32_t oldks = op->ks;
            const uint32_t oldar = op->ar;
            op->ks = 5 - (v >> 6);
            op->ar = (v & 0x1f) ? uint32_t(32 + ((v & 0x1f) << 1)) : 0;
            if (op->ar != oldar || op->ks != oldks) {
                if ((op->ar + (op->kc >> op->ks)) < (32 + 62)) {
                    op->eg_sh_ar = kRates.shift[(op->ar + (op->kc >> op->ks)) & 0x7f];
                    op->eg_sel_ar = kRates.select[(op->ar + (op->kc >> op->ks)) & 0x7f];
                } else {
                    op->eg_sh_ar = 0;
                    op->eg_sel_ar = 17 * kRateSteps;
                }
            }
            if (op->ks != oldks) {
                op->eg_sh_d1r = kRates.shift[(op->d1r + (op->kc >> op->ks)) & 0x7f];
                op->eg_sel_d1r = kRates.select[(op->d1r + (op->kc >> op->ks)) & 0x7f];
                op->eg_sh_d2r = kRates.shift[(op->d2r + (op->kc >> op->ks)) & 0x7f];
                op->eg_sel_d2r = kRates.select[(op->d2r + (op->kc >> op->ks)) & 0x7f];
                op->eg_sh_rr = kRates.shift[(op->rr + (op->kc >> op->ks)) & 0x7f];
                op->eg_sel_rr = kRates.select[(op->rr + (op->kc >> op->ks)) & 0x7f];
            }
            break;
        }
        case 0xa0:  // LFO AM enable, D1R
            op->am_mask = (v & 0x80) ? 0xffffffffu : 0;
            op->d1r = (v & 0x1f) ? uint32_t(32 + ((v & 0x1f) << 1)) : 0;
            op->eg_sh_d1r = kRates.shift[(op->d1r + (op->kc >> op->ks)) & 0x7f];
            op->eg_sel_d1r = kRates.select[(op->d1r + (op->kc >> op->ks)) & 0x7f];
            break;
        case 0xc0: {  // DT2, D2R
            const uint32_t olddt2 = op->dt2;
            op->dt2 = kDt2Tab[(v >> 6) & 3];
            if (op->dt2 != olddt2) {
                op->freq = ((freq_[op->kc_i + op->dt2] + uint32_t(op->dt1)) * op->mul) >> 1;
            }
            op->d2r = (v & 0x1f) ? uint32_t(32 + ((v & 0x1f) << 1)) : 0;
            op->eg_sh_d2r = kRates.shift[(op->d2r + (op->kc >> op->ks)) & 0x7f];
            op->eg_sel_d2r = kRates.select[(op->d2r + (op->kc >> op->ks)) & 0x7f];
            break;
        }
        default:  // 0xe0: D1L, RR
            op->d1l = kWaves.d1l[(v >> 4) & 0x0f];
            op->rr = uint32_t(34 + ((v & 0x0f) << 2));
            op->eg_sh_rr = kRates.shift[(op->rr + (op->kc >> op->ks)) & 0x7f];
            op->eg_sel_rr = kRates.select[(op->rr + (op->kc >> op->ks)) & 0x7f];
            break;
    }
}

void YM2151::timer_a_expired() {
    if (irq_enable_ & 0x04) {
        status_ |= 1;
        set_irq_bit(1);
    }
    if (irq_enable_ & 0x80) csm_req_ = 2;
}

void YM2151::timer_b_expired() {
    if (irq_enable_ & 0x08) {
        status_ |= 2;
        set_irq_bit(2);
    }
}

void YM2151::run_timers(int cycles) {
    if (timer_a_enabled_) {
        timer_a_counter_ -= cycles;
        while (timer_a_counter_ <= 0) {
            timer_a_counter_ += int32_t(64 * (1024 - timer_a_index_));
            timer_a_expired();
        }
    }
    if (timer_b_enabled_) {
        timer_b_counter_ -= cycles;
        while (timer_b_counter_ <= 0) {
            timer_b_counter_ += int32_t(1024 * (256 - timer_b_index_));
            timer_b_expired();
        }
    }
}

int32_t YM2151::op_calc(const Operator& op, uint32_t env, int32_t pm) {
    const int64_t tmp = int64_t(int32_t(op.phase & ~kFreqMask)) + (int64_t(pm) << 15);
    const uint32_t p = (env << 3) + kWaves.sin[sshr(tmp, kFreqSh) & kSinMask];
    return (p >= kTlTabLen) ? 0 : kWaves.tl[p];
}

int32_t YM2151::op_calc1(const Operator& op, uint32_t env, int32_t pm) {
    const int32_t i = int32_t(op.phase & ~kFreqMask) + pm;
    const uint32_t p = (env << 3) + kWaves.sin[sshr(i, kFreqSh) & kSinMask];
    return (p >= kTlTabLen) ? 0 : kWaves.tl[p];
}

uint32_t YM2151::volume_calc(const Operator& op, uint32_t am) {
    return op.tl + uint32_t(op.volume) + (am & op.am_mask);
}

void YM2151::chan_calc(int channel) {
    uint32_t am = 0;
    m2_ = c1_ = c2_ = mem_ = 0;
    Operator& op = oper_[channel * 4];
    Operator& op2 = oper_[channel * 4 + 1];
    Operator& op3 = oper_[channel * 4 + 2];
    Operator& op4 = oper_[channel * 4 + 3];

    *op.mem_connect = op.mem_value;  // restore the delayed sample

    if (op.ams != 0) am = lfa_ << (op.ams - 1);
    uint32_t env = volume_calc(op, am);

    int32_t out = op.fb_out_prev + op.fb_out_curr;
    op.fb_out_prev = op.fb_out_curr;

    if (op.connect == nullptr) {  // algorithm 5
        mem_ = c1_ = c2_ = op.fb_out_prev;
    } else {
        *op.connect = op.fb_out_prev;
    }
    op.fb_out_curr = 0;
    if (env < kEnvQuiet) {
        if (op.fb_shift == 0) out = 0;
        op.fb_out_curr = op_calc1(op, env, out << op.fb_shift);
    }

    env = volume_calc(op2, am);
    if (env < kEnvQuiet) *op2.connect += op_calc(op2, env, m2_);

    env = volume_calc(op3, am);
    if (env < kEnvQuiet) *op3.connect += op_calc(op3, env, c1_);

    env = volume_calc(op4, am);
    if (env < kEnvQuiet) chanout_[channel] += op_calc(op4, env, c2_);

    op.mem_value = mem_;
}

void YM2151::chan7_calc() {
    uint32_t am = 0;
    m2_ = c1_ = c2_ = mem_ = 0;
    Operator& op = oper_[7 * 4];
    Operator& op2 = oper_[7 * 4 + 1];
    Operator& op3 = oper_[7 * 4 + 2];
    Operator& op4 = oper_[7 * 4 + 3];

    *op.mem_connect = op.mem_value;

    if (op.ams != 0) am = lfa_ << (op.ams - 1);
    uint32_t env = volume_calc(op, am);

    int32_t out = op.fb_out_prev + op.fb_out_curr;
    op.fb_out_prev = op.fb_out_curr;

    if (op.connect == nullptr) {  // algorithm 5
        mem_ = c1_ = c2_ = op.fb_out_prev;
    } else {
        *op.connect = op.fb_out_prev;
    }
    op.fb_out_curr = 0;
    if (env < kEnvQuiet) {
        if (op.fb_shift == 0) out = 0;
        op.fb_out_curr = op_calc1(op, env, out << op.fb_shift);
    }

    env = volume_calc(op2, am);
    if (env < kEnvQuiet) *op2.connect += op_calc(op2, env, m2_);

    env = volume_calc(op3, am);
    if (env < kEnvQuiet) *op3.connect += op_calc(op3, env, c1_);

    env = volume_calc(op4, am);
    if (noise_ & 0x80) {
        uint32_t noiseout = 0;
        // the range of the YM2151 noise output is -2044 to 2040
        if (env < 0x3ff) noiseout = (env ^ 0x3ff) * 2;
        if (noise_rng_ & 0x10000) {
            chanout_[7] += int32_t(noiseout);
        } else {
            chanout_[7] -= int32_t(noiseout);
        }
    } else if (env < kEnvQuiet) {
        chanout_[7] += op_calc(op4, env, c2_);
    }

    op.mem_value = mem_;
}

void YM2151::advance_eg() {
    eg_timer_ += eg_timer_add_;
    while (eg_timer_ >= eg_timer_overflow_) {
        eg_timer_ -= eg_timer_overflow_;
        eg_cnt_++;
        for (int i = 0; i < 32; ++i) {
            Operator& op = oper_[i];
            switch (op.state) {
                case kEgAtt:
                    if ((eg_cnt_ & ((1u << op.eg_sh_ar) - 1)) == 0) {
                        op.volume +=
                            (~op.volume * kEgInc[op.eg_sel_ar + ((eg_cnt_ >> op.eg_sh_ar) & 7)]) / 16;
                        if (op.volume <= kMinAttIndex) {
                            op.volume = kMinAttIndex;
                            op.state = kEgDec;
                        }
                    }
                    break;
                case kEgDec:
                    if ((eg_cnt_ & ((1u << op.eg_sh_d1r) - 1)) == 0) {
                        op.volume += kEgInc[op.eg_sel_d1r + ((eg_cnt_ >> op.eg_sh_d1r) & 7)];
                        if (uint32_t(op.volume) >= op.d1l) op.state = kEgSus;
                    }
                    break;
                case kEgSus:
                    if ((eg_cnt_ & ((1u << op.eg_sh_d2r) - 1)) == 0) {
                        op.volume += kEgInc[op.eg_sel_d2r + ((eg_cnt_ >> op.eg_sh_d2r) & 7)];
                        if (op.volume >= kMaxAttIndex) {
                            op.volume = kMaxAttIndex;
                            op.state = kEgOff;
                        }
                    }
                    break;
                case kEgRel:
                    if ((eg_cnt_ & ((1u << op.eg_sh_rr) - 1)) == 0) {
                        op.volume += kEgInc[op.eg_sel_rr + ((eg_cnt_ >> op.eg_sh_rr) & 7)];
                        if (op.volume >= kMaxAttIndex) {
                            op.volume = kMaxAttIndex;
                            op.state = kEgOff;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

void YM2151::advance() {
    if (test_ & 2) {
        lfo_phase_ = 0;
    } else {
        lfo_timer_ += lfo_timer_add_;
        if (lfo_timer_ >= lfo_overflow_) {
            lfo_timer_ -= lfo_overflow_;
            lfo_counter_ += lfo_counter_add_;
            lfo_phase_ = (lfo_phase_ + (lfo_counter_ >> 4)) & 255;
            lfo_counter_ &= 15;
        }
    }

    const int i = int(lfo_phase_);
    int a = 0, p = 0;
    switch (lfo_wsel_) {
        case 0:  // saw
            a = 255 - i;
            p = (i < 128) ? i : i - 255;
            break;
        case 1:  // square
            if (i < 128) {
                a = 255;
                p = 128;
            } else {
                a = 0;
                p = -128;
            }
            break;
        case 2:  // triangle
            a = (i < 128) ? 255 - (i * 2) : (i * 2) - 256;
            if (i < 64) p = i * 2;
            else if (i < 128) p = 255 - i * 2;
            else if (i < 192) p = 256 - i * 2;
            else p = i * 2 - 511;
            break;
        default:  // noise, a snapshot of data from the real chip
            a = kLfoNoiseWaveform[i];
            p = a - 128;
            break;
    }
    lfa_ = uint32_t(a * amd_ / 128);
    lfp_ = int32_t(p * pmd_ / 128);

    // 17 bit shift register, bit 16 is the noise output
    noise_p_ += noise_f_;
    uint32_t shifts = noise_p_ >> 16;
    noise_p_ &= 0xffff;
    while (shifts != 0) {
        const uint32_t j = ((noise_rng_ ^ (noise_rng_ >> 3)) & 1) ^ 1;
        noise_rng_ = (j << 16) | (noise_rng_ >> 1);
        shifts--;
    }

    for (int channel = 0; channel < 8; ++channel) {
        Operator& op = oper_[channel * 4];
        Operator& op2 = oper_[channel * 4 + 1];
        Operator& op3 = oper_[channel * 4 + 2];
        Operator& op4 = oper_[channel * 4 + 3];
        int32_t mod_ind = 0;
        if (op.pms != 0) {
            mod_ind = lfp_;
            mod_ind = (op.pms < 6) ? sshr(mod_ind, int(6 - op.pms)) : (mod_ind << (op.pms - 5));
        }
        if (op.pms != 0 && mod_ind != 0) {
            const uint32_t kc_channel = op.kc_i + uint32_t(mod_ind);
            op.phase += sshr(int32_t((freq_[kc_channel + op.dt2] + uint32_t(op.dt1)) * op.mul), 1);
            op2.phase += sshr(int32_t((freq_[kc_channel + op2.dt2] + uint32_t(op2.dt1)) * op2.mul), 1);
            op3.phase += sshr(int32_t((freq_[kc_channel + op3.dt2] + uint32_t(op3.dt1)) * op3.mul), 1);
            op4.phase += sshr(int32_t((freq_[kc_channel + op4.dt2] + uint32_t(op4.dt1)) * op4.mul), 1);
        } else {
            op.phase += op.freq;
            op2.phase += op2.freq;
            op3.phase += op3.freq;
            op4.phase += op4.freq;
        }
    }

    // CSM is calculated after the phase generator (verified on the real chip)
    if (csm_req_ != 0) {
        if (csm_req_ == 2) {
            for (int i2 = 0; i2 < 32; ++i2) key_on(i2, 2);
            csm_req_ = 1;
        } else {
            for (int i2 = 0; i2 < 32; ++i2) key_off(oper_[i2], 2);
            csm_req_ = 0;
        }
    }
}

int32_t YM2151::update() {
    advance_eg();
    std::memset(chanout_, 0, sizeof(chanout_));

    for (int channel = 0; channel < 7; ++channel) chan_calc(channel);
    chan7_calc();

    int32_t outl = 0, outr = 0;
    for (int channel = 0; channel < 8; ++channel) {
        outl += chanout_[channel] & int32_t(pan_[channel * 2]);
        outr += chanout_[channel] & int32_t(pan_[channel * 2 + 1]);
    }
    outl = std::min(std::max(outl, kMinOut), kMaxOut);
    outr = std::min(std::max(outr, kMinOut), kMaxOut);
    out_left_ = outl;
    out_right_ = outr;
    advance();

    const int32_t mono = int32_t((outl + outr) * amplitude_);
    return std::min(std::max(mono, -32767), 32767);
}

}  // namespace dsp
