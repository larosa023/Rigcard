// Rigcard - a built-in catalog of popular parts Windows can't detect, for the search-as-you-type picker.
// Free text always works too; this just saves typing.
#include "partsdb.h"
#include "util.h"
#include <algorithm>

namespace partsdb {

static const wchar_t* kCoolers[] = {
    L"Noctua NH-D15", L"Noctua NH-D15 G2", L"Noctua NH-U12A", L"Noctua NH-U12S", L"Noctua NH-L9i", L"Noctua NH-U9S",
    L"Thermalright Peerless Assassin 120 SE", L"Thermalright Phantom Spirit 120 SE", L"Thermalright Frozen Notte 360", L"Thermalright Aqua Elite 360",
    L"be quiet! Dark Rock Pro 5", L"be quiet! Dark Rock 4", L"be quiet! Pure Rock 2", L"be quiet! Silent Loop 3 360",
    L"Arctic Liquid Freezer III 360", L"Arctic Liquid Freezer III 280", L"Arctic Liquid Freezer III 420", L"Arctic Liquid Freezer II 360", L"Arctic Freezer 36",
    L"Corsair iCUE H150i Elite LCD XT", L"Corsair iCUE H150i Elite Capellix XT", L"Corsair iCUE LINK H150i RGB", L"Corsair H100i RGB Elite",
    L"Corsair iCUE LINK TITAN 360 RX", L"NZXT Kraken Elite 360", L"NZXT Kraken 360", L"NZXT Kraken 280", L"NZXT Kraken Plus 360",
    L"Lian Li Galahad II Trinity 360", L"Lian Li HydroShift LCD 360", L"Deepcool AK620", L"Deepcool AK400", L"Deepcool LT720", L"Deepcool LS720",
    L"Cooler Master Hyper 212", L"Cooler Master MasterLiquid 360L Core", L"EK Nucleus AIO CR360", L"ID-COOLING SE-224-XTS", L"ID-COOLING FROZN A620",
    L"MSI MAG CoreLiquid E360", L"ASUS ROG Ryujin III 360", L"ASUS ROG Strix LC III 360", L"ASUS TUF Gaming LC II 360", L"AMD Wraith Prism", L"Intel Laminar RM1",
    L"Custom water loop",
};
static const wchar_t* kPsus[] = {
    L"Corsair RM1000x (2024)", L"Corsair RM850x (2024)", L"Corsair RM750e", L"Corsair HX1500i", L"Corsair HX1200i", L"Corsair SF750", L"Corsair SF1000",
    L"Seasonic Vertex GX-1000", L"Seasonic Focus GX-850", L"Seasonic Focus GX-750", L"Seasonic Prime TX-1600", L"Seasonic Prime TX-1000",
    L"be quiet! Dark Power 13 1000W", L"be quiet! Straight Power 12 1000W", L"be quiet! Pure Power 12 M 850W",
    L"MSI MPG A1000G PCIE5", L"MSI MAG A850GL PCIE5", L"ASUS ROG Thor 1200P2", L"ASUS ROG Strix 1000W Gold Aura", L"ASUS TUF Gaming 1000W Gold",
    L"EVGA SuperNOVA 1000 G6", L"EVGA SuperNOVA 850 G6", L"Thermaltake Toughpower GF3 1000W", L"Cooler Master MWE Gold 850 V2", L"Cooler Master V1100 SFX Platinum",
    L"Lian Li Edge 1300W", L"Lian Li SP850", L"NZXT C1200 Gold", L"NZXT C850 Gold", L"FSP Hydro Ti Pro 1000W", L"Super Flower Leadex VII XG 1000W",
    L"Phanteks AMP GH 1000W", L"Montech Titan Gold 1200W",
};
static const wchar_t* kCases[] = {
    L"Lian Li O11 Dynamic EVO", L"Lian Li O11 Dynamic EVO XL", L"Lian Li O11 Vision", L"Lian Li O11 Vision Compact", L"Lian Li Lancool 216", L"Lian Li Lancool 207",
    L"Lian Li Lancool III", L"Lian Li A3-mATX", L"Lian Li Vector V100", L"Fractal Design North", L"Fractal Design North XL", L"Fractal Design Torrent",
    L"Fractal Design Meshify 2", L"Fractal Design Pop Air", L"Fractal Design Terra", L"Fractal Design Ridge", L"NZXT H9 Flow", L"NZXT H9 Elite", L"NZXT H7 Flow",
    L"NZXT H6 Flow", L"NZXT H5 Flow", L"NZXT H1 V2", L"Corsair 6500X", L"Corsair 5000D Airflow", L"Corsair 4000D Airflow", L"Corsair 3500X", L"Corsair 2500X",
    L"HYTE Y70", L"HYTE Y60", L"HYTE Y40", L"Phanteks NV7", L"Phanteks NV5", L"Phanteks Evolv X2", L"Phanteks XT Pro Ultra", L"be quiet! Dark Base Pro 901",
    L"be quiet! Pure Base 500DX", L"be quiet! Light Base 900 DX", L"Montech King 95 Pro", L"Montech AIR 903 MAX", L"Jonsbo D41", L"Jonsbo D31", L"Jonsbo Z20",
    L"Cooler Master HAF 700", L"Cooler Master NR200P", L"Thermaltake View 380", L"Thermaltake Core P3", L"SSUPD Meshroom S", L"Antec C8", L"Antec Flux Pro",
    L"Sama Neview 4151", L"Asus ProArt PA602", L"ASUS ROG Hyperion GR701",
};
static const wchar_t* kFans[] = {
    L"Noctua NF-A12x25", L"Noctua NF-A14", L"Noctua NF-A12x25 G2", L"Arctic P12", L"Arctic P14", L"Arctic P12 Max", L"Lian Li UNI FAN SL-Infinity",
    L"Lian Li UNI FAN TL", L"Lian Li UNI FAN SL120 V2", L"Corsair iCUE LINK RX120 RGB", L"Corsair LL120 RGB", L"Corsair QX120 RGB", L"be quiet! Silent Wings 4",
    L"be quiet! Light Wings", L"Phanteks T30-120", L"Thermalright TL-C12C", L"NZXT F120 RGB Core", L"Fractal Design Aspect 12", L"Cooler Master Mobius 120",
    L"3 × 120 mm intake, 1 × 140 mm exhaust", L"6 × 120 mm",
};
static const wchar_t* kKeyboards[] = {
    L"AULA F75", L"Keychron Q1 Max", L"Keychron V1", L"Keychron K2", L"Wooting 60HE+", L"Wooting 80HE", L"Logitech G915 TKL", L"Logitech G915 X",
    L"Logitech G Pro X TKL", L"Logitech MX Keys S", L"Razer Huntsman V3 Pro", L"Razer BlackWidow V4 Pro", L"SteelSeries Apex Pro TKL", L"Corsair K70 RGB Pro",
    L"Corsair K65 Plus Wireless", L"NuPhy Air75 V2", L"Glorious GMMK 3", L"Ducky One 3", L"HHKB Professional Hybrid", L"Leopold FC900R",
    L"Akko 5075B Plus", L"MonsGeek M1W", L"Asus ROG Azoth",
};
static const wchar_t* kMice[] = {
    L"Logitech G Pro X Superlight 2", L"Logitech G Pro X Superlight", L"Logitech G502 X Plus", L"Logitech G305", L"Logitech MX Master 3S",
    L"Razer DeathAdder V3 Pro", L"Razer Viper V3 Pro", L"Razer Basilisk V3 Pro", L"Zowie EC2-CW", L"Zowie FK2-C", L"Pulsar X2 V2", L"Lamzu Atlantis",
    L"Finalmouse UltralightX", L"SteelSeries Aerox 5 Wireless", L"Corsair M75 Wireless", L"Glorious Model O 2", L"VAXEE XE", L"Endgame Gear OP1 8k",
};
static const wchar_t* kHeadsets[] = {
    L"SteelSeries Arctis Nova Pro Wireless", L"SteelSeries Arctis Nova 7", L"Logitech G Pro X 2 Lightspeed", L"Razer BlackShark V2 Pro", L"HyperX Cloud III",
    L"HyperX Cloud Alpha Wireless", L"Corsair Virtuoso Max", L"Audeze Maxwell", L"Sennheiser HD 600", L"Sennheiser HD 660S2", L"Sennheiser HD 560S",
    L"Beyerdynamic DT 990 Pro", L"Beyerdynamic DT 770 Pro", L"Sony WH-1000XM5", L"Apple AirPods Pro 3", L"Nothing Ear (3)", L"Moondrop Blessing 3",
    L"Schiit Modi + Magni", L"FiiO K7", L"Focusrite Scarlett 2i2", L"Edifier R1280T", L"KEF LSX II", L"Audioengine A2+",
};
static const wchar_t* kMics[] = {
    L"Shure SM7B", L"Shure MV7+", L"Elgato Wave:3", L"Rode PodMic USB", L"Rode NT-USB+", L"HyperX QuadCast S", L"Blue Yeti", L"Audio-Technica AT2020USB-X",
    L"Razer Seiren V3 Pro", L"Fifine AM8",
};
static const wchar_t* kMonitors[] = {
    L"ASUS ROG Swift PG27UCDM", L"ASUS ROG Swift PG32UCDM", L"ASUS ROG Swift PG27AQDM", L"Alienware AW3225QF", L"Alienware AW2725DF", L"Alienware AW3423DWF",
    L"LG UltraGear 27GR95QE", L"LG UltraGear 32GS95UE", L"LG UltraGear 27GS95QE", L"Samsung Odyssey OLED G8", L"Samsung Odyssey OLED G9", L"Samsung Odyssey G7",
    L"MSI MPG 321URX", L"MSI MAG 271QPX", L"Gigabyte FO32U2P", L"Dell U2723QE", L"BenQ Zowie XL2566K", L"BenQ MOBIUZ EX2710Q", L"Acer Predator X27U",
};
static const wchar_t* kOther[] = {
    L"Elgato Stream Deck MK.2", L"Elgato Stream Deck +", L"Elgato Facecam MK.2", L"Logitech C920", L"Logitech Brio 4K", L"Elgato 4K60 Pro MK.2",
    L"Xbox Wireless Controller", L"PlayStation DualSense Edge", L"8BitDo Ultimate Controller", L"Artisan Hien mousepad", L"Glorious XXL mousepad",
};

#define LIST(a) std::vector<std::wstring>(std::begin(a), std::end(a))

const std::vector<std::wstring>& Catalog(const std::wstring& kind) {
    static std::vector<std::wstring> coolers = LIST(kCoolers), psus = LIST(kPsus), cases = LIST(kCases), fans = LIST(kFans), keyboards = LIST(kKeyboards),
        mice = LIST(kMice), headsets = LIST(kHeadsets), mics = LIST(kMics), monitors = LIST(kMonitors), other = LIST(kOther), none;
    if (kind == L"cooler") return coolers;
    if (kind == L"psu") return psus;
    if (kind == L"case") return cases;
    if (kind == L"fans") return fans;
    if (kind == L"keyboard") return keyboards;
    if (kind == L"mouse") return mice;
    if (kind == L"headset") return headsets;
    if (kind == L"mic") return mics;
    if (kind == L"monitor") return monitors;
    if (kind == L"other") return other;
    return none;
}

// every word you typed appears somewhere in the name; names that start with what you typed come first
std::vector<std::wstring> Search(const std::wstring& kind, const std::wstring& query, size_t max) {
    std::vector<std::wstring> words;
    std::wstring q = Lower(Trim(query)), cur;
    for (wchar_t c : q + L" ") {
        if (c == L' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); }
        else cur += c;
    }
    std::vector<std::pair<int, std::wstring>> hits;
    for (auto& name : Catalog(kind)) {
        std::wstring l = Lower(name);
        bool all = true;
        for (auto& w : words) all = all && l.find(w) != std::wstring::npos;
        if (!all) continue;
        int rank = words.empty() ? 1 : (IStartsWith(l, q) ? 0 : l.find(L" " + words[0]) != std::wstring::npos ? 1 : 2);
        hits.push_back({rank, name});
    }
    std::stable_sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<std::wstring> out;
    for (auto& h : hits) { out.push_back(h.second); if (out.size() >= max) break; }
    return out;
}

} // namespace partsdb
