import wave
import struct
import os

def clamp(val):
    return max(-32768, min(32767, int(val)))

def generate_ram_audio(output_filename, duration_sec=180, sample_rate=44100):
    num_samples = duration_sec * sample_rate
    
    # 1. Source Data (Simulated RAM Dump)
    # We load system binaries to get structured "digital trash" 
    # to mimic a memory dump closely.
    memory = bytearray()
    targets = ["/bin/bash", "/lib/x86_64-linux-gnu/libc.so.6", "/usr/bin/python3"]
    for t in targets:
        if os.path.exists(t):
            with open(t, "rb") as f:
                memory.extend(f.read(5 * 1024 * 1024)) # read up to 5MB from each
    
    if len(memory) < 1024:
        # Fallback to random noise if binaries can't be read
        memory = bytearray(os.urandom(10 * 1024 * 1024))
        
    memory_size = len(memory)
    print(f"Loaded {memory_size / 1024 / 1024:.2f} MB of data to simulate RAM.")
    
    audio_data = bytearray()
    
    # State variables
    delta_accum = 0.0
    ptr = 0
    
    # Pseudo-fractal jump to simulate spatial proximity mapping
    def hilbert_index(i, size):
        x = i ^ (i >> 1)
        return (x * 2654435761) % size
        
    print(f"Synthesizing {duration_sec} seconds of audio...")
    segment_length = num_samples // 5
    
    for i in range(num_samples):
        technique = (i // segment_length) + 1
        
        if technique == 1:
            # 1. Hilbert Curve Scanning (Fractal)
            # Structuring the noise by reading through a non-linear mapping
            idx = hilbert_index(i, memory_size)
            val = memory[idx]
            sample = (val - 128) * 200
            
        elif technique == 2:
            # 2. Bit-Slicing & Masking
            idx = (i * 2) % memory_size
            prev_idx = (i - 1000) % memory_size
            val = memory[idx] & 0b10001000  # isolate specific bits
            val ^= memory[prev_idx]         # XOR with a previous position
            sample = (val - 128) * 800      # Exaggerate the limited bit depth
            
        elif technique == 3:
            # 3. Delta-Encoding Synthesis
            idx = (i * 3) % memory_size
            byte_val = memory[idx]
            delta = byte_val - 128
            delta_accum += delta * 18
            delta_accum *= 0.994  # Decay acting as a low-pass filter
            sample = delta_accum
            
        elif technique == 4:
            # 4. Pointer-Hopping Oscillators
            val = memory[ptr]
            ptr = (ptr + val * 67 + 1) % memory_size # Jump to a new address
            sample = (val - 128) * 256
            
        else: # technique == 5
            # 5. Memory Aliasing (Sound Mirages)
            idx = (i * 4) % (memory_size - 4)
            # Interpret block as 8-bit int
            val8 = memory[idx] - 128
            # Interpret block as 16-bit int
            val16 = struct.unpack_from('<h', memory, idx)[0]
            # Mixing the interpretations
            sample = (val8 * 256 * 0.4) + (val16 * 0.6)
            
        audio_data.extend(struct.pack('<h', clamp(sample)))
        
        if i % sample_rate == 0 and i > 0:
            print(f"Progress: {i // sample_rate} / {duration_sec} seconds", end="\r")
            
    with wave.open(output_filename, 'w') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2) # 16-bit
        wf.setframerate(sample_rate)
        wf.writeframes(audio_data)
        
    print(f"\nDone! Audio saved to {output_filename}")

if __name__ == "__main__":
    generate_ram_audio("/mnt/disk_f/Projects/RAM-audio/ram_symphony.wav", duration_sec=180)
