// TesmioMenu - a configurable bottom build-menu tab for TesmioLoader.
//
// Target game build: WRSR 1.1.1.9.
// 296644A9F207D609031FC2AE73FED2DCB34619A1D55A35D1C7B51965CE6841B8
//
// The game already stores bottom tabs in a std::vector-like array. This plugin
// hooks the vanilla menu initializer, lets it build all 26 stock entries, then
// appends one more stock entry. The renderer, DPI scaling, hover state and
// click handling therefore remain the game's own code.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "../include/tesmio_api.h"

static const TsmHost* H;
static unsigned char* g_base;

static const char* INI = "plugins\\tesmiomenu.ini";
static const char* ENGINE_DLL = "C3DDLL64.dll";
static const char* GET_STRING = "?GetString@C3D_LANGUAGE@@QEAAPEA_WH@Z";

// WRSR 1.1.1.9 RVAs.
static const uintptr_t P_MENU_INIT          = 0x051E30;
static const uintptr_t P_BOTTOM_MENU_RENDER = 0x4D72C0;
static const uintptr_t P_CONSTRUCTION_RENDER = 0x07C400;
static const uintptr_t P_TAB_INIT           = 0x079430;
static const uintptr_t P_TAB_PUSH           = 0x08E3C0;
static const uintptr_t P_TAB_CONSTRUCT      = 0x051CA0;
static const uintptr_t P_BOTTOM_PAPER_LEFT  = 0x0814D8;
static const uintptr_t P_FIRST_BAND_START   = 0x081794;
static const uintptr_t P_GROUP_INIT         = 0x079500;
static const uintptr_t P_GROUP_PUSH         = 0x08E500;
static const uintptr_t P_GROUP_CONSTRUCT    = 0x051B80;
static const uintptr_t P_POINTER_PUSH       = 0x01DE20;
static const uintptr_t P_INT_PUSH           = 0x01DF90;
static const uintptr_t P_TOOL_FIND          = 0x03AAA0;
static const uintptr_t P_RESEARCH_FOR_BUILDING = 0x2F76C0;
static const uintptr_t P_RESEARCH_FOR_TOOL  = 0x2F7960;
static const uintptr_t G_BOTTOM_TABS        = 0x9E17D8;
static const uintptr_t G_GAME               = 0x9D4F10;
static const uintptr_t G_SELECTED_BOTTOM_TAB = G_BOTTOM_TABS + 0x18;
static const uintptr_t G_LANDSCAPE_ROOT     = 0x9941F0;
static const uintptr_t G_UI_SCALE           = 0x992088;

static const size_t TAB_SIZE = 0xB0;
static const size_t GROUP_SIZE = 0x30;
static const size_t TAB_GROUPS = 0x50;
static const size_t TOOL_VECTOR = 0xD280;
static const size_t TOOL_SIZE = 0x2D0;
static const size_t TOOL_BUILDING = 0x48;
static const size_t TOOL_PREVIEW_PATH = 0xB4;
static const size_t BUILDING_TYPE_SCAN = 0xBE8;

static const int DEFAULT_TEXT_ID = 1200001;
static const char* const CATALOG_UNDEFINED_TYPE = "__tesmio_undefined";
// The custom tab becomes the new standalone group 0. Vanilla groups are
// shifted from 0..5 to 1..6 after their initializer has completed.
static const int DEFAULT_GROUP = 0;
static const int MAX_BUILDINGS = 64;
static const int MAX_CATALOG_RESOURCES = 320;
static const int MAX_ITEM_RESOURCES = 24;
static const int MAX_CATALOG_TYPES = 256;
static const int MAX_CATALOG_ITEMS = 4096;
static const int MAX_CATALOG_NEEDS = 64;
static const int MAX_TOOLTIP_RESOURCES = MAX_ITEM_RESOURCES * 3;

// Catalog data is deliberately independent from the eventual window renderer.
// The native game window can be replaced without changing how content is
// discovered and classified.
struct CatalogResource
{
    char name[64];
    char templateName[64];
    wchar_t display[96];
    wchar_t englishDisplay[96];
    bool modded;
};

struct CatalogNeed
{
    char resource[64];
    char donor[64];
    char category[32];
};

struct CatalogItemMetadata
{
    char objectName[96];
    char type[64];
    char produces[MAX_ITEM_RESOURCES][64];
    char consumes[MAX_ITEM_RESOURCES][64];
    char stores[MAX_ITEM_RESOURCES][64];
    int produceCount;
    int consumeCount;
    int storeCount;
};

struct CatalogType
{
    unsigned char* tab;
    char name[64];
    wchar_t display[96];
    int textId;
};

enum CatalogSource
{
    CATALOG_SOURCE_VANILLA = 0,
    CATALOG_SOURCE_WORKSHOP = 1,
    CATALOG_SOURCE_TESMIO = 2
};

struct CatalogItem
{
    void* tool;
    void* buildingType;
    int typeIndex;
    int nameTextId;
    CatalogSource source;
    char toolName[128];
    char previewPath[260];
    char fallbackPreviewPath[260];
    char descriptorPath[512];
    wchar_t display[128];
    void* previewTexture;
    bool previewAttempted;
    bool favorite;
    bool availableCached;
    unsigned availabilityCacheEpoch;
    int availabilitySettingsReason;
    int availabilityResearchCount;
    int availabilityResearchNameIds[4];
    void* availabilityResearchTexture;
    CatalogItemMetadata metadata;
};

struct RawVector
{
    unsigned char* begin;
    unsigned char* end;
    unsigned char* capacity;
};

typedef void  (*t_MenuInit)(void);
typedef void* (*t_InitObject)(void*);
typedef void  (*t_PushObject)(void*, const void*);
typedef void  (*t_TabConstruct)(void*, const char*, int, int, const float*);
typedef void  (*t_GroupConstruct)(void*, void*, int, int);
typedef void  (*t_PushPointer)(void*, const void*);
typedef void  (*t_PushInt)(void*, const int*);
typedef void* (*t_FindTool)(void*, const char*);
typedef wchar_t* (*t_GetString)(void*, int);
typedef void (*t_CollectResearch)(void*, void*);

static t_MenuInit o_MenuInit;
static t_TabConstruct o_TabConstruct;
static t_GetString o_GetString;
static void* g_languageObject = NULL;

static int g_enabled = 1;
static int g_group = DEFAULT_GROUP;
static int g_textId = DEFAULT_TEXT_ID;
static int g_probe = 1;
static int g_front = 1;
static int g_bottomMenuLevel1Scale = 0;
static wchar_t g_title[96] = L"Tesmio Catalog";
static bool g_insideMenuInit = false;
static bool g_nativeFrontInserted = false;
static volatile LONG g_nativeBottomPaperLeftPixels = -1;
static void* g_bottomPaperCode = NULL;
static void* g_firstBandCode = NULL;
static void* g_followingBandsCode = NULL;
static CatalogResource g_catalogResources[MAX_CATALOG_RESOURCES];
static int g_catalogResourceCount = 0;
static CatalogNeed g_catalogNeeds[MAX_CATALOG_NEEDS];
static int g_catalogNeedCount = 0;
static CatalogType g_catalogTypes[MAX_CATALOG_TYPES];
static int g_catalogTypeCount = 0;
static CatalogItem g_catalogItems[MAX_CATALOG_ITEMS];
static int g_catalogItemCount = 0;
static unsigned char* g_englishText = NULL;
static size_t g_englishTextSize = 0;
static uint32_t g_englishTextCount = 0;
static size_t g_englishTextBase = 0;
static unsigned char* g_russianText = NULL;
static size_t g_russianTextSize = 0;
static uint32_t g_russianTextCount = 0;
static size_t g_russianTextBase = 0;

enum CatalogUiLanguage
{
    CATALOG_LANGUAGE_ENGLISH = 0,
    CATALOG_LANGUAGE_RUSSIAN = 1
};

enum CatalogUiText
{
    UI_CATALOG_TITLE,
    UI_TYPE,
    UI_RESOURCE,
    UI_SOURCE,
    UI_ALL,
    UI_NONE,
    UI_TWO_SELECTED,
    UI_VANILLA_SHORT,
    UI_WORKSHOP_SHORT,
    UI_TESMIO,
    UI_ONLY_AVAILABLE,
    UI_FAVORITES,
    UI_VANILLA_BUILDINGS,
    UI_WORKSHOP_BUILDINGS,
    UI_SOURCE_VANILLA,
    UI_SOURCE_WORKSHOP,
    UI_TYPE_PREFIX,
    UI_RESOURCE_PREFIX,
    UI_SOURCE_PREFIX,
    UI_PRODUCES_PREFIX,
    UI_CONSUMES_PREFIX,
    UI_UNAVAILABLE,
    UI_REQUIRED_RESEARCH,
    UI_LANDSCAPE_UNAVAILABLE,
    UI_SETTINGS_UNAVAILABLE,
    UI_FILTER_CONSUMES,
    UI_FILTER_PRODUCES,
    UI_COUNT
};

static CatalogUiLanguage g_catalogLanguage = CATALOG_LANGUAGE_ENGLISH;
static bool g_catalogLanguageDetected = false;
static const wchar_t* const g_catalogUiText[2][UI_COUNT] = {
    {
        L"Tesmio Catalog", L"Type", L"Resource", L"Source", L"All", L"None",
        L"2 selected", L"Vanilla", L"Workshop", L"Tesmio", L"Only available",
        L"Favorites", L"Vanilla buildings", L"Workshop buildings", L"Vanilla",
        L"Workshop", L"Type: ", L"Resource: ", L"Source: ", L"Produces: ",
        L"Consumes: ", L"Unavailable", L"Required research",
        L"This item is not available for the current landscape.",
        L"This feature is disabled in the current game settings.",
        L"Consumes", L"Produces"
    },
    {
        L"Каталог Tesmio", L"Тип", L"Ресурс", L"Источник", L"Все", L"Нет",
        L"Выбрано: 2", L"Базовые", L"Мастерская", L"Tesmio",
        L"Только доступные", L"Избранное", L"Базовые здания",
        L"Здания из Мастерской", L"Базовая игра", L"Мастерская",
        L"Тип: ", L"Ресурс: ", L"Источник: ", L"Производит: ",
        L"Потребляет: ", L"Недоступно", L"Требуется исследование",
        L"Этот объект недоступен для текущего ландшафта.",
        L"Эта функция отключена в настройках текущей игры.",
        L"Потребляет", L"Производит"
    }
};

static const wchar_t* Ui(CatalogUiText text)
{
    return g_catalogUiText[(int)g_catalogLanguage][(int)text];
}

static void FormatCatalogResultCount(int count, wchar_t* destination,
                                     size_t capacity)
{
    if (g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN)
    {
        int lastTwo = count % 100;
        int last = count % 10;
        const wchar_t* word = (lastTwo >= 11 && lastTwo <= 14)
            ? L"объектов"
            : (last == 1 ? L"объект"
                         : (last >= 2 && last <= 4 ? L"объекта"
                                                  : L"объектов"));
        swprintf_s(destination, capacity, L"%d %s", count, word);
    }
    else
        swprintf_s(destination, capacity, count == 1 ? L"1 result" : L"%d results",
                   count);
}

static void FormatCatalogPage(int page, int pageCount, wchar_t* destination,
                              size_t capacity)
{
    swprintf_s(destination, capacity,
               g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN
                   ? L"Страница %d / %d"
                   : L"Page %d / %d",
               page, pageCount);
}

static void DetectCatalogLanguage();
static void* g_catalogBuildingTools[MAX_BUILDINGS] = {};
static int g_catalogBuildingCount = 0;

// The catalog is a separate owned Win32 window. It deliberately does not call
// the game's vehicle-purchase renderer: that function requires a live depot or
// border context and crashes when it is given an artificial catalog context.
// Using the game's own window_big.png keeps the visual language native while
// leaving the catalog data and filters under our control.
static int g_selectedType = 0;
static int g_selectedResource = 0;
static bool g_includeVanilla = true;
static bool g_includeWorkshop = true;
static bool g_includeTesmioLoader = true;
static bool g_filterResourceConsumes = false;
static bool g_filterResourceProduces = false;
static bool g_onlyAvailable = false;
static bool g_onlyFavorites = false;
static unsigned g_availabilityCacheEpoch = 1;
static bool g_catalogVisible = false;
static bool g_catalogNativeReady = false;
static void* g_catalogTexture = NULL;
static void* g_catalogSolidTexture = NULL;
static void* g_toolbarTexture = NULL;
struct CapturedPanelRect
{
    float left, top, right, bottom;
};
static bool g_captureBottomPanels = false;
static CapturedPanelRect g_bottomPanels[512] = {};
static int g_bottomPanelCount = 0;
static CapturedPanelRect g_nativeBottomPaper = {};
static bool g_nativeBottomPaperValid = false;
static void* g_favoriteTexture = NULL;
static void* g_centeredLockTextures[16] = {};
static void* g_bottomMenuController = NULL;
static float g_catalogX = -1.0f;
static float g_catalogY = -1.0f;
static bool g_catalogDragging = false;
static float g_catalogDragX = 0.0f;
static float g_catalogDragY = 0.0f;
static bool g_mouseWasDown = false;
// The toolbar click that opens the catalog must be fully released before any
// catalog control can react to a new press.  On the first opening the native
// tab and our overlay are created during the same bottom-menu frame; accepting
// input immediately can leave that first catalog session unable to arm a
// building until the tab is opened again.
static bool g_catalogInputArmed = false;
// Availability is queried once more after the native bottom menu has completed
// a full frame.  The first custom-tab frame can transiently report unresolved
// tools/research and v1.2 cached that false result for the whole session.
static int g_catalogAvailabilityWarmupFrames = 0;
static bool g_escapeWasDown = false;
static bool g_toolbarToggleLatch = false;
static bool g_toolbarMouseWasDown = false;
static bool g_suppressCustomSelection = false;
static int g_openDropdown = 0;
static int g_dropdownPage = 0;
static int g_resultPage = 0;
static int g_regularResultPage = 0;
static int g_favoritesResultPage = 0;
static int g_pendingCatalogItem = -1;
static HWND g_inputShieldWindow = NULL;
static WNDPROC g_originalGameWindowProc = NULL;
static bool g_shieldLeftButton = false;
static bool g_shieldRightButton = false;
static bool g_shieldMiddleButton = false;
static bool g_standaloneButtonCapture = false;
static volatile LONG g_standaloneToggleRequested = 0;
static float g_standaloneButtonX = -1.0f;
static float g_standaloneButtonY = -1.0f;
static float g_standaloneButtonSize = 0.0f;
typedef void (*t_InputRefreshData)(void*, HWND, void*);
static t_InputRefreshData o_InputRefreshData = NULL;

// Managed texture wrappers belong to the current game/menu generation.  The
// engine tears their GPU contents down while a new republic is being created,
// but the wrapper addresses can remain readable.  Keeping those addresses
// produced the stretched arrows/stars and missing paper background seen after
// starting a new game.  Drop every cached wrapper and lazily load fresh ones
// on the first catalogue frame in the new world.
static void ResetCatalogTextureState()
{
    g_catalogTexture = NULL;
    g_catalogSolidTexture = NULL;
    g_toolbarTexture = NULL;
    g_favoriteTexture = NULL;
    memset(g_centeredLockTextures, 0, sizeof(g_centeredLockTextures));
    for (int i = 0; i < g_catalogItemCount; ++i)
    {
        g_catalogItems[i].previewTexture = NULL;
        g_catalogItems[i].previewAttempted = false;
    }
}

static void RememberCurrentResultPage()
{
    if (g_onlyFavorites)
        g_favoritesResultPage = g_resultPage;
    else
        g_regularResultPage = g_resultPage;
}

static char* TrimAscii(char* text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    char* end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = 0;
    return text;
}

static bool IsUnsignedNumber(const char* text)
{
    if (!text || !text[0]) return false;
    for (const char* p = text; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    return true;
}

static const char* FriendlyResourceName(const char* name, const char* supplied)
{
    struct Name { const char* internalName; const char* englishName; };
    static const Name names[] = {
        { "eletric", "Electricity" },
        { "plants", "Crops" },
        { "rawgravel", "Quarried Stone" },
        { "prefabpanels", "Prefabricated Panels" },
        { "rawcoal", "Raw Coal" },
        { "rawiron", "Raw Iron" },
        { "rawbauxite", "Raw Bauxite" },
        { "yellowcake", "Yellowcake" },
        { "uf6", "Uranium Hexafluoride (UF6)" },
        { "nuclearfuel", "Nuclear Fuel" },
        { "nuclearfuelburned", "Spent Nuclear Fuel" },
        { "ecomponents", "Electronic Components" },
        { "mcomponents", "Mechanical Components" },
        { "eletronics", "Electronics" },
        { "usagewater", "Sewage" },
        { "fertiliser_liquid", "Liquid Fertilizer" },
        { "waste_gravel", "Construction Waste" },
        { "waste_steel", "Steel Scrap" },
        { "waste_aluminium", "Aluminium Scrap" },
        { "waste_plastic", "Plastic Waste" },
        { "waste_bio", "Biological Waste" },
        { "fertiliser", "Fertilizer" },
        { "waste_burnable", "Burnable Waste" },
        { "waste_toxic", "Hazardous Waste" },
        { "waste_other", "Mixed Waste" },
        { "waste_ash", "Ash" },
        { "copper_ore", "Copper Ore" },
        { "copper_concentrate", "Copper Concentrate" },
        { "raw_copper", "Raw Copper" },
        { "vehicles", "Vehicles" }
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (_stricmp(name, names[i].internalName) == 0) return names[i].englishName;
    return supplied && supplied[0] ? supplied : name;
}

static bool IsCatalogResourceVisible(const char* name)
{
    // Workers and train consists are internal engine carriers. Vehicles are
    // kept because car dealerships and vehicle factories expose them as an
    // actual accepted/produced catalogue resource.
    return _stricmp(name, "workers") != 0 &&
           _stricmp(name, "trains") != 0;
}

static void NormaliseEnglishCaption(const char* source, char* destination,
                                    size_t capacity)
{
    if (!destination || !capacity) return;
    destination[0] = 0;
    if (!source) return;
    bool startOfWord = true;
    size_t output = 0;
    for (size_t i = 0; source[i] && output + 1 < capacity; ++i)
    {
        unsigned char value = (unsigned char)source[i];
        if (value == '_') value = ' ';
        if (startOfWord && value >= 'a' && value <= 'z') value -= 'a' - 'A';
        destination[output++] = (char)value;
        startOfWord = value == ' ' || value == '-' || value == '(';
    }
    destination[output] = 0;
}

static void AddCatalogResource(const char* name, const char* templateName,
                               const char* displayUtf8, bool modded)
{
    if (!name || !name[0]) return;
    if (!IsCatalogResourceVisible(name)) return;
    for (int i = 0; i < g_catalogResourceCount; ++i)
    {
        if (_stricmp(g_catalogResources[i].name, name) == 0)
        {
            if (modded) g_catalogResources[i].modded = true;
            if (templateName && templateName[0])
                strncpy_s(g_catalogResources[i].templateName,
                          sizeof(g_catalogResources[i].templateName),
                          templateName, _TRUNCATE);
            return;
        }
    }
    if (g_catalogResourceCount >= MAX_CATALOG_RESOURCES) return;

    CatalogResource& entry = g_catalogResources[g_catalogResourceCount++];
    memset(&entry, 0, sizeof(entry));
    strncpy_s(entry.name, sizeof(entry.name), name, _TRUNCATE);
    strncpy_s(entry.templateName, sizeof(entry.templateName),
              templateName && templateName[0] ? templateName : name, _TRUNCATE);
    char captionBuffer[192] = {};
    NormaliseEnglishCaption(FriendlyResourceName(name, displayUtf8),
                            captionBuffer, sizeof(captionBuffer));
    const char* caption = captionBuffer[0] ? captionBuffer : name;
    if (!MultiByteToWideChar(CP_UTF8, 0, caption, -1, entry.display,
                             (int)(sizeof(entry.display) / sizeof(entry.display[0]))))
        MultiByteToWideChar(CP_ACP, 0, caption, -1, entry.display,
                            (int)(sizeof(entry.display) / sizeof(entry.display[0])));
    wcsncpy_s(entry.englishDisplay,
              sizeof(entry.englishDisplay) / sizeof(wchar_t),
              entry.display, _TRUNCATE);
    entry.modded = modded;
}

static const wchar_t* RussianResourceName(const char* name)
{
    struct Name { const char* internalName; const wchar_t* russianName; };
    static const Name names[] = {
        { "eletric", L"Электроэнергия" },
        { "heat", L"Тепло" },
        { "gravel", L"Гравий" },
        { "rawgravel", L"Добытый камень" },
        { "plants", L"Сельхозпродукция" },
        { "steel", L"Сталь" },
        { "aluminium", L"Алюминий" },
        { "prefabpanels", L"Сборные панели" },
        { "bricks", L"Кирпичи" },
        { "wood", L"Древесина" },
        { "oil", L"Нефть" },
        { "chemicals", L"Химикаты" },
        { "coal", L"Уголь" },
        { "rawcoal", L"Добытый уголь" },
        { "iron", L"Железо" },
        { "rawiron", L"Железная руда" },
        { "bauxite", L"Боксит" },
        { "rawbauxite", L"Бокситовая руда" },
        { "bitumen", L"Битум" },
        { "boards", L"Доски" },
        { "uranium", L"Уран" },
        { "yellowcake", L"Урановый концентрат" },
        { "uf6", L"Гексафторид урана (UF6)" },
        { "nuclearfuel", L"Ядерное топливо" },
        { "nuclearfuelburned", L"Отработанное ядерное топливо" },
        { "fuel", L"Топливо" },
        { "fabric", L"Ткань" },
        { "alcohol", L"Алкоголь" },
        { "cement", L"Цемент" },
        { "alumina", L"Глинозём" },
        { "food", L"Пища" },
        { "clothes", L"Одежда" },
        { "meat", L"Мясо" },
        { "livestock", L"Скот" },
        { "asphalt", L"Асфальт" },
        { "concrete", L"Бетон" },
        { "ecomponents", L"Электронные компоненты" },
        { "mcomponents", L"Механические компоненты" },
        { "plastics", L"Пластик" },
        { "eletronics", L"Электроника" },
        { "explosives", L"Взрывчатка" },
        { "water", L"Вода" },
        { "usagewater", L"Сточные воды" },
        { "fertiliser_liquid", L"Жидкие удобрения" },
        { "waste_gravel", L"Строительные отходы" },
        { "waste_steel", L"Стальной лом" },
        { "waste_aluminium", L"Алюминиевый лом" },
        { "waste_plastic", L"Пластиковые отходы" },
        { "waste_bio", L"Биологические отходы" },
        { "fertiliser", L"Удобрения" },
        { "waste_burnable", L"Горючие отходы" },
        { "waste_toxic", L"Опасные отходы" },
        { "waste_other", L"Смешанные отходы" },
        { "waste_ash", L"Зола" },
        { "copper_ore", L"Медная руда" },
        { "copper_concentrate", L"Медный концентрат" },
        { "raw_copper", L"Черновая медь" },
        { "copper", L"Медь" },
        { "furniture", L"Мебель" },
        { "vehicles", L"Автомобили" }
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (_stricmp(name, names[i].internalName) == 0) return names[i].russianName;
    return NULL;
}

static void SortCatalogResources()
{
    for (int i = 1; i < g_catalogResourceCount; ++i)
    {
        CatalogResource value = g_catalogResources[i];
        int j = i - 1;
        while (j >= 0)
        {
            bool previousTranslated =
                RussianResourceName(g_catalogResources[j].name) != NULL;
            bool valueTranslated = RussianResourceName(value.name) != NULL;
            bool movePrevious = false;
            if (g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN &&
                previousTranslated != valueTranslated)
                movePrevious = !previousTranslated && valueTranslated;
            else
                movePrevious =
                    _wcsicmp(g_catalogResources[j].display, value.display) > 0;
            if (!movePrevious) break;
            g_catalogResources[j + 1] = g_catalogResources[j];
            --j;
        }
        g_catalogResources[j + 1] = value;
    }
}

static void RefreshCatalogResourceLabels()
{
    for (int i = 0; i < g_catalogResourceCount; ++i)
    {
        wcsncpy_s(g_catalogResources[i].display,
                  sizeof(g_catalogResources[i].display) / sizeof(wchar_t),
                  g_catalogResources[i].englishDisplay, _TRUNCATE);
        if (g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN)
        {
            const wchar_t* translated = RussianResourceName(g_catalogResources[i].name);
            if (translated)
                wcsncpy_s(g_catalogResources[i].display,
                          sizeof(g_catalogResources[i].display) / sizeof(wchar_t),
                          translated, _TRUNCATE);
        }
    }
    SortCatalogResources();
}

static void LoadBaseCatalogResources()
{
    static const char* names[] = {
        "workers", "eletric", "vehicles", "trains", "heat", "gravel", "rawgravel",
        "plants", "steel", "aluminium", "prefabpanels", "bricks", "wood", "oil",
        "chemicals", "coal", "rawcoal", "iron", "rawiron", "bauxite", "rawbauxite",
        "bitumen", "boards", "uranium", "yellowcake", "uf6", "nuclearfuel",
        "nuclearfuelburned", "fuel", "fabric", "alcohol", "cement", "alumina",
        "food", "clothes", "meat", "livestock", "asphalt", "concrete",
        "ecomponents", "mcomponents", "plastics", "eletronics", "explosives",
        "water", "usagewater", "fertiliser_liquid", "waste_gravel", "waste_steel",
        "waste_aluminium", "waste_plastic", "waste_bio", "fertiliser",
        "waste_burnable", "waste_toxic", "waste_other", "waste_ash"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        AddCatalogResource(names[i], names[i], names[i], false);
}

static int CatalogResourceIndex(const char* name);

// TesmioLoader's resources plugin uses UTF-8 and permits an arbitrary number
// of entries. Parse [list] directly so every future resource becomes a catalog
// filter without a TesmioMenu rebuild.
static void LoadModCatalogResources()
{
    if (!H->pluginDir || !H->pluginDir[0]) return;
    char path[MAX_PATH] = {};
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\resources.ini", H->pluginDir);
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        H->log("tesmiomenu  catalog: no resources.ini; base resources only");
        return;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024)
    {
        CloseHandle(file);
        H->log("tesmiomenu  catalog: resources.ini has an unsafe size");
        return;
    }
    char* buffer = (char*)malloc((size_t)size.QuadPart + 1);
    if (!buffer) { CloseHandle(file); return; }
    DWORD read = 0;
    ReadFile(file, buffer, (DWORD)size.QuadPart, &read, NULL);
    CloseHandle(file);
    buffer[read] = 0;

    bool inList = false;
    char* context = NULL;
    for (char* raw = strtok_s(buffer, "\n", &context); raw;
         raw = strtok_s(NULL, "\n", &context))
    {
        char* line = TrimAscii(raw);
        if (!line[0] || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[')
        {
            inList = _strnicmp(line, "[list]", 6) == 0;
            continue;
        }
        if (!inList) continue;

        char* equals = strchr(line, '=');
        if (!equals) continue;
        *equals = 0;
        char* name = TrimAscii(line);
        char* value = TrimAscii(equals + 1);
        if (!name[0] || !value[0]) continue;

        char* fields[4] = {};
        int fieldCount = 0;
        char* valueContext = NULL;
        for (char* field = strtok_s(value, ",", &valueContext);
             field && fieldCount < 4; field = strtok_s(NULL, ",", &valueContext))
            fields[fieldCount++] = TrimAscii(field);

        int templateField = 0;
        if (fieldCount && (IsUnsignedNumber(fields[0]) || _stricmp(fields[0], "auto") == 0))
            templateField = 1;
        const char* resourceTemplate = fieldCount > templateField
            ? fields[templateField] : name;
        const char* display = fieldCount > templateField + 1
            ? fields[templateField + 1] : name;
        AddCatalogResource(name, resourceTemplate, display, true);
    }
    free(buffer);
}

static void LoadCatalogNeeds()
{
    g_catalogNeedCount = 0;
    if (!H->pluginDir || !H->pluginDir[0]) return;
    char path[MAX_PATH] = {};
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\needs.ini", H->pluginDir);
    HANDLE file = CreateFileA(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 1024 * 1024)
    {
        CloseHandle(file);
        return;
    }
    char* buffer = (char*)malloc((size_t)size.QuadPart + 1);
    if (!buffer) { CloseHandle(file); return; }
    DWORD read = 0;
    if (!ReadFile(file, buffer, (DWORD)size.QuadPart, &read, NULL))
    {
        CloseHandle(file);
        free(buffer);
        return;
    }
    CloseHandle(file);
    buffer[read] = 0;

    bool inList = false;
    char* context = NULL;
    for (char* raw = strtok_s(buffer, "\n", &context); raw;
         raw = strtok_s(NULL, "\n", &context))
    {
        char* line = TrimAscii(raw);
        if (!line[0] || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[')
        {
            inList = _strnicmp(line, "[list]", 6) == 0;
            continue;
        }
        if (!inList || g_catalogNeedCount >= MAX_CATALOG_NEEDS) continue;
        char* equals = strchr(line, '=');
        if (!equals) continue;
        *equals = 0;
        char* resource = TrimAscii(line);
        char* value = TrimAscii(equals + 1);
        if (!resource[0] || !value[0] || CatalogResourceIndex(resource) < 0)
            continue;

        char* fields[5] = {};
        int fieldCount = 0;
        char* valueContext = NULL;
        for (char* field = strtok_s(value, ",", &valueContext);
             field && fieldCount < 5;
             field = strtok_s(NULL, ",", &valueContext))
            fields[fieldCount++] = TrimAscii(field);
        if (!fieldCount || !fields[0][0]) continue;

        CatalogNeed& need = g_catalogNeeds[g_catalogNeedCount++];
        memset(&need, 0, sizeof(need));
        strncpy_s(need.resource, sizeof(need.resource), resource, _TRUNCATE);
        strncpy_s(need.donor, sizeof(need.donor), fields[0], _TRUNCATE);
        strncpy_s(need.category, sizeof(need.category),
                  fieldCount > 2 && fields[2][0] ? fields[2] : "auto",
                  _TRUNCATE);
    }
    free(buffer);
    H->log("tesmiomenu  catalog: %d additional citizen need(s) indexed",
           g_catalogNeedCount);
}

static void LoadCatalogResources()
{
    g_catalogResourceCount = 0;
    LoadBaseCatalogResources();
    LoadModCatalogResources();
    LoadCatalogNeeds();
    SortCatalogResources();
    int modded = 0;
    for (int i = 0; i < g_catalogResourceCount; ++i)
        if (g_catalogResources[i].modded) ++modded;
    H->log("tesmiomenu  catalog: %d resources indexed (%d added by mods)",
           g_catalogResourceCount, modded);
    if (g_probe)
    {
        for (int i = 0; i < g_catalogResourceCount; ++i)
            if (g_catalogResources[i].modded)
                H->log("tesmiomenu    catalog resource: %s", g_catalogResources[i].name);
    }
}

static bool BytesAre(uintptr_t rva, const unsigned char* expected, size_t count)
{
    const void* p = g_base + rva;
    return H->readablePtr(p, count) && memcmp(p, expected, count) == 0;
}

static bool ProbeBuild()
{
    static const unsigned char menuInit[] = {
        0x40,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xAC,0x24,0x30,0x99,0xFF,0xFF
    };
    static const unsigned char tabInit[] = {
        0x33,0xC0,0x48,0x89,0x41,0x50,0x48,0x89,0x41,0x58,
        0x48,0x89,0x41,0x60
    };
    static const unsigned char tabPush[] = {
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,
        0x48,0x8B,0xD9
    };
    static const unsigned char tabConstruct[] = {
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
        0x57,0x48,0x81,0xEC,0x50,0x01,0x00,0x00
    };
    static const unsigned char groupInit[] = {
        0x33,0xC0,0x48,0x89,0x41,0x08,0x48,0x89,0x41,0x10,
        0x48,0x89,0x41,0x18
    };
    static const unsigned char groupConstruct[] = {
        0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x54,0x24,0x10,
        0x55,0x57,0x41,0x56
    };
    static const unsigned char getType[] = {
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
        0x48,0x89,0x7C,0x24,0x18
    };
    static const unsigned char groupPush[] = {
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,
        0x48,0x8B,0xD9
    };
    static const unsigned char pointerPush[] = {
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,
        0x48,0x8B,0x41,0x08
    };
    static const unsigned char intPush[] = {
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,
        0x48,0x8B,0x41,0x08
    };
    static const unsigned char bottomMenuRender[] = {
        0x48,0x8B,0xC4,0x55,0x53,0x48,0x8D,0x68,
        0xE8,0x48,0x81,0xEC,0x08,0x01,0x00,0x00
    };
    static const unsigned char researchForBuilding[] = {
        0x4C,0x8B,0xDC,0x53,0x55,0x41,0x57,0x48,0x83,0xEC,0x50,
        0x48,0x8D,0x99,0x90,0x17,0x01,0x00
    };
    static const unsigned char researchForTool[] = {
        0x40,0x53,0x55,0x56,0x48,0x83,0xEC,0x30,
        0x48,0x8D,0x99,0x90,0x17,0x01,0x00
    };

    struct Probe { uintptr_t rva; const unsigned char* bytes; size_t size; const char* name; };
    const Probe probes[] = {
        { P_MENU_INIT, menuInit, sizeof(menuInit), "bottom menu initializer" },
        { P_TAB_INIT, tabInit, sizeof(tabInit), "tab initializer" },
        { P_TAB_PUSH, tabPush, sizeof(tabPush), "tab vector append" },
        { P_TAB_CONSTRUCT, tabConstruct, sizeof(tabConstruct), "tab constructor" },
        { P_GROUP_INIT, groupInit, sizeof(groupInit), "group initializer" },
        { P_GROUP_PUSH, groupPush, sizeof(groupPush), "group vector append" },
        { P_GROUP_CONSTRUCT, groupConstruct, sizeof(groupConstruct), "group constructor" },
        { P_POINTER_PUSH, pointerPush, sizeof(pointerPush), "building pointer append" },
        { P_INT_PUSH, intPush, sizeof(intPush), "tab integer append" },
        { P_TOOL_FIND, getType, sizeof(getType), "construction-tool lookup" },
        { P_BOTTOM_MENU_RENDER, bottomMenuRender, sizeof(bottomMenuRender),
          "bottom menu renderer" },
        { P_RESEARCH_FOR_BUILDING, researchForBuilding,
          sizeof(researchForBuilding), "building research lookup" },
        { P_RESEARCH_FOR_TOOL, researchForTool,
          sizeof(researchForTool), "tool research lookup" },
    };

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i)
    {
        if (!BytesAre(probes[i].rva, probes[i].bytes, probes[i].size))
        {
            H->log("tesmiomenu  unsupported game build: %s probe differs at rva 0x%llX",
                   probes[i].name, (unsigned long long)probes[i].rva);
            return false;
        }
    }
    return true;
}

// Keep the stock lower construction toolbar exactly where it is, but extend
// only its white paper backing by one already-scaled native button step on the
// left.  The right edge, category bands and buttons are deliberately left
// untouched.
static bool PatchBottomPaperLeft()
{
    static const unsigned char expected[] = {
        0x0F,0x28,0xC1,                         // movaps xmm1, xmm0
        0xF3,0x0F,0x10,0x55,0xE8,              // movss -0x18(%rbp), xmm2
        0xF3,0x0F,0x5C,0xC2,                   // subss xmm2, xmm0
        0xF3,0x0F,0x2C,0xD8,                   // cvttss2si xmm0, ebx
        0x89,0x5C,0x24,0x70                    // mov ebx, 0x70(%rsp)
    };
    if (!BytesAre(P_BOTTOM_PAPER_LEFT, expected, sizeof(expected)))
    {
        H->log("tesmiomenu  bottom paper geometry differs at rva 0x%llX",
               (unsigned long long)P_BOTTOM_PAPER_LEFT);
        return false;
    }

    static const unsigned char bodyPrefix[] = {
        0x0F,0x28,0xC1,                         // movaps xmm1, xmm0
        0xF3,0x0F,0x10,0x55,0xE8,              // movss -0x18(%rbp), xmm2
        0xF3,0x0F,0x5C,0xC2,                   // subss xmm2, xmm0
        0xF3,0x0F,0x5C,0x45,0x94,              // subss -0x6c(%rbp), xmm0
        0xF3,0x0F,0x2C,0xD8,                   // cvttss2si xmm0, ebx
        0x89,0x5C,0x24,0x70                    // mov ebx, 0x70(%rsp)
    };
    const size_t jumpSize = 14;
    const size_t captureSize = 12;               // movabs address + mov [rax], ebx
    const size_t codeSize = sizeof(bodyPrefix) + captureSize + jumpSize;
    unsigned char* code = (unsigned char*)VirtualAlloc(NULL, codeSize,
                                                        MEM_COMMIT | MEM_RESERVE,
                                                        PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        H->log("tesmiomenu  could not allocate bottom-paper geometry stub");
        return false;
    }
    memcpy(code, bodyPrefix, sizeof(bodyPrefix));
    unsigned char* capture = code + sizeof(bodyPrefix);
    capture[0] = 0x48; capture[1] = 0xB8;        // movabs address, rax
    void* capturedLeft = (void*)&g_nativeBottomPaperLeftPixels;
    memcpy(capture + 2, &capturedLeft, sizeof(capturedLeft));
    capture[10] = 0x89; capture[11] = 0x18;      // mov ebx, [rax]
    unsigned char* codeJump = capture + captureSize;
    codeJump[0] = 0xFF; codeJump[1] = 0x25;
    memset(codeJump + 2, 0, 4);
    void* resume = g_base + P_BOTTOM_PAPER_LEFT + sizeof(expected);
    memcpy(codeJump + 6, &resume, sizeof(resume));

    unsigned char replacement[sizeof(expected)] = {};
    replacement[0] = 0xFF; replacement[1] = 0x25;
    memcpy(replacement + 6, &code, sizeof(code));
    memset(replacement + jumpSize, 0x90, sizeof(replacement) - jumpSize);

    DWORD oldProtect = 0;
    void* target = g_base + P_BOTTOM_PAPER_LEFT;
    if (!VirtualProtect(target, sizeof(replacement), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        VirtualFree(code, 0, MEM_RELEASE);
        H->log("tesmiomenu  could not unlock bottom-paper geometry code");
        return false;
    }
    memcpy(target, replacement, sizeof(replacement));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(replacement));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(replacement), oldProtect, &ignored);

    g_bottomPaperCode = code;
    H->log("tesmiomenu  bottom paper extended one slot to the left");
    return true;
}

// The five coloured level-1 category bands are laid out independently from
// the tab records.  Adding a tab at index 0 therefore makes the stock
// Transport band cover it as well.  Keep the stock right edge and move only
// the first band's left edge by one native 50-pixel tab step.  The band's own
// caption-centering code consumes this adjusted rectangle, so the caption
// follows automatically.
static bool PatchFirstBandStart()
{
    if (!g_front) return true;

    static const unsigned char expected[] = {
        0xF3,0x0F,0x10,0x45,0xC0,             // movss -0x40(%rbp), xmm0
        0xF3,0x0F,0x5C,0xC1,                  // subss xmm1, xmm0
        0xF3,0x0F,0x2C,0xF0,                  // cvttss2si xmm0, esi
        0x89,0x74,0x24,0x70                   // mov esi, 0x70(%rsp)
    };
    if (!BytesAre(P_FIRST_BAND_START, expected, sizeof(expected)))
    {
        H->log("tesmiomenu  first category-band geometry differs at rva 0x%llX",
               (unsigned long long)P_FIRST_BAND_START);
        return false;
    }

    // Re-run the overwritten instructions, adding the already-computed native
    // tab step stored at -0x6C(%rbp), then return at the following instruction.
    static const unsigned char body[] = {
        0xF3,0x0F,0x10,0x45,0xC0,             // movss -0x40(%rbp), xmm0
        0xF3,0x0F,0x58,0x45,0x94,             // addss -0x6c(%rbp), xmm0
        0xF3,0x0F,0x5C,0xC1,                  // subss xmm1, xmm0
        0xF3,0x0F,0x2C,0xF0,                  // cvttss2si xmm0, esi
        0x89,0x74,0x24,0x70                   // mov esi, 0x70(%rsp)
    };
    const size_t jumpSize = 14;
    const size_t codeSize = sizeof(body) + jumpSize;
    unsigned char* code = (unsigned char*)VirtualAlloc(NULL, codeSize,
                                                        MEM_COMMIT | MEM_RESERVE,
                                                        PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        H->log("tesmiomenu  could not allocate first-band geometry stub");
        return false;
    }
    memcpy(code, body, sizeof(body));
    unsigned char* codeJump = code + sizeof(body);
    codeJump[0] = 0xFF; codeJump[1] = 0x25;
    memset(codeJump + 2, 0, 4);
    void* resume = g_base + P_FIRST_BAND_START + sizeof(expected);
    memcpy(codeJump + 6, &resume, sizeof(resume));

    unsigned char replacement[sizeof(expected)] = {};
    replacement[0] = 0xFF; replacement[1] = 0x25;
    memcpy(replacement + 6, &code, sizeof(code));
    memset(replacement + jumpSize, 0x90, sizeof(replacement) - jumpSize);

    DWORD oldProtect = 0;
    void* target = g_base + P_FIRST_BAND_START;
    if (!VirtualProtect(target, sizeof(replacement), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        VirtualFree(code, 0, MEM_RELEASE);
        H->log("tesmiomenu  could not unlock first-band geometry code");
        return false;
    }
    memcpy(target, replacement, sizeof(replacement));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(replacement));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(replacement), oldProtect, &ignored);

    g_firstBandCode = code;
    H->log("tesmiomenu  transport band fixed to Roads through Cableways");
    return true;
}

// The remaining stock category bands use fixed vanilla starts.  Once the
// custom standalone tab occupies index 0, Cargo and every later band must move
// one tab to the right.  The first Cargo calculation also reuses the first
// band's width register, so rebuild that start explicitly as seven native
// steps: custom + the six stock Transport tabs, including Cableways.
static bool PatchFollowingBandStarts()
{
    if (!g_front) return true;

    static const uintptr_t secondBandStart = 0x081923;
    static const unsigned char expectedSecond[] = {
        0x41,0x0F,0x28,0xCD,                  // movaps xmm13, xmm1
        0xF3,0x0F,0x58,0x4D,0xC0,             // addss -0x40(%rbp), xmm1
        0xF3,0x0F,0x10,0x15,0x54,0x07,0x91,0x00 // load UI scale
    };
    struct ConstantPatch
    {
        uintptr_t rva;
        unsigned char expected[8];
        unsigned char replacement[8];
    };
    static const ConstantPatch later[] = {
        { 0x081ACB,
          {0xF3,0x0F,0x59,0x0D,0x55,0x8D,0x88,0x00}, // 10 steps
          {0xF3,0x0F,0x59,0x0D,0x85,0x8D,0x88,0x00}  // 11 steps
        },
        { 0x081C5D,
          {0xF3,0x0F,0x59,0x0D,0x3B,0x8C,0x88,0x00}, // 14 steps
          {0xF3,0x0F,0x59,0x0D,0x67,0x8C,0x88,0x00}  // 15 steps
        },
        { 0x081DEF,
          {0xF3,0x0F,0x59,0x0D,0x09,0x8B,0x88,0x00}, // 18 steps
          {0xF3,0x0F,0x59,0x0D,0x21,0x8B,0x88,0x00}  // 19 steps
        }
    };

    if (!BytesAre(secondBandStart, expectedSecond, sizeof(expectedSecond)))
    {
        H->log("tesmiomenu  cargo-band geometry differs at rva 0x%llX",
               (unsigned long long)secondBandStart);
        return false;
    }
    for (size_t i = 0; i < sizeof(later) / sizeof(later[0]); ++i)
    {
        if (!BytesAre(later[i].rva, later[i].expected, sizeof(later[i].expected)))
        {
            H->log("tesmiomenu  later category-band geometry differs at rva 0x%llX",
                   (unsigned long long)later[i].rva);
            return false;
        }
    }

    // Stub layout: calculate base + 7 * nativeStep, reproduce the overwritten
    // UI-scale load, jump back, then keep a private 7.0f constant after code.
    const size_t codeSize = 50;
    unsigned char* code = (unsigned char*)VirtualAlloc(NULL, codeSize,
                                                        MEM_COMMIT | MEM_RESERVE,
                                                        PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        H->log("tesmiomenu  could not allocate following-band geometry stub");
        return false;
    }
    static const unsigned char body[] = {
        0xF3,0x0F,0x10,0x4D,0x94,             // movss -0x6c(%rbp), xmm1
        0xF3,0x0F,0x59,0x0D,0x21,0x00,0x00,0x00, // mulss private 7.0f, xmm1
        0xF3,0x0F,0x58,0x4D,0xC0,             // addss -0x40(%rbp), xmm1
        0x48,0xB8                              // movabs UI-scale address, rax
    };
    memcpy(code, body, sizeof(body));
    void* uiScale = g_base + 0x992088;
    memcpy(code + 20, &uiScale, sizeof(uiScale));
    static const unsigned char loadScale[] = { 0xF3,0x0F,0x10,0x10 };
    memcpy(code + 28, loadScale, sizeof(loadScale));
    code[32] = 0xFF; code[33] = 0x25;
    memset(code + 34, 0, 4);
    void* resume = g_base + secondBandStart + sizeof(expectedSecond);
    memcpy(code + 38, &resume, sizeof(resume));
    const float seven = 7.0f;
    memcpy(code + 46, &seven, sizeof(seven));

    unsigned char replacement[sizeof(expectedSecond)] = {};
    replacement[0] = 0xFF; replacement[1] = 0x25;
    memcpy(replacement + 6, &code, sizeof(code));
    memset(replacement + 14, 0x90, sizeof(replacement) - 14);

    void* target = g_base + secondBandStart;
    const size_t patchSpan = (0x081DEF + 8) - secondBandStart;
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, patchSpan, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        VirtualFree(code, 0, MEM_RELEASE);
        H->log("tesmiomenu  could not unlock following-band geometry code");
        return false;
    }
    memcpy(target, replacement, sizeof(replacement));
    for (size_t i = 0; i < sizeof(later) / sizeof(later[0]); ++i)
        memcpy(g_base + later[i].rva, later[i].replacement,
               sizeof(later[i].replacement));
    FlushInstructionCache(GetCurrentProcess(), target, patchSpan);
    DWORD ignored = 0;
    VirtualProtect(target, patchSpan, oldProtect, &ignored);
    g_followingBandsCode = code;
    H->log("tesmiomenu  cargo and later category bands shifted one tab right");
    return true;
}

static wchar_t* h_GetString(void* self, int id)
{
    if (self)
    {
        g_languageObject = self;
        DetectCatalogLanguage();
    }
    if (id == g_textId) return g_title;
    return o_GetString ? o_GetString(self, id) : (wchar_t*)L"";
}

static uint16_t ReadBigEndian16(const unsigned char* value)
{
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static uint32_t ReadBigEndian32(const unsigned char* value)
{
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
           ((uint32_t)value[2] << 8) | value[3];
}

static bool LoadTextTable(const char* fileName, const char* logName,
                          unsigned char** table, size_t* tableSize,
                          uint32_t* tableCount, size_t* tableBase)
{
    if (*table) return true;
    char path[2 * MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (!length || length >= sizeof(path)) return false;
    char* slash = strrchr(path, '\\');
    if (!slash) return false;
    char relative[128] = {};
    _snprintf_s(relative, sizeof(relative), _TRUNCATE,
                "media_soviet\\%s", fileName);
    strcpy_s(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), relative);

    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        H->log("tesmiomenu  catalog: %s text table unavailable", logName);
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 12 ||
        size.QuadPart > 8 * 1024 * 1024)
    {
        CloseHandle(file);
        return false;
    }
    unsigned char* buffer = (unsigned char*)malloc((size_t)size.QuadPart);
    if (!buffer) { CloseHandle(file); return false; }
    DWORD read = 0;
    bool ok = ReadFile(file, buffer, (DWORD)size.QuadPart, &read, NULL) != FALSE;
    CloseHandle(file);
    if (!ok || read != (DWORD)size.QuadPart)
    {
        free(buffer);
        return false;
    }
    uint32_t count = ReadBigEndian32(buffer);
    size_t textBase = 12u + (size_t)count * 10u;
    if (!count || count > 100000 || textBase > (size_t)size.QuadPart)
    {
        free(buffer);
        return false;
    }
    *table = buffer;
    *tableSize = (size_t)size.QuadPart;
    *tableCount = count;
    *tableBase = textBase;
    H->log("tesmiomenu  catalog: %u %s labels loaded", count, logName);
    return true;
}

static bool LoadEnglishTextTable()
{
    return LoadTextTable("sovietEnglish.btf", "English", &g_englishText,
                         &g_englishTextSize, &g_englishTextCount,
                         &g_englishTextBase);
}

static bool LoadRussianTextTable()
{
    return LoadTextTable("sovietRussian.btf", "Russian", &g_russianText,
                         &g_russianTextSize, &g_russianTextCount,
                         &g_russianTextBase);
}

static bool TextTableById(const unsigned char* table, size_t tableSize,
                          uint32_t tableCount, size_t tableBase, int id,
                          wchar_t* destination, size_t capacity)
{
    if (!destination || !capacity) return false;
    destination[0] = 0;
    if (!table || id < 0) return false;
    // The BTF entry table is grouped for the game's loader, not globally
    // sorted by text ID. A binary search silently misses valid UI strings.
    const unsigned char* entry = NULL;
    for (uint32_t i = 0; i < tableCount; ++i)
    {
        const unsigned char* candidate = table + 12u + (size_t)i * 10u;
        if (ReadBigEndian32(candidate) == (uint32_t)id)
        {
            entry = candidate;
            break;
        }
    }
    if (!entry) return false;
    uint32_t offset = ReadBigEndian32(entry + 4);
    uint16_t length = ReadBigEndian16(entry + 8);
    size_t byteOffset = tableBase + (size_t)offset * 2u;
    size_t byteLength = (size_t)length * 2u;
    if (byteOffset > tableSize || byteLength > tableSize - byteOffset)
        return false;
    size_t output = length < capacity - 1 ? length : capacity - 1;
    for (size_t i = 0; i < output; ++i)
        destination[i] = (wchar_t)ReadBigEndian16(table + byteOffset + i * 2u);
    destination[output] = 0;
    return output > 0;
}

static bool EnglishTextById(int id, wchar_t* destination, size_t capacity)
{
    return TextTableById(g_englishText, g_englishTextSize, g_englishTextCount,
                         g_englishTextBase, id, destination, capacity);
}

static bool RussianTextById(int id, wchar_t* destination, size_t capacity)
{
    return TextTableById(g_russianText, g_russianTextSize, g_russianTextCount,
                         g_russianTextBase, id, destination, capacity);
}

static int EnglishTextIdByValue(const wchar_t* value)
{
    if (!value || !value[0] || !g_englishText) return -1;
    size_t wantedLength = wcslen(value);
    for (uint32_t i = 0; i < g_englishTextCount; ++i)
    {
        const unsigned char* entry = g_englishText + 12u + (size_t)i * 10u;
        uint16_t length = ReadBigEndian16(entry + 8);
        if ((size_t)length != wantedLength) continue;
        uint32_t offset = ReadBigEndian32(entry + 4);
        size_t byteOffset = g_englishTextBase + (size_t)offset * 2u;
        size_t byteLength = (size_t)length * 2u;
        if (byteOffset > g_englishTextSize ||
            byteLength > g_englishTextSize - byteOffset)
            continue;
        bool equal = true;
        for (size_t character = 0; character < wantedLength; ++character)
        {
            wchar_t tableCharacter =
                (wchar_t)ReadBigEndian16(g_englishText + byteOffset + character * 2u);
            if (towlower(tableCharacter) != towlower(value[character]))
            {
                equal = false;
                break;
            }
        }
        if (equal) return (int)ReadBigEndian32(entry);
    }
    return -1;
}

static bool ActiveTextById(int id, wchar_t* destination, size_t capacity)
{
    if (!destination || !capacity) return false;
    destination[0] = 0;
    if (g_languageObject && o_GetString)
    {
        wchar_t* active = o_GetString(g_languageObject, id);
        if (active && H->readablePtr(active, sizeof(wchar_t)) && active[0])
        {
            wcsncpy_s(destination, capacity, active, _TRUNCATE);
            if (destination[0]) return true;
        }
    }
    if (g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN &&
        RussianTextById(id, destination, capacity))
        return true;
    return EnglishTextById(id, destination, capacity);
}

static bool CatalogTextById(int id, wchar_t* destination, size_t capacity)
{
    if (g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN)
    {
        if (RussianTextById(id, destination, capacity)) return true;
    }
    else if (EnglishTextById(id, destination, capacity))
        return true;
    return ActiveTextById(id, destination, capacity);
}

static void DetectCatalogLanguage()
{
    if (!g_languageObject || !o_GetString || !g_russianText) return;
    wchar_t russianProbe[128] = {};
    if (!RussianTextById(67005, russianProbe,
                         sizeof(russianProbe) / sizeof(wchar_t)))
        return;
    wchar_t* activeProbe = o_GetString(g_languageObject, 67005);
    if (!activeProbe || !H->readablePtr(activeProbe, sizeof(wchar_t))) return;

    CatalogUiLanguage detected = _wcsicmp(activeProbe, russianProbe) == 0
        ? CATALOG_LANGUAGE_RUSSIAN
        : CATALOG_LANGUAGE_ENGLISH;
    if (!g_catalogLanguageDetected || detected != g_catalogLanguage)
    {
        g_catalogLanguage = detected;
        g_catalogLanguageDetected = true;
        wcsncpy_s(g_title, sizeof(g_title) / sizeof(wchar_t),
                  Ui(UI_CATALOG_TITLE), _TRUNCATE);
        H->log("tesmiomenu  catalog language: %s",
               detected == CATALOG_LANGUAGE_RUSSIAN ? "Russian" : "English");
    }
}

static void CopyWideFallback(wchar_t* destination, size_t capacity,
                             const char* fallback)
{
    if (!destination || !capacity) return;
    destination[0] = 0;
    if (!fallback) return;
    char caption[160] = {};
    NormaliseEnglishCaption(fallback, caption, sizeof(caption));
    MultiByteToWideChar(CP_UTF8, 0, caption, -1, destination, (int)capacity);
    destination[capacity - 1] = 0;
}

static void RefreshCatalogTypeLabels()
{
    for (int i = 0; i < g_catalogTypeCount; ++i)
    {
        if (strcmp(g_catalogTypes[i].name, CATALOG_UNDEFINED_TYPE) == 0)
        {
            wcscpy_s(g_catalogTypes[i].display,
                     sizeof(g_catalogTypes[i].display) / sizeof(wchar_t),
                     g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN
                         ? L"\u041d\u0435 \u043e\u043f\u0440\u0435\u0434\u0435\u043b\u0435\u043d\u043e"
                         : L"Undefined");
            continue;
        }
        if (!CatalogTextById(g_catalogTypes[i].textId,
                             g_catalogTypes[i].display,
                             sizeof(g_catalogTypes[i].display) / sizeof(wchar_t)))
            CopyWideFallback(g_catalogTypes[i].display,
                             sizeof(g_catalogTypes[i].display) / sizeof(wchar_t),
                             g_catalogTypes[i].name);
    }
}

static bool AsciiContainsNoCase(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0]) return false;
    size_t length = strlen(needle);
    for (const char* start = text; *start; ++start)
        if (_strnicmp(start, needle, length) == 0) return true;
    return false;
}

static bool IsCatalogTypeVisible(const CatalogType& type)
{
    if (AsciiContainsNoCase(type.name, "terrain") ||
        AsciiContainsNoCase(type.name, "landscape") ||
        AsciiContainsNoCase(type.name, "editor_") ||
        AsciiContainsNoCase(type.name, "oldbuilding") ||
        AsciiContainsNoCase(type.name, "old_building") ||
        AsciiContainsNoCase(type.name, "legacy"))
        return false;
    return _wcsicmp(type.display, L"Terrain tools") != 0 &&
           _wcsicmp(type.display, L"Old buildings") != 0 &&
           _wcsicmp(type.display, L"Инструменты рельефа") != 0 &&
           _wcsicmp(type.display, L"Старые здания") != 0;
}

static void CaptureCatalogTypes()
{
    g_catalogTypeCount = 0;
    RawVector* tabs = (RawVector*)(g_base + G_BOTTOM_TABS);
    if (!H->readablePtr(tabs, sizeof(*tabs)) || !tabs->begin || !tabs->end ||
        tabs->end < tabs->begin || ((tabs->end - tabs->begin) % TAB_SIZE) != 0)
        return;

    int count = (int)((tabs->end - tabs->begin) / TAB_SIZE);
    for (int i = 0; i < count && g_catalogTypeCount < MAX_CATALOG_TYPES; ++i)
    {
        unsigned char* tab = tabs->begin + i * TAB_SIZE;
        const char* name = (const char*)tab;
        if (!memchr(name, 0, 64) || !strcmp(name, "tesmioloader")) continue;
        RawVector* titles = (RawVector*)(tab + 0x68);
        RawVector* boundaries = (RawVector*)(tab + 0x80);
        bool validSections = H->readablePtr(titles, sizeof(*titles)) &&
            H->readablePtr(boundaries, sizeof(*boundaries)) &&
            titles->begin && titles->end && titles->end >= titles->begin &&
            boundaries->begin && boundaries->end &&
            boundaries->end >= boundaries->begin &&
            ((titles->end - titles->begin) % sizeof(int)) == 0 &&
            ((boundaries->end - boundaries->begin) % sizeof(int)) == 0;
        size_t sectionCount = validSections
            ? (size_t)(titles->end - titles->begin) / sizeof(int) : 0;
        size_t boundaryCount = validSections
            ? (size_t)(boundaries->end - boundaries->begin) / sizeof(int) : 0;
        if (!sectionCount || sectionCount != boundaryCount || sectionCount > 512)
        {
            validSections = false;
            sectionCount = 1;
        }
        else if (!H->readablePtr(titles->begin, sectionCount * sizeof(int)) ||
                 !H->readablePtr(boundaries->begin,
                                 boundaryCount * sizeof(int)))
        {
            // The vector object can survive a menu rebuild for a frame while
            // its backing storage has already been released. Never dereference
            // the section arrays in that transition state.
            validSections = false;
            sectionCount = 1;
        }

        for (size_t sectionIndex = 0;
             sectionIndex < sectionCount && g_catalogTypeCount < MAX_CATALOG_TYPES;
             ++sectionIndex)
        {
            int textId = sectionCount == 1 && !validSections
                ? *(int*)(tab + 0x48)
                : *(int*)(titles->begin + sectionIndex * sizeof(int));
            if (textId <= 0) continue;

            bool duplicate = false;
            for (int existing = 0; existing < g_catalogTypeCount; ++existing)
                if (g_catalogTypes[existing].textId == textId)
                    duplicate = true;
            if (duplicate) continue;

            CatalogType& type = g_catalogTypes[g_catalogTypeCount++];
            type.tab = tab;
            strncpy_s(type.name, sizeof(type.name), name, _TRUNCATE);
            type.textId = textId;
            type.display[0] = 0;
        }
    }
    RefreshCatalogTypeLabels();
    int visibleCount = 0;
    for (int i = 0; i < g_catalogTypeCount; ++i)
    {
        if (!IsCatalogTypeVisible(g_catalogTypes[i])) continue;
        if (visibleCount != i) g_catalogTypes[visibleCount] = g_catalogTypes[i];
        ++visibleCount;
    }
    g_catalogTypeCount = visibleCount;
    for (int i = 1; i < g_catalogTypeCount; ++i)
    {
        CatalogType value = g_catalogTypes[i];
        int j = i - 1;
        while (j >= 0 && _wcsicmp(g_catalogTypes[j].display, value.display) > 0)
        {
            g_catalogTypes[j + 1] = g_catalogTypes[j];
            --j;
        }
        g_catalogTypes[j + 1] = value;
    }

    // Keep an explicit final bucket for physical Workshop/loader buildings
    // which the game did not assign to any native construction section.  A
    // missing classification must never silently become the first native
    // category (which used to label such buildings as Atomic industry).
    if (g_catalogTypeCount < MAX_CATALOG_TYPES)
    {
        CatalogType& undefined = g_catalogTypes[g_catalogTypeCount++];
        memset(&undefined, 0, sizeof(undefined));
        strncpy_s(undefined.name, sizeof(undefined.name),
                  CATALOG_UNDEFINED_TYPE, _TRUNCATE);
        undefined.textId = 0;
        wcscpy_s(undefined.display,
                 sizeof(undefined.display) / sizeof(wchar_t),
                 g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN
                     ? L"\u041d\u0435 \u043e\u043f\u0440\u0435\u0434\u0435\u043b\u0435\u043d\u043e"
                     : L"Undefined");
    }
    g_selectedType = 0;
    H->log("tesmiomenu  catalog: %d native build subcategories indexed",
           g_catalogTypeCount);
    if (g_probe)
        for (int i = 0; i < g_catalogTypeCount; ++i)
            H->log("tesmiomenu    catalog type: %s (text %d)",
                   g_catalogTypes[i].name, g_catalogTypes[i].textId);
}

static void ReadSettings()
{
    g_enabled = H->configInt(INI, "menu", "enabled", 1);
    g_group = H->configInt(INI, "menu", "group", DEFAULT_GROUP);
    g_textId = H->configInt(INI, "menu", "text_id", DEFAULT_TEXT_ID);
    g_probe = H->configInt(INI, "menu", "probe", 1);
    g_front = H->configInt(INI, "menu", "front", 1);

    char title[160] = {};
    H->configString(INI, "menu", "title", title, sizeof(title), "Tesmio Catalog");
    if (title[0])
    {
        int written = MultiByteToWideChar(CP_UTF8, 0, title, -1, g_title,
                                          (int)(sizeof(g_title) / sizeof(g_title[0])));
        if (!written) wcscpy_s(g_title, sizeof(g_title) / sizeof(g_title[0]),
                               L"Tesmio Catalog");
    }
}

static bool PluginIniPath(char* destination, size_t capacity)
{
    if (!destination || capacity < 32) return false;
    destination[0] = 0;
    MEMORY_BASIC_INFORMATION memory = {};
    if (!VirtualQuery((const void*)&g_enabled, &memory, sizeof(memory)) ||
        !memory.AllocationBase)
        return false;
    DWORD length = GetModuleFileNameA((HMODULE)memory.AllocationBase,
                                      destination, (DWORD)capacity);
    if (!length || length >= capacity) { destination[0] = 0; return false; }
    char* slash = strrchr(destination, '\\');
    if (!slash) { destination[0] = 0; return false; }
    strcpy_s(slash + 1, capacity - (size_t)(slash + 1 - destination),
             "tesmiomenu.ini");
    return true;
}

static void LoadCatalogFavorites()
{
    char path[2 * MAX_PATH] = {};
    if (!PluginIniPath(path, sizeof(path))) return;
    int favoriteCount = 0;
    for (int i = 0; i < g_catalogItemCount; ++i)
    {
        g_catalogItems[i].favorite =
            GetPrivateProfileIntA("favorites", g_catalogItems[i].toolName,
                                  0, path) != 0;
        if (g_catalogItems[i].favorite) ++favoriteCount;
    }
    H->log("tesmiomenu  catalog: %d favorites restored", favoriteCount);
}

static void SaveCatalogFavorite(const CatalogItem& item)
{
    char path[2 * MAX_PATH] = {};
    if (!PluginIniPath(path, sizeof(path))) return;
    WritePrivateProfileStringA("favorites", item.toolName,
                               item.favorite ? "1" : NULL, path);
    H->log("tesmiomenu  catalog favorite %s: %s", item.favorite ? "added" : "removed",
           item.toolName);
}

static unsigned char AsciiLower(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static bool ContainsAsciiInsensitive(const unsigned char* haystack, size_t haystackSize,
                                     const char* needle)
{
    size_t needleSize = strlen(needle);
    if (!needleSize || needleSize > haystackSize) return false;
    for (size_t i = 0; i + needleSize <= haystackSize; ++i)
    {
        size_t j = 0;
        while (j < needleSize &&
               AsciiLower(haystack[i + j]) == AsciiLower((unsigned char)needle[j])) ++j;
        if (j == needleSize) return true;
    }
    return false;
}

// Workshop's $OBJECT_BUILDING name is not always the name of the construction
// tool registered by the engine. Try the public name lookup first, then map the
// object name back through each build tool's BuildingType descriptor.
static void* ResolveBuildingTool(const char* objectName, char* resolvedName, size_t resolvedSize)
{
    t_FindTool find = (t_FindTool)(g_base + P_TOOL_FIND);
    void* game = g_base + G_GAME;
    void* direct = find(game, objectName);
    if (direct)
    {
        strncpy_s(resolvedName, resolvedSize, objectName, _TRUNCATE);
        return direct;
    }

    RawVector* tools = (RawVector*)((unsigned char*)game + TOOL_VECTOR);
    if (!H->readablePtr(tools, sizeof(*tools)) || !tools->begin || !tools->end ||
        tools->end < tools->begin || ((tools->end - tools->begin) % TOOL_SIZE) != 0)
        return NULL;

    size_t toolCount = (size_t)(tools->end - tools->begin) / TOOL_SIZE;
    if (toolCount < 20 || toolCount > 4096) return NULL;

    for (size_t i = 0; i < toolCount; ++i)
    {
        unsigned char* tool = tools->begin + i * TOOL_SIZE;
        const char* toolName = (const char*)tool;
        if (!H->readablePtr(tool, TOOL_BUILDING + sizeof(void*)) ||
            !memchr(toolName, 0, 64)) continue;

        void* buildingType = *(void**)(tool + TOOL_BUILDING);
        if (!buildingType) continue;

        bool match = ContainsAsciiInsensitive((const unsigned char*)toolName,
                                              strnlen(toolName, 64), objectName);
        if (!match && H->readablePtr(buildingType, BUILDING_TYPE_SCAN))
            match = ContainsAsciiInsensitive((const unsigned char*)buildingType,
                                             BUILDING_TYPE_SCAN, objectName);
        if (!match) continue;

        strncpy_s(resolvedName, resolvedSize, toolName, _TRUNCATE);
        return tool;
    }
    return NULL;
}

static int LoadBuildingTools(void** out, char names[][96])
{
    int count = 0;

    for (int i = 1; i <= MAX_BUILDINGS; ++i)
    {
        char key[32];
        char name[96] = {};
        _snprintf_s(key, sizeof(key), _TRUNCATE, "building%d", i);
        H->configString(INI, "buildings", key, name, sizeof(name), "");
        if (!name[0]) continue;

        char resolved[96] = {};
        void* tool = ResolveBuildingTool(name, resolved, sizeof(resolved));
        if (!tool)
        {
            H->log("tesmiomenu  building not found: %s", name);
            continue;
        }
        out[count] = tool;
        _snprintf_s(names[count], 96, _TRUNCATE, "%s => %s", name, resolved);
        ++count;
    }
    g_catalogBuildingCount = count;
    for (int i = 0; i < count; ++i) g_catalogBuildingTools[i] = out[i];
    return count;
}

static bool FileExistsA(const char* path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

struct WorkshopTesmioSourceCache
{
    char itemId[32];
    bool tesmio;
};

static WorkshopTesmioSourceCache g_workshopTesmioSourceCache[1024] = {};
static int g_workshopTesmioSourceCacheCount = 0;
static void GameDirectory(char* destination, size_t capacity);

// A Workshop item can be only the delivery vehicle for a TesmioLoader package.
// Such items contain files that the user must manually merge into
// tesmioloader\build\plugins.  Classify their buildings as Tesmio without a
// per-building allow-list; ordinary subscribed buildings have no such payload
// and remain Workshop items.
static bool WorkshopPackageHasTesmioPayload(const char* toolName)
{
    if (!toolName) return false;
    const char* slash = strchr(toolName, '/');
    if (!slash) slash = strchr(toolName, '\\');
    if (!slash) return false;
    size_t idLength = (size_t)(slash - toolName);
    if (!idLength || idLength >= 32) return false;
    char itemId[32] = {};
    memcpy(itemId, toolName, idLength);
    itemId[idLength] = 0;
    if (!IsUnsignedNumber(itemId)) return false;

    for (int i = 0; i < g_workshopTesmioSourceCacheCount; ++i)
        if (strcmp(g_workshopTesmioSourceCache[i].itemId, itemId) == 0)
            return g_workshopTesmioSourceCache[i].tesmio;

    char game[2 * MAX_PATH] = {};
    GameDirectory(game, sizeof(game));
    bool tesmio = false;
    char steamapps[2 * MAX_PATH] = {};
    strncpy_s(steamapps, sizeof(steamapps), game, _TRUNCATE);
    char* common = strstr(steamapps, "\\common\\");
    if (common)
    {
        *common = 0;
        char searchPath[4 * MAX_PATH] = {};
        _snprintf_s(searchPath, sizeof(searchPath), _TRUNCATE,
                    "%s\\workshop\\content\\784150\\%s\\plugins\\*",
                    steamapps, itemId);
        WIN32_FIND_DATAA entry = {};
        HANDLE search = FindFirstFileA(searchPath, &entry);
        if (search != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    strcmp(entry.cFileName, ".") != 0 &&
                    strcmp(entry.cFileName, "..") != 0)
                {
                    tesmio = true;
                    break;
                }
            } while (FindNextFileA(search, &entry));
            FindClose(search);
        }
    }

    if (g_workshopTesmioSourceCacheCount <
        (int)(sizeof(g_workshopTesmioSourceCache) /
              sizeof(g_workshopTesmioSourceCache[0])))
    {
        WorkshopTesmioSourceCache& cached =
            g_workshopTesmioSourceCache[g_workshopTesmioSourceCacheCount++];
        strncpy_s(cached.itemId, sizeof(cached.itemId), itemId, _TRUNCATE);
        cached.tesmio = tesmio;
    }
    return tesmio;
}

static void GameDirectory(char* destination, size_t capacity)
{
    if (!destination || !capacity) return;
    destination[0] = 0;
    DWORD length = GetModuleFileNameA(NULL, destination, (DWORD)capacity);
    if (!length || length >= capacity) { destination[0] = 0; return; }
    char* slash = strrchr(destination, '\\');
    if (slash) *slash = 0;
}

static bool ReadToolPreviewPath(void* tool, char* destination, size_t capacity)
{
    if (!tool || !destination || capacity < 2 ||
        !H->readablePtr((unsigned char*)tool + TOOL_PREVIEW_PATH, 260))
        return false;
    const char* source = (const char*)tool + TOOL_PREVIEW_PATH;
    const char* terminator = (const char*)memchr(source, 0, 260);
    if (!terminator || terminator == source) return false;
    for (const char* character = source; character < terminator; ++character)
    {
        unsigned char value = (unsigned char)*character;
        if (value < 0x20 || value > 0x7E) return false;
    }
    strncpy_s(destination, capacity, source, _TRUNCATE);
    return destination[0] != 0;
}

static bool DescriptorFromPreviewPath(const char* previewPath,
                                      char* destination, size_t capacity)
{
    if (!previewPath || !previewPath[0] || !destination || !capacity) return false;
    char game[2 * MAX_PATH] = {};
    GameDirectory(game, sizeof(game));
    if (!game[0]) return false;

    char relative[512] = {};
    strncpy_s(relative, sizeof(relative), previewPath, _TRUNCATE);
    for (char* character = relative; *character; ++character)
        if (*character == '/') *character = '\\';
    char* start = relative;
    while (*start == '\\') ++start;
    if (_strnicmp(start, "media_soviet\\", 13) == 0) start += 13;
    char* slash = strrchr(start, '\\');
    if (!slash) return false;
    *slash = 0;
    _snprintf_s(destination, capacity, _TRUNCATE,
                "%s\\media_soviet\\%s\\building.ini", game, start);
    return FileExistsA(destination);
}

static bool VirtualPreviewPath(const char* absolutePath, char* destination,
                               size_t capacity)
{
    if (!absolutePath || !destination || !capacity || !FileExistsA(absolutePath))
        return false;
    const char* marker = strstr(absolutePath, "\\media_soviet\\");
    if (!marker) return false;
    marker += strlen("\\media_soviet\\");
    strncpy_s(destination, capacity, marker, _TRUNCATE);
    for (char* character = destination; *character; ++character)
        if (*character == '\\') *character = '/';
    return destination[0] != 0;
}

static bool InferPreviewPath(const char* toolName, const char* descriptor,
                             char* destination, size_t capacity)
{
    char candidate[4 * MAX_PATH] = {};
    if (descriptor && descriptor[0])
    {
        strncpy_s(candidate, sizeof(candidate), descriptor, _TRUNCATE);
        char* slash = strrchr(candidate, '\\');
        if (slash)
        {
            strcpy_s(slash + 1,
                     sizeof(candidate) - (size_t)(slash + 1 - candidate),
                     "imagegui.png");
            if (VirtualPreviewPath(candidate, destination, capacity)) return true;
        }
    }

    char game[2 * MAX_PATH] = {};
    GameDirectory(game, sizeof(game));
    if (!game[0] || !toolName || !toolName[0]) return false;
    const char* leaf = strrchr(toolName, '/');
    leaf = leaf && leaf[1] ? leaf + 1 : toolName;

    // Most stock build tools do not have a Workshop-style imagegui.png.
    // Their actual catalogue artwork lives in editor/tool_<tool name>.png.
    // Reconstructing this stable asset path restores vanilla thumbnails
    // without retaining the tool's live texture pointer (which is invalidated
    // when a save is loaded).
    _snprintf_s(candidate, sizeof(candidate), _TRUNCATE,
                "%s\\media_soviet\\editor\\tool_%s.png", game, leaf);
    if (VirtualPreviewPath(candidate, destination, capacity)) return true;

    static const char* roots[] = {
        "buildings", "dlc2\\buildings", "dlc3\\buildings",
        "dlc4\\buildings", "cwc\\buildings"
    };
    for (size_t index = 0; index < sizeof(roots) / sizeof(roots[0]); ++index)
    {
        _snprintf_s(candidate, sizeof(candidate), _TRUNCATE,
                    "%s\\media_soviet\\%s\\%s\\imagegui.png",
                    game, roots[index], leaf);
        if (VirtualPreviewPath(candidate, destination, capacity)) return true;
    }
    return false;
}

static bool FindCatalogDescriptor(const char* toolName, char* destination,
                                  size_t capacity)
{
    if (!toolName || !toolName[0] || !destination || !capacity) return false;
    char game[2 * MAX_PATH] = {};
    GameDirectory(game, sizeof(game));
    if (!game[0]) return false;

    char relative[256] = {};
    strncpy_s(relative, sizeof(relative), toolName, _TRUNCATE);
    for (char* p = relative; *p; ++p) if (*p == '/') *p = '\\';

    // Local unpublished buildings are loaded by the game from workshop_wip.
    // Treat this as Tesmio's manual-package channel and read its descriptor
    // directly, without requiring a buildingN entry in tesmiomenu.ini.
    const char* wipRelative = relative;
    if (_strnicmp(wipRelative, "workshop_wip\\", 13) == 0)
        wipRelative += 13;
    if (!strstr(wipRelative, ".."))
    {
        _snprintf_s(destination, capacity, _TRUNCATE,
                    "%s\\media_soviet\\workshop_wip\\%s\\building.ini",
                    game, wipRelative);
        if (FileExistsA(destination)) return true;
    }

    // Loaded Workshop tools use "workshop-id/object-folder" names.
    const char* slash = strchr(toolName, '/');
    char first[32] = {};
    if (slash && (size_t)(slash - toolName) < sizeof(first))
    {
        memcpy(first, toolName, (size_t)(slash - toolName));
        first[slash - toolName] = 0;
    }
    if (slash && IsUnsignedNumber(first))
    {
        char steamapps[2 * MAX_PATH] = {};
        strncpy_s(steamapps, sizeof(steamapps), game, _TRUNCATE);
        char* common = strstr(steamapps, "\\common\\");
        if (common)
        {
            *common = 0;
            _snprintf_s(destination, capacity, _TRUNCATE,
                        "%s\\workshop\\content\\784150\\%s\\building.ini",
                        steamapps, relative);
            if (FileExistsA(destination)) return true;
        }
    }

    _snprintf_s(destination, capacity, _TRUNCATE,
                "%s\\media_soviet\\buildings_types\\%s.ini", game, relative);
    if (FileExistsA(destination)) return true;
    _snprintf_s(destination, capacity, _TRUNCATE,
                "%s\\media_soviet\\%s\\building.ini", game, relative);
    if (FileExistsA(destination)) return true;

    return false;
}

static int CatalogResourceIndex(const char* name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < g_catalogResourceCount; ++i)
        if (_stricmp(g_catalogResources[i].name, name) == 0) return i;
    return -1;
}

static void CleanResourceToken(char* token)
{
    if (!token) return;
    char* start = token;
    while (*start == '"' || *start == '\'') ++start;
    if (start != token) memmove(token, start, strlen(start) + 1);
    size_t length = strlen(token);
    while (length && (token[length - 1] == '"' || token[length - 1] == '\'' ||
                      token[length - 1] == ',' || token[length - 1] == ';'))
        token[--length] = 0;
}

static void AddMetadataResource(char values[][64], int* count, const char* name)
{
    if (!values || !count || !name || !name[0] || *count >= MAX_ITEM_RESOURCES)
        return;
    char cleaned[64] = {};
    strncpy_s(cleaned, sizeof(cleaned), name, _TRUNCATE);
    CleanResourceToken(cleaned);
    if (CatalogResourceIndex(cleaned) < 0) return;
    for (int i = 0; i < *count; ++i)
        if (_stricmp(values[i], cleaned) == 0) return;
    strncpy_s(values[*count], 64, cleaned, _TRUNCATE);
    ++*count;
}

enum CatalogTransport
{
    CATALOG_TRANSPORT_UNKNOWN = -1,
    CATALOG_TRANSPORT_COVERED,
    CATALOG_TRANSPORT_OPEN,
    CATALOG_TRANSPORT_GRAVEL,
    CATALOG_TRANSPORT_OIL,
    CATALOG_TRANSPORT_CEMENT,
    CATALOG_TRANSPORT_COOLER,
    CATALOG_TRANSPORT_LIVESTOCK,
    CATALOG_TRANSPORT_CONCRETE,
    CATALOG_TRANSPORT_ELECTRIC,
    CATALOG_TRANSPORT_NUCLEAR1,
    CATALOG_TRANSPORT_NUCLEAR2,
    CATALOG_TRANSPORT_HEATING,
    CATALOG_TRANSPORT_WATER,
    CATALOG_TRANSPORT_SEWAGE,
    CATALOG_TRANSPORT_WASTE,
    CATALOG_TRANSPORT_VEHICLES
};

static bool ResourceNameIn(const char* name, const char* const* values,
                           size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (_stricmp(name, values[i]) == 0) return true;
    return false;
}

static CatalogTransport BaseResourceTransport(const char* name)
{
    static const char* covered[] = {
        "plants", "chemicals", "fabric", "alcohol", "food", "clothes",
        "ecomponents", "mcomponents", "plastics", "eletronics", "explosives",
        "fertiliser"
    };
    static const char* open[] = {
        "steel", "aluminium", "prefabpanels", "bricks", "wood", "boards"
    };
    static const char* gravel[] = {
        "gravel", "rawgravel", "coal", "rawcoal", "iron", "rawiron",
        "bauxite", "rawbauxite", "uranium", "yellowcake"
    };
    static const char* oil[] = { "oil", "bitumen", "fuel", "fertiliser_liquid" };
    static const char* waste[] = {
        "waste_gravel", "waste_steel", "waste_aluminium", "waste_plastic",
        "waste_bio", "waste_burnable", "waste_toxic", "waste_other", "waste_ash"
    };
    if (ResourceNameIn(name, covered, sizeof(covered) / sizeof(covered[0])))
        return CATALOG_TRANSPORT_COVERED;
    if (ResourceNameIn(name, open, sizeof(open) / sizeof(open[0])))
        return CATALOG_TRANSPORT_OPEN;
    if (ResourceNameIn(name, gravel, sizeof(gravel) / sizeof(gravel[0])))
        return CATALOG_TRANSPORT_GRAVEL;
    if (ResourceNameIn(name, oil, sizeof(oil) / sizeof(oil[0])))
        return CATALOG_TRANSPORT_OIL;
    if (ResourceNameIn(name, waste, sizeof(waste) / sizeof(waste[0])))
        return CATALOG_TRANSPORT_WASTE;
    if (_stricmp(name, "cement") == 0 || _stricmp(name, "alumina") == 0)
        return CATALOG_TRANSPORT_CEMENT;
    if (_stricmp(name, "meat") == 0) return CATALOG_TRANSPORT_COOLER;
    if (_stricmp(name, "livestock") == 0) return CATALOG_TRANSPORT_LIVESTOCK;
    if (_stricmp(name, "asphalt") == 0 || _stricmp(name, "concrete") == 0)
        return CATALOG_TRANSPORT_CONCRETE;
    if (_stricmp(name, "eletric") == 0) return CATALOG_TRANSPORT_ELECTRIC;
    if (_stricmp(name, "uf6") == 0) return CATALOG_TRANSPORT_NUCLEAR1;
    if (_stricmp(name, "nuclearfuel") == 0 ||
        _stricmp(name, "nuclearfuelburned") == 0)
        return CATALOG_TRANSPORT_NUCLEAR2;
    if (_stricmp(name, "heat") == 0) return CATALOG_TRANSPORT_HEATING;
    if (_stricmp(name, "water") == 0) return CATALOG_TRANSPORT_WATER;
    if (_stricmp(name, "usagewater") == 0) return CATALOG_TRANSPORT_SEWAGE;
    if (_stricmp(name, "vehicles") == 0) return CATALOG_TRANSPORT_VEHICLES;
    return CATALOG_TRANSPORT_UNKNOWN;
}

static CatalogTransport CatalogResourceTransport(const char* name, int depth = 0)
{
    if (!name || !name[0] || depth > 8) return CATALOG_TRANSPORT_UNKNOWN;
    CatalogTransport direct = BaseResourceTransport(name);
    if (direct != CATALOG_TRANSPORT_UNKNOWN) return direct;
    int index = CatalogResourceIndex(name);
    if (index < 0) return CATALOG_TRANSPORT_UNKNOWN;
    const char* resourceTemplate = g_catalogResources[index].templateName;
    if (!resourceTemplate[0] || _stricmp(resourceTemplate, name) == 0)
        return CATALOG_TRANSPORT_UNKNOWN;
    return CatalogResourceTransport(resourceTemplate, depth + 1);
}

static CatalogTransport TransportFromStorageLine(const char* line)
{
    struct Token { const char* text; CatalogTransport transport; };
    static const Token tokens[] = {
        { "RESOURCE_TRANSPORT_COVERED", CATALOG_TRANSPORT_COVERED },
        { "RESOURCE_TRANSPORT_OPEN", CATALOG_TRANSPORT_OPEN },
        { "RESOURCE_TRANSPORT_GRAVEL", CATALOG_TRANSPORT_GRAVEL },
        { "RESOURCE_TRANSPORT_OIL", CATALOG_TRANSPORT_OIL },
        { "RESOURCE_TRANSPORT_CEMENT", CATALOG_TRANSPORT_CEMENT },
        { "RESOURCE_TRANSPORT_COOLER", CATALOG_TRANSPORT_COOLER },
        { "RESOURCE_TRANSPORT_LIVESTOCK", CATALOG_TRANSPORT_LIVESTOCK },
        { "RESOURCE_TRANSPORT_CONCRETE", CATALOG_TRANSPORT_CONCRETE },
        { "RESOURCE_TRANSPORT_ELETRIC", CATALOG_TRANSPORT_ELECTRIC },
        { "RESOURCE_TRANSPORT_NUCLEAR1", CATALOG_TRANSPORT_NUCLEAR1 },
        { "RESOURCE_TRANSPORT_NUCLEAR2", CATALOG_TRANSPORT_NUCLEAR2 },
        { "RESOURCE_TRANSPORT_HEATING", CATALOG_TRANSPORT_HEATING },
        { "RESOURCE_TRANSPORT_WATER", CATALOG_TRANSPORT_WATER },
        { "RESOURCE_TRANSPORT_SEWAGE", CATALOG_TRANSPORT_SEWAGE },
        { "RESOURCE_TRANSPORT_WASTE", CATALOG_TRANSPORT_WASTE },
        { "RESOURCE_TRANSPORT_VEHICLES", CATALOG_TRANSPORT_VEHICLES }
    };
    for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i)
        if (AsciiContainsNoCase(line, tokens[i].text)) return tokens[i].transport;
    return CATALOG_TRANSPORT_UNKNOWN;
}

static void AddTransportResources(char values[][64], int* count,
                                  CatalogTransport transport)
{
    if (transport == CATALOG_TRANSPORT_UNKNOWN) return;
    for (int i = 0; i < g_catalogResourceCount; ++i)
        if (CatalogResourceTransport(g_catalogResources[i].name) == transport)
            AddMetadataResource(values, count, g_catalogResources[i].name);
}

static bool BaseDemandContains(const char* category, const char* resource)
{
    if (!category || !resource) return false;
    if (_stricmp(category, "basic") == 0)
        return _stricmp(resource, "food") == 0 || _stricmp(resource, "meat") == 0;
    if (_stricmp(category, "medium") == 0)
        return _stricmp(resource, "food") == 0 || _stricmp(resource, "clothes") == 0;
    if (_stricmp(category, "advanced") == 0)
        return _stricmp(resource, "food") == 0 || _stricmp(resource, "clothes") == 0 ||
               _stricmp(resource, "eletronics") == 0;
    if (_stricmp(category, "mediumadvanced") == 0)
        return _stricmp(resource, "clothes") == 0 ||
               _stricmp(resource, "eletronics") == 0;
    if (_stricmp(category, "hotel") == 0)
        return _stricmp(resource, "food") == 0 || _stricmp(resource, "alcohol") == 0 ||
               _stricmp(resource, "meat") == 0;
    if (_stricmp(category, "prison") == 0)
        return _stricmp(resource, "food") == 0 || _stricmp(resource, "meat") == 0;
    return false;
}

static void AddDemandCategoryResources(CatalogItemMetadata& metadata,
                                       const char* category,
                                       CatalogTransport transport)
{
    static const char* baseDemands[] = {
        "food", "meat", "clothes", "eletronics", "alcohol"
    };
    for (size_t i = 0; i < sizeof(baseDemands) / sizeof(baseDemands[0]); ++i)
    {
        const char* resource = baseDemands[i];
        if (!BaseDemandContains(category, resource) ||
            CatalogResourceTransport(resource) != transport)
            continue;
        AddMetadataResource(metadata.consumes, &metadata.consumeCount, resource);
        AddMetadataResource(metadata.stores, &metadata.storeCount, resource);
    }
    for (int i = 0; i < g_catalogNeedCount; ++i)
    {
        const CatalogNeed& need = g_catalogNeeds[i];
        bool categoryMatches = _stricmp(need.category, "auto") == 0
            ? BaseDemandContains(category, need.donor)
            : _stricmp(need.category, category) == 0;
        if (!categoryMatches ||
            CatalogResourceTransport(need.resource) != transport)
            continue;
        AddMetadataResource(metadata.consumes, &metadata.consumeCount,
                            need.resource);
        AddMetadataResource(metadata.stores, &metadata.storeCount,
                            need.resource);
    }
}

static bool DirectiveLine(const char* line, const char* directive)
{
    size_t length = strlen(directive);
    return _strnicmp(line, directive, length) == 0 &&
           (line[length] == 0 || line[length] == ' ' || line[length] == '\t');
}

static void FallbackItemCaption(const char* toolName, wchar_t* destination,
                                size_t capacity)
{
    const char* leaf = toolName ? toolName : "Building";
    const char* slash = strrchr(leaf, '/');
    if (slash && slash[1]) leaf = slash + 1;
    char caption[192] = {};
    size_t output = 0;
    for (size_t i = 0; leaf[i] && output + 1 < sizeof(caption); ++i)
    {
        unsigned char value = (unsigned char)leaf[i];
        unsigned char previous = i ? (unsigned char)leaf[i - 1] : 0;
        if ((value == '_' || value == '-') && output && caption[output - 1] != ' ')
        {
            caption[output++] = ' ';
            continue;
        }
        if (i && value >= 'A' && value <= 'Z' &&
            ((previous >= 'a' && previous <= 'z') ||
             (previous >= '0' && previous <= '9')) &&
            output && caption[output - 1] != ' ')
            caption[output++] = ' ';
        if (!output && value >= 'a' && value <= 'z') value -= 'a' - 'A';
        caption[output++] = (char)value;
    }
    caption[output] = 0;
    MultiByteToWideChar(CP_UTF8, 0, caption, -1, destination, (int)capacity);
    destination[capacity - 1] = 0;
}

static void ParseCatalogDescriptor(CatalogItem& item, const char* path)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 2 * 1024 * 1024)
    {
        CloseHandle(file);
        return;
    }
    char* data = (char*)malloc((size_t)size.QuadPart + 1);
    if (!data) { CloseHandle(file); return; }
    DWORD read = 0;
    bool ok = ReadFile(file, data, (DWORD)size.QuadPart, &read, NULL) != FALSE;
    CloseHandle(file);
    if (!ok) { free(data); return; }
    data[read] = 0;

    int nameId = -1;
    bool hasLiteralName = false;
    bool isShop = false;
    bool isVehicleDealer = false;
    bool isPub = false;
    bool usesElectricRoad = false;
    char* context = NULL;
    for (char* raw = strtok_s(data, "\r\n", &context); raw;
         raw = strtok_s(NULL, "\r\n", &context))
    {
        char* line = TrimAscii(raw);
        if (!line[0] || !strncmp(line, "//", 2) || line[0] == '-') continue;
        if (DirectiveLine(line, "$NAME_STR"))
        {
            const char* quote = strchr(line, '"');
            const char* end = quote ? strrchr(quote + 1, '"') : NULL;
            if (quote && end && end > quote + 1)
            {
                char caption[256] = {};
                size_t length = (size_t)(end - quote - 1);
                if (length >= sizeof(caption)) length = sizeof(caption) - 1;
                memcpy(caption, quote + 1, length);
                MultiByteToWideChar(CP_UTF8, 0, caption, -1, item.display,
                                    (int)(sizeof(item.display) / sizeof(wchar_t)));
                hasLiteralName = item.display[0] != 0;
            }
            continue;
        }
        if (DirectiveLine(line, "$NAME"))
        {
            sscanf(line + 5, "%d", &nameId);
            continue;
        }

        if (DirectiveLine(line, "$TYPE_SHOP")) isShop = true;
        if (DirectiveLine(line, "$TYPE_CAR_DEALER")) isVehicleDealer = true;
        if (DirectiveLine(line, "$TYPE_PUB")) isPub = true;
        if (DirectiveLine(line, "$SUBTYPE_TRAM") ||
            DirectiveLine(line, "$SUBTYPE_TROLLEYBUS") ||
            DirectiveLine(line, "$ROADVEHICLE_TRAM") ||
            DirectiveLine(line, "$ROADVEHICLE_ELETRIC") ||
            DirectiveLine(line, "$TYPE_TRAM_GATE"))
            usesElectricRoad = true;

        if (_strnicmp(line, "$ELETRIC_CONSUMPTION_", 22) == 0)
        {
            // Loading/unloading consumers are explicit fixed utility loads.
            // Worker-factor directives only tune the game's generic building
            // demand and are intentionally not treated as a separate input.
            if (AsciiContainsNoCase(line, "_FIXED"))
                AddMetadataResource(item.metadata.consumes,
                                    &item.metadata.consumeCount, "eletric");
        }

        char resource[64] = {};
        if (DirectiveLine(line, "$PRODUCTION"))
        {
            if (sscanf(line + 11, "%63s", resource) == 1)
                AddMetadataResource(item.metadata.produces,
                                    &item.metadata.produceCount, resource);
        }
        else if (DirectiveLine(line, "$CONSUMPTION"))
        {
            if (sscanf(line + 12, "%63s", resource) == 1)
                AddMetadataResource(item.metadata.consumes,
                                    &item.metadata.consumeCount, resource);
        }
        else if (DirectiveLine(line, "$CONSUMPTION_PER_SECOND"))
        {
            if (sscanf(line + 23, "%63s", resource) == 1)
                AddMetadataResource(item.metadata.consumes,
                                    &item.metadata.consumeCount, resource);
        }
        else if (_strnicmp(line, "$STORAGE_DEMAND_", 16) == 0)
        {
            const char* category = NULL;
            if (AsciiContainsNoCase(line, "MEDIUMADVANCED"))
                category = "mediumadvanced";
            else if (AsciiContainsNoCase(line, "ADVANCED"))
                category = "advanced";
            else if (AsciiContainsNoCase(line, "MEDIUM"))
                category = "medium";
            else if (AsciiContainsNoCase(line, "BASIC"))
                category = "basic";
            else if (AsciiContainsNoCase(line, "HOTEL"))
                category = "hotel";
            else if (AsciiContainsNoCase(line, "PRISON"))
                category = "prison";
            CatalogTransport transport = TransportFromStorageLine(line);
            if (category)
                AddDemandCategoryResources(item.metadata, category, transport);

            // A demand shelf can also name one resource explicitly. Keep that
            // resource even when it belongs to a custom category unknown to
            // this catalog version.
            if (AsciiContainsNoCase(line, "SPECIAL"))
            {
                char copy[384] = {};
                strncpy_s(copy, sizeof(copy), line, _TRUNCATE);
                char* tokenContext = NULL;
                char* token = strtok_s(copy, " \t", &tokenContext);
                char* last = NULL;
                while (token)
                {
                    last = token;
                    token = strtok_s(NULL, " \t", &tokenContext);
                }
                if (last)
                {
                    AddMetadataResource(item.metadata.stores,
                                        &item.metadata.storeCount, last);
                    AddMetadataResource(item.metadata.consumes,
                                        &item.metadata.consumeCount, last);
                }
            }
        }
        else if (_strnicmp(line, "$STORAGE_", 9) == 0 &&
                 AsciiContainsNoCase(line, "SPECIAL"))
        {
            char copy[384] = {};
            strncpy_s(copy, sizeof(copy), line, _TRUNCATE);
            char* tokenContext = NULL;
            char* token = strtok_s(copy, " \t", &tokenContext);
            char* last = NULL;
            while (token) { last = token; token = strtok_s(NULL, " \t", &tokenContext); }
            if (last)
                AddMetadataResource(item.metadata.stores,
                                    &item.metadata.storeCount, last);
        }
        else if (DirectiveLine(line, "$STORAGE"))
        {
            // A plain storage is a warehouse/transfer shelf whose accepted
            // goods are defined by transport class rather than one resource.
            // Import/export buffers of factories are deliberately excluded:
            // their exact inputs and outputs come from the production lines.
            AddTransportResources(item.metadata.stores,
                                  &item.metadata.storeCount,
                                  TransportFromStorageLine(line));
        }
    }
    free(data);
    // Shops, vehicle dealers and pubs use their storage as operating stock,
    // not as a passive warehouse.  In particular, vanilla bars and cafes
    // declare alcohol through $STORAGE_SPECIAL rather than $CONSUMPTION.
    if (isShop || isVehicleDealer || isPub)
        for (int i = 0; i < item.metadata.storeCount; ++i)
            AddMetadataResource(item.metadata.consumes,
                                &item.metadata.consumeCount,
                                item.metadata.stores[i]);
    if (usesElectricRoad || AsciiContainsNoCase(item.toolName, "tram") ||
        AsciiContainsNoCase(item.toolName, "trolley"))
        AddMetadataResource(item.metadata.consumes,
                            &item.metadata.consumeCount, "eletric");
    int localizedNameId = nameId;
    if (localizedNameId < 0 && item.source == CATALOG_SOURCE_VANILLA)
        localizedNameId = EnglishTextIdByValue(item.display);
    if (localizedNameId >= 0)
    {
        wchar_t localized[128] = {};
        if (ActiveTextById(localizedNameId, localized,
                           sizeof(localized) / sizeof(wchar_t)))
            wcsncpy_s(item.display,
                      sizeof(item.display) / sizeof(wchar_t),
                      localized, _TRUNCATE);
    }
    (void)hasLiteralName;
}

static bool WideContainsNoCase(const wchar_t* text, const wchar_t* needle)
{
    if (!text || !needle || !needle[0]) return false;
    size_t needleLength = wcslen(needle);
    for (const wchar_t* start = text; *start; ++start)
    {
        size_t matched = 0;
        while (matched < needleLength && start[matched] &&
               towlower(start[matched]) == towlower(needle[matched]))
            ++matched;
        if (matched == needleLength) return true;
    }
    return false;
}

// Network tools (roads, bridges and tunnels) are generated by the game and
// do not have a building.ini descriptor. Infer their utility requirement from
// the stable internal tool name and vanilla English localization instead of
// maintaining a fragile list of individual bridges and tunnels.
static void InferCatalogFunctionalConsumption(CatalogItem& item)
{
    bool roadFamily = AsciiContainsNoCase(item.metadata.type, "road");
    bool electricVariant = AsciiContainsNoCase(item.toolName, "tram") ||
                           AsciiContainsNoCase(item.toolName, "trolley") ||
                           AsciiContainsNoCase(item.toolName, "electric") ||
                           AsciiContainsNoCase(item.toolName, "eletric") ||
                           AsciiContainsNoCase(item.descriptorPath, "tram") ||
                           AsciiContainsNoCase(item.descriptorPath, "trolley") ||
                           AsciiContainsNoCase(item.descriptorPath, "electric") ||
                           AsciiContainsNoCase(item.descriptorPath, "eletric");
    wchar_t englishName[192] = {};
    if (item.nameTextId > 0)
        EnglishTextById(item.nameTextId, englishName,
                        sizeof(englishName) / sizeof(wchar_t));
    if (WideContainsNoCase(englishName, L"tram") ||
        WideContainsNoCase(englishName, L"trolley") ||
        WideContainsNoCase(englishName, L"electric"))
        electricVariant = true;

    if (roadFamily && electricVariant)
        AddMetadataResource(item.metadata.consumes,
                            &item.metadata.consumeCount, "eletric");
}

static void ApplyCatalogItemLanguageOverrides(CatalogItem& item)
{
    if (item.source == CATALOG_SOURCE_VANILLA &&
        g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN)
    {
        int textId = EnglishTextIdByValue(item.display);
        wchar_t localized[128] = {};
        if (textId >= 0 && ActiveTextById(textId, localized,
                                          sizeof(localized) / sizeof(wchar_t)))
            wcsncpy_s(item.display,
                      sizeof(item.display) / sizeof(wchar_t),
                      localized, _TRUNCATE);
    }
    if (g_catalogLanguage != CATALOG_LANGUAGE_RUSSIAN) return;

    // Literal $NAME_STR values belong to the package author and therefore do
    // not pass through the game's language table. Keep a small, explicit set
    // of translations for the Tesmio packages shipped/tested with this catalog.
    // Unknown future packages retain their author-provided name instead of
    // receiving an unreliable machine-generated caption.
    if (_wcsicmp(item.display, L"Furniture Factory") == 0 ||
        AsciiContainsNoCase(item.toolName, "FurnitureFactory"))
        wcscpy_s(item.display, sizeof(item.display) / sizeof(wchar_t),
                 L"Мебельная фабрика");
}

static void ApplyVanillaToolCaption(CatalogItem& item)
{
    if (item.source != CATALOG_SOURCE_VANILLA || !item.tool ||
        !H->readablePtr((unsigned char*)item.tool + 0x40, sizeof(int)))
        return;
    int textId = *(int*)((unsigned char*)item.tool + 0x40);
    item.nameTextId = textId;
    wchar_t localized[128] = {};
    if (textId > 0 && CatalogTextById(textId, localized,
                                      sizeof(localized) / sizeof(wchar_t)))
        wcsncpy_s(item.display, sizeof(item.display) / sizeof(wchar_t),
                  localized, _TRUNCATE);
}

static bool IsConfiguredTesmioTool(void* tool)
{
    for (int i = 0; i < g_catalogBuildingCount; ++i)
        if (g_catalogBuildingTools[i] == tool) return true;
    return false;
}

static CatalogSource DetectCatalogSource(void* tool, const char* toolName)
{
    if (IsConfiguredTesmioTool(tool)) return CATALOG_SOURCE_TESMIO;
    if (WorkshopPackageHasTesmioPayload(toolName))
        return CATALOG_SOURCE_TESMIO;

    char descriptor[4 * MAX_PATH] = {};
    if (FindCatalogDescriptor(toolName, descriptor, sizeof(descriptor)) &&
        ContainsAsciiInsensitive((const unsigned char*)descriptor,
                                 strlen(descriptor), "workshop_wip"))
        return CATALOG_SOURCE_TESMIO;

    char preview[512] = {};
    if (ReadToolPreviewPath(tool, preview, sizeof(preview)) &&
        ContainsAsciiInsensitive((const unsigned char*)preview,
                                 strlen(preview), "workshop_wip"))
        return CATALOG_SOURCE_TESMIO;

    const char* slash = toolName ? strchr(toolName, '/') : NULL;
    if (slash)
    {
        char first[32] = {};
        size_t length = (size_t)(slash - toolName);
        if (length < sizeof(first))
        {
            memcpy(first, toolName, length);
            first[length] = 0;
            if (IsUnsignedNumber(first)) return CATALOG_SOURCE_WORKSHOP;
        }
    }
    return CATALOG_SOURCE_VANILLA;
}

static int ExistingCatalogItem(void* tool)
{
    for (int i = 0; i < g_catalogItemCount; ++i)
        if (g_catalogItems[i].tool == tool) return i;
    return -1;
}

static int InferCatalogTypeIndex(const CatalogItemMetadata& metadata);

static void AddCatalogItem(void* tool, int typeIndex, bool forceTesmio)
{
    if (!tool || typeIndex < 0 || typeIndex >= g_catalogTypeCount) return;
    int existing = ExistingCatalogItem(tool);
    if (existing >= 0)
    {
        if (forceTesmio) g_catalogItems[existing].source = CATALOG_SOURCE_TESMIO;
        return;
    }
    if (g_catalogItemCount >= MAX_CATALOG_ITEMS ||
        !H->readablePtr(tool, TOOL_BUILDING + sizeof(void*))) return;
    const char* name = (const char*)tool;
    if (!memchr(name, 0, 128)) return;

    char preview[512] = {};
    ReadToolPreviewPath(tool, preview, sizeof(preview));
    char descriptor[4 * MAX_PATH] = {};
    bool foundDescriptor =
        FindCatalogDescriptor(name, descriptor, sizeof(descriptor)) ||
        DescriptorFromPreviewPath(preview, descriptor, sizeof(descriptor));

    // The loader can expose several construction-tool wrappers for one object
    // (the test quarry currently has three).  Their wrapper names can differ as
    // well, so use the resolved building.ini path as the canonical building
    // identity.  Distinct Tesmio packages necessarily resolve to distinct
    // descriptors and remain separate cards.
    for (int i = 0; i < g_catalogItemCount; ++i)
    {
        bool sameName = _stricmp(g_catalogItems[i].toolName, name) == 0;
        bool sameDescriptor = foundDescriptor &&
            g_catalogItems[i].descriptorPath[0] &&
            _stricmp(g_catalogItems[i].descriptorPath, descriptor) == 0;
        if (!sameName && !sameDescriptor) continue;
        if (forceTesmio) g_catalogItems[i].source = CATALOG_SOURCE_TESMIO;
        return;
    }

    CatalogItem& item = g_catalogItems[g_catalogItemCount++];
    memset(&item, 0, sizeof(item));
    item.tool = tool;
    item.buildingType = *(void**)((unsigned char*)tool + TOOL_BUILDING);
    item.typeIndex = typeIndex;
    strncpy_s(item.toolName, sizeof(item.toolName), name, _TRUNCATE);
    strncpy_s(item.metadata.objectName, sizeof(item.metadata.objectName), name,
              _TRUNCATE);
    strncpy_s(item.metadata.type, sizeof(item.metadata.type),
              g_catalogTypes[typeIndex].name, _TRUNCATE);
    item.source = forceTesmio ? CATALOG_SOURCE_TESMIO
                              : DetectCatalogSource(tool, name);
    strncpy_s(item.previewPath, sizeof(item.previewPath), preview, _TRUNCATE);
    if (foundDescriptor)
        strncpy_s(item.descriptorPath, sizeof(item.descriptorPath), descriptor,
                  _TRUNCATE);
    FallbackItemCaption(name, item.display,
                        sizeof(item.display) / sizeof(wchar_t));
    if (foundDescriptor)
        ParseCatalogDescriptor(item, descriptor);
    if (strcmp(g_catalogTypes[item.typeIndex].name,
               CATALOG_UNDEFINED_TYPE) == 0)
    {
        int inferredType = InferCatalogTypeIndex(item.metadata);
        if (inferredType >= 0)
        {
            item.typeIndex = inferredType;
            strncpy_s(item.metadata.type, sizeof(item.metadata.type),
                      g_catalogTypes[inferredType].name, _TRUNCATE);
            if (g_probe)
                H->log("tesmiomenu  catalog: inferred type %s for %s",
                       g_catalogTypes[inferredType].name, item.toolName);
        }
    }
    ApplyVanillaToolCaption(item);
    InferCatalogFunctionalConsumption(item);
    ApplyCatalogItemLanguageOverrides(item);
    InferPreviewPath(name, foundDescriptor ? descriptor : NULL,
                     item.fallbackPreviewPath,
                     sizeof(item.fallbackPreviewPath));

}

static int FindCatalogTypeIndex(const char* name)
{
    for (int i = 0; i < g_catalogTypeCount; ++i)
        if (_stricmp(g_catalogTypes[i].name, name) == 0) return i;
    return -1;
}

static int FindCatalogTypeIndexByTextId(int textId)
{
    for (int i = 0; i < g_catalogTypeCount; ++i)
        if (g_catalogTypes[i].textId == textId) return i;
    return -1;
}

static const char* CatalogRootResourceTemplate(const char* resource)
{
    if (!resource || !resource[0]) return NULL;
    const char* current = resource;
    for (int depth = 0; depth < 8; ++depth)
    {
        int index = CatalogResourceIndex(current);
        if (index < 0) break;
        const char* resourceTemplate = g_catalogResources[index].templateName;
        if (!resourceTemplate[0] || _stricmp(resourceTemplate, current) == 0)
            break;
        current = resourceTemplate;
    }
    return current;
}

static int ResourceIndustryTextId(const char* resource)
{
    const char* root = CatalogRootResourceTemplate(resource);
    if (!root) return -1;

    // These are unambiguous production families in the stock game.  New
    // Tesmio resources inherit the family of their declared template, so a
    // copper chain cloned from iron/bauxite/steel/aluminium is classified as
    // metallurgy without requiring cooperation from the Workshop author.
    static const char* metallurgy[] = {
        "rawiron", "iron", "steel", "rawbauxite", "bauxite", "alumina",
        "aluminium"
    };
    for (size_t i = 0; i < sizeof(metallurgy) / sizeof(metallurgy[0]); ++i)
        if (_stricmp(root, metallurgy[i]) == 0) return 67082;

    static const char* nuclear[] = {
        "uranium", "yellowcake", "uf6", "nuclearfuel", "nuclearfuelburned"
    };
    for (size_t i = 0; i < sizeof(nuclear) / sizeof(nuclear[0]); ++i)
        if (_stricmp(root, nuclear[i]) == 0) return 67083;

    return -1;
}

static int InferCatalogTypeIndex(const CatalogItemMetadata& metadata)
{
    int inferredTextId = -1;
    for (int i = 0; i < metadata.produceCount; ++i)
    {
        int resourceTextId = ResourceIndustryTextId(metadata.produces[i]);
        if (resourceTextId < 0) continue;
        if (inferredTextId >= 0 && inferredTextId != resourceTextId)
            return -1;
        inferredTextId = resourceTextId;
    }
    return inferredTextId >= 0
        ? FindCatalogTypeIndexByTextId(inferredTextId) : -1;
}

static void RemoveEmptyCatalogTypes()
{
    int usage[MAX_CATALOG_TYPES] = {};
    int remap[MAX_CATALOG_TYPES] = {};
    for (int i = 0; i < MAX_CATALOG_TYPES; ++i) remap[i] = -1;
    for (int i = 0; i < g_catalogItemCount; ++i)
        if (g_catalogItems[i].typeIndex >= 0 &&
            g_catalogItems[i].typeIndex < g_catalogTypeCount)
            ++usage[g_catalogItems[i].typeIndex];

    int kept = 0;
    for (int i = 0; i < g_catalogTypeCount; ++i)
    {
        if (!usage[i]) continue;
        remap[i] = kept;
        if (kept != i) g_catalogTypes[kept] = g_catalogTypes[i];
        ++kept;
    }
    for (int i = 0; i < g_catalogItemCount; ++i)
    {
        int oldIndex = g_catalogItems[i].typeIndex;
        if (oldIndex >= 0 && oldIndex < g_catalogTypeCount && remap[oldIndex] >= 0)
            g_catalogItems[i].typeIndex = remap[oldIndex];
    }
    if (kept != g_catalogTypeCount)
        H->log("tesmiomenu  catalog: %d empty subcategories removed",
               g_catalogTypeCount - kept);
    g_catalogTypeCount = kept;
    g_selectedType = 0;
}

static void CaptureCatalogItems()
{
    g_catalogItemCount = 0;
    g_workshopTesmioSourceCacheCount = 0;
    RawVector* tabs = (RawVector*)(g_base + G_BOTTOM_TABS);
    if (!H->readablePtr(tabs, sizeof(*tabs)) || !tabs->begin || !tabs->end ||
        tabs->end < tabs->begin || ((tabs->end - tabs->begin) % TAB_SIZE) != 0)
        return;
    size_t tabCount = (size_t)(tabs->end - tabs->begin) / TAB_SIZE;
    for (size_t tabIndex = 0; tabIndex < tabCount; ++tabIndex)
    {
        unsigned char* tab = tabs->begin + tabIndex * TAB_SIZE;
        const char* tabName = (const char*)tab;
        if (!memchr(tabName, 0, 64) || !strcmp(tabName, "tesmioloader")) continue;
        RawVector* groups = (RawVector*)(tab + TAB_GROUPS);
        if (!H->readablePtr(groups, sizeof(*groups)) || !groups->begin ||
            !groups->end || groups->end < groups->begin ||
            ((groups->end - groups->begin) % GROUP_SIZE) != 0)
            continue;
        size_t groupCount = (size_t)(groups->end - groups->begin) / GROUP_SIZE;
        if (groupCount > 512) continue;
        RawVector* titles = (RawVector*)(tab + 0x68);
        RawVector* boundaries = (RawVector*)(tab + 0x80);
        bool validSections = H->readablePtr(titles, sizeof(*titles)) &&
            H->readablePtr(boundaries, sizeof(*boundaries)) &&
            titles->begin && titles->end && titles->end >= titles->begin &&
            boundaries->begin && boundaries->end &&
            boundaries->end >= boundaries->begin &&
            ((titles->end - titles->begin) % sizeof(int)) == 0 &&
            ((boundaries->end - boundaries->begin) % sizeof(int)) == 0;
        size_t sectionCount = validSections
            ? (size_t)(titles->end - titles->begin) / sizeof(int) : 0;
        size_t boundaryCount = validSections
            ? (size_t)(boundaries->end - boundaries->begin) / sizeof(int) : 0;
        if (!sectionCount || sectionCount != boundaryCount || sectionCount > 512)
            validSections = false;
        else if (!H->readablePtr(titles->begin, sectionCount * sizeof(int)) ||
                 !H->readablePtr(boundaries->begin,
                                 boundaryCount * sizeof(int)))
            validSections = false;
        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            unsigned char* group = groups->begin + groupIndex * GROUP_SIZE;
            int textId = *(int*)(tab + 0x48);
            if (validSections)
            {
                for (size_t sectionIndex = 0; sectionIndex < sectionCount;
                     ++sectionIndex)
                {
                    int boundary = *(int*)(boundaries->begin +
                                           sectionIndex * sizeof(int));
                    if ((int)groupIndex < boundary)
                    {
                        textId = *(int*)(titles->begin +
                                         sectionIndex * sizeof(int));
                        break;
                    }
                }
            }
            int typeIndex = FindCatalogTypeIndexByTextId(textId);
            if (typeIndex < 0) continue;
            RawVector* members = (RawVector*)(group + 8);
            if (!H->readablePtr(members, sizeof(*members)) || !members->begin ||
                !members->end || members->end < members->begin ||
                ((members->end - members->begin) % sizeof(void*)) != 0)
                continue;
            size_t memberCount = (size_t)(members->end - members->begin) /
                                 sizeof(void*);
            if (memberCount > 4096 ||
                !H->readablePtr(members->begin, memberCount * sizeof(void*)))
                continue;
            for (size_t member = 0; member < memberCount; ++member)
            {
                void* tool = *(void**)(members->begin + member * sizeof(void*));
                AddCatalogItem(tool, typeIndex, false);
            }
        }
    }

    // A physical building can exist in the engine's global tool registry
    // without being assigned to a stock bottom-menu group.  This happens for
    // Workshop packages that use a custom/unknown subcategory and was the
    // reason their resources appeared in the filters while their cards were
    // missing.  Scan the registry as a second source and retain both missing
    // Workshop buildings and loader-powered Tesmio buildings.  Their source
    // remains distinct: merely consuming a Tesmio resource never turns a
    // normal Workshop building into a Tesmio package.
    int undefinedType = FindCatalogTypeIndex(CATALOG_UNDEFINED_TYPE);
    int discoveredWorkshop = 0;
    int discoveredTesmio = 0;
    RawVector* allTools = (RawVector*)(g_base + G_GAME + TOOL_VECTOR);
    if (H->readablePtr(allTools, sizeof(*allTools)) && allTools->begin &&
        allTools->end && allTools->end >= allTools->begin &&
        ((allTools->end - allTools->begin) % TOOL_SIZE) == 0)
    {
        size_t toolCount = (size_t)(allTools->end - allTools->begin) / TOOL_SIZE;
        if (toolCount <= 4096 &&
            H->readablePtr(allTools->begin, toolCount * TOOL_SIZE))
        {
            for (size_t toolIndex = 0; toolIndex < toolCount; ++toolIndex)
            {
                void* tool = allTools->begin + toolIndex * TOOL_SIZE;
                if (ExistingCatalogItem(tool) >= 0) continue;
                if (!H->readablePtr((unsigned char*)tool + TOOL_BUILDING,
                                    sizeof(void*)) ||
                    !*(void**)((unsigned char*)tool + TOOL_BUILDING))
                    continue;
                int before = g_catalogItemCount;
                AddCatalogItem(tool, undefinedType, false);
                if (g_catalogItemCount == before) continue;
                if (g_catalogItems[before].source == CATALOG_SOURCE_TESMIO)
                    ++discoveredTesmio;
                else if (g_catalogItems[before].source ==
                         CATALOG_SOURCE_WORKSHOP)
                    ++discoveredWorkshop;
                else
                    g_catalogItemCount = before;
            }
        }
    }
    H->log("tesmiomenu  catalog: %d ungrouped Workshop and %d Tesmio tool(s) discovered",
           discoveredWorkshop, discoveredTesmio);

    // Explicitly configured legacy structures remain supported.  Without a
    // proven native category they belong to the same honest fallback bucket.
    for (int i = 0; i < g_catalogBuildingCount; ++i)
        AddCatalogItem(g_catalogBuildingTools[i], undefinedType, true);

    RemoveEmptyCatalogTypes();

    for (int i = 1; i < g_catalogItemCount; ++i)
    {
        CatalogItem value = g_catalogItems[i];
        int j = i - 1;
        while (j >= 0 && _wcsicmp(g_catalogItems[j].display, value.display) > 0)
        {
            g_catalogItems[j + 1] = g_catalogItems[j];
            --j;
        }
        g_catalogItems[j + 1] = value;
    }

    LoadCatalogFavorites();

    int vanilla = 0, workshop = 0, tesmio = 0;
    for (int i = 0; i < g_catalogItemCount; ++i)
    {
        if (g_catalogItems[i].source == CATALOG_SOURCE_VANILLA) ++vanilla;
        else if (g_catalogItems[i].source == CATALOG_SOURCE_WORKSHOP) ++workshop;
        else ++tesmio;
    }
    H->log("tesmiomenu  catalog: %d buildables indexed (%d vanilla, %d workshop, %d tesmio)",
           g_catalogItemCount, vanilla, workshop, tesmio);
}

static bool TabAlreadyExists(const RawVector* tabs)
{
    if (!tabs || !tabs->begin || !tabs->end || tabs->end < tabs->begin) return false;
    size_t count = (size_t)(tabs->end - tabs->begin) / TAB_SIZE;
    for (size_t i = 0; i < count; ++i)
    {
        const char* name = (const char*)(tabs->begin + i * TAB_SIZE);
        if (!strncmp(name, "tesmioloader", 63)) return true;
    }
    return false;
}

static unsigned char* FindCustomTab(const RawVector* tabs)
{
    if (!tabs || !tabs->begin || !tabs->end || tabs->end < tabs->begin ||
        ((tabs->end - tabs->begin) % TAB_SIZE) != 0)
        return NULL;
    size_t count = (size_t)(tabs->end - tabs->begin) / TAB_SIZE;
    for (size_t i = 0; i < count; ++i)
    {
        unsigned char* tab = tabs->begin + i * TAB_SIZE;
        const char* name = (const char*)tab;
        if (memchr(name, 0, 64) && strcmp(name, "tesmioloader") == 0)
            return tab;
    }
    return NULL;
}

// A stock build tab is not allowed to contain zero groups.  Tesmio Catalog
// does not use the group's building strip, but the stock renderer still reads
// it before our catalogue overlay is drawn.  Reuse one already-loaded stock
// tool as an inert structural sentinel; it is never classified as Tesmio and
// never activated because selecting this tab is intercepted immediately.
static void* FindStockSentinelTool(const RawVector* tabs, size_t tabCount)
{
    if (!tabs || !tabs->begin) return NULL;
    for (size_t tabIndex = 0; tabIndex < tabCount; ++tabIndex)
    {
        unsigned char* tab = tabs->begin + tabIndex * TAB_SIZE;
        RawVector* groups = (RawVector*)(tab + TAB_GROUPS);
        if (!H->readablePtr(groups, sizeof(*groups)) || !groups->begin ||
            !groups->end || groups->end < groups->begin ||
            ((groups->end - groups->begin) % GROUP_SIZE) != 0)
            continue;
        size_t groupCount = (size_t)(groups->end - groups->begin) / GROUP_SIZE;
        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            RawVector* members = (RawVector*)(groups->begin +
                                               groupIndex * GROUP_SIZE + 8);
            if (!H->readablePtr(members, sizeof(*members)) || !members->begin ||
                !members->end || members->end <= members->begin ||
                ((members->end - members->begin) % sizeof(void*)) != 0)
                continue;
            void* tool = *(void**)members->begin;
            if (tool && H->readablePtr(tool, TOOL_BUILDING + sizeof(void*)))
                return tool;
        }
    }
    return NULL;
}

static void DumpStockTabs(const RawVector* tabs, size_t count)
{
    if (!g_probe) return;

    H->log("tesmiomenu  stock bottom tabs: %llu", (unsigned long long)count);
    for (size_t i = 0; i < count; ++i)
    {
        const unsigned char* tab = tabs->begin + i * TAB_SIZE;
        const char* name = (const char*)tab;
        const int textId = *(const int*)(tab + 0x48);
        const int group = *(const int*)(tab + 0x98);
        const float* color = (const float*)(tab + 0x9C);
        H->log("tesmiomenu    stock[%llu] name=%s text=%d group=%d rgba=%.3f,%.3f,%.3f,%.3f",
               (unsigned long long)i, memchr(name, 0, 64) ? name : "<invalid>",
               textId, group, color[0], color[1], color[2], color[3]);
    }
}

static int PopulateCustomTab(unsigned char* tab, char buildingNames[][96])
{
    void* buildingTypes[MAX_BUILDINGS] = {};
    int buildingCount = LoadBuildingTools(buildingTypes, buildingNames);
    if (!buildingCount)
    {
        H->log("tesmiomenu  no configured building type was found; tab not added");
        return 0;
    }

    t_TabConstruct tabConstruct = o_TabConstruct ? o_TabConstruct :
        (t_TabConstruct)(g_base + P_TAB_CONSTRUCT);
    t_InitObject groupInit = (t_InitObject)(g_base + P_GROUP_INIT);
    t_PushObject groupPush = (t_PushObject)(g_base + P_GROUP_PUSH);
    t_GroupConstruct groupConstruct = (t_GroupConstruct)(g_base + P_GROUP_CONSTRUCT);
    t_PushPointer pointerPush = (t_PushPointer)(g_base + P_POINTER_PUSH);
    t_PushInt intPush = (t_PushInt)(g_base + P_INT_PUSH);

    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    tabConstruct(tab, "tesmioloader", g_textId, g_group, white);

    RawVector* groups = (RawVector*)(tab + TAB_GROUPS);
    alignas(16) unsigned char blankGroup[GROUP_SIZE] = {};
    groupInit(blankGroup);
    groupPush(groups, blankGroup);
    if (!groups->end || groups->end - groups->begin < (ptrdiff_t)GROUP_SIZE)
    {
        H->log("tesmiomenu  building group append failed");
        return 0;
    }

    unsigned char* group = groups->end - GROUP_SIZE;
    groupConstruct(group, buildingTypes[0], g_textId, 0);
    RawVector* members = (RawVector*)(group + 8);
    for (int i = 1; i < buildingCount; ++i)
    {
        void* value = buildingTypes[i];
        pointerPush(members, &value);
    }

    // The group is required structurally, but its stock tool list must stay
    // empty.  Tesmio draws its own catalogue; any member left here is rendered
    // by the vanilla submenu for one frame after a click on newer game builds.
    // Keeping the allocation while setting end == begin preserves valid vector
    // ownership without exposing a placeholder building card to the renderer.
    if (H->readablePtr(members, sizeof(*members)) && members->begin)
        members->end = members->begin;

    int groupTitle = g_textId;
    int groupBoundary = (int)((groups->end - groups->begin) / GROUP_SIZE);
    intPush(tab + 0x68, &groupTitle);
    intPush(tab + 0x80, &groupBoundary);
    return buildingCount;
}

static void h_TabConstruct(void* tab, const char* name, int textId, int group,
                           const float* color)
{
    if (!g_insideMenuInit || !g_front || g_nativeFrontInserted ||
        !name || strcmp(name, "roads") != 0)
    {
        o_TabConstruct(tab, name, textId,
                       (g_insideMenuInit && g_front && g_nativeFrontInserted)
                           ? group + 1 : group,
                       color);
        return;
    }

    char buildingNames[MAX_BUILDINGS][96] = {};
    int buildingCount = PopulateCustomTab((unsigned char*)tab, buildingNames);
    if (!buildingCount)
    {
        o_TabConstruct(tab, name, textId, group, color);
        return;
    }

    // The vanilla initializer has already appended the blank record intended
    // for Roads. Turn that record into our standalone tab, append a new blank
    // record for Roads, and let the rest of the vanilla initializer continue.
    // This is early enough for the game to calculate its coloured section
    // boundaries with TesmioMenu as a real seventh section.
    RawVector* tabs = (RawVector*)(g_base + G_BOTTOM_TABS);
    t_InitObject tabInit = (t_InitObject)(g_base + P_TAB_INIT);
    t_PushObject tabPush = (t_PushObject)(g_base + P_TAB_PUSH);
    alignas(16) unsigned char blankTab[TAB_SIZE] = {};
    tabInit(blankTab);
    tabPush(tabs, blankTab);
    if (!tabs->end || tabs->end - tabs->begin < (ptrdiff_t)TAB_SIZE)
    {
        H->log("tesmiomenu  early Roads record append failed");
        return;
    }

    unsigned char* roads = tabs->end - TAB_SIZE;
    o_TabConstruct(roads, name, textId, group + 1, color);
    g_nativeFrontInserted = true;
    H->log("tesmiomenu  native standalone tab inserted: index 0, group 0, %d building(s)",
           buildingCount);
    if (g_probe)
    {
        for (int i = 0; i < buildingCount; ++i)
            H->log("tesmiomenu    [%d] %s", i + 1, buildingNames[i]);
    }
}

static void AddMenuTab()
{
    RawVector* tabs = (RawVector*)(g_base + G_BOTTOM_TABS);
    if (!H->readablePtr(tabs, sizeof(*tabs)) || !tabs->begin || !tabs->end ||
        tabs->end < tabs->begin || ((tabs->end - tabs->begin) % TAB_SIZE) != 0)
    {
        H->log("tesmiomenu  bottom-tab vector is invalid; tab not added");
        return;
    }

    size_t before = (size_t)(tabs->end - tabs->begin) / TAB_SIZE;
    if (before < 20 || before > 64)
    {
        H->log("tesmiomenu  unexpected bottom-tab count %llu; tab not added",
               (unsigned long long)before);
        return;
    }
    if (TabAlreadyExists(tabs))
    {
        H->log("tesmiomenu  tab already present");
        return;
    }

    DumpStockTabs(tabs, before);

    void* buildingTypes[MAX_BUILDINGS] = {};
    char buildingNames[MAX_BUILDINGS][96] = {};
    int buildingCount = LoadBuildingTools(buildingTypes, buildingNames);
    if (!buildingCount)
    {
        void* sentinel = FindStockSentinelTool(tabs, before);
        if (!sentinel)
        {
            H->log("tesmiomenu  no configured building and no stock sentinel; tab not added");
            return;
        }
        buildingTypes[0] = sentinel;
        strncpy_s(buildingNames[0], sizeof(buildingNames[0]),
                  "<stock structural sentinel>", _TRUNCATE);
        buildingCount = 1;
        H->log("tesmiomenu  empty catalog tab uses a stock structural sentinel");
    }

    t_InitObject tabInit = (t_InitObject)(g_base + P_TAB_INIT);
    t_PushObject tabPush = (t_PushObject)(g_base + P_TAB_PUSH);
    t_TabConstruct tabConstruct = (t_TabConstruct)(g_base + P_TAB_CONSTRUCT);
    t_InitObject groupInit = (t_InitObject)(g_base + P_GROUP_INIT);
    t_PushObject groupPush = (t_PushObject)(g_base + P_GROUP_PUSH);
    t_GroupConstruct groupConstruct = (t_GroupConstruct)(g_base + P_GROUP_CONSTRUCT);
    t_PushPointer pointerPush = (t_PushPointer)(g_base + P_POINTER_PUSH);
    t_PushInt intPush = (t_PushInt)(g_base + P_INT_PUSH);

    alignas(16) unsigned char blankTab[TAB_SIZE] = {};
    tabInit(blankTab);
    tabPush(tabs, blankTab);

    if (!tabs->end || tabs->end - tabs->begin < (ptrdiff_t)TAB_SIZE)
    {
        H->log("tesmiomenu  tab append failed");
        return;
    }

    unsigned char* tab = tabs->end - TAB_SIZE;
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    tabConstruct(tab, "tesmioloader", g_textId, g_group, white);

    RawVector* groups = (RawVector*)(tab + TAB_GROUPS);
    alignas(16) unsigned char blankGroup[GROUP_SIZE] = {};
    groupInit(blankGroup);
    groupPush(groups, blankGroup);

    if (!groups->end || groups->end - groups->begin < (ptrdiff_t)GROUP_SIZE)
    {
        H->log("tesmiomenu  building group append failed");
        return;
    }

    unsigned char* group = groups->end - GROUP_SIZE;
    groupConstruct(group, buildingTypes[0], g_textId, 0);

    RawVector* members = (RawVector*)(group + 8);
    for (int i = 1; i < buildingCount; ++i)
    {
        void* value = buildingTypes[i];
        pointerPush(members, &value);
    }

    if (H->readablePtr(members, sizeof(*members)) && members->begin)
        members->end = members->begin;

    // The vanilla initializer appends one title id and one cumulative group
    // boundary after constructing every group. AddMenuTab runs after that
    // initializer, so these two vectors must be finalized here. Leaving the
    // boundary vector empty crashes the first draw after clicking the tab at
    // SOVIET64.exe + 0x82CE9.
    int groupTitle = g_textId;
    int groupBoundary = (int)((groups->end - groups->begin) / GROUP_SIZE);
    intPush(tab + 0x68, &groupTitle);
    intPush(tab + 0x80, &groupBoundary);

    // Standalone left placement: reserve group 0 for TesmioMenu, shift all
    // six monotonic vanilla groups from 0..5 to 1..6, then move the complete
    // custom record to index 0. The per-tab title and boundary vectors above
    // must already be populated before this remapping; otherwise the first
    // click reaches an empty layout array.
    if (g_front && before > 0)
    {
        bool validGroups = true;
        int previous = -1;
        for (size_t i = 0; i < before; ++i)
        {
            int groupNumber = *(int*)(tabs->begin + i * TAB_SIZE + 0x98);
            if (groupNumber < previous || groupNumber < 0 || groupNumber > 5)
            {
                validGroups = false;
                break;
            }
            previous = groupNumber;
        }

        if (validGroups && g_group == 0)
        {
            for (size_t i = 0; i < before; ++i)
                ++*(int*)(tabs->begin + i * TAB_SIZE + 0x98);

            alignas(16) unsigned char temporary[TAB_SIZE];
            memcpy(temporary, tab, TAB_SIZE);
            memmove(tabs->begin + TAB_SIZE, tabs->begin, before * TAB_SIZE);
            memcpy(tabs->begin, temporary, TAB_SIZE);
            tab = tabs->begin;
        }
        else
        {
            // A safe fallback is to join the final vanilla group on the right.
            *(int*)(tab + 0x98) = previous >= 0 ? previous : 5;
            g_front = 0;
            H->log("tesmiomenu  stock group order differs; using right-side fallback");
        }
    }

    H->log("tesmiomenu  tab added: index %llu, group %d, %d building(s)",
           (unsigned long long)(g_front ? 0 : before), g_group, buildingCount);
    if (g_probe)
    {
        for (int i = 0; i < buildingCount; ++i)
            H->log("tesmiomenu    [%d] %s -> %p", i + 1, buildingNames[i], buildingTypes[i]);
    }
}

#if 0
static HWND FindGameWindow()
{
    struct Search
    {
        DWORD processId;
        HWND result;
    } search = { GetCurrentProcessId(), NULL };

    EnumWindows([](HWND window, LPARAM parameter) -> BOOL
    {
        Search* state = (Search*)parameter;
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId != state->processId || !IsWindowVisible(window) ||
            GetWindow(window, GW_OWNER) != NULL)
            return TRUE;

        wchar_t className[128] = {};
        GetClassNameW(window, className, 127);
        // TesmioLoader also creates a console in diagnostic configurations.
        // Never use that console as the catalog owner.
        if (wcscmp(className, L"ConsoleWindowClass") == 0)
            return TRUE;

        RECT area = {};
        GetClientRect(window, &area);
        if (area.right - area.left < 800 || area.bottom - area.top < 500)
            return TRUE;
        state->result = window;
        return FALSE;
    }, (LPARAM)&search);
    return search.result;
}

static void DrawTextAt(Gdiplus::Graphics& graphics, const wchar_t* text,
                       float x, float y, float size, Gdiplus::Color color,
                       bool bold = false)
{
    Gdiplus::FontFamily family(L"Arial");
    Gdiplus::Font font(&family, size,
        bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
        Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(color);
    graphics.DrawString(text, -1, &font, Gdiplus::PointF(x, y), &brush);
}

static void FillPanel(Gdiplus::Graphics& graphics, float x, float y,
                      float width, float height, Gdiplus::Color fill,
                      Gdiplus::Color border)
{
    Gdiplus::SolidBrush brush(fill);
    Gdiplus::Pen pen(border, 1.0f);
    graphics.FillRectangle(&brush, x, y, width, height);
    graphics.DrawRectangle(&pen, x, y, width, height);
}

static void DrawChip(Gdiplus::Graphics& graphics, const wchar_t* text,
                     float x, float y, float width, bool selected)
{
    const Gdiplus::Color activeFill(230, 171, 64, 55);
    const Gdiplus::Color idleFill(215, 247, 235, 207);
    const Gdiplus::Color activeBorder(255, 151, 45, 37);
    const Gdiplus::Color idleBorder(255, 181, 141, 97);
    FillPanel(graphics, x, y, width, 31.0f,
              selected ? activeFill : idleFill,
              selected ? activeBorder : idleBorder);
    DrawTextAt(graphics, text, x + 9.0f, y + 6.0f, 15.0f,
               selected ? Gdiplus::Color(255, 171, 45, 37)
                        : Gdiplus::Color(255, 101, 91, 77), selected);
}

static void DrawCatalogContents(Gdiplus::Graphics& graphics, int width, int height)
{
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    DrawTextAt(graphics, L"TESMIO CATALOG", 31.0f, 38.0f, 25.0f,
               Gdiplus::Color(255, 255, 245, 225), true);
    DrawTextAt(graphics, L"X", (float)width - 56.0f, 38.0f, 25.0f,
               Gdiplus::Color(255, 245, 220, 196), true);

    const float left = 40.0f;
    const float contentWidth = (float)width - 80.0f;
    FillPanel(graphics, left, 94.0f, contentWidth, 40.0f,
              Gdiplus::Color(225, 255, 246, 218),
              Gdiplus::Color(255, 190, 139, 82));
    DrawTextAt(graphics, L"Search buildings, networks and resources...",
               left + 15.0f, 103.0f, 17.0f,
               Gdiplus::Color(255, 135, 121, 101));

    DrawTextAt(graphics, L"SOURCE", left, 149.0f, 14.0f,
               Gdiplus::Color(255, 174, 48, 40), true);
    DrawChip(graphics, L"BASE GAME", left + 75.0f, 141.0f, 105.0f, false);
    DrawChip(graphics, L"WORKSHOP", left + 190.0f, 141.0f, 108.0f, true);

    DrawTextAt(graphics, L"TYPE", left, 195.0f, 14.0f,
               Gdiplus::Color(255, 174, 48, 40), true);
    float x = left + 50.0f;
    for (int i = 0; i < (int)(sizeof(TYPE_CHIPS) / sizeof(TYPE_CHIPS[0])); ++i)
    {
        DrawChip(graphics, TYPE_CHIPS[i].text, x, 187.0f,
                 (float)TYPE_CHIPS[i].width, i == g_selectedType);
        x += (float)TYPE_CHIPS[i].width + 7.0f;
    }

    DrawTextAt(graphics, L"RESOURCE", left, 241.0f, 14.0f,
               Gdiplus::Color(255, 174, 48, 40), true);
    x = left + 91.0f;
    DrawChip(graphics, L"ALL", x, 233.0f, 55.0f, g_selectedResource == 0);
    x += 62.0f;
    int displayIndex = 1;
    for (int i = 0; i < g_catalogResourceCount && displayIndex <= 6; ++i)
    {
        if (!g_catalogResources[i].modded) continue;
        int chipWidth = 108;
        DrawChip(graphics, g_catalogResources[i].display, x, 233.0f,
                 (float)chipWidth, g_selectedResource == displayIndex);
        x += (float)chipWidth + 7.0f;
        ++displayIndex;
    }
    DrawChip(graphics, L"MORE...", x, 233.0f, 82.0f, false);

    DrawTextAt(graphics, L"ROLE", left, 287.0f, 14.0f,
               Gdiplus::Color(255, 174, 48, 40), true);
    DrawChip(graphics, L"ALL", left + 50.0f, 279.0f, 55.0f, true);
    DrawChip(graphics, L"PRODUCES", left + 112.0f, 279.0f, 102.0f, false);
    DrawChip(graphics, L"CONSUMES", left + 221.0f, 279.0f, 106.0f, false);
    DrawChip(graphics, L"STORES", left + 334.0f, 279.0f, 78.0f, false);

    Gdiplus::Pen separator(Gdiplus::Color(180, 166, 112, 66), 1.0f);
    graphics.DrawLine(&separator, left, 326.0f, (float)width - left, 326.0f);
    DrawTextAt(graphics, L"1 RESULT", left, 340.0f, 14.0f,
               Gdiplus::Color(255, 113, 99, 82), true);

    DrawTextAt(graphics, L"Catalog data is loaded by the in-game renderer.",
               left, 372.0f, 17.0f,
               Gdiplus::Color(255, 105, 93, 76));
}

static LRESULT CALLBACK CatalogWindowProc(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT paint = {};
            HDC target = BeginPaint(window, &paint);
            RECT client = {};
            GetClientRect(window, &client);
            int width = client.right;
            int height = client.bottom;
            HDC memory = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
            HGDIOBJ old = SelectObject(memory, bitmap);
            {
                Gdiplus::Graphics graphics(memory);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.Clear(Gdiplus::Color(255, 232, 218, 188));
                if (g_catalogBackground &&
                    g_catalogBackground->GetLastStatus() == Gdiplus::Ok)
                    graphics.DrawImage(g_catalogBackground, 0, 0, width, height);
                DrawCatalogContents(graphics, width, height);
            }
            BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            RECT client = {};
            GetClientRect(window, &client);
            if (y >= 30 && y <= 78 && x >= client.right - 75)
            {
                ShowWindow(window, SW_HIDE);
                return 0;
            }
            if (y >= 30 && y <= 78)
            {
                ReleaseCapture();
                SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }
            if (y >= 187 && y <= 218)
            {
                int position = 90;
                for (int i = 0; i < (int)(sizeof(TYPE_CHIPS) / sizeof(TYPE_CHIPS[0])); ++i)
                {
                    if (x >= position && x <= position + TYPE_CHIPS[i].width)
                    {
                        g_selectedType = i;
                        InvalidateRect(window, NULL, FALSE);
                        return 0;
                    }
                    position += TYPE_CHIPS[i].width + 7;
                }
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) ShowWindow(window, SW_HIDE);
            return 0;
        case WM_CLOSE:
            ShowWindow(window, SW_HIDE);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static bool EnsureCatalogWindow()
{
    if (g_catalogWindow && IsWindow(g_catalogWindow)) return true;
    if (!g_gameWindow || !IsWindow(g_gameWindow)) g_gameWindow = FindGameWindow();
    if (!g_gameWindow) return false;

    static bool classRegistered = false;
    if (!classRegistered)
    {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = CatalogWindowProc;
        windowClass.hInstance = (HINSTANCE)H->exeModule;
        windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        windowClass.lpszClassName = L"TesmioCatalogWindow";
        classRegistered = RegisterClassExW(&windowClass) != 0 ||
                          GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!classRegistered) return false;
    }

    RECT game = {};
    GetClientRect(g_gameWindow, &game);
    POINT origin = { 0, 0 };
    ClientToScreen(g_gameWindow, &origin);
    int availableWidth = game.right - game.left;
    int availableHeight = game.bottom - game.top;
    int height = availableHeight * 78 / 100;
    if (height > 780) height = 780;
    if (height < 610) height = 610;
    int width = height * 1024 / 850;
    if (width > availableWidth - 40) width = availableWidth - 40;
    int x = origin.x + (availableWidth - width) / 2;
    int y = origin.y + (availableHeight - height) / 2;

    g_catalogWindow = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"TesmioCatalogWindow", L"Tesmio Catalog", WS_POPUP,
        x, y, width, height, g_gameWindow, NULL,
        (HINSTANCE)H->exeModule, NULL);
    return g_catalogWindow != NULL;
}

static void ShowCatalogWindow()
{
    if (!EnsureCatalogWindow())
    {
        H->log("tesmiomenu  catalog window could not be created");
        return;
    }
    ShowWindow(g_catalogWindow, SW_SHOWNORMAL);
    SetWindowPos(g_catalogWindow, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_catalogWindow);
    InvalidateRect(g_catalogWindow, NULL, FALSE);
    H->log("tesmiomenu  catalog window opened");
}

static bool IsCustomTabSelected(RawVector** validTabs = NULL,
                                unsigned char*** validSelectedAddress = NULL)
{
    if (!g_base || !H) return false;
    RawVector* tabs = (RawVector*)(g_base + G_BOTTOM_TABS);
    if (!H->readablePtr(tabs, sizeof(*tabs)) || !tabs->begin || !tabs->end ||
        tabs->end <= tabs->begin || tabs->end - tabs->begin < (ptrdiff_t)(2 * TAB_SIZE))
        return false;

    unsigned char** selectedAddress = (unsigned char**)(g_base + G_SELECTED_BOTTOM_TAB);
    if (!H->readablePtr(selectedAddress, sizeof(*selectedAddress))) return false;
    if (validTabs) *validTabs = tabs;
    if (validSelectedAddress) *validSelectedAddress = selectedAddress;
    return *selectedAddress == tabs->begin;
}

static void OpenCatalogAndSelectRoads()
{
    RawVector* tabs = NULL;
    unsigned char** selectedAddress = NULL;
    if (!IsCustomTabSelected(&tabs, &selectedAddress)) return;
    if (!g_catalogWindow || !IsWindowVisible(g_catalogWindow))
    {
        H->log("tesmiomenu  custom tab selection observed by catalog thread");
        ShowCatalogWindow();
        // Select Roads behind the popup. This removes the old one-line native
        // submenu and also makes the next Tesmio button click detectable.
        *selectedAddress = tabs->begin + TAB_SIZE;
    }
}

static DWORD WINAPI CatalogThreadProc(void*)
{
    H->log("tesmiomenu  catalog worker thread started");
    bool initialized = false;
    bool previousCustom = false;
    for (;;)
    {
        MSG message = {};
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) return 0;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        bool custom = IsCustomTabSelected();
        if (initialized && custom && !previousCustom)
            OpenCatalogAndSelectRoads();
        previousCustom = custom;
        initialized = true;
        Sleep(16);
    }
}
#endif

// ---------------------------------------------------------------- native in-game catalog
//
// This renderer lives in the same C3D_PANEL2D batch as the bottom toolbar.
// There is no HWND, worker thread, GDI surface or focus transfer: the game
// draws the catalog and the game's C3D_INPUT supplies its mouse coordinates.

static const uintptr_t G_PANEL              = 0x9BE060;
static const uintptr_t G_TECHNIQUE          = 0x9EAD08;
static const uintptr_t G_PANEL_POS          = 0x9BE2F0;
static const uintptr_t G_PANEL_PAD          = 0x9BE2F8;
static const uintptr_t G_PANEL_SIZE         = 0x9BE2E8;
static const uintptr_t G_PANEL_COLOR        = 0x9BE30C;
static const uintptr_t G_CLICK_FLAG         = 0xA54E91;
static const uintptr_t G_MIDDLEPOINT        = 0x9EACD0;
static const uintptr_t G_FONT_MANAGER       = 0x996FB0;
static const uintptr_t G_PANEL_FONT         = 0x995220;
static const uintptr_t G_SCREEN_WIDTH       = 0x99528C;
static const uintptr_t G_SCREEN_HEIGHT      = 0x995274;
static const size_t TEX_LOAD2D_FILE         = 0x10;
static const size_t TEX_BIND                = 0x70;

typedef void  (*t_BottomMenuRender)(void*, float);
typedef void  (*t_ConstructionRender)(void*);
typedef void  (*t_PanelDraw)(void*, float, float, float, float, float, bool);
typedef void* (*t_CreateManagedTexture)(void*, const char*);
typedef void  (*t_PrintLeftUnicode)(void*, void*, float, float, unsigned long,
                                    const wchar_t*, ...);

static t_BottomMenuRender o_BottomMenuRender;
static t_ConstructionRender o_ConstructionRender;
static t_PanelDraw o_PanelDraw;
static t_CreateManagedTexture o_CreateManagedTexture;
static t_PrintLeftUnicode o_PrintLeftUnicode;

static void NativeSetRect(float x, float y, float width, float height)
{
    float* position = (float*)(g_base + G_PANEL_POS);
    position[0] = x;
    position[1] = y;
    *(int*)(g_base + G_PANEL_PAD) = 0;
    float* size = (float*)(g_base + G_PANEL_SIZE);
    size[0] = width;
    size[1] = height;
}

static void NativeSetColor(float red, float green, float blue, float alpha)
{
    float* color = (float*)(g_base + G_PANEL_COLOR);
    color[0] = red;
    color[1] = green;
    color[2] = blue;
    color[3] = alpha;
}

static void NativeBindTexture(void* texture)
{
    if (!texture || !H->readablePtr(texture, sizeof(void*))) return;
    void** vtable = *(void***)texture;
    if (!H->readablePtr(vtable, TEX_BIND + sizeof(void*))) return;
    void* technique = *(void**)(g_base + G_TECHNIQUE);
    if (!technique) return;
    typedef void (*t_Bind)(void*, int, void*);
    ((t_Bind)vtable[TEX_BIND / 8])(texture, 0, technique);
}

static void NativeDrawTexture(void* texture, float x, float y,
                              float width, float height,
                              float red = 1.0f, float green = 1.0f,
                              float blue = 1.0f, float alpha = 1.0f)
{
    if (!texture || !o_PanelDraw) return;
    NativeBindTexture(texture);
    // C3D_PANEL2D stores the rectangle position at its centre, while text
    // helpers use ordinary top-left screen coordinates.
    NativeSetRect(x + width * 0.5f, y + height * 0.5f, width, height);
    NativeSetColor(red, green, blue, alpha);
    // Draw the complete source texture.  Older game builds exposed the value
    // 1.0 through a global float, but that global moved in 1.1.1.9.  Reading
    // the old address made every catalogue texture repeat as a small grid.
    o_PanelDraw(g_base + G_PANEL, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, true);
}

static void h_PanelDraw(void* panel, float u0, float v0, float u1, float v1,
                        float rotation, bool enabled)
{
    if (g_captureBottomPanels && g_bottomPanelCount < 512 &&
        panel != g_base + G_PANEL)
    {
        int screenWidth = *(int*)(g_base + G_SCREEN_WIDTH);
        int screenHeight = *(int*)(g_base + G_SCREEN_HEIGHT);
        // Every stock HUD element owns its own C3D_PANEL2D instance. Reading
        // the global catalogue panel here captured our scratch panel instead
        // of the lower toolbar, which is why the button fell back to (18, H).
        // The rectangle fields use the same offsets in each panel instance.
        if (!panel || !H->readablePtr((unsigned char*)panel + 0x288, 16))
        {
            o_PanelDraw(panel, u0, v0, u1, v1, rotation, enabled);
            return;
        }
        float* size = (float*)((unsigned char*)panel + 0x288);
        float* position = (float*)((unsigned char*)panel + 0x290);
        float width = size[0];
        float height = size[1];
        float left = position[0] - width * 0.5f;
        float top = position[1] - height * 0.5f;
        float right = left + width;
        float bottom = top + height;
        if (width >= 12.0f && width <= (float)screenWidth * 1.5f &&
            height >= 12.0f && height <= 160.0f &&
            bottom >= 0.0f && top <= (float)screenHeight &&
            right >= 0.0f && left <= (float)screenWidth)
        {
            g_bottomPanels[g_bottomPanelCount++] = { left, top, right, bottom };
            // The stock construction paper is the widest panel touching the
            // bottom edge. Remember its real geometry after the game has
            // applied resolution and UI scaling.
            if (top >= (float)screenHeight - 180.0f &&
                bottom >= (float)screenHeight - 3.0f &&
                width >= (float)screenWidth * 0.35f &&
                (!g_nativeBottomPaperValid ||
                 width > g_nativeBottomPaper.right - g_nativeBottomPaper.left))
            {
                g_nativeBottomPaper = { left, top, right, bottom };
                g_nativeBottomPaperValid = true;
            }
        }
    }
    o_PanelDraw(panel, u0, v0, u1, v1, rotation, enabled);
}

static bool CapturedBottomBar(float* left, float* top,
                              float* right, float* bottom)
{
    int screenWidth = *(int*)(g_base + G_SCREEN_WIDTH);
    float center = (float)screenWidth * 0.5f;
    float lowestBottom = -100000.0f;
    for (int i = 0; i < g_bottomPanelCount; ++i)
    {
        const CapturedPanelRect& rectangle = g_bottomPanels[i];
        float height = rectangle.bottom - rectangle.top;
        if (height >= 24.0f && height <= 140.0f &&
            rectangle.bottom > lowestBottom)
            lowestBottom = rectangle.bottom;
    }
    if (lowestBottom < 0.0f) return false;
    int seed = -1;
    float seedDistance = 1000000.0f;
    for (int i = 0; i < g_bottomPanelCount; ++i)
    {
        const CapturedPanelRect& rectangle = g_bottomPanels[i];
        float height = rectangle.bottom - rectangle.top;
        if (height < 24.0f || height > 140.0f ||
            rectangle.bottom < lowestBottom - 24.0f)
            continue;
        float distance = 0.0f;
        if (center < rectangle.left) distance = rectangle.left - center;
        else if (center > rectangle.right) distance = center - rectangle.right;
        if (distance < seedDistance)
        {
            seed = i;
            seedDistance = distance;
        }
    }
    if (seed < 0) return false;

    float resultLeft = g_bottomPanels[seed].left;
    float resultTop = g_bottomPanels[seed].top;
    float resultRight = g_bottomPanels[seed].right;
    float resultBottom = g_bottomPanels[seed].bottom;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (int i = 0; i < g_bottomPanelCount; ++i)
        {
            const CapturedPanelRect& rectangle = g_bottomPanels[i];
            float verticalOverlap =
                (resultBottom < rectangle.bottom ? resultBottom : rectangle.bottom) -
                (resultTop > rectangle.top ? resultTop : rectangle.top);
            float horizontalGap = 0.0f;
            if (rectangle.right < resultLeft)
                horizontalGap = resultLeft - rectangle.right;
            else if (rectangle.left > resultRight)
                horizontalGap = rectangle.left - resultRight;
            if (verticalOverlap > 12.0f && horizontalGap <= 14.0f)
            {
                float oldLeft = resultLeft, oldTop = resultTop;
                float oldRight = resultRight, oldBottom = resultBottom;
                if (rectangle.left < resultLeft) resultLeft = rectangle.left;
                if (rectangle.top < resultTop) resultTop = rectangle.top;
                if (rectangle.right > resultRight) resultRight = rectangle.right;
                if (rectangle.bottom > resultBottom) resultBottom = rectangle.bottom;
                changed = changed || oldLeft != resultLeft || oldTop != resultTop ||
                          oldRight != resultRight || oldBottom != resultBottom;
            }
        }
    }
    if (resultRight - resultLeft < 300.0f) return false;
    *left = resultLeft; *top = resultTop;
    *right = resultRight; *bottom = resultBottom;
    return true;
}

static bool CapturedNativeBottomButtonY(float expectedSize, float* y)
{
    if (!y || expectedSize <= 0.0f) return false;
    int screenWidth = *(int*)(g_base + G_SCREEN_WIDTH);
    int screenHeight = *(int*)(g_base + G_SCREEN_HEIGHT);
    float bestScore = 1000000.0f;
    float bestY = 0.0f;
    for (int i = 0; i < g_bottomPanelCount; ++i)
    {
        const CapturedPanelRect& rectangle = g_bottomPanels[i];
        float width = rectangle.right - rectangle.left;
        float height = rectangle.bottom - rectangle.top;
        // Ignore the standalone button itself and all upper HUD controls.
        // The stock construction buttons occupy the centred lower strip and
        // are square panels drawn with the native level-1 slot dimensions.
        if (rectangle.left < (float)screenWidth * 0.12f ||
            rectangle.right > (float)screenWidth * 0.88f ||
            rectangle.top < (float)screenHeight - 180.0f ||
            rectangle.bottom > (float)screenHeight + 2.0f)
            continue;
        if (width < expectedSize * 0.70f || width > expectedSize * 1.30f ||
            height < expectedSize * 0.70f || height > expectedSize * 1.30f)
            continue;
        float squarePenalty = width > height ? width - height : height - width;
        float sizePenalty = width > expectedSize
            ? width - expectedSize : expectedSize - width;
        float score = squarePenalty * 3.0f + sizePenalty;
        if (score < bestScore)
        {
            bestScore = score;
            bestY = rectangle.top;
        }
    }
    if (bestScore >= 1000000.0f) return false;
    *y = bestY;
    return true;
}

static bool CapturedNativeFirstButtonX(float expectedSize, float rowY,
                                       float* x)
{
    if (!x || expectedSize <= 0.0f) return false;
    int screenWidth = *(int*)(g_base + G_SCREEN_WIDTH);
    int screenHeight = *(int*)(g_base + G_SCREEN_HEIGHT);
    float firstLeft = 1000000.0f;
    for (int i = 0; i < g_bottomPanelCount; ++i)
    {
        const CapturedPanelRect& rectangle = g_bottomPanels[i];
        float width = rectangle.right - rectangle.left;
        float height = rectangle.bottom - rectangle.top;
        if (rectangle.left < (float)screenWidth * 0.05f ||
            rectangle.right > (float)screenWidth * 0.88f ||
            rectangle.top < (float)screenHeight - 180.0f ||
            rectangle.bottom > (float)screenHeight + 2.0f)
            continue;
        if (width < expectedSize * 0.70f || width > expectedSize * 1.30f ||
            height < expectedSize * 0.70f || height > expectedSize * 1.30f)
            continue;
        float rowDistance = rectangle.top > rowY
            ? rectangle.top - rowY : rowY - rectangle.top;
        if (rowDistance > expectedSize * 0.20f) continue;
        if (rectangle.left < firstLeft) firstLeft = rectangle.left;
    }
    if (firstLeft >= 1000000.0f) return false;
    // The paper was extended by exactly one native slot, so the catalogue
    // occupies the slot immediately preceding the first stock Roads button.
    *x = firstLeft - expectedSize;
    return true;
}

static void NativePrint(const wchar_t* text, float x, float y,
                        unsigned long color = 0xFF6A5E4Du)
{
    if (!o_PrintLeftUnicode || !text) return;
    void* font = *(void**)(g_base + G_PANEL_FONT);
    if (!font) return;
    o_PrintLeftUnicode(g_base + G_FONT_MANAGER, font, x, y, color, L"%ls", text);
}

static float ApproximateTextWidth(const wchar_t* text)
{
    float width = 0.0f;
    if (!text) return width;
    for (const wchar_t* character = text; *character; ++character)
    {
        if (*character == L' ' || *character == L'.' || *character == L',' ||
            *character == L':' || *character == L'(' || *character == L')')
            width += 4.6f;
        else if (*character == L'I' || *character == L'i' ||
                 *character == L'l' || *character == L'1')
            width += 4.8f;
        else if (*character == L'W' || *character == L'M' ||
                 *character == L'w' || *character == L'm')
            width += 10.0f;
        else
            width += 7.8f;
    }
    // C3D's catalogue font is wider than the old hand-tuned estimate,
    // especially at UI scales above 100%. Keep a conservative margin so text
    // is always ellipsized before it reaches the card edge.
    return width * 1.34f;
}

static void FitWideToWidth(const wchar_t* source, wchar_t* destination,
                           size_t capacity, float maximumWidth)
{
    if (!destination || !capacity) return;
    destination[0] = 0;
    if (!source || maximumWidth <= 0.0f) return;
    wcsncpy_s(destination, capacity, source, _TRUNCATE);
    if (ApproximateTextWidth(destination) <= maximumWidth) return;
    const wchar_t* ellipsis = L"...";
    float ellipsisWidth = ApproximateTextWidth(ellipsis);
    size_t length = wcslen(destination);
    while (length && ApproximateTextWidth(destination) + ellipsisWidth > maximumWidth)
        destination[--length] = 0;
    while (length && destination[length - 1] == L' ')
        destination[--length] = 0;
    if (length + 3 < capacity) wcscat_s(destination, capacity, ellipsis);
}

static void NativePrintFitted(const wchar_t* text, float x, float y,
                              float maximumWidth,
                              unsigned long color = 0xFF6A5E4Du)
{
    wchar_t fitted[192] = {};
    FitWideToWidth(text, fitted, sizeof(fitted) / sizeof(wchar_t), maximumWidth);
    NativePrint(fitted, x, y, color);
}

static void WrapWideToTwoLines(const wchar_t* source, float maximumWidth,
                               wchar_t* first, size_t firstCapacity,
                               wchar_t* second, size_t secondCapacity)
{
    if (!first || !firstCapacity || !second || !secondCapacity) return;
    first[0] = 0;
    second[0] = 0;
    if (!source) return;
    if (ApproximateTextWidth(source) <= maximumWidth)
    {
        wcsncpy_s(first, firstCapacity, source, _TRUNCATE);
        return;
    }

    size_t sourceLength = wcslen(source);
    size_t split = 0;
    size_t lastBreak = 0;
    for (size_t i = 0; i < sourceLength && i + 1 < firstCapacity; ++i)
    {
        wchar_t candidate[256] = {};
        size_t length = i + 1;
        if (length >= sizeof(candidate) / sizeof(wchar_t)) break;
        wcsncpy_s(candidate, sizeof(candidate) / sizeof(wchar_t), source, length);
        candidate[length] = 0;
        if (ApproximateTextWidth(candidate) > maximumWidth) break;
        split = length;
        if (source[i] == L' ' || source[i] == L',') lastBreak = length;
    }
    if (lastBreak > 0) split = lastBreak;
    if (!split) split = 1;
    wcsncpy_s(first, firstCapacity, source, split);
    first[split < firstCapacity ? split : firstCapacity - 1] = 0;
    while (split < sourceLength && source[split] == L' ') ++split;
    FitWideToWidth(source + split, second, secondCapacity, maximumWidth);
}

static void NormalizeWideToSingleParagraph(const wchar_t* source,
                                           wchar_t* destination,
                                           size_t capacity)
{
    if (!destination || !capacity) return;
    destination[0] = 0;
    if (!source) return;
    size_t written = 0;
    bool pendingSpace = false;
    for (size_t i = 0; source[i] && written + 1 < capacity; ++i)
    {
        wchar_t value = source[i];
        if (value == L'\r' || value == L'\n' || value == L'\t' ||
            value == L' ')
        {
            pendingSpace = written != 0;
            continue;
        }
        if (pendingSpace && written + 1 < capacity)
            destination[written++] = L' ';
        pendingSpace = false;
        destination[written++] = value;
    }
    destination[written] = 0;
}

static void* LoadNativeTexture(const char* path)
{
    if (!o_CreateManagedTexture) return NULL;
    void* texture = o_CreateManagedTexture(g_base + G_MIDDLEPOINT, path);
    if (!texture || !H->readablePtr(texture, sizeof(void*))) return NULL;
    void** vtable = *(void***)texture;
    if (!H->readablePtr(vtable, TEX_LOAD2D_FILE + sizeof(void*))) return NULL;
    typedef void (*t_Load)(void*, const char*, int, int, int, int);
    ((t_Load)vtable[TEX_LOAD2D_FILE / 8])(texture, path, 0, 0, 0, 0);
    return texture;
}

static bool IsNativeTexture(void* texture)
{
    if (!texture || !H->readablePtr(texture, sizeof(void*))) return false;
    void** vtable = *(void***)texture;
    return H->readablePtr(vtable, TEX_BIND + sizeof(void*)) &&
           vtable[TEX_BIND / sizeof(void*)] != NULL;
}

static void* CatalogItemPreview(CatalogItem& item)
{
    if (item.previewAttempted) return item.previewTexture;
    item.previewAttempted = true;
    // Never retain the tool's live texture pointer. Building tools can be
    // reconstructed while loading a save, leaving the captured pointer
    // readable but owned by a different object. The copied virtual asset path
    // is stable across that reconstruction.
    // Prefer the path we verified on disk. The engine still returns a texture
    // wrapper for some missing virtual paths, so merely validating its vtable
    // would otherwise suppress a perfectly good vanilla fallback image.
    if (item.fallbackPreviewPath[0])
    {
        void* loaded = LoadNativeTexture(item.fallbackPreviewPath);
        if (IsNativeTexture(loaded)) item.previewTexture = loaded;
    }
    if (!item.previewTexture && item.previewPath[0])
    {
        void* loaded = LoadNativeTexture(item.previewPath);
        if (IsNativeTexture(loaded)) item.previewTexture = loaded;
    }
    return item.previewTexture;
}

static bool LoadCatalogTextures()
{
    if (!g_catalogTexture)
        g_catalogTexture = LoadNativeTexture("editor/bottommenu_area_white.png");
    if (!g_catalogSolidTexture)
        g_catalogSolidTexture = LoadNativeTexture("editor/white.png");
    if (!g_toolbarTexture)
        g_toolbarTexture = LoadNativeTexture("editor/bottomtab_tesmioloader.png");
    if (!g_favoriteTexture)
        g_favoriteTexture = LoadNativeTexture("editor/favorite.png");
    if (!g_catalogTexture || !g_catalogSolidTexture || !g_toolbarTexture)
        return false;
    return true;
}

static void* CenteredLockTexture(int reason)
{
    if (reason < 1 || reason > 16) return NULL;
    static const char* centeredLockPaths[16] = {
        "editor/tesmio_catalog_locks/locked_pollution.png",
        "editor/tesmio_catalog_locks/locked_education.png",
        "editor/tesmio_catalog_locks/locked_crime.png",
        "editor/tesmio_catalog_locks/locked_power.png",
        "editor/tesmio_catalog_locks/locked_powerfuel.png",
        "editor/tesmio_catalog_locks/locked_fires.png",
        "editor/tesmio_catalog_locks/locked_heating.png",
        "editor/tesmio_catalog_locks/locked_seasons.png",
        "editor/tesmio_catalog_locks/locked_waste.png",
        "editor/tesmio_catalog_locks/locked_water.png",
        "editor/tesmio_catalog_locks/locked_traffic.png",
        "editor/tesmio_catalog_locks/locked_maintenance.png",
        "editor/tesmio_catalog_locks/locked_dlc.png",
        "editor/tesmio_catalog_locks/locked_demolition.png",
        "editor/tesmio_catalog_locks/locked_dlc.png",
        "editor/tesmio_catalog_locks/locked_terrain.png"
    };
    int index = reason - 1;
    if (!g_centeredLockTextures[index])
        g_centeredLockTextures[index] = LoadNativeTexture(centeredLockPaths[index]);
    return g_centeredLockTextures[index];
}

static bool PointInside(float mouseX, float mouseY, float x, float y,
                        float width, float height)
{
    return mouseX >= x && mouseX <= x + width &&
           mouseY >= y && mouseY <= y + height;
}

static bool ReadGameMouse(int logicalWidth, int logicalHeight,
                          float* mouseX, float* mouseY)
{
    HWND window = GetForegroundWindow();
    if (!window) return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId()) return false;
    RECT client = {};
    if (GetWindow(window, GW_OWNER) != NULL || !GetClientRect(window, &client) ||
        client.right - client.left < 800 || client.bottom - client.top < 500)
        return false;
    POINT point = {};
    if (!GetCursorPos(&point) || !ScreenToClient(window, &point) ||
        client.right <= 0 || client.bottom <= 0)
        return false;
    *mouseX = (float)point.x * (float)logicalWidth / (float)client.right;
    *mouseY = (float)point.y * (float)logicalHeight / (float)client.bottom;
    return true;
}

static bool CatalogCursorInsideClient(HWND window)
{
    if (!g_catalogVisible || !window || !IsWindow(window)) return false;
    RECT client = {};
    POINT cursor = {};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0 ||
        !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor))
        return false;

    int screenWidth = *(int*)(g_base + G_SCREEN_WIDTH);
    int screenHeight = *(int*)(g_base + G_SCREEN_HEIGHT);
    if (screenWidth < 800 || screenHeight < 600) return false;
    float mouseX = (float)cursor.x * (float)screenWidth / (float)client.right;
    float mouseY = (float)cursor.y * (float)screenHeight / (float)client.bottom;

    float width = (float)screenWidth * 0.59f;
    float height = (float)screenHeight * 0.81f;
    if (width > 1116.0f) width = 1116.0f;
    if (height > 888.0f) height = 888.0f;
    if (width > (float)screenWidth - 40.0f) width = (float)screenWidth - 40.0f;
    if (height > (float)screenHeight - 40.0f) height = (float)screenHeight - 40.0f;
    float x = g_catalogX >= 0.0f ? g_catalogX : ((float)screenWidth - width) * 0.5f;
    float y = g_catalogY >= 0.0f ? g_catalogY : ((float)screenHeight - height) * 0.5f;
    return PointInside(mouseX, mouseY, x, y, width, height);
}

static bool StandaloneButtonHovered(HWND window)
{
    if (!window || !IsWindow(window) || g_standaloneButtonSize <= 0.0f)
        return false;
    RECT client = {};
    POINT cursor = {};
    if (!GetClientRect(window, &client) || client.right <= 0 ||
        client.bottom <= 0 || !GetCursorPos(&cursor) ||
        !ScreenToClient(window, &cursor))
        return false;
    int screenWidth = *(int*)(g_base + G_SCREEN_WIDTH);
    int screenHeight = *(int*)(g_base + G_SCREEN_HEIGHT);
    float mouseX = (float)cursor.x * (float)screenWidth / (float)client.right;
    float mouseY = (float)cursor.y * (float)screenHeight / (float)client.bottom;
    return PointInside(mouseX, mouseY, g_standaloneButtonX,
                       g_standaloneButtonY, g_standaloneButtonSize,
                       g_standaloneButtonSize);
}

static bool ShouldShieldEngineMouse()
{
    HWND window = g_inputShieldWindow;
    if (!window || !IsWindow(window))
    {
        window = GetForegroundWindow();
        DWORD processId = 0;
        if (!window || !GetWindowThreadProcessId(window, &processId) ||
            processId != GetCurrentProcessId())
            return false;
    }
    return CatalogCursorInsideClient(window) || StandaloneButtonHovered(window);
}

static void h_InputRefreshData(void* self, HWND window, void* timer)
{
    o_InputRefreshData(self, window, timer);

    // RefreshData is the single point where C3D_INPUT publishes the mouse
    // state for the new frame. The scene code reads these bytes directly (it
    // does not consistently call the exported accessors), so suppress them
    // here before any world picking can observe the click. TesmioMenu reads
    // GetAsyncKeyState independently and therefore remains interactive.
    if (self && ShouldShieldEngineMouse())
        ZeroMemory((unsigned char*)self + 0x300, 0x10);
}

static bool InstallEngineMouseShield()
{
    HMODULE engine = GetModuleHandleA(ENGINE_DLL);
    if (!engine)
    {
        H->log("tesmiomenu  engine input shield: C3DDLL64.dll unavailable");
        return false;
    }

    void* refresh = (void*)GetProcAddress(
        engine, "?RefreshData@C3D_INPUT@@QEAAXPEAUHWND__@@PEAVC3D_TIMER@@@Z");
    if (!refresh)
    {
        H->log("tesmiomenu  engine input shield: input export missing");
        return false;
    }

    static const unsigned char refreshData[] = {
        0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,
        0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56
    };
    if (!H->installInlineHook(refresh, (void*)h_InputRefreshData,
                              (void**)&o_InputRefreshData,
                              refreshData, sizeof(refreshData),
                              "tesmiomenu direct engine input shield"))
    {
        H->log("tesmiomenu  engine input shield: RefreshData hook failed");
        return false;
    }
    H->log("tesmiomenu  direct C3D_INPUT frame shield installed");
    return true;
}

static LRESULT CALLBACK GameWindowInputShield(HWND window, UINT message,
                                               WPARAM wParam, LPARAM lParam)
{
    bool standalone = StandaloneButtonHovered(window);
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) &&
        standalone)
    {
        g_standaloneButtonCapture = true;
        InterlockedExchange(&g_standaloneToggleRequested, 1);
        return 0;
    }
    if (message == WM_LBUTTONUP && g_standaloneButtonCapture)
    {
        g_standaloneButtonCapture = false;
        return 0;
    }

    bool inside = CatalogCursorInsideClient(window);
    switch (message)
    {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (inside) { g_shieldLeftButton = true; return 0; }
            break;
        case WM_LBUTTONUP:
            if (g_shieldLeftButton || inside)
            {
                g_shieldLeftButton = false;
                return 0;
            }
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
            if (inside) { g_shieldRightButton = true; return 0; }
            break;
        case WM_RBUTTONUP:
            if (g_shieldRightButton || inside)
            {
                g_shieldRightButton = false;
                return 0;
            }
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
            if (inside) { g_shieldMiddleButton = true; return 0; }
            break;
        case WM_MBUTTONUP:
            if (g_shieldMiddleButton || inside)
            {
                g_shieldMiddleButton = false;
                return 0;
            }
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_MOUSEMOVE:
        case WM_INPUT:
            if (inside) return 0;
            break;
    }
    return g_originalGameWindowProc
        ? CallWindowProcW(g_originalGameWindowProc, window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

static bool EnsureGameInputShield()
{
    HWND window = GetForegroundWindow();
    if (!window) return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId()) return false;
    RECT client = {};
    if (GetWindow(window, GW_OWNER) != NULL || !GetClientRect(window, &client) ||
        client.right - client.left < 800 || client.bottom - client.top < 500)
        return false;
    if (g_inputShieldWindow == window && IsWindow(window) &&
        (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC) == GameWindowInputShield)
        return true;

    SetLastError(0);
    LONG_PTR previous = SetWindowLongPtrW(window, GWLP_WNDPROC,
                                          (LONG_PTR)GameWindowInputShield);
    if (!previous && GetLastError() != 0)
    {
        H->log("tesmiomenu  input shield could not subclass the game window");
        return false;
    }
    g_inputShieldWindow = window;
    g_originalGameWindowProc = (WNDPROC)previous;
    H->log("tesmiomenu  catalog input shield installed");
    return true;
}

static void DrawNativeChip(float mouseX, float mouseY, bool pressed,
                           const wchar_t* text, float x, float y, float width,
                           int* selection, int selectValue)
{
    void* button = g_catalogSolidTexture;
    bool selected = *selection == selectValue;
    bool hovered = PointInside(mouseX, mouseY, x, y, width, 30.0f);
    if (button)
    {
        if (selected)
            NativeDrawTexture(button, x, y, width, 30.0f, 0.72f, 0.24f, 0.20f, 1.0f);
        else if (hovered)
            NativeDrawTexture(button, x, y, width, 30.0f, 1.0f, 0.91f, 0.70f, 1.0f);
        else
            NativeDrawTexture(button, x, y, width, 30.0f, 0.96f, 0.88f, 0.70f, 1.0f);
    }
    unsigned long textColor = selected ? 0xFFFFE8D4u : 0xFF6A5E4Du;
    if (text && text[0] && !text[1] && (text[0] == L'<' || text[0] == L'>'))
    {
        float glyphWidth = ApproximateTextWidth(text);
        NativePrint(text, x + (width - glyphWidth) * 0.5f, y + 7.0f, textColor);
    }
    else
    {
        NativePrintFitted(text, x + 8.0f, y + 7.0f, width - 16.0f,
                          textColor);
    }
    if (hovered && pressed && *selection != selectValue)
    {
        *selection = selectValue;
        H->log("tesmiomenu  native catalog filter selected: %d", selectValue);
    }
}

static const wchar_t* CurrentSelectorLabel(int selector, wchar_t* fallback,
                                           size_t capacity)
{
    if (selector == 1)
    {
        if (g_selectedType <= 0 || g_selectedType > g_catalogTypeCount)
            return Ui(UI_ALL);
        return g_catalogTypes[g_selectedType - 1].display;
    }
    if (selector == 2)
    {
        if (g_selectedResource <= 0 || g_selectedResource > g_catalogResourceCount)
            return Ui(UI_ALL);
        return g_catalogResources[g_selectedResource - 1].display;
    }
    int selected = (g_includeVanilla ? 1 : 0) +
                   (g_includeWorkshop ? 1 : 0) +
                   (g_includeTesmioLoader ? 1 : 0);
    if (selected == 3) return Ui(UI_ALL);
    if (selected == 0) return Ui(UI_NONE);
    if (selected == 2) return Ui(UI_TWO_SELECTED);
    if (g_includeVanilla) return Ui(UI_VANILLA_SHORT);
    if (g_includeWorkshop) return Ui(UI_WORKSHOP_SHORT);
    (void)fallback;
    (void)capacity;
    return Ui(UI_TESMIO);
}

static void DrawNativeSelector(float mouseX, float mouseY, bool pressed,
                               int selector, const wchar_t* title,
                               float x, float y, float width)
{
    bool opened = g_openDropdown == selector;
    bool hovered = PointInside(mouseX, mouseY, x, y, width, 42.0f);
    NativeDrawTexture(g_catalogSolidTexture, x, y, width, 42.0f,
                      opened ? 0.72f : (hovered ? 1.0f : 0.96f),
                      opened ? 0.24f : 0.88f,
                      opened ? 0.20f : 0.70f, 0.94f);
    float labelWidth = 72.0f;
    if (selector == 2) labelWidth = 112.0f;
    if (selector == 3) labelWidth =
        g_catalogLanguage == CATALOG_LANGUAGE_RUSSIAN ? 138.0f : 92.0f;
    NativePrintFitted(title, x + 12.0f, y + 12.0f, labelWidth,
                      opened ? 0xFFFFE8D4u : 0xFFAD3028u);
    wchar_t fallback[64] = {};
    // Keep a visible gutter after every fixed label. The game's font is wider
    // than its nominal logical metrics at some UI scales, so these offsets are
    // intentionally conservative.
    float valueX = x + 12.0f + labelWidth + 10.0f;
    float valueWidth = x + width - 38.0f - valueX;
    NativePrintFitted(CurrentSelectorLabel(selector, fallback, 64),
                      valueX, y + 12.0f, valueWidth,
                      opened ? 0xFFFFE8D4u : 0xFF6A5E4Du);
    NativePrint(opened ? L"^" : L"v", x + width - 27.0f, y + 12.0f,
                opened ? 0xFFFFE8D4u : 0xFF6A5E4Du);
    if (hovered && pressed)
    {
        if (g_openDropdown == selector) g_openDropdown = 0;
        else
        {
            g_openDropdown = selector;
            g_dropdownPage = 0;
        }
    }
}

static void DrawNativeSourceCheckbox(float mouseX, float mouseY, bool pressed,
                                     const wchar_t* label, const char* logName,
                                     float x, float y, float width, bool* value)
{
    const float rowHeight = 28.0f;
    const float boxSize = 22.0f;
    bool hovered = PointInside(mouseX, mouseY, x, y, width, rowHeight);
    NativeDrawTexture(g_catalogSolidTexture, x, y, width, rowHeight,
                      hovered ? 1.0f : 0.96f,
                      hovered ? 0.93f : 0.88f,
                      hovered ? 0.76f : 0.70f, 1.0f);
    NativeDrawTexture(g_catalogSolidTexture, x + 3.0f, y + 3.0f,
                      boxSize, boxSize,
                      *value ? 0.35f : 0.96f,
                      *value ? 0.58f : 0.91f,
                      *value ? 0.27f : 0.80f, 0.98f);
    NativePrintFitted(label, x + 34.0f, y + 6.0f, width - 42.0f,
                      0xFF2B2925u);
    if (hovered && pressed)
    {
        *value = !*value;
        g_resultPage = 0;
        H->log("tesmiomenu  source filter %s: %s", logName,
               *value ? "included" : "excluded");
    }
}

static void DrawResourceRelationCheckbox(float mouseX, float mouseY,
                                         bool pressed, const wchar_t* label,
                                         const char* logName, float x, float y,
                                         float width, bool* value)
{
    const float rowHeight = 30.0f;
    const float boxSize = 22.0f;
    bool hovered = PointInside(mouseX, mouseY, x, y, width, rowHeight);
    NativeDrawTexture(g_catalogSolidTexture, x, y, width, rowHeight,
                      hovered ? 1.0f : 0.96f,
                      hovered ? 0.93f : 0.88f,
                      hovered ? 0.76f : 0.70f, 1.0f);
    NativeDrawTexture(g_catalogSolidTexture, x + 4.0f, y + 4.0f,
                      boxSize, boxSize,
                      *value ? 0.35f : 0.96f,
                      *value ? 0.58f : 0.91f,
                      *value ? 0.27f : 0.80f, 0.98f);
    NativePrintFitted(label, x + 35.0f, y + 7.0f, width - 42.0f,
                      0xFF2B2925u);
    if (hovered && pressed)
    {
        *value = !*value;
        H->log("tesmiomenu  resource relation filter %s: %s", logName,
               *value ? "enabled" : "disabled");
    }
}

static void DrawOnlyAvailableCheckbox(float mouseX, float mouseY, bool pressed,
                                      float x, float y, float width)
{
    const float height = 30.0f;
    const float boxSize = 22.0f;
    bool hovered = PointInside(mouseX, mouseY, x, y, width, height);
    NativeDrawTexture(g_catalogSolidTexture, x, y, width, height,
                      hovered ? 1.0f : 0.96f,
                      hovered ? 0.93f : 0.88f,
                      hovered ? 0.76f : 0.70f, 0.98f);
    NativeDrawTexture(g_catalogSolidTexture, x + 4.0f, y + 4.0f,
                      boxSize, boxSize,
                      g_onlyAvailable ? 0.35f : 0.96f,
                      g_onlyAvailable ? 0.58f : 0.91f,
                      g_onlyAvailable ? 0.27f : 0.80f, 0.98f);
    NativePrintFitted(Ui(UI_ONLY_AVAILABLE), x + 36.0f, y + 7.0f,
                      width - 44.0f, 0xFF2B2925u);
    if (hovered && pressed)
    {
        g_onlyAvailable = !g_onlyAvailable;
        ++g_availabilityCacheEpoch;
        if (!g_availabilityCacheEpoch) g_availabilityCacheEpoch = 1;
        H->log("tesmiomenu  only available filter: %s",
               g_onlyAvailable ? "enabled" : "disabled");
    }
}

static void DrawFavoritesModeButton(float mouseX, float mouseY, bool pressed,
                                    float x, float y, float width)
{
    const float height = 34.0f;
    bool hovered = PointInside(mouseX, mouseY, x, y, width, height);
    NativeDrawTexture(g_catalogSolidTexture, x, y, width, height,
                      g_onlyFavorites ? 0.96f : (hovered ? 1.0f : 0.96f),
                      g_onlyFavorites ? 0.67f : (hovered ? 0.93f : 0.88f),
                      g_onlyFavorites ? 0.16f : (hovered ? 0.76f : 0.70f),
                      0.99f);
    if (g_favoriteTexture)
        NativeDrawTexture(g_favoriteTexture, x + 4.0f, y + 3.0f,
                          28.0f, 28.0f,
                          1.0f, 1.0f, 1.0f,
                          g_onlyFavorites ? 1.0f : 0.78f);
    NativePrintFitted(Ui(UI_FAVORITES), x + 39.0f, y + 9.0f,
                      width - 47.0f, 0xFF2B2925u);
    if (hovered && pressed)
    {
        RememberCurrentResultPage();
        g_onlyFavorites = !g_onlyFavorites;
        g_resultPage = g_onlyFavorites
            ? g_favoritesResultPage
            : g_regularResultPage;
        H->log("tesmiomenu  favorites view: %s",
               g_onlyFavorites ? "enabled" : "disabled");
    }
}

static void DrawNativeSourceDropdown(float mouseX, float mouseY, bool pressed,
                                     float x, float y, float width)
{
    if (g_openDropdown != 3) return;
    const float height = 112.0f;
    NativeDrawTexture(g_catalogSolidTexture, x, y, width, height,
                      0.94f, 0.90f, 0.82f, 1.0f);
    DrawNativeSourceCheckbox(mouseX, mouseY, pressed,
                             Ui(UI_VANILLA_BUILDINGS), "vanilla",
                             x + 8.0f, y + 8.0f, width - 16.0f,
                             &g_includeVanilla);
    DrawNativeSourceCheckbox(mouseX, mouseY, pressed,
                             Ui(UI_WORKSHOP_BUILDINGS), "workshop",
                             x + 8.0f, y + 42.0f, width - 16.0f,
                             &g_includeWorkshop);
    DrawNativeSourceCheckbox(mouseX, mouseY, pressed,
                             Ui(UI_TESMIO), "tesmio",
                             x + 8.0f, y + 76.0f, width - 16.0f,
                             &g_includeTesmioLoader);
}

static int DropdownTotal()
{
    if (g_openDropdown == 1) return g_catalogTypeCount + 1;
    if (g_openDropdown == 2) return g_catalogResourceCount + 1;
    return 0;
}

static const wchar_t* DropdownLabel(int value, wchar_t* fallback, size_t capacity)
{
    (void)fallback;
    (void)capacity;
    if (value == 0) return Ui(UI_ALL);
    if (g_openDropdown == 1 && value <= g_catalogTypeCount)
        return g_catalogTypes[value - 1].display;
    if (g_openDropdown == 2 && value <= g_catalogResourceCount)
        return g_catalogResources[value - 1].display;
    return L"";
}

static int* DropdownSelection()
{
    if (g_openDropdown == 1) return &g_selectedType;
    if (g_openDropdown == 2) return &g_selectedResource;
    return &g_selectedType;
}

static void DrawNativeDropdown(float mouseX, float mouseY, bool pressed,
                               float x, float y, float width)
{
    if (!g_openDropdown) return;
    const int columns = 3;
    const int rows = 7;
    const int pageSize = columns * rows;
    int total = DropdownTotal();
    int pageCount = total > 0 ? (total + pageSize - 1) / pageSize : 1;
    if (g_dropdownPage >= pageCount) g_dropdownPage = pageCount - 1;
    if (g_dropdownPage < 0) g_dropdownPage = 0;
    float panelHeight = 304.0f;
    NativeDrawTexture(g_catalogSolidTexture, x, y, width, panelHeight,
                      0.94f, 0.90f, 0.82f, 1.0f);
    float cellWidth = (width - 32.0f) / (float)columns;
    int first = g_dropdownPage * pageSize;
    int last = first + pageSize;
    if (last > total) last = total;
    int* selection = DropdownSelection();
    for (int value = first; value < last; ++value)
    {
        int local = value - first;
        // Read like a directory: top-to-bottom in the first column, then the
        // second and third columns, before advancing to the next page.
        int column = local / rows;
        int row = local % rows;
        float itemX = x + 10.0f + column * cellWidth;
        float itemY = y + 10.0f + row * 36.0f;
        wchar_t fallback[64] = {};
        const wchar_t* label = DropdownLabel(value, fallback, 64);
        DrawNativeChip(mouseX, mouseY, pressed, label, itemX, itemY,
                       cellWidth - 7.0f, selection, value);
        if (pressed && PointInside(mouseX, mouseY, itemX, itemY,
                                   cellWidth - 7.0f, 30.0f))
        {
            g_openDropdown = 0;
            g_resultPage = 0;
            return;
        }
    }
    if (pageCount > 1)
    {
        float arrowY = y + panelHeight - 38.0f;
        DrawNativeChip(mouseX, mouseY, false, L"<", x + width * 0.5f - 75.0f,
                       arrowY, 46.0f, &g_dropdownPage, -1000);
        DrawNativeChip(mouseX, mouseY, false, L">", x + width * 0.5f + 29.0f,
                       arrowY, 46.0f, &g_dropdownPage, -1000);
        wchar_t pageText[32];
        swprintf_s(pageText, 32, L"%d / %d", g_dropdownPage + 1, pageCount);
        NativePrint(pageText, x + width * 0.5f - 22.0f, arrowY + 7.0f);
        if (pressed && PointInside(mouseX, mouseY, x + width * 0.5f - 75.0f,
                                   arrowY, 46.0f, 30.0f) && g_dropdownPage > 0)
            --g_dropdownPage;
        if (pressed && PointInside(mouseX, mouseY, x + width * 0.5f + 29.0f,
                                   arrowY, 46.0f, 30.0f) &&
            g_dropdownPage + 1 < pageCount)
            ++g_dropdownPage;
    }
    if (g_openDropdown == 2)
    {
        float filterY = y + panelHeight - 38.0f;
        float filterWidth = width * 0.5f - 95.0f;
        if (filterWidth > 220.0f) filterWidth = 220.0f;
        if (filterWidth < 80.0f) filterWidth = 80.0f;
        DrawResourceRelationCheckbox(
            mouseX, mouseY, pressed, Ui(UI_FILTER_CONSUMES), "consumes",
            x + 10.0f, filterY, filterWidth, &g_filterResourceConsumes);
        DrawResourceRelationCheckbox(
            mouseX, mouseY, pressed, Ui(UI_FILTER_PRODUCES), "produces",
            x + width - 10.0f - filterWidth, filterY, filterWidth,
            &g_filterResourceProduces);
    }
}

static bool MetadataHasResource(const CatalogItemMetadata& metadata,
                                 const char* resource)
{
    for (int i = 0; i < metadata.produceCount; ++i)
        if (_stricmp(metadata.produces[i], resource) == 0) return true;
    for (int i = 0; i < metadata.consumeCount; ++i)
        if (_stricmp(metadata.consumes[i], resource) == 0) return true;
    for (int i = 0; i < metadata.storeCount; ++i)
        if (_stricmp(metadata.stores[i], resource) == 0) return true;
    return false;
}

static bool MetadataRelationHasResource(char values[][64], int count,
                                        const char* resource)
{
    for (int i = 0; i < count; ++i)
        if (_stricmp(values[i], resource) == 0) return true;
    return false;
}

static bool CatalogItemAvailableCached(CatalogItem& item);

static bool CatalogItemPassesFilters(CatalogItem& item)
{
    if (g_selectedType > 0 && item.typeIndex != g_selectedType - 1) return false;
    if (g_selectedResource > 0)
    {
        if (g_selectedResource > g_catalogResourceCount) return false;
        const char* resource =
            g_catalogResources[g_selectedResource - 1].name;
        if (g_filterResourceConsumes || g_filterResourceProduces)
        {
            bool relationMatches =
                (g_filterResourceConsumes &&
                 MetadataRelationHasResource(item.metadata.consumes,
                                             item.metadata.consumeCount,
                                             resource)) ||
                (g_filterResourceProduces &&
                 MetadataRelationHasResource(item.metadata.produces,
                                             item.metadata.produceCount,
                                             resource));
            if (!relationMatches) return false;
        }
        else if (!MetadataHasResource(item.metadata, resource))
            return false;
    }
    if (item.source == CATALOG_SOURCE_VANILLA && !g_includeVanilla) return false;
    if (item.source == CATALOG_SOURCE_WORKSHOP && !g_includeWorkshop) return false;
    if (item.source == CATALOG_SOURCE_TESMIO && !g_includeTesmioLoader) return false;
    if (g_onlyFavorites && !item.favorite) return false;
    if (g_onlyAvailable && !CatalogItemAvailableCached(item)) return false;
    return true;
}

static const wchar_t* CatalogSourceLabel(CatalogSource source)
{
    if (source == CATALOG_SOURCE_WORKSHOP) return Ui(UI_SOURCE_WORKSHOP);
    if (source == CATALOG_SOURCE_TESMIO) return Ui(UI_TESMIO);
    return Ui(UI_SOURCE_VANILLA);
}

static void AppendResourceDisplay(wchar_t* destination, size_t capacity,
                                  const char* resource)
{
    if (!destination || !capacity || !resource || !resource[0]) return;
    if (destination[0]) wcsncat_s(destination, capacity, L", ", _TRUNCATE);
    int index = CatalogResourceIndex(resource);
    if (index >= 0)
        wcsncat_s(destination, capacity, g_catalogResources[index].display,
                  _TRUNCATE);
    else
    {
        wchar_t fallback[96] = {};
        CopyWideFallback(fallback, sizeof(fallback) / sizeof(wchar_t), resource);
        wcsncat_s(destination, capacity, fallback, _TRUNCATE);
    }
}

static void BuildItemResourceList(const CatalogItem& item, wchar_t* destination,
                                  size_t capacity)
{
    destination[0] = 0;
    for (int i = 0; i < item.metadata.produceCount; ++i)
        AppendResourceDisplay(destination, capacity, item.metadata.produces[i]);
    for (int i = 0; i < item.metadata.consumeCount; ++i)
    {
        bool duplicate = false;
        for (int j = 0; j < item.metadata.produceCount; ++j)
            if (_stricmp(item.metadata.produces[j], item.metadata.consumes[i]) == 0)
                duplicate = true;
        if (!duplicate)
            AppendResourceDisplay(destination, capacity, item.metadata.consumes[i]);
    }
    for (int i = 0; i < item.metadata.storeCount; ++i)
    {
        bool duplicate = false;
        for (int j = 0; j < item.metadata.produceCount; ++j)
            if (_stricmp(item.metadata.produces[j], item.metadata.stores[i]) == 0)
                duplicate = true;
        for (int j = 0; j < item.metadata.consumeCount; ++j)
            if (_stricmp(item.metadata.consumes[j], item.metadata.stores[i]) == 0)
                duplicate = true;
        if (!duplicate) AppendResourceDisplay(destination, capacity,
                                              item.metadata.stores[i]);
    }
    if (!destination[0]) wcscpy_s(destination, capacity, Ui(UI_NONE));
}

static void BuildRelationText(const wchar_t* prefix, const char values[][64], int count,
                              wchar_t* destination, size_t capacity)
{
    wcsncpy_s(destination, capacity, prefix, _TRUNCATE);
    if (!count)
    {
        wcscat_s(destination, capacity, Ui(UI_NONE));
        return;
    }
    wchar_t list[384] = {};
    for (int i = 0; i < count; ++i)
        AppendResourceDisplay(list, sizeof(list) / sizeof(wchar_t), values[i]);
    wcscat_s(destination, capacity, list);
}

struct CatalogResourceTooltip
{
    bool active;
    wchar_t title[64];
    char resources[MAX_TOOLTIP_RESOURCES][64];
    int count;
    int columns;
    int rows;
    float columnWidths[8];
    float x;
    float y;
    float width;
    float height;
};

static void AddTooltipResource(CatalogResourceTooltip& tooltip,
                               const char* resource)
{
    if (!resource || !resource[0] || tooltip.count >= MAX_TOOLTIP_RESOURCES)
        return;
    for (int i = 0; i < tooltip.count; ++i)
        if (_stricmp(tooltip.resources[i], resource) == 0) return;
    strncpy_s(tooltip.resources[tooltip.count], 64, resource, _TRUNCATE);
    ++tooltip.count;
}

static void AddTooltipResources(CatalogResourceTooltip& tooltip,
                                const char values[][64], int count)
{
    for (int i = 0; i < count; ++i)
        AddTooltipResource(tooltip, values[i]);
}

static void PrepareTooltipResources(CatalogResourceTooltip& tooltip,
                                    const CatalogItem& item, int relation)
{
    tooltip.count = 0;
    if (relation == 1 || relation == 0)
        AddTooltipResources(tooltip, item.metadata.produces,
                            item.metadata.produceCount);
    if (relation == 2 || relation == 0)
        AddTooltipResources(tooltip, item.metadata.consumes,
                            item.metadata.consumeCount);
    if (relation == 0)
        AddTooltipResources(tooltip, item.metadata.stores,
                            item.metadata.storeCount);
}

static void LayoutResourceTooltip(CatalogResourceTooltip& tooltip,
                                  float mouseX, float mouseY,
                                  float boundsX, float boundsY,
                                  float boundsWidth, float boundsHeight)
{
    if (!tooltip.active || tooltip.count <= 0) return;
    tooltip.columns = tooltip.count <= 6 ? 1 : (tooltip.count <= 16 ? 2 : 3);
    tooltip.rows = (tooltip.count + tooltip.columns - 1) / tooltip.columns;
    int maximumRows = (int)((boundsHeight - 58.0f) / 27.0f);
    if (maximumRows < 1) maximumRows = 1;
    while (tooltip.rows > maximumRows && tooltip.columns < 8)
    {
        ++tooltip.columns;
        tooltip.rows = (tooltip.count + tooltip.columns - 1) / tooltip.columns;
    }
    for (int column = 0; column < tooltip.columns; ++column)
    {
        float widest = 0.0f;
        int first = column * tooltip.rows;
        int last = first + tooltip.rows;
        if (last > tooltip.count) last = tooltip.count;
        for (int i = first; i < last; ++i)
        {
            int resourceIndex = CatalogResourceIndex(tooltip.resources[i]);
            const wchar_t* display = resourceIndex >= 0
                ? g_catalogResources[resourceIndex].display : L"";
            float textWidth = ApproximateTextWidth(display);
            if (textWidth > widest) widest = textWidth;
        }
        if (widest < 120.0f) widest = 120.0f;
        if (widest > 250.0f) widest = 250.0f;
        tooltip.columnWidths[column] = widest + 28.0f;
    }
    tooltip.width = 28.0f;
    for (int column = 0; column < tooltip.columns; ++column)
        tooltip.width += tooltip.columnWidths[column];
    float titleWidth = ApproximateTextWidth(tooltip.title) + 36.0f;
    if (tooltip.width < titleWidth) tooltip.width = titleWidth;
    if (tooltip.width > boundsWidth - 20.0f)
    {
        tooltip.width = boundsWidth - 20.0f;
        float evenWidth = (tooltip.width - 28.0f) / tooltip.columns;
        for (int column = 0; column < tooltip.columns; ++column)
            tooltip.columnWidths[column] = evenWidth;
    }
    tooltip.height = 48.0f + tooltip.rows * 27.0f;
    if (tooltip.height > boundsHeight - 20.0f)
        tooltip.height = boundsHeight - 20.0f;

    tooltip.x = mouseX + 18.0f;
    tooltip.y = mouseY + 18.0f;
    if (tooltip.x + tooltip.width > boundsX + boundsWidth - 10.0f)
        tooltip.x = mouseX - tooltip.width - 18.0f;
    if (tooltip.y + tooltip.height > boundsY + boundsHeight - 10.0f)
        tooltip.y = mouseY - tooltip.height - 18.0f;
    if (tooltip.x < boundsX + 10.0f) tooltip.x = boundsX + 10.0f;
    if (tooltip.y < boundsY + 10.0f) tooltip.y = boundsY + 10.0f;
}

struct ResourceTooltipRenderSnapshot
{
    wchar_t title[96];
    wchar_t lines[MAX_TOOLTIP_RESOURCES][96];
    int count;
    int columns;
    int rows;
    float columnWidths[8];
    float width;
    float height;
};

static const UINT WM_RESOURCE_TOOLTIP_UPDATE = WM_APP + 0x371;
static HWND g_resourceTooltipWindow = NULL;
static HANDLE g_resourceTooltipThread = NULL;
static HANDLE g_resourceTooltipReadyEvent = NULL;
static SRWLOCK g_resourceTooltipLock = SRWLOCK_INIT;
static ResourceTooltipRenderSnapshot g_resourceTooltipSnapshot = {};
static HWND g_resourceTooltipRequestedParent = NULL;
static RECT g_resourceTooltipRequestedRect = {};
static bool g_resourceTooltipRequestedVisible = false;
static bool g_resourceTooltipRequestedRedraw = false;
static unsigned long long g_resourceTooltipContentHash = 0;
static volatile LONG g_resourceTooltipUpdateQueued = 0;

static void QueueResourceTooltipUpdate()
{
    HWND window = g_resourceTooltipWindow;
    if (!window || !IsWindow(window)) return;
    if (InterlockedExchange(&g_resourceTooltipUpdateQueued, 1) == 0)
    {
        if (!PostMessageW(window, WM_RESOURCE_TOOLTIP_UPDATE, 0, 0))
            InterlockedExchange(&g_resourceTooltipUpdateQueued, 0);
    }
}

static LRESULT CALLBACK ResourceTooltipWindowProc(HWND window, UINT message,
                                                   WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_RESOURCE_TOOLTIP_UPDATE:
        {
            InterlockedExchange(&g_resourceTooltipUpdateQueued, 0);
            HWND parent = NULL;
            RECT requestedRect = {};
            bool visible = false;
            bool redraw = false;
            AcquireSRWLockExclusive(&g_resourceTooltipLock);
            parent = g_resourceTooltipRequestedParent;
            requestedRect = g_resourceTooltipRequestedRect;
            visible = g_resourceTooltipRequestedVisible;
            redraw = g_resourceTooltipRequestedRedraw;
            g_resourceTooltipRequestedRedraw = false;
            ReleaseSRWLockExclusive(&g_resourceTooltipLock);

            if (!visible)
            {
                ShowWindow(window, SW_HIDE);
                return 0;
            }

            if (parent && IsWindow(parent))
                SetWindowLongPtrW(window, GWLP_HWNDPARENT, (LONG_PTR)parent);
            int width = requestedRect.right - requestedRect.left;
            int height = requestedRect.bottom - requestedRect.top;
            if (width < 40) width = 40;
            if (height < 40) height = 40;
            SetWindowPos(window, HWND_TOP, requestedRect.left,
                         requestedRect.top, width, height,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            if (redraw)
                InvalidateRect(window, NULL, FALSE);
            UpdateWindow(window);
            return 0;
        }
        case WM_PAINT:
        {
            ResourceTooltipRenderSnapshot tooltip = {};
            AcquireSRWLockShared(&g_resourceTooltipLock);
            tooltip = g_resourceTooltipSnapshot;
            ReleaseSRWLockShared(&g_resourceTooltipLock);

            PAINTSTRUCT paint = {};
            HDC dc = BeginPaint(window, &paint);
            RECT client = {};
            GetClientRect(window, &client);
            int width = client.right - client.left;
            int height = client.bottom - client.top;
            HBRUSH fill = CreateSolidBrush(RGB(253, 240, 210));
            HBRUSH border = CreateSolidBrush(RGB(121, 99, 72));
            FillRect(dc, &client, fill);
            FrameRect(dc, &client, border);
            DeleteObject(fill);
            DeleteObject(border);

            float scaleX = tooltip.width > 1.0f ? width / tooltip.width : 1.0f;
            float scaleY = tooltip.height > 1.0f ? height / tooltip.height : 1.0f;
            int fontHeight = (int)(19.0f * scaleY + 0.5f);
            if (fontHeight < 14) fontHeight = 14;
            HFONT font = CreateFontW(-fontHeight, 0, 0, 0, FW_NORMAL, FALSE,
                                     FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                     L"Segoe UI");
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(158, 45, 39));
            RECT titleRect = {
                (LONG)(14.0f * scaleX), (LONG)(8.0f * scaleY),
                width - (LONG)(14.0f * scaleX), (LONG)(38.0f * scaleY)
            };
            DrawTextW(dc, tooltip.title, -1, &titleRect,
                      DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            SetTextColor(dc, RGB(43, 41, 37));
            float columnX = 14.0f;
            for (int column = 0; column < tooltip.columns; ++column)
            {
                int first = column * tooltip.rows;
                int last = first + tooltip.rows;
                if (last > tooltip.count) last = tooltip.count;
                for (int i = first; i < last; ++i)
                {
                    RECT rowRect = {
                        (LONG)(columnX * scaleX),
                        (LONG)((39.0f + (i - first) * 27.0f) * scaleY),
                        (LONG)((columnX + tooltip.columnWidths[column] - 12.0f) * scaleX),
                        (LONG)((65.0f + (i - first) * 27.0f) * scaleY)
                    };
                    DrawTextW(dc, tooltip.lines[i], -1, &rowRect,
                              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS |
                              DT_NOPREFIX);
                }
                columnX += tooltip.columnWidths[column];
            }
            SelectObject(dc, oldFont);
            DeleteObject(font);
            EndPaint(window, &paint);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static DWORD WINAPI ResourceTooltipThreadProc(void*)
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = ResourceTooltipWindowProc;
    windowClass.hInstance = (HINSTANCE)H->exeModule;
    windowClass.lpszClassName = L"TesmioResourceTooltip";
    bool classRegistered = RegisterClassExW(&windowClass) != 0 ||
                           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    if (classRegistered)
    {
        g_resourceTooltipWindow = CreateWindowExW(
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"TesmioResourceTooltip", L"", WS_POPUP,
            0, 0, 1, 1, NULL, NULL, (HINSTANCE)H->exeModule, NULL);
    }
    if (g_resourceTooltipReadyEvent)
        SetEvent(g_resourceTooltipReadyEvent);
    if (!g_resourceTooltipWindow) return 1;

    MSG message = {};
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

static bool StartResourceTooltipThread()
{
    if (g_resourceTooltipThread) return g_resourceTooltipWindow != NULL;
    g_resourceTooltipReadyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_resourceTooltipReadyEvent) return false;
    g_resourceTooltipThread = CreateThread(NULL, 0,
        ResourceTooltipThreadProc, NULL, 0, NULL);
    if (!g_resourceTooltipThread) return false;
    WaitForSingleObject(g_resourceTooltipReadyEvent, 1500);
    return g_resourceTooltipWindow != NULL;
}

static void HideResourceTooltipWindow()
{
    bool changed = false;
    AcquireSRWLockExclusive(&g_resourceTooltipLock);
    changed = g_resourceTooltipRequestedVisible;
    g_resourceTooltipRequestedVisible = false;
    ReleaseSRWLockExclusive(&g_resourceTooltipLock);
    if (changed) QueueResourceTooltipUpdate();
}

static unsigned long long ResourceTooltipHash(
    const ResourceTooltipRenderSnapshot& tooltip)
{
    unsigned long long hash = 1469598103934665603ull;
    const unsigned char* bytes = (const unsigned char*)&tooltip;
    for (size_t i = 0; i < sizeof(tooltip); ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

static void UpdateResourceTooltipWindow(const CatalogResourceTooltip& tooltip,
                                        int logicalWidth, int logicalHeight)
{
    if (!tooltip.active || tooltip.count <= 0)
    {
        HideResourceTooltipWindow();
        return;
    }
    HWND parent = GetForegroundWindow();
    DWORD processId = 0;
    if (!parent || !GetWindowThreadProcessId(parent, &processId) ||
        processId != GetCurrentProcessId())
    {
        HideResourceTooltipWindow();
        return;
    }

    if (!g_resourceTooltipWindow || !IsWindow(g_resourceTooltipWindow)) return;

    RECT client = {};
    if (!GetClientRect(parent, &client) || logicalWidth <= 0 || logicalHeight <= 0)
        return;
    float scaleX = (float)(client.right - client.left) / logicalWidth;
    float scaleY = (float)(client.bottom - client.top) / logicalHeight;
    int x = (int)(tooltip.x * scaleX + 0.5f);
    int y = (int)(tooltip.y * scaleY + 0.5f);
    int width = (int)(tooltip.width * scaleX + 0.5f);
    int height = (int)(tooltip.height * scaleY + 0.5f);
    if (width < 40) width = 40;
    if (height < 40) height = 40;

    POINT clientOrigin = {0, 0};
    if (!ClientToScreen(parent, &clientOrigin)) return;
    RECT requestedRect = {
        clientOrigin.x + x, clientOrigin.y + y,
        clientOrigin.x + x + width, clientOrigin.y + y + height
    };

    ResourceTooltipRenderSnapshot snapshot = {};
    wcsncpy(snapshot.title, tooltip.title,
            sizeof(snapshot.title) / sizeof(snapshot.title[0]) - 1);
    snapshot.count = tooltip.count;
    snapshot.columns = tooltip.columns;
    snapshot.rows = tooltip.rows;
    snapshot.width = tooltip.width;
    snapshot.height = tooltip.height;
    for (int column = 0; column < 8; ++column)
        snapshot.columnWidths[column] = tooltip.columnWidths[column];
    for (int i = 0; i < tooltip.count && i < MAX_TOOLTIP_RESOURCES; ++i)
    {
        int resourceIndex = CatalogResourceIndex(tooltip.resources[i]);
        const wchar_t* display = resourceIndex >= 0
            ? g_catalogResources[resourceIndex].display : L"";
        wcsncpy(snapshot.lines[i], display,
                sizeof(snapshot.lines[i]) / sizeof(snapshot.lines[i][0]) - 1);
    }

    unsigned long long contentHash = ResourceTooltipHash(snapshot);
    bool changed = false;
    AcquireSRWLockExclusive(&g_resourceTooltipLock);
    bool contentChanged = contentHash != g_resourceTooltipContentHash;
    bool rectChanged = memcmp(&requestedRect, &g_resourceTooltipRequestedRect,
                              sizeof(RECT)) != 0;
    bool parentChanged = parent != g_resourceTooltipRequestedParent;
    changed = contentChanged || rectChanged || parentChanged ||
              !g_resourceTooltipRequestedVisible;
    if (contentChanged)
    {
        g_resourceTooltipSnapshot = snapshot;
        g_resourceTooltipContentHash = contentHash;
        g_resourceTooltipRequestedRedraw = true;
    }
    g_resourceTooltipRequestedParent = parent;
    g_resourceTooltipRequestedRect = requestedRect;
    g_resourceTooltipRequestedVisible = true;
    ReleaseSRWLockExclusive(&g_resourceTooltipLock);
    if (changed) QueueResourceTooltipUpdate();
}

struct CatalogAvailabilityInfo
{
    int settingsReason;
    int researchCount;
    int researchNameIds[4];
    void* researchTexture;
};

static bool BuildingSubtypePresent(unsigned char* buildingType, int subtype)
{
    if (!buildingType || !H->readablePtr(buildingType + 0x3A8, sizeof(void*)))
        return false;
    unsigned char* begin = *(unsigned char**)(buildingType + 0x3A0);
    unsigned char* end = *(unsigned char**)(buildingType + 0x3A8);
    if (!begin || !end || end < begin || (size_t)(end - begin) % 0xE0 != 0)
        return false;
    size_t count = (size_t)(end - begin) / 0xE0;
    if (count > 256 || !H->readablePtr(begin, count * 0xE0)) return false;
    for (size_t i = 0; i < count; ++i)
        if (*(int*)(begin + i * 0xE0 + 0x90) == subtype) return true;
    return false;
}

static bool ToolUnavailableForLandscape(unsigned char* tool)
{
    if (!tool || !H->readablePtr(tool + 0xA0, sizeof(void*))) return false;
    unsigned char* begin = *(unsigned char**)(tool + 0x98);
    unsigned char* end = *(unsigned char**)(tool + 0xA0);
    // The stock renderer performs this test only for tools carrying at least
    // one 0x40-byte landscape-variant record (trees and other regional props).
    if (!begin || !end || end < begin || (size_t)(end - begin) < 0x40)
        return false;

    unsigned char* rootSlot = g_base + G_LANDSCAPE_ROOT;
    if (!H->readablePtr(rootSlot, sizeof(void*))) return false;
    unsigned char* root = *(unsigned char**)rootSlot;
    if (!root || !H->readablePtr(root + 0xED8, sizeof(void*))) return false;
    unsigned char* landscape = *(unsigned char**)(root + 0xED8);
    if (!landscape || !H->readablePtr(landscape + 0x8EC, sizeof(int))) return false;
    int dynamicOffset = *(int*)(landscape + 0x8EC);
    if (dynamicOffset < -0x1000 || dynamicOffset > 0x100000) return false;
    uintptr_t flagAddress = (uintptr_t)tool + (intptr_t)dynamicOffset + 0xB0u;
    unsigned char* flag = (unsigned char*)flagAddress;
    return H->readablePtr(flag, 1) && *flag == 0;
}

static void CollectCatalogResearch(unsigned char* tool,
                                   unsigned char* buildingType,
                                   CatalogAvailabilityInfo* info)
{
    if (!tool || !info) return;
    unsigned char* game = g_base + G_GAME;
    if (!H->readablePtr(game + 0x117A8, sizeof(void*))) return;

    t_CollectResearch collect = (t_CollectResearch)(
        g_base + (buildingType ? P_RESEARCH_FOR_BUILDING : P_RESEARCH_FOR_TOOL));
    collect(game, buildingType ? (void*)buildingType : (void*)tool);

    RawVector* requirements = (RawVector*)(game + 0x11790);
    if (!H->readablePtr(requirements, sizeof(*requirements)) ||
        !requirements->begin || !requirements->end ||
        requirements->end < requirements->begin ||
        (size_t)(requirements->end - requirements->begin) % sizeof(void*) != 0)
        return;
    size_t count = (size_t)(requirements->end - requirements->begin) / sizeof(void*);
    if (!count || count > 128 ||
        !H->readablePtr(requirements->begin, count * sizeof(void*))) return;

    void** entries = (void**)requirements->begin;
    for (size_t i = 0; i < count; ++i)
    {
        unsigned char* research = (unsigned char*)entries[i];
        if (!research || !H->readablePtr(research + 0x5C, sizeof(int))) continue;
        int nameId = *(int*)(research + 0x58);
        bool duplicate = false;
        for (int j = 0; j < info->researchCount && j < 4; ++j)
            if (info->researchNameIds[j] == nameId) duplicate = true;
        if (!duplicate && info->researchCount < 4)
            info->researchNameIds[info->researchCount++] = nameId;
        if (!info->researchTexture && H->readablePtr(research + 0x48, sizeof(void*)))
        {
            void* texture = *(void**)(research + 0x48);
            if (IsNativeTexture(texture)) info->researchTexture = texture;
        }
    }
}

static void CatalogItemAvailabilityInfo(const CatalogItem& item,
                                        CatalogAvailabilityInfo* info)
{
    memset(info, 0, sizeof(*info));
    if (!item.toolName[0]) { info->settingsReason = 16; return; }

    // Resolve the current object every frame instead of trusting pointers
    // captured during menu initialization. Loading a save reconstructs the
    // game's tool array and invalidates those old addresses.
    char resolvedName[128] = {};
    unsigned char* tool = (unsigned char*)ResolveBuildingTool(
        item.toolName, resolvedName, sizeof(resolvedName));
    if (!tool || !H->readablePtr(tool, TOOL_BUILDING + sizeof(void*)))
    {
        info->settingsReason = 16;
        return;
    }

    unsigned char* buildingType = *(unsigned char**)(tool + TOOL_BUILDING);
    CollectCatalogResearch(tool, buildingType, info);

    unsigned char* game = g_base + G_GAME;
    if (!H->readablePtr(game + 0x5E0, sizeof(int))) return;
    int infrastructureMode = *(int*)(game + 0x5B0);

    // Network tools have no BuildingType, but the stock renderer still locks
    // their power, heating, water, traffic and seasonal variants by name.
    if (infrastructureMode < 1 &&
        (AsciiContainsNoCase(resolvedName, "eletric_high") ||
         AsciiContainsNoCase(resolvedName, "eletric_uhigh") ||
         AsciiContainsNoCase(resolvedName, "eletric_low") ||
         AsciiContainsNoCase(resolvedName, "eletric_ulow")))
        info->settingsReason = 4;
    if ((infrastructureMode == 0 || *(int*)(game + 0x5C0) == 0) &&
        AsciiContainsNoCase(resolvedName, "heating_pipe_"))
        info->settingsReason = 7;
    if (*(int*)(game + 0x5B4) == 0 &&
        (AsciiContainsNoCase(resolvedName, "waterpipe_") ||
         AsciiContainsNoCase(resolvedName, "sewagepipe_")))
        info->settingsReason = 10;
    if (H->readablePtr(game + 0x14451, 1) && game[0x1090] == 0 &&
        game[0x14451] == 0 &&
        (AsciiContainsNoCase(resolvedName, "road_crossroad0") ||
         AsciiContainsNoCase(resolvedName, "road_crossroad1") ||
         AsciiContainsNoCase(resolvedName, "road_crossroad2")))
        info->settingsReason = 11;
    if (*(int*)(game + 0x5C0) == 0 &&
        AsciiContainsNoCase(resolvedName, "road_sign_snow"))
        info->settingsReason = 8;

    if (buildingType && H->readablePtr(buildingType, 0x3B0))
    {
        int type = *(int*)(buildingType + 0x360);
        if (infrastructureMode < 2 && (type == 0x22 || type == 0x23))
            info->settingsReason = 5;
        if (infrastructureMode < 1 &&
            ((type >= 0x11 && type <= 0x12) || type == 0x3C))
            info->settingsReason = 4;
        if (*(int*)(game + 0x5DC) == 1 && (type == 0x0A || type == 0x04))
            info->settingsReason = 1;
        if (*(int*)(game + 0x5D0) == 0 && type == 0x1A)
            info->settingsReason = 2;
        if (*(int*)(game + 0x5D4) == 0 && type == 0x24)
            info->settingsReason = 3;
        if ((infrastructureMode == 0 || *(int*)(game + 0x5C0) == 0) &&
            type >= 0x46 && type <= 0x48)
            info->settingsReason = 7;
        if (*(int*)(game + 0x5C0) == 0 && *(int*)(game + 0x5B4) == 0 &&
            *(int*)(game + 0x5B8) == 0 && type == 0x31)
            info->settingsReason = 8;
        if (*(int*)(game + 0x5E0) == 0 && type >= 0x49 && type <= 0x4C &&
            type != 0x4A)
            info->settingsReason = 9;
        if (*(int*)(game + 0x5B4) == 0 &&
            (((type >= 0x5A && type <= 0x5F) ||
              (type >= 0x61 && type <= 0x62)) ||
             (type == 0 && (BuildingSubtypePresent(buildingType, 0x0F) ||
                            BuildingSubtypePresent(buildingType, 0x10)))))
            info->settingsReason = 10;
        if (*(int*)(game + 0x5B8) == 0 &&
            (type == 0x69 || BuildingSubtypePresent(buildingType, 0x11)))
            info->settingsReason = 12;
        if (*(int*)(game + 0x5B8) == 0 && type == 0x6A)
            info->settingsReason = 13;
        if (*(int*)(game + 0x5BC) == 0 && type == 0x6C)
            info->settingsReason = 14;
        if (type == 0x6D &&
            (*(int*)(game + 0x5B8) != 2 || *(int*)(game + 0x5BC) == 0))
            info->settingsReason = 15;
    }

    // The native landscape test is deliberately last: for a regional plant it
    // is the most useful explanation even if another optional simulation is off.
    if (ToolUnavailableForLandscape(tool)) info->settingsReason = 16;
}

static int CatalogLockTextId(int reason)
{
    switch (reason)
    {
        case 1: return 57012;
        case 2: return 57013;
        case 3: return 57014;
        case 4: return 57015;
        case 5: return 57016;
        case 6: return 57017;
        case 7: return 57018;
        case 8: return 57019;
        case 9: return 57020;
        case 10: return 57021;
        case 11: return 57022;
        case 12: return 57024;
        case 13: return 57025;
        case 14: return 57026;
        case 15: return 57027;
        case 16: return 57028;
        default: return 0;
    }
}

static int CatalogLockReasonFromTextId(int textId)
{
    for (int reason = 1; reason <= 16; ++reason)
        if (CatalogLockTextId(reason) == textId) return reason;
    return 0;
}

static int CatalogLockReasonFromItem(const CatalogItem& item)
{
    const char* values[] = {
        item.toolName, item.descriptorPath, item.metadata.type
    };
    for (int i = 0; i < 3; ++i)
    {
        const char* value = values[i];
        if (!value || !value[0]) continue;
        if (AsciiContainsNoCase(value, "demolition")) return 14;
        if (AsciiContainsNoCase(value, "fire")) return 6;
        if (AsciiContainsNoCase(value, "fuel") ||
            AsciiContainsNoCase(value, "refuel")) return 5;
        if (AsciiContainsNoCase(value, "heating")) return 7;
        if (AsciiContainsNoCase(value, "sewage") ||
            AsciiContainsNoCase(value, "water")) return 10;
        if (AsciiContainsNoCase(value, "waste") ||
            AsciiContainsNoCase(value, "garbage")) return 9;
        if (AsciiContainsNoCase(value, "maintenance") ||
            AsciiContainsNoCase(value, "repair")) return 12;
        if (AsciiContainsNoCase(value, "traffic") ||
            AsciiContainsNoCase(value, "road_sign")) return 11;
        if (AsciiContainsNoCase(value, "school") ||
            AsciiContainsNoCase(value, "university") ||
            AsciiContainsNoCase(value, "education")) return 2;
        if (AsciiContainsNoCase(value, "police") ||
            AsciiContainsNoCase(value, "prison") ||
            AsciiContainsNoCase(value, "crime")) return 3;
        if (AsciiContainsNoCase(value, "pollution")) return 1;
        if (AsciiContainsNoCase(value, "electric") ||
            AsciiContainsNoCase(value, "eletric") ||
            AsciiContainsNoCase(value, "power")) return 4;
        if (AsciiContainsNoCase(value, "snow") ||
            AsciiContainsNoCase(value, "season")) return 8;
        if (AsciiContainsNoCase(value, "terrain") ||
            AsciiContainsNoCase(value, "landscape")) return 16;
    }
    return 0;
}

static void CatalogAvailabilityMessage(const CatalogAvailabilityInfo& info,
                                       wchar_t* destination, size_t capacity)
{
    if (!destination || !capacity) return;
    destination[0] = 0;
    if (info.settingsReason)
    {
        int textId = CatalogLockTextId(info.settingsReason);
        if (textId && ActiveTextById(textId, destination, capacity)) return;
        wcscpy_s(destination, capacity, info.settingsReason == 16
            ? Ui(UI_LANDSCAPE_UNAVAILABLE)
            : Ui(UI_SETTINGS_UNAVAILABLE));
        return;
    }
    if (!info.researchCount) return;

    wchar_t prefix[96] = {};
    wcsncpy_s(prefix, sizeof(prefix) / sizeof(wchar_t),
              Ui(UI_REQUIRED_RESEARCH), _TRUNCATE);
    ActiveTextById(11012, prefix, sizeof(prefix) / sizeof(wchar_t));
    wcsncpy_s(destination, capacity, prefix, _TRUNCATE);
    wcsncat_s(destination, capacity, L": ", _TRUNCATE);
    for (int i = 0; i < info.researchCount && i < 4; ++i)
    {
        wchar_t name[128] = {};
        if (!ActiveTextById(info.researchNameIds[i], name,
                            sizeof(name) / sizeof(wchar_t)))
            continue;
        if (destination[wcslen(destination) - 1] != L' ') 
            wcsncat_s(destination, capacity, L", ", _TRUNCATE);
        wcsncat_s(destination, capacity, name, _TRUNCATE);
    }
}

static int CatalogItemAvailability(const CatalogItem& item)
{
    CatalogAvailabilityInfo info = {};
    CatalogItemAvailabilityInfo(item, &info);
    return info.settingsReason == 0 && info.researchCount == 0 ? 1 : 0;
}

static bool CatalogItemAvailableCached(CatalogItem& item)
{
    if (item.availabilityCacheEpoch != g_availabilityCacheEpoch)
    {
        CatalogAvailabilityInfo info = {};
        CatalogItemAvailabilityInfo(item, &info);
        item.availableCached = info.settingsReason == 0 &&
                               info.researchCount == 0;
        item.availabilitySettingsReason = info.settingsReason;
        item.availabilityResearchCount = info.researchCount;
        memcpy(item.availabilityResearchNameIds, info.researchNameIds,
               sizeof(item.availabilityResearchNameIds));
        item.availabilityResearchTexture = info.researchTexture;
        item.availabilityCacheEpoch = g_availabilityCacheEpoch;
    }
    return item.availableCached;
}

static void CatalogItemAvailabilityInfoCached(CatalogItem& item,
                                              CatalogAvailabilityInfo* info)
{
    if (!info) return;
    CatalogItemAvailableCached(item);
    memset(info, 0, sizeof(*info));
    info->settingsReason = item.availabilitySettingsReason;
    info->researchCount = item.availabilityResearchCount;
    memcpy(info->researchNameIds, item.availabilityResearchNameIds,
           sizeof(info->researchNameIds));
    info->researchTexture = item.availabilityResearchTexture;
}

static bool ActivateCatalogBuilding(const CatalogItem& item)
{
    char resolvedName[128] = {};
    void* currentTool = ResolveBuildingTool(item.toolName, resolvedName,
                                            sizeof(resolvedName));
    if (!currentTool || !g_bottomMenuController ||
        !H->readablePtr(g_bottomMenuController, 0x70))
        return false;
    int availability = CatalogItemAvailability(item);
    if (availability <= 0)
    {
        H->log("tesmiomenu  blocked unavailable catalog building: %s",
               item.toolName);
        return false;
    }
    unsigned char* controller = (unsigned char*)g_bottomMenuController;
    *(void**)(controller + 0x40) = currentTool;
    *(int*)(controller + 0x68) = 0;
    controller[1] = 1;
    *(void**)(g_base + 0x9E2338) = NULL;
    g_catalogVisible = false;
    g_catalogInputArmed = false;
    g_catalogAvailabilityWarmupFrames = 0;
    g_openDropdown = 0;
    HideResourceTooltipWindow();
    g_suppressCustomSelection = true;
    H->log("tesmiomenu  catalog building activated: %s (%p)",
           item.toolName, currentTool);
    return true;
}

static void DrawNativeCatalog()
{
    int screenWidth = *(int*)(g_base + G_SCREEN_WIDTH);
    int screenHeight = *(int*)(g_base + G_SCREEN_HEIGHT);
    if (screenWidth < 800 || screenHeight < 600) return;

    // About 20% larger than the previous native prototype, while always
    // leaving a small margin on low-resolution displays.
    float width = (float)screenWidth * 0.59f;
    float height = (float)screenHeight * 0.81f;
    if (width > 1116.0f) width = 1116.0f;
    if (height > 888.0f) height = 888.0f;
    if (width > (float)screenWidth - 40.0f) width = (float)screenWidth - 40.0f;
    if (height > (float)screenHeight - 40.0f) height = (float)screenHeight - 40.0f;
    if (g_catalogX < 0.0f || g_catalogY < 0.0f)
    {
        g_catalogX = ((float)screenWidth - width) * 0.5f;
        g_catalogY = ((float)screenHeight - height) * 0.5f;
    }
    float x = g_catalogX;
    float y = g_catalogY;

    if (!LoadCatalogTextures()) return;

    float mouseX = -10000.0f;
    float mouseY = -10000.0f;
    ReadGameMouse(screenWidth, screenHeight, &mouseX, &mouseY);
    bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool pressed = false;
    if (!g_catalogInputArmed)
    {
        // Consume the complete toolbar click, including its release.  The next
        // physical press is the first one that belongs to the catalog.
        g_mouseWasDown = leftDown;
        if (!leftDown)
        {
            g_catalogInputArmed = true;
            H->log("tesmiomenu  catalog input armed after opening click");
        }
    }
    else
        pressed = leftDown && !g_mouseWasDown;

    if (g_catalogAvailabilityWarmupFrames > 0 &&
        --g_catalogAvailabilityWarmupFrames == 0)
    {
        ++g_availabilityCacheEpoch;
        if (!g_availabilityCacheEpoch) g_availabilityCacheEpoch = 1;
        H->log("tesmiomenu  initial catalog availability cache refreshed");
    }

    // Selecting a card and placing a building must never be the same physical
    // click. Keep the catalogue (and its input shield) alive until the mouse
    // button has been released, then arm the construction tool.
    if (g_pendingCatalogItem >= 0 && !leftDown)
    {
        int pending = g_pendingCatalogItem;
        g_pendingCatalogItem = -1;
        if (pending < g_catalogItemCount)
            ActivateCatalogBuilding(g_catalogItems[pending]);
        g_mouseWasDown = false;
        return;
    }

    bool closeHovered = PointInside(mouseX, mouseY,
                                    x + width - 66.0f, y + 17.0f, 48.0f, 48.0f);
    bool headerHovered = PointInside(mouseX, mouseY, x, y, width, 72.0f);
    if (!g_catalogDragging && headerHovered && !closeHovered && pressed)
    {
        g_catalogDragging = true;
        g_catalogDragX = mouseX - x;
        g_catalogDragY = mouseY - y;
    }
    if (g_catalogDragging)
    {
        if (leftDown)
        {
            x = mouseX - g_catalogDragX;
            y = mouseY - g_catalogDragY;
            if (x < 0.0f) x = 0.0f;
            if (y < 0.0f) y = 0.0f;
            if (x + width > (float)screenWidth) x = (float)screenWidth - width;
            if (y + height > (float)screenHeight) y = (float)screenHeight - height;
            g_catalogX = x;
            g_catalogY = y;
        }
        else
        {
            g_catalogDragging = false;
            H->log("tesmiomenu  native catalog moved to %.0f, %.0f", x, y);
        }
    }
    NativeDrawTexture(g_catalogTexture, x, y, width, height,
                      1.0f, 1.0f, 1.0f, 0.98f);

    NativePrint(Ui(UI_CATALOG_TITLE), x + 28.0f, y + 30.0f, 0xFF9E2D27u);
    NativePrint(L"X", x + width - 51.0f, y + 30.0f, 0xFF9E2D27u);

    if (closeHovered && pressed)
    {
        g_pendingCatalogItem = -1;
        g_catalogVisible = false;
        g_catalogInputArmed = false;
        g_catalogAvailabilityWarmupFrames = 0;
        HideResourceTooltipWindow();
        *(unsigned char*)(g_base + G_CLICK_FLAG) = 0;
        g_mouseWasDown = leftDown;
        H->log("tesmiomenu  native catalog closed");
        return;
    }

    const float selectorGap = 12.0f;
    const float selectorWidth = (width - 80.0f - selectorGap * 2.0f) / 3.0f;
    float selectorX = x + 40.0f;
    DrawNativeSelector(mouseX, mouseY, pressed, 1, Ui(UI_TYPE), selectorX,
                       y + 88.0f, selectorWidth);
    selectorX += selectorWidth + selectorGap;
    DrawNativeSelector(mouseX, mouseY, pressed, 2, Ui(UI_RESOURCE), selectorX,
                       y + 88.0f, selectorWidth);
    float sourceX = selectorX + selectorWidth + selectorGap;
    DrawNativeSelector(mouseX, mouseY, pressed, 3, Ui(UI_SOURCE), sourceX,
                       y + 88.0f, selectorWidth);

    if (g_openDropdown == 0)
        DrawOnlyAvailableCheckbox(mouseX, mouseY, pressed,
                                  x + width - 320.0f, y + 148.0f, 280.0f);

    int matches[MAX_CATALOG_ITEMS] = {};
    int resultCount = 0;
    for (int i = 0; i < g_catalogItemCount; ++i)
        if (CatalogItemPassesFilters(g_catalogItems[i])) matches[resultCount++] = i;

    const float cardGap = 18.0f;
    const float cardWidth = (width - 98.0f - cardGap) * 0.5f;
    const float cardTop = 188.0f;
    const float footerReserve = 80.0f;
    const float fittedTwoRowHeight =
        (height - cardTop - footerReserve - cardGap) * 0.5f;
    const bool useTwoCardRows = fittedTwoRowHeight >= 210.0f;
    const int pageSize = useTwoCardRows ? 4 : 2;
    float cardHeight = useTwoCardRows ? fittedTwoRowHeight : 244.0f;
    if (cardHeight > 244.0f) cardHeight = 244.0f;
    const float cardVerticalScale = cardHeight / 244.0f;
    int pageCount = resultCount > 0 ? (resultCount + pageSize - 1) / pageSize : 1;
    if (g_resultPage >= pageCount) g_resultPage = pageCount - 1;
    if (g_resultPage < 0) g_resultPage = 0;
    RememberCurrentResultPage();

    // C3D batches font glyphs separately from panel textures. Text covered by
    // a dropdown therefore must not be submitted to the font pass at all.
    bool resultLabelCovered = g_openDropdown == 1 || g_openDropdown == 2;
    if (!resultLabelCovered)
    {
        wchar_t resultText[64] = {};
        FormatCatalogResultCount(resultCount, resultText, 64);
        NativePrint(resultText, x + 40.0f, y + 158.0f, 0xFF6A5E4Du);
    }

    int firstResult = g_resultPage * pageSize;
    int lastResult = firstResult + pageSize;
    if (lastResult > resultCount) lastResult = resultCount;
    CatalogResourceTooltip resourceTooltip = {};
    if (g_openDropdown == 0)
    {
        for (int position = firstResult;
             position < lastResult && !resourceTooltip.active; ++position)
        {
            int local = position - firstResult;
            int column = local % 2;
            int row = local / 2;
            const CatalogItem& item = g_catalogItems[matches[position]];
            float cardX = x + 40.0f + column * (cardWidth + cardGap);
            float cardY = y + cardTop + row * (cardHeight + cardGap);
            float imageX = cardX + 16.0f;
            float imageWidth = cardWidth * 0.34f;
            float textX = imageX + imageWidth + 16.0f;
            float textWidth = cardX + cardWidth - 26.0f - textX;

            wchar_t resources[384] = {};
            BuildItemResourceList(item, resources, 384);
            wchar_t resourceText[448] = {};
            wcsncpy_s(resourceText, 448, Ui(UI_RESOURCE_PREFIX), _TRUNCATE);
            wcscat_s(resourceText, 448, resources);
            wchar_t produces[448] = {};
            wchar_t consumes[448] = {};
            BuildRelationText(Ui(UI_PRODUCES_PREFIX), item.metadata.produces,
                              item.metadata.produceCount, produces, 448);
            BuildRelationText(Ui(UI_CONSUMES_PREFIX), item.metadata.consumes,
                              item.metadata.consumeCount, consumes, 448);

            int relation = -1;
            const wchar_t* title = NULL;
            if (ApproximateTextWidth(resourceText) > textWidth &&
                PointInside(mouseX, mouseY, textX,
                            cardY + 75.0f * cardVerticalScale,
                            textWidth, 29.0f * cardVerticalScale))
            {
                relation = 0;
                title = Ui(UI_RESOURCE);
            }
            else if (ApproximateTextWidth(produces) > textWidth &&
                     PointInside(mouseX, mouseY, textX,
                                 cardY + 141.0f * cardVerticalScale,
                                 textWidth, 29.0f * cardVerticalScale))
            {
                relation = 1;
                title = Ui(UI_PRODUCES_PREFIX);
            }
            else if (ApproximateTextWidth(consumes) > textWidth &&
                     PointInside(mouseX, mouseY, textX,
                                 cardY + 175.0f * cardVerticalScale,
                                 textWidth, 29.0f * cardVerticalScale))
            {
                relation = 2;
                title = Ui(UI_CONSUMES_PREFIX);
            }
            if (relation >= 0)
            {
                resourceTooltip.active = true;
                wcsncpy_s(resourceTooltip.title,
                          sizeof(resourceTooltip.title) / sizeof(wchar_t),
                          title, _TRUNCATE);
                size_t titleLength = wcslen(resourceTooltip.title);
                while (titleLength &&
                       (resourceTooltip.title[titleLength - 1] == L' ' ||
                        resourceTooltip.title[titleLength - 1] == L':'))
                    resourceTooltip.title[--titleLength] = 0;
                PrepareTooltipResources(resourceTooltip, item, relation);
            }
        }
        LayoutResourceTooltip(resourceTooltip, mouseX, mouseY,
                              x + 10.0f, y + 174.0f,
                              width - 20.0f, height - 250.0f);
    }
    const CatalogItem* hoveredLockedItem = NULL;
    CatalogAvailabilityInfo hoveredAvailability = {};
    for (int position = firstResult; position < lastResult; ++position)
    {
        int local = position - firstResult;
        int column = local % 2;
        int row = local / 2;
        CatalogItem& item = g_catalogItems[matches[position]];
        float cardX = x + 40.0f + column * (cardWidth + cardGap);
        float cardY = y + cardTop + row * (cardHeight + cardGap);
        bool cardInteractive = g_openDropdown == 0;
        bool cardHovered = cardInteractive &&
                           PointInside(mouseX, mouseY, cardX, cardY,
                                       cardWidth, cardHeight);
        float starX = cardX + 5.0f;
        float starY = cardY + 5.0f;
        const float starSize = 34.0f;
        bool starHovered = cardInteractive &&
                           PointInside(mouseX, mouseY, starX, starY,
                                       starSize, starSize);
        if (starHovered && pressed)
        {
            item.favorite = !item.favorite;
            SaveCatalogFavorite(item);
        }
        CatalogAvailabilityInfo availability = {};
        CatalogItemAvailabilityInfoCached(item, &availability);
        bool available = availability.settingsReason == 0 &&
                         availability.researchCount == 0;
        if (cardHovered && !available)
        {
            hoveredLockedItem = &item;
            hoveredAvailability = availability;
        }
        NativeDrawTexture(g_catalogSolidTexture, cardX, cardY,
                          cardWidth, cardHeight,
                          !available ? 0.90f : (cardHovered ? 1.0f : 0.98f),
                          !available ? 0.88f : (cardHovered ? 0.94f : 0.91f),
                          !available ? 0.84f : (cardHovered ? 0.78f : 0.72f),
                          0.96f);

        float imageX = cardX + 16.0f;
        float imageY = cardY + 16.0f;
        float imageWidth = cardWidth * 0.34f;
        float imageHeight = cardHeight - 32.0f;
        void* previewTexture = CatalogItemPreview(item);
        if (previewTexture)
            NativeDrawTexture(previewTexture,
                              imageX, imageY, imageWidth, imageHeight,
                              available ? 1.0f : 0.48f,
                              available ? 1.0f : 0.48f,
                              available ? 1.0f : 0.48f, 1.0f);
        else
            NativeDrawTexture(g_catalogSolidTexture, imageX, imageY,
                              imageWidth, imageHeight, 0.80f, 0.76f, 0.68f, 1.0f);

        if (!available)
        {
            // Match the stock build cards: soften the thumbnail, lay a pale
            // fog over it, and use the game's own reason/research pictogram.
            NativeDrawTexture(g_catalogSolidTexture, imageX, imageY,
                              imageWidth, imageHeight,
                              0.95f, 0.95f, 0.93f, 0.34f);
            int cornerPictogramReason = availability.settingsReason;
            if (!cornerPictogramReason)
            {
                for (int i = 0; i < availability.researchCount && i < 4; ++i)
                {
                    cornerPictogramReason = CatalogLockReasonFromTextId(
                        availability.researchNameIds[i]);
                    if (cornerPictogramReason) break;
                }
            }
            if (!cornerPictogramReason)
                cornerPictogramReason = CatalogLockReasonFromItem(item);
            void* centeredPictogram = CenteredLockTexture(cornerPictogramReason);
            if (centeredPictogram)
            {
                const float pictogramSize = 48.0f;
                NativeDrawTexture(
                    centeredPictogram,
                    imageX + imageWidth * 0.5f - pictogramSize * 0.5f,
                    imageY + imageHeight * 0.5f - pictogramSize * 0.5f,
                    pictogramSize, pictogramSize,
                    1.0f, 1.0f, 1.0f, 0.98f);
            }
            else if (availability.researchTexture)
            {
                const float researchSize = 52.0f;
                NativeDrawTexture(
                    availability.researchTexture,
                    imageX + imageWidth * 0.5f - researchSize * 0.5f,
                    imageY + imageHeight * 0.5f - researchSize * 0.5f,
                    researchSize, researchSize,
                    1.0f, 1.0f, 1.0f, 0.96f);
            }
        }

        if (g_favoriteTexture)
        {
            if (starHovered)
                NativeDrawTexture(g_catalogSolidTexture, starX - 2.0f, starY - 2.0f,
                                  starSize + 4.0f, starSize + 4.0f,
                                  1.0f, 0.93f, 0.70f, 0.92f);
            NativeDrawTexture(g_favoriteTexture, starX, starY, starSize, starSize,
                              item.favorite ? 1.0f : 0.68f,
                              item.favorite ? 1.0f : 0.68f,
                              item.favorite ? 1.0f : 0.62f,
                              item.favorite ? 1.0f : 0.72f);
        }

        bool cardTextCovered = (g_openDropdown == 1 || g_openDropdown == 2) && row == 0;
        if (g_openDropdown == 3 && row == 0 && column == 1) cardTextCovered = true;
        if (!cardTextCovered)
        {
            float textX = imageX + imageWidth + 16.0f;
            // This is the hard right boundary for every card line. The extra
            // inset complements the conservative font metric used by
            // FitWideToWidth and keeps glyphs inside the parchment at any UI
            // scale.
            float textWidth = cardX + cardWidth - 26.0f - textX;
            const unsigned long cardText = available ? 0xFF2B2925u
                                                      : 0xFF77736Cu;
            NativePrintFitted(item.display, textX,
                              cardY + 18.0f * cardVerticalScale,
                              textWidth, cardText);

            wchar_t typeText[192] = {};
            wcsncpy_s(typeText, 192, Ui(UI_TYPE_PREFIX), _TRUNCATE);
            wcscat_s(typeText, 192, g_catalogTypes[item.typeIndex].display);
            NativePrintFitted(typeText, textX,
                              cardY + 50.0f * cardVerticalScale,
                              textWidth, cardText);

            wchar_t resources[384] = {};
            BuildItemResourceList(item, resources, 384);
            wchar_t resourceText[448] = {};
            wcsncpy_s(resourceText, 448, Ui(UI_RESOURCE_PREFIX), _TRUNCATE);
            wcscat_s(resourceText, 448, resources);
            NativePrintFitted(resourceText, textX,
                              cardY + 80.0f * cardVerticalScale,
                              textWidth, cardText);

            wchar_t sourceText[96] = {};
            wcsncpy_s(sourceText, 96, Ui(UI_SOURCE_PREFIX), _TRUNCATE);
            wcscat_s(sourceText, 96, CatalogSourceLabel(item.source));
            NativePrintFitted(sourceText, textX,
                              cardY + 110.0f * cardVerticalScale,
                              textWidth, cardText);

            wchar_t produces[448] = {};
            wchar_t consumes[448] = {};
            BuildRelationText(Ui(UI_PRODUCES_PREFIX), item.metadata.produces,
                              item.metadata.produceCount, produces, 448);
            BuildRelationText(Ui(UI_CONSUMES_PREFIX), item.metadata.consumes,
                              item.metadata.consumeCount, consumes, 448);
            NativePrintFitted(produces, textX,
                              cardY + 146.0f * cardVerticalScale,
                              textWidth, cardText);
            NativePrintFitted(consumes, textX,
                              cardY + 180.0f * cardVerticalScale,
                              textWidth, cardText);
            if (!available)
                NativePrintFitted(Ui(UI_UNAVAILABLE), textX,
                                  cardY + 211.0f * cardVerticalScale,
                                  textWidth, 0xFF77736Cu);
        }
        if (cardHovered && pressed && available && !starHovered)
            g_pendingCatalogItem = matches[position];
    }

    if (hoveredLockedItem && g_openDropdown == 0)
    {
        wchar_t lockedMessage[768] = {};
        CatalogAvailabilityMessage(hoveredAvailability, lockedMessage,
                                   sizeof(lockedMessage) / sizeof(wchar_t));
        // Stock restriction strings can contain hard line breaks intended for
        // a different window. Flatten them before wrapping, otherwise the font
        // renderer creates an unbounded third line over the page controls.
        wchar_t normalizedMessage[768] = {};
        NormalizeWideToSingleParagraph(
            lockedMessage, normalizedMessage,
            sizeof(normalizedMessage) / sizeof(wchar_t));
        float tooltipX = x + 40.0f;
        float tooltipY = y + height - 158.0f;
        float tooltipWidth = width - 80.0f;
        NativeDrawTexture(g_catalogSolidTexture, tooltipX, tooltipY,
                          tooltipWidth, 76.0f, 0.98f, 0.94f, 0.84f, 0.98f);
        wchar_t firstLine[384] = {};
        wchar_t secondLine[384] = {};
        WrapWideToTwoLines(normalizedMessage, tooltipWidth - 40.0f,
                           firstLine, sizeof(firstLine) / sizeof(wchar_t),
                           secondLine, sizeof(secondLine) / sizeof(wchar_t));
        NativePrintFitted(firstLine, tooltipX + 16.0f, tooltipY + 12.0f,
                          tooltipWidth - 40.0f, 0xFF9E2D27u);
        if (secondLine[0])
            NativePrintFitted(secondLine, tooltipX + 16.0f, tooltipY + 39.0f,
                              tooltipWidth - 40.0f, 0xFF6A5E4Du);
    }

    if (!hoveredLockedItem)
        UpdateResourceTooltipWindow(resourceTooltip, screenWidth, screenHeight);
    else
        HideResourceTooltipWindow();

    float arrowY = y + height - 61.0f;
    DrawFavoritesModeButton(mouseX, mouseY,
                            g_openDropdown == 0 ? pressed : false,
                            x + 40.0f, arrowY - 2.0f, 180.0f);
    wchar_t pageText[48] = {};
    FormatCatalogPage(g_resultPage + 1, pageCount, pageText, 48);
    wchar_t fittedPageText[48] = {};
    FitWideToWidth(pageText, fittedPageText, 48, 210.0f);
    float fittedPageWidth = ApproximateTextWidth(fittedPageText);
    NativePrint(fittedPageText, x + width * 0.5f - fittedPageWidth * 0.5f,
                y + height - 53.0f);
    if (pageCount > 1)
    {
        int dummy = -9999;
        float previousX = x + width * 0.5f - 160.0f;
        float nextX = x + width * 0.5f + 114.0f;
        DrawNativeChip(mouseX, mouseY, false, L"<", previousX,
                       arrowY, 46.0f, &dummy, -1000);
        DrawNativeChip(mouseX, mouseY, false, L">", nextX,
                       arrowY, 46.0f, &dummy, -1000);
        if (g_openDropdown == 0 && pressed &&
            PointInside(mouseX, mouseY, previousX, arrowY, 46.0f, 30.0f) &&
            g_resultPage > 0)
            --g_resultPage;
        if (g_openDropdown == 0 && pressed &&
            PointInside(mouseX, mouseY, nextX, arrowY, 46.0f, 30.0f) &&
            g_resultPage + 1 < pageCount)
            ++g_resultPage;
        RememberCurrentResultPage();
    }

    // Draw the open list last so it stays above cards and receives the click.
    if (g_openDropdown == 3)
        DrawNativeSourceDropdown(mouseX, mouseY, pressed, sourceX,
                                 y + 140.0f, selectorWidth);
    else
        DrawNativeDropdown(mouseX, mouseY, pressed, x + 40.0f, y + 140.0f,
                           width - 80.0f);

    // Consume a click anywhere inside the catalog before later game UI sees it.
    if (PointInside(mouseX, mouseY, x, y, width, height) &&
        *(unsigned char*)(g_base + G_CLICK_FLAG))
        *(unsigned char*)(g_base + G_CLICK_FLAG) = 0;
    g_mouseWasDown = leftDown;
}

static void ToggleCatalogFromToolbar()
{
    g_catalogVisible = !g_catalogVisible;
    if (!g_catalogVisible)
    {
        HideResourceTooltipWindow();
        g_catalogInputArmed = false;
        g_catalogAvailabilityWarmupFrames = 0;
    }
    g_catalogDragging = false;
    g_openDropdown = 0;
    g_pendingCatalogItem = -1;
    if (g_catalogVisible)
    {
        g_catalogInputArmed = false;
        g_catalogAvailabilityWarmupFrames = 2;
        g_mouseWasDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        ++g_availabilityCacheEpoch;
        if (!g_availabilityCacheEpoch) g_availabilityCacheEpoch = 1;
        g_resultPage = g_onlyFavorites
            ? g_favoritesResultPage
            : g_regularResultPage;
    }
    H->log("tesmiomenu  native catalog %s by toolbar button",
           g_catalogVisible ? "opened" : "closed");
}

static int ReadBottomMenuLevel1Scale()
{
    char path[2 * MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (!length || length >= sizeof(path)) return 0;
    char* slash = strrchr(path, '\\');
    if (!slash) return 0;
    strcpy_s(slash + 1, sizeof(path) - (size_t)(slash + 1 - path),
             "media_soviet\\config.ini");

    HANDLE file = CreateFileA(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 2 * 1024 * 1024)
    {
        CloseHandle(file);
        return 0;
    }
    char* data = (char*)malloc((size_t)size.QuadPart + 1);
    if (!data) { CloseHandle(file); return 0; }
    DWORD read = 0;
    bool ok = ReadFile(file, data, (DWORD)size.QuadPart, &read, NULL) != FALSE;
    CloseHandle(file);
    if (!ok) { free(data); return 0; }
    data[read] = 0;

    int value = 0;
    const char* key = "BOTTOM_MENU_LEVEL_1_SCALE";
    char* found = strstr(data, key);
    if (found)
    {
        found += strlen(key);
        while (*found == ' ' || *found == '\t') ++found;
        value = atoi(found);
        if (value < -100) value = -100;
        if (value > 300) value = 300;
    }
    free(data);
    return value;
}

static float NativeBottomMenuButtonSize()
{
    float uiScale = *(float*)(g_base + G_UI_SCALE);
    if (uiScale <= 0.05f || uiScale > 8.0f) uiScale = 1.0f;
    // This is the game's own level-1 slot formula used by the lower toolbar:
    // 50 px * global UI scale * (1 + configured percentage * 0.003).
    float configuredScale = 1.0f + (float)g_bottomMenuLevel1Scale * 0.003f;
    return 50.0f * uiScale * configuredScale;
}

static void DrawStandaloneCatalogButton(void* controller)
{
    if (!controller || !LoadCatalogTextures())
        return;

    int screenWidth = *(int*)(g_base + G_SCREEN_WIDTH);
    int screenHeight = *(int*)(g_base + G_SCREEN_HEIGHT);
    // Keep this control completely independent from the stock construction
    // tabs.  For this verification pass it lives at a fixed lower-left screen
    // anchor, so opening it cannot select or animate a vanilla build group.
    float size = NativeBottomMenuButtonSize();
    float x = 12.0f;
    // The stock level-1 button textures are anchored at the top of the
    // 82-unit construction-paper strip (the slot width itself is 50 units).
    // Use that exact vertical formula instead of the old screen-bottom inset.
    float paperHeight = 82.0f * (size / 50.0f);
    float y = (float)screenHeight - paperHeight + 10.0f * (size / 50.0f) + 1.0f;
    if (g_nativeBottomPaperValid)
    {
        float paperHeight = g_nativeBottomPaper.bottom - g_nativeBottomPaper.top;
        float inset = (paperHeight - size) * 0.5f;
        if (inset < 0.0f) inset = 0.0f;
        x = g_nativeBottomPaper.left + inset;
    }
    LONG nativePaperLeft = g_nativeBottomPaperLeftPixels;
    if (nativePaperLeft >= 0)
    {
        // Match the stock horizontal padding between the paper edge, this
        // slot and the grey Transport group. Vertical placement is fixed.
        float inset = 10.0f * (size / 50.0f);
        x = (float)nativePaperLeft + inset;
    }

    g_standaloneButtonX = x;
    g_standaloneButtonY = y;
    g_standaloneButtonSize = size;

    float mouseX = -1000.0f;
    float mouseY = -1000.0f;
    ReadGameMouse(screenWidth, screenHeight, &mouseX, &mouseY);
    bool hovered = PointInside(mouseX, mouseY, x, y, size, size);
    float tint = g_catalogVisible ? 0.82f : (hovered ? 1.0f : 0.94f);
    NativeDrawTexture(g_toolbarTexture, x, y, size, size,
                      tint, tint, tint, 1.0f);
}

static void h_BottomMenuRender(void* self, float scale)
{
    EnsureGameInputShield();
    g_bottomMenuController = self;
    unsigned char** selected = (unsigned char**)(g_base + G_SELECTED_BOTTOM_TAB);
    bool validSelection = H->readablePtr(selected, sizeof(*selected));

    if (InterlockedExchange(&g_standaloneToggleRequested, 0) != 0)
        ToggleCatalogFromToolbar();

    if (validSelection && g_catalogVisible && *selected)
    {
        g_pendingCatalogItem = -1;
        g_catalogVisible = false;
        g_catalogInputArmed = false;
        g_catalogAvailabilityWarmupFrames = 0;
        HideResourceTooltipWindow();
        g_openDropdown = 0;
        H->log("tesmiomenu  native catalog closed by stock category");
    }
    if (validSelection && g_catalogVisible) *selected = NULL;

    o_BottomMenuRender(self, scale);
    if (validSelection && g_catalogVisible) *selected = NULL;

    bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (g_catalogVisible && escapeDown && !g_escapeWasDown)
    {
        g_pendingCatalogItem = -1;
        g_catalogVisible = false;
        g_catalogInputArmed = false;
        g_catalogAvailabilityWarmupFrames = 0;
        HideResourceTooltipWindow();
        g_catalogDragging = false;
        g_openDropdown = 0;
        H->log("tesmiomenu  native catalog closed by Escape");
    }
    g_escapeWasDown = escapeDown;

    if (g_catalogVisible && g_catalogNativeReady)
        DrawNativeCatalog();
    else
        HideResourceTooltipWindow();
}

static void h_ConstructionRender(void* self)
{
    o_ConstructionRender(self);
    // Draw last, after the stock construction paper and its category buttons,
    // so the independent catalogue icon remains on the foreground layer.
    DrawStandaloneCatalogButton(self);
}

static bool ResolveNativeCatalogImports()
{
    if (!H->patchIat(H->exeModule, ENGINE_DLL,
                     "?Draw@C3D_PANEL2D@@QEAAXMMMMM_N@Z",
                     (void*)h_PanelDraw, (void**)&o_PanelDraw,
                     "tesmiomenu bottom-panel geometry capture"))
    {
        H->log("tesmiomenu  native panel draw import unavailable");
        return false;
    }
    struct Import { const char* name; void** target; } imports[] = {
        { "?CreateManagedTexture@C3D_MIDDLEPOINT@@QEAAPEAVC3DAPI_TEXTURE@@PEBD@Z",
          (void**)&o_CreateManagedTexture },
        { "?PrintLeftUnicode@C3D_FONTMANAGER@@QEAAXPEAVC3D_FONT@@MMKPEB_WZZ",
          (void**)&o_PrintLeftUnicode }
    };
    for (size_t i = 0; i < sizeof(imports) / sizeof(imports[0]); ++i)
    {
        void** slot = H->findIatSlot(H->exeModule, ENGINE_DLL, imports[i].name);
        if (!slot || !*slot)
        {
            H->log("tesmiomenu  native catalog import missing: %s", imports[i].name);
            return false;
        }
        *imports[i].target = *slot;
    }
    return true;
}

static void h_MenuInit(void)
{
    g_nativeFrontInserted = false;
    g_insideMenuInit = true;
    o_MenuInit();
    g_insideMenuInit = false;
    DetectCatalogLanguage();
    g_bottomMenuLevel1Scale = ReadBottomMenuLevel1Scale();
    RefreshCatalogResourceLabels();

    // A menu rebuild denotes a new engine UI generation (including creation or
    // loading of another republic).  Do not draw the old catalogue for even
    // one transition frame, and never reuse textures owned by the old world.
    g_catalogVisible = false;
    HideResourceTooltipWindow();
    g_catalogDragging = false;
    g_openDropdown = 0;
    g_pendingCatalogItem = -1;
    g_mouseWasDown = false;
    g_catalogInputArmed = false;
    g_catalogAvailabilityWarmupFrames = 0;
    g_toolbarToggleLatch = false;
    g_toolbarMouseWasDown = false;
    g_standaloneButtonCapture = false;
    InterlockedExchange(&g_standaloneToggleRequested, 0);
    g_standaloneButtonX = -1.0f;
    g_standaloneButtonY = -1.0f;
    g_standaloneButtonSize = 0.0f;
    g_bottomPanelCount = 0;
    g_nativeBottomPaper = {};
    g_nativeBottomPaperValid = false;
    g_captureBottomPanels = true;
    g_catalogX = -1.0f;
    g_catalogY = -1.0f;
    ResetCatalogTextureState();
    H->log("tesmiomenu  catalog textures reset for rebuilt game menu");

    CaptureCatalogTypes();
    CaptureCatalogItems();
}

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    H = host;
    g_base = host->exeBase;
    info->name = "Tesmio Catalog";
    info->version = "1.2.2";
    ReadSettings();
    LoadEnglishTextTable();
    LoadRussianTextTable();
    LoadCatalogResources();
    if (g_enabled && !StartResourceTooltipThread())
        H->log("tesmiomenu: resource tooltip UI thread failed to start");
    H->log("tesmiomenu  configurable TesmioLoader build tab");
    return g_enabled ? 0 : 1;
}

extern "C" __declspec(dllexport) int TsmPluginStart(void)
{
    if (!ProbeBuild()) return 1;

    if (!PatchBottomPaperLeft())
    {
        H->log("tesmiomenu  bottom-paper extension failed");
        return 1;
    }

    static const unsigned char menuInit[] = {
        0x40,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8D,0xAC,0x24,0x30,0x99,0xFF,0xFF
    };
    if (!H->installInlineHook(g_base + P_MENU_INIT, (void*)h_MenuInit,
                              (void**)&o_MenuInit, menuInit, sizeof(menuInit),
                              "tesmiomenu bottom tabs"))
    {
        H->log("tesmiomenu  bottom-menu hook failed");
        return 1;
    }

    if (!ResolveNativeCatalogImports())
    {
        H->log("tesmiomenu  native catalog imports unavailable");
        return 1;
    }
    static const unsigned char bottomMenuRender[] = {
        0x48,0x8B,0xC4,0x55,0x53,0x48,0x8D,0x68,
        0xE8,0x48,0x81,0xEC,0x08,0x01,0x00,0x00
    };
    if (!H->installInlineHook(g_base + P_BOTTOM_MENU_RENDER,
                              (void*)h_BottomMenuRender,
                              (void**)&o_BottomMenuRender,
                              bottomMenuRender, sizeof(bottomMenuRender),
                              "tesmiomenu native catalog renderer"))
    {
        H->log("tesmiomenu  native catalog renderer hook failed");
        return 1;
    }
    static const unsigned char constructionRender[] = {
        0x48,0x8B,0xC4,0x55,0x41,0x54,0x41,0x55,
        0x41,0x56,0x41,0x57,0x48,0x8D,0xA8,0x28,
        0xF4,0xFF,0xFF
    };
    if (!H->installInlineHook(g_base + P_CONSTRUCTION_RENDER,
                              (void*)h_ConstructionRender,
                              (void**)&o_ConstructionRender,
                              constructionRender, sizeof(constructionRender),
                              "tesmiomenu foreground catalogue button"))
    {
        H->log("tesmiomenu  construction renderer hook failed");
        return 1;
    }
    g_catalogNativeReady = true;

    // The world picker reads C3D_INPUT's state bytes directly, bypassing both
    // the window procedure and imported button accessors. Suppress those bytes
    // immediately after the engine refreshes them for each frame.
    if (!InstallEngineMouseShield())
    {
        H->log("tesmiomenu  direct engine input shield unavailable");
        return 1;
    }
    // Patch language only after the permanent menu hook succeeded. Returning a
    // failure after an IAT swap would let the loader unload code still named by
    // that slot.
    if (!H->patchIat(H->exeModule, ENGINE_DLL, GET_STRING,
                     (void*)h_GetString, (void**)&o_GetString,
                     "tesmiomenu language"))
    {
        H->log("tesmiomenu  warning: custom tooltip text unavailable");
    }
    H->log("tesmiomenu  native in-game catalog renderer ready");
    return 0;
}
