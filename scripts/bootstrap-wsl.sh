#!/usr/bin/env bash
# Policybook WSL toolchain bootstrap.
# Idempotent, sudo-free: everything installs under $HOME.
set -euo pipefail

command -v curl >/dev/null 2>&1 || { echo "ERROR: curl is required"; exit 1; }

mkdir -p "$HOME/opt" "$HOME/.local/bin"

# --- 1. Node 22 (Linux x64 tarball into ~/opt/node22) ------------------------
if [ -x "$HOME/opt/node22/bin/node" ]; then
  echo "== node already present: $("$HOME/opt/node22/bin/node" --version)"
else
  echo "== installing node 22"
  VER=$(curl -fsSL https://nodejs.org/dist/latest-v22.x/ | grep -oP 'node-v\K22\.[0-9]+\.[0-9]+' | head -1)
  [ -n "$VER" ] || { echo "ERROR: could not resolve latest v22 version"; exit 1; }
  echo "   latest v22 is $VER"
  curl -fsSL "https://nodejs.org/dist/v${VER}/node-v${VER}-linux-x64.tar.xz" -o /tmp/node22.tar.xz
  rm -rf "$HOME/opt/node22"
  mkdir -p "$HOME/opt/node22"
  tar -xJf /tmp/node22.tar.xz -C "$HOME/opt/node22" --strip-components=1
  rm -f /tmp/node22.tar.xz
  echo "   installed $("$HOME/opt/node22/bin/node" --version)"
fi

export PATH="$HOME/opt/node22/bin:$HOME/.local/bin:$PATH"

# --- 2. pnpm 10 (global install into the node prefix) ------------------------
if [ -x "$HOME/opt/node22/bin/pnpm" ]; then
  echo "== pnpm already present: $(pnpm --version)"
else
  echo "== installing pnpm 10"
  npm install -g pnpm@10
fi

# --- 3. uv -------------------------------------------------------------------
if [ -x "$HOME/.local/bin/uv" ]; then
  echo "== uv already present: $("$HOME/.local/bin/uv" --version)"
else
  echo "== installing uv"
  curl -LsSf https://astral.sh/uv/install.sh | sh
fi

# --- 4. cmake + ninja (PyPI wheels via uv tool) ------------------------------
command -v cmake >/dev/null 2>&1 || { echo "== installing cmake"; uv tool install cmake; }
command -v ninja >/dev/null 2>&1 || { echo "== installing ninja"; uv tool install ninja; }

# --- 5. Python venv with test/lint tooling -----------------------------------
if [ ! -x "$HOME/.venvs/policybook/bin/python" ]; then
  echo "== creating venv ~/.venvs/policybook"
  uv venv "$HOME/.venvs/policybook" -p 3.12
fi
echo "== installing pytest/mypy/ruff into the venv"
uv pip install --python "$HOME/.venvs/policybook/bin/python" pytest mypy ruff

# --- 6. pnpm store on ext4 ---------------------------------------------------
pnpm config set store-dir "$HOME/.pnpm-store" --global

# --- 7. env file sourced by every verify command -----------------------------
cat > "$HOME/.policybook-env" <<'EOF'
export PATH="$HOME/opt/node22/bin:$HOME/.local/bin:$HOME/.venvs/policybook/bin:$PATH"
export PNPM_HOME="$HOME/opt/node22/bin"
export CC=gcc
EOF
echo "== wrote ~/.policybook-env"
echo "== bootstrap complete"
