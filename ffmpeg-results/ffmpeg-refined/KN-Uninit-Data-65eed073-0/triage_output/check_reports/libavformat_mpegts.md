### Report Summary

File:| format/mpegts.c  
---|---  
Warning:| line 606, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


545   |     sec->opaque      = opaque;
546   |     sec->section_buf = section_buf;
547   |     sec->check_crc   = check_crc;
548   |     sec->last_ver    = -1;
549   |  
550   |  return filter;
551   | }
552   |  
553   | static MpegTSFilter *mpegts_open_pes_filter(MpegTSContext *ts, unsigned int pid,
554   |                                             PESCallback *pes_cb,
555   |  void *opaque)
556   | {
557   |     MpegTSFilter *filter;
558   |     MpegTSPESFilter *pes;
559   |  
560   |  if (!(filter = mpegts_open_filter(ts, pid, MPEGTS_PES)))
561   |  return NULL;
562   |  
563   |     pes = &filter->u.pes_filter;
564   |     pes->pes_cb = pes_cb;
565   |     pes->opaque = opaque;
566   |  return filter;
567   | }
568   |  
569   | static MpegTSFilter *mpegts_open_pcr_filter(MpegTSContext *ts, unsigned int pid)
570   | {
571   |  return mpegts_open_filter(ts, pid, MPEGTS_PCR);
572   | }
573   |  
574   | static void mpegts_close_filter(MpegTSContext *ts, MpegTSFilter *filter)
575   | {
576   |  int pid;
577   |  
578   |     pid = filter->pid;
579   |  if (filter->type == MPEGTS_SECTION)
580   |         av_freep(&filter->u.section_filter.section_buf);
581   |  else if (filter->type == MPEGTS_PES) {
582   |         PESContext *pes = filter->u.pes_filter.opaque;
583   |         av_buffer_unref(&pes->buffer);
584   |  /* referenced private data will be freed later in
585   |  * avformat_close_input (pes->st->priv_data == pes) */
586   |  if (!pes->st || pes->merged_st) {
587   |             av_freep(&filter->u.pes_filter.opaque);
588   |         }
589   |     }
590   |  
591   |     av_free(filter);
592   |     ts->pids[pid] = NULL;
593   | }
594   |  
595   | static int analyze(const uint8_t *buf, int size, int packet_size,
596   |  int probe)
597   | {
598   |  int stat[TS_MAX_PACKET_SIZE];
599   |  int stat_all = 0;
600   |  int i;
601   |  int best_score = 0;
602   |  
603   |     memset(stat, 0, packet_size * sizeof(*stat));
604   |  
605   |  for (i = 0; i < size - 3; i++) {
    26←Assuming the condition is true→
    27←Loop condition is true.  Entering loop body→
606   |  if (buf[i] == SYNC_BYTE) {
    28←buffer read by avio_read may be partially uninitialized
607   |  int pid = AV_RB16(buf+1) & 0x1FFF;
608   |  int asc = buf[i + 3] & 0x30;
609   |  if (!probe || pid == 0x1FFF || asc) {
610   |  int x = i % packet_size;
611   |                 stat[x]++;
612   |                 stat_all++;
613   |  if (stat[x] > best_score) {
614   |                     best_score = stat[x];
615   |                 }
616   |             }
617   |         }
618   |     }
619   |  
620   |  return best_score - FFMAX(stat_all - 10*best_score, 0)/10;
621   | }
622   |  
623   | /* autodetect fec presence */
624   | static int get_packet_size(AVFormatContext* s)
625   | {
626   |  int score, fec_score, dvhs_score;
627   |  int margin;
628   |  int ret;
629   |  
630   |  /*init buffer to store stream for probing */
631   |     uint8_t buf[PROBE_PACKET_MAX_BUF] = {0};
632   |  int buf_size = 0;
633   |  int max_iterations = 16;
634   |  
635   |  while (buf_size21.1'buf_size' is < PROBE_PACKET_MAX_BUF < PROBE_PACKET_MAX_BUF && max_iterations--) {
    22←Loop condition is true.  Entering loop body→
636   |  ret = avio_read_partial(s->pb, buf + buf_size, PROBE_PACKET_MAX_BUF - buf_size);
637   |  if (ret < 0)
    23←Assuming 'ret' is >= 0→
    24←Taking false branch→
638   |  return AVERROR_INVALIDDATA;
639   |  buf_size += ret;
640   |  
641   |  score      = analyze(buf, buf_size, TS_PACKET_SIZE,      0);
    25←Calling 'analyze'→
642   |         dvhs_score = analyze(buf, buf_size, TS_DVHS_PACKET_SIZE, 0);
643   |         fec_score  = analyze(buf, buf_size, TS_FEC_PACKET_SIZE,  0);
644   |         av_log(s, AV_LOG_TRACE, "Probe: %d, score: %d, dvhs_score: %d, fec_score: %d \n",
645   |             buf_size, score, dvhs_score, fec_score);
646   |  
647   |         margin = mid_pred(score, fec_score, dvhs_score);
648   |  
649   |  if (buf_size < PROBE_PACKET_MAX_BUF)
650   |             margin += PROBE_PACKET_MARGIN; /*if buffer not filled */
651   |  
652   |  if (score > margin)
653   |  return TS_PACKET_SIZE;
654   |  else if (dvhs_score > margin)
655   |  return TS_DVHS_PACKET_SIZE;
656   |  else if (fec_score > margin)
657   |  return TS_FEC_PACKET_SIZE;
658   |     }
659   |  return AVERROR_INVALIDDATA;
660   | }
661   |  
662   | typedef struct SectionHeader {
663   |     uint8_t tid;
664   |     uint16_t id;
665   |     uint8_t version;
666   |     uint8_t current_next;
667   |     uint8_t sec_num;
668   |     uint8_t last_sec_num;
669   | } SectionHeader;
670   |  
671   | static int skip_identical(const SectionHeader *h, MpegTSSectionFilter *tssf)
3121  |             len = *p++;
3122  |  if (len > p_end - p)
3123  |  return 0;
3124  |  if (len && cc_ok) {
3125  |  /* write remaining section bytes */
3126  |                 write_section_data(ts, tss,
3127  |                                    p, len, 0);
3128  |  /* check whether filter has been closed */
3129  |  if (!ts->pids[pid])
3130  |  return 0;
3131  |             }
3132  |             p += len;
3133  |  if (p < p_end) {
3134  |                 write_section_data(ts, tss,
3135  |                                    p, p_end - p, 1);
3136  |             }
3137  |         } else {
3138  |  if (cc_ok) {
3139  |                 write_section_data(ts, tss,
3140  |                                    p, p_end - p, 0);
3141  |             }
3142  |         }
3143  |  
3144  |  // stop find_stream_info from waiting for more streams
3145  |  // when all programs have received a PMT
3146  |  if (ts->stream->ctx_flags & AVFMTCTX_NOHEADER && ts->scan_all_pmts <= 0) {
3147  |  int i;
3148  |  for (i = 0; i < ts->nb_prg; i++) {
3149  |  if (!ts->prg[i].pmt_found)
3150  |  break;
3151  |             }
3152  |  if (i == ts->nb_prg && ts->nb_prg > 0) {
3153  |                 av_log(ts->stream, AV_LOG_DEBUG, "All programs have pmt, headers found\n");
3154  |                 ts->stream->ctx_flags &= ~AVFMTCTX_NOHEADER;
3155  |             }
3156  |         }
3157  |  
3158  |     } else {
3159  |  int ret;
3160  |  // Note: The position here points actually behind the current packet.
3161  |  if (tss->type == MPEGTS_PES) {
3162  |  if ((ret = tss->u.pes_filter.pes_cb(tss, p, p_end - p, is_start,
3163  |                                                 pos - ts->raw_packet_size)) < 0)
3164  |  return ret;
3165  |         }
3166  |     }
3167  |  
3168  |  return 0;
3169  | }
3170  |  
3171  | static int mpegts_resync(AVFormatContext *s, int seekback, const uint8_t *current_packet)
3172  | {
3173  |  MpegTSContext *ts = s->priv_data;
3174  |     AVIOContext *pb = s->pb;
3175  |  int c, i;
3176  |     uint64_t pos = avio_tell(pb);
3177  |  int64_t back = FFMIN(seekback, pos);
    10←Assuming 'seekback' is <= 'pos'→
    11←'?' condition is false→
3178  |  
3179  |  //Special case for files like 01c56b0dc1.ts
3180  |  if (current_packet[0] == 0x80 && current_packet[12] == SYNC_BYTE && pos >= TS_PACKET_SIZE) {
    12←Assuming the condition is false→
3181  |         avio_seek(pb, 12 - TS_PACKET_SIZE, SEEK_CUR);
3182  |  return 0;
3183  |     }
3184  |  
3185  |  avio_seek(pb, -back, SEEK_CUR);
3186  |  
3187  |  for (i = 0; i < ts->resync_size; i++) {
    13←Assuming 'i' is < field 'resync_size'→
    14←Loop condition is true.  Entering loop body→
3188  |  c = avio_r8(pb);
3189  |  if (avio_feof(pb))
    15←Assuming the condition is false→
    16←Taking false branch→
3190  |  return AVERROR_EOF;
3191  |  if (c == SYNC_BYTE) {
    17←Assuming 'c' is equal to SYNC_BYTE→
    18←Taking true branch→
3192  |  int new_packet_size, ret;
3193  |             avio_seek(pb, -1, SEEK_CUR);
3194  |             pos = avio_tell(pb);
3195  |             ret = ffio_ensure_seekback(pb, PROBE_PACKET_MAX_BUF);
3196  |  if (ret < 0)
    19←Assuming 'ret' is >= 0→
    20←Taking false branch→
3197  |  return ret;
3198  |  new_packet_size = get_packet_size(s);
    21←Calling 'get_packet_size'→
3199  |  if (new_packet_size > 0 && new_packet_size != ts->raw_packet_size) {
3200  |                 av_log(ts->stream, AV_LOG_WARNING, "changing packet size to %d\n", new_packet_size);
3201  |                 ts->raw_packet_size = new_packet_size;
3202  |             }
3203  |             avio_seek(pb, pos, SEEK_SET);
3204  |  return 0;
3205  |         }
3206  |     }
3207  |     av_log(s, AV_LOG_ERROR,
3208  |  "max resync size reached, could not find sync byte\n");
3209  |  /* no sync found */
3210  |  return AVERROR_INVALIDDATA;
3211  | }
3212  |  
3213  | /* return AVERROR_something if error or EOF. Return 0 if OK. */
3214  | static int read_packet(AVFormatContext *s, uint8_t *buf, int raw_packet_size,
3215  |  const uint8_t **data)
3216  | {
3217  |     AVIOContext *pb = s->pb;
3218  |  int len;
3219  |  
3220  |  // 192 bytes source packet that start with a 4 bytes TP_extra_header
3221  |  // followed by 188 bytes of TS packet. The sync byte is at offset 4, so skip
3222  |  // the first 4 bytes otherwise we'll end up syncing to the wrong packet.
3223  |  if (raw_packet_size == TS_DVHS_PACKET_SIZE)
3224  |         avio_skip(pb, 4);
3225  |  
3226  |  for (;;) {
3227  |         len = ffio_read_indirect(pb, buf, TS_PACKET_SIZE, data);
3228  |  if (len != TS_PACKET_SIZE)
3537  | {
3538  |     MpegTSContext *ts = s->priv_data;
3539  |  int ret, i;
3540  |  
3541  |     pkt->size = -1;
3542  |     ts->pkt = pkt;
3543  |     ret = handle_packets(ts, 0);
3544  |  if (ret < 0) {
3545  |         av_packet_unref(ts->pkt);
3546  |  /* flush pes data left */
3547  |  for (i = 0; i < NB_PID_MAX; i++)
3548  |  if (ts->pids[i] && ts->pids[i]->type == MPEGTS_PES) {
3549  |                 PESContext *pes = ts->pids[i]->u.pes_filter.opaque;
3550  |  if (pes->state == MPEGTS_PAYLOAD && pes->data_index > 0) {
3551  |                     ret = new_pes_packet(pes, pkt);
3552  |  if (ret < 0)
3553  |  return ret;
3554  |                     pes->state = MPEGTS_SKIP;
3555  |                     ret = 0;
3556  |  break;
3557  |                 }
3558  |             }
3559  |     }
3560  |  
3561  |  if (!ret && pkt->size < 0)
3562  |         ret = AVERROR_INVALIDDATA;
3563  |  return ret;
3564  | }
3565  |  
3566  | static void mpegts_free(MpegTSContext *ts)
3567  | {
3568  |  int i;
3569  |  
3570  |     clear_programs(ts);
3571  |  
3572  |  for (i = 0; i < FF_ARRAY_ELEMS(ts->pools); i++)
3573  |         av_buffer_pool_uninit(&ts->pools[i]);
3574  |  
3575  |  for (i = 0; i < NB_PID_MAX; i++)
3576  |  if (ts->pids[i])
3577  |             mpegts_close_filter(ts, ts->pids[i]);
3578  | }
3579  |  
3580  | static int mpegts_read_close(AVFormatContext *s)
3581  | {
3582  |     MpegTSContext *ts = s->priv_data;
3583  |     mpegts_free(ts);
3584  |  return 0;
3585  | }
3586  |  
3587  | av_unused static int64_t mpegts_get_pcr(AVFormatContext *s, int stream_index,
3588  |                               int64_t *ppos, int64_t pos_limit)
3589  | {
3590  |  MpegTSContext *ts = s->priv_data;
3591  |     int64_t pos, timestamp;
3592  |     uint8_t buf[TS_PACKET_SIZE];
3593  |  int pcr_l, pcr_pid =
3594  |         ((PESContext *)s->streams[stream_index]->priv_data)->pcr_pid;
3595  |  int pos47 = ts->pos47_full % ts->raw_packet_size;
3596  |     pos =
3597  |         ((*ppos + ts->raw_packet_size - 1 - pos47) / ts->raw_packet_size) *
3598  |         ts->raw_packet_size + pos47;
3599  |  while(pos < pos_limit) {
    1Assuming 'pos' is < 'pos_limit'→
    2←Loop condition is true.  Entering loop body→
3600  |  if (avio_seek(s->pb, pos, SEEK_SET) < 0)
    3←Assuming the condition is false→
    4←Taking false branch→
3601  |  return AV_NOPTS_VALUE;
3602  |  if (avio_read(s->pb, buf, TS_PACKET_SIZE) != TS_PACKET_SIZE)
    5←Assuming the condition is false→
    6←Taking false branch→
3603  |  return AV_NOPTS_VALUE;
3604  |  if (buf[0] != SYNC_BYTE) {
    7←Assuming the condition is true→
    8←Taking true branch→
3605  |  if (mpegts_resync(s, TS_PACKET_SIZE, buf) < 0)
    9←Calling 'mpegts_resync'→
3606  |  return AV_NOPTS_VALUE;
3607  |             pos = avio_tell(s->pb);
3608  |  continue;
3609  |         }
3610  |  if ((pcr_pid < 0 || (AV_RB16(buf + 1) & 0x1fff) == pcr_pid) &&
3611  |             parse_pcr(×tamp, &pcr_l, buf) == 0) {
3612  |             *ppos = pos;
3613  |  return timestamp;
3614  |         }
3615  |         pos += ts->raw_packet_size;
3616  |     }
3617  |  
3618  |  return AV_NOPTS_VALUE;
3619  | }
3620  |  
3621  | static int64_t mpegts_get_dts(AVFormatContext *s, int stream_index,
3622  |                               int64_t *ppos, int64_t pos_limit)
3623  | {
3624  |     MpegTSContext *ts = s->priv_data;
3625  |     AVPacket *pkt;
3626  |     int64_t pos;
3627  |  int pos47 = ts->pos47_full % ts->raw_packet_size;
3628  |     pos = ((*ppos  + ts->raw_packet_size - 1 - pos47) / ts->raw_packet_size) * ts->raw_packet_size + pos47;
3629  |     ff_read_frame_flush(s);
3630  |  if (avio_seek(s->pb, pos, SEEK_SET) < 0)
3631  |  return AV_NOPTS_VALUE;
3632  |     pkt = av_packet_alloc();
3633  |  if (!pkt)
3634  |  return AV_NOPTS_VALUE;
3635  |  while(pos < pos_limit) {