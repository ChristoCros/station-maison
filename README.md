# Solar Tracker — ESP32 + Shelly EM

Moniteur de consommation/production électrique : un ESP32 lit deux Shelly EM (réseau + production solaire), affiche les valeurs sur un OLED 128x64, pilote des LEDs de seuils, et expose un dashboard web protégé par login.

## Vue d'ensemble

```
+-----------+        +---------+        +-----------+
|  Shelly   | <----- |  ESP32  | -----> |  OLED     |
|  EM (x2)  |   WiFi |         |  I2C   |  SH1106   |
+-----------+        |         |        +-----------+
                     |         |
                     |         | -----> 5 LEDs (seuils)
                     |         |
                     |         |  WiFi  +-----------+
                     |         | <----> | Dashboard |
                     +---------+        |  Web      |
                                        +-----------+
```

L'ESP32 :
- interroge les deux Shelly EM toutes les 2 s pour récupérer `act_power` ;
- affiche conso réseau, heure NTP et icône soleil sur l'OLED ;
- allume 5 LEDs selon des seuils de consommation/injection ;
- sert un dashboard web HTTP avec login, config WiFi/Shelly, et flux WebSocket temps réel ;
- supporte l'OTA pour les mises à jour WiFi.

## Matériel

| Composant | Modèle | Connexion ESP32 |
|---|---|---|
| Microcontrôleur | ESP32 DevKit (toute variante avec WiFi) | — |
| OLED | SH1106 128x64 I2C | SDA=GPIO21, SCL=GPIO22 |
| LED verte (conso 0–500 W) | LED + résistance | GPIO25 |
| LED jaune (conso 500–1000 W) | LED + résistance | GPIO32 |
| LED rouge (conso > 1000 W) | LED + résistance | GPIO33 |
| LED bleue (injection 0–750 W) | LED + résistance | GPIO26 |
| LED blanche (injection > 750 W) | LED + résistance | GPIO27 |
| Shelly réseau | Shelly EM (id 0) | HTTP local |
| Shelly solaire | Shelly EM (id 1) | HTTP local |

Le mapping des pins est défini dans [display.cpp:8-12](display.cpp#L8-L12).

## Architecture logicielle

```
solar_tracker/
+-- solar_tracker.ino    Entrée principale, setup() + loop(), OTA
+-- config.{h,cpp}       Stockage NVS (WiFi, Shellys, admin)
+-- auth.{h,cpp}         Sessions, hash SHA256, rate-limit
+-- network_mgr.{h,cpp}  WiFi STA + fallback Access Point
+-- shelly.{h,cpp}       Client HTTP des Shelly EM
+-- display.{h,cpp}      OLED + LEDs
+-- web_portal.{h,cpp}   Serveur HTTP + WebSocket
+-- data/                Fichiers servis depuis LittleFS
|   +-- login.html
|   +-- index.html
|   +-- style.css
|   +-- app.js
+-- README.md            Ce fichier
```

Toutes les unités de compilation au niveau racine (`.ino`, `.h`, `.cpp`) sont fusionnées par Arduino IDE — pas besoin de `src/`.

## Installation

### Prérequis

- Arduino IDE 2.x
- Support ESP32 installé via Boards Manager : `esp32` by Espressif Systems (≥ 3.0)
- Plugin LittleFS uploader : [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload) — il ajoute le menu `Outils > Upload LittleFS Data`

### Bibliothèques Arduino (Library Manager)

| Bibliothèque | Auteur | Version testée |
|---|---|---|
| U8g2 | olikraus | 2.36.5 |
| ArduinoJson | bblanchon | 7.4.x |
| ESPAsyncWebServer | **ESP32Async** | 3.7.x |
| AsyncTCP | **ESP32Async** | 3.4.x |

> Important : prends bien les forks **ESP32Async** pour `ESPAsyncWebServer` et `AsyncTCP`. Le `me-no-dev` original est abandonné et a des bugs sur ESP32 récents.

### Sélection de la carte

`Outils > Type de carte > esp32 > ESP32 Dev Module`. Réglages par défaut OK, sauf `Partition Scheme` : sélectionne **`Default with FFat`** ou **`Minimal SPIFFS`** selon ta carte. Le LittleFS uploader détecte la partition.

## Premier déploiement

L'ESP32 ne sait pas faire d'OTA tant qu'il n'a pas reçu un firmware compatible OTA, donc le **premier flash est obligatoirement en USB**.

```
1. Brancher l'ESP32 en USB
2. Outils > Port > /dev/cu.usbserial-XXXX
3. Esquisse > Verifier (compilation)
4. Esquisse > Televerser           (flash du firmware)
5. Outils > Upload LittleFS Data    (flash du dossier data/)
```

Au boot, ouvre le moniteur série à **115200** pour voir les logs.

### Configuration initiale via portail captif

Comme aucun WiFi n'est configuré, l'ESP32 démarre en mode Access Point :

- L'OLED affiche : `SETUP MODE / SSID: SolarTracker-XXXX / Pass: <12 hex aléatoires> / http://192.168.4.1`
- Le mot de passe de l'AP est généré aléatoirement au premier boot et stocké en NVS. Il est affiché sur l'OLED ET dans les logs série (`[WIFI] AP started. pass=...`)
- Avec ton téléphone ou ordi : connecte-toi au WiFi `SolarTracker-XXXX` avec le mot de passe affiché
- Ouvre `http://192.168.4.1`
- Login : `admin / admin`
- L'interface affiche un bandeau orange "Premier démarrage" → **change immédiatement le mot de passe admin**
- Remplis le formulaire WiFi (SSID + mot de passe de ton réseau domestique) → enregistre → l'ESP32 redémarre
- Au redémarrage, l'ESP32 rejoint ton réseau. Note l'IP affichée par le moniteur série (ou trouve-la dans ton routeur)
- Reconnecte-toi à l'interface : `http://<ip_de_l_esp32>`
- Renseigne les URLs des Shellys :
  - Réseau : `http://<ip_shelly_reseau>/rpc/EM1.GetStatus?id=0`
  - Solaire : `http://<ip_shelly_solaire>/rpc/EM1.GetStatus?id=1`

C'est en place. Les LEDs et l'OLED reflètent les mesures temps réel.

## Configuration

Toute la config persistante est stockée en NVS (espace `solartracker`). Détail dans [config.cpp:36-44](config.cpp#L36-L44).

| Clé NVS | Description | Modifiable via |
|---|---|---|
| `ssid` / `pass` | Credentials WiFi STA | Dashboard (reboot ensuite) |
| `urlMain` / `urlSolar` | URLs des deux Shellys | Dashboard |
| `admUser` | Nom d'utilisateur admin | NVS direct uniquement (par défaut `admin`) |
| `admHash` / `admSalt` | Hash SHA256 + sel du mot de passe admin | Dashboard |
| `apPass` | Mot de passe AP fallback (12 hex aléatoires, généré au 1er boot) | Auto |
| `otaPass` | Mot de passe OTA (20 hex aléatoires, généré au 1er boot) | Dashboard (bouton régénérer) |
| `firstBoot` | Flag pour bandeau "change ton password" | Auto |

## Utilisation quotidienne

Le dashboard expose :

- **Temps réel** : conso/injection réseau, production solaire, graphe scrollant des 4 dernières minutes (120 points x 2 s)
- **Status connexion** : LED verte = OK, LED rouge = Shelly injoignable
- **Système** : IP, état WiFi, heap libre, uptime

Le WebSocket pousse chaque mesure côté navigateur sans polling. Si tu vois le badge passer en rouge, vérifie que les URLs des Shellys sont valides et que les Shellys sont joignables sur le LAN.

## Mise à jour OTA (Arduino IDE)

Une fois l'ESP32 sur ton WiFi, plus besoin de câble USB.

1. Dans Arduino IDE : `Outils > Port` → tu verras une section "Network ports" avec `solar-tracker at 192.168.x.x (esp32)`
2. Sélectionne ce port
3. `Esquisse > Téléverser`
4. Mot de passe demandé : le mot de passe OTA affiché dans la section **OTA** du dashboard (généré aléatoirement au premier boot, 20 caractères hex)
5. Pour régénérer un nouveau mot de passe OTA : dashboard → carte "OTA" → bouton **Régénérer**. L'ESP32 redémarre, retiens le nouveau mot de passe.

### OTA du LittleFS (fichiers HTML/CSS/JS)

Le plugin `arduino-littlefs-upload` supporte aussi l'upload réseau. Si tu ne le vois pas dans le menu, fallback : passe en USB juste pour l'upload du filesystem (pas besoin pour le firmware).

### Si OTA échoue

- Vérifie que tu es sur le même réseau WiFi que l'ESP32
- mDNS (`solar-tracker.local`) peut ne pas marcher selon ton routeur — utilise l'IP directe
- Vérifie qu'aucun firewall ne bloque le port 3232 (port OTA Arduino par défaut)
- Si bloqué, fallback USB

## Maintenance

### Logs

Tout passe sur Serial à 115200 :
- `[BOOT]`, `[WIFI]`, `[SHELLY]`, `[WEB]`, `[AUTH]`, `[OTA]`
- Les échecs de login affichent l'IP et le compteur (lockout après 5)
- Les erreurs Shelly affichent l'URL et le code HTTP

### Reset usine (effacer la config NVS)

Si tu veux repartir de zéro (mot de passe oublié, WiFi changé sans accès au dashboard, etc.) :

Option 1 : flash un sketch temporaire qui appelle `Preferences prefs; prefs.begin("solartracker"); prefs.clear();`.

Option 2 : depuis le moniteur série, utilise la commande `esptool` pour effacer toute la flash :
```
esptool.py --port /dev/cu.usbserial-XXXX erase_flash
```
Puis re-flash normal. Tu repartiras en mode AP avec `admin/admin`.

### Sauvegarde / restauration

La config tient en quelques octets en NVS. Une sauvegarde "snapshot" peut se faire en exposant un endpoint `GET /api/config/export` (non implémenté à ce jour — voir TODO).

## Sécurité

### Modèle de menace assumé

- **Périmètre LAN uniquement.** Le projet **ne doit JAMAIS être exposé sur Internet** sans VPN ou reverse proxy avec HTTPS.
- Attaquants considérés : autre appareil sur le même WiFi, invité, voisin sniffant le WiFi.
- **Pas considérés** : attaquant physique avec accès USB (peut tout flasher), root sur ton PC.

### Ce qui est protégé

| Mesure | Localisation |
|---|---|
| Mot de passe admin hashé SHA256 + sel 16 octets aléatoires | [config.cpp:30-34](config.cpp#L30-L34) |
| Comparaison en temps constant (anti-timing-attack) | [config.cpp:87-94](config.cpp#L87-L94) |
| Cookie de session : `HttpOnly` + `SameSite=Strict` + TTL 30 min | [web_portal.cpp:39](web_portal.cpp#L39) |
| Token 24 octets aléatoires (esp_random) | [auth.cpp:24-34](auth.cpp#L24-L34) |
| Rate-limit : 5 tentatives / 5 min → lockout 10 min par IP | [auth.cpp:18-22](auth.cpp#L18-L22) |
| WebSocket authentifié au handshake via cookie de session | [web_portal.cpp:71-89](web_portal.cpp#L71-L89) |
| Mot de passe OTA aléatoire par device (20 hex), stocké NVS, regénérable | [config.cpp:103-106](config.cpp#L103-L106) |
| Mot de passe AP fallback aléatoire par device (12 hex), stocké NVS | [config.cpp:60-63](config.cpp#L60-L63) |
| Comparaisons wrap-around safe (millis() après 49 jours) | [auth.cpp:25-27](auth.cpp#L25-L27) |

### Ce qui n'est PAS protégé

- **HTTP en clair** : login et cookie de session sont sniffables sur le LAN. Pour ajouter HTTPS :
  - meilleure option : reverse proxy (Caddy, Nginx) sur un Raspberry Pi/NAS, terminaison TLS + reverse en HTTP vers l'ESP32 ;
  - alternative lourde : `esp_https_server` avec certif auto-signé (warnings navigateur, ~1 s de handshake).
- **Pas de protection contre l'attaquant physique** : quelqu'un avec un USB peut flasher n'importe quoi.
- **Le Shelly EM lui-même** : si l'auth Shelly n'est pas activée, tout le LAN peut le lire/écrire. Active l'auth basic sur les Shellys (stockage des credentials côté ESP32 : à implémenter, non fait actuellement).

### Bonnes pratiques au déploiement

1. **Changer le mot de passe admin par défaut** (`admin/admin`) dès le premier login — un bandeau te le rappelle.
2. **Noter le mot de passe OTA** affiché dans le dashboard après le premier flash USB — sans lui, plus d'OTA possible. Régénération possible via le bouton dédié.
3. **Utiliser WPA2 ou WPA3** sur ton réseau WiFi, jamais WEP ni open.
4. **Ne pas commiter les credentials** — la config est en NVS, donc rien de sensible dans le repo. Le `.gitignore` couvre `secrets.h` et `.env` au cas où tu en ajoutes.
5. **Mettre à jour les libs** régulièrement (`ESPAsyncWebServer` notamment, qui a des CVE historiques sur le fork abandonné).
6. **Couper l'AP fallback** une fois tout configuré ? Non, l'AP n'apparaît que si la connexion STA échoue — donc bénin si ton WiFi est stable. Si tu veux le désactiver totalement pour réduire la surface, modifie [network_mgr.cpp](network_mgr.cpp).

### Audit log

Actuellement : Serial uniquement. Tout passage par `[AUTH]` est loggué (succès, échec, lockout). Pour de la persistance, on pourrait écrire dans un fichier `events.log` en LittleFS — voir TODO.

## Dépannage

### L'OLED reste vide
- Vérifie le câblage SDA/SCL (GPIO21/22)
- Vérifie l'alim du SH1106 (3.3 V, pas 5 V sur certaines variantes)
- Adresse I2C : SH1106 utilise 0x3C par défaut, U8g2 le gère

### Mode AP au lieu de STA
- Logs série : `[WIFI] STA connection timed out`
- Vérifie SSID/password depuis le portail AP (mauvais mdp = retour AP)
- Bande 2.4 GHz uniquement (ESP32 classique ne fait pas 5 GHz)

### Dashboard affiche `--` partout
- Les URLs Shelly sont vides ou incorrectes → menu Shellys
- Logs série : `[SHELLY] HTTP <code>` ou `JSON parse error`
- Teste directement depuis ton navigateur : `http://<ip_shelly>/rpc/EM1.GetStatus?id=0` doit renvoyer du JSON

### Login refusé alors que mot de passe correct
- Tu es en lockout (5 échecs récents). Attends 10 min ou redémarre l'ESP32 (les compteurs sont en RAM)
- L'IP source n'est peut-être pas celle attendue si tu passes par un proxy/double NAT — débranche le proxy

### OTA refusé
- Mauvais password OTA
- Pas sur le même réseau
- Port 3232 bloqué côté firewall

## Structure du repo (référence rapide)

| Fichier | Rôle |
|---|---|
| [solar_tracker.ino](solar_tracker.ino) | `setup()`, `loop()`, init OTA, polling Shelly |
| [config.h](config.h) / [config.cpp](config.cpp) | `AppConfig` + lecture/écriture NVS + hash mot de passe |
| [auth.h](auth.h) / [auth.cpp](auth.cpp) | Sessions, tokens, rate-limit |
| [network_mgr.h](network_mgr.h) / [network_mgr.cpp](network_mgr.cpp) | WiFi STA + AP fallback + NTP |
| [shelly.h](shelly.h) / [shelly.cpp](shelly.cpp) | Appel HTTP `EM1.GetStatus` + parsing JSON |
| [display.h](display.h) / [display.cpp](display.cpp) | OLED (U8g2) + pilotage des 5 LEDs |
| [web_portal.h](web_portal.h) / [web_portal.cpp](web_portal.cpp) | ESPAsyncWebServer + WS + routes API |
| [data/login.html](data/login.html) | Formulaire de login |
| [data/index.html](data/index.html) | Dashboard |
| [data/style.css](data/style.css) | Style |
| [data/app.js](data/app.js) | Logique JS + WebSocket client |
| [.gitignore](.gitignore) | Fichiers OS / IDE / artifacts ignorés |

## TODO / améliorations futures

Priorités proposées si tu veux étendre :

- [ ] Auth Basic vers les Shellys (config dans le dashboard, header `Authorization` dans [shelly.cpp](shelly.cpp))
- [ ] Historique long terme (LittleFS ou InfluxDB push) + export CSV
- [ ] HTTPS via reverse proxy Caddy (doc + exemple Caddyfile)
- [ ] Endpoint `GET /api/config/export` + `POST /api/config/import` pour backup
- [ ] Audit log persistant en LittleFS (avec rotation)
- [ ] Watchdog `esp_task_wdt` pour reboot automatique sur freeze
- [ ] Notifications (webhook Discord/Slack) sur seuils dépassés
- [ ] Mode économie d'énergie : OLED éteint la nuit (entre 23h et 6h)
- [ ] Tests unitaires sur la logique LEDs et le calcul des seuils
