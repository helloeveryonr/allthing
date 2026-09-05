import subprocess
import sys
import os
import platform
from pathlib import Path
from mcdreforged.api.types import PluginServerInterface
from mcdreforged.api.command import SimpleCommandBuilder, GreedyText  # 改用 GreedyText

PLUGIN_METADATA = {
    'id': 'command_runner',
    'version': '1.2.6',
    'name': 'Command Runner',
    'description': 'Execute system and python commands in game (cross-platform)',
    'author': 'YourName',
    'link': 'https://github.com/'
}

REQUIRED_PERMISSION = 4
IS_WINDOWS = platform.system() == 'Windows'

def get_venv_python():
    current = sys.executable
    for base in [os.path.dirname(current), os.path.dirname(os.path.dirname(current))]:
        if os.path.exists(os.path.join(base, 'pyvenv.cfg')):
            return current
    if 'conda' in current or os.environ.get('CONDA_PREFIX'):
        return current
    mcdr_path = Path(sys.argv[0]).parent if hasattr(sys, 'argv') else Path.cwd()
    candidates = []
    if IS_WINDOWS:
        candidates = ['./venv/Scripts/python.exe', './.venv/Scripts/python.exe']
    else:
        candidates = ['./venv/bin/python', './.venv/bin/python']
    for p in candidates:
        full = mcdr_path / p
        if full.exists():
            return str(full)
    return current

def on_load(server: PluginServerInterface, old):
    server.venv_python = get_venv_python()
    server.logger.info(f'Python: {server.venv_python}')
    server.logger.info(f'OS: {platform.system()} {platform.release()}')

    builder = SimpleCommandBuilder()
    # 使用 GreedyText 吸收所有后续内容作为命令/代码
    builder.arg('command', GreedyText)
    builder.arg('code', GreedyText)

    builder.command('!!cmd', lambda src: src.reply('§6用法: !!cmd sys <命令> | !!cmd python <代码> | !!cmd py <代码> | !!cmd info'))
    builder.command('!!cmd sys <command>', lambda src, ctx: execute_system(src, ctx['command'], server))
    builder.command('!!cmd python <code>', lambda src, ctx: execute_python(src, ctx['code'], server))
    builder.command('!!cmd py <code>', lambda src, ctx: execute_python(src, ctx['code'], server))
    builder.command('!!cmd info', lambda src: show_info(src, server))

    builder.register(server)
    server.register_help_message('!!cmd', '执行系统命令 / Python 代码 (仅服主)')
    server.logger.info('Command Runner loaded!')

def execute_system(src, command: str, server):
    if src.get_permission_level() < REQUIRED_PERMISSION:
        src.reply('§c需要服主权限')
        return
    src.reply(f'§e执行: {command}')
    try:
        env = os.environ.copy()
        venv_path = os.path.dirname(os.path.dirname(server.venv_python))
        if os.path.exists(os.path.join(venv_path, 'pyvenv.cfg')):
            env['VIRTUAL_ENV'] = venv_path
            bin_dir = os.path.join(venv_path, 'Scripts' if IS_WINDOWS else 'bin')
            if os.path.exists(bin_dir):
                env['PATH'] = f"{bin_dir}{os.pathsep}{env.get('PATH', '')}"
        proc = subprocess.run(command, shell=True, capture_output=True, text=True,
                              timeout=30, encoding='utf-8', errors='replace', env=env)
        
        if proc.stdout:
            # 记录完整输出到终端
            server.logger.info(f"[Command Runner - System Out] {command}\n{proc.stdout.strip()}")
            out = proc.stdout.strip()
            if len(out) > 1500:
                out = out[:1500] + '\n... (截断)'
            src.reply(f'§a[输出]\n{out}')
            
        if proc.stderr:
            # 记录完整错误到终端
            server.logger.error(f"[Command Runner - System Err] {command}\n{proc.stderr.strip()}")
            err = proc.stderr.strip()
            if len(err) > 1500:
                err = err[:1500] + '\n... (截断)'
            src.reply(f'§c[错误]\n{err}')
            
        if not proc.stdout and not proc.stderr:
            src.reply('§7(无输出)')
        src.reply(f'§7返回码: {proc.returncode}')
        
    except subprocess.TimeoutExpired:
        src.reply('§c命令超时 (30秒)')
    except Exception as e:
        src.reply(f'§c错误: {e}')

def execute_python(src, code: str, server):
    if src.get_permission_level() < REQUIRED_PERMISSION:
        src.reply('§c需要服主权限')
        return
    src.reply(f'§e执行代码: {code}')
    import tempfile
    tf = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', suffix='.py', delete=False, encoding='utf-8') as f:
            f.write(code)
            tf = f.name
        proc = subprocess.run([server.venv_python, tf], capture_output=True, text=True,
                              timeout=30, encoding='utf-8', errors='replace')
                              
        if proc.stdout:
            # 记录完整 Python 脚本输出到终端
            server.logger.info(f"[Command Runner - Python Out] {code[:30]}...\n{proc.stdout.strip()}")
            out = proc.stdout.strip()
            if len(out) > 1500:
                out = out[:1500] + '\n... (截断)'
            src.reply(f'§a[输出]\n{out}')
            
        if proc.stderr:
            # 记录完整 Python 脚本错误到终端
            server.logger.error(f"[Command Runner - Python Err] {code[:30]}...\n{proc.stderr.strip()}")
            err = proc.stderr.strip()
            if len(err) > 1500:
                err = err[:1500] + '\n... (截断)'
            src.reply(f'§c[错误]\n{err}')
            
        if not proc.stdout and not proc.stderr:
            src.reply('§7(执行成功，无输出)')
            
    except subprocess.TimeoutExpired:
        src.reply('§c代码超时 (30秒)')
    except Exception as e:
        src.reply(f'§c错误: {e}')
    finally:
        if tf and os.path.exists(tf):
            os.unlink(tf)

def show_info(src, server):
    src.reply('§6=== Command Runner 信息 ===')
    src.reply(f'§a操作系统: §f{platform.system()} {platform.release()}')
    src.reply(f'§aPython 版本: §f{sys.version.split()[0]}')
    src.reply(f'§aPython 路径: §f{server.venv_python}')
    src.reply(f'§a虚拟环境: §f{"是" if server.venv_python != sys.executable else "否"}')
    src.reply(f'§aShell: §f{"cmd.exe" if IS_WINDOWS else "/bin/bash"}')
    src.reply(f'§a所需权限: §f{REQUIRED_PERMISSION} (服主)')