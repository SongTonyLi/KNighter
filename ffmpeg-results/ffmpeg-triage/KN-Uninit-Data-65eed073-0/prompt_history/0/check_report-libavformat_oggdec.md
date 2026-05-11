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

File:| format/oggdec.c  
---|---  
Warning:| line 345, column 42  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


268   |  sizeof(*ogg->streams))))
269   |  return AVERROR(ENOMEM);
270   |     ogg->streams = os;
271   |     os           = ogg->streams + idx;
272   |     memset(os, 0, sizeof(*os));
273   |     os->serial        = serial;
274   |     os->bufsize       = DECODER_BUFFER_SIZE;
275   |     os->buf           = av_malloc(os->bufsize + AV_INPUT_BUFFER_PADDING_SIZE);
276   |     os->header        = -1;
277   |     os->start_granule = OGG_NOGRANULE_VALUE;
278   |  if (!os->buf)
279   |  return AVERROR(ENOMEM);
280   |  
281   |  /* Create the associated AVStream */
282   |     st = avformat_new_stream(s, NULL);
283   |  if (!st) {
284   |         av_freep(&os->buf);
285   |  return AVERROR(ENOMEM);
286   |     }
287   |     st->id = idx;
288   |     avpriv_set_pts_info(st, 64, 1, 1000000);
289   |  
290   |     ogg->nstreams++;
291   |  return idx;
292   | }
293   |  
294   | static int data_packets_seen(const struct ogg *ogg)
295   | {
296   |  int i;
297   |  
298   |  for (i = 0; i < ogg->nstreams; i++)
299   |  if (ogg->streams[i].got_data)
300   |  return 1;
301   |  return 0;
302   | }
303   |  
304   | static int buf_realloc(struct ogg_stream *os, int size)
305   | {
306   |  /* Even if invalid guarantee there's enough memory to read the page */
307   |  if (os->bufsize - os->bufpos < size) {
308   |         uint8_t *nb = av_realloc(os->buf, 2*os->bufsize + AV_INPUT_BUFFER_PADDING_SIZE);
309   |  if (!nb)
310   |  return AVERROR(ENOMEM);
311   |         os->buf = nb;
312   |         os->bufsize *= 2;
313   |     }
314   |  
315   |  return 0;
316   | }
317   |  
318   | static int ogg_read_page(AVFormatContext *s, int *sid, int probing)
319   | {
320   |  AVIOContext *bc = s->pb;
321   |  struct ogg *ogg = s->priv_data;
322   |  struct ogg_stream *os;
323   |  int ret, i = 0;
324   |  int flags, nsegs;
325   |     uint64_t gp;
326   |     uint32_t serial;
327   |     uint32_t crc, crc_tmp;
328   |  int size = 0, idx;
329   |     int64_t version, page_pos;
330   |     int64_t start_pos;
331   |     uint8_t sync[4];
332   |     uint8_t segments[255];
333   |     uint8_t *readout_buf;
334   |  int sp = 0;
335   |  
336   |     ret = avio_read(bc, sync, 4);
337   |  if (ret < 4)
    7←Assuming 'ret' is >= 4→
    8←Taking false branch→
338   |  return ret < 0 ? ret : AVERROR_EOF;
339   |  
340   |  do {
    13←Loop condition is true. Execution continues on line 341→
341   |  int c;
342   |  
343   |  if (sync[sp & 3] == 'O' &&
    9←Assuming the condition is false→
    14←Assuming the condition is true→
344   |  sync[(sp + 1) & 3] == 'g' &&
    15←Assuming the condition is true→
345   |  sync[(sp + 2) & 3] == 'g' && sync[(sp + 3) & 3] == 'S')
    16←Assuming the condition is true→
    17←buffer read by avio_read may be partially uninitialized
346   |  break;
347   |  
348   |  if(!i9.1'i' is 0 && (bc->seekable & AVIO_SEEKABLE_NORMAL) && ogg->page_pos > 0) {
    10←Assuming the condition is false→
349   |             memset(sync, 0, 4);
350   |             avio_seek(bc, ogg->page_pos+4, SEEK_SET);
351   |             ogg->page_pos = -1;
352   |         }
353   |  
354   |  c = avio_r8(bc);
355   |  
356   |  if (avio_feof(bc))
    11←Assuming the condition is false→
    12←Taking false branch→
357   |  return AVERROR_EOF;
358   |  
359   |  sync[sp++ & 3] = c;
360   |     } while (i++ < MAX_PAGE_SIZE);
361   |  
362   |  if (i >= MAX_PAGE_SIZE) {
363   |         av_log(s, AV_LOG_INFO, "cannot find sync word\n");
364   |  return AVERROR_INVALIDDATA;
365   |     }
366   |  
367   |  /* 0x4fa9b05f = av_crc(AV_CRC_32_IEEE, 0x0, "OggS", 4) */
368   |     ffio_init_checksum(bc, ff_crc04C11DB7_update, 0x4fa9b05f);
369   |  
370   |  /* To rewind if checksum is bad/check magic on switches - this is the max packet size */
371   |     ret = ffio_ensure_seekback(bc, MAX_PAGE_SIZE);
372   |  if (ret < 0)
373   |  return ret;
374   |     start_pos = avio_tell(bc);
375   |  
376   |     version = avio_r8(bc);
377   |     flags   = avio_r8(bc);
378   |     gp      = avio_rl64(bc);
379   |     serial  = avio_rl32(bc);
380   |     avio_rl32(bc); /* seq */
381   |  
382   |     crc_tmp = ffio_get_checksum(bc);
383   |     crc     = avio_rb32(bc);
384   |     crc_tmp = ff_crc04C11DB7_update(crc_tmp, (uint8_t[4]){0}, 4);
385   |     ffio_init_checksum(bc, ff_crc04C11DB7_update, crc_tmp);
386   |  
387   |     nsegs    = avio_r8(bc);
388   |     page_pos = avio_tell(bc) - 27;
389   |  
390   |     ret = avio_read(bc, segments, nsegs);
454   |             av_free(readout_buf);
455   |  return ret;
456   |         }
457   |  
458   |         memcpy(os->buf + os->bufpos, readout_buf, size);
459   |         av_free(readout_buf);
460   |     }
461   |  
462   |     ogg->page_pos = page_pos;
463   |     os->page_pos  = page_pos;
464   |     os->nsegs     = nsegs;
465   |     os->segp      = 0;
466   |     os->got_data  = !(flags & OGG_FLAG_BOS);
467   |     os->bufpos   += size;
468   |     os->granule   = gp;
469   |     os->flags     = flags;
470   |     memcpy(os->segments, segments, nsegs);
471   |     memset(os->buf + os->bufpos, 0, AV_INPUT_BUFFER_PADDING_SIZE);
472   |  
473   |  if (flags & OGG_FLAG_CONT || os->incomplete) {
474   |  if (!os->psize) {
475   |  // If this is the very first segment we started
476   |  // playback in the middle of a continuation packet.
477   |  // Discard it since we missed the start of it.
478   |  while (os->segp < os->nsegs) {
479   |  int seg = os->segments[os->segp++];
480   |                 os->pstart += seg;
481   |  if (seg < 255)
482   |  break;
483   |             }
484   |             os->sync_pos = os->page_pos;
485   |         }
486   |     } else {
487   |         os->psize    = 0;
488   |         os->sync_pos = os->page_pos;
489   |     }
490   |  
491   |  /* This function is always called with sid != NULL */
492   |     *sid = idx;
493   |  
494   |  return 0;
495   | }
496   |  
497   | /**
498   |  * @brief find the next Ogg packet
499   |  * @param *sid is set to the stream for the packet or -1 if there is
500   |  *             no matching stream, in that case assume all other return
501   |  *             values to be uninitialized.
502   |  * @return negative value on error or EOF.
503   |  */
504   | static int ogg_packet(AVFormatContext *s, int *sid, int *dstart, int *dsize,
505   |                       int64_t *fpos)
506   | {
507   |  FFFormatContext *const si = ffformatcontext(s);
508   |  struct ogg *ogg = s->priv_data;
509   |  int idx, i, ret;
510   |  struct ogg_stream *os;
511   |  int complete = 0;
512   |  int segp     = 0, psize = 0;
513   |  
514   |     av_log(s, AV_LOG_TRACE, "ogg_packet: curidx=%i\n", ogg->curidx);
515   |  if (sid2.1'sid' is non-null)
    3←Taking true branch→
516   |  *sid = -1;
517   |  
518   |  do {
519   |  idx = ogg->curidx;
520   |  
521   |  while (idx < 0) {
    4←Assuming 'idx' is < 0→
    5←Loop condition is true.  Entering loop body→
522   |  ret = ogg_read_page(s, &idx, 0);
    6←Calling 'ogg_read_page'→
523   |  if (ret < 0)
524   |  return ret;
525   |         }
526   |  
527   |         os = ogg->streams + idx;
528   |  
529   |         av_log(s, AV_LOG_TRACE, "ogg_packet: idx=%d pstart=%d psize=%d segp=%d nsegs=%d\n",
530   |                 idx, os->pstart, os->psize, os->segp, os->nsegs);
531   |  
532   |  if (!os->codec) {
533   |  if (os->header < 0) {
534   |                 os->codec = ogg_find_codec(os->buf, os->bufpos);
535   |  if (!os->codec) {
536   |                     av_log(s, AV_LOG_WARNING, "Codec not found\n");
537   |                     os->header = 0;
538   |  return 0;
539   |                 }
540   |             } else {
541   |  return 0;
542   |             }
543   |         }
544   |  
545   |         segp  = os->segp;
546   |         psize = os->psize;
547   |  
548   |  while (os->segp < os->nsegs) {
549   |  int ss = os->segments[os->segp++];
550   |             os->psize += ss;
551   |  if (ss < 255) {
552   |                 complete = 1;
867   |  return ret;
868   |     pkt->stream_index = idx;
869   |     memcpy(pkt->data, os->buf + pstart, psize);
870   |  
871   |     pkt->pts      = pts;
872   |     pkt->dts      = dts;
873   |     pkt->flags    = os->pflags;
874   |     pkt->duration = os->pduration;
875   |     pkt->pos      = fpos;
876   |  
877   |  if (os->start_trimming || os->end_trimming) {
878   |         uint8_t *side_data = av_packet_new_side_data(pkt,
879   |                                                      AV_PKT_DATA_SKIP_SAMPLES,
880   |                                                      10);
881   |  if(!side_data)
882   |  return AVERROR(ENOMEM);
883   |  AV_WL32(side_data + 0, os->start_trimming);
884   |  AV_WL32(side_data + 4, os->end_trimming);
885   |         os->start_trimming = 0;
886   |         os->end_trimming = 0;
887   |     }
888   |  
889   |  if (os->replace) {
890   |         os->replace = 0;
891   |         pkt->dts = pkt->pts = AV_NOPTS_VALUE;
892   |     }
893   |  
894   |  if (os->new_metadata) {
895   |         ret = av_packet_add_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA,
896   |                                       os->new_metadata, os->new_metadata_size);
897   |  if (ret < 0)
898   |  return ret;
899   |  
900   |         os->new_metadata      = NULL;
901   |         os->new_metadata_size = 0;
902   |     }
903   |  
904   |  if (os->new_extradata) {
905   |         ret = av_packet_add_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
906   |                                       os->new_extradata, os->new_extradata_size);
907   |  if (ret < 0)
908   |  return ret;
909   |  
910   |         os->new_extradata      = NULL;
911   |         os->new_extradata_size = 0;
912   |     }
913   |  
914   |  return psize;
915   | }
916   |  
917   | static int64_t ogg_read_timestamp(AVFormatContext *s, int stream_index,
918   |                                   int64_t *pos_arg, int64_t pos_limit)
919   | {
920   |  struct ogg *ogg = s->priv_data;
921   |     AVIOContext *bc = s->pb;
922   |     int64_t pts     = AV_NOPTS_VALUE;
923   |     int64_t keypos  = -1;
924   |  int i;
925   |  int pstart, psize;
926   |     avio_seek(bc, *pos_arg, SEEK_SET);
927   |     ogg_reset(s);
928   |  
929   |  while (   avio_tell(bc) <= pos_limit
    1Assuming the condition is true→
930   |            && !ogg_packet(s, &i, &pstart, &psize, pos_arg)) {
    2←Calling 'ogg_packet'→
931   |  if (i == stream_index) {
932   |  struct ogg_stream *os = ogg->streams + stream_index;
933   |  // Do not trust the last timestamps of an ogm video
934   |  if (    (os->flags & OGG_FLAG_EOS)
935   |                 && !(os->flags & OGG_FLAG_BOS)
936   |                 && os->codec == &ff_ogm_video_codec)
937   |  continue;
938   |             pts = ogg_calc_pts(s, i, NULL);
939   |             ogg_validate_keyframe(s, i, pstart, psize);
940   |  if (os->pflags & AV_PKT_FLAG_KEY) {
941   |                 keypos = *pos_arg;
942   |             } else if (os->keyframe_seek) {
943   |  // if we had a previous keyframe but no pts for it,
944   |  // return that keyframe with this pts value.
945   |  if (keypos >= 0)
946   |                     *pos_arg = keypos;
947   |  else
948   |                     pts = AV_NOPTS_VALUE;
949   |             }
950   |         }
951   |  if (pts != AV_NOPTS_VALUE)
952   |  break;
953   |     }
954   |     ogg_reset(s);
955   |  return pts;
956   | }
957   |  
958   | static int ogg_read_seek(AVFormatContext *s, int stream_index,
959   |                          int64_t timestamp, int flags)
960   | {

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
