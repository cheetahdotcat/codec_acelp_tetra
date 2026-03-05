# `codec_acelp_tetra` (v2, in-process)

This module adds `ACELP/8000` audio support to Asterisk as an out-of-tree codec.

- RTP codec subtype: `ACELP/8000`
- Fixed frame model: 30 ms / 240 samples / 137 bits packed in 18 bytes

## Requirements

1. Asterisk headers, or full Asterisk source tree with generated headers
2. Patched ETSI TETRA C reference source prepared with `codec.diff`

## Prepare patched ETSI source

```bash
git clone https://github.com/sq5bpf/install-tetra-codec /tmp/install-tetra-codec
cd /tmp
wget -O tetra.zip 'http://www.etsi.org/deliver/etsi_en/300300_300399/30039502/01.03.01_60/en_30039502v010301p0.zip'
mkdir -p tetra-acelp-src
cd tetra-acelp-src
unzip -L ../tetra.zip
patch -p1 -N -E < /tmp/install-tetra-codec/codec.diff
```

Use `/tmp/tetra-acelp-src/c-code` as `TETRA_CODEC_SRC`.

## Build and install

From this directory:

```bash
make ASTERISK_SRC=/path/to/asterisk-source TETRA_CODEC_SRC=/tmp/tetra-acelp-src/c-code
sudo make ASTERISK_SRC=/path/to/asterisk-source TETRA_CODEC_SRC=/tmp/tetra-acelp-src/c-code install
```

If you only have packaged Asterisk headers:

```bash
make TETRA_CODEC_SRC=/tmp/tetra-acelp-src/c-code
sudo make TETRA_CODEC_SRC=/tmp/tetra-acelp-src/c-code install
```

## Verify

```bash
asterisk -rx "module show like codec_acelp_tetra"
asterisk -rx "core show translation"
```

