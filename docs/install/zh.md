# 安装 Tremu

[English](en.md) · [Deutsch](de.md) · [Español](es.md) · [Français](fr.md) · [日本語](ja.md) · [Русский](ru.md) · **中文**

Tremu 把 Meta Quest 3 或 3S 变成一台光枪街机：游戏显示在一块巨大的虚拟屏幕上，用 WonderFitter
光枪或 Touch 手柄瞄准。Tremu **不提供任何游戏、BIOS、固件或密钥**，请使用你自己的副本。

## 1. 准备头显

1. 开启**开发者模式**：在 Meta Horizon 手机应用中打开你的头显，进入「头显设置」›「开发者模式」
   （需要一个免费的 Meta 开发者组织）。
2. 在电脑上安装侧载工具：[Meta Quest Developer Hub](https://developers.meta.com/horizon/documentation/android-apps/meta-quest-developer-hub/)
   或 [SideQuest](https://sidequestvr.com/)。用 USB 连接头显，并在头显中允许 USB 调试。

## 2. 安装 Tremu

用该工具安装 `tremu.apk`（把 APK 拖到头显上）。首次启动时 Tremu 会请求：

- **所有文件访问权限**：请允许。Tremu 从 `Download/Roms` 读取游戏，并把自己的文件放在 `Tremu` 文件夹。
- **附近的设备（蓝牙）**：请允许，用于 WonderFitter 光枪。

Tremu 会出现在「应用库」›「未知来源」中。

## 3. 添加游戏

| 内容 | 头显上的位置 |
|---|---|
| 游戏 | `Download/Roms/<主机>`，每台主机一个文件夹（例如 `Download/Roms/ps1`） |
| MAME 街机游戏（Area 51、CarnEvil 等） | `Download/Roms/mame/<游戏>`（解压后，连同其 `.chd`） |
| Wii U、PS3、PS Vita 游戏 | `Download/Roms/wiiu`、`Download/Roms/ps3`、`Download/Roms/vita` |
| BIOS（PS2、土星、Mega-CD 等） | `Tremu/bios`（PS2：`Tremu/bios/pcsx2/bios`） |
| Xbox 系统文件 | `Tremu/Xbox/system` |
| 封面 | `Tremu/Banner`（缺少的封面会自动下载） |

不用数据线：轮播界面底部会显示「添加游戏：http://…」和一个代码。在同一 Wi-Fi 下的电脑或手机上
打开这个地址并放入文件，Tremu 会自动整理。
如果 Tremu 提示缺少文件（BIOS、硬盘），提示会说明缺哪个、放在哪里。

## 4. 配套应用

Tremu 本身可模拟街机、FC、SFC、N64、MD、土星、PS1、PS2、PSP、Dreamcast、GameCube、Wii、Xbox 和
Xbox 360。另外四种需要单独的应用：Tremu 会打开它，游戏结束后回到轮播界面。

| 用于 | 应用 | 源码 |
|---|---|---|
| PS3 | ARMSX3（`com.armsx3`） | [proelitem/ARMSX3](https://github.com/proelitem/ARMSX3) |
| Wii U | Cemu（`info.cemu.cemu`） | [proelitem/Cemu](https://github.com/proelitem/Cemu)，分支 `tremu` |
| PS Vita | Vita3K（`org.vita3k.emulator`） | [proelitem/Vita3K](https://github.com/proelitem/Vita3K)，分支 `tremu` |
| Time Crisis VR | `org.timecrisis.quest` | [proelitem/time-crisis-vr](https://github.com/proelitem/time-crisis-vr) |

必须使用**这些版本**：官方版本收不到在 Tremu 中选择的游戏，也不会回到轮播界面。如果已经装了官方
版本（尤其是 ARMSX3），请**先卸载**：签名不同，Android 不允许覆盖安装。
没有该应用时，游戏卡片仍会显示，Tremu 会告诉你需要安装哪个应用。
PS3 固件、PS Vita 固件（`.PUP`，放在 `Download/Roms/vita`）和 Wii U 密钥需要你自己提供。

## 5. WonderFitter 光枪

打开光枪电源即可：Tremu 会自己通过蓝牙找到它，不需要官方 WonderFitter 应用。
光枪**同一时间只接受一个应用**：如果官方应用开着，请先关闭。安装角度、偏移等设置在 Tremu 菜单中，
并按每把枪分别保存。没有光枪时，用 Touch 手柄瞄准。

Wii 遥控器（Wii 游戏）：只能通过用 USB 连接到头显的 Mayflash **DolphinBar** 使用。

## 6. 一起玩

同一 Wi-Fi：一台头显启动游戏，选择菜单 ›「多人游戏」›「创建主机（Wi-Fi）」；另一台会在轮播界面看到
「加入：…」卡片。两台头显需要**相同版本的 Tremu 和相同的游戏文件**。MAME 游戏中，主机会从头重新
开始游戏并等待玩家 2，双方一起开局。

通过互联网：「在互联网上创建主机」会显示一个代码，交给对方（菜单 › 通过代码加入）。不使用任何付费服务器：
如果没有出现代码，请在路由器上开放 UDP 端口 47617（或开启 UPnP）。

## 退出与菜单

左手柄的菜单键：游戏选项。在从 Tremu 启动的游戏（PS3、Wii U、Vita、Time Crisis VR）中按住 1.5 秒：
回到轮播界面。
