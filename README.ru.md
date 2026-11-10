# sober-cursor-fix

Показывает **системный курсор** в [Sober](https://github.com/VinegarHQ/Sober)
и убирает собственный курсор Roblox.

> Неофициальное решение. Не связано с Roblox или VinegarHQ.

## Проблема

Sober рисует Roblox через SDL. В Wayland SDL скрывает курсор композитора
запросом

```c
wl_pointer.set_cursor(serial, NULL, 0, 0)
```

и при этом никогда не просит показать свой курсор. В итоге над окном Sober
курсор невидим. Кроме того, Roblox рисует собственный курсор из PNG-текстур,
поэтому даже при видимом системном курсоре их было бы два.

## Что делает исправление

1. **`wlproxy`** — маленький прокси Wayland-сокета между Sober и композитором.
   Он разбирает только направление клиент → сервер и отбрасывает запросы
   `wl_pointer.set_cursor(..., NULL, ...)`. Всё остальное, включая передачу
   файловых дескрипторов через `SCM_RIGHTS`, проходит без изменений.
2. **Прозрачные текстуры курсора** — PNG-курсоры Roblox заменяются полностью
   прозрачными того же размера через механизм
   [`asset_overlay`](https://vinegarhq.org/Sober/Configuration/TipsAndTricks.html).
   В результате остаётся только курсор композитора.

## Требования

- Linux с сессией Wayland (проверено на KDE Plasma 6)
- Sober, установленный через Flatpak (`org.vinegarhq.Sober`)
- Компилятор C и `make` для сборки прокси
- `python3` для генерации оверлея курсоров

## Установка

```sh
git clone https://github.com/<ваш-username>/sober-cursor-fix.git
cd sober-cursor-fix
./scripts/install.sh
```

Установка выполняется для текущего пользователя и не требует root. Скрипт:

- собирает `wlproxy` (статический бинарник);
- кладёт его и скрипт-обёртку в `~/.var/app/org.vinegarhq.Sober/data/`;
- читает установленный `base.apk` Roblox и создаёт прозрачные текстуры курсоров
  в `~/.var/app/org.vinegarhq.Sober/data/sober/asset_overlay/`;
- устанавливает ярлык в `~/.local/share/applications/`, который запускает Sober
  через обёртку.

После этого запускайте **Sober из меню приложений** (предварительно закройте
запущенный экземпляр).

Если Roblox ещё не скачан, запустите Sober один раз и повторите установку,
чтобы сгенерировать оверлей.

## Ручной запуск

Обычная команда `flatpak run org.vinegarhq.Sober` обходит обёртку. Используйте
ярлык из меню или:

```sh
flatpak run --command="$HOME/.var/app/org.vinegarhq.Sober/data/sober-wrapper.sh" \
    org.vinegarhq.Sober
```

## Удаление

```sh
./scripts/uninstall.sh
```

Скрипт возвращает стандартный ярлык и удаляет только те текстуры, которые
установил он сам; остальные файлы в `asset_overlay` не трогаются.

## Как это устроено

### Прокси курсора

`wlproxy` слушает приватное имя сокета (`sober-wl-proxy`) и подключается к
настоящему сокету композитора. Обёртка выставляет
`WAYLAND_DISPLAY=sober-wl-proxy` для Sober и `SOBER_WL_REAL=wayland-N` для
прокси.

Прокси отслеживает граф объектов ровно настолько, чтобы найти объекты
`wl_pointer` (`wl_display.get_registry` → `wl_registry.bind` →
`wl_seat.get_pointer`), и удаляет вызовы `set_cursor` с аргументом surface =
`NULL`. Прокси также завершается сам, когда умирает родительский процесс
(Sober).

Сборка:

```sh
make static      # полностью статический бинарник
# или
make             # динамический
```

### Текстуры курсора

`scripts/gen_overlay.py` открывает `base.apk`, находит все
`assets/content/textures/Cursors/**.png` (и
`assets/content/textures/ArrowCursor.png`), читает размеры каждого PNG и
записывает полностью прозрачный RGBA-PNG того же размера в `asset_overlay`.
Sober предпочитает файлы из `asset_overlay` файлам из APK.

## Если что-то не работает

- **Курсор всё ещё скрыт.** Проверьте
  `~/.var/app/org.vinegarhq.Sober/data/wlproxy.log`: при входе указателя в окно
  должны появляться строки `drop wl_pointer.set_cursor(NULL)`. Для подробного
  лога задайте `WLPROXY_DEBUG=1`.
- **Видно два курсора.** Оверлей не применился. Убедитесь, что APK существует,
  повторно запустите `./scripts/install.sh` и перезапустите Sober.
- **Roblox обновился.** Имена или размеры файлов курсоров могли измениться.
  Повторно запустите `./scripts/install.sh`.
- **Запуск из терминала.** `flatpak run org.vinegarhq.Sober` не использует
  обёртку — см. раздел «Ручной запуск».
- **Сессия X11.** Там Sober использует X11, исправление ничего не делает.
- **Несколько экземпляров Sober.** Прокси использует фиксированное имя сокета,
  поэтому поддерживается только один экземпляр.

## Что устанавливается

```
~/.var/app/org.vinegarhq.Sober/data/
├── wlproxy                # бинарник прокси
├── sober-wrapper.sh       # обёртка запуска
├── wlproxy.log            # лог
└── sober/asset_overlay/   # прозрачные текстуры курсоров
~/.local/share/applications/org.vinegarhq.Sober.desktop
```

## Лицензия

MIT — см. [LICENSE](LICENSE).
