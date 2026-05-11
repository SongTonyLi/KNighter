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

File:| /home/cc/ai2sec/KNighter/ffmpeg/./libavutil/uuid.h  
---|---  
Warning:| line 121, column 12  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


341   |  
342   |         av_dict_set_int(&opts, "ism_lookahead", c->lookahead_count, 0);
343   |         av_dict_set(&opts, "movflags", "+frag_custom", 0);
344   |         ret = avformat_write_header(ctx, &opts);
345   |         av_dict_free(&opts);
346   |  if (ret < 0) {
347   |  return ret;
348   |         }
349   |         avio_flush(ctx->pb);
350   |         s->streams[i]->time_base = st->time_base;
351   |  if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
352   |             c->has_video = 1;
353   |             os->stream_type_tag = "video";
354   |  if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
355   |                 os->fourcc = "H264";
356   |             } else if (st->codecpar->codec_id == AV_CODEC_ID_VC1) {
357   |                 os->fourcc = "WVC1";
358   |             } else {
359   |                 av_log(s, AV_LOG_ERROR, "Unsupported video codec\n");
360   |  return AVERROR(EINVAL);
361   |             }
362   |         } else {
363   |             c->has_audio = 1;
364   |             os->stream_type_tag = "audio";
365   |  if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
366   |                 os->fourcc = "AACL";
367   |                 os->audio_tag = 0xff;
368   |             } else if (st->codecpar->codec_id == AV_CODEC_ID_WMAPRO) {
369   |                 os->fourcc = "WMAP";
370   |                 os->audio_tag = 0x0162;
371   |             } else {
372   |                 av_log(s, AV_LOG_ERROR, "Unsupported audio codec\n");
373   |  return AVERROR(EINVAL);
374   |             }
375   |             os->packet_size = st->codecpar->block_align ? st->codecpar->block_align : 4;
376   |         }
377   |         get_private_data(os);
378   |     }
379   |  
380   |  if (!c->has_video && c->min_frag_duration <= 0) {
381   |         av_log(s, AV_LOG_WARNING, "no video stream and no min frag duration set\n");
382   |  return AVERROR(EINVAL);
383   |     }
384   |     ret = write_manifest(s, 0);
385   |  if (ret < 0)
386   |  return ret;
387   |  
388   |  return 0;
389   | }
390   |  
391   | static int parse_fragment(AVFormatContext *s, const char *filename, int64_t *start_ts, int64_t *duration, int64_t *moof_size, int64_t size)
392   | {
393   |  AVIOContext *in;
394   |  int ret;
395   |     uint32_t len;
396   |  if ((ret = s->io_open(s, &in, filename, AVIO_FLAG_READ, NULL)) < 0)
    12←Assuming the condition is false→
    13←Taking false branch→
397   |  return ret;
398   |  ret = AVERROR(EIO);
399   |     *moof_size = avio_rb32(in);
400   |  if (*moof_size < 8 || *moof_size > size)
    14←Assuming the condition is false→
    15←Assuming the condition is false→
    16←Taking false branch→
401   |  goto fail;
402   |  if (avio_rl32(in) != MKTAG('m','o','o','f'))
    17←Assuming the condition is false→
    18←Taking false branch→
403   |  goto fail;
404   |  len = avio_rb32(in);
405   |  if (len > *moof_size)
    19←Assuming the condition is false→
    20←Taking false branch→
406   |  goto fail;
407   |  if (avio_rl32(in) != MKTAG('m','f','h','d'))
    21←Assuming the condition is false→
    22←Taking false branch→
408   |  goto fail;
409   |  avio_seek(in, len - 8, SEEK_CUR);
410   |     avio_rb32(in); /* traf size */
411   |  if (avio_rl32(in) != MKTAG('t','r','a','f'))
    23←Assuming the condition is false→
    24←Taking false branch→
412   |  goto fail;
413   |  while (avio_tell(in) < *moof_size) {
    25←Assuming the condition is true→
    26←Loop condition is true.  Entering loop body→
414   |  uint32_t len = avio_rb32(in);
415   |         uint32_t tag = avio_rl32(in);
416   |         int64_t end = avio_tell(in) + len - 8;
417   |  if (len < 8 || len >= *moof_size)
    27←Assuming 'len' is >= 8→
    28←Assuming the condition is false→
    29←Taking false branch→
418   |  goto fail;
419   |  if (tag == MKTAG('u','u','i','d')) {
    30←Assuming the condition is true→
    31←Taking true branch→
420   |  static const AVUUID tfxd = {
421   |                 0x6d, 0x1d, 0x9b, 0x05, 0x42, 0xd5, 0x44, 0xe6,
422   |                 0x80, 0xe2, 0x14, 0x1d, 0xaf, 0xf7, 0x57, 0xb2
423   |             };
424   |  AVUUID uuid;
425   |             avio_read(in, uuid, 16);
426   |  if (av_uuid_equal(uuid, tfxd) && len >= 8 + 16 + 4 + 16) {
    32←Calling 'av_uuid_equal'→
427   |                 avio_seek(in, 4, SEEK_CUR);
428   |                 *start_ts = avio_rb64(in);
429   |                 *duration = avio_rb64(in);
430   |                 ret = 0;
431   |  break;
432   |             }
433   |         }
434   |         avio_seek(in, end, SEEK_SET);
435   |     }
436   | fail:
437   |     ff_format_io_close(s, &in);
438   |  return ret;
439   | }
440   |  
441   | static int add_fragment(OutputStream *os, const char *file, const char *infofile, int64_t start_time, int64_t duration, int64_t start_pos, int64_t size)
442   | {
443   |  int err;
444   |     Fragment *frag;
445   |  if (os->nb_fragments >= os->fragments_size) {
446   |         os->fragments_size = (os->fragments_size + 1) * 2;
447   |  if ((err = av_reallocp_array(&os->fragments, sizeof(*os->fragments),
448   |                                os->fragments_size)) < 0) {
449   |             os->fragments_size = 0;
450   |             os->nb_fragments = 0;
451   |  return err;
452   |         }
453   |     }
454   |     frag = av_mallocz(sizeof(*frag));
455   |  if (!frag)
456   |  return AVERROR(ENOMEM);
457   |     av_strlcpy(frag->file, file, sizeof(frag->file));
458   |     av_strlcpy(frag->infofile, infofile, sizeof(frag->infofile));
459   |     frag->start_time = start_time;
460   |     frag->duration = duration;
461   |     frag->start_pos = start_pos;
462   |     frag->size = size;
463   |     frag->n = os->fragment_index;
464   |     os->fragments[os->nb_fragments++] = frag;
465   |     os->fragment_index++;
466   |  return 0;
467   | }
468   |  
469   | static int copy_moof(AVFormatContext *s, const char* infile, const char *outfile, int64_t size)
470   | {
471   |     AVIOContext *in, *out;
472   |  int ret = 0;
473   |  if ((ret = s->io_open(s, &in, infile, AVIO_FLAG_READ, NULL)) < 0)
474   |  return ret;
475   |  if ((ret = s->io_open(s, &out, outfile, AVIO_FLAG_WRITE, NULL)) < 0) {
476   |         ff_format_io_close(s, &in);
477   |  return ret;
478   |     }
479   |  while (size > 0) {
480   |         uint8_t buf[8192];
481   |  int n = FFMIN(size, sizeof(buf));
482   |         n = avio_read(in, buf, n);
483   |  if (n <= 0) {
484   |             ret = AVERROR(EIO);
485   |  break;
486   |         }
487   |         avio_write(out, buf, n);
488   |         size -= n;
489   |     }
490   |     avio_flush(out);
491   |     ff_format_io_close(s, &out);
492   |     ff_format_io_close(s, &in);
493   |  return ret;
494   | }
495   |  
496   | static int ism_flush(AVFormatContext *s, int final)
497   | {
498   |  SmoothStreamingContext *c = s->priv_data;
499   |  int i, ret = 0;
500   |  
501   |  for (i = 0; i < s->nb_streams; i++) {
    2←Assuming 'i' is < field 'nb_streams'→
    3←Loop condition is true.  Entering loop body→
502   |  OutputStream *os = &c->streams[i];
503   |  char filename[1024], target_filename[1024], header_filename[1024], curr_dirname[1024];
504   |         int64_t size;
505   |         int64_t start_ts, duration, moof_size;
506   |  if (!os->packets_written)
    4←Assuming field 'packets_written' is not equal to 0→
    5←Taking false branch→
507   |  continue;
508   |  
509   |  snprintf(filename, sizeof(filename), "%s/temp", os->dirname);
510   |         ret = ffurl_open_whitelist(&os->out, filename, AVIO_FLAG_WRITE, &s->interrupt_callback, NULL, s->protocol_whitelist, s->protocol_blacklist, NULL);
511   |  if (ret < 0)
    6←Assuming 'ret' is >= 0→
    7←Taking false branch→
512   |  break;
513   |  os->cur_start_pos = os->tail_pos;
514   |         av_write_frame(os->ctx, NULL);
515   |         avio_flush(os->ctx->pb);
516   |         os->packets_written = 0;
517   |  if (!os->out || os->tail_out)
    8←Assuming field 'out' is non-null→
    9←Assuming field 'tail_out' is null→
    10←Taking false branch→
518   |  return AVERROR(EIO);
519   |  
520   |  ffurl_closep(&os->out);
521   |         size = os->tail_pos - os->cur_start_pos;
522   |  if ((ret = parse_fragment(s, filename, &start_ts, &duration, &moof_size, size)) < 0)
    11←Calling 'parse_fragment'→
523   |  break;
524   |  
525   |  if (!s->streams[i]->codecpar->bit_rate) {
526   |             int64_t bitrate = (int64_t) size * 8 * AV_TIME_BASE / av_rescale_q(duration, s->streams[i]->time_base, AV_TIME_BASE_Q);
527   |  if (!bitrate) {
528   |                 av_log(s, AV_LOG_ERROR, "calculating bitrate got zero.\n");
529   |                 ret = AVERROR(EINVAL);
530   |  return ret;
531   |             }
532   |  
533   |             av_log(s, AV_LOG_DEBUG, "calculated bitrate: %"PRId64"\n", bitrate);
534   |             s->streams[i]->codecpar->bit_rate = bitrate;
535   |             memcpy(curr_dirname, os->dirname, sizeof(os->dirname));
536   |             snprintf(os->dirname, sizeof(os->dirname), "%s/QualityLevels(%"PRId64")", s->url, s->streams[i]->codecpar->bit_rate);
537   |             snprintf(filename, sizeof(filename), "%s/temp", os->dirname);
538   |  
539   |  // rename the tmp folder back to the correct name since we now have the bitrate
540   |  if ((ret = ff_rename((const char*)curr_dirname,  os->dirname, s)) < 0)
541   |  return ret;
542   |         }
543   |  
544   |         snprintf(header_filename, sizeof(header_filename), "%s/FragmentInfo(%s=%"PRIu64")", os->dirname, os->stream_type_tag, start_ts);
545   |         snprintf(target_filename, sizeof(target_filename), "%s/Fragments(%s=%"PRIu64")", os->dirname, os->stream_type_tag, start_ts);
546   |         copy_moof(s, filename, header_filename, moof_size);
547   |         ret = ff_rename(filename, target_filename, s);
548   |  if (ret < 0)
549   |  break;
550   |         add_fragment(os, target_filename, header_filename, start_ts, duration,
551   |                      os->cur_start_pos, size);
552   |     }
556   |             OutputStream *os = &c->streams[i];
557   |  int j;
558   |  int remove = os->nb_fragments - c->window_size - c->extra_window_size - c->lookahead_count;
559   |  if (final && c->remove_at_exit)
560   |                 remove = os->nb_fragments;
561   |  if (remove > 0) {
562   |  for (j = 0; j < remove; j++) {
563   |                     unlink(os->fragments[j]->file);
564   |                     unlink(os->fragments[j]->infofile);
565   |                     av_freep(&os->fragments[j]);
566   |                 }
567   |                 os->nb_fragments -= remove;
568   |                 memmove(os->fragments, os->fragments + remove, os->nb_fragments * sizeof(*os->fragments));
569   |             }
570   |  if (final && c->remove_at_exit)
571   |                 rmdir(os->dirname);
572   |         }
573   |     }
574   |  
575   |  if (ret >= 0)
576   |         ret = write_manifest(s, final);
577   |  return ret;
578   | }
579   |  
580   | static int ism_write_packet(AVFormatContext *s, AVPacket *pkt)
581   | {
582   |     SmoothStreamingContext *c = s->priv_data;
583   |     AVStream *st = s->streams[pkt->stream_index];
584   |     FFStream *const sti = ffstream(st);
585   |     OutputStream *os = &c->streams[pkt->stream_index];
586   |     int64_t end_dts = (c->nb_fragments + 1) * (int64_t) c->min_frag_duration;
587   |  int ret;
588   |  
589   |  if (sti->first_dts == AV_NOPTS_VALUE)
590   |         sti->first_dts = pkt->dts;
591   |  
592   |  if ((!c->has_video || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
593   |         av_compare_ts(pkt->dts - sti->first_dts, st->time_base,
594   |                       end_dts, AV_TIME_BASE_Q) >= 0 &&
595   |         pkt->flags & AV_PKT_FLAG_KEY && os->packets_written) {
596   |  
597   |  if ((ret = ism_flush(s, 0)) < 0)
598   |  return ret;
599   |         c->nb_fragments++;
600   |     }
601   |  
602   |     os->packets_written++;
603   |  return ff_write_chained(os->ctx, 0, pkt, s, 0);
604   | }
605   |  
606   | static int ism_write_trailer(AVFormatContext *s)
607   | {
608   |  SmoothStreamingContext *c = s->priv_data;
609   |  ism_flush(s, 1);
    1Calling 'ism_flush'→
610   |  
611   |  if (c->remove_at_exit) {
612   |  char filename[1024];
613   |         snprintf(filename, sizeof(filename), "%s/Manifest", s->url);
614   |         unlink(filename);
615   |         rmdir(s->url);
616   |     }
617   |  
618   |  return 0;
619   | }
620   |  
621   | #define OFFSET(x) offsetof(SmoothStreamingContext, x)
622   | #define E AV_OPT_FLAG_ENCODING_PARAM
623   | static const AVOption options[] = {
624   |     { "window_size", "number of fragments kept in the manifest", OFFSET(window_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, E },
625   |     { "extra_window_size", "number of fragments kept outside of the manifest before removing from disk", OFFSET(extra_window_size), AV_OPT_TYPE_INT, { .i64 = 5 }, 0, INT_MAX, E },
626   |     { "lookahead_count", "number of lookahead fragments", OFFSET(lookahead_count), AV_OPT_TYPE_INT, { .i64 = 2 }, 0, INT_MAX, E },
627   |     { "min_frag_duration", "minimum fragment duration (in microseconds)", OFFSET(min_frag_duration), AV_OPT_TYPE_INT64, { .i64 = 5000000 }, 0, INT_MAX, E },
628   |     { "remove_at_exit", "remove all fragments when finished", OFFSET(remove_at_exit), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
629   |     { NULL },
630   | };
631   |  
632   | static const AVClass ism_class = {
633   |     .class_name = "smooth streaming muxer",
634   |     .item_name  = av_default_item_name,
635   |     .option     = options,
636   |     .version    = LIBAVUTIL_VERSION_INT,
637   | };
638   |  
639   |  
69    |  * @param[in]  in  String representation of a UUID,
70    |  *                 e.g. 2fceebd0-7017-433d-bafb-d073a7116696
71    |  * @param[out] uu  AVUUID
72    |  * @return         A non-zero value in case of an error.
73    |  */
74    | int av_uuid_parse(const char *in, AVUUID uu);
75    |  
76    | /**
77    |  * Parses a URN representation of a UUID, as specified at IETF RFC 4122,
78    |  * into an AVUUID. The parsing is case-insensitive. The string must be 46
79    |  * characters long, including the terminating NUL character.
80    |  *
81    |  * Example string representation: "urn:uuid:2fceebd0-7017-433d-bafb-d073a7116696"
82    |  *
83    |  * @param[in]  in  URN UUID
84    |  * @param[out] uu  AVUUID
85    |  * @return         A non-zero value in case of an error.
86    |  */
87    | int av_uuid_urn_parse(const char *in, AVUUID uu);
88    |  
89    | /**
90    |  * Parses a string representation of a UUID formatted according to IETF RFC 4122
91    |  * into an AVUUID. The parsing is case-insensitive.
92    |  *
93    |  * @param[in]  in_start Pointer to the first character of the string representation
94    |  * @param[in]  in_end   Pointer to the character after the last character of the
95    |  *                      string representation. That memory location is never
96    |  *                      accessed. It is an error if `in_end - in_start != 36`.
97    |  * @param[out] uu       AVUUID
98    |  * @return              A non-zero value in case of an error.
99    |  */
100   | int av_uuid_parse_range(const char *in_start, const char *in_end, AVUUID uu);
101   |  
102   | /**
103   |  * Serializes a AVUUID into a string representation according to IETF RFC 4122.
104   |  * The string is lowercase and always 37 characters long, including the
105   |  * terminating NUL character.
106   |  *
107   |  * @param[in]  uu  AVUUID
108   |  * @param[out] out Pointer to an array of no less than 37 characters.
109   |  */
110   | void av_uuid_unparse(const AVUUID uu, char *out);
111   |  
112   | /**
113   |  * Compares two UUIDs for equality.
114   |  *
115   |  * @param[in]  uu1  AVUUID
116   |  * @param[in]  uu2  AVUUID
117   |  * @return          Nonzero if uu1 and uu2 are identical, 0 otherwise
118   |  */
119   | static inline int av_uuid_equal(const AVUUID uu1, const AVUUID uu2)
120   | {
121   |  return memcmp(uu1, uu2, AV_UUID_LEN) == 0;
    33←buffer read by avio_read may be partially uninitialized
122   | }
123   |  
124   | /**
125   |  * Copies the bytes of src into dest.
126   |  *
127   |  * @param[out]  dest  AVUUID
128   |  * @param[in]   src   AVUUID
129   |  */
130   | static inline void av_uuid_copy(AVUUID dest, const AVUUID src)
131   | {
132   |     memcpy(dest, src, AV_UUID_LEN);
133   | }
134   |  
135   | /**
136   |  * Sets a UUID to the nil UUID, i.e. a UUID with have all
137   |  * its 128 bits set to zero.
138   |  *
139   |  * @param[in,out]  uu  UUID to be set to the nil UUID
140   |  */
141   | static inline void av_uuid_nil(AVUUID uu)
142   | {
143   |     memset(uu, 0, AV_UUID_LEN);
144   | }
145   |  
146   | #endif /* AVUTIL_UUID_H */

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
