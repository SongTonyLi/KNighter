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

File:| format/mpeg.c  
---|---  
Warning:| line 307, column 51  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


186   |         state = ((state << 8) | v) & 0xffffff;
187   |     }
188   |     val = -1;
189   |  
190   | found:
191   |     *header_state = state;
192   |     *size_ptr     = n;
193   |  return val;
194   | }
195   |  
196   | /**
197   |  * Extract stream types from a program stream map
198   |  * According to ISO/IEC 13818-1 ('MPEG-2 Systems') table 2-35
199   |  *
200   |  * @return number of bytes occupied by PSM in the bitstream
201   |  */
202   | static long mpegps_psm_parse(MpegDemuxContext *m, AVIOContext *pb)
203   | {
204   |  int psm_length, ps_info_length, es_map_length;
205   |  
206   |     psm_length = avio_rb16(pb);
207   |     avio_r8(pb);
208   |     avio_r8(pb);
209   |     ps_info_length = avio_rb16(pb);
210   |  
211   |  /* skip program_stream_info */
212   |     avio_skip(pb, ps_info_length);
213   |  /*es_map_length = */avio_rb16(pb);
214   |  /* Ignore es_map_length, trust psm_length */
215   |     es_map_length = psm_length - ps_info_length - 10;
216   |  
217   |  /* at least one es available? */
218   |  while (es_map_length >= 4) {
219   |  unsigned char type      = avio_r8(pb);
220   |  unsigned char es_id     = avio_r8(pb);
221   |         uint16_t es_info_length = avio_rb16(pb);
222   |  
223   |  /* remember mapping from stream id to stream type */
224   |         m->psm_es_type[es_id] = type;
225   |  /* skip program_stream_info */
226   |         avio_skip(pb, es_info_length);
227   |         es_map_length -= 4 + es_info_length;
228   |     }
229   |     avio_rb32(pb); /* crc32 */
230   |  return 2 + psm_length;
231   | }
232   |  
233   | /* read the next PES header. Return its position in ppos
234   |  * (if not NULL), and its start code, pts and dts.
235   |  */
236   | static int mpegps_read_pes_header(AVFormatContext *s,
237   |                                   int64_t *ppos, int *pstart_code,
238   |                                   int64_t *ppts, int64_t *pdts)
239   | {
240   |  MpegDemuxContext *m = s->priv_data;
241   |  int len, size, startcode, c, flags, header_len;
242   |  int pes_ext, ext2_len, id_ext, skip;
243   |     int64_t pts, dts;
244   |  int64_t last_sync = avio_tell(s->pb);
245   |  
246   | error_redo:
247   |  avio_seek(s->pb, last_sync, SEEK_SET);
248   | redo:
249   |  /* next start code (should be immediately after) */
250   |  m->header_state = 0xff;
251   |  size      = MAX_SYNC_SIZE;
252   |     startcode = find_next_start_code(s->pb, &size, &m->header_state);
253   |     last_sync = avio_tell(s->pb);
254   |  if (startcode < 0) {
    1Assuming 'startcode' is >= 0→
    2←Taking false branch→
255   |  if (avio_feof(s->pb))
256   |  return AVERROR_EOF;
257   |  // FIXME we should remember header_state
258   |  return FFERROR_REDO;
259   |     }
260   |  
261   |  if (startcode == PACK_START_CODE)
    3←Assuming 'startcode' is not equal to PACK_START_CODE→
    4←Taking false branch→
262   |  goto redo;
263   |  if (startcode == SYSTEM_HEADER_START_CODE)
    5←Assuming 'startcode' is not equal to SYSTEM_HEADER_START_CODE→
    6←Taking false branch→
264   |  goto redo;
265   |  if (startcode == PADDING_STREAM) {
    7←Assuming 'startcode' is not equal to PADDING_STREAM→
    8←Taking false branch→
266   |         avio_skip(s->pb, avio_rb16(s->pb));
267   |  goto redo;
268   |     }
269   |  if (startcode == PRIVATE_STREAM_2) {
    9←Assuming 'startcode' is equal to PRIVATE_STREAM_2→
    10←Taking true branch→
270   |  if (!m->sofdec) {
    11←Assuming field 'sofdec' is 0→
    12←Taking true branch→
271   |  /* Need to detect whether this from a DVD or a 'Sofdec' stream */
272   |  int len = avio_rb16(s->pb);
273   |  int bytesread = 0;
274   |             uint8_t *ps2buf = av_malloc(len);
275   |  
276   |  if (ps2buf) {
    13←Assuming 'ps2buf' is non-null→
    14←Taking true branch→
277   |  bytesread = avio_read(s->pb, ps2buf, len);
278   |  
279   |  if (bytesread != len) {
    15←Assuming 'bytesread' is equal to 'len'→
    16←Taking false branch→
280   |                     avio_skip(s->pb, len - bytesread);
281   |                 } else {
282   |  uint8_t *p = 0;
283   |  if (len >= 6)
    17←Assuming 'len' is >= 6→
    18←Taking true branch→
284   |  p = memchr(ps2buf, 'S', len - 5);
285   |  
286   |  if (p)
    19←Assuming 'p' is null→
    20←Taking false branch→
287   |                         m->sofdec = !memcmp(p+1, "ofdec", 5);
288   |  
289   |  m->sofdec -= !m->sofdec;
290   |  
291   |  if (m->sofdec20.1Field 'sofdec' is < 0 < 0) {
292   |  if (len == 980  && ps2buf[0] == 0) {
    21←Assuming 'len' is not equal to 980→
293   |  /* PCI structure? */
294   |                             uint32_t startpts = AV_RB32(ps2buf + 0x0d);
295   |                             uint32_t endpts = AV_RB32(ps2buf + 0x11);
296   |                             uint8_t hours = ((ps2buf[0x19] >> 4) * 10) + (ps2buf[0x19] & 0x0f);
297   |                             uint8_t mins  = ((ps2buf[0x1a] >> 4) * 10) + (ps2buf[0x1a] & 0x0f);
298   |                             uint8_t secs  = ((ps2buf[0x1b] >> 4) * 10) + (ps2buf[0x1b] & 0x0f);
299   |  
300   |                             m->dvd = (hours <= 23 &&
301   |                                       mins  <= 59 &&
302   |                                       secs  <= 59 &&
303   |                                       (ps2buf[0x19] & 0x0f) < 10 &&
304   |                                       (ps2buf[0x1a] & 0x0f) < 10 &&
305   |                                       (ps2buf[0x1b] & 0x0f) < 10 &&
306   |                                       endpts >= startpts);
307   |                         } else if (len == 1018 && ps2buf[0] == 1) {
    22←Assuming 'len' is equal to 1018→
    23←buffer read by avio_read may be partially uninitialized
308   |  /* DSI structure? */
309   |                             uint8_t hours = ((ps2buf[0x1d] >> 4) * 10) + (ps2buf[0x1d] & 0x0f);
310   |                             uint8_t mins  = ((ps2buf[0x1e] >> 4) * 10) + (ps2buf[0x1e] & 0x0f);
311   |                             uint8_t secs  = ((ps2buf[0x1f] >> 4) * 10) + (ps2buf[0x1f] & 0x0f);
312   |  
313   |                             m->dvd = (hours <= 23 &&
314   |                                       mins  <= 59 &&
315   |                                       secs  <= 59 &&
316   |                                       (ps2buf[0x1d] & 0x0f) < 10 &&
317   |                                       (ps2buf[0x1e] & 0x0f) < 10 &&
318   |                                       (ps2buf[0x1f] & 0x0f) < 10);
319   |                         }
320   |                     }
321   |                 }
322   |  
323   |                 av_free(ps2buf);
324   |  
325   |  /* If this isn't a DVD packet or no memory
326   |  * could be allocated, just ignore it.
327   |  * If we did, move back to the start of the
328   |  * packet (plus 'length' field) */
329   |  if (!m->dvd || avio_skip(s->pb, -(len + 2)) < 0) {
330   |  /* Skip back failed.
331   |  * This packet will be lost but that can't be helped
332   |  * if we can't skip back
333   |  */
334   |  goto redo;
335   |                 }
336   |             } else {
337   |  /* No memory */

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
