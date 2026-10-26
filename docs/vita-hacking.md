# PS Vita 破解指南（为 PSV Agent Remote 准备）

> 本文整理自社区权威指南 **https://vita.hacks.guide**（2026-10 查阅）。每一步开始前请先打开原文的对应页面核对，网址和步骤可能会变。
> 任何修改系统的操作都有**变砖**的风险，后果自负。整个过程**插着充电器**做，不要让机器中途休眠。

**你的路线（系统 3.73）**：升级到 3.74 → 用 HENlo 临时破解 → 用 VitaDeploy 装 3.65 + Ensō，实现永久破解 → 收尾设置 → 装 VitaShell。

## 0. 准备

- **看系统版本**：设置 → 系统 → 系统信息。
- **二手机器**，或者你不确定以前有没有破解过：先按 [Restoring System](https://vita.hacks.guide/restoring-system) 把系统恢复，再继续。
- **一代机（PCH-1000，OLED 屏）没有官方记忆卡的**：要先按 [Creating an internal memory card](https://vita.hacks.guide/) 创建内部存储分区（这个页面在指南侧栏里），**否则不要做第 2 步**。二代机（PCH-2000）自带 1GB 内存，可以跳过。
- Wi‑Fi 能连外网（下载组件要用）。
- 确认键：本项目按 **✕ = 确认**（国际布局）设计。

## 1. 从 3.73 升级到 3.74（你的机器是 3.73，这一步必须做）

HENlo 只支持 3.65、3.68 和 3.74，**3.73 不能直接破解**，要先升到 3.74。

1. 设置 → **系统更新** → **使用 Wi‑Fi 更新**。
2. **确认提示里写的是 "Version 3.74"**，不是 3.74 就不要继续。
3. 如果提示的版本不对，把 Wi‑Fi 设置里的 DNS 改回自动获取，然后重试。
4. 按提示更新，完成后机器会自动重启。重启后到 设置 → 系统 → 系统信息 确认是 3.74。

原文：https://vita.hacks.guide/updating-firmware-(3.74)

## 2. HENlo：临时破解（浏览器漏洞）

1. 打开 PSV 自带的浏览器，访问 `http://jailbreak.psp2.dev`
2. 选 **Unlock my Vita** → **Unlock**，按 ✕ 确认。报错的话参考指南里的 Troubleshooting → "A browser-based exploit is not working"，多试几次。
3. 出现 **henlo-bootstrap** 界面后，依次选：
   - **Install henkaku**
   - **Install VitaDeploy**
   - **Exit**
4. 打开 设置 → **HENkaku Settings** → 勾选 **Enable Unsafe Homebrew**。

注意：HENlo **重启后就失效了**。每次重启都要重新访问一次上面的网址，直到完成第 3 步。

原文：https://vita.hacks.guide/using-henlo

## 3. Ensō：永久破解（会从 3.74 降级到 3.65）

1. 打开 **VitaDeploy** → **Install a different OS** → **Quick 3.65 Install**（需要联网）。看不到这个选项，就是还没开 Unsafe Homebrew。
2. 等组件下载完 → 按 ✕ → 读完提示，等 20 秒 → 再按 ✕。
3. 机器会装好 3.65 + Ensō 并自动重启。以后每次开机都是破解状态。

警告：
- 这一步对系统做的是**永久修改**，中途出问题可能没法恢复。一定要插着电，期间不要让机器休眠。
- 如果卡在黑屏：长按电源键关机，然后重新做这一步。
- 重启时如果问要不要把内部存储的数据转移到记忆卡，**选 No**（选 Yes 会清空记忆卡）。

原文：https://vita.hacks.guide/installing-enso

## 4. 收尾设置

1. VitaDeploy → **App downloader** → 选 **iTLS Installer** → 下载 → 打开 iTLS-Enso → **Install the full iTLS package** → 等它自动重启。
2. **关掉自动更新**：设置 → 系统 → 自动启动设置 → 取消勾选"下载系统软件的更新文件"。
3. （可选，想用 PSN 的话）HENkaku Settings → 勾选 Enable PSN spoofing 和 Enable Version Spoofing，Spoofed Version 填 `3.74`。

原文：https://vita.hacks.guide/finalizing-setup

## 4.5 亚洲版：把确认键改成 ✕（可选）

设置 → **HENkaku Settings** → **○ 按钮的行为（○ Button Behavior）**，改成「返回 / 取消」，然后**重启 PSV**。

重启后整个系统界面（主界面、LiveArea、系统设置、系统应用）都会变成 ✕ 确认（已实测）。不重启的话，只有「设置」菜单会生效。

不要用 设置 → 辅助功能 → 按钮分配：它是把物理按键整体对调，所有游戏都会受影响，欧美版游戏会反过来变成 ○ 确认。

本项目的应用不受这个设置影响，**始终是 ✕ 确认**。

## 5. 装 VitaShell（用来安装本项目的 VPK）

在 VitaDeploy 的 **App downloader** 里找 **VitaShell** 下载安装。如果列表里没有，就去 https://github.com/TheOfficialFloW/VitaShell/releases 下载 VPK，参考 vita.hacks.guide 上对应的页面安装。

## 6. 安装 PSV Agent Remote

1. 电脑上编译出 `psv_agent_remote.vpk`（构建方法见 `vita/README`，等项目做好以后补上）。
2. PSV 打开 VitaShell，按 **SELECT** 开启 FTP，屏幕上会显示 `ftp://192.168.x.x:1337`。
3. 电脑上用 FTP 客户端（Finder 的"连接服务器"只能读不能写，建议用 Cyberduck 或 FileZilla，或者用命令行 `curl -T psv_agent_remote.vpk ftp://<PSV 的 IP>:1337/ux0:/`）把 VPK 传到 `ux0:/`。
4. 在 VitaShell 里找到这个 VPK，按 ✕ → 安装。
5. 主屏幕上出现图标后打开。第一次运行前，可以先编辑 `ux0:data/psv_agent_remote/config.txt`，确认 broker 地址是 `192.168.2.222:1883`。

## 6.5 破解时没插记忆卡，插卡后 VitaDeploy 等应用不见了

原因：没插卡时，`ux0:`（应用和数据所在的位置）在机器内部的存储上；插卡后，`ux0:` 变成记忆卡，系统只显示卡上的应用。内部存储里的应用**没有丢**，拔掉卡就会重新出现。破解本身（Ensō）装在系统分区，插不插卡都在。

处理方法：**插着卡**，把需要的工具重新装到卡上。
1. 插卡开机。如果问要不要把内部存储的数据转移到记忆卡，选 **No**。
2. 打开浏览器访问 `http://jailbreak.psp2.dev`，在 henlo-bootstrap 界面只选 **Install VitaDeploy**，然后 Exit。
3. 打开 VitaDeploy → App downloader → 装 **VitaShell**（还需要 iTLS 的话也一起装）。
4. 本项目的 VPK 也装到卡上（第 6 节）。

如果第 2 步的浏览器页面在已破解的机器上打不开，可以拔卡开机，用内部存储里的 VitaShell 看一下，然后在指南的 Troubleshooting 页面查找对应的问题。

## 7. 网络提示（本项目相关）

- PSV 连的 Wi‑Fi 必须和 pop-os（`192.168.2.222`）在**同一个局域网**里。
- 一代机（PCH-1000）只支持 **2.4GHz** Wi‑Fi，加密方式选 WPA2；路由器如果只开了 5GHz 或者只开了 WPA3，PSV 是连不上的。
