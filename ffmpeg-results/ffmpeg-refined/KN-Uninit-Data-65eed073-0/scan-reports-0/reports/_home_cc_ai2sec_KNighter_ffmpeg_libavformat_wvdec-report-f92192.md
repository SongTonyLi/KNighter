### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/wvdec.c  
---|---  
Warning:| line 322, column 25  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


28    | #include "id3v1.h"
29    | #include "wv.h"
30    |  
31    | enum WV_FLAGS {
32    |     WV_MONO   = 0x0004,
33    |     WV_HYBRID = 0x0008,
34    |     WV_JOINT  = 0x0010,
35    |     WV_CROSSD = 0x0020,
36    |     WV_HSHAPE = 0x0040,
37    |     WV_FLOAT  = 0x0080,
38    |     WV_INT32  = 0x0100,
39    |     WV_HBR    = 0x0200,
40    |     WV_HBAL   = 0x0400,
41    |     WV_MCINIT = 0x0800,
42    |     WV_MCEND  = 0x1000,
43    |     WV_DSD    = 0x80000000,
44    | };
45    |  
46    | static const int wv_rates[16] = {
47    |      6000,  8000,  9600, 11025, 12000, 16000,  22050, 24000,
48    |     32000, 44100, 48000, 64000, 88200, 96000, 192000,    -1
49    | };
50    |  
51    | typedef struct WVContext {
52    |     uint8_t block_header[WV_HEADER_SIZE];
53    |     WvHeader header;
54    |  int rate, chan, bpp;
55    |     uint32_t chmask;
56    |  int multichannel;
57    |  int block_parsed;
58    |     int64_t pos;
59    |  
60    |     int64_t apetag_start;
61    | } WVContext;
62    |  
63    | static int wv_probe(const AVProbeData *p)
64    | {
65    |  /* check file header */
66    |  if (p->buf_size <= 32)
67    |  return 0;
68    |  if (AV_RL32(&p->buf[0]) == MKTAG('w', 'v', 'p', 'k') &&
69    |  AV_RL32(&p->buf[4]) >= 24 &&
70    |  AV_RL32(&p->buf[4]) <= WV_BLOCK_LIMIT &&
71    |  AV_RL16(&p->buf[8]) >= 0x402 &&
72    |  AV_RL16(&p->buf[8]) <= 0x410)
73    |  return AVPROBE_SCORE_MAX;
74    |  else
75    |  return 0;
76    | }
77    |  
78    | static int wv_read_block_header(AVFormatContext *ctx, AVIOContext *pb)
79    | {
80    |     WVContext *wc = ctx->priv_data;
81    |  int ret;
82    |  int rate, bpp, chan;
83    |     uint32_t chmask, flags;
84    |  unsigned rate_x;
85    |  
86    |     wc->pos = avio_tell(pb);
87    |  
88    |  /* don't return bogus packets with the ape tag data */
89    |  if (wc->apetag_start && wc->pos >= wc->apetag_start)
90    |  return AVERROR_EOF;
91    |  
92    |     ret = avio_read(pb, wc->block_header, WV_HEADER_SIZE);
93    |  if (ret != WV_HEADER_SIZE)
94    |  return (ret < 0) ? ret : AVERROR_EOF;
95    |  
96    |     ret = ff_wv_parse_header(&wc->header, wc->block_header);
97    |  if (ret < 0) {
98    |         av_log(ctx, AV_LOG_ERROR, "Invalid block header.\n");
99    |  return ret;
100   |     }
101   |  
102   |  if (wc->header.version < 0x402 || wc->header.version > 0x410) {
103   |         avpriv_report_missing_feature(ctx, "WV version 0x%03X",
104   |                                       wc->header.version);
105   |  return AVERROR_PATCHWELCOME;
106   |     }
107   |  
108   |  /* Blocks with zero samples don't contain actual audio information
109   |  * and should be ignored */
110   |  if (!wc->header.samples)
111   |  return 0;
112   |  // parse flags
113   |     flags  = wc->header.flags;
114   |     rate_x = (flags & WV_DSD) ? 4 : 1;
115   |     bpp    = (flags & WV_DSD) ? 0 : ((flags & 3) + 1) << 3;
116   |     chan   = 1 + !(flags & WV_MONO);
117   |     chmask = flags & WV_MONO ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
118   |     rate   = wv_rates[(flags >> 23) & 0xF];
119   |     wc->multichannel = !(wc->header.initial && wc->header.final);
120   |  if (wc->multichannel) {
121   |         chan   = wc->chan;
122   |         chmask = wc->chmask;
123   |     }
124   |  if ((rate == -1 || !chan || flags & WV_DSD) && !wc->block_parsed) {
125   |         int64_t block_end = avio_tell(pb) + wc->header.blocksize;
126   |  if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
127   |             av_log(ctx, AV_LOG_ERROR,
128   |  "Cannot determine additional parameters\n");
129   |  return AVERROR_INVALIDDATA;
130   |         }
131   |  while (avio_tell(pb) < block_end && !avio_feof(pb)) {
132   |  int id, size;
133   |             id   = avio_r8(pb);
134   |             size = (id & 0x80) ? avio_rl24(pb) : avio_r8(pb);
135   |             size <<= 1;
136   |  if (id & 0x40)
137   |                 size--;
138   |  switch (id & 0x3F) {
139   |  case 0xD:
140   |  if (size <= 1) {
141   |                     av_log(ctx, AV_LOG_ERROR,
229   |     }
230   |  return 0;
231   | }
232   |  
233   | static int wv_read_header(AVFormatContext *s)
234   | {
235   |     AVIOContext *pb = s->pb;
236   |     WVContext *wc = s->priv_data;
237   |     AVStream *st;
238   |  int ret;
239   |  
240   |     wc->block_parsed = 0;
241   |  for (;;) {
242   |  if ((ret = wv_read_block_header(s, pb)) < 0)
243   |  return ret;
244   |  if (!wc->header.samples)
245   |             avio_skip(pb, wc->header.blocksize);
246   |  else
247   |  break;
248   |     }
249   |  
250   |  /* now we are ready: build format streams */
251   |     st = avformat_new_stream(s, NULL);
252   |  if (!st)
253   |  return AVERROR(ENOMEM);
254   |  if ((ret = ff_alloc_extradata(st->codecpar, 2)) < 0)
255   |  return ret;
256   |  AV_WL16(st->codecpar->extradata, wc->header.version);
257   |     st->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
258   |     st->codecpar->codec_id              = AV_CODEC_ID_WAVPACK;
259   |     st->codecpar->channels              = wc->chan;
260   |     st->codecpar->channel_layout        = wc->chmask;
261   |     st->codecpar->sample_rate           = wc->rate;
262   |     st->codecpar->bits_per_coded_sample = wc->bpp;
263   |     avpriv_set_pts_info(st, 64, 1, wc->rate);
264   |     st->start_time = 0;
265   |  if (wc->header.total_samples != 0xFFFFFFFFu)
266   |         st->duration = wc->header.total_samples;
267   |  
268   |  if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
269   |         int64_t cur = avio_tell(s->pb);
270   |         wc->apetag_start = ff_ape_parse_tag(s);
271   |  if (!av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX))
272   |             ff_id3v1_read(s);
273   |         avio_seek(s->pb, cur, SEEK_SET);
274   |     }
275   |  
276   |  return 0;
277   | }
278   |  
279   | static int wv_read_packet(AVFormatContext *s, AVPacket *pkt)
280   | {
281   |  WVContext *wc = s->priv_data;
282   |  int ret;
283   |  int off;
284   |     int64_t pos;
285   |     uint32_t block_samples;
286   |  
287   |  if (avio_feof(s->pb))
    1Assuming the condition is false→
    2←Taking false branch→
288   |  return AVERROR_EOF;
289   |  if (wc->block_parsed) {
    3←Assuming field 'block_parsed' is not equal to 0→
    4←Taking true branch→
290   |  if ((ret = wv_read_block_header(s, s->pb)) < 0)
    5←Taking false branch→
291   |  return ret;
292   |     }
293   |  
294   |  pos = wc->pos;
295   |  if ((ret = av_new_packet(pkt, wc->header.blocksize + WV_HEADER_SIZE)) < 0)
    6←Assuming the condition is false→
    7←Taking false branch→
296   |  return ret;
297   |  memcpy(pkt->data, wc->block_header, WV_HEADER_SIZE);
298   |     ret = avio_read(s->pb, pkt->data + WV_HEADER_SIZE, wc->header.blocksize);
299   |  if (ret != wc->header.blocksize) {
    8←Assuming 'ret' is equal to field 'blocksize'→
    9←Taking false branch→
300   |  return AVERROR(EIO);
301   |     }
302   |  while (!(wc->header.flags & WV_FLAG_FINAL_BLOCK)) {
    10←Assuming the condition is false→
    11←Loop condition is false. Execution continues on line 318→
303   |  if ((ret = wv_read_block_header(s, s->pb)) < 0) {
304   |  return ret;
305   |         }
306   |  
307   |         off = pkt->size;
308   |  if ((ret = av_grow_packet(pkt, WV_HEADER_SIZE + wc->header.blocksize)) < 0) {
309   |  return ret;
310   |         }
311   |         memcpy(pkt->data + off, wc->block_header, WV_HEADER_SIZE);
312   |  
313   |         ret = avio_read(s->pb, pkt->data + off + WV_HEADER_SIZE, wc->header.blocksize);
314   |  if (ret != wc->header.blocksize) {
315   |  return (ret < 0) ? ret : AVERROR_EOF;
316   |         }
317   |     }
318   |  pkt->stream_index = 0;
319   |     pkt->pos          = pos;
320   |     wc->block_parsed  = 1;
321   |     pkt->pts          = wc->header.block_idx;
322   |  block_samples     = wc->header.samples;
    12←buffer read by avio_read may be partially uninitialized
323   |  if (block_samples > INT32_MAX)
324   |         av_log(s, AV_LOG_WARNING,
325   |  "Too many samples in block: %"PRIu32"\n", block_samples);
326   |  else
327   |         pkt->duration = block_samples;
328   |  
329   |  return 0;
330   | }
331   |  
332   | const AVInputFormat ff_wv_demuxer = {
333   |     .name           = "wv",
334   |     .long_name      = NULL_IF_CONFIG_SMALL("WavPack"),
335   |     .priv_data_size = sizeof(WVContext),
336   |     .read_probe     = wv_probe,
337   |     .read_header    = wv_read_header,
338   |     .read_packet    = wv_read_packet,
339   |     .flags          = AVFMT_GENERIC_INDEX,
340   | };