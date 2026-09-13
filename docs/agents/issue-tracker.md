# Issue tracker: GitHub

本项目的任务和 PRD 使用 [LiLeyi/ChargeHub](https://github.com/LiLeyi/ChargeHub) 的 GitHub Issues。

## 约定

- 使用 `gh` CLI 创建、读取、评论、标记和关闭 Issue。
- 在本仓库克隆中运行 `gh`，由 `origin` 自动确定目标仓库。
- 当技能要求“发布到任务追踪器”时，创建 GitHub Issue。
- 当技能要求“读取相关工单”时，读取对应 Issue、评论和标签。
- 当前机器尚未安装 `gh`；首次执行 Issue 写操作前需安装并完成 GitHub 登录。

常用操作：

```bash
gh issue create --title "..." --body "..."
gh issue view <number> --comments
gh issue list --state open --json number,title,body,labels,comments
gh issue comment <number> --body "..."
gh issue edit <number> --add-label "..."
gh issue edit <number> --remove-label "..."
gh issue close <number> --comment "..."
```

## Pull Requests

**外部 Pull Request 不作为需求或分诊入口。** 分诊技能只处理 GitHub Issues，不把外部 PR 加入同一队列。
