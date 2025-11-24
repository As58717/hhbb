# OmniCompose 视频聊天 App 骨架

本目录提供一个从零起步的 Android Jetpack Compose 示例项目，覆盖“附近同城”“随机视频匹配”“一对一聊天入口”“个人资料”四个核心板块，并预留了 WebRTC 视频通话的引导代码，便于在现有仓库中快速落地移动端原型。

## 目录结构
- `build.gradle` / `settings.gradle`：顶层构建脚本。
- `app/build.gradle`：应用模块依赖（Compose、Navigation、WebRTC、CameraX）。
- `app/src/main/AndroidManifest.xml`：权限与入口 Activity 声明。
- `app/src/main/java/com/example/omnicompose/`
  - `ui/`：Jetpack Compose 界面、导航、ViewModel。
  - `data/`：示例数据模型与假数据仓库。
  - `call/VideoCallManager.kt`：WebRTC 初始化与本地音视频轨道示例。

## 快速开始（在 Android Studio）
1. 使用 Android Studio Giraffe+ 打开 `android/VideoChatApp` 目录。
2. 确保本地安装 JDK 17；Gradle 插件 8.1.0、Kotlin 1.9.10 已在脚本中指定。
3. 连接一台开启摄像头和麦克风的设备/模拟器，授予相机、录音、网络和定位权限。
4. 运行应用，底部导航包含：
   - 附近同城：展示示例用户列表并支持刷新城市数据。
   - 随机视频：点击“开始速配”即可获取一位随机匹配的示例用户。
   - 聊天：展示示例会话列表（可扩展为 IM + 视频拨号）。
   - 我的：显示示例个人资料，包含“开启视频验证”按钮。

## 对接真实后端与信令
- 将 `UserRepository` 替换为实际的 REST/gRPC/GraphQL 数据源，填充附近和推荐列表。
- 在 `MainViewModel.openChat/openProfile` 中对接聊天路由与用户详情页。
- `VideoCallManager` 已封装 WebRTC 工厂、摄像头采集与本地轨道创建：
  - 替换 `createPeerConnection` 调用处的 ICE 服务器列表（STUN/TURN）。
  - 在 `SimplePeerObserver` 中将 SDP/ICE 事件通过 Socket.IO/MQTT 等推送到信令服务器。
  - 在 UI 中使用 `org.webrtc.SurfaceViewRenderer` 或 Compose + `AndroidView` 嵌入远端/本地图像。

## 注意事项
- 示例使用 `https://placekitten.com` 作为头像占位图，请根据实际业务替换。
- 生产环境需处理权限请求、网络异常、前后台生命周期、账号登录与风控、实时消息存储等。
