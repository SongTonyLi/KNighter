### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/dv.c  
---|---  
Warning:| line 267, column 45  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


181   |  for (j = 0; j < 9; j++) {
182   |  for (d = 8; d < 80; d += 2) {
183   |  if (quant == 0) {  /* 16-bit quantization */
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
197   |                         lc = ((uint16_t)frame[d]     << 4) |
198   |                              ((uint16_t)frame[d + 2] >> 4);
199   |                         rc = ((uint16_t)frame[d + 1] << 4) |
200   |                              ((uint16_t)frame[d + 2] & 0x0f);
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
237   |  if (!as_pack || !c->sys14.1Field 'sys' is non-null) {    /* No audio ? */
    14←Assuming 'as_pack' is non-null→
    15←Taking false branch→
238   |         c->ach = 0;
239   |  return 0;
240   |     }
241   |  
242   |  smpls = as_pack[1]      & 0x3f; /* samples in this frame - min. samples */
243   |     freq  = as_pack[4] >> 3 & 0x07; /* 0 - 48kHz, 1 - 44,1kHz, 2 - 32kHz */
244   |     stype = as_pack[3]      & 0x1f; /* 0 - 2CH, 2 - 4CH, 3 - 8CH */
245   |     quant = as_pack[4]      & 0x07; /* 0 - 16-bit linear, 1 - 12-bit nonlinear */
246   |  
247   |  if (freq >= FF_ARRAY_ELEMS(dv_audio_frequency)) {
    16←Assuming the condition is false→
    17←Taking false branch→
248   |         av_log(c->fctx, AV_LOG_ERROR,
249   |  "Unrecognized audio sample rate index (%d)\n", freq);
250   |  return 0;
251   |     }
252   |  
253   |  if (stype > 3) {
    18←Assuming 'stype' is <= 3→
    19←Taking false branch→
254   |         av_log(c->fctx, AV_LOG_ERROR, "stype %d is invalid\n", stype);
255   |         c->ach = 0;
256   |  return 0;
257   |     }
258   |  
259   |  /* note: ach counts PAIRS of channels (i.e. stereo channels) */
260   |  ach = ((int[4]) { 1, 0, 2, 4 })[stype];
261   |  if (ach == 1 && quant && freq == 2)
    20←Assuming 'ach' is not equal to 1→
262   |         ach = 2;
263   |  
264   |  /* Dynamic handling of the audio streams in DV */
265   |  for (i = 0; i < ach; i++) {
    21←Assuming 'i' is < 'ach'→
    22←Loop condition is true.  Entering loop body→
266   |  if (!c->ast[i]) {
    23←Assuming the condition is true→
    24←Taking true branch→
267   |  c->ast[i] = avformat_new_stream(c->fctx, NULL);
    25←buffer read by avio_read may be partially uninitialized
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
281   |         c->ast[i]->codecpar->sample_rate    = dv_audio_frequency[freq];
282   |         c->ast[i]->codecpar->channels       = 2;
283   |         c->ast[i]->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
284   |         c->ast[i]->codecpar->bit_rate       = 2 * dv_audio_frequency[freq] * 16;
285   |         c->ast[i]->start_time            = 0;
286   |     }
287   |     c->ach = i;
288   |  
289   |  return (c->sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */
290   | }
291   |  
292   | static int dv_extract_video_info(DVDemuxContext *c, const uint8_t *frame)
293   | {
294   |  const uint8_t *vsc_pack;
295   |     AVCodecParameters *par;
296   |  int apt, is16_9;
297   |  
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
    13←Calling 'dv_extract_audio_info'→
403   |  for (i = 0; i < c->ach; i++) {
404   |         c->audio_pkt[i].pos  = pos;
405   |         c->audio_pkt[i].size = size;
406   |         c->audio_pkt[i].pts  = (c->sys->height == 720) ? (c->frames & ~1) : c->frames;
407   |         ppcm[i] = c->audio_buf[i];
408   |     }
409   |  if (c->ach)
410   |         dv_extract_audio(buf, ppcm, c->sys);
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