# Nowa karta (PS3 / PSL1GHT)

Launcher zakladek + prosta przegladarka TEKSTOWA dla PS3 z HEN/CFW.
To NIE jest port aplikacji z Androida - brak CSS, JavaScriptu, obrazkow i HTTPS.
Dziala tylko ze stronami po `http://` (np. wersje tekstowe serwisow).

## Budowanie PKG (na PC z Linuksem/WSL/macOS)
1. Zainstaluj toolchain: https://github.com/ps3dev/ps3toolchain
   oraz PSL1GHT: https://github.com/ps3dev/PSL1GHT
   Ustaw zmienne: `PS3DEV` i `PSL1GHT` (zgodnie z instrukcja ps3toolchain).
2. W tym katalogu: `make pkg`
3. Otrzymasz `NowaKarta.pkg` (oraz `NowaKarta.gnpdrm.pkg`). Na HEN uzyj zwykle `NowaKarta.pkg`.
4. Skopiuj PKG na pendrive do `/package` albo `/PKG` i zainstaluj z XMB:
   Gra -> Package Manager / Install Package Files (zalezy od HEN).

## Sterowanie
- Gora/Dol - wybor / przewijanie, L1/R1 - strona wyzej/nizej
- Krzyzyk - otworz zakladke, Kolo - powrot / wyjscie, Start - wczytaj zakladki ponownie

## Wlasne zakladki
Plik `/dev_hdd0/game/NKART0001/USRDIR/bookmarks.txt`, linie `Nazwa|http://adres`.

## Znane ograniczenia (kod nie byl kompilowany na prawdziwym toolchainie PS3)
- Pobieranie strony jest blokujace (bez limitu czasu) - przy zawieszonym serwerze ekran zamarznie.
- Czcionka ma tylko ASCII; polskie litery sa zamieniane na odpowiedniki bez ogonkow.
- Czcionka i kod rysowania pochodza z przykladow PSL1GHT (licencja w PSL1GHT-LICENSE.txt).
