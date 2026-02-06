# 贡献指南

感谢您对ESP_HUD项目的关注！我们欢迎各种形式的贡献。

## 如何贡献

### 报告Bug

- 使用GitHub Issues搜索是否已有相关问题
- 提供详细的复现步骤
- 包含环境信息（硬件型号、软件版本等）
- 如果可能，提供最小复现代码

### 提交功能请求

- 清晰描述需求和使用场景
- 说明解决的问题
- 如果有技术方案想法，欢迎一并提出

### 代码贡献

1. **Fork仓库**
2. **创建特性分支**
   ```bash
   git checkout -b feature/your-feature-name
   ```
3. **编写代码**
   - 遵循现有代码风格
   - 添加必要注释
   - 确保编译通过
4. **提交更改**
   ```bash
   git commit -m "Add feature: brief description"
   ```
5. **推送分支**
   ```bash
   git push origin feature/your-feature-name
   ```
6. **创建Pull Request**

## 代码规范

### C/C++代码风格
- 使用4空格缩进（不要使用Tab）
- 变量和函数名使用snake_case
- 常量使用UPPER_CASE
- 类名使用PascalCase
- 文件名使用snake_case

### 注释规范
```cpp
/**
 * @brief 简要描述函数功能
 * @param param1 参数1说明
 * @param param2 参数2说明
 * @return 返回值说明
 */
```

### 提交信息格式
```
type(scope): description

详细说明（可选）

[type列表]
feat: 新功能
fix: bug修复
docs: 文档更新
style: 代码格式调整
refactor: 代码重构
test: 测试相关
chore: 构建过程或辅助工具变动
```

## 开发环境设置

请参考README中的[开发环境搭建](README.md#开发环境搭建)部分。

## 测试要求

- 所有新功能都需要相应的测试
- 确保现有测试仍然通过
- 对于硬件相关的改动，请在实际设备上验证

## 问题讨论

- 使用GitHub Discussions进行技术讨论
- 在Issues中报告具体问题
- 欢迎参与代码审查

## 行为准则

请保持友善、专业和建设性的交流态度。