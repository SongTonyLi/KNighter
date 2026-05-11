### Report Summary

File:| format/wavdec.c  
---|---  
Warning:| line 888, column 9  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


818   |             wav->smv_block = smv_timestamp / wav->smv_frames_per_jpeg;
819   |         }
820   |     }
821   |  
822   |  switch (ast->codecpar->codec_id) {
823   |  case AV_CODEC_ID_MP2:
824   |  case AV_CODEC_ID_MP3:
825   |  case AV_CODEC_ID_AC3:
826   |  case AV_CODEC_ID_DTS:
827   |  case AV_CODEC_ID_XMA2:
828   |  /* use generic seeking with dynamically generated indexes */
829   |  return -1;
830   |  default:
831   |  break;
832   |     }
833   |  return ff_pcm_read_seek(s, 0, timestamp, flags);
834   | }
835   |  
836   | static const AVClass wav_demuxer_class = {
837   |     .class_name = "WAV demuxer",
838   |     .item_name  = av_default_item_name,
839   |     .option     = demux_options,
840   |     .version    = LIBAVUTIL_VERSION_INT,
841   | };
842   | const FFInputFormat ff_wav_demuxer = {
843   |     .p.name         = "wav",
844   |     .p.long_name    = NULL_IF_CONFIG_SMALL("WAV / WAVE (Waveform Audio)"),
845   |     .p.flags        = AVFMT_GENERIC_INDEX,
846   |     .p.codec_tag    = ff_wav_codec_tags_list,
847   |     .p.priv_class   = &wav_demuxer_class,
848   |     .priv_data_size = sizeof(WAVDemuxContext),
849   |     .read_probe     = wav_probe,
850   |     .read_header    = wav_read_header,
851   |     .read_packet    = wav_read_packet,
852   |     .read_seek      = wav_read_seek,
853   | };
854   | #endif /* CONFIG_WAV_DEMUXER */
855   |  
856   | #if CONFIG_W64_DEMUXER
857   | static int w64_probe(const AVProbeData *p)
858   | {
859   |  if (p->buf_size <= 40)
860   |  return 0;
861   |  if (!memcmp(p->buf,      ff_w64_guid_riff, 16) &&
862   |         !memcmp(p->buf + 24, ff_w64_guid_wave, 16))
863   |  return AVPROBE_SCORE_MAX;
864   |  else
865   |  return 0;
866   | }
867   |  
868   | static int w64_read_header(AVFormatContext *s)
869   | {
870   |     int64_t size, data_ofs = 0;
871   |     AVIOContext *pb      = s->pb;
872   |     WAVDemuxContext *wav = s->priv_data;
873   |     AVStream *st;
874   |     uint8_t guid[16];
875   |  int ret = ffio_read_size(pb, guid, 16);
876   |  
877   |  if (ret < 0)
    1Assuming 'ret' is >= 0→
    2←Taking false branch→
878   |  return ret;
879   |  
880   |  if (memcmp(guid, ff_w64_guid_riff, 16))
    3←Assuming the condition is false→
    4←Taking false branch→
881   |  return AVERROR_INVALIDDATA;
882   |  
883   |  /* riff + wave + fmt + sizes */
884   |  if (avio_rl64(pb) < 16 + 8 + 16 + 8 + 16 + 8)
    5←Assuming the condition is false→
    6←Taking false branch→
885   |  return AVERROR_INVALIDDATA;
886   |  
887   |  avio_read(pb, guid, 16);
888   |  if (memcmp(guid, ff_w64_guid_wave, 16)) {
    7←buffer read by avio_read may be partially uninitialized
889   |         av_log(s, AV_LOG_ERROR, "could not find wave guid\n");
890   |  return AVERROR_INVALIDDATA;
891   |     }
892   |  
893   |     wav->w64 = 1;
894   |  
895   |     st = avformat_new_stream(s, NULL);
896   |  if (!st)
897   |  return AVERROR(ENOMEM);
898   |  
899   |  while (!avio_feof(pb)) {
900   |  if (avio_read(pb, guid, 16) != 16)
901   |  break;
902   |         size = avio_rl64(pb);
903   |  if (size <= 24 || INT64_MAX - size < avio_tell(pb)) {
904   |  if (data_ofs)
905   |  break;
906   |  return AVERROR_INVALIDDATA;
907   |         }
908   |  
909   |  if (!memcmp(guid, ff_w64_guid_fmt, 16)) {
910   |  /* subtract chunk header size - normal wav file doesn't count it */
911   |             ret = ff_get_wav_header(s, pb, st->codecpar, size - 24, 0);
912   |  if (ret < 0)
913   |  return ret;
914   |             avio_skip(pb, FFALIGN(size, INT64_C(8)) - size);
915   |  if (st->codecpar->block_align &&
916   |                 st->codecpar->ch_layout.nb_channels < FF_SANE_NB_CHANNELS &&
917   |                 st->codecpar->bits_per_coded_sample < 128) {
918   |                 int64_t block_align = st->codecpar->block_align;