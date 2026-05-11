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

File:| format/rtsp.c  
---|---  
Warning:| line 2321, column 37  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


2253  |  if (fdsnum != 2) {
2254  |                     av_log(s, AV_LOG_ERROR,
2255  |  "Number of fds %d not supported\n", fdsnum);
2256  |                     av_freep(&fds);
2257  |  return AVERROR_INVALIDDATA;
2258  |                 }
2259  |  for (fdsidx = 0; fdsidx < fdsnum; fdsidx++) {
2260  |                     p[rt->max_p].fd       = fds[fdsidx];
2261  |                     p[rt->max_p++].events = POLLIN;
2262  |                 }
2263  |                 av_freep(&fds);
2264  |             }
2265  |         }
2266  |     }
2267  |  
2268  |  for (;;) {
2269  |  if (ff_check_interrupt(&s->interrupt_callback))
2270  |  return AVERROR_EXIT;
2271  |  if (wait_end && wait_end - av_gettime_relative() < 0)
2272  |  return AVERROR(EAGAIN);
2273  |         n = poll(p, rt->max_p, POLLING_TIME);
2274  |  if (n > 0) {
2275  |  int j = rt->rtsp_hd ? 1 : 0;
2276  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2277  |                 rtsp_st = rt->rtsp_streams[i];
2278  |  if (rtsp_st->rtp_handle) {
2279  |  if (p[j].revents & POLLIN || p[j+1].revents & POLLIN) {
2280  |                         ret = ffurl_read(rtsp_st->rtp_handle, buf, buf_size);
2281  |  if (ret > 0) {
2282  |                             *prtsp_st = rtsp_st;
2283  |  return ret;
2284  |                         }
2285  |                     }
2286  |                     j+=2;
2287  |                 }
2288  |             }
2289  | #if CONFIG_RTSP_DEMUXER
2290  |  if (rt->rtsp_hd && p[0].revents & POLLIN) {
2291  |  if ((ret = parse_rtsp_message(s)) < 0) {
2292  |  return ret;
2293  |                 }
2294  |             }
2295  | #endif
2296  |         } else if (n == 0 && rt->stimeout > 0 && --runs <= 0) {
2297  |  return AVERROR(ETIMEDOUT);
2298  |         } else if (n < 0 && errno != EINTR)
2299  |  return AVERROR(errno);
2300  |     }
2301  | }
2302  |  
2303  | static int pick_stream(AVFormatContext *s, RTSPStream **rtsp_st,
2304  |  const uint8_t *buf, int len)
2305  | {
2306  |  RTSPState *rt = s->priv_data;
2307  |  int i;
2308  |  if (len < 0)
    25←Assuming 'len' is >= 0→
    26←Taking false branch→
2309  |  return len;
2310  |  if (rt->nb_rtsp_streams == 1) {
    27←Assuming field 'nb_rtsp_streams' is not equal to 1→
2311  |         *rtsp_st = rt->rtsp_streams[0];
2312  |  return len;
2313  |     }
2314  |  if (len >= 8 && rt->transport == RTSP_TRANSPORT_RTP) {
    28←Assuming 'len' is >= 8→
    29←Assuming field 'transport' is equal to RTSP_TRANSPORT_RTP→
2315  |  if (RTP_PT_IS_RTCP(rt->recvbuf[1])) {
    30←Taking true branch→
    31←Assuming the condition is true→
    32←Assuming the condition is true→
2316  |  int no_ssrc = 0;
2317  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
    33←Assuming 'i' is < field 'nb_rtsp_streams'→
    34←Loop condition is true.  Entering loop body→
2318  |  RTPDemuxContext *rtpctx = rt->rtsp_streams[i]->transport_priv;
2319  |  if (!rtpctx)
    35←Assuming 'rtpctx' is non-null→
    36←Taking false branch→
2320  |  continue;
2321  |  if (rtpctx->ssrc == AV_RB32(&buf[4])) {
    37←buffer read by avio_read may be partially uninitialized
2322  |                     *rtsp_st = rt->rtsp_streams[i];
2323  |  return len;
2324  |                 }
2325  |  if (!rtpctx->ssrc)
2326  |                     no_ssrc = 1;
2327  |             }
2328  |  if (no_ssrc) {
2329  |                 av_log(s, AV_LOG_WARNING,
2330  |  "Unable to pick stream for packet - SSRC not known for "
2331  |  "all streams\n");
2332  |  return AVERROR(EAGAIN);
2333  |             }
2334  |         } else {
2335  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2336  |  if ((buf[1] & 0x7f) == rt->rtsp_streams[i]->sdp_payload_type) {
2337  |                     *rtsp_st = rt->rtsp_streams[i];
2338  |  return len;
2339  |                 }
2340  |             }
2341  |         }
2342  |     }
2343  |     av_log(s, AV_LOG_WARNING, "Unable to pick stream for packet\n");
2344  |  return AVERROR(EAGAIN);
2345  | }
2346  |  
2347  | static int read_packet(AVFormatContext *s,
2348  |                        RTSPStream **rtsp_st, RTSPStream *first_queue_st,
2349  |                        int64_t wait_end)
2350  | {
2351  |  RTSPState *rt = s->priv_data;
2352  |  int len;
2353  |  
2354  |  switch(rt->lower_transport) {
    23←Control jumps to 'case RTSP_LOWER_TRANSPORT_CUSTOM:'  at line 2367→
2355  |  default:
2356  | #if CONFIG_RTSP_DEMUXER
2357  |  case RTSP_LOWER_TRANSPORT_TCP:
2358  |         len = ff_rtsp_tcp_read_packet(s, rtsp_st, rt->recvbuf, RECVBUF_SIZE);
2359  |  break;
2360  | #endif
2361  |  case RTSP_LOWER_TRANSPORT_UDP:
2362  |  case RTSP_LOWER_TRANSPORT_UDP_MULTICAST:
2363  |         len = udp_read_packet(s, rtsp_st, rt->recvbuf, RECVBUF_SIZE, wait_end);
2364  |  if (len > 0 && (*rtsp_st)->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
2365  |             ff_rtp_check_and_send_back_rr((*rtsp_st)->transport_priv, (*rtsp_st)->rtp_handle, NULL, len);
2366  |  break;
2367  |  case RTSP_LOWER_TRANSPORT_CUSTOM:
2368  |  if (first_queue_st23.1'first_queue_st' is null && rt->transport == RTSP_TRANSPORT_RTP &&
2369  |             wait_end && wait_end < av_gettime_relative())
2370  |             len = AVERROR(EAGAIN);
2371  |  else
2372  |  len = avio_read_partial(s->pb, rt->recvbuf, RECVBUF_SIZE);
2373  |  len = pick_stream(s, rtsp_st, rt->recvbuf, len);
    24←Calling 'pick_stream'→
2374  |  if (len > 0 && (*rtsp_st)->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
2375  |             ff_rtp_check_and_send_back_rr((*rtsp_st)->transport_priv, NULL, s->pb, len);
2376  |  break;
2377  |     }
2378  |  
2379  |  if (len == 0)
2380  |  return AVERROR_EOF;
2381  |  
2382  |  return len;
2383  | }
2384  |  
2385  | int ff_rtsp_fetch_packet(AVFormatContext *s, AVPacket *pkt)
2386  | {
2387  |  RTSPState *rt = s->priv_data;
2388  |  int ret, len;
2389  |     RTSPStream *rtsp_st, *first_queue_st = NULL;
2390  |     int64_t wait_end = 0;
2391  |  
2392  |  if (rt->nb_byes == rt->nb_rtsp_streams)
    1Assuming field 'nb_byes' is not equal to field 'nb_rtsp_streams'→
    2←Taking false branch→
2393  |  return AVERROR_EOF;
2394  |  
2395  |  /* get next frames from the same RTP packet */
2396  |  if (rt->cur_transport_priv) {
    3←Assuming field 'cur_transport_priv' is null→
    4←Taking false branch→
2397  |  if (rt->transport == RTSP_TRANSPORT_RDT) {
2398  |             ret = ff_rdt_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
2399  |         } else if (rt->transport == RTSP_TRANSPORT_RTP) {
2400  |             ret = ff_rtp_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
2401  |         } else if (CONFIG_RTPDEC && rt->ts) {
2402  |             ret = avpriv_mpegts_parse_packet(rt->ts, pkt, rt->recvbuf + rt->recvbuf_pos, rt->recvbuf_len - rt->recvbuf_pos);
2403  |  if (ret >= 0) {
2404  |                 rt->recvbuf_pos += ret;
2405  |                 ret = rt->recvbuf_pos < rt->recvbuf_len;
2406  |             }
2407  |         } else
2408  |             ret = -1;
2409  |  if (ret == 0) {
2410  |             rt->cur_transport_priv = NULL;
2411  |  return 0;
2412  |         } else if (ret == 1) {
2413  |  return 0;
2414  |         } else
2415  |             rt->cur_transport_priv = NULL;
2416  |     }
2417  |  
2418  | redo:
2419  |  if (rt->transport19.1Field 'transport' is not equal to RTSP_TRANSPORT_RTP == RTSP_TRANSPORT_RTP) {
    5←Assuming field 'transport' is not equal to RTSP_TRANSPORT_RTP→
    6←Taking false branch→
    20←Taking false branch→
2420  |  int i;
2421  |         int64_t first_queue_time = 0;
2422  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2423  |             RTPDemuxContext *rtpctx = rt->rtsp_streams[i]->transport_priv;
2424  |             int64_t queue_time;
2425  |  if (!rtpctx)
2426  |  continue;
2427  |             queue_time = ff_rtp_queued_packet_time(rtpctx);
2428  |  if (queue_time && (queue_time - first_queue_time < 0 ||
2429  |                                !first_queue_time)) {
2430  |                 first_queue_time = queue_time;
2431  |                 first_queue_st   = rt->rtsp_streams[i];
2432  |             }
2433  |         }
2434  |  if (first_queue_time) {
2435  |             wait_end = first_queue_time + s->max_delay;
2436  |         } else {
2437  |             wait_end = 0;
2438  |             first_queue_st = NULL;
2439  |         }
2440  |     }
2441  |  
2442  |  /* read next RTP packet */
2443  |  if (!rt->recvbuf20.1Field 'recvbuf' is non-null) {
    7←Assuming field 'recvbuf' is non-null→
    8←Taking false branch→
    21←Taking false branch→
2444  |         rt->recvbuf = av_malloc(RECVBUF_SIZE);
2445  |  if (!rt->recvbuf)
2446  |  return AVERROR(ENOMEM);
2447  |     }
2448  |  
2449  |  len = read_packet(s, &rtsp_st, first_queue_st, wait_end);
    22←Calling 'read_packet'→
2450  |  if (len == AVERROR(EAGAIN) && first_queue_st &&
    9←Assuming the condition is false→
2451  |         rt->transport == RTSP_TRANSPORT_RTP) {
2452  |         av_log(s, AV_LOG_WARNING,
2453  |  "max delay reached. need to consume packet\n");
2454  |         rtsp_st = first_queue_st;
2455  |         ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, NULL, 0);
2456  |  goto end;
2457  |     }
2458  |  if (len < 0)
    10←Assuming 'len' is >= 0→
    11←Taking false branch→
2459  |  return len;
2460  |  
2461  |  if (rt->transport == RTSP_TRANSPORT_RDT) {
    12←Assuming field 'transport' is not equal to RTSP_TRANSPORT_RDT→
    13←Taking false branch→
2462  |         ret = ff_rdt_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
2463  |     } else if (rt->transport13.1Field 'transport' is not equal to RTSP_TRANSPORT_RTP == RTSP_TRANSPORT_RTP) {
2464  |         ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
2465  |  if (rtsp_st->feedback) {
2466  |             AVIOContext *pb = NULL;
2467  |  if (rt->lower_transport == RTSP_LOWER_TRANSPORT_CUSTOM)
2468  |                 pb = s->pb;
2469  |             ff_rtp_send_rtcp_feedback(rtsp_st->transport_priv, rtsp_st->rtp_handle, pb);
2470  |         }
2471  |  if (ret < 0) {
2472  |  /* Either bad packet, or a RTCP packet. Check if the
2473  |  * first_rtcp_ntp_time field was initialized. */
2474  |             RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
2475  |  if (rtpctx->first_rtcp_ntp_time != AV_NOPTS_VALUE) {
2476  |  /* first_rtcp_ntp_time has been initialized for this stream,
2477  |  * copy the same value to all other uninitialized streams,
2478  |  * in order to map their timestamp origin to the same ntp time
2479  |  * as this one. */
2480  |  int i;
2481  |                 AVStream *st = NULL;
2482  |  if (rtsp_st->stream_index >= 0)
2483  |                     st = s->streams[rtsp_st->stream_index];
2484  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2485  |                     RTPDemuxContext *rtpctx2 = rt->rtsp_streams[i]->transport_priv;
2486  |                     AVStream *st2 = NULL;
2487  |  if (rt->rtsp_streams[i]->stream_index >= 0)
2488  |                         st2 = s->streams[rt->rtsp_streams[i]->stream_index];
2489  |  if (rtpctx2 && st && st2 &&
2490  |                         rtpctx2->first_rtcp_ntp_time == AV_NOPTS_VALUE) {
2491  |                         rtpctx2->first_rtcp_ntp_time = rtpctx->first_rtcp_ntp_time;
2492  |                         rtpctx2->rtcp_ts_offset = av_rescale_q(
2493  |                             rtpctx->rtcp_ts_offset, st->time_base,
2494  |                             st2->time_base);
2495  |                     }
2496  |                 }
2497  |  // Make real NTP start time available in AVFormatContext
2498  |  if (s->start_time_realtime == AV_NOPTS_VALUE) {
2499  |                     s->start_time_realtime = ff_parse_ntp_time(rtpctx->first_rtcp_ntp_time) - NTP_OFFSET_US;
2500  |  if (rtpctx->st) {
2501  |                         s->start_time_realtime -=
2502  |                             av_rescale_q (rtpctx->rtcp_ts_offset, rtpctx->st->time_base, AV_TIME_BASE_Q);
2503  |                     }
2504  |                 }
2505  |             }
2506  |  if (ret == -RTCP_BYE) {
2507  |                 rt->nb_byes++;
2508  |  
2509  |                 av_log(s, AV_LOG_DEBUG, "Received BYE for stream %d (%d/%d)\n",
2510  |                        rtsp_st->stream_index, rt->nb_byes, rt->nb_rtsp_streams);
2511  |  
2512  |  if (rt->nb_byes == rt->nb_rtsp_streams)
2513  |  return AVERROR_EOF;
2514  |             }
2515  |         }
2516  |     } else if (CONFIG_RTPDEC && rt->ts) {
    14←Assuming field 'ts' is non-null→
    15←Taking true branch→
2517  |  ret = avpriv_mpegts_parse_packet(rt->ts, pkt, rt->recvbuf, len);
2518  |  if (ret >= 0) {
    16←Assuming 'ret' is < 0→
    17←Taking false branch→
2519  |  if (ret < len) {
2520  |                 rt->recvbuf_len = len;
2521  |                 rt->recvbuf_pos = ret;
2522  |                 rt->cur_transport_priv = rt->ts;
2523  |  return 1;
2524  |             } else {
2525  |                 ret = 0;
2526  |             }
2527  |         }
2528  |     } else {
2529  |  return AVERROR_INVALIDDATA;
2530  |     }
2531  | end:
2532  |  if (ret17.1'ret' is < 0 < 0)
    18←Taking true branch→
2533  |  goto redo;
    19←Control jumps to line 2419→
2534  |  if (ret == 1)
2535  |  /* more packets may follow, so we save the RTP context */
2536  |         rt->cur_transport_priv = rtsp_st->transport_priv;
2537  |  
2538  |  return ret;
2539  | }
2540  | #endif /* CONFIG_RTPDEC */
2541  |  
2542  | #if CONFIG_SDP_DEMUXER
2543  | static int sdp_probe(const AVProbeData *p1)
2544  | {
2545  |  const char *p = p1->buf, *p_end = p1->buf + p1->buf_size;
2546  |  
2547  |  /* we look for a line beginning "c=IN IP" */
2548  |  while (p < p_end && *p != '\0') {
2549  |  if (sizeof("c=IN IP") - 1 < p_end - p &&
2550  |             av_strstart(p, "c=IN IP", NULL))
2551  |  return AVPROBE_SCORE_EXTENSION;
2552  |  
2553  |  while (p < p_end - 1 && *p != '\n') p++;
2554  |  if (++p >= p_end)
2555  |  break;
2556  |  if (*p == '\r')
2557  |             p++;
2558  |     }
2559  |  return 0;
2560  | }
2561  |  
2562  | static void append_source_addrs(char *buf, int size, const char *name,
2563  |  int count, struct RTSPSource **addrs)

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
