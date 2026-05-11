### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/sga.c  
---|---  
Warning:| line 432, column 22  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


180   | static int sga_read_header(AVFormatContext *s)
181   | {
182   |     SGADemuxContext *sga = s->priv_data;
183   |     AVIOContext *pb = s->pb;
184   |  
185   |     sga->sector_headers = 1;
186   |     sga->first_audio_size = 0;
187   |     sga->video_stream_index = -1;
188   |     sga->audio_stream_index = -1;
189   |     sga->left = 2048;
190   |     sga->idx = 0;
191   |  
192   |     s->ctx_flags |= AVFMTCTX_NOHEADER;
193   |  
194   |  if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
195   |  while (!avio_feof(pb)) {
196   |  int header = avio_rb16(pb);
197   |  int type = header >> 8;
198   |  int skip = 2046;
199   |  int clock;
200   |  
201   |  if (!sga->first_audio_size &&
202   |                 (type == 0xAA ||
203   |                  type == 0xA1 ||
204   |                  type == 0xA2 ||
205   |                  type == 0xA3)) {
206   |                 sga->first_audio_size = avio_rb16(pb);
207   |                 avio_skip(pb, 4);
208   |                 clock = avio_rb16(pb);
209   |                 sga->sample_rate = av_rescale(clock,
210   |  SEGA_CD_PCM_NUM,
211   |  SEGA_CD_PCM_DEN);
212   |                 skip -= 8;
213   |             }
214   |  if ((header > 0x07FE && header < 0x8100) ||
215   |                 (header > 0x8200 && header < 0xA100) ||
216   |                 (header > 0xA200 && header < 0xC100)) {
217   |                 sga->sector_headers = 0;
218   |  break;
219   |             }
220   |  
221   |             avio_skip(pb, skip);
222   |         }
223   |  
224   |         avio_seek(pb, 0, SEEK_SET);
225   |     }
226   |  
227   |  return 0;
228   | }
229   |  
230   | static void print_stats(AVFormatContext *s, const char *where)
231   | {
232   |     SGADemuxContext *sga = s->priv_data;
233   |  
234   |     av_log(s, AV_LOG_DEBUG, "START %s\n", where);
235   |     av_log(s, AV_LOG_DEBUG, "pos: %"PRIX64"\n", avio_tell(s->pb));
236   |     av_log(s, AV_LOG_DEBUG, "idx: %X\n", sga->idx);
237   |     av_log(s, AV_LOG_DEBUG, "packet_type: %X\n", sga->packet_type);
238   |     av_log(s, AV_LOG_DEBUG, "payload_size: %X\n", sga->payload_size);
239   |     av_log(s, AV_LOG_DEBUG, "SECTOR: %016"PRIX64"\n", AV_RB64(sga->sector));
240   |     av_log(s, AV_LOG_DEBUG, "stream: %X\n", sga->sector[1]);
241   |     av_log(s, AV_LOG_DEBUG, "END %s\n", where);
242   | }
243   |  
244   | static void update_type_size(AVFormatContext *s)
245   | {
246   |     SGADemuxContext *sga = s->priv_data;
247   |  
248   |  if (sga->idx >= 4) {
249   |         sga->packet_type  = sga->sector[0];
250   |         sga->payload_size = AV_RB16(sga->sector + 2);
251   |     } else {
252   |         sga->packet_type  = 0;
253   |         sga->payload_size = 0;
254   |     }
255   | }
256   |  
257   | static int sga_video_packet(AVFormatContext *s, AVPacket *pkt)
258   | {
259   |     SGADemuxContext *sga = s->priv_data;
260   |  int ret;
261   |  
262   |  if (sga->payload_size <= 8)
263   |  return AVERROR_INVALIDDATA;
264   |  
265   |  if (sga->video_stream_index == -1) {
266   |         AVRational frame_rate;
267   |  
268   |         AVStream *st = avformat_new_stream(s, NULL);
269   |  if (!st)
270   |  return AVERROR(ENOMEM);
271   |  
272   |         st->start_time              = 0;
273   |         st->codecpar->codec_type    = AVMEDIA_TYPE_VIDEO;
274   |         st->codecpar->codec_tag     = 0;
275   |         st->codecpar->codec_id      = AV_CODEC_ID_SGA_VIDEO;
276   |         sga->video_stream_index     = st->index;
277   |  
278   |  if (sga->first_audio_size > 0 && sga->sample_rate > 0) {
279   |             frame_rate.num = sga->sample_rate;
280   |             frame_rate.den = sga->first_audio_size;
281   |         } else {
282   |             frame_rate.num = 15;
283   |             frame_rate.den = 1;
337   |  return AVERROR(ENOMEM);
338   |     memcpy(pkt->data, sga->sector + 12, sga->payload_size - 8);
339   |  av_assert0(sga->idx >= sga->payload_size + 4);
340   |     memmove(sga->sector, sga->sector + sga->payload_size + 4, sga->idx - sga->payload_size - 4);
341   |  
342   |     pkt->stream_index = sga->audio_stream_index;
343   |     pkt->duration = pkt->size;
344   |     pkt->pos = sga->pkt_pos;
345   |     pkt->flags |= sga->flags;
346   |     sga->idx -= sga->payload_size + 4;
347   |     sga->flags = 0;
348   |     update_type_size(s);
349   |  
350   |     av_log(s, AV_LOG_DEBUG, "AUDIO PACKET: %d:%016"PRIX64" i:%X\n", pkt->size, AV_RB64(sga->sector), sga->idx);
351   |  
352   |  return 0;
353   | }
354   |  
355   | static int sga_packet(AVFormatContext *s, AVPacket *pkt)
356   | {
357   |     SGADemuxContext *sga = s->priv_data;
358   |  int ret = 0;
359   |  
360   |  if (sga->packet_type == 0xCD ||
361   |         sga->packet_type == 0xCB ||
362   |         sga->packet_type == 0xC9 ||
363   |         sga->packet_type == 0xC8 ||
364   |         sga->packet_type == 0xC7 ||
365   |         sga->packet_type == 0xC6 ||
366   |         sga->packet_type == 0xC1 ||
367   |         sga->packet_type == 0xE7) {
368   |         ret = sga_video_packet(s, pkt);
369   |     } else if (sga->packet_type == 0xA1 ||
370   |                sga->packet_type == 0xA2 ||
371   |                sga->packet_type == 0xA3 ||
372   |                sga->packet_type == 0xAA) {
373   |         ret = sga_audio_packet(s, pkt);
374   |     } else {
375   |  if (sga->idx == 0)
376   |  return AVERROR_EOF;
377   |  if (sga->sector[0])
378   |  return AVERROR_INVALIDDATA;
379   |         memmove(sga->sector, sga->sector + 1, sga->idx - 1);
380   |         sga->idx--;
381   |  return AVERROR(EAGAIN);
382   |     }
383   |  
384   |  return ret;
385   | }
386   |  
387   | static int try_packet(AVFormatContext *s, AVPacket *pkt)
388   | {
389   |     SGADemuxContext *sga = s->priv_data;
390   |  int ret = AVERROR(EAGAIN);
391   |  
392   |     update_type_size(s);
393   |  if (sga->idx >= sga->payload_size + 4) {
394   |         print_stats(s, "before sga_packet");
395   |         ret = sga_packet(s, pkt);
396   |         print_stats(s,  "after sga_packet");
397   |  if (ret != AVERROR(EAGAIN))
398   |  return ret;
399   |     }
400   |  
401   |  return sga->idx < sga->payload_size + 4 ? AVERROR(EAGAIN) : ret;
402   | }
403   |  
404   | static int sga_read_packet(AVFormatContext *s, AVPacket *pkt)
405   | {
406   |  SGADemuxContext *sga = s->priv_data;
407   |     AVIOContext *pb = s->pb;
408   |  int header, ret = 0;
409   |  
410   |  sga->pkt_pos = avio_tell(pb);
411   |  
412   | retry:
413   |  update_type_size(s);
414   |  
415   |  print_stats(s, "start");
416   |  if (avio_feof(pb) &&
    1Assuming the condition is false→
    13←Assuming the condition is false→
417   |         (!sga->payload_size || sga->idx < sga->payload_size + 4))
418   |  return AVERROR_EOF;
419   |  
420   |  if (sga->idx < sga->payload_size + 4) {
    2←Taking true branch→
    14←Assuming the condition is true→
    15←Taking true branch→
421   |  ret = ffio_ensure_seekback(pb, 2);
422   |  if (ret < 0)
    3←Assuming 'ret' is >= 0→
    4←Taking false branch→
    16←Assuming 'ret' is >= 0→
    17←Taking false branch→
423   |  return ret;
424   |  
425   |  print_stats(s, "before read header");
426   |         header = avio_rb16(pb);
427   |  if (!header) {
    5←Assuming 'header' is 0→
    6←Taking true branch→
    18←Assuming 'header' is not equal to 0→
428   |  avio_skip(pb, 2046);
429   |  sga->left = 0;
430   |         } else if (!avio_feof(pb) &&
    19←Assuming the condition is true→
431   |                    ((header >> 15) ||
    20←Assuming the condition is false→
432   |                     !sga->sector_headers)) {
    21←buffer read by avio_read may be partially uninitialized
433   |             avio_seek(pb, -2, SEEK_CUR);
434   |             sga->flags = AV_PKT_FLAG_KEY;
435   |             sga->left = 2048;
436   |         } else {
437   |             sga->left = 2046;
438   |         }
439   |  
440   |  av_assert0(sga->idx + sga->left < sizeof(sga->sector));
    7←Taking false branch→
    8←Loop condition is false.  Exiting loop→
441   |  ret = avio_read(pb, sga->sector + sga->idx, sga->left);
442   |  if (ret > 0)
    9←Assuming 'ret' is <= 0→
443   |             sga->idx += ret;
444   |  else if (ret != AVERROR_EOF && ret)
    10←Assuming the condition is false→
445   |  return ret;
446   |  print_stats(s, "after read header");
447   |  
448   |  update_type_size(s);
449   |     }
450   |  
451   |  ret = try_packet(s, pkt);
452   |  if (ret == AVERROR(EAGAIN))
    11←Taking true branch→
453   |  goto retry;
    12←Control jumps to line 413→
454   |  
455   |  return ret;
456   | }
457   |  
458   | static int sga_seek(AVFormatContext *s, int stream_index,
459   |                      int64_t timestamp, int flags)
460   | {
461   |     SGADemuxContext *sga = s->priv_data;
462   |  
463   |     sga->packet_type = sga->payload_size = sga->idx = 0;
464   |     memset(sga->sector, 0, sizeof(sga->sector));
465   |  
466   |  return -1;
467   | }
468   |  
469   | const AVInputFormat ff_sga_demuxer = {
470   |     .name           = "sga",
471   |     .long_name      = NULL_IF_CONFIG_SMALL("Digital Pictures SGA"),
472   |     .priv_data_size = sizeof(SGADemuxContext),
473   |     .read_probe     = sga_probe,
474   |     .read_header    = sga_read_header,
475   |     .read_packet    = sga_read_packet,
476   |     .read_seek      = sga_seek,
477   |     .extensions     = "sga",
478   |     .flags          = AVFMT_GENERIC_INDEX,
479   | };