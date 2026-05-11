### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/dv.c  
---|---  
Warning:| line 200, column 41  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


34    | #include "libavcodec/dv_profile.h"
35    | #include "libavcodec/dv.h"
36    | #include "libavutil/channel_layout.h"
37    | #include "libavutil/intreadwrite.h"
38    | #include "libavutil/mathematics.h"
39    | #include "libavutil/timecode.h"
40    | #include "dv.h"
41    | #include "libavutil/avassert.h"
42    |  
43    | // Must be kept in sync with AVPacket
44    | struct DVPacket {
45    |     int64_t  pts;
46    |     uint8_t *data;
47    |  int      size;
48    |  int      stream_index;
49    |  int      flags;
50    |     int64_t  pos;
51    | };
52    |  
53    | struct DVDemuxContext {
54    |  const AVDVProfile*  sys;    /* Current DV profile. E.g.: 525/60, 625/50 */
55    |     AVFormatContext*  fctx;
56    |     AVStream*         vst;
57    |     AVStream*         ast[4];
58    |  struct DVPacket   audio_pkt[4];
59    |     uint8_t           audio_buf[4][8192];
60    |  int               ach;
61    |  int               frames;
62    | };
63    |  
64    | static inline uint16_t dv_audio_12to16(uint16_t sample)
65    | {
66    |     uint16_t shift, result;
67    |  
68    |     sample = (sample < 0x800) ? sample : sample | 0xf000;
69    |     shift  = (sample & 0xf00) >> 8;
70    |  
71    |  if (shift < 0x2 || shift > 0xd) {
72    |         result = sample;
73    |     } else if (shift < 0x8) {
74    |         shift--;
75    |         result = (sample - (256 * shift)) << shift;
76    |     } else {
77    |         shift  = 0xe - shift;
78    |         result = ((sample + ((256 * shift) + 1)) << shift) - 1;
79    |     }
80    |  
81    |  return result;
82    | }
83    |  
84    | static const uint8_t *dv_extract_pack(const uint8_t *frame, enum dv_pack_type t)
85    | {
86    |  int offs;
87    |  int c;
88    |  
89    |  for (c = 0; c < 10; c++) {
90    |  switch (t) {
91    |  case dv_audio_source:
92    |  if (c&1)    offs = (80 * 6 + 80 * 16 * 0 + 3 + c*12000);
93    |  else        offs = (80 * 6 + 80 * 16 * 3 + 3 + c*12000);
94    |  break;
95    |  case dv_audio_control:
96    |  if (c&1)    offs = (80 * 6 + 80 * 16 * 1 + 3 + c*12000);
97    |  else        offs = (80 * 6 + 80 * 16 * 4 + 3 + c*12000);
98    |  break;
99    |  case dv_video_control:
100   |  if (c&1)    offs = (80 * 3 + 8      + c*12000);
101   |  else        offs = (80 * 5 + 48 + 5 + c*12000);
102   |  break;
103   |  case dv_timecode:
104   |             offs = (80*1 + 3 + 3);
105   |  break;
106   |  default:
107   |  return NULL;
108   |         }
109   |  if (frame[offs] == t)
110   |  break;
111   |     }
112   |  
113   |  return frame[offs] == t ? &frame[offs] : NULL;
114   | }
115   |  
116   | static const int dv_audio_frequency[3] = {
117   |     48000, 44100, 32000,
118   | };
119   |  
120   | /*
121   |  * There's a couple of assumptions being made here:
122   |  * 1. By default we silence erroneous (0x8000/16-bit 0x800/12-bit) audio samples.
123   |  *    We can pass them upwards when libavcodec will be ready to deal with them.
124   |  * 2. We don't do software emphasis.
125   |  * 3. Audio is always returned as 16-bit linear samples: 12-bit nonlinear samples
126   |  *    are converted into 16-bit linear ones.
127   |  */
128   | static int dv_extract_audio(const uint8_t *frame, uint8_t **ppcm,
129   |  const AVDVProfile *sys)
130   | {
131   |  int size, chan, i, j, d, of, smpls, freq, quant, half_ch;
132   |     uint16_t lc, rc;
133   |  const uint8_t *as_pack;
134   |     uint8_t *pcm, ipcm;
135   |  
136   |     as_pack = dv_extract_pack(frame, dv_audio_source);
137   |  if (!as_pack)    /* No audio ? */
    21←Assuming 'as_pack' is non-null→
    22←Taking false branch→
138   |  return 0;
139   |  
140   |  smpls = as_pack[1]      & 0x3f; /* samples in this frame - min. samples */
141   |     freq  = as_pack[4] >> 3 & 0x07; /* 0 - 48kHz, 1 - 44,1kHz, 2 - 32kHz */
142   |     quant = as_pack[4]      & 0x07; /* 0 - 16-bit linear, 1 - 12-bit nonlinear */
143   |  
144   |  if (quant > 1)
    23←Assuming 'quant' is <= 1→
    24←Taking false branch→
145   |  return -1;  /* unsupported quantization */
146   |  
147   |  if (freq >= FF_ARRAY_ELEMS(dv_audio_frequency))
    25←Assuming the condition is false→
    26←Taking false branch→
148   |  return AVERROR_INVALIDDATA;
149   |  
150   |  size    = (sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */
151   |     half_ch = sys->difseg_size / 2;
152   |  
153   |  /* We work with 720p frames split in half, thus even frames have
154   |  * channels 0,1 and odd 2,3. */
155   |  ipcm = (sys->height == 720 && !(frame[1] & 0x0C)) ? 2 : 0;
    27←Assuming field 'height' is not equal to 720→
156   |  
157   |  if (ipcm + sys->n_difchan > (quant == 1 ? 2 : 4)) {
    28←Assuming 'quant' is equal to 1→
    29←'?' condition is true→
    30←Assuming the condition is false→
    31←Taking false branch→
158   |         av_log(NULL, AV_LOG_ERROR, "too many dv pcm frames\n");
159   |  return AVERROR_INVALIDDATA;
160   |     }
161   |  
162   |  /* for each DIF channel */
163   |  for (chan = 0; chan < sys->n_difchan; chan++) {
    32←Assuming 'chan' is < field 'n_difchan'→
    33←Loop condition is true.  Entering loop body→
164   |  av_assert0(ipcm<4);
    34←Taking false branch→
    35←Loop condition is false.  Exiting loop→
165   |  pcm = ppcm[ipcm++];
166   |  if (!pcm35.1'pcm' is non-null)
    36←Taking false branch→
167   |  break;
168   |  
169   |  /* for each DIF segment */
170   |  for (i = 0; i < sys->difseg_size; i++) {
    37←Assuming 'i' is < field 'difseg_size'→
    38←Loop condition is true.  Entering loop body→
171   |  frame += 6 * 80; /* skip DIF segment header */
172   |  if (quant38.1'quant' is equal to 1 == 1 && i == half_ch) {
    39←Assuming 'i' is not equal to 'half_ch'→
    40←Taking false branch→
173   |  /* next stereo channel (12-bit mode only) */
174   |  av_assert0(ipcm<4);
175   |                 pcm = ppcm[ipcm++];
176   |  if (!pcm)
177   |  break;
178   |             }
179   |  
180   |  /* for each AV sequence */
181   |  for (j = 0; j < 9; j++) {
    41←Loop condition is true.  Entering loop body→
182   |  for (d = 8; d < 80; d += 2) {
    42←Loop condition is true.  Entering loop body→
183   |  if (quant42.1'quant' is not equal to 0 == 0) {  /* 16-bit quantization */
    43←Taking false branch→
184   |                         of = sys->audio_shuffle[i][j] +
185   |                              (d - 8) / 2 * sys->audio_stride;
186   |  if (of * 2 >= size)
187   |  continue;
188   |  
189   |  /* FIXME: maybe we have to admit that DV is a
190   |  * big-endian PCM */
191   |                         pcm[of * 2]     = frame[d + 1];
192   |                         pcm[of * 2 + 1] = frame[d];
193   |  
194   |  if (pcm[of * 2 + 1] == 0x80 && pcm[of * 2] == 0x00)
195   |                             pcm[of * 2 + 1] = 0;
196   |                     } else {           /* 12-bit quantization */
197   |  lc = ((uint16_t)frame[d]     << 4) |
198   |                              ((uint16_t)frame[d + 2] >> 4);
199   |  rc = ((uint16_t)frame[d + 1] << 4) |
200   |                              ((uint16_t)frame[d + 2] & 0x0f);
    44←buffer read by avio_read may be partially uninitialized
201   |                         lc = (lc == 0x800 ? 0 : dv_audio_12to16(lc));
202   |                         rc = (rc == 0x800 ? 0 : dv_audio_12to16(rc));
203   |  
204   |                         of = sys->audio_shuffle[i % half_ch][j] +
205   |                              (d - 8) / 3 * sys->audio_stride;
206   |  if (of * 2 >= size)
207   |  continue;
208   |  
209   |  /* FIXME: maybe we have to admit that DV is a
210   |  * big-endian PCM */
211   |                         pcm[of * 2]     = lc & 0xff;
212   |                         pcm[of * 2 + 1] = lc >> 8;
213   |                         of = sys->audio_shuffle[i % half_ch + half_ch][j] +
214   |                              (d - 8) / 3 * sys->audio_stride;
215   |  /* FIXME: maybe we have to admit that DV is a
216   |  * big-endian PCM */
217   |                         pcm[of * 2]     = rc & 0xff;
218   |                         pcm[of * 2 + 1] = rc >> 8;
219   |                         ++d;
220   |                     }
221   |                 }
222   |  
223   |                 frame += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
224   |             }
225   |         }
226   |     }
227   |  
228   |  return size;
229   | }
230   |  
231   | static int dv_extract_audio_info(DVDemuxContext *c, const uint8_t *frame)
232   | {
233   |  const uint8_t *as_pack;
234   |  int freq, stype, smpls, quant, i, ach;
235   |  
236   |     as_pack = dv_extract_pack(frame, dv_audio_source);
237   |  if (!as_pack || !c->sys) {    /* No audio ? */
238   |         c->ach = 0;
239   |  return 0;
240   |     }
241   |  
242   |     smpls = as_pack[1]      & 0x3f; /* samples in this frame - min. samples */
243   |     freq  = as_pack[4] >> 3 & 0x07; /* 0 - 48kHz, 1 - 44,1kHz, 2 - 32kHz */
244   |     stype = as_pack[3]      & 0x1f; /* 0 - 2CH, 2 - 4CH, 3 - 8CH */
245   |     quant = as_pack[4]      & 0x07; /* 0 - 16-bit linear, 1 - 12-bit nonlinear */
246   |  
247   |  if (freq >= FF_ARRAY_ELEMS(dv_audio_frequency)) {
248   |         av_log(c->fctx, AV_LOG_ERROR,
249   |  "Unrecognized audio sample rate index (%d)\n", freq);
250   |  return 0;
251   |     }
252   |  
253   |  if (stype > 3) {
254   |         av_log(c->fctx, AV_LOG_ERROR, "stype %d is invalid\n", stype);
255   |         c->ach = 0;
256   |  return 0;
257   |     }
258   |  
259   |  /* note: ach counts PAIRS of channels (i.e. stereo channels) */
260   |     ach = ((int[4]) { 1, 0, 2, 4 })[stype];
261   |  if (ach == 1 && quant && freq == 2)
262   |         ach = 2;
263   |  
264   |  /* Dynamic handling of the audio streams in DV */
265   |  for (i = 0; i < ach; i++) {
266   |  if (!c->ast[i]) {
267   |             c->ast[i] = avformat_new_stream(c->fctx, NULL);
268   |  if (!c->ast[i])
269   |  break;
270   |             avpriv_set_pts_info(c->ast[i], 64, c->sys->time_base.num, c->sys->time_base.den);
271   |             c->ast[i]->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
272   |             c->ast[i]->codecpar->codec_id   = AV_CODEC_ID_PCM_S16LE;
273   |  
274   |             c->audio_pkt[i].size         = 0;
275   |             c->audio_pkt[i].data         = c->audio_buf[i];
276   |             c->audio_pkt[i].stream_index = c->ast[i]->index;
277   |             c->audio_pkt[i].flags       |= AV_PKT_FLAG_KEY;
278   |             c->audio_pkt[i].pts          = AV_NOPTS_VALUE;
279   |             c->audio_pkt[i].pos          = -1;
280   |         }
315   |  
316   | static int dv_extract_timecode(DVDemuxContext* c, const uint8_t* frame, char *tc)
317   | {
318   |  const uint8_t *tc_pack;
319   |  
320   |  // For PAL systems, drop frame bit is replaced by an arbitrary
321   |  // bit so its value should not be considered. Drop frame timecode
322   |  // is only relevant for NTSC systems.
323   |  int prevent_df = c->sys->ltc_divisor == 25 || c->sys->ltc_divisor == 50;
324   |  
325   |     tc_pack = dv_extract_pack(frame, dv_timecode);
326   |  if (!tc_pack)
327   |  return 0;
328   |     av_timecode_make_smpte_tc_string2(tc, av_inv_q(c->sys->time_base), AV_RB32(tc_pack + 1), prevent_df, 1);
329   |  return 1;
330   | }
331   |  
332   | /* The following 3 functions constitute our interface to the world */
333   |  
334   | static int dv_init_demux(AVFormatContext *s, DVDemuxContext *c)
335   | {
336   |     c->vst = avformat_new_stream(s, NULL);
337   |  if (!c->vst)
338   |  return AVERROR(ENOMEM);
339   |  
340   |     c->fctx                   = s;
341   |     c->vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
342   |     c->vst->codecpar->codec_id   = AV_CODEC_ID_DVVIDEO;
343   |     c->vst->codecpar->bit_rate   = 25000000;
344   |     c->vst->start_time        = 0;
345   |  
346   |  return 0;
347   | }
348   |  
349   | DVDemuxContext *avpriv_dv_init_demux(AVFormatContext *s)
350   | {
351   |     DVDemuxContext *c;
352   |  
353   |     c = av_mallocz(sizeof(DVDemuxContext));
354   |  if (!c)
355   |  return NULL;
356   |  
357   |  if (dv_init_demux(s, c)) {
358   |         av_free(c);
359   |  return NULL;
360   |     }
361   |  
362   |  return c;
363   | }
364   |  
365   | int avpriv_dv_get_packet(DVDemuxContext *c, AVPacket *pkt)
366   | {
367   |  int size = -1;
368   |  int i;
369   |  
370   |  for (i = 0; i < c->ach; i++) {
371   |  if (c->ast[i] && c->audio_pkt[i].size) {
372   |             pkt->size         = c->audio_pkt[i].size;
373   |             pkt->data         = c->audio_pkt[i].data;
374   |             pkt->stream_index = c->audio_pkt[i].stream_index;
375   |             pkt->flags        = c->audio_pkt[i].flags;
376   |             pkt->pts          = c->audio_pkt[i].pts;
377   |             pkt->pos          = c->audio_pkt[i].pos;
378   |  
379   |             c->audio_pkt[i].size = 0;
380   |             size                 = pkt->size;
381   |  break;
382   |         }
383   |     }
384   |  
385   |  return size;
386   | }
387   |  
388   | int avpriv_dv_produce_packet(DVDemuxContext *c, AVPacket *pkt,
389   |                              uint8_t *buf, int buf_size, int64_t pos)
390   | {
391   |  int size, i;
392   |     uint8_t *ppcm[5] = { 0 };
393   |  
394   |  if (buf_size < DV_PROFILE_BYTES ||
    9←Assuming the condition is false→
    12←Taking false branch→
395   |  !(c->sys = av_dv_frame_profile(c->sys, buf, buf_size)) ||
    10←Assuming field 'sys' is non-null→
396   |  buf_size < c->sys->frame_size) {
    11←Assuming 'buf_size' is >= field 'frame_size'→
397   |  return -1;   /* Broken frame, or not enough data */
398   |     }
399   |  
400   |  /* Queueing audio packet */
401   |  /* FIXME: in case of no audio/bad audio we have to do something */
402   |  size = dv_extract_audio_info(c, buf);
403   |  for (i = 0; i < c->ach; i++) {
    13←Assuming 'i' is < field 'ach'→
    14←Loop condition is true.  Entering loop body→
    17←Assuming 'i' is >= field 'ach'→
    18←Loop condition is false. Execution continues on line 409→
404   |  c->audio_pkt[i].pos  = pos;
405   |         c->audio_pkt[i].size = size;
406   |  c->audio_pkt[i].pts  = (c->sys->height == 720) ? (c->frames & ~1) : c->frames;
    15←Assuming field 'height' is not equal to 720→
    16←'?' condition is false→
407   |  ppcm[i] = c->audio_buf[i];
408   |  }
409   |  if (c->ach18.1Field 'ach' is not equal to 0)
    19←Taking true branch→
410   |  dv_extract_audio(buf, ppcm, c->sys);
    20←Calling 'dv_extract_audio'→
411   |  
412   |  /* We work with 720p frames split in half, thus even frames have
413   |  * channels 0,1 and odd 2,3. */
414   |  if (c->sys->height == 720) {
415   |  if (buf[1] & 0x0C) {
416   |             c->audio_pkt[2].size = c->audio_pkt[3].size = 0;
417   |         } else {
418   |             c->audio_pkt[0].size = c->audio_pkt[1].size = 0;
419   |         }
420   |     }
421   |  
422   |  /* Now it's time to return video packet */
423   |     size = dv_extract_video_info(c, buf);
424   |     pkt->data         = buf;
425   |     pkt->pos          = pos;
426   |     pkt->size         = size;
427   |     pkt->flags       |= AV_PKT_FLAG_KEY;
428   |     pkt->stream_index = c->vst->index;
429   |     pkt->pts          = c->frames;
430   |  
431   |     c->frames++;
432   |  
433   |  return size;
434   | }
435   |  
436   | static int64_t dv_frame_offset(AVFormatContext *s, DVDemuxContext *c,
437   |                                int64_t timestamp, int flags)
438   | {
439   |  // FIXME: sys may be wrong if last dv_read_packet() failed (buffer is junk)
440   |  const int frame_size = c->sys->frame_size;
501   | static int dv_read_header(AVFormatContext *s)
502   | {
503   |  unsigned state, marker_pos = 0;
504   |     RawDVContext *c = s->priv_data;
505   |  int ret;
506   |  
507   |  if ((ret = dv_init_demux(s, &c->dv_demux)) < 0)
508   |  return ret;
509   |  
510   |     state = avio_rb32(s->pb);
511   |  while ((state & 0xffffff7f) != 0x1f07003f) {
512   |  if (avio_feof(s->pb)) {
513   |             av_log(s, AV_LOG_ERROR, "Cannot find DV header.\n");
514   |  return AVERROR_INVALIDDATA;
515   |         }
516   |  if (state == 0x003f0700 || state == 0xff3f0700)
517   |             marker_pos = avio_tell(s->pb);
518   |  if (state == 0xff3f0701 && avio_tell(s->pb) - marker_pos == 80) {
519   |             avio_seek(s->pb, -163, SEEK_CUR);
520   |             state = avio_rb32(s->pb);
521   |  break;
522   |         }
523   |         state = (state << 8) | avio_r8(s->pb);
524   |     }
525   |  AV_WB32(c->buf, state);
526   |  
527   |  if (avio_read(s->pb, c->buf + 4, DV_PROFILE_BYTES - 4) != DV_PROFILE_BYTES - 4 ||
528   |         avio_seek(s->pb, -DV_PROFILE_BYTES, SEEK_CUR) < 0) {
529   |  return AVERROR(EIO);
530   |     }
531   |  
532   |     c->dv_demux.sys = av_dv_frame_profile(c->dv_demux.sys,
533   |                                            c->buf,
534   |  DV_PROFILE_BYTES);
535   |  if (!c->dv_demux.sys) {
536   |         av_log(s, AV_LOG_ERROR,
537   |  "Can't determine profile of DV input stream.\n");
538   |  return AVERROR_INVALIDDATA;
539   |     }
540   |  
541   |     s->bit_rate = av_rescale_q(c->dv_demux.sys->frame_size,
542   |                                (AVRational) { 8, 1 },
543   |                                c->dv_demux.sys->time_base);
544   |  
545   |  if (s->pb->seekable & AVIO_SEEKABLE_NORMAL)
546   |         dv_read_timecode(s);
547   |  
548   |  return 0;
549   | }
550   |  
551   | static int dv_read_packet(AVFormatContext *s, AVPacket *pkt)
552   | {
553   |  int size;
554   |     RawDVContext *c = s->priv_data;
555   |  
556   |     size = avpriv_dv_get_packet(&c->dv_demux, pkt);
557   |  
558   |  if (size0.1'size' is < 0 < 0) {
    1Taking true branch→
559   |  int ret;
560   |         int64_t pos = avio_tell(s->pb);
561   |  if (!c->dv_demux.sys)
    2←Assuming field 'sys' is non-null→
    3←Taking false branch→
562   |  return AVERROR(EIO);
563   |  size = c->dv_demux.sys->frame_size;
564   |         ret = avio_read(s->pb, c->buf, size);
565   |  if (ret < 0) {
    4←Assuming 'ret' is >= 0→
    5←Taking false branch→
566   |  return ret;
567   |         } else if (ret == 0) {
    6←Assuming 'ret' is not equal to 0→
    7←Taking false branch→
568   |  return AVERROR(EIO);
569   |         }
570   |  
571   |  size = avpriv_dv_produce_packet(&c->dv_demux, pkt, c->buf, size, pos);
    8←Calling 'avpriv_dv_produce_packet'→
572   |     }
573   |  
574   |  return size;
575   | }
576   |  
577   | static int dv_read_seek(AVFormatContext *s, int stream_index,
578   |                         int64_t timestamp, int flags)
579   | {
580   |     RawDVContext *r   = s->priv_data;
581   |     DVDemuxContext *c = &r->dv_demux;
582   |     int64_t offset    = dv_frame_offset(s, c, timestamp, flags);
583   |  
584   |  if (avio_seek(s->pb, offset, SEEK_SET) < 0)
585   |  return -1;
586   |  
587   |     ff_dv_offset_reset(c, offset / c->sys->frame_size);
588   |  return 0;
589   | }
590   |  
591   | static int dv_probe(const AVProbeData *p)
592   | {
593   |  unsigned marker_pos = 0;
594   |  int i;
595   |  int matches           = 0;
596   |  int firstmatch        = 0;
597   |  int secondary_matches = 0;
598   |  
599   |  if (p->buf_size < 5)
600   |  return 0;
601   |  