#!/bin/bash

set -e
WORKSPACE="/workspace"
VENV_DIR="$WORKSPACE/mcdreforged_venv"
VENV_PIP="$VENV_DIR/bin/pip"
VENV_PY="$VENV_DIR/bin/python"
VENV_MCD="$VENV_DIR/bin/mcdreforged"

echo "=== 雨云纯环境面板 mcdreforged 自动安装与启动脚本 (精简版) ==="

########################################
# Step 1: 切换清华源
########################################
echo "[1/5] 切换 apt 源为清华镜像..."
. /etc/os-release
CODENAME=$VERSION_CODENAME

cat >/etc/apt/sources.list <<EOF
deb https://mirrors.tuna.tsinghua.edu.cn/debian/ $CODENAME main contrib non-free non-free-firmware
deb https://mirrors.tuna.tsinghua.edu.cn/debian/ $CODENAME-updates main contrib non-free non-free-firmware
deb https://mirrors.tuna.tsinghua.edu.cn/debian/ $CODENAME-backports main contrib non-free non-free-firmware
deb https://mirrors.tuna.tsinghua.edu.cn/debian-security/ $CODENAME-security main contrib non-free non-free-firmware
EOF

# 清理残留
for f in /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
    if [ -f "$f" ]; then
        mv "$f" "$f.bak"
    fi
done

rm -rf /var/lib/apt/lists/*
apt-get clean
apt-get update -y
echo "✅ apt 源已切换完成"

########################################
# Step 2: 安装 Python 基础依赖
########################################
echo "[2/5] 安装 Python 环境依赖..."
# 这里不再包含 JDK 安装，仅保留 Python 运行 MCDR 所需的环境
apt install -y python3 python3-venv python3-distutils curl wget build-essential

########################################
# Step 3: 创建虚拟环境
########################################
if [ ! -d "$VENV_DIR" ]; then
    echo "[3/5] 创建虚拟环境: $VENV_DIR"
    python3 -m venv "$VENV_DIR"
else
    echo "[3/5] 虚拟环境已存在，跳过创建"
fi

########################################
# Step 4: 安装/升级 mcdreforged
########################################
echo "[4/5] 使用清华源安装/升级 mcdreforged..."
"$VENV_PIP" install --upgrade pip --index-url https://pypi.tuna.tsinghua.edu.cn/simple
"$VENV_PIP" install mcdreforged --index-url https://pypi.tuna.tsinghua.edu.cn/simple

########################################
# Step 5: 检测初始化并启动
########################################
echo "[5/5] 检测运行环境..."

# 检查是否需要初始化 (判断核心目录是否存在)
if [ ! -d "$WORKSPACE/server" ] || [ ! -f "$WORKSPACE/config.yml" ]; then
    echo "检测到未初始化，正在执行 mcdreforged init..."
    "$VENV_MCD" init
    echo "✅ 初始化完成！请将你的服务端放入 server 文件夹，修改 config.yml 后再次运行脚本。"
    exit 0
fi

echo "🚀 正在启动 mcdreforged..."
exec "$VENV_MCD" start