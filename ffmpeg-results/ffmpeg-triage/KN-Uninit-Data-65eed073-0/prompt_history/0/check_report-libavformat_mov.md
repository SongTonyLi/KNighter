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

File:| format/mov.c  
---|---  
Warning:| line 10559, column 14  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


144   |     c->fc->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
145   |     av_dict_set(&c->fc->metadata, key, ff_id3v1_genre_str[genre-1], 0);
146   |  
147   |  return 0;
148   | }
149   |  
150   | static const uint32_t mac_to_unicode[128] = {
151   |     0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
152   |     0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
153   |     0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
154   |     0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
155   |     0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
156   |     0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
157   |     0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
158   |     0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
159   |     0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
160   |     0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
161   |     0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
162   |     0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
163   |     0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
164   |     0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
165   |     0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
166   |     0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7,
167   | };
168   |  
169   | static int mov_read_mac_string(MOVContext *c, AVIOContext *pb, int len,
170   |  char *dst, int dstlen)
171   | {
172   |  char *p = dst;
173   |  char *end = dst+dstlen-1;
174   |  int i;
175   |  
176   |  for (i = 0; i < len; i++) {
177   |         uint8_t t, c = avio_r8(pb);
178   |  
179   |  if (p >= end)
180   |  continue;
181   |  
182   |  if (c < 0x80)
183   |             *p++ = c;
184   |  else if (p < end)
185   |  PUT_UTF8(mac_to_unicode[c-0x80], t, if (p < end) *p++ = t;);
186   |     }
187   |     *p = 0;
188   |  return p - dst;
189   | }
190   |  
191   | /**
192   |  * Get the requested item.
193   |  */
194   | static HEIFItem *get_heif_item(MOVContext *c, unsigned id)
195   | {
196   |     HEIFItem *item = NULL;
197   |  
198   |  for (int i = 0; i < c->nb_heif_item; i++) {
199   |  if (!c->heif_item[i] || c->heif_item[i]->item_id != id)
200   |  continue;
201   |  
202   |         item = c->heif_item[i];
203   |  break;
204   |     }
205   |  
206   |  return item;
207   | }
208   |  
209   | /**
210   |  * Get the current stream in the parsing process. This can either be the
211   |  * latest stream added to the context, or the stream referenced by an item.
212   |  */
213   | static AVStream *get_curr_st(MOVContext *c)
214   | {
215   |     AVStream *st = NULL;
216   |     HEIFItem *item;
217   |  
218   |  if (c->fc->nb_streams < 1)
219   |  return NULL;
220   |  
221   |  if (c->cur_item_id == -1)
222   |  return c->fc->streams[c->fc->nb_streams-1];
223   |  
224   |     item = get_heif_item(c, c->cur_item_id);
225   |  if (item)
226   |         st = item->st;
227   |  
228   |  return st;
229   | }
230   |  
231   | static int mov_read_covr(MOVContext *c, AVIOContext *pb, int type, int len)
232   | {
233   |     AVStream *st;
234   |     MOVStreamContext *sc;
235   |  enum AVCodecID id;
236   |  int ret;
5126  |             sc->time_scale = 1;
5127  |     }
5128  | }
5129  |  
5130  | #if CONFIG_IAMFDEC
5131  | static int mov_update_iamf_streams(MOVContext *c, const AVStream *st)
5132  | {
5133  |  const MOVStreamContext *sc = st->priv_data;
5134  |  const IAMFContext *iamf = &sc->iamf->iamf;
5135  |  
5136  |  for (int i = 0; i < iamf->nb_audio_elements; i++) {
5137  |  const AVStreamGroup *stg = NULL;
5138  |  
5139  |  for (int j = 0; j < c->fc->nb_stream_groups; j++)
5140  |  if (c->fc->stream_groups[j]->id == iamf->audio_elements[i]->audio_element_id)
5141  |                 stg = c->fc->stream_groups[j];
5142  |  av_assert0(stg);
5143  |  
5144  |  for (int j = 0; j < stg->nb_streams; j++) {
5145  |  const FFStream *sti = cffstream(st);
5146  |             AVStream *out = stg->streams[j];
5147  |             FFStream *out_sti = ffstream(stg->streams[j]);
5148  |  
5149  |             out->codecpar->bit_rate = 0;
5150  |  
5151  |  if (out == st)
5152  |  continue;
5153  |  
5154  |             out->time_base           = st->time_base;
5155  |             out->start_time          = st->start_time;
5156  |             out->duration            = st->duration;
5157  |             out->nb_frames           = st->nb_frames;
5158  |             out->discard             = st->discard;
5159  |  
5160  |  av_assert0(!out_sti->index_entries);
5161  |             out_sti->index_entries = av_malloc(sti->index_entries_allocated_size);
5162  |  if (!out_sti->index_entries)
5163  |  return AVERROR(ENOMEM);
5164  |  
5165  |             out_sti->index_entries_allocated_size = sti->index_entries_allocated_size;
5166  |             out_sti->nb_index_entries = sti->nb_index_entries;
5167  |             out_sti->skip_samples = sti->skip_samples;
5168  |             memcpy(out_sti->index_entries, sti->index_entries, sti->index_entries_allocated_size);
5169  |         }
5170  |     }
5171  |  
5172  |  return 0;
5173  | }
5174  | #endif
5175  |  
5176  | static int sanity_checks(void *log_obj, MOVStreamContext *sc, int index)
5177  | {
5178  |  if ((sc->chunk_count && (!sc->stts_count || !sc->stsc_count ||
5179  |                             (!sc->sample_size && !sc->sample_count))) ||
5180  |         (sc->sample_count && (!sc->chunk_count ||
5181  |                              (!sc->sample_size && !sc->sample_sizes)))) {
5182  |         av_log(log_obj, AV_LOG_ERROR, "stream %d, missing mandatory atoms, broken header\n",
5183  |                index);
5184  |  return 1;
5185  |     }
5186  |  
5187  |  if (sc->stsc_count && sc->stsc_data[ sc->stsc_count - 1 ].first > sc->chunk_count) {
5188  |         av_log(log_obj, AV_LOG_ERROR, "stream %d, contradictionary STSC and STCO\n",
5189  |                index);
5190  |  return 2;
5191  |     }
5192  |  return 0;
5193  | }
5194  |  
5195  | static int mov_read_trak(MOVContext *c, AVIOContext *pb, MOVAtom atom)
5196  | {
5197  |     AVStream *st;
5198  |     MOVStreamContext *sc;
5199  |  int ret;
5200  |  
5201  |     st = avformat_new_stream(c->fc, NULL);
5202  |  if (!st) return AVERROR(ENOMEM);
5203  |     st->id = -1;
5204  |     sc = av_mallocz(sizeof(MOVStreamContext));
5205  |  if (!sc) return AVERROR(ENOMEM);
5206  |  
5207  |     st->priv_data = sc;
5208  |     st->codecpar->codec_type = AVMEDIA_TYPE_DATA;
5209  |     sc->ffindex = st->index;
5210  |     c->trak_index = st->index;
5211  |     sc->tref_flags = 0;
5212  |     sc->tref_id = -1;
5213  |     sc->refcount = 1;
5214  |  
5215  |  if ((ret = mov_read_default(c, pb, atom)) < 0)
5216  |  return ret;
5217  |  
5218  |     c->trak_index = -1;
5219  |  
5220  |  // Here stsc refers to a chunk not described in stco. This is technically invalid,
5221  |  // but we can overlook it (clearing stsc) whenever stts_count == 0 (indicating no samples).
5222  |  if (!sc->chunk_count && !sc->stts_count && sc->stsc_count) {
10447 |     }
10448 |  
10449 |  if (offset > INT64_MAX - item->extent_offset)
10450 |  return AVERROR_INVALIDDATA;
10451 |  
10452 |     avio_seek(s->pb, item->extent_offset + offset, SEEK_SET);
10453 |  
10454 |     avio_r8(s->pb);    /* version */
10455 |     flags = avio_r8(s->pb);
10456 |  
10457 |  for (int i = 0; i < 4; i++)
10458 |         canvas_fill_value[i] = avio_rb16(s->pb);
10459 |     av_log(c->fc, AV_LOG_TRACE, "iovl: canvas_fill_value { %u, %u, %u, %u }\n",
10460 |            canvas_fill_value[0], canvas_fill_value[1],
10461 |            canvas_fill_value[2], canvas_fill_value[3]);
10462 |  for (int i = 0; i < 4; i++)
10463 |         tile_grid->background[i] = canvas_fill_value[i];
10464 |  
10465 |  /* actual width and height of output image */
10466 |     tile_grid->width        =
10467 |     tile_grid->coded_width  = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);
10468 |     tile_grid->height       =
10469 |     tile_grid->coded_height = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);
10470 |  
10471 |     av_log(c->fc, AV_LOG_TRACE, "iovl: output_width %d, output_height %d\n",
10472 |            tile_grid->width, tile_grid->height);
10473 |  
10474 |     tile_grid->nb_tiles = grid->nb_tiles;
10475 |     tile_grid->offsets = av_malloc_array(tile_grid->nb_tiles, sizeof(*tile_grid->offsets));
10476 |  if (!tile_grid->offsets) {
10477 |         ret = AVERROR(ENOMEM);
10478 |  goto fail;
10479 |     }
10480 |  
10481 |  for (int i = 0; i < tile_grid->nb_tiles; i++) {
10482 |         tile_grid->offsets[i].idx        = grid->tile_idx_list[i];
10483 |         tile_grid->offsets[i].horizontal = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);
10484 |         tile_grid->offsets[i].vertical   = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);
10485 |         av_log(c->fc, AV_LOG_TRACE, "iovl: stream_idx[%d] %u, "
10486 |  "horizontal_offset[%d] %d, vertical_offset[%d] %d\n",
10487 |                i, tile_grid->offsets[i].idx,
10488 |                i, tile_grid->offsets[i].horizontal, i, tile_grid->offsets[i].vertical);
10489 |     }
10490 |  
10491 | fail:
10492 |     avio_seek(s->pb, pos, SEEK_SET);
10493 |  
10494 |  return ret;
10495 | }
10496 |  
10497 | static int mov_parse_exif_item(AVFormatContext *s,
10498 |                                AVPacketSideData **coded_side_data, int *nb_coded_side_data,
10499 |  const HEIFItem *ref)
10500 | {
10501 |  MOVContext *c = s->priv_data;
10502 |     AVPacketSideData *sd;
10503 |     AVExifMetadata ifd = { 0 };
10504 |     AVBufferRef *buf;
10505 |     int64_t offset = 0, pos = avio_tell(s->pb);
10506 |  unsigned orientation_id = av_exif_get_tag_id("Orientation");
10507 |  int err;
10508 |  
10509 |  if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
    31←Assuming the condition is false→
    32←Taking false branch→
10510 |         av_log(c->fc, AV_LOG_WARNING, "Exif metadata with non seekable input\n");
10511 |  return AVERROR_PATCHWELCOME;
10512 |     }
10513 |  if (ref->is_idat_relative32.1Field 'is_idat_relative' is 0) {
    33←Taking false branch→
10514 |  if (!c->idat_offset) {
10515 |             av_log(c->fc, AV_LOG_ERROR, "missing idat box required by the Exif metadata\n");
10516 |  return AVERROR_INVALIDDATA;
10517 |         }
10518 |         offset = c->idat_offset;
10519 |     }
10520 |  
10521 |  buf = av_buffer_alloc(ref->extent_length);
10522 |  if (!buf)
    34←Assuming 'buf' is non-null→
    35←Taking false branch→
10523 |  return AVERROR(ENOMEM);
10524 |  
10525 |  if (offset > INT64_MAX - ref->extent_offset) {
    36←Taking false branch→
10526 |         err = AVERROR(ENOMEM);
10527 |  goto fail;
10528 |     }
10529 |  
10530 |  avio_seek(s->pb, ref->extent_offset + offset, SEEK_SET);
10531 |     err = avio_read(s->pb, buf->data, ref->extent_length);
10532 |  if (err != ref->extent_length) {
    37←Assuming 'err' is equal to field 'extent_length'→
    38←Taking false branch→
10533 |  if (err > 0)
10534 |             err = AVERROR_INVALIDDATA;
10535 |  goto fail;
10536 |     }
10537 |  
10538 |  // HEIF spec states that Exif metadata is informative. The irot item property is
10539 |  // the normative source of rotation information. So we remove any Orientation tag
10540 |  // present in the Exif buffer.
10541 |  err = av_exif_parse_buffer(s, buf->data, ref->extent_length, &ifd, AV_EXIF_T_OFF);
10542 |  if (err < 0) {
    39←Assuming 'err' is >= 0→
    40←Taking false branch→
10543 |         av_log(s, AV_LOG_ERROR, "Unable to parse Exif metadata\n");
10544 |  goto fail;
10545 |     }
10546 |  
10547 |  err = av_exif_remove_entry(s, &ifd, orientation_id, 0);
10548 |  if (err < 0)
    41←Assuming 'err' is >= 0→
    42←Taking false branch→
10549 |  goto fail;
10550 |  else if (!err)
    43←Assuming 'err' is 0→
    44←Taking true branch→
10551 |  goto finish;
    45←Control jumps to line 10559→
10552 |  
10553 |     av_buffer_unref(&buf);
10554 |     err = av_exif_write(s, &ifd, &buf, AV_EXIF_T_OFF);
10555 |  if (err < 0)
10556 |  goto fail;
10557 |  
10558 | finish:
10559 |  offset = AV_RB32(buf->data) + 4;
    46←buffer read by avio_read may be partially uninitialized
10560 |  if (offset >= buf->size) {
10561 |         err = AVERROR_INVALIDDATA;
10562 |  goto fail;
10563 |     }
10564 |     sd = av_packet_side_data_new(coded_side_data, nb_coded_side_data,
10565 |                                  AV_PKT_DATA_EXIF, buf->size - offset, 0);
10566 |  if (!sd) {
10567 |         err = AVERROR(ENOMEM);
10568 |  goto fail;
10569 |     }
10570 |     memcpy(sd->data, buf->data + offset, buf->size - offset);
10571 |  
10572 |     err = 0;
10573 | fail:
10574 |     av_buffer_unref(&buf);
10575 |     av_exif_free(&ifd);
10576 |     avio_seek(s->pb, pos, SEEK_SET);
10577 |  
10578 |  return err;
10579 | }
10580 |  
10581 | static int mov_parse_tiles(AVFormatContext *s)
10582 | {
10583 |     MOVContext *mov = s->priv_data;
10584 |  
10585 |  for (int i = 0; i < mov->nb_heif_grid; i++) {
10586 |         AVStreamGroup *stg = avformat_stream_group_create(s, AV_STREAM_GROUP_PARAMS_TILE_GRID, NULL);
10587 |         AVStreamGroupTileGrid *tile_grid;
10588 |  const HEIFGrid *grid = &mov->heif_grid[i];
10589 |  int err, loop = 1;
10657 |  case MKTAG('i','o','v','l'):
10658 |             err = read_image_iovl(s, grid, tile_grid);
10659 |  break;
10660 |  default:
10661 |  av_assert0(0);
10662 |         }
10663 |  if (err < 0)
10664 |  return err;
10665 |  
10666 |  for (int j = 0; j < grid->item->nb_iref_list; j++) {
10667 |             HEIFItem *ref = get_heif_item(mov, grid->item->iref_list[j].item_id);
10668 |  
10669 |  av_assert0(ref);
10670 |  switch(ref->type) {
10671 |  case MKTAG('E','x','i','f'):
10672 |                 err = mov_parse_exif_item(s, &tile_grid->coded_side_data,
10673 |                                              &tile_grid->nb_coded_side_data, ref);
10674 |  if (err < 0 && (s->error_recognition & AV_EF_EXPLODE))
10675 |  return err;
10676 |  break;
10677 |  default:
10678 |  break;
10679 |             }
10680 |         }
10681 |  
10682 |  /* rotation */
10683 |  if (grid->item->rotation || grid->item->hflip || grid->item->vflip) {
10684 |             err = set_display_matrix_from_item(&tile_grid->coded_side_data,
10685 |                                                &tile_grid->nb_coded_side_data, grid->item);
10686 |  if (err < 0)
10687 |  return err;
10688 |         }
10689 |  
10690 |  /* ICC profile */
10691 |  if (grid->item->icc_profile_size) {
10692 |             err = set_icc_profile_from_item(&tile_grid->coded_side_data,
10693 |                                             &tile_grid->nb_coded_side_data, grid->item);
10694 |  if (err < 0)
10695 |  return err;
10696 |         }
10697 |  
10698 |  if (grid->item->name)
10699 |             av_dict_set(&stg->metadata, "title", grid->item->name, 0);
10700 |  if (grid->item->item_id == mov->primary_item_id)
10701 |             stg->disposition |= AV_DISPOSITION_DEFAULT;
10702 |     }
10703 |  
10704 |  return 0;
10705 | }
10706 |  
10707 | static int mov_parse_heif_items(AVFormatContext *s)
10708 | {
10709 |  MOVContext *mov = s->priv_data;
10710 |  int err;
10711 |  
10712 |  for (int i = 0; i < mov->nb_heif_item; i++) {
    12←Assuming 'i' is < field 'nb_heif_item'→
    13←Loop condition is true.  Entering loop body→
10713 |  HEIFItem *item = mov->heif_item[i];
10714 |         MOVStreamContext *sc;
10715 |         AVStream *st;
10716 |         int64_t offset = 0;
10717 |  
10718 |  if (!item)
    14←Assuming 'item' is non-null→
    15←Taking false branch→
10719 |  continue;
10720 |  if (!item->st) {
    16←Assuming field 'st' is non-null→
    17←Taking false branch→
10721 |  continue;
10722 |         }
10723 |  if (item->is_idat_relative) {
    18←Assuming field 'is_idat_relative' is 0→
    19←Taking false branch→
10724 |  if (!mov->idat_offset) {
10725 |                 av_log(s, AV_LOG_ERROR, "Missing idat box for item %d\n", item->item_id);
10726 |  return AVERROR_INVALIDDATA;
10727 |             }
10728 |             offset = mov->idat_offset;
10729 |         }
10730 |  
10731 |  st = item->st;
10732 |         sc = st->priv_data;
10733 |         st->codecpar->width  = item->width;
10734 |         st->codecpar->height = item->height;
10735 |  
10736 |         sc->sample_size  = sc->stsz_sample_size = item->extent_length;
10737 |         sc->sample_count = 1;
10738 |  
10739 |         err = sanity_checks(s, sc, st->index);
10740 |  if (err19.1'err' is 0)
    20←Taking false branch→
10741 |  return AVERROR_INVALIDDATA;
10742 |  
10743 |  if (offset > INT64_MAX - item->extent_offset)
    21←Assuming the condition is false→
    22←Taking false branch→
10744 |  return AVERROR_INVALIDDATA;
10745 |  
10746 |  sc->chunk_offsets[0] = item->extent_offset + offset;
10747 |  
10748 |  if (item->item_id == mov->primary_item_id)
    23←Assuming field 'item_id' is not equal to field 'primary_item_id'→
    24←Taking false branch→
10749 |             st->disposition |= AV_DISPOSITION_DEFAULT;
10750 |  
10751 |  for (int j = 0; j < item->nb_iref_list; j++) {
    25←Assuming 'j' is < field 'nb_iref_list'→
    26←Loop condition is true.  Entering loop body→
10752 |  HEIFItem *ref = get_heif_item(mov, item->iref_list[j].item_id);
10753 |  
10754 |  av_assert0(ref);
    27←Taking false branch→
    28←Loop condition is false.  Exiting loop→
10755 |  switch(ref->type) {
    29←Control jumps to 'case 1718188101:'  at line 10756→
10756 |  case MKTAG('E','x','i','f'):
10757 |  err = mov_parse_exif_item(s, &st->codecpar->coded_side_data,
    30←Calling 'mov_parse_exif_item'→
10758 |  &st->codecpar->nb_coded_side_data, ref);
10759 |  if (err < 0 && (s->error_recognition & AV_EF_EXPLODE))
10760 |  return err;
10761 |  break;
10762 |  default:
10763 |  break;
10764 |             }
10765 |         }
10766 |  
10767 |  if (item->rotation || item->hflip || item->vflip) {
10768 |             err = set_display_matrix_from_item(&st->codecpar->coded_side_data,
10769 |                                                &st->codecpar->nb_coded_side_data, item);
10770 |  if (err < 0)
10771 |  return err;
10772 |         }
10773 |  
10774 |         mov_build_index(mov, st);
10775 |     }
10776 |  
10777 |  if (mov->nb_heif_grid) {
10778 |         err = mov_parse_tiles(s);
10779 |  if (err < 0)
10780 |  return err;
10781 |     }
10782 |  
10783 |  return 0;
10784 | }
10785 |  
10786 | static AVStream *mov_find_reference_track(AVFormatContext *s, AVStream *st,
10787 |  int first_index)
10788 | {
10844 |         }
10845 |  
10846 |         err = avformat_stream_group_add_stream(stg, st);
10847 |  if (err < 0)
10848 |  return err;
10849 |  
10850 |         stg->params.lcevc->lcevc_index = stg->nb_streams - 1;
10851 |     }
10852 |  
10853 |  return 0;
10854 | }
10855 |  
10856 | static void fix_stream_ids(AVFormatContext *s)
10857 | {
10858 |  int highest_id = 0, lowest_iamf_id = INT_MAX;
10859 |  
10860 |  for (int i = 0; i < s->nb_streams; i++) {
10861 |  const AVStream *st = s->streams[i];
10862 |  const MOVStreamContext *sc = st->priv_data;
10863 |  if (!sc->iamf)
10864 |             highest_id = FFMAX(highest_id, st->id);
10865 |     }
10866 |  
10867 |  for (int i = 0; i < s->nb_stream_groups; i++) {
10868 |         AVStreamGroup *stg = s->stream_groups[i];
10869 |  if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
10870 |  continue;
10871 |  for (int j = 0; j < stg->nb_streams; j++) {
10872 |             AVStream *st = stg->streams[j];
10873 |             lowest_iamf_id = FFMIN(lowest_iamf_id, st->id);
10874 |         }
10875 |     }
10876 |  
10877 |  if (highest_id < lowest_iamf_id)
10878 |  return;
10879 |  
10880 |     highest_id += !lowest_iamf_id;
10881 |  for (int i = 0; highest_id > 1 && i < s->nb_stream_groups; i++) {
10882 |         AVStreamGroup *stg = s->stream_groups[i];
10883 |  if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
10884 |  continue;
10885 |  for (int j = 0; j < stg->nb_streams; j++) {
10886 |             AVStream *st = stg->streams[j];
10887 |             MOVStreamContext *sc = st->priv_data;
10888 |             st->id += highest_id;
10889 |             sc->iamf_stream_offset = highest_id;
10890 |         }
10891 |     }
10892 | }
10893 |  
10894 | static int mov_read_header(AVFormatContext *s)
10895 | {
10896 |  MOVContext *mov = s->priv_data;
10897 |     AVIOContext *pb = s->pb;
10898 |  int j, err;
10899 |     MOVAtom atom = { AV_RL32("root") };
10900 |  int i;
10901 |  
10902 |     mov->fc = s;
10903 |     mov->trak_index = -1;
10904 |     mov->primary_item_id = -1;
10905 |     mov->cur_item_id = -1;
10906 |  /* .mov and .mp4 aren't streamable anyway (only progressive download if moov is before mdat) */
10907 |  if (pb->seekable & AVIO_SEEKABLE_NORMAL)
    1Assuming the condition is false→
    2←Taking false branch→
10908 |         atom.size = avio_size(pb);
10909 |  else
10910 |  atom.size = INT64_MAX;
10911 |  
10912 |  /* check MOV header */
10913 |  do {
10914 |  if (mov->moov_retry)
    3←Assuming field 'moov_retry' is 0→
    4←Taking false branch→
10915 |             avio_seek(pb, 0, SEEK_SET);
10916 |  if ((err = mov_read_default(mov, pb, atom)) < 0) {
    5←Assuming the condition is false→
10917 |             av_log(s, AV_LOG_ERROR, "error reading header\n");
10918 |  return err;
10919 |         }
10920 |     } while ((pb->seekable & AVIO_SEEKABLE_NORMAL) &&
    6←Assuming the condition is false→
10921 |              !mov->found_moov && (!mov->found_iloc || !mov->found_iinf) && !mov->moov_retry++);
10922 |  if (!mov->found_moov && !mov->found_iloc && !mov->found_iinf) {
    7←Assuming field 'found_moov' is not equal to 0→
10923 |         av_log(s, AV_LOG_ERROR, "moov atom not found\n");
10924 |  return AVERROR_INVALIDDATA;
10925 |     }
10926 |  av_log(mov->fc, AV_LOG_TRACE, "on_parse_exit_offset=%"PRId64"\n", avio_tell(pb));
10927 |  
10928 |  if (mov->found_iloc && mov->found_iinf) {
    8←Assuming field 'found_iloc' is not equal to 0→
    9←Assuming field 'found_iinf' is not equal to 0→
    10←Taking true branch→
10929 |  err = mov_parse_heif_items(s);
    11←Calling 'mov_parse_heif_items'→
10930 |  if (err < 0)
10931 |  return err;
10932 |     }
10933 |  // prevent iloc and iinf boxes from being parsed while reading packets.
10934 |  // this is needed because an iinf box may have been parsed but ignored
10935 |  // for having old infe boxes which create no streams.
10936 |     mov->found_iloc = mov->found_iinf = 1;
10937 |  
10938 |  if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
10939 |  if (mov->nb_chapter_tracks > 0 && !mov->ignore_chapters)
10940 |             mov_read_chapters(s);
10941 |  for (i = 0; i < s->nb_streams; i++)
10942 |  if (s->streams[i]->codecpar->codec_tag == AV_RL32("tmcd")) {
10943 |                 mov_read_timecode_track(s, s->streams[i]);
10944 |             } else if (s->streams[i]->codecpar->codec_tag == AV_RL32("rtmd")) {
10945 |                 mov_read_rtmd_track(s, s->streams[i]);
10946 |             }
10947 |     }
10948 |  
10949 |  /* copy timecode metadata from tmcd tracks to the related video streams */
10950 |  for (i = 0; i < s->nb_streams; i++) {
10951 |         AVStream *st = s->streams[i];
10952 |         MOVStreamContext *sc = st->priv_data;
10953 |  if (sc->timecode_track > 0) {
10954 |             AVDictionaryEntry *tcr;
10955 |  int tmcd_st_id = -1;
10956 |  
10957 |  for (j = 0; j < s->nb_streams; j++) {
10958 |                 MOVStreamContext *sc2 = s->streams[j]->priv_data;
10959 |  if (sc2->id == sc->timecode_track)

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
