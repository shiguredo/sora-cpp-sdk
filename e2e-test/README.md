# sumomo-e2e-test

`e2e-test/` 以下で実行してください。

```bash
uv run pytest -v -s
```

## ハードウェアアクセラレーター

- Intel VPL の検証をする場合
  - `INTEL_VPL=1`
- NVIDIA Video Codec の検証をする場合
  - `NVIDIA_VIDEO_CODEC=1`
- AMD AMF の検証をする場合
  - `AMD_AMF=1`
- Raspberry Pi の V4L2 M2M の検証をする場合
  - `RASPBERRY_PI=1`
- Apple Video Toolbox の検証をする場合
  - `APPLE_VIDEO_TOOLBOX=1`
