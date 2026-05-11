### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/mxfdec.c  
---|---  
Warning:| line 3618, column 34  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


372   |  case TaggedValue:
373   |         av_freep(&((MXFTaggedValue *)*ctx)->name);
374   |         av_freep(&((MXFTaggedValue *)*ctx)->value);
375   |  break;
376   |  case Track:
377   |         av_freep(&((MXFTrack *)*ctx)->name);
378   |  break;
379   |  case IndexTableSegment:
380   |         seg = (MXFIndexTableSegment *)*ctx;
381   |         av_freep(&seg->temporal_offset_entries);
382   |         av_freep(&seg->flag_entries);
383   |         av_freep(&seg->stream_offset_entries);
384   |  default:
385   |  break;
386   |     }
387   |  if (freectx) {
388   |         av_freep(ctx);
389   |     }
390   | }
391   |  
392   | static int64_t klv_decode_ber_length(AVIOContext *pb)
393   | {
394   |     uint64_t size = avio_r8(pb);
395   |  if (size & 0x80) { /* long form */
396   |  int bytes_num = size & 0x7f;
397   |  /* SMPTE 379M 5.3.4 guarantee that bytes_num must not exceed 8 bytes */
398   |  if (bytes_num > 8)
399   |  return AVERROR_INVALIDDATA;
400   |         size = 0;
401   |  while (bytes_num--)
402   |             size = size << 8 | avio_r8(pb);
403   |     }
404   |  if (size > INT64_MAX)
405   |  return AVERROR_INVALIDDATA;
406   |  return size;
407   | }
408   |  
409   | static int mxf_read_sync(AVIOContext *pb, const uint8_t *key, unsigned size)
410   | {
411   |  int i, b;
412   |  for (i = 0; i < size && !avio_feof(pb); i++) {
413   |         b = avio_r8(pb);
414   |  if (b == key[0])
415   |             i = 0;
416   |  else if (b != key[i])
417   |             i = -1;
418   |     }
419   |  return i == size;
420   | }
421   |  
422   | static int klv_read_packet(KLVPacket *klv, AVIOContext *pb)
423   | {
424   |     int64_t length, pos;
425   |  if (!mxf_read_sync(pb, mxf_klv_key, 4))
426   |  return AVERROR_INVALIDDATA;
427   |     klv->offset = avio_tell(pb) - 4;
428   |     memcpy(klv->key, mxf_klv_key, 4);
429   |     avio_read(pb, klv->key + 4, 12);
430   |     length = klv_decode_ber_length(pb);
431   |  if (length < 0)
432   |  return length;
433   |     klv->length = length;
434   |     pos = avio_tell(pb);
435   |  if (pos > INT64_MAX - length)
436   |  return AVERROR_INVALIDDATA;
437   |     klv->next_klv = pos + length;
438   |  return 0;
439   | }
440   |  
441   | static int mxf_get_stream_index(AVFormatContext *s, KLVPacket *klv, int body_sid)
442   | {
443   |  int i;
444   |  
445   |  for (i = 0; i < s->nb_streams; i++) {
446   |         MXFTrack *track = s->streams[i]->priv_data;
447   |  /* SMPTE 379M 7.3 */
448   |  if (track && (!body_sid || !track->body_sid || track->body_sid == body_sid) && !memcmp(klv->key + sizeof(mxf_essence_element_key), track->track_number, sizeof(track->track_number)))
449   |  return i;
450   |     }
451   |  /* return 0 if only one stream, for OP Atom files with 0 as track number */
452   |  return s->nb_streams == 1 && s->streams[0]->priv_data ? 0 : -1;
453   | }
454   |  
455   | static int find_body_sid_by_absolute_offset(MXFContext *mxf, int64_t offset)
456   | {
457   |  // we look for partition where the offset is placed
458   |  int a, b, m;
459   |     int64_t pack_ofs;
460   |  
461   |     a = -1;
462   |     b = mxf->partitions_count;
463   |  
464   |  while (b - a > 1) {
465   |         m = (a + b) >> 1;
466   |         pack_ofs = mxf->partitions[m].pack_ofs;
467   |  if (pack_ofs <= offset)
468   |             a = m;
3552  | {
3553  |     AVStream *st = mxf->fc->streams[pkt->stream_index];
3554  |     MXFTrack *track = st->priv_data;
3555  |     int64_t bits_per_sample = par->bits_per_coded_sample;
3556  |  
3557  |  if (!bits_per_sample)
3558  |         bits_per_sample = av_get_bits_per_sample(par->codec_id);
3559  |  
3560  |     pkt->pts = track->sample_count;
3561  |  
3562  |  if (   par->channels <= 0
3563  |         || bits_per_sample <= 0
3564  |         || par->channels * (int64_t)bits_per_sample < 8)
3565  |         track->sample_count = mxf_compute_sample_count(mxf, st, av_rescale_q(track->sample_count, st->time_base, av_inv_q(track->edit_rate)) + 1);
3566  |  else
3567  |         track->sample_count += pkt->size / (par->channels * (int64_t)bits_per_sample / 8);
3568  |  
3569  |  return 0;
3570  | }
3571  |  
3572  | static int mxf_set_pts(MXFContext *mxf, AVStream *st, AVPacket *pkt)
3573  | {
3574  |     AVCodecParameters *par = st->codecpar;
3575  |     MXFTrack *track = st->priv_data;
3576  |  
3577  |  if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
3578  |  /* see if we have an index table to derive timestamps from */
3579  |         MXFIndexTable *t = mxf_find_index_table(mxf, track->index_sid);
3580  |  
3581  |  if (t && track->sample_count < t->nb_ptses) {
3582  |             pkt->dts = track->sample_count + t->first_dts;
3583  |             pkt->pts = t->ptses[track->sample_count];
3584  |         } else if (track->intra_only) {
3585  |  /* intra-only -> PTS = EditUnit.
3586  |  * let utils.c figure out DTS since it can be < PTS if low_delay = 0 (Sony IMX30) */
3587  |             pkt->pts = track->sample_count;
3588  |         }
3589  |         track->sample_count++;
3590  |     } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
3591  |  int ret = mxf_set_audio_pts(mxf, par, pkt);
3592  |  if (ret < 0)
3593  |  return ret;
3594  |     } else if (track) {
3595  |         pkt->dts = pkt->pts = track->sample_count;
3596  |         pkt->duration = 1;
3597  |         track->sample_count++;
3598  |     }
3599  |  return 0;
3600  | }
3601  |  
3602  | static int mxf_read_packet(AVFormatContext *s, AVPacket *pkt)
3603  | {
3604  |  KLVPacket klv;
3605  |     MXFContext *mxf = s->priv_data;
3606  |  int ret;
3607  |  
3608  |  while (1) {
    1Loop condition is true.  Entering loop body→
3609  |  int64_t max_data_size;
3610  |         int64_t pos = avio_tell(s->pb);
3611  |  
3612  |  if (pos < mxf->current_klv_data.next_klv - mxf->current_klv_data.length || pos >= mxf->current_klv_data.next_klv) {
    2←Assuming the condition is false→
    3←Assuming 'pos' is >= field 'next_klv'→
    4←Taking true branch→
3613  |  mxf->current_klv_data = (KLVPacket){{0}};
3614  |             ret = klv_read_packet(&klv, s->pb);
3615  |  if (ret4.1'ret' is >= 0 < 0)
    5←Taking false branch→
3616  |  break;
3617  |  max_data_size = klv.length;
3618  |  pos = klv.next_klv - klv.length;
    6←buffer read by avio_read may be partially uninitialized
3619  |  PRINT_KEY(s, "read packet", klv.key);
3620  |             av_log(s, AV_LOG_TRACE, "size %"PRIu64" offset %#"PRIx64"\n", klv.length, klv.offset);
3621  |  if (IS_KLV_KEY(klv.key, mxf_encrypted_triplet_key)) {
3622  |                 ret = mxf_decrypt_triplet(s, pkt, &klv);
3623  |  if (ret < 0) {
3624  |                     av_log(s, AV_LOG_ERROR, "invalid encoded triplet\n");
3625  |  return ret;
3626  |                 }
3627  |  return 0;
3628  |             }
3629  |         } else {
3630  |             klv = mxf->current_klv_data;
3631  |             max_data_size = klv.next_klv - pos;
3632  |         }
3633  |  if (IS_KLV_KEY(klv.key, mxf_essence_element_key) ||
3634  |  IS_KLV_KEY(klv.key, mxf_canopus_essence_element_key) ||
3635  |  IS_KLV_KEY(klv.key, mxf_avid_essence_element_key)) {
3636  |  int body_sid = find_body_sid_by_absolute_offset(mxf, klv.offset);
3637  |  int index = mxf_get_stream_index(s, &klv, body_sid);
3638  |             int64_t next_ofs;
3639  |             AVStream *st;
3640  |             MXFTrack *track;
3641  |  
3642  |  if (index < 0) {
3643  |                 av_log(s, AV_LOG_ERROR,
3644  |  "error getting stream index %"PRIu32"\n",
3645  |  AV_RB32(klv.key + 12));
3646  |  goto skip;
3647  |             }
3648  |  