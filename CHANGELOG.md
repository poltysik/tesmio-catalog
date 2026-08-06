# Changelog

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
