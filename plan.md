# RAM Audio Evolution Plan

## Goal

Сделать движок менее предсказуемым и более "живым" за счет:
- новых источников звука (алгоритмов);
- продвинутого переключения сцен и голосов;
- более умного смешения (mixing + modulation);
- контроля стабильности, производительности и повторяемости.

## Scope

В план включены все предложенные элементы:

- Новые алгоритмы (6):
  - `ram_wavelet_scanner`
  - `markov_byte_lattice`
  - `fdn_prime_feedback`
  - `spectral_freeze_permuter`
  - `modal_mesh_exciter`
  - `bytebeat_formula_evolver`
- Механизмы переключения (6):
  - `scene_conductor`
  - `hmm_with_tabu`
  - `entropy_triggered_switch`
  - `novelty_guard`
  - `genetic_voice_spawn`
  - `chaotic_timing`
- Механизмы смешения (6):
  - `band_split_mixer`
  - `modulation_matrix`
  - `probabilistic_crossfade`
  - `ghost_buffer_blend`
  - `transient_sustain_split`
  - `adaptive_bus_limiter`

## Epics and Tasks (with DoD)

### Epic E1 - Engine Foundation for Conductor and Mixer

#### [x] T1.1 Add conductor and mixer abstractions
- Description: ввести интерфейсы и структуры состояния (`ISwitchPolicy`, `IMixPolicy`, `SceneState`, `VoiceDescriptor`).
- DoD:
  - Добавлены интерфейсы и базовые реализации без ломки текущего поведения.
  - Сборка проходит (`cmake --build`) без новых warnings уровня error.
  - В `--mode file` и `--mode stream` поведение по умолчанию осталось совместимым.

#### [x] T1.2 Add engine telemetry primitives
- Description: реализовать сбор признаков потока: RMS, spectral centroid, flatness, ZCR, плотность транзиентов.
- DoD:
  - Метрики считаются онлайн в окне (например, 2048-8192 сэмплов).
  - Добавлен unit/integration тест на корректный диапазон метрик.
  - При `--quiet` метрики не логируются в stderr, но доступны внутренним модулям.

#### [x] T1.3 Extend CLI and config for new modes
- Description: добавить опции выбора `switch-mode`, `mix-mode`, пороги novelty/entropy, distribution профили.
- DoD:
  - Новые флаги отображаются в `--help`.
  - Невалидные комбинации параметров корректно валидируются с понятной ошибкой.
  - Конфиг передается в `RamAudioEngine` без хардкода значений в runtime.

#### [ ] T1.4 Add deterministic run logging
- Description: журналировать события сцен (switch/spawn/mix profile), чтобы можно было повторить поведение по seed.
- DoD:
  - При заданном `--seed` последовательность событий совпадает между запусками.
  - Лог содержит timestamp sample/time + event type + involved algorithm IDs.
  - Формат лога документирован в README или отдельном разделе docs.

---

### Epic E2 - New Sound Algorithms

#### T2.1 Implement `ram_wavelet_scanner`
- Description: wavelet-анализ окна RAM и озвучивание масштабов/деталей как слой осцилляторов.
- DoD:
  - Есть минимум 3 уровня декомпозиции (low/mid/high detail).
  - Алгоритм корректно переживает смену размера memory snapshot.
  - На 60-сек рендере нет NaN/Inf/денормалов и фатальных клипов.

#### T2.2 Implement `markov_byte_lattice`
- Description: строить вероятностную модель переходов из байтовых окон и генерировать pitch/rhythm события.
- DoD:
  - Реализована матрица переходов с нормализацией и smoothing.
  - При фиксированном `--seed` результат детерминирован.
  - События генерируются без "залипания" в одном состоянии дольше заданного лимита.

#### T2.3 Implement `fdn_prime_feedback`
- Description: feedback delay network с prime-length линиями, параметризованными RAM.
- DoD:
  - Минимум 6 delay-линий, длины выбираются из простых чисел.
  - Есть защита устойчивости (feedback < 1, limiter в контуре или post).
  - Нет runaway-энергии на 30-минутном стриме.

#### T2.4 Implement `spectral_freeze_permuter`
- Description: STFT freeze + перестановка/маскирование бинов на основе RAM-энтропии.
- DoD:
  - Реализованы windowing + overlap-add без слышимых щелчков на границах.
  - Есть freeze/unfreeze и permutation depth.
  - CPU budget укладывается в целевой режим (реалтайм 44.1kHz mono).

#### T2.5 Implement `modal_mesh_exciter`
- Description: модальная/сеточная физическая модель, возбуждаемая RAM-импульсами.
- DoD:
  - Реализована численно стабильная схема (ограничения шага и коэффициентов).
  - Есть управление демпфированием и плотностью возбуждений через macroMod/RAM.
  - Звук остается разнообразным при смене PID/памяти без тишины.

#### T2.6 Implement `bytebeat_formula_evolver`
- Description: генерация и мутация bytebeat-формул с отбором по novelty score.
- DoD:
  - Есть grammar/AST или безопасный генератор формул без UB.
  - Реализованы mutation + selection на основе метрик новизны.
  - Формулы не повторяются чаще заданного tabu-порога.

---

### Epic E3 - Switching Mechanisms

#### [x] T3.1 Implement `scene_conductor`
- Description: двухуровневая драматургия: macro-сцены (30-180 c) + micro-мутации (0.3-4 c).
- DoD:
  - Реализованы отдельные таймеры macro/micro слоев.
  - Каждая macro-сцена задает профиль плотности, спектра и активных семейств алгоритмов.
  - Переходы сцен логируются и воспроизводимы по seed.

#### [x] T3.2 Implement `hmm_with_tabu`
- Description: выбор следующего состояния через матрицу переходов + запрет последних N состояний.
- DoD:
  - Матрица переходов валидируется (строки нормализованы).
  - Tabu-окно реально предотвращает немедленные повторы.
  - Есть fallback-логика при недоступных состояниях (не падает в dead-end).

#### [x] T3.3 Implement `entropy_triggered_switch`
- Description: переключения по событиям роста/падения энтропии RAM и по смене PID.
- DoD:
  - Введены пороги `entropy_delta_up`/`entropy_delta_down` с hysteresis.
  - Смена PID может инициировать scene switch/crossfade без щелчка.
  - Срабатывания не "дребезжат" (cooldown между switch-событиями).

#### [x] T3.4 Implement `novelty_guard`
- Description: защита от повторов через fingerprint аудиопотока.
- DoD:
  - Fingerprint включает минимум RMS + centroid + flatness + ZCR.
  - При превышении similarity threshold запускается recovery-стратегия (switch/spawn/mutation).
  - Частота ложных срабатываний ограничена и настраивается параметром.

#### [ ] T3.5 Implement `genetic_voice_spawn`
- Description: новый голос наследует параметры 2 родительских голосов и мутирует.
- DoD:
  - Реализованы crossover и bounded mutation для всех параметров голоса.
  - Есть защита от невалидных параметров (clamp, sanitize).
  - Spawn-события увеличивают разнообразие без потери стабильности loudness.

#### [ ] T3.6 Implement `chaotic_timing`
- Description: интервалы switch/spawn по log-normal/power-law, а не равномерно.
- DoD:
  - Реализованы минимум 2 распределения + режим auto.
  - Параметры распределений вынесены в конфиг/CLI.
  - Тайминги не создают starvation (слишком редкие события) и не перегружают CPU.

---

### Epic E4 - Mixing and Cross-Modulation

#### [ ] T4.1 Implement `band_split_mixer`
- Description: многополосный микшер (low/mid/high) с дрейфом частот раздела.
- DoD:
  - Реализованы кроссоверы с минимальной фазовой/амплитудной ошибкой для текущих ограничений проекта.
  - Частоты раздела плавно модулируются, без zipper noise.
  - Можно закрепить voice family за полосой (опционально).

#### [ ] T4.2 Implement `modulation_matrix`
- Description: матрица модуляций между голосами/шинами (AM/FM/ring/wavefold depth).
- DoD:
  - Есть N x N routing с ограничением глубины модуляции.
  - Защита от runaway feedback в матрице.
  - Матрица может быть включена/выключена без артефактов перехода.

#### [x] T4.3 Implement `probabilistic_crossfade`
- Description: вероятностный crossfade с hysteresis и min-dwell на сцену.
- DoD:
  - Уход от резких переключений: проверка на отсутствие кликов при смене сцены.
  - Реализован `min_scene_time` и `crossfade_ms`.
  - Вероятности переключений учитывают текущий energy/novelty профиль.

#### [ ] T4.4 Implement `ghost_buffer_blend`
- Description: "призрак" предыдущей сцены в кольцевом буфере, гранулярно подмешиваемый в новую.
- DoD:
  - Используется ограниченный ring buffer с контролируемым memory footprint.
  - Есть параметры depth/decay/grain size.
  - Подмешивание не вызывает накапливающегося шума и DC bias.

#### [ ] T4.5 Implement `transient_sustain_split`
- Description: разделение на transient/sustain шины и раздельный микс по типу контента.
- DoD:
  - Детектор транзиентов работает в реальном времени и имеет настраиваемые пороги.
  - Транзиенты и sustain регулируются независимыми gain/shape параметрами.
  - Нет заметного "дребезга" на границе классификации.

#### [x] T4.6 Implement `adaptive_bus_limiter`
- Description: адаптивный loudness-control и мягкий лимитер на мастер-шине.
- DoD:
  - Реализован soft limiter/clipper, предотвращающий систематический клип.
  - Target loudness держится в целевом коридоре (на практическом отрезке теста).
  - Лимитер не убивает динамику в спокойных сценах.

---

### Epic E5 - QA, Soak Tests, Presets

#### T5.1 Build scenario presets
- Description: подготовить профили запуска (`chaotic`, `ambient`, `rhythmic`, `extreme`).
- DoD:
  - Каждый preset задает `--algorithms`, `--switch-mode`, `--mix-mode`, пороги.
  - Пресеты документированы и повторяемы по seed.
  - Пресеты запускаются без ручной правки кода.

#### T5.2 Long-run stability tests
- Description: soak-тесты 30-120 минут на stream mode.
- DoD:
  - Нет крашей/зависаний/утечек памяти за время теста.
  - CPU usage в пределах ожидаемого бюджета для целевой машины.
  - Нет перманентной тишины благодаря anti-silence + новым политикам.

#### T5.3 Novelty regression checks
- Description: автоматическая проверка, что разнообразие не деградирует между коммитами.
- DoD:
  - Для стандартного прогона сохраняются summary-метрики новизны.
  - При регрессе ниже порога тест сигнализирует о проблеме.
  - Метрики и пороги зафиксированы в документации.

## Critical Path

Ниже задачи, которые определяют минимальный срок доставки рабочего результата:

1. `T1.1` -> базовые абстракции conductor/mixer (иначе нет точки интеграции).
2. `T1.2` -> метрики потока (нужны для `novelty_guard`, `entropy_triggered_switch`, адаптивного микса).
3. `T4.3` + `T4.6` -> безопасные переходы и контроль уровня (иначе артефакты при switch).
4. `T3.1` -> общий `scene_conductor` (каркас сценарной логики).
5. `T3.4` -> `novelty_guard` (ключ к неповторяемости и анти-циклам).
6. `T3.2` -> `hmm_with_tabu` (устойчивая стратегия выбора следующей сцены).
7. `T2.4` + `T2.3` + `T2.1` -> первые "высокоэффектные" новые алгоритмы.
8. `T5.2` -> long-run стабильность перед релизом.

Параллельно (вне критического пути, можно делать в отдельной ветке):
- `T2.2`, `T2.5`, `T2.6`
- `T3.5`, `T3.6`
- `T4.4`, `T4.5`

## Risk Register

| ID | Risk | Probability | Impact | Mitigation | Contingency Trigger |
|---|---|---|---|---|---|
| R1 | Перегруз CPU из-за FFT/wavelet/modal/FDN | High | High | Профилирование, снижение размеров окон, adaptive quality mode | XRuns/underrun, CPU > целевого бюджета |
| R2 | Нестабильность feedback-контуров (`fdn_prime_feedback`, modulation loops) | Medium | High | Ограничение коэффициентов, safety limiter, saturation в контуре | Рост RMS без возврата, клип сериями |
| R3 | Щелчки при переключениях сцен/алгоритмов | High | Medium | Обязательный `probabilistic_crossfade`, сглаживание параметров, min fade time | Слышимые click/pop в A/B тесте |
| R4 | Потеря разнообразия (циклы, повтор паттернов) | Medium | High | `novelty_guard`, `hmm_with_tabu`, tabu для формул/сцен | Similarity > порога на длительном окне |
| R5 | Переусложнение логики и рост времени разработки | High | Medium | Поэтапный rollout, feature flags, MVP по эпикам | Невыполнение milestone > 1 итерации |
| R6 | Утечки/рост памяти (`ghost_buffer_blend`, большие буферы) | Medium | High | Ring buffers с лимитами, RAII, периодические soak-тесты | RSS растет монотонно в soak-тесте |
| R7 | Непредсказуемость слишком сильная, результат музыкально "ломается" | Medium | Medium | Preset-профили, bounded randomness, clamp на экстремальные параметры | Жалобы на неуправляемый шум/хаос |
| R8 | Неповторяемость багов из-за случайности | Medium | Medium | Строгий seed + event log + deterministic replay mode | Баг не воспроизводится двумя прогонами |
| R9 | Ограничения доступа к `/proc/*/mem` и зависимость от окружения | Low | Medium | Валидация доступа при старте, ясные ошибки, fallback на demo-memory режим | Runtime access errors на старте |
| R10 | Регресс loudness/клип после добавления новых алгоритмов | Medium | High | `adaptive_bus_limiter`, автотест уровня, контроль peak/RMS | Частые peak hits и clip warnings |

## Milestone Proposal

- M1 (Foundation): `E1` + `T4.3` + `T4.6`
- M2 (Switch Core): `E3` без `T3.5`
- M3 (Algorithms Wave 1): `T2.1`, `T2.3`, `T2.4`
- M4 (Algorithms Wave 2): `T2.2`, `T2.5`, `T2.6`
- M5 (Mix Advanced): `T4.1`, `T4.2`, `T4.4`, `T4.5`
- M6 (QA/Release): `E5`, risk burn-down, preset finalization

## Global Release DoD

- Все 18 новых элементов (6 + 6 + 6) реализованы и документированы.
- `--list-algorithms` содержит все 6 новых algorithm IDs.
- В `stream --infinite` нет крашей и перманентной тишины в soak-тесте.
- Переключения сцен и микс-политики управляются через CLI/конфиг.
- Зафиксированы baseline-метрики новизны и loudness для регрессионной проверки.
