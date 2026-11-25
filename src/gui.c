/*
 * ============================================================================
 *  NEXUS - Interface grafica (janela do Windows, Win32 + GDI)
 * ============================================================================
 *
 *  Substitui terminal.c + nexus.c por uma janela desenhada com a API do
 *  Windows. As REGRAS do jogo continuam exatamente as mesmas: este arquivo
 *  so chama as funcoes de jogo.c, mapa.c e missoes.c.
 *
 *    - teclado: mesmas teclas da versao terminal (setas tambem movem)
 *    - mouse:   todos os botoes podem ser clicados
 *
 *  Compilar (MinGW):
 *    gcc -std=c99 -Wall -Wextra src/gui.c src/jogo.c src/mapa.c src/missoes.c
 *        -o nexus_gui.exe -mwindows -lgdi32 -lmsimg32 -lm
 * ============================================================================
 */

#define WINVER       0x0501
#define _WIN32_WINNT 0x0501
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <math.h>
#include <string.h>
#include <wchar.h>

#include "jogo.h"
#include "mapa.h"
#include "missoes.h"

#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY 5
#endif

/* ----------------------------------------------------------------------------
 * DIMENSOES (em unidades logicas; a janela e escalada conforme o DPI)
 * -------------------------------------------------------------------------- */
#define LARG     1120
#define ALT      680
#define TILE     42
#define PI_      3.14159265358979
#define SEM_COR  0xFFFFFFFFu
#define MAX_BOTOES 24

/* ----------------------------------------------------------------------------
 * PALETA
 * -------------------------------------------------------------------------- */
#define COR_FUNDO     RGB(7, 10, 18)
#define COR_PAINEL    RGB(16, 22, 38)
#define COR_PAINEL2   RGB(22, 31, 52)
#define COR_BORDA     RGB(40, 54, 88)
#define COR_CHAO      RGB(13, 19, 33)
#define COR_GRADE     RGB(20, 29, 49)
#define COR_PAREDE    RGB(30, 41, 68)
#define COR_PAREDE_T  RGB(58, 78, 122)
#define COR_PAREDE_S  RGB(17, 24, 42)
#define COR_CIANO     RGB(56, 225, 255)
#define COR_VERDE     RGB(61, 220, 151)
#define COR_VERMELHO  RGB(255, 92, 122)
#define COR_AMARELO   RGB(255, 209, 102)
#define COR_OURO      RGB(246, 195, 67)
#define COR_ROXO      RGB(180, 140, 255)
#define COR_LARANJA   RGB(255, 150, 90)
#define COR_TEXTO     RGB(230, 237, 247)
#define COR_MUDO      RGB(138, 151, 177)
#define COR_BRANCO    RGB(255, 255, 255)

/* ----------------------------------------------------------------------------
 * ESTADO DA INTERFACE GRAFICA
 * -------------------------------------------------------------------------- */
typedef struct { int x, y, w, h; char tecla; } Botao;

static Jogo g_jogo;                  /* o MESMO estado usado pela versao terminal */
static HWND g_janela;
static double g_escala = 1.0;
static int g_largDisp, g_altDisp;    /* tamanho real em pixels                   */
static HDC g_dcBuffer, g_dcEscuro;
static HBITMAP g_bmpBuffer;
static unsigned long g_tick, g_tickTela;
static double g_px, g_py;            /* posicao desenhada do jogador (suave)     */
static int g_dirX = 1, g_dirY = 0;   /* para onde o jogador olha                 */
static int g_mouseX = -1, g_mouseY = -1;
static Botao g_botoes[MAX_BOTOES];
static int g_nBotoes;
static wchar_t g_aviso[200];
static COLORREF g_avisoCor;
static unsigned long g_avisoTick;

static HFONT fTitulo, fNumero, fH1, fH2, fBotao, fCorpo, fCorpoB, fPeq, fPeqB,
             fMono, fMonoB, fCitacao;

/* ============================================================================
 *  UTILITARIOS DE DESENHO
 * ========================================================================== */
static COLORREF misturar(COLORREF a, COLORREF b, double t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static double pulso(double velocidade) {
    return (sin(g_tick * velocidade) + 1.0) / 2.0;
}

static void retangulo(HDC dc, int x, int y, int w, int h, COLORREF cor) {
    RECT r;
    HBRUSH b = CreateSolidBrush(cor);
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    FillRect(dc, &r, b);
    DeleteObject(b);
}

/* Prepara pincel/caneta: fundo SEM_COR = vazado, espessura 0 = sem borda */
static void selecionar(HDC dc, COLORREF fundo, COLORREF borda, int esp,
                       HBRUSH *b, HPEN *p, HGDIOBJ *ob, HGDIOBJ *op) {
    *b = (fundo == SEM_COR) ? NULL : CreateSolidBrush(fundo);
    *p = (esp > 0) ? CreatePen(PS_SOLID, esp, borda) : NULL;
    *ob = SelectObject(dc, *b ? (HGDIOBJ)*b : GetStockObject(HOLLOW_BRUSH));
    *op = SelectObject(dc, *p ? (HGDIOBJ)*p : GetStockObject(NULL_PEN));
}

static void liberar(HDC dc, HBRUSH b, HPEN p, HGDIOBJ ob, HGDIOBJ op) {
    SelectObject(dc, ob);
    SelectObject(dc, op);
    if (b) DeleteObject(b);
    if (p) DeleteObject(p);
}

static void caixa(HDC dc, int x, int y, int w, int h, int raio,
                  COLORREF fundo, COLORREF borda, int esp) {
    HBRUSH b; HPEN p; HGDIOBJ ob, op;
    selecionar(dc, fundo, borda, esp, &b, &p, &ob, &op);
    RoundRect(dc, x, y, x + w + (esp ? 0 : 1), y + h + (esp ? 0 : 1), raio, raio);
    liberar(dc, b, p, ob, op);
}

static void elipse(HDC dc, int cx, int cy, int rx, int ry,
                   COLORREF fundo, COLORREF borda, int esp) {
    HBRUSH b; HPEN p; HGDIOBJ ob, op;
    selecionar(dc, fundo, borda, esp, &b, &p, &ob, &op);
    Ellipse(dc, cx - rx, cy - ry, cx + rx + (esp ? 0 : 1), cy + ry + (esp ? 0 : 1));
    liberar(dc, b, p, ob, op);
}

static void linha(HDC dc, int x1, int y1, int x2, int y2, COLORREF cor, int esp) {
    HPEN p = CreatePen(PS_SOLID, esp, cor);
    HGDIOBJ op = SelectObject(dc, p);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, op);
    DeleteObject(p);
}

/* Brilho "neon": contornos que se afastam e vao sumindo */
static void brilho(HDC dc, int x, int y, int w, int h, int raio,
                   COLORREF cor, double forca, int camadas) {
    int i;
    for (i = camadas; i >= 1; i--) {
        double t = forca * (1.0 - (double)i / (camadas + 1));
        caixa(dc, x - 2 * i, y - 2 * i, w + 4 * i, h + 4 * i, raio + 4 * i,
              SEM_COR, misturar(COR_FUNDO, cor, t * 0.55), 2);
    }
}

static void brilhoCirculo(HDC dc, int cx, int cy, int r, COLORREF cor,
                          double forca, int camadas) {
    int i;
    for (i = camadas; i >= 1; i--) {
        double t = forca * (1.0 - (double)i / (camadas + 1));
        elipse(dc, cx, cy, r + 3 * i, r + 3 * i, SEM_COR,
               misturar(COR_FUNDO, cor, t * 0.6), 3);
    }
}

static void texto(HDC dc, const wchar_t *s, int x, int y, int w, int h,
                  HFONT f, COLORREF cor, UINT formato) {
    RECT r;
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, cor);
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    DrawTextW(dc, s, -1, &r, formato | DT_NOPREFIX);
    SelectObject(dc, of);
}

#define T_ESQ   (DT_LEFT | DT_SINGLELINE | DT_VCENTER)
#define T_CENTRO (DT_CENTER | DT_SINGLELINE | DT_VCENTER)
#define T_DIR   (DT_RIGHT | DT_SINGLELINE | DT_VCENTER)
#define T_PARAG (DT_LEFT | DT_WORDBREAK)
#define T_PARAG_C (DT_CENTER | DT_WORDBREAK)

static int larguraTexto(HDC dc, const wchar_t *s, HFONT f) {
    SIZE sz;
    HGDIOBJ of = SelectObject(dc, f);
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, of);
    return sz.cx;
}

/* Converte os textos ASCII de missoes.c, tirando espacos e quebras extras */
static void paraWide(const char *origem, wchar_t *destino, int tamanho) {
    int n;
    while (*origem == ' ' || *origem == '\n') origem++;
    MultiByteToWideChar(CP_ACP, 0, origem, -1, destino, tamanho);
    n = (int)wcslen(destino);
    while (n > 0 && (destino[n - 1] == L'\n' || destino[n - 1] == L' ')) destino[--n] = 0;
}

/* Etiqueta arredondada alinhada pela direita; devolve a largura */
static int etiqueta(HDC dc, int xDireita, int y, const wchar_t *s, COLORREF cor) {
    int w = larguraTexto(dc, s, fPeqB) + 24;
    caixa(dc, xDireita - w, y, w, 26, 26, misturar(COR_PAINEL, cor, 0.16), misturar(COR_PAINEL, cor, 0.6), 1);
    texto(dc, s, xDireita - w, y, w, 26, fPeqB, cor, T_CENTRO);
    return w;
}

/* Tecla desenhada como uma "tecla de teclado" */
static void teclaCap(HDC dc, int x, int y, int tam, const wchar_t *k, COLORREF cor) {
    int w = tam;
    if (wcslen(k) > 1) w = larguraTexto(dc, k, fPeqB) + 16;
    caixa(dc, x, y + 2, w, tam, 8, misturar(COR_FUNDO, cor, 0.25), SEM_COR, 0);
    caixa(dc, x, y, w, tam - 2, 8, misturar(COR_PAINEL2, cor, 0.22), misturar(COR_BORDA, cor, 0.7), 1);
    texto(dc, k, x, y, w, tam - 2, wcslen(k) > 1 ? fPeqB : fMonoB, cor, T_CENTRO);
}

static int mouseSobre(int x, int y, int w, int h) {
    return g_mouseX >= x && g_mouseX < x + w && g_mouseY >= y && g_mouseY < y + h;
}

/* Botao clicavel: registra a tecla equivalente para o clique do mouse */
static void botao(HDC dc, int x, int y, int w, int h, const wchar_t *k,
                  const wchar_t *rotulo, COLORREF cor, char codigo, int destaque) {
    int sobre = mouseSobre(x, y, w, h);
    COLORREF fundo = misturar(COR_PAINEL2, cor, destaque ? (sobre ? 0.32 : 0.2) : (sobre ? 0.16 : 0.0));
    COLORREF borda = (sobre || destaque) ? cor : COR_BORDA;

    if (sobre || destaque) brilho(dc, x, y, w, h, 14, cor, sobre ? 0.9 : 0.5 + 0.3 * pulso(0.1), 3);
    caixa(dc, x, y, w, h, 14, fundo, borda, 2);
    if (k) {
        teclaCap(dc, x + 14, y + (h - 32) / 2, 32, k, cor);
        texto(dc, rotulo, x + 58, y, w - 66, h, fBotao, COR_TEXTO, T_ESQ);
    } else {
        texto(dc, rotulo, x, y, w, h, fBotao, COR_TEXTO, T_CENTRO);
    }
    if (g_nBotoes < MAX_BOTOES) {
        g_botoes[g_nBotoes].x = x; g_botoes[g_nBotoes].y = y;
        g_botoes[g_nBotoes].w = w; g_botoes[g_nBotoes].h = h;
        g_botoes[g_nBotoes].tecla = codigo;
        g_nBotoes++;
    }
}

/* Escurece tudo o que ja foi desenhado (fundo das janelas modais) */
static void escurecer(HDC dc, int alfa) {
    XFORM antigo;
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER; bf.BlendFlags = 0;
    bf.SourceConstantAlpha = (BYTE)alfa; bf.AlphaFormat = 0;
    GetWorldTransform(dc, &antigo);
    ModifyWorldTransform(dc, &antigo, MWT_IDENTITY);
    AlphaBlend(dc, 0, 0, g_largDisp, g_altDisp, g_dcEscuro, 0, 0, 1, 1, bf);
    SetWorldTransform(dc, &antigo);
}

/* Valor logico V / F / ? como selo colorido */
static void seloValor(HDC dc, int x, int y, wchar_t v) {
    wchar_t s[2];
    COLORREF cor = (v == L'V') ? COR_VERDE : (v == L'F') ? COR_LARANJA : COR_MUDO;
    s[0] = v; s[1] = 0;
    caixa(dc, x, y, 40, 32, 10, misturar(COR_PAINEL, cor, v == L'?' ? 0.06 : 0.2),
          misturar(COR_PAINEL, cor, 0.7), 1);
    texto(dc, s, x, y, 40, 32, fMonoB, cor, T_CENTRO);
}

/* ============================================================================
 *  ICONES E PECAS DO MAPA
 * ========================================================================== */
static void desenharChave(HDC dc, int cx, int cy, int s, COLORREF cor) {
    int esp = s / 8 < 2 ? 2 : s / 8;
    elipse(dc, cx - s * 28 / 100, cy, s * 18 / 100, s * 18 / 100, SEM_COR, cor, esp);
    retangulo(dc, cx - s * 10 / 100, cy - esp / 2, s * 52 / 100, esp, cor);
    retangulo(dc, cx + s * 26 / 100, cy, s * 8 / 100 + 1, s * 18 / 100, cor);
    retangulo(dc, cx + s * 36 / 100, cy, s * 7 / 100 + 1, s * 12 / 100, cor);
}

static void iconePista(HDC dc, int cx, int cy, int r) {
    brilhoCirculo(dc, cx, cy, r, COR_AMARELO, 0.5 + 0.5 * pulso(0.12), 3);
    elipse(dc, cx, cy, r, r, misturar(COR_FUNDO, COR_AMARELO, 0.22), COR_AMARELO, 2);
    texto(dc, L"?", cx - r, cy - r, 2 * r, 2 * r, r > 12 ? fH2 : fPeqB, COR_AMARELO, T_CENTRO);
}

/* estado: 0 bloqueado, 1 disponivel, 2 corrigido */
static void monitor(HDC dc, int x, int y, int w, int h, int estado, const wchar_t *rotulo) {
    COLORREF cor = estado == 0 ? RGB(110, 122, 146) : estado == 1 ? COR_CIANO : COR_VERDE;
    COLORREF tela = misturar(RGB(4, 7, 13), cor, estado == 0 ? 0.08 : 0.2);
    int yy;

    if (estado == 1) brilho(dc, x + 3, y + 4, w - 6, h - 12, 10, cor, 0.4 + 0.5 * pulso(0.15), 3);
    caixa(dc, x + 3, y + 4, w - 6, h - 12, 9, RGB(26, 34, 56), misturar(COR_BORDA, cor, 0.6), 2);
    caixa(dc, x + 8, y + 9, w - 16, h - 22, 5, tela, SEM_COR, 0);
    for (yy = y + 11; yy < y + h - 14; yy += 4) {
        retangulo(dc, x + 9, yy, w - 18, 1, misturar(tela, cor, 0.12));
    }
    texto(dc, rotulo, x + 8, y + 9, w - 16, h - 22, h > 34 ? fMonoB : fPeqB, cor, T_CENTRO);
    retangulo(dc, x + w / 2 - 5, y + h - 8, 10, 4, COR_BORDA);
    retangulo(dc, x + w / 2 - 12, y + h - 4, 24, 3, COR_BORDA);
    elipse(dc, x + w - 10, y + h - 12, 2, 2, estado == 0 ? COR_VERMELHO : cor, SEM_COR, 0);
}

static void portaFechada(HDC dc, int x, int y, int s) {
    COLORREF borda = misturar(COR_VERMELHO, COR_BRANCO, 0.15 * pulso(0.1));
    int i, cx = x + s / 2, cy = y + s / 2;
    caixa(dc, x + s / 7, y + 2, s - 2 * (s / 7), s - 4, 6, RGB(58, 20, 34), borda, 2);
    for (i = 1; i <= 3; i++) {
        retangulo(dc, x + s / 7 + 4, y + i * s / 4, s - 2 * (s / 7) - 8, 1, misturar(RGB(58, 20, 34), COR_VERMELHO, 0.4));
    }
    elipse(dc, cx, cy - s / 10, s / 9, s / 9, SEM_COR, COR_AMARELO, 2);
    caixa(dc, cx - s / 6, cy - s / 20, s / 3, s / 4, 4, COR_AMARELO, SEM_COR, 0);
}

static void portaAberta(HDC dc, int x, int y, int s) {
    retangulo(dc, x + 1, y + 1, s - 2, s - 2, misturar(COR_CHAO, COR_VERDE, 0.1));
    retangulo(dc, x + 2, y, 4, s, COR_VERDE);
    retangulo(dc, x + s - 6, y, 4, s, COR_VERDE);
    retangulo(dc, x + 6, y + s / 2, s - 12, 1, misturar(COR_CHAO, COR_VERDE, 0.35));
}

static void jogador(HDC dc, int cx, int cy, int r) {
    elipse(dc, cx, cy + r + 1, r - 1, r / 4 + 1, RGB(4, 6, 12), SEM_COR, 0);
    brilhoCirculo(dc, cx, cy, r, COR_CIANO, 0.7, 2);
    elipse(dc, cx, cy, r, r, RGB(18, 58, 80), COR_CIANO, 2);
    caixa(dc, cx - r * 6 / 10 + g_dirX * r / 5, cy - r * 4 / 10 + g_dirY * r / 5,
          r * 12 / 10, r * 6 / 10, r / 2, misturar(COR_CIANO, COR_BRANCO, 0.45), SEM_COR, 0);
    retangulo(dc, cx - 1, cy - r - 6, 2, 6, COR_CIANO);
    elipse(dc, cx, cy - r - 7, 3, 3, misturar(COR_AMARELO, COR_BRANCO, pulso(0.2) * 0.5), SEM_COR, 0);
}

static void tileParede(HDC dc, int x, int y, int s, int topo, int base) {
    retangulo(dc, x, y, s, s, COR_PAREDE);
    if (topo) retangulo(dc, x, y, s, 5, COR_PAREDE_T);
    if (base) retangulo(dc, x, y + s - 4, s, 4, COR_PAREDE_S);
}

static void escudo(HDC dc, int cx, int cy, int s, COLORREF cor) {
    POINT pts[6];
    HBRUSH b; HPEN p; HGDIOBJ ob, op;
    pts[0].x = cx;          pts[0].y = cy - s / 2;
    pts[1].x = cx + s / 2;  pts[1].y = cy - s / 3;
    pts[2].x = cx + s / 2;  pts[2].y = cy + s / 10;
    pts[3].x = cx;          pts[3].y = cy + s / 2;
    pts[4].x = cx - s / 2;  pts[4].y = cy + s / 10;
    pts[5].x = cx - s / 2;  pts[5].y = cy - s / 3;
    selecionar(dc, misturar(COR_PAINEL, cor, 0.18), cor, 3, &b, &p, &ob, &op);
    Polygon(dc, pts, 6);
    liberar(dc, b, p, ob, op);
    elipse(dc, cx, cy - s / 12, s / 9, s / 9, SEM_COR, cor, 3);
    caixa(dc, cx - s / 6, cy, s / 3, s / 4, 5, cor, SEM_COR, 0);
}

/* ============================================================================
 *  PARTES COMUNS DAS TELAS
 * ========================================================================== */
static void fundoAnimado(HDC dc) {
    int i, x, y;
    retangulo(dc, 0, 0, LARG, ALT, COR_FUNDO);
    for (x = 0; x < LARG; x += 48) retangulo(dc, x, 0, 1, ALT, RGB(11, 16, 28));
    for (y = 0; y < ALT; y += 48) retangulo(dc, 0, y, LARG, 1, RGB(11, 16, 28));
    for (i = 0; i < 40; i++) {
        int px = (int)((i * 211 + g_tick * (1 + i % 3) / 2) % LARG);
        int py = (int)((i * 97 * 7 + i * i * 13) % ALT);
        int tam = 1 + i % 2;
        retangulo(dc, px, py, tam, tam, misturar(COR_FUNDO, i % 4 ? COR_CIANO : COR_ROXO, 0.25 + 0.3 * ((i * 37) % 10) / 10.0));
    }
}

static const wchar_t *nomeFase(int fase) {
    return fase == 1 ? L"Central da Verdade" : fase == 2 ? L"Setor de Privacidade" : L"Núcleo da Justiça";
}

static void barraTopo(HDC dc) {
    const EstadoJogo *p = &g_jogo.partida;
    wchar_t buf[64];
    int i, x;
    COLORREF cores[3];
    int tem[3];

    retangulo(dc, 0, 0, LARG, 64, COR_PAINEL);
    retangulo(dc, 0, 63, LARG, 1, COR_BORDA);
    texto(dc, L"NEXUS", 26, 0, 140, 64, fH1, COR_CIANO, T_ESQ);
    retangulo(dc, 150, 20, 1, 24, COR_BORDA);
    wcscpy(buf, L"FASE ");
    buf[5] = (wchar_t)(L'0' + p->faseAtual); buf[6] = 0;
    texto(dc, buf, 168, 12, 200, 20, fPeqB, COR_MUDO, T_ESQ);
    texto(dc, nomeFase(p->faseAtual), 168, 30, 300, 24, fCorpoB, COR_TEXTO, T_ESQ);

    /* chaves */
    cores[0] = COR_OURO; cores[1] = COR_ROXO; cores[2] = COR_CIANO;
    tem[0] = p->chaveDaVerdade; tem[1] = p->chaveDaPrivacidade; tem[2] = p->chaveDaJustica;
    x = LARG - 30 - 3 * 50;
    for (i = 0; i < 3; i++) {
        int cx = x + i * 50 + 22;
        caixa(dc, cx - 21, 12, 42, 40, 10, tem[i] ? misturar(COR_PAINEL, cores[i], 0.15) : COR_PAINEL2,
              tem[i] ? cores[i] : COR_BORDA, 1);
        desenharChave(dc, cx + 1, 32, 30, tem[i] ? cores[i] : RGB(52, 64, 92));
    }
    texto(dc, L"CHAVES", x - 80, 12, 70, 20, fPeqB, COR_MUDO, T_DIR);
    wsprintfW(buf, L"%d/%d", p->chaveDaVerdade + p->chaveDaPrivacidade + p->chaveDaJustica, TOTAL_CHAVES);
    texto(dc, buf, x - 80, 30, 70, 24, fCorpoB, COR_TEXTO, T_DIR);
    retangulo(dc, x - 96, 20, 1, 24, COR_BORDA);
    texto(dc, L"PONTOS", x - 220, 12, 110, 20, fPeqB, COR_MUDO, T_DIR);
    wsprintfW(buf, L"%d", p->pontos);
    texto(dc, buf, x - 220, 28, 110, 28, fH2, COR_AMARELO, T_DIR);
}

/* Janela modal: escurece o fundo, desenha o painel e o cabecalho */
static void modal(HDC dc, int w, int h, COLORREF cor, const wchar_t *titulo,
                  const wchar_t *selo, int *ox, int *oy) {
    unsigned long t = g_tick - g_tickTela;
    int desloca = t < 10 ? (int)(10 - t) * 3 : 0;
    int x = (LARG - w) / 2, y = (ALT - h) / 2 + desloca;

    escurecer(dc, 165);
    g_nBotoes = 0;                     /* so os botoes da janela ficam ativos */
    brilho(dc, x, y, w, h, 22, cor, 0.8, 6);
    caixa(dc, x, y, w, h, 22, COR_PAINEL, misturar(COR_BORDA, cor, 0.55), 2);
    retangulo(dc, x + 28, y + 26, 4, 30, cor);
    texto(dc, titulo, x + 44, y + 18, w - 300, 46, fH1, COR_TEXTO, T_ESQ);
    if (selo) etiqueta(dc, x + w - 28, y + 28, selo, cor);
    retangulo(dc, x + 28, y + 74, w - 56, 1, COR_BORDA);
    *ox = x; *oy = y;
}

/* ============================================================================
 *  TELA: MENU
 * ========================================================================== */
static void telaMenu(HDC dc) {
    int dx, dy, bx = (LARG - 380) / 2, varre;
    fundoAnimado(dc);

    elipse(dc, LARG / 2, 150, 300, 110, SEM_COR, misturar(COR_FUNDO, COR_CIANO, 0.10), 1);
    elipse(dc, LARG / 2, 150, 360, 140, SEM_COR, misturar(COR_FUNDO, COR_ROXO, 0.08), 1);

    for (dx = -3; dx <= 3; dx += 3) {
        for (dy = -3; dy <= 3; dy += 3) {
            texto(dc, L"NEXUS", dx, 70 + dy, LARG, 140, fTitulo,
                  misturar(COR_FUNDO, COR_CIANO, 0.12 + 0.06 * pulso(0.08)), T_CENTRO);
        }
    }
    texto(dc, L"NEXUS", 0, 70, LARG, 140, fTitulo, misturar(COR_CIANO, COR_BRANCO, 0.35), T_CENTRO);
    varre = 90 + (int)((g_tick * 2) % 110);
    retangulo(dc, LARG / 2 - 230, varre, 460, 1, misturar(COR_FUNDO, COR_CIANO, 0.35));

    texto(dc, L"ESCAPE DO SISTEMA CONTROLADO POR INTELIGÊNCIA ARTIFICIAL",
          0, 206, LARG, 24, fPeqB, COR_CIANO, T_CENTRO);
    texto(dc, L"Você está preso em um sistema digital controlado pela IA NEXUS. "
              L"Corrija os erros do sistema e recupere as chaves de segurança para escapar.",
          (LARG - 640) / 2, 248, 640, 60, fCorpo, COR_MUDO, T_PARAG_C);

    botao(dc, bx, 340, 380, 58, L"1", L"Iniciar jogo", COR_CIANO, '1', 1);
    botao(dc, bx, 410, 380, 58, L"2", L"Instruções", COR_ROXO, '2', 0);
    botao(dc, bx, 480, 380, 58, L"3", L"Sair", COR_VERMELHO, '3', 0);

    texto(dc, L"Use o teclado ou clique nos botões", 0, ALT - 74, LARG, 20, fPeq, COR_MUDO, T_CENTRO);
    texto(dc, L"Projeto Integrador  ·  CESAR School  ·  Squad NEXUS", 0, ALT - 48, LARG, 20, fPeqB,
          misturar(COR_MUDO, COR_FUNDO, 0.3), T_CENTRO);
}

/* ============================================================================
 *  TELA: INSTRUCOES
 * ========================================================================== */
static void linhaControle(HDC dc, int x, int y, const wchar_t **teclas, int n, const wchar_t *desc) {
    int i, cx = x;
    for (i = 0; i < n; i++) {
        teclaCap(dc, cx, y, 32, teclas[i], COR_CIANO);
        cx += (wcslen(teclas[i]) > 1 ? larguraTexto(dc, teclas[i], fPeqB) + 16 : 32) + 6;
    }
    texto(dc, desc, x + 170, y, 260, 32, fCorpo, COR_TEXTO, T_ESQ);
}

static void telaInstrucoes(HDC dc) {
    int x = (LARG - 980) / 2, y = 44, w = 980, h = 592, cx, cy, i;
    const wchar_t *mov[4] = { L"W", L"A", L"S", L"D" };
    const wchar_t *setas[1] = { L"Setas" };
    const wchar_t *e[1] = { L"E" };
    const wchar_t *iv[1] = { L"I" };
    const wchar_t *pp[1] = { L"P" };
    const wchar_t *q[2] = { L"Q", L"Esc" };
    const wchar_t *vf[3] = { L"V", L"F", L"1-3" };
    static const wchar_t *nomes[7] = { L"Você (jogador)", L"Parede", L"Pista", L"Computador",
                                       L"Porta trancada", L"Porta aberta", L"Chave" };

    fundoAnimado(dc);
    g_nBotoes = 0;
    brilho(dc, x, y, w, h, 22, COR_ROXO, 0.6, 5);
    caixa(dc, x, y, w, h, 22, COR_PAINEL, misturar(COR_BORDA, COR_ROXO, 0.5), 2);
    retangulo(dc, x + 28, y + 26, 4, 30, COR_ROXO);
    texto(dc, L"Instruções", x + 44, y + 18, 400, 46, fH1, COR_TEXTO, T_ESQ);
    etiqueta(dc, x + w - 28, y + 28, L"NÃO PRECISA APERTAR ENTER", COR_ROXO);
    retangulo(dc, x + 28, y + 74, w - 56, 1, COR_BORDA);

    /* controles */
    cx = x + 40; cy = y + 96;
    texto(dc, L"CONTROLES", cx, cy, 300, 20, fPeqB, COR_MUDO, T_ESQ);
    linhaControle(dc, cx, cy + 30, mov, 4, L"Mover");
    linhaControle(dc, cx, cy + 74, setas, 1, L"Também movem");
    linhaControle(dc, cx, cy + 118, e, 1, L"Interagir com objeto próximo");
    linhaControle(dc, cx, cy + 162, iv, 1, L"Abrir o inventário");
    linhaControle(dc, cx, cy + 206, pp, 1, L"Salvar (próxima etapa)");
    linhaControle(dc, cx, cy + 250, q, 2, L"Voltar / sair (com confirmação)");
    linhaControle(dc, cx, cy + 294, vf, 3, L"Responder os desafios");

    /* simbolos */
    retangulo(dc, x + 500, y + 96, 1, 330, COR_BORDA);
    cx = x + 530;
    texto(dc, L"SÍMBOLOS DO MAPA", cx, cy, 300, 20, fPeqB, COR_MUDO, T_ESQ);
    for (i = 0; i < 7; i++) {
        int ty = cy + 28 + i * 44, tx = cx;
        retangulo(dc, tx, ty, 72, 40, COR_CHAO);
        if (i == 0) jogador(dc, tx + 36, ty + 22, 12);
        else if (i == 1) tileParede(dc, tx + 16, ty, 40, 1, 1);
        else if (i == 2) iconePista(dc, tx + 36, ty + 20, 12);
        else if (i == 3) monitor(dc, tx + 4, ty, 64, 40, 1, L"C1");
        else if (i == 4) portaFechada(dc, tx + 16, ty, 40);
        else if (i == 5) portaAberta(dc, tx + 16, ty, 40);
        else desenharChave(dc, tx + 38, ty + 20, 40, COR_OURO);
        texto(dc, nomes[i], tx + 92, ty, 300, 40, fCorpo, COR_TEXTO, T_ESQ);
    }

    /* objetivo */
    caixa(dc, x + 40, y + 436, w - 80, 78, 14, misturar(COR_PAINEL, COR_CIANO, 0.07),
          misturar(COR_BORDA, COR_CIANO, 0.4), 1);
    texto(dc, L"OBJETIVO", x + 60, y + 444, 200, 20, fPeqB, COR_CIANO, T_ESQ);
    texto(dc, L"Encontre a pista, investigue os computadores, corrija o sistema e pegue a "
              L"Chave da Verdade. Depois, resolva os desafios do Setor de Privacidade.",
          x + 60, y + 464, w - 120, 46, fCorpo, COR_TEXTO, T_PARAG);

    botao(dc, x + (w - 280) / 2, y + h - 66, 280, 52, L"E", L"Voltar ao menu", COR_ROXO, 'E', 1);
}

/* ============================================================================
 *  TELA: MAPA (Fase 1 - Central da Verdade)
 * ========================================================================== */
static void itemLegenda(HDC dc, int *x, int y, int tipo, const wchar_t *rotulo) {
    int cx = *x;
    if (tipo == 0) jogador(dc, cx + 12, y + 16, 10);
    else if (tipo == 1) iconePista(dc, cx + 12, y + 16, 10);
    else if (tipo == 2) monitor(dc, cx - 2, y + 2, 32, 28, 1, L"C");
    else if (tipo == 3) portaFechada(dc, cx, y + 3, 26);
    else if (tipo == 4) portaAberta(dc, cx, y + 3, 26);
    else desenharChave(dc, cx + 14, y + 16, 30, COR_OURO);
    texto(dc, rotulo, cx + 36, y, 200, 32, fCorpo, COR_MUDO, T_ESQ);
    *x = cx + 36 + larguraTexto(dc, rotulo, fCorpo) + 26;
}

static void telaMapa(HDC dc, int mostrarDicas) {
    const EstadoJogo *p = &g_jogo.partida;
    int ox = (LARG - LARGURA_MAPA * TILE) / 2, oy = 160;
    int l, c, i, x, y, atual, objeto;
    char cel;
    const wchar_t *objetivo;
    const wchar_t *passos[4] = { L"Pista", L"C1", L"C2", L"Chave" };
    int feito[4];
    const wchar_t *mov[4] = { L"W", L"A", L"S", L"D" };

    fundoAnimado(dc);
    barraTopo(dc);

    /* ---------- objetivo + progresso ---------- */
    if (!p->pistaEncontrada) objetivo = L"Explore a sala e encontre a pista (?).";
    else if (!p->computador1Resolvido) objetivo = L"Investigue o computador C1.";
    else if (!p->computador2Resolvido) objetivo = L"Corrija o filtro do computador C2.";
    else if (!p->chaveDaVerdade) objetivo = L"A porta D está aberta! Pegue a chave.";
    else objetivo = L"Chave obtida!";

    caixa(dc, 24, 80, LARG - 48, 60, 14, COR_PAINEL, COR_BORDA, 1);
    retangulo(dc, 40, 96, 4, 28, COR_CIANO);
    texto(dc, L"OBJETIVO", 56, 86, 200, 20, fPeqB, COR_MUDO, T_ESQ);
    texto(dc, objetivo, 56, 106, 560, 26, fCorpoB, COR_TEXTO, T_ESQ);

    feito[0] = p->pistaEncontrada; feito[1] = p->computador1Resolvido;
    feito[2] = p->computador2Resolvido; feito[3] = p->chaveDaVerdade;
    atual = 4;
    for (i = 3; i >= 0; i--) if (!feito[i]) atual = i;
    for (i = 0; i < 4; i++) {
        int px = LARG - 48 - 4 * 112 + i * 112 + 20, py = 110;
        COLORREF cor = feito[i] ? COR_VERDE : (i == atual ? COR_CIANO : COR_BORDA);
        if (i < 3) retangulo(dc, px + 12, py, 100, 2, feito[i] ? misturar(COR_PAINEL, COR_VERDE, 0.6) : COR_BORDA);
        if (i == atual) brilhoCirculo(dc, px, py, 10, COR_CIANO, 0.4 + 0.6 * pulso(0.15), 2);
        elipse(dc, px, py, 10, 10, feito[i] ? COR_VERDE : COR_PAINEL2, cor, 2);
        if (feito[i]) {
            linha(dc, px - 5, py, px - 1, py + 4, COR_PAINEL, 2);
            linha(dc, px - 1, py + 4, px + 5, py - 4, COR_PAINEL, 2);
        }
        texto(dc, passos[i], px + 14, py - 26, 80, 20, fPeqB, feito[i] || i == atual ? COR_TEXTO : COR_MUDO, T_ESQ);
    }

    /* ---------- moldura do mapa ---------- */
    brilho(dc, ox - 8, oy - 8, LARGURA_MAPA * TILE + 16, ALTURA_MAPA * TILE + 16, 16, COR_CIANO, 0.35, 4);
    caixa(dc, ox - 8, oy - 8, LARGURA_MAPA * TILE + 16, ALTURA_MAPA * TILE + 16, 16, COR_PAINEL, COR_BORDA, 2);

    /* passada 1: chao e paredes */
    for (l = 0; l < ALTURA_MAPA; l++) {
        for (c = 0; c < LARGURA_MAPA; c++) {
            x = ox + c * TILE; y = oy + l * TILE;
            cel = celulaDoMapa(p, l, c);
            if (cel == PAREDE) {
                int topo = l > 0 && celulaDoMapa(p, l - 1, c) != PAREDE;
                int base = l < ALTURA_MAPA - 1 && celulaDoMapa(p, l + 1, c) != PAREDE;
                tileParede(dc, x, y, TILE, topo || l == 0, base);
            } else {
                COLORREF chao = c > COLUNA_DIVISORIA ? misturar(COR_CHAO, COR_OURO, p->portaAberta ? 0.05 : 0.02) : COR_CHAO;
                retangulo(dc, x, y, TILE, TILE, chao);
                retangulo(dc, x, y, TILE, 1, COR_GRADE);
                retangulo(dc, x, y, 1, TILE, COR_GRADE);
            }
        }
    }

    /* passada 2: objetos */
    for (l = 0; l < ALTURA_MAPA; l++) {
        for (c = 0; c < LARGURA_MAPA; c++) {
            int bob = (int)(sin(g_tick * 0.12 + c) * 3);
            x = ox + c * TILE; y = oy + l * TILE;
            cel = celulaDoMapa(p, l, c);
            if (cel == PISTA) {
                iconePista(dc, x + TILE / 2, y + TILE / 2 + bob, 13);
            } else if (cel == COMPUTADOR && l == C1_LINHA && c == C1_COLUNA) {
                monitor(dc, x, y + 2, 2 * TILE, TILE - 2,
                        p->computador1Resolvido ? 2 : (p->pistaEncontrada ? 1 : 0), L"C1");
            } else if (cel == COMPUTADOR && l == C2_LINHA && c == C2_COLUNA) {
                monitor(dc, x, y + 2, 2 * TILE, TILE - 2,
                        p->computador2Resolvido ? 2 : (p->computador1Resolvido ? 1 : 0), L"C2");
            } else if (cel == PORTA) {
                brilho(dc, x + 6, y + 2, TILE - 12, TILE - 4, 6, COR_VERMELHO, 0.3 + 0.4 * pulso(0.1), 2);
                portaFechada(dc, x, y, TILE);
            } else if (cel == PORTA_ABERTA) {
                portaAberta(dc, x, y, TILE);
            } else if (cel == CHAVE) {
                brilhoCirculo(dc, x + TILE / 2, y + TILE / 2 + bob, 14, COR_OURO, 0.5 + 0.5 * pulso(0.12), 3);
                desenharChave(dc, x + TILE / 2 + 2, y + TILE / 2 + bob, 34, COR_OURO);
            }
        }
    }

    /* jogador (posicao suavizada) */
    jogador(dc, ox + (int)(g_px * TILE) + TILE / 2, oy + (int)(g_py * TILE) + TILE / 2 + 2, 13);

    /* dica de interacao sobre o objeto alcancavel */
    objeto = mostrarDicas ? procurarObjetoProximo(p) : OBJ_NENHUM;
    if (objeto != OBJ_NENHUM) {
        int ol = 0, oc = 0, larg = 1, w, bx, by;
        const wchar_t *acao = L"";
        COLORREF cor = COR_CIANO;
        if (objeto == OBJ_PISTA) { ol = PISTA_LINHA; oc = PISTA_COLUNA; acao = L"Ler pista"; cor = COR_AMARELO; }
        else if (objeto == OBJ_COMPUTADOR1) { ol = C1_LINHA; oc = C1_COLUNA; larg = 2;
            acao = p->pistaEncontrada ? L"Acessar C1" : L"C1 bloqueado"; cor = p->pistaEncontrada ? COR_CIANO : COR_VERMELHO; }
        else if (objeto == OBJ_COMPUTADOR2) { ol = C2_LINHA; oc = C2_COLUNA; larg = 2;
            acao = p->computador1Resolvido ? L"Acessar C2" : L"C2 bloqueado"; cor = p->computador1Resolvido ? COR_CIANO : COR_VERMELHO; }
        else if (objeto == OBJ_PORTA) { ol = PORTA_LINHA; oc = PORTA_COLUNA; acao = L"Porta trancada"; cor = COR_VERMELHO; }
        else { ol = CHAVE_LINHA; oc = CHAVE_COLUNA; acao = L"Pegar chave"; cor = COR_OURO; }
        w = larguraTexto(dc, acao, fPeqB) + 52;
        bx = ox + oc * TILE + larg * TILE / 2 - w / 2;
        by = oy + ol * TILE - 30 - (int)(pulso(0.15) * 3);
        caixa(dc, bx, by, w, 28, 12, misturar(COR_FUNDO, cor, 0.2), cor, 1);
        teclaCap(dc, bx + 5, by + 3, 22, L"E", cor);
        texto(dc, acao, bx + 32, by, w - 36, 28, fPeqB, COR_TEXTO, T_ESQ);
    }

    /* ---------- controles e legenda ---------- */
    caixa(dc, 24, 478, LARG - 48, 112, 14, COR_PAINEL, COR_BORDA, 1);
    texto(dc, L"CONTROLES", 44, 492, 110, 32, fPeqB, COR_MUDO, T_ESQ);
    x = 150;
    for (i = 0; i < 4; i++) { teclaCap(dc, x, 492, 30, mov[i], COR_CIANO); x += 34; }
    texto(dc, L"mover", x + 4, 492, 80, 30, fCorpo, COR_TEXTO, T_ESQ); x += 80;
    teclaCap(dc, x, 492, 30, L"E", COR_CIANO);
    texto(dc, L"interagir", x + 38, 492, 100, 30, fCorpo, COR_TEXTO, T_ESQ); x += 130;
    teclaCap(dc, x, 492, 30, L"I", COR_CIANO);
    texto(dc, L"inventário", x + 38, 492, 110, 30, fCorpo, COR_TEXTO, T_ESQ); x += 140;
    teclaCap(dc, x, 492, 30, L"P", COR_CIANO);
    texto(dc, L"salvar", x + 38, 492, 80, 30, fCorpo, COR_TEXTO, T_ESQ); x += 110;
    teclaCap(dc, x, 492, 30, L"Q", COR_CIANO);
    texto(dc, L"menu", x + 38, 492, 80, 30, fCorpo, COR_TEXTO, T_ESQ);

    retangulo(dc, 44, 534, LARG - 88, 1, COR_BORDA);
    texto(dc, L"LEGENDA", 44, 546, 110, 32, fPeqB, COR_MUDO, T_ESQ);
    x = 150;
    itemLegenda(dc, &x, 546, 0, L"você");
    itemLegenda(dc, &x, 546, 1, L"pista");
    itemLegenda(dc, &x, 546, 2, L"computador");
    itemLegenda(dc, &x, 546, 3, L"porta trancada");
    itemLegenda(dc, &x, 546, 4, L"porta aberta");
    itemLegenda(dc, &x, 546, 5, L"chave");
}

/* ============================================================================
 *  JANELAS SOBRE O MAPA
 * ========================================================================== */
static void modalPista(HDC dc) {
    int x, y, w = 720, h = 470;
    modal(dc, w, h, COR_AMARELO, L"Pista encontrada!", L"+10 PONTOS", &x, &y);

    caixa(dc, x + 44, y + 98, w - 88, 112, 10, RGB(248, 238, 206), SEM_COR, 0);
    retangulo(dc, x + 44, y + 98, w - 88, 4, COR_AMARELO);
    elipse(dc, x + w / 2, y + 104, 7, 7, COR_VERMELHO, RGB(150, 40, 60), 1);
    texto(dc, L"BILHETE DO TÉCNICO DE PLANTÃO", x + 64, y + 114, 400, 20, fPeqB, RGB(140, 120, 80), T_ESQ);
    texto(dc, L"“O servidor principal foi desligado às 22h.”",
          x + 44, y + 138, w - 88, 60, fCitacao, RGB(50, 40, 22), T_CENTRO);

    texto(dc, L"Guarde essa informação: a IA NEXUS pode afirmar o contrário.",
          x + 44, y + 228, w - 88, 26, fCorpo, COR_TEXTO, T_ESQ);

    caixa(dc, x + 44, y + 266, w - 88, 80, 12, misturar(COR_PAINEL, COR_AMARELO, 0.08),
          misturar(COR_BORDA, COR_AMARELO, 0.5), 1);
    texto(dc, L"ALUCINAÇÃO", x + 64, y + 276, 300, 26, fCorpoB, COR_AMARELO, T_ESQ);
    texto(dc, L"Quando uma IA afirma algo falso como se fosse verdade.",
          x + 64, y + 304, w - 128, 30, fCorpo, COR_TEXTO, T_ESQ);

    texto(dc, L"Próximo passo: investigue o computador C1.", x + 44, y + 356, w - 88, 26, fCorpo, COR_MUDO, T_ESQ);
    botao(dc, x + (w - 240) / 2, y + h - 74, 240, 52, L"E", L"Continuar", COR_AMARELO, 'E', 1);
}

static void legendaLogica(HDC dc, int x, int y) {
    texto(dc, L"~ negação     ^ conjunção (E)     V verdadeiro     F falso",
          x, y, 700, 22, fMono, COR_MUDO, T_ESQ);
}

static void modalC1(HDC dc) {
    const EstadoJogo *p = &g_jogo.partida;
    int x, y, w = 880, h = 560, etapa = p->etapaComputador1, i;
    wchar_t alt[2][100], valor;
    const wchar_t *props[2] = { L"p", L"~p" };
    const wchar_t *frases[2] = { L"“O servidor principal está ligado.”",
                                 L"“Não é verdade que o servidor principal está ligado.”" };

    modal(dc, w, h, COR_CIANO, L"Computador C1", L"AFIRMAÇÕES DA IA NEXUS", &x, &y);
    legendaLogica(dc, x + 44, y + 88);

    caixa(dc, x + 44, y + 120, w - 88, 40, 10, misturar(COR_PAINEL, COR_AMARELO, 0.07),
          misturar(COR_BORDA, COR_AMARELO, 0.4), 1);
    texto(dc, L"PISTA", x + 60, y + 120, 80, 40, fPeqB, COR_AMARELO, T_ESQ);
    texto(dc, L"“O servidor principal foi desligado às 22h.”", x + 130, y + 120, w - 200, 40, fCorpo, COR_TEXTO, T_ESQ);

    texto(dc, L"A IA NEXUS afirma:", x + 44, y + 172, 300, 24, fCorpo, COR_MUDO, T_ESQ);
    for (i = 0; i < 2; i++) {
        int ry = y + 202 + i * 66, ativo = (etapa == i);
        if (ativo) brilho(dc, x + 44, ry, w - 88, 56, 12, COR_CIANO, 0.3 + 0.4 * pulso(0.12), 2);
        caixa(dc, x + 44, ry, w - 88, 56, 12, ativo ? misturar(COR_PAINEL2, COR_CIANO, 0.08) : COR_PAINEL2,
              ativo ? COR_CIANO : COR_BORDA, ativo ? 2 : 1);
        texto(dc, props[i], x + 64, ry, 50, 56, fMonoB, COR_CIANO, T_ESQ);
        texto(dc, frases[i], x + 118, ry, w - 260, 56, fCorpo, COR_TEXTO, T_ESQ);
        valor = etapa > i ? (wchar_t)verdade[i].resposta : L'?';
        texto(dc, L"valor", x + w - 160, ry, 50, 56, fPeq, COR_MUDO, T_DIR);
        seloValor(dc, x + w - 104, ry + 12, valor);
    }

    if (etapa < ETAPAS_C1) {
        caixa(dc, x + 44, y + 346, w - 88, 64, 12, misturar(COR_PAINEL, COR_ROXO, 0.06),
              misturar(COR_BORDA, COR_ROXO, 0.35), 1);
        texto(dc, L"RACIOCÍNIO", x + 60, y + 352, 200, 20, fPeqB, COR_ROXO, T_ESQ);
        texto(dc, etapa == 0
                  ? L"A pista fala sobre o servidor; relacione-a com a proposição p antes de julgar seu valor lógico."
                  : L"A pista diz que o servidor foi DESLIGADO; logo, p (“está ligado”) é falsa. E a ~p?",
              x + 60, y + 372, w - 120, 36, fCorpo, COR_TEXTO, T_ESQ);

        texto(dc, etapa == 0 ? L"Qual o valor lógico de p?" : L"Qual o valor lógico de ~p (negação de p)?",
              x + 44, y + 424, w - 88, 34, fH2, COR_TEXTO, T_ESQ);
        paraWide(verdade[etapa].alternativas[0], alt[0], 100);
        paraWide(verdade[etapa].alternativas[1], alt[1], 100);
        botao(dc, x + 44, y + h - 92, 230, 56, L"V", alt[0], COR_VERDE, 'V', 0);
        botao(dc, x + 290, y + h - 92, 230, 56, L"F", alt[1], COR_LARANJA, 'F', 0);
        botao(dc, x + w - 274, y + h - 92, 230, 56, L"Q", L"Sair", COR_MUDO, 'Q', 0);
    } else {
        caixa(dc, x + 44, y + 346, w - 88, 110, 14, misturar(COR_PAINEL, COR_VERDE, 0.1), COR_VERDE, 2);
        texto(dc, L"SISTEMA CORRIGIDO!", x + 68, y + 360, 400, 34, fH2, COR_VERDE, T_ESQ);
        texto(dc, L"A afirmação p era uma ALUCINAÇÃO: parecia certa, mas contradiz a pista.",
              x + 68, y + 398, w - 136, 50, fCorpo, COR_TEXTO, T_PARAG);
        botao(dc, x + (w - 280) / 2, y + h - 92, 280, 56, L"E", L"Voltar ao mapa", COR_VERDE, 'E', 1);
    }
}

static void modalC2(HDC dc) {
    const EstadoJogo *p = &g_jogo.partida;
    int x, y, w = 880, h = 560, i, tx, ty, linhaAtual = p->linhaTabela;
    wchar_t alt[2][100], buf[120];
    const int colW[3] = { 80, 80, 120 };

    modal(dc, w, h, COR_CIANO, L"Computador C2", L"FILTRO DE CONFIANÇA", &x, &y);
    legendaLogica(dc, x + 44, y + 88);

    /* explicacao */
    texto(dc, L"Uma resposta da IA só é CONFIÁVEL se:", x + 44, y + 128, 380, 24, fCorpoB, COR_TEXTO, T_ESQ);
    texto(dc, L"p", x + 60, y + 166, 30, 26, fMonoB, COR_CIANO, T_ESQ);
    texto(dc, L"a resposta cita uma fonte", x + 96, y + 166, 320, 26, fCorpo, COR_TEXTO, T_ESQ);
    texto(dc, L"q", x + 60, y + 198, 30, 26, fMonoB, COR_CIANO, T_ESQ);
    texto(dc, L"o dado confere com as pistas", x + 96, y + 198, 320, 26, fCorpo, COR_TEXTO, T_ESQ);
    caixa(dc, x + 44, y + 240, 380, 70, 12, misturar(COR_PAINEL, COR_CIANO, 0.07), misturar(COR_BORDA, COR_CIANO, 0.4), 1);
    texto(dc, L"confiável = p ^ q", x + 60, y + 248, 340, 28, fMonoB, COR_CIANO, T_ESQ);
    texto(dc, L"conjunção “E”  ·  em C: p && q", x + 60, y + 276, 340, 24, fPeq, COR_MUDO, T_ESQ);

    /* tabela-verdade */
    tx = x + 500; ty = y + 112;
    caixa(dc, tx, ty, 280, 36, 8, COR_PAINEL2, SEM_COR, 0);
    texto(dc, L"p", tx, ty, colW[0], 36, fMonoB, COR_MUDO, T_CENTRO);
    texto(dc, L"q", tx + colW[0], ty, colW[1], 36, fMonoB, COR_MUDO, T_CENTRO);
    texto(dc, L"p ^ q", tx + colW[0] + colW[1], ty, colW[2], 36, fMonoB, COR_CIANO, T_CENTRO);
    for (i = 0; i < LINHAS_TABELA; i++) {
        int ry = ty + 42 + i * 46, vp = tabelaConjuncao[i][0], vq = tabelaConjuncao[i][1];
        int atual = (i == linhaAtual);
        if (atual) {
            caixa(dc, tx - 4, ry - 3, 288, 44, 10, misturar(COR_PAINEL, COR_CIANO, 0.08), COR_CIANO, 2);
            texto(dc, L"▶", tx - 30, ry, 24, 38, fCorpoB, COR_CIANO, T_CENTRO);
        } else {
            retangulo(dc, tx, ry + 41, 280, 1, COR_BORDA);
        }
        seloValor(dc, tx + 20, ry + 3, vp ? L'V' : L'F');
        seloValor(dc, tx + colW[0] + 20, ry + 3, vq ? L'V' : L'F');
        seloValor(dc, tx + colW[0] + colW[1] + 40, ry + 3,
                  i < linhaAtual ? (wchar_t)verdade[2 + i].resposta : L'?');
    }

    if (linhaAtual < LINHAS_TABELA) {
        wsprintfW(buf, L"Linha %d:  p = %c,  q = %c.  Qual o valor de p ^ q?", linhaAtual + 1,
                  tabelaConjuncao[linhaAtual][0] ? L'V' : L'F', tabelaConjuncao[linhaAtual][1] ? L'V' : L'F');
        texto(dc, buf, x + 44, y + 342, w - 88, 34, fH2, COR_TEXTO, T_ESQ);
        texto(dc, L"Lembre: p ^ q só é V quando p E q são V ao mesmo tempo.",
              x + 44, y + 380, w - 88, 24, fCorpo, COR_MUDO, T_ESQ);
        paraWide(verdade[2 + linhaAtual].alternativas[0], alt[0], 100);
        paraWide(verdade[2 + linhaAtual].alternativas[1], alt[1], 100);
        botao(dc, x + 44, y + h - 92, 230, 56, L"V", alt[0], COR_VERDE, 'V', 0);
        botao(dc, x + 290, y + h - 92, 230, 56, L"F", alt[1], COR_LARANJA, 'F', 0);
        botao(dc, x + w - 274, y + h - 92, 230, 56, L"Q", L"Sair", COR_MUDO, 'Q', 0);
    } else {
        caixa(dc, x + 44, y + 340, w - 88, 100, 14, misturar(COR_PAINEL, COR_VERDE, 0.1), COR_VERDE, 2);
        texto(dc, L"FILTRO CORRIGIDO!", x + 68, y + 354, 400, 34, fH2, COR_VERDE, T_ESQ);
        texto(dc, p->portaAberta ? L"C1 ^ C2 = V  →  a porta D foi destrancada." : L"Agora corrija o outro computador.",
              x + 68, y + 392, w - 136, 30, fCorpo, COR_TEXTO, T_ESQ);
        botao(dc, x + (w - 280) / 2, y + h - 92, 280, 56, L"E", L"Voltar ao mapa", COR_VERDE, 'E', 1);
    }
}

static void modalInventario(HDC dc) {
    const EstadoJogo *p = &g_jogo.partida;
    int x, y, w = 780, h = 520, i, kx;
    wchar_t buf[40];
    const wchar_t *nomes[3] = { L"Chave da Verdade", L"Chave da Privacidade", L"Chave da Justiça" };
    COLORREF cores[3];
    int tem[3];

    cores[0] = COR_OURO; cores[1] = COR_ROXO; cores[2] = COR_CIANO;
    tem[0] = p->chaveDaVerdade; tem[1] = p->chaveDaPrivacidade; tem[2] = p->chaveDaJustica;
    modal(dc, w, h, COR_CIANO, L"Inventário", NULL, &x, &y);

    kx = x + (w - (3 * 216 + 2 * 22)) / 2;
    for (i = 0; i < 3; i++) {
        int cx = kx + i * 238;
        if (tem[i]) brilho(dc, cx, y + 96, 216, 150, 16, cores[i], 0.5, 3);
        caixa(dc, cx, y + 96, 216, 150, 16, tem[i] ? misturar(COR_PAINEL, cores[i], 0.1) : COR_PAINEL2,
              tem[i] ? cores[i] : COR_BORDA, tem[i] ? 2 : 1);
        desenharChave(dc, cx + 112, y + 146, 70, tem[i] ? cores[i] : RGB(52, 64, 92));
        texto(dc, nomes[i], cx, y + 186, 216, 24, fCorpoB, tem[i] ? COR_TEXTO : COR_MUDO, T_CENTRO);
        texto(dc, tem[i] ? L"obtida" : (i == 2 ? L"próxima fase" : L"não obtida"),
              cx, y + 212, 216, 20, fPeq, tem[i] ? cores[i] : COR_MUDO, T_CENTRO);
    }

    texto(dc, L"Pista encontrada", x + 70, y + 272, 300, 30, fCorpo, COR_MUDO, T_ESQ);
    texto(dc, p->pistaEncontrada ? L"sim" : L"não", x + 380, y + 272, 300, 30, fCorpoB,
          p->pistaEncontrada ? COR_VERDE : COR_TEXTO, T_ESQ);
    texto(dc, L"Computadores corrigidos", x + 70, y + 304, 300, 30, fCorpo, COR_MUDO, T_ESQ);
    wsprintfW(buf, L"%d / 2", p->computador1Resolvido + p->computador2Resolvido);
    texto(dc, buf, x + 380, y + 304, 300, 30, fCorpoB, COR_TEXTO, T_ESQ);
    texto(dc, L"Porta D", x + 70, y + 336, 300, 30, fCorpo, COR_MUDO, T_ESQ);
    texto(dc, p->portaAberta ? L"aberta" : L"trancada", x + 380, y + 336, 300, 30, fCorpoB,
          p->portaAberta ? COR_VERDE : COR_VERMELHO, T_ESQ);
    texto(dc, L"Regra (conjunção)", x + 70, y + 372, 300, 32, fCorpo, COR_MUDO, T_ESQ);
    texto(dc, L"C1", x + 380, y + 372, 30, 32, fMonoB, COR_TEXTO, T_ESQ);
    seloValor(dc, x + 412, y + 372, p->computador1Resolvido ? L'V' : L'F');
    texto(dc, L"^  C2", x + 462, y + 372, 60, 32, fMonoB, COR_TEXTO, T_ESQ);
    seloValor(dc, x + 524, y + 372, p->computador2Resolvido ? L'V' : L'F');
    texto(dc, L"=", x + 574, y + 372, 20, 32, fMonoB, COR_TEXTO, T_ESQ);
    seloValor(dc, x + 598, y + 372, p->portaAberta ? L'V' : L'F');

    botao(dc, x + (w - 240) / 2, y + h - 76, 240, 52, L"I", L"Fechar", COR_CIANO, 'I', 1);
}

static void modalChave(HDC dc) {
    int x, y, w = 700, h = 500, i, cx, cy, tam;
    modal(dc, w, h, COR_OURO, L"Chave da Verdade obtida!", L"+30 PONTOS", &x, &y);
    cx = x + w / 2; cy = y + 170;

    for (i = 0; i < 12; i++) {
        double a = i * PI_ / 6 + g_tick * 0.02;
        linha(dc, cx + (int)(cos(a) * 52), cy + (int)(sin(a) * 52),
              cx + (int)(cos(a) * 82), cy + (int)(sin(a) * 82), misturar(COR_PAINEL, COR_OURO, 0.35), 3);
    }
    brilhoCirculo(dc, cx, cy, 44, COR_OURO, 0.6 + 0.4 * pulso(0.1), 4);
    elipse(dc, cx, cy, 44, 44, misturar(COR_PAINEL, COR_OURO, 0.12), COR_OURO, 2);
    tam = 76 + (int)(4 * pulso(0.12));
    desenharChave(dc, cx + 4, cy, tam, COR_OURO);

    texto(dc, L"A Central da Verdade voltou a funcionar.", x, y + 262, w, 30, fH2, COR_TEXTO, T_CENTRO);
    caixa(dc, x + 50, y + 306, w - 100, 92, 14, misturar(COR_PAINEL, COR_OURO, 0.07), misturar(COR_BORDA, COR_OURO, 0.4), 1);
    texto(dc, L"LIÇÃO", x + 70, y + 314, 200, 20, fPeqB, COR_OURO, T_ESQ);
    texto(dc, L"Uma IA pode “alucinar” e afirmar algo falso com confiança. "
              L"Sempre confira as respostas com fontes e informações confiáveis.",
          x + 70, y + 336, w - 140, 56, fCorpo, COR_TEXTO, T_PARAG);
    botao(dc, x + (w - 240) / 2, y + h - 76, 240, 52, L"E", L"Continuar", COR_OURO, 'E', 1);
}

/* ============================================================================
 *  TELA: MISSAO 2 - SETOR DE PRIVACIDADE
 * ========================================================================== */
static void telaPrivacidade(HDC dc) {
    const EstadoJogo *p = &g_jogo.partida;
    int n = p->perguntaPrivacidade, i, lx = 24, ly = 84, lw = 380, lh = ALT - 84 - 24;
    int rx = lx + lw + 20, rw = LARG - rx - 24;
    wchar_t pergunta[220], alt[100], num[2], buf[40];
    const Pergunta *q;

    if (n > 2) n = 2;
    q = &privacidade[n];
    fundoAnimado(dc);
    barraTopo(dc);
    g_nBotoes = 0;

    /* painel da missao */
    caixa(dc, lx, ly, lw, lh, 18, COR_PAINEL, misturar(COR_BORDA, COR_ROXO, 0.4), 1);
    escudo(dc, lx + 60, ly + 62, 60, COR_ROXO);
    texto(dc, L"MISSÃO 2", lx + 108, ly + 34, 200, 20, fPeqB, COR_ROXO, T_ESQ);
    texto(dc, L"Setor de Privacidade", lx + 108, ly + 54, lw - 120, 34, fH2, COR_TEXTO, T_ESQ);
    texto(dc, L"O sistema está guardando dados pessoais desnecessários. Corrija três decisões "
              L"da IA e obtenha a Chave da Privacidade.",
          lx + 28, ly + 118, lw - 56, 80, fCorpo, COR_MUDO, T_PARAG);

    caixa(dc, lx + 24, ly + 214, lw - 48, 196, 14, misturar(COR_PAINEL, COR_ROXO, 0.07),
          misturar(COR_BORDA, COR_ROXO, 0.4), 1);
    texto(dc, L"POLÍTICA DO SETOR", lx + 44, ly + 226, 300, 20, fPeqB, COR_ROXO, T_ESQ);
    elipse(dc, lx + 50, ly + 270, 4, 4, COR_ROXO, SEM_COR, 0);
    texto(dc, L"Minimizar dados: guardar só o necessário para a tarefa.",
          lx + 64, ly + 256, lw - 110, 50, fCorpo, COR_TEXTO, T_PARAG);
    elipse(dc, lx + 50, ly + 326, 4, 4, COR_ROXO, SEM_COR, 0);
    texto(dc, L"Acesso exige autorização E necessidade:", lx + 64, ly + 312, lw - 110, 50, fCorpo, COR_TEXTO, T_PARAG);
    caixa(dc, lx + 44, ly + 364, lw - 88, 32, 8, misturar(COR_PAINEL, COR_ROXO, 0.14), SEM_COR, 0);
    texto(dc, L"autorização ^ necessidade", lx + 44, ly + 364, lw - 88, 32, fMono, COR_ROXO, T_CENTRO);

    texto(dc, L"PROGRESSO", lx + 28, ly + lh - 92, 200, 20, fPeqB, COR_MUDO, T_ESQ);
    for (i = 0; i < 3; i++) {
        COLORREF cor = i < p->perguntaPrivacidade ? COR_VERDE : (i == n ? COR_ROXO : COR_BORDA);
        caixa(dc, lx + 28 + i * ((lw - 56) / 3), ly + lh - 62, (lw - 56) / 3 - 10, 10, 10, cor, SEM_COR, 0);
    }
    wsprintfW(buf, L"Desafio %d de 3", n + 1);
    texto(dc, buf, lx + 28, ly + lh - 46, 300, 26, fCorpoB, COR_TEXTO, T_ESQ);

    /* painel do desafio */
    brilho(dc, rx, ly, rw, lh, 18, COR_ROXO, 0.4, 3);
    caixa(dc, rx, ly, rw, lh, 18, COR_PAINEL, misturar(COR_BORDA, COR_ROXO, 0.6), 2);
    wsprintfW(buf, L"DESAFIO %d / 3", n + 1);
    etiqueta(dc, rx + rw - 28, ly + 28, buf, COR_ROXO);
    texto(dc, L"A IA NEXUS precisa decidir:", rx + 36, ly + 28, 400, 26, fPeqB, COR_MUDO, T_ESQ);
    paraWide(q->texto, pergunta, 220);
    texto(dc, pergunta, rx + 36, ly + 70, rw - 72, 110, fH1, COR_TEXTO, T_PARAG);
    texto(dc, L"Escolha a decisão correta:", rx + 36, ly + 196, 400, 24, fCorpo, COR_MUDO, T_ESQ);
    for (i = 0; i < 3; i++) {
        paraWide(q->alternativas[i], alt, 100);
        num[0] = (wchar_t)(L'1' + i); num[1] = 0;
        botao(dc, rx + 36, ly + 236 + i * 90, rw - 72, 72, num, alt, COR_ROXO, (char)('1' + i), 0);
    }
}

/* ============================================================================
 *  TELA: FIM DAS MISSOES
 * ========================================================================== */
static void telaFim(HDC dc) {
    const EstadoJogo *p = &g_jogo.partida;
    int x = (LARG - 820) / 2, y = 84, w = 820, h = ALT - 84 - 66, i, maximo, barra;
    wchar_t buf[60];
    const wchar_t *desempenho;
    COLORREF corDes;

    maximo = PONTUACAO_MAXIMA;
    for (i = 0; i < 3; i++) maximo += privacidade[i].pontos;

    fundoAnimado(dc);
    barraTopo(dc);
    g_nBotoes = 0;
    brilho(dc, x, y, w, h, 22, COR_VERDE, 0.6, 5);
    caixa(dc, x, y, w, h, 22, COR_PAINEL, misturar(COR_BORDA, COR_VERDE, 0.5), 2);

    texto(dc, L"Duas missões concluídas!", x, y + 24, w, 44, fH1, COR_VERDE, T_CENTRO);
    texto(dc, L"Chave da Verdade e Chave da Privacidade recuperadas.", x, y + 68, w, 24, fCorpo, COR_MUDO, T_CENTRO);

    for (i = 0; i < 3; i++) {
        int cx = x + w / 2 + (i - 1) * 130;
        COLORREF cor = i == 0 ? COR_OURO : i == 1 ? COR_ROXO : RGB(52, 64, 92);
        int tem = i == 0 ? p->chaveDaVerdade : i == 1 ? p->chaveDaPrivacidade : p->chaveDaJustica;
        if (tem) brilhoCirculo(dc, cx, y + 140, 34, cor, 0.5 + 0.4 * pulso(0.1 + i * 0.02), 3);
        elipse(dc, cx, y + 140, 34, 34, tem ? misturar(COR_PAINEL, cor, 0.12) : COR_PAINEL2, tem ? cor : COR_BORDA, 2);
        desenharChave(dc, cx + 3, y + 140, 52, tem ? cor : RGB(52, 64, 92));
    }

    texto(dc, L"PONTUAÇÃO", x, y + 196, w, 20, fPeqB, COR_MUDO, T_CENTRO);
    wsprintfW(buf, L"%d", p->pontos);
    texto(dc, buf, x, y + 214, w / 2 + 10, 64, fNumero, COR_AMARELO, T_DIR);
    wsprintfW(buf, L"/ %d", maximo);
    texto(dc, buf, x + w / 2 + 22, y + 226, 200, 50, fH2, COR_MUDO, T_ESQ);
    caixa(dc, x + 160, y + 288, w - 320, 12, 12, COR_PAINEL2, SEM_COR, 0);
    barra = (w - 320) * p->pontos / (maximo > 0 ? maximo : 1);
    if (barra > 12) caixa(dc, x + 160, y + 288, barra, 12, 12, COR_AMARELO, SEM_COR, 0);

    if (p->respostasIncorretas == 0) { desempenho = L"Perfeito!"; corDes = COR_VERDE; }
    else if (p->respostasIncorretas <= 2) { desempenho = L"Muito bom!"; corDes = COR_CIANO; }
    else { desempenho = L"Continue praticando!"; corDes = COR_AMARELO; }

    wsprintfW(buf, L"%d", p->respostasIncorretas);
    texto(dc, L"Respostas incorretas", x + 160, y + 318, 260, 28, fCorpo, COR_MUDO, T_ESQ);
    texto(dc, buf, x + 420, y + 318, w - 580, 28, fCorpoB, COR_TEXTO, T_DIR);
    texto(dc, L"Desempenho", x + 160, y + 348, 260, 28, fCorpo, COR_MUDO, T_ESQ);
    texto(dc, desempenho, x + 420, y + 348, w - 580, 28, fCorpoB, corDes, T_DIR);

    texto(dc, L"Entrega U2 concluída. A terceira fase e o escape não fazem parte desta versão.",
          x, y + 392, w, 22, fPeq, COR_MUDO, T_CENTRO);

    botao(dc, x + 40, y + h - 84, 236, 56, L"1", L"Jogar novamente", COR_VERDE, '1', 1);
    botao(dc, x + 292, y + h - 84, 236, 56, L"2", L"Voltar ao menu", COR_CIANO, '2', 0);
    botao(dc, x + 544, y + h - 84, 236, 56, L"3", L"Sair do jogo", COR_VERMELHO, '3', 0);
}

/* ============================================================================
 *  JANELA: CONFIRMAR SAIDA
 * ========================================================================== */
static void modalConfirmar(HDC dc) {
    int x, y, w = 580, h = 270, doMenu = (g_jogo.ui.origemConfirmacao == TELA_MENU);
    modal(dc, w, h, COR_VERMELHO, doMenu ? L"Sair do NEXUS?" : L"Voltar ao menu?", NULL, &x, &y);
    texto(dc, doMenu ? L"Deseja realmente fechar o jogo?" : L"O progresso da fase será perdido.",
          x + 44, y + 96, w - 88, 30, fCorpo, COR_TEXTO, T_ESQ);
    botao(dc, x + 44, y + h - 88, 230, 56, L"S", L"Sim", COR_VERMELHO, 'S', 0);
    botao(dc, x + w - 274, y + h - 88, 230, 56, L"N", L"Não", COR_CIANO, 'N', 1);
}

/* ============================================================================
 *  AVISOS (substituem o rodape ">> ..." da versao terminal)
 * ========================================================================== */
static void avisar(const wchar_t *s, COLORREF cor) {
    wcsncpy(g_aviso, s, 199);
    g_aviso[199] = 0;
    g_avisoCor = cor;
    g_avisoTick = g_tick;
}

static void mostrarMensagem(int msg) {
    switch (msg) {
    case MSG_PAREDE:          avisar(L"Caminho bloqueado! Você não pode atravessar obstáculos.", COR_AMARELO); break;
    case MSG_NADA_PROXIMO:    avisar(L"Nada para interagir aqui. Chegue perto de ?, C, D ou K.", COR_MUDO); break;
    case MSG_C1_BLOQUEADO:    avisar(L"O computador C1 está bloqueado. Encontre a pista (?) primeiro.", COR_VERMELHO); break;
    case MSG_C2_BLOQUEADO:    avisar(L"Acesso negado ao C2. Primeiro corrija as afirmações do C1.", COR_VERMELHO); break;
    case MSG_PORTA_TRANCADA:  avisar(L"Porta D trancada: ela só abre se C1 E C2 estiverem corrigidos.", COR_VERMELHO); break;
    case MSG_SALVAR:          avisar(L"O salvamento em arquivo será implementado na próxima etapa.", COR_CIANO); break;
    case MSG_ACERTO:          avisar(L"Correto!", COR_VERDE); break;
    case MSG_ERRO_P:          avisar(L"Incorreto. A pista diz que o servidor foi DESLIGADO, então p é Falsa.", COR_VERMELHO); break;
    case MSG_ERRO_NAO_P:      avisar(L"Incorreto. A negação inverte o valor: se p = F, então ~p = V.", COR_VERMELHO); break;
    case MSG_ERRO_TABELA:     avisar(L"Incorreto. p ^ q só é V quando p E q são V ao mesmo tempo.", COR_VERMELHO); break;
    case MSG_PORTA_ABERTA:    avisar(L"Correto! A porta D foi DESTRANCADA.", COR_VERDE); break;
    case MSG_ERRO_PRIVACIDADE: avisar(L"Incorreto. Revise a política do setor.", COR_VERMELHO); break;
    default: break;
    }
}

static void desenharAviso(HDC dc) {
    unsigned long idade = g_tick - g_avisoTick;
    double f;
    int w, x, y = ALT - 54;
    if (!g_aviso[0] || idade > 130) return;
    f = idade > 110 ? 1.0 - (idade - 110) / 20.0 : 1.0;
    if (idade < 6) y += (int)(6 - idade) * 3;
    w = larguraTexto(dc, g_aviso, fCorpoB) + 64;
    x = (LARG - w) / 2;
    caixa(dc, x, y, w, 44, 44, misturar(COR_FUNDO, misturar(COR_PAINEL, g_avisoCor, 0.2), f),
          misturar(COR_FUNDO, g_avisoCor, f), 2);
    elipse(dc, x + 24, y + 22, 5, 5, misturar(COR_FUNDO, g_avisoCor, f), SEM_COR, 0);
    texto(dc, g_aviso, x + 40, y, w - 48, 44, fCorpoB, misturar(COR_FUNDO, COR_TEXTO, f), T_ESQ);
}

/* ============================================================================
 *  DESENHO DO QUADRO COMPLETO
 * ========================================================================== */
static void desenharTudo(HDC dc) {
    int tela = g_jogo.ui.telaAtual;
    int origem = g_jogo.ui.origemConfirmacao;
    g_nBotoes = 0;

    if (tela == TELA_MENU || (tela == TELA_CONFIRMAR_SAIDA && origem == TELA_MENU)) {
        telaMenu(dc);
    } else if (tela == TELA_INSTRUCOES) {
        telaInstrucoes(dc);
    } else if (tela == TELA_PRIVACIDADE) {
        telaPrivacidade(dc);
    } else if (tela == TELA_FIM_FASE) {
        telaFim(dc);
    } else {
        telaMapa(dc, tela == TELA_MAPA);
    }

    if (tela == TELA_PISTA) modalPista(dc);
    else if (tela == TELA_COMPUTADOR1) modalC1(dc);
    else if (tela == TELA_COMPUTADOR2) modalC2(dc);
    else if (tela == TELA_INVENTARIO) modalInventario(dc);
    else if (tela == TELA_CHAVE) modalChave(dc);
    else if (tela == TELA_CONFIRMAR_SAIDA) modalConfirmar(dc);

    desenharAviso(dc);
}

/* ============================================================================
 *  ENTRADA: mesma validacao de lerTecla() da versao terminal
 * ========================================================================== */
static int teclaValida(const Jogo *jogo, char t) {
    const EstadoJogo *p = &jogo->partida;
    if (t == 0 || t == ' ' || t == '\r' || t == '\n' || t == '\t') return 0;
    switch (jogo->ui.telaAtual) {
    case TELA_PRIVACIDADE: case TELA_MENU: case TELA_FIM_FASE:
        return t >= '1' && t <= '3';
    case TELA_INSTRUCOES:
        return 1;
    case TELA_MAPA:
        return strchr("WASDEIPQ", t) != NULL;
    case TELA_PISTA: case TELA_CHAVE:
        return t == 'E';
    case TELA_COMPUTADOR1:
        return p->computador1Resolvido ? (t == 'E' || t == 'Q') : (t == 'V' || t == 'F' || t == 'Q');
    case TELA_COMPUTADOR2:
        return p->computador2Resolvido ? (t == 'E' || t == 'Q') : (t == 'V' || t == 'F' || t == 'Q');
    case TELA_INVENTARIO:
        return t == 'I' || t == 'Q';
    default:
        return t == 'S' || t == 'N';
    }
}

/* Uma volta do "game loop" da versao terminal, disparada por uma tecla */
static void executarTecla(char t) {
    int telaAntes = g_jogo.ui.telaAtual;

    if (t >= 'a' && t <= 'z') t = (char)(t - ('a' - 'A'));
    if (!teclaValida(&g_jogo, t)) {
        if (t > ' ' && t <= '~') {
            wchar_t s[64];
            wcscpy(s, L"Tecla ' ' inválida nesta tela.");
            s[7] = (wchar_t)t;
            avisar(s, COR_AMARELO);
        }
        return;
    }
    if (telaAntes == TELA_MAPA && strchr("WASD", t)) {
        g_dirX = (t == 'D') - (t == 'A');
        g_dirY = (t == 'S') - (t == 'W');
    }

    g_jogo.ui.mensagem = MSG_NENHUMA;
    processarTecla(&g_jogo, t);
    reiniciarSePedido(&g_jogo);
    aplicarMovimento(&g_jogo);

    if (g_jogo.ui.mensagem != MSG_NENHUMA) mostrarMensagem(g_jogo.ui.mensagem);
    else g_aviso[0] = 0;
    g_jogo.ui.mensagem = MSG_NENHUMA;

    if (g_jogo.ui.telaAtual != telaAntes) g_tickTela = g_tick;
    if (fabs(g_px - g_jogo.partida.jogadorColuna) + fabs(g_py - g_jogo.partida.jogadorLinha) > 1.5) {
        g_px = g_jogo.partida.jogadorColuna;
        g_py = g_jogo.partida.jogadorLinha;
    }
    if (g_jogo.ui.telaAtual == TELA_ENCERRAR) {
        DestroyWindow(g_janela);
        return;
    }
    InvalidateRect(g_janela, NULL, FALSE);
}

static int botaoEm(int x, int y) {
    int i;
    for (i = g_nBotoes - 1; i >= 0; i--) {
        if (x >= g_botoes[i].x && x < g_botoes[i].x + g_botoes[i].w &&
            y >= g_botoes[i].y && y < g_botoes[i].y + g_botoes[i].h) return i;
    }
    return -1;
}

/* ============================================================================
 *  JANELA DO WINDOWS
 * ========================================================================== */
static LRESULT CALLBACK procJanela(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(h, 1, 30, NULL);
        return 0;

    case WM_TIMER:
        g_tick++;
        g_px += (g_jogo.partida.jogadorColuna - g_px) * 0.4;
        g_py += (g_jogo.partida.jogadorLinha - g_py) * 0.4;
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        XFORM xf;
        HDC dc = BeginPaint(h, &ps);
        SetGraphicsMode(g_dcBuffer, GM_ADVANCED);
        xf.eM11 = (FLOAT)g_escala; xf.eM12 = 0; xf.eM21 = 0;
        xf.eM22 = (FLOAT)g_escala; xf.eDx = 0; xf.eDy = 0;
        SetWorldTransform(g_dcBuffer, &xf);
        desenharTudo(g_dcBuffer);
        ModifyWorldTransform(g_dcBuffer, &xf, MWT_IDENTITY);
        BitBlt(dc, 0, 0, g_largDisp, g_altDisp, g_dcBuffer, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_KEYDOWN:
        if (g_jogo.ui.telaAtual == TELA_MAPA) {
            if (wp == VK_UP) executarTecla('W');
            else if (wp == VK_DOWN) executarTecla('S');
            else if (wp == VK_LEFT) executarTecla('A');
            else if (wp == VK_RIGHT) executarTecla('D');
        }
        return 0;

    case WM_CHAR:
        if (wp == '\r' || wp == ' ') {
            if (teclaValida(&g_jogo, 'E')) executarTecla('E');
        } else if (wp == 27) {
            if (g_jogo.ui.telaAtual == TELA_CONFIRMAR_SAIDA) executarTecla('N');
            else if (teclaValida(&g_jogo, 'Q')) executarTecla('Q');
        } else if (wp > ' ' && wp < 127) {
            executarTecla((char)wp);
        }
        return 0;

    case WM_MOUSEMOVE:
        g_mouseX = (int)((short)LOWORD(lp) / g_escala);
        g_mouseY = (int)((short)HIWORD(lp) / g_escala);
        return 0;

    case WM_LBUTTONDOWN: {
        int i = botaoEm((int)((short)LOWORD(lp) / g_escala), (int)((short)HIWORD(lp) / g_escala));
        if (i >= 0) executarTecla(g_botoes[i].tecla);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && botaoEm(g_mouseX, g_mouseY) >= 0) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_DESTROY:
        KillTimer(h, 1);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HFONT criarFonte(int altura, int peso, BOOL italico, const wchar_t *nome) {
    return CreateFontW(-altura, 0, 0, 0, peso, italico, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, nome);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE anterior, LPSTR linhaCmd, int mostrar) {
    typedef BOOL (WINAPI *FuncDpi)(void);
    WNDCLASSW wc;
    RECT r;
    MSG m;
    HDC tela;
    HBITMAP bmpEscuro;
    FuncDpi definirDpi;
    DWORD estilo = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    int telaW, telaH;

    (void)anterior; (void)linhaCmd;

    /* Nitidez em telas com zoom (125%, 150%...): desenha na resolucao real */
    definirDpi = (FuncDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware");
    if (definirDpi) definirDpi();
    tela = GetDC(NULL);
    g_escala = GetDeviceCaps(tela, LOGPIXELSY) / 96.0;
    telaW = GetSystemMetrics(SM_CXSCREEN);
    telaH = GetSystemMetrics(SM_CYSCREEN);
    if (LARG * g_escala > telaW * 0.95) g_escala = telaW * 0.95 / LARG;
    if (ALT * g_escala > telaH * 0.88) g_escala = telaH * 0.88 / ALT;
    g_largDisp = (int)(LARG * g_escala + 0.5);
    g_altDisp = (int)(ALT * g_escala + 0.5);

    g_dcBuffer = CreateCompatibleDC(tela);
    g_bmpBuffer = CreateCompatibleBitmap(tela, g_largDisp, g_altDisp);
    SelectObject(g_dcBuffer, g_bmpBuffer);
    g_dcEscuro = CreateCompatibleDC(tela);
    bmpEscuro = CreateCompatibleBitmap(tela, 1, 1);
    SelectObject(g_dcEscuro, bmpEscuro);
    SetPixel(g_dcEscuro, 0, 0, RGB(2, 4, 9));
    ReleaseDC(NULL, tela);

    fTitulo  = criarFonte(110, FW_BLACK, FALSE, L"Segoe UI");
    fNumero  = criarFonte(60, FW_BOLD, FALSE, L"Segoe UI");
    fH1      = criarFonte(28, FW_BOLD, FALSE, L"Segoe UI");
    fH2      = criarFonte(22, FW_SEMIBOLD, FALSE, L"Segoe UI");
    fBotao   = criarFonte(19, FW_SEMIBOLD, FALSE, L"Segoe UI");
    fCorpo   = criarFonte(17, FW_NORMAL, FALSE, L"Segoe UI");
    fCorpoB  = criarFonte(17, FW_SEMIBOLD, FALSE, L"Segoe UI");
    fPeq     = criarFonte(14, FW_NORMAL, FALSE, L"Segoe UI");
    fPeqB    = criarFonte(13, FW_BOLD, FALSE, L"Segoe UI");
    fMono    = criarFonte(16, FW_NORMAL, FALSE, L"Consolas");
    fMonoB   = criarFonte(19, FW_BOLD, FALSE, L"Consolas");
    fCitacao = criarFonte(25, FW_NORMAL, TRUE, L"Georgia");

    iniciarJogo(&g_jogo);
    g_px = g_jogo.partida.jogadorColuna;
    g_py = g_jogo.partida.jogadorLinha;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = procJanela;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"NexusJanela";
    RegisterClassW(&wc);

    r.left = 0; r.top = 0; r.right = g_largDisp; r.bottom = g_altDisp;
    AdjustWindowRect(&r, estilo, FALSE);
    g_janela = CreateWindowW(L"NexusJanela", L"NEXUS — Escape do sistema", estilo,
                             (telaW - (r.right - r.left)) / 2, (telaH - (r.bottom - r.top)) / 2 - 20,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(g_janela, mostrar);
    UpdateWindow(g_janela);

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
