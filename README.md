# Nowa karta (PS3 / PSL1GHT)

Launcher zakladek + prosta przegladarka TEKSTOWA dla PS3 z HEN/CFW.
To NIE jest port aplikacji z Androida - brak CSS, JavaScriptu i obrazkow.
Obsluguje http:// i https:// (mbedTLS, TLS 1.2). Certyfikaty serwerow NIE sa weryfikowane
(brak paczki CA), wiec nie loguj sie nigdzie i nie wpisuj hasel. Dziala najlepiej
ze stronami tekstowymi/lekkimi (np. text.npr.org, lite.duckduckgo.com).

## Budowanie PKG
Najprosciej przez GitHub Actions: wgraj ten katalog do repozytorium na GitHubie
(plik `.github/workflows/build.yml` robi reszte), a PKG pobierzesz z zakladki
Actions -> ostatnie uruchomienie -> Artifacts. Workflow pobiera gotowy toolchain,
buduje PSL1GHT i mbedTLS, potem robi `make pkg`.

Lokalnie (Linux): zainstaluj ps3toolchain + PSL1GHT + ps3libraries (skrypt 018, mbedTLS),
ustaw PS3DEV i PSL1GHT i uruchom `make pkg`.
Instalacja na konsoli: PKG na pendrive (/package lub /PKG) -> Package Manager w HEN.

## Sterowanie
- Gora/Dol - wybor / przewijanie, L1/R1 - strona wyzej/nizej
- Krzyzyk - otworz zakladke, Kolo - powrot / wyjscie, Start - wczytaj zakladki ponownie

## Wlasne zakladki
Plik `/dev_hdd0/game/NKART0001/USRDIR/bookmarks.txt`, linie `Nazwa|adres`.

## Znane ograniczenia (kod nie byl kompilowany na prawdziwym toolchainie PS3)
- Pobieranie strony jest blokujace (bez limitu czasu) - przy zawieszonym serwerze ekran zamarznie.
- Czcionka ma tylko ASCII; polskie litery sa zamieniane na odpowiedniki bez ogonkow.
- Czcionka i kod rysowania pochodza z przykladow PSL1GHT (licencja w PSL1GHT-LICENSE.txt).
