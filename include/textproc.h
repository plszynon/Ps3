#ifndef TEXTPROC_H
#define TEXTPROC_H

#include <string>
#include <vector>

// Zamienia HTML na zwykly tekst (bez skryptow/styli, z podstawowymi encjami
// i polskimi znakami sprowadzonymi do ASCII, bo czcionka ma tylko ASCII).
std::string html_to_text(const std::string &html);

// Zawija tekst na linie o max `cols` znakow.
std::vector<std::string> wrap_text(const std::string &text, int cols);

// Rozbija adres http(s)://host[:port]/sciezka. Zwraca false dla innych adresow.
bool parse_url(const std::string &url, bool &tls, std::string &host, int &port, std::string &path);

// Usuwa naglowki odpowiedzi HTTP; zwraca kod statusu, a w `location` adres przekierowania.
int split_http_response(const std::string &raw, std::string &body, std::string &location);

#endif
