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

File:| /home/cc/ai2sec/KNighter/ffmpeg/./libavcodec/get_bits.h  
---|---  
Warning:| line 394, column 26  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


261   |  if (ret < 0)
262   |  return ret;
263   |  if (ret != obu_unit_size)
264   |  return AVERROR_INVALIDDATA;
265   |  
266   |     c->temporal_unit_size -= obu_unit_size + len;
267   |     c->frame_unit_size -= obu_unit_size + len;
268   |  
269   | end:
270   |     ret = av_bsf_send_packet(c->bsf, pkt);
271   |  if (ret < 0) {
272   |         av_log(s, AV_LOG_ERROR, "Failed to send packet to "
273   |  "av1_frame_merge filter\n");
274   |  return ret;
275   |     }
276   |  
277   |     ret = av_bsf_receive_packet(c->bsf, pkt);
278   |  if (ret < 0) {
279   |  if (ret == AVERROR(EAGAIN))
280   |  goto retry;
281   |  if (ret != AVERROR_EOF)
282   |             av_log(s, AV_LOG_ERROR, "av1_frame_merge filter failed to "
283   |  "send output packet\n");
284   |  return ret;
285   |     }
286   |  
287   |     pkt->pos = pos;
288   |  
289   |  return 0;
290   | }
291   |  
292   | const FFInputFormat ff_av1_demuxer = {
293   |     .p.name         = "av1",
294   |     .p.long_name    = NULL_IF_CONFIG_SMALL("AV1 Annex B"),
295   |     .p.extensions   = "obu",
296   |     .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
297   |     .p.priv_class   = &av1_demuxer_class,
298   |     .priv_data_size = sizeof(AV1DemuxContext),
299   |     .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
300   |     .read_probe     = annexb_probe,
301   |     .read_header    = av1_read_header,
302   |     .read_packet    = annexb_read_packet,
303   |     .read_close     = av1_read_close,
304   | };
305   | #endif
306   |  
307   | #if CONFIG_OBU_DEMUXER
308   | //For low overhead obu, we can't foresee the obu size before we parsed the header.
309   | //So, we can't use parse_obu_header here, since it will check size <= buf_size
310   | //see c27c7b49dc for more details
311   | static int read_obu_with_size(const uint8_t *buf, int buf_size, int64_t *obu_size, int *type)
312   | {
313   |  GetBitContext gb;
314   |  int ret, extension_flag, start_pos;
315   |     int64_t size;
316   |  
317   |  ret = init_get_bits8(&gb, buf, FFMIN(buf_size, MAX_OBU_HEADER_SIZE));
    10←Assuming the condition is true→
    11←'?' condition is true→
318   |  if (ret11.1'ret' is >= 011.1'ret' is >= 0 < 0)
    12←Taking false branch→
319   |  return ret;
320   |  
321   |  if (get_bits1(&gb) != 0) // obu_forbidden_bit
    13←Calling 'get_bits1'→
322   |  return AVERROR_INVALIDDATA;
323   |  
324   |     *type      = get_bits(&gb, 4);
325   |     extension_flag = get_bits1(&gb);
326   |  if (!get_bits1(&gb))    // has_size_flag
327   |  return AVERROR_INVALIDDATA;
328   |     skip_bits1(&gb);        // obu_reserved_1bit
329   |  
330   |  if (extension_flag) {
331   |         get_bits(&gb, 3);   // temporal_id
332   |         get_bits(&gb, 2);   // spatial_id
333   |         skip_bits(&gb, 3);  // extension_header_reserved_3bits
334   |     }
335   |  
336   |     *obu_size  = get_leb128(&gb);
337   |  if (*obu_size > INT_MAX)
338   |  return AVERROR_INVALIDDATA;
339   |  
340   |  if (get_bits_left(&gb) < 0)
341   |  return AVERROR_INVALIDDATA;
342   |  
343   |     start_pos = get_bits_count(&gb) / 8;
344   |  
345   |     size = *obu_size + start_pos;
346   |  if (size > INT_MAX)
347   |  return AVERROR_INVALIDDATA;
348   |  return size;
349   | }
350   |  
351   | static int obu_probe(const AVProbeData *p)
352   | {
353   |     int64_t obu_size;
354   |  int seq = 0;
355   |  int ret, type, cnt;
356   |  
357   |  // Check that the first OBU is a Temporal Delimiter.
358   |     cnt = read_obu_with_size(p->buf, p->buf_size, &obu_size, &type);
359   |  if (cnt < 0 || type != AV1_OBU_TEMPORAL_DELIMITER || obu_size != 0)
360   |  return 0;
361   |  
362   |  while (1) {
363   |         ret = read_obu_with_size(p->buf + cnt, p->buf_size - cnt, &obu_size, &type);
364   |  if (ret < 0 || obu_size <= 0)
365   |  return 0;
366   |         cnt += FFMIN(ret, p->buf_size - cnt);
367   |  
368   |         ret = get_score(type, &seq);
369   |  if (ret >= 0)
370   |  return ret;
371   |     }
372   |  return 0;
373   | }
374   |  
375   | static int obu_get_packet(AVFormatContext *s, AVPacket *pkt)
376   | {
377   |  AV1DemuxContext *const c = s->priv_data;
378   |     uint8_t header[MAX_OBU_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
379   |     int64_t obu_size;
380   |  int size;
381   |  int ret, len, type;
382   |  
383   |  if ((ret = ffio_ensure_seekback(s->pb, MAX_OBU_HEADER_SIZE)) < 0)
    5←Assuming the condition is false→
    6←Taking false branch→
384   |  return ret;
385   |  size = avio_read(s->pb, header, MAX_OBU_HEADER_SIZE);
386   |  if (size < 0)
    7←Assuming 'size' is >= 0→
    8←Taking false branch→
387   |  return size;
388   |  
389   |  memset(header + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
390   |  len = read_obu_with_size(header, size, &obu_size, &type);
    9←Calling 'read_obu_with_size'→
391   |  if (len < 0) {
392   |         av_log(c, AV_LOG_ERROR, "Failed to read obu\n");
393   |  return len;
394   |     }
395   |     avio_seek(s->pb, -size, SEEK_CUR);
396   |  
397   |     ret = av_get_packet(s->pb, pkt, len);
398   |  if (ret != len) {
399   |         av_log(c, AV_LOG_ERROR, "Failed to get packet for obu\n");
400   |  return ret < 0 ? ret : AVERROR_INVALIDDATA;
401   |     }
402   |  return 0;
403   | }
404   |  
405   | static int obu_read_packet(AVFormatContext *s, AVPacket *pkt)
406   | {
407   |  AV1DemuxContext *const c = s->priv_data;
408   |  int ret;
409   |  
410   |  if (s->io_repositioned) {
    1Assuming field 'io_repositioned' is 0→
    2←Taking false branch→
411   |         av_bsf_flush(c->bsf);
412   |         s->io_repositioned = 0;
413   |     }
414   |  while (1) {
    3←Loop condition is true.  Entering loop body→
415   |  ret = obu_get_packet(s, pkt);
    4←Calling 'obu_get_packet'→
416   |  /* In case of AVERROR_EOF we need to flush the BSF. Conveniently
417   |  * obu_get_packet() returns a blank pkt in this case which
418   |  * can be used to signal that the BSF should be flushed. */
419   |  if (ret < 0 && ret != AVERROR_EOF)
420   |  return ret;
421   |         ret = av_bsf_send_packet(c->bsf, pkt);
422   |  if (ret < 0) {
423   |             av_log(s, AV_LOG_ERROR, "Failed to send packet to "
424   |  "av1_frame_merge filter\n");
425   |  return ret;
426   |         }
427   |         ret = av_bsf_receive_packet(c->bsf, pkt);
428   |  if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
429   |             av_log(s, AV_LOG_ERROR, "av1_frame_merge filter failed to "
430   |  "send output packet\n");
431   |  if (ret != AVERROR(EAGAIN))
432   |  break;
433   |     }
434   |  
435   |  return ret;
436   | }
437   |  
438   | const FFInputFormat ff_obu_demuxer = {
439   |     .p.name         = "obu",
440   |     .p.long_name    = NULL_IF_CONFIG_SMALL("AV1 low overhead OBU"),
441   |     .p.extensions   = "obu",
442   |     .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK | AVFMT_NOTIMESTAMPS,
443   |     .p.priv_class   = &av1_demuxer_class,
444   |     .priv_data_size = sizeof(AV1DemuxContext),
445   |     .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
341   |  av_assert2(n>0 && n<=25);
342   |  UPDATE_CACHE(re, s);
343   |     tmp = SHOW_UBITS(re, s, n);
344   |  LAST_SKIP_BITS(re, s, n);
345   |  CLOSE_READER(re, s);
346   |  av_assert2(tmp < UINT64_C(1) << n);
347   |  return tmp;
348   | }
349   |  
350   | /**
351   |  * Read 0-25 bits.
352   |  */
353   | static av_always_inline int get_bitsz(GetBitContext *s, int n)
354   | {
355   |  return n ? get_bits(s, n) : 0;
356   | }
357   |  
358   | static inline unsigned int get_bits_le(GetBitContext *s, int n)
359   | {
360   |  register int tmp;
361   |  OPEN_READER(re, s);
362   |  av_assert2(n>0 && n<=25);
363   |  UPDATE_CACHE_LE(re, s);
364   |     tmp = SHOW_UBITS_LE(re, s, n);
365   |  LAST_SKIP_BITS(re, s, n);
366   |  CLOSE_READER(re, s);
367   |  return tmp;
368   | }
369   |  
370   | /**
371   |  * Show 1-25 bits.
372   |  */
373   | static inline unsigned int show_bits(GetBitContext *s, int n)
374   | {
375   |  register unsigned int tmp;
376   |  OPEN_READER_NOSIZE(re, s);
377   |  av_assert2(n>0 && n<=25);
378   |  UPDATE_CACHE(re, s);
379   |     tmp = SHOW_UBITS(re, s, n);
380   |  return tmp;
381   | }
382   |  
383   | static inline void skip_bits(GetBitContext *s, int n)
384   | {
385   |  OPEN_READER_NOSIZE_NOCACHE(re, s);
386   |  OPEN_READER_SIZE(re, s);
387   |  LAST_SKIP_BITS(re, s, n);
388   |  CLOSE_READER(re, s);
389   | }
390   |  
391   | static inline unsigned int get_bits1(GetBitContext *s)
392   | {
393   |  unsigned int index = s->index;
394   |  uint8_t result     = s->buffer[index >> 3];
    14←buffer read by avio_read may be partially uninitialized
395   | #ifdef BITSTREAM_READER_LE
396   |     result >>= index & 7;
397   |     result  &= 1;
398   | #else
399   |     result <<= index & 7;
400   |     result >>= 8 - 1;
401   | #endif
402   | #if !UNCHECKED_BITSTREAM_READER
403   |  if (s->index < s->size_in_bits_plus8)
404   | #endif
405   |         index++;
406   |     s->index = index;
407   |  
408   |  return result;
409   | }
410   |  
411   | static inline unsigned int show_bits1(GetBitContext *s)
412   | {
413   |  return show_bits(s, 1);
414   | }
415   |  
416   | static inline void skip_bits1(GetBitContext *s)
417   | {
418   |     skip_bits(s, 1);
419   | }
420   |  
421   | /**
422   |  * Read 0-32 bits.
423   |  */
424   | static inline unsigned int get_bits_long(GetBitContext *s, int n)
467   | #endif
468   |     }
469   | }
470   |  
471   | /**
472   |  * Read 0-32 bits as a signed integer.
473   |  */
474   | static inline int get_sbits_long(GetBitContext *s, int n)
475   | {
476   |  // sign_extend(x, 0) is undefined
477   |  if (!n)
478   |  return 0;
479   |  
480   |  return sign_extend(get_bits_long(s, n), n);
481   | }
482   |  
483   | /**
484   |  * Read 0-64 bits as a signed integer.
485   |  */
486   | static inline int64_t get_sbits64(GetBitContext *s, int n)
487   | {
488   |  // sign_extend(x, 0) is undefined
489   |  if (!n)
490   |  return 0;
491   |  
492   |  return sign_extend64(get_bits64(s, n), n);
493   | }
494   |  
495   | /**
496   |  * Show 0-32 bits.
497   |  */
498   | static inline unsigned int show_bits_long(GetBitContext *s, int n)
499   | {
500   |  if (n <= MIN_CACHE_BITS) {
501   |  return show_bits(s, n);
502   |     } else {
503   |         GetBitContext gb = *s;
504   |  return get_bits_long(&gb, n);
505   |     }
506   | }
507   |  
508   |  
509   | /**
510   |  * Initialize GetBitContext.
511   |  * @param buffer bitstream buffer, must be AV_INPUT_BUFFER_PADDING_SIZE bytes
512   |  *        larger than the actual read bits because some optimized bitstream
513   |  *        readers read 32 or 64 bit at once and could read over the end
514   |  * @param bit_size the size of the buffer in bits
515   |  * @return 0 on success, AVERROR_INVALIDDATA if the buffer_size would overflow.
516   |  */
517   | static inline int init_get_bits(GetBitContext *s, const uint8_t *buffer,
518   |  int bit_size)
519   | {
520   |  int ret = 0;
521   |  
522   |  if (bit_size >= INT_MAX - FFMAX(7, AV_INPUT_BUFFER_PADDING_SIZE*8) || bit_size < 0 || !buffer) {
523   |         bit_size    = 0;
524   |         buffer      = NULL;
525   |         ret         = AVERROR_INVALIDDATA;
526   |     }
527   |  
528   |     s->buffer             = buffer;
529   |     s->size_in_bits       = bit_size;
530   |     s->size_in_bits_plus8 = bit_size + 8;
531   |     s->index              = 0;
532   |  
533   |  return ret;
534   | }
535   |  
536   | /**
537   |  * Initialize GetBitContext.
538   |  * @param buffer bitstream buffer, must be AV_INPUT_BUFFER_PADDING_SIZE bytes
539   |  *        larger than the actual read bits because some optimized bitstream
540   |  *        readers read 32 or 64 bit at once and could read over the end
541   |  * @param byte_size the size of the buffer in bytes
542   |  * @return 0 on success, AVERROR_INVALIDDATA if the buffer_size would overflow.
543   |  */
544   | static inline int init_get_bits8(GetBitContext *s, const uint8_t *buffer,
545   |  int byte_size)
546   | {
547   |  if (byte_size > INT_MAX / 8 || byte_size < 0)
548   |         byte_size = -1;
549   |  return init_get_bits(s, buffer, byte_size * 8);
550   | }
551   |  
552   | static inline int init_get_bits8_le(GetBitContext *s, const uint8_t *buffer,
553   |  int byte_size)
554   | {
555   |  if (byte_size > INT_MAX / 8 || byte_size < 0)
556   |         byte_size = -1;
557   |  return init_get_bits(s, buffer, byte_size * 8);
558   | }
559   |  
560   | static inline const uint8_t *align_get_bits(GetBitContext *s)
561   | {
562   |  int n = -get_bits_count(s) & 7;
563   |  if (n)
564   |         skip_bits(s, n);
565   |  return s->buffer + (s->index >> 3);
566   | }
567   |  
568   | /**
569   |  * If the vlc code is invalid and max_depth=1, then no bits will be removed.
570   |  * If the vlc code is invalid and max_depth>1, then the number of bits removed
571   |  * is undefined.
572   |  */
573   | #define GET_VLC(code, name, gb, table, bits, max_depth)         \
574   |  do {                                                        \
575   |  unsigned idx_ = SHOW_UBITS(name, gb, bits);             \
576   |  code          = table[idx_].sym;                        \
577   |  int        n_ = table[idx_].len;                        \
578   |  \
579   |  if (max_depth > 1 && n_ < 0) {                          \

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
