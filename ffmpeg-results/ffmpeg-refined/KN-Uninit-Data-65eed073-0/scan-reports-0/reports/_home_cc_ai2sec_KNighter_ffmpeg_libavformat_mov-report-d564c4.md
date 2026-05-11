### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/mov.c  
---|---  
Warning:| line 6724, column 16  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


6638  |                 encrypted_index = current_index - frag_stream_info->index_entry;
6639  |                 encryption_index = frag_stream_info->encryption_index;
6640  |             } else {
6641  |                 encryption_index = sc->cenc.encryption_index;
6642  |             }
6643  |         }
6644  |     } else {
6645  |         encryption_index = sc->cenc.encryption_index;
6646  |     }
6647  |  
6648  |  if (encryption_index) {
6649  |  if (encryption_index->auxiliary_info_sample_count &&
6650  |             !encryption_index->nb_encrypted_samples) {
6651  |             av_log(mov->fc, AV_LOG_ERROR, "saiz atom found without saio\n");
6652  |  return AVERROR_INVALIDDATA;
6653  |         }
6654  |  if (encryption_index->auxiliary_offsets_count &&
6655  |             !encryption_index->nb_encrypted_samples) {
6656  |             av_log(mov->fc, AV_LOG_ERROR, "saio atom found without saiz\n");
6657  |  return AVERROR_INVALIDDATA;
6658  |         }
6659  |  
6660  |  if (!encryption_index->nb_encrypted_samples) {
6661  |  // Full-sample encryption with default settings.
6662  |             encrypted_sample = sc->cenc.default_encrypted_sample;
6663  |         } else if (encrypted_index >= 0 && encrypted_index < encryption_index->nb_encrypted_samples) {
6664  |  // Per-sample setting override.
6665  |             encrypted_sample = encryption_index->encrypted_samples[encrypted_index];
6666  |         } else {
6667  |             av_log(mov->fc, AV_LOG_ERROR, "Incorrect number of samples in encryption info\n");
6668  |  return AVERROR_INVALIDDATA;
6669  |         }
6670  |  
6671  |  if (mov->decryption_key) {
6672  |  return cenc_decrypt(mov, sc, encrypted_sample, pkt->data, pkt->size);
6673  |         } else {
6674  |             size_t size;
6675  |             uint8_t *side_data = av_encryption_info_add_side_data(encrypted_sample, &size);
6676  |  if (!side_data)
6677  |  return AVERROR(ENOMEM);
6678  |             ret = av_packet_add_side_data(pkt, AV_PKT_DATA_ENCRYPTION_INFO, side_data, size);
6679  |  if (ret < 0)
6680  |                 av_free(side_data);
6681  |  return ret;
6682  |         }
6683  |     }
6684  |  
6685  |  return 0;
6686  | }
6687  |  
6688  | static int mov_read_dops(MOVContext *c, AVIOContext *pb, MOVAtom atom)
6689  | {
6690  |  const int OPUS_SEEK_PREROLL_MS = 80;
6691  |  int ret;
6692  |     AVStream *st;
6693  |     size_t size;
6694  |     uint16_t pre_skip;
6695  |  
6696  |  if (c->fc->nb_streams < 1)
    1Assuming field 'nb_streams' is >= 1→
    2←Taking false branch→
6697  |  return 0;
6698  |  st = c->fc->streams[c->fc->nb_streams-1];
6699  |  
6700  |  if ((uint64_t)atom.size > (1<<30) || atom.size < 11)
    3←Assuming the condition is false→
    4←Assuming field 'size' is >= 11→
    5←Taking false branch→
6701  |  return AVERROR_INVALIDDATA;
6702  |  
6703  |  /* Check OpusSpecificBox version. */
6704  |  if (avio_r8(pb) != 0) {
    6←Assuming the condition is false→
    7←Taking false branch→
6705  |         av_log(c->fc, AV_LOG_ERROR, "unsupported OpusSpecificBox version\n");
6706  |  return AVERROR_INVALIDDATA;
6707  |     }
6708  |  
6709  |  /* OpusSpecificBox size plus magic for Ogg OpusHead header. */
6710  |  size = atom.size + 8;
6711  |  
6712  |  if ((ret = ff_alloc_extradata(st->codecpar, size)) < 0)
    8←Assuming the condition is false→
    9←Taking false branch→
6713  |  return ret;
6714  |  
6715  |  AV_WL32(st->codecpar->extradata, MKTAG('O','p','u','s'));
6716  |  AV_WL32(st->codecpar->extradata + 4, MKTAG('H','e','a','d'));
6717  |  AV_WB8(st->codecpar->extradata + 8, 1); /* OpusHead version */
    10←Loop condition is false.  Exiting loop→
6718  |  avio_read(pb, st->codecpar->extradata + 9, size - 9);
6719  |  
6720  |  /* OpusSpecificBox is stored in big-endian, but OpusHead is
6721  |  little-endian; aside from the preceeding magic and version they're
6722  |  otherwise currently identical.  Data after output gain at offset 16
6723  |  doesn't need to be bytewapped. */
6724  |  pre_skip = AV_RB16(st->codecpar->extradata + 10);
    11←buffer read by avio_read may be partially uninitialized
6725  |  AV_WL16(st->codecpar->extradata + 10, pre_skip);
6726  |  AV_WL32(st->codecpar->extradata + 12, AV_RB32(st->codecpar->extradata + 12));
6727  |  AV_WL16(st->codecpar->extradata + 16, AV_RB16(st->codecpar->extradata + 16));
6728  |  
6729  |     st->codecpar->initial_padding = pre_skip;
6730  |     st->codecpar->seek_preroll = av_rescale_q(OPUS_SEEK_PREROLL_MS,
6731  |                                               (AVRational){1, 1000},
6732  |                                               (AVRational){1, 48000});
6733  |  
6734  |  return 0;
6735  | }
6736  |  
6737  | static int mov_read_dmlp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
6738  | {
6739  |     AVStream *st;
6740  |  unsigned format_info;
6741  |  int channel_assignment, channel_assignment1, channel_assignment2;
6742  |  int ratebits;
6743  |  
6744  |  if (c->fc->nb_streams < 1)
6745  |  return 0;
6746  |     st = c->fc->streams[c->fc->nb_streams-1];
6747  |  
6748  |  if (atom.size < 10)
6749  |  return AVERROR_INVALIDDATA;
6750  |  
6751  |     format_info = avio_rb32(pb);
6752  |  
6753  |     ratebits            = (format_info >> 28) & 0xF;
6754  |     channel_assignment1 = (format_info >> 15) & 0x1F;