# Changelog

## 1.2.2 — WRSR 1.1.1.9 and TesmioLoader B0.3.6 compatibility

### Русский

- Tesmio Catalog адаптирован для Workers & Resources: Soviet Republic 1.1.1.9
  и обновлённого TesmioLoader B0.3.6.
- Обновлена привязка к штатной нижней строительной панели новой сборки игры.
- Исправлено некорректное отображение карточек зданий: карточки, избранное и
  навигация больше не накладываются друг на друга.
- На более высоких экранах сохраняется сетка из четырёх карточек; высота
  карточек автоматически подстраивается под доступное место.
- Интерфейс проверен на разрешениях 1366×768, 1440×900, 1600×900,
  1920×1080 и 2560×1440, включая нестандартный масштаб интерфейса.
- Добавлена предварительная проверка отрисовщика панели: при будущей
  несовместимости каталог отключится до внесения изменений в интерфейс игры.

### English

- Adapted Tesmio Catalog for Workers & Resources: Soviet Republic 1.1.1.9
  and the updated TesmioLoader B0.3.6.
- Updated the native bottom construction toolbar integration for the new game
  build.
- Fixed incorrect building-card rendering so cards, Favorites and pagination
  no longer overlap.
- Taller screens retain the four-card grid with card height fitted to the
  available space.
- Verified the interface at 1366×768, 1440×900, 1600×900, 1920×1080 and
  2560×1440, including custom UI scaling.
- Added a renderer preflight check so a future incompatible game build is
  rejected before the catalog changes the native toolbar.

## 1.2.1 — First-open input fix

### Русский

- Исправлено первое открытие каталога, при котором карточки зданий могли не
  реагировать до закрытия и повторного открытия окна.
- Нажатие на кнопку каталога теперь полностью отпускается до включения карточек.
- Кэш доступности автоматически перепроверяется после первого полного кадра
  штатного строительного меню.

### English

- Fixed the first catalog session occasionally ignoring building-card clicks
  until the window was closed and reopened.
- The toolbar click is now fully released before catalog controls are armed.
- Availability is refreshed after the native construction menu completes its
  first full frame.

## 1.2.0 — Toolbar hitbox and catalog performance update

### Русский

Версия 1.2 исправляет невидимую область нажатия над кнопкой каталога и
уменьшает нагрузку при просмотре большого числа построек из базовой игры и
Steam Workshop.

#### Исправлено

- Удалена расширенная невидимая область нажатия над нижней строительной
  панелью. Каталог теперь открывается только при нажатии на его видимую кнопку.
- Обработка кнопки передана штатной системе попадания интерфейса игры, поэтому
  её активная область совпадает с изображением при разных разрешениях и
  масштабах UI.
- Устранён постоянный повтор тяжёлой проверки доступности и исследований у
  видимых карточек на каждом кадре.
- Уменьшены продолжительные просадки производительности при просмотре и
  переключении источников `Базовая игра` и `Workshop`.

Проверка доступности теперь сохраняется в кэше на время текущего сеанса
каталога и обновляется при его повторном открытии. Первичная загрузка ранее не
просмотренных изображений всё ещё может вызвать короткую разовую задержку.

### English

Version 1.2 removes an invisible clickable area above the catalog button and
reduces the cost of browsing large Vanilla and Steam Workshop collections.

#### Fixed

- Removed the oversized invisible hitbox above the bottom construction
  toolbar. The catalog now opens only from its visible button.
- Delegated button hit testing to the game's native UI, keeping the active
  area aligned with the icon at different resolutions and UI scales.
- Stopped repeating the expensive availability and research checks for visible
  cards on every frame.
- Reduced sustained performance drops while browsing or switching between the
  `Vanilla buildings` and `Workshop buildings` sources.

Availability details are now cached for the current catalog session and
refreshed when the catalog is reopened. Loading previously unseen preview
images may still cause a short one-time delay.

## 1.1.0 — Workshop compatibility update

### Русский

Версия 1.1 исправляет обнаружение зданий из Steam Workshop и добавляет более
безопасную автоматическую классификацию модифицированных построек.

#### Добавлено

- Дополнительное сканирование общего реестра строительных объектов игры.
- Поддержка Workshop-построек, не привязанных к стандартным вкладкам
  строительного меню.
- Тип `Не определено` для построек, отрасль которых невозможно установить
  достоверно.
- Консервативное определение металлургической и атомной отрасли по производимым
  ресурсам и их ванильным прототипам в `resources.ini`.
- Поддержка медной производственной цепочки TesmioLoader: Copper Mine, Copper
  Concentrate Factory, Copper Smelter и Electrolysis Plant.

#### Исправлено

- Каталог больше не показывает только новые ресурсы, пропуская связанные с
  ними физические Workshop-здания.
- Постройки с нестандартной или отсутствующей категорией больше не теряются.
- Неопределённые постройки больше не получают случайную первую категорию,
  например `Атомная отрасль`.
- Workshop-здание не считается источником Tesmio только из-за использования
  ресурсов TesmioLoader.
- Усилена защита от повторных карточек одного здания, зарегистрированного через
  несколько строительных инструментов.

Каталог сначала читает стандартные строительные вкладки, затем проверяет общий
реестр игры. Если отрасль нельзя установить по проверяемым признакам, здание
попадает в `Не определено`. После установки новых Workshop-построек достаточно
полностью перезапустить игру через TesmioLauncher; новый мир не требуется.

### English

Version 1.1 improves Steam Workshop building discovery and introduces safer
automatic classification for modded construction objects.

#### Added

- A secondary scan of the game's global construction-tool registry.
- Support for Workshop buildings not assigned to standard construction-menu
  groups.
- An `Undefined` type for buildings whose industry cannot be determined
  reliably.
- Conservative metallurgy and nuclear-industry inference from produced
  resources and their vanilla templates in `resources.ini`.
- Support for the TesmioLoader copper production chain: Copper Mine, Copper
  Concentrate Factory, Copper Smelter and Electrolysis Plant.

#### Fixed

- The catalog no longer discovers new resources while omitting their physical
  Workshop buildings.
- Workshop buildings with custom, unknown or absent menu categories are no
  longer skipped.
- Unclassified buildings no longer receive the first available category, such
  as `Nuclear industry`.
- A Workshop building is not classified as Tesmio solely because it consumes
  or produces TesmioLoader resources.
- Duplicate protection was strengthened for buildings exposed through several
  construction-tool wrappers.

The catalog first reads the standard construction menu and then scans the
game's global registry. If an industry cannot be established from verifiable
signals, the building is placed in `Undefined`. After installing new Workshop
buildings, fully restart the game through TesmioLauncher; a new game is not
required.
