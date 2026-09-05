import os
import subprocess
import sys

# 支持中文路径输出
sys.stdout.reconfigure(encoding='utf-8')

# 定义我们要保留的“纯代码/文本”后缀
# 如果有其他比赛常用的后缀（如 .pas 等），可以自己往里加
CODE_EXTENSIONS = {
    '.py', '.cpp', '.c', '.h', '.hpp', '.java', '.go', '.rs', '.js', '.ts', 
    '.md', '.txt', '.json', '.gitignore'
}

def create_strict_gitignore():
    """生成极其严格的 .gitignore：默认黑掉所有，只放行代码"""
    print("📝 正在配置严苛的 .gitignore 放行名单...")
    gitignore_content = (
        "# 默认拒绝所有文件和文件夹\n"
        "/*\n\n"
        "# 允许文件夹结构通过（否则无法扫描子目录）\n"
        "!/*/\n\n"
        "# 仅放行以下合法的纯代码和文本文件\n"
    )
    for ext in sorted(CODE_EXTENSIONS):
        gitignore_content += f"!**/*{ext}\n"
        
    gitignore_content += (
        "\n# 额外强制锁死常见的比赛垃圾文件（双重保险）\n"
        "*.exe\n*.o\n*.obj\n*.zip\n*.tar.gz\n*.rar\n*.out\n*.in\n"
    )
    
    with open('.gitignore', 'w', encoding='utf-8') as f:
        f.write(gitignore_content)
    print("   └── ✅ .gitignore 创建成功！未来任何非代码文件都无法被 git add。")

def purge_non_code_history():
    """利用现代工具，将历史记录中所有的非代码文件斩草除根"""
    print("\n🔍 正在分析并清洗 Git 历史记录中的非代码文件...")
    
    # 1. 扫描当前目录下所有文件的后缀，找出哪些是不符合代码定义的
    exclude_args = []
    
    # 遍历当前目录（这里作为代表，filter-repo会作用于整个历史）
    for root, dirs, files in os.walk('.', topdown=True):
        # 别去扫描 .git 内部
        if '.git' in dirs:
            dirs.remove('.git')
            
        for file in files:
            _, ext = os.path.splitext(file)
            ext = ext.lower()
            # 如果后缀不在代码白名单里，且还没加入排除列表
            if ext and ext not in CODE_EXTENSIONS:
                match_pattern = f"*{ext}"
                if match_pattern not in exclude_args:
                    exclude_args.append(match_pattern)

    if not exclude_args:
        print("ℹ️ 未在当前工作区发现明显的非代码大文件类型。")
        return

    print(f"🚫 历史记录中即将被彻底抹除的文件类型: {', '.join(exclude_args)}")
    print("⏳ 正在调用 git-filter-repo 重写历史 (这需要一点时间)...")
    
    # 2. 构建 git-filter-repo 命令
    # --invert-paths 代表删除匹配到的路径
    # --force 代表允许在当前工作区直接动手术
    cmd = ["git", "filter-repo", "--force"]
    for pattern in exclude_args:
        cmd.extend(["--path-match", pattern])
    cmd.append("--invert-paths")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='ignore')
        if result.returncode == 0:
            print("✨ 历史清洗圆满完成！所有非代码的历史幽灵已被彻底蒸发。")
        else:
            print(f"❌ 清洗失败: {result.stderr}")
    except Exception as e:
        print(f"❌ 执行异常: {e}")

if __name__ == "__main__":
    if not os.path.exists('.git'):
        print("❌ 错误：请将此脚本放置在包含 .git 的项目主目录下运行！")
        sys.exit(1)
        
    # 执行大扫除
    create_strict_gitignore()
    purge_non_code_history()
    
    print("\n🎉 恭喜！现在你的仓库无论现在还是未来，都只配留下纯粹的代码了。")