minimp3
==========

[![Build Status](https://travis-ci.org/lieff/minimp3.svg)](https://travis-ci.org/lieff/minimp3)
<a href="https://scan.coverity.com/projects/lieff-minimp3">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/14844/badge.svg"/>
</a>
[![codecov](https://codecov.io/gh/lieff/minimp3/branch/master/graph/badge.svg)](https://codecov.io/gh/lieff/minimp3)

一个用于解码mp3的简单的，单个头文件的库。 minimp3设计为
小，快速（具有SSE和NEON支持），并且准确（兼容ISO）。你可以
在下面找到一个粗糙的基准测试，使用  ``perf``  测量i7-6700k，包括IO。没有CPU加热来解决Speedstep：

| Vector      | Hz    | Samples| Sec    | Clockticks | Clockticks per second | PSNR | Max diff |
| ----------- | ----- | ------ | ------ | --------- | ------ | ------ | - |
|compl.bit    | 48000 | 248832 | 5.184  | 14306684  | 2.759M | 124.22 | 1 |
|he_32khz.bit | 32000 | 172800 | 5.4    | 8426158   | 1.560M | 139.67 | 1 |
|he_44khz.bit | 44100 | 472320 | 10.710 | 21296300  | 1.988M | 144.04 | 1 |
|he_48khz.bit | 48000 | 172800 | 3.6    | 8453846   | 2.348M | 139.67 | 1 |
|hecommon.bit | 44100 | 69120  | 1.567  | 3169715   | 2.022M | 133.93 | 1 |
|he_free.bit  | 44100 | 156672 | 3.552  | 5798418   | 1.632M | 137.48 | 1 |
|he_mode.bit  | 44100 | 262656 | 5.955  | 9882314   | 1.659M | 118.00 | 1 |
|si.bit       | 44100 | 135936 | 3.082  | 7170520   | 2.326M | 120.30 | 1 |
|si_block.bit | 44100 | 73728  | 1.671  | 4233136   | 2.533M | 125.18 | 1 |
|si_huff.bit  | 44100 | 86400  | 1.959  | 4785322   | 2.442M | 107.98 | 1 |
|sin1k0db.bit | 44100 | 725760 | 16.457 | 24842977  | 1.509M | 111.03 | 1 |

通过所有文件（PSNR> 96dB）通过的一致性测试。

## 与 keyj's [minimp3](https://keyj.emphy.de/minimp3/)的比较

特征比较：

| Keyj minimp3 | 此项目 |
| ------------ | ------- |
| 定点  | 浮点 |
| 源代码: 84kb | 70kb |
| 二进制: 34kb (20kb compressed) | 30kb (20kb) |
| 无加速 | 支持SSE/NEON |
| 不支持自由格式 | 支持自由格式 |

下面是Keyj的minimp3的基准和顺应性测试：

| Vector      | Hz    | Samples| Sec    | Clockticks | Clockticks per second | PSNR | Max diff |
| ----------- | ----- | ------ | ------ | --------- | ------  | ----- | - |
|compl.bit    | 48000 | 248832 | 5.184  | 31849373  | 6.143M  | 71.50 | 41 |
|he_32khz.bit | 32000 | 172800 | 5.4    | 26302319  | 4.870M  | 71.63 | 24 |
|he_44khz.bit | 44100 | 472320 | 10.710 | 41628861  | 3.886M  | 71.63 | 24 |
|he_48khz.bit | 48000 | 172800 | 3.6    | 25899527  | 7.194M  | 71.63 | 24 |
|hecommon.bit | 44100 | 69120  | 1.567  | 20437779  | 13.039M | 71.58 | 25 |
|he_free.bit  | 44100 | 0 | 0  | -  | - | -  | - |
|he_mode.bit  | 44100 | 262656 | 5.955  | 30988984  | 5.203M  | 71.78 | 27 |
|si.bit       | 44100 | 135936 | 3.082  | 24096223  | 7.817M  | 72.35 | 36 |
|si_block.bit | 44100 | 73728  | 1.671  | 20722017  | 12.394M | 71.84 | 26 |
|si_huff.bit  | 44100 | 86400  | 1.959  | 21121376  | 10.780M | 27.80 | 65535 |
|sin1k0db.bit | 44100 | 730368 | 16.561 | 55569636  | 3.355M  | 0.15  | 58814 |

keyj minimp3符合测试在所有向量（psnr <96dB）上失败，且不支持自由
格式。使用时会引起一些问题。
[这里](https://github.com/lieff/lvg) 是这项工作的主要动机。

## 使用

首先，我们需要初始化解码器所需要的数据结构：

```c
//#define MINIMP3_ONLY_MP3
//#define MINIMP3_ONLY_SIMD
//#define MINIMP3_NO_SIMD
//#define MINIMP3_NONSTANDARD_BUT_LOGICAL
//#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
...
    static mp3dec_t mp3d;
    mp3dec_init(&mp3d);
```

请注意，您必须在一个源文件中定义 ``MINIMP3_IMPLEMENTATION`` 。
您可以根据需要在尽可能多的文件中添加``#include`` ``minimp3.h`` 。
另外，您可以使用定义 ``MINIMP3_ONLY_MP3`` 来剥离MP1/MP2解码代码。
``MINIMP3_ONLY_SIMD`` 这个定义控制通用（非SSE/NEON）代码生成（始终在X64/ARM64目标上启用）。
如果您不需要任何特定于平台的SIMD优化，则可以定义 ``MINIMP3_NO_SIMD``。
``MINIMP3_NONSTANDARD_BUT_LOGICAL`` 定义保存一些代码字节，并强制执行non-standard but logical的mono-stereo transition（罕见情况）。
``MINIMP3_FLOAT_OUTPUT`` 使得 ``mp3dec_decode_frame()`` 输出浮点而不是short（uint16_t） ，同时，附加函数 ``mp3dec_f32_to_s16 ``在需要的情况下可以做到float->short（uint16_t）。

逐帧解码输入流：

```c
    /*typedef struct
    {
        int frame_bytes;
        int channels;
        int hz;
        int layer;
        int bitrate_kbps;
    } mp3dec_frame_info_t;*/
    mp3dec_frame_info_t info;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    /*unsigned char *input_buf; - input byte stream*/
    samples = mp3dec_decode_frame(&mp3d, input_buf, buf_size, pcm, &info);
```

 ``mp3dec_decode_frame()`` 函数解码一个MP3帧, 要求输入buffer必须足够大以容纳下一个帧

解码器将分析输入缓冲区以与MP3流正确同步，并跳过ID3数据以及任何无效的数据。 短缓冲区可能会导致错误的同步，并可能产生“尖叫”的副作用。 输入缓冲区的大小越大，同步过程就越可靠。 我们建议一次在输入缓冲区中连续多达10个连续的MP3帧（〜16KB）。

在流的末端，只需通过其余的缓冲区，即使在流中只有1帧，同步过程也应起作用（除了最终的免费格式和垃圾外，可能会弄乱事物，因此必须先删除ID3V1和APE标签）。

对于自由格式，进行适当同步需要至少3帧：2帧来检测框架长度，1个检查检测的下一帧是很好的。



消耗的mp3数据的大小在``mp3dec_frame_info_t``结构的 ``frame_bytes`` 中， 必须在下次调用解码函数前从输入buffer中去除 ``frame_bytes`` 字节长度的数据。

解码函数返回解码采样的数量，有以下可能

- **0:** 输入buffer中没找到mp3数据
- **384:**  Layer 1
- **576:**  MPEG 2 Layer 3
- **1152:** 其他

以下是对采样数量和``frame_bytes``字段值的可能组合的描述：

- 采样数量超过0，且 ``frame_bytes > 0``:  解码成功
- 采样数量为0，且 ``frame_bytes >  0``: 解码器跳过了id3或无用的数据
- 采样数量为0，且 ``frame_bytes == 0``: 数据不足

如果 ``frame_bytes == 0``, 那么``mp3dec_frame_info_t``的其他成员可能未初始或未改变; 如果  ``frame_bytes != 0``, 那么``mp3dec_frame_info_t``的其他成员可用。 用户程序可以在调整解码位置时调用 ``mp3dec_init()`` ，但不是必须的。

作为一种特殊情况，解码器支持已经拆分mp3流（例如，在执行MP4解复用之后）。 在这种情况下，输入缓冲区必须完全包含一个非免费格式框架。

## 寻址

您可以跳转到文件流中的任何字节，并调用``mp3dec_decode_frame``; 这几乎在所有情况下都可以使用，但不能完全保证。 当MAX_FRAME_SYNC_MATCHES值增长时，同步过程失败的可能性降低。 默认值MAX_FRAME_SYNC_MATCHES=10 ，同步故障的可能性应非常低。 如果将颗粒数据意外检测为有效的MP3标头，则可能出现short audio artefacting。

高级MP3DEC_EX_SEEK函数支持精确寻求使用索引和二进制搜索，例如（mp3d_seek_to_sample）。

## 音轨长度检测

如果已知文件是CBR，则所有帧的长度都相等，并且缺少ID3标签，这样可以解码第一个帧并将所有帧位置计算为``frame_bytes * N``。 但是由于填充，即使在这种情况下，尺寸也可能有所不同。

一般而言，需要整个流扫描来计算其长度。 如果存在VBR标签，则可以省略扫描（由Lake和FFMPEG等编码器添加），其中包含长度信息。 高级功能如果存在，则会自动使用VBR标签。

## 高级 API

如果仅需要解码文件/缓冲区或使用精确的搜索，则可以使用可选的高级API。
只需要在需要的地方 ``#include`` ``minimp3_ex.h`` 即可。

```c
#define MP3D_SEEK_TO_BYTE   0
#define MP3D_SEEK_TO_SAMPLE 1

#define MINIMP3_PREDECODE_FRAMES 2 /* frames to pre-decode and skip after seek (to fill internal structures) */
/*#define MINIMP3_SEEK_IDX_LINEAR_SEARCH*/ /* define to use linear index search instead of binary search on seek */
#define MINIMP3_IO_SIZE (128*1024) /* io buffer size for streaming functions, must be greater than MINIMP3_BUF_SIZE */
#define MINIMP3_BUF_SIZE (16*1024) /* buffer which can hold minimum 10 consecutive mp3 frames (~16KB) worst case */
#define MINIMP3_ENABLE_RING 0      /* enable hardware magic ring buffer if available, to make less input buffer memmove(s) in callback IO mode */

#define MP3D_E_MEMORY  -1
#define MP3D_E_IOERROR -2

typedef struct
{
    mp3d_sample_t *buffer;
    size_t samples; /* channels included, byte size = samples*sizeof(mp3d_sample_t) */
    int channels, hz, layer, avg_bitrate_kbps;
} mp3dec_file_info_t;

typedef size_t (*MP3D_READ_CB)(void *buf, size_t size, void *user_data);
typedef int (*MP3D_SEEK_CB)(uint64_t position, void *user_data);

typedef struct
{
    MP3D_READ_CB read;
    void *read_data;
    MP3D_SEEK_CB seek;
    void *seek_data;
} mp3dec_io_t;

typedef struct
{
    uint64_t samples;
    mp3dec_frame_info_t info;
    int last_error;
    ...
} mp3dec_ex_t;

typedef int (*MP3D_ITERATE_CB)(void *user_data, const uint8_t *frame, int frame_size, int free_format_bytes, size_t buf_size, uint64_t offset, mp3dec_frame_info_t *info);
typedef int (*MP3D_PROGRESS_CB)(void *user_data, size_t file_size, uint64_t offset, mp3dec_frame_info_t *info);

/* decode whole buffer block */
int mp3dec_load_buf(mp3dec_t *dec, const uint8_t *buf, size_t buf_size, mp3dec_file_info_t *info, MP3D_PROGRESS_CB progress_cb, void *user_data);
int mp3dec_load_cb(mp3dec_t *dec, mp3dec_io_t *io, uint8_t *buf, size_t buf_size, mp3dec_file_info_t *info, MP3D_PROGRESS_CB progress_cb, void *user_data);
/* iterate through frames */
int mp3dec_iterate_buf(const uint8_t *buf, size_t buf_size, MP3D_ITERATE_CB callback, void *user_data);
int mp3dec_iterate_cb(mp3dec_io_t *io, uint8_t *buf, size_t buf_size, MP3D_ITERATE_CB callback, void *user_data);
/* streaming decoder with seeking capability */
int mp3dec_ex_open_buf(mp3dec_ex_t *dec, const uint8_t *buf, size_t buf_size, int seek_method);
int mp3dec_ex_open_cb(mp3dec_ex_t *dec, mp3dec_io_t *io, int seek_method);
void mp3dec_ex_close(mp3dec_ex_t *dec);
int mp3dec_ex_seek(mp3dec_ex_t *dec, uint64_t position);
size_t mp3dec_ex_read(mp3dec_ex_t *dec, mp3d_sample_t *buf, size_t samples);
#ifndef MINIMP3_NO_STDIO
/* stdio versions of file load, iterate and stream */
int mp3dec_load(mp3dec_t *dec, const char *file_name, mp3dec_file_info_t *info, MP3D_PROGRESS_CB progress_cb, void *user_data);
int mp3dec_iterate(const char *file_name, MP3D_ITERATE_CB callback, void *user_data);
int mp3dec_ex_open(mp3dec_ex_t *dec, const char *file_name, int seek_method);
#ifdef _WIN32
int mp3dec_load_w(mp3dec_t *dec, const wchar_t *file_name, mp3dec_file_info_t *info, MP3D_PROGRESS_CB progress_cb, void *user_data);
int mp3dec_iterate_w(const wchar_t *file_name, MP3D_ITERATE_CB callback, void *user_data);
int mp3dec_ex_open_w(mp3dec_ex_t *dec, const wchar_t *file_name, int seek_method);
#endif
#endif
```

使用 MINIMP3_NO_STDIO 定义 来排除 STDIO 函数.
MINIMP3_ALLOW_MONO_STEREO_TRANSITION 可以在同一个文件里混合单声道和立体声。
In that case ``mp3dec_frame_info_t->channels = 0`` is reported on such files and correct channels number passed to progress_cb callback for each frame in mp3dec_frame_info_t structure.
在这种情况下``mp3dec_frame_info_t->channels = 0``在此类文件上报告了MP3DEC_FRAME_INFO_T结构中每个帧的正确频道编号。

MP3D_PROGRESS_CB 是可选的，且可以为NULL。

文件解码实例:

```c
    mp3dec_t mp3d;
    mp3dec_file_info_t info;
    if (mp3dec_load(&mp3d, input_file_name, &info, NULL, NULL))
    {
        /* error */
    }
    /* mp3dec_file_info_t contains decoded samples and info,
       use free(info.buffer) to deallocate samples */
```

用搜索功能解码的文件解码的示例：

```c
    mp3dec_ex_t dec;
    if (mp3dec_ex_open(&dec, input_file_name, MP3D_SEEK_TO_SAMPLE))
    {
        /* error */
    }
    /* dec.samples, dec.info.hz, dec.info.layer, dec.info.channels should be filled */
    if (mp3dec_ex_seek(&dec, position))
    {
        /* error */
    }
    mp3d_sample_t *buffer = malloc(dec.samples*sizeof(mp3d_sample_t));
    size_t readed = mp3dec_ex_read(&dec, buffer, dec.samples);
    if (readed != dec.samples) /* normal eof or error condition */
    {
        if (dec.last_error)
        {
            /* error */
        }
    }
```

## Bindings

 * https://github.com/tosone/minimp3 - go bindings
 * https://github.com/notviri/rmp3 - rust `no_std` bindings which don't allocate.
 * https://github.com/germangb/minimp3-rs - rust bindings
 * https://github.com/johangu/node-minimp3 - NodeJS bindings
 * https://github.com/pyminimp3/pyminimp3 - python bindings
 * https://github.com/bashi/minimp3-wasm - wasm bindings
 * https://github.com/DasZiesel/minimp3-delphi - delphi bindings
 * https://github.com/mgeier/minimp3_ex-sys - low-level rust bindings to `minimp3_ex`

## Interesting links

 * https://keyj.emphy.de/minimp3/
 * https://github.com/technosaurus/PDMP3
 * https://github.com/technosaurus/PDMP2
 * https://github.com/packjpg/packMP3
 * https://sites.google.com/a/kmlager.com/www/projects
 * https://sourceforge.net/projects/mp3dec/
 * http://blog.bjrn.se/2008/10/lets-build-mp3-decoder.html
 * http://www.mp3-converter.com/mp3codec/
 * http://www.multiweb.cz/twoinches/mp3inside.htm
 * https://www.mp3-tech.org/
 * https://id3.org/mp3Frame
 * https://www.datavoyage.com/mpgscript/mpeghdr.htm
