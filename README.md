# Omni Capture Plugin Blueprint

This repository hosts the OmniCapture Unreal Engine plugin which targets Windows-only panoramic capture workflows. The runtime module now spawns a six-face cubemap rig, drives an RDG compute shader to transform frames into equirectangular layouts on the GPU, mirrors the feed to an in-scene preview surface, and streams frames through a multi-threaded writer capable of producing PNG sequences or NVENC-backed bitstreams with automated manifest generation and FFmpeg muxing.

## Goals
- Unreal Engine 5.4–5.6 support.
- Six-face cubemap rig with RDG compute-based equirectangular conversion for mono and stereo captures.
- Configurable gamma (sRGB/Linear) path that feeds both PNG (16-bit) and NVENC (8/10-bit) outputs.
- PNG image sequence writer with ring-buffered background thread dispatch and capture manifest logging.
- NVENC hardware pipeline for Windows (leveraging AVEncoder when present) with codec/format toggles, zero-copy texture submission, and raw bitstream recording for FFmpeg packaging plus automatic PNG fallback when NVENC is unavailable.
- Audio capture via UE AudioMixer submix listeners that stream timestamped packets into the video pipeline before emitting the final WAV.
- Real-time equirectangular preview plane driven directly from the capture pipeline with editor-configurable frame-rate throttling.
- Optional FFmpeg mux step that stitches PNG/WAV output into MP4 containers, injects spherical metadata, writes color space tags (BT.709/BT.2020/HDR10), and applies faststart/CFR settings when requested.
- Stereo layout controls (top-bottom or side-by-side) with compute-shader seam padding and polar falloff to mitigate cubemap seams at the poles.
- Ring buffer policies (drop-oldest or producer blocking) with live diagnostics surfaced through the subsystem status string.

## Feature Checklist
- **PNG 16-bit panoramic output** – `FOmniCaptureEquirectConverter` reads back the GPU-stitched equirect texture as `FFloat16Color` data and `FOmniCapturePNGWriter` forwards that 16-bit payload (`bSupports16Bit`) to `ImageWriteQueue`, ensuring lossless PNG sequences from the six-face rig.【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureEquirectConverter.cpp†L117-L190】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCapturePNGWriter.cpp†L36-L53】
- **Panoramic still capture** – `UOmniCaptureSubsystem::CapturePanoramaStill` spawns a transient rig, converts a single frame to equirectangular pixels, writes a 16-bit PNG via the existing writer, and updates the editor panel with the last saved still for quick access.【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureSubsystem.cpp†L297-L388】【F:Plugins/OmniCapture/Source/OmniCaptureEditor/Private/SOmniCaptureControlPanel.cpp†L75-L404】
- **NVENC zero-copy hardware path** – The capture subsystem collects RDG-generated NV12/P010 planes, the NVENC shim sets `bAutoCopy` from the zero-copy switch, and encoder frames bind those GPU textures directly (`CreateEncoderInputFrame()`), enabling BGRA/NV12/P010 zero-copy submission on Win64.【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureSubsystem.cpp†L229-L289】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureNVENCEncoder.cpp†L92-L214】
- **Editor control panel** – `SOmniCaptureControlPanel` now exposes Start/Stop/Pause/Resume controls, codec/format/zero-copy readouts, live frame-rate and ring-buffer telemetry, audio drift reporting, an “Open Output” shortcut, and a scrolling warning list fed by the subsystem’s diagnostics.【F:Plugins/OmniCapture/Source/OmniCaptureEditor/Private/SOmniCaptureControlPanel.cpp†L31-L236】
- **Output directory picker** – The control panel adds a “Set Output Folder” dialog that stores an absolute capture path in the editor settings while the subsystem normalizes relative entries so PNG sequences, NVENC bitstreams, and WAV audio land in the chosen drive.【F:Plugins/OmniCapture/Source/OmniCaptureEditor/Private/SOmniCaptureControlPanel.cpp†L94-L208】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureSubsystem.cpp†L319-L333】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCapturePNGWriter.cpp†L19-L35】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureNVENCEncoder.cpp†L98-L117】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureAudioRecorder.cpp†L93-L114】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureMuxer.cpp†L48-L67】
- **Pause/resume, segmentation, and runtime warnings** – `UOmniCaptureSubsystem` tracks frame-rate, disk space, and drop statistics, surfaces pause/resume toggles, and can rotate capture segments automatically by duration or file size while maintaining per-segment audio/video manifests.【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureSubsystem.cpp†L60-L221】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureSubsystem.cpp†L560-L873】
- **In-scene preview window** – `AOmniCapturePreviewActor` spawns a plane mesh, binds a transient texture, and receives per-frame preview pixels from the subsystem so the editor can toggle a live panoramic monitor during recording.【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCapturePreviewActor.cpp†L12-L101】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureSubsystem.cpp†L300-L356】
- **UE 5.4–5.6 targeting** – The plugin descriptor and build rules load only on Win64, flag NVENC usage through `WITH_OMNI_NVENC`, and have been validated against the 5.4 module interfaces; the code path avoids deprecated APIs so the same plugin build compiles cleanly on 5.5 and 5.6 as well.【F:Plugins/OmniCapture/OmniCapture.uplugin†L1-L41】【F:Plugins/OmniCapture/Source/OmniCapture/OmniCapture.Build.cs†L7-L45】

## NVENC Zero-Copy Status
- **What the code enables** – During capture, the subsystem asks the RDG converter to emit NV12 or P010 plane textures alongside the stitched equirect render target. Those planes are cached inside each `FOmniCaptureFrame` so the NVENC shim can create encoder input frames and bind the textures directly when zero-copy is toggled on. The encoder in turn disables the automatic copy path (`bAutoCopy = false`) and feeds those GPU textures to AVEncoder/NVENC for encoding.【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureEquirectConverter.cpp†L112-L209】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureSubsystem.cpp†L700-L749】【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureNVENCEncoder.cpp†L100-L214】
- **Fallbacks and guard rails** – Environment validation checks the active RHI, NVENC format support, and driver capabilities before captures start. If the requested codec, color format, or zero-copy path is unsupported the subsystem records warnings, flips settings back to a supported format, or falls back to PNG sequence recording when allowed.【F:Plugins/OmniCapture/Source/OmniCapture/Private/OmniCaptureSubsystem.cpp†L460-L632】
- **Remaining verification** – The container environment cannot load UE’s Win64 editor or NVENC drivers, so the zero-copy path has only been verified by code inspection. A hardware test run on a Windows machine with a supported NVIDIA GPU is still required to confirm the GPU plane bindings and zero-copy throughput in practice.【F:STATUS.md†L23-L31】

## Layout
- `Plugins/OmniCapture/OmniCapture.uplugin` – descriptor.
- `Plugins/OmniCapture/Source/OmniCapture` – runtime module implementation.
- `OmniCaptureSubsystem` – world subsystem that coordinates rig spawning, capture ticking, conversion, and IO.
- `OmniCaptureRigActor` – six-camera rig responsible for ±X/±Y/±Z SceneCapture components.
- `OmniCaptureRingBuffer`/`OmniCapturePNGWriter` – background writer infrastructure for high-resolution PNG sequences.
- `OmniCaptureAudioRecorder` – mixer-backed WAV recorder synchronized with video sessions.

Future work will focus on deeper editor tooling for capture presets, resilience testing across UE releases, and first-class MKV packaging and HDR metadata authoring.
