#!/bin/bash
set -e

# install-nvim.sh
# This script builds Toy Forth tools and installs them to a local directory for Neovim (Linux/macOS).

# --- CONFIGURATION ---
# Default installation directory
INSTALL_DIR="${HOME}/.local/share/toyforth"
# ---------------------

# Get absolute path of the repository root
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LSP_SRC_DIR="${REPO_ROOT}/tools/toyforth-lsp"
TS_SRC_DIR="${REPO_ROOT}/tools/tree-sitter-toyforth"

echo -e "\033[0;36m--- Toy Forth Local Installation (Linux/macOS) ---\033[0m"
echo -e "\033[0;90mTarget Installation Directory: ${INSTALL_DIR}\033[0m"

# 1. Create Target Directory
mkdir -p "${INSTALL_DIR}"

# 2. Build the LSP
echo -e "\n\033[0;33m[1/3] Building Toy Forth LSP...\033[0m"
if ! command -v go &> /dev/null; then
    echo "Error: Go is not installed. Please install Go to build the LSP."
    exit 1
fi

pushd "${LSP_SRC_DIR}" > /dev/null
    go build -o toyforth-lsp ./cmd/toyforth-lsp
    cp toyforth-lsp "${INSTALL_DIR}/"
    echo -e "\033[0;32mLSP installed to: ${INSTALL_DIR}/toyforth-lsp\033[0m"
popd > /dev/null

# 3. Generate and Copy Tree-sitter files
echo -e "\n\033[0;33m[2/3] Generating Tree-sitter Parser...\033[0m"
if ! command -v npm &> /dev/null; then
    echo "Error: npm is not installed. Please install Node.js/npm."
    exit 1
fi

pushd "${TS_SRC_DIR}" > /dev/null
    echo "Running npm install..."
    npm install --silent
    
    echo "Generating parser.c..."
    if command -v tree-sitter &> /dev/null; then
        tree-sitter generate
    else
        npx tree-sitter generate
    fi

    TS_DEST="${INSTALL_DIR}/tree-sitter-toyforth"
    rm -rf "${TS_DEST}"
    mkdir -p "${TS_DEST}"
    
    cp -r src queries grammar.js tree-sitter.json "${TS_DEST}/"
    echo -e "\033[0;32mTree-sitter files installed to: ${TS_DEST}\033[0m"
popd > /dev/null

# 4. Output Neovim Configuration
LSP_PATH="${INSTALL_DIR}/toyforth-lsp"
TS_PATH="${INSTALL_DIR}/tree-sitter-toyforth"

echo -e "\n\033[0;36m[3/3] --- Neovim Configuration Snippet ---\033[0m"
echo -e "\033[0;90mAdd the following to your init.lua:\033[0m"

cat <<EOF

-- Toy Forth LSP Configuration
vim.lsp.config("toyforth_lsp", {
    cmd = { "$LSP_PATH" },
    filetypes = { "toyforth" },
    root_markers = { ".git", "README.md" },
})
vim.lsp.enable("toyforth_lsp")

-- Toy Forth Tree-sitter Configuration
require("nvim-treesitter.parsers").get_parser_configs().toyforth = {
    install_info = {
        url = "$TS_PATH",
        files = { "src/parser.c" },
        branch = "main",
    },
    filetype = "toyforth",
}

-- Add queries and parser info to runtime path
vim.opt.rtp:append("$TS_PATH")

-- Register filetypes
vim.filetype.add({
    extension = {
        fth = "toyforth",
        tf = "toyforth",
    },
})
EOF

echo -e "\n\033[0;33mAfter updating your config, restart Neovim and run :TSInstall toyforth\033[0m"
