// Nowa karta - prosty launcher zakladek i przegladarka tekstowa dla PS3 (PSL1GHT)
// Oparte na przykladzie "blitting" z PSL1GHT. Czcionka: debugfont z przykladow PSL1GHT.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>
#include <ppu-types.h>

#include <sys/process.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <io/pad.h>
#include <rsx/rsx.h>
#include <sysutil/sysutil.h>

#include <string>
#include <vector>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>

#include "rsxutil.h"
#include "bitmap.h"
#include "textproc.h"

SYS_PROCESS_PARAM(1001, 0x100000);

// ---------------------------------------------------------------- czcionka
// Atlas 128x128, komorki 8x9, 16 kolumn, indeks = znak - 32.
static const u8 g_font[128 * 128] = {
#include "fontdata.h"
};
#define GLYPH_W 8
#define GLYPH_H 9

// ---------------------------------------------------------------- stan
static volatile u32 running = 0;
static u32 *canvas = NULL;   // bufor programowy
static Bitmap screenBmp;     // kopia w pamieci RSX
static int W, H;

struct Bookmark { std::string name, url; };
static std::vector<Bookmark> bookmarks;

enum View { VIEW_HOME, VIEW_PAGE };

#define BOOKMARKS_FILE "/dev_hdd0/game/NKART0001/USRDIR/bookmarks.txt"

extern "C" {
static void program_exit_callback()
{
	sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
	gcmSetWaitFlip(context);
	rsxFinish(context, 1);
}

static void sysutil_exit_callback(u64 status, u64 param, void *usrdata)
{
	if (status == SYSUTIL_EXIT_GAME) running = 0;
}
}

// ---------------------------------------------------------------- rysowanie
static void fill_rect(int x, int y, int w, int h, u32 color)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;
	for (int j = 0; j < h; j++) {
		u32 *row = canvas + (y + j) * W + x;
		for (int i = 0; i < w; i++) row[i] = color;
	}
}

static void draw_char(int x, int y, int scale, u32 color, char ch)
{
	if (ch < 32 || ch > 126) ch = '?';
	int idx = ch - 32;
	int gx = (idx % 16) * GLYPH_W;
	int gy = (idx / 16) * GLYPH_H;
	for (int j = 0; j < GLYPH_H; j++) {
		for (int i = 0; i < GLYPH_W; i++) {
			if (g_font[(gy + j) * 128 + gx + i])
				fill_rect(x + i * scale, y + j * scale, scale, scale, color);
		}
	}
}

static void draw_text(int x, int y, int scale, u32 color, const std::string &s)
{
	for (size_t i = 0; i < s.size(); i++)
		draw_char(x + (int)i * GLYPH_W * scale, y, scale, color, s[i]);
}

static void present()
{
	memcpy(screenBmp.pixels, canvas, (size_t)W * H * 4);
}

// Wyswietla caly ekran z canvas przez transfer RSX (jak w przykladzie blitting)
static void blit_screen()
{
	rsxSetTransferImage(context, GCM_TRANSFER_LOCAL_TO_LOCAL,
		color_offset[curr_fb], color_pitch, 0, 0,
		screenBmp.offset, screenBmp.width * 4,
		rsxGetFixedUint16(0.f), rsxGetFixedUint16(0.f),
		screenBmp.width, screenBmp.height, 4);
}

// ---------------------------------------------------------------- zakladki
static void load_bookmarks()
{
	bookmarks.clear();
	FILE *f = fopen(BOOKMARKS_FILE, "r");
	if (f) {
		char line[512];
		while (fgets(line, sizeof(line), f)) {
			std::string l(line);
			while (!l.empty() && (l[l.size() - 1] == '\n' || l[l.size() - 1] == '\r')) l.erase(l.size() - 1);
			if (l.empty() || l[0] == '#') continue;
			size_t bar = l.find('|');
			Bookmark b;
			if (bar == std::string::npos) { b.name = l; b.url = l; }
			else { b.name = l.substr(0, bar); b.url = l.substr(bar + 1); }
			bookmarks.push_back(b);
		}
		fclose(f);
	}
	if (bookmarks.empty()) {
		const char *defs[][2] = {
			{"Example",              "https://example.com/"},
			{"NPR (tekst)",          "https://text.npr.org/"},
			{"DuckDuckGo (szukaj)",  "https://lite.duckduckgo.com/lite/"},
			{"Pierwsza strona WWW",  "http://info.cern.ch/"},
		};
		for (size_t i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
			Bookmark b; b.name = defs[i][0]; b.url = defs[i][1];
			bookmarks.push_back(b);
		}
	}
}

// ---------------------------------------------------------------- HTTP / HTTPS
#define MAX_PAGE (256 * 1024)

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
	int r = send(*(int *)ctx, buf, len, 0);
	return (r < 0) ? MBEDTLS_ERR_NET_SEND_FAILED : r;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
	int r = recv(*(int *)ctx, buf, len, 0);
	return (r < 0) ? MBEDTLS_ERR_NET_RECV_FAILED : r;
}

static std::string tls_error(const char *what, int ret)
{
	char eb[128];
	mbedtls_strerror(ret, eb, sizeof(eb));
	return std::string(what) + ": " + eb;
}

// Wysyla zapytanie i czyta cala odpowiedz. Przy HTTPS uzywa mbedTLS (TLS 1.2).
// UWAGA: certyfikaty serwera NIE sa weryfikowane (brak paczki certyfikatow CA).
static bool http_exchange(bool tls, const std::string &host, int port,
                          const std::string &req, std::string &raw, std::string &err)
{
	struct hostent *he = gethostbyname(host.c_str());
	if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
		err = "Nie mozna znalezc serwera: " + host + "\nSprawdz polaczenie z internetem w ustawieniach PS3.";
		return false;
	}

	int s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0) { err = "Nie mozna utworzyc gniazda."; return false; }

	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_len = sizeof(server);
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	memcpy(&server.sin_addr, he->h_addr_list[0], sizeof(server.sin_addr));

	if (connect(s, (struct sockaddr *)&server, sizeof(server)) != 0) {
		close(s);
		err = "Nie mozna polaczyc z serwerem: " + host;
		return false;
	}

	char buf[4096];

	if (!tls) {
		if (send(s, req.c_str(), req.size(), 0) < 0) {
			close(s);
			err = "Nie mozna wyslac zapytania.";
			return false;
		}
		while (raw.size() < MAX_PAGE) {
			int n = recv(s, buf, sizeof(buf), 0);
			if (n <= 0) break;
			raw.append(buf, n);
		}
		close(s);
		return true;
	}

	// ---- TLS
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context drbg;
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);

	bool ok = false;
	int ret = 0;
	const char *pers = "nowakarta";

	do {
		ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
		                            (const unsigned char *)pers, strlen(pers));
		if (ret != 0) { err = tls_error("TLS (generator losowy)", ret); break; }

		ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
		                                  MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
		if (ret != 0) { err = tls_error("TLS (konfiguracja)", ret); break; }

		mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
		mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);

		ret = mbedtls_ssl_setup(&ssl, &conf);
		if (ret != 0) { err = tls_error("TLS (setup)", ret); break; }

		mbedtls_ssl_set_hostname(&ssl, host.c_str()); // SNI
		mbedtls_ssl_set_bio(&ssl, &s, bio_send, bio_recv, NULL);

		while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
			if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) break;
		}
		if (ret != 0) { err = tls_error("Blad polaczenia HTTPS", ret); break; }

		size_t off = 0;
		bool wfail = false;
		while (off < req.size()) {
			ret = mbedtls_ssl_write(&ssl, (const unsigned char *)req.data() + off, req.size() - off);
			if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
			if (ret < 0) { wfail = true; break; }
			off += ret;
		}
		if (wfail) { err = tls_error("Blad wysylania (HTTPS)", ret); break; }

		while (raw.size() < MAX_PAGE) {
			ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf, sizeof(buf));
			if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
			if (ret <= 0) break; // koniec danych albo zamkniecie polaczenia
			raw.append(buf, ret);
		}
		if (raw.empty() && ret < 0 && ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
			err = tls_error("Blad odczytu (HTTPS)", ret);
			break;
		}
		mbedtls_ssl_close_notify(&ssl);
		ok = true;
	} while (0);

	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&conf);
	mbedtls_ctr_drbg_free(&drbg);
	mbedtls_entropy_free(&entropy);
	close(s);
	return ok;
}

// Zwraca true i wypelnia `text`, albo false i wstawia komunikat bledu w `text`.
static bool fetch_page(const std::string &startUrl, std::string &text)
{
	std::string url = startUrl;

	for (int redirects = 0; redirects < 5; redirects++) {
		bool tls; std::string host, path; int port;
		if (!parse_url(url, tls, host, port, path)) {
			text = "Ten adres nie jest obslugiwany:\n" + url +
			       "\n\nObslugiwane sa adresy http:// i https://.";
			return false;
		}

		std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host +
		                  "\r\nUser-Agent: Mozilla/5.0 (PLAYSTATION 3) NowaKarta/1.0\r\n"
		                  "Accept: text/html,text/plain\r\nAccept-Encoding: identity\r\n"
		                  "Connection: close\r\n\r\n";

		std::string raw, err;
		if (!http_exchange(tls, host, port, req, raw, err)) { text = err; return false; }

		std::string body, location;
		int status = split_http_response(raw, body, location);

		if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && !location.empty()) {
			if (location[0] == '/') {
				char pb[16] = "";
				if (port != (tls ? 443 : 80)) snprintf(pb, sizeof(pb), ":%d", port);
				location = std::string(tls ? "https://" : "http://") + host + pb + location;
			}
			url = location;
			continue;
		}
		if (status >= 400 || status == 0) {
			char m[64]; snprintf(m, sizeof(m), "Serwer zwrocil blad (kod %d).", status);
			text = m;
			return false;
		}

		bool looksHtml = body.find('<') != std::string::npos;
		text = looksHtml ? html_to_text(body) : body;
		if (text.empty()) text = "(pusta strona)";
		return true;
	}
	text = "Zbyt wiele przekierowan.";
	return false;
}

// ---------------------------------------------------------------- widoki
#define COL_BG      0x00202124
#define COL_FG      0x00E8EAED
#define COL_DIM     0x009AA0A6
#define COL_ACCENT  0x004285F4
#define COL_BAR     0x00303134

static void draw_home(int sel)
{
	fill_rect(0, 0, W, H, COL_BG);

	std::string title = "Nowa karta";
	int ts = 4;
	draw_text((W - (int)title.size() * GLYPH_W * ts) / 2, 50, ts, COL_FG, title);

	int boxW = W * 3 / 5, boxX = (W - boxW) / 2, y = 150;
	int rowH = 56, scale = 2;

	for (size_t i = 0; i < bookmarks.size() && (int)i < 8; i++) {
		bool on = ((int)i == sel);
		fill_rect(boxX, y, boxW, rowH - 8, on ? COL_ACCENT : COL_BAR);

		char letter[2] = {'?', 0};
		std::string u = bookmarks[i].url;
		size_t st = u.find("//");
		st = (st == std::string::npos) ? 0 : st + 2;
		if (st < u.size()) letter[0] = (char)toupper((unsigned char)u[st]);
		draw_text(boxX + 14, y + 8, scale, COL_FG, letter);

		std::string nm = bookmarks[i].name;
		if (nm.size() > 40) nm = nm.substr(0, 40);
		draw_text(boxX + 60, y + 8, scale, COL_FG, nm);
		y += rowH;
	}

	draw_text(boxX, H - 70, 1, COL_DIM, "D-pad: wybor     X: otworz     Kolo: wyjscie     Start: wczytaj zakladki ponownie");
	draw_text(boxX, H - 52, 1, COL_DIM, "Wlasne zakladki: plik bookmarks.txt w USRDIR, linie: Nazwa|https://adres   (HTTPS bez weryfikacji certyfikatow)");
}

static void draw_page(const std::string &url, const std::vector<std::string> &lines, int top, int rows, int scale)
{
	fill_rect(0, 0, W, H, COL_BG);
	fill_rect(0, 0, W, 36, COL_BAR);
	std::string u = url.size() > (size_t)(W / 8 - 4) ? url.substr(0, W / 8 - 4) : url;
	draw_text(20, 10, 1, COL_FG, u);

	int lh = GLYPH_H * scale + 4;
	for (int i = 0; i < rows && top + i < (int)lines.size(); i++)
		draw_text(40, 56 + i * lh, scale, COL_FG, lines[top + i]);

	char status[96];
	int total = (int)lines.size();
	snprintf(status, sizeof(status), "Gora/Dol: przewijanie   L1/R1: strona   Kolo: powrot     linia %d/%d",
	         total ? top + 1 : 0, total);
	fill_rect(0, H - 30, W, 30, COL_BAR);
	draw_text(20, H - 22, 1, COL_DIM, status);
}

// ---------------------------------------------------------------- main
int main(int argc, const char *argv[])
{
	void *host_addr = memalign(1024 * 1024, HOST_SIZE);
	init_screen(host_addr, HOST_SIZE);
	ioPadInit(7);

	atexit(program_exit_callback);
	sysUtilRegisterCallback(0, sysutil_exit_callback, NULL);

	W = display_width;
	H = display_height;
	canvas = (u32 *)malloc((size_t)W * H * 4);
	bitmapInit(&screenBmp, W, H);

	load_bookmarks();
	setRenderTarget(curr_fb);

	View view = VIEW_HOME;
	int sel = 0;
	std::string pageUrl;
	std::vector<std::string> lines;
	int top = 0;
	const int scale = 2;
	int rows = (H - 100) / (GLYPH_H * scale + 4);
	int cols = (W - 80) / (GLYPH_W * scale);
	bool dirty = true;

	padInfo padinfo;
	padData paddata;
	u32 prev = 0;
	int repeat = 0;

	running = 1;
	while (running) {
		sysUtilCheckCallback();

		// stan przyciskow jako maska bitow
		u32 now = 0;
		ioPadGetInfo(&padinfo);
		for (int i = 0; i < MAX_PADS; i++) {
			if (!padinfo.status[i]) continue;
			ioPadGetData(i, &paddata);
			if (paddata.BTN_UP)     now |= 1;
			if (paddata.BTN_DOWN)   now |= 2;
			if (paddata.BTN_CROSS)  now |= 4;
			if (paddata.BTN_CIRCLE) now |= 8;
			if (paddata.BTN_L1)     now |= 16;
			if (paddata.BTN_R1)     now |= 32;
			if (paddata.BTN_START)  now |= 64;
		}
		u32 pressed = now & ~prev;
		// autopowtarzanie dla gory/dolu/L1/R1
		if (now & (1 | 2 | 16 | 32)) {
			if (pressed) repeat = 0;
			else if (++repeat > 20 && (repeat % 3) == 0) pressed |= (now & (1 | 2 | 16 | 32));
		} else repeat = 0;
		prev = now;

		if (view == VIEW_HOME) {
			int n = (int)bookmarks.size() < 8 ? (int)bookmarks.size() : 8;
			if ((pressed & 1) && sel > 0) { sel--; dirty = true; }
			if ((pressed & 2) && sel < n - 1) { sel++; dirty = true; }
			if (pressed & 64) { load_bookmarks(); sel = 0; dirty = true; }
			if (pressed & 8) break;
			if ((pressed & 4) && n > 0) {
				pageUrl = bookmarks[sel].url;
				// ekran ladowania
				fill_rect(0, 0, W, H, COL_BG);
				draw_text(40, 60, 2, COL_FG, "Ladowanie: " + pageUrl);
				draw_text(40, 100, 1, COL_DIM, "Prosze czekac...");
				present();
				rsxSetClearColor(context, COL_BG);
				rsxClearSurface(context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
				blit_screen();
				flip();

				std::string text;
				fetch_page(pageUrl, text);
				lines = wrap_text(text, cols);
				top = 0;
				view = VIEW_PAGE;
				dirty = true;
			}
		} else {
			int maxTop = (int)lines.size() - rows;
			if (maxTop < 0) maxTop = 0;
			if ((pressed & 1) && top > 0) { top--; dirty = true; }
			if ((pressed & 2) && top < maxTop) { top++; dirty = true; }
			if ((pressed & 16)) { top -= rows - 1; if (top < 0) top = 0; dirty = true; }
			if ((pressed & 32)) { top += rows - 1; if (top > maxTop) top = maxTop; dirty = true; }
			if (pressed & 8) { view = VIEW_HOME; dirty = true; }
		}

		if (dirty) {
			if (view == VIEW_HOME) draw_home(sel);
			else draw_page(pageUrl, lines, top, rows, scale);
			present();
			dirty = false;
		}

		rsxSetClearColor(context, COL_BG);
		rsxClearSurface(context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
		blit_screen();
		flip();
	}

	bitmapDestroy(&screenBmp);
	free(canvas);
	return 0;
}
