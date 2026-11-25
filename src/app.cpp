/* =============================================================================
- PROJETO: Bíblia Almeida ARC-1911 — Aplicativo para Windows
- OBJETIVO: Janela do Windows (WebView2) usando o MESMO motor em C do terminal
-           (motor/motor.biblia.c). O texto sai igual, letra por letra.
- AUTOR: Romulo CSR (GitHub: RomuloCSR)
- COMPILAR: powershell -ExecutionPolicy Bypass -File compilar.ps1
- =============================================================================
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cctype>
#include "WebView2.h"

/* ---------------------------------------------------------------------------
- 1. MOTOR (o mesmo do terminal, ligado em C)
- --------------------------------------------------------------------------- */
extern "C" {
    extern const char *pastas_at[39];
    extern const char *pastas_nt[27];
    extern const char *nomes_at[39];
    extern const char *nomes_nt[27];
    extern const int capitulos_at[39];
    extern const int capitulos_nt[27];

    void limpar_texto_verso(const char *origem, char *destino);
    void atualizar_ortografia(char *texto);
}

#define MAX_LINHA_APP 8192

/* ---------------------------------------------------------------------------
- 2. ESTADO DO APLICATIVO
- --------------------------------------------------------------------------- */
static HWND g_janela = nullptr;
static ICoreWebView2Controller *g_controller = nullptr;
static ICoreWebView2 *g_webview = nullptr;
static std::wstring g_pasta_exe;
static std::wstring g_pasta_dados;

static const wchar_t *TITULO_JANELA = L"Bíblia Almeida ARC-1911";
static const wchar_t *URL_GITHUB = L"https://github.com/RomuloCSR";

/* ---------------------------------------------------------------------------
- 3. UTILITÁRIOS (UTF-8, JSON, ARQUIVOS)
- --------------------------------------------------------------------------- */
static std::wstring utf8_para_w(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string w_para_utf8(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static void json_texto(std::string &saida, const char *s) {
    saida += '"';
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  saida += "\\\""; break;
            case '\\': saida += "\\\\"; break;
            case '\n': saida += "\\n"; break;
            case '\r': break;
            case '\t': saida += ' '; break;
            default:
                if (c < 0x20) saida += ' ';
                else saida += (char)c;
        }
    }
    saida += '"';
}

static bool ler_arquivo(const std::wstring &caminho, std::string &conteudo) {
    FILE *f = _wfopen(caminho.c_str(), L"rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    conteudo.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) conteudo.append(buf, n);
    fclose(f);
    return true;
}

static std::wstring pasta_do_exe(void) {
    wchar_t caminho[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, caminho, (DWORD)(sizeof(caminho) / sizeof(caminho[0])));
    std::wstring s(caminho, n);
    size_t barra = s.find_last_of(L"\\/");
    return barra == std::wstring::npos ? L"." : s.substr(0, barra);
}

/* ---------------------------------------------------------------------------
- 4. LEITURA DE CAPÍTULO (mesma lógica do terminal e do Android)
- --------------------------------------------------------------------------- */
static std::string capitulo_json(int testamento, int livro, int capitulo) {
    const char *pasta = nullptr;
    int total = 0;
    if (testamento == 1 && livro >= 0 && livro < 39) { pasta = pastas_at[livro]; total = capitulos_at[livro]; }
    if (testamento == 2 && livro >= 0 && livro < 27) { pasta = pastas_nt[livro]; total = capitulos_nt[livro]; }

    std::string saida = "{\"tipo\":\"capitulo\",\"t\":" + std::to_string(testamento) +
                        ",\"b\":" + std::to_string(livro) + ",\"c\":" + std::to_string(capitulo) + ",\"versos\":[";

    if (!pasta || capitulo < 1 || capitulo > total) {
        saida += "],\"erro\":\"Capítulo inválido.\"}";
        return saida;
    }

    std::wstring base = g_pasta_exe + L"\\textos\\" + utf8_para_w(pasta) + L"\\";
    wchar_t nome[16];
    std::string conteudo;
    swprintf(nome, 16, L"%02d.md", capitulo);
    if (!ler_arquivo(base + nome, conteudo)) {
        swprintf(nome, 16, L"%03d.md", capitulo);
        if (!ler_arquivo(base + nome, conteudo)) {
            saida += "],\"erro\":\"Arquivo do capítulo não encontrado. Verifique a pasta textos.\"}";
            return saida;
        }
    }

    static char linha[MAX_LINHA_APP];
    static char texto_limpo[MAX_LINHA_APP];
    bool primeiro = true;
    const char *cursor = conteudo.c_str();

    while (*cursor != '\0') {
        const char *fim = strchr(cursor, '\n');
        size_t len = fim ? (size_t)(fim - cursor) : strlen(cursor);
        if (len >= MAX_LINHA_APP - 1) len = MAX_LINHA_APP - 2;
        memcpy(linha, cursor, len);
        linha[len] = '\0';
        cursor = fim ? fim + 1 : cursor + len;

        if (strncmp(linha, "---", 3) == 0) break;
        if (linha[0] == '#' || linha[0] == '[') continue;

        int idx = 0;
        while (linha[idx] == '*' || linha[idx] == ' ') idx++;

        if (linha[idx] >= '1' && linha[idx] <= '9') {
            char num[20];
            int v = 0;
            while (linha[idx] >= '0' && linha[idx] <= '9' && v < 19) num[v++] = linha[idx++];
            num[v] = '\0';
            while (linha[idx] == '*' || linha[idx] == ' ') idx++;

            limpar_texto_verso(&linha[idx], texto_limpo);
            atualizar_ortografia(texto_limpo);

            if (!primeiro) saida += ',';
            saida += "{\"n\":";
            json_texto(saida, num);
            saida += ",\"t\":";
            json_texto(saida, texto_limpo);
            saida += '}';
            primeiro = false;
        } else if (strncmp(linha, "**", 2) == 0 && linha[idx] != '\0' && linha[idx] != '\r') {
            limpar_texto_verso(&linha[idx], texto_limpo);
            atualizar_ortografia(texto_limpo);

            if (!primeiro) saida += ',';
            saida += "{\"s\":";
            json_texto(saida, texto_limpo);
            saida += '}';
            primeiro = false;
        }
    }
    saida += "]}";
    return saida;
}

/* ---------------------------------------------------------------------------
- 5. ESTADO SALVO (onde parou, tamanho da letra, tema)
- --------------------------------------------------------------------------- */
struct Estado { int t = 1, b = 0, c = 1, fonte = 20; char tema[16] = "claro"; };

static std::wstring arquivo_estado(void) { return g_pasta_dados + L"\\estado.txt"; }

static Estado ler_estado(void) {
    Estado e;
    FILE *f = _wfopen(arquivo_estado().c_str(), L"r");
    if (f) {
        Estado lido;
        if (fscanf(f, "%d %d %d %d %15s", &lido.t, &lido.b, &lido.c, &lido.fonte, lido.tema) == 5) e = lido;
        fclose(f);
    }
    if (e.t != 1 && e.t != 2) e.t = 1;
    int max_livro = e.t == 1 ? 39 : 27;
    if (e.b < 0 || e.b >= max_livro) e.b = 0;
    int max_cap = e.t == 1 ? capitulos_at[e.b] : capitulos_nt[e.b];
    if (e.c < 1 || e.c > max_cap) e.c = 1;
    if (e.fonte < 14 || e.fonte > 40) e.fonte = 20;
    if (strcmp(e.tema, "claro") && strcmp(e.tema, "sepia") && strcmp(e.tema, "escuro")) strcpy(e.tema, "claro");
    return e;
}

static void salvar_estado(int t, int b, int c, int fonte, const std::string &tema) {
    FILE *f = _wfopen(arquivo_estado().c_str(), L"w");
    if (!f) return;
    std::string tema_ok = (tema == "sepia" || tema == "escuro") ? tema : "claro";
    fprintf(f, "%d %d %d %d %s\n", t, b, c, fonte, tema_ok.c_str());
    fclose(f);
}

static std::string init_json(void) {
    std::string s = "{\"tipo\":\"init\",\"at\":[";
    for (int i = 0; i < 39; i++) {
        if (i) s += ',';
        s += "{\"n\":"; json_texto(s, nomes_at[i]); s += ",\"c\":" + std::to_string(capitulos_at[i]) + "}";
    }
    s += "],\"nt\":[";
    for (int i = 0; i < 27; i++) {
        if (i) s += ',';
        s += "{\"n\":"; json_texto(s, nomes_nt[i]); s += ",\"c\":" + std::to_string(capitulos_nt[i]) + "}";
    }
    Estado e = ler_estado();
    s += "],\"estado\":{\"t\":" + std::to_string(e.t) + ",\"b\":" + std::to_string(e.b) +
         ",\"c\":" + std::to_string(e.c) + ",\"fonte\":" + std::to_string(e.fonte) + ",\"tema\":";
    json_texto(s, e.tema);
    s += "}}";
    return s;
}

/* ---------------------------------------------------------------------------
- 6. ÁREA DE TRANSFERÊNCIA (botão Copiar)
- --------------------------------------------------------------------------- */
static void copiar_texto(const std::wstring &texto) {
    if (!OpenClipboard(g_janela)) return;
    EmptyClipboard();
    size_t bytes = (texto.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void *p = GlobalLock(mem);
        memcpy(p, texto.c_str(), bytes);
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    }
    CloseClipboard();
}

/* ---------------------------------------------------------------------------
- 7. MENSAGENS DA TELA (HTML) PARA O MOTOR
-    init | cap|t|b|c | salvar|t|b|c|fonte|tema | copiar|texto | abrir|github
- --------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------
- 6b. ENVIAR (WhatsApp, Telegram, E-mail, X)
-     O texto completo sempre vai também para a área de transferência.
-     Só estes destinos fixos são aceitos; o endereço é montado aqui.
- --------------------------------------------------------------------------- */
static std::string url_codificar(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string r;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') r += (char)c;
        else { r += '%'; r += hex[c >> 4]; r += hex[c & 15]; }
    }
    return r;
}

static void compartilhar(const std::string &destino, const std::string &ref, const std::string &texto) {
    copiar_texto(utf8_para_w(texto));

    const size_t LIMITE = 1800; /* endereços muito longos não abrem em todo programa */
    std::string corpo = texto;
    if (url_codificar(corpo).size() > LIMITE)
        corpo = ref + "\n\n(O texto completo foi copiado. Cole aqui com Ctrl+V.)\n\nBíblia Almeida ARC-1911 — https://github.com/RomuloCSR";

    std::string url;
    if (destino == "whatsapp") {
        url = "https://wa.me/?text=" + url_codificar(corpo);
    } else if (destino == "telegram") {
        url = "https://t.me/share/url?url=" + url_codificar("https://github.com/RomuloCSR") +
              "&text=" + url_codificar(corpo);
    } else if (destino == "email") {
        std::string c = url_codificar(corpo), com_crlf;
        for (size_t i = 0; i < c.size(); i++) {
            if (c.compare(i, 3, "%0A") == 0) { com_crlf += "%0D%0A"; i += 2; }
            else com_crlf += c[i];
        }
        url = "mailto:?subject=" + url_codificar(ref + " - Bíblia Almeida ARC-1911") + "&body=" + com_crlf;
    } else if (destino == "x") {
        std::string curto = ref + " — Bíblia Almeida ARC-1911 (texto copiado, cole com Ctrl+V)";
        url = "https://x.com/intent/post?text=" + url_codificar(curto) + "&url=" + url_codificar("https://github.com/RomuloCSR");
    } else {
        return;
    }
    ShellExecuteW(g_janela, L"open", utf8_para_w(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void enviar_para_tela(const std::string &json) {
    if (g_webview) g_webview->PostWebMessageAsJson(utf8_para_w(json).c_str());
}

static std::vector<std::string> partes(const std::string &s, size_t max) {
    std::vector<std::string> v;
    size_t ini = 0;
    while (v.size() + 1 < max) {
        size_t p = s.find('|', ini);
        if (p == std::string::npos) break;
        v.push_back(s.substr(ini, p - ini));
        ini = p + 1;
    }
    v.push_back(s.substr(ini));
    return v;
}

static void tratar_mensagem(const std::wstring &msg_w) {
    std::string msg = w_para_utf8(msg_w);
    if (msg == "init") {
        enviar_para_tela(init_json());
        return;
    }
    if (msg.rfind("cap|", 0) == 0) {
        std::vector<std::string> p = partes(msg, 4);
        if (p.size() == 4) enviar_para_tela(capitulo_json(atoi(p[1].c_str()), atoi(p[2].c_str()), atoi(p[3].c_str())));
        return;
    }
    if (msg.rfind("salvar|", 0) == 0) {
        std::vector<std::string> p = partes(msg, 6);
        if (p.size() == 6) salvar_estado(atoi(p[1].c_str()), atoi(p[2].c_str()), atoi(p[3].c_str()), atoi(p[4].c_str()), p[5]);
        return;
    }
    if (msg.rfind("copiar|", 0) == 0) {
        copiar_texto(msg_w.substr(7));
        return;
    }
    if (msg == "abrir|github") {
        ShellExecuteW(g_janela, L"open", URL_GITHUB, nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (msg.rfind("enviar|", 0) == 0) {
        std::vector<std::string> p = partes(msg, 4);
        if (p.size() == 4) compartilhar(p[1], p[2], p[3]);
        return;
    }
}

/* ---------------------------------------------------------------------------
- 8. WEBVIEW2 (objetos COM de retorno)
- --------------------------------------------------------------------------- */
template <class I>
struct Retorno : public I {
    ULONG refs = 1;
    virtual ~Retorno() {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **saida) override {
        *saida = this;
        AddRef();
        return S_OK;
    }
};

static void erro_fatal(const wchar_t *texto) {
    MessageBoxW(g_janela, texto, TITULO_JANELA, MB_ICONERROR | MB_OK);
    PostQuitMessage(1);
}

struct RetornoMensagem : Retorno<ICoreWebView2WebMessageReceivedEventHandler> {
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) override {
        LPWSTR texto = nullptr;
        if (SUCCEEDED(args->TryGetWebMessageAsString(&texto)) && texto) {
            tratar_mensagem(texto);
            CoTaskMemFree(texto);
        }
        return S_OK;
    }
};

static void ajustar_tamanho(void) {
    if (!g_controller) return;
    RECT r;
    GetClientRect(g_janela, &r);
    g_controller->put_Bounds(r);
}

struct RetornoControlador : Retorno<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller *controller) override {
        if (FAILED(hr) || !controller) {
            erro_fatal(L"Não foi possível abrir a área de leitura (WebView2).");
            return S_OK;
        }
        g_controller = controller;
        g_controller->AddRef();
        g_controller->get_CoreWebView2(&g_webview);

        ICoreWebView2Settings *cfg = nullptr;
        if (SUCCEEDED(g_webview->get_Settings(&cfg)) && cfg) {
            cfg->put_AreDevToolsEnabled(FALSE);
            cfg->put_IsStatusBarEnabled(FALSE);
            cfg->put_IsZoomControlEnabled(FALSE);  /* o zoom é o A- / A+ do aplicativo */
            cfg->Release();
        }

        EventRegistrationToken token;
        RetornoMensagem *rm = new RetornoMensagem();
        g_webview->add_WebMessageReceived(rm, &token);
        rm->Release();

        ajustar_tamanho();

        std::string html;
        if (!ler_arquivo(g_pasta_exe + L"\\ui\\index.html", html)) {
            erro_fatal(L"Arquivo ui\\index.html não encontrado ao lado do Biblia.exe.");
            return S_OK;
        }
        g_webview->NavigateToString(utf8_para_w(html).c_str());
        return S_OK;
    }
};

struct RetornoAmbiente : Retorno<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment *ambiente) override {
        if (FAILED(hr) || !ambiente) {
            erro_fatal(L"O WebView2 da Microsoft não foi encontrado.\n\n"
                       L"Ele já vem no Windows 10 e 11. Se faltar, instale o\n"
                       L"\"Microsoft Edge WebView2 Runtime\" no site da Microsoft.");
            return S_OK;
        }
        RetornoControlador *rc = new RetornoControlador();
        ambiente->CreateCoreWebView2Controller(g_janela, rc);
        rc->Release();
        return S_OK;
    }
};

typedef HRESULT(STDAPICALLTYPE *CriarAmbienteFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
                                                 ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

static bool iniciar_webview(void) {
    HMODULE dll = LoadLibraryW((g_pasta_exe + L"\\WebView2Loader.dll").c_str());
    if (!dll) {
        erro_fatal(L"Arquivo WebView2Loader.dll não encontrado ao lado do Biblia.exe.");
        return false;
    }
    CriarAmbienteFn criar = (CriarAmbienteFn)(void *)GetProcAddress(dll, "CreateCoreWebView2EnvironmentWithOptions");
    if (!criar) {
        erro_fatal(L"WebView2Loader.dll inválido.");
        return false;
    }
    RetornoAmbiente *ra = new RetornoAmbiente();
    HRESULT hr = criar(nullptr, g_pasta_dados.c_str(), nullptr, ra);
    ra->Release();
    if (FAILED(hr)) {
        erro_fatal(L"O WebView2 da Microsoft não foi encontrado.\n\n"
                   L"Instale o \"Microsoft Edge WebView2 Runtime\" no site da Microsoft.");
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
- 9. JANELA DO WINDOWS
- --------------------------------------------------------------------------- */
static LRESULT CALLBACK proc_janela(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SIZE:
            ajustar_tamanho();
            return 0;
        case WM_DPICHANGED: {
            RECT *r = (RECT *)l;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO *mm = (MINMAXINFO *)l;
            mm->ptMinTrackSize.x = 380;
            mm->ptMinTrackSize.y = 480;
            return 0;
        }
        case WM_DESTROY:
            if (g_controller) { g_controller->Close(); g_controller->Release(); g_controller = nullptr; }
            if (g_webview) { g_webview->Release(); g_webview = nullptr; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* Barra de título na cor do topo (#2C3E50) com letras brancas. Só no Windows 11; no 10 fica a padrão. */
static void pintar_barra_de_titulo(HWND h) {
    typedef HRESULT(WINAPI *FnDwm)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    FnDwm f = dwm ? (FnDwm)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute") : nullptr;
    if (!f) return;
    COLORREF fundo = RGB(0x2C, 0x3E, 0x50), letra = RGB(0xFF, 0xFF, 0xFF);
    f(h, 35 /* DWMWA_CAPTION_COLOR */, &fundo, sizeof(fundo));
    f(h, 36 /* DWMWA_TEXT_COLOR */, &letra, sizeof(letra));
}

/* Modo portátil: os dados (onde parou, letra, tema) ficam na pasta "dados" ao lado do
   Biblia.exe. Assim o app roda do pendrive e não deixa arquivos no computador.
   Só se a pasta não aceitar gravação (ex.: CD) os dados vão para %LOCALAPPDATA%. */
static std::wstring escolher_pasta_dados(void) {
    std::wstring portatil = g_pasta_exe + L"\\dados";
    CreateDirectoryW(portatil.c_str(), nullptr);
    std::wstring teste = portatil + L"\\.teste";
    HANDLE h = CreateFileW(teste.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return portatil;
    }
    wchar_t local[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    std::wstring alt = (n > 0 && n < MAX_PATH) ? std::wstring(local) + L"\\BibliaARC1911" : portatil;
    CreateDirectoryW(alt.c_str(), nullptr);
    return alt;
}

static void ativar_dpi(void) {
    typedef BOOL(WINAPI *FnDpi)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    FnDpi f = u ? (FnDpi)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : nullptr;
    if (f) f((HANDLE)-4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int mostrar) {
    ativar_dpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_pasta_exe = pasta_do_exe();
    g_pasta_dados = escolher_pasta_dados();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc_janela;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, L"ICONE");
    wc.hbrBackground = CreateSolidBrush(RGB(0x2C, 0x3E, 0x50));
    wc.lpszClassName = L"BibliaARC1911";
    RegisterClassExW(&wc);

    /* Janela centralizada, do tamanho certo para a tela (sem ficar atrás da barra de tarefas) */
    RECT area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    int area_l = area.right - area.left, area_a = area.bottom - area.top;
    int larg = area_l * 70 / 100, alt = area_a * 92 / 100;
    if (larg < 420) larg = area_l;
    if (larg > 1100) larg = 1100;
    int x = area.left + (area_l - larg) / 2, y = area.top + (area_a - alt) / 2;

    g_janela = CreateWindowExW(0, wc.lpszClassName, TITULO_JANELA, WS_OVERLAPPEDWINDOW,
                               x, y, larg, alt, nullptr, nullptr, inst, nullptr);
    pintar_barra_de_titulo(g_janela);
    ShowWindow(g_janela, mostrar);
    UpdateWindow(g_janela);

    if (!iniciar_webview()) {
        CoUninitialize();
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
