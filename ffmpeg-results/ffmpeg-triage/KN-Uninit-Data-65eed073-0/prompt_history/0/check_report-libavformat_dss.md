# Instruction

Determine whether the static analyzer report is a real bug in the Linux kernel and matches the target bug pattern

Your analysis should:
- **Compare the report against the provided target bug pattern specification,** using the **buggy function (pre-patch)** and the **fix patch** as the reference.
- Explain your reasoning for classifying this as either:
  - **A true positive** (matches the target bug pattern **and** is a real bug), or
  - **A false positive** (does **not** match the target bug pattern **or** is **not** a real bug).

Please evaluate thoroughly using the following process:

- **First, understand** the reported code pattern and its control/data flow.
- **Then, compare** it against the target bug pattern characteristics.
- **Finally, validate** against the **pre-/post-patch** behavior:
  - The reported case demonstrates the same root cause pattern as the target bug pattern/function and would be addressed by a similar fix.

- **Numeric / bounds feasibility** (if applicable):
  - Infer tight **min/max** ranges for all involved variables from types, prior checks, and loop bounds.
  - Show whether overflow/underflow or OOB is actually triggerable (compute the smallest/largest values that violate constraints).

- **Null-pointer dereference feasibility** (if applicable):
  1. **Identify the pointer source** and return convention of the producing function(s) in this path (e.g., returns **NULL**, **ERR_PTR**, negative error code via cast, or never-null).
  2. **Check real-world feasibility in this specific driver/socket/filesystem/etc.**:
     - Enumerate concrete conditions under which the producer can return **NULL/ERR_PTR** here (e.g., missing DT/ACPI property, absent PCI device/function, probe ordering, hotplug/race, Kconfig options, chip revision/quirks).
     - Verify whether those conditions can occur given the driver’s init/probe sequence and the kernel helpers used.
  3. **Lifetime & concurrency**: consider teardown paths, RCU usage, refcounting (`get/put`), and whether the pointer can become invalid/NULL across yields or callbacks.
  4. If the producer is provably non-NULL in this context (by spec or preceding checks), classify as **false positive**.

If there is any uncertainty in the classification, **err on the side of caution and classify it as a false positive**. Your analysis will be used to improve the static analyzer's accuracy.

## Patch Description

avformat: check avio_read() return values in dss/dtshd/mlv

Multiple demuxers call avio_read() without checking its return
value. When input is truncated, destination buffers remain
uninitialized but are still used for offset calculations, memcmp,
and metadata handling. This results in undefined behavior
(detectable with Valgrind/MSan).

Fix this by checking the return value of avio_read() in:
- dss.c: dss_read_seek() — check before using header buffer
- dtshddec.c: FILEINFO chunk — check before using value buffer
- mlvdec.c: check_file_header() — check before memcmp on version

Fixes: #21520

## Buggy Code

```c
// Function: dss_read_seek in libavformat/dss.c
static int dss_read_seek(AVFormatContext *s, int stream_index,
                         int64_t timestamp, int flags)
{
    DSSDemuxContext *ctx = s->priv_data;
    int64_t ret, seekto;
    uint8_t header[DSS_AUDIO_BLOCK_HEADER_SIZE];
    int offset;

    if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
        seekto = timestamp / 264 * 41 / 506 * 512;
    else
        seekto = timestamp / 240 * ctx->packet_size / 506 * 512;

    if (seekto < 0)
        seekto = 0;

    seekto += ctx->dss_header_size;

    ret = avio_seek(s->pb, seekto, SEEK_SET);
    if (ret < 0)
        return ret;

    avio_read(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
    ctx->swap = !!(header[0] & 0x80);
    offset = 2*header[1] + 2*ctx->swap;
    if (offset < DSS_AUDIO_BLOCK_HEADER_SIZE)
        return AVERROR_INVALIDDATA;
    if (offset == DSS_AUDIO_BLOCK_HEADER_SIZE) {
        ctx->counter = 0;
        offset = avio_skip(s->pb, -DSS_AUDIO_BLOCK_HEADER_SIZE);
    } else {
        ctx->counter = DSS_BLOCK_SIZE - offset;
        offset = avio_skip(s->pb, offset - DSS_AUDIO_BLOCK_HEADER_SIZE);
    }
    ctx->dss_sp_swap_byte = -1;
    return 0;
}
```

```c
// Function: check_file_header in libavformat/mlvdec.c
static int check_file_header(AVIOContext *pb, uint64_t guid)
{
    unsigned int size;
    uint8_t version[8];

    avio_skip(pb, 4);
    size = avio_rl32(pb);
    if (size < 52)
        return AVERROR_INVALIDDATA;
    avio_read(pb, version, 8);
    if (memcmp(version, MLV_VERSION, 5) || avio_rl64(pb) != guid)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, size - 24);
    return 0;
}
```

```c
// Function: dtshd_read_header in libavformat/dtshddec.c
static int dtshd_read_header(AVFormatContext *s)
{
    DTSHDDemuxContext *dtshd = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t chunk_type, chunk_size;
    int64_t duration, orig_nb_samples, data_start;
    AVStream *st;
    FFStream *sti;
    int ret;
    char *value;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_DTS;
    sti = ffstream(st);
    sti->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    for (;;) {
        chunk_type = avio_rb64(pb);
        chunk_size = avio_rb64(pb);

        if (avio_feof(pb))
            break;

        if (chunk_size < 4) {
            av_log(s, AV_LOG_ERROR, "chunk size too small\n");
            return AVERROR_INVALIDDATA;
        }
        if (chunk_size > ((uint64_t)1 << 61)) {
            av_log(s, AV_LOG_ERROR, "chunk size too big\n");
            return AVERROR_INVALIDDATA;
        }

        switch (chunk_type) {
        case STRMDATA:
            data_start = avio_tell(pb);
            dtshd->data_end = data_start + chunk_size;
            if (dtshd->data_end <= chunk_size)
                return AVERROR_INVALIDDATA;
            if (!(pb->seekable & AVIO_SEEKABLE_NORMAL))
                goto break_loop;
            goto skip;
            break;
        case AUPR_HDR:
            if (chunk_size < 21)
                return AVERROR_INVALIDDATA;
            avio_skip(pb, 3);
            st->codecpar->sample_rate = avio_rb24(pb);
            if (!st->codecpar->sample_rate)
                return AVERROR_INVALIDDATA;
            duration  = avio_rb32(pb); // num_frames
            duration *= avio_rb16(pb); // samples_per_frames
            st->duration = duration;
            orig_nb_samples  = avio_rb32(pb);
            orig_nb_samples <<= 8;
            orig_nb_samples |= avio_r8(pb);
            st->codecpar->ch_layout.nb_channels = ff_dca_count_chs_for_mask(avio_rb16(pb));
            st->codecpar->initial_padding = avio_rb16(pb);
            st->codecpar->trailing_padding = FFMAX(st->duration - orig_nb_samples - st->codecpar->initial_padding, 0);
            st->start_time =
            sti->start_skip_samples = st->codecpar->initial_padding;
            sti->first_discard_sample = orig_nb_samples + st->codecpar->initial_padding;
            sti->last_discard_sample = st->duration;
            avio_skip(pb, chunk_size - 21);
            break;
        case FILEINFO:
            if (chunk_size > INT_MAX)
                goto skip;
            value = av_malloc(chunk_size);
            if (!value)
                goto skip;
            avio_read(pb, value, chunk_size);
            value[chunk_size - 1] = 0;
            av_dict_set(&s->metadata, "fileinfo", value,
                        AV_DICT_DONT_STRDUP_VAL);
            break;
        default:
skip:
            ret = avio_skip(pb, chunk_size);
            if (ret < 0)
                return ret;
        };
    }

    if (!dtshd->data_end)
        return AVERROR_EOF;

    avio_seek(pb, data_start, SEEK_SET);

break_loop:
    if (st->codecpar->sample_rate)
        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}
```

## Bug Fix Patch

```diff
diff --git a/libavformat/dss.c b/libavformat/dss.c
index 6cabdb5421..ce86b32d6f 100644
--- a/libavformat/dss.c
+++ b/libavformat/dss.c
@@ -26,6 +26,7 @@
 #include "avformat.h"
 #include "demux.h"
 #include "internal.h"
+#include "avio_internal.h"
 
 #define DSS_HEAD_OFFSET_AUTHOR        0xc
 #define DSS_AUTHOR_SIZE               16
@@ -339,7 +340,9 @@ static int dss_read_seek(AVFormatContext *s, int stream_index,
     if (ret < 0)
         return ret;
 
-    avio_read(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
+    ret = ffio_read_size(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
+    if (ret < 0)
+        return ret;
     ctx->swap = !!(header[0] & 0x80);
     offset = 2*header[1] + 2*ctx->swap;
     if (offset < DSS_AUDIO_BLOCK_HEADER_SIZE)
diff --git a/libavformat/dtshddec.c b/libavformat/dtshddec.c
index b980fde6a9..843fd5460d 100644
--- a/libavformat/dtshddec.c
+++ b/libavformat/dtshddec.c
@@ -26,6 +26,7 @@
 #include "avformat.h"
 #include "demux.h"
 #include "internal.h"
+#include "avio_internal.h"
 
 #define AUPR_HDR 0x415550522D484452
 #define AUPRINFO 0x41555052494E464F
@@ -125,7 +126,11 @@ static int dtshd_read_header(AVFormatContext *s)
             value = av_malloc(chunk_size);
             if (!value)
                 goto skip;
-            avio_read(pb, value, chunk_size);
+            ret = ffio_read_size(pb, value, chunk_size);
+            if (ret < 0) {
+                av_free(value);
+                goto skip;
+            }
             value[chunk_size - 1] = 0;
             av_dict_set(&s->metadata, "fileinfo", value,
                         AV_DICT_DONT_STRDUP_VAL);
diff --git a/libavformat/mlvdec.c b/libavformat/mlvdec.c
index fa35bc9c45..2c1fe001c7 100644
--- a/libavformat/mlvdec.c
+++ b/libavformat/mlvdec.c
@@ -36,6 +36,7 @@
 #include "avformat.h"
 #include "demux.h"
 #include "internal.h"
+#include "avio_internal.h"
 #include "riff.h"
 
 #define MLV_VERSION "v2.0"
@@ -74,12 +75,15 @@ static int check_file_header(AVIOContext *pb, uint64_t guid)
 {
     unsigned int size;
     uint8_t version[8];
+    int ret;
 
     avio_skip(pb, 4);
     size = avio_rl32(pb);
     if (size < 52)
         return AVERROR_INVALIDDATA;
-    avio_read(pb, version, 8);
+    ret = ffio_read_size(pb, version, 8);
+    if (ret < 0)
+        return ret;
     if (memcmp(version, MLV_VERSION, 5) || avio_rl64(pb) != guid)
         return AVERROR_INVALIDDATA;
     avio_skip(pb, size - 24);
```


## Bug Pattern

The bug pattern is **calling a partial/short-read I/O API (`avio_read()`) and then using the destination buffer as if it were fully initialized, without verifying that the requested number of bytes was actually read**.

This commonly happens when:
- data is read into a stack or heap buffer,
- the return value of the read function is ignored,
- the buffer is then used for:
  - parsing fields / offset calculations,
  - `memcmp()` or other comparisons,
  - string/metadata handling,
  - control-flow decisions.

If the input is truncated or the read is otherwise short, part of the buffer remains uninitialized, causing undefined behavior and potentially incorrect parsing or memory-safety issues. The correct pattern is to check for a full read (or use a helper like `ffio_read_size()` that guarantees exact-size reads or returns an error) before consuming the buffer.

# Report

### Report Summary

File:| format/dss.c  
---|---  
Warning:| line 199, column 33  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


137   |  return ret;
138   |  
139   |     ret = dss_read_metadata_string(s, DSS_HEAD_OFFSET_COMMENT,
140   |  DSS_COMMENT_SIZE, "comment");
141   |  if (ret)
142   |  return ret;
143   |  
144   |     avio_seek(pb, DSS_HEAD_OFFSET_ACODEC, SEEK_SET);
145   |     ctx->audio_codec = avio_r8(pb);
146   |  
147   |  if (ctx->audio_codec == DSS_ACODEC_DSS_SP) {
148   |         st->codecpar->codec_id    = AV_CODEC_ID_DSS_SP;
149   |         st->codecpar->sample_rate = 11025;
150   |         s->bit_rate = 8 * (DSS_FRAME_SIZE - 1) * st->codecpar->sample_rate
151   |                         * 512 / (506 * 264);
152   |     } else if (ctx->audio_codec == DSS_ACODEC_G723_1) {
153   |         st->codecpar->codec_id    = AV_CODEC_ID_G723_1;
154   |         st->codecpar->sample_rate = 8000;
155   |     } else {
156   |         avpriv_request_sample(s, "Support for codec %x in DSS",
157   |                               ctx->audio_codec);
158   |  return AVERROR_PATCHWELCOME;
159   |     }
160   |  
161   |     st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
162   |     st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
163   |  
164   |     avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
165   |     st->start_time = 0;
166   |  
167   |  /* Jump over header */
168   |  
169   |  if ((ret64 = avio_seek(pb, ctx->dss_header_size, SEEK_SET)) < 0)
170   |  return (int)ret64;
171   |  
172   |     ctx->counter = 0;
173   |     ctx->swap    = 0;
174   |  
175   |  return 0;
176   | }
177   |  
178   | static void dss_skip_audio_header(AVFormatContext *s, AVPacket *pkt)
179   | {
180   |     DSSDemuxContext *ctx = s->priv_data;
181   |     AVIOContext *pb = s->pb;
182   |  
183   |     avio_skip(pb, DSS_AUDIO_BLOCK_HEADER_SIZE);
184   |     ctx->counter += DSS_BLOCK_SIZE - DSS_AUDIO_BLOCK_HEADER_SIZE;
185   | }
186   |  
187   | static void dss_sp_byte_swap(DSSDemuxContext *ctx, uint8_t *data)
188   | {
189   |  int i;
190   |  
191   |  if (ctx->swap14.1Field 'swap' is 0) {
    15←Taking false branch→
192   |  for (i = 0; i < DSS_FRAME_SIZE - 2; i += 2)
193   |             data[i] = data[i + 4];
194   |  
195   |  /* Zero the padding. */
196   |         data[DSS_FRAME_SIZE] = 0;
197   |         data[1] = ctx->dss_sp_swap_byte;
198   |     } else {
199   |  ctx->dss_sp_swap_byte = data[DSS_FRAME_SIZE - 2];
    16←buffer read by avio_read may be partially uninitialized
200   |     }
201   |  
202   |  /* make sure byte 40 is always 0 */
203   |     data[DSS_FRAME_SIZE - 2] = 0;
204   |     ctx->swap             ^= 1;
205   | }
206   |  
207   | static int dss_sp_read_packet(AVFormatContext *s, AVPacket *pkt)
208   | {
209   |  DSSDemuxContext *ctx = s->priv_data;
210   |  int read_size, ret, offset = 0, buff_offset = 0;
211   |     int64_t pos = avio_tell(s->pb);
212   |  
213   |  if (ctx->counter == 0)
    4←Assuming field 'counter' is not equal to 0→
    5←Taking false branch→
214   |         dss_skip_audio_header(s, pkt);
215   |  
216   |  if (ctx->swap) {
    6←Assuming field 'swap' is 0→
    7←Taking false branch→
217   |         read_size   = DSS_FRAME_SIZE - 2;
218   |         buff_offset = 3;
219   |     } else
220   |  read_size = DSS_FRAME_SIZE;
221   |  
222   |  ret = av_new_packet(pkt, DSS_FRAME_SIZE);
223   |  if (ret < 0)
    8←Assuming 'ret' is >= 0→
    9←Taking false branch→
224   |  return ret;
225   |  
226   |  pkt->duration     = 264;
227   |     pkt->pos = pos;
228   |     pkt->stream_index = 0;
229   |  
230   |  if (ctx->counter < read_size) {
    10←Assuming 'read_size' is <= field 'counter'→
    11←Taking false branch→
231   |         ret = avio_read(s->pb, pkt->data + buff_offset,
232   |                         ctx->counter);
233   |  if (ret < ctx->counter)
234   |  goto error_eof;
235   |  
236   |         offset = ctx->counter;
237   |         dss_skip_audio_header(s, pkt);
238   |     }
239   |  ctx->counter -= read_size;
240   |  
241   |  /* This will write one byte into pkt's padding if buff_offset == 3 */
242   |     ret = avio_read(s->pb, pkt->data + offset + buff_offset,
243   |                     read_size - offset);
244   |  if (ret < read_size - offset)
    12←Assuming the condition is false→
    13←Taking false branch→
245   |  goto error_eof;
246   |  
247   |  dss_sp_byte_swap(ctx, pkt->data);
    14←Calling 'dss_sp_byte_swap'→
248   |  
249   |  if (ctx->dss_sp_swap_byte < 0) {
250   |  return AVERROR(EAGAIN);
251   |     }
252   |  
253   |  return 0;
254   |  
255   | error_eof:
256   |  return ret < 0 ? ret : AVERROR_EOF;
257   | }
258   |  
259   | static int dss_723_1_read_packet(AVFormatContext *s, AVPacket *pkt)
260   | {
261   |     DSSDemuxContext *ctx = s->priv_data;
262   |     AVStream *st = s->streams[0];
263   |  int size, byte, ret, offset;
264   |     int64_t pos = avio_tell(s->pb);
265   |  
266   |  if (ctx->counter == 0)
267   |         dss_skip_audio_header(s, pkt);
268   |  
269   |  /* We make one byte-step here. Don't forget to add offset. */
270   |     byte = avio_r8(s->pb);
271   |  if (byte == 0xff)
272   |  return AVERROR_INVALIDDATA;
273   |  
274   |     size = frame_size[byte & 3];
275   |  
276   |     ctx->packet_size = size;
277   |     ctx->counter--;
278   |  
279   |     ret = av_new_packet(pkt, size);
280   |  if (ret < 0)
281   |  return ret;
282   |     pkt->pos = pos;
283   |  
284   |     pkt->data[0]  = byte;
285   |     offset        = 1;
286   |     pkt->duration = 240;
287   |     s->bit_rate = 8LL * size-- * st->codecpar->sample_rate * 512 / (506 * pkt->duration);
288   |  
289   |     pkt->stream_index = 0;
290   |  
291   |  if (ctx->counter < size) {
292   |         ret = avio_read(s->pb, pkt->data + offset,
293   |                         ctx->counter);
294   |  if (ret < ctx->counter)
295   |  return ret < 0 ? ret : AVERROR_EOF;
296   |  
297   |         offset += ctx->counter;
298   |         size   -= ctx->counter;
299   |         ctx->counter = 0;
300   |         dss_skip_audio_header(s, pkt);
301   |     }
302   |     ctx->counter -= size;
303   |  
304   |     ret = avio_read(s->pb, pkt->data + offset, size);
305   |  if (ret < size)
306   |  return ret < 0 ? ret : AVERROR_EOF;
307   |  
308   |  return 0;
309   | }
310   |  
311   | static int dss_read_packet(AVFormatContext *s, AVPacket *pkt)
312   | {
313   |  DSSDemuxContext *ctx = s->priv_data;
314   |  
315   |  if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
    1Assuming field 'audio_codec' is equal to DSS_ACODEC_DSS_SP→
    2←Taking true branch→
316   |  return dss_sp_read_packet(s, pkt);
    3←Calling 'dss_sp_read_packet'→
317   |  else
318   |  return dss_723_1_read_packet(s, pkt);
319   | }
320   |  
321   | static int dss_read_seek(AVFormatContext *s, int stream_index,
322   |                          int64_t timestamp, int flags)
323   | {
324   |     DSSDemuxContext *ctx = s->priv_data;
325   |     int64_t ret, seekto;
326   |     uint8_t header[DSS_AUDIO_BLOCK_HEADER_SIZE];
327   |  int offset;
328   |  
329   |  if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
330   |         seekto = timestamp / 264 * 41 / 506 * 512;
331   |  else
332   |         seekto = timestamp / 240 * ctx->packet_size / 506 * 512;
333   |  
334   |  if (seekto < 0)
335   |         seekto = 0;
336   |  
337   |     seekto += ctx->dss_header_size;
338   |  
339   |     ret = avio_seek(s->pb, seekto, SEEK_SET);
340   |  if (ret < 0)
341   |  return ret;
342   |  
343   |     ret = ffio_read_size(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
344   |  if (ret < 0)
345   |  return ret;
346   |     ctx->swap = !!(header[0] & 0x80);

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
