### Report Summary

File:| format/oggdec.c  
---|---  
Warning:| line 343, column 13  
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
341   |  int c;
342   |  
343   |  if (sync[sp & 3] == 'O' &&
    9←buffer read by avio_read may be partially uninitialized
344   |             sync[(sp + 1) & 3] == 'g' &&
345   |             sync[(sp + 2) & 3] == 'g' && sync[(sp + 3) & 3] == 'S')
346   |  break;
347   |  
348   |  if(!i && (bc->seekable & AVIO_SEEKABLE_NORMAL) && ogg->page_pos > 0) {
349   |             memset(sync, 0, 4);
350   |             avio_seek(bc, ogg->page_pos+4, SEEK_SET);
351   |             ogg->page_pos = -1;
352   |         }
353   |  
354   |         c = avio_r8(bc);
355   |  
356   |  if (avio_feof(bc))
357   |  return AVERROR_EOF;
358   |  
359   |         sync[sp++ & 3] = c;
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