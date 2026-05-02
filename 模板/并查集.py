class DSU:
    def __init__(self, n):
        self.pa = list(range(n + 1))
        self.sz = [1] * (n + 1)
        self.mp = list(range(n + 1))
        self.cur = n + 1

    def _gid(self, i):
        if i >= len(self.mp):
            old = len(self.mp)
            self.mp.extend(range(old, i + 1))
            self.pa.extend(range(len(self.pa), i + 1))
            self.sz.extend([1] * (i + 1 - old))
            if i >= self.cur:
                self.cur = i + 1
        return self.mp[i]

    def _find(self, x):
        while self.pa[x] != x:
            self.pa[x] = self.pa[self.pa[x]]
            x = self.pa[x]
        return x

    def union(self, i, j):
        ri, rj = self._find(self._gid(i)), self._find(self._gid(j))
        if ri != rj:
            if self.sz[ri] < self.sz[rj]:
                ri, rj = rj, ri
            self.pa[rj] = ri
            self.sz[ri] += self.sz[rj]

    def is_same(self, i, j):
        return self._find(self._gid(i)) == self._find(self._gid(j))

    def get_sz(self, i):
        return self.sz[self._find(self._gid(i))]

    def delete(self, i):
        idx = self._gid(i)
        ri = self._find(idx)
        self.sz[ri] -= 1
        nid = self.cur
        self.pa.append(nid)
        self.sz.append(1)
        self.mp[i] = nid
        self.cur += 1

    def move(self, i, j):
        ri, rj = self._find(self._gid(i)), self._find(self._gid(j))
        if ri != rj:
            self.sz[ri] -= 1
            nid = self.cur
            self.pa.append(nid)
            self.sz.append(1)
            self.mp[i] = nid
            self.pa[nid] = rj
            self.sz[rj] += 1
            self.cur += 1


dsu = DSU(pow(10, 5))
dsu.union(19, 99)
dsu.union(28, 99)
dsu.union(28, 99)
print(dsu.is_same(19, 28))
print(dsu.is_same(29, 28))
print(dsu.get_sz(99))
dsu.delete(19)
dsu.move(28, 999)
dsu.union(99, 283848)
print(dsu.is_same(283848, 999))
