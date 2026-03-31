#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  bash scripts/stream_ffplay.sh [duration_sec]
  bash scripts/stream_ffplay.sh --infinite

Examples:
  bash scripts/stream_ffplay.sh 60
  bash scripts/stream_ffplay.sh --infinite

Env:
  RAM_AUDIO_SAMPLE_RATE   default: 44100
  RAM_AUDIO_BUFFER_MS     default: 1000
  RAM_AUDIO_SEED          optional, fixed seed
EOF
}

duration_arg=(--duration 60)
if [[ $# -gt 1 ]]; then
    usage
    exit 1
fi

if [[ $# -eq 1 ]]; then
    if [[ "$1" == "--infinite" ]]; then
        duration_arg=(--infinite)
    elif [[ "$1" =~ ^[0-9]+$ ]] && [[ "$1" -gt 0 ]]; then
        duration_arg=(--duration "$1")
    else
        usage
        exit 1
    fi
fi

sample_rate="${RAM_AUDIO_SAMPLE_RATE:-44100}"
buffer_ms="${RAM_AUDIO_BUFFER_MS:-1000}"
seed_value="${RAM_AUDIO_SEED:-}"

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/.." && pwd)"
engine_bin="${project_root}/build/ram_audio"

if [[ ! -x "${engine_bin}" ]]; then
    echo "Engine binary not found: ${engine_bin}" >&2
    echo "Build first: cmake -S . -B build && cmake --build build -j" >&2
    exit 1
fi

echo "[sudo] Сейчас будет запрос пароля (в этом терминале)." >&2
sudo -k
sudo --prompt='[sudo] Пароль для %u: ' -v
echo "[ok] Пароль принят, запускаю stream -> ffplay" >&2

engine_cmd=(
    sudo
    "${engine_bin}"
    --mode stream
    "${duration_arg[@]}"
    --sample-rate "${sample_rate}"
    --buffer-ms "${buffer_ms}"
)

if [[ -n "${seed_value}" ]]; then
    engine_cmd+=(--seed "${seed_value}")
fi

"${engine_cmd[@]}" | ffplay -hide_banner -nostats -loglevel warning -f s16le -ar "${sample_rate}" -ch_layout mono -
