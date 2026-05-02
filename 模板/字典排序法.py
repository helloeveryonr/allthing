di = {1: [3, 9], 4: [7, 4], 2: [6, 2], 3: [7, 5]}
for i, j in di.items():
    print(i, j, end="    ")

# 按键排序
print(sorted(di))
l = sorted(di)
print({i: di[i] for i in l})

# 按val排序(基础)
print(sorted(di.items(), key=lambda x: (x[1])))  # x[1] 就是那个列表 [3, 9], [3, 2]
print()
# 按长度排序
print(sorted(di.items(), key=lambda x: len(x[1])))
print()
# 按val排序(进阶)
print(
    sorted(di.items(), key=lambda x: (-x[1][0], x[1][1]))
)  # x[1][1] 就是那个列表 [3, 9], [3, 2]的第2个数,9,2
