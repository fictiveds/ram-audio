import wave
import struct
import os
import sys
import random
import math

def clamp(val):
    return max(-32768, min(32767, int(val)))

def get_all_pids():
    """Возвращает список всех активных PID в системе."""
    pids = []
    for d in os.listdir('/proc'):
        if d.isdigit():
            pids.append(d)
    return pids

def get_process_memory(pid):
    """Считывает доступные регионы памяти реального процесса, отсеивая пустые куски."""
    maps_file = f"/proc/{pid}/maps"
    mem_file = f"/proc/{pid}/mem"
    
    if not os.path.exists(maps_file) or not os.path.exists(mem_file):
        return bytearray()
        
    memory_dump = bytearray()
    
    try:
        with open(maps_file, 'r') as map_f, open(mem_file, 'rb', 0) as mem_f:
            for line in map_f:
                parts = line.split()
                if len(parts) < 2: continue
                
                address_range = parts[0]
                perms = parts[1]
                
                if 'r' not in perms: continue
                    
                start_addr, end_addr = [int(addr, 16) for addr in address_range.split('-')]
                size = end_addr - start_addr
                
                if size < 4096 or size > 200 * 1024 * 1024: continue
                    
                try:
                    mem_f.seek(start_addr)
                    chunk_size = 65536
                    for offset in range(0, size, chunk_size):
                        chunk = mem_f.read(min(chunk_size, size - offset))
                        if not chunk: break
                        if chunk != b'\x00' * len(chunk):
                            memory_dump.extend(chunk)
                            if len(memory_dump) > 60 * 1024 * 1024:
                                break
                except OSError:
                    continue
                if len(memory_dump) > 60 * 1024 * 1024:
                    break
    except PermissionError:
        print(f"Ошибка доступа. Запустите скрипт через sudo: sudo python3 {sys.argv[0]}")
        sys.exit(1)
    except Exception:
        pass
        
    return memory_dump

def get_random_process_memory():
    """Случайно выбирает подходящий процесс (PID) и возвращает его память."""
    while True:
        pids = get_all_pids()
        target_pid = random.choice(pids)
        
        try:
            with open(f"/proc/{target_pid}/comm", "r") as f:
                name = f.read().strip()
        except Exception:
            name = "unknown"
            
        memory = get_process_memory(target_pid)
        memory_size = len(memory)
        
        if memory_size > 50 * 1024:
            print(f"\n[+] Взломано сознание: {name} (PID: {target_pid}). Считано: {memory_size / 1024 / 1024:.2f} MB.")
            return memory, memory_size

class SynthVoice:
    """Голос синтезатора — одна из 8 независимых техник чтения памяти."""
    def __init__(self, memory_size, sample_rate):
        self.type = random.randint(1, 8)
        self.ptr = random.randint(0, memory_size - 1)
        self.accum = 0.0
        self.timer = 0
        self.sample_rate = sample_rate
        self.vol = random.uniform(0.3, 0.9)
        
        # Голос живет от 5 до 25 секунд независимо от других
        self.life_samples = int(random.uniform(5.0, 25.0) * sample_rate)
        self.age = 0
        
        # Индивидуальный даунсэмплинг (Sample & Hold) превращает шум в тоны
        self.downsample = int(random.uniform(1, 60))
        if self.type in [6, 7]: # PM и ритмы лучше звучат с высоким разрешением
            self.downsample = random.choice([1, 1, 3, 10])
            
        self.current_val = 0.0
        self.hold_count = 0
        
        # Индивидуальные параметры (делают каждый запуск уникальным)
        self.p1 = random.uniform(0.1, 10.0)
        self.p2 = int(random.uniform(8, 2048))
        self.p3 = random.randint(1, 255)
        
    def is_dead(self):
        return self.age >= self.life_samples
        
    def tick(self, i, memory, memory_size, macro_mod):
        self.age += 1
        
        # Огибающая (Envelope) — голос плавно появляется и плавно исчезает (Crossfade)
        env = 1.0
        fade_len = self.sample_rate * 2.0  # Фейд-ин/аут 2 секунды
        if self.age < fade_len:
            env = self.age / fade_len
        elif self.life_samples - self.age < fade_len:
            env = (self.life_samples - self.age) / fade_len
            if env < 0: env = 0
            
        # Sample & Hold (квантование времени)
        self.hold_count += 1
        if self.hold_count >= self.downsample:
            self.hold_count = 0
            self.current_val = self._generate(i, memory, memory_size, macro_mod)
            
        return self.current_val * env * self.vol
        
    def _generate(self, i, memory, memory_size, macro_mod):
        # 1. Hilbert Drone
        if self.type == 1:
            rs = int(1 + macro_mod * 15 * self.p1)
            x = (i * rs) ^ ((i * rs) >> 1)
            return (memory[(x * 2654435761) % memory_size] - 128) * 350
            
        # 2. Bit-Slicing Arpeggios 
        elif self.type == 2:
            idx = int(self.ptr + i * self.p1) % memory_size
            mask = 0b11110000 if macro_mod > 0.5 else 0b00001111
            val = (memory[idx] & mask) ^ (memory[(idx - 1024) % memory_size] & mask)
            return (val - 128) * 500
            
        # 3. Wavefolding Delta Bass
        elif self.type == 3:
            idx = (self.ptr + int(i * 0.2 * self.p1)) % memory_size
            self.accum += (memory[idx] - 128) * (1.0 + macro_mod * 4.0)
            if self.accum > 16000: self.accum -= 32000
            elif self.accum < -16000: self.accum += 32000
            return self.accum
            
        # 4. Wavetable Granular Loop
        elif self.type == 4:
            frag = int(self.p2 + macro_mod * 100)
            idx = (self.ptr + (i % max(1, frag))) % memory_size
            val = memory[idx]
            if i % int(self.sample_rate * self.p1) == 0:
                self.ptr = (self.ptr + val * self.p3) % memory_size
            return (val - 128) * 400
            
        # 5. Low-pass Float Aliasing
        elif self.type == 5:
            idx = (self.ptr + int(i * 0.5 * self.p1)) % (memory_size - 4)
            val8 = memory[idx] - 128
            try: val16 = struct.unpack_from('<h', memory, idx)[0]
            except: val16 = 0
            return (val8 * 256 * (1.0 - macro_mod)) + (val16 * macro_mod * 0.5)
            
        # 6. Memory Phase Modulation (FM-синтез под управлением ОЗУ)
        elif self.type == 6:
            idx = (self.ptr + int(i * self.p1)) % memory_size
            mod = memory[idx] / 255.0
            phase = (i * 440.0 * self.p1 / self.sample_rate) + mod * 15.0
            return math.sin(phase) * 10000
            
        # 7. Percussive Rhythm Triggers (Бочка/Глитч)
        elif self.type == 7:
            idx = (self.ptr + int(i / 100)) % memory_size
            val = memory[idx]
            if val > 240 and self.timer <= 0:
                self.timer = int(self.sample_rate * 0.1 * self.p1)
                self.ptr = (self.ptr + val * self.p3) % memory_size
            if self.timer > 0:
                self.timer -= 1
                env = self.timer / (self.sample_rate * 0.1 * self.p1)
                return math.sin(self.timer * env * self.p1) * 12000 * env
            return 0
            
        # 8. Byte-beat Процессор
        elif self.type == 8:
            t = int((i * self.p1 * 0.1) + self.ptr)
            try: val = (t * ((t>>12|t>>8)&63&t>>4)) & 255
            except: val = 0
            return (val - 128) * 200
            
        return 0

def generate_ram_audio(output_filename, duration_sec=180, sample_rate=44100):
    print(f"Генерация {duration_sec} секунд глубокой полифонической ОЗУ-интерференции...")
    
    num_samples = duration_sec * sample_rate
    audio_data = bytearray()
    
    memory, memory_size = get_random_process_memory()
    
    # Полифонический генератор (Одновременно звучат несколько голосов)
    voices = [SynthVoice(memory_size, sample_rate)]
    next_voice_time = sample_rate * random.uniform(1.0, 5.0)
    
    pid_timer = sample_rate * random.uniform(15.0, 30.0)
    smoothed_sample = 0.0
    
    def lfo(phase, freq):
        return math.sin(2 * math.pi * freq * phase / sample_rate)
    
    for i in range(num_samples):
        # 1. Глобальная смена "подопытного" процесса
        if pid_timer <= 0:
            memory, memory_size = get_random_process_memory()
            pid_timer = sample_rate * random.uniform(15.0, 40.0)
            for v in voices:
                v.ptr = v.ptr % memory_size # Спасаем голоса от выхода за пределы
        pid_timer -= 1
        
        # 2. Рандомный спавн новых живых алгоритмов
        next_voice_time -= 1
        # Одновременно могут играть от 2 до 6 совершенно разных алгоритмов
        if next_voice_time <= 0 and len(voices) < random.randint(2, 6):
            voices.append(SynthVoice(memory_size, sample_rate))
            next_voice_time = sample_rate * random.uniform(2.0, 8.0)
            
        macro_mod = (lfo(i, 0.05) + 1.0) / 2.0 
        
        # 3. Микширование всех живых голосов
        mixed_sample = 0.0
        alive_voices = []
        for v in voices:
            mixed_sample += v.tick(i, memory, memory_size, macro_mod)
            if not v.is_dead():
                alive_voices.append(v)
                
        voices = alive_voices

        # 4. Общий Low-Pass фильтр (мастеринг)
        filter_cutoff = 0.03 + macro_mod * 0.2 
        smoothed_sample += filter_cutoff * (mixed_sample - smoothed_sample)
        
        # Суммирование голосов создает натуральный эффект дисторшна (fuzz), 
        # когда они превышают лимит в 16-бит, так как clamp обрежет пики.
        audio_data.extend(struct.pack('<h', clamp(smoothed_sample)))
        
        if i % sample_rate == 0 and i > 0:
            # Небольшой индикатор полифонии
            print(f"Синтез... {i // sample_rate} / {duration_sec} сек. [Голосов активно: {len(voices)}]", end="\r")
            
    with wave.open(output_filename, 'w') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(audio_data)
        
    print(f"\nГотово! Артефакт сохранен: {output_filename}")

if __name__ == "__main__":
    if os.geteuid() != 0:
        print(f"Использование: sudo python3 {sys.argv[0]}")
        sys.exit(1)
        
    output_wav = "/mnt/disk_f/Projects/RAM-audio/real_ram_symphony.wav"
    generate_ram_audio(output_wav, duration_sec=180)



