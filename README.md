# RAM Audio (C++)

Порт `real_ram_audio.py` на C++ с двумя режимами вывода:

- `file` - генерация и сохранение в WAV-файл.
- `stream` - непрерывный RAW PCM поток в `stdout` (удобно для пайпа в плеер/энкодер).

Также добавлены:

- параметры командной строки;
- реестр алгоритмов для простого расширения синтеза;
- сборка через CMake.

## Требования

- Linux (`/proc/<pid>/mem`, `/proc/<pid>/maps`)
- CMake 3.16+
- Компилятор с поддержкой C++17 (GCC/Clang)
- root/sudo для чтения памяти процессов

## Сборка

```bash
cmake -S . -B build
cmake --build build -j
```

Бинарник: `build/ram_audio`

## Быстрый запуск

### 1) Режим сохранения в WAV

```bash
sudo ./build/ram_audio --mode file --output real_ram_symphony_cpp.wav --duration 180
```

### 2) Режим стрима (RAW PCM S16LE, mono)

```bash
sudo ./build/ram_audio --mode stream --duration 60 --sample-rate 44100 | ffplay -f s16le -ar 44100 -ac 1 -
```

### 3) Бесконечный стрим (до Ctrl+C)

```bash
sudo ./build/ram_audio --mode stream --infinite --sample-rate 44100 | ffplay -f s16le -ar 44100 -ac 1 -
```

Для снижения underrun можно увеличить буфер:

```bash
sudo ./build/ram_audio --mode stream --infinite --sample-rate 44100 --buffer-ms 1000 | aplay -f S16_LE -r 44100 -c 1
```

Альтернатива через ALSA:

```bash
sudo ./build/ram_audio --mode stream --duration 60 --sample-rate 44100 | aplay -f S16_LE -r 44100 -c 1
```

## Параметры CLI

Посмотреть все опции:

```bash
./build/ram_audio --help
```

Основные параметры:

- `--mode file|stream` - режим вывода.
- `--output <path>` - путь WAV-файла (для `file`).
- `--duration <sec>` - длительность генерации.
- `--infinite` - бесконечная генерация до `Ctrl+C` (только `--mode stream`).
- `--sample-rate <hz>` - частота дискретизации.
- `--buffer-ms <ms>` - буфер stream-вывода в миллисекундах (`0` отключает буфер).
- `--max-memory-mb <mb>` - лимит считанной памяти процесса.
- `--algorithms <a,b,c>` - ограничить набор алгоритмов по ID.
- `--list-algorithms` - вывести доступные алгоритмы.
- `--seed <num>` - фиксированный seed для повторяемости.
- `--quiet` - отключить прогресс-лог.

Параметры полифонии/таймингов:

- `--min-voices`, `--max-voices`
- `--memory-switch-min`, `--memory-switch-max`
- `--voice-spawn-min`, `--voice-spawn-max`

## Архитектура

- `src/ram_audio_engine.*` - движок чтения памяти процессов, управление голосами, микширование.
- `src/algorithms.hpp` - интерфейс алгоритма и публичный реестр.
- `src/algorithms/` - реализации и регистрация алгоритмов по группам:
  - `core_registry.cpp` - реализация реестра;
  - `classic_algorithms.cpp` - базовые алгоритмы;
  - `texture_algorithms.cpp` - текстурные/шумовые;
  - `advanced_algorithms.cpp` - продвинутые спектральные/моделирующие;
  - `musical_algorithms.cpp` - микротональные/полиметрические музыкальные;
  - `common.hpp` - общие helper-функции для алгоритмов.
- `src/audio_io.*` - sink-ы вывода: WAV и RAW stream.
- `src/main.cpp` - CLI, валидация опций, запуск движка.

## Как добавить новый алгоритм

1. Реализуйте класс от `IRamAlgorithm` в `src/algorithms.cpp`:
   - `id()`
   - `generate(...)`
   - `onMemorySizeChanged(...)`

2. Зарегистрируйте фабрику в `createDefaultAlgorithmRegistry()`:

```cpp
registry.registerAlgorithm({
    "my_algorithm",
    "My Algorithm",
    [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
        return std::make_unique<MyAlgorithm>(memorySize, sampleRate, rng);
    },
});
```

3. Пересоберите проект. Новый ID сразу доступен в `--list-algorithms` и `--algorithms`.

## Важно

- Без root/sudo доступ к `/proc/*/mem` чаще всего будет запрещен.
- Стрим-режим выводит бинарный поток PCM в `stdout`, лог пишется в `stderr`.
- При `--mode stream` используется буферизованный вывод (по умолчанию `500 ms`), это уменьшает вероятность underrun в `aplay`.

## Новые алгоритмы

Добавлены дополнительные источники более необычного звука:

- `chaotic_lorenz_fm` - хаотический FM на основе аттрактора Лоренца.
- `pointer_walk_melody` - мелодический random-walk указателя по памяти.
- `granular_freeze_scrub` - гранулярный freeze/scrub из окон RAM.
- `cellular_automata_noise` - шум и ритм на базе эволюции клеточного автомата.
- `resonator_bank_entropy` - резонаторный тон под управлением энтропии RAM-окон.
- `ring_mod_bitplanes` - ring-mod синтез старших/младших битплейнов.
- `karplus_ram_string` - струнный Karplus-Strong с возбуждением из RAM.
- `fractal_byte_terrain` - фрактальный terrain-осциллятор с RAM-модуляцией.
- `microtonal_glitch_grid` - микротональная сетка с нерегулярным шагом и глитч-квантованием.
- `polymeter_euclidean_micro` - полиметрические Euclidean-паттерны с 31/19-EDO высотами.
- `tritave_odd_meter_chords` - аккордовые события в 13-ступенном tritave и нечётных размерах.

Использование только новых алгоритмов:

```bash
sudo ./build/ram_audio --mode stream --infinite --algorithms chaotic_lorenz_fm,pointer_walk_melody,granular_freeze_scrub | aplay -f S16_LE -r 44100 -c 1
```

Экстремальный микс из всех экспериментальных алгоритмов:

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1200 --algorithms chaotic_lorenz_fm,pointer_walk_melody,granular_freeze_scrub,cellular_automata_noise,resonator_bank_entropy,ring_mod_bitplanes,karplus_ram_string,fractal_byte_terrain,microtonal_glitch_grid,polymeter_euclidean_micro,tritave_odd_meter_chords | aplay -f S16_LE -r 44100 -c 1
```

Более музыкальный IDM/микротональный набор:

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1000 --algorithms microtonal_glitch_grid,polymeter_euclidean_micro,tritave_odd_meter_chords,pointer_walk_melody,granular_freeze_scrub | aplay -f S16_LE -r 44100 -c 1
```

## Защита от тишины

В движок добавлен anti-silence guard:

- постоянный `anchor`-голос (не умирает, только плавно входит);
- RMS-проверка энергии микса;
- автоматический аварийный спавн голосов при длительном провале плотности.
