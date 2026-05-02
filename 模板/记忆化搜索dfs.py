from functools import lru_cache
import sys

sys.setrecursionlimit(20000)


@lru_cache(None)  # 关键：记忆化搜索，否则 O(2^n) 会超时
def paths(n, m):
    # 边界条件：到达边界只有 1 条路
    if n == 0 or m == 0:
        return 1
    # 二维递归逻辑
    return paths(n - 1, m) + paths(n, m - 1)


# 使用方式
print(paths(50, 1000))  # 输出 20
