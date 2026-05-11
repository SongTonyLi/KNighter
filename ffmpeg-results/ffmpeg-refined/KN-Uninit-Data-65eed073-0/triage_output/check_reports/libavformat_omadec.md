### Report Summary

File:| format/omadec.c  
---|---  
Warning:| line 429, column 9  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


356   |  else
357   |             memset(oc->iv, 0, 8);
358   |     }
359   |  
360   |  return ret;
361   | }
362   |  
363   | static int aal_read_packet(AVFormatContext *s, AVPacket *pkt)
364   | {
365   |     int64_t pos = avio_tell(s->pb);
366   |  int ret, pts;
367   |  int packet_size;
368   |  unsigned tag;
369   |  
370   |  if (avio_feof(s->pb))
371   |  return AVERROR_EOF;
372   |  
373   |     tag = avio_rb24(s->pb);
374   |  if (tag == 0)
375   |  return AVERROR_EOF;
376   |  else if (tag != MKBETAG(0,'B','L','K'))
377   |  return AVERROR_INVALIDDATA;
378   |  
379   |     avio_skip(s->pb, 1);
380   |     packet_size = avio_rb16(s->pb);
381   |     avio_skip(s->pb, 2);
382   |     pts = avio_rb32(s->pb);
383   |     avio_skip(s->pb, 12);
384   |     ret = av_get_packet(s->pb, pkt, packet_size);
385   |  if (ret < packet_size)
386   |         pkt->flags |= AV_PKT_FLAG_CORRUPT;
387   |  
388   |  if (ret < 0)
389   |  return ret;
390   |  if (!ret)
391   |  return AVERROR_EOF;
392   |  
393   |     pkt->stream_index = 0;
394   |     pkt->pos = pos;
395   |  if (s->streams[0]->codecpar->codec_id == AV_CODEC_ID_ATRAC3AL) {
396   |         pkt->duration = 1024;
397   |         pkt->pts = pts * 1024LL;
398   |     } else {
399   |         pkt->duration = 2048;
400   |         pkt->pts = pts * 2048LL;
401   |     }
402   |  
403   |  return ret;
404   | }
405   |  
406   | static int oma_read_header(AVFormatContext *s)
407   | {
408   |  int ret, framesize, jsflag, samplerate;
409   |     uint32_t codec_params, channel_id;
410   |     int16_t eid;
411   |     uint8_t buf[EA3_HEADER_SIZE];
412   |     uint8_t *edata;
413   |     AVStream *st;
414   |     ID3v2ExtraMeta *extra_meta;
415   |     OMAContext *oc = s->priv_data;
416   |  
417   |     ff_id3v2_read(s, ID3v2_EA3_MAGIC, &extra_meta, 0);
418   |  if ((ret = ff_id3v2_parse_chapters(s, extra_meta)) < 0) {
    1Assuming the condition is false→
    2←Taking false branch→
419   |         ff_id3v2_free_extra_meta(&extra_meta);
420   |  return ret;
421   |     }
422   |  
423   |  ret = avio_read(s->pb, buf, EA3_HEADER_SIZE);
424   |  if (ret < EA3_HEADER_SIZE) {
    3←Assuming 'ret' is >= EA3_HEADER_SIZE→
425   |         ff_id3v2_free_extra_meta(&extra_meta);
426   |  return -1;
427   |     }
428   |  
429   |  if (memcmp(buf, ((const uint8_t[]){'E', 'A', '3'}), 3) ||
    4←buffer read by avio_read may be partially uninitialized
430   |         buf[4] != 0 || buf[5] != EA3_HEADER_SIZE) {
431   |         ff_id3v2_free_extra_meta(&extra_meta);
432   |         av_log(s, AV_LOG_ERROR, "Couldn't find the EA3 header !\n");
433   |  return AVERROR_INVALIDDATA;
434   |     }
435   |  
436   |     oc->content_start = avio_tell(s->pb);
437   |  
438   |  /* encrypted file */
439   |     eid = AV_RB16(&buf[6]);
440   |  if (eid != -1 && eid != -128 && decrypt_init(s, extra_meta, buf) < 0) {
441   |         ff_id3v2_free_extra_meta(&extra_meta);
442   |  return -1;
443   |     }
444   |  
445   |     ff_id3v2_free_extra_meta(&extra_meta);
446   |  
447   |     codec_params = AV_RB24(&buf[33]);
448   |  
449   |     st = avformat_new_stream(s, NULL);
450   |  if (!st)
451   |  return AVERROR(ENOMEM);
452   |  
453   |     st->start_time = 0;
454   |     st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
455   |     st->codecpar->codec_tag  = buf[32];
456   |     st->codecpar->codec_id   = ff_codec_get_id(ff_oma_codec_tags,
457   |                                                st->codecpar->codec_tag);
458   |  
459   |     oc->read_packet = read_packet;