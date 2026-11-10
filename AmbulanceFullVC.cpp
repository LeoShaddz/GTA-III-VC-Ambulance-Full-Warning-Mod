// AmbulanceFullVC.asi - GTA Vice City v1.0 PC (gta-vc.exe 3,088,896 bytes)
// Mostra "Ambulance full!!" (A_FULL) assim que a ambulancia fica lotada, sem precisar tentar pegar mais um paciente,
// somente enquanto a missao de paramedico esta em andamento.
// Versao SEM CRT (so API do Windows). Porte do AmbulanceFull de GTA III.
//
// Compilar: ver main.yml (cl /LD /GS- + link /NODEFAULTLIB /ENTRY:DllMain).
//
// Diferencas para o GTA III:
//  - O texto A_FULL do Vice City esta na tabela de missao (AMBULAE), que so existe carregada durante a missao de
//    paramedico. Por isso o aviso so aparece nela (se a chave nao existe, o mod nao mostra nada).
//  - O gancho e por quadro, na chamada de CGameLogic::Update (0x4A461B), de forma encadeada (convive com outros mods).

#include <windows.h>
#include <stdarg.h>

typedef unsigned char  u8;
typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned long  uptr;

// ---------------- Enderecos (GTA VC 1.0) ----------------
static const uptr VERSION_ADDR     = 0x667BF0;   // entrada do exe 1.0
static const u32  VERSION_OK       = 0x53E58955;
static const uptr ADDR_HOOK_CALL   = 0x4A461B;   // call CGameLogic::Update (chamado todo quadro de jogo)
static const uptr ADDR_HOOK_TARGET = 0x42BB70;   // CGameLogic::Update
static const uptr ADDR_MSG_JUMPQ   = 0x584300;   // CMessages::AddMessageJumpQ(wchar*, uint32 time, uint16 flag) = PRINT_NOW do script
static const uptr ADDR_TEXT_GET    = 0x584F30;   // CText::Get(const char*) [thiscall]
static const uptr ADDR_THETEXT     = 0x94B220;   // TheText
static const uptr ADDR_TIMER_MS    = 0x974B2C;   // CTimer::m_snTimeInMilliseconds
static const uptr ADDR_MSG_SLOT0   = 0x9458D0;   // CMessages::BriefMessages[0] (legenda)
static const uptr ADDR_PLAYERS     = 0x94AD28;   // CWorld::Players[] (passo 0x170, ped no offset 0)
static const uptr ADDR_PLAYERFOCUS = 0xA10AFB;   // CWorld::PlayerInFocus (byte)
static const uptr ADDR_MODELINFO   = 0x92D4C8;   // CModelInfo::ms_modelInfoPtrs[]

// ---------------- Offsets ----------------
static const int PLAYER_STRIDE     = 0x170;
static const int OFF_PED_INVEHICLE = 0x3AC;  // CPed::bInVehicle (byte)
static const int OFF_PED_VEHICLE   = 0x3A8;  // CPed::m_pMyVehicle
static const int OFF_MODEL_INDEX   = 0x5C;   // CEntity::m_modelIndex (int16)
static const int OFF_DRIVER        = 0x1A8;  // CVehicle::pDriver
static const int OFF_PASSENGERS    = 0x1AC;  // CVehicle::pPassengers[8]
static const int OFF_NUM_PASSENGER = 0x1CC;  // CVehicle::m_nNumPassengers
static const int OFF_NUM_GETTINGIN = 0x1CD;  // CVehicle::m_nNumGettingIn
static const int OFF_NUM_MAX_PASS  = 0x1D0;  // CVehicle::m_nNumMaxPassengers
static const int MODELINFO_NAME    = 0x04;   // nome do modelo (char[24])
static const int MAX_MODELS        = 6500;

typedef wchar_t* (__fastcall *TextGet_t)(void* thisPtr, int edxDummy, const char* key);
typedef void     (__cdecl    *AddMessageJumpQ_t)(wchar_t* text, u32 time, u16 flag);
typedef void     (__cdecl    *Update_t)();

static int  g_fullPeople = 0;      // 0 = automatico (todos os assentos ocupados)
static bool g_logEnabled = false;
static char g_logPath[MAX_PATH];
static Update_t g_origUpdate = 0;

static void Log(const char* fmt, ...)
{
    if (!g_logEnabled || !g_logPath[0]) return;
    char line[700];
    SYSTEMTIME t; GetLocalTime(&t);
    int n = wsprintfA(line, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    n += wvsprintfA(line + n, fmt, ap);
    va_end(ap);
    line[n++] = '\r'; line[n++] = '\n';
    HANDLE h = CreateFileA(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(h, line, n, &w, NULL);
    CloseHandle(h);
}

static bool IsAmbulance(int model)
{
    if (model < 0 || model >= MAX_MODELS) return false;
    const u8* mi = *(const u8* const*)(ADDR_MODELINFO + (uptr)model * 4);
    if (!mi) return false;
    static const char want[8] = { 'a','m','b','u','l','a','n',0 };
    const char* n = (const char*)(mi + MODELINFO_NAME);
    for (int i = 0; i < 8; ++i)
    {
        char c = n[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != want[i]) return false;
    }
    return true;
}

static void ShowAmbulanceFull()
{
    TextGet_t get = (TextGet_t)ADDR_TEXT_GET;
    wchar_t* text = get((void*)ADDR_THETEXT, 0, "A_FULL");
    if (!text) return;
    // Chave inexistente (tabela de missao nao carregada): o jogo devolve sempre o mesmo buffer de erro.
    if (text == get((void*)ADDR_THETEXT, 0, "ZZ_NOKEY_")) return;

    wchar_t* cur   = *(wchar_t**)ADDR_MSG_SLOT0;
    u32      time  = *(u32*)(ADDR_MSG_SLOT0 + 8);
    u32      start = *(u32*)(ADDR_MSG_SLOT0 + 12);
    u32      now   = *(u32*)ADDR_TIMER_MS;
    if (cur == text && (now - start) + 300 < time)
        return;                                    // ja esta na tela

    ((AddMessageJumpQ_t)ADDR_MSG_JUMPQ)(text, 1500, 1);   // mesmos parametros do PRINT_NOW do script (legenda)
    Log("AddMessageJumpQ: text=%08X slot0.antes=%08X time=%u start=%u now=%u", (u32)text, (u32)cur, time, start, now);
}


// ---------------- Deteccao da missao de paramedico ----------------
static const uptr ADDR_ACTIVE_SCRIPTS = 0x975338;   // CTheScripts::pActiveScripts (lista de threads)
static const uptr ADDR_SCRIPTSPACE    = 0x821280;   // CTheScripts::ScriptSpace (variaveis globais)
static const uptr ADDR_ONMISSION_OFF  = 0x978748;   // CTheScripts::OnAMissionFlag (offset da variavel "em missao")
static const int  SCR_OFF_NAME        = 8;          // CRunningScript::m_abScriptName
static const int  VAR_ONMISSION       = 0x04E4;     // g4E4: flag "jogador em missao" (1 durante a missao de paramedico)
static const int  VAR_AMB_STATE       = 0x18B4;     // g18B4: 0 = missao rodando, 1 = concluida, 2 = falhou

static bool NameIsAmbulan(const u8* n)
{
    static const char want[8] = { 'A','M','B','U','L','A','N',0 };
    for (int i = 0; i < 8; ++i) {
        char c = (char)n[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c != want[i]) return false;
    }
    return true;
}

static u8* FindAmbulanceThread()
{
    u8* s = *(u8**)ADDR_ACTIVE_SCRIPTS;
    for (int guard = 0; s && guard < 256; ++guard) {
        if (NameIsAmbulan(s + SCR_OFF_NAME)) return s;
        s = *(u8**)s; // next
    }
    return 0;
}

// Missao de paramedico em andamento: thread AMBULAN existe, g4E4 (em missao) != 0 e g18B4 (estado) == 0.
static bool AmbulanceMissionActive(u8** outThread)
{
    u8* th = FindAmbulanceThread();
    if (outThread) *outThread = th;
    return th &&
           *(volatile u32*)ADDR_ONMISSION_OFF == (u32)VAR_ONMISSION &&     // confere que g4E4 e mesmo a variavel de missao
           *(volatile int*)(ADDR_SCRIPTSPACE + VAR_ONMISSION) != 0 &&
           *(volatile int*)(ADDR_SCRIPTSPACE + VAR_AMB_STATE) == 0;
}

static void Tick()
{
    static void* lastVeh = (void*)1;
    static int   lastModel = -2, lastPeople = -2, lastNum = -2, lastGetIn = -2;

    int focus = *(volatile u8*)ADDR_PLAYERFOCUS;
    if (focus < 0 || focus > 3) return;
    u8* ped = *(u8**)(ADDR_PLAYERS + (uptr)focus * PLAYER_STRIDE);
    u8* v = 0;
    if (ped && ped[OFF_PED_INVEHICLE]) v = *(u8**)(ped + OFF_PED_VEHICLE);   // = FindPlayerVehicle()
    if (!v)
    {
        if (lastVeh != 0) { Log("jogador nao esta em veiculo"); lastVeh = 0; }
        return;
    }

    int   model  = *(short*)(v + OFF_MODEL_INDEX);
    void* driver = *(void**)(v + OFF_DRIVER);
    int   num    = v[OFF_NUM_PASSENGER];
    int   getIn  = (signed char)v[OFF_NUM_GETTINGIN];
    int   max    = v[OFF_NUM_MAX_PASS];
    int   people = (driver ? 1 : 0) + num;

    if (v != lastVeh || model != lastModel || num != lastNum || getIn != lastGetIn || people != lastPeople)
    {
        Log("veh=%08X model=%d driver=%08X numPassengers=%d gettingIn=%d max=%d people=%d alvo=%d",
            (u32)v, model, (u32)driver, num, getIn, max, people, g_fullPeople);
        lastVeh = v; lastModel = model; lastNum = num; lastGetIn = getIn; lastPeople = people;
    }

    if (!IsAmbulance(model)) return;
    if (!driver) return;                                   // so com o jogador dirigindo
    int target = g_fullPeople > 0 ? g_fullPeople : (max + 1);
    if (max < 1 || max > 8) return;
    if (people >= target && AmbulanceMissionActive(0))   // so durante a missao de paramedico
        ShowAmbulanceFull();
}

static void __cdecl HookedUpdate()
{
    g_origUpdate();
    Tick();
}

static bool PatchCall()
{
    u8* p = (u8*)ADDR_HOOK_CALL;
    if (p[0] != 0xE8) { Log("ERRO: nao e um CALL (exe nao e v1.0?)"); return false; }
    int rel = *(int*)(p + 1);
    uptr target = (uptr)(p + 5 + rel);
    // Encadeado: se outro mod ja trocou o destino, chamamos o dele (que chama o original).
    g_origUpdate = (Update_t)target;
    if (target != ADDR_HOOK_TARGET) Log("aviso: destino do CALL ja alterado por outro mod (%08X); encadeando", (u32)target);

    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) { Log("ERRO: VirtualProtect falhou"); return false; }
    *(int*)(p + 1) = (int)((uptr)&HookedUpdate - (uptr)(p + 5));
    VirtualProtect(p, 5, old, &old);
    Log("hook aplicado com sucesso");
    return true;
}

static void SetFileNameInPath(char* path, const char* name)
{
    char* last = path;
    for (char* c = path; *c; ++c) if (*c == '\\') last = c;
    char* d = last + 1;
    while ((*d++ = *name++)) {}
}

extern "C" BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        char asiDirIni[MAX_PATH], exeDirIni[MAX_PATH], asiDirLog[MAX_PATH];
        GetModuleFileNameA(hModule, asiDirIni, MAX_PATH);
        for (int i = 0; i < MAX_PATH; ++i) { asiDirLog[i] = asiDirIni[i]; if (!asiDirIni[i]) break; }
        GetModuleFileNameA(NULL, exeDirIni, MAX_PATH);
        SetFileNameInPath(asiDirIni, "AmbulanceFullVC.ini");
        SetFileNameInPath(asiDirLog, "AmbulanceFullVC.log");
        SetFileNameInPath(exeDirIni, "AmbulanceFullVC.ini");
        for (int i = 0; i < MAX_PATH; ++i) { g_logPath[i] = asiDirLog[i]; if (!asiDirLog[i]) break; }

        const char* iniPath = asiDirIni;
        bool iniFound = GetFileAttributesA(asiDirIni) != INVALID_FILE_ATTRIBUTES;
        if (!iniFound && GetFileAttributesA(exeDirIni) != INVALID_FILE_ATTRIBUTES) { iniPath = exeDirIni; iniFound = true; }

        bool hook = true;
        if (iniFound) {
            g_fullPeople = GetPrivateProfileIntA("Settings", "PeopleToShowWarning", 0, iniPath);
            if (g_fullPeople < 0) g_fullPeople = 0;
            g_logEnabled = GetPrivateProfileIntA("Settings", "Log", 0, iniPath) != 0;
            hook         = GetPrivateProfileIntA("Settings", "Hook", 1, iniPath) != 0;

        } else {
            g_logEnabled = true;   // sem .ini: grava o log para avisar onde procurou
        }

        if (g_logEnabled) DeleteFileA(g_logPath);
        Log("AmbulanceFullVC (sem CRT) carregado. PeopleToShowWarning=%d Hook=%d", g_fullPeople, hook ? 1 : 0);
        if (!iniFound) Log("AVISO: AmbulanceFullVC.ini nao encontrado. Procurei em: %s e %s (usando valores padrao)", asiDirIni, exeDirIni);
        else Log("ini usado: %s", iniPath);

        if (*(volatile u32*)VERSION_ADDR != VERSION_OK) { Log("ERRO: exe nao e o Vice City 1.0; mod desativado"); return TRUE; }
        if (hook) PatchCall(); else Log("Hook=0: patch NAO aplicado");
    }
    return TRUE;
}
