# OopsOS 测试与合并指南

本文档说明如何在提交 Merge Request 之前进行充分的测试，确保代码质量。

---

## 📋 提交前的检查清单

### 第一步：本地测试（必须）

在 WSL Ubuntu 环境中执行：

```bash
# 1. 进入项目目录
cd /path/to/project3035746-353055

# 2. 清理之前的编译物
make clean

# 3. 重新编译
make

# 4. 运行 QEMU
make qemu
```

### 第二步：根据改动范围选择测试

#### 📌 改动进程相关代码？
```bash
# 必须跑这些
$ forktest        # 进程创建测试
$ currentproc     # 查看进程信息
$ alarmtest       # 定时提醒测试（如有改动）
$ msgtest         # 消息队列测试（如涉及IPC）
$ sharemm         # 共享内存测试（如涉及IPC）
```

#### 📌 改动内存管理代码？
```bash
# 必须跑这些
$ cowtest         # 写时复制测试
$ lazytest        # 懒分配测试
$ mmaptest        # 内存映射文件测试
$ kalloctest      # 内存分配测试
```

#### 📌 改动文件系统代码？
```bash
# 必须跑这些
$ symlinktest     # 符号链接测试
$ chmodtest       # 文件权限测试
$ recoveritest    # 文件恢复测试
$ bcachetest      # Buffer缓存测试
$ bigfiletest     # 大文件测试
```

#### 📌 改动网络代码？
```bash
# 必须跑这些
$ nettest         # 网络功能测试
```

#### 📌 不确定改动范围？
```bash
# 跑综合测试（最保险）
$ usertests       # 包含所有系统调用的综合测试
```

---

## 🧪 编写新功能测试

如果你新增了功能，**必须写对应的测试**！

### 步骤 1：创建测试文件

```bash
# 在 /user/test/ 目录下创建测试
vim user/test/myfeaturetest.c
```

### 步骤 2：编写测试代码模板

```c
// user/test/myfeaturetest.c
#include "types.h"
#include "stat.h"
#include "user/user.h"

void test_basic_case() {
    printf("testing basic case...\n");
    // 你的测试代码
    // assert 或其他验证逻辑
    printf("basic case passed\n");
}

void test_edge_case() {
    printf("testing edge cases...\n");
    // 边界情况测试
    printf("edge cases passed\n");
}

int main() {
    printf("===== MyFeature Test =====\n");
    
    test_basic_case();
    test_edge_case();
    
    printf("===== All tests passed! =====\n");
    exit(0);
}
```

### 步骤 3：添加到 Makefile

找到 Makefile 中的 `UPROGS` 部分：

```makefile
UPROGS = \
    $U/program/cat\
    $U/program/echo\
    ...
    $U/test/_myfeaturetest\     # 加上你的测试
    ...
```

### 步骤 4：重新编译并测试

```bash
make clean
make qemu

# 进入OS后运行你的测试
$ myfeaturetest
```

---

## ✅ 提交前的完整检查

### 检查清单

- [ ] 代码已本地测试通过
- [ ] 所有相关测试都执行过并通过
- [ ] 没有编译警告
- [ ] 提交信息清晰明确
- [ ] 改动范围有限，逻辑清晰

### 编译无警告检查

```bash
# 检查编译输出中是否有 warning
make clean
make 2>&1 | grep -i warning

# 如果没有输出 warning，说明编译干净
```

---

## 🚀 提交 Merge Request 的模板

### 标题格式

```
feat: 实现XXX功能
fix: 修复XXX bug
optimize: 优化XXX性能
refactor: 重构XXX代码
docs: 更新XXX文档
```

### MR 描述模板

```markdown
## 功能说明
简要描述你的改动是什么

例如：
实现了动态优先级调度算法，支持进程在运行时动态调整优先级

## 改动文件
列出主要改动的文件

例如：
- kernel/proc/proc.c     (添加优先级调整逻辑)
- kernel/include/proc.h  (新增数据结构)
- user/test/schedtest.c  (添加测试用例)

## 核心改动说明
简述核心的实现细节

例如：
1. 在进程结构体中新增 `dynamic_priority` 字段
2. 修改 `scheduler()` 函数的调度逻辑
3. 添加 `SYS_setPriority` 系统调用

## 测试情况
说明你进行了哪些测试

例如：
✅ usertests - 通过
✅ forktest - 通过
✅ mytest (新增测试) - 通过

所有相关测试都通过，没有发现回归。

## 性能影响
如有性能相关改动，说明影响

例如：
- 调度性能提升约 10%
- 内存占用无增加
```

---

## 📝 提交流程回顾

```bash
# 1. 更新主分支
git checkout main
git pull origin main

# 2. 创建你的工作分支
git checkout -b feature-name

# 3. 修改代码并测试（重复此步骤）
make clean
make qemu
# 在QEMU中运行测试...

# 4. 提交代码
git add .
git commit -m "feat: 实现新功能"

# 5. 推送到远程
git push -u origin feature-name

# 6. 在GitLab网页上创建 MR
# 填写MR描述（使用上面的模板）

# 7. 等待审查和合并
```

---

## 🔍 代码审查时常见问题

### 编译失败？
```bash
# 检查是否有编译错误
make clean
make 2>&1 | head -20

# 常见原因：
# - 缺少头文件 include
# - 函数未声明
# - 类型不匹配
```

### 测试失败？
```bash
# 重新运行相关测试
make clean
make qemu

# 进入QEMU后：
$ usertests
$ mytest

# 检查输出中的错误信息
```

### 代码有警告？
```bash
# 清理所有警告
make clean
make 2>&1 | grep warning

# 常见警告：
# - 未使用的变量
# - 隐式类型转换
# - 未初始化的变量
```

---

## 📚 最佳实践

### DO ✅

- ✅ 在本地充分测试再提交
- ✅ 编写清晰的提交信息
- ✅ 一个 MR 只做一个功能
- ✅ 为新功能编写测试代码
- ✅ 在 MR 中详细说明改动理由

### DON'T ❌

- ❌ 不测试就直接提交
- ❌ MR 中包含大量不相关改动
- ❌ 提交未编译的代码
- ❌ 忽略编译警告
- ❌ 修改他人负责的目录而不沟通

---

## 快速参考

```bash
# 一键测试流程（推荐）
make clean && make qemu
# 进入QEMU后
$ usertests
# 按 Ctrl+A 然后 X 退出

# 如果通过，就可以提交了！
git push
```

---

## 遇到问题？

1. **编译失败** → 检查 `make clean` 后是否重新编译
2. **测试失败** → 查看错误信息，调试代码
3. **合并冲突** → `git pull origin main` 后重新解决
4. **不确定** → 在 GitLab MR 中提问，或联系团队成员

---

**记住：充分的测试是高质量代码的保证！** 🎯
