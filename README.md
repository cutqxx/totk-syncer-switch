# TotK Save Sync

Перенос сохранений **The Legend of Zelda: Tears of the Kingdom** между прошитым Nintendo Switch и PC-портом
([totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp)) через Яндекс Диск (WebDAV).

- **Switch** — homebrew `totk-sync.nro` (этот репозиторий, папка [`switch/`](switch/)): «Отправить на ПК» / «Забрать с ПК».
- **ПК** — лаунчер `pc/totk_sync.py` (сам подтягивает свежий сейв перед игрой и отправляет после).
  > В репозитории его пока нет — добавьте файл в `pc/`.
- **Обмен** — ZIP-файлы в формате JKSV в папке `JKSV/The Legend of Zelda Tears of the Kingdom/` на Диске:
  `SW_YYYY-MM-DD_HH-MM-SS.zip` (со свитча) и `PC_YYYY-MM-DD_HH-MM-SS.zip` (с ПК).

## Статус

| Что | Состояние |
|-----|-----------|
| Фаза 0 (тест загрузки) | Реализована как пункт меню **Upload test**. **Нужен прогон на консоли** (см. ниже). |
| Фаза 1 (push) | Реализована. На железе не проверена. |
| Фаза 2 (pull) | Реализована (бэкап, проверка ZIP, откат при ошибке). На железе не проверена. |
| Фаза 3 | Частично: статус «на Диске есть более свежий PC-сейв» в пункте **Check connection**. |
| Хост-тесты | `make -C switch/tests`: ZIP/мета/PROPFIND/конфиг/бэкапы + WebDAV против мок-сервера. |
| Сборка `.nro` | GitHub Actions (`devkitpro/devkita64`), артефакт `totk-sync-nro`. |

## Сборка

**GitHub Actions** — каждый push собирает `.nro`; скачать: вкладка *Actions* → последний запуск → артефакт `totk-sync-nro`.

**Docker:**
```bash
docker build -t totk-sync-build switch/
```
```bash
docker run --rm -v "$PWD/switch:/src" totk-sync-build
```
Результат: `switch/totk-sync.nro`.

**Локально с devkitPro:** нужны пакеты `switch-dev switch-curl switch-mbedtls switch-zlib`, затем `make -C switch`.

**Хост-тесты** (Linux, нужны `gcc`, `libcurl` dev-заголовки, `python3`):
```bash
make -C switch/tests
```

## Установка на Switch

1. Скопировать `totk-sync.nro` в `sd:/switch/totk-sync/totk-sync.nro`.
2. Создать `sd:/config/totk-sync/config.json` по образцу [`switch/config.example.json`](switch/config.example.json):
   ```json
   {
     "origin": "https://webdav.yandex.com",
     "basepath": "JKSV/The Legend of Zelda Tears of the Kingdom",
     "username": "LOGIN",
     "password": "APP_PASSWORD",
     "keep_backups": 20
   }
   ```
   - `password` — **пароль приложения** Яндекса типа «Файлы WebDAV», не основной пароль.
   - Используйте `webdav.yandex.com` — `.ru` со свитча падал по тайм-ауту.
   - Если файла нет, берутся `origin/username/password/basepath` из JKSV (`sd:/config/JKSV/webdav.json`),
     к `basepath` дописывается `/The Legend of Zelda Tears of the Kingdom`.
   - Необязательные поля: `"use_temp_name": false` — грузить сразу в финальное имя без `.part` + `MOVE`;
     `"cainfo": "sdmc:/config/totk-sync/cacert.pem"` — свой файл CA (см. «TLS»).
3. Запустить из hbmenu (обычный режим через Альбом подходит).

Реальный `config.json` в git не попадает (`.gitignore`), пароль не пишется в лог.

## Использование

Интерфейс на английском: в консольном шрифте libnx нет кириллицы.

| Пункт | Что делает |
|-------|------------|
| **Send to PC** | Проверяет, что TotK не запущена, монтирует сейв только для чтения, собирает ZIP в памяти (мета `.nx_save_meta.bin` + файлы + записи каталогов), создаёт папку на Диске при необходимости (`MKCOL`), грузит в `SW_<время>.zip.part` и переименовывает `MOVE` в `SW_<время>.zip`. |
| **Get from PC** | Находит самый свежий `PC_*.zip` (по `getlastmodified`, затем по имени), спрашивает подтверждение, скачивает, проверяет ZIP, делает бэкап текущего сейва на SD, очищает сейв, записывает файлы с коммитами, сообщает об успехе только после финального коммита. |
| **Check connection / status** | `PROPFIND` по папке: HTTP-код и объяснение (401 — логин/пароль, 404 — нет папки, тайм-аут — сеть), самые свежие `SW_`/`PC_` и подсказка, если PC-сейв новее. |
| **Upload test** | Диагностика Фазы 0 (ниже). |
| **Change user** | Выбор пользователя консоли (если пользователь один — выбирается сам). |

Файлы на SD:
- `sd:/switch/totk-sync/log.txt` — лог (шаги, HTTP-коды, коды curl; без пароля). При >512 КБ переименовывается в `log.txt.old`.
- `sd:/switch/totk-sync/backups/<время>_before_pull.zip` — бэкапы перед каждым pull, хранятся последние `keep_backups`.
  Это обычный ZIP в формате JKSV: чтобы восстановить вручную, положите его в папку игры JKSV
  (`sd:/JKSV/The Legend of Zelda Tears of the Kingdom/`) и восстановите из JKSV.

## Безопасность сейва

- В сейв ничего не пишется без бэкапа на SD; если бэкап не удался — операция прерывается.
- Перед записью (и ещё раз после скачивания) проверяется, что TotK не запущена и не приостановлена
  (`pmdmnt`: процесс с title ID `0100F2C0115B6000`). Если проверить не удалось — отказ.
- Скачанный ZIP проверяется до любых изменений: целостность (CRC каждого файла), наличие `option.sav` и
  `slot_00/progress.sav`, отсутствие опасных путей (`..`, абсолютные пути), title ID в мете (если она есть).
- Запись: сейв очищается, файлы пишутся с коммитом после каждого файла и до переполнения журнала
  (как в JKSV). Кнопка HOME на время записи блокируется.
- Если запись не удалась, утилита автоматически восстанавливает сейв из только что сделанного бэкапа.
- На Диске утилита ничего не удаляет (удаление через WebDAV безвозвратно).

## Фаза 0: тест загрузки

Пункт **Upload test**. Данные — `sd:/switch/totk-sync/test.zip`, если он есть, иначе 4 МБ случайных байт.
Файлы грузятся в папку игры как `TEST_<время>_v<N>.bin` (расширение не `.zip`, чтобы лаунчер их не трогал;
удалите их с Диска вручную). Повторы в этом режиме отключены, чтобы видеть сырой результат.

- **A** — только режим из ТЗ: `Content-Length` из буфера, без `Expect: 100-continue`, HTTP/1.1.
- **X** — все варианты:

| # | Вариант | Зачем |
|---|---------|-------|
| 1 | length, no Expect | режим утилиты |
| 2 | length + Expect | как JKSV при загрузке нового файла |
| 3 | chunked, no Expect | |
| 4 | chunked + Expect | как JKSV при перезаписи существующего файла |
| 5 | length, буфер 16 КБ | влияние размера буфера отправки |
| 6 | режим 1 на `webdav.yandex.ru` | сравнение `.ru` / `.com` |

Для каждого печатаются HTTP-код, код curl, время и скорость (и пишутся в `log.txt`).

**Приёмка:** вариант 1 даёт `201`/`204`, файл появляется на Диске. Если вариант 1 падает так же, как JKSV
(curl 56), — внести результаты сюда и перейти на запасной путь (ниже).

### Что уже известно из исходников JKSV

Гипотеза из ТЗ подтвердилась лишь частично. В актуальном JKSV (`source/remote/WebDav.cpp`):
- `upload_file` (новый файл) **задаёт** `CURLOPT_INFILESIZE_LARGE`, т.е. отправляет `Content-Length`,
  но не отключает `Expect: 100-continue`;
- `patch_file` (перезапись существующего) **не задаёт** размер → curl шлёт `Transfer-Encoding: chunked`
  и тоже с `Expect`;
- тайм-аутов (`CONNECT_TIMEOUT`, `LOW_SPEED_*`) нет, `CURLOPT_HTTP_VERSION` не задан.

Поэтому кандидаты на причину curl 56 — `Expect: 100-continue` и/или chunked. Варианты 1–4 теста разделяют эти факторы.

### Результаты на консоли

_Заполнить после прогона:_

| # | HTTP | curl | время | примечание |
|---|------|------|-------|------------|
| 1 | | | | |
| 2 | | | | |
| 3 | | | | |
| 4 | | | | |
| 5 | | | | |
| 6 | | | | |

## Технические детали

**HTTP** (libcurl из portlibs devkitPro): `PUT` из буфера в памяти с `CURLOPT_INFILESIZE_LARGE` (без chunked), пустой
заголовок `Expect:`, HTTP/1.1, Basic-авторизация. Тайм-ауты: подключение 15 с; `PROPFIND`/`MKCOL`/`MOVE` — 60 с;
загрузка и скачивание — до 600 с, но обрыв при скорости ниже 512 Б/с дольше 30 с. При сетевой ошибке, 5xx или 429 —
ещё 2 попытки с паузой 3 с; 4xx не повторяются.

**TLS:** проверка сертификата включена (`VERIFYPEER=1`, `VERIFYHOST=2`). Пакет `switch-curl` в devkitPro
собирается с `--with-default-ssl-backend=libnx` (системный сервис `ssl` консоли), который проверяет сертификат по системному
хранилищу CA, поэтому файл сертификатов не нужен (JKSV тоже не задаёт `CAINFO`). Если «Check connection» покажет
ошибку сертификата (curl 60/77), положите PEM-бандл (например, `cacert.pem` с curl.se) в
`sd:/config/totk-sync/cacert.pem` — он подхватится сам — или укажите путь в `cainfo`.

**MOVE:** `Destination: <полный URL>`, `Overwrite: F`. Если на Яндексе `MOVE` не сработает, утилита
загрузит файл сразу в финальное имя, а `.part` останется на Диске (ничего не удаляется). Если это повторяется,
поставьте `"use_temp_name": false`.

**ZIP:** miniz (vendored, MIT, `switch/third_party/miniz`), DEFLATE уровень 6. Порядок: `.nx_save_meta.bin`,
затем содержимое сейва по алфавиту с записями каталогов (`album/`, `slot_00/`, …). Мусор не попадает в ZIP и не
пишется в сейв: `.nx_save_meta.bin`, `*.7z`, `*.zip`, `*.rar`, `desktop.ini`, `Thumbs.db`.
При pull поддерживается ZIP, где сейв лежит во вложенной папке (корень определяется по `option.sav`).

**`.nx_save_meta.bin`** (86 байт, packed LE, структура `fs::SaveMetaData` из JKSV):

| Смещение | Поле | Размер |
|---|---|---|
| 0x00 | magic `JKSV` | 4 |
| 0x04 | revision (`01`) | 1 |
| 0x05 | applicationID | 8 |
| 0x0D | accountID (uid) | 16 |
| 0x1D | systemSaveID | 8 |
| 0x25 | saveDataType (1 = Account) | 1 |
| 0x26 | saveDataRank | 1 |
| 0x27 | saveDataIndex | 2 |
| 0x29 | ownerID | 8 |
| 0x31 | timestamp | 8 |
| 0x39 | flags | 4 |
| 0x3D | saveDataSize | 8 |
| 0x45 | journalSize | 8 |
| 0x4D | commitID | 8 |
| 0x55 | saveDataSpaceID | 1 |

Поля заполняются из `FsSaveDataExtraData` (`fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId`), как в JKSV.
Пример из ТЗ разбирается и собирается обратно байт в байт (хост-тест). При pull, если в мете `saveDataSize` больше
текущего, сейв расширяется (`fsExtendSaveDataFileSystem`), как в JKSV.

## Структура

```
switch/
  Makefile, Dockerfile, config.example.json
  source/
    main.c          меню и сценарии push / pull / status / upload test
    save_switch.c   libnx: пользователи, проверка запуска игры, монтирование, коммиты, расширение сейва
    webdav.c        PROPFIND / MKCOL / PUT / MOVE / GET на libcurl
    propfind.c      разбор ответа PROPFIND
    savezip.c       сборка / проверка / распаковка ZIP
    savemeta.c      .nx_save_meta.bin
    config.c        config.json + фолбэк на JKSV webdav.json
    backups.c       ротация бэкапов на SD
    util.c, log.c
  tests/            хост-тесты + мок WebDAV-сервер (python)
  third_party/miniz
.github/workflows/build.yml
```

## Тест-план (§7 ТЗ)

| # | Сценарий | Чем покрыто |
|---|----------|-------------|
| 1 | Push при корректном конфиге | хост: PUT/MOVE на моке; консоль — проверить |
| 2 | Неверный пароль | хост: 401 без повторов, текст «Wrong login or app password (401)» |
| 3 | Нет сети / тайм-аут | хост: отказ подключения → 3 попытки и объяснение; консоль: проверка nifm |
| 4 | Нет удалённой папки | хост: создаётся через MKCOL по сегментам |
| 5 | Pull свежего PC-ZIP | хост: проверка + распаковка с коммитами; консоль — проверить |
| 6 | Pull при запущенной TotK | консоль — проверить (pmdmnt) |
| 7 | Битый ZIP / ZIP другой игры | хост: отклоняются (CRC, нет файлов, чужой title ID, `../`) |
| 8 | Нет `PC_*.zip` | сообщение «nothing to get» |
| 9 | Полный круг Switch → PC → Switch | консоль + ПК — проверить |
| 10 | ZIP утилиты восстанавливается JKSV | формат совпадает с JKSV; консоль — проверить |

Первые проверки pull — с заранее сделанным бэкапом в JKSV.

## Запасной путь (если Фаза 0 провалится)

Switch-утилиту не использовать. JKSV как есть, транспорт через ПК:
```bash
rclone serve webdav "<папка Яндекс Диска>\JKSV" --addr :8080 --user USER --pass PASS
```
В `sd:/config/JKSV/webdav.json` указать `http://<IP ПК>:8080`. Свитч грузит по локальной сети, клиент Диска сам
отправляет в облако. Лаунчер не меняется.

## Вне рамок

Другие игры, Steam Deck/Linux, другие облака. Игровые файлы, ключи и прошивка в репозиторий не кладутся.
