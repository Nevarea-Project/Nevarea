set -euo pipefail
PRESET="${1:-linux-gcc}"
cmake --preset "$PRESET"
cmake --build --preset "$PRESET"
