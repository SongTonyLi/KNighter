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

File:| format/ty.c  
---|---  
Warning:| line 365, column 57  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


286   |  
287   |     ty->first_audio_pts = AV_NOPTS_VALUE;
288   |     ty->last_audio_pts = AV_NOPTS_VALUE;
289   |     ty->last_video_pts = AV_NOPTS_VALUE;
290   |  
291   |  for (i = 0; i < CHUNK_PEEK_COUNT; i++) {
292   |         avio_read(pb, ty->chunk, CHUNK_SIZE);
293   |  
294   |         ret = analyze_chunk(s, ty->chunk);
295   |  if (ret < 0)
296   |  return ret;
297   |  if (ty->tivo_series != TIVO_SERIES_UNKNOWN &&
298   |             ty->audio_type  != TIVO_AUDIO_UNKNOWN &&
299   |             ty->tivo_type   != TIVO_TYPE_UNKNOWN)
300   |  break;
301   |     }
302   |  
303   |  if (ty->tivo_series == TIVO_SERIES_UNKNOWN ||
304   |         ty->audio_type == TIVO_AUDIO_UNKNOWN ||
305   |         ty->tivo_type == TIVO_TYPE_UNKNOWN)
306   |  return AVERROR_INVALIDDATA;
307   |  
308   |     st = avformat_new_stream(s, NULL);
309   |  if (!st)
310   |  return AVERROR(ENOMEM);
311   |     st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
312   |     st->codecpar->codec_id   = AV_CODEC_ID_MPEG2VIDEO;
313   |     ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
314   |     avpriv_set_pts_info(st, 64, 1, 90000);
315   |  
316   |     ast = avformat_new_stream(s, NULL);
317   |  if (!ast)
318   |  return AVERROR(ENOMEM);
319   |     ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
320   |  
321   |  if (ty->audio_type == TIVO_AUDIO_MPEG) {
322   |         ast->codecpar->codec_id = AV_CODEC_ID_MP2;
323   |         ffstream(ast)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
324   |     } else {
325   |         ast->codecpar->codec_id = AV_CODEC_ID_AC3;
326   |     }
327   |     avpriv_set_pts_info(ast, 64, 1, 90000);
328   |  
329   |     ty->first_chunk = 1;
330   |  
331   |     avio_seek(pb, 0, SEEK_SET);
332   |  
333   |  return 0;
334   | }
335   |  
336   | static int get_chunk(AVFormatContext *s)
337   | {
338   |  TYDemuxContext *ty = s->priv_data;
339   |     AVIOContext *pb = s->pb;
340   |  int read_size, num_recs;
341   |  
342   |  ff_dlog(s, "parsing ty chunk #%d\n", ty->cur_chunk);
    5←Taking false branch→
    6←Loop condition is false.  Exiting loop→
343   |  
344   |  /* if we have left-over filler space from the last chunk, get that */
345   |  if (avio_feof(pb))
    7←Assuming the condition is false→
    8←Taking false branch→
346   |  return AVERROR_EOF;
347   |  
348   |  /* read the TY packet header */
349   |  read_size = avio_read(pb, ty->chunk, CHUNK_SIZE);
350   |     ty->cur_chunk++;
351   |  
352   |  if ((read_size < 4) || (AV_RB32(ty->chunk) == 0)) {
    9←Assuming 'read_size' is >= 4→
    10←Assuming the condition is false→
    11←Taking false branch→
353   |  return AVERROR_EOF;
354   |     }
355   |  
356   |  /* check if it's a PART Header */
357   |  if (AV_RB32(ty->chunk) == TIVO_PES_FILEID) {
    12←Assuming the condition is false→
    13←Taking false branch→
358   |  /* skip master chunk and read new chunk */
359   |  return get_chunk(s);
360   |     }
361   |  
362   |  /* number of records in chunk (8- or 16-bit number) */
363   |  if (ty->chunk[3] & 0x80) {
    14←Assuming the condition is true→
    15←Taking true branch→
364   |  /* 16 bit rec cnt */
365   |  ty->num_recs = num_recs = (ty->chunk[1] << 8) + ty->chunk[0];
    16←buffer read by avio_read may be partially uninitialized
366   |     } else {
367   |  /* 8 bit reclen - TiVo 1.3 format */
368   |         ty->num_recs = num_recs = ty->chunk[0];
369   |     }
370   |     ty->cur_rec = 0;
371   |     ty->first_chunk = 0;
372   |  
373   |  ff_dlog(s, "chunk has %d records\n", num_recs);
374   |     ty->cur_chunk_pos = 4;
375   |  
376   |     av_freep(&ty->rec_hdrs);
377   |  
378   |  if (num_recs * 16 >= CHUNK_SIZE - 4)
379   |  return AVERROR_INVALIDDATA;
380   |  
381   |     ty->rec_hdrs = parse_chunk_headers(ty->chunk + 4, num_recs);
382   |  if (!ty->rec_hdrs)
383   |  return AVERROR(ENOMEM);
384   |     ty->cur_chunk_pos += 16 * num_recs;
385   |  
386   |  return 0;
387   | }
388   |  
389   | static int demux_video(AVFormatContext *s, TyRecHdr *rec_hdr, AVPacket *pkt)
390   | {
391   |     TYDemuxContext *ty = s->priv_data;
392   |  const int subrec_type = rec_hdr->subrec_type;
393   |  const int64_t rec_size = rec_hdr->rec_size;
394   |  int es_offset1, ret;
395   |  int got_packet = 0;
605   |  if (check_sync_pes(s, pkt, es_offset1, rec_size) == -1) {
606   |  /* partial PES header found, nothing else.
607   |  * we're done. */
608   |             av_packet_unref(pkt);
609   |  return 0;
610   |         }
611   |     } else if (subrec_type == 0x04) {
612   |  /* SA Audio with no PES Header                      */
613   |  /* ================================================ */
614   |  if ((ret = av_new_packet(pkt, rec_size)) < 0)
615   |  return ret;
616   |         memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
617   |         ty->cur_chunk_pos += rec_size;
618   |         pkt->stream_index = 1;
619   |         pkt->pts = ty->last_audio_pts;
620   |     } else if (subrec_type == 0x09) {
621   |  if ((ret = av_new_packet(pkt, rec_size)) < 0)
622   |  return ret;
623   |         memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
624   |         ty->cur_chunk_pos += rec_size ;
625   |         pkt->stream_index = 1;
626   |  
627   |  /* DTiVo AC3 Audio Data with PES Header             */
628   |  /* ================================================ */
629   |         es_offset1 = find_es_header(ty_AC3AudioPacket, pkt->data, 5);
630   |  
631   |  /* Check for complete PES */
632   |  if (check_sync_pes(s, pkt, es_offset1, rec_size) == -1) {
633   |  /* partial PES header found, nothing else.  we're done. */
634   |             av_packet_unref(pkt);
635   |  return 0;
636   |         }
637   |  /* S2 DTivo has invalid long AC3 packets */
638   |  if (ty->tivo_series == TIVO_SERIES2) {
639   |  if (pkt->size > AC3_PKT_LENGTH) {
640   |                 pkt->size -= 2;
641   |                 ty->ac3_pkt_size = 0;
642   |             } else {
643   |                 ty->ac3_pkt_size = pkt->size;
644   |             }
645   |         }
646   |     } else {
647   |  /* Unsupported/Unknown */
648   |         ty->cur_chunk_pos += rec_size;
649   |  return 0;
650   |     }
651   |  
652   |  return 1;
653   | }
654   |  
655   | static int ty_read_packet(AVFormatContext *s, AVPacket *pkt)
656   | {
657   |  TYDemuxContext *ty = s->priv_data;
658   |     AVIOContext *pb = s->pb;
659   |     TyRecHdr *rec;
660   |     int64_t rec_size = 0;
661   |  int ret = 0;
662   |  
663   |  if (avio_feof(pb))
    1Assuming the condition is false→
    2←Taking false branch→
664   |  return AVERROR_EOF;
665   |  
666   |  while (ret <= 0) {
667   |  if (!ty->rec_hdrs || ty->first_chunk || ty->cur_rec >= ty->num_recs) {
    3←Assuming field 'rec_hdrs' is null→
668   |  if (get_chunk(s) < 0 || ty->num_recs <= 0)
    4←Calling 'get_chunk'→
669   |  return AVERROR_EOF;
670   |         }
671   |  
672   |         rec = &ty->rec_hdrs[ty->cur_rec];
673   |         rec_size = rec->rec_size;
674   |         ty->cur_rec++;
675   |  
676   |  if (rec_size <= 0)
677   |  continue;
678   |  
679   |  if (ty->cur_chunk_pos + rec->rec_size > CHUNK_SIZE)
680   |  return AVERROR_INVALIDDATA;
681   |  
682   |  if (avio_feof(pb))
683   |  return AVERROR_EOF;
684   |  
685   |  switch (rec->rec_type) {
686   |  case VIDEO_ID:
687   |             ret = demux_video(s, rec, pkt);
688   |  break;
689   |  case AUDIO_ID:
690   |             ret = demux_audio(s, rec, pkt);
691   |  break;
692   |  default:
693   |  ff_dlog(s, "Invalid record type 0x%02x\n", rec->rec_type);
694   |  case 0x01:
695   |  case 0x02:
696   |  case 0x03: /* TiVo data services */
697   |  case 0x05: /* unknown, but seen regularly */
698   |             ty->cur_chunk_pos += rec->rec_size;

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
