#include "textproc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static std::string lower(std::string s)
{
	for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
	return s;
}

// Polskie litery w UTF-8 -> ASCII
static bool polish_to_ascii(unsigned char a, unsigned char b, char &out)
{
	struct M { unsigned char a, b; char c; };
	static const M map[] = {
		{0xC4,0x85,'a'},{0xC4,0x84,'A'},{0xC4,0x87,'c'},{0xC4,0x86,'C'},
		{0xC4,0x99,'e'},{0xC4,0x98,'E'},{0xC5,0x82,'l'},{0xC5,0x81,'L'},
		{0xC5,0x84,'n'},{0xC5,0x83,'N'},{0xC3,0xB3,'o'},{0xC3,0x93,'O'},
		{0xC5,0x9B,'s'},{0xC5,0x9A,'S'},{0xC5,0xBA,'z'},{0xC5,0xB9,'Z'},
		{0xC5,0xBC,'z'},{0xC5,0xBB,'Z'}
	};
	for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++)
		if (map[i].a == a && map[i].b == b) { out = map[i].c; return true; }
	return false;
}

static void append_entity(const std::string &ent, std::string &out)
{
	if (ent == "amp") out += '&';
	else if (ent == "lt") out += '<';
	else if (ent == "gt") out += '>';
	else if (ent == "quot") out += '"';
	else if (ent == "apos") out += '\'';
	else if (ent == "nbsp") out += ' ';
	else if (ent.size() > 1 && ent[0] == '#') {
		long v = (ent[1] == 'x' || ent[1] == 'X') ? strtol(ent.c_str() + 2, NULL, 16)
		                                          : strtol(ent.c_str() + 1, NULL, 10);
		out += (v >= 32 && v < 127) ? (char)v : '?';
	} else out += '?';
}

static bool is_block_tag(const std::string &t)
{
	static const char *tags[] = {"br","p","div","li","tr","h1","h2","h3","h4","h5","h6",
	                             "ul","ol","table","section","article","header","footer","pre","hr"};
	for (size_t i = 0; i < sizeof(tags)/sizeof(tags[0]); i++)
		if (t == tags[i]) return true;
	return false;
}

std::string html_to_text(const std::string &html)
{
	std::string out;
	size_t i = 0, n = html.size();
	const std::string lh = lower(html);

	while (i < n) {
		unsigned char c = (unsigned char)html[i];

		if (c == '<') {
			if (html.compare(i, 4, "<!--") == 0) { // komentarz
				size_t ce = html.find("-->", i + 4);
				i = (ce == std::string::npos) ? n : ce + 3;
				continue;
			}
			size_t end = html.find('>', i);
			if (end == std::string::npos) break;
			std::string tag = html.substr(i + 1, end - i - 1);
			i = end + 1;

			bool closing = !tag.empty() && tag[0] == '/';
			size_t s = closing ? 1 : 0, e = s;
			while (e < tag.size() && (isalnum((unsigned char)tag[e]))) e++;
			std::string name = lower(tag.substr(s, e - s));

			if (!closing && (name == "script" || name == "style")) {
				// przeskocz zawartosc az do </script> lub </style>
				size_t close = lh.find("</" + name, i);
				i = (close == std::string::npos) ? n : close;
			} else if (is_block_tag(name)) {
				out += '\n';
			}
			continue;
		}

		if (c == '&') {
			size_t semi = html.find(';', i);
			if (semi != std::string::npos && semi - i <= 8) {
				append_entity(html.substr(i + 1, semi - i - 1), out);
				i = semi + 1;
				continue;
			}
			out += '&'; i++; continue;
		}

		if (c >= 0x80) {
			char r;
			if (i + 1 < n && polish_to_ascii(c, (unsigned char)html[i + 1], r)) { out += r; i += 2; continue; }
			// pomin cala sekwencje UTF-8, wstaw '?'
			size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
			out += '?';
			i += len;
			continue;
		}

		if (c == '\r' || c == '\t') c = ' ';
		out += (char)c;
		i++;
	}

	// zwin spacje i ogranicz puste linie
	std::string res;
	int nl = 0;
	bool sp = false;
	for (size_t k = 0; k < out.size(); k++) {
		char ch = out[k];
		if (ch == '\n') {
			sp = false;
			if (nl < 2 && !res.empty()) res += '\n';
			nl++;
		} else if (ch == ' ') {
			if (!sp && nl == 0 && !res.empty()) res += ' ';
			else if (!sp && nl > 0) { /* spacje na poczatku linii pomijamy */ }
			sp = true;
		} else {
			res += ch; nl = 0; sp = false;
		}
	}
	return res;
}

std::vector<std::string> wrap_text(const std::string &text, int cols)
{
	std::vector<std::string> lines;
	if (cols < 8) cols = 8;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		std::string para = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

		if (para.empty()) { lines.push_back(""); continue; }
		while ((int)para.size() > cols) {
			size_t cut = para.rfind(' ', cols);
			if (cut == std::string::npos || cut == 0) cut = cols;
			lines.push_back(para.substr(0, cut));
			para = para.substr(cut);
			size_t st = 0;
			while (st < para.size() && para[st] == ' ') st++;
			para = para.substr(st);
		}
		lines.push_back(para);
	}
	return lines;
}

bool parse_http_url(const std::string &url, std::string &host, int &port, std::string &path)
{
	if (lower(url.substr(0, 7)) != "http://") return false;
	std::string rest = url.substr(7);
	size_t slash = rest.find('/');
	std::string hp = (slash == std::string::npos) ? rest : rest.substr(0, slash);
	path = (slash == std::string::npos) ? "/" : rest.substr(slash);
	port = 80;
	size_t colon = hp.find(':');
	if (colon != std::string::npos) {
		port = atoi(hp.c_str() + colon + 1);
		hp = hp.substr(0, colon);
	}
	host = hp;
	return !host.empty() && port > 0 && port < 65536;
}

int split_http_response(const std::string &raw, std::string &body, std::string &location)
{
	size_t hend = raw.find("\r\n\r\n");
	size_t skip = 4;
	if (hend == std::string::npos) { hend = raw.find("\n\n"); skip = 2; }
	if (hend == std::string::npos) { body = raw; return 0; }

	std::string head = raw.substr(0, hend);
	body = raw.substr(hend + skip);

	int status = 0;
	size_t sp = head.find(' ');
	if (sp != std::string::npos) status = atoi(head.c_str() + sp + 1);

	std::string lh = lower(head);
	size_t lp = lh.find("\nlocation:");
	location.clear();
	if (lp != std::string::npos) {
		size_t st = lp + 10, en = head.find('\n', st);
		location = head.substr(st, en == std::string::npos ? std::string::npos : en - st);
		size_t a = location.find_first_not_of(" \t");
		size_t b = location.find_last_not_of(" \t\r");
		location = (a == std::string::npos) ? "" : location.substr(a, b - a + 1);
	}
	return status;
}
