# 既知の問題

## Intel VPL で H.264 の映像をデコードすると正常に動作しない

Intel VPL で H.264 の映像をデコードすると、以下の事象が発生することを確認しています。

- 受信した映像が止まる

調査の結果、IntelVPL に問題がある可能性が高く、現在問題報告を行なって対応をしております。
https://community.intel.com/t5/Media-Intel-Video-Processing/Issue-with-Decoding-H-264-Video-Encoded-in-macOS-Safari/m-p/1587647

こちらの問題が解消すると改善する見込みです。

Intel VPL を利用して映像をデコードする場合は、 H.264 を避けて他のコーデックを利用してください。