# sober-cursor-fix

**Показывает обычный курсор мыши в Sober на Wayland и убирает собственный
курсор Roblox.**

[English version →](README.md)

Sober скрывает курсор рабочего стола и рисует свой. Этот инструмент исправляет
и то, и другое: системный курсор остаётся видимым, а дубликат курсора Roblox
исчезает.

## Быстрый старт

```sh
git clone https://github.com/teapond/my-project.git
cd my-project
./install.sh
```

Без `sudo` и без root. Всё устанавливается в вашу домашнюю папку.

После этого откройте **Sober** из меню приложений.

> Не хотите использовать git? На GitHub нажмите **Code → Download ZIP**,
> распакуйте архив, откройте терминал в этой папке и выполните `./install.sh`.

## Что делает установщик

1. Собирает маленький Wayland-прокси (`wlproxy`).
2. Кладёт его и скрипт-обёртку в папку данных Sober.
3. Создаёт прозрачные текстуры курсоров Roblox из установленного `base.apk`.
4. Добавляет ярлык в меню, который запускает Sober через обёртку.

Если чего-то не хватает, установщик напишет, что именно установить и какой
командой это сделать в вашем дистрибутиве.

## Требования

- Linux с сессией **Wayland** (проверено на KDE Plasma 6)
- **Sober**, установленный через Flatpak
- `cc`/`gcc` + `make` и `python3`
  (если их нет, установщик покажет команду для вашего дистрибутива)

## После установки

- Запускайте Sober из **меню приложений**.
- Если Sober уже был запущен, закройте его и откройте снова.
- Запуск через `flatpak run org.vinegarhq.Sober` из терминала обходит
  исправление. Используйте ярлык из меню или:

```sh
flatpak run --command="$HOME/.var/app/org.vinegarhq.Sober/data/sober-wrapper.sh" org.vinegarhq.Sober
```

## Удаление

```sh
./uninstall.sh
```

или:

```sh
./install.sh --uninstall
```

Скрипт возвращает стандартный ярлык и удаляет только те файлы, которые создал
установщик. Остальные файлы в `asset_overlay` не трогаются.

## Если что-то не работает

<details>
<summary><b>Курсор всё ещё скрыт</b></summary>

Посмотрите лог:

```sh
tail -f ~/.var/app/org.vinegarhq.Sober/data/wlproxy.log
```

Когда мышь входит в окно Sober, там должны появляться строки вида
`drop wl_pointer.set_cursor(NULL)`. Если лог пустой, убедитесь, что Sober
запущен из ярлыка в меню, а не обычной командой `flatpak run`.
</details>

<details>
<summary><b>Видно два курсора</b></summary>

Текстуры курсоров не применились. Убедитесь, что Roblox скачан, снова выполните
`./install.sh` и перезапустите Sober.
</details>

<details>
<summary><b>Roblox обновился</b></summary>

Просто снова выполните `./install.sh`. Скрипт заново создаст текстуры для новой
версии.
</details>

<details>
<summary><b>Хочу технические подробности</b></summary>

Смотрите раздел [Как это устроено](#как-это-устроено).
</details>

## Как это устроено

### Прокси курсора

`wlproxy` слушает приватное имя сокета (`sober-wl-proxy`) и подключается к
настоящему сокету композитора. Обёртка выставляет
`WAYLAND_DISPLAY=sober-wl-proxy` для Sober и `SOBER_WL_REAL=wayland-N` для
прокси.

Прокси отслеживает граф объектов ровно настолько, чтобы найти объекты
`wl_pointer` (`wl_display.get_registry` → `wl_registry.bind` →
`wl_seat.get_pointer`), и удаляет вызовы `set_cursor` с аргументом surface =
`NULL`. Когда родительский процесс (Sober) завершается, прокси выходит сам.

Собрать вручную:

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

## Структура проекта

```
install.sh                установщик
uninstall.sh              удаление
src/wlproxy.c             Wayland-прокси курсора
scripts/sober-wrapper.sh  обёртка запуска для ярлыка
scripts/gen_overlay.py    генератор прозрачных текстур
```

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

---

MADE WITH AI
