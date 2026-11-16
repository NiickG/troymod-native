# troymod-native

Native Erweiterung (32-Bit-DLL) für die Rome-2-Mod **TroyMod**.
*Native extension (32-bit DLL) for the Total War: Rome II mod TroyMod.*

## Für Spieler

Nichts von Hand tun: Fehlt die DLL, zeigt TroyMod im Hauptmenü den Knopf
**„TroyMod-Erweiterung herunterladen“**. Ein Klick lädt `troymod_native.dll` aus den
[Releases](../../releases) in den Spielordner und prüft ihre SHA-256-Prüfsumme.

Von Hand: `troymod_native.dll` aus dem neuesten Release neben `Rome2.exe` legen.
Entfernen: die Datei löschen.

## Was die DLL macht

TroyMod lädt sie aus Lua mit
`package.loadlib("troymod_native.dll", "luaopen_troymod_native")`.
Rome 2 exportiert seine Lua-Funktionen nicht, deshalb ruft die DLL sie über Adressen in
`empire.retail.dll` auf. Vorher prüft sie die Build-Kennung des Spiels; passt sie nicht
(z. B. nach einem Spiel-Patch), lädt sie nicht und schreibt das in `troymod_native.log` –
das Spiel stürzt dabei nicht ab.

Derzeit stellt sie nur eine Test-Funktion bereit: `log(text)` schreibt in `troymod_native.log`.

Nur Einzelspieler.

## Bauen

Visual Studio 2022 mit C++-Werkzeugen (x86), dann `build.cmd` (Pfad zu `vcvars32.bat` ggf. anpassen).
