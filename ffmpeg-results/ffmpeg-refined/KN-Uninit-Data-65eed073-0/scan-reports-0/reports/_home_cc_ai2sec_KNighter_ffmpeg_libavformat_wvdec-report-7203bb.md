### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/wvdec.c  
---|---  
Warning:| line 218, column 19  
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
80    |  WVContext *wc = ctx->priv_data;
81    |  int ret;
82    |  int rate, bpp, chan;
83    |     uint32_t chmask, flags;
84    |  unsigned rate_x;
85    |  
86    |     wc->pos = avio_tell(pb);
87    |  
88    |  /* don't return bogus packets with the ape tag data */
89    |  if (wc->apetag_start && wc->pos >= wc->apetag_start)
    3←Assuming field 'apetag_start' is 0→
90    |  return AVERROR_EOF;
91    |  
92    |  ret = avio_read(pb, wc->block_header, WV_HEADER_SIZE);
93    |  if (ret != WV_HEADER_SIZE)
    4←Assuming 'ret' is equal to WV_HEADER_SIZE→
    5←Taking false branch→
94    |  return (ret < 0) ? ret : AVERROR_EOF;
95    |  
96    |  ret = ff_wv_parse_header(&wc->header, wc->block_header);
97    |  if (ret < 0) {
    6←Assuming 'ret' is >= 0→
98    |         av_log(ctx, AV_LOG_ERROR, "Invalid block header.\n");
99    |  return ret;
100   |     }
101   |  
102   |  if (wc->header.version < 0x402 || wc->header.version > 0x410) {
    7←Assuming field 'version' is >= 1026→
    8←Assuming field 'version' is <= 1040→
    9←Taking false branch→
103   |         avpriv_report_missing_feature(ctx, "WV version 0x%03X",
104   |                                       wc->header.version);
105   |  return AVERROR_PATCHWELCOME;
106   |     }
107   |  
108   |  /* Blocks with zero samples don't contain actual audio information
109   |  * and should be ignored */
110   |  if (!wc->header.samples)
    10←Assuming field 'samples' is not equal to 0→
    11←Taking false branch→
111   |  return 0;
112   |  // parse flags
113   |  flags  = wc->header.flags;
114   |  rate_x = (flags & WV_DSD) ? 4 : 1;
    12←Assuming the condition is false→
    13←'?' condition is false→
115   |  bpp    = (flags & WV_DSD) ? 0 : ((flags & 3) + 1) << 3;
    14←'?' condition is false→
116   |  chan   = 1 + !(flags & WV_MONO);
    15←Assuming the condition is false→
117   |  chmask = flags & WV_MONO ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    16←'?' condition is true→
118   |     rate   = wv_rates[(flags >> 23) & 0xF];
119   |  wc->multichannel = !(wc->header.initial && wc->header.final);
    17←Assuming field 'initial' is not equal to 0→
    18←Assuming the condition is false→
120   |  if (wc->multichannel18.1Field 'multichannel' is 0) {
121   |         chan   = wc->chan;
122   |         chmask = wc->chmask;
123   |     }
124   |  if ((rate == -1 || !chan19.1'chan' is 1 || flags & WV_DSD) && !wc->block_parsed) {
    19←Assuming the condition is false→
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
142   |  "Insufficient channel information\n");
143   |  return AVERROR_INVALIDDATA;
144   |                 }
145   |                 chan = avio_r8(pb);
146   |  switch (size - 2) {
147   |  case 0:
148   |                     chmask = avio_r8(pb);
149   |  break;
150   |  case 1:
151   |                     chmask = avio_rl16(pb);
152   |  break;
153   |  case 2:
154   |                     chmask = avio_rl24(pb);
155   |  break;
156   |  case 3:
157   |                     chmask = avio_rl32(pb);
158   |  break;
159   |  case 4:
160   |                     avio_skip(pb, 1);
161   |                     chan  |= (avio_r8(pb) & 0xF) << 8;
162   |                     chan  += 1;
163   |                     chmask = avio_rl24(pb);
164   |  break;
165   |  case 5:
166   |                     avio_skip(pb, 1);
167   |                     chan  |= (avio_r8(pb) & 0xF) << 8;
168   |                     chan  += 1;
169   |                     chmask = avio_rl32(pb);
170   |  break;
171   |  default:
172   |                     av_log(ctx, AV_LOG_ERROR,
173   |  "Invalid channel info size %d\n", size);
174   |  return AVERROR_INVALIDDATA;
175   |                 }
176   |  break;
177   |  case 0xE:
178   |  if (size <= 1) {
179   |                     av_log(ctx, AV_LOG_ERROR,
180   |  "Invalid DSD block\n");
181   |  return AVERROR_INVALIDDATA;
182   |                 }
183   |                 rate_x = 1U << (avio_r8(pb) & 0x1f);
184   |  if (size)
185   |                     avio_skip(pb, size-1);
186   |  break;
187   |  case 0x27:
188   |                 rate = avio_rl24(pb);
189   |  break;
190   |  default:
191   |                 avio_skip(pb, size);
192   |             }
193   |  if (id & 0x40)
194   |                 avio_skip(pb, 1);
195   |         }
196   |  if (rate == -1 || rate * (uint64_t)rate_x >= INT_MAX) {
197   |             av_log(ctx, AV_LOG_ERROR,
198   |  "Cannot determine custom sampling rate\n");
199   |  return AVERROR_INVALIDDATA;
200   |         }
201   |         avio_seek(pb, block_end - wc->header.blocksize, SEEK_SET);
202   |     }
203   |  if (!wc->bpp)
    20←Assuming field 'bpp' is not equal to 0→
    21←Taking false branch→
204   |         wc->bpp    = bpp;
205   |  if (!wc->chan)
    22←Assuming field 'chan' is not equal to 0→
    23←Taking false branch→
206   |         wc->chan   = chan;
207   |  if (!wc->chmask)
    24←Assuming field 'chmask' is not equal to 0→
    25←Taking false branch→
208   |         wc->chmask = chmask;
209   |  if (!wc->rate)
    26←Assuming field 'rate' is not equal to 0→
210   |         wc->rate   = rate * rate_x;
211   |  
212   |  if (flags && bpp != wc->bpp) {
    27←Assuming 'flags' is not equal to 0→
    28←Assuming 'bpp' is equal to field 'bpp'→
213   |         av_log(ctx, AV_LOG_ERROR,
214   |  "Bits per sample differ, this block: %i, header block: %i\n",
215   |                bpp, wc->bpp);
216   |  return AVERROR_INVALIDDATA;
217   |     }
218   |  if (flags28.1'flags' is not equal to 0 && !wc->multichannel && chan != wc->chan) {
    29←buffer read by avio_read may be partially uninitialized
219   |         av_log(ctx, AV_LOG_ERROR,
220   |  "Channels differ, this block: %i, header block: %i\n",
221   |                chan, wc->chan);
222   |  return AVERROR_INVALIDDATA;
223   |     }
224   |  if (flags && rate != -1 && !(flags & WV_DSD) && rate * rate_x != wc->rate) {
225   |         av_log(ctx, AV_LOG_ERROR,
226   |  "Sampling rate differ, this block: %i, header block: %i\n",
227   |                rate * rate_x, wc->rate);
228   |  return AVERROR_INVALIDDATA;
229   |     }
230   |  return 0;
231   | }
232   |  
233   | static int wv_read_header(AVFormatContext *s)
234   | {
235   |  AVIOContext *pb = s->pb;
236   |     WVContext *wc = s->priv_data;
237   |     AVStream *st;
238   |  int ret;
239   |  
240   |     wc->block_parsed = 0;
241   |  for (;;) {
    1Loop condition is true.  Entering loop body→
242   |  if ((ret = wv_read_block_header(s, pb)) < 0)
    2←Calling 'wv_read_block_header'→
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