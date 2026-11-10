// AmbulanceFull.asi - GTA III v1.0 PC (gta3.exe 2,383,872 bytes)
// Mostra "Ambulance full!!" (A_FULL) quando a ambulancia tem N pessoas (incluindo motorista),
// somente enquanto a missao de paramedico esta em andamento.
// Versao SEM CRT (sem fopen/printf/strcpy): so usa a API do Windows. DLL minima.
//
// Compilar: ver main.yml (cl /LD /GS- + link /NODEFAULTLIB /ENTRY:DllMain).

#include <windows.h>
#include <stdarg.h>

typedef unsigned char      u8;
typedef unsigned int       u32;
typedef unsigned short     u16;
typedef unsigned long      uptr;

// ---------------- Enderecos (GTA III 1.0) ----------------
static const uptr ADDR_HOOK_CALL      = 0x48E42F; // call CMessages::Display
static const uptr ADDR_MSG_DISPLAY    = 0x529800; // CMessages::Display
static const uptr ADDR_MSG_JUMPQ      = 0x529A10; // CMessages::AddMessageJumpQ(wchar*, uint32 time, uint16 flag)
static const uptr ADDR_TEXT_GET       = 0x52C5A0; // CText::Get(const char*) [thiscall]
static const uptr ADDR_THETEXT        = 0x941520; // TheText
static const uptr ADDR_FIND_PLAYERVEH = 0x4A10C0; // FindPlayerVehicle()
static const uptr ADDR_TIMER_MS       = 0x885B48; // CTimer::m_snTimeInMilliseconds
static const uptr ADDR_MSG_SLOT0      = 0x8786E0; // CMessages::BriefMessages[0]
static const uptr ADDR_GAMESTATE      = 0x8F5838; // gGameState (9 = jogando)

// ---------------- Offsets ----------------
static const int OFF_MODEL_INDEX   = 0x5C;   // CEntity::m_modelIndex (int16)
static const int OFF_DRIVER        = 0x1A4;  // CVehicle::pDriver
static const int OFF_PASSENGERS    = 0x1A8;  // CVehicle::pPassengers[8]
static const int OFF_NUM_PASSENGER = 0x1C8;  // CVehicle::m_nNumPassengers
static const int OFF_NUM_GETTINGIN = 0x1C9;  // CVehicle::m_nNumGettingIn
static const int OFF_NUM_MAX_PASS  = 0x1CC;  // CVehicle::m_nNumMaxPassengers
static const int MI_AMBULANCE      = 106;    // 0x6A
static const int GS_PLAYING_GAME   = 9;  // 8 = GS_INIT_PLAYING_GAME (transitorio), 9 = jogando

typedef void*    (__cdecl    *FindPlayerVehicle_t)();
typedef wchar_t* (__fastcall *TextGet_t)(void* thisPtr, int edxDummy, const char* key);
typedef void     (__cdecl    *AddMessageJumpQ_t)(wchar_t* text, u32 time, u16 flag);

static int  g_fullPeople = 4;
static bool g_logEnabled = false;
static char g_logPath[MAX_PATH];

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

static void ShowAmbulanceFull()
{
    wchar_t* text = ((TextGet_t)ADDR_TEXT_GET)((void*)ADDR_THETEXT, 0, "A_FULL");
    if (!text) { Log("ERRO: TheText.Get(A_FULL) retornou NULL"); return; }

    wchar_t* cur   = *(wchar_t**)ADDR_MSG_SLOT0;
    u32      time  = *(u32*)(ADDR_MSG_SLOT0 + 8);
    u32      start = *(u32*)(ADDR_MSG_SLOT0 + 12);
    u32      now   = *(u32*)ADDR_TIMER_MS;
    if (cur == text && (now - start) + 300 < time)
        return;

    ((AddMessageJumpQ_t)ADDR_MSG_JUMPQ)(text, 1500, 1);
    Log("AddMessageJumpQ: text=%08X slot0.antes=%08X time=%u start=%u now=%u", (u32)text, (u32)cur, time, start, now);
}


// ---------------- Deteccao da missao de paramedico ----------------
static const uptr ADDR_ACTIVE_SCRIPTS  = 0x8E2BF4; // CTheScripts::pActiveScripts (lista de threads)
static const uptr ADDR_SCRIPTSPACE     = 0x74B248; // CTheScripts::ScriptSpace (variaveis globais)
static const int  SCR_OFF_NAME         = 8;        // CRunningScript::m_abScriptName
static const int  VAR_ONMISSION        = 0x033C;   // flag "jogador em missao"
static const int  VAR_AMB_ACTIVE       = 0x05CC;   // flag "missao de paramedico ativa" (0 ao terminar)

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

// Missao de paramedico em andamento: thread AMBULAN existe, flag de missao de paramedico e flag "em missao" != 0.
static bool AmbulanceMissionActive(u8** outThread)
{
    u8* th = FindAmbulanceThread();
    if (outThread) *outThread = th;
    return th &&
           *(volatile int*)(ADDR_SCRIPTSPACE + VAR_AMB_ACTIVE) != 0 &&
           *(volatile int*)(ADDR_SCRIPTSPACE + VAR_ONMISSION)  != 0;
}

static void Tick()
{
    static void* lastVeh = (void*)1;
    static int   lastModel = -2, lastPeople = -2, lastNum = -2, lastGetIn = -2;

    void* veh = ((FindPlayerVehicle_t)ADDR_FIND_PLAYERVEH)();
    if (!veh) {
        if (lastVeh != 0) { Log("jogador nao esta em veiculo"); lastVeh = 0; }
        return;
    }

    u8*  v      = (u8*)veh;
    int  model  = *(short*)(v + OFF_MODEL_INDEX);
    void* driver = *(void**)(v + OFF_DRIVER);
    int  num    = v[OFF_NUM_PASSENGER];
    int  getIn  = (signed char)v[OFF_NUM_GETTINGIN];
    int  max    = v[OFF_NUM_MAX_PASS];
    int  people = (driver ? 1 : 0) + num;

    if (veh != lastVeh || model != lastModel || num != lastNum || getIn != lastGetIn || people != lastPeople) {
        Log("veh=%08X model=%d driver=%08X numPassengers=%d gettingIn=%d max=%d people=%d alvo=%d",
            (u32)veh, model, (u32)driver, num, getIn, max, people, g_fullPeople);
        lastVeh = veh; lastModel = model; lastNum = num; lastGetIn = getIn; lastPeople = people;
    }

    if (model != MI_AMBULANCE) return;
    if (people >= g_fullPeople && AmbulanceMissionActive(0))   // so durante a missao de paramedico
        ShowAmbulanceFull();
}

static __declspec(noinline) void __cdecl SafeTick()
{
    // So age durante o jogo normal (nao em loading, menu, inicializacao de save)
    if (*(volatile int*)ADDR_GAMESTATE != GS_PLAYING_GAME)
        return;
    Tick();
}

static uptr g_origDisplay = ADDR_MSG_DISPLAY;

// Preserva todos os registradores/flags, roda o Tick e salta para a CMessages::Display original.
static __declspec(naked) void HookedDisplay()
{
    __asm {
        pushfd
        pushad
        call SafeTick
        popad
        popfd
        jmp  dword ptr [g_origDisplay]
    }
}

static bool PatchCall()
{
    u8* p = (u8*)ADDR_HOOK_CALL;
    if (p[0] != 0xE8) { Log("ERRO: nao e um CALL (exe nao e v1.0?)"); return false; }
    int rel = *(int*)(p + 1);
    uptr target = (uptr)(p + 5 + rel);
    if (target != ADDR_MSG_DISPLAY) { Log("ERRO: destino do CALL = %08X", (u32)target); return false; }

    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) { Log("ERRO: VirtualProtect falhou"); return false; }
    *(int*)(p + 1) = (int)((uptr)&HookedDisplay - (uptr)(p + 5));
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

        // Pasta do .asi e pasta do gta3.exe: o .ini e o .log ficam ao lado do .asi;
        // se o .ini nao estiver la, procura ao lado do gta3.exe.
        char asiDirIni[MAX_PATH], exeDirIni[MAX_PATH], asiDirLog[MAX_PATH];
        GetModuleFileNameA(hModule, asiDirIni, MAX_PATH);
        for (int i = 0; i < MAX_PATH; ++i) { asiDirLog[i] = asiDirIni[i]; if (!asiDirIni[i]) break; }
        GetModuleFileNameA(NULL, exeDirIni, MAX_PATH);
        SetFileNameInPath(asiDirIni, "AmbulanceFull.ini");
        SetFileNameInPath(asiDirLog, "AmbulanceFull.log");
        SetFileNameInPath(exeDirIni, "AmbulanceFull.ini");
        for (int i = 0; i < MAX_PATH; ++i) { g_logPath[i] = asiDirLog[i]; if (!asiDirLog[i]) break; }

        const char* iniPath = asiDirIni;
        bool iniFound = GetFileAttributesA(asiDirIni) != INVALID_FILE_ATTRIBUTES;
        if (!iniFound && GetFileAttributesA(exeDirIni) != INVALID_FILE_ATTRIBUTES) { iniPath = exeDirIni; iniFound = true; }

        bool hook = true;
        if (iniFound) {
            g_fullPeople = GetPrivateProfileIntA("Settings", "PeopleToShowWarning", 4, iniPath);
            if (g_fullPeople < 1) g_fullPeople = 1;
            g_logEnabled = GetPrivateProfileIntA("Settings", "Log", 0, iniPath) != 0;
            hook         = GetPrivateProfileIntA("Settings", "Hook", 1, iniPath) != 0;

        } else {
            g_logEnabled = true;   // sem .ini: grava o log para avisar onde procurou
        }

        if (g_logEnabled) DeleteFileA(g_logPath);
        Log("AmbulanceFull (sem CRT) carregado. PeopleToShowWarning=%d Hook=%d", g_fullPeople, hook ? 1 : 0);
        if (!iniFound) Log("AVISO: AmbulanceFull.ini nao encontrado. Procurei em: %s e %s (usando valores padrao)", asiDirIni, exeDirIni);
        else Log("ini usado: %s", iniPath);
        if (hook) PatchCall(); else Log("Hook=0: patch NAO aplicado");
    }
    return TRUE;
}
