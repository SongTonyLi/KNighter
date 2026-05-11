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

File:| format/wavdec.c  
---|---  
Warning:| line 701, column 14  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


641   |  * FIXME: Come up with cleaner, more general solution */
642   |  if (st->codecpar->codec_id == AV_CODEC_ID_G729 && sample_count && (data_size << 3) > sample_count) {
643   |         av_log(s, AV_LOG_WARNING, "ignoring wrong sample_count %"PRId64"\n", sample_count);
644   |         sample_count = 0;
645   |     }
646   |  
647   |  if (!sample_count || av_get_exact_bits_per_sample(st->codecpar->codec_id) > 0)
648   |  if (   st->codecpar->ch_layout.nb_channels
649   |             && data_size
650   |             && av_get_bits_per_sample(st->codecpar->codec_id)
651   |             && wav->data_end <= avio_size(pb))
652   |             sample_count = (data_size << 3)
653   |                                   /
654   |                 (st->codecpar->ch_layout.nb_channels * (uint64_t)av_get_bits_per_sample(st->codecpar->codec_id));
655   |  
656   |  if (sample_count)
657   |         st->duration = sample_count;
658   |  
659   |  if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S32LE &&
660   |         st->codecpar->block_align == st->codecpar->ch_layout.nb_channels * 4 &&
661   |         st->codecpar->bits_per_coded_sample == 32 &&
662   |         st->codecpar->extradata_size == 2 &&
663   |  AV_RL16(st->codecpar->extradata) == 1) {
664   |         st->codecpar->codec_id = AV_CODEC_ID_PCM_F16LE;
665   |         st->codecpar->bits_per_coded_sample = 16;
666   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S24LE &&
667   |                st->codecpar->block_align == st->codecpar->ch_layout.nb_channels * 4 &&
668   |                st->codecpar->bits_per_coded_sample == 24) {
669   |         st->codecpar->codec_id = AV_CODEC_ID_PCM_F24LE;
670   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_XMA1 ||
671   |                st->codecpar->codec_id == AV_CODEC_ID_XMA2) {
672   |         st->codecpar->block_align = 2048;
673   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_ADPCM_MS && st->codecpar->ch_layout.nb_channels > 2 &&
674   |                st->codecpar->block_align < INT_MAX / st->codecpar->ch_layout.nb_channels) {
675   |         st->codecpar->block_align *= st->codecpar->ch_layout.nb_channels;
676   |     }
677   |  
678   |     ff_metadata_conv_ctx(s, NULL, wav_metadata_conv);
679   |     ff_metadata_conv_ctx(s, NULL, ff_riff_info_conv);
680   |  
681   |     set_spdif(s, wav);
682   |     set_max_size(st, wav);
683   |  
684   |  return 0;
685   | }
686   |  
687   | /**
688   |  * Find chunk with w64 GUID by skipping over other chunks.
689   |  * @return the size of the found chunk
690   |  */
691   | static int64_t find_guid(AVIOContext *pb, const uint8_t guid1[16])
692   | {
693   |  uint8_t guid[16];
694   |     int64_t size;
695   |  
696   |  while (!avio_feof(pb)) {
    11←Assuming the condition is true→
    12←Loop condition is true.  Entering loop body→
697   |  avio_read(pb, guid, 16);
698   |         size = avio_rl64(pb);
699   |  if (size <= 24 || size > INT64_MAX - 8)
    13←Assuming 'size' is > 24→
    14←Assuming the condition is false→
    15←Taking false branch→
700   |  return AVERROR_INVALIDDATA;
701   |  if (!memcmp(guid, guid1, 16))
    16←buffer read by avio_read may be partially uninitialized
702   |  return size;
703   |         avio_skip(pb, FFALIGN(size, INT64_C(8)) - 24);
704   |     }
705   |  return AVERROR_EOF;
706   | }
707   |  
708   | static int wav_read_packet(AVFormatContext *s, AVPacket *pkt)
709   | {
710   |  int ret, size;
711   |     int64_t left;
712   |     WAVDemuxContext *wav = s->priv_data;
713   |     AVStream *st = s->streams[0];
714   |  
715   |  if (CONFIG_SPDIF_DEMUXER && wav->spdif == 1)
    1Assuming field 'spdif' is not equal to 1→
    2←Taking false branch→
716   |  return ff_spdif_read_packet(s, pkt);
717   |  
718   |  if (wav->smv_data_ofs > 0) {
    3←Assuming field 'smv_data_ofs' is <= 0→
    4←Taking false branch→
719   |         int64_t audio_dts, video_dts;
720   |         AVStream *vst = wav->vst;
721   | smv_retry:
722   |         audio_dts = (int32_t)ffstream( st)->cur_dts;
723   |         video_dts = (int32_t)ffstream(vst)->cur_dts;
724   |  
725   |  if (audio_dts != AV_NOPTS_VALUE && video_dts != AV_NOPTS_VALUE) {
726   |  /*We always return a video frame first to get the pixel format first*/
727   |             wav->smv_last_stream = wav->smv_given_first ?
728   |                 av_compare_ts(video_dts, vst->time_base,
729   |                               audio_dts,  st->time_base) > 0 : 0;
730   |             wav->smv_given_first = 1;
731   |         }
732   |         wav->smv_last_stream = !wav->smv_last_stream;
733   |         wav->smv_last_stream |= wav->audio_eof;
734   |         wav->smv_last_stream &= !wav->smv_eof;
735   |  if (wav->smv_last_stream) {
736   |             uint64_t old_pos = avio_tell(s->pb);
737   |             uint64_t new_pos = wav->smv_data_ofs +
738   |                 wav->smv_block * (int64_t)wav->smv_block_size;
739   |  if (avio_seek(s->pb, new_pos, SEEK_SET) < 0) {
740   |                 ret = AVERROR_EOF;
741   |  goto smv_out;
742   |             }
743   |             size = avio_rl24(s->pb);
744   |  if (size > wav->smv_block_size) {
745   |                 ret = AVERROR_EOF;
746   |  goto smv_out;
747   |             }
748   |             ret  = av_get_packet(s->pb, pkt, size);
749   |  if (ret < 0)
750   |  goto smv_out;
751   |             pkt->pos -= 3;
752   |             pkt->pts = wav->smv_block * wav->smv_frames_per_jpeg;
753   |             pkt->duration = wav->smv_frames_per_jpeg;
754   |             wav->smv_block++;
755   |  
756   |             pkt->stream_index = vst->index;
757   | smv_out:
758   |             avio_seek(s->pb, old_pos, SEEK_SET);
759   |  if (ret == AVERROR_EOF) {
760   |                 wav->smv_eof = 1;
761   |  goto smv_retry;
762   |             }
763   |  return ret;
764   |         }
765   |     }
766   |  
767   |  left = wav->data_end - avio_tell(s->pb);
768   |  if (wav->ignore_length)
    5←Assuming field 'ignore_length' is 0→
    6←Taking false branch→
769   |         left = INT_MAX;
770   |  if (left <= 0) {
    7←Assuming 'left' is <= 0→
771   |  if (CONFIG_W64_DEMUXER && wav->w64)
    8←Assuming field 'w64' is not equal to 0→
    9←Taking true branch→
772   |  left = find_guid(s->pb, ff_w64_guid_data) - 24;
    10←Calling 'find_guid'→
773   |  else
774   |             left = find_tag(wav, s->pb, MKTAG('d', 'a', 't', 'a'));
775   |  if (left < 0) {
776   |             wav->audio_eof = 1;
777   |  if (wav->smv_data_ofs > 0 && !wav->smv_eof)
778   |  goto smv_retry;
779   |  return AVERROR_EOF;
780   |         }
781   |  if (INT64_MAX - left < avio_tell(s->pb))
782   |  return AVERROR_INVALIDDATA;
783   |         wav->data_end = avio_tell(s->pb) + left;
784   |     }
785   |  
786   |     size = wav->max_size;
787   |  if (st->codecpar->block_align > 1) {
788   |  if (size < st->codecpar->block_align)
789   |             size = st->codecpar->block_align;
790   |         size = (size / st->codecpar->block_align) * st->codecpar->block_align;
791   |     }
792   |     size = FFMIN(size, left);
793   |     ret  = av_get_packet(s->pb, pkt, size);
794   |  if (ret < 0)
795   |  return ret;
796   |     pkt->stream_index = 0;
797   |  
798   |  return ret;
799   | }
800   |  
801   | static int wav_read_seek(AVFormatContext *s,
802   |  int stream_index, int64_t timestamp, int flags)

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
