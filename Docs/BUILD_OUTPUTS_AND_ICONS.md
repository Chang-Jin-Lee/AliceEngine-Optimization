# Build Outputs & Icons (DLL Layout)

This note captures the **current build output layout**, **DLL packaging rules**, and **icon embedding**.
It is the reference for both Editor (Launch) and Game (AlicePlayer) builds.

---

## 1) Build Output Layout

### Editor build (Launch)
```
build/bin/<Debug|Release>/
  Launch.exe
  dll/                # all runtime DLLs live here
    AliceScripts.dll  # in dll/ (hot reload uses dll first)
  ...
```

### Game build (Build Game → AlicePlayer)
```
<ExportRoot>/Bin/
  AlicePlayer.exe
  dll/                # all runtime DLLs (including AliceScripts.dll)
  Cooked/
  Metas/
  BuildSettings.json
  EngineSettings.json # Lighting/Skybox 등 런타임 설정
```

**Rule:** Do not place DLLs next to the exe. All non‑system DLLs go into `dll/`.

---

## 2) DLL Packaging & Loading Rules

### Packaging (CMake / Build Game)
- CMake **copies all runtime DLLs into `dll/`** for Launch/AlicePlayer.
- Build Game export **copies the entire `dll/` folder** (no per‑DLL copy).
- `AliceScripts.dll` (ScriptsBuild result) is **copied into `dll/`** as well.
- `EngineSettings.json`은 **Build Game 시 최신 Lighting/Skybox 설정을 저장**하고 `Bin/`에 포함된다.

### Loading (Runtime)
- Process sets **DLL search to `exe/dll`** (`SetDllDirectoryW`).
- Delay‑load failure hook also **tries `exe/dll/<missing.dll>`**.
- Script loader **tries `dll/` first**, then `exe/` fallback.

**If a new DLL is added:**
1) Add it to the CMake copy list (into `dll/`)
2) Add it to delay‑load list if it is an import dependency

---

## 3) Icons (Editor vs Game)

Icons are embedded at build time via CMake (.rc).

**Fixed paths:**
```
Resource/Icon/EditorIcon.ico   -> Launch.exe
Resource/Icon/GameIcon.ico     -> AlicePlayer.exe
```

**To change icons:** replace the .ico files above (no code change required).

---

## 4) Safe Loading Optimization (Planned)

**Keep only as a note; no runtime changes yet.**

**Chunk index cache + scene list index preloading** is the safest perf win:
- Load **only index/metadata** for scenes listed in `BuildSettings.json`.
- Avoids Update‑time asset mutation issues.
- Real data loading remains synchronous/controlled at scene switch.
